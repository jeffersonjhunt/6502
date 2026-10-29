# DRIVER.COM, the model test's driver (hw/oodos.md, "The BDOS, phase 3";
# ../modelscript.py has the script and result formats). Needs cc65 (ca65,
# ld65) and python3.
#
#   make            # build/driver.com
#   make check      # the model's own tests (../test_model.py)
#
# A .COM linked with ../../cfg/com.cfg and checked against the 65C816 rules
# (../../tools/rules.py), as ../../Makefile's programs are; any ld65 warning
# fails the build. emulator/test/host/CMakeLists.txt runs this for ctest's
# test_oodos_model, with BUILD in the build tree.

BUILD    ?= build
PYTHON   = python3 -I
AFLAGS   = -g -I ../../include

LD65 = @echo ld65 $(1); ld65 $(1) 2>$(2).ld; s=$$?; w=$$(cat $(2).ld); rm -f $(2).ld; \
	[ -z "$$w" ] || echo "$$w" >&2; [ $$s = 0 ] && [ -z "$$w" ]

all: $(BUILD)/driver.com

$(BUILD):
	mkdir -p $@

$(BUILD)/driver.o: driver.s ../../include/oodos.inc | $(BUILD)
	ca65 $(AFLAGS) $< -o $@

$(BUILD)/driver.com: $(BUILD)/driver.o ../../cfg/com.cfg ../../tools/rules.py
	$(call LD65,-C ../../cfg/com.cfg $< -o $@.tmp -m $(BUILD)/driver.map -Ln $(BUILD)/driver.lbl,$@)
	$(PYTHON) ../../tools/rules.py $@.tmp --map $(BUILD)/driver.map
	mv $@.tmp $@

check:
	$(PYTHON) ../test_model.py

clean:
	rm -rf $(BUILD)

.PHONY: all check clean
.SECONDARY:
