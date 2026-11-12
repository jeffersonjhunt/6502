# OODOS's programs (README.md). Needs cc65 (ca65, ld65) and python3.
#
#   make            # build/NAME.com and build/NAME.prl for each of PROGRAMS,
#                   # and the utilities
#   make utils      # build/NAME.com for each utils/NAME.s (phase 4b's)
#   make check      # the tools' tests (test/check_tools.py, test/check_mkasset.py)
#   make ooui       # build/ooui.lib, OOUI's program part, and build/OOUI.BNK (below)
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
# A new one needs no change here, unless it uses OOLIB (below: OOLIB_USERS).

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
# SUBMIT.COM's RSX is subrsx.rsx (an RSX's name is at most 8 characters).

all: $(COMS) $(PRLS) $(UTIL_COMS) $(BUILD)/ooui.lib $(BUILD)/OOUI.BNK

utils: $(UTIL_COMS) $(RSXS:%=$(BUILD)/%.rsx) | $(BUILD)

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: $(SRC)/%.s $(wildcard include/*.inc) | $(BUILD)
	ca65 $(AFLAGS) $< -o $@

$(UTILS:%=$(BUILD)/%.o): $(BUILD)/%.o: utils/%.s $(UTIL_INCS) | $(BUILD)
	ca65 $(AFLAGS) $< -o $@

$(RSXS:%=$(BUILD)/%.o): $(BUILD)/%.o: rsx/%.s include/oodos.inc include/rsx.inc include/rxfile.inc | $(BUILD)
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

$(BUILD)/submit.com: $(BUILD)/submit.o $(BUILD)/subrsx.rsx cfg/com.cfg tools/rules.py tools/gencom.py
	$(call LD65,-C cfg/com.cfg $< -o $(BUILD)/submit.plain -m $(BUILD)/submit.map -Ln $(BUILD)/submit.lbl,$@)
	$(PYTHON) tools/rules.py $(BUILD)/submit.plain --map $(BUILD)/submit.map
	$(PYTHON) tools/gencom.py $(BUILD)/submit.plain $(BUILD)/subrsx.rsx -o $@

# OOLIB, the linked library (lib/*.s, include/oolib.inc; hw/oolib.md, question 9): each lib/NAME.s
# assembled once into build/oolib.lib, which ld65 takes only the modules of that a program uses. A
# utility in OOLIB_USERS is linked with it, through the same gate (its code and the library's), and
# tools/w1.py: none of its segments in W1's $4000-$7FFF (the W1 contract), or the build fails.
# Each module's zero page, lib/zpNAME.s, goes into the archive right after NAME, so ld65 lays a
# program's zero page out as it would with the bytes in NAME itself (libzp.py check holds it).
LIBSRC = $(patsubst lib/%.s,%,$(wildcard lib/*.s))
LIBS = $(foreach m,$(filter-out zp%,$(LIBSRC)),$(m) $(filter zp$(m),$(LIBSRC)))
$(if $(filter-out $(LIBS),$(LIBSRC)),$(error lib/: zero page modules without their code: $(filter-out $(LIBS),$(LIBSRC))))
OOLIB_USERS = playsid view

$(LIBS:%=$(BUILD)/lib-%.o): $(BUILD)/lib-%.o: lib/%.s $(wildcard lib/*.inc) include/oodos.inc include/oolib.inc | $(BUILD)
	ca65 $(AFLAGS) -I lib --bin-include-dir $(BUILD) $< -o $@

# The music driver for the sound card's 6510 (lib/mudrv/, L6a): a 6502 program, not a library module,
# assembled on its own and taken into lib/mu.s's image whole.
$(BUILD)/mudrv.bin: lib/mudrv/mudrv.s lib/mudrv/mudrv.cfg | $(BUILD)
	ca65 lib/mudrv/mudrv.s -o $(BUILD)/mudrv.o
	ld65 -C lib/mudrv/mudrv.cfg $(BUILD)/mudrv.o -o $@
$(BUILD)/lib-mu.o: $(BUILD)/mudrv.bin

# Every module's zero page in a lib/zp*.s of its own (tools/libzp.py), so an overlay can link the library.
$(BUILD)/oolib.lib: $(LIBS:%=$(BUILD)/lib-%.o) tools/libzp.py
	$(PYTHON) tools/libzp.py check $(LIBS:%=$(BUILD)/lib-%.o)
	rm -f $@
	ar65 a $@ $(LIBS:%=$(BUILD)/lib-%.o)

$(OOLIB_USERS:%=$(BUILD)/%.o): include/oolib.inc

$(OOLIB_USERS:%=$(BUILD)/%.com): $(BUILD)/%.com: $(BUILD)/%.o $(BUILD)/oolib.lib cfg/com.cfg tools/rules.py tools/w1.py
	$(call LD65,-C cfg/com.cfg $< $(BUILD)/oolib.lib -o $@.tmp -m $(BUILD)/$*.map -Ln $(BUILD)/$*.lbl,$@)
	$(PYTHON) tools/rules.py $@.tmp --map $(BUILD)/$*.map
	$(PYTHON) tools/w1.py $(BUILD)/$*.map
	mv $@.tmp $@

$(BUILD)/%.com: $(BUILD)/%.o cfg/com.cfg tools/rules.py
	$(call LD65,-C cfg/com.cfg $< -o $@.tmp -m $(BUILD)/$*.map -Ln $(BUILD)/$*.lbl,$@)
	$(PYTHON) tools/rules.py $@.tmp --map $(BUILD)/$*.map
	mv $@.tmp $@

# OOUI, the UI toolkit (include/ooui.inc; hw/ooui.md): a banked library from U3a. Its modules, ui/*.s
# (but uiturbo, a program's only), are assembled with UIBANK defined (bnk-NAME.o) and made into
# build/OOUI.BNK by tools/mkbank.py: three links of cfg/ooui-bnk.cfg, the fixups found and proved, the
# references across blocks, the fall-throughs between segments and the 65C816 rules checked
# (hw/ooui.md, "U3a: OOUI as a banked library"); build/ooui-bnk.lbl its labels, for the tests. The
# program part, build/ooui.lib, is uiprog/*.s (the stubs, ui_load, UISTATE, and zpuiload.s: OOUI's zero
# page, ui/zpuicore.s's, the program's) and the themes ui/uitheme.s and ui/uiturbo.s (lib-NAME.o,
# libzp.py check).
# A program in OOUI_USERS is linked with ooui.lib, then oolib.lib, through the rules gate and w1.py,
# with OOUI_CFG: a config with a UISTATE area of its own on a page (test/ui/uiprog.cfg shows one).
UISRC = $(patsubst ui/%.s,%,$(wildcard ui/*.s))
BANKMODS = $(filter-out uiturbo,$(UISRC))
UIPRSRC = $(patsubst uiprog/%.s,%,$(wildcard uiprog/*.s))
UIPROG = $(foreach m,$(filter-out zp%,$(UIPRSRC)),$(m) $(filter zp$(m),$(UIPRSRC)))
UILIBS = $(UIPROG) uitheme uiturbo
$(if $(filter $(UISRC),$(LIBSRC)),$(error ui/ and lib/ share a module's name: $(filter $(UISRC),$(LIBSRC))))
$(if $(filter $(UIPROG),$(UISRC) $(LIBSRC)),$(error uiprog/ shares a module's name: $(filter $(UIPROG),$(UISRC) $(LIBSRC))))
OOUI_USERS =
OOUI_CFG = test/ui/uiprog.cfg
UI_INCS = $(wildcard ui/*.inc) include/oodos.inc include/oolib.inc include/vmem.inc include/ooui.inc

$(BANKMODS:%=$(BUILD)/bnk-%.o): $(BUILD)/bnk-%.o: ui/%.s $(UI_INCS) | $(BUILD)
	ca65 $(AFLAGS) -I ui -D UIBANK=1 $< -o $@

$(BUILD)/OOUI.BNK: $(BANKMODS:%=$(BUILD)/bnk-%.o) cfg/ooui-bnk.cfg ui/uiimp.inc tools/mkbank.py tools/rules.py
	$(PYTHON) tools/mkbank.py --cfg cfg/ooui-bnk.cfg --iface ui/uiimp.inc --work $(BUILD)/bnk -o $@ \
		--lbl $(BUILD)/ooui-bnk.lbl --src $(BANKMODS:%=ui/%.s) -- $(BANKMODS:%=$(BUILD)/bnk-%.o)

$(UIPROG:%=$(BUILD)/lib-%.o): $(BUILD)/lib-%.o: uiprog/%.s $(UI_INCS) | $(BUILD)
	ca65 $(AFLAGS) -I ui $< -o $@
$(BUILD)/lib-uitheme.o $(BUILD)/lib-uiturbo.o: $(BUILD)/lib-%.o: ui/%.s $(UI_INCS) | $(BUILD)
	ca65 $(AFLAGS) -I ui $< -o $@

$(BUILD)/ooui.lib: $(UILIBS:%=$(BUILD)/lib-%.o) tools/libzp.py
	$(PYTHON) tools/libzp.py check $(UILIBS:%=$(BUILD)/lib-%.o)
	rm -f $@
	ar65 a $@ $(UILIBS:%=$(BUILD)/lib-%.o)

ooui: $(BUILD)/ooui.lib $(BUILD)/OOUI.BNK

$(OOUI_USERS:%=$(BUILD)/%.o): include/ooui.inc include/oolib.inc $(wildcard $(SRC)/*.s) # (one may include another)

$(OOUI_USERS:%=$(BUILD)/%.com): $(BUILD)/%.com: $(BUILD)/%.o $(BUILD)/ooui.lib $(BUILD)/oolib.lib $(OOUI_CFG) tools/rules.py \
		tools/w1.py
	$(call LD65,-C $(OOUI_CFG) $< $(BUILD)/ooui.lib $(BUILD)/oolib.lib -o $@.tmp -m $(BUILD)/$*.map -Ln $(BUILD)/$*.lbl,$@)
	$(PYTHON) tools/rules.py $@.tmp --map $(BUILD)/$*.map
	$(PYTHON) tools/w1.py $(BUILD)/$*.map
	mv $@.tmp $@

# The C target (Plan C1; the Programmer's Guide, appendix F, "A program in C"): each of CPROGRAMS is
# SRC/NAME.c, compiled by cc65 for the 65C02 and linked as a .COM with cfg/c.cfg, build/oodosc.lib and
# cc65's none.lib, through the same rules gate (STARTUP, ONCE and CODE: the program's code and the C
# library's). A cc65 warning fails the build, as an ld65 one does. oodosc.lib is c/*.s: the start
# (crt0.s), the console's read and write, argc and argv, bdos(), and cc65's _printf.s altered for rule
# 2; ld65 takes its modules ahead of none.lib's. include/oodos.h's names are oodos.inc's (oodosh.py).
# CLDFLAGS goes to the link: -D __STACKSIZE__=N for another C stack's room than c.cfg's 2 KB.
CPROGRAMS =
CLDFLAGS =
CSRC = $(patsubst c/%.s,%,$(wildcard c/*.s))
CFLAGS = -t none --cpu 65c02 -O -I include
CC65 = @echo cc65 $(1); cc65 $(1) 2>$(2).cc; s=$$?; w=$$(cat $(2).cc); rm -f $(2).cc; \
	[ -z "$$w" ] || echo "$$w" >&2; [ $$s = 0 ] && [ -z "$$w" ]

$(CSRC:%=$(BUILD)/c-%.o): $(BUILD)/c-%.o: c/%.s include/oodos.inc | $(BUILD)
	ca65 --cpu 65c02 $(AFLAGS) $< -o $@

$(BUILD)/oodosc.lib: $(CSRC:%=$(BUILD)/c-%.o)
	rm -f $@
	ar65 a $@ $^

$(BUILD)/oodosh.ok: include/oodos.h include/oodos.inc tools/oodosh.py | $(BUILD)
	$(PYTHON) tools/oodosh.py check include/oodos.inc include/oodos.h
	touch $@

$(CPROGRAMS:%=$(BUILD)/%.o): $(BUILD)/%.o: $(SRC)/%.c include/oodos.h $(BUILD)/oodosh.ok | $(BUILD)
	$(call CC65,$(CFLAGS) $< -o $(BUILD)/$*.cs,$@)
	ca65 --cpu 65c02 -g $(BUILD)/$*.cs -o $@

$(CPROGRAMS:%=$(BUILD)/%.com): $(BUILD)/%.com: $(BUILD)/%.o $(BUILD)/oodosc.lib cfg/c.cfg tools/rules.py
	$(call LD65,-C cfg/c.cfg $(CLDFLAGS) $< --start-group $(BUILD)/oodosc.lib none.lib --end-group -o $@.tmp -m $(BUILD)/$*.map -Ln $(BUILD)/$*.lbl,$@)
	$(PYTHON) tools/rules.py $@.tmp --map $(BUILD)/$*.map --code STARTUP,ONCE,CODE
	mv $@.tmp $@

# An overlay linked with the library (the Programmer's Guide, chapter 8, "Linking the library into an
# overlay"): each of OVL_LIB, NAME.prl from NAME.o and oolib.lib, with the caller's addresses for the
# zero page names OVL_ZP, from the map of OVL_CALLER's .COM (tools/libzp.py names). Its zero page is
# then its caller's, and its own size 0, as function 59 needs. With OVL_ZP empty, the library's zero
# page modules come with it (test_oodos_playsid's refused overlay).
OVL_LIB    =
OVL_CALLER =
OVL_ZP     =
OVLZPO     = $(if $(OVL_ZP),$(BUILD)/$*.zp.o)
$(OVL_LIB:%=$(BUILD)/%.prl): $(BUILD)/%.prl: $(BUILD)/%.o $(BUILD)/oolib.lib $(BUILD)/$(OVL_CALLER).com cfg/prl.cfg \
		tools/rules.py tools/mkprl.py tools/libzp.py
	$(if $(OVL_ZP),$(PYTHON) tools/libzp.py names $(BUILD)/$(OVL_CALLER).map $(OVL_ZP) -o $(BUILD)/$*.zp.s)
	$(if $(OVL_ZP),ca65 $(AFLAGS) $(BUILD)/$*.zp.s -o $(BUILD)/$*.zp.o)
	$(call LD65,-C cfg/prl.cfg $< $(OVLZPO) $(BUILD)/oolib.lib -o $(BUILD)/$*.a -m $(BUILD)/$*.a.map,$@)
	$(call LD65,-C cfg/prl.cfg -D __OODOS_PAGE__=4 -D __OODOS_ZP__=2 $< $(OVLZPO) $(BUILD)/oolib.lib -o $(BUILD)/$*.b,$@)
	$(call LD65,-C cfg/prl.cfg -D __OODOS_PAGE__=5 -D __OODOS_ZP__=5 $< $(OVLZPO) $(BUILD)/oolib.lib -o $(BUILD)/$*.c,$@)
	$(PYTHON) tools/rules.py $(BUILD)/$*.a --map $(BUILD)/$*.a.map
	$(PYTHON) tools/mkprl.py $(BUILD)/$*.a $(BUILD)/$*.b $(BUILD)/$*.c --map $(BUILD)/$*.a.map -o $@

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
	$(PYTHON) -B test/check_mkasset.py

clean:
	rm -rf $(BUILD)

.PHONY: all utils ooui check clean
.SECONDARY:
