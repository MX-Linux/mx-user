QTDIR = /usr/share/qt4
TEMPLATE = app
TARGET = mx-user
TRANSLATIONS += translations/mx-user_ca.ts \
                translations/mx-user_de.ts \
                translations/mx-user_es.ts \
                translations/mx-user_fr.ts \
                translations/mx-user_ja.ts \
                translations/mx-user_nl.ts \
                translations/mx-user_pt_BR.ts \ 
                translations/mx-user_ro.ts \ 
                translations/mx-user_sv.ts
FORMS += meconfig.ui
HEADERS += mconfig.h 
SOURCES += main.cpp mconfig.cpp
LIBS += -lcrypt
CONFIG += release warn_on thread qt
QT += webkit
