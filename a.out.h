/* a.out.h

   Copyright 1997, 1998, 1999, 2001 Red Hat, Inc.

This file is part of Cygwin.

This software is a copyrighted work licensed under the terms of the
Cygwin license.  Please consult the file "CYGWIN_LICENSE" for
details. */

#ifndef _A_OUT_H_
#define _A_OUT_H_

#ifdef __cplusplus
extern "C" {
#endif
#define COFF_IMAGE_WITH_PE
#define COFF_LONG_SECTION_NAMES

/*** coff information for Intel 386/486.  */


/********************** FILE HEADER **********************/

struct external_filehdr {
  short f_magic;	/* magic number			*/
  short f_nscns;	/* number of sections		*/
  host_ulong f_timdat;	/* time & date stamp		*/
  host_ulong f_symptr;	/* file pointer to symtab	*/
  host_ulong f_nsyms;	/* number of symtab entries	*/
  short f_opthdr;	/* sizeof(optional hdr)		*/
  short f_flags;	/* flags			*/
};

/* Bits for f_flags:
 *	F_RELFLG	relocation info stripped from file
 *	F_EXEC		file is executable (no unresolved external references)
 *	F_LNNO		line numbers stripped from file
 *	F_LSYMS		local symbols stripped from file
 *	F_AR32WR	file has byte ordering of an AR32WR machine (e.g. vax)
 */

#define F_RELFLG	(0x0001)
#define F_EXEC		(0x0002)
#define F_LNNO		(0x0004)
#define F_LSYMS		(0x0008)



#define	I386MAGIC	0x14c
#define I386PTXMAGIC	0x154
#define I386AIXMAGIC	0x175

/* This is Lynx's all-platform magic number for executables. */

#define LYNXCOFFMAGIC	0415

#define I386BADMAG(x) (((x).f_magic != I386MAGIC) \
		       && (x).f_magic != I386AIXMAGIC \
		       && (x).f_magic != I386PTXMAGIC \
		       && (x).f_magic != LYNXCOFFMAGIC)

#define	FILHDR	struct external_filehdr
#define	FILHSZ	20


/********************** AOUT "OPTIONAL HEADER"=
 **********************/


typedef struct
{
  unsigned short magic;		/* type of file				*/
  unsigned short vstamp;	/* version stamp			*/
  host_ulong	tsize;		/* text size in bytes, padded to FW bdry*/
  host_ulong	dsize;		/* initialized data "  "		*/
  host_ulong	bsize;		/* uninitialized data "   "		*/
  host_ulong	entry;		/* entry pt.				*/
  host_ulong text_start;	/* base of text used for this file */
  host_ulong data_start;	/* base of data used for this file=
 */
}
AOUTHDR;

#define AOUTSZ 28
#define AOUTHDRSZ 28

#define OMAGIC          0404    /* object files, eg as output */
#define ZMAGIC          0413    /* demand load format, eg normal ld output */
#define STMAGIC		0401	/* target shlib */
#define SHMAGIC		0443	/* host   shlib */


/* define some NT default values */
/*  #define NT_IMAGE_BASE        0x400000 moved to internal.h */
#define NT_SECTION_ALIGNMENT 0x1000
#define NT_FILE_ALIGNMENT    0x200
#define NT_DEF_RESERVE       0x100000
#define NT_DEF_COMMIT        0x1000

/********************** SECTION HEADER **********************/


struct external_scnhdr {
  char		s_name[8];	/* section name			*/
  host_ulong	s_paddr;	/* physical address, offset
				   of last addr in scn */
  host_ulong	s_vaddr;	/* virtual address		*/
  host_ulong	s_size;		/* section size			*/
  host_ulong	s_scnptr;	/* file ptr to raw data for section */
  host_ulong	s_relptr;	/* file ptr to relocation	*/
  host_ulong	s_lnnoptr;	/* file ptr to line numbers	*/
  unsigned short s_nreloc;	/* number of relocation entries	*/
  unsigned short s_nlnno;	/* number of line number entries*/
  host_ulong	s_flags;	/* flags			*/
};

#define	SCNHDR	struct external_scnhdr
#define	SCNHSZ	40

/*
 * names of "special" sections
 */
#define _TEXT	".text"
#define _DATA	".data"
#define _BSS	".bss"
#define _COMMENT ".comment"
#define _LIB ".lib"

/********************** LINE NUMBERS **********************/

/* 1 line number entry for every "breakpointable" source line in a section.
 * Line numbers are grouped on a per function basis; first entry in a function
 * grouping will have l_lnno = 0 and in place of physical address will be the
 * symbol table index of the function name.
 */
struct external_lineno {
  union {
    host_ulong l_symndx; /* function name symbol index, iff l_lnno 0 */
    host_ulong l_paddr;	/* (physical) address of line number	*/
  } l_addr;
  unsigned short l_lnno;	/* line number		*/
};

#define	LINENO	struct external_lineno
#define	LINESZ	6

/********************** SYMBOLS **********************/

#define E_SYMNMLEN	8	/* # characters in a symbol name	*/
#define E_FILNMLEN	14	/* # characters in a file name		*/
#define E_DIMNUM	4	/* # array dimensions in auxiliary entry */

struct QEMU_PACKED external_syment
{
  union {
    char e_name[E_SYMNMLEN];
    struct {
      host_ulong e_zeroes;
      host_ulong e_offset;
    } e;
  } e;
  host_ulong e_value;
  unsigned short e_scnum;
  unsigned short e_type;
  char e_sclass[1];
  char e_numaux[1];
};

#define N_BTMASK	(0xf)
#define N_TMASK		(0x30)
#define N_BTSHFT	(4)
#define N_TSHIFT	(2)

union external_auxent {
  struct {
    host_ulong x_tagndx;	/* str, un, or enum tag indx */
    union {
      struct {
	unsigned short  x_lnno; /* declaration line number */
	unsigned short  x_size; /* str/union/array size */
      } x_lnsz;
      host_ulong x_fsize;	/* size of function */
    } x_misc;
    union {
      struct {			/* if ISFCN, tag, or .bb */
	host_ulong x_lnnoptr;/* ptr to fcn line # */
	host_ulong x_endndx;	/* entry ndx past block end */
      } x_fcn;
      struct {			/* if ISARY, up to 4 dimen. */
	char x_dimen[E_DIMNUM][2];
      } x_ary;
    } x_fcnary;
    unsigned short x_tvndx;	/* tv index */
  } x_sym;

  union {
    char x_fname[E_FILNMLEN];
    struct {
      host_ulong x_zeroes;
      host_ulong x_offset;
    } x_n;
  } x_file;

  struct {
    host_ulong x_scnlen;	/* section length */
    unsigned short x_nreloc;	/* # relocation entries */
    unsigned short x_nlinno;	/* # line numbers */
    host_ulong x_checksum;	/* section COMDAT checksum */
    unsigned short x_associated;/* COMDAT associated section index */
    char x_comdat[1];		/* COMDAT selection number */
  } x_scn;

  struct {
    host_ulong x_tvfill;	/* tv fill value */
    unsigned short x_tvlen;	/* length of .tv */
    char x_tvran[2][2];		/* tv range */
  } x_tv;	/* info about .tv section (in auxent of symbol .tv)) */

};

#define	SYMENT	struct external_syment
#define	SYMESZ	18
#define	AUXENT	union external_auxent
#define	AUXESZ	18

#define _ETEXT	"etext"

/********************** RELOCATION DIRECTIVES **********************/

struct external_reloc {
  char r_vaddr[4];
  char r_symndx[4];
  char r_type[2];
};

#define RELOC struct external_reloc
#define RELSZ 10

/* end of coff/i386.h */

/* PE COFF header information */

#ifndef _PE_H
#define _PE_H

/* NT specific file attributes */
#define IMAGE_FILE_RELOCS_STRIPPED           0x0001
#define IMAGE_FILE_EXECUTABLE_IMAGE          0x0002
#define IMAGE_FILE_LINE_NUMS_STRIPPED        0x0004
#define IMAGE_FILE_LOCAL_SYMS_STRIPPED       0x0008
#define IMAGE_FILE_BYTES_REVERSED_LO         0x0080
#define IMAGE_FILE_32BIT_MACHINE             0x0100
#define IMAGE_FILE_DEBUG_STRIPPED            0x0200
#define IMAGE_FILE_SYSTEM                    0x1000
#define IMAGE_FILE_DLL                       0x2000
#define IMAGE_FILE_BYTES_REVERSED_HI         0x8000

/* additional flags to be set for section headers to allow the NT loader to
   read and write to the section data (to replace the addresses of data in
   dlls for one thing); also to execute the section in .text's case=
 */
#define IMAGE_SCN_MEM_DISCARDABLE 0x02000000
#define IMAGE_SCN_MEM_EXECUTE     0x20000000
#define IMAGE_SCN_MEM_READ        0x40000000
#define IMAGE_SCN_MEM_WRITE       0x80000000

/*
 * Section characteristics added for ppc-nt
 */

#define IMAGE_SCN_TYPE_NO_PAD                0x00000008  /* Reserved.  */

#define IMAGE_SCN_CNT_CODE                   0x00000020  /* Section contains code. */
#define IMAGE_SCN_CNT_INITIALIZED_DATA       0x00000040  /* Section contains initialized data. */
#define IMAGE_SCN_CNT_UNINITIALIZED_DATA     0x00000080  /* Section contains uninitialized data. */

#define IMAGE_SCN_LNK_OTHER                  0x00000100  /* Reserved.  */
#define IMAGE_SCN_LNK_INFO                   0x00000200  /* Section contains comments or some other type of information. */
#define IMAGE_SCN_LNK_REMOVE                 0x00000800  /* Section contents will not become part of image. */
#define IMAGE_SCN_LNK_COMDAT                 0x00001000  /* Section contents comdat. */

#define IMAGE_SCN_MEM_FARDATA                0x00008000

#define IMAGE_SCN_MEM_PURGEABLE              0x00020000
#define IMAGE_SCN_MEM_16BIT                  0x00020000
#define IMAGE_SCN_MEM_LOCKED                 0x00040000
#define IMAGE_SCN_MEM_PRELOAD                0x00080000

#define IMAGE_SCN_ALIGN_1BYTES               0x00100000
#define IMAGE_SCN_ALIGN_2BYTES               0x00200000
#define IMAGE_SCN_ALIGN_4BYTES               0x00300000
#define IMAGE_SCN_ALIGN_8BYTES               0x00400000
#define IMAGE_SCN_ALIGN_16BYTES              0x00500000  /* Default alignment if no others are specified. */
#define IMAGE_SCN_ALIGN_32BYTES              0x00600000
#define IMAGE_SCN_ALIGN_64BYTES              0x00700000


#define IMAGE_SCN_LNK_NRELOC_OVFL            0x01000000  /* Section contains extended relocations. */
#define IMAGE_SCN_MEM_NOT_CACHED             0x04000000  /* Section is not cachable.               */
#define IMAGE_SCN_MEM_NOT_PAGED              0x08000000  /* Section is not pageable.               */
#define IMAGE_SCN_MEM_SHARED                 0x10000000  /* Section is shareable.                  */

/* COMDAT selection codes.  */

#define IMAGE_COMDAT_SELECT_NODUPLICATES     (1) /* Warn if duplicates.  */
#define IMAGE_COMDAT_SELECT_ANY		     (2) /* No warning.  */
#define IMAGE_COMDAT_SELECT_SAME_SIZE	     (3) /* Warn if different size.  */
#define IMAGE_COMDAT_SELECT_EXACT_MATCH	     (4) /* Warn if different.  */
#define IMAGE_COMDAT_SELECT_ASSOCIATIVE	     (5) /* Base on other section.  */

/* Magic values that are true for all dos/nt implementations */
#define DOSMAGIC       0x5a4d
#define NT_SIGNATURE   0x00004550

/* NT allows long filenames, we want to accommodate this.  This may break
     some of the bfd functions */
#undef  FILNMLEN
#define FILNMLEN	18	/* # characters in a file name		*/


#ifdef COFF_IMAGE_WITH_PE
/* The filehdr is only weired in images */

#undef FILHDR
struct external_PE_filehdr
{
  /* DOS header fields */
  unsigned short e_magic;	/* Magic number, 0x5a4d */
  unsigned short e_cblp;	/* Bytes on last page of file, 0x90 */
  unsigned short e_cp;		/* Pages in file, 0x3 */
  unsigned short e_crlc;	/* Relocations, 0x0 */
  unsigned short e_cparhdr;	/* Size of header in paragraphs, 0x4 */
  unsigned short e_minalloc;	/* Minimum extra paragraphs needed, 0x0 */
  unsigned short e_maxalloc;	/* Maximum extra paragraphs needed, 0xFFFF */
  unsigned short e_ss;		/* Initial (relative) SS value, 0x0 */
  unsigned short e_sp;		/* Initial SP value, 0xb8 */
  unsigned short e_csum;	/* Checksum, 0x0 */
  unsigned short e_ip;		/* Initial IP value, 0x0 */
  unsigned short e_cs;		/* Initial (relative) CS value, 0x0 */
  unsigned short e_lfarlc;	/* File address of relocation table, 0x40 */
  unsigned short e_ovno;	/* Overlay number, 0x0 */
  char e_res[4][2];		/* Reserved words, all 0x0 */
  unsigned short e_oemid;	/* OEM identifier (for e_oeminfo), 0x0 */
  unsigned short e_oeminfo;	/* OEM information; e_oemid specific, 0x0 */
  char e_res2[10][2];		/* Reserved words, all 0x0 */
  host_ulong e_lfanew;	/* File address of new exe header, 0x80 */
  char dos_message[16][4];	/* other stuff, always follow DOS header */
  unsigned int nt_signature;	/* required NT signature, 0x4550 */

  /* From standard header */

  unsigned short f_magic;	/* magic number			*/
  unsigned short f_nscns;	/* number of sections		*/
  host_ulong f_timdat;	/* time & date stamp		*/
  host_ulong f_symptr;	/* file pointer to symtab	*/
  host_ulong f_nsyms;	/* number of symtab entries	*/
  unsigned short f_opthdr;	/* sizeof(optional hdr)		*/
  unsigned short f_flags;	/* flags			*/
};


#define FILHDR struct external_PE_filehdr
#undef FILHSZ
#define FILHSZ 152

#endif

typedef struct
{
  unsigned short magic;		/* type of file				*/
  unsigned short vstamp;	/* version stamp			*/
  host_ulong	tsize;		/* text size in bytes, padded to FW bdry*/
  host_ulong	dsize;		/* initialized data "  "		*/
  host_ulong	bsize;		/* uninitialized data "   "		*/
  host_ulong	entry;		/* entry pt.				*/
  host_ulong text_start;	/* base of text used for this file */
  host_ulong data_start;	/* base of all data used for this file */

  /* NT extra fields; see internal.h for descriptions */
  host_ulong  ImageBase;
  host_ulong  SectionAlignment;
  host_ulong  FileAlignment;
  unsigned short  MajorOperatingSystemVersion;
  unsigned short  MinorOperatingSystemVersion;
  unsigned short  MajorImageVersion;
  unsigned short  MinorImageVersion;
  unsigned short  MajorSubsystemVersion;
  unsigned short  MinorSubsystemVersion;
  char  Reserved1[4];
  host_ulong  SizeOfImage;
  host_ulong  SizeOfHeaders;
  host_ulong  CheckSum;
  unsigned short Subsystem;
  unsigned short DllCharacteristics;
  host_ulong  SizeOfStackReserve;
  host_ulong  SizeOfStackCommit;
  host_ulong  SizeOfHeapReserve;
  host_ulong  SizeOfHeapCommit;
  host_ulong  LoaderFlags;
  host_ulong  NumberOfRvaAndSizes;
  /* IMAGE_DATA_DIRECTORY DataDirectory[IMAGE_NUMBEROF_DIRECTORY_ENTRIES]; */
  char  DataDirectory[16][2][4]; /* 16 entries, 2 elements/entry, 4 chars */

} PEAOUTHDR;


#undef AOUTSZ
#define AOUTSZ (AOUTHDRSZ + 196)

#undef  E_FILNMLEN
#define E_FILNMLEN	18	/* # characters in a file name		*/
#endif

/* end of coff/pe.h */

#define DT_NON		(0)	/* no derived type */
#define DT_PTR		(1)	/* pointer */
#define DT_FCN		(2)	/* function */
#define DT_ARY		(3)	/* array */

#define ISPTR(x)	(((x) & N_TMASK) == (DT_PTR << N_BTSHFT))
#define ISFCN(x)	(((x) & N_TMASK) == (DT_FCN << N_BTSHFT))
#define ISARY(x)	(((x) & N_TMASK) == (DT_ARY << N_BTSHFT))

#ifdef __cplusplus
}
#endif

#endif /* _A_OUT_H_ */
