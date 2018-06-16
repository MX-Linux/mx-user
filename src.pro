QT       += core gui widgets

TEMPLATE = app
TARGET = mx-user

TRANSLATIONS += translations/mx-user_am.ts \
                translations/mx-user_ca.ts \
                translations/mx-user_cs.ts \
                translations/mx-user_de.ts \
                translations/mx-user_el.ts \
                translations/mx-user_es.ts \
                translations/mx-user_fi.ts \
                translations/mx-user_fr.ts \
                translations/mx-user_hi.ts \
                translations/mx-user_hr.ts \
                translations/mx-user_hu.ts \
                translations/mx-user_it.ts \
                translations/mx-user_ja.ts \
                translations/mx-user_kk.ts \
                translations/mx-user_lt.ts \
                translations/mx-user_nl.ts \
                translations/mx-user_pl.ts \
                translations/mx-user_pt.ts \
                translations/mx-user_pt_BR.ts \
                translations/mx-user_ro.ts \
                translations/mx-user_ru.ts \
                translations/mx-user_sk.ts \
                translations/mx-user_sv.ts \
                translations/mx-user_tr.ts \
                translations/mx-user_uk.ts \
                translations/mx-user_zh_TW.ts

FORMS += meconfig.ui
HEADERS += mconfig.h
SOURCES += main.cpp mconfig.cpp
LIBS += -lcrypt
CONFIG += release warn_on thread qt

RESOURCES += \
    images.qrc
