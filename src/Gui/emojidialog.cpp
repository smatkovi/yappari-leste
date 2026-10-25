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

#include <QGridLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QScroller>

#include "globalconstants.h"
#include "client.h"

#include "emojidialog.h"

#define COLUMNS         8
#define MAX_RECENT      16

static const char *emojiList[] = {
    "😀", "😃", "😄", "😁", "😆", "😅", "😂", "🤣",
    "😊", "😇", "🙂", "😉", "😌", "😍", "🥰", "😘",
    "😋", "😛", "😜", "🤪", "😎", "🤓", "🥳", "😏",
    "😒", "😞", "😔", "😟", "😕", "🙁", "😣", "😖",
    "😫", "😩", "🥺", "😢", "😭", "😤", "😠", "😡",
    "🤯", "😳", "😱", "😨", "😰", "🤔", "🤭", "🤫",
    "😶", "😐", "😑", "😬", "🙄", "😯", "😴", "🤤",
    "😷", "🤒", "🤕", "🤢", "🤮", "🤧", "😵", "🤠",
    "👍", "👎", "👌", "✌️", "🤞", "🤝", "👏", "🙌",
    "👋", "🙏", "💪", "👀", "🤷", "🤦", "🎉", "🎂",
    "❤️", "🧡", "💛", "💚", "💙", "💜", "🖤", "💔",
    "💯", "🔥", "⭐", "✨", "☀️", "🌧️", "❄️", "🌈",
    "☕", "🍺", "🍷", "🍕", "🍔", "🍰", "🍫", "🍎",
    "🐶", "🐱", "🐭", "🐰", "🦊", "🐻", "🐼", "🐸",
    "🚗", "🚆", "✈️", "🏠", "⏰", "📱", "💻", "📷",
    "✅", "❌", "❓", "❗", "➡️", "⬅️", "🆗", "💤",
    0
};

EmojiDialog::EmojiDialog(QWidget *parent) :
    QDialog(parent)
{
    setWindowTitle("Emoji");

    QStringList emojis = Client::settings->value(SETTINGS_RECENT_EMOJI).toStringList();
    for (int i = 0; emojiList[i]; i++)
    {
        QString emoji = QString::fromUtf8(emojiList[i]);
        if (!emojis.contains(emoji))
            emojis.append(emoji);
    }

    QWidget *grid = new QWidget;
    QGridLayout *layout = new QGridLayout(grid);
    layout->setSpacing(0);

    QFont font = this->font();
    font.setPointSize(font.pointSize() * 3 / 2);

    for (int i = 0; i < emojis.size(); i++)
    {
        QPushButton *button = new QPushButton(emojis.at(i));
        button->setFont(font);
        button->setMinimumHeight(70);
        connect(button, SIGNAL(clicked()), this, SLOT(emojiClicked()));
        layout->addWidget(button, i / COLUMNS, i % COLUMNS);
    }

    QScrollArea *scrollArea = new QScrollArea;
    scrollArea->setWidget(grid);
    scrollArea->setWidgetResizable(true);
    scrollArea->setMinimumHeight(330);
    QScroller::grabGesture(scrollArea->viewport(), QScroller::LeftMouseButtonGesture);

    QVBoxLayout *main = new QVBoxLayout(this);
    main->addWidget(scrollArea);
}

QString EmojiDialog::selectedEmoji() const
{
    return selected;
}

void EmojiDialog::emojiClicked()
{
    QPushButton *button = qobject_cast<QPushButton *>(sender());
    if (!button)
        return;

    selected = button->text();

    QStringList recent = Client::settings->value(SETTINGS_RECENT_EMOJI).toStringList();
    recent.removeAll(selected);
    recent.prepend(selected);
    while (recent.size() > MAX_RECENT)
        recent.removeLast();
    Client::settings->setValue(SETTINGS_RECENT_EMOJI, recent);

    accept();
}
