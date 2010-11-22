dnl Copyright (C) 2008,2010 Nokia Corporation
dnl Contact: Kai Vehmanen <kai.vehmanen@nokia.com>
dnl Licensed under LGPL. See file COPYING.

dnl 
dnl ref: http://www.gnu.org/software/autoconf/manual/autoconf.info.gz
dnl
AC_PREREQ(2.57)

dnl information on the package
dnl ---------------------------

AC_INIT([libcmtspeechdata], [2.0.8+dev])
AC_CONFIG_SRCDIR([cmtspeech.h])
AM_INIT_AUTOMAKE([foreign])
AM_MAINTAINER_MODE

VERSION_MAJOR=`echo $VERSION |cut -f1 -d.`
VERSION_LIBRARY_API=`echo $VERSION |cut -f2 -d.`
VERSION_MINOR=`echo $VERSION |cut -f3 -d.`
VERSION_PROTOCOL=2
AC_SUBST(VERSION_MAJOR)
AC_SUBST(VERSION_LIBRARY_API)
AC_SUBST(VERSION_MINOR)
AC_SUBST(VERSION_PROTOCOL)

dnl checks for programs
dnl -------------------
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_CPP
AC_CHECK_TOOL(AR, ar, ar)
AC_CHECK_TOOL(LD, ld, ld)
AC_PROG_LIBTOOL
AM_PROG_CC_C_O

dnl check for debugging options
dnl -------------------
AC_MSG_CHECKING([whether to enable asserts and other debug facilities])
AC_ARG_ENABLE([extra-debug], 
              AC_HELP_STRING([--disable-extra-debug], [Disable asserts and debug traces]),
              AC_MSG_RESULT([no])
               AC_DEFINE(NDEBUG, 1, [Define to 1 if debug facilities should be disabled]),
              AC_MSG_RESULT([yes]))

dnl check for doxygen
dnl -----------------

AC_ARG_ENABLE(docs, AC_HELP_STRING([--enable-docs], [Build DOXYGEN documentation (requires Doxygen)]),
                    enable_docs=$enableval,enable_docs=auto)

AC_PATH_PROG(DOXYGEN, doxygen, no)
AC_MSG_CHECKING([whether to build Doxygen documentation])

if test x$DOXYGEN = xno ; then
    have_doxygen=no
else
    have_doxygen=yes
fi
if test x$enable_docs = xauto ; then
    if test x$have_doxygen = xno ; then
        enable_docs=no
    else
        enable_docs=yes
    fi
fi
if test x$enable_docs = xyes; then
    if test x$have_doxygen = xno; then
        AC_MSG_ERROR([Building Doxygen docs explicitly required, but Doxygen not found])
    else
    AC_MSG_RESULT(yes)
    fi
else
    AC_MSG_RESULT(no)
fi

AM_CONDITIONAL(DOXYGEN_DOCS_ENABLED, test x$enable_docs = xyes)
AC_SUBST(DOXYGEN)

dnl checks for libraries
dnl --------------------

PKG_CHECK_MODULES([CHECK], [check >= 0.9.4])

PKG_CHECK_MODULES([DBUS], [dbus-1 >= 1.0])
AC_SUBST(DBUS_CFLAGS)
AC_SUBST(DBUS_LIBS)

dnl note: this affects the main library build (the dummy
dnl       library is built always under dummy-backend/ )
default_backend="nokiamodem"
AC_ARG_WITH([backend], 
  AC_HELP_STRING([--with-backend=BACKEND],
    [select which backend to use]),
    [
      case "$withval" in
        nokiamodem)
          backend="nokiamodem"
	  ;;
        null)
          backend="null"
	  ;;
	*)
	  AC_MSG_ERROR([unsupported backend: $withval])
	  ;;
      esac
    ])
if test "x$backend" = "x" ; then
  backend="$default_backend"
fi
AM_CONDITIONAL(USE_BACKEND_NOKIAMODEM, [test "x$backend" = "xnokiamodem"])
AM_CONDITIONAL(USE_BACKEND_NULL, [test "x$backend" = "xnull"])
AC_MSG_RESULT([selecting default backend: $backend])

dnl checks for types
dnl ----------------

dnl checks for structures
dnl ---------------------
AC_CHECK_HEADERS(stdio.h)
AC_CHECK_HEADERS(sched.h sys/stat.h sys/mman.h)
AC_CHECK_HEADERS(mcheck.h syslog.h)
AC_CHECK_HEADERS(linux/cs-protocol.h)

dnl checks for typedefs, structures, and compiler characteristics.
dnl --------------------------------------------------------------
AC_C_CONST

dnl output
dnl ------

AC_CONFIG_FILES([
Makefile
cmtspeech_config.h
libcmtspeechdata.pc
doc/Makefile
doc/doxygen.cfg
dummy-backend/Makefile
])

AC_OUTPUT
