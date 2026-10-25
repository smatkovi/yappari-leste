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

#include <QMaemo5InformationBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QScroller>
#include <QLineEdit>
#include <QPushButton>
#include <QPainter>
#include <QLabel>
#include <QFrame>
#include <QTimer>

#include <qrencode.h>

#include "WhatsApp/waconnection.h"

#include "client.h"

#include "linkwindow.h"

LinkWindow::LinkWindow(QWidget *parent) :
    QMainWindow(parent)
{
    setWindowTitle("Link with WhatsApp");
    qrWanted = false;
    linked = false;

    QWidget *page = new QWidget;
    QVBoxLayout *layout = new QVBoxLayout(page);

    QLabel *intro = new QLabel("Yappari works as a linked device of your WhatsApp account, "
                               "like WhatsApp Web. Link it once with WhatsApp on your main phone.");
    intro->setWordWrap(true);
    layout->addWidget(intro);

    QLabel *phoneLabel = new QLabel("Phone number of the account, with country code:");
    phoneLabel->setWordWrap(true);
    layout->addWidget(phoneLabel);

    QHBoxLayout *phoneRow = new QHBoxLayout;
    phoneEdit = new QLineEdit;
    phoneEdit->setPlaceholderText("+43 660 1234567");
    phoneEdit->setInputMethodHints(Qt::ImhDialableCharactersOnly);
    codeButton = new QPushButton("Get code");
    phoneRow->addWidget(phoneEdit, 1);
    phoneRow->addWidget(codeButton);
    layout->addLayout(phoneRow);

    codeLabel = new QLabel;
    QFont big = codeLabel->font();
    big.setPointSize(big.pointSize() * 2);
    big.setBold(true);
    codeLabel->setFont(big);
    codeLabel->setAlignment(Qt::AlignCenter);
    codeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    codeLabel->hide();
    layout->addWidget(codeLabel);

    codeHelp = new QLabel("On your main phone open WhatsApp: Settings → Linked devices → "
                          "Link a device → Link with phone number instead, and enter this code.");
    codeHelp->setWordWrap(true);
    codeHelp->hide();
    layout->addWidget(codeHelp);

    QFrame *line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    layout->addWidget(line);

    qrButton = new QPushButton("Use a QR code instead");
    layout->addWidget(qrButton);

    // Code and explanation side by side: the screen is wide, not tall
    QHBoxLayout *qrRow = new QHBoxLayout;
    qrLabel = new QLabel;
    qrLabel->setAlignment(Qt::AlignCenter);
    qrLabel->hide();
    qrRow->addWidget(qrLabel);

    qrHelp = new QLabel("On your main phone open WhatsApp: Settings → Linked devices → "
                        "Link a device, and scan this code.");
    qrHelp->setWordWrap(true);
    qrHelp->hide();
    qrRow->addWidget(qrHelp, 1);
    layout->addLayout(qrRow);

    statusLabel = new QLabel;
    statusLabel->setWordWrap(true);
    layout->addWidget(statusLabel);

    layout->addStretch();

    scrollArea = new QScrollArea;
    scrollArea->setWidget(page);
    scrollArea->setWidgetResizable(true);
    QScroller::grabGesture(scrollArea->viewport(), QScroller::LeftMouseButtonGesture);
    setCentralWidget(scrollArea);

    connect(codeButton, SIGNAL(clicked()), this, SLOT(requestCode()));
    connect(phoneEdit, SIGNAL(returnPressed()), this, SLOT(requestCode()));
    connect(qrButton, SIGNAL(clicked()), this, SLOT(requestQR()));

    connect(Client::connection, SIGNAL(pairingCode(QString)), this, SLOT(pairingCode(QString)));
    connect(Client::connection, SIGNAL(qrCode(QString)), this, SLOT(qrCode(QString)));
    connect(Client::connection, SIGNAL(linkError(QString)), this, SLOT(linkError(QString)));
    connect(Client::connection, SIGNAL(stateChanged(QString)), this, SLOT(stateChanged(QString)));
}

void LinkWindow::requestCode()
{
    // International format without "+" or "00"
    QString phone = phoneEdit->text();
    phone.remove(QRegExp("[^0-9+]"));
    if (phone.startsWith("+"))
        phone.remove(0, 1);
    else if (phone.startsWith("00"))
        phone.remove(0, 2);
    else if (phone.startsWith("0") || phone.length() < 7)
    {
        statusLabel->setText("Please enter the number with its country code, e.g. +43 660 1234567.");
        return;
    }

    if (phone.contains('+') || phone.length() < 7)
    {
        statusLabel->setText("This is not a valid phone number.");
        return;
    }

    codeButton->setEnabled(false);
    statusLabel->setText("Asking WhatsApp for a code...");
    Client::connection->pairPhone(phone);
}

void LinkWindow::requestQR()
{
    qrWanted = true;
    qrButton->setEnabled(false);
    statusLabel->setText("Asking WhatsApp for a QR code...");
    Client::connection->requestQR();
}

void LinkWindow::pairingCode(QString code)
{
    codeButton->setEnabled(true);
    statusLabel->clear();

    if (code.length() == 8)
        code = code.left(4) + "-" + code.mid(4);

    codeLabel->setText(code);
    codeLabel->show();
    codeHelp->show();
}

void LinkWindow::qrCode(QString code)
{
    if (!qrWanted)
        return;

    if (code.isEmpty())
    {
        // The codes ran out
        qrLabel->hide();
        qrHelp->hide();
        qrButton->setText("New QR code");
        qrButton->setEnabled(true);
        if (!linked)
            statusLabel->setText("The QR code has expired.");
        return;
    }

    // As large as the screen allows, but it has to fit without scrolling
    int size = qBound(200, scrollArea->viewport()->height() - 20, 420);
    QImage image = qrImage(code, size);
    if (image.isNull())
        qrLabel->setText(code);
    else
        qrLabel->setPixmap(QPixmap::fromImage(image));

    qrLabel->show();
    qrHelp->show();
    statusLabel->clear();

    QTimer::singleShot(0, this, SLOT(showQR()));
}

void LinkWindow::showQR()
{
    scrollArea->ensureWidgetVisible(qrLabel, 0, 10);
}

void LinkWindow::linkError(QString error)
{
    codeButton->setEnabled(true);
    qrButton->setEnabled(true);
    statusLabel->setText("WhatsApp: " + error);
}

void LinkWindow::stateChanged(QString state)
{
    if (state == "connecting" || state == "connected")
    {
        linked = true;
        statusLabel->setText("Linked. Connecting...");
        qrLabel->hide();
        codeLabel->hide();

        if (state == "connected")
        {
            QMaemo5InformationBox::information(parentWidget(),
                                               "Yappari is now linked with your WhatsApp account.");
            close();
        }
    }
}

QImage LinkWindow::qrImage(const QString &text, int maxSize)
{
    QByteArray data = text.toUtf8();
    QRcode *qr = QRcode_encodeString(data.constData(), 0, QR_ECLEVEL_L, QR_MODE_8, 1);

    if (!qr)
        return QImage();

    const int border = 4;
    int modules = qr->width + 2 * border;
    int scale = qMax(2, maxSize / modules);
    int size = modules * scale;

    QImage image(size, size, QImage::Format_RGB32);
    image.fill(Qt::white);

    QPainter painter(&image);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::black);

    for (int y = 0; y < qr->width; y++)
        for (int x = 0; x < qr->width; x++)
            if (qr->data[y * qr->width + x] & 1)
                painter.drawRect((x + border) * scale, (y + border) * scale, scale, scale);

    painter.end();
    QRcode_free(qr);

    return image;
}
