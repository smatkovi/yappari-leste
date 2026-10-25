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
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QDateTime>

#include "util/utilities.h"

#include "chattextedit.h"
#include "client.h"

#define MAX_LINES       5

ChatTextEdit::ChatTextEdit(QWidget *parent) :
    QTextEdit(parent)
{
    // Messages are plain text; emoji are characters
    setAcceptRichText(false);

    QFontMetrics metrics(currentFont());
    fontHeight = metrics.height();

    isComposing = false;
    lastKeyPressed = 0;

    connect(&composingTimer,SIGNAL(timeout()),
            this,SLOT(verifyPaused()));

    connect(this,SIGNAL(textChanged()),
            this,SLOT(textChanged()));

    installEventFilter(this);
}


bool ChatTextEdit::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyPress)
    {
        QKeyEvent *keyEvent = (QKeyEvent *) event;

        if (keyEvent->key() == Qt::Key_Up)
        {
            // Key up
            QTextCursor cursor = textCursor();
            int pos = cursor.position();
            bool sel = keyEvent->modifiers() == Qt::ShiftModifier;
            cursor.movePosition(QTextCursor::Up, (sel ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor));

            if (pos == cursor.position())
                cursor.movePosition(QTextCursor::Start, (sel ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor));

            setTextCursor(cursor);

            return true;
        }
        else  if (keyEvent->key() == Qt::Key_Down)
        {
            // Key down
            QTextCursor cursor = textCursor();
            int pos = cursor.position();
            bool sel = keyEvent->modifiers() == Qt::ShiftModifier;
            cursor.movePosition(QTextCursor::Down, (sel ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor));

            if (pos == cursor.position())
                cursor.movePosition(QTextCursor::End, (sel ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor));

            setTextCursor(cursor);
            return true;
        }
        else if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter)
        {
            // Return pressed
            if (Client::enterIsSend && !(keyEvent->modifiers() & Qt::ShiftModifier))
            {
                isComposing = false;
                composingTimer.stop();
                emit returnPressed();
                return true;
            }
        }
        else if (!keyEvent->text().isEmpty())
        {
            lastKeyPressed = QDateTime::currentMSecsSinceEpoch();
            composingTimer.start(2000);

            if (!isComposing)
            {
                isComposing = true;
                emit composing();
            }
        }
    }
    else if (event->type() == QEvent::InputMethod)
    {
        QInputMethodEvent *inputEvent = (QInputMethodEvent *) event;

        // Virtual keyboard: its Enter key commits a line break
        if (inputEvent->commitString() == "\n" && Client::enterIsSend)
        {
            isComposing = false;
            composingTimer.stop();
            emit returnPressed();
            return true;
        }
    }

    return QTextEdit::eventFilter(obj,event);
}

void ChatTextEdit::verifyPaused()
{
    if ((QDateTime::currentMSecsSinceEpoch() - lastKeyPressed) >= 2000)
    {
        composingTimer.stop();
        if (isComposing)
        {
            isComposing = false;
            emit paused();
        }
    }
}

void ChatTextEdit::textChanged()
{
    int editHeight = size().height();
    double textLines = (editHeight - 39) / fontHeight;

    int docHeight = document()->size().height();

    QFontMetrics metrics(currentFont());

    fontHeight = metrics.height();

    double docLines = (docHeight - 8) / fontHeight;

    if (qAbs(docLines) < MAX_LINES && qAbs(docLines) != qAbs(textLines))
    {
        setFixedHeight((docLines * fontHeight) + 39);
        update();
    }
}
