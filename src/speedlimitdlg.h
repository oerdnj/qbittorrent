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

#ifndef BANDWIDTH_ALLOCATION_H
#define BANDWIDTH_ALLOCATION_H

#include <QDialog>
#include <QList>
#include <QSettings>
#include "ui_bandwidth_limit.h"
#include "misc.h"
#include "bittorrent.h"

using namespace libtorrent;

class SpeedLimitDialog : public QDialog, private Ui_bandwidth_dlg {
  Q_OBJECT

  public:
    SpeedLimitDialog(QWidget *parent=0): QDialog(parent) {
      setupUi(this);
      qDebug("Bandwidth allocation dialog creation");
      // Connect to slots
      connect(bandwidthSlider, SIGNAL(valueChanged(int)), this, SLOT(updateBandwidthLabel(int)));
    }

    ~SpeedLimitDialog(){
      qDebug("Deleting bandwidth allocation dialog");
    }

    // -2: if cancel
    static long askSpeedLimit(bool *ok, QString title, long default_value) {
      SpeedLimitDialog dlg;
      dlg.setWindowTitle(title);
      dlg.setDefaultValue(default_value/1024.);
      if(dlg.exec() == QDialog::Accepted) {
        *ok = true;
        int val = dlg.getSpeedLimit();
        if(val <= 0)
          return -1;
        return val*1024;
      } else {
        *ok = false;
        return -2;
      }
    }

  protected slots:
    void updateBandwidthLabel(int val){
      if(val <= 0){
        limit_lbl->setText(QString::fromUtf8("∞"));
        kb_lbl->setText(QString::fromUtf8(""));
      }else{
        limit_lbl->setText(misc::toQString(val));
        kb_lbl->setText(tr("KiB/s"));
      }
    }

    long getSpeedLimit() const {
      long val = bandwidthSlider->value();
      if(val > 0)
        return val;
      return -1;
    }

    void setDefaultValue(long val) const {
      if(val < 0) val = 0;
      bandwidthSlider->setValue(val);
    }
};

#endif
