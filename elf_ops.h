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

static struct elf_shdr *glue(find_section, SZ)(struct elf_shdr *shdr_table,
                                               int n, int type)
{
    int i;
    for(i=0;i<n;i++) {
        if (shdr_table[i].sh_type == type)
            return shdr_table + i;
    }
    return NULL;
}

static int glue(load_symbols, SZ)(struct elfhdr *ehdr, int fd, int must_swab)
{
    struct elf_shdr *symtab, *strtab, *shdr_table = NULL;
    struct elf_sym *syms = NULL;
#if (SZ == 64)
    struct elf32_sym *syms32 = NULL;
#endif
    struct syminfo *s;
    int nsyms, i;
    char *str = NULL;

    shdr_table = load_at(fd, ehdr->e_shoff,
                         sizeof(struct elf_shdr) * ehdr->e_shnum);
    if (!shdr_table)
        return -1;

    if (must_swab) {
        for (i = 0; i < ehdr->e_shnum; i++) {
            glue(bswap_shdr, SZ)(shdr_table + i);
        }
    }

    symtab = glue(find_section, SZ)(shdr_table, ehdr->e_shnum, SHT_SYMTAB);
    if (!symtab)
        goto fail;
    syms = load_at(fd, symtab->sh_offset, symtab->sh_size);
    if (!syms)
        goto fail;

    nsyms = symtab->sh_size / sizeof(struct elf_sym);
#if (SZ == 64)
    syms32 = qemu_mallocz(nsyms * sizeof(struct elf32_sym));
#endif
    for (i = 0; i < nsyms; i++) {
        if (must_swab)
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
    if (symtab->sh_link >= ehdr->e_shnum)
        goto fail;
    strtab = &shdr_table[symtab->sh_link];

    str = load_at(fd, strtab->sh_offset, strtab->sh_size);
    if (!str)
	goto fail;

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
    qemu_free(shdr_table);
    return 0;
 fail:
#if (SZ == 64)
    qemu_free(syms32);
#endif
    qemu_free(syms);
    qemu_free(str);
    qemu_free(shdr_table);
    return -1;
}

static int glue(load_elf, SZ)(int fd, int64_t virt_to_phys_addend,
                              int must_swab, uint64_t *pentry,
                              uint64_t *lowaddr, uint64_t *highaddr)
{
    struct elfhdr ehdr;
    struct elf_phdr *phdr = NULL, *ph;
    int size, i, total_size;
    elf_word mem_size;
    uint64_t addr, low = 0, high = 0;
    uint8_t *data = NULL;

    if (read(fd, &ehdr, sizeof(ehdr)) != sizeof(ehdr))
        goto fail;
    if (must_swab) {
        glue(bswap_ehdr, SZ)(&ehdr);
    }

    if (ELF_MACHINE != ehdr.e_machine)
        goto fail;

    if (pentry)
   	*pentry = (uint64_t)(elf_sword)ehdr.e_entry;

    glue(load_symbols, SZ)(&ehdr, fd, must_swab);

    size = ehdr.e_phnum * sizeof(phdr[0]);
    lseek(fd, ehdr.e_phoff, SEEK_SET);
    phdr = qemu_mallocz(size);
    if (!phdr)
        goto fail;
    if (read(fd, phdr, size) != size)
        goto fail;
    if (must_swab) {
        for(i = 0; i < ehdr.e_phnum; i++) {
            ph = &phdr[i];
            glue(bswap_phdr, SZ)(ph);
        }
    }

    total_size = 0;
    for(i = 0; i < ehdr.e_phnum; i++) {
        ph = &phdr[i];
        if (ph->p_type == PT_LOAD) {
            mem_size = ph->p_memsz;
            /* XXX: avoid allocating */
            data = qemu_mallocz(mem_size);
            if (ph->p_filesz > 0) {
                if (lseek(fd, ph->p_offset, SEEK_SET) < 0)
                    goto fail;
                if (read(fd, data, ph->p_filesz) != ph->p_filesz)
                    goto fail;
            }
            addr = ph->p_vaddr + virt_to_phys_addend;

            cpu_physical_memory_write_rom(addr, data, mem_size);

            total_size += mem_size;
            if (!low || addr < low)
                low = addr;
            if (!high || (addr + mem_size) > high)
                high = addr + mem_size;

            qemu_free(data);
            data = NULL;
        }
    }
    qemu_free(phdr);
    if (lowaddr)
        *lowaddr = (uint64_t)(elf_sword)low;
    if (highaddr)
        *highaddr = (uint64_t)(elf_sword)high;
    return total_size;
 fail:
    qemu_free(data);
    qemu_free(phdr);
    return -1;
}
