
# compile wozmon
ca65 wozmon.s 
ca65 basic.s
ld65 -C apple1.cfg wozmon.o basic.o -o apple1.rom

# Convert a.out file to C Array
hexdump -v -e '16/1 "0x%02X, "' -e '"\n"' apple1.rom > hexarray.h
echo "#pragma once\n\nstatic const uint8_t ROM[] = {" > rom.h
cat hexarray.h >> rom.h
echo "};\n" >> rom.h

# Clean up
rm *.o
rm hexarray.h
#rm apple1.rom
