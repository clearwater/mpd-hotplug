AC_INIT([mpdhotplug], [1.0], [guy@clearwater.com.au])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AC_PROG_CC
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([
 Makefile
 src/Makefile
])

dnl Check for libmpdclient 2.0                                                  
PKG_CHECK_MODULES([LIBMPDCLIENT], [libmpdclient >= 2.0],,
        [AC_MSG_ERROR([libmpdclient2 is required])])

AC_SUBST(AM_CFLAGS)

AC_OUTPUT
