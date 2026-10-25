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

#ifndef LINKWINDOW_H
#define LINKWINDOW_H

#include <QMainWindow>
#include <QImage>

class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;

/**
    @class      LinkWindow

    @brief      Links Yappari to a WhatsApp account as a linked device.

                Replaces Yappari's old registration by SMS or voice call:
                the user enters the phone number of the account and types
                the 8-character code shown here into WhatsApp on the main
                phone (Linked devices -> Link with phone number), or scans a
                QR code instead.
*/

class LinkWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit LinkWindow(QWidget *parent = 0);

    // The QR code text as a picture (libqrencode)
    static QImage qrImage(const QString &text, int maxSize);

private slots:
    void requestCode();
    void requestQR();
    void pairingCode(QString code);
    void qrCode(QString code);
    void linkError(QString error);
    void showQR();
    void stateChanged(QString state);

private:
    QLineEdit *phoneEdit;
    QPushButton *codeButton;
    QPushButton *qrButton;
    QLabel *codeLabel;
    QLabel *codeHelp;
    QLabel *qrLabel;
    QLabel *qrHelp;
    QLabel *statusLabel;
    QScrollArea *scrollArea;
    bool qrWanted;
    bool linked;
};

#endif // LINKWINDOW_H
