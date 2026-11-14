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
#   make            # bios.rom and bios.rom.lbl (labels for --symbols)
#   ../../emulator/build/release/apps/emu/emulator --machine ../../emulator/machines/backplane.machine bios.rom

AFLAGS  = -g  # debug info, so -Ln lists every label, not just the exports
OBJS    = bios.o wozmon.o video.o input.o apple.o sd.o demo.o splash.o diag21.o diag22.o diag23.o
# The ROM's diagnostics (hw/diag.md, plan 2) take oohw's memory tests, built for the ROM (OOHW_ROM): block 23's
OOHW    = ../oohw
HWOBJS  = hw-hwmem.o hw-hwzp.o
ROM     = bios.rom

$(ROM): $(OBJS) $(HWOBJS) bios.cfg romsum.py
	ld65 -C bios.cfg $(OBJS) $(HWOBJS) -o $(ROM) -Ln $(ROM).lbl
	python3 romsum.py $(ROM)

%.o: %.s backplane.inc diag.inc diagcon.inc $(OOHW)/oohw.inc font/vga8x16.bin $(wildcard demo/*.bin) splash/bootsnd.bin splash/docsnd.bin splash/wave.bin
	ca65 $(AFLAGS) -I $(OOHW) $<

hw-%.o: $(OOHW)/%.s $(OOHW)/oohw.inc backplane.inc
	ca65 $(AFLAGS) -I . -I $(OOHW) -D OOHW_ROM $< -o $@

splash/bootsnd.bin: splash/bootsnd.s splash/tables.inc splash/bootsnd.cfg
	cd splash && ca65 bootsnd.s -o bootsnd.o && ld65 -C bootsnd.cfg bootsnd.o -o bootsnd.bin && rm bootsnd.o

splash/docsnd.bin: splash/docsnd.s splash/doctables.inc splash/bootsnd.cfg
	cd splash && ca65 docsnd.s -o docsnd.o && ld65 -C bootsnd.cfg docsnd.o -o docsnd.bin && rm docsnd.o

splash:
	$(MAKE) -C splash

clean:
	rm -f $(OBJS) $(HWOBJS) $(ROM) $(ROM).lbl

.PHONY: clean splash
