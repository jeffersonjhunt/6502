# The backplane BIOS: a full 512 KB ROM A image (bios.rom) with the BIOS
# and Woz Monitor in its top 16 KB, the font (font/) in block 30 and the
# video demo (demo.s, demo/) in block 29. Needs cc65 (ca65, ld65); the
# demo's graphics are made by demo/make_assets.py, and committed. The boot
# splash's sound is the sound card's (revision 3 plays it from its own flash,
# on one record); `make splash` remakes the video card's splash ROM, the
# sound's samples and tables for the card's flash, and the preview
# (splash/README.md).
#
#   make            # bios.rom and bios.rom.lbl (labels for --symbols), bios-even.rom and bios-odd.rom
#   ../../emulator/build/release/apps/emu/emulator --machine ../../emulator/machines/backplane.machine bios.rom

AFLAGS  = -g  # debug info, so -Ln lists every label, not just the exports
OBJS    = bios.o wozmon.o video.o input.o apple.o sd.o demo.o splash.o flash.o diag21.o diag22.o diag23.o
# The ROM's diagnostics (hw/diag.md, plan 2) take oohw's modules, built for the ROM (OOHW_ROM): each block its own
# copy of those it uses, block 23's under the plain names, block 22's and 21's with their number in each name
# (OOHW_BLK, oohw.inc), so the one link holds all three.
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

%.o: %.s backplane.inc diag.inc diagcon.inc diagtest.inc diagsnd.inc $(OOHW)/oohw.inc font/vga8x16.bin $(wildcard demo/*.bin)
	ca65 $(AFLAGS) -I $(OOHW) $<

hw-%.o: $(OOHW)/%.s $(OOHW)/oohw.inc backplane.inc
	ca65 $(AFLAGS) -I . -I $(OOHW) -D OOHW_ROM $< -o $@

hw22-%.o: $(OOHW)/%.s $(OOHW)/oohw.inc backplane.inc
	ca65 $(AFLAGS) -I . -I $(OOHW) -D OOHW_ROM -D OOHW_BLK=22 $< -o $@

hw21-%.o: $(OOHW)/%.s $(OOHW)/oohw.inc backplane.inc
	ca65 $(AFLAGS) -I . -I $(OOHW) -D OOHW_ROM -D OOHW_BLK=21 $< -o $@

splash:
	$(MAKE) -C splash

clean:
	rm -f $(OBJS) $(HWOBJS) $(ROM) $(ROM).lbl $(CHIPS)

.PHONY: all clean splash
