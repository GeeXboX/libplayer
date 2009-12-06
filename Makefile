ifeq (,$(wildcard config.mak))
$(error "config.mak is not present, run configure !")
endif
include config.mak

PKGCONFIG_DIR = $(libdir)/pkgconfig
PKGCONFIG_FILE = libplayer.pc

PLREGTEST = libplayer-regtest
PLREGTEST_SRCS = libplayer-regtest.c
PLTEST = libplayer-test
PLTEST_SRCS = libplayer-test.c
PLTESTVDR = libplayer-testvdr
PLTESTVDR_SRCS = libplayer-testvdr.c

CFLAGS += -Isrc
LDFLAGS += -Lsrc -lplayer -lpthread

ifeq ($(BUILD_STATIC),yes)
  LDFLAGS += $(EXTRALIBS)
endif

DISTFILE = libplayer-$(LIBPLAYER_VERSION).tar.bz2

EXTRADIST = \
	AUTHORS \
	ChangeLog \
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

all: lib test docs bindings

lib:
	$(MAKE) -C src

test: lib
	$(CC) $(PLREGTEST_SRCS) $(OPTFLAGS) $(CFLAGS) $(EXTRACFLAGS) $(LDFLAGS) -o $(PLREGTEST)
	$(CC) $(PLTEST_SRCS) $(OPTFLAGS) $(CFLAGS) $(EXTRACFLAGS) $(LDFLAGS) -o $(PLTEST)
	$(CC) $(PLTESTVDR_SRCS) $(OPTFLAGS) $(CFLAGS) $(EXTRACFLAGS) $(LDFLAGS) -o $(PLTESTVDR)

docs:
	$(MAKE) -C DOCS

docs-clean:
	$(MAKE) -C DOCS clean

bindings: lib
	$(MAKE) -C bindings

bindings-clean:
	$(MAKE) -C bindings clean

clean: bindings-clean
	$(MAKE) -C src clean
	rm -f $(PLREGTEST)
	rm -f $(PLTEST)
	rm -f $(PLTESTVDR)

distclean: clean docs-clean
	rm -f config.log
	rm -f config.mak
	rm -f $(DISTFILE)
	rm -f $(PKGCONFIG_FILE)

install: install-lib install-pkgconfig install-test install-docs install-bindings

install-lib: lib
	$(MAKE) -C src install

install-pkgconfig: $(PKGCONFIG_FILE)
	$(INSTALL) -d "$(PKGCONFIG_DIR)"
	$(INSTALL) -m 644 $< "$(PKGCONFIG_DIR)"

install-test: test
	$(INSTALL) -d $(bindir)
	$(INSTALL) -c -m 755 $(PLREGTEST) $(bindir)
	$(INSTALL) -c -m 755 $(PLTEST) $(bindir)
	$(INSTALL) -c -m 755 $(PLTESTVDR) $(bindir)

install-docs: docs
	$(MAKE) -C DOCS install

install-bindings:
	$(MAKE) -C bindings install

uninstall: uninstall-lib uninstall-pkgconfig uninstall-test uninstall-docs

uninstall-lib:
	$(MAKE) -C src uninstall

uninstall-pkgconfig:
	rm -f $(PKGCONFIG_DIR)/$(PKGCONFIG_FILE)

uninstall-test:
	rm -f $(bindir)/$(PLREGTEST)
	rm -f $(bindir)/$(PLTEST)
	rm -f $(bindir)/$(PLTESTVDR)

uninstall-docs:
	$(MAKE) -C DOCS uninstall

.PHONY: *clean *install* docs binding*

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
	cp $(EXTRADIST) $(PLREGTEST_SRCS) $(PLTEST_SRCS) $(PLTESTVDR_SRCS) Makefile $(DIST)

.PHONY: dist dist-all
