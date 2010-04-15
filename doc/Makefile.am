# Copyright (C) 2008,2009,2010 Nokia Corporation
# Contact: Kai Vehmanen 
# Licensed under LGPL. See file COPYING.

# documentation rules
# -------------------

EXTRA_DIST = doxygen.cfg.in libcmtspeechdata_api_docs_main.txt

if DOXYGEN_DOCS_ENABLED
  DOXYGEN_LINE = $(DOXYGEN) doxygen.cfg

install-data-local:
	if test -e doc-target-stamp ; then $(INSTALL) -d $(DESTDIR)${datarootdir}/doc/libcmtspeechdata-doc/html ; fi
	if test -e doc-target-stamp ; then $(INSTALL_DATA) html/* $(DESTDIR)${datarootdir}/doc/libcmtspeechdata-doc/html ; fi

uninstall-local:
	rm -Rf $(DESTDIR)${datarootdir}/doc/libcmtspeechdata-doc/

else 
  DOXYGEN_LINE = 
endif

.PHONY : doc

doc: doxygen.cfg
	touch doc-target-stamp
	$(DOXYGEN_LINE)

clean-local:
	-rm -f doc-target-stamp
	-rm -rf html latex