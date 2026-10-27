# OODOS's programs (README.md). Needs cc65 (ca65, ld65) and python3.
#
#   make            # build/NAME.com and build/NAME.prl for each of PROGRAMS
#   make check      # the tools' tests (test/check_tools.py)
#
# Each program is SRC/NAME.s, assembled once and linked as a .COM at $0300
# (cfg/com.cfg) and as a .PRL: linked three times with cfg/prl.cfg, at page
# 3 with the zero page at $00, page 4 with it at $02 and page 5 with it at
# $05, and compared by mkprl.py. The CODE of the .COM and of the .PRL's
# first link is checked against the 65C816 rules (tools/rules.py); a
# violation fails the build, and so does any ld65 warning.

SRC      ?= examples
BUILD    ?= build
PROGRAMS ?= hello
AFLAGS   = -g -I include
PYTHON   = python3 -I

# ld65 $(1), failing on a warning as on an error: an initialized zero page
# byte, for one, which ld65 drops with only a warning (the zero page is BSS).
LD65 = @echo ld65 $(1); ld65 $(1) 2>$(2).ld; s=$$?; w=$$(cat $(2).ld); rm -f $(2).ld; \
	[ -z "$$w" ] || echo "$$w" >&2; [ $$s = 0 ] && [ -z "$$w" ]

COMS = $(PROGRAMS:%=$(BUILD)/%.com)
PRLS = $(PROGRAMS:%=$(BUILD)/%.prl)

all: $(COMS) $(PRLS)

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: $(SRC)/%.s include/oodos.inc | $(BUILD)
	ca65 $(AFLAGS) $< -o $@

$(BUILD)/%.com: $(BUILD)/%.o cfg/com.cfg tools/rules.py
	$(call LD65,-C cfg/com.cfg $< -o $@.tmp -m $(BUILD)/$*.map -Ln $(BUILD)/$*.lbl,$@)
	$(PYTHON) tools/rules.py $@.tmp --map $(BUILD)/$*.map
	mv $@.tmp $@

# Link a is the .PRL's image; b is a page and two bytes of zero page above,
# c two pages and five bytes.
$(BUILD)/%.prl: $(BUILD)/%.o cfg/prl.cfg tools/rules.py tools/mkprl.py
	$(call LD65,-C cfg/prl.cfg $< -o $(BUILD)/$*.a -m $(BUILD)/$*.a.map -Ln $(BUILD)/$*.a.lbl,$@)
	$(call LD65,-C cfg/prl.cfg -D __OODOS_PAGE__=4 -D __OODOS_ZP__=2 $< -o $(BUILD)/$*.b -m $(BUILD)/$*.b.map,$@)
	$(call LD65,-C cfg/prl.cfg -D __OODOS_PAGE__=5 -D __OODOS_ZP__=5 $< -o $(BUILD)/$*.c -m $(BUILD)/$*.c.map,$@)
	$(PYTHON) tools/rules.py $(BUILD)/$*.a --map $(BUILD)/$*.a.map
	$(PYTHON) tools/mkprl.py $(BUILD)/$*.a $(BUILD)/$*.b $(BUILD)/$*.c --map $(BUILD)/$*.a.map -o $@

check: all
	$(PYTHON) test/check_tools.py $(BUILD)

clean:
	rm -rf $(BUILD)

.PHONY: all check clean
.SECONDARY:
