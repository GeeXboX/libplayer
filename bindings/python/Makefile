ifeq (,$(wildcard ../../config.mak))
$(error "../../config.mak is not present, run configure !")
endif
include ../../config.mak

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

.PHONY: binding clean install
