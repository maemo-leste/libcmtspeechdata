*************************************
Developer README for libcmtspeechdata
*************************************

Copyright (C) 2009,2010 Nokia Corporation. All rights reserved.

Introduction
============

This package provides the "libcmtspeechdata" library. Libcmtspeechdata
provides an application interface for implementing the speech data
path for cellular voice calls. The library depends on other components
for setting up and managing the call signaling path.

This README file provides information for developers. See also 
the main package README (part of the source tree).

API: Developer's guide to libcmtspeechdata interfaces
=====================================================

See the API documentation in:
   - source-tree libcmtspeechdata/doc/libcmtspeechdata_api_docs_main.txt
   - libcmtspeehcdata-doc debian package:
     /usr/share/doc/libcmtspeechdata-doc/html/

Internals: Developer's guide to libcmtspeechdata internals
==========================================================

This section documents issues related to how libcmtspeechdata, and the
various library backends, are implemented. It is meant for developers
that are modifying, or otherwise need to understand the internals of,
libcmtspeechdata.

Internals: Code layout
----------------------

'cmtspeech_*.[ch]'
    Sources for the public library.

'sal_*.[ch]'
    Library sources with legay file prefix (library was formely named 
    the SSI Audio Library (SAL)).

Internals: Resource allocation in 'nokiamodem' backend
------------------------------------------------------

When a library instance is opened with cmtspeech_open(), the backend
will open the /dev/cmt_speech kernel device. The SSI control channel for 
CMT Speech Data protocol is opened. Also memory for buffer transfer is 
allocated at this point. The resources allocated in cmtspeech_open() are 
freed only when the library instance is closed with cmtspeech_close().

Internals: The libcmtspeechdata backend interface 
-------------------------------------------------

The library backends implement the public interface defined in
cmtspeech.h file. To avoid unnecessary code duplication, some common
helper functions for implementing cmtspeech.h are available in
cmtspeech_backend_common.h. Also the message decoding/encoding
interface provided by cmtspeech_msgs.h should be used by all backends.

Currently no dynamic mechanism is provided to select different
backends. Instead, a separate version of the library is build
for each backend. The library interface is the same, so if
dynamic linking is used, it is possible to change to a different 
backend without recompiling the clients.
