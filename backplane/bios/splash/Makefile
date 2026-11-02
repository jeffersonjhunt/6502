# The boot splash's assets (README.md). Everything made here is committed, so
# the BIOS and the emulator build without Python; run `make` after changing
# the logo, the motion or the sound. Needs python3 (numpy, Pillow) and cc65.
# `make preview` also needs py65, pyresidfp and ffmpeg.

HEADER = ../../../emulator/components/devices/splash_rom.h

all: splash.bin $(HEADER) bootsnd.bin

splash.bin splash.mem $(HEADER): splashgen.py stamp.py OOE_Logo_WHT_1920-1080.png $(wildcard fonts/*.TTF)
	python3 splashgen.py OOE_Logo_WHT_1920-1080.png --header $(HEADER)

tables.inc: soundgen.py splashgen.py
	python3 soundgen.py tables.inc

bootsnd.bin: bootsnd.s bootsnd.cfg tables.inc
	ca65 bootsnd.s -o bootsnd.o && ld65 -C bootsnd.cfg bootsnd.o -o bootsnd.bin && rm bootsnd.o

# preview/: bootsnd.sid (stereo PSID), splash.wav and splash.mp4, as the card
# draws the ROM and the tune runs on two 8580s. Not committed.
preview: splash.bin bootsnd.bin
	python3 render.py splash.bin bootsnd.bin preview

.PHONY: all preview
