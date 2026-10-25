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

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMap>
#include <QTimer>
#include <QString>
#include <QPointer>
#include <QMainWindow>

#include "FMessage/fmessage.h"

#include "Contacts/contact.h"
#include "Contacts/contactroster.h"

#include "Dbus/notifyobject.h"

#include "Sql/conversationsdb.h"

#include "Gui/chatdisplayitem.h"
#include "Gui/contactselectionmodel.h"
#include "Gui/chatwindow.h"

namespace Ui {
    class MainWindow;
}

class LinkWindow;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(ContactRoster *roster, QWidget *parent = 0);
    ~MainWindow();

    void setActiveChat(QString jid);
    bool hasChatOpen(Contact *c);

protected:
    void closeEvent(QCloseEvent *event);
    bool eventFilter(QObject *obj, QEvent *event);

public slots:
    void createChatWindow();
    void messageReceived(FMessage message);
    void historyLogged(QString jid);
    void updateGroup(Group& group, bool notify);
    void sendMessageFromChat(FMessage message);
    void deleteChat(QObject *obj);
    void messageStatusUpdate(FMessage message);
    void readElsewhere(QString jid);
    void quit();
    void contactSelected(QModelIndex index);
    void showAboutDialog();
    void showGlobalSettingsDialog();
    void showAccount();
    void showLinkWindow();
    void connectionReady();
    void available(QString jid, bool online);
    void available(QString jid, qint64 lastSeen);
    void composing(QString jid, QString participant, QString media);
    void paused(QString jid, QString participant);
    void requestSync();
    void contactsUpdated();
    void contextMenu(QPoint p);
    void mute(QString jid, bool muted, qint64 muteExpireTimestamp);
    void updateTimestamps();
    void updatePhoto(Contact& c);
    void requestPhotoUpdate(QString jid, QString photoId, bool largeFormat);
    void sendMessageRead(FMessage message);
    void requestUpdateLastDir(int waType, QString dir);
    void forwardMessage(FMessage message);
    void requestMediaDownload(FMessage message);
    void mediaDownloaded(FMessage message);
    void mediaDownloadFailed(FMessage message, QString error);
    void thumbnailReceived(FMessage message);

private:
    Ui::MainWindow *ui;

    // This is the list of the lastContactList contacts that have an
    // chat window open
    QMap<QString,ChatWindow *> chatWindowList;

    // This is the list of the contacts with ongoing conversations
    // aka the contact list in the MainWindow
    QMap<QString,ChatDisplayItem *> lastContactsList;

    ContactRoster *roster;
    NotifyObject *notifyObject;
    ConversationsDB chatsDB;
    ContactSelectionModel *model;
    QTimer *newDayTimer;
    QPointer<LinkWindow> linkWindow;

    void loadOpenChats();
    ChatDisplayItem *addChatToList(Contact& contact);
    ChatWindow *createChatWindow(Contact& contact, bool show);
    Contact& chatContact(QString jid);
    void notify(const Contact& contact,FMessage& message);
    void showWindow(QWidget *window);
    void resetNewDayTimer();

signals:
    void sendMessage(FMessage message);
    void sendNotification(QString name, FMessage message);
    void activeChatChanged(QString jid);
    void sync();
    void sendRightButtonClicked(const QPoint& p);
    void settingsUpdated();
    void queryLastOnline(QString jid);
    void subscribe(QString jid);
    void photoRequest(QString jid, QString expectedPhotoId, bool largeFormat);
    void onlineStatusChanged(QString jid);
    void messageRead(FMessage message);
    void updateLastDir(int waType, QString dir);
};

#endif // MAINWINDOW_H
