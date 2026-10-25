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

#include <QUrl>

#include "util/utilities.h"

#include "audioplayer.h"

AudioPlayer::AudioPlayer(QObject *parent) :
    QObject(parent)
{
    player = new QMediaPlayer(this);
    player->setNotifyInterval(250);
    lastSecond = -1;
    done = false;

    connect(player, SIGNAL(positionChanged(qint64)), this, SLOT(positionChanged(qint64)));
    connect(player, SIGNAL(stateChanged(QMediaPlayer::State)),
            this, SLOT(stateChanged(QMediaPlayer::State)));
    connect(player, SIGNAL(error(QMediaPlayer::Error)), this, SLOT(error(QMediaPlayer::Error)));
}

void AudioPlayer::play(QString uri)
{
    QUrl url(uri);
    if (url.scheme().isEmpty())
        url = QUrl::fromLocalFile(uri);

    player->setMedia(url);
    player->play();
}

void AudioPlayer::stop()
{
    player->stop();
    finish();
}

void AudioPlayer::positionChanged(qint64 position)
{
    int second = position / 1000;
    if (second != lastSecond)
    {
        lastSecond = second;
        emit progress(second);
    }
}

void AudioPlayer::stateChanged(QMediaPlayer::State state)
{
    if (state == QMediaPlayer::StoppedState)
        finish();
}

void AudioPlayer::error(QMediaPlayer::Error error)
{
    Q_UNUSED(error);

    Utilities::logData("AudioPlayer: " + player->errorString());
    finish();
}

void AudioPlayer::finish()
{
    if (done)
        return;

    done = true;
    emit finished();
    deleteLater();
}
