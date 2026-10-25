/* Copyright 2013 Naikel Aparicio. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ''AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL EELI REILIN OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the author and should not be interpreted as representing
 * official policies, either expressed or implied, of the copyright holder.
 */

#ifndef CHATWINDOW_H
#define CHATWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QMap>

#include "FMessage/fmessage.h"

#include "Contacts/contact.h"

#include "Sql/chatlogger.h"

namespace Ui {
    class ChatWindow;
}

class ChatWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit ChatWindow(Contact *contact, QWidget *parent = 0);
    ~ChatWindow();

    void messageReceived(FMessage& message);
    void sendTextMessage(FMessage &message);
    void setContact(Contact *contact);
    const Contact& getContact() const;
    void messageStatusUpdate(FMessage& message);
    void available(bool online, qint64 lastSeen);
    void composing(QString participant, QString media);
    void paused(QString participant);
    FMessage lastMessage();
    void setMute(qint64 timestamp);
    void updateTimestamps();
    void mediaDownloadFinished(FMessage msg);
    void mediaDownloadError(FMessage msg, QString error);
    void thumbnailReceived(FMessage msg);

public slots:
    void readMoreLogLines();
    void setMessagesAsRead();
    void textChanged();
    void myselfComposing(int waType = -1);
    void myselfPaused();
    void sendButtonClicked();
    void selectMultimediaMessage();
    void sendMultimediaMessage(QString fileName, int waType, bool live, QString caption);
    void mediaDownloadRequested(FMessage msg);
    void deleteAllMessages();
    void mute();
    void unmute();
    void forwardMessageRequested(FMessage message);
    void deleteMessage(FMessage message);
    void selectEmoji();

signals:
    void logMessage(FMessage message);
    void updateLoggedMessage(FMessage message);
    void updateUriMessage(FMessage message);
    void sendMessage(FMessage message);
    void mute(QString jid,bool muted,qint64 muteExpireTimestamp);
    void messageRead(FMessage message);
    void updateLastDir(int waType, QString dir);
    void forwardMessage(FMessage message);
    void mediaDownload(FMessage message);

private:
    bool isPeerComposing;
    bool isMyselfComposing;
    ChatLogger logger;

    bool muted;
    qint64 muteExpireTimestamp;

    // Received messages not reported as read yet
    QList<FMessage> unread;

    void showMessageInUI(FMessage& message);

protected:
    Ui::ChatWindow *ui;
    Contact *contact;
    bool eventFilter(QObject *obj, QEvent *event);
    void setOnlineText(QString text);
};

#endif // CHATWINDOW_H
