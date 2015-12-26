/*
 * Bittorrent Client using Qt4 and libtorrent.
 * Copyright (C) 2014  Ivan Sorokin
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
 * Contact : vanyacpp@gmail.com
 */

#include "torrentcontenttreeview.h"

#include <QKeyEvent>
#include <QModelIndexList>
#ifdef QBT_USES_QT5
#include <QTableView>
#include <QHeaderView>
#endif

#include "torrentcontentmodelitem.h"

TorrentContentTreeView::TorrentContentTreeView(QWidget* parent)
  : QTreeView(parent)
{
#ifdef QBT_USES_QT5
    // This hack fixes reordering of first column with Qt5.
    // https://github.com/qtproject/qtbase/commit/e0fc088c0c8bc61dbcaf5928b24986cd61a22777
    QTableView unused;
    unused.setVerticalHeader(header());
    header()->setParent(this);
    unused.setVerticalHeader(new QHeaderView(Qt::Horizontal));
#endif
}

void TorrentContentTreeView::keyPressEvent(QKeyEvent *event) {
  if (event->key() != Qt::Key_Space && event->key() != Qt::Key_Select) {
    QTreeView::keyPressEvent(event);
    return;
  }

  event->accept();

  QModelIndex current = currentNameCell();

  QVariant value = current.data(Qt::CheckStateRole);
  if (!value.isValid()) {
    Q_ASSERT(false);
    return;
  }

  Qt::CheckState state = (static_cast<Qt::CheckState>(value.toInt()) == Qt::Checked
                       ? Qt::Unchecked : Qt::Checked);

  QModelIndexList selection = selectionModel()->selectedRows(TorrentContentModelItem::COL_NAME);

  for (QModelIndexList::const_iterator i = selection.begin(); i != selection.end(); ++i) {
    QModelIndex index = *i;
    Q_ASSERT(i->column() == TorrentContentModelItem::COL_NAME);
    model()->setData(index, state, Qt::CheckStateRole);
  }
}

QModelIndex TorrentContentTreeView::currentNameCell() {
  QModelIndex current = currentIndex();
  if (!current.isValid()) {
    Q_ASSERT(false);
    return QModelIndex();
  }

  return model()->index(current.row(), TorrentContentModelItem::COL_NAME, current.parent());
}
