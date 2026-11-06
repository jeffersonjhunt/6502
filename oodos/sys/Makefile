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
SYSOBJS  = common bdos bdosfile bdosdisk bdosxtra bdossys bdosmem bios disk2 crt cold ccp loader
PROGRAMS = hello args biostest stubtest fartest timetest irqtest vt52test linetest wptest banktest holetest memtest \
           fstest dirtest errtest stamptest logintest ruletest xtratest systest writeoo1 cmdtest uttest \
           rsxq ovlt iotest
# Phase 5's (test_oodos_rsx): test RSXs, overlays, RSXQ with RSXs attached by
# gencom.py, and mkprl.py's loads of them where the loader will put them.
RSXS     = trsxa trsxb trsxc trsxd trsxe
OVLS     = ovl ovlzp
QS       = q01 q02 q03 q04 q05 q06 q07 q08 q09 q10 q11 q12 q13 q14 q15
GENCOMS  = rqa rqab rqc rqd rqbe rqn rqs rq15 iota iotab iots iotn iotx
IMAGES   = trsxa.bf trsxb.be trsxb.bf ovl.6000 ovl.7100
GENCOM   = $(PYTHON) ../tools/gencom.py
AFLAGS   = -g -I . -I ../include -I ../../backplane/bios
PFLAGS   = -g -I test -I ../include

# Where each part goes: block 60, 61, then block 62 from $C800 and $F800.
PARTS = 0xF0000:$(BUILD)/oodos.bdos 0xF4000:$(BUILD)/oodos.bios \
        0xF8800:$(BUILD)/oodos.res 0xFB800:$(BUILD)/oodos.common

LD65 = @echo ld65 $(1); ld65 $(1) 2>$(2).ld; s=$$?; w=$$(cat $(2).ld); rm -f $(2).ld; \
	[ -z "$$w" ] || echo "$$w" >&2; [ $$s = 0 ] && [ -z "$$w" ]

INCS = sys.inc bdos.inc disk2.inc ../include/cmd.inc ../include/oodos.inc ../include/system.inc ../../backplane/bios/backplane.inc

all: $(BUILD)/OODOS.SYS $(PROGRAMS:%=$(BUILD)/%.com) $(RSXS:%=$(BUILD)/%.rsx) $(OVLS:%=$(BUILD)/%.prl) \
     $(GENCOMS:%=$(BUILD)/%.com) $(IMAGES:%=$(BUILD)/%.img)

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: %.s $(INCS) | $(BUILD)
	ca65 $(AFLAGS) $< -o $@

$(BUILD)/OODOS.SYS: $(SYSOBJS:%=$(BUILD)/%.o) sys.cfg mksys.py ../include/system.inc
	$(call LD65,-C sys.cfg $(SYSOBJS:%=$(BUILD)/%.o) -o $(BUILD)/oodos -m $(BUILD)/oodos.map -Ln $(BUILD)/oodos.lbl,$@)
	$(PYTHON) mksys.py -o $@ --entry COLD --labels $(BUILD)/oodos.lbl $(PARTS)

$(BUILD)/hello.p.o: ../examples/hello.s ../include/oodos.inc | $(BUILD)
	ca65 $(PFLAGS) $< -o $@

$(BUILD)/%.p.o: test/%.s test/print.inc test/printzp.inc test/fs.inc test/fszp.inc test/trsx.inc ../include/oodos.inc ../include/system.inc ../include/cmd.inc ../include/util.inc | $(BUILD)
	ca65 $(PFLAGS) $< -o $@

$(BUILD)/%.com: $(BUILD)/%.p.o ../cfg/com.cfg ../tools/rules.py
	$(call LD65,-C ../cfg/com.cfg $< -o $@.tmp -m $(BUILD)/$*.map -Ln $(BUILD)/$*.lbl,$@)
	$(PYTHON) ../tools/rules.py $@.tmp --map $(BUILD)/$*.map
	mv $@.tmp $@

# An RSX and an overlay: three links a page and a zero page apart, as ../Makefile's .PRLs.
$(BUILD)/%.rsx: $(BUILD)/%.p.o ../cfg/rsx.cfg ../tools/rules.py ../tools/mkprl.py
	$(call LD65,-C ../cfg/rsx.cfg $< -o $(BUILD)/$*.ra -m $(BUILD)/$*.ra.map,$@)
	$(call LD65,-C ../cfg/rsx.cfg -D __OODOS_PAGE__=4 -D __OODOS_ZP__=2 $< -o $(BUILD)/$*.rb,$@)
	$(call LD65,-C ../cfg/rsx.cfg -D __OODOS_PAGE__=5 -D __OODOS_ZP__=5 $< -o $(BUILD)/$*.rc,$@)
	$(PYTHON) ../tools/rules.py $(BUILD)/$*.ra --map $(BUILD)/$*.ra.map
	$(PYTHON) ../tools/mkprl.py $(BUILD)/$*.ra $(BUILD)/$*.rb $(BUILD)/$*.rc --map $(BUILD)/$*.ra.map -o $@
$(BUILD)/%.prl: $(BUILD)/%.p.o ../cfg/prl.cfg ../tools/rules.py ../tools/mkprl.py
	$(call LD65,-C ../cfg/prl.cfg $< -o $(BUILD)/$*.pa -m $(BUILD)/$*.pa.map,$@)
	$(call LD65,-C ../cfg/prl.cfg -D __OODOS_PAGE__=4 -D __OODOS_ZP__=2 $< -o $(BUILD)/$*.pb,$@)
	$(call LD65,-C ../cfg/prl.cfg -D __OODOS_PAGE__=5 -D __OODOS_ZP__=5 $< -o $(BUILD)/$*.pc,$@)
	$(PYTHON) ../tools/rules.py $(BUILD)/$*.pa --map $(BUILD)/$*.pa.map
	$(PYTHON) ../tools/mkprl.py $(BUILD)/$*.pa $(BUILD)/$*.pb $(BUILD)/$*.pc --map $(BUILD)/$*.pa.map -o $@
$(BUILD)/q%.rsx: $(BUILD)/trsxa.rsx
	cp $< $@

# RSXQ with RSXs: A; A then B (B first in the chain); C (remove flag $FF); A
# and D (non-banked); B and E (20 bytes of zero page); C alone (RSX-only);
# SCB entries (console mode's high byte, a user flag); fifteen As.
$(BUILD)/rqa.com: $(BUILD)/rsxq.com $(BUILD)/trsxa.rsx ../tools/gencom.py
	$(GENCOM) $(BUILD)/rsxq.com $(BUILD)/trsxa.rsx -o $@
$(BUILD)/rqab.com: $(BUILD)/rsxq.com $(BUILD)/trsxa.rsx $(BUILD)/trsxb.rsx ../tools/gencom.py
	$(GENCOM) $(BUILD)/rsxq.com $(BUILD)/trsxa.rsx $(BUILD)/trsxb.rsx -o $@
$(BUILD)/rqc.com: $(BUILD)/rsxq.com $(BUILD)/trsxc.rsx ../tools/gencom.py
	$(GENCOM) $(BUILD)/rsxq.com $(BUILD)/trsxc.rsx -o $@
$(BUILD)/rqd.com: $(BUILD)/rsxq.com $(BUILD)/trsxa.rsx $(BUILD)/trsxd.rsx ../tools/gencom.py
	$(GENCOM) $(BUILD)/rsxq.com $(BUILD)/trsxa.rsx $(BUILD)/trsxd.rsx -o $@
$(BUILD)/rqbe.com: $(BUILD)/rsxq.com $(BUILD)/trsxb.rsx $(BUILD)/trsxe.rsx ../tools/gencom.py
	$(GENCOM) $(BUILD)/rsxq.com $(BUILD)/trsxb.rsx $(BUILD)/trsxe.rsx -o $@
$(BUILD)/rqn.com: $(BUILD)/trsxc.rsx ../tools/gencom.py
	$(GENCOM) --null $(BUILD)/trsxc.rsx -o $@
$(BUILD)/rqs.com: $(BUILD)/rsxq.com ../tools/gencom.py
	$(GENCOM) $(BUILD)/rsxq.com --scb 34,02 --scb 06,5A -o $@
$(BUILD)/rq15.com: $(BUILD)/rsxq.com $(QS:%=$(BUILD)/%.rsx) ../tools/gencom.py
	$(GENCOM) $(BUILD)/rsxq.com $(QS:%=$(BUILD)/%.rsx) -o $@

# GENCOM.COM's expected files (test_oodos_utils_rsx): IOTEST with A; A then B; SCB
# entries; TRSXA alone ([NULL]); IOTEST stripped again (what GENCOM name gives).
$(BUILD)/iota.com: $(BUILD)/iotest.com $(BUILD)/trsxa.rsx ../tools/gencom.py
	$(GENCOM) $(BUILD)/iotest.com $(BUILD)/trsxa.rsx -o $@
$(BUILD)/iotab.com: $(BUILD)/iota.com $(BUILD)/trsxb.rsx ../tools/gencom.py
	$(GENCOM) $(BUILD)/iota.com $(BUILD)/trsxb.rsx -o $@
$(BUILD)/iots.com: $(BUILD)/iotest.com ../tools/gencom.py
	$(GENCOM) $(BUILD)/iotest.com --scb 34,02 --scb 06,5A -o $@
$(BUILD)/iotn.com: $(BUILD)/trsxa.rsx ../tools/gencom.py
	$(GENCOM) --null $(BUILD)/trsxa.rsx -o $@
$(BUILD)/iotx.com: $(BUILD)/iota.com ../tools/gencom.py
	$(GENCOM) --strip $(BUILD)/iota.com -o $@

# mkprl.py's loads: NAME.PAGE.img, an RSX at that page and $F0, an overlay at that address.
$(BUILD)/%.bf.img: $(BUILD)/%.rsx ../tools/mkprl.py
	$(PYTHON) ../tools/mkprl.py --load $< --rsx --page 0xBF --zp 0xF0 -o $@
$(BUILD)/%.be.img: $(BUILD)/%.rsx ../tools/mkprl.py
	$(PYTHON) ../tools/mkprl.py --load $< --rsx --page 0xBE --zp 0xF0 -o $@
$(BUILD)/ovl.6000.img: $(BUILD)/ovl.prl ../tools/mkprl.py
	$(PYTHON) ../tools/mkprl.py --load $< --overlay 0x6000 -o $@
$(BUILD)/ovl.7100.img: $(BUILD)/ovl.prl ../tools/mkprl.py
	$(PYTHON) ../tools/mkprl.py --load $< --overlay 0x7100 -o $@

sizes: $(BUILD)/OODOS.SYS
	$(PYTHON) mksys.py --check $<

check: $(BUILD)/OODOS.SYS
	$(PYTHON) test/check_mksys.py $(BUILD)

clean:
	rm -rf $(BUILD)

.PHONY: all sizes check clean
.SECONDARY:
