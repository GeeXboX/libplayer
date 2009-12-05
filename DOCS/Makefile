ifeq (,$(wildcard ../config.mak))
$(error "../config.mak is not present, run configure !")
endif
include ../config.mak

EXTRADIST = \
	Doxyfile \
	event_handler-supervisor.txt \
	libplayer-internals.png \

dist-all:
	cp $(EXTRADIST) Makefile $(DIST)

.PHONY: dist-all
