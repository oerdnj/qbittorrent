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

#include <QDir>
#include <QTreeView>
#include <QStandardItemModel>
#include <QHeaderView>
#include <QSortFilterProxyModel>
#include <QLabel>
#include <QVBoxLayout>
#ifdef QBT_USES_QT5
#include <QTableView>
#endif

#include "base/utils/misc.h"
#include "base/preferences.h"
#include "searchsortmodel.h"
#include "searchlistdelegate.h"
#include "searchwidget.h"
#include "searchtab.h"

SearchTab::SearchTab(SearchWidget *parent)
    : QWidget(parent)
    , m_parent(parent)
{
    m_box = new QVBoxLayout(this);
    m_resultsLbl = new QLabel(this);
    m_resultsBrowser = new QTreeView(this);
#ifdef QBT_USES_QT5
    // This hack fixes reordering of first column with Qt5.
    // https://github.com/qtproject/qtbase/commit/e0fc088c0c8bc61dbcaf5928b24986cd61a22777
    QTableView unused;
    unused.setVerticalHeader(m_resultsBrowser->header());
    m_resultsBrowser->header()->setParent(m_resultsBrowser);
    unused.setVerticalHeader(new QHeaderView(Qt::Horizontal));
#endif
    m_resultsBrowser->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_box->addWidget(m_resultsLbl);
    m_box->addWidget(m_resultsBrowser);

    setLayout(m_box);

    // Set Search results list model
    m_searchListModel = new QStandardItemModel(0, SearchSortModel::NB_SEARCH_COLUMNS, this);
    m_searchListModel->setHeaderData(SearchSortModel::NAME, Qt::Horizontal, tr("Name", "i.e: file name"));
    m_searchListModel->setHeaderData(SearchSortModel::SIZE, Qt::Horizontal, tr("Size", "i.e: file size"));
    m_searchListModel->setHeaderData(SearchSortModel::SEEDS, Qt::Horizontal, tr("Seeders", "i.e: Number of full sources"));
    m_searchListModel->setHeaderData(SearchSortModel::LEECHS, Qt::Horizontal, tr("Leechers", "i.e: Number of partial sources"));
    m_searchListModel->setHeaderData(SearchSortModel::ENGINE_URL, Qt::Horizontal, tr("Search engine"));

    m_proxyModel = new SearchSortModel(this);
    m_proxyModel->setDynamicSortFilter(true);
    m_proxyModel->setSourceModel(m_searchListModel);
    m_resultsBrowser->setModel(m_proxyModel);

    m_searchDelegate = new SearchListDelegate(this);
    m_resultsBrowser->setItemDelegate(m_searchDelegate);

    m_resultsBrowser->hideColumn(SearchSortModel::DL_LINK); // Hide url column
    m_resultsBrowser->hideColumn(SearchSortModel::DESC_LINK);

    m_resultsBrowser->setRootIsDecorated(false);
    m_resultsBrowser->setAllColumnsShowFocus(true);
    m_resultsBrowser->setSortingEnabled(true);

    // Connect signals to slots (search part)
    connect(m_resultsBrowser, SIGNAL(doubleClicked(const QModelIndex&)), this, SLOT(downloadSelectedItem(const QModelIndex&)));

    // Load last columns width for search results list
    if (!loadColWidthResultsList())
        m_resultsBrowser->header()->resizeSection(0, 275);

    // Sort by Seeds
    m_resultsBrowser->sortByColumn(SearchSortModel::SEEDS, Qt::DescendingOrder);
}

void SearchTab::downloadSelectedItem(const QModelIndex &index)
{
    QString torrentUrl = m_proxyModel->data(m_proxyModel->index(index.row(), SearchSortModel::DL_LINK)).toString();
    setRowColor(index.row(), "blue");
    m_parent->downloadTorrent(torrentUrl);
}

QHeaderView* SearchTab::header() const
{
    return m_resultsBrowser->header();
}

bool SearchTab::loadColWidthResultsList()
{
    QString line = Preferences::instance()->getSearchColsWidth();
    if (line.isEmpty()) return false;

    QStringList widthList = line.split(' ');
    if (widthList.size() > m_searchListModel->columnCount())
        return false;

    unsigned int listSize = widthList.size();
    for (unsigned int i = 0; i < listSize; ++i) {
        m_resultsBrowser->header()->resizeSection(i, widthList.at(i).toInt());
    }

    return true;
}

QLabel* SearchTab::getCurrentLabel() const
{
    return m_resultsLbl;
}

QTreeView* SearchTab::getCurrentTreeView() const
{
    return m_resultsBrowser;
}

QSortFilterProxyModel* SearchTab::getCurrentSearchListProxy() const
{
    return m_proxyModel;
}

QStandardItemModel* SearchTab::getCurrentSearchListModel() const
{
    return m_searchListModel;
}

// Set the color of a row in data model
void SearchTab::setRowColor(int row, QString color)
{
    m_proxyModel->setDynamicSortFilter(false);
    for (int i = 0; i < m_proxyModel->columnCount(); ++i) {
        m_proxyModel->setData(m_proxyModel->index(row, i), QVariant(QColor(color)), Qt::ForegroundRole);
    }

    m_proxyModel->setDynamicSortFilter(true);
}

QString SearchTab::status() const
{
    return m_status;
}

void SearchTab::setStatus(const QString &value)
{
    m_status = value;
}
