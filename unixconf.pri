# COMPILATION SPECIFIC
QT += dbus
QMAKE_LFLAGS_APP += -rdynamic
CONFIG += link_pkgconfig
PKGCONFIG += libtorrent-rasterbar

libnotify {
  DEFINES += WITH_LIBNOTIFY
}

# Man page
nox {
    man.files = ../doc/qbittorrent-nox.1
} else {
    man.files = ../doc/qbittorrent.1
}
man.path = $$PREFIX/share/man/man1/
INSTALLS += man

# Menu Icon
!nox {
      menuicon.files = Icons/qBittorrent.desktop
      menuicon.path = $$PREFIX/share/applications/
      INSTALLS += menuicon
      icon16.files = menuicons/16x16/apps/qbittorrent.png
      icon16.path = $$PREFIX/share/icons/hicolor/16x16/apps/
      icon22.files = menuicons/22x22/apps/qbittorrent.png
      icon22.path = $$PREFIX/share/icons/hicolor/22x22/apps/
      icon24.files = menuicons/24x24/apps/qbittorrent.png
      icon24.path = $$PREFIX/share/icons/hicolor/24x24/apps/
      icon32.files = menuicons/32x32/apps/qbittorrent.png
      icon32.path = $$PREFIX/share/icons/hicolor/32x32/apps/
      icon36.files = menuicons/36x36/apps/qbittorrent.png
      icon36.path = $$PREFIX/share/icons/hicolor/36x36/apps/
      icon48.files = menuicons/48x48/apps/qbittorrent.png
      icon48.path = $$PREFIX/share/icons/hicolor/48x48/apps/
      icon64.files = menuicons/64x64/apps/qbittorrent.png
      icon64.path = $$PREFIX/share/icons/hicolor/64x64/apps/
      icon72.files = menuicons/72x72/apps/qbittorrent.png
      icon72.path = $$PREFIX/share/icons/hicolor/72x72/apps/
      icon96.files = menuicons/96x96/apps/qbittorrent.png
      icon96.path = $$PREFIX/share/icons/hicolor/96x96/apps/
      icon128.files = menuicons/128x128/apps/qbittorrent.png
      icon128.path = $$PREFIX/share/icons/hicolor/128x128/apps/
      icon192.files = menuicons/192x192/apps/qbittorrent.png
      icon192.path = $$PREFIX/share/icons/hicolor/192x192/apps/

      INSTALLS += icon16 \
          icon22 \
          icon24 \
          icon32 \
          icon36 \
          icon48 \
          icon64 \
          icon72 \
          icon96 \
          icon128 \
          icon192
}

# INSTALL
target.path = $$PREFIX/bin/
INSTALLS += target

!nox {
  # DEFINE added by configure
  contains(DEFINES, WITH_GEOIP_EMBEDDED) {
    message("You chose to embed GeoIP database in qBittorrent executable.")
  }
}
