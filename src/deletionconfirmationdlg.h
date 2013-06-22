/*
 * Bittorrent Client using Qt4 and libtorrent.
 * Copyright (C) 2006  Christophe Dumez
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * In addition, as a special exception, the copyright holders give permission to
 * link this program with the OpenSSL project's "OpenSSL" library (or with
 * modified versions of it that use the same license as the "OpenSSL" library),
 * and distribute the linked executables. You must obey the GNU General Public
 * License in all respects for all of the code used other than "OpenSSL".  If you
 * modify file(s), you may extend this exception to your version of the file(s),
 * but you are not obligated to do so. If you do not wish to do so, delete this
 * exception statement from your version.
 *
 * Contact : chris@qbittorrent.org
 */

#ifndef DELETIONCONFIRMATIONDLG_H
#define DELETIONCONFIRMATIONDLG_H

#include <QDialog>
#include "ui_confirmdeletiondlg.h"
#include "preferences.h"
#include "iconprovider.h"

class DeletionConfirmationDlg : public QDialog, private Ui::confirmDeletionDlg {
  Q_OBJECT

  public:
  DeletionConfirmationDlg(QWidget *parent=0): QDialog(parent) {
    setupUi(this);
    // Icons
    lbl_warn->setPixmap(IconProvider::instance()->getIcon("dialog-warning").pixmap(lbl_warn->height()));
    lbl_warn->setFixedWidth(lbl_warn->height());
    rememberBtn->setIcon(IconProvider::instance()->getIcon("object-locked"));

    move(misc::screenCenter(this));
    checkPermDelete->setChecked(Preferences().deleteTorrentFilesAsDefault());
    connect(checkPermDelete, SIGNAL(clicked()), this, SLOT(updateRememberButtonState()));
    buttonBox->setFocus();
  }

  bool shouldDeleteLocalFiles() const {
    return checkPermDelete->isChecked();
  }

  static bool askForDeletionConfirmation(bool *delete_local_files) {
    DeletionConfirmationDlg dlg;
    if (dlg.exec() == QDialog::Accepted) {
      *delete_local_files = dlg.shouldDeleteLocalFiles();
      return true;
    }
    return false;
  }

private slots:
  void updateRememberButtonState() {
    rememberBtn->setEnabled(checkPermDelete->isChecked() != Preferences().deleteTorrentFilesAsDefault());
  }

  void on_rememberBtn_clicked() {
    Preferences().setDeleteTorrentFilesAsDefault(checkPermDelete->isChecked());
    rememberBtn->setEnabled(false);
  }
};

#endif // DELETIONCONFIRMATIONDLG_H
