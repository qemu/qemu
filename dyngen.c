#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <inttypes.h>
#include <elf.h>
#include <unistd.h>
#include <fcntl.h>

#include "thunk.h"

/* all dynamically generated functions begin with this code */
#define OP_PREFIX "op"

int elf_must_swap(Elf32_Ehdr *h)
{
  union {
      uint32_t i;
      uint8_t b[4];
  } swaptest;

  swaptest.i = 1;
  return (h->e_ident[EI_DATA] == ELFDATA2MSB) != 
      (swaptest.b[0] == 0);
}
  
void swab16s(uint16_t *p)
{
    *p = bswap16(*p);
}

void swab32s(uint32_t *p)
{
    *p = bswap32(*p);
}

void swab64s(uint32_t *p)
{
    *p = bswap64(*p);
}

void elf_swap_ehdr(Elf32_Ehdr *h)
{
    swab16s(&h->e_type);			/* Object file type */
    swab16s(&h->	e_machine);		/* Architecture */
    swab32s(&h->	e_version);		/* Object file version */
    swab32s(&h->	e_entry);		/* Entry point virtual address */
    swab32s(&h->	e_phoff);		/* Program header table file offset */
    swab32s(&h->	e_shoff);		/* Section header table file offset */
    swab32s(&h->	e_flags);		/* Processor-specific flags */
    swab16s(&h->	e_ehsize);		/* ELF header size in bytes */
    swab16s(&h->	e_phentsize);		/* Program header table entry size */
    swab16s(&h->	e_phnum);		/* Program header table entry count */
    swab16s(&h->	e_shentsize);		/* Section header table entry size */
    swab16s(&h->	e_shnum);		/* Section header table entry count */
    swab16s(&h->	e_shstrndx);		/* Section header string table index */
}

void elf_swap_shdr(Elf32_Shdr *h)
{
  swab32s(&h->	sh_name);		/* Section name (string tbl index) */
  swab32s(&h->	sh_type);		/* Section type */
  swab32s(&h->	sh_flags);		/* Section flags */
  swab32s(&h->	sh_addr);		/* Section virtual addr at execution */
  swab32s(&h->	sh_offset);		/* Section file offset */
  swab32s(&h->	sh_size);		/* Section size in bytes */
  swab32s(&h->	sh_link);		/* Link to another section */
  swab32s(&h->	sh_info);		/* Additional section information */
  swab32s(&h->	sh_addralign);		/* Section alignment */
  swab32s(&h->	sh_entsize);		/* Entry size if section holds table */
}

void elf_swap_phdr(Elf32_Phdr *h)
{
    swab32s(&h->p_type);			/* Segment type */
    swab32s(&h->p_offset);		/* Segment file offset */
    swab32s(&h->p_vaddr);		/* Segment virtual address */
    swab32s(&h->p_paddr);		/* Segment physical address */
    swab32s(&h->p_filesz);		/* Segment size in file */
    swab32s(&h->p_memsz);		/* Segment size in memory */
    swab32s(&h->p_flags);		/* Segment flags */
    swab32s(&h->p_align);		/* Segment alignment */
}

int do_swap;
int e_machine;

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

void __attribute__((noreturn)) error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "dyngen: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}


Elf32_Shdr *find_elf_section(Elf32_Shdr *shdr, int shnum, const char *shstr, 
                             const char *name)
{
    int i;
    const char *shname;
    Elf32_Shdr *sec;

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

#define MAX_ARGS 3

/* generate op code */
void gen_code(const char *name, unsigned long offset, unsigned long size, 
              FILE *outfile, uint8_t *text, void *relocs, int nb_relocs, int reloc_sh_type,
              Elf32_Sym *symtab, char *strtab)
{
    int copy_size = 0;
    uint8_t *p_start, *p_end;
    int nb_args, i;
    uint8_t args_present[MAX_ARGS];
    const char *sym_name, *p;

    /* compute exact size excluding return instruction */
    p_start = text + offset;
    p_end = p_start + size;
    switch(e_machine) {
    case EM_386:
        {
            uint8_t *p;
            p = p_end - 1;
            if (p == p_start)
                error("empty code for %s", name);
            if (p[0] != 0xc3)
                error("ret expected at the end of %s", name);
            copy_size = p - p_start;
        }
        break;
    case EM_PPC:
        {
            uint8_t *p;
            p = (void *)(p_end - 4);
            /* find ret */
            while (p > p_start && get32((uint32_t *)p) != 0x4e800020)
                p -= 4;
            /* skip double ret */
            if (p > p_start && get32((uint32_t *)(p - 4)) == 0x4e800020)
                p -= 4;
            if (p == p_start)
                error("empty code for %s", name);
            copy_size = p - p_start;
        }
        break;
    default:
        error("unsupported CPU (%d)", e_machine);
    }

    /* compute the number of arguments by looking at the relocations */
    for(i = 0;i < MAX_ARGS; i++)
        args_present[i] = 0;

    if (reloc_sh_type == SHT_REL) {
        Elf32_Rel *rel;
        int n;
        for(i = 0, rel = relocs;i < nb_relocs; i++, rel++) {
            if (rel->r_offset >= offset && rel->r_offset < offset + copy_size) {
                sym_name = strtab + symtab[ELF32_R_SYM(rel->r_info)].st_name;
                if (strstart(sym_name, "__op_param", &p)) {
                    n = strtoul(p, NULL, 10);
                    if (n >= MAX_ARGS)
                        error("too many arguments in %s", name);
                    args_present[n - 1] = 1;
                } else {
                    fprintf(outfile, "extern char %s;\n", sym_name);
                }
            }
        }
    } else {
        Elf32_Rela *rel;
        int n;
        for(i = 0, rel = relocs;i < nb_relocs; i++, rel++) {
            if (rel->r_offset >= offset && rel->r_offset < offset + copy_size) {
                sym_name = strtab + symtab[ELF32_R_SYM(rel->r_info)].st_name;
                if (strstart(sym_name, "__op_param", &p)) {
                    n = strtoul(p, NULL, 10);
                    if (n >= MAX_ARGS)
                        error("too many arguments in %s", name);
                    args_present[n - 1] = 1;
                } else {
                    fprintf(outfile, "extern char %s;\n", sym_name);
                }
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

    /* output C code */
    fprintf(outfile, "extern void %s();\n", name);
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
    fprintf(outfile, "    memcpy(gen_code_ptr, &%s, %d);\n", name, copy_size);
    
    /* patch relocations */
    switch(e_machine) {
    case EM_386:
        {
            Elf32_Rel *rel;
            char name[256];
            int type;
            long addend;
            for(i = 0, rel = relocs;i < nb_relocs; i++, rel++) {
                if (rel->r_offset >= offset && rel->r_offset < offset + copy_size) {
                    sym_name = strtab + symtab[ELF32_R_SYM(rel->r_info)].st_name;
                    if (strstart(sym_name, "__op_param", &p)) {
                        snprintf(name, sizeof(name), "param%s", p);
                    } else {
                        snprintf(name, sizeof(name), "(long)(&%s)", sym_name);
                    }
                    type = ELF32_R_TYPE(rel->r_info);
                    addend = get32((uint32_t *)(text + rel->r_offset));
                    switch(type) {
                    case R_386_32:
                        fprintf(outfile, "    *(uint32_t *)(gen_code_ptr + %ld) = %s + %ld;\n", 
                                rel->r_offset - offset, name, addend);
                        break;
                    case R_386_PC32:
                        fprintf(outfile, "    *(uint32_t *)(gen_code_ptr + %ld) = %s - (long)(gen_code_ptr + %ld) + %ld;\n", 
                                rel->r_offset - offset, name, rel->r_offset - offset, addend);
                        break;
                    default:
                        error("unsupported i386 relocation (%d)", type);
                    }
                }
            }
        }
        break;
    default:
        error("unsupported CPU for relocations (%d)", e_machine);
    }


    fprintf(outfile, "    gen_code_ptr += %d;\n", copy_size);
    fprintf(outfile, "}\n\n");
}

/* load an elf object file */
int load_elf(const char *filename, FILE *outfile)
{
    int fd;
    Elf32_Ehdr ehdr;
    Elf32_Shdr *sec, *shdr, *symtab_sec, *strtab_sec, *text_sec;
    int i, j, nb_syms;
    Elf32_Sym *symtab, *sym;
    const char *cpu_name;
    char *shstr, *strtab;
    uint8_t *text;
    void *relocs;
    int nb_relocs, reloc_sh_type;
    
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
     || ehdr.e_ident[EI_CLASS] != ELFCLASS32
     || ehdr.e_ident[EI_VERSION] != EV_CURRENT) {
        error("bad ELF header");
    }

    do_swap = elf_must_swap(&ehdr);
    if (do_swap)
        elf_swap_ehdr(&ehdr);
    if (ehdr.e_type != ET_REL)
        error("ELF object file expected");
    if (ehdr.e_version != EV_CURRENT)
        error("Invalid ELF version");
    e_machine = ehdr.e_machine;

    /* read section headers */
    shdr = load_data(fd, ehdr.e_shoff, ehdr.e_shnum * sizeof(Elf32_Shdr));
    if (do_swap) {
        for(i = 0; i < ehdr.e_shnum; i++) {
            elf_swap_shdr(&shdr[i]);
        }
    }

    sec = &shdr[ehdr.e_shstrndx];
    shstr = load_data(fd, sec->sh_offset, sec->sh_size);

    /* text section */

    text_sec = find_elf_section(shdr, ehdr.e_shnum, shstr, ".text");
    if (!text_sec)
        error("could not find .text section");
    text = load_data(fd, text_sec->sh_offset, text_sec->sh_size);

    /* find text relocations, if any */
    nb_relocs = 0;
    relocs = NULL;
    reloc_sh_type = 0;
    for(i = 0; i < ehdr.e_shnum; i++) {
        sec = &shdr[i];
        if ((sec->sh_type == SHT_REL || sec->sh_type == SHT_RELA) &&
            sec->sh_info == (text_sec - shdr)) {
            reloc_sh_type = sec->sh_type;
            relocs = load_data(fd, sec->sh_offset, sec->sh_size);
            nb_relocs = sec->sh_size / sec->sh_entsize;
            if (do_swap) {
                if (sec->sh_type == SHT_REL) {
                    Elf32_Rel *rel = relocs;
                    for(j = 0, rel = relocs; j < nb_relocs; j++, rel++) {
                        swab32s(&rel->r_offset);
                        swab32s(&rel->r_info);
                    }
                } else {
                    Elf32_Rela *rel = relocs;
                    for(j = 0, rel = relocs; j < nb_relocs; j++, rel++) {
                        swab32s(&rel->r_offset);
                        swab32s(&rel->r_info);
                        swab32s(&rel->r_addend);
                    }
                }
            }
            break;
        }
    }

    symtab_sec = find_elf_section(shdr, ehdr.e_shnum, shstr, ".symtab");
    if (!symtab_sec)
        error("could not find .symtab section");
    strtab_sec = &shdr[symtab_sec->sh_link];

    symtab = load_data(fd, symtab_sec->sh_offset, symtab_sec->sh_size);
    strtab = load_data(fd, strtab_sec->sh_offset, strtab_sec->sh_size);
    
    nb_syms = symtab_sec->sh_size / sizeof(Elf32_Sym);
    if (do_swap) {
        for(i = 0, sym = symtab; i < nb_syms; i++, sym++) {
            swab32s(&sym->st_name);
            swab32s(&sym->st_value);
            swab32s(&sym->st_size);
            swab16s(&sym->st_shndx);
        }
    }

    switch(e_machine) {
    case EM_386:
        cpu_name = "i386";
        break;
    case EM_PPC:
        cpu_name = "ppc";
        break;
    case EM_MIPS:
        cpu_name = "mips";
        break;
    case EM_ARM:
        cpu_name = "arm";
        break;
    case EM_SPARC:
        cpu_name = "sparc";
        break;
    default:
        error("unsupported CPU (e_machine=%d)", e_machine);
    }

    fprintf(outfile, "#include \"gen-%s.h\"\n\n", cpu_name);

    for(i = 0, sym = symtab; i < nb_syms; i++, sym++) {
        const char *name;
        name = strtab + sym->st_name;
        if (strstart(name, "op_", NULL) ||
            strstart(name, "op1_", NULL) ||
            strstart(name, "op2_", NULL) ||
            strstart(name, "op3_", NULL)) {
#if 0
            printf("%4d: %s pos=0x%08x len=%d\n", 
                   i, name, sym->st_value, sym->st_size);
#endif
            if (sym->st_shndx != (text_sec - shdr))
                error("invalid section for opcode (0x%x)", sym->st_shndx);
            gen_code(name, sym->st_value, sym->st_size, outfile, 
                     text, relocs, nb_relocs, reloc_sh_type, symtab, strtab);
        }
    }

    close(fd);
    return 0;
}

void usage(void)
{
    printf("dyngen (c) 2003 Fabrice Bellard\n"
           "usage: dyngen [-o outfile] objfile\n"
           "Generate a dynamic code generator from an object file\n");
    exit(1);
}

int main(int argc, char **argv)
{
    int c;
    const char *filename, *outfilename;
    FILE *outfile;

    outfilename = "out.c";
    for(;;) {
        c = getopt(argc, argv, "ho:");
        if (c == -1)
            break;
        switch(c) {
        case 'h':
            usage();
            break;
        case 'o':
            outfilename = optarg;
            break;
        }
    }
    if (optind >= argc)
        usage();
    filename = argv[optind];
    outfile = fopen(outfilename, "w");
    if (!outfile)
        error("could not open '%s'", outfilename);
    load_elf(filename, outfile);
    fclose(outfile);
    return 0;
}
