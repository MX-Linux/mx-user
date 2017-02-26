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
#include <QMessageBox>
#include <QProcess>
#include <QTimer>

class MConfig : public QDialog, public Ui::MEConfig {
    Q_OBJECT
protected:
    QProcess *proc;
    QTimer *timer;


public:
    MConfig(QWidget* parent = 0);
    ~MConfig();
    QMessageBox *mbox;

    // helpers
    static QString getCmdOut(QString cmd);
    static QStringList getCmdOuts(QString cmd);
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
    void refreshChangePass();
    void refreshClean();    
    void refreshGroups();
    void refreshMembership();
    void applyRestore();
    void applyDesktop();
    void applyAdd();
    void applyChangePass();
    void applyDelete();
    void applyGroup();
    void applyMembership();
    void applyClean();
    void buildListGroups();
    void restartPanel(QString user);
    QString getVersion(QString name);


public slots:
    void syncStart();
    void syncTime();
    void syncDone(int exitCode, QProcess::ExitStatus exitStatus);
    void installDone(int exitCode, QProcess::ExitStatus exitStatus);

    virtual void on_fromUserComboBox_activated();
    virtual void on_userComboBox_activated();
    virtual void on_userComboMembership_activated();
    virtual void on_deleteUserCombo_activated();
    virtual void on_userNameEdit_textEdited();
    virtual void on_groupNameEdit_textEdited();
    virtual void on_deleteGroupCombo_activated();
    virtual void on_userCleanCB_activated();
    virtual void on_tabWidget_currentChanged();
    virtual void on_buttonApply_clicked();
    virtual void on_buttonCancel_clicked();
    virtual void on_buttonAbout_clicked();
    virtual void on_buttonHelp_clicked();

protected:

private:
    static bool hasInternetConnection();
protected slots:
    /*$PROTECTED_SLOTS$*/

private slots:
    void on_baobabPushButton_clicked();
    void on_radioHorizontalPanel_clicked();
    void on_radioVerticalPanel_clicked();
    void on_checkXfce_clicked(bool checked);
    void on_comboChangePass_activated();
};

#endif

