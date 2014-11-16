//
//   Copyright (C) 2003-2010 by Warren Woodford
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

#ifndef MCONFIG_H
#define MCONFIG_H

#include "ui_meconfig.h"
#include <qlistview.h>
#include <qcheckbox.h>
#include <qmessagebox.h>
#include <qcombobox.h>
#include <qcursor.h>
#include <qprocess.h>
#include <qtimer.h>

class MConfig : public QDialog, public Ui::MEConfig {
    Q_OBJECT
protected:
    QProcess *proc;
    QTimer *timer;

public:
    MConfig(QWidget* parent = 0);
    ~MConfig();
    // helpers
    static QString getCmdOut(QString cmd);
    static QStringList getCmdOuts(QString cmd);
    void displaySite(QString site);
    static QString getCmdValue(QString cmd, QString key, QString keydel, QString valdel);
    static QStringList getCmdValues(QString cmd, QString key, QString keydel, QString valdel);
    static bool replaceStringInFile(QString oldtext, QString newtext, QString filepath);
    // common
    void refresh();
    // special
    void refreshRestore();
    void refreshDesktop();
    void refreshAdd();
    void refreshDelete();
    void refreshGroups();
    void refreshMembership();
    void applyRestore();
    void applyDesktop();
    void applyAdd();
    void applyDelete();
    void applyGroup();
    void applyMembership();
    void buildListGroups();

public slots:
    void syncStart();
    void syncTime();
    void syncDone(int exitCode, QProcess::ExitStatus exitStatus);

    virtual void on_fromUserComboBox_activated();
    virtual void on_userComboBox_activated();
    virtual void on_userComboMembership_activated();
    virtual void on_deleteUserCombo_activated();
    virtual void on_userNameEdit_textEdited();
    virtual void on_groupNameEdit_textEdited();
    virtual void on_deleteGroupCombo_activated();
    virtual void on_tabWidget_currentChanged();
    virtual void on_buttonApply_clicked();
    virtual void on_buttonCancel_clicked();
    virtual void on_buttonAbout_clicked();
    virtual void on_buttonHelp_clicked();

protected:

private:
    static bool hasInternetConnection();
    static void executeChild(const char* cmd, const char* param);
protected slots:
    /*$PROTECTED_SLOTS$*/

};

#endif

