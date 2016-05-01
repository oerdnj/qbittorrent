/*
 * Bittorrent Client using Qt4 and libtorrent.
 * Copyright (C) 2006  Christophe Dumez, Arnaud Demaiziere
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
 * Contact : chris@qbittorrent.org arnaud@qbittorrent.org
 */

#include <QDesktopServices>
#include <QMenu>
#include <QStandardItemModel>
#include <QMessageBox>
#include <QString>
#include <QClipboard>
#include <QDragMoveEvent>
#include <QDebug>

#include "rss_imp.h"
#include "feedlistwidget.h"
#include "base/bittorrent/session.h"
#include "base/net/downloadmanager.h"
#include "cookiesdlg.h"
#include "base/preferences.h"
#include "rsssettingsdlg.h"
#include "base/rss/rssmanager.h"
#include "base/rss/rssfolder.h"
#include "base/rss/rssarticle.h"
#include "base/rss/rssfeed.h"
#include "automatedrssdownloader.h"
#include "guiiconprovider.h"
#include "autoexpandabledialog.h"
#include "addnewtorrentdialog.h"

namespace Article
{
    enum ArticleRoles
    {
        TitleRole = Qt::DisplayRole,
        IconRole = Qt::DecorationRole,
        ColorRole = Qt::ForegroundRole,
        IdRole = Qt::UserRole + 1,
        FeedUrlRole = Qt::UserRole + 2
    };
}

// display a right-click menu
void RSSImp::displayRSSListMenu(const QPoint& pos)
{
    if (!m_feedList->indexAt(pos).isValid())
        // No item under the mouse, clear selection
        m_feedList->clearSelection();
    QMenu myRSSListMenu(this);
    QList<QTreeWidgetItem*> selectedItems = m_feedList->selectedItems();
    if (selectedItems.size() > 0) {
        myRSSListMenu.addAction(actionUpdate);
        myRSSListMenu.addAction(actionMark_items_read);
        myRSSListMenu.addSeparator();
        if (selectedItems.size() == 1) {
            if (m_feedList->getRSSItem(selectedItems.first()) != m_rssManager->rootFolder()) {
                myRSSListMenu.addAction(actionRename);
                myRSSListMenu.addAction(actionDelete);
                myRSSListMenu.addSeparator();
                if (m_feedList->isFolder(selectedItems.first()))
                    myRSSListMenu.addAction(actionNew_folder);
                else
                    myRSSListMenu.addAction(actionManage_cookies);
            }
        }
        else {
            myRSSListMenu.addAction(actionDelete);
            myRSSListMenu.addSeparator();
        }
        myRSSListMenu.addAction(actionNew_subscription);
        if (m_feedList->isFeed(selectedItems.first())) {
            myRSSListMenu.addSeparator();
            myRSSListMenu.addAction(actionCopy_feed_URL);
        }
    }
    else {
        myRSSListMenu.addAction(actionNew_subscription);
        myRSSListMenu.addAction(actionNew_folder);
        myRSSListMenu.addSeparator();
        myRSSListMenu.addAction(actionUpdate_all_feeds);
    }
    myRSSListMenu.exec(QCursor::pos());
}

void RSSImp::displayItemsListMenu(const QPoint&)
{
    QMenu myItemListMenu(this);
    QList<QListWidgetItem*> selectedItems = listArticles->selectedItems();
    if (selectedItems.size() <= 0)
        return;

    bool hasTorrent = false;
    bool hasLink = false;
    foreach (const QListWidgetItem* item, selectedItems) {
        if (!item) continue;
        Rss::FeedPtr feed = m_feedList->getRSSItemFromUrl(item->data(Article::FeedUrlRole).toString());
        if (!feed) continue;
        Rss::ArticlePtr article = feed->getItem(item->data(Article::IdRole).toString());
        if (!article) continue;

        if (!article->torrentUrl().isEmpty())
            hasTorrent = true;
        if (!article->link().isEmpty())
            hasLink = true;
        if (hasTorrent && hasLink)
            break;
    }
    if (hasTorrent)
        myItemListMenu.addAction(actionDownload_torrent);
    if (hasLink)
        myItemListMenu.addAction(actionOpen_news_URL);
    if (hasTorrent || hasLink)
        myItemListMenu.exec(QCursor::pos());
}

void RSSImp::on_actionManage_cookies_triggered()
{
    Q_ASSERT(!m_feedList->selectedItems().empty());

    // TODO: Create advanced application wide Cookie dialog and use it everywhere.
    QUrl feedUrl = QUrl::fromEncoded(m_feedList->getItemID(m_feedList->selectedItems().first()).toUtf8());
    QList<QNetworkCookie> cookies;
    if (CookiesDlg::askForCookies(this, feedUrl, cookies)) {
        auto downloadManager = Net::DownloadManager::instance();
        QList<QNetworkCookie> oldCookies = downloadManager->cookiesForUrl(feedUrl);
        foreach (const QNetworkCookie &oldCookie, oldCookies) {
            if (!cookies.contains(oldCookie))
                downloadManager->deleteCookie(oldCookie);
        }

        downloadManager->setCookiesFromUrl(cookies, feedUrl);
    }
}

void RSSImp::askNewFolder()
{
    QTreeWidgetItem* parent_item = 0;
    Rss::FolderPtr rss_parent;
    if (m_feedList->selectedItems().size() > 0) {
        parent_item = m_feedList->selectedItems().at(0);
        rss_parent = qSharedPointerDynamicCast<Rss::Folder>(m_feedList->getRSSItem(parent_item));
        Q_ASSERT(rss_parent);
    }
    else {
        rss_parent = m_rssManager->rootFolder();
    }
    bool ok;
    QString new_name = AutoExpandableDialog::getText(this, tr("Please choose a folder name"), tr("Folder name:"), QLineEdit::Normal, tr("New folder"), &ok);
    if (!ok || rss_parent->hasChild(new_name))
        return;

    Rss::FolderPtr newFolder(new Rss::Folder(new_name));
    rss_parent->addFile(newFolder);
    QTreeWidgetItem* folderItem = createFolderListItem(newFolder);
    if (parent_item)
        parent_item->addChild(folderItem);
    else
        m_feedList->addTopLevelItem(folderItem);
    // Notify TreeWidget
    m_feedList->itemAdded(folderItem, newFolder);
    // Expand parent folder to display new folder
    if (parent_item)
        parent_item->setExpanded(true);
    m_rssManager->saveStreamList();
}

// add a stream by a button
void RSSImp::on_newFeedButton_clicked()
{
    // Determine parent folder for new feed
    QTreeWidgetItem *parent_item = 0;
    QList<QTreeWidgetItem *> selected_items = m_feedList->selectedItems();
    if (!selected_items.empty()) {
        parent_item = selected_items.first();
        // Consider the case where the user clicked on Unread item
        if (parent_item == m_feedList->stickyUnreadItem())
            parent_item = 0;
        else
        if (!m_feedList->isFolder(parent_item))
            parent_item = parent_item->parent();
    }
    Rss::FolderPtr rss_parent;
    if (parent_item)
        rss_parent = qSharedPointerCast<Rss::Folder>(m_feedList->getRSSItem(parent_item));
    else
        rss_parent = m_rssManager->rootFolder();
    // Ask for feed URL
    bool ok;
    QString clip_txt = qApp->clipboard()->text();
    QString default_url = "http://";
    if (clip_txt.startsWith("http://", Qt::CaseInsensitive) || clip_txt.startsWith("https://", Qt::CaseInsensitive) || clip_txt.startsWith("ftp://", Qt::CaseInsensitive))
        default_url = clip_txt;

    QString newUrl = AutoExpandableDialog::getText(this, tr("Please type a RSS stream URL"), tr("Stream URL:"), QLineEdit::Normal, default_url, &ok);
    if (!ok)
        return;

    newUrl = newUrl.trimmed();
    if (newUrl.isEmpty())
        return;

    if (m_feedList->hasFeed(newUrl)) {
        QMessageBox::warning(this, "qBittorrent",
                             tr("This RSS feed is already in the list."),
                             QMessageBox::Ok);
        return;
    }

    Rss::FeedPtr stream(new Rss::Feed(newUrl, m_rssManager.data()));
    rss_parent->addFile(stream);
    // Create TreeWidget item
    QTreeWidgetItem* item = createFolderListItem(stream);
    if (parent_item)
        parent_item->addChild(item);
    else
        m_feedList->addTopLevelItem(item);
    // Notify TreeWidget
    m_feedList->itemAdded(item, stream);

    m_rssManager->saveStreamList();
}

// delete a stream by a button
void RSSImp::deleteSelectedItems()
{
    QList<QTreeWidgetItem*> selectedItems = m_feedList->selectedItems();
    if (selectedItems.isEmpty())
        return;
    if ((selectedItems.size() == 1) && (selectedItems.first() == m_feedList->stickyUnreadItem()))
        return;

    QMessageBox::StandardButton answer = QMessageBox::question(this, tr("Deletion confirmation"),
        tr("Are you sure you want to delete the selected RSS feeds?"),
        QMessageBox::Yes|QMessageBox::No, QMessageBox::No);
    if (answer == QMessageBox::No)
        return;

    foreach (QTreeWidgetItem* item, selectedItems) {
        if (item == m_feedList->stickyUnreadItem())
            continue;
        Rss::FilePtr rss_item = m_feedList->getRSSItem(item);
        QTreeWidgetItem* parent = item->parent();
        // Notify TreeWidget
        m_feedList->itemAboutToBeRemoved(item);
        // Actually delete the item
        rss_item->parentFolder()->removeChild(rss_item->id());
        delete item;
        // Update parents count
        while (parent && (parent != m_feedList->invisibleRootItem())) {
            updateItemInfos(parent);
            parent = parent->parent();
        }
    }
    m_rssManager->saveStreamList();
    // Update Unread items
    updateItemInfos(m_feedList->stickyUnreadItem());
    if (m_feedList->currentItem() == m_feedList->stickyUnreadItem())
        populateArticleList(m_feedList->stickyUnreadItem());

}

void RSSImp::loadFoldersOpenState()
{
    QStringList open_folders = Preferences::instance()->getRssOpenFolders();
    foreach (const QString& var_path, open_folders) {
        QStringList path = var_path.split("\\");
        QTreeWidgetItem* parent = 0;
        foreach (const QString& name, path) {
            int nbChildren = 0;
            if (parent)
                nbChildren = parent->childCount();
            else
                nbChildren = m_feedList->topLevelItemCount();
            for (int i = 0; i < nbChildren; ++i) {
                QTreeWidgetItem* child;
                if (parent)
                    child = parent->child(i);
                else
                    child = m_feedList->topLevelItem(i);
                if (m_feedList->getRSSItem(child)->id() == name) {
                    parent = child;
                    parent->setExpanded(true);
                    qDebug("expanding folder %s", qPrintable(name));
                    break;
                }
            }
        }
    }
}

void RSSImp::saveFoldersOpenState()
{
    QStringList open_folders;
    QList<QTreeWidgetItem*> items = m_feedList->getAllOpenFolders();
    foreach (QTreeWidgetItem* item, items) {
        QString path = m_feedList->getItemPath(item).join("\\");
        qDebug("saving open folder: %s", qPrintable(path));
        open_folders << path;
    }
    Preferences::instance()->setRssOpenFolders(open_folders);
}

// refresh all streams by a button
void RSSImp::refreshAllFeeds()
{
    foreach (QTreeWidgetItem* item, m_feedList->getAllFeedItems())
        item->setData(0, Qt::DecorationRole, QVariant(QIcon(":/icons/loading.png")));
    m_rssManager->refresh();
}

void RSSImp::downloadSelectedTorrents()
{
    QList<QListWidgetItem*> selected_items = listArticles->selectedItems();
    if (selected_items.size() <= 0)
        return;
    foreach (QListWidgetItem* item, selected_items) {
        if (!item) continue;
        Rss::FeedPtr feed = m_feedList->getRSSItemFromUrl(item->data(Article::FeedUrlRole).toString());
        if (!feed) continue;
        Rss::ArticlePtr article = feed->getItem(item->data(Article::IdRole).toString());
        if (!article) continue;

        // Mark as read
        article->markAsRead();
        item->setData(Article::ColorRole, QVariant(QColor("grey")));
        item->setData(Article::IconRole, QVariant(QIcon(":/icons/sphere.png")));

        if (article->torrentUrl().isEmpty())
            continue;
        if (Preferences::instance()->useAdditionDialog())
            AddNewTorrentDialog::show(article->torrentUrl());
        else
            BitTorrent::Session::instance()->addTorrent(article->torrentUrl());
    }
    // Decrement feed nb unread news
    updateItemInfos(m_feedList->stickyUnreadItem());
    updateItemInfos(m_feedList->getTreeItemFromUrl(selected_items.first()->data(Article::FeedUrlRole).toString()));
}

// open the url of the selected RSS articles in the Web browser
void RSSImp::openSelectedArticlesUrls()
{
    QList<QListWidgetItem *> selected_items = listArticles->selectedItems();
    if (selected_items.size() <= 0)
        return;
    foreach (QListWidgetItem* item, selected_items) {
        if (!item) continue;
        Rss::FeedPtr feed = m_feedList->getRSSItemFromUrl(item->data(Article::FeedUrlRole).toString());
        if (!feed) continue;
        Rss::ArticlePtr article = feed->getItem(item->data(Article::IdRole).toString());
        if (!article) continue;

        // Mark as read
        article->markAsRead();
        item->setData(Article::ColorRole, QVariant(QColor("grey")));
        item->setData(Article::IconRole, QVariant(QIcon(":/icons/sphere.png")));

        const QString link = article->link();
        if (!link.isEmpty())
            QDesktopServices::openUrl(QUrl(link));
    }
    // Decrement feed nb unread news
    updateItemInfos(m_feedList->stickyUnreadItem());
    updateItemInfos(m_feedList->getTreeItemFromUrl(selected_items.first()->data(Article::FeedUrlRole).toString()));
}

//right-click on stream : give it an alias
void RSSImp::renameSelectedRssFile()
{
    QList<QTreeWidgetItem*> selectedItems = m_feedList->selectedItems();
    if (selectedItems.size() != 1)
        return;
    QTreeWidgetItem* item = selectedItems.first();
    if (item == m_feedList->stickyUnreadItem())
        return;
    Rss::FilePtr rss_item = m_feedList->getRSSItem(item);
    bool ok;
    QString newName;
    do {
        newName = AutoExpandableDialog::getText(this, tr("Please choose a new name for this RSS feed"), tr("New feed name:"), QLineEdit::Normal, m_feedList->getRSSItem(item)->displayName(), &ok);
        // Check if name is already taken
        if (ok) {
            if (rss_item->parentFolder()->hasChild(newName)) {
                QMessageBox::warning(0, tr("Name already in use"), tr("This name is already used by another item, please choose another one."));
                ok = false;
            }
        }
        else {
            return;
        }
    } while (!ok);
    // Rename item
    rss_item->rename(newName);
    // Update TreeWidget
    updateItemInfos(item);
}

// right-click on stream : refresh it
void RSSImp::refreshSelectedItems()
{
    QList<QTreeWidgetItem*> selectedItems = m_feedList->selectedItems();
    foreach (QTreeWidgetItem* item, selectedItems) {
        Rss::FilePtr file = m_feedList->getRSSItem(item);
        // Update icons
        if (item == m_feedList->stickyUnreadItem()) {
            refreshAllFeeds();
            return;
        }
        else {
            if (!file->refresh())
                continue;
            // Update UI
            if (qSharedPointerDynamicCast<Rss::Feed>(file)) {
                item->setData(0, Qt::DecorationRole, QVariant(QIcon(":/icons/loading.png")));
            }
            else if (qSharedPointerDynamicCast<Rss::Folder>(file)) {
                // Update feeds in the folder
                foreach (QTreeWidgetItem *feed, m_feedList->getAllFeedItems(item))
                    feed->setData(0, Qt::DecorationRole, QVariant(QIcon(":/icons/loading.png")));
            }
        }
    }
}

void RSSImp::copySelectedFeedsURL()
{
    QStringList URLs;
    QList<QTreeWidgetItem*> selectedItems = m_feedList->selectedItems();
    QTreeWidgetItem* item;
    foreach (item, selectedItems)
        if (m_feedList->isFeed(item))
            URLs << m_feedList->getItemID(item);
    qApp->clipboard()->setText(URLs.join("\n"));
}

void RSSImp::on_markReadButton_clicked()
{
    QList<QTreeWidgetItem*> selectedItems = m_feedList->selectedItems();
    foreach (QTreeWidgetItem* item, selectedItems) {
        Rss::FilePtr rss_item = m_feedList->getRSSItem(item);
        Q_ASSERT(rss_item);
        rss_item->markAsRead();
        updateItemInfos(item);
    }
    // Update article list
    if (!selectedItems.isEmpty())
        populateArticleList(m_feedList->currentItem());
}

QTreeWidgetItem* RSSImp::createFolderListItem(const Rss::FilePtr& rssFile)
{
    Q_ASSERT(rssFile);
    QTreeWidgetItem* item = new QTreeWidgetItem;
    item->setData(0, Qt::DisplayRole, QVariant(rssFile->displayName() + QString::fromUtf8("  (") + QString::number(rssFile->unreadCount()) + QString(")")));
    item->setData(0, Qt::DecorationRole, QIcon(rssFile->iconPath()));

    return item;
}

void RSSImp::fillFeedsList(QTreeWidgetItem* parent, const Rss::FolderPtr& rss_parent)
{
    QList<Rss::FilePtr> children;
    if (parent)
        children = rss_parent->getContent();
    else
        children = m_rssManager->rootFolder()->getContent();
    foreach (const Rss::FilePtr& rssFile, children) {
        QTreeWidgetItem* item = createFolderListItem(rssFile);
        Q_ASSERT(item);
        if (parent)
            parent->addChild(item);
        else
            m_feedList->addTopLevelItem(item);

        // Notify TreeWidget of item addition
        m_feedList->itemAdded(item, rssFile);

        // Recursive call if this is a folder.
        if (Rss::FolderPtr folder = qSharedPointerDynamicCast<Rss::Folder>(rssFile))
            fillFeedsList(item, folder);
    }
}

QListWidgetItem* RSSImp::createArticleListItem(const Rss::ArticlePtr& article)
{
    Q_ASSERT(article);
    QListWidgetItem* item = new QListWidgetItem;

    item->setData(Article::TitleRole, article->title());
    item->setData(Article::FeedUrlRole, article->parent()->url());
    item->setData(Article::IdRole, article->guid());
    if (article->isRead()) {
        item->setData(Article::ColorRole, QVariant(QColor("grey")));
        item->setData(Article::IconRole, QVariant(QIcon(":/icons/sphere.png")));
    }
    else {
        item->setData(Article::ColorRole, QVariant(QColor("blue")));
        item->setData(Article::IconRole, QVariant(QIcon(":/icons/sphere2.png")));
    }

    return item;
}

// fills the newsList
void RSSImp::populateArticleList(QTreeWidgetItem* item)
{
    if (!item) {
        listArticles->clear();
        return;
    }

    Rss::FilePtr rss_item = m_feedList->getRSSItem(item);
    if (!rss_item)
        return;

    // Clear the list first
    textBrowser->clear();
    m_currentArticle = 0;
    listArticles->clear();

    qDebug("Getting the list of news");
    Rss::ArticleList articles;
    if (rss_item == m_rssManager->rootFolder())
        articles = rss_item->unreadArticleListByDateDesc();
    else
        articles = rss_item->articleListByDateDesc();

    qDebug("Got the list of news");
    foreach (const Rss::ArticlePtr& article, articles) {
        QListWidgetItem* articleItem = createArticleListItem(article);
        listArticles->addItem(articleItem);
    }
    qDebug("Added all news to the GUI");
}

// display a news
void RSSImp::refreshTextBrowser()
{
    QList<QListWidgetItem*> selection = listArticles->selectedItems();
    if (selection.empty()) return;
    QListWidgetItem *item = selection.first();
    Q_ASSERT(item);
    if (item == m_currentArticle) return;
    m_currentArticle = item;

    Rss::FeedPtr feed = m_feedList->getRSSItemFromUrl(item->data(Article::FeedUrlRole).toString());
    if (!feed) return;
    Rss::ArticlePtr article = feed->getItem(item->data(Article::IdRole).toString());
    if (!article) return;
    QString html;
    html += "<div style='border: 2px solid red; margin-left: 5px; margin-right: 5px; margin-bottom: 5px;'>";
    html += "<div style='background-color: #678db2; font-weight: bold; color: #fff;'>" + article->title() + "</div>";
    if (article->date().isValid())
        html += "<div style='background-color: #efefef;'><b>" + tr("Date: ") + "</b>" + article->date().toLocalTime().toString(Qt::SystemLocaleLongDate) + "</div>";
    if (!article->author().isEmpty())
        html += "<div style='background-color: #efefef;'><b>" + tr("Author: ") + "</b>" + article->author() + "</div>";
    html += "</div>";
    html += "<div style='margin-left: 5px; margin-right: 5px;'>";
    if(Qt::mightBeRichText(article->description())) {
        html += article->description();
    }
    else {
        QString description = article->description();
        QRegExp rx;
        // If description is plain text, replace BBCode tags with HTML and wrap everything in <pre></pre> so it looks nice
        rx.setMinimal(true);
        rx.setCaseSensitivity(Qt::CaseInsensitive);

        rx.setPattern("\\[img\\](.+)\\[/img\\]");
        description = description.replace(rx, "<img src=\"\\1\">");

        rx.setPattern("\\[url=(\")?(.+)\\1\\]");
        description = description.replace(rx, "<a href=\"\\2\">");
        description = description.replace("[/url]", "</a>", Qt::CaseInsensitive);

        rx.setPattern("\\[(/)?([bius])\\]");
        description = description.replace(rx, "<\\1\\2>");

        rx.setPattern("\\[color=(\")?(.+)\\1\\]");
        description = description.replace(rx, "<span style=\"color:\\2\">");
        description = description.replace("[/color]", "</span>", Qt::CaseInsensitive);

        rx.setPattern("\\[size=(\")?(.+)\\d\\1\\]");
        description = description.replace(rx, "<span style=\"font-size:\\2px\">");
        description = description.replace("[/size]", "</span>", Qt::CaseInsensitive);

        html += "<pre>" + description + "</pre>";
    }
    html += "</div>";
    textBrowser->setHtml(html);
    article->markAsRead();
    item->setData(Article::ColorRole, QVariant(QColor("grey")));
    item->setData(Article::IconRole, QVariant(QIcon(":/icons/sphere.png")));
    // Decrement feed nb unread news
    updateItemInfos(m_feedList->stickyUnreadItem());
    updateItemInfos(m_feedList->getTreeItemFromUrl(item->data(Article::FeedUrlRole).toString()));
}

void RSSImp::saveSlidersPosition()
{
    // Remember sliders positions
    Preferences* const pref = Preferences::instance();
    pref->setRssHSplitterState(splitter_h->saveState());
    pref->setRssVSplitterState(splitter_v->saveState());
    qDebug("Splitters position saved");
}

void RSSImp::restoreSlidersPosition()
{
    const Preferences* const pref = Preferences::instance();
    const QByteArray pos_h = pref->getRssHSplitterState();
    if (!pos_h.isEmpty())
        splitter_h->restoreState(pos_h);
    const QByteArray pos_v = pref->getRssVSplitterState();
    if (!pos_v.isEmpty())
        splitter_v->restoreState(pos_v);
}

void RSSImp::updateItemsInfos(const QList<QTreeWidgetItem*>& items)
{
    foreach (QTreeWidgetItem* item, items)
        updateItemInfos(item);
}

void RSSImp::updateItemInfos(QTreeWidgetItem *item)
{
    Rss::FilePtr rss_item = m_feedList->getRSSItem(item);
    if (!rss_item)
        return;

    QString name;
    if (rss_item == m_rssManager->rootFolder()) {
        name = tr("Unread");
        emit updateRSSCount(rss_item->unreadCount());
    }
    else
        name = rss_item->displayName();
    item->setText(0, name + QString::fromUtf8("  (") + QString::number(rss_item->unreadCount()) + QString(")"));
    // If item has a parent, update it too
    if (item->parent())
        updateItemInfos(item->parent());
}

void RSSImp::updateFeedIcon(const QString& url, const QString& iconPath)
{
    QTreeWidgetItem* item = m_feedList->getTreeItemFromUrl(url);
    item->setData(0, Qt::DecorationRole, QVariant(QIcon(iconPath)));
}

void RSSImp::updateFeedInfos(const QString& url, const QString& display_name, uint nbUnread)
{
    qDebug() << Q_FUNC_INFO << display_name;
    QTreeWidgetItem *item = m_feedList->getTreeItemFromUrl(url);
    Rss::FeedPtr stream = qSharedPointerCast<Rss::Feed>(m_feedList->getRSSItem(item));
    item->setText(0, display_name + QString::fromUtf8("  (") + QString::number(nbUnread) + QString(")"));
    if (!stream->isLoading())
        item->setData(0, Qt::DecorationRole, QIcon(stream->iconPath()));
    // Update parent
    if (item->parent())
        updateItemInfos(item->parent());
    // Update Unread item
    updateItemInfos(m_feedList->stickyUnreadItem());
}

void RSSImp::onFeedContentChanged(const QString& url)
{
    qDebug() << Q_FUNC_INFO << url;
    QTreeWidgetItem *item = m_feedList->getTreeItemFromUrl(url);
    // If the feed is selected, update the displayed news
    if (m_feedList->currentItem() == item)
        populateArticleList(item);
    // Update unread items
    else if (m_feedList->currentItem() == m_feedList->stickyUnreadItem())
        populateArticleList(m_feedList->stickyUnreadItem());
}

void RSSImp::updateRefreshInterval(uint val)
{
    m_rssManager->updateRefreshInterval(val);
}

RSSImp::RSSImp(QWidget *parent):
    QWidget(parent),
    m_rssManager(new Rss::Manager)
{
    setupUi(this);
    // Icons
    actionCopy_feed_URL->setIcon(GuiIconProvider::instance()->getIcon("edit-copy"));
    actionDelete->setIcon(GuiIconProvider::instance()->getIcon("edit-delete"));
    actionDownload_torrent->setIcon(GuiIconProvider::instance()->getIcon("download"));
    actionManage_cookies->setIcon(GuiIconProvider::instance()->getIcon("preferences-web-browser-cookies"));
    actionMark_items_read->setIcon(GuiIconProvider::instance()->getIcon("mail-mark-read"));
    actionNew_folder->setIcon(GuiIconProvider::instance()->getIcon("folder-new"));
    actionNew_subscription->setIcon(GuiIconProvider::instance()->getIcon("list-add"));
    actionOpen_news_URL->setIcon(GuiIconProvider::instance()->getIcon("application-x-mswinurl"));
    actionRename->setIcon(GuiIconProvider::instance()->getIcon("edit-rename"));
    actionUpdate->setIcon(GuiIconProvider::instance()->getIcon("view-refresh"));
    actionUpdate_all_feeds->setIcon(GuiIconProvider::instance()->getIcon("view-refresh"));
    newFeedButton->setIcon(GuiIconProvider::instance()->getIcon("list-add"));
    markReadButton->setIcon(GuiIconProvider::instance()->getIcon("mail-mark-read"));
    updateAllButton->setIcon(GuiIconProvider::instance()->getIcon("view-refresh"));
    rssDownloaderBtn->setIcon(GuiIconProvider::instance()->getIcon("download"));
    settingsButton->setIcon(GuiIconProvider::instance()->getIcon("preferences-system"));

    m_feedList = new FeedListWidget(splitter_h, m_rssManager);
    splitter_h->insertWidget(0, m_feedList);
    editHotkey = new QShortcut(QKeySequence("F2"), m_feedList, 0, 0, Qt::WidgetShortcut);
    connect(editHotkey, SIGNAL(activated()), SLOT(renameSelectedRssFile()));
    connect(m_feedList, SIGNAL(doubleClicked(QModelIndex)), SLOT(renameSelectedRssFile()));
    deleteHotkey = new QShortcut(QKeySequence(QKeySequence::Delete), m_feedList, 0, 0, Qt::WidgetShortcut);
    connect(deleteHotkey, SIGNAL(activated()), SLOT(deleteSelectedItems()));

    m_rssManager->loadStreamList();
    fillFeedsList();
    populateArticleList(m_feedList->currentItem());

    loadFoldersOpenState();
    connect(m_rssManager.data(), SIGNAL(feedInfosChanged(QString, QString, unsigned int)), SLOT(updateFeedInfos(QString, QString, unsigned int)));
    connect(m_rssManager.data(), SIGNAL(feedContentChanged(QString)), SLOT(onFeedContentChanged(QString)));
    connect(m_rssManager.data(), SIGNAL(feedIconChanged(QString, QString)), SLOT(updateFeedIcon(QString, QString)));

    connect(m_feedList, SIGNAL(customContextMenuRequested(const QPoint &)), SLOT(displayRSSListMenu(const QPoint &)));
    connect(listArticles, SIGNAL(customContextMenuRequested(const QPoint &)), SLOT(displayItemsListMenu(const QPoint &)));

    // Feeds list actions
    connect(actionDelete, SIGNAL(triggered()), this, SLOT(deleteSelectedItems()));
    connect(actionRename, SIGNAL(triggered()), this, SLOT(renameSelectedRssFile()));
    connect(actionUpdate, SIGNAL(triggered()), this, SLOT(refreshSelectedItems()));
    connect(actionNew_folder, SIGNAL(triggered()), this, SLOT(askNewFolder()));
    connect(actionNew_subscription, SIGNAL(triggered()), this, SLOT(on_newFeedButton_clicked()));
    connect(actionUpdate_all_feeds, SIGNAL(triggered()), this, SLOT(refreshAllFeeds()));
    connect(updateAllButton, SIGNAL(clicked()), SLOT(refreshAllFeeds()));
    connect(actionCopy_feed_URL, SIGNAL(triggered()), this, SLOT(copySelectedFeedsURL()));
    connect(actionMark_items_read, SIGNAL(triggered()), this, SLOT(on_markReadButton_clicked()));
    // News list actions
    connect(actionOpen_news_URL, SIGNAL(triggered()), this, SLOT(openSelectedArticlesUrls()));
    connect(actionDownload_torrent, SIGNAL(triggered()), this, SLOT(downloadSelectedTorrents()));

    connect(m_feedList, SIGNAL(currentItemChanged(QTreeWidgetItem*,QTreeWidgetItem*)), this, SLOT(populateArticleList(QTreeWidgetItem*)));
    connect(m_feedList, SIGNAL(foldersAltered(QList<QTreeWidgetItem*>)), this, SLOT(updateItemsInfos(QList<QTreeWidgetItem*>)));

    connect(listArticles, SIGNAL(itemSelectionChanged()), this, SLOT(refreshTextBrowser()));
    connect(listArticles, SIGNAL(itemDoubleClicked(QListWidgetItem *)), this, SLOT(downloadSelectedTorrents()));

    // Restore sliders position
    restoreSlidersPosition();
    // Bind saveSliders slots
    connect(splitter_v, SIGNAL(splitterMoved(int, int)), this, SLOT(saveSlidersPosition()));
    connect(splitter_h, SIGNAL(splitterMoved(int, int)), this, SLOT(saveSlidersPosition()));

    qDebug("RSSImp constructed");
}

RSSImp::~RSSImp()
{
    qDebug("Deleting RSSImp...");
    saveFoldersOpenState();
    delete editHotkey;
    delete deleteHotkey;
    delete m_feedList;
    qDebug("RSSImp deleted");
}

void RSSImp::on_settingsButton_clicked()
{
    RssSettingsDlg dlg(this);
    if (dlg.exec())
        updateRefreshInterval(Preferences::instance()->getRSSRefreshInterval());
}

void RSSImp::on_rssDownloaderBtn_clicked()
{
    AutomatedRssDownloader dlg(m_rssManager, this);
    dlg.exec();
    if (dlg.isRssDownloaderEnabled()) {
        m_rssManager->rootFolder()->recheckRssItemsForDownload();
        refreshAllFeeds();
    }
}
