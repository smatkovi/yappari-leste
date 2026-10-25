#!/usr/bin/python3
"""Stand-in for the yappari-wa daemon, for testing the UI without a
WhatsApp account.

Owns org.yappari.WhatsApp on the session bus with the same API as
backend/main.go and answers like a linked, connected account with a few
contacts and a group. Sent messages get "server", "delivered" and "read"
receipts after a moment. Events can be triggered from the shell through
the extra interface org.yappari.WhatsApp.Mock, e.g.

  dbus-send --session --dest=org.yappari.WhatsApp /org/yappari/WhatsApp \\
      org.yappari.WhatsApp.Mock.Incoming string:4366011111111@s.whatsapp.net \\
      string:4366011111111@s.whatsapp.net string:Anna string:text string:Hello

  Mock.Incoming(chat, sender, pushname, type, text)   type: text, image, voice...
  Mock.Typing(chat, sender, composing)
  Mock.SetPresence(jid, available, lastseen)
  Mock.SendHistory()              sends a few older messages (History signal)
  Mock.SetState(state)            emits StateChanged

Options: --logged-out starts unlinked (GetQR/PairPhone then "link" after a
few seconds); --notifications also stands in for the Hildon notification
services (org.freedesktop.Notifications, com.nokia.HildonSVNotificationDaemon),
for a private test bus. Every call is logged to stdout.
"""

import base64
import os
import sys
import time

import dbus
import dbus.service
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib

NAME = "org.yappari.WhatsApp"
PATH = "/org/yappari/WhatsApp"
IFACE = "org.yappari.WhatsApp"
MOCK = "org.yappari.WhatsApp.Mock"

OWN = "4366000000001@s.whatsapp.net"
CONTACTS = [
    ("4366011111111@s.whatsapp.net", "Anna Example", "Anna"),
    ("4366022222222@s.whatsapp.net", "Bernd Beispiel", "Bernd"),
    ("4366033333333@s.whatsapp.net", "", "Carla"),
]
GROUPS = [("120363000000000001@g.us", "Leste Testers")]

# 96x72 PNG as preview, 640x480 PNG as "downloaded" picture
THUMB = base64.b64decode("iVBORw0KGgoAAAANSUhEUgAAAGAAAABICAIAAACGBWc0AAABhUlEQVR4nO3bwUlEQRBF0fliHK5dGYFBGIJhGM6EYBiujchFgwjyvWK9V13Cu2uxa45ljx/1uH95u6TzbnYPML0AQQGCAgQFCAoQFCAoQFCAoNvdA1zen55//oCH12vHHCcdWx41EOWsfqxuoD/TfK2TqQlI4vK9BqmOS9qkY/3Mn3k3qOEFrHyrZNygNh3rWS6gTh3riRagfh3fuXqgXTqm08VAe3UcMyiBJuishJPIgOborFTz5Gke0gBNW5+VZKpsECQAmrk+q/ps2SCoCjR5fVbFCbNBUICgAEEloPkX0KoyZzYIChAUIChAUICgAEEBggIElYD2/mHK76vMmQ2CAgQFCKoCzb+GihNmgyAB0OQlqs+WDYI0QDOXSDJVNgiSAU1bItU8yg2aYyScRPwtNsFIO4P+DtprJD/dcknvMnKc63oX6zcynWh8m+808p3V8c8s1l/Aur8MHT8o+l5Dw5Ied9dH9xn/ujxqQAGCAgQFCAoQFCAoQFCAoABBAYICBH0A/Kpj9rVkkD0AAAAASUVORK5CYII=")
FULL = base64.b64decode("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")

MEDIA_DIR = os.path.join(os.environ.get("XDG_CACHE_HOME", "/tmp"), "yappari-mock")


def log(*args):
    print(time.strftime("%H:%M:%S"), *args, flush=True)


class Mock(dbus.service.Object):
    def __init__(self, bus, linked):
        super().__init__(bus, PATH)
        self.state = "connected" if linked else "logged-out"
        self.counter = 0

    def later(self, seconds, func, *args):
        GLib.timeout_add(int(seconds * 1000), lambda: (func(*args), False)[1])

    # --- org.yappari.WhatsApp ---------------------------------------------

    @dbus.service.method(IFACE, out_signature="ss")
    def Status(self):
        log("Status")
        return self.state, OWN if self.state != "logged-out" else ""

    @dbus.service.method(IFACE, in_signature="s", out_signature="s")
    def PairPhone(self, phone):
        log("PairPhone", phone)
        self.later(4, self.link)
        return "MOCK2345"

    @dbus.service.method(IFACE, out_signature="s")
    def GetQR(self):
        log("GetQR")
        self.later(3, self.QRCode, "2@mock-second-code,abcdefghijklmnopqrstuvwxyz0123456789,"
                                   "ABCDEFGHIJKLMNOPQRSTUVWXYZ,0123456789abcdef")
        return ("2@mock-first-code,abcdefghijklmnopqrstuvwxyz0123456789,"
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ,0123456789abcdef")

    def link(self):
        self.state = "connecting"
        self.StateChanged("connecting")
        self.later(1, self.connected)

    def connected(self):
        self.state = "connected"
        self.StateChanged("connected")

    @dbus.service.method(IFACE)
    def Logout(self):
        log("Logout")
        self.state = "logged-out"
        self.StateChanged("logged-out")

    def receipts(self, jid, msg_id):
        now = int(time.time())
        self.later(0.5, self.Receipt, jid, "", [msg_id], "server", now)
        self.later(1.5, self.Receipt, jid, jid, [msg_id], "delivered", now + 1)
        self.later(3.0, self.Receipt, jid, jid, [msg_id], "read", now + 2)

    @dbus.service.method(IFACE, in_signature="sss", out_signature="s")
    def SendText(self, msg_id, jid, text):
        log("SendText", msg_id, jid, repr(str(text)))
        self.receipts(jid, msg_id)
        return msg_id

    @dbus.service.method(IFACE, in_signature="ssss", out_signature="s")
    def SendFile(self, msg_id, jid, path, caption):
        log("SendFile", msg_id, jid, path, repr(str(caption)))
        self.receipts(jid, msg_id)
        return msg_id

    @dbus.service.method(IFACE, in_signature="ssas")
    def MarkRead(self, chat, sender, ids):
        log("MarkRead", chat, sender, list(map(str, ids)))

    @dbus.service.method(IFACE, in_signature="sb")
    def SendChatState(self, jid, composing):
        log("SendChatState", jid, bool(composing))

    @dbus.service.method(IFACE, in_signature="b")
    def SetAvailable(self, available):
        log("SetAvailable", bool(available))

    @dbus.service.method(IFACE, in_signature="s")
    def SubscribePresence(self, jid):
        log("SubscribePresence", jid)
        self.later(1, self.Presence, jid, True, 0)

    @dbus.service.method(IFACE, out_signature="a(sss)")
    def GetContacts(self):
        log("GetContacts")
        return dbus.Array(CONTACTS, signature="(sss)")

    @dbus.service.method(IFACE, out_signature="a(ss)")
    def GetGroups(self):
        log("GetGroups")
        return dbus.Array(GROUPS, signature="(ss)")

    @dbus.service.method(IFACE, in_signature="s", out_signature="s")
    def GetProfilePicture(self, jid):
        log("GetProfilePicture", jid)
        return ""

    @dbus.service.method(IFACE, in_signature="s", out_signature="ay")
    def GetThumbnail(self, msg_id):
        log("GetThumbnail", msg_id)
        return dbus.ByteArray(THUMB)

    @dbus.service.method(IFACE, in_signature="s", out_signature="s")
    def DownloadMedia(self, msg_id):
        log("DownloadMedia", msg_id)
        os.makedirs(MEDIA_DIR, exist_ok=True)
        path = os.path.join(MEDIA_DIR, msg_id + ".png")
        with open(path, "wb") as f:
            f.write(FULL)
        return path

    @dbus.service.method(IFACE, out_signature="u")
    def ReplayQueued(self):
        log("ReplayQueued")
        return 0

    @dbus.service.signal(IFACE, signature="s")
    def StateChanged(self, state):
        log("signal StateChanged", state)

    @dbus.service.signal(IFACE, signature="s")
    def QRCode(self, code):
        log("signal QRCode")

    @dbus.service.signal(IFACE, signature="ssssxbsssx")
    def Message(self, msg_id, chat, sender, pushname, t, fromme, kind, text, mime, size):
        log("signal Message", msg_id, chat, kind)

    @dbus.service.signal(IFACE, signature="ssssxbsssx")
    def History(self, msg_id, chat, sender, pushname, t, fromme, kind, text, mime, size):
        pass

    @dbus.service.signal(IFACE, signature="ssassx")
    def Receipt(self, chat, sender, ids, kind, t):
        log("signal Receipt", chat, list(map(str, ids)), kind)

    @dbus.service.signal(IFACE, signature="ssb")
    def ChatState(self, chat, sender, composing):
        log("signal ChatState", chat, bool(composing))

    @dbus.service.signal(IFACE, signature="sbx")
    def Presence(self, jid, available, lastseen):
        log("signal Presence", jid, bool(available))

    # --- org.yappari.WhatsApp.Mock ----------------------------------------

    def next_id(self):
        self.counter += 1
        return "MOCK%08X%04d" % (int(time.time()), self.counter)

    @dbus.service.method(MOCK, in_signature="sssss", out_signature="s")
    def Incoming(self, chat, sender, pushname, kind, text):
        msg_id = self.next_id()
        mime, size = "", 0
        if kind in ("image", "sticker"):
            mime, size = "image/png", len(FULL)
        elif kind in ("voice", "audio"):
            mime, size = "audio/ogg; codecs=opus", 4000
        elif kind == "video":
            mime, size = "video/mp4", 2000000
        elif kind == "document":
            mime, size = "application/pdf", 120000
        self.Message(msg_id, chat, sender, pushname, int(time.time()), False,
                     kind, text, mime, size)
        return msg_id

    @dbus.service.method(MOCK, in_signature="ssb")
    def Typing(self, chat, sender, composing):
        self.ChatState(chat, sender, composing)

    @dbus.service.method(MOCK, in_signature="sbx")
    def SetPresence(self, jid, available, lastseen):
        self.Presence(jid, available, lastseen)

    @dbus.service.method(MOCK)
    def SendHistory(self):
        now = int(time.time())
        anna, bernd = CONTACTS[0][0], CONTACTS[1][0]
        group = GROUPS[0][0]
        items = [
            (anna, anna, "Anna", now - 86400 * 2, False, "Hi! Are you on Leste now?"),
            (anna, OWN, "", now - 86400 * 2 + 60, True, "Yes, Yappari is back :-)"),
            (bernd, bernd, "Bernd", now - 86400 * 5, False, "See you tomorrow"),
            (group, bernd, "Bernd", now - 3600 * 5, False, "Welcome to the group"),
            (group, CONTACTS[2][0], "Carla", now - 3600 * 4, False, "Hello everyone"),
        ]
        for chat, sender, push, t, fromme, text in items:
            self.History(self.next_id(), chat, sender, push, t, fromme, "text", text, "", 0)
        log("sent", len(items), "history messages")

    @dbus.service.method(MOCK, in_signature="s")
    def SetState(self, state):
        self.state = str(state)
        self.StateChanged(self.state)


class Notifications(dbus.service.Object):
    """Logs notifications instead of showing them."""

    def __init__(self, bus):
        super().__init__(bus, "/org/freedesktop/Notifications")
        self.last = 0

    @dbus.service.method("org.freedesktop.Notifications", in_signature="susssasa{sv}i",
                         out_signature="u")
    def Notify(self, app, replaces, icon, summary, body, actions, hints, timeout):
        self.last += 1
        log("Notify", repr(str(summary)), repr(str(body)), dict((str(k), str(v)) for k, v in hints.items()))
        return self.last

    @dbus.service.method("org.freedesktop.Notifications", in_signature="u")
    def CloseNotification(self, nid):
        log("CloseNotification", int(nid))


class SoundAndVibra(dbus.service.Object):
    def __init__(self, bus):
        super().__init__(bus, "/com/nokia/HildonSVNotificationDaemon")

    @dbus.service.method("com.nokia.HildonSVNotificationDaemon", in_signature="a{sv}s",
                         out_signature="i")
    def PlayEvent(self, hints, sender):
        log("PlayEvent", dict((str(k), str(v)) for k, v in hints.items()))
        return 1


def main():
    DBusGMainLoop(set_as_default=True)
    bus = dbus.SessionBus()
    name = dbus.service.BusName(NAME, bus, do_not_queue=True)
    Mock(bus, "--logged-out" not in sys.argv)
    if "--notifications" in sys.argv:
        names = [dbus.service.BusName("org.freedesktop.Notifications", bus),
                 dbus.service.BusName("com.nokia.HildonSVNotificationDaemon", bus)]
        Notifications(bus)
        SoundAndVibra(bus)
    log("mock yappari-wa running")
    GLib.MainLoop().run()


if __name__ == "__main__":
    main()
