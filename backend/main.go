// yappari-wa: the WhatsApp connection of Yappari for Maemo Leste.
//
// Yappari spoke WhatsApp's old FunXMPP protocol, which no longer works.
// This daemon speaks the current multi-device protocol through whatsmeow
// and offers it to the Yappari UI on the session bus, shaped after the
// slots and signals of Yappari's former Connection class:
//
//	org.yappari.WhatsApp  /org/yappari/WhatsApp  org.yappari.WhatsApp
//	  Status() -> (s state, s jid)       state: logged-out, connecting,
//	                                     connected, disconnected
//	  PairPhone(s phone) -> (s code)     "Link with phone number" code
//	  GetQR() -> (s code)                QR code text to link from a second screen
//	  Logout()
//	  SendText(s jid, s text) -> (s id, x time)
//	  SendFile(s jid, s path, s caption) -> (s id, x time)
//	  MarkRead(s chat, s sender, as ids)
//	  SendChatState(s jid, b composing)
//	  SetAvailable(b available)
//	  SubscribePresence(s jid)
//	  GetContacts() -> a(sss)            jid, full name, push name
//	  GetGroups() -> a(ss)               jid, subject
//	  GetProfilePicture(s jid) -> (s path)
//	  DownloadMedia(s id) -> (s path)
//	signals:
//	  StateChanged(s state)
//	  Message(s id, s chat, s sender, s pushname, x time, b fromme,
//	          s type, s text, s mime, x size)
//	  Receipt(s chat, s sender, as ids, s type, x time)
//	  ChatState(s chat, s sender, b composing)
//	  Presence(s jid, b available, x lastseen)
//
// Data: $XDG_DATA_HOME/yappari-wa (session), $XDG_CACHE_HOME/yappari-wa
// (media and profile pictures).
package main

import (
	"context"
	"fmt"
	"mime"
	"net/http"
	"os"
	"os/signal"
	"path/filepath"
	"strings"
	"sync"
	"syscall"
	"time"

	"github.com/godbus/dbus/v5"
	"github.com/godbus/dbus/v5/introspect"
	"go.mau.fi/whatsmeow"
	"go.mau.fi/whatsmeow/proto/waE2E"
	"go.mau.fi/whatsmeow/store/sqlstore"
	"go.mau.fi/whatsmeow/types"
	"go.mau.fi/whatsmeow/types/events"
	waLog "go.mau.fi/whatsmeow/util/log"
	"google.golang.org/protobuf/proto"
	_ "modernc.org/sqlite"
)

const (
	busName = "org.yappari.WhatsApp"
	objPath = dbus.ObjectPath("/org/yappari/WhatsApp")
	iface   = "org.yappari.WhatsApp"
)

type Service struct {
	cli      *whatsmeow.Client
	conn     *dbus.Conn
	cacheDir string
	mu       sync.Mutex
	state    string
}

func dataDir(env, fallback string) string {
	if d := os.Getenv(env); d != "" {
		return filepath.Join(d, "yappari-wa")
	}
	home, _ := os.UserHomeDir()
	return filepath.Join(home, fallback, "yappari-wa")
}

func dbusErr(err error) *dbus.Error {
	if err == nil {
		return nil
	}
	return dbus.MakeFailedError(err)
}

func (s *Service) setState(state string) {
	s.mu.Lock()
	changed := s.state != state
	s.state = state
	s.mu.Unlock()
	if changed {
		s.conn.Emit(objPath, iface+".StateChanged", state)
	}
}

func parseJID(jid string) (types.JID, *dbus.Error) {
	j, err := types.ParseJID(jid)
	if err != nil {
		return j, dbus.MakeFailedError(err)
	}
	return j, nil
}

// --- methods -----------------------------------------------------------------------

func (s *Service) Status() (string, string, *dbus.Error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	jid := ""
	if s.cli.Store.ID != nil {
		jid = s.cli.Store.ID.ToNonAD().String()
	}
	return s.state, jid, nil
}

func (s *Service) PairPhone(phone string) (string, *dbus.Error) {
	if s.cli.Store.ID != nil {
		return "", dbus.MakeFailedError(fmt.Errorf("already logged in"))
	}
	if !s.cli.IsConnected() {
		if err := s.cli.Connect(); err != nil {
			return "", dbusErr(err)
		}
	}
	phone = strings.TrimLeft(strings.ReplaceAll(phone, " ", ""), "+")
	code, err := s.cli.PairPhone(context.Background(), phone, true, whatsmeow.PairClientChrome, "Chrome (Linux)")
	return code, dbusErr(err)
}

func (s *Service) GetQR() (string, *dbus.Error) {
	if s.cli.Store.ID != nil {
		return "", dbus.MakeFailedError(fmt.Errorf("already logged in"))
	}
	if s.cli.IsConnected() {
		s.cli.Disconnect()
	}
	qr, err := s.cli.GetQRChannel(context.Background())
	if err != nil {
		return "", dbusErr(err)
	}
	if err := s.cli.Connect(); err != nil {
		return "", dbusErr(err)
	}
	for item := range qr {
		if item.Event == "code" {
			// the channel keeps sending fresh codes; drain it in the background
			go func() {
				for range qr {
				}
			}()
			return item.Code, nil
		}
		return "", dbus.MakeFailedError(fmt.Errorf("pairing: %s", item.Event))
	}
	return "", dbus.MakeFailedError(fmt.Errorf("no QR code"))
}

func (s *Service) Logout() *dbus.Error {
	return dbusErr(s.cli.Logout(context.Background()))
}

func (s *Service) SendText(jid, text string) (string, int64, *dbus.Error) {
	to, derr := parseJID(jid)
	if derr != nil {
		return "", 0, derr
	}
	r, err := s.cli.SendMessage(context.Background(), to, &waE2E.Message{Conversation: proto.String(text)})
	if err != nil {
		return "", 0, dbusErr(err)
	}
	return r.ID, r.Timestamp.Unix(), nil
}

func (s *Service) SendFile(jid, path, caption string) (string, int64, *dbus.Error) {
	to, derr := parseJID(jid)
	if derr != nil {
		return "", 0, derr
	}
	data, err := os.ReadFile(path)
	if err != nil {
		return "", 0, dbusErr(err)
	}
	mt := mime.TypeByExtension(strings.ToLower(filepath.Ext(path)))
	if mt == "" {
		mt = http.DetectContentType(data)
	}
	ctx := context.Background()
	msg := &waE2E.Message{}
	kind := whatsmeow.MediaDocument
	switch {
	case strings.HasPrefix(mt, "image/"):
		kind = whatsmeow.MediaImage
	case strings.HasPrefix(mt, "video/"):
		kind = whatsmeow.MediaVideo
	case strings.HasPrefix(mt, "audio/"):
		kind = whatsmeow.MediaAudio
	}
	up, err := s.cli.Upload(ctx, data, kind)
	if err != nil {
		return "", 0, dbusErr(err)
	}
	size := proto.Uint64(uint64(len(data)))
	switch kind {
	case whatsmeow.MediaImage:
		msg.ImageMessage = &waE2E.ImageMessage{Caption: proto.String(caption), Mimetype: proto.String(mt),
			URL: &up.URL, DirectPath: &up.DirectPath, MediaKey: up.MediaKey, FileEncSHA256: up.FileEncSHA256,
			FileSHA256: up.FileSHA256, FileLength: size}
	case whatsmeow.MediaVideo:
		msg.VideoMessage = &waE2E.VideoMessage{Caption: proto.String(caption), Mimetype: proto.String(mt),
			URL: &up.URL, DirectPath: &up.DirectPath, MediaKey: up.MediaKey, FileEncSHA256: up.FileEncSHA256,
			FileSHA256: up.FileSHA256, FileLength: size}
	case whatsmeow.MediaAudio:
		msg.AudioMessage = &waE2E.AudioMessage{Mimetype: proto.String(mt),
			URL: &up.URL, DirectPath: &up.DirectPath, MediaKey: up.MediaKey, FileEncSHA256: up.FileEncSHA256,
			FileSHA256: up.FileSHA256, FileLength: size}
	default:
		msg.DocumentMessage = &waE2E.DocumentMessage{Caption: proto.String(caption), Mimetype: proto.String(mt),
			FileName: proto.String(filepath.Base(path)),
			URL:      &up.URL, DirectPath: &up.DirectPath, MediaKey: up.MediaKey, FileEncSHA256: up.FileEncSHA256,
			FileSHA256: up.FileSHA256, FileLength: size}
	}
	r, err := s.cli.SendMessage(ctx, to, msg)
	if err != nil {
		return "", 0, dbusErr(err)
	}
	return r.ID, r.Timestamp.Unix(), nil
}

func (s *Service) MarkRead(chat, sender string, ids []string) *dbus.Error {
	c, derr := parseJID(chat)
	if derr != nil {
		return derr
	}
	var from types.JID
	if sender != "" {
		if from, derr = parseJID(sender); derr != nil {
			return derr
		}
	}
	msgIDs := make([]types.MessageID, len(ids))
	for i, id := range ids {
		msgIDs[i] = types.MessageID(id)
	}
	return dbusErr(s.cli.MarkRead(context.Background(), msgIDs, time.Now(), c, from))
}

func (s *Service) SendChatState(jid string, composing bool) *dbus.Error {
	to, derr := parseJID(jid)
	if derr != nil {
		return derr
	}
	state := types.ChatPresencePaused
	if composing {
		state = types.ChatPresenceComposing
	}
	return dbusErr(s.cli.SendChatPresence(context.Background(), to, state, types.ChatPresenceMediaText))
}

func (s *Service) SetAvailable(available bool) *dbus.Error {
	p := types.PresenceUnavailable
	if available {
		p = types.PresenceAvailable
	}
	return dbusErr(s.cli.SendPresence(context.Background(), p))
}

func (s *Service) SubscribePresence(jid string) *dbus.Error {
	j, derr := parseJID(jid)
	if derr != nil {
		return derr
	}
	return dbusErr(s.cli.SubscribePresence(context.Background(), j))
}

func (s *Service) GetContacts() ([][]string, *dbus.Error) {
	all, err := s.cli.Store.Contacts.GetAllContacts(context.Background())
	if err != nil {
		return nil, dbusErr(err)
	}
	out := make([][]string, 0, len(all))
	for jid, c := range all {
		out = append(out, []string{jid.String(), c.FullName, c.PushName})
	}
	return out, nil
}

func (s *Service) GetGroups() ([][]string, *dbus.Error) {
	groups, err := s.cli.GetJoinedGroups(context.Background())
	if err != nil {
		return nil, dbusErr(err)
	}
	out := make([][]string, 0, len(groups))
	for _, g := range groups {
		out = append(out, []string{g.JID.String(), g.Name})
	}
	return out, nil
}

func (s *Service) GetProfilePicture(jid string) (string, *dbus.Error) {
	j, derr := parseJID(jid)
	if derr != nil {
		return "", derr
	}
	info, err := s.cli.GetProfilePictureInfo(context.Background(), j, &whatsmeow.GetProfilePictureParams{Preview: true})
	if err != nil || info == nil {
		return "", dbusErr(err)
	}
	path := filepath.Join(s.cacheDir, "pictures", j.User+"-"+info.ID+".jpg")
	if _, err := os.Stat(path); err == nil {
		return path, nil
	}
	resp, err := http.Get(info.URL)
	if err != nil {
		return "", dbusErr(err)
	}
	defer resp.Body.Close()
	data := make([]byte, 0, 32768)
	buf := make([]byte, 8192)
	for {
		n, rerr := resp.Body.Read(buf)
		data = append(data, buf[:n]...)
		if rerr != nil {
			break
		}
	}
	os.MkdirAll(filepath.Dir(path), 0700)
	return path, dbusErr(os.WriteFile(path, data, 0600))
}

func (s *Service) mediaPath(id string) string {
	return filepath.Join(s.cacheDir, "messages", id+".pb")
}

func (s *Service) DownloadMedia(id string) (string, *dbus.Error) {
	raw, err := os.ReadFile(s.mediaPath(id))
	if err != nil {
		return "", dbusErr(fmt.Errorf("no media message %s", id))
	}
	var msg waE2E.Message
	if err := proto.Unmarshal(raw, &msg); err != nil {
		return "", dbusErr(err)
	}
	_, _, mt, _, name := describe(&msg)
	ext := ""
	if exts, _ := mime.ExtensionsByType(mt); len(exts) > 0 {
		ext = exts[0]
	}
	if name == "" {
		name = id + ext
	}
	path := filepath.Join(s.cacheDir, "media", name)
	if _, err := os.Stat(path); err == nil {
		return path, nil
	}
	data, err := s.cli.DownloadAny(context.Background(), &msg)
	if err != nil {
		return "", dbusErr(err)
	}
	os.MkdirAll(filepath.Dir(path), 0700)
	return path, dbusErr(os.WriteFile(path, data, 0600))
}

// --- events -------------------------------------------------------------------------

// describe gives a message's type, text, media type and file name.
func describe(m *waE2E.Message) (kind, text, mimeType string, size int64, name string) {
	switch {
	case m.GetConversation() != "":
		return "text", m.GetConversation(), "", 0, ""
	case m.GetExtendedTextMessage() != nil:
		return "text", m.GetExtendedTextMessage().GetText(), "", 0, ""
	case m.GetImageMessage() != nil:
		i := m.GetImageMessage()
		return "image", i.GetCaption(), i.GetMimetype(), int64(i.GetFileLength()), ""
	case m.GetVideoMessage() != nil:
		v := m.GetVideoMessage()
		return "video", v.GetCaption(), v.GetMimetype(), int64(v.GetFileLength()), ""
	case m.GetAudioMessage() != nil:
		a := m.GetAudioMessage()
		kind := "audio"
		if a.GetPTT() {
			kind = "voice"
		}
		return kind, "", a.GetMimetype(), int64(a.GetFileLength()), ""
	case m.GetDocumentMessage() != nil:
		d := m.GetDocumentMessage()
		return "document", d.GetCaption(), d.GetMimetype(), int64(d.GetFileLength()), d.GetFileName()
	case m.GetStickerMessage() != nil:
		st := m.GetStickerMessage()
		return "sticker", "", st.GetMimetype(), int64(st.GetFileLength()), ""
	case m.GetLocationMessage() != nil:
		l := m.GetLocationMessage()
		return "location", fmt.Sprintf("geo:%f,%f", l.GetDegreesLatitude(), l.GetDegreesLongitude()), "", 0, ""
	case m.GetContactMessage() != nil:
		return "contact", m.GetContactMessage().GetVcard(), "text/vcard", 0, ""
	}
	return "", "", "", 0, ""
}

func (s *Service) handle(evt interface{}) {
	switch e := evt.(type) {
	case *events.Connected:
		s.setState("connected")
	case *events.Disconnected:
		s.setState("disconnected")
	case *events.LoggedOut:
		s.setState("logged-out")
	case *events.PairSuccess:
		s.setState("connecting")
	case *events.Message:
		msg := e.Message
		if msg == nil {
			return
		}
		kind, text, mt, size, _ := describe(msg)
		if kind == "" {
			return
		}
		if mt != "" {
			if raw, err := proto.Marshal(msg); err == nil {
				os.MkdirAll(filepath.Join(s.cacheDir, "messages"), 0700)
				os.WriteFile(s.mediaPath(e.Info.ID), raw, 0600)
			}
		}
		s.conn.Emit(objPath, iface+".Message", e.Info.ID, e.Info.Chat.String(), e.Info.Sender.ToNonAD().String(),
			e.Info.PushName, e.Info.Timestamp.Unix(), e.Info.IsFromMe, kind, text, mt, size)
	case *events.Receipt:
		ids := make([]string, len(e.MessageIDs))
		for i, id := range e.MessageIDs {
			ids[i] = string(id)
		}
		kind := string(e.Type)
		if kind == "" {
			kind = "delivered"
		}
		s.conn.Emit(objPath, iface+".Receipt", e.Chat.String(), e.Sender.ToNonAD().String(), ids, kind, e.Timestamp.Unix())
	case *events.ChatPresence:
		s.conn.Emit(objPath, iface+".ChatState", e.Chat.String(), e.Sender.ToNonAD().String(),
			e.State == types.ChatPresenceComposing)
	case *events.Presence:
		s.conn.Emit(objPath, iface+".Presence", e.From.ToNonAD().String(), !e.Unavailable, e.LastSeen.Unix())
	}
}

const introspection = `
<node>
 <interface name="` + iface + `">
  <method name="Status"><arg type="s" direction="out"/><arg type="s" direction="out"/></method>
  <method name="PairPhone"><arg type="s" direction="in"/><arg type="s" direction="out"/></method>
  <method name="GetQR"><arg type="s" direction="out"/></method>
  <method name="Logout"/>
  <method name="SendText"><arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="s" direction="out"/><arg type="x" direction="out"/></method>
  <method name="SendFile"><arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="s" direction="out"/><arg type="x" direction="out"/></method>
  <method name="MarkRead"><arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="as" direction="in"/></method>
  <method name="SendChatState"><arg type="s" direction="in"/><arg type="b" direction="in"/></method>
  <method name="SetAvailable"><arg type="b" direction="in"/></method>
  <method name="SubscribePresence"><arg type="s" direction="in"/></method>
  <method name="GetContacts"><arg type="a(sss)" direction="out"/></method>
  <method name="GetGroups"><arg type="a(ss)" direction="out"/></method>
  <method name="GetProfilePicture"><arg type="s" direction="in"/><arg type="s" direction="out"/></method>
  <method name="DownloadMedia"><arg type="s" direction="in"/><arg type="s" direction="out"/></method>
  <signal name="StateChanged"><arg type="s"/></signal>
  <signal name="Message"><arg type="s"/><arg type="s"/><arg type="s"/><arg type="s"/><arg type="x"/><arg type="b"/><arg type="s"/><arg type="s"/><arg type="s"/><arg type="x"/></signal>
  <signal name="Receipt"><arg type="s"/><arg type="s"/><arg type="as"/><arg type="s"/><arg type="x"/></signal>
  <signal name="ChatState"><arg type="s"/><arg type="s"/><arg type="b"/></signal>
  <signal name="Presence"><arg type="s"/><arg type="b"/><arg type="x"/></signal>
 </interface>` + introspect.IntrospectDataString + `</node>`

func main() {
	ctx := context.Background()
	stateDir := dataDir("XDG_DATA_HOME", ".local/share")
	os.MkdirAll(stateDir, 0700)
	log := waLog.Stdout("yappari-wa", "WARN", false)

	container, err := sqlstore.New(ctx, "sqlite",
		"file:"+filepath.Join(stateDir, "session.db")+"?_pragma=foreign_keys(1)&_pragma=busy_timeout(5000)", log)
	if err != nil {
		fmt.Fprintln(os.Stderr, "yappari-wa: store:", err)
		os.Exit(1)
	}
	device, err := container.GetFirstDevice(ctx)
	if err != nil {
		fmt.Fprintln(os.Stderr, "yappari-wa: device:", err)
		os.Exit(1)
	}

	conn, err := dbus.ConnectSessionBus()
	if err != nil {
		fmt.Fprintln(os.Stderr, "yappari-wa: session bus:", err)
		os.Exit(1)
	}
	reply, err := conn.RequestName(busName, dbus.NameFlagDoNotQueue)
	if err != nil || reply != dbus.RequestNameReplyPrimaryOwner {
		fmt.Fprintln(os.Stderr, "yappari-wa: already running")
		os.Exit(0)
	}

	s := &Service{cli: whatsmeow.NewClient(device, log), conn: conn,
		cacheDir: dataDir("XDG_CACHE_HOME", ".cache"), state: "logged-out"}
	s.cli.AddEventHandler(s.handle)
	conn.Export(s, objPath, iface)
	conn.Export(introspect.Introspectable(introspection), objPath, "org.freedesktop.DBus.Introspectable")

	// With a session, connect right away; without one, PairPhone connects.
	if s.cli.Store.ID != nil {
		s.setState("connecting")
		if err := s.cli.Connect(); err != nil {
			fmt.Fprintln(os.Stderr, "yappari-wa: connect:", err)
			s.setState("disconnected")
		}
	}

	sig := make(chan os.Signal, 1)
	signal.Notify(sig, syscall.SIGINT, syscall.SIGTERM)
	<-sig
	s.cli.Disconnect()
}
