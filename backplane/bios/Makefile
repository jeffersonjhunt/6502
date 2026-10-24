# The backplane BIOS: a full 512 KB ROM A image (bios.rom) with the BIOS
# and Woz Monitor in its top 16 KB, the font (font/) in block 30 and the
# video demo (demo.s, demo/) in block 29. Needs cc65 (ca65, ld65); the
# demo's graphics are made by demo/make_assets.py, and committed.
#
#   make            # bios.rom and bios.rom.lbl (labels for --symbols)
#   ../../emulator/build/release/apps/emu/emulator --machine ../../emulator/machines/backplane.machine bios.rom

AFLAGS  = -g  # debug info, so -Ln lists every label, not just the exports
OBJS    = bios.o wozmon.o video.o demo.o
ROM     = bios.rom

$(ROM): $(OBJS) bios.cfg
	ld65 -C bios.cfg $(OBJS) -o $(ROM) -Ln $(ROM).lbl

%.o: %.s backplane.inc font/vga8x16.bin $(wildcard demo/*.bin)
	ca65 $(AFLAGS) $<

clean:
	rm -f $(OBJS) $(ROM) $(ROM).lbl

.PHONY: clean
