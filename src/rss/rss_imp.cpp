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
#include <QInputDialog>
#include <QMenu>
#include <QStandardItemModel>
#include <QMessageBox>
#include <QString>
#include <QClipboard>
#include <QDragMoveEvent>

#include "rss_imp.h"
#include "feedlistwidget.h"
#include "qbtsession.h"
#include "cookiesdlg.h"
#include "preferences.h"
#include "rsssettingsdlg.h"
#include "rssmanager.h"
#include "rssfolder.h"
#include "rssarticle.h"
#include "rssfeed.h"
#include "rsssettings.h"
#include "automatedrssdownloader.h"
#include "iconprovider.h"

namespace Article {
enum ArticleRoles {
  TitleRole = Qt::DisplayRole,
  IconRole = Qt::DecorationRole,
  ColorRole = Qt::ForegroundRole,
  IdRole = Qt::UserRole + 1,
  FeedUrlRole = Qt::UserRole + 2
};
}

// display a right-click menu
void RSSImp::displayRSSListMenu(const QPoint& pos){
  if(!m_feedList->indexAt(pos).isValid()) {
    // No item under the mouse, clear selection
    m_feedList->clearSelection();
  }
  QMenu myRSSListMenu(this);
  QList<QTreeWidgetItem*> selectedItems = m_feedList->selectedItems();
  if(selectedItems.size() > 0) {
    myRSSListMenu.addAction(actionUpdate);
    myRSSListMenu.addAction(actionMark_items_read);
    myRSSListMenu.addSeparator();
    if(selectedItems.size() == 1) {
      if(m_feedList->getRSSItem(selectedItems.first()) != m_rssManager) {
        myRSSListMenu.addAction(actionRename);
        myRSSListMenu.addAction(actionDelete);
        myRSSListMenu.addSeparator();
        if(m_feedList->getItemType(selectedItems.first()) == IRssFile::FOLDER) {
          myRSSListMenu.addAction(actionNew_folder);
        } else {
          myRSSListMenu.addAction(actionManage_cookies);
        }
      }
    }
    myRSSListMenu.addAction(actionNew_subscription);
    if(m_feedList->getItemType(selectedItems.first()) == IRssFile::FEED) {
      myRSSListMenu.addSeparator();
      myRSSListMenu.addAction(actionCopy_feed_URL);
    }
  }else{
    myRSSListMenu.addAction(actionNew_subscription);
    myRSSListMenu.addAction(actionNew_folder);
    myRSSListMenu.addSeparator();
    myRSSListMenu.addAction(actionUpdate_all_feeds);
  }
  myRSSListMenu.exec(QCursor::pos());
}

void RSSImp::displayItemsListMenu(const QPoint&){
  QMenu myItemListMenu(this);
  QList<QListWidgetItem*> selectedItems = listArticles->selectedItems();
  if(selectedItems.size() > 0) {
    bool has_attachment = false;
    foreach(const QListWidgetItem *item, selectedItems) {
      qDebug("text(3) URL: %s", qPrintable(item->data(Article::FeedUrlRole).toString()));
      qDebug("text(2) TITLE: %s", qPrintable(item->data(Article::TitleRole).toString()));
      if(m_feedList->getRSSItemFromUrl(item->data(Article::FeedUrlRole).toString())
          ->getItem(item->data(Article::IdRole).toString()).hasAttachment()) {
        has_attachment = true;
        break;
      }
    }
    if(has_attachment)
      myItemListMenu.addAction(actionDownload_torrent);
    myItemListMenu.addAction(actionOpen_news_URL);
  }
  myItemListMenu.exec(QCursor::pos());
}

void RSSImp::on_actionManage_cookies_triggered() {
  Q_ASSERT(!m_feedList->selectedItems().empty());
  // Get feed hostname
  QString feed_url = m_feedList->getItemID(m_feedList->selectedItems().first());
  QString feed_hostname = QUrl::fromEncoded(feed_url.toLocal8Bit()).host();
  qDebug("RSS Feed hostname is: %s", qPrintable(feed_hostname));
  Q_ASSERT(!feed_hostname.isEmpty());
  bool ok = false;
  RssSettings settings;
  QList<QByteArray> raw_cookies = CookiesDlg::askForCookies(this, settings.getHostNameCookies(feed_hostname), &ok);
  if(ok) {
    settings.setHostNameCookies(feed_hostname, raw_cookies);
  }
}

void RSSImp::askNewFolder() {
  QTreeWidgetItem *parent_item = 0;
  RssFolder *rss_parent;
  if(m_feedList->selectedItems().size() > 0) {
    parent_item = m_feedList->selectedItems().at(0);
    rss_parent = dynamic_cast<RssFolder*>(m_feedList->getRSSItem(parent_item));
    Q_ASSERT(rss_parent->type() == IRssFile::FOLDER);
  } else {
    rss_parent = m_rssManager;
  }
  bool ok;
  QString new_name = QInputDialog::getText(this, tr("Please choose a folder name"), tr("Folder name:"), QLineEdit::Normal, tr("New folder"), &ok);
  if(ok) {
    RssFolder* new_folder = rss_parent->addFolder(new_name);
    QTreeWidgetItem* folder_item;
    if(parent_item)
      folder_item = new QTreeWidgetItem(parent_item);
    else
      folder_item = new QTreeWidgetItem(m_feedList);
    // Notify TreeWidget
    m_feedList->itemAdded(folder_item, new_folder);
    // Set Text
    folder_item->setText(0, new_folder->displayName() + QString::fromUtf8("  (0)"));
    folder_item->setData(0,Qt::DecorationRole, QVariant(IconProvider::instance()->getIcon("inode-directory")));
    // Expand parent folder to display new folder
    if(parent_item)
      parent_item->setExpanded(true);
    m_rssManager->saveStreamList();
  }
}

void RSSImp::displayOverwriteError(const QString &filename) {
  QMessageBox::warning(this, tr("Overwrite attempt"),
                       tr("You cannot overwrite %1 item.", "You cannot overwrite myFolder item.").arg(filename),
                       QMessageBox::Ok);
}

// add a stream by a button
void RSSImp::on_newFeedButton_clicked() {
  // Determine parent folder for new feed
  QTreeWidgetItem *parent_item = 0;
  QList<QTreeWidgetItem *> selected_items = m_feedList->selectedItems();
  if(!selected_items.empty()) {
    parent_item = selected_items.first();
    // Consider the case where the user clicked on Unread item
    if(parent_item == m_feedList->stickyUnreadItem()) {
      parent_item = 0;
    } else {
      if(m_feedList->getItemType(parent_item) != IRssFile::FOLDER)
        parent_item = parent_item->parent();
    }
  }
  RssFolder *rss_parent;
  if(parent_item) {
    rss_parent = (RssFolder*)m_feedList->getRSSItem(parent_item);
  } else {
    rss_parent = m_rssManager;
  }
  // Ask for feed URL
  bool ok;
  QString clip_txt = qApp->clipboard()->text();
  QString default_url = "http://";
  if(clip_txt.startsWith("http://", Qt::CaseInsensitive) || clip_txt.startsWith("https://", Qt::CaseInsensitive) || clip_txt.startsWith("ftp://", Qt::CaseInsensitive)) {
    default_url = clip_txt;
  }
  QString newUrl = QInputDialog::getText(this, tr("Please type a rss stream url"), tr("Stream URL:"), QLineEdit::Normal, default_url, &ok);
  if(ok) {
    newUrl = newUrl.trimmed();
    if(!newUrl.isEmpty()){
      if(m_feedList->hasFeed(newUrl)) {
        QMessageBox::warning(this, tr("qBittorrent"),
                             tr("This rss feed is already in the list."),
                             QMessageBox::Ok);
        return;
      }
      RssFeed *stream = rss_parent->addStream(newUrl);
      // Create TreeWidget item
      QTreeWidgetItem* item;
      if(parent_item)
        item = new QTreeWidgetItem(parent_item);
      else
        item = new QTreeWidgetItem(m_feedList);
      // Notify TreeWidget
      m_feedList->itemAdded(item, stream);
      // Set text
      item->setText(0, stream->displayName() + QString::fromUtf8("  (0)"));
      item->setData(0,Qt::DecorationRole, QVariant(QIcon(":/Icons/loading.png")));
      stream->refresh();
      m_rssManager->saveStreamList();
    }
  }
}

// delete a stream by a button
void RSSImp::deleteSelectedItems() {
  QList<QTreeWidgetItem*> selectedItems = m_feedList->selectedItems();
  if(selectedItems.size() == 0) return;
  int ret;
  if(selectedItems.size() > 1)
    ret = QMessageBox::question(this, tr("Are you sure? -- qBittorrent"), tr("Are you sure you want to delete these elements from the list?"),
                                tr("&Yes"), tr("&No"),
                                QString(), 0, 1);
  else
    ret = QMessageBox::question(this, tr("Are you sure? -- qBittorrent"), tr("Are you sure you want to delete this element from the list?"),
                                tr("&Yes"), tr("&No"),
                                QString(), 0, 1);
  if(!ret) {
    foreach(QTreeWidgetItem *item, selectedItems){
      if(m_feedList->currentFeed() == item){
        textBrowser->clear();
        m_currentArticle = 0;
        listArticles->clear();
      }
      IRssFile *rss_item = m_feedList->getRSSItem(item);
      QTreeWidgetItem * parent = item->parent();
      // Notify TreeWidget
      m_feedList->itemAboutToBeRemoved(item);
      // Actually delete the item
      rss_item->parent()->removeChild(rss_item->id());
      delete item;
      // Update parents count
      while (parent && parent != m_feedList->invisibleRootItem()) {
        updateItemInfos (parent);
        parent = parent->parent();
      }
    }
    m_rssManager->saveStreamList();
    // Update Unread items
    updateItemInfos(m_feedList->stickyUnreadItem());
  }
}

void RSSImp::loadFoldersOpenState() {
  QIniSettings settings("qBittorrent", "qBittorrent");
  settings.beginGroup("Rss");
  QStringList open_folders = settings.value("open_folders", QStringList()).toStringList();
  settings.endGroup();
  foreach(QString var_path, open_folders) {
    QStringList path = var_path.split("\\");
    QTreeWidgetItem *parent = 0;
    foreach(QString name, path) {
      int nbChildren = 0;
      if(parent)
        nbChildren = parent->childCount();
      else
        nbChildren = m_feedList->topLevelItemCount();
      for(int i=0; i<nbChildren; ++i) {
        QTreeWidgetItem* child;
        if(parent)
          child = parent->child(i);
        else
          child = m_feedList->topLevelItem(i);
        if(m_feedList->getRSSItem(child)->id() == name) {
          parent = child;
          parent->setExpanded(true);
          qDebug("expanding folder %s", qPrintable(name));
          break;
        }  
      }
    }
  }
}

void RSSImp::saveFoldersOpenState() {
  QStringList open_folders;
  QList<QTreeWidgetItem*> items = m_feedList->getAllOpenFolders();
  foreach(QTreeWidgetItem* item, items) {
    QString path = m_feedList->getItemPath(item).join("\\");
    qDebug("saving open folder: %s", qPrintable(path));
    open_folders << path;
  }
  QIniSettings settings("qBittorrent", "qBittorrent");
  settings.beginGroup("Rss");
  settings.setValue("open_folders", open_folders);
  settings.endGroup();
}

// refresh all streams by a button
void RSSImp::on_updateAllButton_clicked() {
  foreach(QTreeWidgetItem *item, m_feedList->getAllFeedItems()) {
    item->setData(0,Qt::DecorationRole, QVariant(QIcon(":/Icons/loading.png")));
  }
  m_rssManager->refresh();
}

void RSSImp::downloadTorrent() {
  QList<QListWidgetItem *> selected_items = listArticles->selectedItems();
  foreach(const QListWidgetItem* item, selected_items) {
    const RssArticle article =  m_feedList->getRSSItemFromUrl(item->data(Article::FeedUrlRole).toString())
        ->getItem(item->data(Article::IdRole).toString());
    if(article.hasAttachment()) {
      QBtSession::instance()->downloadFromUrl(article.torrentUrl());
    } else {
      QBtSession::instance()->downloadFromUrl(article.link());
    }
  }
}

// open the url of the news in a browser
void RSSImp::openNewsUrl() {
  QList<QListWidgetItem *> selected_items = listArticles->selectedItems();
  foreach(const QListWidgetItem* item, selected_items) {
    const RssArticle news =  m_feedList->getRSSItemFromUrl(item->data(Article::FeedUrlRole).toString())
        ->getItem(item->data(Article::IdRole).toString());
    const QString link = news.link();
    if(!link.isEmpty())
      QDesktopServices::openUrl(QUrl(link));
  }
}

//right-click on stream : give it an alias
void RSSImp::renameFiles() {
  QList<QTreeWidgetItem*> selectedItems = m_feedList->selectedItems();
  Q_ASSERT(selectedItems.size() == 1);
  QTreeWidgetItem *item = selectedItems.at(0);
  IRssFile *rss_item = m_feedList->getRSSItem(item);
  bool ok;
  QString newName;
  do {
    newName = QInputDialog::getText(this, tr("Please choose a new name for this RSS feed"), tr("New feed name:"), QLineEdit::Normal, m_feedList->getRSSItem(item)->displayName(), &ok);
    // Check if name is already taken
    if(ok) {
      if(rss_item->parent()->hasChild(newName)) {
        QMessageBox::warning(0, tr("Name already in use"), tr("This name is already used by another item, please choose another one."));
        ok = false;
      }
    } else {
      return;
    }
  }while(!ok);
  // Rename item
  rss_item->rename(newName);
  // Update TreeWidget
  updateItemInfos(item);
}

//right-click on stream : refresh it
void RSSImp::refreshSelectedItems() {
  QList<QTreeWidgetItem*> selectedItems = m_feedList->selectedItems();
  foreach(QTreeWidgetItem* item, selectedItems){
    IRssFile* file = m_feedList->getRSSItem(item);
    // Update icons
    if(item == m_feedList->stickyUnreadItem()) {
      foreach(QTreeWidgetItem *feed, m_feedList->getAllFeedItems()) {
        feed->setData(0,Qt::DecorationRole, QVariant(QIcon(":/Icons/loading.png")));
      }
      file->refresh();
      break;
    } else {
      if(file->type() == IRssFile::FEED) {
        item->setData(0,Qt::DecorationRole, QVariant(QIcon(":/Icons/loading.png")));
      } else {
        // Update feeds in the folder
        foreach(QTreeWidgetItem *feed, m_feedList->getAllFeedItems(item)) {
          feed->setData(0,Qt::DecorationRole, QVariant(QIcon(":/Icons/loading.png")));
        }
      }
    }
    // Actually refresh
    file->refresh();
  }
}

void RSSImp::copySelectedFeedsURL() {
  QStringList URLs;
  QList<QTreeWidgetItem*> selectedItems = m_feedList->selectedItems();
  QTreeWidgetItem* item;
  foreach(item, selectedItems){
    if(m_feedList->getItemType(item) == IRssFile::FEED)
      URLs << m_feedList->getItemID(item);
  }
  qApp->clipboard()->setText(URLs.join("\n"));
}

void RSSImp::on_markReadButton_clicked() {
  QList<QTreeWidgetItem*> selectedItems = m_feedList->selectedItems();
  QTreeWidgetItem* item;
  foreach(item, selectedItems){
    IRssFile *rss_item = m_feedList->getRSSItem(item);
    rss_item->markAsRead();
    updateItemInfos(item);
  }
  if(selectedItems.size())
    refreshArticleList(m_feedList->currentItem());
}

void RSSImp::fillFeedsList(QTreeWidgetItem *parent, RssFolder *rss_parent) {
  QList<IRssFile*> children;
  if(parent) {
    children = rss_parent->getContent();
  } else {
    children = m_rssManager->getContent();
  }
  foreach(IRssFile* rss_child, children){
    QTreeWidgetItem* item;
    if(!parent)
      item = new QTreeWidgetItem(m_feedList);
    else
      item = new QTreeWidgetItem(parent);
    item->setData(0, Qt::DisplayRole, QVariant(rss_child->displayName()+ QString::fromUtf8("  (")+QString::number(rss_child->unreadCount(), 10)+QString(")")));
    // Notify TreeWidget of item addition
    m_feedList->itemAdded(item, rss_child);
    // Set Icon
    if(rss_child->type() == IRssFile::FEED) {
      item->setData(0,Qt::DecorationRole, QVariant(QIcon(QString::fromUtf8(":/Icons/loading.png"))));
    } else {
      item->setData(0,Qt::DecorationRole, QVariant(IconProvider::instance()->getIcon("inode-directory")));
      // Recurvive call to load sub folders/files
      fillFeedsList(item, (RssFolder*)rss_child);
    }
  }
}

// fills the newsList
void RSSImp::refreshArticleList(QTreeWidgetItem* item) {
  if(!item) {
    listArticles->clear();
    return;
  }

  IRssFile *rss_item = m_feedList->getRSSItem(item);
  if(!rss_item) return;

  qDebug("Getting the list of news");
  QList<RssArticle> news;
  if(rss_item == m_rssManager)
    news = RssManager::sortNewsList(rss_item->unreadArticleList());
  else if(rss_item)
    news = RssManager::sortNewsList(rss_item->articleList());
  // Clear the list first
  textBrowser->clear();
  m_currentArticle = 0;
  listArticles->clear();
  qDebug("Got the list of news");
  foreach(const RssArticle &article, news){
    QListWidgetItem* it = new QListWidgetItem(listArticles);
    it->setData(Article::TitleRole, article.title());
    it->setData(Article::FeedUrlRole, article.parent()->url());
    it->setData(Article::IdRole, article.guid());
    if(article.isRead()){
      it->setData(Article::ColorRole, QVariant(QColor("grey")));
      it->setData(Article::IconRole, QVariant(QIcon(":/Icons/sphere.png")));
    }else{
      it->setData(Article::ColorRole, QVariant(QColor("blue")));
      it->setData(Article::IconRole, QVariant(QIcon(":/Icons/sphere2.png")));
    }
  }
  qDebug("Added all news to the GUI");
  qDebug("First news selected");
}

// display a news
void RSSImp::refreshTextBrowser() {
  QList<QListWidgetItem*> selection = listArticles->selectedItems();
  if(selection.empty()) return;
  Q_ASSERT(selection.size() == 1);
  QListWidgetItem *item = selection.first();
  Q_ASSERT(item);
  if(item == m_currentArticle) return;
  // Stop displaying previous news if necessary
  if(m_feedList->currentFeed() == m_feedList->stickyUnreadItem()) {
    if(m_currentArticle) {
      disconnect(listArticles, SIGNAL(itemSelectionChanged()), this, SLOT(refreshTextBrowser()));
      listArticles->removeItemWidget(m_currentArticle);
      Q_ASSERT(m_currentArticle);
      delete m_currentArticle;
      connect(listArticles, SIGNAL(itemSelectionChanged()), this, SLOT(refreshTextBrowser()));
    }
    m_currentArticle = item;
  }
  RssFeed *stream = m_feedList->getRSSItemFromUrl(item->data(Article::FeedUrlRole).toString());
  RssArticle &article = stream->getItem(item->data(Article::IdRole).toString());
  QString html;
  html += "<div style='border: 2px solid red; margin-left: 5px; margin-right: 5px; margin-bottom: 5px;'>";
  html += "<div style='background-color: #678db2; font-weight: bold; color: #fff;'>"+article.title() + "</div>";
  if(article.date().isValid()) {
    html += "<div style='background-color: #efefef;'><b>"+tr("Date: ")+"</b>"+article.date().toLocalTime().toString(Qt::SystemLocaleLongDate)+"</div>";
  }
  if(!article.author().isEmpty()) {
    html += "<div style='background-color: #efefef;'><b>"+tr("Author: ")+"</b>"+article.author()+"</div>";
  }
  html += "</div>";
  html += "<divstyle='margin-left: 5px; margin-right: 5px;'>"+article.description()+"</div>";
  textBrowser->setHtml(html);
  article.markAsRead();
  item->setData(Article::ColorRole, QVariant(QColor("grey")));
  item->setData(Article::IconRole, QVariant(QIcon(":/Icons/sphere.png")));
  // Decrement feed nb unread news
  updateItemInfos(m_feedList->stickyUnreadItem());
  updateItemInfos(m_feedList->getTreeItemFromUrl(item->data(Article::FeedUrlRole).toString()));
}

void RSSImp::saveSlidersPosition() {
  // Remember sliders positions
  QIniSettings settings("qBittorrent", "qBittorrent");
  settings.setValue("rss/splitter_h", splitter_h->saveState());
  settings.setValue("rss/splitter_v", splitter_v->saveState());
  qDebug("Splitters position saved");
}

void RSSImp::restoreSlidersPosition() {
  QIniSettings settings("qBittorrent", "qBittorrent");
  QByteArray pos_h = settings.value("rss/splitter_h", QByteArray()).toByteArray();
  if(!pos_h.isNull()) {
    splitter_h->restoreState(pos_h);
  }
  QByteArray pos_v = settings.value("rss/splitter_v", QByteArray()).toByteArray();
  if(!pos_v.isNull()) {
    splitter_v->restoreState(pos_v);
  }
}

void RSSImp::updateItemsInfos(const QList<QTreeWidgetItem *> &items) {
  foreach(QTreeWidgetItem* item, items) {
    updateItemInfos(item);
  }
}

void RSSImp::updateItemInfos(QTreeWidgetItem *item) {
  IRssFile *rss_item = m_feedList->getRSSItem(item);
  if (!rss_item)
    return;

  QString name;
  if(rss_item == m_rssManager)
    name = tr("Unread");
  else
    name = rss_item->displayName();
  item->setText(0, name + QString::fromUtf8("  (") + QString::number(rss_item->unreadCount(), 10)+ QString(")"));
  // If item has a parent, update it too
  if(item->parent())
    updateItemInfos(item->parent());
}

void RSSImp::updateFeedIcon(const QString &url, const QString &icon_path){
  QTreeWidgetItem *item = m_feedList->getTreeItemFromUrl(url);
  item->setData(0,Qt::DecorationRole, QVariant(QIcon(icon_path)));
}

void RSSImp::updateFeedInfos(const QString &url, const QString &display_name, uint nbUnread){
  qDebug() << Q_FUNC_INFO << display_name;
  QTreeWidgetItem *item = m_feedList->getTreeItemFromUrl(url);
  RssFeed *stream = (RssFeed*)m_feedList->getRSSItem(item);
  item->setText(0, display_name + QString::fromUtf8("  (") + QString::number(nbUnread, 10)+ QString(")"));
  if(!stream->isLoading())
    item->setData(0,Qt::DecorationRole, QVariant(QIcon(stream->icon())));
  // Update parent
  if(item->parent())
    updateItemInfos(item->parent());
  // Update Unread item
  updateItemInfos(m_feedList->stickyUnreadItem());
  // If the feed is selected, update the displayed news
  if(m_feedList->currentItem() == item ){
    refreshArticleList(item);
  } else {
    // Update unread items
    if(m_feedList->currentItem() == m_feedList->stickyUnreadItem()) {
      refreshArticleList(m_feedList->stickyUnreadItem());
    }
  }
}

void RSSImp::updateRefreshInterval(uint val) {
  m_rssManager->updateRefreshInterval(val);
}

RSSImp::RSSImp(QWidget *parent) : QWidget(parent) {
  setupUi(this);
  // Icons
  actionCopy_feed_URL->setIcon(IconProvider::instance()->getIcon("edit-copy"));
  actionDelete->setIcon(IconProvider::instance()->getIcon("edit-delete"));
  actionDownload_torrent->setIcon(IconProvider::instance()->getIcon("download"));
  actionManage_cookies->setIcon(IconProvider::instance()->getIcon("preferences-web-browser-cookies"));
  actionMark_items_read->setIcon(IconProvider::instance()->getIcon("mail-mark-read"));
  actionNew_folder->setIcon(IconProvider::instance()->getIcon("folder-new"));
  actionNew_subscription->setIcon(IconProvider::instance()->getIcon("list-add"));
  actionOpen_news_URL->setIcon(IconProvider::instance()->getIcon("application-x-mswinurl"));
  actionRename->setIcon(IconProvider::instance()->getIcon("edit-rename"));
  actionUpdate->setIcon(IconProvider::instance()->getIcon("view-refresh"));
  actionUpdate_all_feeds->setIcon(IconProvider::instance()->getIcon("view-refresh"));
  newFeedButton->setIcon(IconProvider::instance()->getIcon("list-add"));
  markReadButton->setIcon(IconProvider::instance()->getIcon("mail-mark-read"));
  updateAllButton->setIcon(IconProvider::instance()->getIcon("view-refresh"));
  rssDownloaderBtn->setIcon(IconProvider::instance()->getIcon("download"));
  settingsButton->setIcon(IconProvider::instance()->getIcon("preferences-system"));

  m_rssManager = RssManager::instance();

  m_feedList = new FeedListWidget(splitter_h, m_rssManager);
  splitter_h->insertWidget(0, m_feedList);
  listArticles->setSelectionBehavior(QAbstractItemView::SelectItems);
  listArticles->setSelectionMode(QAbstractItemView::SingleSelection);

  m_rssManager->loadStreamList();
  fillFeedsList();
  refreshArticleList(m_feedList->currentItem());

  loadFoldersOpenState();
  connect(m_rssManager, SIGNAL(feedInfosChanged(QString, QString, unsigned int)), this, SLOT(updateFeedInfos(QString, QString, unsigned int)));
  connect(m_rssManager, SIGNAL(feedIconChanged(QString, QString)), this, SLOT(updateFeedIcon(QString, QString)));

  connect(m_feedList, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(displayRSSListMenu(const QPoint&)));
  connect(listArticles, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(displayItemsListMenu(const QPoint&)));

  // Feeds list actions
  connect(actionDelete, SIGNAL(triggered()), this, SLOT(deleteSelectedItems()));
  connect(actionRename, SIGNAL(triggered()), this, SLOT(renameFiles()));
  connect(actionUpdate, SIGNAL(triggered()), this, SLOT(refreshSelectedItems()));
  connect(actionNew_folder, SIGNAL(triggered()), this, SLOT(askNewFolder()));
  connect(actionNew_subscription, SIGNAL(triggered()), this, SLOT(on_newFeedButton_clicked()));
  connect(actionUpdate_all_feeds, SIGNAL(triggered()), this, SLOT(on_updateAllButton_clicked()));
  connect(actionCopy_feed_URL, SIGNAL(triggered()), this, SLOT(copySelectedFeedsURL()));
  connect(actionMark_items_read, SIGNAL(triggered()), this, SLOT(on_markReadButton_clicked()));
  // News list actions
  connect(actionOpen_news_URL, SIGNAL(triggered()), this, SLOT(openNewsUrl()));
  connect(actionDownload_torrent, SIGNAL(triggered()), this, SLOT(downloadTorrent()));

  connect(m_feedList, SIGNAL(currentItemChanged(QTreeWidgetItem*,QTreeWidgetItem*)), this, SLOT(refreshArticleList(QTreeWidgetItem*)));
  connect(m_feedList, SIGNAL(foldersAltered(QList<QTreeWidgetItem*>)), this, SLOT(updateItemsInfos(QList<QTreeWidgetItem*>)));
  connect(m_feedList, SIGNAL(overwriteAttempt(QString)), this, SLOT(displayOverwriteError(QString)));

  connect(listArticles, SIGNAL(itemSelectionChanged()), this, SLOT(refreshTextBrowser()));
  connect(listArticles, SIGNAL(itemDoubleClicked(QListWidgetItem *)), this, SLOT(downloadTorrent()));

  // Refresh all feeds
  m_rssManager->refresh();
  // Restore sliders position
  restoreSlidersPosition();
  // Bind saveSliders slots
  connect(splitter_v, SIGNAL(splitterMoved(int, int)), this, SLOT(saveSlidersPosition()));
  connect(splitter_h, SIGNAL(splitterMoved(int, int)), this, SLOT(saveSlidersPosition()));

  qDebug("RSSImp constructed");
}

RSSImp::~RSSImp(){
  qDebug("Deleting RSSImp...");
  saveFoldersOpenState();
  delete m_feedList;
  RssManager::drop();
  qDebug("RSSImp deleted");
}


void RSSImp::on_settingsButton_clicked() {
  RssSettingsDlg dlg(this);
  if(dlg.exec())
    updateRefreshInterval(RssSettings().getRSSRefreshInterval());
}

void RSSImp::on_rssDownloaderBtn_clicked()
{
  AutomatedRssDownloader dlg(this);
  dlg.exec();
  if(dlg.isRssDownloaderEnabled())
    on_updateAllButton_clicked();
}
