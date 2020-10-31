//
//   Copyright (C) 2003-2008 by Warren Woodford
//
//   Licensed under the Apache License, Version 2.0 (the "License");
//   you may not use this file except in compliance with the License.
//   You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the License is distributed on an "AS IS" BASIS,
//   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//   See the License for the specific language governing permissions and
//   limitations under the License.
//

#include <QApplication>
#include <QTranslator>
#include <QLocale>
#include "mainwindow.h"
#include <unistd.h>

int main( int argc, char ** argv ) {
    QApplication app(argc, argv);
    app.setWindowIcon(QIcon::fromTheme("mx-user"));

    QTranslator qtTran;
    qtTran.load(QString("qt_") + QLocale::system().name());
    app.installTranslator(&qtTran);
    QTranslator appTran;
    appTran.load(QString("mx-user_") + QLocale::system().name(), "/usr/share/mx-user/locale");
    app.installTranslator(&appTran);

    if (getuid() == 0) {
        MainWindow mw;
        mw.show();
        return app.exec();
    } else {
        system("su-to-root -X -c " + QCoreApplication::applicationFilePath().toUtf8() + "&");
//        QApplication::beep();
//        QMessageBox::critical(nullptr, QString::null,
//                              QApplication::tr("You must run this program as root."));
//        return EXIT_FAILURE;
    }
}

