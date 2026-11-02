# The books' examples (docs/README.md, "Examples"). Each NAME.s here is an
# OODOS .COM, built by oodos/Makefile's rules (oodos.inc, cfg/com.cfg, the
# 65C816 rules gate, ld65's warnings fatal) into BUILD/NAME.com. NAME.out
# beside it is what it prints when run from the CCP: test_docs_examples
# (emulator/test/host) runs each one and compares.
#
#   make                 # build/NAME.com for each NAME.s
#   make BUILD=dir       # somewhere else

BUILD    ?= build
OODOS    := $(abspath ../../oodos)
HERE     := $(abspath .)
PROGRAMS := $(basename $(notdir $(wildcard $(HERE)/*.s)))
PYTHON   ?= python3 -I

all:
	$(MAKE) -C $(OODOS) SRC=$(HERE) BUILD=$(abspath $(BUILD)) PROGRAMS="$(PROGRAMS)" \
		PYTHON="$(PYTHON)" $(PROGRAMS:%=$(abspath $(BUILD))/%.com)

clean:
	rm -rf $(BUILD)

.PHONY: all clean
