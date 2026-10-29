package main

// Signals for a UI that is not running: they are stored in a small SQLite
// database and replayed when the UI asks for them (ReplayQueued). New
// messages are announced with a desktop notification meanwhile.

import (
	"bytes"
	"context"
	"database/sql"
	"encoding/json"
	"fmt"
	"os"
	"sync"

	"github.com/godbus/dbus/v5"
	"go.mau.fi/whatsmeow/types"
)

// Signals that are queued while the UI is away, with their argument types.
var queuedSignals = map[string][]string{
	"Message": {"s", "s", "s", "s", "x", "b", "s", "s", "s", "x"},
	"History": {"s", "s", "s", "s", "x", "b", "s", "s", "s", "x"},
	"Receipt": {"s", "s", "as", "s", "x"},
}

type QueuedSignal struct {
	Signal string
	Args   []interface{}
}

type Queue struct {
	db *sql.DB
	mu sync.Mutex
}

func OpenQueue(path string) (*Queue, error) {
	db, err := sql.Open("sqlite", "file:"+path+"?_pragma=busy_timeout(5000)")
	if err != nil {
		return nil, err
	}
	_, err = db.Exec(`create table if not exists queue (
		seq integer primary key autoincrement,
		signal text not null,
		args text not null)`)
	if err != nil {
		db.Close()
		return nil, err
	}
	return &Queue{db: db}, nil
}

func (q *Queue) Close() { q.db.Close() }

func (q *Queue) Push(signal string, args []interface{}) error {
	raw, err := json.Marshal(args)
	if err != nil {
		return err
	}
	q.mu.Lock()
	defer q.mu.Unlock()
	_, err = q.db.Exec("insert into queue (signal, args) values (?, ?)", signal, string(raw))
	return err
}

// TakeAll removes and returns all queued signals, oldest first.
func (q *Queue) TakeAll() ([]QueuedSignal, error) {
	q.mu.Lock()
	defer q.mu.Unlock()
	tx, err := q.db.Begin()
	if err != nil {
		return nil, err
	}
	defer tx.Rollback()
	rows, err := tx.Query("select signal, args from queue order by seq")
	if err != nil {
		return nil, err
	}
	var out []QueuedSignal
	for rows.Next() {
		var name, raw string
		if err := rows.Scan(&name, &raw); err != nil {
			rows.Close()
			return nil, err
		}
		args, err := decodeArgs(name, raw)
		if err != nil {
			fmt.Fprintln(os.Stderr, "yappari-wa: dropping queued", name+":", err)
			continue
		}
		out = append(out, QueuedSignal{name, args})
	}
	rows.Close()
	if _, err := tx.Exec("delete from queue"); err != nil {
		return nil, err
	}
	return out, tx.Commit()
}

// decodeArgs turns stored JSON back into the D-Bus types of the signal.
func decodeArgs(name, raw string) ([]interface{}, error) {
	types, ok := queuedSignals[name]
	if !ok {
		return nil, fmt.Errorf("unknown signal")
	}
	dec := json.NewDecoder(bytes.NewReader([]byte(raw)))
	dec.UseNumber()
	var vals []interface{}
	if err := dec.Decode(&vals); err != nil {
		return nil, err
	}
	if len(vals) != len(types) {
		return nil, fmt.Errorf("%d arguments, want %d", len(vals), len(types))
	}
	args := make([]interface{}, len(vals))
	for i, t := range types {
		switch t {
		case "s":
			v, ok := vals[i].(string)
			if !ok {
				return nil, fmt.Errorf("argument %d is not a string", i)
			}
			args[i] = v
		case "x":
			n, ok := vals[i].(json.Number)
			if !ok {
				return nil, fmt.Errorf("argument %d is not a number", i)
			}
			v, err := n.Int64()
			if err != nil {
				return nil, err
			}
			args[i] = v
		case "b":
			v, ok := vals[i].(bool)
			if !ok {
				return nil, fmt.Errorf("argument %d is not a boolean", i)
			}
			args[i] = v
		case "as":
			list, ok := vals[i].([]interface{})
			if !ok && vals[i] != nil {
				return nil, fmt.Errorf("argument %d is not a list", i)
			}
			strs := make([]string, 0, len(list))
			for _, e := range list {
				str, ok := e.(string)
				if !ok {
					return nil, fmt.Errorf("argument %d holds a non-string", i)
				}
				strs = append(strs, str)
			}
			args[i] = strs
		}
	}
	return args, nil
}

// uiPresent tells whether the Yappari UI is running on the session bus.
func (s *Service) uiPresent() bool {
	if s.stdio != nil {
		return true // the other end of standard output
	}
	var has bool
	err := s.conn.BusObject().Call("org.freedesktop.DBus.NameHasOwner", 0, uiName).Store(&has)
	return err == nil && has
}

// emit sends a signal, or queues it while the UI is away.
func (s *Service) emit(name string, args ...interface{}) {
	if _, ok := queuedSignals[name]; ok && !s.uiPresent() {
		if err := s.queue.Push(name, args); err == nil {
			if name == "Message" {
				s.notify(args)
			}
			return
		}
	}
	s.signal(name, args...)
}

var notifyIDs = map[string]uint32{}
var notifyMu sync.Mutex

// notify announces a message that arrived while the UI is not running.
// Activating the notification starts the UI through D-Bus activation.
func (s *Service) notify(args []interface{}) {
	chat, _ := args[1].(string)
	sender, _ := args[2].(string)
	pushName, _ := args[3].(string)
	fromMe, _ := args[5].(bool)
	kind, _ := args[6].(string)
	text, _ := args[7].(string)
	if fromMe {
		return
	}
	title := s.displayName(sender, pushName)
	if j, err := types.ParseJID(chat); err == nil && j.Server == types.GroupServer {
		title += " (group)"
	}
	body := text
	if kind != "text" {
		body = "[" + kind + "]"
		if text != "" && kind != "location" && kind != "contact" {
			body += " " + text
		}
	}
	hints := map[string]dbus.Variant{
		"category":              dbus.MakeVariant("yappari-message"),
		"persistent":            dbus.MakeVariant(true),
		"dbus-callback-default": dbus.MakeVariant(uiName + " " + uiPath + " " + uiName + " ShowWindow"),
	}
	notifyMu.Lock()
	replaces := notifyIDs[chat]
	notifyMu.Unlock()
	go func() {
		var id uint32
		err := s.conn.Object("org.freedesktop.Notifications", "/org/freedesktop/Notifications").Call(
			"org.freedesktop.Notifications.Notify", 0, "Yappari", replaces, "yappari-notification",
			title, body, []string{}, hints, int32(-1)).Store(&id)
		if err != nil {
			fmt.Fprintln(os.Stderr, "yappari-wa: notification:", err)
			return
		}
		notifyMu.Lock()
		notifyIDs[chat] = id
		notifyMu.Unlock()
	}()
}

// displayName gives the address book name, push name or number of jid.
func (s *Service) displayName(jid, pushName string) string {
	if j, err := types.ParseJID(jid); err == nil && s.cli.Store.Contacts != nil {
		if c, err := s.cli.Store.Contacts.GetContact(context.Background(), j); err == nil {
			if c.FullName != "" {
				return c.FullName
			}
			if c.PushName != "" {
				return c.PushName
			}
		}
		if pushName == "" {
			return "+" + j.User
		}
	}
	return pushName
}
