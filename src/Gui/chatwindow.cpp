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

#include <QMaemo5InformationBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QImageReader>
#include <QBuffer>
#include <QDir>
#include <QDateTime>
#include <QX11Info>

#include "emojidialog.h"
#include "chatwindow.h"
#include "ui_chatwindow.h"
#include "mutedialog.h"
#include "globalconstants.h"

#include "util/utilities.h"
#include "util/datetimeutilities.h"

#include "client.h"

#include <X11/Xlib.h>
#include <X11/Xatom.h>

#define TEXTEDIT_HEIGHT     70

ChatWindow::ChatWindow(Contact *contact, QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::ChatWindow)
{
    this->contact = contact;

    ui->setupUi(this);
    ui->scrollArea->init();

    // Stacked on top of the list of chats
    setProperty("X-Maemo-StackedWindow", 1);

    ui->recordingLabel->hide();
    ui->recordButton->hide();

    QIcon smiley("/usr/share/icons/hicolor/48x48/hildon/chat_smiley_happy.png");
    if (!smiley.isNull())
        ui->selectEmojiButton->setIcon(smiley);
    else
        ui->selectEmojiButton->setText(":-)");

    muted = false;
    muteExpireTimestamp = 0;

    connect(ui->scrollArea,SIGNAL(mediaDownload(FMessage)),
            this,SLOT(mediaDownloadRequested(FMessage)));
    connect(ui->scrollArea,SIGNAL(forwardMessage(FMessage)),
            this,SLOT(forwardMessageRequested(FMessage)));
    connect(ui->scrollArea,SIGNAL(deleteMessage(FMessage)),
            this,SLOT(deleteMessage(FMessage)));
    connect(ui->sendButton,SIGNAL(clicked()),this,SLOT(sendButtonClicked()));
    connect(ui->selectEmojiButton,SIGNAL(clicked()),this,SLOT(selectEmoji()));

    if (contact->type == Contact::TypeContact)
        available(contact->isOnline,contact->lastSeen);

    isPeerComposing = false;
    isMyselfComposing = false;

    setAttribute(Qt::WA_QuitOnClose,false);

    ui->textEdit->setFocus();

    // Hildon notifications of this chat belong to this window
    if (QX11Info::isPlatformX11())
    {
        Atom atom = XInternAtom(QX11Info::display(), "_HILDON_NOTIFICATION_THREAD", False);
        if (atom)
        {
            QByteArray thread = contact->jid.toUtf8();
            XChangeProperty (QX11Info::display(),
                             winId(),
                             atom,
                             XA_STRING,
                             8,
                             PropModeReplace,
                             (unsigned char *)thread.constData(),
                             thread.length()
                             );
        }
    }

    logger.init(contact->jid);

    connect(this,SIGNAL(logMessage(FMessage)),
            &logger,SLOT(logMessage(FMessage)));

    connect(this,SIGNAL(updateLoggedMessage(FMessage)),
            &logger,SLOT(updateLoggedMessage(FMessage)));

    connect(this,SIGNAL(updateUriMessage(FMessage)),
            &logger,SLOT(updateUriMessage(FMessage)));

    connect(ui->actionDeleteAll,SIGNAL(triggered()),
            this,SLOT(deleteAllMessages()));

    connect(ui->actionSendMultimedia,SIGNAL(triggered()),
            this,SLOT(selectMultimediaMessage()));

    connect(ui->actionMute,SIGNAL(triggered()),
            this,SLOT(mute()));

    connect(ui->scrollArea,SIGNAL(topReached()),
            this,SLOT(readMoreLogLines()));

    connect(ui->scrollArea,SIGNAL(bottomReached()),
            this,SLOT(setMessagesAsRead()));

    unread = logger.unreadMessages();

    readMoreLogLines();

    // After the layout is done. No processEvents() here: D-Bus replies for
    // this chat could be handled before the window is registered.
    QTimer::singleShot(0, ui->scrollArea, SLOT(goToBottom()));

    connect(ui->textEdit,SIGNAL(paused()),
            this,SLOT(myselfPaused()));

    connect(ui->textEdit,SIGNAL(composing()),
            this,SLOT(myselfComposing()));

    connect(ui->textEdit,SIGNAL(returnPressed()),
            this,SLOT(sendButtonClicked()));

    connect(ui->textEdit,SIGNAL(textChanged()),
            this,SLOT(textChanged()));

    installEventFilter(this);
}

ChatWindow::~ChatWindow()
{
    delete ui;
}

void ChatWindow::readMoreLogLines()
{
    QList<FMessage> list = logger.lastMessages();

    ui->scrollArea->loadLogMessages(list);
}

// Reports the received messages as read once the user looks at them
void ChatWindow::setMessagesAsRead()
{
    if (!isActiveWindow() || unread.isEmpty())
        return;

    foreach (FMessage msg, unread)
    {
        emit messageRead(msg);
        msg.status = FMessage::ReceivedByTarget;
        logger.updateLoggedMessage(msg);
    }
    unread.clear();
}

void ChatWindow::messageReceived(FMessage& message)
{
    if (logger.contains(message.key.id))
        return;

    showMessageInUI(message);
    emit logMessage(message);

    if (!message.key.from_me)
    {
        isPeerComposing = false;
        ui->typingStatusLabel->clear();

        if (message.status == FMessage::Unsent)
        {
            unread.append(message);
            setMessagesAsRead();
        }
    }
}

void ChatWindow::sendButtonClicked()
{
    QString text = ui->textEdit->toPlainText().trimmed();

    if (!text.isEmpty())
    {
        FMessage message(contact->jid,true);
        message.type = FMessage::BodyMessage;
        message.setData(text);

        if (isMyselfComposing)
            myselfPaused();

        sendTextMessage(message);

        ui->textEdit->clear();
        ui->textEdit->setFixedHeight(TEXTEDIT_HEIGHT);
    }

    ui->textEdit->setFocus();
}

void ChatWindow::sendTextMessage(FMessage& message)
{
    emit sendMessage(message);
    emit logMessage(message);

    showMessageInUI(message);
}

void ChatWindow::showMessageInUI(FMessage& message)
{
    ui->scrollArea->insertMessageAtBottom(message);
}

void ChatWindow::setContact(Contact *contact)
{
    this->contact = contact;

    QString title = contact->name;
    if (title.isEmpty())
        title = (contact->type == Contact::TypeGroup) ? QString("Group") : contact->phone;
    setWindowTitle(title);
}

const Contact& ChatWindow::getContact() const
{
    return *(this->contact);
}

void ChatWindow::messageStatusUpdate(FMessage& message)
{
    ui->scrollArea->updateStatus(message);
    emit updateLoggedMessage(message);

    // Read on another device: nothing left to report
    if (!message.key.from_me)
    {
        for (int i = unread.size() - 1; i >= 0; i--)
            if (unread.at(i).key.id == message.key.id)
                unread.removeAt(i);
    }
}

void ChatWindow::available(bool online, qint64 lastSeen)
{
    QString text;

    contact->isOnline = online;
    contact->lastSeen = lastSeen;

    if (!online)
    {
        if (!lastSeen)
            text = "";
        else
            text = "Last seen " + DateTimeUtilities::formattedStamp(lastSeen);
    }
    else
        text = "Online";

    setOnlineText(text);
}

void ChatWindow::setOnlineText(QString text)
{
    ui->onlineStatusLabel->setText(text);
}

void ChatWindow::textChanged()
{
}

void ChatWindow::myselfComposing(int waType)
{
    isMyselfComposing = true;

    FMessage message(contact->jid,true);
    message.type = FMessage::ComposingMessage;
    message.media_wa_type = waType;

    emit sendMessage(message);
}

void ChatWindow::myselfPaused()
{
    isMyselfComposing = false;

    FMessage message(contact->jid,true);
    message.type = FMessage::PausedMessage;

    emit sendMessage(message);
}


void ChatWindow::composing(QString participant, QString media)
{
    isPeerComposing = true;
    QString text = (media == "audio") ? " is recording audio..." : " is typing...";
    QString n = participant.isEmpty() ? contact->name : participant;
    if (n.isEmpty())
        n = contact->phone;
    ui->typingStatusLabel->setText(n + text);
}

void ChatWindow::paused(QString participant)
{
    Q_UNUSED(participant);

    if (isPeerComposing)
        ui->typingStatusLabel->clear();
    isPeerComposing = false;
}

bool ChatWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == this && event->type() == QEvent::WindowActivate)
        QTimer::singleShot(0, this, SLOT(setMessagesAsRead()));

    return QMainWindow::eventFilter(obj,event);
}

void ChatWindow::selectEmoji()
{
    EmojiDialog dialog(this);

    if (dialog.exec() == QDialog::Accepted)
    {
        ui->textEdit->insertPlainText(dialog.selectedEmoji());
        ui->textEdit->setFocus();
    }
}

// The kind of media WhatsApp makes of a file
static int mediaTypeFor(QString fileName)
{
    QString ext = Utilities::getExtension(fileName);

    if (QStringList({"jpg","jpeg","png","gif","webp"}).contains(ext))
        return FMessage::Image;
    if (QStringList({"mp4","3gp","mov","mkv","avi","webm"}).contains(ext))
        return FMessage::Video;
    if (QStringList({"mp3","m4a","aac","ogg","opus","wav","amr","flac"}).contains(ext))
        return FMessage::Audio;

    return FMessage::Document;
}

void ChatWindow::selectMultimediaMessage()
{
    QString fileName = QFileDialog::getOpenFileName(this, "Send File", Client::lastFileDir);

    if (fileName.isEmpty())
        return;

    QFileInfo info(fileName);

    emit updateLastDir(-1, info.absolutePath());

    if (info.size() > MAX_FILE_SIZE)
    {
        QMaemo5InformationBox::information(this,"This file is too big to send",
                                           QMaemo5InformationBox::DefaultTimeout);
        return;
    }

    int waType = mediaTypeFor(fileName);
    QString caption;

    if (waType != FMessage::Audio)
    {
        bool ok;
        caption = QInputDialog::getText(this, "Caption", "Caption (optional):",
                                        QLineEdit::Normal, QString(), &ok);
        if (!ok)
            return;
    }

    sendMultimediaMessage(fileName, waType, false, caption);
}

void ChatWindow::sendMultimediaMessage(QString fileName, int waType, bool live, QString caption)
{
    QFileInfo info(fileName);

    if (waType <= FMessage::Undefined)
        waType = mediaTypeFor(fileName);

    FMessage msg(contact->jid, true);

    msg.status = FMessage::Unsent;
    msg.type = FMessage::MediaMessage;
    msg.media_size = info.size();
    msg.media_wa_type = waType;
    msg.media_name = info.fileName();
    msg.local_file_uri = info.absoluteFilePath();
    msg.live = live;
    msg.media_caption = caption;

    // Preview for the conversation
    if (waType == FMessage::Image)
    {
        QImageReader reader(fileName);
        reader.setAutoTransform(true);
        QImage image = reader.read();

        if (!image.isNull())
        {
            QImage preview = image.scaled(PREVIEW_WIDTH, PREVIEW_HEIGHT,
                                          Qt::KeepAspectRatio, Qt::SmoothTransformation);
            QBuffer buffer(&msg.data);
            buffer.open(QIODevice::WriteOnly);
            preview.save(&buffer, "JPEG", 80);
        }
    }

    emit sendMessage(msg);
    emit logMessage(msg);

    showMessageInUI(msg);
}

void ChatWindow::mediaDownloadRequested(FMessage msg)
{
    emit mediaDownload(msg);
}

void ChatWindow::mediaDownloadFinished(FMessage msg)
{
    ui->scrollArea->updateUri(msg);

    emit updateUriMessage(msg);
}

void ChatWindow::mediaDownloadError(FMessage msg, QString error)
{
    Utilities::logData("There has been an error while trying to download media. " + error);

    if (msg.thumb_image != "auto")
        QMaemo5InformationBox::information(this,"Could not download the file.\n" + error,
                                           QMaemo5InformationBox::DefaultTimeout);

    ui->scrollArea->resetButton(msg);
}

void ChatWindow::thumbnailReceived(FMessage msg)
{
    ui->scrollArea->updateImage(msg);
    logger.updateMediaData(msg);
}

void ChatWindow::deleteAllMessages()
{
    QMessageBox msg(this);

    msg.setText("Are you sure you want to clear the conversation history?");
    msg.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);

    if (msg.exec() == QMessageBox::Yes)
    {
        logger.deleteAllMessages();
        ui->scrollArea->deleteAllMessages();
        unread.clear();
    }
}

void ChatWindow::setMute(qint64 timestamp)
{
    muted = true;
    muteExpireTimestamp = timestamp;
    ui->actionMute->setText("Unmute");
}

void ChatWindow::mute()
{
    if (!muted)
    {
        MuteDialog dialog(this);

        if (dialog.exec() == QDialog::Accepted)
        {
            setMute(dialog.getMuteExpireTimestamp());
            emit mute(contact->jid,muted,muteExpireTimestamp);
        }
    }
    else
        unmute();
}

void ChatWindow::unmute()
{
    muted = false;
    muteExpireTimestamp = 0;
    ui->actionMute->setText("Mute");
    emit mute(contact->jid,muted,muteExpireTimestamp);
}

FMessage ChatWindow::lastMessage()
{
    return logger.lastMessage();
}

void ChatWindow::updateTimestamps()
{
    ui->scrollArea->requestUpdateTimestamps();
}

void ChatWindow::forwardMessageRequested(FMessage message)
{
    emit forwardMessage(message);
}

void ChatWindow::deleteMessage(FMessage message)
{
    logger.deleteMessage(message);
}
