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

static void glue(bswap_rela, SZ)(struct elf_rela *rela)
{
    bswapSZs(&rela->r_offset);
    bswapSZs(&rela->r_info);
    bswapSZs((elf_word *)&rela->r_addend);
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
                                  int clear_lsb, symbol_fn_t sym_cb)
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

    /* String table */
    if (symtab->sh_link >= ehdr->e_shnum) {
        goto fail;
    }
    strtab = &shdr_table[symtab->sh_link];

    str = load_at(fd, strtab->sh_offset, strtab->sh_size);
    if (!str) {
        goto fail;
    }

    i = 0;
    while (i < nsyms) {
        if (must_swab) {
            glue(bswap_sym, SZ)(&syms[i]);
        }
        if (sym_cb) {
            sym_cb(str + syms[i].st_name, syms[i].st_info,
                   syms[i].st_value, syms[i].st_size);
        }
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

static int glue(elf_reloc, SZ)(struct elfhdr *ehdr, int fd, int must_swab,
                               uint64_t (*translate_fn)(void *, uint64_t),
                               void *translate_opaque, uint8_t *data,
                               struct elf_phdr *ph, int elf_machine)
{
    struct elf_shdr *reltab, *shdr_table = NULL;
    struct elf_rela *rels = NULL;
    int nrels, i, ret = -1;
    elf_word wordval;
    void *addr;

    shdr_table = load_at(fd, ehdr->e_shoff,
                         sizeof(struct elf_shdr) * ehdr->e_shnum);
    if (!shdr_table) {
        return -1;
    }
    if (must_swab) {
        for (i = 0; i < ehdr->e_shnum; i++) {
            glue(bswap_shdr, SZ)(&shdr_table[i]);
        }
    }

    reltab = glue(find_section, SZ)(shdr_table, ehdr->e_shnum, SHT_RELA);
    if (!reltab) {
        goto fail;
    }
    rels = load_at(fd, reltab->sh_offset, reltab->sh_size);
    if (!rels) {
        goto fail;
    }
    nrels = reltab->sh_size / sizeof(struct elf_rela);

    for (i = 0; i < nrels; i++) {
        if (must_swab) {
            glue(bswap_rela, SZ)(&rels[i]);
        }
        if (rels[i].r_offset < ph->p_vaddr ||
            rels[i].r_offset >= ph->p_vaddr + ph->p_filesz) {
            continue;
        }
        addr = &data[rels[i].r_offset - ph->p_vaddr];
        switch (elf_machine) {
        case EM_S390:
            switch (rels[i].r_info) {
            case R_390_RELATIVE:
                wordval = *(elf_word *)addr;
                if (must_swab) {
                    bswapSZs(&wordval);
                }
                wordval = translate_fn(translate_opaque, wordval);
                if (must_swab) {
                    bswapSZs(&wordval);
                }
                *(elf_word *)addr = wordval;
                break;
            default:
                fprintf(stderr, "Unsupported relocation type %i!\n",
                        (int)rels[i].r_info);
            }
        }
    }

    ret = 0;
fail:
    g_free(rels);
    g_free(shdr_table);
    return ret;
}

/*
 * Given 'nhdr', a pointer to a range of ELF Notes, search through them
 * for a note matching type 'elf_note_type' and return a pointer to
 * the matching ELF note.
 */
static struct elf_note *glue(get_elf_note_type, SZ)(struct elf_note *nhdr,
                                                    elf_word note_size,
                                                    elf_word phdr_align,
                                                    elf_word elf_note_type)
{
    elf_word nhdr_size = sizeof(struct elf_note);
    elf_word elf_note_entry_offset = 0;
    elf_word note_type;
    elf_word nhdr_namesz;
    elf_word nhdr_descsz;

    if (nhdr == NULL) {
        return NULL;
    }

    note_type = nhdr->n_type;
    while (note_type != elf_note_type) {
        nhdr_namesz = nhdr->n_namesz;
        nhdr_descsz = nhdr->n_descsz;

        elf_note_entry_offset = nhdr_size +
            QEMU_ALIGN_UP(nhdr_namesz, phdr_align) +
            QEMU_ALIGN_UP(nhdr_descsz, phdr_align);

        /*
         * If the offset calculated in this iteration exceeds the
         * supplied size, we are done and no matching note was found.
         */
        if (elf_note_entry_offset > note_size) {
            return NULL;
        }

        /* skip to the next ELF Note entry */
        nhdr = (void *)nhdr + elf_note_entry_offset;
        note_type = nhdr->n_type;
    }

    return nhdr;
}

static int glue(load_elf, SZ)(const char *name, int fd,
                              uint64_t (*elf_note_fn)(void *, void *, bool),
                              uint64_t (*translate_fn)(void *, uint64_t),
                              void *translate_opaque,
                              int must_swab, uint64_t *pentry,
                              uint64_t *lowaddr, uint64_t *highaddr,
                              int elf_machine, int clear_lsb, int data_swab,
                              AddressSpace *as, bool load_rom,
                              symbol_fn_t sym_cb)
{
    struct elfhdr ehdr;
    struct elf_phdr *phdr = NULL, *ph;
    int size, i, total_size;
    elf_word mem_size, file_size, data_offset;
    uint64_t addr, low = (uint64_t)-1, high = 0;
    GMappedFile *mapped_file = NULL;
    uint8_t *data = NULL;
    char label[128];
    int ret = ELF_LOAD_FAILED;

    if (read(fd, &ehdr, sizeof(ehdr)) != sizeof(ehdr))
        goto fail;
    if (must_swab) {
        glue(bswap_ehdr, SZ)(&ehdr);
    }

    if (elf_machine <= EM_NONE) {
        /* The caller didn't specify an ARCH, we can figure it out */
        elf_machine = ehdr.e_machine;
    }

    switch (elf_machine) {
        case EM_PPC64:
            if (ehdr.e_machine != EM_PPC64) {
                if (ehdr.e_machine != EM_PPC) {
                    ret = ELF_LOAD_WRONG_ARCH;
                    goto fail;
                }
            }
            break;
        case EM_X86_64:
            if (ehdr.e_machine != EM_X86_64) {
                if (ehdr.e_machine != EM_386) {
                    ret = ELF_LOAD_WRONG_ARCH;
                    goto fail;
                }
            }
            break;
        case EM_MICROBLAZE:
            if (ehdr.e_machine != EM_MICROBLAZE) {
                if (ehdr.e_machine != EM_MICROBLAZE_OLD) {
                    ret = ELF_LOAD_WRONG_ARCH;
                    goto fail;
                }
            }
            break;
        case EM_MOXIE:
            if (ehdr.e_machine != EM_MOXIE) {
                if (ehdr.e_machine != EM_MOXIE_OLD) {
                    ret = ELF_LOAD_WRONG_ARCH;
                    goto fail;
                }
            }
            break;
        case EM_MIPS:
        case EM_NANOMIPS:
            if ((ehdr.e_machine != EM_MIPS) &&
                (ehdr.e_machine != EM_NANOMIPS)) {
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

    glue(load_symbols, SZ)(&ehdr, fd, must_swab, clear_lsb, sym_cb);

    size = ehdr.e_phnum * sizeof(phdr[0]);
    if (lseek(fd, ehdr.e_phoff, SEEK_SET) != ehdr.e_phoff) {
        goto fail;
    }
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

    /*
     * Since we want to be able to modify the mapped buffer, we set the
     * 'writeble' parameter to 'true'. Modifications to the buffer are not
     * written back to the file.
     */
    mapped_file = g_mapped_file_new_from_fd(fd, true, NULL);
    if (!mapped_file) {
        goto fail;
    }

    total_size = 0;
    for(i = 0; i < ehdr.e_phnum; i++) {
        ph = &phdr[i];
        if (ph->p_type == PT_LOAD) {
            mem_size = ph->p_memsz; /* Size of the ROM */
            file_size = ph->p_filesz; /* Size of the allocated data */
            data_offset = ph->p_offset; /* Offset where the data is located */

            if (file_size > 0) {
                if (g_mapped_file_get_length(mapped_file) <
                    file_size + data_offset) {
                    goto fail;
                }

                data = (uint8_t *)g_mapped_file_get_contents(mapped_file);
                data += data_offset;
            }

            /* The ELF spec is somewhat vague about the purpose of the
             * physical address field. One common use in the embedded world
             * is that physical address field specifies the load address
             * and the virtual address field specifies the execution address.
             * Segments are packed into ROM or flash, and the relocation
             * and zero-initialization of data is done at runtime. This
             * means that the memsz header represents the runtime size of the
             * segment, but the filesz represents the loadtime size. If
             * we try to honour the memsz value for an ELF file like this
             * we will end up with overlapping segments (which the
             * loader.c code will later reject).
             * We support ELF files using this scheme by by checking whether
             * paddr + memsz for this segment would overlap with any other
             * segment. If so, then we assume it's using this scheme and
             * truncate the loaded segment to the filesz size.
             * If the segment considered as being memsz size doesn't overlap
             * then we use memsz for the segment length, to handle ELF files
             * which assume that the loader will do the zero-initialization.
             */
            if (mem_size > file_size) {
                /* If this segment's zero-init portion overlaps another
                 * segment's data or zero-init portion, then truncate this one.
                 * Invalid ELF files where the segments overlap even when
                 * only file_size bytes are loaded will be rejected by
                 * the ROM overlap check in loader.c, so we don't try to
                 * explicitly detect those here.
                 */
                int j;
                elf_word zero_start = ph->p_paddr + file_size;
                elf_word zero_end = ph->p_paddr + mem_size;

                for (j = 0; j < ehdr.e_phnum; j++) {
                    struct elf_phdr *jph = &phdr[j];

                    if (i != j && jph->p_type == PT_LOAD) {
                        elf_word other_start = jph->p_paddr;
                        elf_word other_end = jph->p_paddr + jph->p_memsz;

                        if (!(other_start >= zero_end ||
                              zero_start >= other_end)) {
                            mem_size = file_size;
                            break;
                        }
                    }
                }
            }

            if (mem_size > INT_MAX - total_size) {
                ret = ELF_LOAD_TOO_BIG;
                goto fail;
            }

            /* address_offset is hack for kernel images that are
               linked at the wrong physical address.  */
            if (translate_fn) {
                addr = translate_fn(translate_opaque, ph->p_paddr);
                glue(elf_reloc, SZ)(&ehdr, fd, must_swab,  translate_fn,
                                    translate_opaque, data, ph, elf_machine);
            } else {
                addr = ph->p_paddr;
            }

            if (data_swab) {
                int j;
                for (j = 0; j < file_size; j += (1 << data_swab)) {
                    uint8_t *dp = data + j;
                    switch (data_swab) {
                    case (1):
                        *(uint16_t *)dp = bswap16(*(uint16_t *)dp);
                        break;
                    case (2):
                        *(uint32_t *)dp = bswap32(*(uint32_t *)dp);
                        break;
                    case (3):
                        *(uint64_t *)dp = bswap64(*(uint64_t *)dp);
                        break;
                    default:
                        g_assert_not_reached();
                    }
                }
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

            /* Some ELF files really do have segments of zero size;
             * just ignore them rather than trying to create empty
             * ROM blobs, because the zero-length blob can falsely
             * trigger the overlapping-ROM-blobs check.
             */
            if (mem_size != 0) {
                if (load_rom) {
                    snprintf(label, sizeof(label), "phdr #%d: %s", i, name);

                    /*
                     * rom_add_elf_program() takes its own reference to
                     * 'mapped_file'.
                     */
                    rom_add_elf_program(label, mapped_file, data, file_size,
                                        mem_size, addr, as);
                } else {
                    address_space_write(as ? as : &address_space_memory,
                                        addr, MEMTXATTRS_UNSPECIFIED,
                                        data, file_size);
                }
            }

            total_size += mem_size;
            if (addr < low)
                low = addr;
            if ((addr + mem_size) > high)
                high = addr + mem_size;

            data = NULL;

        } else if (ph->p_type == PT_NOTE && elf_note_fn) {
            struct elf_note *nhdr = NULL;

            file_size = ph->p_filesz; /* Size of the range of ELF notes */
            data_offset = ph->p_offset; /* Offset where the notes are located */

            if (file_size > 0) {
                if (g_mapped_file_get_length(mapped_file) <
                    file_size + data_offset) {
                    goto fail;
                }

                data = (uint8_t *)g_mapped_file_get_contents(mapped_file);
                data += data_offset;
            }

            /*
             * Search the ELF notes to find one with a type matching the
             * value passed in via 'translate_opaque'
             */
            nhdr = (struct elf_note *)data;
            assert(translate_opaque != NULL);
            nhdr = glue(get_elf_note_type, SZ)(nhdr, file_size, ph->p_align,
                                               *(uint64_t *)translate_opaque);
            if (nhdr != NULL) {
                bool is64 =
                    sizeof(struct elf_note) == sizeof(struct elf64_note);
                elf_note_fn((void *)nhdr, (void *)&ph->p_align, is64);
            }
            data = NULL;
        }
    }

    if (lowaddr)
        *lowaddr = (uint64_t)(elf_sword)low;
    if (highaddr)
        *highaddr = (uint64_t)(elf_sword)high;
    ret = total_size;
 fail:
    g_mapped_file_unref(mapped_file);
    g_free(phdr);
    return ret;
}
