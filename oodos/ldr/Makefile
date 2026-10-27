# OOLDR, /BOOT.BIN on an OODOS card, and the test OODOS.SYS (hw/oodos.md,
# "The system, phase 2"). Needs cc65 (ca65, ld65).
#
#   make            # build/BOOT.BIN (OOLDR) and build/OODOS.SYS (testsys.s)
#
# OOLDR links the CP/M reader, ../sys/cpmread.s, which OODOS.SYS's stand-in
# CCP links too. tools/make_card.sh builds both in its container and puts
# them on the test card. As in ../Makefile, an ld65 warning fails the build.

BUILD  ?= build
AFLAGS = -g -I ../include -I ../../backplane/bios
INCS   = ../include/system.inc ../../backplane/bios/backplane.inc

LD65 = @echo ld65 $(1); ld65 $(1) 2>$(2).ld; s=$$?; w=$$(cat $(2).ld); rm -f $(2).ld; \
	[ -z "$$w" ] || echo "$$w" >&2; [ $$s = 0 ] && [ -z "$$w" ]

all: $(BUILD)/BOOT.BIN $(BUILD)/OODOS.SYS

$(BUILD):
	mkdir -p $@

$(BUILD)/cpmread.o: ../sys/cpmread.s $(INCS) | $(BUILD)
	ca65 $(AFLAGS) $< -o $@

$(BUILD)/%.o: %.s $(INCS) | $(BUILD)
	ca65 $(AFLAGS) $< -o $@

$(BUILD)/BOOT.BIN: $(BUILD)/ooldr.o $(BUILD)/cpmread.o ldr.cfg
	$(call LD65,-C ldr.cfg $(BUILD)/ooldr.o $(BUILD)/cpmread.o -o $@ -m $(BUILD)/ooldr.map -Ln $(BUILD)/ooldr.lbl,$@)

$(BUILD)/OODOS.SYS: $(BUILD)/testsys.o testsys.cfg
	$(call LD65,-C testsys.cfg $< -o $@ -m $(BUILD)/testsys.map,$@)

clean:
	rm -rf $(BUILD)

.PHONY: all clean
