
# compile wozmon
ca65 wozmon.s
ld65 -C apple1.cfg wozmon.o

# Convert a.out file to C Array
hexdump -v -e '16/1 "0x%02X, "' -e '"\n"' a.out > hexarray.h
echo "#pragma once\n\nuint8_t WOZMON[] = {" > wozmon.h
cat hexarray.h | tail -16 >> wozmon.h
echo "};\n" >> wozmon.h

# Clean up
rm a.out
rm hexarray.h
rm wozmon.o
