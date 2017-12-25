/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2017  Vladimir Golovnev <glassez@yandex.ru>
 * Copyright (C) 2010  Christophe Dumez <chris@qbittorrent.org>
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

#include "automatedrssdownloader.h"

#include <QCursor>
#include <QDebug>
#include <QFileDialog>
#include <QMenu>
#include <QMessageBox>
#include <QPair>
#include <QRegularExpression>
#include <QSet>
#include <QShortcut>
#include <QSignalBlocker>
#include <QString>

#include "base/bittorrent/session.h"
#include "base/preferences.h"
#include "base/rss/rss_article.h"
#include "base/rss/rss_autodownloader.h"
#include "base/rss/rss_feed.h"
#include "base/rss/rss_folder.h"
#include "base/rss/rss_session.h"
#include "base/utils/fs.h"
#include "base/utils/string.h"
#include "guiiconprovider.h"
#include "autoexpandabledialog.h"
#include "ui_automatedrssdownloader.h"

const QString EXT_JSON {QStringLiteral(".json")};
const QString EXT_LEGACY {QStringLiteral(".rssrules")};

AutomatedRssDownloader::AutomatedRssDownloader(QWidget *parent)
    : QDialog(parent)
    , m_formatFilterJSON(QString("%1 (*%2)").arg(tr("Rules")).arg(EXT_JSON))
    , m_formatFilterLegacy(QString("%1 (*%2)").arg(tr("Rules (legacy)")).arg(EXT_LEGACY))
    , m_ui(new Ui::AutomatedRssDownloader)
    , m_currentRuleItem(nullptr)
{
    m_ui->setupUi(this);
    // Icons
    m_ui->removeRuleBtn->setIcon(GuiIconProvider::instance()->getIcon("list-remove"));
    m_ui->addRuleBtn->setIcon(GuiIconProvider::instance()->getIcon("list-add"));

    // Ui Settings
    m_ui->listRules->setSortingEnabled(true);
    m_ui->listRules->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_ui->treeMatchingArticles->setSortingEnabled(true);
    m_ui->treeMatchingArticles->sortByColumn(0, Qt::AscendingOrder);
    m_ui->hsplitter->setCollapsible(0, false);
    m_ui->hsplitter->setCollapsible(1, false);
    m_ui->hsplitter->setCollapsible(2, true); // Only the preview list is collapsible

    connect(m_ui->checkRegex, &QAbstractButton::toggled, this, &AutomatedRssDownloader::updateFieldsToolTips);
    connect(m_ui->listRules, &QWidget::customContextMenuRequested, this, &AutomatedRssDownloader::displayRulesListMenu);

    m_episodeRegex = new QRegularExpression("^(^\\d{1,4}x(\\d{1,4}(-(\\d{1,4})?)?;){1,}){1,1}"
                                            , QRegularExpression::CaseInsensitiveOption);
    QString tip = "<p>" + tr("Matches articles based on episode filter.") + "</p><p><b>" + tr("Example: ")
                  + "1x2;8-15;5;30-;</b>" + tr(" will match 2, 5, 8 through 15, 30 and onward episodes of season one", "example X will match") + "</p>";
    tip += "<p>" + tr("Episode filter rules: ") + "</p><ul><li>" + tr("Season number is a mandatory non-zero value") + "</li>"
           + "<li>" + tr("Episode number is a mandatory positive value") + "</li>"
           + "<li>" + tr("Filter must end with semicolon") + "</li>"
           + "<li>" + tr("Three range types for episodes are supported: ") + "</li>" + "<li><ul>"
           + "<li>" + tr("Single number: <b>1x25;</b> matches episode 25 of season one") + "</li>"
           + "<li>" + tr("Normal range: <b>1x25-40;</b> matches episodes 25 through 40 of season one") + "</li>"
           + "<li>" + tr("Infinite range: <b>1x25-;</b> matches episodes 25 and upward of season one, and all episodes of later seasons") + "</li>" + "</ul></li></ul>";
    m_ui->lineEFilter->setToolTip(tip);

    initCategoryCombobox();
    loadSettings();

    connect(RSS::AutoDownloader::instance(), &RSS::AutoDownloader::ruleAdded, this, &AutomatedRssDownloader::handleRuleAdded);
    connect(RSS::AutoDownloader::instance(), &RSS::AutoDownloader::ruleRenamed, this, &AutomatedRssDownloader::handleRuleRenamed);
    connect(RSS::AutoDownloader::instance(), &RSS::AutoDownloader::ruleChanged, this, &AutomatedRssDownloader::handleRuleChanged);
    connect(RSS::AutoDownloader::instance(), &RSS::AutoDownloader::ruleAboutToBeRemoved, this, &AutomatedRssDownloader::handleRuleAboutToBeRemoved);

    // Update matching articles when necessary
    connect(m_ui->lineContains, &QLineEdit::textEdited, this, &AutomatedRssDownloader::handleRuleDefinitionChanged);
    connect(m_ui->lineContains, &QLineEdit::textEdited, this, &AutomatedRssDownloader::updateMustLineValidity);
    connect(m_ui->lineNotContains, &QLineEdit::textEdited, this, &AutomatedRssDownloader::handleRuleDefinitionChanged);
    connect(m_ui->lineNotContains, &QLineEdit::textEdited, this, &AutomatedRssDownloader::updateMustNotLineValidity);
    connect(m_ui->lineEFilter, &QLineEdit::textEdited, this, &AutomatedRssDownloader::handleRuleDefinitionChanged);
    connect(m_ui->lineEFilter, &QLineEdit::textEdited, this, &AutomatedRssDownloader::updateEpisodeFilterValidity);
    connect(m_ui->checkRegex, &QCheckBox::stateChanged, this, &AutomatedRssDownloader::handleRuleDefinitionChanged);
    connect(m_ui->checkRegex, &QCheckBox::stateChanged, this, &AutomatedRssDownloader::updateMustLineValidity);
    connect(m_ui->checkRegex, &QCheckBox::stateChanged, this, &AutomatedRssDownloader::updateMustNotLineValidity);

    connect(m_ui->listFeeds, &QListWidget::itemChanged, this, &AutomatedRssDownloader::handleFeedCheckStateChange);

    connect(m_ui->listRules, &QListWidget::itemSelectionChanged, this, &AutomatedRssDownloader::updateRuleDefinitionBox);
    connect(m_ui->listRules, &QListWidget::itemChanged, this, &AutomatedRssDownloader::handleRuleCheckStateChange);

    m_editHotkey = new QShortcut(Qt::Key_F2, m_ui->listRules, 0, 0, Qt::WidgetShortcut);
    connect(m_editHotkey, &QShortcut::activated, this, &AutomatedRssDownloader::renameSelectedRule);
    connect(m_ui->listRules, &QAbstractItemView::doubleClicked, this, &AutomatedRssDownloader::renameSelectedRule);

    m_deleteHotkey = new QShortcut(QKeySequence::Delete, m_ui->listRules, 0, 0, Qt::WidgetShortcut);
    connect(m_deleteHotkey, &QShortcut::activated, this, &AutomatedRssDownloader::on_removeRuleBtn_clicked);

    loadFeedList();

    m_ui->listRules->blockSignals(true);
    foreach (const RSS::AutoDownloadRule &rule, RSS::AutoDownloader::instance()->rules())
        createRuleItem(rule);
    m_ui->listRules->blockSignals(false);

    updateRuleDefinitionBox();

    if (RSS::AutoDownloader::instance()->isProcessingEnabled())
        m_ui->labelWarn->hide();
    connect(RSS::AutoDownloader::instance(), &RSS::AutoDownloader::processingStateChanged
            , this, &AutomatedRssDownloader::handleProcessingStateChanged);
}

AutomatedRssDownloader::~AutomatedRssDownloader()
{
    // Save current item on exit
    saveEditedRule();
    saveSettings();

    delete m_editHotkey;
    delete m_deleteHotkey;
    delete m_ui;
    delete m_episodeRegex;
}

void AutomatedRssDownloader::loadSettings()
{
    const Preferences *const pref = Preferences::instance();
    resize(pref->getRssGeometrySize(this->size()));
    m_ui->hsplitter->restoreState(pref->getRssHSplitterSizes());
}

void AutomatedRssDownloader::saveSettings()
{
    Preferences *const pref = Preferences::instance();
    pref->setRssGeometrySize(this->size());
    pref->setRssHSplitterSizes(m_ui->hsplitter->saveState());
}

void AutomatedRssDownloader::createRuleItem(const RSS::AutoDownloadRule &rule)
{
    QListWidgetItem *item = new QListWidgetItem(rule.name(), m_ui->listRules);
    m_itemsByRuleName.insert(rule.name(), item);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(rule.isEnabled() ? Qt::Checked : Qt::Unchecked);
}

void AutomatedRssDownloader::loadFeedList()
{
    const QSignalBlocker feedListSignalBlocker(m_ui->listFeeds);

    foreach (auto feed, RSS::Session::instance()->feeds()) {
        QListWidgetItem *item = new QListWidgetItem(feed->name(), m_ui->listFeeds);
        item->setData(Qt::UserRole, feed->url());
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsTristate);
    }

    updateFeedList();
}

void AutomatedRssDownloader::updateFeedList()
{
    const QSignalBlocker feedListSignalBlocker(m_ui->listFeeds);

    QList<QListWidgetItem *> selection;

    if (m_currentRuleItem)
        selection << m_currentRuleItem;
    else
        selection = m_ui->listRules->selectedItems();

    bool enable = !selection.isEmpty();

    for (int i = 0; i < m_ui->listFeeds->count(); ++i) {
        QListWidgetItem *item = m_ui->listFeeds->item(i);
        const QString feedURL = item->data(Qt::UserRole).toString();
        item->setHidden(!enable);

        bool allEnabled = true;
        bool anyEnabled = false;

        foreach (const QListWidgetItem *ruleItem, selection) {
            auto rule = RSS::AutoDownloader::instance()->ruleByName(ruleItem->text());
            if (rule.feedURLs().contains(feedURL))
                anyEnabled = true;
            else
                allEnabled = false;
        }

        if (anyEnabled && allEnabled)
            item->setCheckState(Qt::Checked);
        else if (anyEnabled)
            item->setCheckState(Qt::PartiallyChecked);
        else
            item->setCheckState(Qt::Unchecked);
    }

    m_ui->listFeeds->sortItems();
    m_ui->lblListFeeds->setEnabled(enable);
    m_ui->listFeeds->setEnabled(enable);
}

void AutomatedRssDownloader::updateRuleDefinitionBox()
{
    const QList<QListWidgetItem *> selection = m_ui->listRules->selectedItems();
    QListWidgetItem *currentRuleItem = ((selection.count() == 1) ? selection.first() : nullptr);
    if (m_currentRuleItem != currentRuleItem) {
        saveEditedRule(); // Save previous rule first
        m_currentRuleItem = currentRuleItem;
        //m_ui->listRules->setCurrentItem(m_currentRuleItem);
    }

    // Update rule definition box
    if (m_currentRuleItem) {
        m_currentRule = RSS::AutoDownloader::instance()->ruleByName(m_currentRuleItem->text());

        m_ui->lineContains->setText(m_currentRule.mustContain());
        m_ui->lineNotContains->setText(m_currentRule.mustNotContain());
        if (!m_currentRule.episodeFilter().isEmpty())
            m_ui->lineEFilter->setText(m_currentRule.episodeFilter());
        else
            m_ui->lineEFilter->clear();
        m_ui->saveDiffDir_check->setChecked(!m_currentRule.savePath().isEmpty());
        m_ui->lineSavePath->setText(Utils::Fs::toNativePath(m_currentRule.savePath()));
        m_ui->checkRegex->blockSignals(true);
        m_ui->checkRegex->setChecked(m_currentRule.useRegex());
        m_ui->checkRegex->blockSignals(false);
        m_ui->comboCategory->setCurrentIndex(m_ui->comboCategory->findText(m_currentRule.assignedCategory()));
        if (m_currentRule.assignedCategory().isEmpty())
            m_ui->comboCategory->clearEditText();
        int index = 0;
        if (m_currentRule.addPaused() == TriStateBool::True)
            index = 1;
        else if (m_currentRule.addPaused() == TriStateBool::False)
            index = 2;
        m_ui->comboAddPaused->setCurrentIndex(index);
        m_ui->spinIgnorePeriod->setValue(m_currentRule.ignoreDays());
        QDateTime dateTime = m_currentRule.lastMatch();
        QString lMatch;
        if (dateTime.isValid())
            lMatch = tr("Last Match: %1 days ago").arg(dateTime.daysTo(QDateTime::currentDateTime()));
        else
            lMatch = tr("Last Match: Unknown");
        m_ui->lblLastMatch->setText(lMatch);
        updateMustLineValidity();
        updateMustNotLineValidity();
        updateEpisodeFilterValidity();

        updateFieldsToolTips(m_ui->checkRegex->isChecked());
        m_ui->ruleDefBox->setEnabled(true);
    }
    else {
        m_currentRule = RSS::AutoDownloadRule();
        clearRuleDefinitionBox();
        m_ui->ruleDefBox->setEnabled(false);
    }

    updateFeedList();
    updateMatchingArticles();
}

void AutomatedRssDownloader::clearRuleDefinitionBox()
{
    m_ui->lineContains->clear();
    m_ui->lineNotContains->clear();
    m_ui->lineEFilter->clear();
    m_ui->saveDiffDir_check->setChecked(false);
    m_ui->lineSavePath->clear();
    m_ui->comboCategory->clearEditText();
    m_ui->comboCategory->setCurrentIndex(-1);
    m_ui->checkRegex->setChecked(false);
    m_ui->spinIgnorePeriod->setValue(0);
    m_ui->comboAddPaused->clearEditText();
    m_ui->comboAddPaused->setCurrentIndex(-1);
    updateFieldsToolTips(m_ui->checkRegex->isChecked());
    updateMustLineValidity();
    updateMustNotLineValidity();
    updateEpisodeFilterValidity();
}

void AutomatedRssDownloader::initCategoryCombobox()
{
    // Load torrent categories
    QStringList categories = BitTorrent::Session::instance()->categories().keys();
    std::sort(categories.begin(), categories.end(), Utils::String::naturalLessThan<Qt::CaseInsensitive>);
    m_ui->comboCategory->addItem("");
    m_ui->comboCategory->addItems(categories);
}

void AutomatedRssDownloader::updateEditedRule()
{
    if (!m_currentRuleItem || !m_ui->ruleDefBox->isEnabled()) return;

    m_currentRule.setEnabled(m_currentRuleItem->checkState() != Qt::Unchecked);
    m_currentRule.setUseRegex(m_ui->checkRegex->isChecked());
    m_currentRule.setMustContain(m_ui->lineContains->text());
    m_currentRule.setMustNotContain(m_ui->lineNotContains->text());
    m_currentRule.setEpisodeFilter(m_ui->lineEFilter->text());
    m_currentRule.setSavePath(m_ui->saveDiffDir_check->isChecked() ? m_ui->lineSavePath->text() : "");
    m_currentRule.setCategory(m_ui->comboCategory->currentText());
    TriStateBool addPaused; // Undefined by default
    if (m_ui->comboAddPaused->currentIndex() == 1)
        addPaused = TriStateBool::True;
    else if (m_ui->comboAddPaused->currentIndex() == 2)
        addPaused = TriStateBool::False;
    m_currentRule.setAddPaused(addPaused);
    m_currentRule.setIgnoreDays(m_ui->spinIgnorePeriod->value());
}

void AutomatedRssDownloader::saveEditedRule()
{
    if (!m_currentRuleItem || !m_ui->ruleDefBox->isEnabled()) return;

    updateEditedRule();
    RSS::AutoDownloader::instance()->insertRule(m_currentRule);
}

void AutomatedRssDownloader::on_addRuleBtn_clicked()
{
//    saveEditedRule();

    // Ask for a rule name
    const QString ruleName = AutoExpandableDialog::getText(
                this, tr("New rule name"), tr("Please type the name of the new download rule."));
    if (ruleName.isEmpty()) return;

    // Check if this rule name already exists
    if (RSS::AutoDownloader::instance()->hasRule(ruleName)) {
        QMessageBox::warning(this, tr("Rule name conflict")
                             , tr("A rule with this name already exists, please choose another name."));
        return;
    }

    RSS::AutoDownloader::instance()->insertRule(RSS::AutoDownloadRule(ruleName));
}

void AutomatedRssDownloader::on_removeRuleBtn_clicked()
{
    const QList<QListWidgetItem *> selection = m_ui->listRules->selectedItems();
    if (selection.isEmpty()) return;

    // Ask for confirmation
    const QString confirmText = ((selection.count() == 1)
                                 ? tr("Are you sure you want to remove the download rule named '%1'?")
                                   .arg(selection.first()->text())
                                 : tr("Are you sure you want to remove the selected download rules?"));
    if (QMessageBox::question(this, tr("Rule deletion confirmation"), confirmText, QMessageBox::Yes, QMessageBox::No) != QMessageBox::Yes)
        return;

    foreach (QListWidgetItem *item, selection)
        RSS::AutoDownloader::instance()->removeRule(item->text());
}

void AutomatedRssDownloader::on_browseSP_clicked()
{
    QString savePath = QFileDialog::getExistingDirectory(this, tr("Destination directory"), QDir::homePath());
    if (!savePath.isEmpty())
        m_ui->lineSavePath->setText(Utils::Fs::toNativePath(savePath));
}

void AutomatedRssDownloader::on_exportBtn_clicked()
{
    if (RSS::AutoDownloader::instance()->rules().isEmpty()) {
        QMessageBox::warning(this, tr("Invalid action")
                             , tr("The list is empty, there is nothing to export."));
        return;
    }

    QString selectedFilter {m_formatFilterJSON};
    QString path = QFileDialog::getSaveFileName(
                this, tr("Export RSS rules"), QDir::homePath()
                , QString("%1;;%2").arg(m_formatFilterJSON).arg(m_formatFilterLegacy), &selectedFilter);
    if (path.isEmpty()) return;

    const RSS::AutoDownloader::RulesFileFormat format {
        (selectedFilter == m_formatFilterJSON)
                ? RSS::AutoDownloader::RulesFileFormat::JSON
                : RSS::AutoDownloader::RulesFileFormat::Legacy
    };

    if (format == RSS::AutoDownloader::RulesFileFormat::JSON) {
        if (!path.endsWith(EXT_JSON, Qt::CaseInsensitive))
            path += EXT_JSON;
    }
    else {
        if (!path.endsWith(EXT_LEGACY, Qt::CaseInsensitive))
            path += EXT_LEGACY;
    }

    QFile file {path};
    if (!file.open(QFile::WriteOnly)
            || (file.write(RSS::AutoDownloader::instance()->exportRules(format)) == -1)) {
        QMessageBox::critical(
                    this, tr("I/O Error")
                    , tr("Failed to create the destination file. Reason: %1").arg(file.errorString()));
    }
}

void AutomatedRssDownloader::on_importBtn_clicked()
{
    QString selectedFilter {m_formatFilterJSON};
    QString path = QFileDialog::getOpenFileName(
                this, tr("Import RSS rules"), QDir::homePath()
                , QString("%1;;%2").arg(m_formatFilterJSON).arg(m_formatFilterLegacy), &selectedFilter);
    if (path.isEmpty() || !QFile::exists(path))
        return;

    QFile file {path};
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(
                    this, tr("I/O Error")
                    , tr("Failed to open the file. Reason: %1").arg(file.errorString()));
        return;
    }

    const RSS::AutoDownloader::RulesFileFormat format {
        (selectedFilter == m_formatFilterJSON)
                ? RSS::AutoDownloader::RulesFileFormat::JSON
                : RSS::AutoDownloader::RulesFileFormat::Legacy
    };

    try {
        RSS::AutoDownloader::instance()->importRules(file.readAll(),format);
    }
    catch (const RSS::ParsingError &error) {
        QMessageBox::critical(
                    this, tr("Import Error")
                    , tr("Failed to import the selected rules file. Reason: %1").arg(error.message()));
    }
}

void AutomatedRssDownloader::displayRulesListMenu()
{
    QMenu menu;
    QAction *addAct = menu.addAction(GuiIconProvider::instance()->getIcon("list-add"), tr("Add new rule..."));
    QAction *delAct = nullptr;
    QAction *renameAct = nullptr;
    const QList<QListWidgetItem *> selection = m_ui->listRules->selectedItems();
    if (!selection.isEmpty()) {
        if (selection.count() == 1) {
            delAct = menu.addAction(GuiIconProvider::instance()->getIcon("list-remove"), tr("Delete rule"));
            menu.addSeparator();
            renameAct = menu.addAction(GuiIconProvider::instance()->getIcon("edit-rename"), tr("Rename rule..."));
        }
        else {
            delAct = menu.addAction(GuiIconProvider::instance()->getIcon("list-remove"), tr("Delete selected rules"));
        }
    }

    QAction *act = menu.exec(QCursor::pos());
    if (!act) return;

    if (act == addAct)
        on_addRuleBtn_clicked();
    else if (act == delAct)
        on_removeRuleBtn_clicked();
    else if (act == renameAct)
        renameSelectedRule();
}

void AutomatedRssDownloader::renameSelectedRule()
{
    const QList<QListWidgetItem *> selection = m_ui->listRules->selectedItems();
    if (selection.isEmpty()) return;

    QListWidgetItem *item = selection.first();
    forever {
        QString newName = AutoExpandableDialog::getText(
                    this, tr("Rule renaming"), tr("Please type the new rule name")
                    , QLineEdit::Normal, item->text());
        newName = newName.trimmed();
        if (newName.isEmpty()) return;

        if (RSS::AutoDownloader::instance()->hasRule(newName)) {
            QMessageBox::warning(this, tr("Rule name conflict")
                                 , tr("A rule with this name already exists, please choose another name."));
        }
        else {
            // Rename the rule
            RSS::AutoDownloader::instance()->renameRule(item->text(), newName);
            return;
        }
    }
}

void AutomatedRssDownloader::handleRuleCheckStateChange(QListWidgetItem *ruleItem)
{
    m_ui->listRules->setCurrentItem(ruleItem);
}

void AutomatedRssDownloader::handleFeedCheckStateChange(QListWidgetItem *feedItem)
{
    const QString feedURL = feedItem->data(Qt::UserRole).toString();
    foreach (QListWidgetItem *ruleItem, m_ui->listRules->selectedItems()) {
        RSS::AutoDownloadRule rule = (ruleItem == m_currentRuleItem
                                       ? m_currentRule
                                       : RSS::AutoDownloader::instance()->ruleByName(ruleItem->text()));
        QStringList affectedFeeds = rule.feedURLs();
        if ((feedItem->checkState() == Qt::Checked) && !affectedFeeds.contains(feedURL))
            affectedFeeds << feedURL;
        else if ((feedItem->checkState() == Qt::Unchecked) && affectedFeeds.contains(feedURL))
            affectedFeeds.removeOne(feedURL);

        rule.setFeedURLs(affectedFeeds);
        if (ruleItem != m_currentRuleItem)
            RSS::AutoDownloader::instance()->insertRule(rule);
        else
            m_currentRule = rule;
    }

    handleRuleDefinitionChanged();
}

void AutomatedRssDownloader::updateMatchingArticles()
{
    m_ui->treeMatchingArticles->clear();

    foreach (const QListWidgetItem *ruleItem, m_ui->listRules->selectedItems()) {
        RSS::AutoDownloadRule rule = (ruleItem == m_currentRuleItem
                                       ? m_currentRule
                                       : RSS::AutoDownloader::instance()->ruleByName(ruleItem->text()));
        foreach (const QString &feedURL, rule.feedURLs()) {
            auto feed = RSS::Session::instance()->feedByURL(feedURL);
            if (!feed) continue; // feed doesn't exists

            QStringList matchingArticles;
            foreach (auto article, feed->articles())
                if (rule.matches(article->title()))
                    matchingArticles << article->title();
            if (!matchingArticles.isEmpty())
                addFeedArticlesToTree(feed, matchingArticles);
        }
    }

    m_treeListEntries.clear();
}

void AutomatedRssDownloader::addFeedArticlesToTree(RSS::Feed *feed, const QStringList &articles)
{
    // Turn off sorting while inserting
    m_ui->treeMatchingArticles->setSortingEnabled(false);

    // Check if this feed is already in the tree
    QTreeWidgetItem *treeFeedItem = nullptr;
    for (int i = 0; i < m_ui->treeMatchingArticles->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_ui->treeMatchingArticles->topLevelItem(i);
        if (item->data(0, Qt::UserRole).toString() == feed->url()) {
            treeFeedItem = item;
            break;
        }
    }

    // If there is none, create it
    if (!treeFeedItem) {
        treeFeedItem = new QTreeWidgetItem(QStringList() << feed->name());
        treeFeedItem->setToolTip(0, feed->name());
        QFont f = treeFeedItem->font(0);
        f.setBold(true);
        treeFeedItem->setFont(0, f);
        treeFeedItem->setData(0, Qt::DecorationRole, GuiIconProvider::instance()->getIcon("inode-directory"));
        treeFeedItem->setData(0, Qt::UserRole, feed->url());
        m_ui->treeMatchingArticles->addTopLevelItem(treeFeedItem);
    }

    // Insert the articles
    foreach (const QString &article, articles) {
        QPair<QString, QString> key(feed->name(), article);

        if (!m_treeListEntries.contains(key)) {
            m_treeListEntries << key;
            QTreeWidgetItem *item = new QTreeWidgetItem(QStringList() << article);
            item->setToolTip(0, article);
            treeFeedItem->addChild(item);
        }
    }

    m_ui->treeMatchingArticles->expandItem(treeFeedItem);
    m_ui->treeMatchingArticles->sortItems(0, Qt::AscendingOrder);
    m_ui->treeMatchingArticles->setSortingEnabled(true);
}

void AutomatedRssDownloader::updateFieldsToolTips(bool regex)
{
    QString tip;
    if (regex) {
        tip = "<p>" + tr("Regex mode: use Perl-compatible regular expressions") + "</p>";
    }
    else {
        tip = "<p>" + tr("Wildcard mode: you can use") + "<ul>"
              + "<li>" + tr("? to match any single character") + "</li>"
              + "<li>" + tr("* to match zero or more of any characters") + "</li>"
              + "<li>" + tr("Whitespaces count as AND operators (all words, any order)") + "</li>"
              + "<li>" + tr("| is used as OR operator") + "</li></ul></p>"
              + "<p>" + tr("If word order is important use * instead of whitespace.") + "</p>";
    }

    // Whether regex or wildcard, warn about a potential gotcha for users.
    // Explanatory string broken over multiple lines for readability (and multiple
    // statements to prevent uncrustify indenting excessively.
    tip += "<p>";
    tip += tr("An expression with an empty %1 clause (e.g. %2)",
              "We talk about regex/wildcards in the RSS filters section here."
              " So a valid sentence would be: An expression with an empty | clause (e.g. expr|)"
              ).arg("<tt>|</tt>").arg("<tt>expr|</tt>");
    m_ui->lineContains->setToolTip(tip + tr(" will match all articles.") + "</p>");
    m_ui->lineNotContains->setToolTip(tip + tr(" will exclude all articles.") + "</p>");
}

void AutomatedRssDownloader::updateMustLineValidity()
{
    const QString text = m_ui->lineContains->text();
    bool isRegex = m_ui->checkRegex->isChecked();
    bool valid = true;
    QString error;

    if (!text.isEmpty()) {
        QStringList tokens;
        if (isRegex)
            tokens << text;
        else
            foreach (const QString &token, text.split("|"))
                tokens << Utils::String::wildcardToRegex(token);

        foreach (const QString &token, tokens) {
            QRegularExpression reg(token, QRegularExpression::CaseInsensitiveOption);
            if (!reg.isValid()) {
                if (isRegex)
                    error = tr("Position %1: %2").arg(reg.patternErrorOffset()).arg(reg.errorString());
                valid = false;
                break;
            }
        }
    }

    if (valid) {
        m_ui->lineContains->setStyleSheet("");
        m_ui->lbl_must_stat->setPixmap(QPixmap());
        m_ui->lbl_must_stat->setToolTip("");
    }
    else {
        m_ui->lineContains->setStyleSheet("QLineEdit { color: #ff0000; }");
        m_ui->lbl_must_stat->setPixmap(GuiIconProvider::instance()->getIcon("task-attention").pixmap(16, 16));
        m_ui->lbl_must_stat->setToolTip(error);
    }
}

void AutomatedRssDownloader::updateMustNotLineValidity()
{
    const QString text = m_ui->lineNotContains->text();
    bool isRegex = m_ui->checkRegex->isChecked();
    bool valid = true;
    QString error;

    if (!text.isEmpty()) {
        QStringList tokens;
        if (isRegex)
            tokens << text;
        else
            foreach (const QString &token, text.split("|"))
                tokens << Utils::String::wildcardToRegex(token);

        foreach (const QString &token, tokens) {
            QRegularExpression reg(token, QRegularExpression::CaseInsensitiveOption);
            if (!reg.isValid()) {
                if (isRegex)
                    error = tr("Position %1: %2").arg(reg.patternErrorOffset()).arg(reg.errorString());
                valid = false;
                break;
            }
        }
    }

    if (valid) {
        m_ui->lineNotContains->setStyleSheet("");
        m_ui->lbl_mustnot_stat->setPixmap(QPixmap());
        m_ui->lbl_mustnot_stat->setToolTip("");
    }
    else {
        m_ui->lineNotContains->setStyleSheet("QLineEdit { color: #ff0000; }");
        m_ui->lbl_mustnot_stat->setPixmap(GuiIconProvider::instance()->getIcon("task-attention").pixmap(16, 16));
        m_ui->lbl_mustnot_stat->setToolTip(error);
    }
}

void AutomatedRssDownloader::updateEpisodeFilterValidity()
{
    const QString text = m_ui->lineEFilter->text();
    bool valid = text.isEmpty() || m_episodeRegex->match(text).hasMatch();

    if (valid) {
        m_ui->lineEFilter->setStyleSheet("");
        m_ui->lbl_epfilter_stat->setPixmap(QPixmap());
    }
    else {
        m_ui->lineEFilter->setStyleSheet("QLineEdit { color: #ff0000; }");
        m_ui->lbl_epfilter_stat->setPixmap(GuiIconProvider::instance()->getIcon("task-attention").pixmap(16, 16));
    }
}

void AutomatedRssDownloader::handleRuleDefinitionChanged()
{
    updateEditedRule();
    updateMatchingArticles();
}

void AutomatedRssDownloader::handleRuleAdded(const QString &ruleName)
{
    createRuleItem(RSS::AutoDownloadRule(ruleName));
}

void AutomatedRssDownloader::handleRuleRenamed(const QString &ruleName, const QString &oldRuleName)
{
    auto item = m_itemsByRuleName.take(oldRuleName);
    m_itemsByRuleName.insert(ruleName, item);
    if (m_currentRule.name() == oldRuleName)
        m_currentRule.setName(ruleName);
    item->setText(ruleName);
}

void AutomatedRssDownloader::handleRuleChanged(const QString &ruleName)
{
    auto item = m_itemsByRuleName.value(ruleName);
    if (item != m_currentRuleItem)
        item->setCheckState(RSS::AutoDownloader::instance()->ruleByName(ruleName).isEnabled() ? Qt::Checked : Qt::Unchecked);
}

void AutomatedRssDownloader::handleRuleAboutToBeRemoved(const QString &ruleName)
{
    delete m_itemsByRuleName.take(ruleName);
}

void AutomatedRssDownloader::handleProcessingStateChanged(bool enabled)
{
    m_ui->labelWarn->setVisible(!enabled);
}
