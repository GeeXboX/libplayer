ifeq (,$(wildcard ../config.mak))
$(error "../config.mak is not present, run configure !")
endif
include ../config.mak

EXTRADIST = \
	Doxyfile \
	event_handler-supervisor.txt \
	libplayer.png \
	libplayer.svg \
	libplayer-internals.png \
	libplayer-internals.svg \

all: doxygen

doxygen:
ifeq ($(DOC),yes)
ifeq (,$(wildcard doxygen))
	PROJECT_NUMBER="$(VERSION)" doxygen Doxyfile
endif
endif

clean:
	rm -rf doxygen

install:
ifeq ($(DOC),yes)
	if [ -d doxygen/html ]; then \
		$(INSTALL) -d $(docdir)/libplayer; \
		$(INSTALL) -c -m 755 doxygen/html/* $(docdir)/libplayer; \
	fi
endif

uninstall:
	rm -rf $(docdir)/libplayer

dist-all:
	cp $(EXTRADIST) Makefile $(DIST)

.PHONY: clean dist-all doxygen *install
