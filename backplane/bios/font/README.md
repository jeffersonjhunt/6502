# Font

`vga8x16.bin` is the BIOS's text font: the 256 characters of code page 437
(the IBM PC's, box drawing included), 16 bytes each, the leftmost pixel in
bit 7, as the video card's text layer reads it (`hw/design.md`, "Formats in
video memory"). The BIOS copies it from flash block 30 into video memory at
boot.

It is **IBM VGA 8x16** from **The Ultimate Oldschool PC Font Pack** by
**VileR** (<https://int10h.org/oldschool-pc-fonts/>), v2.2, converted from
`Bm437_IBM_VGA_8x16.FON` by `fon2bin.py` with no change to the glyphs:

```sh
python3 fon2bin.py "fon - Bm (windows bitmap)/Bm437_IBM_VGA_8x16.FON" vga8x16.bin
```

## License

Unlike the rest of this repository (MIT), `vga8x16.bin` is licensed under
the **Creative Commons Attribution-ShareAlike 4.0 International License**
([LICENSE-CC-BY-SA-4.0.txt](LICENSE-CC-BY-SA-4.0.txt), from the font pack),
as the font pack is. The original IBM bitmaps are credited to their
designers; the pack's author claims no rights in them. Anything that
redistributes the font (`bios.rom`, the emulator's web build) carries it
under that license, with this credit. `fon2bin.py` is MIT, like the rest.
