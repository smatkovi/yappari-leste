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

#include "globalsettingsdialog.h"
#include "ui_globalsettingsdialog.h"

#include "globalconstants.h"

#include "client.h"

#define MAX_BYTES       65536

GlobalSettingsDialog::GlobalSettingsDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::GlobalSettingsDialog)
{
    ui->setupUi(this);

    // Configure the checkboxes

    ui->nicknamesCheckBox->setChecked(Client::showNicknames);
    ui->shownumbersCheckBox->setChecked(Client::showNumbers);
    ui->popupCheckbox->setChecked(Client::popupOnFirstMessage);
    ui->blueChecksCheckbox->setChecked(Client::blueChecks);
    ui->enterIsSendCheckbox->setChecked(Client::enterIsSend);

    // Configure automatic download bytes

    ui->automaticDownloadLineEdit->setText(QString::number(Client::automaticDownloadBytes));
}

GlobalSettingsDialog::~GlobalSettingsDialog()
{
    delete ui;
}

bool GlobalSettingsDialog::getShowNicknames()
{
    return ui->nicknamesCheckBox->isChecked();
}

bool GlobalSettingsDialog::getShowNumbers()
{
    return ui->shownumbersCheckBox->isChecked();
}

bool GlobalSettingsDialog::getPopupOnFirstMessage()
{
    return ui->popupCheckbox->isChecked();
}

bool GlobalSettingsDialog::getBlueChecks()
{
    return ui->blueChecksCheckbox->isChecked();
}

bool GlobalSettingsDialog::getEnterIsSend()
{
    return ui->enterIsSendCheckbox->isChecked();
}

int GlobalSettingsDialog::getAutomaticDownloadBytes()
{
    int bytes = ui->automaticDownloadLineEdit->text().toInt();

    if (bytes > MAX_BYTES)
        bytes = MAX_BYTES;
    else if (bytes < 0)
        bytes = 0;  // disable this feature

    return bytes;
}
