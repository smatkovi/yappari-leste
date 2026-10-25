// yappari-wa: the WhatsApp connection of Yappari for Maemo Leste.
//
// Yappari spoke WhatsApp's old FunXMPP protocol, which no longer works.
// This daemon speaks the current multi-device protocol through whatsmeow
// and offers it to the Yappari UI on the session bus, shaped after the
// slots and signals of Yappari's former Connection class. It is started
// by D-Bus activation and keeps running when the UI is closed.
//
//	org.yappari.WhatsApp  /org/yappari/WhatsApp  org.yappari.WhatsApp
//
// Methods:
//
//	Status() -> (s state, s jid)
//	    state: logged-out, connecting, connected, disconnected;
//	    jid: own phone number JID once linked
//	PairPhone(s phone) -> (s code)
//	    "Link with phone number": returns the 8-character code to type into
//	    WhatsApp on the main phone (Linked devices -> Link with phone number)
//	GetQR() -> (s code)
//	    starts linking by QR code if needed and returns the current QR text;
//	    later codes arrive with the QRCode signal
//	Logout()
//	    unlinks this device; the daemon exits afterwards and starts afresh
//	    on the next call
//	SendText(s id, s jid, s text) -> (s id)
//	SendFile(s id, s jid, s path, s caption) -> (s id)
//	    queue a message for sending and return at once. id may be empty to
//	    let the daemon choose one. The outcome arrives as a Receipt of type
//	    "server" (accepted by the server, time = server timestamp) or
//	    "error" (not sent).
//	MarkRead(s chat, s sender, as ids)
//	    sender is the author of the messages in groups, empty otherwise
//	SendChatState(s jid, b composing)
//	SetAvailable(b available)
//	SubscribePresence(s jid)
//	GetContacts() -> a(sss)        jid, full name (address book of the main
//	                               phone), push name; phone number JIDs only
//	GetGroups() -> a(ss)           jid, subject
//	GetProfilePicture(s jid) -> (s path)    preview picture, "" if none
//	GetThumbnail(s id) -> (ay jpeg)         preview of a media message
//	DownloadMedia(s id) -> (s path)         media of a received message
//	ReplayQueued() -> (u count)
//	    emits the signals that were queued while no UI was running
//
// Signals:
//
//	StateChanged(s state)
//	QRCode(s code)                 a new QR code; "" when linking timed out
//	Message(s id, s chat, s sender, s pushname, x time, b fromme,
//	        s type, s text, s mime, x size)
//	History(s id, s chat, s sender, s pushname, x time, b fromme,
//	        s type, s text, s mime, x size)
//	    older messages that WhatsApp hands over right after linking
//	Receipt(s chat, s sender, as ids, s type, x time)
//	    type: server, error, delivered, read, played, read-self, played-self
//	ChatState(s chat, s sender, b composing)
//	Presence(s jid, b available, x lastseen)
//
// Message types: text, image, video, audio, voice, document, sticker,
// location (text = "geo:lat,lon"), contact (text = vCard). Times are Unix
// seconds. Chats and senders are given by phone number JIDs
// (...@s.whatsapp.net) or group JIDs (...@g.us); WhatsApp's hidden user
// IDs (@lid) are translated where the mapping is known.
//
// While the UI (bus name org.yappari.Yappari) is not running, Message,
// History and Receipt signals are stored and replayed on ReplayQueued(),
// and new messages are announced with a desktop notification.
//
// Data: $XDG_DATA_HOME/yappari-wa (session, queue), $XDG_CACHE_HOME/yappari-wa
// (media and profile pictures).
package main

import (
	"context"
	"fmt"
	"os"
	"os/signal"
	"path/filepath"
	"sync"
	"syscall"
	"time"

	"github.com/godbus/dbus/v5"
	"github.com/godbus/dbus/v5/introspect"
	"go.mau.fi/whatsmeow"
	"go.mau.fi/whatsmeow/store"
	"go.mau.fi/whatsmeow/store/sqlstore"
	waLog "go.mau.fi/whatsmeow/util/log"
	_ "modernc.org/sqlite"
)

const (
	busName = "org.yappari.WhatsApp"
	objPath = dbus.ObjectPath("/org/yappari/WhatsApp")
	iface   = "org.yappari.WhatsApp"
	uiName  = "org.yappari.Yappari"
	uiPath  = "/org/yappari/Yappari"

	// History sync: at most this many of the newest messages per chat.
	historyPerChat = 40
)

type Service struct {
	cli      *whatsmeow.Client
	conn     *dbus.Conn
	cacheDir string
	queue    *Queue
	quit     chan struct{}

	mu    sync.Mutex
	state string

	pairMu  sync.Mutex
	pairing *pairSession
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

// restart makes the daemon exit shortly after a logout, so that the next
// call starts it again with a fresh, unlinked device.
func (s *Service) restart() {
	go func() {
		time.Sleep(500 * time.Millisecond)
		select {
		case <-s.quit:
		default:
			close(s.quit)
		}
	}()
}

const introspection = `
<node>
 <interface name="` + iface + `">
  <method name="Status"><arg name="state" type="s" direction="out"/><arg name="jid" type="s" direction="out"/></method>
  <method name="PairPhone"><arg name="phone" type="s" direction="in"/><arg name="code" type="s" direction="out"/></method>
  <method name="GetQR"><arg name="code" type="s" direction="out"/></method>
  <method name="Logout"/>
  <method name="SendText"><arg name="id" type="s" direction="in"/><arg name="jid" type="s" direction="in"/><arg name="text" type="s" direction="in"/><arg name="id" type="s" direction="out"/></method>
  <method name="SendFile"><arg name="id" type="s" direction="in"/><arg name="jid" type="s" direction="in"/><arg name="path" type="s" direction="in"/><arg name="caption" type="s" direction="in"/><arg name="id" type="s" direction="out"/></method>
  <method name="MarkRead"><arg name="chat" type="s" direction="in"/><arg name="sender" type="s" direction="in"/><arg name="ids" type="as" direction="in"/></method>
  <method name="SendChatState"><arg name="jid" type="s" direction="in"/><arg name="composing" type="b" direction="in"/></method>
  <method name="SetAvailable"><arg name="available" type="b" direction="in"/></method>
  <method name="SubscribePresence"><arg name="jid" type="s" direction="in"/></method>
  <method name="GetContacts"><arg name="contacts" type="a(sss)" direction="out"/></method>
  <method name="GetGroups"><arg name="groups" type="a(ss)" direction="out"/></method>
  <method name="GetProfilePicture"><arg name="jid" type="s" direction="in"/><arg name="path" type="s" direction="out"/></method>
  <method name="GetThumbnail"><arg name="id" type="s" direction="in"/><arg name="jpeg" type="ay" direction="out"/></method>
  <method name="DownloadMedia"><arg name="id" type="s" direction="in"/><arg name="path" type="s" direction="out"/></method>
  <method name="ReplayQueued"><arg name="count" type="u" direction="out"/></method>
  <signal name="StateChanged"><arg name="state" type="s"/></signal>
  <signal name="QRCode"><arg name="code" type="s"/></signal>
  <signal name="Message">` + messageArgs + `</signal>
  <signal name="History">` + messageArgs + `</signal>
  <signal name="Receipt"><arg name="chat" type="s"/><arg name="sender" type="s"/><arg name="ids" type="as"/><arg name="type" type="s"/><arg name="time" type="x"/></signal>
  <signal name="ChatState"><arg name="chat" type="s"/><arg name="sender" type="s"/><arg name="composing" type="b"/></signal>
  <signal name="Presence"><arg name="jid" type="s"/><arg name="available" type="b"/><arg name="lastseen" type="x"/></signal>
 </interface>` + introspect.IntrospectDataString + `</node>`

const messageArgs = `<arg name="id" type="s"/><arg name="chat" type="s"/><arg name="sender" type="s"/><arg name="pushname" type="s"/><arg name="time" type="x"/><arg name="fromme" type="b"/><arg name="type" type="s"/><arg name="text" type="s"/><arg name="mime" type="s"/><arg name="size" type="x"/>`

func main() {
	ctx := context.Background()
	stateDir := dataDir("XDG_DATA_HOME", ".local/share")
	os.MkdirAll(stateDir, 0700)
	log := waLog.Stdout("yappari-wa", "WARN", false)

	// The name WhatsApp shows in the list of linked devices.
	store.SetOSInfo("Yappari (Maemo Leste)", [3]uint32{3, 0, 0})

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
	queue, err := OpenQueue(filepath.Join(stateDir, "queue.db"))
	if err != nil {
		fmt.Fprintln(os.Stderr, "yappari-wa: queue:", err)
		os.Exit(1)
	}

	conn, err := dbus.ConnectSessionBus()
	if err != nil {
		fmt.Fprintln(os.Stderr, "yappari-wa: session bus:", err)
		os.Exit(1)
	}
	s := &Service{cli: whatsmeow.NewClient(device, log), conn: conn, queue: queue,
		cacheDir: dataDir("XDG_CACHE_HOME", ".cache"), state: "logged-out", quit: make(chan struct{})}
	s.cli.AddEventHandler(s.handle)
	conn.Export(s, objPath, iface)
	conn.Export(introspect.Introspectable(introspection), objPath, "org.freedesktop.DBus.Introspectable")

	reply, err := conn.RequestName(busName, dbus.NameFlagDoNotQueue)
	if err != nil || reply != dbus.RequestNameReplyPrimaryOwner {
		fmt.Fprintln(os.Stderr, "yappari-wa: already running")
		os.Exit(0)
	}

	// With a session, connect right away; without one, GetQR/PairPhone connect.
	if s.cli.Store.ID != nil {
		s.setState("connecting")
		if err := s.cli.Connect(); err != nil {
			fmt.Fprintln(os.Stderr, "yappari-wa: connect:", err)
			s.setState("disconnected")
		}
	}

	sig := make(chan os.Signal, 1)
	signal.Notify(sig, syscall.SIGINT, syscall.SIGTERM)
	select {
	case <-sig:
	case <-s.quit:
	}
	s.cli.Disconnect()
	queue.Close()
}
