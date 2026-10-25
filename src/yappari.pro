# Yappari for Maemo Leste: Qt 5 port of the Yappari WhatsApp client.
#
# The WhatsApp connection is the yappari-wa daemon in ../backend
# (Go, whatsmeow), reached over D-Bus; see WhatsApp/waconnection.h.

QT          += core gui widgets sql dbus multimedia maemo5 x11extras
CONFIG      += c++11 link_pkgconfig
PKGCONFIG   += libqrencode x11

TARGET = yappari
TEMPLATE = app

isEmpty(VERSION_STRING): VERSION_STRING = 3.0.0
DEFINES += VERSION=\\\"$$VERSION_STRING\\\"

INCLUDEPATH += .

SOURCES += main.cpp \
    client.cpp \
    FMessage/fmessage.cpp \
    FMessage/key.cpp \
    FMessage/receipt.cpp \
    util/utilities.cpp \
    util/datetimeutilities.cpp \
    WhatsApp/waconnection.cpp \
    Contacts/contact.cpp \
    Contacts/contactlist.cpp \
    Contacts/contactlistiterator.cpp \
    Contacts/contactroster.cpp \
    Contacts/group.cpp \
    Sql/chatlogger.cpp \
    Sql/conversationsdb.cpp \
    Sql/rosterdbmanager.cpp \
    Dbus/dbusifadaptor.cpp \
    Dbus/dbusnotificationsif.cpp \
    Dbus/dbusnokiahildonsvndif.cpp \
    Dbus/notifyobject.cpp \
    Multimedia/audioplayer.cpp \
    Gui/aboutdialog.cpp \
    Gui/chatarea.cpp \
    Gui/chatdisplaydelegate.cpp \
    Gui/chatdisplayitem.cpp \
    Gui/chatimageitem.cpp \
    Gui/chattextedit.cpp \
    Gui/chattextitem.cpp \
    Gui/chatwindow.cpp \
    Gui/contactdisplaydelegate.cpp \
    Gui/contactdisplayitem.cpp \
    Gui/contactselectionmodel.cpp \
    Gui/emojidialog.cpp \
    Gui/globalsettingsdialog.cpp \
    Gui/imageviewer.cpp \
    Gui/linkwindow.cpp \
    Gui/mainwindow.cpp \
    Gui/mutedialog.cpp \
    Gui/selectcontactdialog.cpp

HEADERS += client.h \
    globalconstants.h \
    version.h \
    FMessage/fmessage.h \
    FMessage/key.h \
    FMessage/receipt.h \
    util/utilities.h \
    util/datetimeutilities.h \
    WhatsApp/waconnection.h \
    Contacts/contact.h \
    Contacts/contactlist.h \
    Contacts/contactlistiterator.h \
    Contacts/contactroster.h \
    Contacts/group.h \
    Sql/chatlogger.h \
    Sql/conversationsdb.h \
    Sql/rosterdbmanager.h \
    Dbus/dbusifadaptor.h \
    Dbus/dbusnotificationsif.h \
    Dbus/dbusnokiahildonsvndif.h \
    Dbus/notifyobject.h \
    Multimedia/audioplayer.h \
    Gui/aboutdialog.h \
    Gui/chatarea.h \
    Gui/chatdisplaydelegate.h \
    Gui/chatdisplayitem.h \
    Gui/chatimageitem.h \
    Gui/chattextedit.h \
    Gui/chattextitem.h \
    Gui/chatwindow.h \
    Gui/contactdisplaydelegate.h \
    Gui/contactdisplayitem.h \
    Gui/contactselectionmodel.h \
    Gui/emojidialog.h \
    Gui/globalsettingsdialog.h \
    Gui/imageviewer.h \
    Gui/linkwindow.h \
    Gui/mainwindow.h \
    Gui/mutedialog.h \
    Gui/selectcontactdialog.h

FORMS += \
    Gui/ui/aboutdialog.ui \
    Gui/ui/chatimageitem.ui \
    Gui/ui/chatwindow.ui \
    Gui/ui/globalsettingsdialog.ui \
    Gui/ui/mainwindow.ui \
    Gui/ui/mutedialog.ui \
    Gui/ui/selectcontactdialog.ui

# Installation (the daemon, desktop entry and D-Bus services are installed
# by debian/rules and debian/install)

target.path = /usr/bin

icon16.path = /usr/share/icons/hicolor/16x16/apps
icon16.files = icons/16x16/yappari.png

icon48.path = /usr/share/icons/hicolor/48x48/apps
icon48.files = icons/48x48/yappari.png icons/48x48/yappari-notification.png

icon64.path = /usr/share/icons/hicolor/64x64/apps
icon64.files = icons/64x64/yappari.png

sharedicons.path = /usr/share/yappari/icons
sharedicons.files = icons/17x13 icons/100x100 icons/64x64

INSTALLS += target icon16 icon48 icon64 sharedicons
