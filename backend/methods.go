package main

// D-Bus methods of org.yappari.WhatsApp (see main.go for the API).

import (
	"context"
	"fmt"
	"io"
	"mime"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"

	"github.com/godbus/dbus/v5"
	"go.mau.fi/whatsmeow"
	"go.mau.fi/whatsmeow/proto/waE2E"
	"go.mau.fi/whatsmeow/types"
	"google.golang.org/protobuf/proto"
)

var errNotLinked = dbus.MakeFailedError(fmt.Errorf("not linked to a WhatsApp account"))

// linked returns an error for methods that need a linked device.
func (s *Service) linked() *dbus.Error {
	if s.cli.Store.ID == nil {
		return errNotLinked
	}
	return nil
}

func parseJID(jid string) (types.JID, *dbus.Error) {
	j, err := types.ParseJID(jid)
	if err != nil {
		return j, dbus.MakeFailedError(err)
	}
	return j, nil
}

func (s *Service) Status() (string, string, *dbus.Error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	jid := ""
	if s.cli.Store.ID != nil {
		jid = s.cli.Store.ID.ToNonAD().String()
	}
	return s.state, jid, nil
}

// --- linking ------------------------------------------------------------------------

// pairSession is one run of the login websocket without a session: it
// produces QR codes until the device is linked or the codes run out.
type pairSession struct {
	ready chan struct{} // closed with the first code or the end
	code  string
	err   error
}

// startPairing opens the login connection (or reuses the open one) and
// waits for its first QR code, which also means it is ready for PairPhone.
func (s *Service) startPairing() (string, error) {
	if s.cli.Store.ID != nil {
		return "", fmt.Errorf("already linked")
	}
	s.pairMu.Lock()
	p := s.pairing
	if p == nil {
		if s.cli.IsConnected() {
			s.cli.Disconnect()
		}
		qr, err := s.cli.GetQRChannel(context.Background())
		if err != nil {
			s.pairMu.Unlock()
			return "", err
		}
		if err := s.cli.Connect(); err != nil {
			s.pairMu.Unlock()
			return "", err
		}
		p = &pairSession{ready: make(chan struct{})}
		s.pairing = p
		go s.runPairing(p, qr)
	}
	s.pairMu.Unlock()

	select {
	case <-p.ready:
	case <-time.After(30 * time.Second):
		return "", fmt.Errorf("no answer from WhatsApp")
	}
	s.pairMu.Lock()
	defer s.pairMu.Unlock()
	return p.code, p.err
}

func (s *Service) runPairing(p *pairSession, qr <-chan whatsmeow.QRChannelItem) {
	first := true
	signalReady := func() {
		if first {
			first = false
			close(p.ready)
		}
	}
	for item := range qr {
		s.pairMu.Lock()
		switch item.Event {
		case "code":
			p.code = item.Code
		case "success":
			p.code = ""
		default:
			p.code = ""
			p.err = fmt.Errorf("linking: %s", item.Event)
			if item.Error != nil {
				p.err = fmt.Errorf("linking: %v", item.Error)
			}
		}
		code := p.code
		s.pairMu.Unlock()
		if item.Event != "success" {
			s.conn.Emit(objPath, iface+".QRCode", code)
		}
		signalReady()
	}
	signalReady()
	s.pairMu.Lock()
	if s.pairing == p {
		s.pairing = nil
	}
	s.pairMu.Unlock()
	if s.cli.Store.ID == nil {
		s.setState("logged-out")
	}
}

func (s *Service) GetQR() (string, *dbus.Error) {
	code, err := s.startPairing()
	return code, dbusErr(err)
}

func (s *Service) PairPhone(phone string) (string, *dbus.Error) {
	if _, err := s.startPairing(); err != nil {
		return "", dbusErr(err)
	}
	phone = strings.TrimLeft(strings.ReplaceAll(phone, " ", ""), "+")
	code, err := s.cli.PairPhone(context.Background(), phone, true, whatsmeow.PairClientChrome, "Chrome (Linux)")
	return code, dbusErr(err)
}

func (s *Service) Logout() *dbus.Error {
	err := s.cli.Logout(context.Background())
	if err != nil && s.cli.Store.ID != nil {
		return dbusErr(err)
	}
	s.setState("logged-out")
	s.restart()
	return nil
}

// --- sending ------------------------------------------------------------------------

// sendAsync sends msg in the background and reports the outcome as a Receipt.
func (s *Service) sendAsync(to types.JID, id string, build func() (*waE2E.Message, error)) {
	go func() {
		msg, err := build()
		if err == nil {
			var r whatsmeow.SendResponse
			r, err = s.cli.SendMessage(context.Background(), to, msg, whatsmeow.SendRequestExtra{ID: types.MessageID(id)})
			if err == nil {
				s.emit("Receipt", to.String(), "", []string{id}, "server", r.Timestamp.Unix())
				return
			}
		}
		fmt.Fprintln(os.Stderr, "yappari-wa: send", id, "failed:", err)
		s.emit("Receipt", to.String(), "", []string{id}, "error", time.Now().Unix())
	}()
}

func (s *Service) newID(id string) string {
	if id == "" {
		return string(s.cli.GenerateMessageID())
	}
	return id
}

func (s *Service) SendText(id, jid, text string) (string, *dbus.Error) {
	if err := s.linked(); err != nil {
		return "", err
	}
	to, derr := parseJID(jid)
	if derr != nil {
		return "", derr
	}
	id = s.newID(id)
	s.sendAsync(to, id, func() (*waE2E.Message, error) {
		return &waE2E.Message{Conversation: proto.String(text)}, nil
	})
	return id, nil
}

func (s *Service) SendFile(id, jid, path, caption string) (string, *dbus.Error) {
	if err := s.linked(); err != nil {
		return "", err
	}
	to, derr := parseJID(jid)
	if derr != nil {
		return "", derr
	}
	if _, err := os.Stat(path); err != nil {
		return "", dbusErr(err)
	}
	id = s.newID(id)
	s.sendAsync(to, id, func() (*waE2E.Message, error) { return s.buildFile(path, caption) })
	return id, nil
}

func (s *Service) buildFile(path, caption string) (*waE2E.Message, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	mt := mime.TypeByExtension(strings.ToLower(filepath.Ext(path)))
	if mt == "" {
		mt = http.DetectContentType(data)
	}
	kind := whatsmeow.MediaDocument
	switch {
	case strings.HasPrefix(mt, "image/"):
		kind = whatsmeow.MediaImage
	case strings.HasPrefix(mt, "video/"):
		kind = whatsmeow.MediaVideo
	case strings.HasPrefix(mt, "audio/"):
		kind = whatsmeow.MediaAudio
	}
	up, err := s.cli.Upload(context.Background(), data, kind)
	if err != nil {
		return nil, err
	}
	size := proto.Uint64(uint64(len(data)))
	msg := &waE2E.Message{}
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
	return msg, nil
}

func (s *Service) MarkRead(chat, sender string, ids []string) *dbus.Error {
	if err := s.linked(); err != nil {
		return err
	}
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
	if err := s.linked(); err != nil {
		return err
	}
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
	if err := s.linked(); err != nil {
		return err
	}
	p := types.PresenceUnavailable
	if available {
		p = types.PresenceAvailable
	}
	return dbusErr(s.cli.SendPresence(context.Background(), p))
}

func (s *Service) SubscribePresence(jid string) *dbus.Error {
	if err := s.linked(); err != nil {
		return err
	}
	j, derr := parseJID(jid)
	if derr != nil {
		return derr
	}
	return dbusErr(s.cli.SubscribePresence(context.Background(), j))
}

// --- contacts, groups, pictures, media ---------------------------------------------

type contactEntry struct {
	JID, FullName, PushName string
}

type groupEntry struct {
	JID, Subject string
}

func (s *Service) GetContacts() ([]contactEntry, *dbus.Error) {
	if s.linked() != nil {
		return []contactEntry{}, nil
	}
	all, err := s.cli.Store.Contacts.GetAllContacts(context.Background())
	if err != nil {
		return nil, dbusErr(err)
	}
	out := make([]contactEntry, 0, len(all))
	for jid, c := range all {
		if jid.Server != types.DefaultUserServer {
			continue
		}
		name := c.FullName
		if name == "" {
			name = c.FirstName
		}
		if name == "" {
			name = c.BusinessName
		}
		out = append(out, contactEntry{jid.String(), name, c.PushName})
	}
	return out, nil
}

func (s *Service) GetGroups() ([]groupEntry, *dbus.Error) {
	if err := s.linked(); err != nil {
		return nil, err
	}
	groups, err := s.cli.GetJoinedGroups(context.Background())
	if err != nil {
		return nil, dbusErr(err)
	}
	out := make([]groupEntry, 0, len(groups))
	for _, g := range groups {
		out = append(out, groupEntry{g.JID.String(), g.Name})
	}
	return out, nil
}

var httpClient = &http.Client{Timeout: 60 * time.Second}

func (s *Service) GetProfilePicture(jid string) (string, *dbus.Error) {
	if err := s.linked(); err != nil {
		return "", err
	}
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
	resp, err := httpClient.Get(info.URL)
	if err != nil {
		return "", dbusErr(err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return "", dbusErr(fmt.Errorf("profile picture: HTTP %d", resp.StatusCode))
	}
	data, err := io.ReadAll(resp.Body)
	if err != nil {
		return "", dbusErr(err)
	}
	os.MkdirAll(filepath.Dir(path), 0700)
	return path, dbusErr(os.WriteFile(path, data, 0600))
}

func (s *Service) mediaPath(id string) string {
	return filepath.Join(s.cacheDir, "messages", id+".pb")
}

func (s *Service) loadMedia(id string) (*waE2E.Message, error) {
	raw, err := os.ReadFile(s.mediaPath(filepath.Base(id)))
	if err != nil {
		return nil, fmt.Errorf("no media message %s", id)
	}
	var msg waE2E.Message
	if err := proto.Unmarshal(raw, &msg); err != nil {
		return nil, err
	}
	return &msg, nil
}

func (s *Service) GetThumbnail(id string) ([]byte, *dbus.Error) {
	msg, err := s.loadMedia(id)
	if err != nil {
		return nil, dbusErr(err)
	}
	switch {
	case msg.GetImageMessage() != nil:
		return msg.GetImageMessage().GetJPEGThumbnail(), nil
	case msg.GetVideoMessage() != nil:
		return msg.GetVideoMessage().GetJPEGThumbnail(), nil
	case msg.GetDocumentMessage() != nil:
		return msg.GetDocumentMessage().GetJPEGThumbnail(), nil
	}
	return []byte{}, nil
}

func (s *Service) DownloadMedia(id string) (string, *dbus.Error) {
	if err := s.linked(); err != nil {
		return "", err
	}
	msg, err := s.loadMedia(id)
	if err != nil {
		return "", dbusErr(err)
	}
	_, _, mt, _, name := describe(msg)
	ext := ""
	if exts, _ := mime.ExtensionsByType(strings.Split(mt, ";")[0]); len(exts) > 0 {
		ext = exts[0]
	}
	if name == "" {
		name = filepath.Base(id) + ext
	} else {
		name = filepath.Base(id) + "-" + filepath.Base(name)
	}
	path := filepath.Join(s.cacheDir, "media", name)
	if _, err := os.Stat(path); err == nil {
		return path, nil
	}
	data, err := s.cli.DownloadAny(context.Background(), msg)
	if err != nil {
		return "", dbusErr(err)
	}
	os.MkdirAll(filepath.Dir(path), 0700)
	return path, dbusErr(os.WriteFile(path, data, 0600))
}

func (s *Service) ReplayQueued() (uint32, *dbus.Error) {
	items, err := s.queue.TakeAll()
	if err != nil {
		return 0, dbusErr(err)
	}
	for _, it := range items {
		s.emit(it.Signal, it.Args...)
	}
	return uint32(len(items)), nil
}
