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

#include <QDesktopServices>
#include <QImageReader>
#include <QFileInfo>
#include <QMenuBar>
#include <QLabel>
#include <QScrollArea>
#include <QScroller>
#include <QUrl>

#include "imageviewer.h"

ImageViewer::ImageViewer(const QString &fileName, QWidget *parent) :
    QMainWindow(parent)
{
    this->fileName = fileName;

    setProperty("X-Maemo-StackedWindow", 1);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlags(windowFlags() | Qt::Window);
    setWindowTitle(QFileInfo(fileName).fileName());

    QImageReader reader(fileName);
    reader.setAutoTransform(true);
    image = reader.read();

    label = new QLabel;
    label->setAlignment(Qt::AlignCenter);

    scrollArea = new QScrollArea(this);
    scrollArea->setWidget(label);
    scrollArea->setWidgetResizable(true);
    scrollArea->setAlignment(Qt::AlignCenter);
    QScroller::grabGesture(scrollArea->viewport(), QScroller::LeftMouseButtonGesture);
    setCentralWidget(scrollArea);

    menuBar()->addAction("Open in another application", this, SLOT(openExternally()));

    if (image.isNull())
        label->setText("This picture cannot be shown.");
}

void ImageViewer::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    showImage();
}

// Fit to the width of the window, never enlarge
void ImageViewer::showImage()
{
    if (image.isNull())
        return;

    int width = scrollArea->viewport()->width();
    QImage scaled = (image.width() > width)
            ? image.scaledToWidth(width, Qt::SmoothTransformation)
            : image;

    label->setPixmap(QPixmap::fromImage(scaled));
}

void ImageViewer::openExternally()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(fileName));
}
