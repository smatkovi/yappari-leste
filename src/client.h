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

#ifndef CLIENT_H
#define CLIENT_H

#include <QObject>
#include <QString>
#include <QQueue>
#include <QTimer>
#include <QHash>
#include <QSet>
#include <QSettings>

#include "Dbus/dbusifadaptor.h"

#include "WhatsApp/waconnection.h"

#include "Contacts/contactroster.h"

#include "Gui/mainwindow.h"

/**
    @class      Client

    @brief      This is the main class of Yappari.

                It provides interaction between the GUI and the yappari-wa
                daemon (WAConnection) that keeps the connection to the
                WhatsApp servers.

                The object from this class keeps running in the background
                when the main window is closed, until Quit is chosen.
*/

class Client : public QObject
{
    Q_OBJECT

public:

    /** ***********************************************************************
     ** Enumerations
     **/

    // Status of the connection to the WhatsApp servers
    enum ConnectionStatus {
        Unknown,
        WaitingForConnection,
        Connecting,
        Connected,
        LoggedIn,
        LoginFailure,
        Disconnected,
        Registering,
        RegistrationFailed
    };

    /** ***********************************************************************
     ** Public members
     **/

    // Status of the connection
    static ConnectionStatus connectionStatus;

    // Global settings
    static QSettings *settings;

    // Background color of own self text
    static QString mycolor;

    // Nicknames color
    static QString nickcolor;

    // Text color
    static QString textcolor;

    // Own JID
    static QString myJid;

    // Show nicknames in conversations instead of their names from the address book
    static bool showNicknames;

    // Show users phone numbers in select contact dialog
    static bool showNumbers;

    // Pop up conversation windows when first message is received
    static bool popupOnFirstMessage;

    // Report when the other party has read messages (in both directions)
    static bool blueChecks;

    // Automatic download of media if less than this number of kilobytes
    static int automaticDownloadBytes;

    // Enter is Send
    static bool enterIsSend;

    // Last folder a file was sent from
    static QString lastFileDir;

    // Main GUI window
    static MainWindow *mainWin;

    // Roster
    static ContactRoster *roster;

    // Connection to the yappari-wa daemon
    static WAConnection *connection;


    /** ***********************************************************************
     ** Constructors and destructors
     **/

    // Name to show for the author of a message
    static QString authorName(const FMessage &message);

    // Create a Client object
    explicit Client(bool minimized, QObject *parent = 0);

    // Destroy a Client object
    ~Client();


public slots:

    // Update the global settings file with the current settings
    void updateSettings();

    // Messages from the GUI
    void queueMessage(FMessage message);
    void sendMessageRead(FMessage message);
    void subscribe(QString jid);
    void updatePhoto(QString jid, QString expectedPhotoId, bool largeFormat);
    void synchronizeContacts();
    void updateLastDir(int waType, QString dir);

    // D-Bus interface org.yappari.Yappari
    void ShowWindow();
    bool isRunning();

private slots:
    void stateChanged(QString state);
    void receiptReceived(QString chat, QString sender, QStringList ids,
                         QString type, qint64 timestamp);
    void historyReceived(FMessage message);
    void processHistory();
    void contactsReceived(QList<QStringList> contacts);
    void groupsReceived(QMap<QString,QString> groups);
    void profilePictureReceived(QString jid, QString path);
    void messageReceived(FMessage message);
    void applicationStateChanged(Qt::ApplicationState state);

private:
    DBusIfAdaptor *ifAdaptor;

    // Messages that could not be sent, retried on the next connection
    QHash<QString,FMessage> pendingMessages;
    QHash<QString,FMessage> sentMessages;
    QHash<QString,int> sendAttempts;

    // History messages waiting to be stored, per chat
    QHash<QString,QList<FMessage> > historyQueue;
    QTimer historyTimer;

    // Profile pictures asked for in this session
    QSet<QString> photoRequested;

    // Time of the last request for the group list
    qint64 lastGroupsRequest;

    void readSettings();
    void showConnectionStatus(QString status);
    QString parseConnectionStatus();
};

#endif // CLIENT_H
