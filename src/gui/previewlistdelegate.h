/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2006  Christophe Dumez <chris@qbittorrent.org>
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
 */

#ifndef PREVIEWLISTDELEGATE_H
#define PREVIEWLISTDELEGATE_H

#include <QApplication>
#include <QItemDelegate>
#include <QModelIndex>
#include <QPainter>
#ifdef Q_OS_WIN
#include <QProxyStyle>
#endif
#include <QStyleOptionProgressBar>
#include <QStyleOptionViewItem>

#include "base/utils/misc.h"
#include "base/utils/string.h"
#include "previewselectdialog.h"

class PreviewListDelegate : public QItemDelegate
{
    Q_OBJECT

public:
    PreviewListDelegate(QObject *parent = nullptr)
        : QItemDelegate(parent)
    {
    }

    ~PreviewListDelegate() {}

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        painter->save();
        QStyleOptionViewItem opt = QItemDelegate::setOptions(index, option);

        switch (index.column()) {
        case PreviewSelectDialog::SIZE:
            QItemDelegate::drawBackground(painter, opt, index);
            QItemDelegate::drawDisplay(painter, opt, option.rect, Utils::Misc::friendlyUnit(index.data().toLongLong()));
            break;
        case PreviewSelectDialog::PROGRESS: {
                QStyleOptionProgressBar newopt;
                qreal progress = index.data().toDouble() * 100.;
                newopt.rect = opt.rect;
                newopt.text = ((progress == 100.0) ? QString("100%") : Utils::String::fromDouble(progress, 1) + "%");
                newopt.progress = static_cast<int>(progress);
                newopt.maximum = 100;
                newopt.minimum = 0;
                newopt.state |= QStyle::State_Enabled;
                newopt.textVisible = true;
#ifndef Q_OS_WIN
                QApplication::style()->drawControl(QStyle::CE_ProgressBar, &newopt, painter);
#else
                // XXX: To avoid having the progress text on the right of the bar
                QProxyStyle st("fusion");
                st.drawControl(QStyle::CE_ProgressBar, &newopt, painter, 0);
#endif
            }
            break;
        default:
            QItemDelegate::paint(painter, option, index);
        }

        painter->restore();
    }

    QWidget *createEditor(QWidget *, const QStyleOptionViewItem &, const QModelIndex &) const
    {
        // No editor here
        return nullptr;
    }
};

#endif // PREVIEWLISTDELEGATE_H
