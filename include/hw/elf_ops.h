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

static int glue(symfind, SZ)(const void *s0, const void *s1)
{
    hwaddr addr = *(hwaddr *)s0;
    struct elf_sym *sym = (struct elf_sym *)s1;
    int result = 0;
    if (addr < sym->st_value) {
        result = -1;
    } else if (addr >= sym->st_value + sym->st_size) {
        result = 1;
    }
    return result;
}

static const char *glue(lookup_symbol, SZ)(struct syminfo *s,
                                           hwaddr orig_addr)
{
    struct elf_sym *syms = glue(s->disas_symtab.elf, SZ);
    struct elf_sym *sym;

    sym = bsearch(&orig_addr, syms, s->disas_num_syms, sizeof(*syms),
                  glue(symfind, SZ));
    if (sym != NULL) {
        return s->disas_strtab + sym->st_name;
    }

    return "";
}

static int glue(symcmp, SZ)(const void *s0, const void *s1)
{
    struct elf_sym *sym0 = (struct elf_sym *)s0;
    struct elf_sym *sym1 = (struct elf_sym *)s1;
    return (sym0->st_value < sym1->st_value)
        ? -1
        : ((sym0->st_value > sym1->st_value) ? 1 : 0);
}

static int glue(load_symbols, SZ)(struct elfhdr *ehdr, int fd, int must_swab,
                                  int clear_lsb)
{
    struct elf_shdr *symtab, *strtab, *shdr_table = NULL;
    struct elf_sym *syms = NULL;
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

    i = 0;
    while (i < nsyms) {
        if (must_swab)
            glue(bswap_sym, SZ)(&syms[i]);
        /* We are only interested in function symbols.
           Throw everything else away.  */
        if (syms[i].st_shndx == SHN_UNDEF ||
                syms[i].st_shndx >= SHN_LORESERVE ||
                ELF_ST_TYPE(syms[i].st_info) != STT_FUNC) {
            nsyms--;
            if (i < nsyms) {
                syms[i] = syms[nsyms];
            }
            continue;
        }
        if (clear_lsb) {
            /* The bottom address bit marks a Thumb or MIPS16 symbol.  */
            syms[i].st_value &= ~(glue(glue(Elf, SZ), _Addr))1;
        }
        i++;
    }
    syms = g_realloc(syms, nsyms * sizeof(*syms));

    qsort(syms, nsyms, sizeof(*syms), glue(symcmp, SZ));
    for (i = 0; i < nsyms - 1; i++) {
        if (syms[i].st_size == 0) {
            syms[i].st_size = syms[i + 1].st_value - syms[i].st_value;
        }
    }

    /* String table */
    if (symtab->sh_link >= ehdr->e_shnum)
        goto fail;
    strtab = &shdr_table[symtab->sh_link];

    str = load_at(fd, strtab->sh_offset, strtab->sh_size);
    if (!str)
        goto fail;

    /* Commit */
    s = g_malloc0(sizeof(*s));
    s->lookup_symbol = glue(lookup_symbol, SZ);
    glue(s->disas_symtab.elf, SZ) = syms;
    s->disas_num_syms = nsyms;
    s->disas_strtab = str;
    s->next = syminfos;
    syminfos = s;
    g_free(shdr_table);
    return 0;
 fail:
    g_free(syms);
    g_free(str);
    g_free(shdr_table);
    return -1;
}

static int glue(load_elf, SZ)(const char *name, int fd,
                              uint64_t (*translate_fn)(void *, uint64_t),
                              void *translate_opaque,
                              int must_swab, uint64_t *pentry,
                              uint64_t *lowaddr, uint64_t *highaddr,
                              int elf_machine, int clear_lsb)
{
    struct elfhdr ehdr;
    struct elf_phdr *phdr = NULL, *ph;
    int size, i, total_size;
    elf_word mem_size, file_size;
    uint64_t addr, low = (uint64_t)-1, high = 0;
    uint8_t *data = NULL;
    char label[128];
    int ret = ELF_LOAD_FAILED;

    if (read(fd, &ehdr, sizeof(ehdr)) != sizeof(ehdr))
        goto fail;
    if (must_swab) {
        glue(bswap_ehdr, SZ)(&ehdr);
    }

    switch (elf_machine) {
        case EM_PPC64:
            if (EM_PPC64 != ehdr.e_machine)
                if (EM_PPC != ehdr.e_machine) {
                    ret = ELF_LOAD_WRONG_ARCH;
                    goto fail;
                }
            break;
        case EM_X86_64:
            if (EM_X86_64 != ehdr.e_machine)
                if (EM_386 != ehdr.e_machine) {
                    ret = ELF_LOAD_WRONG_ARCH;
                    goto fail;
                }
            break;
        case EM_MICROBLAZE:
            if (EM_MICROBLAZE != ehdr.e_machine)
                if (EM_MICROBLAZE_OLD != ehdr.e_machine) {
                    ret = ELF_LOAD_WRONG_ARCH;
                    goto fail;
                }
            break;
        default:
            if (elf_machine != ehdr.e_machine) {
                ret = ELF_LOAD_WRONG_ARCH;
                goto fail;
            }
    }

    if (pentry)
   	*pentry = (uint64_t)(elf_sword)ehdr.e_entry;

    glue(load_symbols, SZ)(&ehdr, fd, must_swab, clear_lsb);

    size = ehdr.e_phnum * sizeof(phdr[0]);
    lseek(fd, ehdr.e_phoff, SEEK_SET);
    phdr = g_malloc0(size);
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
            mem_size = ph->p_memsz; /* Size of the ROM */
            file_size = ph->p_filesz; /* Size of the allocated data */
            data = g_malloc0(file_size);
            if (ph->p_filesz > 0) {
                if (lseek(fd, ph->p_offset, SEEK_SET) < 0) {
                    goto fail;
                }
                if (read(fd, data, file_size) != file_size) {
                    goto fail;
                }
            }
            /* address_offset is hack for kernel images that are
               linked at the wrong physical address.  */
            if (translate_fn) {
                addr = translate_fn(translate_opaque, ph->p_paddr);
            } else {
                addr = ph->p_paddr;
            }

            /* the entry pointer in the ELF header is a virtual
             * address, if the text segments paddr and vaddr differ
             * we need to adjust the entry */
            if (pentry && !translate_fn &&
                    ph->p_vaddr != ph->p_paddr &&
                    ehdr.e_entry >= ph->p_vaddr &&
                    ehdr.e_entry < ph->p_vaddr + ph->p_filesz &&
                    ph->p_flags & PF_X) {
                *pentry = ehdr.e_entry - ph->p_vaddr + ph->p_paddr;
            }

            snprintf(label, sizeof(label), "phdr #%d: %s", i, name);

            /* rom_add_elf_program() seize the ownership of 'data' */
            rom_add_elf_program(label, data, file_size, mem_size, addr);

            total_size += mem_size;
            if (addr < low)
                low = addr;
            if ((addr + mem_size) > high)
                high = addr + mem_size;

            data = NULL;
        }
    }
    g_free(phdr);
    if (lowaddr)
        *lowaddr = (uint64_t)(elf_sword)low;
    if (highaddr)
        *highaddr = (uint64_t)(elf_sword)high;
    return total_size;
 fail:
    g_free(data);
    g_free(phdr);
    return ret;
}
