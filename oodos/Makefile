# OODOS's programs (README.md). Needs cc65 (ca65, ld65) and python3.
#
#   make            # build/NAME.com and build/NAME.prl for each of PROGRAMS
#   make check      # the tools' tests (test/check_tools.py)
#
# Each program is SRC/NAME.s, assembled once and linked as a .COM at $0300
# (cfg/com.cfg) and as a .PRL: linked twice with cfg/prl.cfg, at page 3 with
# the zero page at $00 and at page 4 with it at $02, and compared by mkprl.py.
# Every link is checked against the 65C816 rules (tools/rules.py); a
# violation fails the build.

SRC      ?= examples
BUILD    ?= build
PROGRAMS ?= hello
AFLAGS   = -g -I include
PYTHON   = python3 -I
CFG      = cfg/com.cfg cfg/prl.cfg

COMS = $(PROGRAMS:%=$(BUILD)/%.com)
PRLS = $(PROGRAMS:%=$(BUILD)/%.prl)

all: $(COMS) $(PRLS)

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: $(SRC)/%.s include/oodos.inc | $(BUILD)
	ca65 $(AFLAGS) $< -o $@

$(BUILD)/%.com: $(BUILD)/%.o cfg/com.cfg tools/rules.py
	ld65 -C cfg/com.cfg $< -o $@.tmp -m $(BUILD)/$*.map -Ln $(BUILD)/$*.lbl
	$(PYTHON) tools/rules.py $@.tmp --map $(BUILD)/$*.map
	mv $@.tmp $@

# Link a is the .PRL's image; b, a page and two bytes of zero page above.
$(BUILD)/%.prl: $(BUILD)/%.o cfg/prl.cfg tools/rules.py tools/mkprl.py
	ld65 -C cfg/prl.cfg $< -o $(BUILD)/$*.a -m $(BUILD)/$*.a.map -Ln $(BUILD)/$*.a.lbl
	ld65 -C cfg/prl.cfg -D __OODOS_PAGE__=4 -D __OODOS_ZP__=2 $< -o $(BUILD)/$*.b -m $(BUILD)/$*.b.map
	$(PYTHON) tools/rules.py $(BUILD)/$*.a --map $(BUILD)/$*.a.map
	$(PYTHON) tools/mkprl.py $(BUILD)/$*.a $(BUILD)/$*.b -o $@

check: all
	$(PYTHON) test/check_tools.py

clean:
	rm -rf $(BUILD)

.PHONY: all check clean
.SECONDARY:
