ifeq (,$(wildcard ../../config.mak))
$(error "../../config.mak is not present, run configure !")
endif
include ../../config.mak

EXTRADIST = \
	mrl.c \
	player.c \
	player_module.c \
	pyplayer.h \
	player-example.py \
	setup.py \

all: binding

binding:
ifeq ($(BINDING_PYTHON),yes)
	PKG_CONFIG_PATH="../../" python setup.py build
endif

clean:
ifeq ($(BINDING_PYTHON),yes)
	python setup.py clean && rm -rf build
endif

install:
ifeq ($(BINDING_PYTHON),yes)
	python setup.py install --prefix=$(DESTDIR)$(PREFIX)
endif

dist-all:
	cp $(EXTRADIST) Makefile $(DIST)

.PHONY: binding clean dist-all install
