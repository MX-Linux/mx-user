//
//   Copyright (C) 2003-2010 by Warren Woodford
//   Copyright (C) 2014 by Timothy E. Harris
//   for modifications applicable to the MX Linux project.
//
//   Heavily modified by Adrian adrian@mxlinux.org
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

#include "about.h"
#include "mainwindow.h"
#include "version.h"

#include <QDebug>
#include <QFileDialog>
#include <QTextEdit>
#include <QTimer>


MainWindow::MainWindow(QWidget* parent) : QDialog(parent)
{
    qDebug().noquote() << QCoreApplication::applicationName() << "version:" << VERSION;
    setupUi(this);
    setWindowFlags(Qt::Window); // for the close, min and max buttons
    setWindowIcon(QApplication::windowIcon());

    shell = new Cmd(this);
    tabWidget->blockSignals(true);
    tabWidget->setCurrentIndex(0);
    tabWidget->blockSignals(false);
    refresh();
}

MainWindow::~MainWindow(){
}

/////////////////////////////////////////////////////////////////////////
// util functions
bool MainWindow::replaceStringInFile(QString oldtext, QString newtext, QString filepath)
{

    QString cmd = QString("sed -i 's/%1/%2/g' %3").arg(oldtext).arg(newtext).arg(filepath);
    if (system(cmd.toUtf8()) != 0) {
        return false;
    }
    return true;
}


/////////////////////////////////////////////////////////////////////////
// common

void MainWindow::refresh()
{
    setCursor(QCursor(Qt::ArrowCursor));
    syncProgressBar->setValue(0);
    int i = tabWidget->currentIndex();

    switch (i) {
    case 1:
        refreshRestore();
        buttonApply->setEnabled(false);
        break;

    case 2:
        refreshDesktop();
        buttonApply->setEnabled(true);
        break;

    case 3:
        refreshGroups();
        buttonApply->setEnabled(false);
        break;

    case 4:
        refreshMembership();
        buttonApply->setEnabled(false);
        break;

    default:
        refreshAdd();
        refreshDelete();
        refreshChangePass();
        refreshRename();
        users = shell->getCmdOut("lslogins --noheadings -u -o user |  grep -vw root").split("\n");
        users.sort();
        comboRenameUser->addItems(users);
        comboChangePass->addItems(users);
        comboDeleteUser->addItems(users);
        buttonApply->setEnabled(false);
        break;
    }
}

void MainWindow::refreshRestore()
{
    userComboBox->clear();
    userComboBox->addItem(tr("none"));
    userComboBox->addItems(users);
    checkGroups->setChecked(false);
    checkMozilla->setChecked(false);
    radioAutologinNo->setAutoExclusive(false);
    radioAutologinNo->setChecked(false);
    radioAutologinNo->setAutoExclusive(true);
    radioAutologinYes->setAutoExclusive(false);
    radioAutologinYes->setChecked(false);
    radioAutologinYes->setAutoExclusive(true);
}

void MainWindow::refreshDesktop()
{
    fromUserComboBox->clear();
    fromUserComboBox->addItems(users);
    copyRadioButton->setChecked(true);
    entireRadioButton->setChecked(true);
    on_fromUserComboBox_activated("");
}

void MainWindow::refreshAdd()
{
    userNameEdit->clear();
    userPasswordEdit->clear();
    userPassword2Edit->clear();
    addUserBox->setEnabled(true);
}

void MainWindow::refreshDelete()
{
    comboDeleteUser->clear();
    comboDeleteUser->addItem(tr("none"));
    deleteHomeCheckBox->setChecked(false);
    deleteUserBox->setEnabled(true);
}

void MainWindow::refreshChangePass()
{
    comboChangePass->clear();
    comboChangePass->addItem(tr("none"));
    changePasswordBox->setEnabled(true);
    comboChangePass->addItem("root");
    lineEditChangePass->clear();
    lineEditChangePassConf->clear();
}


void MainWindow::refreshGroups()
{
    groupNameEdit->clear();
    addBox->setEnabled(true);
    deleteGroupCombo->clear();
    deleteGroupCombo->addItem(tr("none"));
    deleteBox->setEnabled(true);
    QStringList groups = shell->getCmdOut("cat /etc/group | cut -f 1 -d :").split("\n");
    groups.removeAll("root");
    groups.sort();
    deleteGroupCombo->addItems(groups);
}

void MainWindow::refreshMembership()
{
    userComboMembership->clear();
    userComboMembership->addItem(tr("none"));
    listGroups->clear();
    userComboMembership->addItems(users);
}

void MainWindow::refreshRename()
{
    renameUserNameEdit->clear();
    comboRenameUser->clear();
    comboRenameUser->addItem(tr("none"));
    renameUserBox->setEnabled(true);
}


// apply but do not close
void MainWindow::applyRestore()
{
    QString user = userComboBox->currentText();
    if (user.compare(tr("none")) == 0) {
        // no user selected
        return;
    }
    QString home = user;
    if (user.compare("root") != 0) {
        home = QString("/home/%1").arg(user);
    }
    QString cmd;

    if (checkGroups->isChecked() || checkMozilla->isChecked()) {
        int ans = QMessageBox::warning(this, windowTitle(),
                                       tr("The user configuration will be repaired. Please close all other applications now. When finished, please logout or reboot. Are you sure you want to repair now?"),
                                       QMessageBox::Yes, QMessageBox::No);
        if (ans != QMessageBox::Yes) {
            return;
        }
    }
    setCursor(QCursor(Qt::WaitCursor));

    // restore groups
    if (checkGroups->isChecked() && user.compare("root") != 0) {
        buildListGroups();
        cmd = QString("sed -n '/^EXTRA_GROUPS=/s/^EXTRA_GROUPS=//p' /etc/adduser.conf | sed  -e 's/ /,/g' -e 's/\"//g'");
        QStringList extra_groups_list = shell->getCmdOut(cmd).split(",");
        QStringList new_group_list;
        for (const QString &group : extra_groups_list) {
            if (!listGroups->findItems(group, Qt::MatchExactly).isEmpty()) {
                new_group_list << group;
            }
        }
        cmd = "usermod -G '' " + user + "; usermod -G " + new_group_list.join(",") + " " + user; // reset group and add extra from /etc/adduser.conf
        system(cmd.toUtf8());
        QMessageBox::information(this, windowTitle(),
                                 tr("User group membership was restored."));
    }
    // restore Mozilla configs
    if (checkMozilla->isChecked()) {
        cmd = QString("/bin/rm -r %1/.mozilla").arg(home);
        system(cmd.toUtf8());
        QMessageBox::information(this, windowTitle(),
                                 tr("Mozilla settings were reset."));
    }
    if (radioAutologinNo->isChecked()) {
        if (QFile::exists("/etc/lightdm/lightdm.conf")) {
            cmd = QString("sed -i -r '/^autologin-user=/d' /etc/lightdm/lightdm.conf").arg(user);
            system(cmd.toUtf8());
        }
        if (QFile::exists("/etc/sddm.conf")) {
            system("sed -i 's/^User=.*/User=/' /etc/sddm.conf");
        }
        QMessageBox::information(this, tr("Autologin options"),
                                 (tr("Autologin has been disabled for the '%1' account.").arg(user)));
    } else if (radioAutologinYes->isChecked()) {
        if (QFile::exists("/etc/lightdm/lightdm.conf")) {
            cmd = QString("sed -i -r '/^autologin-user=/d; /^[[]SeatDefaults[]]/aautologin-user=%1' /etc/lightdm/lightdm.conf").arg(user);
            system(cmd.toUtf8());
        }
        if (QFile::exists("/etc/sddm.conf")) {
            system(QString("sed -i 's/^User=.*/User=%1/' /etc/sddm.conf").arg(user).toUtf8());
        }
        QMessageBox::information(this, tr("Autologin options"),
                                 (tr("Autologin has been enabled for the '%1' account.").arg(user)));
    }
    setCursor(QCursor(Qt::ArrowCursor));

    refresh();
}

void MainWindow::applyDesktop()
{

    if (toUserComboBox->currentText().isEmpty()) {
        QMessageBox::information(this, windowTitle(),
                                 tr("You must specify a 'copy to' destination. You cannot copy to the desktop you are logged in to."));
        return;
    }
    // verify
    int ans = QMessageBox::critical(this, windowTitle(), tr("Before copying, close all other applications. Be sure the copy to destination is large enough to contain the files you are copying. Copying between desktops may overwrite or delete your files or preferences on the destination desktop. Are you sure you want to proceed?"),
                                    QMessageBox::Yes, QMessageBox::No);
    if (ans != QMessageBox::Yes) {
        return;
    }

    QString fromDir = QString("/home/%1").arg(fromUserComboBox->currentText());
    QString toDir = QString("/home/%1").arg(toUserComboBox->currentText());
    if (toUserComboBox->currentText().contains("/")) {  // if a directory rather than a user name
        toDir = toUserComboBox->currentText();
    }
    if (docsRadioButton->isChecked()) {
        fromDir.append("/Documents");
        toDir.append("/Documents");
    } else if (mozillaRadioButton->isChecked()) {
        fromDir.append("/.mozilla");
        toDir.append("/.mozilla");
    } else if (sharedRadioButton->isChecked()) {
        fromDir.append("/Shared");
        toDir.append("/Shared");
    }
    fromDir.append("/");

    setCursor(QCursor(Qt::WaitCursor));
    if (syncRadioButton->isChecked()) {
        syncStatusEdit->setText(tr("Synchronizing desktop..."));
    } else {
        syncStatusEdit->setText(tr("Copying desktop..."));
    }
    QString cmd = QString("rsync -qa ");
    if (syncRadioButton->isChecked()) {
        cmd.append("--delete-after ");
    }
    cmd.append(fromDir);
    cmd.append(" ");
    cmd.append(toDir);
    QTimer timer;
    timer.start(100);
    connect(&timer, &QTimer::timeout, this, &MainWindow::progress);
    syncDone(shell->run(cmd));
}

void MainWindow::applyAdd()
{
    //validate data before proceeding
    // see if username is reasonable length
    if (userNameEdit->text().length() < 2) {
        QMessageBox::critical(this, windowTitle(),
                              tr("The user name needs to be at least 2 characters long. Please select a longer name before proceeding."));
        return;
    } else if (!userNameEdit->text().contains(QRegularExpression("^[a-z_][a-z0-9_-]*[$]?$"))) {
        QMessageBox::critical(this, windowTitle(),
                              tr("The user name needs to be lower case and it\n"
                                 "cannot contain special characters or spaces.\n"
                                 "Please choose another name before proceeding."));
        return;
    }
    // check that user name is not already used
    QString cmd = QString("grep -w '^%1' /etc/passwd >/dev/null").arg(userNameEdit->text());
    if (system(cmd.toUtf8()) == 0) {
        QMessageBox::critical(this, windowTitle(),
                              tr("Sorry, this name is in use. Please enter a different name."));
        return;
    }
    if (userPasswordEdit->text().compare(userPassword2Edit->text()) != 0) {
        QMessageBox::critical(this, windowTitle(),
                              tr("Password entries do not match. Please try again."));
        return;
    }
    if (userPasswordEdit->text().length() < 2) {
        QMessageBox::critical(this, windowTitle(),
                              tr("Password needs to be at least 2 characters long. Please enter a longer password before proceeding."));
        return;
    }

    cmd = QString("adduser --disabled-login --force-badname --gecos %1 %1").arg(userNameEdit->text());
    system(cmd.toUtf8());
    cmd = QString("passwd %1").arg(userNameEdit->text());

    QProcess proc;
    proc.start(cmd);
    proc.waitForStarted();
    proc.write(userPasswordEdit->text().toUtf8() + "\n");
    proc.write(userPasswordEdit->text().toUtf8() + "\n");
    proc.waitForFinished();

    if (proc.exitCode() == 0) {
        QMessageBox::information(this, windowTitle(),
                                 tr("The user was added ok."));
        refresh();
    } else {
        QMessageBox::critical(this, windowTitle(),
                              tr("Failed to add the user."));
    }
}

// change user password
void MainWindow::applyChangePass()
{
    if (lineEditChangePass->text().compare(lineEditChangePassConf->text()) != 0) {
        QMessageBox::critical(this, windowTitle(),
                              tr("Password entries do not match. Please try again."));
        return;
    }
    if (lineEditChangePass->text().length() < 2) {
        QMessageBox::critical(this, windowTitle(),
                              tr("Password needs to be at least 2 characters long. Please enter a longer password before proceeding."));
        return;
    }
    QString cmd = QString("passwd %1").arg(comboChangePass->currentText());

    QProcess proc;
    proc.start(cmd);
    proc.waitForStarted();
    proc.write(lineEditChangePass->text().toUtf8() + "\n");
    proc.write(lineEditChangePass->text().toUtf8() + "\n");
    proc.waitForFinished();

    if (proc.exitCode() == 0) {
        QMessageBox::information(this, windowTitle(),
                                 tr("Password successfully changed."));
        refresh();
    } else {
        QMessageBox::critical(this, windowTitle(),
                              tr("Failed to change password."));
    }
}

void MainWindow::applyDelete()
{
    QString cmd = QString(tr("This action cannot be undone. Are you sure you want to delete user %1?")).arg(comboDeleteUser->currentText());
    int ans = QMessageBox::warning(this, windowTitle(), cmd,
                                   QMessageBox::Yes, QMessageBox::No);
    if (ans == QMessageBox::Yes) {
        if (deleteHomeCheckBox->isChecked()) {
            cmd = QString("killall -u %1").arg( comboDeleteUser->currentText());
            system(cmd.toUtf8());
            cmd = QString("deluser --force --remove-home %1").arg( comboDeleteUser->currentText());
        } else {
            cmd = QString("deluser %1").arg(comboDeleteUser->currentText());
        }
        if (system(cmd.toUtf8()) == 0) {
            QMessageBox::information(this, windowTitle(),
                                     tr("The user has been deleted."));
        } else {
            QMessageBox::critical(this, windowTitle(),
                                  tr("Failed to delete the user."));
        }
        refresh();
    }
}

void MainWindow::applyGroup()
{
    //checks if adding or removing groups
    if (addBox->isEnabled()) {
        //validate data before proceeding
        // see if groupname is reasonable length
        if (groupNameEdit->text().length() < 2) {
            QMessageBox::critical(this, windowTitle(),
                                  tr("The group name needs to be at least 2 characters long. Please select a longer name before proceeding."));
            return;
        } else if (!groupNameEdit->text().contains(QRegularExpression("^[a-z_][a-z0-9_-]*[$]?$"))) {
            QMessageBox::critical(this, windowTitle(),
                                  tr("The group name needs to be lower case and it \n"
                                     "cannot contain special characters or spaces.\n"
                                     "Please choose another name before proceeding."));
            return;
        }
        // check that group name is not already used
        QString cmd = QString("grep -w '^%1' /etc/group >/dev/null").arg(groupNameEdit->text());
        if (system(cmd.toUtf8()) == 0) {
            QMessageBox::critical(this, windowTitle(),
                                  tr("Sorry, that group name already exists. Please enter a different name."));
            return;
        }
        // run addgroup command
        cmd = QString("addgroup --system %1").arg( groupNameEdit->text());
        if (system(cmd.toUtf8()) == 0) {
            QMessageBox::information(this, windowTitle(),
                                     tr("The system group was added ok."));
        } else {
            QMessageBox::critical(this, windowTitle(),
                                  tr("Failed to add the system group."));
        }
    }  else { //deleting group if addBox disabled
        QString cmd = QString(tr("This action cannot be undone. Are you sure you want to delete group %1?")).arg(deleteGroupCombo->currentText());
        int ans = QMessageBox::warning(this, windowTitle(), cmd,
                                       QMessageBox::Yes, QMessageBox::No);
        if (ans == QMessageBox::Yes) {
            cmd = QString("delgroup %1").arg(deleteGroupCombo->currentText());
            if (system(cmd.toUtf8()) == 0) {
                QMessageBox::information(this, windowTitle(),
                                         tr("The group has been deleted."));
            } else {
                QMessageBox::critical(this, windowTitle(),
                                      tr("Failed to delete the group."));
            }
        }
    }
    refresh();
}

void MainWindow::applyMembership()
{
    QString cmd;
    //Add all WidgetItems from listGroups
    QList<QListWidgetItem *> items = listGroups->findItems(QString("*"), Qt::MatchWrap | Qt::MatchWildcard);
    while (!items.isEmpty()) {
        QListWidgetItem *item = items.takeFirst();
        if (item->checkState() == 2) {
            cmd += item->text() + ",";
        }
    }
    cmd.chop(1);
    cmd = QString("usermod -G %1 %2").arg(cmd).arg(userComboMembership->currentText());
    if (shell->run(cmd)) {
        QMessageBox::information(this, windowTitle(),
                                 tr("The changes have been applied."));
    } else {
        QMessageBox::critical(this, windowTitle(),
                              tr("Failed to apply group changes"));
    }
}

void MainWindow::applyRename()
{
    QString old_name = comboRenameUser->currentText();
    QString new_name = renameUserNameEdit->text();

    // validate data before proceeding
    // check if selected user is in use
    if (shell->getCmdOut("logname", true) == old_name) {
        QMessageBox::critical(this, windowTitle(),
                              tr("The selected user name is currently in use.") + "\n\n" +
                              tr("To rename this user, please log out and log back in using another user account."));
        refresh();
        return;
    }

    // see if username is reasonable length
    if (new_name.length() < 2) {
        QMessageBox::critical(this, windowTitle(),
                              tr("The user name needs to be at least 2 characters long. Please select a longer name before proceeding."));
        return;
    } else if (!new_name.contains(QRegularExpression("^[a-z_][a-z0-9_-]*[$]?$"))) {
        QMessageBox::critical(this, windowTitle(),
                              tr("The user name needs to be lower case and it\n"
                                 "cannot contain special characters or spaces.\n"
                                 "Please choose another name before proceeding."));
        return;
    }
    // check that user name is not already used
    QString cmd = QString("grep -w '^%1' /etc/passwd >/dev/null").arg(new_name);
    if (system(cmd.toUtf8()) == 0) {
        QMessageBox::critical(this, windowTitle(),
                              tr("Sorry, this name already exists on your system. Please enter a different name."));
        return;
    }

    // rename user
    bool success = shell->run("usermod --login " + new_name + " --move-home --home /home/" + new_name + " " + old_name);
    if (!success) {
        QMessageBox::critical(this, windowTitle(),
                              tr("Failed to rename the user. Please make sure that the user is not logged in, you might need to restart"));
        return;
    }

    // rename other instances of the old name, like "Finger" name if present
    shell->run("sed -i 's/\\b" + old_name + "\\b/" + new_name + "/g' /etc/passwd");

    // change group
    shell->run("groupmod --new-name " + new_name + " " + old_name);

    // fix "home/old_user" string in all ~/ files
    shell->run(QString("grep -rl \"home/%1\" /home/%2 | xargs sed -i 's|home/%1|home/%2|g'").arg(old_name).arg(new_name));

    // change autologin name (Xfce and KDE)
    if (QFile::exists("/etc/lightdm/lightdm.conf")) {
        shell->run(QString("sed -i 's/autologin-user=%1/autologin-user=%2/g' /etc/lightdm/lightdm.conf").arg(old_name).arg(new_name));
    }
    if (QFile::exists("/etc/sddm.conf")) {
        shell->run(QString("sed -i 's/User=%1/User=%2/g' /etc/sddm.conf").arg(old_name).arg(new_name));
    }

    QMessageBox::information(this, windowTitle(), tr("The user was renamed."));
    refresh();
}

void MainWindow::syncDone(bool success)
{
    if (success) {
        QString fromDir = QString("/home/%1").arg(fromUserComboBox->currentText());
        QString toDir = QString("/home/%1").arg(toUserComboBox->currentText());

        // if a directory rather than a user name
        if (toUserComboBox->currentText().contains("/")) {
            if (syncRadioButton->isChecked()) {
                syncStatusEdit->setText(tr("Synchronizing desktop...ok"));
            } else {
                syncStatusEdit->setText(tr("Copying desktop...ok"));
            }
            syncProgressBar->setValue(syncProgressBar->maximum());
            setCursor(QCursor(Qt::ArrowCursor));
            return;
        }

        // fix owner
        QString cmd = QString("chown -R %1:%1 %2").arg(toUserComboBox->currentText()).arg(toDir);
        system(cmd.toUtf8());

        // fix "home/old_user" string in all ~/ or ~/.mozilla files
        if (entireRadioButton->isChecked()) {
            cmd = QString("grep -rl \"home/%1\" /home/%2 | xargs sed -i 's|home/%1|home/%2|g'").arg(fromUserComboBox->currentText()).arg(toUserComboBox->currentText());
        } else if (mozillaRadioButton->isChecked()) {
            cmd = QString("grep -rl \"home/%1\" /home/%2/.mozilla | xargs sed -i 's|home/%1|home/%2|g'").arg(fromUserComboBox->currentText()).arg(toUserComboBox->currentText());
        }
        shell->run(cmd);

        if (entireRadioButton->isChecked()) {
            //delete some files
            cmd = QString("rm -f %1/.recently-used >/dev/null").arg(toDir);
            system(cmd.toUtf8());
            cmd = QString("rm -f %1/.openoffice.org/*/.lock >/dev/null").arg(toDir);
            system(cmd.toUtf8());
        }
        if (syncRadioButton->isChecked()) {
            syncStatusEdit->setText(tr("Synchronizing desktop...ok"));
        } else {
            syncStatusEdit->setText(tr("Copying desktop...ok"));
        }
    } else {
        if (syncRadioButton->isChecked()) {
            syncStatusEdit->setText(tr("Synchronizing desktop...failed"));
        } else {
            syncStatusEdit->setText(tr("Copying desktop...failed"));
        }
    }
    syncProgressBar->setValue(syncProgressBar->maximum());
    setCursor(QCursor(Qt::ArrowCursor));
}


void MainWindow::on_fromUserComboBox_activated(QString)
{
    buttonApply->setEnabled(true);
    syncProgressBar->setValue(0);
    toUserComboBox->clear();
    QStringList items = users;
    items.removeAll(shell->getCmdOut("logname", true));
    items.removeAll(fromUserComboBox->currentText());
    items.sort();
    toUserComboBox->addItems(items);
    toUserComboBox->addItem(tr("browse..."));
}

void MainWindow::on_userComboBox_activated(QString)
{
    buttonApply->setEnabled(true);
    if (userComboBox->currentText() == tr("none")) {
        refresh();
    }
    radioAutologinNo->setAutoExclusive(false);
    radioAutologinNo->setChecked(false);
    radioAutologinNo->setAutoExclusive(true);
    radioAutologinYes->setAutoExclusive(false);
    radioAutologinYes->setChecked(false);
    radioAutologinYes->setAutoExclusive(true);
}

void MainWindow::on_comboDeleteUser_activated(QString)
{
    addUserBox->setEnabled(false);
    changePasswordBox->setEnabled(false);
    renameUserBox->setEnabled(false);
    buttonApply->setEnabled(true);
    if (comboDeleteUser->currentText() == tr("none")) {
        refresh();
    }
}

void MainWindow::on_userNameEdit_textEdited()
{
    deleteUserBox->setEnabled(false);
    changePasswordBox->setEnabled(false);
    renameUserBox->setEnabled(false);
    buttonApply->setEnabled(true);
    if (userNameEdit->text() == "") {
        refresh();
    }
}

void MainWindow::on_groupNameEdit_textEdited()
{
    deleteBox->setEnabled(false);
    renameUserBox->setEnabled(false);
    buttonApply->setEnabled(true);
    if (groupNameEdit->text() == "") {
        refresh();
    }
}

void MainWindow::on_deleteGroupCombo_activated(QString)
{
    addBox->setEnabled(false);
    renameUserBox->setEnabled(false);
    buttonApply->setEnabled(true);
    if (deleteGroupCombo->currentText() == tr("none")) {
        refresh();
    }
}

void MainWindow::on_userComboMembership_activated(QString)
{
    buildListGroups();
    buttonApply->setEnabled(true);
    if (userComboMembership->currentText() == tr("none")) {
        refresh();
    }
}


void MainWindow::buildListGroups()
{
    listGroups->clear();
    //read /etc/group and add all the groups in the listGroups
    QStringList groups = shell->getCmdOut("cat /etc/group | cut -f 1 -d :").split("\n");
    groups.sort();
    for (const QString &group : groups) {
        QListWidgetItem *item = new QListWidgetItem;
        item->setText(group);
        item->setCheckState(Qt::Unchecked);
        listGroups->addItem(item);
    }
    //check the boxes for the groups that the current user belongs to
    QString cmd = QString("id -nG %1").arg(userComboMembership->currentText());
    QString out = shell->getCmdOut(cmd);
    QStringList out_tok = out.split(" ");
    while (!out_tok.isEmpty()) {
        QString text = out_tok.takeFirst();
        QList<QListWidgetItem*> list = listGroups->findItems(text, Qt::MatchExactly);
        while (!list.isEmpty()) {
            list.takeFirst()->setCheckState(Qt::Checked);
        }
    }
}

// apply but do not close
void MainWindow::on_buttonApply_clicked()
{
    if (!buttonApply->isEnabled()) {
        return;
    }

    int i = tabWidget->currentIndex();

    switch (i) {

    case 1:
        setCursor(QCursor(Qt::WaitCursor));
        applyRestore();
        setCursor(QCursor(Qt::ArrowCursor));
        buttonApply->setEnabled(false);
        break;

    case 2:
        applyDesktop();
        buttonApply->setEnabled(false);
        break;

    case 3:
        setCursor(QCursor(Qt::WaitCursor));
        applyGroup();
        setCursor(QCursor(Qt::ArrowCursor));
        buttonApply->setEnabled(false);
        break;

    case 4:
        setCursor(QCursor(Qt::WaitCursor));
        applyMembership();
        setCursor(QCursor(Qt::ArrowCursor));
        break;

    default:
        setCursor(QCursor(Qt::WaitCursor));
        if (addUserBox->isEnabled()) {
            applyAdd();
        } else if (deleteUserBox->isEnabled()) {
            applyDelete();
            buttonApply->setEnabled(false);
        } else if (changePasswordBox->isEnabled()) {
            applyChangePass();
        } else if (renameUserBox->isEnabled()) {
            applyRename();
        }
        setCursor(QCursor(Qt::ArrowCursor));
        break;
    }
}

void MainWindow::on_tabWidget_currentChanged()
{
    refresh();
}


// close but do not apply
void MainWindow::on_buttonCancel_clicked()
{
    close();
}

void MainWindow::progress()
{
    if (syncProgressBar->value() == 100) {
        syncProgressBar->reset();
    }
    syncProgressBar->setValue(syncProgressBar->value() + 1);
}

// show about
void MainWindow::on_buttonAbout_clicked()
{
    this->hide();
    displayAboutMsgBox(tr("About %1").arg(this->windowTitle()), "<p align=\"center\"><b><h2>" + this->windowTitle() +"</h2></b></p><p align=\"center\">" +
                       tr("Version: ") + VERSION + "</p><p align=\"center\"><h3>" +
                        tr("Simple user configuration for MX Linux") +
                       "</h3></p><p align=\"center\"><a href=\"http://mxlinux.org\">http://mxlinux.org</a><br /></p><p align=\"center\">" +
                       tr("Copyright (c) MX Linux") + "<br /><br /></p>",
                       "/usr/share/doc/mx-user/license.html", tr("%1 License").arg(this->windowTitle()), true);
    this->show();
}

// Help button clicked
void MainWindow::on_buttonHelp_clicked()
{
    QLocale locale;
    QString lang = locale.bcp47Name();

    QString url = "/usr/share/doc/mx-user/mx-user.html";

    if (lang.startsWith("fr")) {
        url = "https://mxlinux.org/wiki/help-files/help-gestionnaire-des-utilisateurs";
    }
    displayDoc(url, tr("%1 Help").arg(this->windowTitle()), true);
}


void MainWindow::restartPanel(QString user)
{
    QString cmd = QString("pkill xfconfd; sudo -Eu %1 bash -c 'xfce4-panel -r'").arg(user);
    system(cmd.toUtf8());
}


void MainWindow::on_comboChangePass_activated(QString)
{
    addUserBox->setEnabled(false);
    deleteUserBox->setEnabled(false);
    renameUserBox->setEnabled(false);
    buttonApply->setEnabled(true);
    if (comboChangePass->currentText() == tr("none")) {
        refresh();
    }
}


void MainWindow::on_toUserComboBox_activated(QString)
{
    buttonApply->setEnabled(true);
    syncProgressBar->setValue(0);
}

void MainWindow::on_copyRadioButton_clicked()
{
    buttonApply->setEnabled(true);
    syncProgressBar->setValue(0);
}

void MainWindow::on_syncRadioButton_clicked()
{
    buttonApply->setEnabled(true);
    syncProgressBar->setValue(0);
}

void MainWindow::on_entireRadioButton_clicked()
{
    buttonApply->setEnabled(true);
    syncProgressBar->setValue(0);
}

void MainWindow::on_docsRadioButton_clicked()
{
    buttonApply->setEnabled(true);
    syncProgressBar->setValue(0);
}

void MainWindow::on_mozillaRadioButton_clicked()
{
    buttonApply->setEnabled(true);
    syncProgressBar->setValue(0);
}

void MainWindow::on_sharedRadioButton_clicked()
{
    buttonApply->setEnabled(true);
    syncProgressBar->setValue(0);
}



void MainWindow::on_toUserComboBox_currentIndexChanged(const QString &arg1)
{
    if (arg1 == tr("browse...")) {
        QString dir = QFileDialog::getExistingDirectory(this, tr("Select folder to copy to"), "/",QFileDialog::ShowDirsOnly
                                                     | QFileDialog::DontResolveSymlinks);
        if (dir != "") {
            toUserComboBox->removeItem(toUserComboBox->currentIndex());
            toUserComboBox->addItem(dir);
            int idx = toUserComboBox->findText(dir, Qt::MatchExactly | Qt::MatchCaseSensitive);
            toUserComboBox->setCurrentIndex(idx);
            toUserComboBox->addItem(tr("browse..."));
        } else {
            toUserComboBox->setCurrentIndex(toUserComboBox->currentIndex() - 1);
        }
    }
}


void MainWindow::on_userPassword2Edit_textChanged(const QString &arg1)
{
    QPalette pal = userPassword2Edit->palette();
    if (arg1 != userPasswordEdit->text()) {
        pal.setColor(QPalette::Base, QColor(255, 0, 0, 20));
    } else {
        pal.setColor(QPalette::Base, QColor(0, 255, 0, 10));
    }
    userPasswordEdit->setPalette(pal);
    userPassword2Edit->setPalette(pal);
}

void MainWindow::on_lineEditChangePassConf_textChanged(const QString &arg1)
{
    QPalette pal = lineEditChangePassConf->palette();
    if (arg1 != lineEditChangePass->text()) {
        pal.setColor(QPalette::Base, QColor(255, 0, 0, 20));
    } else {
        pal.setColor(QPalette::Base, QColor(0, 255, 0, 10));
    }
    lineEditChangePassConf->setPalette(pal);
    lineEditChangePass->setPalette(pal);
}

void MainWindow::on_userPasswordEdit_textChanged()
{
    userPassword2Edit->clear();
    userPasswordEdit->setPalette(QApplication::palette());
    userPassword2Edit->setPalette(QApplication::palette());
}

void MainWindow::on_lineEditChangePass_textChanged()
{
    lineEditChangePassConf->clear();
    lineEditChangePass->setPalette(QApplication::palette());
    lineEditChangePassConf->setPalette(QApplication::palette());
}


void MainWindow::on_comboRenameUser_activated(QString)
{
    addUserBox->setEnabled(false);
    changePasswordBox->setEnabled(false);
    deleteUserBox->setEnabled(false);
    buttonApply->setEnabled(true);
    if (comboRenameUser->currentText() == tr("none")) {
        refresh();
    }
}
