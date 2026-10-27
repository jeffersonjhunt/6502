# OODOS.SYS (hw/oodos.md, "The system, phase 2") and its test programs.
# Needs cc65 (ca65, ld65) and python3.
#
#   make            # build/OODOS.SYS, and build/NAME.com for each of PROGRAMS
#   make sizes      # each segment and how full its block is
#   make check      # mksys.py's tests (test/check_mksys.py)
#
# The system is one ld65 link (sys.cfg) whose parts are files of their own,
# one a block: mksys.py makes OODOS.SYS of them, each at its physical
# address. The test programs are .COMs, linked with ../cfg/com.cfg and
# checked against the 65C816 rules as ../Makefile's are. Any ld65 warning
# fails the build. emulator/test/host/CMakeLists.txt runs this for ctest's
# test_oodos_sys, with BUILD in the build tree.

BUILD    ?= build
PYTHON   = python3 -I
SYSOBJS  = common bdos bios crt cold ccp cpmread
PROGRAMS = hello biostest stubtest fartest timetest irqtest vt52test linetest wptest banktest
AFLAGS   = -g -I . -I ../include -I ../../backplane/bios
PFLAGS   = -g -I test -I ../include

# Where each part goes: block 60, 61, then block 62 from $C800 and $F800.
PARTS = 0xF0000:$(BUILD)/oodos.bdos 0xF4000:$(BUILD)/oodos.bios \
        0xF8800:$(BUILD)/oodos.res 0xFB800:$(BUILD)/oodos.common

LD65 = @echo ld65 $(1); ld65 $(1) 2>$(2).ld; s=$$?; w=$$(cat $(2).ld); rm -f $(2).ld; \
	[ -z "$$w" ] || echo "$$w" >&2; [ $$s = 0 ] && [ -z "$$w" ]

INCS = sys.inc ../include/system.inc ../../backplane/bios/backplane.inc

all: $(BUILD)/OODOS.SYS $(PROGRAMS:%=$(BUILD)/%.com)

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: %.s $(INCS) | $(BUILD)
	ca65 $(AFLAGS) $< -o $@

$(BUILD)/OODOS.SYS: $(SYSOBJS:%=$(BUILD)/%.o) sys.cfg mksys.py
	$(call LD65,-C sys.cfg $(SYSOBJS:%=$(BUILD)/%.o) -o $(BUILD)/oodos -m $(BUILD)/oodos.map -Ln $(BUILD)/oodos.lbl,$@)
	$(PYTHON) mksys.py -o $@ --entry COLD --labels $(BUILD)/oodos.lbl $(PARTS)

$(BUILD)/hello.p.o: ../examples/hello.s ../include/oodos.inc | $(BUILD)
	ca65 $(PFLAGS) $< -o $@

$(BUILD)/%.p.o: test/%.s test/print.inc test/printzp.inc ../include/oodos.inc ../include/system.inc | $(BUILD)
	ca65 $(PFLAGS) $< -o $@

$(BUILD)/%.com: $(BUILD)/%.p.o ../cfg/com.cfg ../tools/rules.py
	$(call LD65,-C ../cfg/com.cfg $< -o $@.tmp -m $(BUILD)/$*.map -Ln $(BUILD)/$*.lbl,$@)
	$(PYTHON) ../tools/rules.py $@.tmp --map $(BUILD)/$*.map
	mv $@.tmp $@

sizes: $(BUILD)/OODOS.SYS
	$(PYTHON) mksys.py --check $<

check: $(BUILD)/OODOS.SYS
	$(PYTHON) test/check_mksys.py $(BUILD)

clean:
	rm -rf $(BUILD)

.PHONY: all sizes check clean
.SECONDARY:
