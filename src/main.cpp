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
#include <QMessageBox>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>

#include "client.h"
#include "version.h"
#include "globalconstants.h"

int main(int argc, char *argv[])
{
    QApplication a(argc,argv);
    Client *client = 0;
    int retval = 0;

    if (qgetenv("USER") == "root")
    {
        QMessageBox msg;

        msg.setText("You can't run this application as root.");
        msg.exec();

        return -1;
    }

    a.setApplicationName(YAPPARI_APPLICATION_NAME);
    a.setApplicationVersion(VERSION);
    a.setQuitOnLastWindowClosed(false);

    // Check that Yappari isn't already running. Ask the bus (this does not
    // start Yappari through D-Bus activation) and show the running one.
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.interface() && bus.interface()->isServiceRegistered(YAPPARI_SERVICE))
    {
        QDBusMessage show = QDBusMessage::createMethodCall(YAPPARI_SERVICE, YAPPARI_OBJECT,
                                                           YAPPARI_SERVICE, "ShowWindow");
        bus.call(show, QDBus::Block, 5000);
        return 1;
    }

    bool minimized = a.arguments().contains("-m");

    client = new Client(minimized,&a);
    retval = a.exec();
    delete client;

    return retval;
}
