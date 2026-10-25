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

#include <QTextDocument>
#include <QStandardPaths>
#include <QRegExp>
#include <QDir>

#include "utilities.h"

Utilities::Utilities()
{
}

// Debug output only when YAPPARI_DEBUG is set in the environment
void Utilities::logData(QString s)
{
    static const bool debug = !qgetenv("YAPPARI_DEBUG").isEmpty();

    if (debug)
        qDebug() << qPrintable(s);
}

QString Utilities::dataDir()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    QString dir = base + "/yappari";

    QDir().mkpath(dir);
    return dir;
}

// Turns message text into HTML for the conversation view. Yappari used to
// replace emoji by its own images here; on Leste they are left as text and
// drawn by the system's emoji font (fonts-noto-color-emoji).
QString Utilities::WATextToHtml(QString data, int iconSize, bool urlConversion)
{
    Q_UNUSED(iconSize);

    data.replace("&","&amp;");
    data.replace("<","&lt;");
    data.replace(">","&gt;");
    data.replace("\n","<br>");

    QString result = data;

    if (urlConversion)
    {
        int pos = 0;
        QRegExp urlreg("(https?://[_a-zA-Z0-9./~:?#!$&'+,;=%@-]+)");

        urlreg.setCaseSensitivity(Qt::CaseInsensitive);

        while ((pos = urlreg.indexIn(result,pos)) != -1)
        {
            QString url = "<a href=\"" + urlreg.cap(1) + "\">" +
                          shortURL(urlreg.cap(1)) + "</a>";
            result.replace(pos,urlreg.cap(1).length(),url);

            pos += url.length();
        }

        pos = 0;

        QRegExp wwwreg("(^|\\s)(www\\.[_a-zA-Z0-9./~:?#!$&'+,;=%-]+)");
        wwwreg.setCaseSensitivity(Qt::CaseInsensitive);

        while ((pos = wwwreg.indexIn(result,pos)) != -1)
        {
            QString url = wwwreg.cap(1) + "<a href=\"http://" + wwwreg.cap(2) + "\">" +
                          shortURL(wwwreg.cap(2)) + "</a>";
            result.replace(pos,wwwreg.cap(1).length() + wwwreg.cap(2).length(),url);

            pos += url.length();
        }
    }

    return result;
}

// Emoji are plain text now, nothing to remove
QString Utilities::removeEmoji(QString data)
{
    return data;
}

QString Utilities::shortURL(QString url)
{
    if (url.size() > 40)
    {
        int bytesToRemove = url.size() - 40;
        int pos = url.lastIndexOf("/");

        if (pos < bytesToRemove + 6)
            pos = url.lastIndexOf(".");

        if (pos < bytesToRemove + 6)
            pos = url.size() - 6;

        url = url.left(pos - (bytesToRemove+6)) + "..." + url.right(url.size() - pos);
    }

    return url;
}

QString Utilities::getExtension(QString filename)
{
    QString lower = filename.toLower();
    int index = lower.lastIndexOf('.');

    if (index > 0 && (index + 1) < lower.length())
        return lower.mid(index+1);

    return "unknown";
}

QString Utilities::formatBytes(qint64 bytes)
{
    qreal value = bytes;

    int unit = 0;
    while (value >= 1024)
    {
        unit++;
        value /= 1024;
    }

    QString result;
    if (unit)
        result = QString::asprintf("%4.2f ",value);
    else
        result = QString::number(value) + " ";

    switch (unit)
    {
        case 0:
            result.append("B");
            break;
        case 1:
            result.append("KB");
            break;
        case 2:
            result.append("MB");
            break;
        case 3:
            result.append("GB");
            break;
        case 4:
            result.append("TB");
            break;
        default:
            result = "#####";
            break;
    }

    return result;
}

QString Utilities::htmlToWAText(QString html)
{
    QTextDocument doc;
    doc.setHtml(html);

    return doc.toPlainText();
}
