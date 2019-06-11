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

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "ui_mainwindow.h"
#include <QMessageBox>

#include "cmd.h"

class MainWindow : public QDialog, public Ui::MEConfig {
    Q_OBJECT

public:
    MainWindow(QWidget* parent = 0);
    ~MainWindow();

    // helpers
    static bool replaceStringInFile(QString oldtext, QString newtext, QString filepath);
    // common
    void refresh();
    // special
    void refreshRestore();
    void refreshDesktop();
    void refreshAdd();
    void refreshDelete();
    void refreshChangePass();
    void refreshGroups();
    void refreshMembership();
    void refreshRename();
    void applyRestore();
    void applyDesktop();
    void applyAdd();
    void applyChangePass();
    void applyDelete();
    void applyGroup();
    void applyMembership();
    void applyRename();
    void buildListGroups();
    void displayDoc(QString url);
    void restartPanel(QString user);

public slots:
    void progress(int counter, int duration); // updates progressBar when tick signal is emited
    void syncDone(int errorCode);

private slots:
    void on_buttonAbout_clicked();
    void on_buttonApply_clicked();
    void on_buttonCancel_clicked();
    void on_buttonHelp_clicked();
    void on_comboChangePass_activated(QString);
    void on_comboDeleteUser_activated(QString);
    void on_comboRenameUser_activated(QString);
    void on_copyRadioButton_clicked();
    void on_deleteGroupCombo_activated(QString);
    void on_docsRadioButton_clicked();
    void on_entireRadioButton_clicked();
    void on_fromUserComboBox_activated(QString);
    void on_groupNameEdit_textEdited();
    void on_lineEditChangePassConf_textChanged(const QString &arg1);
    void on_lineEditChangePass_textChanged();
    void on_mozillaRadioButton_clicked();
    void on_sharedRadioButton_clicked();
    void on_syncRadioButton_clicked();
    void on_tabWidget_currentChanged();
    void on_toUserComboBox_activated(QString);
    void on_toUserComboBox_currentIndexChanged(const QString &arg1);
    void on_userComboBox_activated(QString);
    void on_userComboMembership_activated(QString);
    void on_userNameEdit_textEdited();
    void on_userPassword2Edit_textChanged(const QString &arg1);
    void on_userPasswordEdit_textChanged();

private:
    Cmd *shell;

};

#endif

