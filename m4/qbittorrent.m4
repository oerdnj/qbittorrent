# Checking for pkg-config. If found, check for QtCore and query pkg-config
# for its exec-prefix variable.

# FIND_QT5()
# Sets the QT_QMAKE variable to the path of Qt5 qmake if found.
# --------------------------------------
AC_DEFUN([FIND_QT5],
[PKG_CHECK_EXISTS([Qt5Core >= 5.5.1],
                 [PKG_CHECK_VAR(QT_QMAKE,
                                [Qt5Core >= 5.5.1],
                                [host_bins])
                 ])

AS_IF([test -f "$QT_QMAKE/qmake"],
              [QT_QMAKE="$QT_QMAKE/qmake"],
              [AS_IF([test -f "$QT_QMAKE/qmake-qt5"],
                             [QT_QMAKE="$QT_QMAKE/qmake-qt5"],
                             [QT_QMAKE=""])
              ])

AC_MSG_CHECKING([for Qt5 qmake >= 5.5.1])
AS_IF([test "x$QT_QMAKE" != "x"],
      [AC_MSG_RESULT([$QT_QMAKE])],
      [AC_MSG_RESULT([not found])]
      )
])

# FIND_QTDBUS()
# Sets the HAVE_QTDBUS variable to true or false.
# --------------------------------------
AC_DEFUN([FIND_QTDBUS],
       [AC_MSG_CHECKING([for Qt5DBus >= 5.5.1])
       PKG_CHECK_EXISTS([Qt5DBus >= 5.5.1],
                        [AC_MSG_RESULT([found])
                         HAVE_QTDBUS=[true]],
                        [AC_MSG_RESULT([not found])
                         HAVE_QTDBUS=[false]])
])
