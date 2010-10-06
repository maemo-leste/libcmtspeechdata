# Copyright (C) 2008,2009,2010 Nokia Corporation
# Contact: Kai Vehmanen 
# Licensed under LGPL. See file COPYING.

SUBDIRS =		doc \
			dummy-backend

EXTRA_DIST = 		cmtspeech_config.h.in \
			libcmtspeechdata.ver

# libcmtspeechdata main library sources
# -------------------------------------

lib_LTLIBRARIES = 	libcmtspeechdata.la

cmtspeechdata_common_src = \
			cmtspeech_msgs.c \
			cmtspeech_backend_common.c \
			sal_debug.c

cmtspeech_data_kernel_inc = \
			kernel-headers/linux/cs-protocol.h

cmtspeechdata_common_inc = \
			cmtspeech_backend_common.h \
			sal_ring.h \
			sal_debug.h

cmtspeechdata_pub_inc = \
			cmtspeech.h \
			cmtspeech_msgs.h

cmtspeechdata_pub_inc_nodist = \
			cmtspeech_config.h

include_HEADERS =	$(cmtspeechdata_pub_inc)

nodist_include_HEADERS =$(cmtspeechdata_pub_inc_nodist)

libcmtspeechdata_la_SOURCES = \
			$(cmtspeechdata_common_src) \
			$(cmtspeechdata_pub_inc) \
			cmtspeech_nokiamodem.c
libcmtspeechdata_la_CFLAGS = -prefer-pic
libcmtspeechdata_la_LIBADD = -lrt
libcmtspeechdata_la_LDFLAGS =  -Wl,--version-script,$(srcdir)/libcmtspeechdata.ver

# test application sources
# ------------------------

TESTS = 		test_cmtspeech_msgs \
			test_ring

noinst_PROGRAMS = 	test_cmtspeech_msgs \
			test_ring \
			test_cmtspeech \
			utils/send_cmtspeech_reset \
			utils/cmtspeech_ramp_test \
			utils/cmtspeech_ofono_test

noinst_HEADERS =	$(cmtspeechdata_common_inc) \
			$(cmtspeech_data_kernel_inc)

test_cmtspeech_msgs_SOURCES = test_cmtspeech_msgs.c
test_cmtspeech_msgs_CFLAGS = @CHECK_CFLAGS@
test_cmtspeech_msgs_LDADD = \
			@CHECK_LIBS@ \
			libcmtspeechdata.la

test_cmtspeech_SOURCES = test_cmtspeech.c
test_cmtspeech_CFLAGS = @CHECK_CFLAGS@
test_cmtspeech_LDADD = 	@CHECK_LIBS@ \
			libcmtspeechdata.la \
			-lrt

test_ring_CFLAGS = 	@CHECK_CFLAGS@
test_ring_LDADD = 	@CHECK_LIBS@ \
			libcmtspeechdata.la

utils_send_cmtspeech_reset_SOURCES = utils/send_cmtspeech_reset.c
utils_send_cmtspeech_reset_LDADD = \
			libcmtspeechdata.la

utils_cmtspeech_ramp_test_SOURCES = utils/cmtspeech_ramp_test.c
utils_cmtspeech_ramp_test_LDADD = \
			libcmtspeechdata.la

utils_cmtspeech_ofono_test_SOURCES = utils/cmtspeech_ofono_test.c
utils_cmtspeech_ofono_test_CFLAGS = \
			@DBUS_CFLAGS@
utils_cmtspeech_ofono_test_LDADD = \
			@DBUS_LIBS@ \
			libcmtspeechdata.la

# pkg-config
# ----------

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libcmtspeechdata.pc
$(pkgconfig_DATA): config.status

# documentation rules
# -------------------

doc: 
	(cd doc && $(MAKE) $(AM_MAKEFLAGS) doc)

.PHONY : doc
