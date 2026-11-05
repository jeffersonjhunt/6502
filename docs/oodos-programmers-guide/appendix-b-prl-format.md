# Appendix B: The PRL format

The .PRL file byte by byte: the header, the image, the two relocation bitmaps.

The design record, for the reasons: [hw/oodos.md, "The .PRL file"](../../hw/oodos.md#the-prl-file). How a `.PRL` is
made and what it is for: [chapter 6](06-program-formats.md#prl-files). Function 59 loads one as an overlay, and the
loader loads RSXs, `.PRL`s with flag bit 1 too ([chapter 6](06-program-formats.md#rsxs)).

## The layout

The file is records of 128 bytes. With `size` the image's size in bytes (header offset 6) and ⌈ ⌉ rounding up:

| Record | Bytes | What |
|---|---|---|
| 0 | 16 | the header, as a `.COM`'s ([chapter 6](06-program-formats.md#com-files)), with flag bit 0 set |
| 0 | size | the image, from byte 16, linked at the link page (offset 5) with its zero page at the zero page base (offset 10); zeros to the end of its last record |
| p = ⌈(16 + size) / 128⌉ | ⌈size / 8⌉ | the page bitmap; zeros to the end of its last record |
| p + ⌈⌈size / 8⌉ / 128⌉ | ⌈size / 8⌉ | the zero page bitmap; zeros to the end of its last record |

An attached RSX table (gencom.py, GENCOM) follows a `.COM`'s image on a later record, the one header offset 14 names
([chapter 6](06-program-formats.md#rsxs)).

## The bitmaps

Each has a bit for each byte of the image: the image's first byte is bit 7 of the bitmap's first byte, its eighth
byte bit 0, its ninth bit 7 of the second. A record of a bitmap covers 1,024 bytes of the image. Bits past the
image's last byte are padding and mean nothing.

- **The page bitmap:** a 1 marks a byte that is the high byte of an address in the image.
- **The zero page bitmap:** a 1 marks a byte that is the address of a byte in the image's own `ZEROPAGE` segment.

A byte is marked in one bitmap or in neither.

## Loading at page P, zero page Z

1. The header is checked: the magic number, no flag bit the loader doesn't know, and the image and BSS below the
   top of the TPA from `P` × 256. P must be `$03` or above.
2. The image is read to `P` × 256 as a `.COM`'s is: exactly its size, nothing past it.
3. The page bitmap is read a record at a time, and P minus the link page added to each byte it marks.
4. The zero page bitmap the same way, adding Z minus the zero page base.

Both sums are modulo 256. For a program Z is `$00`, and its zero page must end at or below `$F0`. An RSX gets a
slice of `$F0-$FF` and may need 16 bytes at most. An overlay has no zero page: its size must be 0.

## A worked case

A 300-byte image: the image takes bytes 16-315, records 0-2 (⌈316 / 128⌉ = 3), so p = 3. Each bitmap is ⌈300 / 8⌉ =
38 bytes, one record: the page bitmap is record 3, the zero page bitmap record 4. The file is 5 records, 640
bytes. Loaded at page `$80` from a link page of `$03`, each byte the page bitmap marks gains `$7D`.

[`oodos/tools/mkprl.py`](../../oodos/tools/mkprl.py) makes the file and, with `--load`, loads one by these steps,
with the loader's refusals.
