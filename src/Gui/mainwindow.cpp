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

#include <QApplication>
#include <QMaemo5InformationBox>
#include <QMessageBox>
#include <QPushButton>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QScroller>
#include <QGestureEvent>
#include <QTapAndHoldGesture>
#include <QMenu>

#include "Contacts/group.h"

#include "aboutdialog.h"
#include "chatdisplaydelegate.h"
#include "globalsettingsdialog.h"
#include "linkwindow.h"
#include "chatwindow.h"
#include "selectcontactdialog.h"

#include "globalconstants.h"

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "util/utilities.h"

#include "Sql/chatlogger.h"

#include "client.h"

MainWindow::MainWindow(ContactRoster *roster, QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // Configure the main listView

    model = new ContactSelectionModel(ui->listView);
    model->setSortRole(Qt::UserRole + 2);
    ui->listView->setModel(model);
    ui->listView->installEventFilter(this);
    ui->listView->setContextMenuPolicy(Qt::DefaultContextMenu);
    QScroller::grabGesture(ui->listView->viewport(), QScroller::LeftMouseButtonGesture);

    // Tap and hold opens the context menu, as on Maemo 5
    ui->listView->viewport()->grabGesture(Qt::TapAndHoldGesture);
    ui->listView->viewport()->installEventFilter(this);

    ChatDisplayDelegate *delegate = new ChatDisplayDelegate(ui->listView);

    ui->listView->setItemDelegate(delegate);

    connect(ui->createChatButton,SIGNAL(clicked()),this,SLOT(createChatWindow()));
    connect(ui->actionSettings,SIGNAL(triggered()),this,SLOT(showGlobalSettingsDialog()));
    connect(ui->actionAccount,SIGNAL(triggered()),this,SLOT(showAccount()));
    connect(ui->actionQuit,SIGNAL(triggered()),this,SLOT(quit()));
    connect(ui->actionAbout,SIGNAL(triggered()),this,SLOT(showAboutDialog()));
    connect(ui->actionSync,SIGNAL(triggered()),this,SLOT(requestSync()));

    connect(ui->listView,SIGNAL(clicked(QModelIndex)),
            this,SLOT(contactSelected(QModelIndex)));

    connect(this,SIGNAL(sendRightButtonClicked(QPoint)),
            this,SLOT(contextMenu(QPoint)));

    this->roster = roster;

    // Stacked window root (Hildon window stack of the Leste Qt platform plugin)
    setProperty("X-Maemo-StackedWindow", 1);
    setAttribute(Qt::WA_DeleteOnClose,false);
    setAttribute(Qt::WA_QuitOnClose,false);

    notifyObject = new NotifyObject(this);
    connect(this,SIGNAL(sendNotification(QString,FMessage)),
            notifyObject,SLOT(sendNotify(QString,FMessage)));

    newDayTimer = new QTimer(this);
    connect(newDayTimer,SIGNAL(timeout()),this,SLOT(updateTimestamps()));
    resetNewDayTimer();

    loadOpenChats();
}

MainWindow::~MainWindow()
{
    delete ui;

    Utilities::logData("MainWindow destroyed");
}

// The roster entry of a chat: a Group for group chats
Contact& MainWindow::chatContact(QString jid)
{
    if (jid.endsWith("@" GROUP_DOMAIN))
        return roster->getGroup(jid);

    return roster->getContact(jid);
}

void MainWindow::loadOpenChats()
{
    QList<ConversationsDBEntry> list = chatsDB.getAllChats();

    QListIterator<ConversationsDBEntry> i(list);

    while(i.hasNext())
    {
        ConversationsDBEntry entry = i.next();

        Contact& contact = chatContact(entry.jid);
        contact.hasOpenChat = true;
        ChatDisplayItem *item = new ChatDisplayItem(&contact);
        lastContactsList.insert(entry.jid,item);
        item->muted = entry.muted;
        item->muteExpireTimestamp = entry.muteExpireTimestamp;
        model->appendRow(item);

        // Set the last line logged in the item
        FMessage msg = ChatLogger::lastMessage(entry.jid);
        item->updateData(msg);
    }
    model->sort(0,Qt::DescendingOrder);
}

// Adds a conversation to the list of chats in the main window
ChatDisplayItem *MainWindow::addChatToList(Contact& contact)
{
    QString jid = contact.jid;

    if (lastContactsList.contains(jid))
        return lastContactsList.value(jid);

    ChatDisplayItem *item = new ChatDisplayItem(&contact);
    lastContactsList.insert(jid,item);
    model->appendRow(item);

    contact.hasOpenChat = true;

    FMessage msg = ChatLogger::lastMessage(jid);
    item->updateData(msg);

    ConversationsDBEntry entry;
    entry.jid = jid;
    entry.muted = false;
    entry.muteExpireTimestamp = 0;
    chatsDB.createChat(entry);

    emit photoRequest(jid, contact.photoId, false);

    return item;
}

void MainWindow::createChatWindow()
{
    ContactList list;

    // People with a name (address book of the main phone or push name) and groups
    foreach (Contact *c, roster->getContactList())
    {
        if (c->jid == Client::myJid)
            continue;
        if (c->type == Contact::TypeGroup ? !c->name.isEmpty()
                                          : (c->fromAddressBook || !c->alias.isEmpty()))
            list.insert(c->jid, c);
    }

    if (list.isEmpty())
    {
        QMaemo5InformationBox::information(this,"WhatsApp hasn't sent your contacts yet.",
                                           QMaemo5InformationBox::DefaultTimeout);
    }
    else
    {
        SelectContactDialog selectContactDialog(list,this,false,true);

        if (selectContactDialog.exec() == QDialog::Accepted)
        {
            Contact& contact = selectContactDialog.getSelectedContact();

            createChatWindow(contact, true);
        }
    }
}

ChatWindow *MainWindow::createChatWindow(Contact& contact, bool show)
{
    ChatWindow *chat;

    QString jid = contact.jid;

    if (!chatWindowList.contains(jid))
    {
        chat = new ChatWindow(&contact,this);

        chatWindowList.insert(jid,chat);

        if (!lastContactsList.contains(jid))
        {
            // This is a new chat and it is not in the
            // open chats list
            addChatToList(contact);

            if (contact.type == Contact::TypeContact)
                emit subscribe(jid);
        }
        else
        {
            // This chat is in the open chats list

            // Configure mute settings
            ChatDisplayItem *item = lastContactsList.value(jid);
            if (item->muted)
                chat->setMute(item->muteExpireTimestamp);
        }

        chat->setAttribute(Qt::WA_DeleteOnClose);

        connect(chat,SIGNAL(sendMessage(FMessage)),
                this,SLOT(sendMessageFromChat(FMessage)));

        connect(chat,SIGNAL(destroyed(QObject *)),
                this,SLOT(deleteChat(QObject *)));

        connect(chat,SIGNAL(mute(QString,bool,qint64)),
                this,SLOT(mute(QString,bool,qint64)));

        connect(chat,SIGNAL(messageRead(FMessage)),
                this,SLOT(sendMessageRead(FMessage)));

        connect(chat,SIGNAL(updateLastDir(int,QString)),
                this,SLOT(requestUpdateLastDir(int,QString)));

        connect(chat,SIGNAL(forwardMessage(FMessage)),
                this,SLOT(forwardMessage(FMessage)));

        connect(chat,SIGNAL(mediaDownload(FMessage)),
                this,SLOT(requestMediaDownload(FMessage)));

        if (contact.type == Contact::TypeContact)
            emit queryLastOnline(jid);

        if (show)
            chat->show();
    }
    else
    {
        chat = chatWindowList.value(jid);
        if (show)
            setActiveChat(jid);
    }

    chat->setContact(&contact);

    return chat;
}

void MainWindow::deleteChat(QObject *obj)
{
    // Called from QObject's destructor: only compare pointers
    foreach (QString jid, chatWindowList.keys())
        if (chatWindowList.value(jid) == obj)
            chatWindowList.remove(jid);
}

void MainWindow::setActiveChat(QString jid)
{
    if (!chatWindowList.contains(jid) && lastContactsList.contains(jid))
        createChatWindow(chatContact(jid), false);

    if (chatWindowList.contains(jid))
    {
        ChatWindow *chat = chatWindowList.value(jid);
        chat->show();
        chat->raise();
        chat->activateWindow();
        emit activeChatChanged(jid);
    }
}

void MainWindow::updateGroup(Group &group, bool notify)
{
    if (lastContactsList.contains(group.jid))
    {
        ChatDisplayItem *item = lastContactsList.value(group.jid);
        item->updateData();
    }

    if (chatWindowList.contains(group.jid))
    {
        ChatWindow *chat = chatWindowList.value(group.jid);
        chat->setContact(&group);
    }

    if (notify)
        QMaemo5InformationBox::information(this, "Group " + group.name);
}

void MainWindow::messageReceived(FMessage message)
{
    ChatWindow *chat;

    QString jid = message.key.remote_jid;
    bool isGroup = jid.endsWith("@" GROUP_DOMAIN);
    QString authorJid = isGroup ? message.remote_resource : jid;

    // Contact who sent the message
    if (!message.key.from_me && !authorJid.isEmpty())
    {
        Contact& contact = roster->getContact(authorJid);

        // Update contact alias (the push name)
        if (!message.notify_name.isEmpty() && contact.alias != message.notify_name)
        {
            contact.alias = message.notify_name;
            roster->updateAlias(&contact);
        }

        // Update contact name if is empty with the alias
        if (contact.name.isEmpty() ||
                (!contact.fromAddressBook && !contact.alias.isEmpty() && contact.name != contact.alias))
        {
            contact.name = contact.alias.isEmpty() ? contact.phone : contact.alias;
            roster->updateName(&contact);
        }

        // If this is from a contact we don't have in the address book
        // or if the show nicknames settings is enabled then
        // let's use the alias
        message.notify_name = ((Client::showNicknames || !contact.fromAddressBook) &&
                               !contact.alias.isEmpty())
                                ? contact.alias
                                : contact.name;
    }

    Contact& chatPeer = chatContact(jid);

    // Check if a window for this chat is already open
    if (!chatWindowList.contains(jid))
    {
        // Create a new window for this chat
        chat = createChatWindow(chatPeer, Client::popupOnFirstMessage && !message.key.from_me);

        if (!message.key.from_me)
            notify(chatPeer,message);
    }
    else
    {
        chat = chatWindowList.value(jid);
        if (!message.key.from_me && !chat->isActiveWindow())
            notify(chatPeer,message);
    }

    chat->messageReceived(message);
    ChatDisplayItem *item = addChatToList(chatPeer);
    item->updateData(message);
    model->sort(0,Qt::DescendingOrder);
}

// Older messages of a chat were stored (history sync after linking)
void MainWindow::historyLogged(QString jid)
{
    ChatDisplayItem *item = addChatToList(chatContact(jid));

    FMessage msg = ChatLogger::lastMessage(jid);
    item->updateData(msg);
    model->sort(0,Qt::DescendingOrder);
}

void MainWindow::sendMessageFromChat(FMessage message)
{
    // Update the open conversations list only on text
    // and multimedia messages
    if (message.type == FMessage::BodyMessage ||
        message.type == FMessage::MediaMessage)
    {
        ChatDisplayItem *item = addChatToList(chatContact(message.key.remote_jid));
        item->updateData(message);
        model->sort(0,Qt::DescendingOrder);
    }
    emit sendMessage(message);
}

void MainWindow::messageStatusUpdate(FMessage message)
{
    if (!chatWindowList.contains(message.key.remote_jid))
    {
        ChatLogger logger;
        logger.init(message.key.remote_jid);
        logger.updateLoggedMessage(message);
    }
    else
    {
       ChatWindow *chat = chatWindowList.value(message.key.remote_jid);
       chat->messageStatusUpdate(message);
    }
}

// The chat was read on another device of ours
void MainWindow::readElsewhere(QString jid)
{
    notifyObject->closeNotification(jid);
}

void MainWindow::quit()
{
    Utilities::logData("User has quit the application");

    qApp->quit();
}

void MainWindow::contactSelected(QModelIndex index)
{
    QString jid = index.data(Qt::UserRole + 1).toString();

    createChatWindow(chatContact(jid), true);

    ui->listView->clearSelection();
}

void MainWindow::notify(const Contact& contact, FMessage& message)
{
    QString jid = message.key.remote_jid;

    if (lastContactsList.contains(jid))
    {
        ChatDisplayItem *item = lastContactsList.value(jid);
        if (item->muted && item->muteExpireTimestamp != 0 &&
                item->muteExpireTimestamp < QDateTime::currentMSecsSinceEpoch())
        {
            item->muted = false;
            if (chatWindowList.contains(jid))
            {
                ChatWindow *chat = chatWindowList.value(jid);
                chat->unmute();
            }
        }

        if (item->muted)
            return;
    }

    QString name;

    if (contact.type == Contact::TypeGroup)
    {
        // "Author @ Group"
        name = message.notify_name + " @ " +
               (contact.name.isEmpty() ? QString("group") : contact.name);
    }
    else
    {
        name = ((Client::showNicknames || !contact.fromAddressBook) && !contact.alias.isEmpty())
                ? contact.alias
                : (contact.name.isEmpty() ? contact.phone : contact.name);
    }

    emit sendNotification(name,message);
}

void MainWindow::showAboutDialog()
{
    AboutDialog dialog(this);

    dialog.exec();
}

void MainWindow::available(QString jid, bool online)
{
    if (roster->isContactInRoster(jid))
    {
        Contact& c = roster->getContact(jid);
        c.setOnlineStatus(online);

        if (chatWindowList.contains(jid))
        {
            ChatWindow *chat = chatWindowList.value(jid);
            chat->available(online,c.lastSeen);
        }

        roster->updateLastSeen(&c);
    }

    emit onlineStatusChanged(jid);
}

void MainWindow::available(QString jid, qint64 lastSeen)
{
    if (roster->isContactInRoster(jid))
    {
        Contact& c = roster->getContact(jid);
        c.lastSeen = lastSeen;

        if (chatWindowList.contains(jid))
        {
            ChatWindow *chat = chatWindowList.value(jid);
            chat->available(c.isOnline,c.lastSeen);
        }

        roster->updateLastSeen(&c);
    }

    emit onlineStatusChanged(jid);
}


void MainWindow::composing(QString jid, QString participant, QString media)
{
    if (chatWindowList.contains(jid))
    {
        ChatWindow *chat = chatWindowList.value(jid);
        if (participant.isEmpty()) {
            chat->composing("", media);
        } else {
            Contact& c = roster->getContact(participant);
            chat->composing(c.name.isEmpty() ? c.phone : c.name, media);
        }
    }
}


void MainWindow::paused(QString jid, QString participant)
{
    if (chatWindowList.contains(jid))
    {
        ChatWindow *chat = chatWindowList.value(jid);
        chat->paused(participant);
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Yappari keeps running in the background; Quit is in the menu
    hide();
    event->ignore();
}

void MainWindow::showGlobalSettingsDialog()
{
    GlobalSettingsDialog dialog(this);

    if (dialog.exec() == QDialog::Accepted)
    {
        Client::showNicknames = dialog.getShowNicknames();
        Client::showNumbers = dialog.getShowNumbers();
        Client::popupOnFirstMessage = dialog.getPopupOnFirstMessage();
        Client::blueChecks = dialog.getBlueChecks();
        Client::automaticDownloadBytes = dialog.getAutomaticDownloadBytes();
        Client::enterIsSend = dialog.getEnterIsSend();

        emit settingsUpdated();
    }
}

void MainWindow::showAccount()
{
    if (Client::connection->state() == "logged-out")
    {
        showLinkWindow();
        return;
    }

    QString number = Client::myJid.left(Client::myJid.indexOf('@'));

    QMessageBox msg(this);
    msg.setWindowTitle("Account");
    msg.setText("Yappari is linked to the WhatsApp account of +" + number +
                " as a linked device.\n\nUnlinking removes this device from the "
                "account; your chats stay on this phone.");
    QPushButton *unlink = msg.addButton("Unlink", QMessageBox::DestructiveRole);
    msg.addButton("Close", QMessageBox::RejectRole);
    msg.exec();

    if (msg.clickedButton() == unlink)
    {
        QMessageBox confirm(this);
        confirm.setText("Unlink Yappari from WhatsApp?");
        confirm.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);

        if (confirm.exec() == QMessageBox::Yes)
            Client::connection->logout();
    }
}

void MainWindow::showLinkWindow()
{
    if (!isVisible())
        show();

    if (linkWindow.isNull())
    {
        linkWindow = new LinkWindow(this);
        showWindow(linkWindow);
    }
    else
    {
        linkWindow->show();
        linkWindow->activateWindow();
    }
}

void MainWindow::connectionReady()
{
    // Profile pictures of the chats in the list, presence of open chats
    foreach (QString jid, lastContactsList.keys())
    {
        Contact &c = chatContact(jid);
        emit photoRequest(jid, c.photoId, false);
    }

    foreach (QString jid, chatWindowList.keys())
        if (!jid.endsWith("@" GROUP_DOMAIN))
            emit subscribe(jid);
}

void MainWindow::requestSync()
{
    if (Client::connectionStatus == Client::LoggedIn)
    {
        QMaemo5InformationBox::information(this,"Contacts will be refreshed in the background.");
        emit sync();
    }
    else
    {
        QMaemo5InformationBox::information(this,"You need to be connected to refresh your contacts",
                                           QMaemo5InformationBox::DefaultTimeout);
    }
}

// Names changed: redraw the chat list and window titles
void MainWindow::contactsUpdated()
{
    foreach (QString jid, lastContactsList.keys())
        lastContactsList.value(jid)->updateData();

    foreach (QString jid, chatWindowList.keys())
    {
        ChatWindow *chat = chatWindowList.value(jid);
        chat->setContact(&chatContact(jid));
    }
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::Gesture) {
        QGestureEvent *gestureEvent = static_cast<QGestureEvent*> (event);
        QGesture *gesture = gestureEvent->gesture(Qt::TapAndHoldGesture);
        if (gesture)
        {
            gestureEvent->accept(gesture);
            if (gesture->state() == Qt::GestureFinished)
            {
                QPoint pos = static_cast<QTapAndHoldGesture*>(gesture)->position().toPoint();
                emit sendRightButtonClicked(pos);
            }
            return true;
        }
    }
    else if (event->type() == QEvent::ContextMenu) {
        QContextMenuEvent *menuEvent = static_cast<QContextMenuEvent*> (event);
        emit sendRightButtonClicked(menuEvent->globalPos());
        return true;
    }
    else
        return QObject::eventFilter(obj, event);
}

void MainWindow::contextMenu(QPoint p)
{
    QModelIndex index = ui->listView->indexAt(ui->listView->viewport()->mapFromGlobal(p));

    if (index.isValid())
    {
        QString jid = index.data(Qt::UserRole + 1).toString();

        Contact& c = chatContact(jid);

        QMenu *menu = new QMenu(this);
        QAction *deleteChat = new QAction("Delete Chat",this);
        menu->addAction(deleteChat);

        QAction *action = menu->exec(p);
        if (action == deleteChat)
        {
            QMessageBox msg(this);

            msg.setText("Are you sure you want to delete this chat?\n"
                        "The messages stay stored and come back with the next message.");
            msg.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);

            if (msg.exec() == QMessageBox::Yes)
            {
                if (chatWindowList.contains(jid))
                {
                    ChatWindow *chat = chatWindowList.value(jid);
                    chatWindowList.remove(jid);
                    chat->close();
                }

                model->removeRow(index.row());
                lastContactsList.remove(jid);
                chatsDB.removeChat(jid);

                c.hasOpenChat = false;
            }
        }
        menu->deleteLater();
        ui->listView->clearSelection();
    }
}

void MainWindow::mute(QString jid, bool muted, qint64 muteExpireTimestamp)
{
    if (lastContactsList.contains(jid))
    {
        ChatDisplayItem *item = lastContactsList.value(jid);
        item->muted = muted;
        item->muteExpireTimestamp = muteExpireTimestamp;

        ConversationsDBEntry entry;
        entry.jid = jid;
        entry.muted = muted;
        entry.muteExpireTimestamp = muteExpireTimestamp;
        chatsDB.updateChat(entry);
    }
}

bool MainWindow::hasChatOpen(Contact *c)
{
    return lastContactsList.contains(c->jid);
}

void MainWindow::updateTimestamps()
{
    newDayTimer->stop();

    // Update Timestamps in the Open Chats list.
    foreach (QString key, lastContactsList.keys())
    {
        ChatDisplayItem *item = lastContactsList.value(key);
        item->updateData();
    }

    // Update Timestamps in open conversation windows
    foreach (QString jid, chatWindowList.keys())
    {
        ChatWindow *chat = chatWindowList.value(jid);
        chat->updateTimestamps();
    }

    resetNewDayTimer();
}

void MainWindow::resetNewDayTimer()
{
    QDateTime now = QDateTime::currentDateTime();
    QDateTime tomorrow = QDateTime(now.date().addDays(1), QTime(0, 0));
    qint64 nextRunTime = tomorrow.toMSecsSinceEpoch() -
                         now.toMSecsSinceEpoch();

    newDayTimer->start(nextRunTime);
}

void MainWindow::updatePhoto(Contact& c)
{
    if (lastContactsList.contains(c.jid))
    {
        ChatDisplayItem *item = lastContactsList.value(c.jid);
        item->updatePhoto(&c);
    }
}

void MainWindow::showWindow(QWidget *window)
{
    window->setProperty("X-Maemo-StackedWindow", 1);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->setWindowFlags(window->windowFlags() | Qt::Window);
    window->show();
}

void MainWindow::requestPhotoUpdate(QString jid, QString photoId, bool largeFormat)
{
    emit photoRequest(jid, photoId, largeFormat);
}

void MainWindow::sendMessageRead(FMessage message)
{
    emit messageRead(message);
}

void MainWindow::requestUpdateLastDir(int waType, QString dir)
{
    emit updateLastDir(waType, dir);
}

void MainWindow::requestMediaDownload(FMessage message)
{
    Client::connection->downloadMedia(message);
}

void MainWindow::mediaDownloaded(FMessage message)
{
    QString jid = message.key.remote_jid;

    if (chatWindowList.contains(jid))
        chatWindowList.value(jid)->mediaDownloadFinished(message);
    else
    {
        ChatLogger logger;
        logger.init(jid);
        logger.updateUriMessage(message);
    }
}

void MainWindow::mediaDownloadFailed(FMessage message, QString error)
{
    QString jid = message.key.remote_jid;

    if (chatWindowList.contains(jid))
        chatWindowList.value(jid)->mediaDownloadError(message, error);
}

void MainWindow::thumbnailReceived(FMessage message)
{
    QString jid = message.key.remote_jid;

    if (chatWindowList.contains(jid))
        chatWindowList.value(jid)->thumbnailReceived(message);
    else
    {
        ChatLogger logger;
        logger.init(jid);
        logger.updateMediaData(message);
    }
}

void MainWindow::forwardMessage(FMessage message)
{
    ContactList list;

    foreach (QString jid, lastContactsList.keys())
    {
        Contact& c = chatContact(jid);
        list.insert(jid, &c);
    }

    SelectContactDialog selectContactDialog(list, this, false, true);

    if (selectContactDialog.exec() == QDialog::Accepted)
    {
        Contact& contact = selectContactDialog.getSelectedContact();

        // This will re-open the chat window if it wasn't open yet
        ChatWindow *chat = createChatWindow(contact, true);

        switch(message.type)
        {
            case FMessage::BodyMessage:
            {
                FMessage msg(contact.jid, true);
                msg.type = FMessage::BodyMessage;
                msg.data = message.data;
                chat->sendTextMessage(msg);
                break;
            }

            case FMessage::MediaMessage:
                if (!message.local_file_uri.isEmpty())
                    chat->sendMultimediaMessage(message.local_file_uri, message.media_wa_type,
                                                false, message.media_caption);
                else
                    QMaemo5InformationBox::information(this, "Download the file first to forward it");
                break;

            default:
                break;
        }
    }
}
