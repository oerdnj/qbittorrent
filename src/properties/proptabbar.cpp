/*
 * Bittorrent Client using Qt4 and libtorrent.
 * Copyright (C) 2010  Christophe Dumez
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

#include <QButtonGroup>
#include <QPushButton>
#include <QSpacerItem>
#include <QKeySequence>

#include "proptabbar.h"

#ifdef Q_WS_MAC
#define DEFAULT_BUTTON_CSS "QPushButton {border: 1px solid rgb(85, 81, 91);border-radius: 3px;padding: 2px; margin-left: 8px; margin-right: 8px;}"
#define SELECTED_BUTTON_CSS "QPushButton {border: 1px solid rgb(85, 81, 91);border-radius: 3px;padding: 2px;background-color: rgb(255, 208, 105); margin-left: 8px; margin-right: 8px;}"
#else
#define DEFAULT_BUTTON_CSS "QPushButton {border: 1px solid rgb(85, 81, 91);border-radius: 3px;padding: 2px; margin-left: 3px; margin-right: 3px;}"
#define SELECTED_BUTTON_CSS "QPushButton {border: 1px solid rgb(85, 81, 91);border-radius: 3px;padding: 2px;background-color: rgb(255, 208, 105); margin-left: 3px; margin-right: 3px;}"
#endif

const int BTN_ICON_SIZE = 16;

PropTabBar::PropTabBar(QWidget *parent) :
  QHBoxLayout(parent), m_currentIndex(-1)
{
  m_btnGroup = new QButtonGroup(this);
  setContentsMargins(5, 4, 5, 2);
  // General tab
  QPushButton *main_infos_button = new QPushButton(QIcon(":/Icons/oxygen/help-about.png"), tr("General"), parent);
  main_infos_button->setShortcut(QKeySequence(QString::fromUtf8("Alt+P")));
  main_infos_button->setStyleSheet(DEFAULT_BUTTON_CSS);
  main_infos_button->setIconSize(QSize(BTN_ICON_SIZE, BTN_ICON_SIZE));
  addWidget(main_infos_button);
  m_btnGroup->addButton(main_infos_button, MAIN_TAB);
  // Trackers tab
  QPushButton *trackers_button = new QPushButton(QIcon(":/Icons/oxygen/network-server.png"), tr("Trackers"), parent);
  trackers_button->setStyleSheet(DEFAULT_BUTTON_CSS);
  trackers_button->setIconSize(QSize(BTN_ICON_SIZE, BTN_ICON_SIZE));
  addWidget(trackers_button);
  m_btnGroup->addButton(trackers_button, TRACKERS_TAB);
  // Peers tab
  QPushButton *peers_button = new QPushButton(QIcon(":/Icons/oxygen/peer.png"), tr("Peers"), parent);
  peers_button->setStyleSheet(DEFAULT_BUTTON_CSS);
  peers_button->setIconSize(QSize(BTN_ICON_SIZE, BTN_ICON_SIZE));
  addWidget(peers_button);
  m_btnGroup->addButton(peers_button, PEERS_TAB);
  // URL seeds tab
  QPushButton *urlseeds_button = new QPushButton(QIcon(":/Icons/oxygen/urlseed.png"), tr("URL Seeds"), parent);
  urlseeds_button->setStyleSheet(DEFAULT_BUTTON_CSS);
  urlseeds_button->setIconSize(QSize(BTN_ICON_SIZE, BTN_ICON_SIZE));
  addWidget(urlseeds_button);
  m_btnGroup->addButton(urlseeds_button, URLSEEDS_TAB);
  // Files tab
  QPushButton *files_button = new QPushButton(QIcon(":/Icons/oxygen/folder.png"), tr("Files"), parent);
  files_button->setStyleSheet(DEFAULT_BUTTON_CSS);
  files_button->setIconSize(QSize(BTN_ICON_SIZE, BTN_ICON_SIZE));
  addWidget(files_button);
  m_btnGroup->addButton(files_button, FILES_TAB);
  // Spacer
  addItem(new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum));
  // SIGNAL/SLOT
  connect(m_btnGroup, SIGNAL(buttonClicked(int)), SLOT(setCurrentIndex(int)));
}

PropTabBar::~PropTabBar() {
  delete m_btnGroup;
}

int PropTabBar::currentIndex() const
{
  return m_currentIndex;
}

void PropTabBar::setCurrentIndex(int index)
{
  // If asked to hide or if the currently selected tab is clicked
  if(index < 0 || m_currentIndex == index) {
    if(m_currentIndex >= 0) {
      m_btnGroup->button(m_currentIndex)->setStyleSheet(DEFAULT_BUTTON_CSS);
      m_currentIndex = -1;
      emit visibilityToggled(false);
    }
    return;
  }
  // Unselect previous tab
  if(m_currentIndex >= 0) {
    m_btnGroup->button(m_currentIndex)->setStyleSheet(DEFAULT_BUTTON_CSS);
  } else {
    // Nothing was selected, show!
    emit visibilityToggled(true);
  }
  // Select the new button
  m_btnGroup->button(index)->setStyleSheet(SELECTED_BUTTON_CSS);
  m_currentIndex = index;
  // Emit the signal
  emit tabChanged(index);
}
