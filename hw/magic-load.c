#include "vl.h"
#include "disas.h"

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

static int find_strtab(struct elfhdr *ehdr, int fd, struct elf_shdr *shdr, struct elf_shdr *symtab)
{
    int retval;

    retval = lseek(fd, ehdr->e_shoff + sizeof(struct elf_shdr) * symtab->sh_link, SEEK_SET);
    if (retval < 0)
	return -1;

    retval = read(fd, shdr, sizeof(*shdr));
    if (retval < 0)
	return -1;
    bswap_shdr(shdr);
    if (shdr->sh_type == SHT_STRTAB)
	return qemu_malloc(shdr->sh_size);;
    return 0;
}

static int read_program(int fd, struct elf_phdr *phdr, void *dst)
{
    int retval;
    retval = lseek(fd, 0x4000, SEEK_SET);
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
    if (disas_symtab)
	qemu_free(disas_symtab); /* XXX Merge with old symbols? */
    if (disas_strtab)
	qemu_free(disas_strtab);
    disas_symtab = syms;
    disas_num_syms = nsyms;
    disas_strtab = str;
    return;
 error_freesyms:
    qemu_free(syms);
    return;
}

int load_elf(const char * filename, uint8_t *addr)
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
	|| ehdr.e_machine != EM_SPARC)
	goto error;

    if (find_phdr(&ehdr, fd, &phdr, PT_LOAD))
	goto error;
    retval = read_program(fd, &phdr, addr);
    if (retval < 0)
	goto error;

    load_symbols(&ehdr, fd);

    close(fd);
    return retval;
 error:
    close(fd);
    return -1;
}

int load_kernel(const char *filename, uint8_t *addr)
{
    int fd, size;

    fd = open(filename, O_RDONLY | O_BINARY);
    if (fd < 0)
        return -1;
    /* load 32 bit code */
    size = read(fd, addr, 16 * 1024 * 1024);
    if (size < 0)
        goto fail;
    close(fd);
    return size;
 fail:
    close(fd);
    return -1;
}

typedef struct MAGICState {
    uint32_t addr;
    uint32_t saved_addr;
    int magic_state;
    char saved_kfn[1024];
} MAGICState;

static uint32_t magic_mem_readl(void *opaque, target_phys_addr_t addr)
{
    int ret;
    MAGICState *s = opaque;

    if (s->magic_state == 0) {
        ret = load_elf(s->saved_kfn, (uint8_t *)s->saved_addr);
	if (ret < 0)
	    ret = load_kernel(s->saved_kfn, (uint8_t *)s->saved_addr);
        if (ret < 0) {
            fprintf(stderr, "qemu: could not load kernel '%s'\n", 
                    s->saved_kfn);
        }
	s->magic_state = 1; /* No more magic */
	tb_flush();
	return bswap32(ret);
    }
    return 0;
}

static void magic_mem_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
}


static CPUReadMemoryFunc *magic_mem_read[3] = {
    magic_mem_readl,
    magic_mem_readl,
    magic_mem_readl,
};

static CPUWriteMemoryFunc *magic_mem_write[3] = {
    magic_mem_writel,
    magic_mem_writel,
    magic_mem_writel,
};

void magic_init(const char *kfn, int kloadaddr, uint32_t addr)
{
    int magic_io_memory;
    MAGICState *s;

    s = qemu_mallocz(sizeof(MAGICState));
    if (!s)
        return;

    strcpy(s->saved_kfn, kfn);
    s->saved_addr = kloadaddr;
    s->magic_state = 0;
    s->addr = addr;
    magic_io_memory = cpu_register_io_memory(0, magic_mem_read, magic_mem_write, s);
    cpu_register_physical_memory(addr, 4, magic_io_memory);
}

