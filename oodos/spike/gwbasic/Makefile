# The GW-BASIC size spike (README.md). Needs cc65 (ca65, ld65), a C++20
# compiler for the runner, and python3.
#
#   make            # spike.bin, spike.map (the sizes) and spike.lbl
#   make check      # the math vectors and the programs, against mbf.py
#   make sizes      # the 6502 and 8086 sizes, and the extrapolation
#
# The 8086 sizes are read from Microsoft's source, which isn't in this repo:
# point GWSRC at a clone of microsoft/GW-BASIC (make sizes GWSRC=...).

AFLAGS  = -g
OBJS    = harness.o gwmain.o gweval.o bimisc.o math1.o math2.o tables.o stubs.o farcall.o
EMU     = ../../../emulator/components
RUNNER  = run6502
PYTHON  = python3 -I

spike.bin: $(OBJS) spike.cfg
	ld65 -C spike.cfg $(OBJS) -o spike.bin -m spike.map -Ln spike.lbl

%.o: %.s gwbasic.inc
	ca65 $(AFLAGS) $<

$(RUNNER): run6502.cpp
	$(CXX) -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror \
	    -I$(EMU)/cpu6502/include -I$(EMU)/bus/include -I$(EMU)/emu_log/include \
	    run6502.cpp $(EMU)/cpu6502/cpu6502.cpp $(EMU)/emu_log/emu_log.cpp -o $(RUNNER)

check: spike.bin $(RUNNER)
	$(PYTHON) rules.py spike.bin spike.map spike.lbl
	$(PYTHON) check.py

sizes: spike.bin
	@test -n "$(GWSRC)" || { echo "usage: make sizes GWSRC=/path/to/GW-BASIC (a clone of microsoft/GW-BASIC)" >&2; exit 2; }
	$(PYTHON) sizes.py spike.map $(GWSRC)

clean:
	rm -f $(OBJS) spike.bin spike.map spike.lbl $(RUNNER) *.vec *.out

.PHONY: check sizes clean
