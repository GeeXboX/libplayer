ifeq (,$(wildcard config.mak))
$(error "config.mak is not present, run configure !")
endif
include config.mak

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

install:
	$(MAKE) -C src install
	$(INSTALL) -c -m 755 $(LIBTEST) $(bindir)
	$(INSTALL) -c -m 755 $(TESTPLAYER) $(bindir)

.phony: clean distclean
