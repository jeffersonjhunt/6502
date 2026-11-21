# The native file system #

The design record for OODOS's native file system: directories, file metadata, application bundles, and the layers
between a program and the media. It sits beside the CP/M 3 file system that OODOS has today
([hw/oodos.md, "Disk format"](../oodos.md#disk-format)), which is kept unchanged.

Nothing here is built. Each section says what is **decided** (the owner's) and what is **proposed** (a draft for
the owner to decide). The open questions are at the end.

## Why ##

**Decided (the owner, 2026-11-21): a native file system with directories and file metadata,** for a future GUI. CP/M 3's
flat directory of 8.3 names, sixteen user areas and a few attribute bits can't tell a Finder what a file is, which
application opens it, or what icon to draw. The goal is to get the most out of the 8/16-bit CPUs while giving the
GUI what it needs.

What the GUI asks of a file system, and what it implies:

- **A folder opens fast.** Everything a window draws (name, type, creator, size, date, flags, icon position) is in
  the catalog record, so drawing a folder never opens its files.
- **Lists come sorted.** A catalog ordered by (parent, name) gives a folder's entries in order by walking it: the
  CPU never sorts.
- **Icons by type.** A desktop database maps type and creator to icons; a file carries its own icon only when it
  has a custom one.
- **References survive moves.** Every file has an ID that never changes, so aliases, recent documents and the
  desktop database still find a file after it is renamed or moved.
- **Windows notice change.** A change counter per folder and per volume lets the Finder see with one compare that
  a window is stale.

The precedents: GEOS's file header blocks (icon, type, class, author in a block the directory entry points to),
the classic Mac's catalog with Finder info and its desktop database, RISC OS's file types and application
directories, and GS/OS's ProDOS types with an optional second stream.

**Not chosen: metadata appended to the end of each file** (the owner's first idea, 2026-11-21, as tagged formats
keep it). CP/M has no exact byte length, so the file system would have to hide the trailer from BDOS, which makes it
a second fork stored back to back with the first; an append has to move it; and reading a folder's metadata means a
seek to the end of every file. A tail-tagged layout is kept for the one place it is right: the transfer format
([below](#oo-single-the-transfer-format)).

## The CPUs ##

**Decided (the owner, 2026-11-21): the GUI requires a 65C816.** It is not offered on a 6502 OO-1.

**Decided (the owner, 2026-11-21): the native file system's driver is the 65C816's alone.** A 6502 OO-1 reaches native volumes
only through a card that does the file system itself, the storage card's RP2350 today
([below](#offload-drivers)). CP/M volumes work on both CPUs, as now.

So the on-disk format owes the 6502 nothing: fields can be word-aligned for 16-bit loads, nodes can be larger than
a sector, and caches as large as banks allow. Programs that keep the 65C816 rules
([hw/oodos.md](../oodos.md#rules-for-programs-the-65c816)) run unchanged; GUI applications are native 65C816 code.

## Layers ##

**Proposed.** Three layers, so that the RP2350 can serve files now and native raw access can replace it later
without a program noticing (the owner, 2026-11-21: "a unified API to OODOS is interesting").

```
 programs ──► OODOS file API (paths, handles, metadata)
                 │
        ┌────────┼──────────────┬───────────────┐
   native FS   CP/M FS     offload driver    (later) network
   driver      (today's)   "the card does it"  driver, ESP32
        │         │              │
        └─ block device ─┘   RP2350 / another smart card
           (sectors by DMA)
```

### Block devices ###

Read or write N sectors at a 32-bit LBA, by DMA. The storage card's unit 0 is one already
([docs, chapter 10](../../docs/oo1-technical-reference/10-storage.md)); native raw storage (SD, CF or IDE without a
translating controller) is the owner's later step and would be another block driver.

### File system drivers ###

Every driver does the same operations: mount, unmount, open, close, read, write, seek, truncate, read a directory,
get and set a file's info, make a directory, rename (and move), delete, and read and write the attribute stream.

- **Local drivers** run in the banked BDOS over a block device: the native driver (65C816) and the CP/M 3 code
  OODOS has now (both CPUs).
- **Offload drivers** hand each operation to a card whose firmware does the work, as a Commodore drive ran its DOS
  on its own 6502. The RP2350 is the first; the ESP32 network card could be another, which gives OODOS networked
  volumes, as CP/NET did.

An offload driver exposes exactly the metadata a local one does, not a lesser set: the card speaks the native
format, it doesn't translate it to FAT.

### The OODOS file API ###

A new block of OODOS functions, path and handle based, with calls for metadata and directories. The FCB functions
stay, for CP/M volumes. Function numbers are left to the OODOS phase that builds it, under AGENTS.md's "interfaces
only grow" rule.

### One implementation in C ###

**Proposed.** The file system is written once in portable C, and that code is the format's reference. It runs in
the RP2350's firmware (where it is the 6502's driver), in the emulator (C++, WASM), and in host tools that make,
check and copy volumes on a PC, as cpmtools does for CP/M images. The 65C816 driver is written later in assembly and
tested against it, volume for volume. A B-tree's bugs are far easier to find in C, with a fuzzer, than in ca65.

## CP/M volumes ##

**Decided (the owner, 2026-11-21): CP/M volumes are read and written, mounted by drive letter.** Nothing more is
asked of them: the native file system isn't shown to CP/M programs as a drive, so no 8.3 aliases, no ^Z padding and
no user-area mapping are needed.

**Proposed:** a CP/M volume is an image file, as today, and may also be an image file inside a native volume, or a
CP/M partition on raw media. MOUNT names the target and, optionally, a default user:

```
MOUNT A: /disks/work.cpm          ; an image file on a native volume
MOUNT B: SD0:P2                   ; a raw CP/M partition
MOUNT C: /disks/games.cpm /U:3    ; user 3 at the prompt
MOUNT C: /D                       ; unmount
```

Function 74 (O_MOUNT) and `DRIVES.TXT` are today's form of this; the native file system adds paths as targets.

## The on-disk format ##

**Proposed: inspired by HFS, not compatible with it.** HFS's structure (a B-tree catalog, file IDs, thread
records, extents with an overflow tree) suits a GUI; its byte order, its 16-bit allocation block count and its
resource forks don't suit this machine.

| | HFS | The native format |
|---|---|---|
| Byte order | big-endian | little-endian, as the 65C816 is |
| Block numbers | 16-bit (65,535 allocation blocks a volume) | 32-bit |
| File sizes | 32-bit | 32-bit |
| Node size | 512 bytes | per volume, 512 bytes to 4 KB |
| Catalog key | parent ID + name, compared through a table | parent ID + folded name, compared as bytes |
| Forks | data and resource | data, plus one optional attribute stream |
| Names | 31 bytes, Mac Roman | long names, the OO-1's character set (open) |

### The volume ###

A volume header at a fixed place: the format's version, the block size, the node size, the volume's name, its
change counter, the next file ID, and where the allocation bitmap, the catalog and the extents tree are. A second
copy at the volume's end, as HFS keeps its alternate header.

### The catalog ###

A B-tree of 512-byte to 4 KB nodes, keyed by the parent folder's ID and the file's name, case-folded. The record
keeps the name as it was typed, for display; the key keeps the folded copy, so every compare in a lookup is a plain
byte compare with no folding table in the loop. A folder's entries are a run of leaf records in name order.

Each file and folder has a 32-bit ID that never changes, and a thread record keyed by that ID that names its parent
and its name, so an ID leads to a path (HFS's catalog node IDs and threads).

### A catalog record ###

Fixed size, word-aligned, so that every field is at a constant offset and a 16-bit load reaches it. Two layers:

- **Core metadata, kept by the driver** whatever program writes the file: ID, parent ID, flags, type and creator,
  data length, attribute stream length, the creation, modification and access dates, the first extents, and, for a
  folder, its entry count and change counter. A command-line tool that writes a file keeps these right, so the GUI
  is never misled.
- **Finder info, carried by the driver:** a fixed block (32 bytes, say) that only the GUI interprets: the icon's
  position in its window, a folder's view and window settings, a label or colour. The driver never reads it; it
  only keeps it with the file.

**Types** are four characters, as the Mac's are: a type and a creator, each a 32-bit compare. Files that come in
from CP/M or a PC get a type from their extension (`.COM`, `.TXT`, `.SID`, `.MOD`, ...) by a table.

**Dates** are FAT's packed form, 32 bits: year from 1980, month, day, hour, minute, two-second steps. Sorting is a
4-byte compare and showing one is shifts and masks, where seconds from an epoch would need a 32-bit division for
every line of a directory listing.

**Flags** include: bundle ([below](#bundles)), hidden, locked, has a custom icon, system.

### Extents ###

A file's first few extents (start block and length) are in its catalog record; a fragmented file's further
extents are in a second B-tree keyed by file ID, stream and position, as HFS has. The allocation bitmap has a bit a
block.

### The attribute stream ###

One optional stream per file, beside the data, in blocks of its own and pointed to by the catalog record. It holds
what doesn't fit a fixed record and most files don't have: a custom icon, a comment, a preview. Its contents are a
simple list of tagged items (tag, length, bytes). It is not a general resource fork: bundles take that role.

### Change counters ###

The volume and each folder have a counter that goes up whenever anything in them changes. The Finder keeps the
counter it last drew a window from and redraws when it differs.

### Crash safety ###

A node split writes several blocks; power lost half way through can leave the catalog damaged. **Proposed:** the
driver orders its writes so that a half-done split leaves the tree consistent, and a small intent log at a fixed
place records an operation before it starts, so a check at mount finishes or undoes it. How much of this to build
is an open question.

## Bundles ##

**Decided (the owner, 2026-11-21): applications are bundles,** directories that the system treats as one thing,
as RISC OS's application directories and the Mac's application bundles are. Bundles take the place of resource
forks.

**Proposed: the layout.**

```
OOAmp.app/                  a directory, its catalog record's bundle flag set
  INFO                      the manifest
  OOAMP.816                 the 65C816 executable
  OOAMP.PRL                 optional: a 6502 build, for a tool that runs on both
  ICONS                     its icons, in the video card's own format
  RES/                      strings, OOUI dialog layouts, sounds, help
```

- **Marked by a flag and a name.** The Finder tests the catalog's bundle flag, one bit an entry. The `.app` suffix
  makes a bundle plain in a command-line listing and on a PC.
- **Declarative, never code run on sight.** RISC OS ran an application's `!Boot` when the Filer first saw it; here
  the Finder reads a manifest, so a broken or hostile application runs nothing by being mounted.
- **Icons go straight to the card.** They are kept in the video card's format, so registering an application is a
  DMA copy into card memory.
- **Two builds, optionally.** The loader runs the build for the CPU it finds. A GUI application ships `.816`
  alone.
- **Run by name.** The CCP runs `OOAMP` or `OOAMP.APP`: the loader reads INFO and starts the right build. Bundles
  aren't the GUI's only.
- **Document bundles** use the same mechanism: a document made of many files, such as a Build Plate level pack with
  its art and songs, an OO-Amp skin, or a project, shows as one icon.

### The manifest ###

**Decided (the owner, 2026-11-21): the manifest declares services, "open with" handlers among them,** not only
document types.

**Proposed: plain text,** one key and its value a line, as `DRIVES.TXT` is. It is read when an application is
registered, not when it runs, so its parsing costs nothing that matters, and it can be written by hand.

```
NAME      = OO-Amp
CREATOR   = OAMP
VERSION   = 1.2
REQUIRES  = 65816, OODOS 2.0

DOCTYPE   = SIDF  ext=SID  icon=2  role=viewer
DOCTYPE   = MODF  ext=MOD  icon=3  role=viewer
DOCTYPE   = PLST  ext=PLS  icon=4  role=editor   ; its own documents

OPENWITH  = MP3F  WAVF                           ; opens these too, not as their owner
PREVIEW   = SIDF  module=PREVIEW.816             ; draws a song's details for the Finder
TRANSLATE = MODF -> WAVF  module=RENDER.816      ; converts, when asked
COMMAND   = OOAMP                                ; the name the CCP runs it by
```

- **DOCTYPE:** a type the application owns, with the extensions that map to it, its icon, and its role (editor or
  viewer).
- **OPENWITH:** types it opens but doesn't own. A document opens with its creator, else its type's owner, else one
  that lists it; more than one candidate gives an "Open with" menu. The owner can choose the default for a type in
  the desktop database.
- **PREVIEW:** a small 65C816 module the Finder loads into a bank of its own and calls by `JSL` to draw a thumbnail
  or a details panel. Previews are cached by file ID and modification date, never written into the user's files.
- **TRANSLATE:** a module that converts one type to another, on request (MOD to WAV, a CP/M text file to a native
  document), as the Mac's Easy Open did.
- **COMMAND:** the name the CCP runs the bundle by.
- **Reserved: background helpers.** They depend on what OODOS's scheduler becomes on the 65C816; the key is kept so
  that manifests can add them later.

**Modules can't be protected.** The 65C816 has no MMU, so a preview module that runs wild can bring down the
Finder. Modules are kept small, with a fixed entry table and a time budget, and the Finder turns off a module that
fails until its application is installed again.

### The desktop database ###

A file in each volume's system folder, built from the manifests: types and creators to icons, applications and
their services, and the owner's defaults for "open with". The Finder registers a bundle when a volume is mounted,
when a bundle is copied in, and when it opens a folder with one not yet registered; "rebuild desktop" reads every
bundle again. Bundles are directories with IDs that never change, so a moved application stays registered. The
65C816's Finder keeps the whole database and the icon cache in memory (icons in card memory).

## OO-Single: the transfer format ##

**Proposed.** A native file leaves the machine (over serial, the network card, or onto a PC) as one file:
its data, then its catalog metadata and its attribute stream, then a fixed footer at the very end with a magic
number and the lengths, as a ZIP's end record is found. OODOS's copy and network tools pack and unpack it; a tool
that knows nothing of it still finds the data intact at the front. Bundles travel as an archive of OO-Single files.

## Open Questions ##

- **Volume naming.** Drive letters only, or named volumes (`WORK:/src/game.s`) with letters bound by MOUNT when a
  CP/M tool wants one. The draft leans to names.
- **The character set and case folding** for names: the OO-1's font's set, Latin-1, or another; and the longest
  name.
- **Icon format.** Fixed-size icons (32 × 32, say) in the palette layers' colours, or in screen type 7's RGB444; one
  size or several.
- **Finder info's contents and size,** and whether icon positions live in it (fast to read, a catalog write for
  each move, held in the 65C816's cache and written back) or in a small file per folder.
- **Crash safety:** ordered writes alone, or ordered writes with the intent log.
- **Block and node sizes** for SD volumes, and the largest volume.
- **The partition map** for raw media: native volumes, CP/M partitions, and perhaps a small FAT partition for a PC.
- **Running programs from a native volume:** the CCP and the loader take paths, and the new executable format for
  native 65C816 programs that span banks.
- **The 65C816 interfaces that must widen** (block numbers, 20-bit physical addresses), already listed in
  [hw/design.md, "Future: a 65C816 and 16 MB"](../design.md#future-a-65c816-and-16-mb).
- **The RP2350's command set** for an offload driver: one command an operation, through the storage card's
  existing registers, or a new mode.
