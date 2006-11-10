include config.mak

LIBTEST = regtest-libplayer
SRCS = regtest-libplayer.c

CFLAGS += -Isrc
LDFLAGS += -Lsrc -lplayer -lpthread

all: lib test

lib:
	$(MAKE) -C src

test:
	$(CC) $(SRCS) $(OPTFLAGS) $(CFLAGS) $(LDFLAGS) -o $(LIBTEST)

clean:
	$(MAKE) -C src clean
	rm -f $(LIBTEST)

distclean: clean
	rm -f config.log
	rm -f config.mak

install:
	$(MAKE) -C src install
	$(INSTALL) -c -m 755 $(LIBTEST) $(bindir)

.phony: clean distclean
