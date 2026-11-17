# The backplane BIOS: a full 512 KB ROM A image (bios.rom) with the BIOS
# and Woz Monitor in its top 16 KB, the font (font/) in block 30 and the
# video demo (demo.s, demo/) in block 29. Needs cc65 (ca65, ld65); the
# demo's graphics are made by demo/make_assets.py, and committed. The boot
# splash's sound, its two players in the top block (splash/bootsnd.bin for
# the SIDs, splash/docsnd.bin for the ES5503) and the ES5503's samples in
# blocks 24-28 (splash/wave.bin, committed), are assembled here from
# splash/bootsnd.s and splash/docsnd.s; `make splash` remakes their tables,
# the samples, the video card's splash ROM and the preview (splash/README.md).
#
#   make            # bios.rom and bios.rom.lbl (labels for --symbols), bios-even.rom and bios-odd.rom
#   ../../emulator/build/release/apps/emu/emulator --machine ../../emulator/machines/backplane.machine bios.rom

AFLAGS  = -g  # debug info, so -Ln lists every label, not just the exports
OBJS    = bios.o wozmon.o video.o input.o apple.o sd.o demo.o splash.o flash.o diag21.o diag22.o diag23.o
# The ROM's diagnostics (hw/diag.md, plan 2) take oohw's modules, built for the ROM (OOHW_ROM): each block its own
# copy of those it uses, block 23's under the plain names, block 22's and 21's with their number in each name
# (OOHW_BLK, oohw.inc), so the one link holds all three. Block 22 takes the sound card's 6510 program whole.
OOHW    = ../oohw
HWOBJS  = hw-hwmem.o hw-hwzp.o \
          hw22-hwprobe.o hw22-hwclock.o hw22-hwfmt.o hw22-hwmem.o hw22-hwport.o hw22-hwzp.o \
          hw21-hwprobe.o hw21-hwclock.o hw21-hwfmt.o hw21-hwsdid.o hw21-hwzp.o
ROM     = bios.rom
CHIPS   = bios-even.rom bios-odd.rom

# Linked to a temporary file, then the flash's sums written (romsum.py), then renamed: a failed step leaves no bios.rom.
# Then ROM A's two chips, a file each for a programmer: the even bytes and the odd (romsplit.py).
.DELETE_ON_ERROR:
all: $(ROM) $(CHIPS)

$(ROM): $(OBJS) $(HWOBJS) bios.cfg romsum.py
	ld65 -C bios.cfg $(OBJS) $(HWOBJS) -o $(ROM).tmp -Ln $(ROM).lbl
	python3 romsum.py $(ROM).tmp $(ROM).lbl
	mv $(ROM).tmp $(ROM)

bios-odd.rom: bios-even.rom
bios-even.rom: $(ROM) romsplit.py
	python3 romsplit.py $(ROM) bios-even.rom bios-odd.rom

%.o: %.s backplane.inc diag.inc diagcon.inc diagtest.inc $(OOHW)/oohw.inc font/vga8x16.bin $(wildcard demo/*.bin) splash/bootsnd.bin splash/docsnd.bin splash/wave.bin
	ca65 $(AFLAGS) -I $(OOHW) $<

hw-%.o: $(OOHW)/%.s $(OOHW)/oohw.inc backplane.inc
	ca65 $(AFLAGS) -I . -I $(OOHW) -D OOHW_ROM $< -o $@

hw22-%.o: $(OOHW)/%.s $(OOHW)/oohw.inc backplane.inc
	ca65 $(AFLAGS) -I . -I $(OOHW) -D OOHW_ROM -D OOHW_BLK=22 $< -o $@

hw21-%.o: $(OOHW)/%.s $(OOHW)/oohw.inc backplane.inc
	ca65 $(AFLAGS) -I . -I $(OOHW) -D OOHW_ROM -D OOHW_BLK=21 $< -o $@

diag22.o: card6510.bin

card6510.bin: $(OOHW)/card6510.s $(OOHW)/card6510.cfg $(OOHW)/oohw.inc
	ca65 -I $(OOHW) $(OOHW)/card6510.s -o card6510.o
	ld65 -C $(OOHW)/card6510.cfg card6510.o -o $@
	rm card6510.o

splash/bootsnd.bin: splash/bootsnd.s splash/tables.inc splash/bootsnd.cfg
	cd splash && ca65 bootsnd.s -o bootsnd.o && ld65 -C bootsnd.cfg bootsnd.o -o bootsnd.bin && rm bootsnd.o

splash/docsnd.bin: splash/docsnd.s splash/doctables.inc splash/bootsnd.cfg
	cd splash && ca65 docsnd.s -o docsnd.o && ld65 -C bootsnd.cfg docsnd.o -o docsnd.bin && rm docsnd.o

splash:
	$(MAKE) -C splash

clean:
	rm -f $(OBJS) $(HWOBJS) $(ROM) $(ROM).lbl $(CHIPS) card6510.bin

.PHONY: all clean splash
