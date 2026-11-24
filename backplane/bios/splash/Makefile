# The boot splash's assets (README.md). Everything made here is committed, so
# the BIOS and the emulator build without Python; run `make` after changing
# the logo, the motion or the sound. Needs python3 (numpy, Pillow).
# `make preview` also needs ffmpeg.
#
# The sound is the sound card's (revision 3): its firmware plays it from the
# card's flash. Its samples (wave.bin) and its sequence's tables
# (doctables.inc) are made here into tools/sndflash/, from which
# tools/mksndflash.py makes the emulator's copy of that flash (sound_flash.h).

HEADER = ../../../emulator/components/devices/splash_rom.h
FLASH = ../../../tools/sndflash
SNDHEADER = ../../../emulator/components/devices/include/sound_flash.h

all: splash.bin $(HEADER) $(SNDHEADER)

splash.bin splash.mem $(HEADER): splashgen.py stamp.py OOE_Logo_WHT_1920-1080.png $(wildcard fonts/*.TTF)
	python3 splashgen.py OOE_Logo_WHT_1920-1080.png --header $(HEADER)

$(FLASH)/wave.bin: docsamples.py
	python3 docsamples.py -o $(FLASH)/wave.bin

$(FLASH)/doctables.inc: docgen.py docsamples.py soundgen.py splashgen.py
	python3 docgen.py $(FLASH)/doctables.inc

$(SNDHEADER): $(FLASH)/wave.bin $(FLASH)/doctables.inc ../../../tools/mksndflash.py
	python3 ../../../tools/mksndflash.py

# preview/splash.mp4: the splash ROM drawn by the reference model (splashgen.Rom), silent. Not committed.
preview: splash.bin
	python3 render.py splash.bin preview

.PHONY: all preview
