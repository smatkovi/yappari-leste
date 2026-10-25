package main

// whatsmeow events -> D-Bus signals.

import (
	"context"
	"fmt"
	"os"
	"path/filepath"
	"sort"

	"go.mau.fi/whatsmeow/proto/waE2E"
	"go.mau.fi/whatsmeow/types"
	"go.mau.fi/whatsmeow/types/events"
	"google.golang.org/protobuf/proto"
)

// describe gives a message's type, text, media type, size and file name.
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

// pn gives the phone number JID for a hidden user ID (@lid) if it is known.
func (s *Service) pn(j types.JID) types.JID {
	j = j.ToNonAD()
	if j.Server == types.HiddenUserServer && s.cli.Store.LIDs != nil {
		if p, err := s.cli.Store.LIDs.GetPNForLID(context.Background(), j); err == nil && !p.IsEmpty() {
			return p.ToNonAD()
		}
	}
	return j
}

// normalize prefers the phone number form of j, using alt (the other
// address WhatsApp sent along) when j is a hidden user ID.
func (s *Service) normalize(j, alt types.JID) types.JID {
	j = j.ToNonAD()
	if j.Server == types.HiddenUserServer && alt.Server == types.DefaultUserServer {
		return alt.ToNonAD()
	}
	return s.pn(j)
}

// source gives the chat and sender of a message, receipt or chat state in
// the form the UI uses. ok is false for chats the UI does not show
// (status updates, channels, own broadcast lists).
func (s *Service) source(src *types.MessageSource) (chat, sender types.JID, ok bool) {
	sender = s.normalize(src.Sender, src.SenderAlt)
	chat = src.Chat.ToNonAD()
	switch chat.Server {
	case types.GroupServer:
	case types.BroadcastServer:
		if chat.User == "status" || !src.IsIncomingBroadcast() {
			return chat, sender, false
		}
		chat = sender
	case types.NewsletterServer:
		return chat, sender, false
	default:
		alt := src.SenderAlt
		if src.IsFromMe {
			alt = src.RecipientAlt
		}
		chat = s.normalize(chat, alt)
	}
	return chat, sender, true
}

// emitMessage sends a received or historical message as signal name.
func (s *Service) emitMessage(signal string, e *events.Message) bool {
	msg := e.Message
	if msg == nil {
		return false
	}
	kind, text, mt, size, _ := describe(msg)
	if kind == "" {
		return false
	}
	chat, sender, ok := s.source(&e.Info.MessageSource)
	if !ok {
		return false
	}
	if mt != "" {
		if raw, err := proto.Marshal(msg); err == nil {
			os.MkdirAll(filepath.Join(s.cacheDir, "messages"), 0700)
			os.WriteFile(s.mediaPath(e.Info.ID), raw, 0600)
		}
	}
	s.emit(signal, e.Info.ID, chat.String(), sender.String(), e.Info.PushName,
		e.Info.Timestamp.Unix(), e.Info.IsFromMe, kind, text, mt, size)
	return true
}

func (s *Service) handleHistory(e *events.HistorySync) {
	for _, conv := range e.Data.GetConversations() {
		chatJID, err := types.ParseJID(conv.GetID())
		if err != nil {
			continue
		}
		// Keep the newest messages, whatever order WhatsApp sent them in
		var msgs []*events.Message
		for _, hm := range conv.GetMessages() {
			if evt, err := s.cli.ParseWebMessage(chatJID, hm.GetMessage()); err == nil {
				msgs = append(msgs, evt)
			}
		}
		sort.SliceStable(msgs, func(i, j int) bool {
			return msgs[i].Info.Timestamp.After(msgs[j].Info.Timestamp)
		})
		n := 0
		for _, evt := range msgs {
			if n >= historyPerChat {
				break
			}
			if s.emitMessage("History", evt) {
				n++
			}
		}
	}
}

func (s *Service) handle(evt interface{}) {
	switch e := evt.(type) {
	case *events.Connected:
		s.setState("connected")
	case *events.Disconnected, *events.StreamReplaced, *events.ConnectFailure:
		if s.cli.Store.ID != nil {
			s.setState("disconnected")
		}
	case *events.LoggedOut:
		s.setState("logged-out")
		s.restart()
	case *events.PairSuccess:
		s.setState("connecting")
	case *events.Message:
		s.emitMessage("Message", e)
	case *events.HistorySync:
		s.handleHistory(e)
	case *events.Receipt:
		kind := string(e.Type)
		switch e.Type {
		case types.ReceiptTypeDelivered:
			kind = "delivered"
		case types.ReceiptTypeRead, types.ReceiptTypeReadSelf, types.ReceiptTypePlayed, types.ReceiptTypePlayedSelf:
		default:
			return
		}
		chat, sender, ok := s.source(&e.MessageSource)
		if !ok {
			return
		}
		ids := make([]string, len(e.MessageIDs))
		for i, id := range e.MessageIDs {
			ids[i] = string(id)
		}
		s.emit("Receipt", chat.String(), sender.String(), ids, kind, e.Timestamp.Unix())
	case *events.ChatPresence:
		chat, sender, ok := s.source(&e.MessageSource)
		if !ok {
			return
		}
		s.emit("ChatState", chat.String(), sender.String(), e.State == types.ChatPresenceComposing)
	case *events.Presence:
		var lastSeen int64
		if !e.LastSeen.IsZero() {
			lastSeen = e.LastSeen.Unix()
		}
		s.emit("Presence", s.pn(e.From).String(), !e.Unavailable, lastSeen)
	}
}
