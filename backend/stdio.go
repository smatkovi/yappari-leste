package main

// The daemon without a session bus: yappari-wa -stdio speaks the same
// methods and signals as JSON lines on its standard input and output, for
// user interfaces that have no D-Bus (Yappari's messenger on KolibriOS,
// which runs this daemon through its Linux personality). One object per
// line:
//
//	in:  {"id": 7, "call": "SendText", "args": ["", "4366...@s.whatsapp.net", "hello"]}
//	out: {"id": 7, "result": ["3EB0..."]}
//	     {"id": 7, "error": "not connected"}
//	     {"signal": "QRCode", "args": ["2@..."]}
//
// Results and signal arguments are the D-Bus method's out arguments in
// order; structures (GetContacts, GetGroups) are arrays of their fields,
// byte arrays (GetThumbnail) base64 strings. The other end is the user
// interface, so nothing is queued and no notifications are shown. The
// daemon ends when its standard input does. Log output goes to standard
// error.

import (
	"bufio"
	"encoding/json"
	"fmt"
	"os"
	"reflect"
	"sync"

	"github.com/godbus/dbus/v5"
)

type stdioBus struct {
	mu  sync.Mutex
	out *bufio.Writer
}

// the methods of the D-Bus interface, the only ones callable
var stdioMethods = map[string]bool{
	"Status": true, "PairPhone": true, "GetQR": true, "Logout": true, "SendText": true,
	"SendFile": true, "MarkRead": true, "SendChatState": true, "SetAvailable": true,
	"SubscribePresence": true, "GetContacts": true, "GetGroups": true,
	"GetProfilePicture": true, "GetThumbnail": true, "DownloadMedia": true,
	"ReplayQueued": true,
}

// newStdioBus takes standard output for the protocol; everything else
// printed (whatsmeow's log) goes to standard error from now on.
func newStdioBus() *stdioBus {
	b := &stdioBus{out: bufio.NewWriter(os.Stdout)}
	os.Stdout = os.Stderr
	return b
}

func (b *stdioBus) send(v map[string]interface{}) {
	line, err := json.Marshal(v)
	if err != nil {
		line, _ = json.Marshal(map[string]interface{}{"error": err.Error()})
	}
	b.mu.Lock()
	b.out.Write(line)
	b.out.WriteByte('\n')
	b.out.Flush()
	b.mu.Unlock()
}

func (b *stdioBus) signal(name string, args ...interface{}) {
	out := make([]interface{}, len(args))
	for i, a := range args {
		out[i] = plain(reflect.ValueOf(a))
	}
	b.send(map[string]interface{}{"signal": name, "args": out})
}

// plain turns structures into arrays of their fields, as D-Bus has them
func plain(v reflect.Value) interface{} {
	if !v.IsValid() {
		return nil
	}
	switch v.Kind() {
	case reflect.Struct:
		f := make([]interface{}, v.NumField())
		for i := range f {
			f[i] = plain(v.Field(i))
		}
		return f
	case reflect.Slice:
		if v.Type().Elem().Kind() == reflect.Uint8 {
			return v.Bytes()
		}
		f := make([]interface{}, v.Len())
		for i := range f {
			f[i] = plain(v.Index(i))
		}
		return f
	case reflect.Interface, reflect.Ptr:
		if v.IsNil() {
			return nil
		}
		return plain(v.Elem())
	default:
		return v.Interface()
	}
}

type stdioCall struct {
	ID   json.RawMessage   `json:"id"`
	Call string            `json:"call"`
	Args []json.RawMessage `json:"args"`
}

func (b *stdioBus) reply(id json.RawMessage, result []interface{}, err error) {
	m := map[string]interface{}{"id": id}
	if err != nil {
		m["error"] = err.Error()
	} else {
		m["result"] = result
	}
	b.send(m)
}

// call runs one method of s by its name, the arguments converted to its
// parameters' types
func (b *stdioBus) call(s *Service, c stdioCall) {
	if !stdioMethods[c.Call] {
		b.reply(c.ID, nil, fmt.Errorf("no method %q", c.Call))
		return
	}
	m := reflect.ValueOf(s).MethodByName(c.Call)
	t := m.Type()
	if len(c.Args) != t.NumIn() {
		b.reply(c.ID, nil, fmt.Errorf("%s takes %d arguments", c.Call, t.NumIn()))
		return
	}
	in := make([]reflect.Value, len(c.Args))
	for i, raw := range c.Args {
		p := reflect.New(t.In(i))
		if err := json.Unmarshal(raw, p.Interface()); err != nil {
			b.reply(c.ID, nil, fmt.Errorf("%s: argument %d: %v", c.Call, i+1, err))
			return
		}
		in[i] = p.Elem()
	}
	out := m.Call(in)
	result := make([]interface{}, 0, len(out))
	for _, o := range out {
		if e, ok := o.Interface().(*dbus.Error); ok {
			if e != nil {
				b.reply(c.ID, nil, e)
				return
			}
			continue
		}
		result = append(result, plain(o))
	}
	b.reply(c.ID, result, nil)
}

// serve reads the calls until standard input ends, then the daemon quits
func (b *stdioBus) serve(s *Service) {
	in := bufio.NewScanner(os.Stdin)
	in.Buffer(make([]byte, 64<<10), 16<<20)
	for in.Scan() {
		var c stdioCall
		if err := json.Unmarshal(in.Bytes(), &c); err != nil {
			b.reply(nil, nil, fmt.Errorf("not a call: %v", err))
			continue
		}
		// one at a time would block the UI behind a slow call (a download)
		go b.call(s, c)
	}
	select {
	case <-s.quit:
	default:
		close(s.quit)
	}
}
