# The boot splash's assets (README.md). Everything made here is committed, so
# the BIOS and the emulator build without Python; run `make` after changing
# the logo, the motion or the sound. Needs python3 (numpy, Pillow) and cc65.
# `make preview` also needs py65, pyresidfp and ffmpeg.
#
# The sound twice over: the SIDs' (bootsnd, revision 1 of the sound card) and
# the ES5503's (docsnd, its samples in wave.bin; revision 2).

HEADER = ../../../emulator/components/devices/splash_rom.h

all: splash.bin $(HEADER) bootsnd.bin docsnd.bin wave.bin

splash.bin splash.mem $(HEADER): splashgen.py stamp.py OOE_Logo_WHT_1920-1080.png $(wildcard fonts/*.TTF)
	python3 splashgen.py OOE_Logo_WHT_1920-1080.png --header $(HEADER)

tables.inc: soundgen.py splashgen.py
	python3 soundgen.py tables.inc

bootsnd.bin: bootsnd.s bootsnd.cfg tables.inc
	ca65 bootsnd.s -o bootsnd.o && ld65 -C bootsnd.cfg bootsnd.o -o bootsnd.bin && rm bootsnd.o

wave.bin: docsamples.py
	python3 docsamples.py -o wave.bin

doctables.inc: docgen.py docsamples.py soundgen.py splashgen.py
	python3 docgen.py doctables.inc

docsnd.bin: docsnd.s bootsnd.cfg doctables.inc
	ca65 docsnd.s -o docsnd.o && ld65 -C bootsnd.cfg docsnd.o -o docsnd.bin && rm docsnd.o

# preview/: bootsnd.sid (stereo PSID), splash.wav and splash.mp4, as the card
# draws the ROM and the tune runs on two 8580s (revision 1), and splash_doc.wav
# and splash_doc.mp4 with the ES5503's (revision 2). Not committed.
preview: splash.bin bootsnd.bin docsnd.bin wave.bin
	python3 render.py splash.bin bootsnd.bin preview --doc docsnd.bin wave.bin

.PHONY: all preview
