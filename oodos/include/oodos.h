/*
 * oodos.h: OODOS for a C program, compiled by cc65 (Plan C1; the Programmer's Guide, appendix F,
 * "A program in C").
 *
 * A C program is a .COM linked with cfg/c.cfg, oodosc.lib (c/: the start, the console's read and
 * write, argc and argv, bdos()) and cc65's none.lib: oodos/Makefile's CPROGRAMS. stdin, stdout and
 * stderr are the console; files through stdio are a later step.
 */

#ifndef OODOS_H
#define OODOS_H

/* BEGIN: written from oodos.inc by tools/oodosh.py; don't edit by hand. */

/* The base page (CP/M's page 0, at $0200). */
#define BP_WBOOT         0x0200
#define BP_BDOS          0x0203
#define BP_TOP           0x0204
#define BP_FCB           0x025C
#define BP_FCB2          0x026C
#define BP_DMA           0x0280
#define BP_TPA           0x0300

/* The BDOS functions, bdos()'s fn. */
#define P_TERMCPM        0
#define C_READ           1
#define C_WRITE          2
#define A_READ           3
#define A_WRITE          4
#define L_WRITE          5
#define C_RAWIO          6
#define A_STATIN         7
#define A_STATOUT        8
#define C_WRITESTR       9
#define C_READSTR        10
#define C_STAT           11
#define S_BDOSVER        12
#define DRV_ALLRESET     13
#define DRV_SET          14
#define F_OPEN           15
#define F_CLOSE          16
#define F_SFIRST         17
#define F_SNEXT          18
#define F_DELETE         19
#define F_READ           20
#define F_WRITE          21
#define F_MAKE           22
#define F_RENAME         23
#define DRV_LOGINVEC     24
#define DRV_GET          25
#define F_DMAOFF         26
#define DRV_ALLOCVEC     27
#define DRV_SETRO        28
#define DRV_ROVEC        29
#define F_ATTRIB         30
#define DRV_DPB          31
#define F_USERNUM        32
#define F_READRAND       33
#define F_WRITERAND      34
#define F_SIZE           35
#define F_RANDREC        36
#define DRV_RESET        37
#define DRV_ACCESS       38
#define DRV_FREEDRV      39
#define F_WRITEZF        40
#define F_LOCK           42
#define F_UNLOCK         43
#define F_MULTISEC       44
#define F_ERRMODE        45
#define DRV_SPACE        46
#define P_CHAIN          47
#define DRV_FLUSH        48
#define S_SCB            49
#define S_BIOS           50
#define F_DMASEG         51
#define F_DMAGET         52
#define MC_MAX           53
#define MC_ABSMAX        54
#define MC_ALLOC         55
#define MC_ABSALLOC      56
#define MC_FREE          57
#define MC_ALLFREE       58
#define P_LOAD           59
#define P_RSX            60
#define DRV_FREE         98
#define F_TRUNCATE       99
#define DRV_SETLABEL     100
#define DRV_GETLABEL     101
#define F_TIMEDATE       102
#define F_WRITEXFCB      103
#define T_SET            104
#define T_GET            105
#define F_PASSWD         106
#define S_SERIAL         107
#define P_CODE           108
#define C_MODE           109
#define C_DELIMIT        110
#define C_WRITEBLK       111
#define L_WRITEBLK       112
#define F_PARSE          152
#define O_VERSION        72
#define O_PHYSADDR       73
#define O_MOUNT          74
#define O_ROM            75
#define O_SECTOR         76
#define O_ROMVER         77
#define O_FRAME          80
#define O_INPUT          81
#define O_SOUND          82
#define O_DMA            83

/* END: written from oodos.inc. */

/*
 * Any BDOS function: fn in Y, arg in A (low) and X (high), the result A (low) and X (high), as
 * oodos.inc's rule says. A function that returns a byte gives it in the low byte; the high byte is
 * X as the function leaves it.
 */
unsigned __fastcall__ bdos(unsigned char fn, unsigned arg);

#endif
