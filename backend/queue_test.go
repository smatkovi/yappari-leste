package main

import (
	"path/filepath"
	"reflect"
	"testing"
)

func TestQueueRoundTrip(t *testing.T) {
	q, err := OpenQueue(filepath.Join(t.TempDir(), "queue.db"))
	if err != nil {
		t.Fatal(err)
	}
	defer q.Close()
	msg := []interface{}{"ID1", "4366012345@s.whatsapp.net", "4366012345@s.whatsapp.net", "Anna",
		int64(1792925732), false, "text", "Hallo äöü \U0001F600", "", int64(0)}
	rcpt := []interface{}{"4366012345@s.whatsapp.net", "", []string{"A", "B"}, "read", int64(1792925733)}
	empty := []interface{}{"c", "s", []string{}, "delivered", int64(1)}
	for _, it := range []QueuedSignal{{"Message", msg}, {"Receipt", rcpt}, {"Receipt", empty}} {
		if err := q.Push(it.Signal, it.Args); err != nil {
			t.Fatal(err)
		}
	}
	got, err := q.TakeAll()
	if err != nil {
		t.Fatal(err)
	}
	want := []QueuedSignal{{"Message", msg}, {"Receipt", rcpt}, {"Receipt", empty}}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("got %#v\nwant %#v", got, want)
	}
	if again, _ := q.TakeAll(); len(again) != 0 {
		t.Fatalf("queue not emptied: %v", again)
	}
}
