#ifdef BSWAP_NEEDED
static void glue(bswap_ehdr, SZ)(struct elfhdr *ehdr)
{
    bswap16s(&ehdr->e_type);			/* Object file type */
    bswap16s(&ehdr->e_machine);		/* Architecture */
    bswap32s(&ehdr->e_version);		/* Object file version */
    bswapSZs(&ehdr->e_entry);		/* Entry point virtual address */
    bswapSZs(&ehdr->e_phoff);		/* Program header table file offset */
    bswapSZs(&ehdr->e_shoff);		/* Section header table file offset */
    bswap32s(&ehdr->e_flags);		/* Processor-specific flags */
    bswap16s(&ehdr->e_ehsize);		/* ELF header size in bytes */
    bswap16s(&ehdr->e_phentsize);		/* Program header table entry size */
    bswap16s(&ehdr->e_phnum);		/* Program header table entry count */
    bswap16s(&ehdr->e_shentsize);		/* Section header table entry size */
    bswap16s(&ehdr->e_shnum);		/* Section header table entry count */
    bswap16s(&ehdr->e_shstrndx);		/* Section header string table index */
}

static void glue(bswap_phdr, SZ)(struct elf_phdr *phdr)
{
    bswap32s(&phdr->p_type);			/* Segment type */
    bswapSZs(&phdr->p_offset);		/* Segment file offset */
    bswapSZs(&phdr->p_vaddr);		/* Segment virtual address */
    bswapSZs(&phdr->p_paddr);		/* Segment physical address */
    bswapSZs(&phdr->p_filesz);		/* Segment size in file */
    bswapSZs(&phdr->p_memsz);		/* Segment size in memory */
    bswap32s(&phdr->p_flags);		/* Segment flags */
    bswapSZs(&phdr->p_align);		/* Segment alignment */
}

static void glue(bswap_shdr, SZ)(struct elf_shdr *shdr)
{
    bswap32s(&shdr->sh_name);
    bswap32s(&shdr->sh_type);
    bswapSZs(&shdr->sh_flags);
    bswapSZs(&shdr->sh_addr);
    bswapSZs(&shdr->sh_offset);
    bswapSZs(&shdr->sh_size);
    bswap32s(&shdr->sh_link);
    bswap32s(&shdr->sh_info);
    bswapSZs(&shdr->sh_addralign);
    bswapSZs(&shdr->sh_entsize);
}

static void glue(bswap_sym, SZ)(struct elf_sym *sym)
{
    bswap32s(&sym->st_name);
    bswapSZs(&sym->st_value);
    bswapSZs(&sym->st_size);
    bswap16s(&sym->st_shndx);
}
#endif

static int glue(find_phdr, SZ)(struct elfhdr *ehdr, int fd, struct elf_phdr *phdr, elf_word type)
{
    int i, retval;

    retval = lseek(fd, ehdr->e_phoff, SEEK_SET);
    if (retval < 0)
	return -1;

    for (i = 0; i < ehdr->e_phnum; i++) {
	retval = read(fd, phdr, sizeof(*phdr));
	if (retval < 0)
	    return -1;
	glue(bswap_phdr, SZ)(phdr);
	if (phdr->p_type == type)
	    return 0;
    }
    return -1;
}

static void * glue(find_shdr, SZ)(struct elfhdr *ehdr, int fd, struct elf_shdr *shdr, elf_word type)
{
    int i, retval;

    retval = lseek(fd, ehdr->e_shoff, SEEK_SET);
    if (retval < 0)
	return NULL;

    for (i = 0; i < ehdr->e_shnum; i++) {
	retval = read(fd, shdr, sizeof(*shdr));
	if (retval < 0)
	    return NULL;
	glue(bswap_shdr, SZ)(shdr);
	if (shdr->sh_type == type)
	    return qemu_malloc(shdr->sh_size);
    }
    return NULL;
}

static void * glue(find_strtab, SZ)(struct elfhdr *ehdr, int fd, struct elf_shdr *shdr, struct elf_shdr *symtab)
{
    int retval;

    retval = lseek(fd, ehdr->e_shoff + sizeof(struct elf_shdr) * symtab->sh_link, SEEK_SET);
    if (retval < 0)
	return NULL;

    retval = read(fd, shdr, sizeof(*shdr));
    if (retval < 0)
	return NULL;
    glue(bswap_shdr, SZ)(shdr);
    if (shdr->sh_type == SHT_STRTAB)
	return qemu_malloc(shdr->sh_size);;
    return NULL;
}

static int glue(read_program, SZ)(int fd, struct elf_phdr *phdr, void *dst, elf_word entry)
{
    int retval;
    retval = lseek(fd, phdr->p_offset + entry - phdr->p_vaddr, SEEK_SET);
    if (retval < 0)
	return -1;
    return read(fd, dst, phdr->p_filesz);
}

static int glue(read_section, SZ)(int fd, struct elf_shdr *s, void *dst)
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

static void * glue(process_section, SZ)(struct elfhdr *ehdr, int fd, struct elf_shdr *shdr, elf_word type)
{
    void *dst;

    dst = glue(find_shdr, SZ)(ehdr, fd, shdr, type);
    if (!dst)
	goto error;

    if (glue(read_section, SZ)(fd, shdr, dst))
	goto error;
    return dst;
 error:
    qemu_free(dst);
    return NULL;
}

static void * glue(process_strtab, SZ)(struct elfhdr *ehdr, int fd, struct elf_shdr *shdr, struct elf_shdr *symtab)
{
    void *dst;

    dst = glue(find_strtab, SZ)(ehdr, fd, shdr, symtab);
    if (!dst)
	goto error;

    if (glue(read_section, SZ)(fd, shdr, dst))
	goto error;
    return dst;
 error:
    qemu_free(dst);
    return NULL;
}

static void glue(load_symbols, SZ)(struct elfhdr *ehdr, int fd)
{
    struct elf_shdr symtab, strtab;
    struct elf_sym *syms;
#if (SZ == 64)
    struct elf32_sym *syms32;
#endif
    struct syminfo *s;
    int nsyms, i;
    char *str;

    /* Symbol table */
    syms = glue(process_section, SZ)(ehdr, fd, &symtab, SHT_SYMTAB);
    if (!syms)
	return;

    nsyms = symtab.sh_size / sizeof(struct elf_sym);
#if (SZ == 64)
    syms32 = qemu_mallocz(nsyms * sizeof(struct elf32_sym));
#endif
    for (i = 0; i < nsyms; i++) {
	glue(bswap_sym, SZ)(&syms[i]);
#if (SZ == 64)
	syms32[i].st_name = syms[i].st_name;
	syms32[i].st_info = syms[i].st_info;
	syms32[i].st_other = syms[i].st_other;
	syms32[i].st_shndx = syms[i].st_shndx;
	syms32[i].st_value = syms[i].st_value & 0xffffffff;
	syms32[i].st_size = syms[i].st_size & 0xffffffff;
#endif
    }
    /* String table */
    str = glue(process_strtab, SZ)(ehdr, fd, &strtab, &symtab);
    if (!str)
	goto error_freesyms;

    /* Commit */
    s = qemu_mallocz(sizeof(*s));
#if (SZ == 64)
    s->disas_symtab = syms32;
    qemu_free(syms);
#else
    s->disas_symtab = syms;
#endif
    s->disas_num_syms = nsyms;
    s->disas_strtab = str;
    s->next = syminfos;
    syminfos = s;
    return;
 error_freesyms:
#if (SZ == 64)
    qemu_free(syms32);
#endif
    qemu_free(syms);
    return;
}
