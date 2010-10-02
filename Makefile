ifeq (,$(wildcard config.mak))
$(error "config.mak is not present, run configure !")
endif
include config.mak

PKGCONFIG_DIR = $(libdir)/pkgconfig
PKGCONFIG_FILE = libplayer.pc

PLTEST = libplayer-test
PLTEST_SRCS = libplayer-test.c
PLTEST_OBJS = $(PLTEST_SRCS:.c=.o)
PLTEST_MAN = $(PLTEST).1
PLTESTVDR = libplayer-testvdr
PLTESTVDR_SRCS = libplayer-testvdr.c
PLTESTVDR_OBJS = $(PLTESTVDR_SRCS:.c=.o)
PLTESTVDR_MAN = $(PLTESTVDR).1

APPS_CPPFLAGS = -Isrc $(CFG_CPPFLAGS) $(CPPFLAGS)
APPS_LDFLAGS = -Lsrc -lplayer $(CFG_LDFLAGS) $(LDFLAGS)

MANS = $(PLTEST_MAN) $(PLTESTVDR_MAN)

ifeq ($(BUILD_STATIC),yes)
ifeq ($(BUILD_SHARED),no)
  APPS_LDFLAGS += $(EXTRALIBS)
endif
endif

DISTFILE = libplayer-$(VERSION).tar.bz2

EXTRADIST = \
	AUTHORS \
	ChangeLog \
	configure \
	COPYING \
	README \
	stats.sh \
	TODO \
	$(MANS) \

SUBDIRS = \
	bindings \
	bindings/python \
	DOCS \
	samples \
	src \

.SUFFIXES: .c .o

all: lib apps docs bindings

.c.o:
	$(CC) -c $(OPTFLAGS) $(CFLAGS) $(APPS_CPPFLAGS) -o $@ $<

config.mak: configure
	@echo "############################################################"
	@echo "####### Please run ./configure again - it's changed! #######"
	@echo "############################################################"

lib:
	$(MAKE) -C src

$(PLTEST): $(PLTEST_OBJS)
	$(CC) $(PLTEST_OBJS) $(APPS_LDFLAGS) -o $(PLTEST)
$(PLTESTVDR): $(PLTESTVDR_OBJS)
	$(CC) $(PLTESTVDR_OBJS) $(APPS_LDFLAGS) -o $(PLTESTVDR)

apps-dep:
	$(CC) -MM $(CFLAGS) $(APPS_CPPFLAGS) $(PLTEST_SRCS) 1>.depend
	$(CC) -MM $(CFLAGS) $(APPS_CPPFLAGS) $(PLTESTVDR_SRCS) 1>>.depend

apps-all: $(PLTEST) $(PLTESTVDR)

apps: apps-dep lib
	$(MAKE) apps-all

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
	rm -f *.o
	rm -f $(PLTEST)
	rm -f $(PLTESTVDR)
	rm -f .depend

distclean: clean docs-clean
	rm -f config.log
	rm -f config.mak
	rm -f $(DISTFILE)
	rm -f $(PKGCONFIG_FILE)

install: install-lib install-pkgconfig install-apps install-docs install-man install-bindings

install-lib: lib
	$(MAKE) -C src install

install-pkgconfig: $(PKGCONFIG_FILE)
	$(INSTALL) -d "$(PKGCONFIG_DIR)"
	$(INSTALL) -m 644 $< "$(PKGCONFIG_DIR)"

install-apps: apps
	$(INSTALL) -d $(bindir)
	$(INSTALL) -c -m 755 $(PLTEST) $(bindir)
	$(INSTALL) -c -m 755 $(PLTESTVDR) $(bindir)

install-docs: docs
	$(MAKE) -C DOCS install

install-man: $(MANS)
	for m in $(MANS); do \
	  section=`echo $$m | sed -e 's/^.*\\.//'`; \
	  $(INSTALL) -d $(mandir)/man$$section; \
	  $(INSTALL) -m 644 $$m $(mandir)/man$$section; \
	done

install-bindings:
	$(MAKE) -C bindings install

uninstall: uninstall-lib uninstall-pkgconfig uninstall-apps uninstall-docs uninstall-man

uninstall-lib:
	$(MAKE) -C src uninstall

uninstall-pkgconfig:
	rm -f $(PKGCONFIG_DIR)/$(PKGCONFIG_FILE)

uninstall-apps:
	rm -f $(bindir)/$(PLTEST)
	rm -f $(bindir)/$(PLTESTVDR)

uninstall-docs:
	$(MAKE) -C DOCS uninstall

uninstall-man:
	for m in $(MANS); do \
	  section=`echo $$m | sed -e 's/^.*\\.//'`; \
	  rm -f $(mandir)/man$$section/$$m; \
	done

.PHONY: *clean *install* docs binding* apps*

dist:
	-$(RM) $(DISTFILE)
	dist=$(shell pwd)/libplayer-$(VERSION) && \
	for subdir in . $(SUBDIRS); do \
		mkdir -p "$$dist/$$subdir"; \
		$(MAKE) -C $$subdir dist-all DIST="$$dist/$$subdir"; \
	done && \
	tar cjf $(DISTFILE) libplayer-$(VERSION)
	-$(RM) -rf libplayer-$(VERSION)

dist-all:
	cp $(EXTRADIST) $(PLTEST_SRCS) $(PLTESTVDR_SRCS) Makefile $(DIST)

.PHONY: dist dist-all

#
# include dependency files if they exist
#
ifneq ($(wildcard .depend),)
include .depend
endif
