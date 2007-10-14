/*
 * Copyright (C) 2002-2003  David McCullough <davidm@snapgear.com>
 * Copyright (C) 1998       Kenneth Albanowski <kjahds@kjahds.com>
 *                          The Silver Hammer Group, Ltd.
 *
 * This file provides the definitions and structures needed to
 * support uClinux flat-format executables.
 */

#define	FLAT_VERSION			0x00000004L

#ifdef CONFIG_BINFMT_SHARED_FLAT
#define	MAX_SHARED_LIBS			(4)
#else
#define	MAX_SHARED_LIBS			(1)
#endif

/*
 * To make everything easier to port and manage cross platform
 * development,  all fields are in network byte order.
 */

struct flat_hdr {
	char magic[4];
	abi_ulong rev;          /* version (as above) */
	abi_ulong entry;        /* Offset of first executable instruction
                                   with text segment from beginning of file */
	abi_ulong data_start;   /* Offset of data segment from beginning of
                                   file */
	abi_ulong data_end;     /* Offset of end of data segment
                                   from beginning of file */
	abi_ulong bss_end;      /* Offset of end of bss segment from beginning
                                   of file */

	/* (It is assumed that data_end through bss_end forms the bss segment.) */

	abi_ulong stack_size;   /* Size of stack, in bytes */
	abi_ulong reloc_start;  /* Offset of relocation records from
                                   beginning of file */
	abi_ulong reloc_count;  /* Number of relocation records */
	abi_ulong flags;
	abi_ulong build_date;   /* When the program/library was built */
	abi_ulong filler[5];    /* Reservered, set to zero */
};

#define FLAT_FLAG_RAM    0x0001 /* load program entirely into RAM */
#define FLAT_FLAG_GOTPIC 0x0002 /* program is PIC with GOT */
#define FLAT_FLAG_GZIP   0x0004 /* all but the header is compressed */
#define FLAT_FLAG_GZDATA 0x0008 /* only data/relocs are compressed (for XIP) */
#define FLAT_FLAG_KTRACE 0x0010 /* output useful kernel trace for debugging */


/*
 * While it would be nice to keep this header clean,  users of older
 * tools still need this support in the kernel.  So this section is
 * purely for compatibility with old tool chains.
 *
 * DO NOT make changes or enhancements to the old format please,  just work
 *        with the format above,  except to fix bugs with old format support.
 */

#define	OLD_FLAT_VERSION			0x00000002L
#define OLD_FLAT_RELOC_TYPE_TEXT	0
#define OLD_FLAT_RELOC_TYPE_DATA	1
#define OLD_FLAT_RELOC_TYPE_BSS		2

#   	define OLD_FLAT_FLAG_RAM    0x1 /* load program entirely into RAM */
