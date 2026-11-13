# GW-BASIC for OODOS (hw/oodos.md, "The plan, phase 7"). Needs cc65 (ca65,
# ld65) and python3.
#
#   make            # BUILD/GWBASIC.COM and BUILD/GWBASIC.OVL, the gates passed
#   make sizes      # the size gate's table alone
#   make test       # BUILD/flat.bin, the flat test image (test/README.md), and
#                   # BUILD/run6502 if a C++20 compiler is there; then
#                   # test/check.py (CHECK=--timing for the cycles) and
#                   # test/programs.py
#
# The resident core is res/*.s, linked as an OODOS .COM (res.cfg); each block
# P1-P8 is pN/*.s, linked at $C800 (blk.cfg) with the resident core's exports
# (tools/gwsym.py) and its header and jump table (tools/gwgen.py, from the
# ";@entry" lines). Every link must give no ld65 warning, every code segment
# must pass OODOS's 65C816 rules (../tools/rules.py), and the size gate
# (tools/gwsize.py) must pass: at least 32 KB free for BASIC, LOWRAM under
# $4000. GWSIZE_MIN raises the gate, to see it fail.

BUILD      ?= build
PYTHON      = python3 -I
GWSIZE_MIN ?= 32768
AFLAGS      = -g -I . -I $(BUILD) -I ../include
BLOCKS      = $(sort $(patsubst %/,%,$(dir $(wildcard p[1-8]/*.s))))
RES_SRC     = $(wildcard res/*.s)
RES_OBJ     = $(RES_SRC:res/%.s=$(BUILD)/res/%.o)
INCS        = gwbasic.inc ../include/oodos.inc $(BUILD)/entries.inc
FARMISS     = $$(awk '/\.FARMISS$$/ {print "0x" $$2; exit}' $(BUILD)/gwbasic.lbl)

LD65 = @echo ld65 $(1); ld65 $(1) 2>$(2).ld; s=$$?; w=$$(cat $(2).ld); rm -f $(2).ld; \
	[ -z "$$w" ] || echo "$$w" >&2; [ $$s = 0 ] && [ -z "$$w" ]

all: $(BUILD)/GWBASIC.COM

$(BUILD) $(BUILD)/res $(addprefix $(BUILD)/,$(BLOCKS)):
	mkdir -p $@

# The entries, whenever a block's source changes (gwgen.py writes only what changed). The headers it
# writes have no recipe of their own, so make doesn't see them change in the run that makes them: each
# header's object depends on entries.inc as well.
$(BUILD)/entries.inc: $(wildcard p[1-8]/*.s) tools/gwgen.py | $(BUILD)
	$(PYTHON) tools/gwgen.py $(BUILD) $(BLOCKS)
$(foreach b,$(BLOCKS),$(BUILD)/$(b)_hdr.s): $(BUILD)/entries.inc

$(BUILD)/res/%.o: res/%.s $(INCS) | $(BUILD)/res
	ca65 $(AFLAGS) $< -o $@

$(BUILD)/gwbasic.res: $(RES_OBJ) res.cfg
	$(call LD65,-C res.cfg $(RES_OBJ) -o $@ -m $(BUILD)/gwbasic.map -vm -Ln $(BUILD)/gwbasic.lbl,$@)
	$(PYTHON) ../tools/rules.py $@ --map $(BUILD)/gwbasic.map --code STARTUP,CODE,LOADER --farcall $(FARMISS) \
	    --lbl $(BUILD)/gwbasic.lbl --inline SYNCHR

$(BUILD)/ressym.s: $(BUILD)/gwbasic.res tools/gwsym.py
	$(PYTHON) tools/gwsym.py $(BUILD)/gwbasic.map -o $@
$(BUILD)/ressym.o: $(BUILD)/ressym.s
	ca65 $(AFLAGS) $< -o $@
$(BUILD)/ressym.lst: $(BUILD)/ressym.s
	awk '/^[A-Za-z_]/ && $$1 != ".export" {printf "%s0x%s", s, substr($$3,2); s=","}' $< > $@

# A block: its header, its modules, the resident core's symbols. Its rules check takes the resident core's
# exports as the addresses outside the block a JSR or JMP may go to.
define BLOCK
$(BUILD)/$(1)/%.o: $(1)/%.s $(INCS) | $(BUILD)/$(1)
	ca65 $$(AFLAGS) -D BLOCK_$(1) $$< -o $$@
$(BUILD)/$(1)_hdr.o: $(BUILD)/$(1)_hdr.s $(BUILD)/entries.inc
	ca65 $$(AFLAGS) $$< -o $$@
$(BUILD)/$(1).bin: $(BUILD)/$(1)_hdr.o $(patsubst $(1)/%.s,$(BUILD)/$(1)/%.o,$(wildcard $(1)/*.s)) $(BUILD)/ressym.o $(BUILD)/ressym.lst blk.cfg
	$$(call LD65,-C blk.cfg $(BUILD)/$(1)_hdr.o $(patsubst $(1)/%.s,$(BUILD)/$(1)/%.o,$(wildcard $(1)/*.s)) $(BUILD)/ressym.o -o $$@ -m $(BUILD)/$(1).map -Ln $(BUILD)/$(1).lbl,$$@)
	$$(PYTHON) ../tools/rules.py $$@ --load 0xC800 --map $(BUILD)/$(1).map --code CODE --farcall $$(FARMISS) \
	    --import $$$$(cat $(BUILD)/ressym.lst) --lbl $(BUILD)/gwbasic.lbl --inline SYNCHR
endef
$(foreach b,$(BLOCKS),$(eval $(call BLOCK,$(b))))

$(BUILD)/GWBASIC.COM: $(BUILD)/gwbasic.res $(BLOCKS:%=$(BUILD)/%.bin) tools/mkgw.py tools/gwsize.py
	$(PYTHON) tools/gwsize.py $(BUILD)/gwbasic.map $(BLOCKS:%=--block $(BUILD)/%.map) --min $(GWSIZE_MIN) --free $(BUILD)/free.txt
	$(PYTHON) tools/mkgw.py --com $(BUILD)/gwbasic.res $(foreach b,$(BLOCKS),--block $(subst p,,$(b)):$(BUILD)/$(b).bin) -o $(BUILD)

sizes: $(BUILD)/gwbasic.res $(BLOCKS:%=$(BUILD)/%.bin)
	$(PYTHON) tools/gwsize.py $(BUILD)/gwbasic.map $(BLOCKS:%=--block $(BUILD)/%.map) --min $(GWSIZE_MIN)

# The flat test image (test/, the project's own): every module, resident and banked, assembled with
# -D FLAT (a far call is a JSR) and linked as one image at $0300 with the math harness first
# (test/flat.cfg), for test/check.py on the emulator's W65C02S core (test/run6502.cpp). ctest's
# test_gwbasic_math builds the runner itself and runs check.py on this image.
FLAT_SRC    = test/harness.s $(RES_SRC) $(wildcard p[1-8]/*.s)
FLAT_OBJ    = $(FLAT_SRC:%.s=$(BUILD)/flat/%.o)
EMU         = ../../emulator/components
CHECK      ?=

$(BUILD)/flat/%.o: %.s $(INCS)
	@mkdir -p $(dir $@)
	ca65 $(AFLAGS) -D FLAT $< -o $@
$(BUILD)/flat.bin: $(FLAT_OBJ) test/flat.cfg
	$(call LD65,-C test/flat.cfg $(FLAT_OBJ) -o $@ -m $(BUILD)/flat.map -Ln $(BUILD)/flat.lbl,$@)
$(BUILD)/run6502: test/run6502.cpp | $(BUILD)
	$(CXX) -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror \
	    -I$(EMU)/cpu6502/include -I$(EMU)/bus/include -I$(EMU)/emu_log/include \
	    test/run6502.cpp $(EMU)/cpu6502/cpu6502.cpp $(EMU)/emu_log/emu_log.cpp -o $@

test: $(BUILD)/flat.bin $(BUILD)/run6502
	$(PYTHON) test/check.py --runner $(BUILD)/run6502 --image $(BUILD)/flat.bin $(CHECK)
	$(PYTHON) test/programs.py --runner $(BUILD)/run6502 --image $(BUILD)/flat.bin

.PHONY: all sizes test
