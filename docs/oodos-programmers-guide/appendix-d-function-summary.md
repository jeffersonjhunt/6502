# Appendix D: Function summary

Every function on one page: number, name, what it does, entry and exit registers. Written by hand from chapter 7's
entries; [docs/index/functions.json](../index/functions.json) is the generated list. Nothing checks this table
against the entries, so where the two differ, the entry is right.

Every call is `JSR BDOS` (`$0203`) with the function in Y ([chapter 2](02-system-interface.md#calling-the-bdos)).
A/X is a word, A low and X high. "A = 0" means X is 0 too, for a byte result; a code that isn't 0 or `$FF` is in
the function's entry, and the physical errors, A = `$FF` with a code in X, are in
[appendix A](appendix-a-errors.md). This is a summary: the entry is what a program is written from.

| No. | Name | What | Entry | Exit |
|---|---|---|---|---|
| [0](07-function-reference.md#bdos-0) | `P_TERMCPM` | end the program | — | doesn't return |
| [1](07-function-reference.md#bdos-1) | `C_READ` | read a key from the console | — | A = the key |
| [2](07-function-reference.md#bdos-2) | `C_WRITE` | write a character to the console | A = the character | A/X = 0 |
| [3](07-function-reference.md#bdos-3) | `A_READ` | read a character from AUX: | — | A = the character |
| [4](07-function-reference.md#bdos-4) | `A_WRITE` | write a character to AUX: | A = the character | A/X = 0 |
| [5](07-function-reference.md#bdos-5) | `L_WRITE` | write a character to LST: | A = the character | A/X = 0 |
| [6](07-function-reference.md#bdos-6) | `C_RAWIO` | the console without the BDOS's handling | A = `$FF`, `$FE`, `$FD`, or a character | A = a key, 0, or the status |
| [7](07-function-reference.md#bdos-7) | `A_STATIN` | is an AUX: character waiting? | — | A = `$FF` yes, 0 no |
| [8](07-function-reference.md#bdos-8) | `A_STATOUT` | is AUX: ready for a character? | — | A = `$FF` yes, 0 no |
| [9](07-function-reference.md#bdos-9) | `C_WRITESTR` | print a string | A/X = the string, to the delimiter | A/X = 0 |
| [10](07-function-reference.md#bdos-10) | `C_READSTR` | read an edited line from the console | A/X = the buffer | A/X = 0 |
| [11](07-function-reference.md#bdos-11) | `C_STAT` | is a console key waiting? | — | A = 1 yes, 0 no |
| [12](07-function-reference.md#bdos-12) | `S_BDOSVER` | the version number | — | A/X = `$0031` |
| [13](07-function-reference.md#bdos-13) | `DRV_ALLRESET` | reset every drive | — | A/X = 0, or an error |
| [14](07-function-reference.md#bdos-14) | `DRV_SET` | select the current drive | A = the drive, 0-15 | A/X = 0, or an error |
| [15](07-function-reference.md#bdos-15) | `F_OPEN` | open a file | A/X = the FCB | A = 0, `$FF` none |
| [16](07-function-reference.md#bdos-16) | `F_CLOSE` | close a file | A/X = the FCB | A = 0, `$FF` |
| [17](07-function-reference.md#bdos-17) | `F_SFIRST` | search for the first match | A/X = the FCB | A = 0-3, `$FF` none |
| [18](07-function-reference.md#bdos-18) | `F_SNEXT` | search for the next match | — | A = 0-3, `$FF` none |
| [19](07-function-reference.md#bdos-19) | `F_DELETE` | delete files | A/X = the FCB | A = 0, `$FF` none |
| [20](07-function-reference.md#bdos-20) | `F_READ` | read the next record | A/X = the FCB | A = 0, 1 the end, or a code |
| [21](07-function-reference.md#bdos-21) | `F_WRITE` | write the next record | A/X = the FCB | A = 0, or a code |
| [22](07-function-reference.md#bdos-22) | `F_MAKE` | make a file | A/X = the FCB | A = 0, `$FF` full |
| [23](07-function-reference.md#bdos-23) | `F_RENAME` | rename a file | A/X = the FCB | A = 0, `$FF` none |
| [24](07-function-reference.md#bdos-24) | `DRV_LOGINVEC` | the logged-in drives | — | A/X = the vector |
| [25](07-function-reference.md#bdos-25) | `DRV_GET` | the current drive | — | A = the drive, 0-15 |
| [26](07-function-reference.md#bdos-26) | `F_DMAOFF` | set the DMA address | A/X = the address | A/X = 0 |
| [27](07-function-reference.md#bdos-27) | `DRV_ALLOCVEC` | the allocation vector's address | — | A/X = `$FFFF` |
| [28](07-function-reference.md#bdos-28) | `DRV_SETRO` | make the current drive read-only | — | A/X = 0 |
| [29](07-function-reference.md#bdos-29) | `DRV_ROVEC` | the read-only drives | — | A/X = the vector |
| [30](07-function-reference.md#bdos-30) | `F_ATTRIB` | set a file's attributes | A/X = the FCB | A = 0, `$FF` none |
| [31](07-function-reference.md#bdos-31) | `DRV_DPB` | the current drive's parameters | — | A/X = the DPB |
| [32](07-function-reference.md#bdos-32) | `F_USERNUM` | get or set the user number | A = `$FF` get, or the user | A = the user, or 0 |
| [33](07-function-reference.md#bdos-33) | `F_READRAND` | read a random record | A/X = the FCB | A = 0, or a code |
| [34](07-function-reference.md#bdos-34) | `F_WRITERAND` | write a random record | A/X = the FCB | A = 0, or a code |
| [35](07-function-reference.md#bdos-35) | `F_SIZE` | a file's size | A/X = the FCB | A = 0, R0-R2 the size |
| [36](07-function-reference.md#bdos-36) | `F_RANDREC` | set the random record from the file's place | A/X = the FCB | A/X = 0, R0-R2 set |
| [37](07-function-reference.md#bdos-37) | `DRV_RESET` | reset drives | A/X = the vector | A/X = 0, or an error |
| [38](07-function-reference.md#bdos-38) | `DRV_ACCESS` | claim drives (MP/M's) | - | A/X = 0 |
| [39](07-function-reference.md#bdos-39) | `DRV_FREEDRV` | release drives (MP/M's) | - | A/X = 0 |
| [40](07-function-reference.md#bdos-40) | `F_WRITEZF` | write a random record, zero-filled | A/X = the FCB | as 34 |
| [42](07-function-reference.md#bdos-42) | `F_LOCK` | lock records | — (A and X aren't read) | A/X = 0 |
| [43](07-function-reference.md#bdos-43) | `F_UNLOCK` | unlock records | — (A and X aren't read) | A/X = 0 |
| [44](07-function-reference.md#bdos-44) | `F_MULTISEC` | set the multi-sector count | A = 1-128 | A = 0, `$FF` |
| [45](07-function-reference.md#bdos-45) | `F_ERRMODE` | set the error mode | A = the mode | A/X = 0 |
| [46](07-function-reference.md#bdos-46) | `DRV_SPACE` | a drive's free space | A = the drive | A/X = 0, 3 bytes at the DMA address |
| [47](07-function-reference.md#bdos-47) | `P_CHAIN` | end the program and run a command | A = `$FF` keep its drive and user; the command at `$0280` | doesn't return |
| [48](07-function-reference.md#bdos-48) | `DRV_FLUSH` | write the buffers back | A = `$FF` drop them too | A/X = 0, or an error |
| [49](07-function-reference.md#bdos-49) | `S_SCB` | get or set a field of the SCB | A/X = the parameter block | A/X = the word got |
| [50](07-function-reference.md#bdos-50) | `S_BIOS` | call the BIOS | A/X = the parameter block | A/X = the BIOS's |
| [51](07-function-reference.md#bdos-51) | `F_DMASEG` | set the DMA segment | A/X = the paragraph, `$FFFF` none | A/X = 0 |
| [52](07-function-reference.md#bdos-52) | `F_DMAGET` | get the DMA address and segment | A/X = 4 bytes to fill | A/X = 0 |
| [53](07-function-reference.md#bdos-53) | `MC_MAX` | allocate the largest free run | A/X = the MCB | A = 0, `$FF` |
| [54](07-function-reference.md#bdos-54) | `MC_ABSMAX` | allocate the largest run at a base | A/X = the MCB | A = 0, `$FF` |
| [55](07-function-reference.md#bdos-55) | `MC_ALLOC` | allocate memory | A/X = the MCB | A = 0, `$FF` |
| [56](07-function-reference.md#bdos-56) | `MC_ABSALLOC` | allocate memory at a base | A/X = the MCB | A = 0, `$FF` |
| [57](07-function-reference.md#bdos-57) | `MC_FREE` | free memory | A/X = the MCB | A = 0, `$FF` |
| [58](07-function-reference.md#bdos-58) | `MC_ALLFREE` | free all memory | — | A/X = 0 |
| [59](07-function-reference.md#bdos-59) | `P_LOAD` | load an overlay | A/X = an opened FCB, R0-R1 the address; 0 removal only | A = 0, `$FE`, 1, `$FF` |
| [60](07-function-reference.md#bdos-60) | `P_RSX` | call an RSX | A/X = the RSX parameter block | the RSX's; `$FF` if none takes it |
| [72](07-function-reference.md#bdos-72) | `O_VERSION` | OODOS's version and FARCALL | A/X = 10 bytes to fill | A/X = FARCALL |
| [73](07-function-reference.md#bdos-73) | `O_PHYSADDR` | a CPU address's physical address | A/X = 4 bytes | A = 0, `$FF` |
| [74](07-function-reference.md#bdos-74) | `O_MOUNT` | mount or unmount an SD drive | A/X = the drive, flags, path | A = 0, `$FF` |
| [75](07-function-reference.md#bdos-75) | `O_ROM` | leave OODOS for the ROM | A = 0 the monitor, 1 Apple II mode | doesn't return, or `$FF` |
| [76](07-function-reference.md#bdos-76) | `O_SECTOR` | an Apple disk's DOS 3.3 sector | A/X = the block | A = 0, 1, 2, `$FF` |
| [98](07-function-reference.md#bdos-98) | `DRV_FREE` | free blocks not closed | — | A/X = 0 |
| [99](07-function-reference.md#bdos-99) | `F_TRUNCATE` | truncate a file | A/X = the FCB | A = 0, `$FF` |
| [100](07-function-reference.md#bdos-100) | `DRV_SETLABEL` | make or change a directory label | A/X = the FCB | A = 0, `$FF` |
| [101](07-function-reference.md#bdos-101) | `DRV_GETLABEL` | a drive's label mode | A = the drive | A = the mode, 0 none |
| [102](07-function-reference.md#bdos-102) | `F_TIMEDATE` | a file's stamps and password mode | A/X = the FCB | A = 0, `$FF` none |
| [103](07-function-reference.md#bdos-103) | `F_WRITEXFCB` | set a file's password and mode | A/X = the FCB | A = 0, `$FF` |
| [104](07-function-reference.md#bdos-104) | `T_SET` | set the date and time | A/X = the DAT | A/X = 0 |
| [105](07-function-reference.md#bdos-105) | `T_GET` | get the date and time | A/X = the DAT to fill | A = the seconds |
| [106](07-function-reference.md#bdos-106) | `F_PASSWD` | set the default password | A/X = 8 bytes | A/X = 0 |
| [107](07-function-reference.md#bdos-107) | `S_SERIAL` | get the serial number | A/X = 6 bytes to fill | A/X = 0 |
| [108](07-function-reference.md#bdos-108) | `P_CODE` | get or set the program return code | A/X = `$FFFF` get, or the code | A/X = the code |
| [109](07-function-reference.md#bdos-109) | `C_MODE` | get or set the console mode | A/X = `$FFFF` get, or the mode | A/X = the mode |
| [110](07-function-reference.md#bdos-110) | `C_DELIMIT` | get or set the output delimiter | A/X = `$FFFF` get, or A the delimiter | A = the delimiter |
| [111](07-function-reference.md#bdos-111) | `C_WRITEBLK` | write a block of characters to the console | A/X = the CCB | A/X = 0 |
| [112](07-function-reference.md#bdos-112) | `L_WRITEBLK` | write a block of characters to LST: | A/X = the CCB | A/X = 0 |
| [152](07-function-reference.md#bdos-152) | `F_PARSE` | parse a file name into an FCB | A/X = the parse block | A/X = what follows, 0, or `$FFFF` |

A function number OODOS doesn't have returns A = X = `$FF`: 61-71, 77-97, 113-151 and 153 on
([appendix A](appendix-a-errors.md)). 41, MP/M's test and write, returns A = `$FF` with X 0, as CP/M 3 answers it.
