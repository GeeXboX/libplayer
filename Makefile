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

CFLAGS += -Isrc
LDFLAGS += -Lsrc -lplayer -lpthread

ifeq ($(BUILD_STATIC),yes)
  LDFLAGS += $(EXTRALIBS)
endif

all: lib test

lib:
	$(MAKE) -C src

test:
	$(CC) $(LIBTEST_SRCS) $(OPTFLAGS) $(CFLAGS) $(LDFLAGS) -o $(LIBTEST)
	$(CC) $(TESTPLAYER_SRCS) $(OPTFLAGS) $(CFLAGS) $(LDFLAGS) -o $(TESTPLAYER)

clean:
	$(MAKE) -C src clean
	rm -f $(LIBTEST)
	rm -f $(TESTPLAYER)

distclean: clean
	rm -f config.log
	rm -f config.mak
	rm -f $(PKGCONFIG_FILE)

install: install-pkgconfig
	$(MAKE) -C src install
	$(INSTALL) -c -m 755 $(LIBTEST) $(bindir)
	$(INSTALL) -c -m 755 $(TESTPLAYER) $(bindir)

install-pkgconfig: $(PKGCONFIG_FILE)
	$(INSTALL) -d "$(PKGCONFIG_DIR)"
	$(INSTALL) -m 644 $< "$(PKGCONFIG_DIR)"

uninstall:
	$(MAKE) -C src uninstall
	rm -f $(bindir)/$(LIBTEST)
	rm -f $(bindir)/$(TESTPLAYER)
	rm -f $(PKGCONFIG_DIR)/$(PKGCONFIG_FILE)

.phony: clean distclean
.phony: install install-pkgconfig
