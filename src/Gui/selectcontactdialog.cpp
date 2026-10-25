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

#include <QSortFilterProxyModel>
#include <QContextMenuEvent>
#include <QMessageBox>
#include <QScroller>
#include <QMenu>

#include "selectcontactdialog.h"
#include "ui_selectcontactdialog.h"

#include "Contacts/contactlistiterator.h"

#include "util/utilities.h"

#include "contactdisplayitem.h"
#include "contactdisplaydelegate.h"

#include "globalconstants.h"
#include "client.h"

SelectContactDialog::SelectContactDialog(ContactList list, QWidget *parent,
                                         bool contextMenuEnabled, bool includeGroups) :
    QDialog(parent),
    ui(new Ui::SelectContactDialog)
{
    ui->setupUi(this);

    /*
    QFont font = ui->searchLabel->font();
    font.setBold(true);
    ui->searchLabel->setFont(font);
    */

    model = new ContactSelectionModel(ui->listView);
    QSortFilterProxyModel *proxy = new QSortFilterProxyModel(this);
    proxy->setSourceModel(model);
    ui->listView->setModel(proxy);

    ui->listView->installEventFilter(this);
    this->contextMenuEnabled = contextMenuEnabled;
    QScroller::grabGesture(ui->listView->viewport(), QScroller::LeftMouseButtonGesture);

    proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxy->setFilterRole(Qt::UserRole + 3);
    proxy->setSortRole(Qt::UserRole + 3);
    proxy->setSortCaseSensitivity(Qt::CaseInsensitive);

    ContactDisplayDelegate *delegate = new ContactDisplayDelegate(ui->listView);

    ui->listView->setItemDelegate(delegate);

    contactList = list;
    foreach(Contact *contact, contactList)
    {
        if ((includeGroups || (!includeGroups && contact->type == Contact::TypeContact))
                && contact->jid != Client::myJid)
            model->appendRow(new ContactDisplayItem(contact));
    }

    proxy->sort(0);

    connect(this,SIGNAL(sendRightButtonClicked(QPoint)),
            this,SLOT(contextMenu(QPoint)));

    connect(ui->lineEdit,SIGNAL(textChanged(QString)),
            proxy,SLOT(setFilterFixedString(QString)));

    ui->lineEdit->hide();

    // A tap picks the contact, like the pickers of Maemo 5
    connect(ui->listView,SIGNAL(clicked(QModelIndex)),
            this,SLOT(accept()));

}

SelectContactDialog::~SelectContactDialog()
{
    delete ui;
}

Contact& SelectContactDialog::getSelectedContact()
{
    //ContactDisplayItem *item = (ContactDisplayItem *)
    //        model->item(ui->listView->currentIndex().row());


    QModelIndex index = ui->listView->currentIndex();
    QSortFilterProxyModel *proxy = (QSortFilterProxyModel *) ui->listView->model();

    ContactDisplayItem *item =
            (ContactDisplayItem *) model->itemFromIndex(proxy->mapToSource(index));

    return (*item->getContact());
}

void SelectContactDialog::accept()
{
    if (ui->listView->currentIndex().row() >= 0)
        QDialog::accept();
}

bool SelectContactDialog::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::ContextMenu) {
        if (contextMenuEnabled)
        {
            QContextMenuEvent *menuEvent = static_cast<QContextMenuEvent*> (event);
            emit sendRightButtonClicked(menuEvent->globalPos());
        }
        return true;
    }
    else if (obj != ui->lineEdit && event->type() == QEvent::KeyPress)
    {
        ui->lineEdit->show();
        ui->lineEdit->setFocus();
        ui->lineEdit->event(event);

        return true;
    }
    else
        return QDialog::eventFilter(obj, event);
}

void SelectContactDialog::contextMenu(QPoint p)
{
    QModelIndex index = ui->listView->indexAt(ui->listView->viewport()->mapFromGlobal(p));

    if (index.isValid())
    {
        QString jid = index.data(Qt::UserRole + 1).toString();

        QMenu *menu = new QMenu(this);
        QAction *viewContact = new QAction("View Contact",this);
        menu->addAction(viewContact);

        QAction *action = menu->exec(p);
        if (action == viewContact)
        {
            Contact *c = contactList.value(jid);
            emit showContactInfo(c);
            close();
        }
        menu->deleteLater();
        ui->listView->clearSelection();
    }
}
