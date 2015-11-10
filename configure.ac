dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT(glbindify, [0.4])

AC_CANONICAL_SYSTEM
AM_INIT_AUTOMAKE([foreign])

AC_CHECK_PROG(GPERF,gperf,yes,no)

AC_PROG_CXX

if [test x$GPERF == xyes]; then
	AC_DEFINE([HAVE_GPERF], 1, [Set if gperf is available])
fi

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
