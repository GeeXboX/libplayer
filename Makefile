ifeq (,$(wildcard config.mak))
$(error "config.mak is not present, run configure !")
endif
include config.mak

PKGCONFIG_DIR = $(libdir)/pkgconfig
PKGCONFIG_FILE = libplayer.pc

LIBTEST = regtest-libplayer
LIBTEST_SRCS = regtest-libplayer.c
TESTPLAYER = test-player
TESTPLAYER_SRCS = test-player.c
TESTVDR = test-vdr
TESTVDR_SRCS = test-vdr.c

CFLAGS += -Isrc
LDFLAGS += -Lsrc -lplayer -lpthread

ifeq ($(BUILD_STATIC),yes)
  LDFLAGS += $(EXTRALIBS)
endif

DISTFILE = libplayer-$(LIBPLAYER_VERSION).tar.bz2

EXTRADIST = \
	AUTHORS \
	configure \
	COPYING \
	README \
	stats.sh \

SUBDIRS = \
	bindings \
	bindings/python \
	DOCS \
	samples \
	src \

all: lib test doxygen bindings

lib:
	$(MAKE) -C src

test: lib
	$(CC) $(LIBTEST_SRCS) $(OPTFLAGS) $(CFLAGS) $(EXTRACFLAGS) $(LDFLAGS) -o $(LIBTEST)
	$(CC) $(TESTPLAYER_SRCS) $(OPTFLAGS) $(CFLAGS) $(EXTRACFLAGS) $(LDFLAGS) -o $(TESTPLAYER)
	$(CC) $(TESTVDR_SRCS) $(OPTFLAGS) $(CFLAGS) $(EXTRACFLAGS) $(LDFLAGS) -o $(TESTVDR)

doxygen:
ifeq ($(DOC),yes)
  ifeq (,$(wildcard DOCS/doxygen))
	PROJECT_NUMBER="$(LIBPLAYER_VERSION)" doxygen DOCS/Doxyfile
  endif
endif

bindings: lib
	$(MAKE) -C bindings

bindings-clean:
	$(MAKE) -C bindings clean

clean: bindings-clean
	$(MAKE) -C src clean
	rm -f $(LIBTEST)
	rm -f $(TESTPLAYER)
	rm -f $(TESTVDR)

distclean: clean
	rm -f config.log
	rm -f config.mak
	rm -f $(PKGCONFIG_FILE)
	rm -rf DOCS/doxygen

install: install-pkgconfig install-lib install-test install-doxygen install-bindings

install-pkgconfig: $(PKGCONFIG_FILE)
	$(INSTALL) -d "$(PKGCONFIG_DIR)"
	$(INSTALL) -m 644 $< "$(PKGCONFIG_DIR)"

install-lib: lib
	$(MAKE) -C src install

install-test: test
	$(INSTALL) -d $(bindir)
	$(INSTALL) -c -m 755 $(LIBTEST) $(bindir)
	$(INSTALL) -c -m 755 $(TESTPLAYER) $(bindir)
	$(INSTALL) -c -m 755 $(TESTVDR) $(bindir)

install-doxygen: doxygen
ifeq ($(DOC),yes)
	if [ -d DOCS/doxygen/html ]; then \
		$(INSTALL) -d $(docdir)/libplayer; \
		$(INSTALL) -c -m 755 DOCS/doxygen/html/* $(docdir)/libplayer; \
	fi
endif

install-bindings:
	$(MAKE) -C bindings install

uninstall-pkgconfig:
	rm -f $(PKGCONFIG_DIR)/$(PKGCONFIG_FILE)

uninstall-lib:
	$(MAKE) -C src uninstall

uninstall-test:
	rm -f $(bindir)/$(LIBTEST)
	rm -f $(bindir)/$(TESTPLAYER)
	rm -f $(bindir)/$(TESTVDR)

uninstall-doxygen:
	rm -rf $(docdir)/libplayer

uninstall: uninstall-pkgconfig uninstall-lib uninstall-test uninstall-doxygen

.PHONY: *clean *install* doxygen binding*

dist:
	-$(RM) $(DISTFILE)
	dist=$(shell pwd)/libplayer-$(LIBPLAYER_VERSION) && \
	for subdir in . $(SUBDIRS); do \
		mkdir -p "$$dist/$$subdir"; \
		$(MAKE) -C $$subdir dist-all DIST="$$dist/$$subdir"; \
	done && \
	tar cjf $(DISTFILE) libplayer-$(LIBPLAYER_VERSION)
	-$(RM) -rf libplayer-$(LIBPLAYER_VERSION)

dist-all:
	cp $(EXTRADIST) $(LIBTEST_SRCS) $(TESTPLAYER_SRCS) $(TESTVDR_SRCS) Makefile $(DIST)

.PHONY: dist dist-all
