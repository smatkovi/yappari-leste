# Yappari for Maemo Leste

Yappari was the WhatsApp client of the Nokia N900 (Maemo 5), written by
Naikel Aparicio and later maintained by Alvaro Gamez. Its protocol code
spoke WhatsApp's old FunXMPP protocol, which WhatsApp switched off years
ago. This is Yappari's user interface ported to Qt 5 on
[Maemo Leste](https://maemo-leste.github.io/), with a new connection to
WhatsApp underneath: **yappari-wa**, a small daemon built on
[whatsmeow](https://github.com/tulir/whatsmeow) that speaks today's
multi-device protocol.

Yappari runs as a *linked device* of an existing WhatsApp account, like
WhatsApp Web: the account stays on your main phone, and Yappari is linked
to it once with a pairing code or a QR code.

Developed and tested on an F(x)tec Pro1X (arm64) with Maemo Leste
(Devuan 5 / Debian 12, Qt 5.15).

> Yappari is not affiliated with WhatsApp or Meta. WhatsApp does not
> permit unofficial clients; using one may get the account blocked. Use
> it at your own risk.

## How it fits together

```
 Yappari (Qt 5, src/)  <-- session D-Bus -->  yappari-wa (Go, backend/)  <-->  WhatsApp
   org.yappari.Yappari                          org.yappari.WhatsApp
```

* **yappari-wa** (`backend/`) keeps the connection to WhatsApp, the
  encryption keys and the session. It offers WhatsApp to the UI on the
  session bus (`org.yappari.WhatsApp` at `/org/yappari/WhatsApp`), shaped
  after the slots and signals of Yappari's former `Connection` class. The
  full API (methods, signals, types) is documented in the header comment
  of `backend/main.go`.
* **Yappari** (`src/`) is the Qt 4 GUI of Yappari 2.0.27 ported to Qt 5:
  `WhatsApp/waconnection.cpp` replaces the protocol code with an
  asynchronous D-Bus client of the daemon; `client.cpp` relays between
  the GUI and the daemon; the GUI, storage (SQLite) and contact code are
  Yappari's own, adapted.
* The daemon is started by D-Bus activation on the first call
  (`/usr/share/dbus-1/services/org.yappari.WhatsApp.service`) and keeps
  running when Yappari is closed, so messages keep arriving. While the UI
  is not on the bus, the daemon stores incoming messages and receipts and
  shows a notification for each new message; Yappari fetches the stored
  ones when it starts (`ReplayQueued`). The notification asks Hildon to
  start Yappari through `org.yappari.Yappari` when tapped.
* Closing Yappari's main window only hides it, as on the N900; **Quit** in
  the menu ends the UI. There is no autostart at boot: after a reboot,
  the daemon starts together with Yappari.
* `yappari-wa -stdio` runs without a session bus: the same methods and
  signals as JSON lines on standard input and output (`backend/stdio.go`),
  for user interfaces without D-Bus, such as the messenger of
  [KolibriOS for ARM](https://github.com/smatkovi/kolibrios-arm), which
  runs the daemon as a Linux program.

## Linking your WhatsApp account

1. Start Yappari. If it is not linked yet, the window *Link with
   WhatsApp* opens (later: menu, **Account**).
2. Enter the phone number of your WhatsApp account with its country code
   (e.g. `+43 660 1234567`) and tap **Get code**. Yappari shows an
   8-character code such as `ABCD-EFGH`.
3. On your main phone open WhatsApp: **Settings → Linked devices → Link
   a device → Link with phone number instead**, and type the code.
4. Alternatively tap **Use a QR code instead** and scan the code with
   WhatsApp on the main phone (**Settings → Linked devices → Link a
   device**). The QR code renews itself; when it runs out, tap
   **New QR code**.

When linking succeeds the window closes. WhatsApp then sends the recent
history of your chats, your contacts (with the names from your main
phone's address book) and group names. The device appears as *Yappari
(Maemo Leste)* in the list of linked devices. WhatsApp unlinks devices
whose main phone has been offline for about 14 days.

**Account → Unlink** removes Yappari from the account; the chats stored
on the phone stay.

## What works

* Chat list with last message, time, profile pictures, group avatars;
  new chat with any contact or group.
* Sending and receiving text messages, in single and group chats;
  message history after linking.
* Receipts: one check (server), two checks (delivered), blue checks
  (read), voice note played; read receipts for messages you look at
  (can be turned off for single chats in **Settings**).
* Typing indicator in both directions, online / last seen.
* Pictures, stickers, videos, voice notes, audio and documents: preview
  pictures in the chat, download (automatic up to a size set in
  **Settings**), pictures in a built-in viewer, voice notes and audio
  played in the chat, other files opened with the system's handler.
  Locations open in a map in the browser, shared contacts are shown as
  text.
* Sending files (pictures, videos, audio, documents) with a caption:
  **Send File** in the chat menu.
* Notifications (Hildon notifications with sound and vibration) for new
  messages, muting chats.
* Messages are stored on the phone (Yappari's SQLite logs), also those
  sent from your other devices.
* Tap and hold a chat in the list to delete it, a message to copy,
  forward or delete it.

## What is gone or not done

Dropped with the old protocol or because Leste lacks it: registration by
SMS/voice call and everything tied to the phone number as account (IMEI
passwords, SIM change detection), setting your status, name and profile
picture, creating and managing groups, blocking contacts, network usage
counters, synchronisation with the Maemo address book (names now come
from WhatsApp's copy of the main phone's contacts), recording voice notes
(GStreamer 0.10), the status menu applet (GTK, libhildondesktop), start
at boot, and Yappari's emoji picture sets (emoji are text now, drawn by
the system's emoji font; install `fonts-noto-color-emoji` for colour
emoji). Not implemented: reactions, edits, deletions, replies/quotes,
polls, calls.

Known limitations of this version:

* WhatsApp now often addresses people by hidden user IDs (`@lid`).
  yappari-wa translates them to phone numbers when WhatsApp has sent the
  mapping; until then such a chat shows up under the hidden ID, apart
  from the chat with the same person's number.
* A message the server refuses is tried again on the next two
  connections, then given up with a notice.
* Read receipts are sent one message at a time.
* Yappari does not start at boot.

## Data

* `~/.local/share/yappari/`: chats (`logs/*.dblog`), contacts
  (`roster.db`), open chats (`chats.db`)
* `~/.config/yappari/yappari.conf`: settings
* `~/.local/share/yappari-wa/`: WhatsApp session (`session.db`, keep it
  private) and the queue for a closed UI
* `~/.cache/yappari-wa/`: downloaded media and profile pictures

`YAPPARI_DEBUG=1 yappari` prints debug output.

## Building

The daemon needs a current Go (see `backend/go.mod`); Debian 12's Go is
too old for whatsmeow, so it is cross-built on a PC:

```sh
make backend      # backend/yappari-wa for arm64, static, no cgo
make test         # go vet and the unit tests of the daemon
```

The package is then built on the phone (or in an arm64 Leste chroot):

```sh
apt-get install --no-install-recommends debhelper pkg-config qtbase5-dev \
    qt5-qmake libqt5maemo5-dev libqt5x11extras5-dev qtmultimedia5-dev \
    libqrencode-dev libx11-dev
dpkg-buildpackage -b -us -uc     # with backend/yappari-wa in the tree
```

This gives `yappari-leste_3.0.0_arm64.deb`. Without packaging,
`cd src && qmake && make` builds the UI alone.

### Testing without a WhatsApp account

`tools/mock-backend.py` stands in for yappari-wa: it owns
`org.yappari.WhatsApp` with the same API, answers like a linked account
with a few contacts and a group, acknowledges sent messages with
receipts, and has an extra interface to inject incoming messages, typing
and history from the shell (see its header). With `--notifications` it
also logs the notifications Yappari sends. Run it on a private session
bus (`dbus-run-session`), together with Yappari.

## Licenses

* Yappari's source files carry a BSD 2-clause notice (Naikel Aparicio,
  Alvaro Gamez); the Debian packaging of Yappari declared the package
  GPL-2+. Both notices are kept, see `debian/copyright`. The files that
  are new in this port (`backend/`, `src/WhatsApp/`, `LinkWindow`,
  `ImageViewer`, `EmojiDialog`, `tools/`, `debian/`) are BSD 2-clause
  too.
* The icons in `src/icons/` are Yappari's. Yappari's emoji picture sets
  are not included, their origin and license are unclear.
* Yappari 2.0.27 also contained parts of libexif/libjpeg (LGPL) and an X11
  header (MIT); this port does not use them.
* yappari-wa links whatsmeow (MPL-2.0, Tulir Asokan) and
  go.mau.fi/libsignal (GPL-3.0), plus Go modules under BSD, MIT and
  Apache-2.0 licenses (`backend/go.sum`). The `yappari-wa` binary is
  therefore distributed under the terms of the GPL-3.0 as a whole; its
  source is `backend/` with the module versions pinned in `go.sum`.

## Credits

* Naikel Aparicio (Scorpius): Yappari
* Alvaro Gamez: Yappari maintenance, 2014-2015
* Tulir Asokan and contributors: whatsmeow
* The Maemo Leste project: Maemo on current hardware, Qt 5 with the
  Maemo 5 extras
