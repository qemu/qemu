/* This is the Linux kernel elf-loading code, ported into user space */
#include "vl.h"
#include "disas.h"

/* XXX: this code is not used as it is under the GPL license. Please
   remove or recode it */
//#define USE_ELF_LOADER

#ifdef USE_ELF_LOADER
/* should probably go in elf.h */
#ifndef ELIBBAD
#define ELIBBAD 80
#endif


#define ELF_START_MMAP 0x80000000

#define elf_check_arch(x) ( (x) == EM_SPARC )

#define ELF_CLASS   ELFCLASS32
#define ELF_DATA    ELFDATA2MSB
#define ELF_ARCH    EM_SPARC

#include "elf.h"

/*
 * This structure is used to hold the arguments that are 
 * used when loading binaries.
 */
struct linux_binprm {
        char buf[128];
	int fd;
};

#define TARGET_ELF_EXEC_PAGESIZE TARGET_PAGE_SIZE
#define TARGET_ELF_PAGESTART(_v) ((_v) & ~(unsigned long)(TARGET_ELF_EXEC_PAGESIZE-1))
#define TARGET_ELF_PAGEOFFSET(_v) ((_v) & (TARGET_ELF_EXEC_PAGESIZE-1))

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
#endif

static int prepare_binprm(struct linux_binprm *bprm)
{
    int retval;

    memset(bprm->buf, 0, sizeof(bprm->buf));
    retval = lseek(bprm->fd, 0L, SEEK_SET);
    if(retval >= 0) {
        retval = read(bprm->fd, bprm->buf, 128);
    }
    if(retval < 0) {
	perror("prepare_binprm");
	exit(-1);
	/* return(-errno); */
    }
    else {
	return(retval);
    }
}

/* Best attempt to load symbols from this ELF object. */
static void load_symbols(struct elfhdr *hdr, int fd)
{
    unsigned int i;
    struct elf_shdr sechdr, symtab, strtab;
    char *strings;

    lseek(fd, hdr->e_shoff, SEEK_SET);
    for (i = 0; i < hdr->e_shnum; i++) {
	if (read(fd, &sechdr, sizeof(sechdr)) != sizeof(sechdr))
	    return;
#ifdef BSWAP_NEEDED
	bswap_shdr(&sechdr);
#endif
	if (sechdr.sh_type == SHT_SYMTAB) {
	    symtab = sechdr;
	    lseek(fd, hdr->e_shoff
		  + sizeof(sechdr) * sechdr.sh_link, SEEK_SET);
	    if (read(fd, &strtab, sizeof(strtab))
		!= sizeof(strtab))
		return;
#ifdef BSWAP_NEEDED
	    bswap_shdr(&strtab);
#endif
	    goto found;
	}
    }
    return; /* Shouldn't happen... */

 found:
    /* Now know where the strtab and symtab are.  Snarf them. */
    disas_symtab = qemu_malloc(symtab.sh_size);
    disas_strtab = strings = qemu_malloc(strtab.sh_size);
    if (!disas_symtab || !disas_strtab)
	return;
	
    lseek(fd, symtab.sh_offset, SEEK_SET);
    if (read(fd, disas_symtab, symtab.sh_size) != symtab.sh_size)
	return;

#ifdef BSWAP_NEEDED
    for (i = 0; i < symtab.sh_size / sizeof(struct elf_sym); i++)
	bswap_sym(disas_symtab + sizeof(struct elf_sym)*i);
#endif

    lseek(fd, strtab.sh_offset, SEEK_SET);
    if (read(fd, strings, strtab.sh_size) != strtab.sh_size)
	return;
    disas_num_syms = symtab.sh_size / sizeof(struct elf_sym);
}

static int load_elf_binary(struct linux_binprm * bprm, uint8_t *addr)
{
    struct elfhdr elf_ex;
    unsigned long startaddr = addr;
    int i;
    struct elf_phdr * elf_ppnt;
    struct elf_phdr *elf_phdata;
    int retval;

    elf_ex = *((struct elfhdr *) bprm->buf);          /* exec-header */
#ifdef BSWAP_NEEDED
    bswap_ehdr(&elf_ex);
#endif

    if (elf_ex.e_ident[0] != 0x7f ||
	strncmp(&elf_ex.e_ident[1], "ELF",3) != 0) {
	return  -ENOEXEC;
    }

    /* First of all, some simple consistency checks */
    if (! elf_check_arch(elf_ex.e_machine)) {
	return -ENOEXEC;
    }

    /* Now read in all of the header information */
    elf_phdata = (struct elf_phdr *)qemu_malloc(elf_ex.e_phentsize*elf_ex.e_phnum);
    if (elf_phdata == NULL) {
	return -ENOMEM;
    }

    retval = lseek(bprm->fd, elf_ex.e_phoff, SEEK_SET);
    if(retval > 0) {
	retval = read(bprm->fd, (char *) elf_phdata, 
				elf_ex.e_phentsize * elf_ex.e_phnum);
    }

    if (retval < 0) {
	perror("load_elf_binary");
	exit(-1);
	qemu_free (elf_phdata);
	return -errno;
    }

#ifdef BSWAP_NEEDED
    elf_ppnt = elf_phdata;
    for (i=0; i<elf_ex.e_phnum; i++, elf_ppnt++) {
        bswap_phdr(elf_ppnt);
    }
#endif
    elf_ppnt = elf_phdata;

    /* Now we do a little grungy work by mmaping the ELF image into
     * the correct location in memory.  At this point, we assume that
     * the image should be loaded at fixed address, not at a variable
     * address.
     */

    for(i = 0, elf_ppnt = elf_phdata; i < elf_ex.e_phnum; i++, elf_ppnt++) {
        unsigned long error, offset, len;
        
	if (elf_ppnt->p_type != PT_LOAD)
            continue;
#if 0        
        error = target_mmap(TARGET_ELF_PAGESTART(load_bias + elf_ppnt->p_vaddr),
                            elf_prot,
                            (MAP_FIXED | MAP_PRIVATE | MAP_DENYWRITE),
                            bprm->fd,
                            (elf_ppnt->p_offset - 
                             TARGET_ELF_PAGEOFFSET(elf_ppnt->p_vaddr)));
#endif
	//offset = elf_ppnt->p_offset - TARGET_ELF_PAGEOFFSET(elf_ppnt->p_vaddr);
	offset = 0x4000;
	lseek(bprm->fd, offset, SEEK_SET);
	len = elf_ppnt->p_filesz + TARGET_ELF_PAGEOFFSET(elf_ppnt->p_vaddr);
	error = read(bprm->fd, addr, len); 

        if (error == -1) {
            perror("mmap");
            exit(-1);
        }
	addr += len;
    }

    qemu_free(elf_phdata);

    load_symbols(&elf_ex, bprm->fd);

    return addr-startaddr;
}

int elf_exec(const char * filename, uint8_t *addr)
{
        struct linux_binprm bprm;
        int retval;

        retval = open(filename, O_RDONLY);
        if (retval < 0)
            return retval;
        bprm.fd = retval;

        retval = prepare_binprm(&bprm);

        if(retval>=0) {
	    retval = load_elf_binary(&bprm, addr);
	}
	return retval;
}
#endif

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

static char saved_kfn[1024];
static uint32_t saved_addr;
static int magic_state;

static uint32_t magic_mem_readl(void *opaque, target_phys_addr_t addr)
{
    int ret;

    if (magic_state == 0) {
#ifdef USE_ELF_LOADER
        ret = elf_exec(saved_kfn, saved_addr);
#else
        ret = load_kernel(saved_kfn, (uint8_t *)saved_addr);
#endif
        if (ret < 0) {
            fprintf(stderr, "qemu: could not load kernel '%s'\n", 
                    saved_kfn);
        }
	magic_state = 1; /* No more magic */
	tb_flush();
    }
    return ret;
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

void magic_init(const char *kfn, int kloadaddr)
{
    int magic_io_memory;

    strcpy(saved_kfn, kfn);
    saved_addr = kloadaddr;
    magic_state = 0;
    magic_io_memory = cpu_register_io_memory(0, magic_mem_read, magic_mem_write, 0);
    cpu_register_physical_memory(0x20000000, 4,
                                 magic_io_memory);
}

