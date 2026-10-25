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

#ifndef WACONNECTION_H
#define WACONNECTION_H

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QDBusMessage>
#include <QMap>

#include "FMessage/fmessage.h"

class QDBusPendingCallWatcher;

/**
    @class      WAConnection

    @brief      Client of the yappari-wa daemon (org.yappari.WhatsApp on the
                session bus), which keeps the connection to WhatsApp.

                It takes the place of Yappari's former Connection class,
                which spoke the FunXMPP protocol itself. Every call is
                asynchronous; results arrive as signals. The daemon is
                started by D-Bus activation on the first call.
*/

class WAConnection : public QObject
{
    Q_OBJECT

public:
    explicit WAConnection(QObject *parent = 0);

    // Daemon state: logged-out, connecting, connected, disconnected
    QString state() const;
    QString ownJid() const;
    bool isConnected() const;

    // Subscribes to the daemon's signals and asks for its state
    void start();

    // Hands over the signals the daemon queued while the UI was closed
    void replayQueued();

    // Linking
    void requestQR();
    void pairPhone(const QString &phone);
    void logout();

    // Messages
    void sendMessage(const FMessage &message);
    void sendChatState(const QString &jid, bool composing);
    void markRead(const QString &chat, const QString &sender, const QStringList &ids);
    void downloadMedia(const FMessage &message);
    void requestThumbnail(const FMessage &message);

    // Presence
    void setAvailable(bool available);
    void subscribePresence(const QString &jid);

    // Contacts and groups
    void requestContacts();
    void requestGroups();
    void requestProfilePicture(const QString &jid);

    // Builds a message from the arguments of a Message or History signal
    static FMessage messageFromSignal(const QVariantList &args, bool history);

signals:
    void stateChanged(QString state);
    void qrCode(QString code);
    void pairingCode(QString code);
    void linkError(QString error);

    void messageReceived(FMessage message);
    void historyReceived(FMessage message);
    void receiptReceived(QString chat, QString sender, QStringList ids,
                         QString type, qint64 timestamp);
    void composing(QString jid, QString participant, QString media);
    void paused(QString jid, QString participant);
    void presenceChanged(QString jid, bool available, qint64 lastSeen);

    // Each entry: jid, full name (address book of the main phone), push name
    void contactsReceived(QList<QStringList> contacts);
    // jid -> subject
    void groupsReceived(QMap<QString,QString> groups);
    void profilePictureReceived(QString jid, QString path);
    void thumbnailReceived(FMessage message);
    void mediaDownloaded(FMessage message);
    void mediaDownloadFailed(FMessage message, QString error);

private slots:
    void onStateChanged(const QString &state);
    void onQRCode(const QString &code);
    void onMessage(const QDBusMessage &msg);
    void onHistory(const QDBusMessage &msg);
    void onReceipt(const QDBusMessage &msg);
    void onChatState(const QDBusMessage &msg);
    void onPresence(const QDBusMessage &msg);

private:
    QString m_state;
    QString m_jid;

    QDBusMessage methodCall(const QString &method, const QVariantList &args = QVariantList());
    QDBusPendingCallWatcher *asyncCall(const QString &method,
                                       const QVariantList &args = QVariantList(),
                                       int timeout = -1);
    void fireAndForget(const QString &method, const QVariantList &args);
    void sendFailed(const FMessage &message);
};

#endif // WACONNECTION_H
