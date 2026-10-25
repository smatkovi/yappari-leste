/**
 * Copyright (C) 2013 Naikel Aparicio. All rights reserved.
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
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStatusBar>
#include <QDBusConnection>
#include <QMaemo5InformationBox>

#include "version.h"
#include "client.h"
#include "globalconstants.h"

#include "Sql/chatlogger.h"

#include "util/utilities.h"

/** ***********************************************************************
 ** Static global members
 **/

// Status of the connection
Client::ConnectionStatus Client::connectionStatus;

// Global settings
QSettings *Client::settings;

// Background color of own self text
QString Client::mycolor;

// Nicknames color
QString Client::nickcolor;

// Text color
QString Client::textcolor;

// Own JID
QString Client::myJid;

// Show nicknames in conversations instead of their names from the address book
bool Client::showNicknames;

// Show users phone numbers in select contact dialog
bool Client::showNumbers;

// Pop up conversation windows when first message is received
bool Client::popupOnFirstMessage;

// Report when the other party has read messages (in both directions)
bool Client::blueChecks;

// Automatic download of media if less than this number of kilobytes
int Client::automaticDownloadBytes;

// Enter is Send
bool Client::enterIsSend;

// Last folder a file was sent from
QString Client::lastFileDir;

// Main GUI window
MainWindow *Client::mainWin;

// Roster
ContactRoster *Client::roster;

// Connection to the yappari-wa daemon
WAConnection *Client::connection;

// Ask WhatsApp for the group list at most this often
#define GROUPS_REQUEST_INTERVAL     60000

/**
    Constructs a Client object.

    @param minimized    Starts Yappari minimized if true.
    @param parent       QObject parent.
*/
Client::Client(bool minimized, QObject *parent) : QObject(parent)
{
    Utilities::logData("Yappari " FULL_VERSION);

    readSettings();

    connectionStatus = Connecting;
    lastGroupsRequest = 0;

    roster = new ContactRoster(this);
    connection = new WAConnection(this);

    // Create main window application
    mainWin = new MainWindow(roster);

    connect(mainWin,SIGNAL(settingsUpdated()),
            this,SLOT(updateSettings()));

    connect(mainWin,SIGNAL(sendMessage(FMessage)),
            this,SLOT(queueMessage(FMessage)));

    connect(mainWin,SIGNAL(messageRead(FMessage)),
            this,SLOT(sendMessageRead(FMessage)));

    connect(mainWin,SIGNAL(subscribe(QString)),
            this,SLOT(subscribe(QString)));

    connect(mainWin,SIGNAL(queryLastOnline(QString)),
            this,SLOT(subscribe(QString)));

    connect(mainWin,SIGNAL(photoRequest(QString,QString,bool)),
            this,SLOT(updatePhoto(QString,QString,bool)));

    connect(mainWin,SIGNAL(sync()),
            this,SLOT(synchronizeContacts()));

    connect(mainWin,SIGNAL(updateLastDir(int,QString)),
            this,SLOT(updateLastDir(int,QString)));

    // The daemon's signals

    connect(connection,SIGNAL(stateChanged(QString)),
            this,SLOT(stateChanged(QString)));

    connect(connection,SIGNAL(messageReceived(FMessage)),
            this,SLOT(messageReceived(FMessage)));

    connect(connection,SIGNAL(historyReceived(FMessage)),
            this,SLOT(historyReceived(FMessage)));

    connect(connection,SIGNAL(receiptReceived(QString,QString,QStringList,QString,qint64)),
            this,SLOT(receiptReceived(QString,QString,QStringList,QString,qint64)));

    connect(connection,SIGNAL(composing(QString,QString,QString)),
            mainWin,SLOT(composing(QString,QString,QString)));

    connect(connection,SIGNAL(paused(QString,QString)),
            mainWin,SLOT(paused(QString,QString)));

    connect(connection,&WAConnection::presenceChanged,
            this,[](QString jid, bool available, qint64 lastSeen) {
        mainWin->available(jid, available);
        if (!available && lastSeen > 0)
            mainWin->available(jid, lastSeen * 1000);
    });

    connect(connection,&WAConnection::contactsReceived,
            this,&Client::contactsReceived);

    connect(connection,&WAConnection::groupsReceived,
            this,&Client::groupsReceived);

    connect(connection,SIGNAL(profilePictureReceived(QString,QString)),
            this,SLOT(profilePictureReceived(QString,QString)));

    connect(connection,SIGNAL(thumbnailReceived(FMessage)),
            mainWin,SLOT(thumbnailReceived(FMessage)));

    connect(connection,SIGNAL(mediaDownloaded(FMessage)),
            mainWin,SLOT(mediaDownloaded(FMessage)));

    connect(connection,SIGNAL(mediaDownloadFailed(FMessage,QString)),
            mainWin,SLOT(mediaDownloadFailed(FMessage,QString)));

    historyTimer.setSingleShot(true);
    connect(&historyTimer,SIGNAL(timeout()),this,SLOT(processHistory()));

    connect(qApp,SIGNAL(applicationStateChanged(Qt::ApplicationState)),
            this,SLOT(applicationStateChanged(Qt::ApplicationState)));

    // Status bar
    QStatusBar *bar = mainWin->statusBar();
    bar->setStyleSheet("QStatusBar { font-size:18px; }");
    bar->showMessage(FULL_VERSION);
    bar->show();

    // Subscribe to the daemon's signals first, then take our bus name: the
    // daemon queues signals while the name is not on the bus.
    connection->start();

    ifAdaptor = new DBusIfAdaptor(this);
    QDBusConnection sessionbus = QDBusConnection::sessionBus();
    sessionbus.registerObject(YAPPARI_OBJECT, this);
    if (!sessionbus.registerService(YAPPARI_SERVICE))
        Utilities::logData("Could not register " YAPPARI_SERVICE);

    connection->replayQueued();

    if (!minimized)
        mainWin->show();
}

/**
    Destroys a Client object
*/
Client::~Client()
{
    Utilities::logData("Application destroyed");
}

/** ***********************************************************************
 ** Settings methods
 **/

/**
    Reads the global settings and store them in the public static members.
*/
void Client::readSettings()
{
    settings = new QSettings(SETTINGS_ORGANIZATION,SETTINGS_APPLICATION,this);

    // Colors
    mycolor = settings->value(SETTINGS_MYCOLOR).toString();
    textcolor = settings->value(SETTINGS_TEXTCOLOR).toString();
    nickcolor = settings->value(SETTINGS_NICKCOLOR).toString();

    showNicknames = settings->value(SETTINGS_SHOW_NICKNAMES,
                                    QVariant(DEFAULT_SHOW_NICKNAMES)).toBool();
    showNumbers = settings->value(SETTINGS_SHOW_NUMBERS,
                                  QVariant(DEFAULT_SHOW_NUMBERS)).toBool();
    popupOnFirstMessage = settings->value(SETTINGS_POPUP_ON_FIRST_MESSAGE,
                                          QVariant(DEFAULT_POPUP_ON_FIRST_MESSAGE)).toBool();
    blueChecks = settings->value(SETTINGS_BLUE_CHECKS,
                                 QVariant(DEFAULT_BLUE_CHECKS)).toBool();
    automaticDownloadBytes = settings->value(SETTINGS_AUTOMATIC_DOWNLOAD,
                                             QVariant(DEFAULT_AUTOMATIC_DOWNLOAD)).toInt();
    enterIsSend = settings->value(SETTINGS_ENTER_IS_SEND,
                                  QVariant(DEFAULT_ENTER_IS_SEND)).toBool();

    QString myDocs = QDir::homePath() + "/MyDocs";
    lastFileDir = settings->value(SETTINGS_LAST_FILE_DIR,
                                  QDir(myDocs).exists() ? myDocs : QDir::homePath()).toString();
}

/**
    Update the global settings file with the current settings
*/
void Client::updateSettings()
{
    settings->setValue(SETTINGS_SHOW_NICKNAMES,showNicknames);
    settings->setValue(SETTINGS_SHOW_NUMBERS,showNumbers);
    settings->setValue(SETTINGS_POPUP_ON_FIRST_MESSAGE,popupOnFirstMessage);
    settings->setValue(SETTINGS_BLUE_CHECKS,blueChecks);
    settings->setValue(SETTINGS_AUTOMATIC_DOWNLOAD,automaticDownloadBytes);
    settings->setValue(SETTINGS_ENTER_IS_SEND,enterIsSend);
}

void Client::updateLastDir(int waType, QString dir)
{
    Q_UNUSED(waType);

    lastFileDir = dir;
    settings->setValue(SETTINGS_LAST_FILE_DIR,lastFileDir);
}

/** ***********************************************************************
 ** Connection state
 **/

void Client::stateChanged(QString state)
{
    if (state == "connected")
        connectionStatus = LoggedIn;
    else if (state == "connecting")
        connectionStatus = Connecting;
    else if (state == "logged-out")
        connectionStatus = Registering;
    else
        connectionStatus = WaitingForConnection;

    if (!connection->ownJid().isEmpty())
        myJid = connection->ownJid();

    showConnectionStatus(parseConnectionStatus());

    if (connectionStatus == LoggedIn)
    {
        synchronizeContacts();

        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - lastGroupsRequest > GROUPS_REQUEST_INTERVAL)
        {
            lastGroupsRequest = now;
            connection->requestGroups();
        }

        connection->setAvailable(qApp->applicationState() == Qt::ApplicationActive);

        // Send what could not be sent before
        QList<FMessage> pending = pendingMessages.values();
        pendingMessages.clear();
        foreach (FMessage message, pending)
            connection->sendMessage(message);

        mainWin->connectionReady();
    }
    else if (connectionStatus == Registering)
    {
        mainWin->showLinkWindow();
    }
}

QString Client::parseConnectionStatus()
{
    switch(connectionStatus)
    {
        case WaitingForConnection:
            return "Waiting for connection";

        case Connecting:
            return "Connecting";

        case LoggedIn:
            return "Connected";

        case Registering:
            return "Not linked to WhatsApp";

        default:
            return FULL_VERSION;
    }
}

void Client::showConnectionStatus(QString status)
{
    QStatusBar *bar = mainWin->statusBar();
    bar->showMessage(status);
}

void Client::applicationStateChanged(Qt::ApplicationState state)
{
    // WhatsApp only reports typing and presence while we are "available"
    connection->setAvailable(state == Qt::ApplicationActive);
}

/** ***********************************************************************
 ** Messages
 **/

void Client::queueMessage(FMessage message)
{
    if (message.type == FMessage::BodyMessage || message.type == FMessage::MediaMessage)
    {
        sentMessages.insert(message.key.id, message);

        if (connectionStatus != LoggedIn)
        {
            pendingMessages.insert(message.key.id, message);
            return;
        }
    }

    connection->sendMessage(message);
}

void Client::receiptReceived(QString chat, QString sender, QStringList ids,
                             QString type, qint64 timestamp)
{
    Q_UNUSED(sender);
    Q_UNUSED(timestamp);

    bool group = chat.endsWith("@" GROUP_DOMAIN);
    FMessage::Status status;
    bool fromMe = true;

    if (type == "server")
        status = FMessage::ReceivedByServer;
    else if (type == "delivered")
        status = FMessage::ReceivedByTarget;
    else if (type == "read")
        status = (blueChecks || group) ? FMessage::ReadByTarget : FMessage::ReceivedByTarget;
    else if (type == "played")
        status = FMessage::Played;
    else if (type == "read-self" || type == "played-self")
    {
        // Read on another device of ours
        status = FMessage::ReceivedByTarget;
        fromMe = false;
    }
    else if (type == "error")
    {
        // Not sent: try again with the next connection
        foreach (QString id, ids)
            if (sentMessages.contains(id))
                pendingMessages.insert(id, sentMessages.value(id));
        Utilities::logData("Sending failed for " + ids.join(" "));
        return;
    }
    else
        return;

    foreach (QString id, ids)
    {
        if (status == FMessage::ReceivedByServer)
            sentMessages.remove(id);

        FMessage msg(chat, fromMe, id);
        msg.status = status;
        msg.type = FMessage::UndefinedMessage;
        mainWin->messageStatusUpdate(msg);
    }

    if (!fromMe)
        mainWin->readElsewhere(chat);
}

void Client::messageReceived(FMessage message)
{
    // Preview pictures for images, videos and documents
    if (message.type == FMessage::MediaMessage &&
        (message.media_wa_type == FMessage::Image ||
         message.media_wa_type == FMessage::Video ||
         message.media_wa_type == FMessage::Document))
        connection->requestThumbnail(message);

    // Unknown group: ask for the group names
    QString chat = message.key.remote_jid;
    if (chat.endsWith("@" GROUP_DOMAIN) &&
        (!roster->isContactInRoster(chat) || roster->getGroup(chat).name.isEmpty()))
    {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - lastGroupsRequest > GROUPS_REQUEST_INTERVAL)
        {
            lastGroupsRequest = now;
            connection->requestGroups();
        }
    }

    mainWin->messageReceived(message);
}

void Client::historyReceived(FMessage message)
{
    historyQueue[message.key.remote_jid].append(message);

    // Store them in batches: right after linking there can be thousands
    if (!historyTimer.isActive())
        historyTimer.start(500);
}

void Client::processHistory()
{
    QStringList chats = historyQueue.keys();

    foreach (QString jid, chats)
    {
        QList<FMessage> list = historyQueue.take(jid);

        // Remember push names of people we have no name for
        QList<Contact *> changed;
        foreach (FMessage msg, list)
        {
            QString from = msg.remote_resource.isEmpty() ? msg.key.remote_jid : msg.remote_resource;
            if (msg.key.from_me || msg.notify_name.isEmpty() || from.endsWith("@" GROUP_DOMAIN))
                continue;

            Contact &c = roster->getContact(from);
            if (c.alias != msg.notify_name && !changed.contains(&c))
            {
                c.alias = msg.notify_name;
                if (c.name.isEmpty() || !c.fromAddressBook)
                    c.name = c.alias;
                changed.append(&c);
            }
        }

        // Names as in live messages (address book name unless nicknames are wanted)
        for (int i = 0; i < list.size(); i++)
        {
            FMessage &msg = list[i];
            QString from = msg.remote_resource.isEmpty() ? msg.key.remote_jid : msg.remote_resource;
            if (msg.key.from_me || !roster->isContactInRoster(from))
                continue;

            Contact &c = roster->getContact(from);
            msg.notify_name = ((showNicknames || !c.fromAddressBook) && !c.alias.isEmpty())
                              ? c.alias : c.name;
        }
        if (!changed.isEmpty())
            roster->updateContacts(changed);

        ChatLogger logger;
        logger.init(jid);
        int count = logger.logMessages(list);

        Utilities::logData("History: " + QString::number(count) + " messages for " + jid);

        if (count > 0)
            mainWin->historyLogged(jid);

        foreach (FMessage msg, list)
            if (msg.type == FMessage::MediaMessage &&
                (msg.media_wa_type == FMessage::Image || msg.media_wa_type == FMessage::Video))
                connection->requestThumbnail(msg);
    }
}

// Collects read receipts and sends them per chat and author
void Client::sendMessageRead(FMessage message)
{
    QString chat = message.key.remote_jid;
    bool group = chat.endsWith("@" GROUP_DOMAIN);

    if (message.key.from_me || (!group && !blueChecks))
        return;

    connection->markRead(chat, group ? message.remote_resource : QString(),
                         QStringList(message.key.id));
}

void Client::subscribe(QString jid)
{
    connection->subscribePresence(jid);
}

/** ***********************************************************************
 ** Contacts, groups and pictures
 **/

void Client::synchronizeContacts()
{
    connection->requestContacts();
}

void Client::contactsReceived(QList<QStringList> contacts)
{
    QList<Contact *> changed;

    foreach (QStringList entry, contacts)
    {
        QString jid = entry.value(0);
        QString fullName = entry.value(1);
        QString pushName = entry.value(2);

        if (jid == myJid)
            continue;

        // Only people with a name: WhatsApp also lists everyone it ever saw
        if (fullName.isEmpty() && !roster->isContactInRoster(jid))
            continue;

        Contact &c = roster->getContact(jid, false);
        bool dirty = false;

        if (!fullName.isEmpty() && (c.name != fullName || !c.fromAddressBook))
        {
            c.name = fullName;
            c.fromAddressBook = true;
            dirty = true;
        }

        if (!pushName.isEmpty() && c.alias != pushName)
        {
            c.alias = pushName;
            if (c.name.isEmpty())
                c.name = pushName;
            dirty = true;
        }

        if (dirty)
            changed.append(&c);
    }

    if (!changed.isEmpty())
    {
        roster->updateContacts(changed);
        mainWin->contactsUpdated();
    }

    Utilities::logData("Contacts: " + QString::number(contacts.size()) + " received, " +
                       QString::number(changed.size()) + " updated");
}

void Client::groupsReceived(QMap<QString,QString> groups)
{
    foreach (QString jid, groups.keys())
    {
        QString subject = groups.value(jid);
        Group &g = roster->getGroup(jid);

        if (g.name != subject && !subject.isEmpty())
        {
            g.name = subject;
            roster->updateContact(&g);
            mainWin->updateGroup(g, false);
        }
    }
}

void Client::updatePhoto(QString jid, QString expectedPhotoId, bool largeFormat)
{
    Q_UNUSED(expectedPhotoId);
    Q_UNUSED(largeFormat);

    if (connectionStatus == LoggedIn && !photoRequested.contains(jid))
    {
        photoRequested.insert(jid);
        connection->requestProfilePicture(jid);
    }
}

void Client::profilePictureReceived(QString jid, QString path)
{
    if (path.isEmpty())
        return;

    QString photoId = QFileInfo(path).fileName();
    Contact &c = roster->getContact(jid);

    if (c.photoId != photoId || c.photo.isNull())
    {
        QImage image(path);
        if (image.isNull())
            return;

        c.photo = image.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        c.photoId = photoId;
        roster->updatePhoto(&c);
        mainWin->updatePhoto(c);
    }
}

QString Client::authorName(const FMessage &message)
{
    if (message.key.from_me)
        return "You";

    if (!message.notify_name.isEmpty())
        return message.notify_name;

    QString jid = message.remote_resource.isEmpty() ? message.key.remote_jid
                                                    : message.remote_resource;

    if (roster && roster->isContactInRoster(jid))
    {
        Contact &c = roster->getContact(jid);
        if (!c.name.isEmpty())
            return c.name;
    }

    return "+" + jid.left(jid.indexOf('@'));
}

/** ***********************************************************************
 ** D-Bus interface org.yappari.Yappari
 **/

void Client::ShowWindow()
{
    mainWin->show();
    mainWin->raise();
    mainWin->activateWindow();

    if (connectionStatus == Registering)
        mainWin->showLinkWindow();
}

bool Client::isRunning()
{
    return true;
}
