/* Copyright 2026 Sebastian Matkovich. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER ''AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <QDBusConnection>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusArgument>
#include <QDateTime>
#include <QRegExp>

#include "globalconstants.h"
#include "util/utilities.h"

#include "waconnection.h"

// Downloads and linking can take a while
#define LONG_TIMEOUT    (5 * 60 * 1000)
#define LINK_TIMEOUT    (60 * 1000)

WAConnection::WAConnection(QObject *parent) :
    QObject(parent)
{
    m_state = "disconnected";
}

QString WAConnection::state() const
{
    return m_state;
}

QString WAConnection::ownJid() const
{
    return m_jid;
}

bool WAConnection::isConnected() const
{
    return m_state == "connected";
}

QDBusMessage WAConnection::methodCall(const QString &method, const QVariantList &args)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(WA_SERVICE, WA_OBJECT, WA_INTERFACE, method);
    msg.setArguments(args);
    return msg;
}

QDBusPendingCallWatcher *WAConnection::asyncCall(const QString &method, const QVariantList &args,
                                                 int timeout)
{
    QDBusPendingCall call = QDBusConnection::sessionBus().asyncCall(methodCall(method, args), timeout);
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished,
            watcher, &QDBusPendingCallWatcher::deleteLater);
    return watcher;
}

void WAConnection::fireAndForget(const QString &method, const QVariantList &args)
{
    QDBusPendingCallWatcher *watcher = asyncCall(method, args);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [method](QDBusPendingCallWatcher *w) {
        if (w->isError())
            Utilities::logData(method + ": " + w->error().message());
    });
}

void WAConnection::start()
{
    QDBusConnection bus = QDBusConnection::sessionBus();

    // Any sender: the daemon may not be running yet (D-Bus activation)
    bus.connect(QString(), WA_OBJECT, WA_INTERFACE, "StateChanged",
                this, SLOT(onStateChanged(QString)));
    bus.connect(QString(), WA_OBJECT, WA_INTERFACE, "QRCode",
                this, SLOT(onQRCode(QString)));
    bus.connect(QString(), WA_OBJECT, WA_INTERFACE, "Message",
                this, SLOT(onMessage(QDBusMessage)));
    bus.connect(QString(), WA_OBJECT, WA_INTERFACE, "History",
                this, SLOT(onHistory(QDBusMessage)));
    bus.connect(QString(), WA_OBJECT, WA_INTERFACE, "Receipt",
                this, SLOT(onReceipt(QDBusMessage)));
    bus.connect(QString(), WA_OBJECT, WA_INTERFACE, "ChatState",
                this, SLOT(onChatState(QDBusMessage)));
    bus.connect(QString(), WA_OBJECT, WA_INTERFACE, "Presence",
                this, SLOT(onPresence(QDBusMessage)));

    QDBusPendingCallWatcher *watcher = asyncCall("Status");
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<QString, QString> reply = *w;
        if (reply.isError())
        {
            Utilities::logData("Status(): " + reply.error().message());
            onStateChanged("disconnected");
            return;
        }
        m_jid = reply.argumentAt<1>();
        onStateChanged(reply.argumentAt<0>());
    });
}

void WAConnection::replayQueued()
{
    fireAndForget("ReplayQueued", QVariantList());
}

void WAConnection::onStateChanged(const QString &state)
{
    Utilities::logData("WhatsApp connection state: " + state);

    if (state == "connected" && m_jid.isEmpty())
    {
        // Just linked: learn our own number
        QDBusPendingCallWatcher *watcher = asyncCall("Status");
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
            QDBusPendingReply<QString, QString> reply = *w;
            if (!reply.isError())
            {
                m_jid = reply.argumentAt<1>();
                emit stateChanged(m_state);
            }
        });
    }
    else if (state == "logged-out")
        m_jid.clear();

    m_state = state;
    emit stateChanged(state);
}

void WAConnection::onQRCode(const QString &code)
{
    emit qrCode(code);
}

// --- Linking -------------------------------------------------------------

void WAConnection::requestQR()
{
    QDBusPendingCallWatcher *watcher = asyncCall("GetQR", QVariantList(), LINK_TIMEOUT);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<QString> reply = *w;
        if (reply.isError())
            emit linkError(reply.error().message());
        else
            emit qrCode(reply.value());
    });
}

void WAConnection::pairPhone(const QString &phone)
{
    QDBusPendingCallWatcher *watcher = asyncCall("PairPhone", QVariantList() << phone, LINK_TIMEOUT);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<QString> reply = *w;
        if (reply.isError())
            emit linkError(reply.error().message());
        else
            emit pairingCode(reply.value());
    });
}

void WAConnection::logout()
{
    QDBusPendingCallWatcher *watcher = asyncCall("Logout", QVariantList(), LINK_TIMEOUT);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        if (w->isError())
            emit linkError(w->error().message());
        else
            onStateChanged("logged-out");
    });
}

// --- Messages ------------------------------------------------------------

void WAConnection::sendFailed(const FMessage &message)
{
    emit receiptReceived(message.key.remote_jid, QString(), QStringList(message.key.id),
                         "error", QDateTime::currentMSecsSinceEpoch() / 1000);
}

void WAConnection::sendMessage(const FMessage &message)
{
    QString jid = message.key.remote_jid;

    switch (message.type)
    {
        case FMessage::ComposingMessage:
        case FMessage::PausedMessage:
            sendChatState(jid, message.type == FMessage::ComposingMessage);
            return;

        case FMessage::BodyMessage:
        case FMessage::MediaMessage:
        {
            QDBusPendingCallWatcher *watcher;

            if (message.type == FMessage::BodyMessage)
                watcher = asyncCall("SendText", QVariantList() << message.key.id << jid
                                    << QString::fromUtf8(message.data));
            else
                watcher = asyncCall("SendFile", QVariantList() << message.key.id << jid
                                    << message.local_file_uri << message.media_caption);

            // The daemon answers at once; the outcome comes as a Receipt
            connect(watcher, &QDBusPendingCallWatcher::finished, this,
                    [this, message](QDBusPendingCallWatcher *w) {
                if (w->isError())
                {
                    Utilities::logData("send: " + w->error().message());
                    sendFailed(message);
                }
            });
            return;
        }

        default:
            return;
    }
}

void WAConnection::sendChatState(const QString &jid, bool composing)
{
    if (isConnected())
        fireAndForget("SendChatState", QVariantList() << jid << composing);
}

void WAConnection::markRead(const QString &chat, const QString &sender, const QStringList &ids)
{
    if (!ids.isEmpty())
        fireAndForget("MarkRead", QVariantList() << chat << sender << ids);
}

void WAConnection::downloadMedia(const FMessage &message)
{
    QDBusPendingCallWatcher *watcher = asyncCall("DownloadMedia", QVariantList() << message.key.id,
                                                 LONG_TIMEOUT);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, message](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<QString> reply = *w;
        if (reply.isError())
            emit mediaDownloadFailed(message, reply.error().message());
        else
        {
            FMessage msg = message;
            msg.local_file_uri = reply.value();
            emit mediaDownloaded(msg);
        }
    });
}

void WAConnection::requestThumbnail(const FMessage &message)
{
    QDBusPendingCallWatcher *watcher = asyncCall("GetThumbnail", QVariantList() << message.key.id);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, message](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<QByteArray> reply = *w;
        if (!reply.isError() && !reply.value().isEmpty())
        {
            FMessage msg = message;
            msg.data = reply.value();
            emit thumbnailReceived(msg);
        }
    });
}

// --- Presence ------------------------------------------------------------

void WAConnection::setAvailable(bool available)
{
    if (isConnected())
        fireAndForget("SetAvailable", QVariantList() << available);
}

void WAConnection::subscribePresence(const QString &jid)
{
    if (isConnected() && jid.endsWith("@" JID_DOMAIN))
        fireAndForget("SubscribePresence", QVariantList() << jid);
}

// --- Contacts and groups -------------------------------------------------

void WAConnection::requestContacts()
{
    QDBusPendingCallWatcher *watcher = asyncCall("GetContacts");
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        QDBusMessage reply = w->reply();
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
            return;

        QList<QStringList> contacts;
        const QDBusArgument arg = reply.arguments().at(0).value<QDBusArgument>();
        arg.beginArray();
        while (!arg.atEnd())
        {
            QString jid, fullName, pushName;
            arg.beginStructure();
            arg >> jid >> fullName >> pushName;
            arg.endStructure();
            contacts.append(QStringList() << jid << fullName << pushName);
        }
        arg.endArray();

        emit contactsReceived(contacts);
    });
}

void WAConnection::requestGroups()
{
    QDBusPendingCallWatcher *watcher = asyncCall("GetGroups", QVariantList(), LONG_TIMEOUT);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        QDBusMessage reply = w->reply();
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
            return;

        QMap<QString,QString> groups;
        const QDBusArgument arg = reply.arguments().at(0).value<QDBusArgument>();
        arg.beginArray();
        while (!arg.atEnd())
        {
            QString jid, subject;
            arg.beginStructure();
            arg >> jid >> subject;
            arg.endStructure();
            groups.insert(jid, subject);
        }
        arg.endArray();

        emit groupsReceived(groups);
    });
}

void WAConnection::requestProfilePicture(const QString &jid)
{
    QDBusPendingCallWatcher *watcher = asyncCall("GetProfilePicture", QVariantList() << jid,
                                                 LONG_TIMEOUT);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, jid](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<QString> reply = *w;
        if (!reply.isError())
            emit profilePictureReceived(jid, reply.value());
    });
}

// --- Signals of the daemon -----------------------------------------------

// A vCard as one line: "Name, +43 660 1234567"
static QString contactSummary(const QString &vcard)
{
    QString name, phone;

    foreach (QString line, vcard.split(QRegExp("\r?\n")))
    {
        if (name.isEmpty() && line.startsWith("FN", Qt::CaseInsensitive) && line.contains(':'))
            name = line.mid(line.indexOf(':') + 1).trimmed();
        else if (phone.isEmpty() && line.startsWith("TEL", Qt::CaseInsensitive) && line.contains(':'))
            phone = line.mid(line.lastIndexOf(':') + 1).trimmed();
    }

    QString text = "[contact] " + name;
    if (!phone.isEmpty())
        text += (name.isEmpty() ? "" : ", ") + phone;
    return text;
}

FMessage WAConnection::messageFromSignal(const QVariantList &args, bool history)
{
    QString id = args.value(0).toString();
    QString chat = args.value(1).toString();
    QString sender = args.value(2).toString();
    QString pushName = args.value(3).toString();
    qint64 time = args.value(4).toLongLong();
    bool fromMe = args.value(5).toBool();
    QString kind = args.value(6).toString();
    QString text = args.value(7).toString();
    QString mime = args.value(8).toString();
    qint64 size = args.value(9).toLongLong();

    FMessage msg(chat, fromMe, id);

    msg.timestamp = time * 1000;
    msg.notify_name = pushName;
    msg.media_mime_type = mime;
    msg.media_size = size;

    if (chat.endsWith("@" GROUP_DOMAIN))
        msg.remote_resource = sender;

    // Incoming messages count as read (ReceivedByTarget) once the user has
    // seen them; history is not reported as read again.
    if (fromMe)
        msg.status = FMessage::ReceivedByServer;
    else
        msg.status = history ? FMessage::ReceivedByTarget : FMessage::Unsent;

    if (kind == "text")
    {
        msg.type = FMessage::BodyMessage;
        msg.setData(text);
    }
    else if (kind == "contact")
    {
        msg.type = FMessage::BodyMessage;
        msg.setData(contactSummary(text));
    }
    else
    {
        msg.type = FMessage::MediaMessage;
        msg.media_caption = text;

        if (kind == "image")
            msg.media_wa_type = FMessage::Image;
        else if (kind == "video")
            msg.media_wa_type = FMessage::Video;
        else if (kind == "audio")
            msg.media_wa_type = FMessage::Audio;
        else if (kind == "voice")
        {
            msg.media_wa_type = FMessage::Audio;
            msg.live = true;
        }
        else if (kind == "document")
            msg.media_wa_type = FMessage::Document;
        else if (kind == "sticker")
            msg.media_wa_type = FMessage::Sticker;
        else if (kind == "location")
        {
            msg.media_wa_type = FMessage::Location;
            msg.media_caption.clear();

            QStringList coords = text.mid(text.indexOf(':') + 1).split(',');
            msg.latitude = coords.value(0).toDouble();
            msg.longitude = coords.value(1).toDouble();
            msg.media_name = QString::number(msg.latitude, 'f', 5) + ", " +
                             QString::number(msg.longitude, 'f', 5);
        }
        else
            msg.media_wa_type = FMessage::Undefined;
    }

    return msg;
}

void WAConnection::onMessage(const QDBusMessage &msg)
{
    if (msg.arguments().size() >= 10)
        emit messageReceived(messageFromSignal(msg.arguments(), false));
}

void WAConnection::onHistory(const QDBusMessage &msg)
{
    if (msg.arguments().size() >= 10)
        emit historyReceived(messageFromSignal(msg.arguments(), true));
}

void WAConnection::onReceipt(const QDBusMessage &msg)
{
    QVariantList args = msg.arguments();
    if (args.size() < 5)
        return;

    QStringList ids;
    QVariant list = args.at(2);
    if (list.canConvert<QDBusArgument>())
        list.value<QDBusArgument>() >> ids;
    else
        ids = list.toStringList();

    emit receiptReceived(args.at(0).toString(), args.at(1).toString(), ids,
                         args.at(3).toString(), args.at(4).toLongLong());
}

void WAConnection::onChatState(const QDBusMessage &msg)
{
    QVariantList args = msg.arguments();
    if (args.size() < 3)
        return;

    QString chat = args.at(0).toString();
    QString sender = args.at(1).toString();
    QString participant = chat.endsWith("@" GROUP_DOMAIN) ? sender : QString();

    if (args.at(2).toBool())
        emit composing(chat, participant, "text");
    else
        emit paused(chat, participant);
}

void WAConnection::onPresence(const QDBusMessage &msg)
{
    QVariantList args = msg.arguments();
    if (args.size() < 3)
        return;

    emit presenceChanged(args.at(0).toString(), args.at(1).toBool(),
                         args.at(2).toLongLong());
}
