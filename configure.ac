AC_INIT([mpdhotplug], [1.0], [guy@clearwater.com.au])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AC_PROG_CC
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([
 Makefile
 src/Makefile
])

AC_SUBST(AM_CFLAGS)

dnl ---------------------------------------------------------------------------
dnl Mandatory Libraries
dnl ---------------------------------------------------------------------------
PKG_CHECK_MODULES([GLIB], [glib-2.0 >= 2.12 gthread-2.0],,
		[AC_MSG_ERROR([GLib 2.12 is required])])

AC_OUTPUT
