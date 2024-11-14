AFLAGS  =
LFLAGS  =
APPLESOFT_ROM = applesoft.rom
IBASIC_ROM = ibasic.rom

SHARED_OBJS = wozmon.o
APPLESOFT_OBJS = applesoft.o io.o
IBASIC_OBJS    = ibasic.o

roms: $(APPLESOFT_ROM) $(IBASIC_ROM)

$(APPLESOFT_ROM): $(APPLESOFT_OBJS) $(SHARED_OBJS)
	ld65 $(LFLAGS) -C apple1.cfg $(APPLESOFT_OBJS) $(SHARED_OBJS) -o $(APPLESOFT_ROM) -Ln $(APPLESOFT_ROM).lbl

$(IBASIC_ROM): $(IBASIC_OBJS) $(SHARED_OBJS)
	ld65 $(LFLAGS) -C apple1.cfg $(IBASIC_OBJS) $(SHARED_OBJS) -o $(IBASIC_ROM) -Ln $(IBASIC_ROM).lbl

applesoft.o: applesoft.s
	ca65 $(AFLAGS) $<

ibasic.o: ibasic.s
	ca65 $(AFLAGS) $<

wozmon.o: wozmon.s
	ca65 $(AFLAGS) $<

io.o: io.s
	ca65 $(AFLAGS) $<

clean:
	rm $(SHARED_OBJS) $(APPLESOFT_OBJS) $(IBASIC_OBJS) $(APPLESOFT_ROM) $(IBASIC_ROM) *.lbl *.hex

hexdump: roms
	hexdump -v -e '16/1 "0x%02X, "' -e '"\n"' $(APPLESOFT_ROM) > applesoft.hex
	hexdump -v -e '16/1 "0x%02X, "' -e '"\n"' $(IBASIC_ROM) > ibasic.hex