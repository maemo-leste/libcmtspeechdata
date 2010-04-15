# Copyright (C) 2008,2009,2010 Nokia Corporation
# Contact: Kai Vehmanen 
# Licensed under LGPL. See file COPYING.

EXTRA_DIST = 		dummy_cmt.c

INCLUDES =		-I${top_srcdir}

lib_LTLIBRARIES = 	libcmtspeechdata.la

libdir = ${exec_prefix}/lib/libcmtspeechdata-dummy

# libcmtspeechdata main library sources
# -------------------------------------

dummy_cmtspeechdata_pub_inc = \
			cmtspeech_dummy.h \
			dummy_common.h

dummy_cmtspeechdata_common_src = \
			${top_srcdir}/cmtspeech_msgs.c \
			${top_srcdir}/cmtspeech_backend_common.c \
			${top_srcdir}/sal_debug.c

#dummy_cmtspeechdata_pub_inc = \
#			${top_srcdir}/cmtspeech.h \
#			${top_srcdir}/cmtspeech_msgs.h

dummy_cmtspeechdata_pub_inc_nodist = \
			${top_builddir}/cmtspeech_config.h

libcmtspeechdata_la_SOURCES = \
			$(dummy_cmtspeechdata_pub_inc) \
			dummy_common.c \
			cmtspeech_dummy.c
nodist_libcmtspeechdata_la_SOURCES = \
			$(dummy_cmtspeechdata_common_src) \
			$(dummy_cmtspeechdata_pub_inc_nodist)
libcmtspeechdata_la_CFLAGS = -prefer-pic
libcmtspeechdata_la_LIBADD = -lrt
libcmtspeechdata_la_LDFLAGS =  -Wl,--version-script,$(top_srcdir)/libcmtspeechdata.ver
