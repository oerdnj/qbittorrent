PROJECT_NAME = qbittorrent
PROJECT_VERSION = 2.8.2

os2 {
    DEFINES += VERSION=\'\"v$${PROJECT_VERSION}\"\'
} else {
    DEFINES += VERSION=\\\"v$${PROJECT_VERSION}\\\"
}

DEFINES += VERSION_MAJOR=2
DEFINES += VERSION_MINOR=8
DEFINES += VERSION_BUGFIX=2
