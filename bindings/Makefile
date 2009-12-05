ifeq (,$(wildcard ../config.mak))
$(error "../config.mak is not present, run configure !")
endif
include ../config.mak

all: binding-python

clean: binding-python-clean

install: binding-python-install

binding-python:
	$(MAKE) -C python

binding-python-clean:
	$(MAKE) -C python clean

binding-python-install:
	$(MAKE) -C python install

dist-all:
	cp Makefile $(DIST)

.PHONY: clean dist-all install binding*
