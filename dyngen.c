/*
 *  Generic Dynamic compiler generator
 * 
 *  Copyright (c) 2003 Fabrice Bellard
 *
 *  The COFF object format support was extracted from Kazu's QEMU port
 *  to Win32.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>

#include "config-host.h"

#if defined(_WIN32)
#define CONFIG_FORMAT_COFF
#else
#define CONFIG_FORMAT_ELF
#endif

#ifdef CONFIG_FORMAT_ELF

/* elf format definitions. We use these macros to test the CPU to
   allow cross compilation (this tool must be ran on the build
   platform) */
#if defined(HOST_I386)

#define ELF_CLASS	ELFCLASS32
#define ELF_ARCH	EM_386
#define elf_check_arch(x) ( ((x) == EM_386) || ((x) == EM_486) )
#undef ELF_USES_RELOCA

#elif defined(HOST_AMD64)

#define ELF_CLASS	ELFCLASS64
#define ELF_ARCH	EM_X86_64
#define elf_check_arch(x) ((x) == EM_X86_64)
#define ELF_USES_RELOCA

#elif defined(HOST_PPC)

#define ELF_CLASS	ELFCLASS32
#define ELF_ARCH	EM_PPC
#define elf_check_arch(x) ((x) == EM_PPC)
#define ELF_USES_RELOCA

#elif defined(HOST_S390)

#define ELF_CLASS	ELFCLASS32
#define ELF_ARCH	EM_S390
#define elf_check_arch(x) ((x) == EM_S390)
#define ELF_USES_RELOCA

#elif defined(HOST_ALPHA)

#define ELF_CLASS	ELFCLASS64
#define ELF_ARCH	EM_ALPHA
#define elf_check_arch(x) ((x) == EM_ALPHA)
#define ELF_USES_RELOCA

#elif defined(HOST_IA64)

#define ELF_CLASS	ELFCLASS64
#define ELF_ARCH	EM_IA_64
#define elf_check_arch(x) ((x) == EM_IA_64)
#define ELF_USES_RELOCA

#elif defined(HOST_SPARC)

#define ELF_CLASS	ELFCLASS32
#define ELF_ARCH	EM_SPARC
#define elf_check_arch(x) ((x) == EM_SPARC || (x) == EM_SPARC32PLUS)
#define ELF_USES_RELOCA

#elif defined(HOST_SPARC64)

#define ELF_CLASS	ELFCLASS64
#define ELF_ARCH	EM_SPARCV9
#define elf_check_arch(x) ((x) == EM_SPARCV9)
#define ELF_USES_RELOCA

#elif defined(HOST_ARM)

#define ELF_CLASS	ELFCLASS32
#define ELF_ARCH	EM_ARM
#define elf_check_arch(x) ((x) == EM_ARM)
#define ELF_USES_RELOC

#elif defined(HOST_M68K)

#define ELF_CLASS	ELFCLASS32
#define ELF_ARCH	EM_68K
#define elf_check_arch(x) ((x) == EM_68K)
#define ELF_USES_RELOCA

#else
#error unsupported CPU - please update the code
#endif

#include "elf.h"

#if ELF_CLASS == ELFCLASS32
typedef int32_t host_long;
typedef uint32_t host_ulong;
#define swabls(x) swab32s(x)
#else
typedef int64_t host_long;
typedef uint64_t host_ulong;
#define swabls(x) swab64s(x)
#endif

#ifdef ELF_USES_RELOCA
#define SHT_RELOC SHT_RELA
#else
#define SHT_RELOC SHT_REL
#endif

#define EXE_RELOC ELF_RELOC
#define EXE_SYM ElfW(Sym)

#endif /* CONFIG_FORMAT_ELF */

#ifdef CONFIG_FORMAT_COFF

#include "a.out.h"

typedef int32_t host_long;
typedef uint32_t host_ulong;

#define FILENAMELEN 256

typedef struct coff_sym {
    struct external_syment *st_syment;
    char st_name[FILENAMELEN];
    uint32_t st_value;
    int  st_size;
    uint8_t st_type;
    uint8_t st_shndx;
} coff_Sym;

typedef struct coff_rel {
    struct external_reloc *r_reloc;
    int  r_offset;
    uint8_t r_type;
} coff_Rel;

#define EXE_RELOC struct coff_rel
#define EXE_SYM struct coff_sym

#endif /* CONFIG_FORMAT_COFF */

#include "bswap.h"

enum {
    OUT_GEN_OP,
    OUT_CODE,
    OUT_INDEX_OP,
};

/* all dynamically generated functions begin with this code */
#define OP_PREFIX "op_"

int do_swap;

void __attribute__((noreturn)) __attribute__((format (printf, 1, 2))) error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "dyngen: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

void *load_data(int fd, long offset, unsigned int size)
{
    char *data;

    data = malloc(size);
    if (!data)
        return NULL;
    lseek(fd, offset, SEEK_SET);
    if (read(fd, data, size) != size) {
        free(data);
        return NULL;
    }
    return data;
}

int strstart(const char *str, const char *val, const char **ptr)
{
    const char *p, *q;
    p = str;
    q = val;
    while (*q != '\0') {
        if (*p != *q)
            return 0;
        p++;
        q++;
    }
    if (ptr)
        *ptr = p;
    return 1;
}

void pstrcpy(char *buf, int buf_size, const char *str)
{
    int c;
    char *q = buf;

    if (buf_size <= 0)
        return;

    for(;;) {
        c = *str++;
        if (c == 0 || q >= buf + buf_size - 1)
            break;
        *q++ = c;
    }
    *q = '\0';
}

void swab16s(uint16_t *p)
{
    *p = bswap16(*p);
}

void swab32s(uint32_t *p)
{
    *p = bswap32(*p);
}

void swab64s(uint64_t *p)
{
    *p = bswap64(*p);
}

uint16_t get16(uint16_t *p)
{
    uint16_t val;
    val = *p;
    if (do_swap)
        val = bswap16(val);
    return val;
}

uint32_t get32(uint32_t *p)
{
    uint32_t val;
    val = *p;
    if (do_swap)
        val = bswap32(val);
    return val;
}

void put16(uint16_t *p, uint16_t val)
{
    if (do_swap)
        val = bswap16(val);
    *p = val;
}

void put32(uint32_t *p, uint32_t val)
{
    if (do_swap)
        val = bswap32(val);
    *p = val;
}

/* executable information */
EXE_SYM *symtab;
int nb_syms;
int text_shndx;
uint8_t *text;
EXE_RELOC *relocs;
int nb_relocs;

#ifdef CONFIG_FORMAT_ELF

/* ELF file info */
struct elf_shdr *shdr;
uint8_t **sdata;
struct elfhdr ehdr;
char *strtab;

int elf_must_swap(struct elfhdr *h)
{
  union {
      uint32_t i;
      uint8_t b[4];
  } swaptest;

  swaptest.i = 1;
  return (h->e_ident[EI_DATA] == ELFDATA2MSB) != 
      (swaptest.b[0] == 0);
}
  
void elf_swap_ehdr(struct elfhdr *h)
{
    swab16s(&h->e_type);			/* Object file type */
    swab16s(&h->	e_machine);		/* Architecture */
    swab32s(&h->	e_version);		/* Object file version */
    swabls(&h->	e_entry);		/* Entry point virtual address */
    swabls(&h->	e_phoff);		/* Program header table file offset */
    swabls(&h->	e_shoff);		/* Section header table file offset */
    swab32s(&h->	e_flags);		/* Processor-specific flags */
    swab16s(&h->	e_ehsize);		/* ELF header size in bytes */
    swab16s(&h->	e_phentsize);		/* Program header table entry size */
    swab16s(&h->	e_phnum);		/* Program header table entry count */
    swab16s(&h->	e_shentsize);		/* Section header table entry size */
    swab16s(&h->	e_shnum);		/* Section header table entry count */
    swab16s(&h->	e_shstrndx);		/* Section header string table index */
}

void elf_swap_shdr(struct elf_shdr *h)
{
  swab32s(&h->	sh_name);		/* Section name (string tbl index) */
  swab32s(&h->	sh_type);		/* Section type */
  swabls(&h->	sh_flags);		/* Section flags */
  swabls(&h->	sh_addr);		/* Section virtual addr at execution */
  swabls(&h->	sh_offset);		/* Section file offset */
  swabls(&h->	sh_size);		/* Section size in bytes */
  swab32s(&h->	sh_link);		/* Link to another section */
  swab32s(&h->	sh_info);		/* Additional section information */
  swabls(&h->	sh_addralign);		/* Section alignment */
  swabls(&h->	sh_entsize);		/* Entry size if section holds table */
}

void elf_swap_phdr(struct elf_phdr *h)
{
    swab32s(&h->p_type);			/* Segment type */
    swabls(&h->p_offset);		/* Segment file offset */
    swabls(&h->p_vaddr);		/* Segment virtual address */
    swabls(&h->p_paddr);		/* Segment physical address */
    swabls(&h->p_filesz);		/* Segment size in file */
    swabls(&h->p_memsz);		/* Segment size in memory */
    swab32s(&h->p_flags);		/* Segment flags */
    swabls(&h->p_align);		/* Segment alignment */
}

void elf_swap_rel(ELF_RELOC *rel)
{
    swabls(&rel->r_offset);
    swabls(&rel->r_info);
#ifdef ELF_USES_RELOCA
    swabls(&rel->r_addend);
#endif
}

struct elf_shdr *find_elf_section(struct elf_shdr *shdr, int shnum, const char *shstr, 
                                  const char *name)
{
    int i;
    const char *shname;
    struct elf_shdr *sec;

    for(i = 0; i < shnum; i++) {
        sec = &shdr[i];
        if (!sec->sh_name)
            continue;
        shname = shstr + sec->sh_name;
        if (!strcmp(shname, name))
            return sec;
    }
    return NULL;
}

int find_reloc(int sh_index)
{
    struct elf_shdr *sec;
    int i;

    for(i = 0; i < ehdr.e_shnum; i++) {
        sec = &shdr[i];
        if (sec->sh_type == SHT_RELOC && sec->sh_info == sh_index) 
            return i;
    }
    return 0;
}

static char *get_rel_sym_name(EXE_RELOC *rel)
{
    return strtab + symtab[ELFW(R_SYM)(rel->r_info)].st_name;
}

static char *get_sym_name(EXE_SYM *sym)
{
    return strtab + sym->st_name;
}

/* load an elf object file */
int load_object(const char *filename)
{
    int fd;
    struct elf_shdr *sec, *symtab_sec, *strtab_sec, *text_sec;
    int i, j;
    ElfW(Sym) *sym;
    char *shstr;
    ELF_RELOC *rel;
    
    fd = open(filename, O_RDONLY);
    if (fd < 0) 
        error("can't open file '%s'", filename);
    
    /* Read ELF header.  */
    if (read(fd, &ehdr, sizeof (ehdr)) != sizeof (ehdr))
        error("unable to read file header");

    /* Check ELF identification.  */
    if (ehdr.e_ident[EI_MAG0] != ELFMAG0
     || ehdr.e_ident[EI_MAG1] != ELFMAG1
     || ehdr.e_ident[EI_MAG2] != ELFMAG2
     || ehdr.e_ident[EI_MAG3] != ELFMAG3
     || ehdr.e_ident[EI_VERSION] != EV_CURRENT) {
        error("bad ELF header");
    }

    do_swap = elf_must_swap(&ehdr);
    if (do_swap)
        elf_swap_ehdr(&ehdr);
    if (ehdr.e_ident[EI_CLASS] != ELF_CLASS)
        error("Unsupported ELF class");
    if (ehdr.e_type != ET_REL)
        error("ELF object file expected");
    if (ehdr.e_version != EV_CURRENT)
        error("Invalid ELF version");
    if (!elf_check_arch(ehdr.e_machine))
        error("Unsupported CPU (e_machine=%d)", ehdr.e_machine);

    /* read section headers */
    shdr = load_data(fd, ehdr.e_shoff, ehdr.e_shnum * sizeof(struct elf_shdr));
    if (do_swap) {
        for(i = 0; i < ehdr.e_shnum; i++) {
            elf_swap_shdr(&shdr[i]);
        }
    }

    /* read all section data */
    sdata = malloc(sizeof(void *) * ehdr.e_shnum);
    memset(sdata, 0, sizeof(void *) * ehdr.e_shnum);
    
    for(i = 0;i < ehdr.e_shnum; i++) {
        sec = &shdr[i];
        if (sec->sh_type != SHT_NOBITS)
            sdata[i] = load_data(fd, sec->sh_offset, sec->sh_size);
    }

    sec = &shdr[ehdr.e_shstrndx];
    shstr = sdata[ehdr.e_shstrndx];

    /* swap relocations */
    for(i = 0; i < ehdr.e_shnum; i++) {
        sec = &shdr[i];
        if (sec->sh_type == SHT_RELOC) {
            nb_relocs = sec->sh_size / sec->sh_entsize;
            if (do_swap) {
                for(j = 0, rel = (ELF_RELOC *)sdata[i]; j < nb_relocs; j++, rel++)
                    elf_swap_rel(rel);
            }
        }
    }
    /* text section */

    text_sec = find_elf_section(shdr, ehdr.e_shnum, shstr, ".text");
    if (!text_sec)
        error("could not find .text section");
    text_shndx = text_sec - shdr;
    text = sdata[text_shndx];

    /* find text relocations, if any */
    relocs = NULL;
    nb_relocs = 0;
    i = find_reloc(text_shndx);
    if (i != 0) {
        relocs = (ELF_RELOC *)sdata[i];
        nb_relocs = shdr[i].sh_size / shdr[i].sh_entsize;
    }

    symtab_sec = find_elf_section(shdr, ehdr.e_shnum, shstr, ".symtab");
    if (!symtab_sec)
        error("could not find .symtab section");
    strtab_sec = &shdr[symtab_sec->sh_link];

    symtab = (ElfW(Sym) *)sdata[symtab_sec - shdr];
    strtab = sdata[symtab_sec->sh_link];
    
    nb_syms = symtab_sec->sh_size / sizeof(ElfW(Sym));
    if (do_swap) {
        for(i = 0, sym = symtab; i < nb_syms; i++, sym++) {
            swab32s(&sym->st_name);
            swabls(&sym->st_value);
            swabls(&sym->st_size);
            swab16s(&sym->st_shndx);
        }
    }
    close(fd);
    return 0;
}

#endif /* CONFIG_FORMAT_ELF */

#ifdef CONFIG_FORMAT_COFF

/* COFF file info */
struct external_scnhdr *shdr;
uint8_t **sdata;
struct external_filehdr fhdr;
struct external_syment *coff_symtab;
char *strtab;
int coff_text_shndx, coff_data_shndx;

int data_shndx;

#define STRTAB_SIZE 4

#define DIR32   0x06
#define DISP32  0x14

#define T_FUNCTION  0x20
#define C_EXTERNAL  2

void sym_ent_name(struct external_syment *ext_sym, EXE_SYM *sym)
{
    char *q;
    int c, i, len;
    
    if (ext_sym->e.e.e_zeroes != 0) {
        q = sym->st_name;
        for(i = 0; i < 8; i++) {
            c = ext_sym->e.e_name[i];
            if (c == '\0')
                break;
            *q++ = c;
        }
        *q = '\0';
    } else {
        pstrcpy(sym->st_name, sizeof(sym->st_name), strtab + ext_sym->e.e.e_offset);
    }

    /* now convert the name to a C name (suppress the leading '_') */
    if (sym->st_name[0] == '_') {
        len = strlen(sym->st_name);
        memmove(sym->st_name, sym->st_name + 1, len - 1);
        sym->st_name[len - 1] = '\0';
    }
}

char *name_for_dotdata(struct coff_rel *rel)
{
	int i;
	struct coff_sym *sym;
	uint32_t text_data;

	text_data = *(uint32_t *)(text + rel->r_offset);

	for (i = 0, sym = symtab; i < nb_syms; i++, sym++) {
		if (sym->st_syment->e_scnum == data_shndx &&
                    text_data >= sym->st_value &&
                    text_data < sym->st_value + sym->st_size) {
                    
                    return sym->st_name;

		}
	}
	return NULL;
}

static char *get_sym_name(EXE_SYM *sym)
{
    return sym->st_name;
}

static char *get_rel_sym_name(EXE_RELOC *rel)
{
    char *name;
    name = get_sym_name(symtab + *(uint32_t *)(rel->r_reloc->r_symndx));
    if (!strcmp(name, ".data"))
        name = name_for_dotdata(rel);
    return name;
}

struct external_scnhdr *find_coff_section(struct external_scnhdr *shdr, int shnum, const char *name)
{
    int i;
    const char *shname;
    struct external_scnhdr *sec;

    for(i = 0; i < shnum; i++) {
        sec = &shdr[i];
        if (!sec->s_name)
            continue;
        shname = sec->s_name;
        if (!strcmp(shname, name))
            return sec;
    }
    return NULL;
}

/* load a coff object file */
int load_object(const char *filename)
{
    int fd;
    struct external_scnhdr *sec, *text_sec, *data_sec;
    int i;
    struct external_syment *ext_sym;
    struct external_reloc *coff_relocs;
    struct external_reloc *ext_rel;
    uint32_t *n_strtab;
    EXE_SYM *sym;
    EXE_RELOC *rel;
	
    fd = open(filename, O_RDONLY 
#ifdef _WIN32
              | O_BINARY
#endif
              );
    if (fd < 0) 
        error("can't open file '%s'", filename);
    
    /* Read COFF header.  */
    if (read(fd, &fhdr, sizeof (fhdr)) != sizeof (fhdr))
        error("unable to read file header");

    /* Check COFF identification.  */
    if (fhdr.f_magic != I386MAGIC) {
        error("bad COFF header");
    }
    do_swap = 0;

    /* read section headers */
    shdr = load_data(fd, sizeof(struct external_filehdr) + fhdr.f_opthdr, fhdr.f_nscns * sizeof(struct external_scnhdr));
	
    /* read all section data */
    sdata = malloc(sizeof(void *) * fhdr.f_nscns);
    memset(sdata, 0, sizeof(void *) * fhdr.f_nscns);
    
    const char *p;
    for(i = 0;i < fhdr.f_nscns; i++) {
        sec = &shdr[i];
        if (!strstart(sec->s_name,  ".bss", &p))
            sdata[i] = load_data(fd, sec->s_scnptr, sec->s_size);
    }


    /* text section */
    text_sec = find_coff_section(shdr, fhdr.f_nscns, ".text");
    if (!text_sec)
        error("could not find .text section");
    coff_text_shndx = text_sec - shdr;
    text = sdata[coff_text_shndx];

    /* data section */
    data_sec = find_coff_section(shdr, fhdr.f_nscns, ".data");
    if (!data_sec)
        error("could not find .data section");
    coff_data_shndx = data_sec - shdr;
    
    coff_symtab = load_data(fd, fhdr.f_symptr, fhdr.f_nsyms*SYMESZ);
    for (i = 0, ext_sym = coff_symtab; i < nb_syms; i++, ext_sym++) {
        for(i=0;i<8;i++)
            printf(" %02x", ((uint8_t *)ext_sym->e.e_name)[i]);
        printf("\n");
    }


    n_strtab = load_data(fd, (fhdr.f_symptr + fhdr.f_nsyms*SYMESZ), STRTAB_SIZE);
    strtab = load_data(fd, (fhdr.f_symptr + fhdr.f_nsyms*SYMESZ), *n_strtab); 
    
    nb_syms = fhdr.f_nsyms;

    for (i = 0, ext_sym = coff_symtab; i < nb_syms; i++, ext_sym++) {
      if (strstart(ext_sym->e.e_name, ".text", NULL))
		  text_shndx = ext_sym->e_scnum;
	  if (strstart(ext_sym->e.e_name, ".data", NULL))
		  data_shndx = ext_sym->e_scnum;
    }

	/* set coff symbol */
	symtab = malloc(sizeof(struct coff_sym) * nb_syms);

	int aux_size, j;
	for (i = 0, ext_sym = coff_symtab, sym = symtab; i < nb_syms; i++, ext_sym++, sym++) {
		memset(sym, 0, sizeof(*sym));
		sym->st_syment = ext_sym;
		sym_ent_name(ext_sym, sym);
		sym->st_value = ext_sym->e_value;

		aux_size = *(int8_t *)ext_sym->e_numaux;
		if (ext_sym->e_scnum == text_shndx && ext_sym->e_type == T_FUNCTION) {
			for (j = aux_size + 1; j < nb_syms - i; j++) {
				if ((ext_sym + j)->e_scnum == text_shndx &&
					(ext_sym + j)->e_type == T_FUNCTION ){
					sym->st_size = (ext_sym + j)->e_value - ext_sym->e_value;
					break;
				} else if (j == nb_syms - i - 1) {
					sec = &shdr[coff_text_shndx];
					sym->st_size = sec->s_size - ext_sym->e_value;
					break;
				}
			}
		} else if (ext_sym->e_scnum == data_shndx && *(uint8_t *)ext_sym->e_sclass == C_EXTERNAL) {
			for (j = aux_size + 1; j < nb_syms - i; j++) {
				if ((ext_sym + j)->e_scnum == data_shndx) {
					sym->st_size = (ext_sym + j)->e_value - ext_sym->e_value;
					break;
				} else if (j == nb_syms - i - 1) {
					sec = &shdr[coff_data_shndx];
					sym->st_size = sec->s_size - ext_sym->e_value;
					break;
				}
			}
		} else {
			sym->st_size = 0;
		}
		
		sym->st_type = ext_sym->e_type;
		sym->st_shndx = ext_sym->e_scnum;
	}

		
    /* find text relocations, if any */
    sec = &shdr[coff_text_shndx];
    coff_relocs = load_data(fd, sec->s_relptr, sec->s_nreloc*RELSZ);
    nb_relocs = sec->s_nreloc;

    /* set coff relocation */
    relocs = malloc(sizeof(struct coff_rel) * nb_relocs);
    for (i = 0, ext_rel = coff_relocs, rel = relocs; i < nb_relocs; 
         i++, ext_rel++, rel++) {
        memset(rel, 0, sizeof(*rel));
        rel->r_reloc = ext_rel;
        rel->r_offset = *(uint32_t *)ext_rel->r_vaddr;
        rel->r_type = *(uint16_t *)ext_rel->r_type;
    }
    return 0;
}

#endif /* CONFIG_FORMAT_COFF */

#ifdef HOST_ARM

int arm_emit_ldr_info(const char *name, unsigned long start_offset,
                      FILE *outfile, uint8_t *p_start, uint8_t *p_end,
                      ELF_RELOC *relocs, int nb_relocs)
{
    uint8_t *p;
    uint32_t insn;
    int offset, min_offset, pc_offset, data_size;
    uint8_t data_allocated[1024];
    unsigned int data_index;
    
    memset(data_allocated, 0, sizeof(data_allocated));
    
    p = p_start;
    min_offset = p_end - p_start;
    while (p < p_start + min_offset) {
        insn = get32((uint32_t *)p);
        if ((insn & 0x0d5f0000) == 0x051f0000) {
            /* ldr reg, [pc, #im] */
            offset = insn & 0xfff;
            if (!(insn & 0x00800000))
                        offset = -offset;
            if ((offset & 3) !=0)
                error("%s:%04x: ldr pc offset must be 32 bit aligned", 
                      name, start_offset + p - p_start);
            pc_offset = p - p_start + offset + 8;
            if (pc_offset <= (p - p_start) || 
                pc_offset >= (p_end - p_start))
                error("%s:%04x: ldr pc offset must point inside the function code", 
                      name, start_offset + p - p_start);
            if (pc_offset < min_offset)
                min_offset = pc_offset;
            if (outfile) {
                /* ldr position */
                fprintf(outfile, "    arm_ldr_ptr->ptr = gen_code_ptr + %d;\n", 
                        p - p_start);
                /* ldr data index */
                data_index = ((p_end - p_start) - pc_offset - 4) >> 2;
                fprintf(outfile, "    arm_ldr_ptr->data_ptr = arm_data_ptr + %d;\n", 
                        data_index);
                fprintf(outfile, "    arm_ldr_ptr++;\n");
                if (data_index >= sizeof(data_allocated))
                    error("%s: too many data", name);
                if (!data_allocated[data_index]) {
                    ELF_RELOC *rel;
                    int i, addend, type;
                    const char *sym_name, *p;
                    char relname[1024];

                    data_allocated[data_index] = 1;

                    /* data value */
                    addend = get32((uint32_t *)(p_start + pc_offset));
                    relname[0] = '\0';
                    for(i = 0, rel = relocs;i < nb_relocs; i++, rel++) {
                        if (rel->r_offset == (pc_offset + start_offset)) {
                            sym_name = get_rel_sym_name(rel);
                            /* the compiler leave some unnecessary references to the code */
                            if (strstart(sym_name, "__op_param", &p)) {
                                snprintf(relname, sizeof(relname), "param%s", p);
                            } else {
                                snprintf(relname, sizeof(relname), "(long)(&%s)", sym_name);
                            }
                            type = ELF32_R_TYPE(rel->r_info);
                            if (type != R_ARM_ABS32)
                                error("%s: unsupported data relocation", name);
                            break;
                        }
                    }
                    fprintf(outfile, "    arm_data_ptr[%d] = 0x%x",
                            data_index, addend);
                    if (relname[0] != '\0')
                        fprintf(outfile, " + %s", relname);
                    fprintf(outfile, ";\n");
                }
            }
        }
        p += 4;
    }
    data_size = (p_end - p_start) - min_offset;
    if (data_size > 0 && outfile) {
        fprintf(outfile, "    arm_data_ptr += %d;\n", data_size >> 2);
    }

    /* the last instruction must be a mov pc, lr */
    if (p == p_start)
        goto arm_ret_error;
    p -= 4;
    insn = get32((uint32_t *)p);
    if ((insn & 0xffff0000) != 0xe91b0000) {
    arm_ret_error:
        if (!outfile)
            printf("%s: invalid epilog\n", name);
    }
    return p - p_start;	    
}
#endif


#define MAX_ARGS 3

/* generate op code */
void gen_code(const char *name, host_ulong offset, host_ulong size, 
              FILE *outfile, int gen_switch)
{
    int copy_size = 0;
    uint8_t *p_start, *p_end;
    host_ulong start_offset;
    int nb_args, i, n;
    uint8_t args_present[MAX_ARGS];
    const char *sym_name, *p;
    EXE_RELOC *rel;

    /* Compute exact size excluding prologue and epilogue instructions.
     * Increment start_offset to skip epilogue instructions, then compute
     * copy_size the indicate the size of the remaining instructions (in
     * bytes).
     */
    p_start = text + offset;
    p_end = p_start + size;
    start_offset = offset;
#if defined(HOST_I386) || defined(HOST_AMD64)
#ifdef CONFIG_FORMAT_COFF
    {
        uint8_t *p;
        p = p_end - 1;
        if (p == p_start)
            error("empty code for %s", name);
        while (*p != 0xc3) {
            p--;
            if (p <= p_start)
                error("ret or jmp expected at the end of %s", name);
        }
        copy_size = p - p_start;
    }
#else
    {
        int len;
        len = p_end - p_start;
        if (len == 0)
            error("empty code for %s", name);
        if (p_end[-1] == 0xc3) {
            len--;
        } else {
            error("ret or jmp expected at the end of %s", name);
        }
        copy_size = len;
    }
#endif    
#elif defined(HOST_PPC)
    {
        uint8_t *p;
        p = (void *)(p_end - 4);
        if (p == p_start)
            error("empty code for %s", name);
        if (get32((uint32_t *)p) != 0x4e800020)
            error("blr expected at the end of %s", name);
        copy_size = p - p_start;
    }
#elif defined(HOST_S390)
    {
        uint8_t *p;
        p = (void *)(p_end - 2);
        if (p == p_start)
            error("empty code for %s", name);
        if (get16((uint16_t *)p) != 0x07fe && get16((uint16_t *)p) != 0x07f4)
            error("br %%r14 expected at the end of %s", name);
        copy_size = p - p_start;
    }
#elif defined(HOST_ALPHA)
    {
        uint8_t *p;
        p = p_end - 4;
#if 0
        /* XXX: check why it occurs */
        if (p == p_start)
            error("empty code for %s", name);
#endif
        if (get32((uint32_t *)p) != 0x6bfa8001)
            error("ret expected at the end of %s", name);
        copy_size = p - p_start;	    
    }
#elif defined(HOST_IA64)
    {
        uint8_t *p;
        p = (void *)(p_end - 4);
        if (p == p_start)
            error("empty code for %s", name);
        /* br.ret.sptk.many b0;; */
        /* 08 00 84 00 */
        if (get32((uint32_t *)p) != 0x00840008)
            error("br.ret.sptk.many b0;; expected at the end of %s", name);
        copy_size = p - p_start;
    }
#elif defined(HOST_SPARC)
    {
        uint32_t start_insn, end_insn1, end_insn2;
        uint8_t *p;
        p = (void *)(p_end - 8);
        if (p <= p_start)
            error("empty code for %s", name);
        start_insn = get32((uint32_t *)(p_start + 0x0));
        end_insn1 = get32((uint32_t *)(p + 0x0));
        end_insn2 = get32((uint32_t *)(p + 0x4));
        if ((start_insn & ~0x1fff) == 0x9de3a000) {
            p_start += 0x4;
            start_offset += 0x4;
            if ((int)(start_insn | ~0x1fff) < -128)
                error("Found bogus save at the start of %s", name);
            if (end_insn1 != 0x81c7e008 || end_insn2 != 0x81e80000)
                error("ret; restore; not found at end of %s", name);
        } else {
            error("No save at the beginning of %s", name);
        }
#if 0
        /* Skip a preceeding nop, if present.  */
        if (p > p_start) {
            skip_insn = get32((uint32_t *)(p - 0x4));
            if (skip_insn == 0x01000000)
                p -= 4;
        }
#endif
        copy_size = p - p_start;
    }
#elif defined(HOST_SPARC64)
    {
        uint32_t start_insn, end_insn1, end_insn2, skip_insn;
        uint8_t *p;
        p = (void *)(p_end - 8);
        if (p <= p_start)
            error("empty code for %s", name);
        start_insn = get32((uint32_t *)(p_start + 0x0));
        end_insn1 = get32((uint32_t *)(p + 0x0));
        end_insn2 = get32((uint32_t *)(p + 0x4));
        if ((start_insn & ~0x1fff) == 0x9de3a000) {
            p_start += 0x4;
            start_offset += 0x4;
            if ((int)(start_insn | ~0x1fff) < -256)
                error("Found bogus save at the start of %s", name);
            if (end_insn1 != 0x81c7e008 || end_insn2 != 0x81e80000)
                error("ret; restore; not found at end of %s", name);
        } else {
            error("No save at the beginning of %s", name);
        }
        
        /* Skip a preceeding nop, if present.  */
        if (p > p_start) {
            skip_insn = get32((uint32_t *)(p - 0x4));
            if (skip_insn == 0x01000000)
                p -= 4;
        }
        
        copy_size = p - p_start;
    }
#elif defined(HOST_ARM)
    {
        if ((p_end - p_start) <= 16)
            error("%s: function too small", name);
        if (get32((uint32_t *)p_start) != 0xe1a0c00d ||
            (get32((uint32_t *)(p_start + 4)) & 0xffff0000) != 0xe92d0000 ||
            get32((uint32_t *)(p_start + 8)) != 0xe24cb004)
            error("%s: invalid prolog", name);
        p_start += 12;
        start_offset += 12;
        copy_size = arm_emit_ldr_info(name, start_offset, NULL, p_start, p_end, 
                                      relocs, nb_relocs);
    }
#elif defined(HOST_M68K)
    {
        uint8_t *p;
        p = (void *)(p_end - 2);
        if (p == p_start)
            error("empty code for %s", name);
        // remove NOP's, probably added for alignment
        while ((get16((uint16_t *)p) == 0x4e71) &&
               (p>p_start)) 
            p -= 2;
        if (get16((uint16_t *)p) != 0x4e75)
            error("rts expected at the end of %s", name);
        copy_size = p - p_start;
    }
#else
#error unsupported CPU
#endif

    /* compute the number of arguments by looking at the relocations */
    for(i = 0;i < MAX_ARGS; i++)
        args_present[i] = 0;

    for(i = 0, rel = relocs;i < nb_relocs; i++, rel++) {
        if (rel->r_offset >= start_offset &&
	    rel->r_offset < start_offset + (p_end - p_start)) {
            sym_name = get_rel_sym_name(rel);
            if (strstart(sym_name, "__op_param", &p)) {
                n = strtoul(p, NULL, 10);
                if (n > MAX_ARGS)
                    error("too many arguments in %s", name);
                args_present[n - 1] = 1;
            }
        }
    }
    
    nb_args = 0;
    while (nb_args < MAX_ARGS && args_present[nb_args])
        nb_args++;
    for(i = nb_args; i < MAX_ARGS; i++) {
        if (args_present[i])
            error("inconsistent argument numbering in %s", name);
    }

    if (gen_switch == 2) {
        fprintf(outfile, "DEF(%s, %d, %d)\n", name + 3, nb_args, copy_size);
    } else if (gen_switch == 1) {

        /* output C code */
        fprintf(outfile, "case INDEX_%s: {\n", name);
        if (nb_args > 0) {
            fprintf(outfile, "    long ");
            for(i = 0; i < nb_args; i++) {
                if (i != 0)
                    fprintf(outfile, ", ");
                fprintf(outfile, "param%d", i + 1);
            }
            fprintf(outfile, ";\n");
        }
        fprintf(outfile, "    extern void %s();\n", name);

        for(i = 0, rel = relocs;i < nb_relocs; i++, rel++) {
            if (rel->r_offset >= start_offset &&
		rel->r_offset < start_offset + (p_end - p_start)) {
                sym_name = get_rel_sym_name(rel);
                if (*sym_name && 
                    !strstart(sym_name, "__op_param", NULL) &&
                    !strstart(sym_name, "__op_jmp", NULL)) {
#if defined(HOST_SPARC)
		    if (sym_name[0] == '.') {
			fprintf(outfile,
				"extern char __dot_%s __asm__(\"%s\");\n",
				sym_name+1, sym_name);
			continue;
		    }
#endif
                    fprintf(outfile, "extern char %s;\n", sym_name);
                }
            }
        }

        fprintf(outfile, "    memcpy(gen_code_ptr, (void *)((char *)&%s+%d), %d);\n", name, start_offset - offset, copy_size);

        /* emit code offset information */
        {
            EXE_SYM *sym;
            const char *sym_name, *p;
            unsigned long val;
            int n;

            for(i = 0, sym = symtab; i < nb_syms; i++, sym++) {
                sym_name = get_sym_name(sym);
                if (strstart(sym_name, "__op_label", &p)) {
                    uint8_t *ptr;
                    unsigned long offset;
                    
                    /* test if the variable refers to a label inside
                       the code we are generating */
#ifdef CONFIG_FORMAT_COFF
                    if (sym->st_shndx == text_shndx) {
                        ptr = sdata[coff_text_shndx];
                    } else if (sym->st_shndx == data_shndx) {
                        ptr = sdata[coff_data_shndx];
                    } else {
                        ptr = NULL;
                    }
#else
                    ptr = sdata[sym->st_shndx];
#endif
                    if (!ptr)
                        error("__op_labelN in invalid section");
                    offset = sym->st_value;
                    val = *(unsigned long *)(ptr + offset);
#ifdef ELF_USES_RELOCA
                    {
                        int reloc_shndx, nb_relocs1, j;

                        /* try to find a matching relocation */
                        reloc_shndx = find_reloc(sym->st_shndx);
                        if (reloc_shndx) {
                            nb_relocs1 = shdr[reloc_shndx].sh_size / 
                                shdr[reloc_shndx].sh_entsize;
                            rel = (ELF_RELOC *)sdata[reloc_shndx];
                            for(j = 0; j < nb_relocs1; j++) {
                                if (rel->r_offset == offset) {
				    val = rel->r_addend;
                                    break;
                                }
				rel++;
                            }
                        }
                    }
#endif                    

                    if (val >= start_offset && val < start_offset + copy_size) {
                        n = strtol(p, NULL, 10);
                        fprintf(outfile, "    label_offsets[%d] = %ld + (gen_code_ptr - gen_code_buf);\n", n, val - start_offset);
                    }
                }
            }
        }

        /* load parameres in variables */
        for(i = 0; i < nb_args; i++) {
            fprintf(outfile, "    param%d = *opparam_ptr++;\n", i + 1);
        }

        /* patch relocations */
#if defined(HOST_I386)
            {
                char name[256];
                int type;
                int addend;
                for(i = 0, rel = relocs;i < nb_relocs; i++, rel++) {
                if (rel->r_offset >= start_offset &&
		    rel->r_offset < start_offset + copy_size) {
                    sym_name = get_rel_sym_name(rel);
                    if (strstart(sym_name, "__op_jmp", &p)) {
                        int n;
                        n = strtol(p, NULL, 10);
                        /* __op_jmp relocations are done at
                           runtime to do translated block
                           chaining: the offset of the instruction
                           needs to be stored */
                        fprintf(outfile, "    jmp_offsets[%d] = %d + (gen_code_ptr - gen_code_buf);\n",
                                n, rel->r_offset - start_offset);
                        continue;
                    }
                        
                    if (strstart(sym_name, "__op_param", &p)) {
                        snprintf(name, sizeof(name), "param%s", p);
                    } else {
                        snprintf(name, sizeof(name), "(long)(&%s)", sym_name);
                    }
                    addend = get32((uint32_t *)(text + rel->r_offset));
#ifdef CONFIG_FORMAT_ELF
                    type = ELF32_R_TYPE(rel->r_info);
                    switch(type) {
                    case R_386_32:
                        fprintf(outfile, "    *(uint32_t *)(gen_code_ptr + %d) = %s + %d;\n", 
                                rel->r_offset - start_offset, name, addend);
                        break;
                    case R_386_PC32:
                        fprintf(outfile, "    *(uint32_t *)(gen_code_ptr + %d) = %s - (long)(gen_code_ptr + %d) + %d;\n", 
                                rel->r_offset - start_offset, name, rel->r_offset - start_offset, addend);
                        break;
                    default:
                        error("unsupported i386 relocation (%d)", type);
                    }
#elif defined(CONFIG_FORMAT_COFF)
                    type = rel->r_type;
                    switch(type) {
                    case DIR32:
                        fprintf(outfile, "    *(uint32_t *)(gen_code_ptr + %d) = %s + %d;\n", 
                                rel->r_offset - start_offset, name, addend);
                        break;
                    case DISP32:
                        fprintf(outfile, "    *(uint32_t *)(gen_code_ptr + %d) = %s - (long)(gen_code_ptr + %d) + %d -4;\n", 
                                rel->r_offset - start_offset, name, rel->r_offset - start_offset, addend);
                        break;
                    default:
                        error("unsupported i386 relocation (%d)", type);
                    }
#else
#error unsupport object format
#endif
                }
                }
            }
#elif defined(HOST_AMD64)
            {
                char name[256];
                int type;
                int addend;
                for(i = 0, rel = relocs;i < nb_relocs; i++, rel++) {
                if (rel->r_offset >= start_offset &&
		    rel->r_offset < start_offset + copy_size) {
                    sym_name = strtab + symtab[ELFW(R_SYM)(rel->r_info)].st_name;
                    if (strstart(sym_name, "__op_param", &p)) {
                        snprintf(name, sizeof(name), "param%s", p);
                    } else {
                        snprintf(name, sizeof(name), "(long)(&%s)", sym_name);
                    }
                    type = ELF32_R_TYPE(rel->r_info);
                    addend = rel->r_addend;
                    switch(type) {
                    case R_X86_64_32:
                        fprintf(outfile, "    *(uint32_t *)(gen_code_ptr + %d) = (uint32_t)%s + %d;\n", 
                                rel->r_offset - start_offset, name, addend);
                        break;
                    case R_X86_64_32S:
                        fprintf(outfile, "    *(uint32_t *)(gen_code_ptr + %d) = (int32_t)%s + %d;\n", 
                                rel->r_offset - start_offset, name, addend);
                        break;
                    case R_X86_64_PC32:
                        fprintf(outfile, "    *(uint32_t *)(gen_code_ptr + %d) = %s - (long)(gen_code_ptr + %d) + %d;\n", 
                                rel->r_offset - start_offset, name, rel->r_offset - start_offset, addend);
                        break;
                    default:
                        error("unsupported AMD64 relocation (%d)", type);
                    }
                }
                }
            }
#elif defined(HOST_PPC)
            {
                char name[256];
                int type;
                int addend;
                for(i = 0, rel = relocs;i < nb_relocs; i++, rel++) {
                    if (rel->r_offset >= start_offset &&
			rel->r_offset < start_offset + copy_size) {
                        sym_name = strtab + symtab[ELFW(R_SYM)(rel->r_info)].st_name;
                        if (strstart(sym_name, "__op_jmp", &p)) {
                            int n;
                            n = strtol(p, NULL, 10);
                            /* __op_jmp relocations are done at
                               runtime to do translated block
                               chaining: the offset of the instruction
                               needs to be stored */
                            fprintf(outfile, "    jmp_offsets[%d] = %d + (gen_code_ptr - gen_code_buf);\n",
                                    n, rel->r_offset - start_offset);
                            continue;
                        }
                        
                        if (strstart(sym_name, "__op_param", &p)) {
                            snprintf(name, sizeof(name), "param%s", p);
                        } else {
                            snprintf(name, sizeof(name), "(long)(&%s)", sym_name);
                        }
                        type = ELF32_R_TYPE(rel->r_info);
                        addend = rel->r_addend;
                        switch(type) {
                        case R_PPC_ADDR32:
                            fprintf(outfile, "    *(uint32_t *)(gen_code_ptr + %d) = %s + %d;\n", 
                                    rel->r_offset - start_offset, name, addend);
                            break;
                        case R_PPC_ADDR16_LO:
                            fprintf(outfile, "    *(uint16_t *)(gen_code_ptr + %d) = (%s + %d);\n", 
                                    rel->r_offset - start_offset, name, addend);
                            break;
                        case R_PPC_ADDR16_HI:
                            fprintf(outfile, "    *(uint16_t *)(gen_code_ptr + %d) = (%s + %d) >> 16;\n", 
                                    rel->r_offset - start_offset, name, addend);
                            break;
                        case R_PPC_ADDR16_HA:
                            fprintf(outfile, "    *(uint16_t *)(gen_code_ptr + %d) = (%s + %d + 0x8000) >> 16;\n", 
                                    rel->r_offset - start_offset, name, addend);
                            break;
                        case R_PPC_REL24:
                            /* warning: must be at 32 MB distancy */
                            fprintf(outfile, "    *(uint32_t *)(gen_code_ptr + %d) = (*(uint32_t *)(gen_code_ptr + %d) & ~0x03fffffc) | ((%s - (long)(gen_code_ptr + %d) + %d) & 0x03fffffc);\n", 
                                    rel->r_offset - start_offset, rel->r_offset - start_offset, name, rel->r_offset - start_offset, addend);
                            break;
                        default:
                            error("unsupported powerpc relocation (%d)", type);
                        }
                    }
                }
            }
#elif defined(HOST_S390)
            {
                char name[256];
                int type;
                int addend;
                for(i = 0, rel = relocs;i < nb_relocs; i++, rel++) {
                    if (rel->r_offset >= start_offset &&
			rel->r_offset < start_offset + copy_size) {
                        sym_name = strtab + symtab[ELFW(R_SYM)(rel->r_info)].st_name;
                        if (strstart(sym_name, "__op_param", &p)) {
                            snprintf(name, sizeof(name), "param%s", p);
                        } else {
                            snprintf(name, sizeof(name), "(long)(&%s)", sym_name);
                        }
                        type = ELF32_R_TYPE(rel->r_info);
                        addend = rel->r_addend;
                        switch(type) {
                        case R_390_32:
                            fprintf(outfile, "    *(uint32_t *)(gen_code_ptr + %d) = %s + %d;\n", 
                                    rel->r_offset - start_offset, name, addend);
                            break;
                        case R_390_16:
                            fprintf(outfile, "    *(uint16_t *)(gen_code_ptr + %d) = %s + %d;\n", 
                                    rel->r_offset - start_offset, name, addend);
                            break;
                        case R_390_8:
                            fprintf(outfile, "    *(uint8_t *)(gen_code_ptr + %d) = %s + %d;\n", 
                                    rel->r_offset - start_offset, name, addend);
                            break;
                        default:
                            error("unsupported s390 relocation (%d)", type);
                        }
                    }
                }
            }
#elif defined(HOST_ALPHA)
            {
                for (i = 0, rel = relocs; i < nb_relocs; i++, rel++) {
		    if (rel->r_offset >= start_offset && rel->r_offset < start_offset + copy_size) {
			int type;

			type = ELF64_R_TYPE(rel->r_info);
			sym_name = strtab + symtab[ELF64_R_SYM(rel->r_info)].st_name;
			switch (type) {
			case R_ALPHA_GPDISP:
			    /* The gp is just 32 bit, and never changes, so it's easiest to emit it
			       as an immediate instead of constructing it from the pv or ra.  */
			    fprintf(outfile, "    immediate_ldah(gen_code_ptr + %ld, gp);\n",
				    rel->r_offset - start_offset);
			    fprintf(outfile, "    immediate_lda(gen_code_ptr + %ld, gp);\n",
				    rel->r_offset - start_offset + rel->r_addend);
			    break;
			case R_ALPHA_LITUSE:
			    /* jsr to literal hint. Could be used to optimize to bsr. Ignore for
			       now, since some called functions (libc) need pv to be set up.  */
			    break;
			case R_ALPHA_HINT:
			    /* Branch target prediction hint. Ignore for now.  Should be already
			       correct for in-function jumps.  */
			    break;
			case R_ALPHA_LITERAL:
			    /* Load a literal from the GOT relative to the gp.  Since there's only a
			       single gp, nothing is to be done.  */
			    break;
			case R_ALPHA_GPRELHIGH:
			    /* Handle fake relocations against __op_param symbol.  Need to emit the
			       high part of the immediate value instead.  Other symbols need no
			       special treatment.  */
			    if (strstart(sym_name, "__op_param", &p))
				fprintf(outfile, "    immediate_ldah(gen_code_ptr + %ld, param%s);\n",
					rel->r_offset - start_offset, p);
			    break;
			case R_ALPHA_GPRELLOW:
			    if (strstart(sym_name, "__op_param", &p))
				fprintf(outfile, "    immediate_lda(gen_code_ptr + %ld, param%s);\n",
					rel->r_offset - start_offset, p);
			    break;
			case R_ALPHA_BRSGP:
			    /* PC-relative jump. Tweak offset to skip the two instructions that try to
			       set up the gp from the pv.  */
			    fprintf(outfile, "    fix_bsr(gen_code_ptr + %ld, (uint8_t *) &%s - (gen_code_ptr + %ld + 4) + 8);\n",
				    rel->r_offset - start_offset, sym_name, rel->r_offset - start_offset);
			    break;
			default:
			    error("unsupported Alpha relocation (%d)", type);
			}
		    }
                }
            }
#elif defined(HOST_IA64)
            {
                char name[256];
                int type;
                int addend;
                for(i = 0, rel = relocs;i < nb_relocs; i++, rel++) {
                    if (rel->r_offset >= start_offset && rel->r_offset < start_offset + copy_size) {
                        sym_name = strtab + symtab[ELF64_R_SYM(rel->r_info)].st_name;
                        if (strstart(sym_name, "__op_param", &p)) {
                            snprintf(name, sizeof(name), "param%s", p);
                        } else {
                            snprintf(name, sizeof(name), "(long)(&%s)", sym_name);
                        }
                        type = ELF64_R_TYPE(rel->r_info);
                        addend = rel->r_addend;
                        switch(type) {
			case R_IA64_LTOFF22:
			    error("must implemnt R_IA64_LTOFF22 relocation");
			case R_IA64_PCREL21B:
			    error("must implemnt R_IA64_PCREL21B relocation");
                        default:
                            error("unsupported ia64 relocation (%d)", type);
                        }
                    }
                }
            }
#elif defined(HOST_SPARC)
            {
                char name[256];
                int type;
                int addend;
                for(i = 0, rel = relocs;i < nb_relocs; i++, rel++) {
                    if (rel->r_offset >= start_offset &&
			rel->r_offset < start_offset + copy_size) {
                        sym_name = strtab + symtab[ELF32_R_SYM(rel->r_info)].st_name;
                        if (strstart(sym_name, "__op_param", &p)) {
                            snprintf(name, sizeof(name), "param%s", p);
                        } else {
				if (sym_name[0] == '.')
					snprintf(name, sizeof(name),
						 "(long)(&__dot_%s)",
						 sym_name + 1);
				else
					snprintf(name, sizeof(name),
						 "(long)(&%s)", sym_name);
                        }
                        type = ELF32_R_TYPE(rel->r_info);
                        addend = rel->r_addend;
                        switch(type) {
                        case R_SPARC_32:
                            fprintf(outfile, "    *(uint32_t *)(gen_code_ptr + %d) = %s + %d;\n", 
                                    rel->r_offset - start_offset, name, addend);
			    break;
			case R_SPARC_HI22:
                            fprintf(outfile,
				    "    *(uint32_t *)(gen_code_ptr + %d) = "
				    "((*(uint32_t *)(gen_code_ptr + %d)) "
				    " & ~0x3fffff) "
				    " | (((%s + %d) >> 10) & 0x3fffff);\n",
                                    rel->r_offset - start_offset,
				    rel->r_offset - start_offset,
				    name, addend);
			    break;
			case R_SPARC_LO10:
                            fprintf(outfile,
				    "    *(uint32_t *)(gen_code_ptr + %d) = "
				    "((*(uint32_t *)(gen_code_ptr + %d)) "
				    " & ~0x3ff) "
				    " | ((%s + %d) & 0x3ff);\n",
                                    rel->r_offset - start_offset,
				    rel->r_offset - start_offset,
				    name, addend);
			    break;
			case R_SPARC_WDISP30:
			    fprintf(outfile,
				    "    *(uint32_t *)(gen_code_ptr + %d) = "
				    "((*(uint32_t *)(gen_code_ptr + %d)) "
				    " & ~0x3fffffff) "
				    " | ((((%s + %d) - (long)(gen_code_ptr + %d))>>2) "
				    "    & 0x3fffffff);\n",
				    rel->r_offset - start_offset,
				    rel->r_offset - start_offset,
				    name, addend,
				    rel->r_offset - start_offset);
			    break;
                        default:
                            error("unsupported sparc relocation (%d)", type);
                        }
                    }
                }
            }
#elif defined(HOST_SPARC64)
            {
                char name[256];
                int type;
                int addend;
                for(i = 0, rel = relocs;i < nb_relocs; i++, rel++) {
                    if (rel->r_offset >= start_offset &&
			rel->r_offset < start_offset + copy_size) {
                        sym_name = strtab + symtab[ELF64_R_SYM(rel->r_info)].st_name;
                        if (strstart(sym_name, "__op_param", &p)) {
                            snprintf(name, sizeof(name), "param%s", p);
                        } else {
                            snprintf(name, sizeof(name), "(long)(&%s)", sym_name);
                        }
                        type = ELF64_R_TYPE(rel->r_info);
                        addend = rel->r_addend;
                        switch(type) {
                        case R_SPARC_32:
                            fprintf(outfile, "    *(uint32_t *)(gen_code_ptr + %d) = %s + %d;\n",
                                    rel->r_offset - start_offset, name, addend);
			    break;
			case R_SPARC_HI22:
                            fprintf(outfile,
				    "    *(uint32_t *)(gen_code_ptr + %d) = "
				    "((*(uint32_t *)(gen_code_ptr + %d)) "
				    " & ~0x3fffff) "
				    " | (((%s + %d) >> 10) & 0x3fffff);\n",
                                    rel->r_offset - start_offset,
				    rel->r_offset - start_offset,
				    name, addend);
			    break;
			case R_SPARC_LO10:
                            fprintf(outfile,
				    "    *(uint32_t *)(gen_code_ptr + %d) = "
				    "((*(uint32_t *)(gen_code_ptr + %d)) "
				    " & ~0x3ff) "
				    " | ((%s + %d) & 0x3ff);\n",
                                    rel->r_offset - start_offset,
				    rel->r_offset - start_offset,
				    name, addend);
			    break;
			case R_SPARC_WDISP30:
			    fprintf(outfile,
				    "    *(uint32_t *)(gen_code_ptr + %d) = "
				    "((*(uint32_t *)(gen_code_ptr + %d)) "
				    " & ~0x3fffffff) "
				    " | ((((%s + %d) - (long)(gen_code_ptr + %d))>>2) "
				    "    & 0x3fffffff);\n",
				    rel->r_offset - start_offset,
				    rel->r_offset - start_offset,
				    name, addend,
				    rel->r_offset - start_offset);
			    break;
                        default:
			    error("unsupported sparc64 relocation (%d)", type);
                        }
                    }
                }
            }
#elif defined(HOST_ARM)
            {
                char name[256];
                int type;
                int addend;

                arm_emit_ldr_info(name, start_offset, outfile, p_start, p_end,
                                  relocs, nb_relocs);

                for(i = 0, rel = relocs;i < nb_relocs; i++, rel++) {
                if (rel->r_offset >= start_offset &&
		    rel->r_offset < start_offset + copy_size) {
                    sym_name = strtab + symtab[ELFW(R_SYM)(rel->r_info)].st_name;
                    /* the compiler leave some unnecessary references to the code */
                    if (sym_name[0] == '\0')
                        continue;
                    if (strstart(sym_name, "__op_param", &p)) {
                        snprintf(name, sizeof(name), "param%s", p);
                    } else {
                        snprintf(name, sizeof(name), "(long)(&%s)", sym_name);
                    }
                    type = ELF32_R_TYPE(rel->r_info);
                    addend = get32((uint32_t *)(text + rel->r_offset));
                    switch(type) {
                    case R_ARM_ABS32:
                        fprintf(outfile, "    *(uint32_t *)(gen_code_ptr + %d) = %s + %d;\n", 
                                rel->r_offset - start_offset, name, addend);
                        break;
                    case R_ARM_PC24:
                        fprintf(outfile, "    arm_reloc_pc24((uint32_t *)(gen_code_ptr + %d), 0x%x, %s);\n", 
                                rel->r_offset - start_offset, addend, name);
                        break;
                    default:
                        error("unsupported arm relocation (%d)", type);
                    }
                }
                }
            }
#elif defined(HOST_M68K)
            {
                char name[256];
                int type;
                int addend;
		Elf32_Sym *sym;
                for(i = 0, rel = relocs;i < nb_relocs; i++, rel++) {
                if (rel->r_offset >= start_offset &&
		    rel->r_offset < start_offset + copy_size) {
		    sym = &(symtab[ELFW(R_SYM)(rel->r_info)]);
                    sym_name = strtab + symtab[ELFW(R_SYM)(rel->r_info)].st_name;
                    if (strstart(sym_name, "__op_param", &p)) {
                        snprintf(name, sizeof(name), "param%s", p);
                    } else {
                        snprintf(name, sizeof(name), "(long)(&%s)", sym_name);
                    }
                    type = ELF32_R_TYPE(rel->r_info);
                    addend = get32((uint32_t *)(text + rel->r_offset)) + rel->r_addend;
                    switch(type) {
                    case R_68K_32:
		        fprintf(outfile, "    /* R_68K_32 RELOC, offset %x */\n", rel->r_offset) ;
                        fprintf(outfile, "    *(uint32_t *)(gen_code_ptr + %d) = %s + %#x;\n", 
                                rel->r_offset - start_offset, name, addend );
                        break;
                    case R_68K_PC32:
		        fprintf(outfile, "    /* R_68K_PC32 RELOC, offset %x */\n", rel->r_offset);
                        fprintf(outfile, "    *(uint32_t *)(gen_code_ptr + %d) = %s - (long)(gen_code_ptr + %#x) + %#x;\n", 
                                rel->r_offset - start_offset, name, rel->r_offset - start_offset, /*sym->st_value+*/ addend);
                        break;
                    default:
                        error("unsupported m68k relocation (%d)", type);
                    }
                }
                }
            }
#else
#error unsupported CPU
#endif
        fprintf(outfile, "    gen_code_ptr += %d;\n", copy_size);
        fprintf(outfile, "}\n");
        fprintf(outfile, "break;\n\n");
    } else {
        fprintf(outfile, "static inline void gen_%s(", name);
        if (nb_args == 0) {
            fprintf(outfile, "void");
        } else {
            for(i = 0; i < nb_args; i++) {
                if (i != 0)
                    fprintf(outfile, ", ");
                fprintf(outfile, "long param%d", i + 1);
            }
        }
        fprintf(outfile, ")\n");
        fprintf(outfile, "{\n");
        for(i = 0; i < nb_args; i++) {
            fprintf(outfile, "    *gen_opparam_ptr++ = param%d;\n", i + 1);
        }
        fprintf(outfile, "    *gen_opc_ptr++ = INDEX_%s;\n", name);
        fprintf(outfile, "}\n\n");
    }
}

int gen_file(FILE *outfile, int out_type)
{
    int i;
    EXE_SYM *sym;

    if (out_type == OUT_INDEX_OP) {
        fprintf(outfile, "DEF(end, 0, 0)\n");
        fprintf(outfile, "DEF(nop, 0, 0)\n");
        fprintf(outfile, "DEF(nop1, 1, 0)\n");
        fprintf(outfile, "DEF(nop2, 2, 0)\n");
        fprintf(outfile, "DEF(nop3, 3, 0)\n");
        for(i = 0, sym = symtab; i < nb_syms; i++, sym++) {
            const char *name, *p;
            name = get_sym_name(sym);
            if (strstart(name, OP_PREFIX, &p)) {
                gen_code(name, sym->st_value, sym->st_size, outfile, 2);
            }
        }
    } else if (out_type == OUT_GEN_OP) {
        /* generate gen_xxx functions */

        for(i = 0, sym = symtab; i < nb_syms; i++, sym++) {
            const char *name;
            name = get_sym_name(sym);
            if (strstart(name, OP_PREFIX, NULL)) {
                if (sym->st_shndx != text_shndx)
                    error("invalid section for opcode (0x%x)", sym->st_shndx);
                gen_code(name, sym->st_value, sym->st_size, outfile, 0);
            }
        }
        
    } else {
        /* generate big code generation switch */
fprintf(outfile,
"int dyngen_code(uint8_t *gen_code_buf,\n"
"                uint16_t *label_offsets, uint16_t *jmp_offsets,\n"
"                const uint16_t *opc_buf, const uint32_t *opparam_buf)\n"
"{\n"
"    uint8_t *gen_code_ptr;\n"
"    const uint16_t *opc_ptr;\n"
"    const uint32_t *opparam_ptr;\n");

#ifdef HOST_ARM
fprintf(outfile,
"    uint8_t *last_gen_code_ptr = gen_code_buf;\n"
"    LDREntry *arm_ldr_ptr = arm_ldr_table;\n"
"    uint32_t *arm_data_ptr = arm_data_table;\n");
#endif

fprintf(outfile,
"\n"
"    gen_code_ptr = gen_code_buf;\n"
"    opc_ptr = opc_buf;\n"
"    opparam_ptr = opparam_buf;\n");

	/* Generate prologue, if needed. */ 

fprintf(outfile,
"    for(;;) {\n"
"        switch(*opc_ptr++) {\n"
);

        for(i = 0, sym = symtab; i < nb_syms; i++, sym++) {
            const char *name;
            name = get_sym_name(sym);
            if (strstart(name, OP_PREFIX, NULL)) {
#if 0
                printf("%4d: %s pos=0x%08x len=%d\n", 
                       i, name, sym->st_value, sym->st_size);
#endif
                if (sym->st_shndx != text_shndx)
                    error("invalid section for opcode (0x%x)", sym->st_shndx);
                gen_code(name, sym->st_value, sym->st_size, outfile, 1);
            }
        }

fprintf(outfile,
"        case INDEX_op_nop:\n"
"            break;\n"
"        case INDEX_op_nop1:\n"
"            opparam_ptr++;\n"
"            break;\n"
"        case INDEX_op_nop2:\n"
"            opparam_ptr += 2;\n"
"            break;\n"
"        case INDEX_op_nop3:\n"
"            opparam_ptr += 3;\n"
"            break;\n"
"        default:\n"
"            goto the_end;\n"
"        }\n");

#ifdef HOST_ARM
/* generate constant table if needed */
fprintf(outfile,
"        if ((gen_code_ptr - last_gen_code_ptr) >= (MAX_FRAG_SIZE - MAX_OP_SIZE)) {\n"
"            gen_code_ptr = arm_flush_ldr(gen_code_ptr, arm_ldr_table, arm_ldr_ptr, arm_data_table, arm_data_ptr, 1);\n"
"            last_gen_code_ptr = gen_code_ptr;\n"
"            arm_ldr_ptr = arm_ldr_table;\n"
"            arm_data_ptr = arm_data_table;\n"
"        }\n");         
#endif


fprintf(outfile,
"    }\n"
" the_end:\n"
);

/* generate some code patching */ 
#ifdef HOST_ARM
fprintf(outfile, "gen_code_ptr = arm_flush_ldr(gen_code_ptr, arm_ldr_table, arm_ldr_ptr, arm_data_table, arm_data_ptr, 0);\n");
#endif
    /* flush instruction cache */
    fprintf(outfile, "flush_icache_range((unsigned long)gen_code_buf, (unsigned long)gen_code_ptr);\n");

    fprintf(outfile, "return gen_code_ptr -  gen_code_buf;\n");
    fprintf(outfile, "}\n\n");

    }

    return 0;
}

void usage(void)
{
    printf("dyngen (c) 2003 Fabrice Bellard\n"
           "usage: dyngen [-o outfile] [-c] objfile\n"
           "Generate a dynamic code generator from an object file\n"
           "-c     output enum of operations\n"
           "-g     output gen_op_xx() functions\n"
           );
    exit(1);
}

int main(int argc, char **argv)
{
    int c, out_type;
    const char *filename, *outfilename;
    FILE *outfile;

    outfilename = "out.c";
    out_type = OUT_CODE;
    for(;;) {
        c = getopt(argc, argv, "ho:cg");
        if (c == -1)
            break;
        switch(c) {
        case 'h':
            usage();
            break;
        case 'o':
            outfilename = optarg;
            break;
        case 'c':
            out_type = OUT_INDEX_OP;
            break;
        case 'g':
            out_type = OUT_GEN_OP;
            break;
        }
    }
    if (optind >= argc)
        usage();
    filename = argv[optind];
    outfile = fopen(outfilename, "w");
    if (!outfile)
        error("could not open '%s'", outfilename);

    load_object(filename);
    gen_file(outfile, out_type);
    fclose(outfile);
    return 0;
}
