# OODOS's programs (README.md). Needs cc65 (ca65, ld65) and python3.
#
#   make            # build/NAME.com and build/NAME.prl for each of PROGRAMS,
#                   # and the utilities
#   make utils      # build/NAME.com for each utils/NAME.s (phase 4b's)
#   make check      # the tools' tests (test/check_tools.py)
#
# Each program is SRC/NAME.s, assembled once and linked as a .COM at $0300
# (cfg/com.cfg) and as a .PRL: linked three times with cfg/prl.cfg, at page
# 3 with the zero page at $00, page 4 with it at $02 and page 5 with it at
# $05, and compared by mkprl.py. The CODE of the .COM and of the .PRL's
# first link is checked against the 65C816 rules (tools/rules.py); a
# violation fails the build, and so does any ld65 warning. Each is rebuilt
# when any include/*.inc changes, as it may include any of them.
#
# The utilities (hw/oodos.md, "The utilities, phase 4b") are every
# utils/NAME.s there is, .COMs only, through the same gate; each includes
# include/cmd.inc and include/util.inc, and some include/system.inc (the
# BIOS entries, for function 50): each is rebuilt when any of them changes.
# A new one needs no change here.

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
UTILS = $(patsubst utils/%.s,%,$(wildcard utils/*.s))
UTIL_COMS = $(UTILS:%=$(BUILD)/%.com)
UTIL_INCS = include/oodos.inc include/system.inc include/cmd.inc include/util.inc include/redir.inc
# OODOS's RSXs (phase 5b): rsx/NAME.s, built as NAME.rsx (cfg/rsx.cfg, three links
# and mkprl.py, as a .PRL). A utility in ATTACHED gets its RSX, NAMErsx.rsx,
# attached by tools/gencom.py: GET.COM and PUT.COM.
RSXS = $(patsubst rsx/%.s,%,$(wildcard rsx/*.s))
ATTACHED = get put

all: $(COMS) $(PRLS) $(UTIL_COMS)

utils: $(UTIL_COMS) $(RSXS:%=$(BUILD)/%.rsx) | $(BUILD)

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: $(SRC)/%.s $(wildcard include/*.inc) | $(BUILD)
	ca65 $(AFLAGS) $< -o $@

$(UTILS:%=$(BUILD)/%.o): $(BUILD)/%.o: utils/%.s $(UTIL_INCS) | $(BUILD)
	ca65 $(AFLAGS) $< -o $@

$(RSXS:%=$(BUILD)/%.o): $(BUILD)/%.o: rsx/%.s include/oodos.inc include/rsx.inc | $(BUILD)
	ca65 $(AFLAGS) $< -o $@

$(BUILD)/%.rsx: $(BUILD)/%.o cfg/rsx.cfg tools/rules.py tools/mkprl.py
	$(call LD65,-C cfg/rsx.cfg $< -o $(BUILD)/$*.ra -m $(BUILD)/$*.ra.map -Ln $(BUILD)/$*.ra.lbl,$@)
	$(call LD65,-C cfg/rsx.cfg -D __OODOS_PAGE__=4 -D __OODOS_ZP__=2 $< -o $(BUILD)/$*.rb,$@)
	$(call LD65,-C cfg/rsx.cfg -D __OODOS_PAGE__=5 -D __OODOS_ZP__=5 $< -o $(BUILD)/$*.rc,$@)
	$(PYTHON) tools/rules.py $(BUILD)/$*.ra --map $(BUILD)/$*.ra.map
	$(PYTHON) tools/mkprl.py $(BUILD)/$*.ra $(BUILD)/$*.rb $(BUILD)/$*.rc --map $(BUILD)/$*.ra.map -o $@

# A utility with its RSX attached: linked, through the rules gate, then gencom.py.
$(ATTACHED:%=$(BUILD)/%.com): $(BUILD)/%.com: $(BUILD)/%.o $(BUILD)/%rsx.rsx cfg/com.cfg tools/rules.py tools/gencom.py
	$(call LD65,-C cfg/com.cfg $< -o $(BUILD)/$*.plain -m $(BUILD)/$*.map -Ln $(BUILD)/$*.lbl,$@)
	$(PYTHON) tools/rules.py $(BUILD)/$*.plain --map $(BUILD)/$*.map
	$(PYTHON) tools/gencom.py $(BUILD)/$*.plain $(BUILD)/$*rsx.rsx -o $@

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

.PHONY: all utils check clean
.SECONDARY:
