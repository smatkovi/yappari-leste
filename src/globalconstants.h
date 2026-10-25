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

#ifndef GLOBALCONSTANTS_H
#define GLOBALCONSTANTS_H

// Application constants

#define YAPPARI_APPLICATION_NAME    "Yappari"

// Icons

#define YAPPARI_ICON                "yappari"
#define YAPPARI_NOTIFICATION_ICON   "yappari-notification"

// WhatsApp servers

#define JID_DOMAIN              "s.whatsapp.net"
#define GROUP_DOMAIN            "g.us"
#define URL_LOCATION_SHARING    "https://maps.google.com/maps?q=loc:"

// DBus services

// The Yappari UI (single instance, ShowWindow)
#define YAPPARI_SERVICE                         "org.yappari.Yappari"
#define YAPPARI_OBJECT                          "/org/yappari/Yappari"

// The WhatsApp connection daemon yappari-wa (see backend/main.go)
#define WA_SERVICE                              "org.yappari.WhatsApp"
#define WA_OBJECT                               "/org/yappari/WhatsApp"
#define WA_INTERFACE                            "org.yappari.WhatsApp"

#define HD_NOTIFICATION_MANAGER_DBUS_NAME       "org.freedesktop.Notifications"
#define HD_NOTIFICATION_MANAGER_DBUS_PATH       "/org/freedesktop/Notifications"

#define HD_SV_NOTIFICATION_DAEMON_DBUS_NAME     "com.nokia.HildonSVNotificationDaemon"
#define HD_SV_NOTIFICATION_DAEMON_DBUS_PATH     "/com/nokia/HildonSVNotificationDaemon"

// Settings (QSettings: ~/.config/yappari/yappari.conf)

#define SETTINGS_ORGANIZATION               "yappari"
#define SETTINGS_APPLICATION                "yappari"

#define SETTINGS_NICKCOLOR                  "nickcolor"
#define SETTINGS_MYCOLOR                    "mycolor"
#define SETTINGS_TEXTCOLOR                  "textcolor"
#define SETTINGS_SHOW_NICKNAMES             "shownicknames"
#define SETTINGS_SHOW_NUMBERS               "shownumbers"
#define SETTINGS_POPUP_ON_FIRST_MESSAGE     "popuponfirstmessage"
#define SETTINGS_BLUE_CHECKS                "bluechecks"
#define SETTINGS_AUTOMATIC_DOWNLOAD         "automaticdownload"
#define SETTINGS_ENTER_IS_SEND              "enterissend"
#define SETTINGS_LAST_FILE_DIR              "lastfiledir"
#define SETTINGS_RECENT_EMOJI               "recentemoji"

// Default settings values

#define DEFAULT_SHOW_NICKNAMES              false
#define DEFAULT_SHOW_NUMBERS                true
#define DEFAULT_POPUP_ON_FIRST_MESSAGE      false
#define DEFAULT_BLUE_CHECKS                 true
#define DEFAULT_AUTOMATIC_DOWNLOAD          100
#define DEFAULT_ENTER_IS_SEND               true

// Directories, below $XDG_DATA_HOME/yappari (~/.local/share/yappari)

#define LOGS_DIR        "/logs/"

// Status icons in conversations

#define VOICEPLAYED     "<img src=\"/usr/share/yappari/icons/17x13/yappari-voiceplayed.png\" />"
#define GRAYCHECK       "<img src=\"/usr/share/yappari/icons/17x13/yappari-graycheck.png\" />"
#define CHECK           "<img src=\"/usr/share/yappari/icons/17x13/yappari-greencheck.png\" />"
#define DOUBLECHECK     "<img src=\"/usr/share/yappari/icons/17x13/yappari-greendoublecheck.png\" />"
#define DOUBLEBLUECHECK "<img src=\"/usr/share/yappari/icons/17x13/yappari-bluedoublecheck.png\" />"
#define UPLOADING       "<img src=\"/usr/share/yappari/icons/17x13/yappari-uploading.png\" />"

// Image sizes

#define PREVIEW_WIDTH      224
#define PREVIEW_HEIGHT     224

// Largest file WhatsApp accepts for sending here

#define MAX_FILE_SIZE      (64 * 1024 * 1024)

#endif // GLOBALCONSTANTS_H
