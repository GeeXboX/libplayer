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

DOXYGEN =

ifeq ($(DOC),yes)
  DOXYGEN = doxygen
endif

all: lib test $(DOXYGEN) bindings

lib:
	$(MAKE) -C src

test: lib
	$(CC) $(LIBTEST_SRCS) $(OPTFLAGS) $(CFLAGS) $(EXTRACFLAGS) $(LDFLAGS) -o $(LIBTEST)
	$(CC) $(TESTPLAYER_SRCS) $(OPTFLAGS) $(CFLAGS) $(EXTRACFLAGS) $(LDFLAGS) -o $(TESTPLAYER)
	$(CC) $(TESTVDR_SRCS) $(OPTFLAGS) $(CFLAGS) $(EXTRACFLAGS) $(LDFLAGS) -o $(TESTVDR)

doxygen:
ifeq (,$(wildcard DOCS/doxygen))
	PROJECT_NUMBER="$(LIBPLAYER_VERSION)" doxygen DOCS/Doxyfile
endif

bindings: binding-python

binding-python:
ifeq ($(BINDING_PYTHON),yes)
	cd bindings/python && python setup.py build
endif

bindings-clean: binding-python-clean

binding-python-clean:
ifeq ($(BINDING_PYTHON),yes)
	cd bindings/python && python setup.py clean && rm -rf build
endif

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

install: install-pkgconfig install-lib install-test install-doxygen

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
	if [ -d DOCS/doxygen/html ]; then \
		$(INSTALL) -d $(docdir)/libplayer; \
		$(INSTALL) -c -m 755 DOCS/doxygen/html/* $(docdir)/libplayer; \
	fi

uninstall:
	$(MAKE) -C src uninstall
	rm -f $(bindir)/$(LIBTEST)
	rm -f $(bindir)/$(TESTPLAYER)
	rm -f $(bindir)/$(TESTVDR)
	rm -f $(PKGCONFIG_DIR)/$(PKGCONFIG_FILE)

.PHONY: clean distclean
.PHONY: install install-pkgconfig uninstall
.PHONY: doxygen
