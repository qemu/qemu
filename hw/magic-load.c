#include "vl.h"
#include "disas.h"
#include "exec-all.h"

struct exec
{
  uint32_t a_info;   /* Use macros N_MAGIC, etc for access */
  uint32_t a_text;   /* length of text, in bytes */
  uint32_t a_data;   /* length of data, in bytes */
  uint32_t a_bss;    /* length of uninitialized data area, in bytes */
  uint32_t a_syms;   /* length of symbol table data in file, in bytes */
  uint32_t a_entry;  /* start address */
  uint32_t a_trsize; /* length of relocation info for text, in bytes */
  uint32_t a_drsize; /* length of relocation info for data, in bytes */
};

#ifdef BSWAP_NEEDED
static void bswap_ahdr(struct exec *e)
{
    bswap32s(&e->a_info);
    bswap32s(&e->a_text);
    bswap32s(&e->a_data);
    bswap32s(&e->a_bss);
    bswap32s(&e->a_syms);
    bswap32s(&e->a_entry);
    bswap32s(&e->a_trsize);
    bswap32s(&e->a_drsize);
}
#else
#define bswap_ahdr(x) do { } while (0)
#endif

#define N_MAGIC(exec) ((exec).a_info & 0xffff)
#define OMAGIC 0407
#define NMAGIC 0410
#define ZMAGIC 0413
#define QMAGIC 0314
#define _N_HDROFF(x) (1024 - sizeof (struct exec))
#define N_TXTOFF(x)							\
    (N_MAGIC(x) == ZMAGIC ? _N_HDROFF((x)) + sizeof (struct exec) :	\
     (N_MAGIC(x) == QMAGIC ? 0 : sizeof (struct exec)))
#define N_TXTADDR(x) (N_MAGIC(x) == QMAGIC ? TARGET_PAGE_SIZE : 0)
#define N_DATOFF(x) (N_TXTOFF(x) + (x).a_text)
#define _N_SEGMENT_ROUND(x) (((x) + TARGET_PAGE_SIZE - 1) & ~(TARGET_PAGE_SIZE - 1))

#define _N_TXTENDADDR(x) (N_TXTADDR(x)+(x).a_text)

#define N_DATADDR(x) \
    (N_MAGIC(x)==OMAGIC? (_N_TXTENDADDR(x)) \
     : (_N_SEGMENT_ROUND (_N_TXTENDADDR(x))))


#define ELF_CLASS   ELFCLASS32
#define ELF_DATA    ELFDATA2MSB
#define ELF_ARCH    EM_SPARC

#include "elf.h"

#ifdef BSWAP_NEEDED
static void bswap_ehdr(Elf32_Ehdr *ehdr)
{
    bswap16s(&ehdr->e_type);			/* Object file type */
    bswap16s(&ehdr->e_machine);		/* Architecture */
    bswap32s(&ehdr->e_version);		/* Object file version */
    bswap32s(&ehdr->e_entry);		/* Entry point virtual address */
    bswap32s(&ehdr->e_phoff);		/* Program header table file offset */
    bswap32s(&ehdr->e_shoff);		/* Section header table file offset */
    bswap32s(&ehdr->e_flags);		/* Processor-specific flags */
    bswap16s(&ehdr->e_ehsize);		/* ELF header size in bytes */
    bswap16s(&ehdr->e_phentsize);		/* Program header table entry size */
    bswap16s(&ehdr->e_phnum);		/* Program header table entry count */
    bswap16s(&ehdr->e_shentsize);		/* Section header table entry size */
    bswap16s(&ehdr->e_shnum);		/* Section header table entry count */
    bswap16s(&ehdr->e_shstrndx);		/* Section header string table index */
}

static void bswap_phdr(Elf32_Phdr *phdr)
{
    bswap32s(&phdr->p_type);			/* Segment type */
    bswap32s(&phdr->p_offset);		/* Segment file offset */
    bswap32s(&phdr->p_vaddr);		/* Segment virtual address */
    bswap32s(&phdr->p_paddr);		/* Segment physical address */
    bswap32s(&phdr->p_filesz);		/* Segment size in file */
    bswap32s(&phdr->p_memsz);		/* Segment size in memory */
    bswap32s(&phdr->p_flags);		/* Segment flags */
    bswap32s(&phdr->p_align);		/* Segment alignment */
}

static void bswap_shdr(Elf32_Shdr *shdr)
{
    bswap32s(&shdr->sh_name);
    bswap32s(&shdr->sh_type);
    bswap32s(&shdr->sh_flags);
    bswap32s(&shdr->sh_addr);
    bswap32s(&shdr->sh_offset);
    bswap32s(&shdr->sh_size);
    bswap32s(&shdr->sh_link);
    bswap32s(&shdr->sh_info);
    bswap32s(&shdr->sh_addralign);
    bswap32s(&shdr->sh_entsize);
}

static void bswap_sym(Elf32_Sym *sym)
{
    bswap32s(&sym->st_name);
    bswap32s(&sym->st_value);
    bswap32s(&sym->st_size);
    bswap16s(&sym->st_shndx);
}
#else
#define bswap_ehdr(e) do { } while (0)
#define bswap_phdr(e) do { } while (0)
#define bswap_shdr(e) do { } while (0)
#define bswap_sym(e) do { } while (0)
#endif

static int find_phdr(struct elfhdr *ehdr, int fd, struct elf_phdr *phdr, uint32_t type)
{
    int i, retval;

    retval = lseek(fd, ehdr->e_phoff, SEEK_SET);
    if (retval < 0)
	return -1;

    for (i = 0; i < ehdr->e_phnum; i++) {
	retval = read(fd, phdr, sizeof(*phdr));
	if (retval < 0)
	    return -1;
	bswap_phdr(phdr);
	if (phdr->p_type == type)
	    return 0;
    }
    return -1;
}

static void *find_shdr(struct elfhdr *ehdr, int fd, struct elf_shdr *shdr, uint32_t type)
{
    int i, retval;

    retval = lseek(fd, ehdr->e_shoff, SEEK_SET);
    if (retval < 0)
	return NULL;

    for (i = 0; i < ehdr->e_shnum; i++) {
	retval = read(fd, shdr, sizeof(*shdr));
	if (retval < 0)
	    return NULL;
	bswap_shdr(shdr);
	if (shdr->sh_type == type)
	    return qemu_malloc(shdr->sh_size);
    }
    return NULL;
}

static void *find_strtab(struct elfhdr *ehdr, int fd, struct elf_shdr *shdr, struct elf_shdr *symtab)
{
    int retval;

    retval = lseek(fd, ehdr->e_shoff + sizeof(struct elf_shdr) * symtab->sh_link, SEEK_SET);
    if (retval < 0)
	return NULL;

    retval = read(fd, shdr, sizeof(*shdr));
    if (retval < 0)
	return NULL;
    bswap_shdr(shdr);
    if (shdr->sh_type == SHT_STRTAB)
	return qemu_malloc(shdr->sh_size);;
    return NULL;
}

static int read_program(int fd, struct elf_phdr *phdr, void *dst, uint32_t entry)
{
    int retval;
    retval = lseek(fd, phdr->p_offset + entry - phdr->p_vaddr, SEEK_SET);
    if (retval < 0)
	return -1;
    return read(fd, dst, phdr->p_filesz);
}

static int read_section(int fd, struct elf_shdr *s, void *dst)
{
    int retval;

    retval = lseek(fd, s->sh_offset, SEEK_SET);
    if (retval < 0)
	return -1;
    retval = read(fd, dst, s->sh_size);
    if (retval < 0)
	return -1;
    return 0;
}

static void *process_section(struct elfhdr *ehdr, int fd, struct elf_shdr *shdr, uint32_t type)
{
    void *dst;

    dst = find_shdr(ehdr, fd, shdr, type);
    if (!dst)
	goto error;

    if (read_section(fd, shdr, dst))
	goto error;
    return dst;
 error:
    qemu_free(dst);
    return NULL;
}

static void *process_strtab(struct elfhdr *ehdr, int fd, struct elf_shdr *shdr, struct elf_shdr *symtab)
{
    void *dst;

    dst = find_strtab(ehdr, fd, shdr, symtab);
    if (!dst)
	goto error;

    if (read_section(fd, shdr, dst))
	goto error;
    return dst;
 error:
    qemu_free(dst);
    return NULL;
}

static void load_symbols(struct elfhdr *ehdr, int fd)
{
    struct elf_shdr symtab, strtab;
    struct elf_sym *syms;
    struct syminfo *s;
    int nsyms, i;
    char *str;

    /* Symbol table */
    syms = process_section(ehdr, fd, &symtab, SHT_SYMTAB);
    if (!syms)
	return;

    nsyms = symtab.sh_size / sizeof(struct elf_sym);
    for (i = 0; i < nsyms; i++)
	bswap_sym(&syms[i]);

    /* String table */
    str = process_strtab(ehdr, fd, &strtab, &symtab);
    if (!str)
	goto error_freesyms;

    /* Commit */
    s = qemu_mallocz(sizeof(*s));
    s->disas_symtab = syms;
    s->disas_num_syms = nsyms;
    s->disas_strtab = str;
    s->next = syminfos;
    syminfos = s;
    return;
 error_freesyms:
    qemu_free(syms);
    return;
}

int load_elf(const char *filename, uint8_t *addr)
{
    struct elfhdr ehdr;
    struct elf_phdr phdr;
    int retval, fd;

    fd = open(filename, O_RDONLY | O_BINARY);
    if (fd < 0)
	goto error;

    retval = read(fd, &ehdr, sizeof(ehdr));
    if (retval < 0)
	goto error;

    bswap_ehdr(&ehdr);

    if (ehdr.e_ident[0] != 0x7f || ehdr.e_ident[1] != 'E'
	|| ehdr.e_ident[2] != 'L' || ehdr.e_ident[3] != 'F'
	|| (ehdr.e_machine != EM_SPARC
	    && ehdr.e_machine != EM_SPARC32PLUS))
	goto error;

    if (find_phdr(&ehdr, fd, &phdr, PT_LOAD))
	goto error;
    retval = read_program(fd, &phdr, addr, ehdr.e_entry);
    if (retval < 0)
	goto error;

    load_symbols(&ehdr, fd);

    close(fd);
    return retval;
 error:
    close(fd);
    return -1;
}

int load_aout(const char *filename, uint8_t *addr)
{
    int fd, size, ret;
    struct exec e;
    uint32_t magic;

    fd = open(filename, O_RDONLY | O_BINARY);
    if (fd < 0)
        return -1;

    size = read(fd, &e, sizeof(e));
    if (size < 0)
        goto fail;

    bswap_ahdr(&e);

    magic = N_MAGIC(e);
    switch (magic) {
    case ZMAGIC:
    case QMAGIC:
    case OMAGIC:
	lseek(fd, N_TXTOFF(e), SEEK_SET);
	size = read(fd, addr, e.a_text + e.a_data);
	if (size < 0)
	    goto fail;
	break;
    case NMAGIC:
	lseek(fd, N_TXTOFF(e), SEEK_SET);
	size = read(fd, addr, e.a_text);
	if (size < 0)
	    goto fail;
	ret = read(fd, addr + N_DATADDR(e), e.a_data);
	if (ret < 0)
	    goto fail;
	size += ret;
	break;
    default:
	goto fail;
    }
    close(fd);
    return size;
 fail:
    close(fd);
    return -1;
}

