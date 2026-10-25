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

#include <QMaemo5Style>

#include <QDesktopServices>
#include <QImageReader>
#include <QFileInfo>
#include <QPainter>
#include <QTimer>
#include <QUrl>

#include "util/datetimeutilities.h"
#include "util/utilities.h"

#include "Multimedia/audioplayer.h"

#include "globalconstants.h"
#include "client.h"

#include "imageviewer.h"
#include "chatimageitem.h"
#include "ui_chatimageitem.h"

#define ICONS_DIR       "/usr/share/yappari/icons/100x100/"
#define HILDON_ICONS    "/usr/share/icons/hicolor/48x48/hildon/"

ChatImageItem::ChatImageItem(FMessage message, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ChatImageItem)
{
    ui->setupUi(this);

    waiting = downloading = autoDownload = false;
    this->message = message;

    setImage();

    ui->progressBar->setRange(0,100);
    ui->progressBar->setValue(0);
    ui->progressBar->hide();

    ui->nameLabel->hide();

    ui->timeLabel->hide();

    connect(ui->viewImageButton,SIGNAL(clicked()),this,SLOT(downloadOrViewImage()));

    setButton();

    setNickname(message);
    setTimestamp(message);

    if (message.media_caption.length() > 0) {
        ui->imageCaption->setWordWrap(true);
        ui->imageCaption->setText(message.media_caption);
    } else {
        ui->imageCaption->hide();
    }
}

FMessage ChatImageItem::getMessage()
{
    return message;
}

void ChatImageItem::setImage()
{
    QImage image;
    int type = message.media_wa_type;

    if (!message.data.isEmpty())
        image = QImage::fromData(message.data);

    if (image.isNull() && !message.local_file_uri.isEmpty() &&
        (type == FMessage::Image || type == FMessage::Sticker))
    {
        QImageReader reader(message.local_file_uri);
        reader.setAutoTransform(true);
        QSize size = reader.size();
        if (size.isValid())
            reader.setScaledSize(size.scaled(PREVIEW_WIDTH, PREVIEW_HEIGHT, Qt::KeepAspectRatio));
        image = reader.read();
    }

    if (image.isNull())
    {
        switch (type)
        {
            case FMessage::Audio:
                image.load(message.live ? ICONS_DIR "voice_overlay_icon.png"
                                        : ICONS_DIR "audio_overlay_icon.png");
                break;

            case FMessage::Video:
                image.load(ICONS_DIR "video_overlay_icon.png");
                break;

            case FMessage::Location:
                image.load(HILDON_ICONS "general_map.png");
                break;

            case FMessage::Document:
                image.load(HILDON_ICONS "email_attachment.png");
                break;

            default:
                break;
        }
    }
    else if (image.width() > PREVIEW_WIDTH || image.height() > PREVIEW_HEIGHT)
        image = image.scaled(PREVIEW_WIDTH, PREVIEW_HEIGHT, Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);

    if (type != FMessage::Video && type != FMessage::Audio)
    {
        ui->image->setPixmap(QPixmap::fromImage(image));
        return;
    }

    // Videos and audio get an overlay: play symbol, duration

    QSize size = (image.isNull()) ? QSize(100,100) : image.size();

    QImage imageWithOverlay = QImage(size, QImage::Format_ARGB32_Premultiplied);
    imageWithOverlay.fill(Qt::transparent);

    QPainter painter(&imageWithOverlay);

    if (!image.isNull())
    {
        painter.setCompositionMode((QPainter::CompositionMode_SourceOver));
        painter.drawImage(0, 0, image);
    }

    if (type == FMessage::Video && !message.data.isEmpty())
    {
        QImage overlayImage(ICONS_DIR "play_overlay_icon.png");
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter.drawImage((size.width() - overlayImage.width()) / 2,
                          (size.height() - overlayImage.height()) / 2, overlayImage);
    }

    if (message.media_duration_seconds > 0)
    {
        QFont font = painter.font();
        font.setPointSize(14);
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(QPen(QColor(Qt::white)));

        int minutes = message.media_duration_seconds / 60;
        int seconds = message.media_duration_seconds % 60;
        painter.drawText(QRect(0, size.height() - 20, size.width(), 20), Qt::AlignCenter,
                         QString::asprintf("%02d:%02d", minutes, seconds));
    }
    painter.end();
    ui->image->setPixmap(QPixmap::fromImage(imageWithOverlay));
}


void ChatImageItem::setNickname(FMessage message)
{
    QString from = Client::authorName(message);

    QColor color = QMaemo5Style::standardColor("ActiveTextColor");
    QString nickcolor = Client::nickcolor.isEmpty() ? color.name() : Client::nickcolor;

    QString mycolor = Client::mycolor.isEmpty() ? "#333333" : Client::mycolor;

    ui->groupBox->setStyleSheet("QGroupBox { border-color: "
                                + mycolor +
                                "; border-width: 1px; "
                                "border-style: solid; }");

    QString html = "<style type=\"text/css\">"
                   ".nick { color:" + nickcolor + "; }"
                   "</style>"
                   "<span class=\"nick\">" +
                   Utilities::WATextToHtml(from + ":",32,false) +
                   "</span>&nbsp;";

    ui->nickname->setTextFormat(Qt::RichText);
    ui->nickname->setAlignment(Qt::AlignCenter|Qt::AlignLeft);
    ui->nickname->setText(html);
}


void ChatImageItem::setTimestamp(FMessage message)
{
    bool from_me = message.key.from_me;

    QString day;

    if (!DateTimeUtilities::isSameDate(message.timestamp,
                                       QDateTime::currentMSecsSinceEpoch()))
        day = DateTimeUtilities::shortDayFormat(message.timestamp);


    QString time = DateTimeUtilities::shortTimeFormat(message.timestamp);

    QColor color = QMaemo5Style::standardColor("SecondaryTextColor");

    QString html = "<div style=\"font-size:18px;color:" + color.name() + "\">"
                    + day + " </div>";

    if (from_me)
    {
        html.append("<div align=\"right\">");

        if (message.status == FMessage::ReceivedByServer)
            html.append(CHECK);
        else if (message.status == FMessage::ReceivedByTarget)
            html.append(DOUBLECHECK);
        else if (message.status == FMessage::ReadByTarget)
            html.append(DOUBLEBLUECHECK);
        else if (message.status == FMessage::Played)
        {
            html.append(VOICEPLAYED);
            html.append(DOUBLEBLUECHECK);
        }
        else
            html.append(UPLOADING);

        html.append("</div>");
    }

    html.append("<div align=\"right\" style=\"font-size:18px;color:" + color.name() + "\">" + time +
                "</div>");

    if (message.media_size > 0)
        html.append("<div align=\"right\" style=\"font-size:14px;color:" + color.name() + "\">" +
                    Utilities::formatBytes(message.media_size) + "</div>");

    ui->timestamp->setTextFormat(Qt::RichText);
    ui->timestamp->setAlignment(Qt::AlignTop);
    ui->timestamp->setText(html);

    // Update local message
    this->message.status = message.status;
    this->message.timestamp = message.timestamp;
}

void ChatImageItem::setStatus(int status)
{
    if (status > message.status || !message.key.from_me)
    {
        message.status = (FMessage::Status) status;
        setTimestamp(message);
    }
}

void ChatImageItem::setButton()
{
    ui->viewImageButton->setEnabled(true);

    if (message.media_wa_type == FMessage::Location)
    {
        ui->nameLabel->setText(message.media_name);
        ui->nameLabel->show();
        ui->viewImageButton->setText("Map");
    }
    else if (message.local_file_uri.isEmpty())
    {
        ui->viewImageButton->setText("Download");

        // Small pictures, stickers and voice notes come by themselves
        bool small = message.media_size <= (Client::automaticDownloadBytes * 1024);
        bool kind = message.media_wa_type == FMessage::Image ||
                    message.media_wa_type == FMessage::Sticker ||
                    (message.media_wa_type == FMessage::Audio && message.live);

        if (small && kind && !downloading)
        {
            autoDownload = true;
            QTimer::singleShot(50,this,SLOT(downloadOrViewImage()));
        }
    }
    else
    {
        if (message.media_wa_type == FMessage::Document)
        {
            ui->nameLabel->setText(QFileInfo(message.local_file_uri).fileName());
            ui->nameLabel->show();
        }

        switch (message.media_wa_type)
        {
            case FMessage::Image:
            case FMessage::Sticker:
                ui->viewImageButton->setText("View");
                break;

            case FMessage::Audio:
            case FMessage::Video:
                ui->viewImageButton->setText("Play");
                break;

            default:
                ui->viewImageButton->setText("Open");
                break;
        }
    }
}

ChatImageItem::~ChatImageItem()
{
    if (!player.isNull())
        player->stop();

    delete ui;
}

void ChatImageItem::downloadOrViewImage()
{
    if (message.media_wa_type == FMessage::Location)
    {
        QString url = URL_LOCATION_SHARING +
                      QString::number(message.latitude, 'f', 6) + "," +
                      QString::number(message.longitude, 'f', 6);
        QDesktopServices::openUrl(QUrl(url));
    }
    else if (!message.local_file_uri.isEmpty())
    {
        switch (message.media_wa_type)
        {
            case FMessage::Audio:
                if (!player.isNull())
                {
                    player->stop();
                    break;
                }

                player = new AudioPlayer(this);

                connect(player,SIGNAL(progress(int)),this,SLOT(updateTime(int)));
                connect(player,SIGNAL(finished()),this,SLOT(finishedAudioPlay()));

                ui->viewImageButton->setText("Stop");

                player->play(message.local_file_uri);

                if (message.live)
                    emit voiceNotePlayed(message);
                break;

            case FMessage::Image:
            case FMessage::Sticker:
            {
                ImageViewer *viewer = new ImageViewer(message.local_file_uri, window());
                viewer->show();
                break;
            }

            default:
                QDesktopServices::openUrl(QUrl::fromLocalFile(message.local_file_uri));
                break;
        }
    }
    else
    {
        // Download media

        downloading = true;
        ui->viewImageButton->setEnabled(false);
        ui->viewImageButton->setText("Loading");

        // Failed automatic downloads are not reported to the user
        FMessage request = message;
        request.thumb_image = autoDownload ? "auto" : QString();
        autoDownload = false;
        emit mediaDownload(request);
    }
}

void ChatImageItem::updateTimestamp()
{
    setTimestamp(message);
}

void ChatImageItem::updateProgress(float p)
{
    if (p >= 100.0)
        p = 100;

    ui->progressBar->show();
    ui->progressBar->setValue((int)p);
}

void ChatImageItem::updateTime(int current)
{
    int minutes = current / 60;
    int seconds = current % 60;
    ui->timeLabel->show();
    ui->timeLabel->setText(QString::asprintf("%02d:%02d",minutes,seconds));
}

void ChatImageItem::finishedAudioPlay()
{
    ui->timeLabel->hide();
    ui->viewImageButton->setText("Play");
    ui->viewImageButton->setEnabled(true);
}

void ChatImageItem::updateUri(QString uri)
{
    downloading = false;
    ui->progressBar->hide();
    message.local_file_uri = uri;
    setButton();

    if (message.data.isEmpty())
        setImage();
}

void ChatImageItem::resetButton()
{
    downloading = false;
    ui->progressBar->hide();
    ui->viewImageButton->setText("Download");
    ui->viewImageButton->setEnabled(true);
}

void ChatImageItem::updateImage(FMessage message)
{
    this->message.data = message.data;
    setImage();
}
