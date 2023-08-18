/*
 *  ELF loading code
 *
 *  Copyright (c) 2013 Stacey D. Son
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
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"

#include "qemu.h"
#include "disas/disas.h"
#include "qemu/path.h"

static abi_ulong target_auxents;   /* Where the AUX entries are in target */
static size_t target_auxents_sz;   /* Size of AUX entries including AT_NULL */

#include "target_arch_reg.h"
#include "target_os_elf.h"
#include "target_os_stack.h"
#include "target_os_thread.h"
#include "target_os_user.h"

abi_ulong target_stksiz;
abi_ulong target_stkbas;

static int elf_core_dump(int signr, CPUArchState *env);
static int load_elf_sections(const struct elfhdr *hdr, struct elf_phdr *phdr,
    int fd, abi_ulong rbase, abi_ulong *baddrp);

static inline void memcpy_fromfs(void *to, const void *from, unsigned long n)
{
    memcpy(to, from, n);
}

#ifdef BSWAP_NEEDED
static void bswap_ehdr(struct elfhdr *ehdr)
{
    bswap16s(&ehdr->e_type);            /* Object file type */
    bswap16s(&ehdr->e_machine);         /* Architecture */
    bswap32s(&ehdr->e_version);         /* Object file version */
    bswaptls(&ehdr->e_entry);           /* Entry point virtual address */
    bswaptls(&ehdr->e_phoff);           /* Program header table file offset */
    bswaptls(&ehdr->e_shoff);           /* Section header table file offset */
    bswap32s(&ehdr->e_flags);           /* Processor-specific flags */
    bswap16s(&ehdr->e_ehsize);          /* ELF header size in bytes */
    bswap16s(&ehdr->e_phentsize);       /* Program header table entry size */
    bswap16s(&ehdr->e_phnum);           /* Program header table entry count */
    bswap16s(&ehdr->e_shentsize);       /* Section header table entry size */
    bswap16s(&ehdr->e_shnum);           /* Section header table entry count */
    bswap16s(&ehdr->e_shstrndx);        /* Section header string table index */
}

static void bswap_phdr(struct elf_phdr *phdr, int phnum)
{
    int i;

    for (i = 0; i < phnum; i++, phdr++) {
        bswap32s(&phdr->p_type);        /* Segment type */
        bswap32s(&phdr->p_flags);       /* Segment flags */
        bswaptls(&phdr->p_offset);      /* Segment file offset */
        bswaptls(&phdr->p_vaddr);       /* Segment virtual address */
        bswaptls(&phdr->p_paddr);       /* Segment physical address */
        bswaptls(&phdr->p_filesz);      /* Segment size in file */
        bswaptls(&phdr->p_memsz);       /* Segment size in memory */
        bswaptls(&phdr->p_align);       /* Segment alignment */
    }
}

static void bswap_shdr(struct elf_shdr *shdr, int shnum)
{
    int i;

    for (i = 0; i < shnum; i++, shdr++) {
        bswap32s(&shdr->sh_name);
        bswap32s(&shdr->sh_type);
        bswaptls(&shdr->sh_flags);
        bswaptls(&shdr->sh_addr);
        bswaptls(&shdr->sh_offset);
        bswaptls(&shdr->sh_size);
        bswap32s(&shdr->sh_link);
        bswap32s(&shdr->sh_info);
        bswaptls(&shdr->sh_addralign);
        bswaptls(&shdr->sh_entsize);
    }
}

static void bswap_sym(struct elf_sym *sym)
{
    bswap32s(&sym->st_name);
    bswaptls(&sym->st_value);
    bswaptls(&sym->st_size);
    bswap16s(&sym->st_shndx);
}

static void bswap_note(struct elf_note *en)
{
    bswap32s(&en->n_namesz);
    bswap32s(&en->n_descsz);
    bswap32s(&en->n_type);
}

#else /* ! BSWAP_NEEDED */

static void bswap_ehdr(struct elfhdr *ehdr) { }
static void bswap_phdr(struct elf_phdr *phdr, int phnum) { }
static void bswap_shdr(struct elf_shdr *shdr, int shnum) { }
static void bswap_sym(struct elf_sym *sym) { }
static void bswap_note(struct elf_note *en) { }

#endif /* ! BSWAP_NEEDED */

#include "elfcore.c"

/*
 * 'copy_elf_strings()' copies argument/envelope strings from user
 * memory to free pages in kernel mem. These are in a format ready
 * to be put directly into the top of new user memory.
 *
 */
static abi_ulong copy_elf_strings(int argc, char **argv, void **page,
                                  abi_ulong p)
{
    char *tmp, *tmp1, *pag = NULL;
    int len, offset = 0;

    if (!p) {
        return 0;       /* bullet-proofing */
    }
    while (argc-- > 0) {
        tmp = argv[argc];
        if (!tmp) {
            fprintf(stderr, "VFS: argc is wrong");
            exit(-1);
        }
        tmp1 = tmp;
        while (*tmp++) {
            continue;
        }
        len = tmp - tmp1;
        if (p < len) {  /* this shouldn't happen - 128kB */
            return 0;
        }
        while (len) {
            --p; --tmp; --len;
            if (--offset < 0) {
                offset = p % TARGET_PAGE_SIZE;
                pag = page[p / TARGET_PAGE_SIZE];
                if (!pag) {
                    pag = g_try_malloc0(TARGET_PAGE_SIZE);
                    page[p / TARGET_PAGE_SIZE] = pag;
                    if (!pag) {
                        return 0;
                    }
                }
            }
            if (len == 0 || offset == 0) {
                *(pag + offset) = *tmp;
            } else {
              int bytes_to_copy = (len > offset) ? offset : len;
              tmp -= bytes_to_copy;
              p -= bytes_to_copy;
              offset -= bytes_to_copy;
              len -= bytes_to_copy;
              memcpy_fromfs(pag + offset, tmp, bytes_to_copy + 1);
            }
        }
    }
    return p;
}

static void setup_arg_pages(struct bsd_binprm *bprm, struct image_info *info,
                            abi_ulong *stackp, abi_ulong *stringp)
{
    abi_ulong stack_base, size;
    abi_long addr;

    /*
     * Create enough stack to hold everything.  If we don't use it for args,
     * we'll use it for something else...
     */
    size = target_dflssiz;
    stack_base = TARGET_USRSTACK - size;
    addr = target_mmap(stack_base , size + qemu_host_page_size,
            PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (addr == -1) {
        perror("stk mmap");
        exit(-1);
    }
    /* we reserve one extra page at the top of the stack as guard */
    target_mprotect(addr + size, qemu_host_page_size, PROT_NONE);

    target_stksiz = size;
    target_stkbas = addr;

    if (setup_initial_stack(bprm, stackp, stringp) != 0) {
        perror("stk setup");
        exit(-1);
    }
}

static void set_brk(abi_ulong start, abi_ulong end)
{
    /* page-align the start and end addresses... */
    start = HOST_PAGE_ALIGN(start);
    end = HOST_PAGE_ALIGN(end);
    if (end <= start) {
        return;
    }
    if (target_mmap(start, end - start, PROT_READ | PROT_WRITE | PROT_EXEC,
        MAP_FIXED | MAP_PRIVATE | MAP_ANON, -1, 0) == -1) {
        perror("cannot mmap brk");
        exit(-1);
    }
}


/*
 * We need to explicitly zero any fractional pages after the data
 * section (i.e. bss).  This would contain the junk from the file that
 * should not be in memory.
 */
static void padzero(abi_ulong elf_bss, abi_ulong last_bss)
{
    abi_ulong nbyte;

    if (elf_bss >= last_bss) {
        return;
    }

    /*
     * XXX: this is really a hack : if the real host page size is
     * smaller than the target page size, some pages after the end
     * of the file may not be mapped. A better fix would be to
     * patch target_mmap(), but it is more complicated as the file
     * size must be known.
     */
    if (qemu_real_host_page_size() < qemu_host_page_size) {
        abi_ulong end_addr, end_addr1;
        end_addr1 = REAL_HOST_PAGE_ALIGN(elf_bss);
        end_addr = HOST_PAGE_ALIGN(elf_bss);
        if (end_addr1 < end_addr) {
            mmap((void *)g2h_untagged(end_addr1), end_addr - end_addr1,
                 PROT_READ | PROT_WRITE | PROT_EXEC,
                 MAP_FIXED | MAP_PRIVATE | MAP_ANON, -1, 0);
        }
    }

    nbyte = elf_bss & (qemu_host_page_size - 1);
    if (nbyte) {
        nbyte = qemu_host_page_size - nbyte;
        do {
            /* FIXME - what to do if put_user() fails? */
            put_user_u8(0, elf_bss);
            elf_bss++;
        } while (--nbyte);
    }
}

static abi_ulong load_elf_interp(struct elfhdr *interp_elf_ex,
                                 int interpreter_fd,
                                 abi_ulong *interp_load_addr)
{
    struct elf_phdr *elf_phdata  =  NULL;
    abi_ulong rbase;
    int retval;
    abi_ulong baddr, error;

    error = 0;

    bswap_ehdr(interp_elf_ex);
    /* First of all, some simple consistency checks */
    if ((interp_elf_ex->e_type != ET_EXEC && interp_elf_ex->e_type != ET_DYN) ||
          !elf_check_arch(interp_elf_ex->e_machine)) {
        return ~((abi_ulong)0UL);
    }


    /* Now read in all of the header information */
    if (sizeof(struct elf_phdr) * interp_elf_ex->e_phnum > TARGET_PAGE_SIZE) {
        return ~(abi_ulong)0UL;
    }

    elf_phdata =  (struct elf_phdr *) malloc(sizeof(struct elf_phdr) *
            interp_elf_ex->e_phnum);

    if (!elf_phdata) {
        return ~((abi_ulong)0UL);
    }

    /*
     * If the size of this structure has changed, then punt, since
     * we will be doing the wrong thing.
     */
    if (interp_elf_ex->e_phentsize != sizeof(struct elf_phdr)) {
        free(elf_phdata);
        return ~((abi_ulong)0UL);
    }

    retval = lseek(interpreter_fd, interp_elf_ex->e_phoff, SEEK_SET);
    if (retval >= 0) {
        retval = read(interpreter_fd, (char *) elf_phdata,
                sizeof(struct elf_phdr) * interp_elf_ex->e_phnum);
    }
    if (retval < 0) {
        perror("load_elf_interp");
        exit(-1);
        free(elf_phdata);
        return retval;
    }
    bswap_phdr(elf_phdata, interp_elf_ex->e_phnum);

    rbase = 0;
    if (interp_elf_ex->e_type == ET_DYN) {
        /*
         * In order to avoid hardcoding the interpreter load
         * address in qemu, we allocate a big enough memory zone.
         */
        rbase = target_mmap(0, INTERP_MAP_SIZE, PROT_NONE,
                MAP_PRIVATE | MAP_ANON, -1, 0);
        if (rbase == -1) {
            perror("mmap");
            exit(-1);
        }
    }

    error = load_elf_sections(interp_elf_ex, elf_phdata, interpreter_fd, rbase,
        &baddr);
    if (error != 0) {
        perror("load_elf_sections");
        exit(-1);
    }

    /* Now use mmap to map the library into memory. */
    close(interpreter_fd);
    free(elf_phdata);

    *interp_load_addr = baddr;
    return ((abi_ulong) interp_elf_ex->e_entry) + rbase;
}

static int symfind(const void *s0, const void *s1)
{
    struct elf_sym *sym = (struct elf_sym *)s1;
    __typeof(sym->st_value) addr = *(uint64_t *)s0;
    int result = 0;

    if (addr < sym->st_value) {
        result = -1;
    } else if (addr >= sym->st_value + sym->st_size) {
        result = 1;
    }
    return result;
}

static const char *lookup_symbolxx(struct syminfo *s, uint64_t orig_addr)
{
#if ELF_CLASS == ELFCLASS32
    struct elf_sym *syms = s->disas_symtab.elf32;
#else
    struct elf_sym *syms = s->disas_symtab.elf64;
#endif

    /* binary search */
    struct elf_sym *sym;

    sym = bsearch(&orig_addr, syms, s->disas_num_syms, sizeof(*syms), symfind);
    if (sym != NULL) {
        return s->disas_strtab + sym->st_name;
    }

    return "";
}

/* FIXME: This should use elf_ops.h  */
static int symcmp(const void *s0, const void *s1)
{
    struct elf_sym *sym0 = (struct elf_sym *)s0;
    struct elf_sym *sym1 = (struct elf_sym *)s1;
    return (sym0->st_value < sym1->st_value) ? -1 :
        ((sym0->st_value > sym1->st_value) ? 1 : 0);
}

/* Best attempt to load symbols from this ELF object. */
static void load_symbols(struct elfhdr *hdr, int fd)
{
    unsigned int i, nsyms;
    struct elf_shdr sechdr, symtab, strtab;
    char *strings;
    struct syminfo *s;
    struct elf_sym *syms, *new_syms;

    lseek(fd, hdr->e_shoff, SEEK_SET);
    for (i = 0; i < hdr->e_shnum; i++) {
        if (read(fd, &sechdr, sizeof(sechdr)) != sizeof(sechdr)) {
            return;
        }
        bswap_shdr(&sechdr, 1);
        if (sechdr.sh_type == SHT_SYMTAB) {
            symtab = sechdr;
            lseek(fd, hdr->e_shoff + sizeof(sechdr) * sechdr.sh_link,
                  SEEK_SET);
            if (read(fd, &strtab, sizeof(strtab)) != sizeof(strtab)) {
                return;
            }
            bswap_shdr(&strtab, 1);
            goto found;
        }
    }
    return; /* Shouldn't happen... */

found:
    /* Now know where the strtab and symtab are.  Snarf them. */
    s = malloc(sizeof(*s));
    syms = malloc(symtab.sh_size);
    if (!syms) {
        free(s);
        return;
    }
    s->disas_strtab = strings = malloc(strtab.sh_size);
    if (!s->disas_strtab) {
        free(s);
        free(syms);
        return;
    }

    lseek(fd, symtab.sh_offset, SEEK_SET);
    if (read(fd, syms, symtab.sh_size) != symtab.sh_size) {
        free(s);
        free(syms);
        free(strings);
        return;
    }

    nsyms = symtab.sh_size / sizeof(struct elf_sym);

    i = 0;
    while (i < nsyms) {
        bswap_sym(syms + i);
        /* Throw away entries which we do not need. */
        if (syms[i].st_shndx == SHN_UNDEF ||
                syms[i].st_shndx >= SHN_LORESERVE ||
                ELF_ST_TYPE(syms[i].st_info) != STT_FUNC) {
            nsyms--;
            if (i < nsyms) {
                syms[i] = syms[nsyms];
            }
            continue;
        }
#if defined(TARGET_ARM) || defined(TARGET_MIPS)
        /* The bottom address bit marks a Thumb or MIPS16 symbol.  */
        syms[i].st_value &= ~(target_ulong)1;
#endif
        i++;
    }

     /*
      * Attempt to free the storage associated with the local symbols
      * that we threw away.  Whether or not this has any effect on the
      * memory allocation depends on the malloc implementation and how
      * many symbols we managed to discard.
      */
    new_syms = realloc(syms, nsyms * sizeof(*syms));
    if (new_syms == NULL) {
        free(s);
        free(syms);
        free(strings);
        return;
    }
    syms = new_syms;

    qsort(syms, nsyms, sizeof(*syms), symcmp);

    lseek(fd, strtab.sh_offset, SEEK_SET);
    if (read(fd, strings, strtab.sh_size) != strtab.sh_size) {
        free(s);
        free(syms);
        free(strings);
        return;
    }
    s->disas_num_syms = nsyms;
#if ELF_CLASS == ELFCLASS32
    s->disas_symtab.elf32 = syms;
    s->lookup_symbol = (lookup_symbol_t)lookup_symbolxx;
#else
    s->disas_symtab.elf64 = syms;
    s->lookup_symbol = (lookup_symbol_t)lookup_symbolxx;
#endif
    s->next = syminfos;
    syminfos = s;
}

/* Check the elf header and see if this a target elf binary. */
int is_target_elf_binary(int fd)
{
    uint8_t buf[128];
    struct elfhdr elf_ex;

    if (lseek(fd, 0L, SEEK_SET) < 0) {
        return 0;
    }
    if (read(fd, buf, sizeof(buf)) < 0) {
        return 0;
    }

    elf_ex = *((struct elfhdr *)buf);
    bswap_ehdr(&elf_ex);

    if ((elf_ex.e_type != ET_EXEC && elf_ex.e_type != ET_DYN) ||
        (!elf_check_arch(elf_ex.e_machine))) {
        return 0;
    } else {
        return 1;
    }
}

static int
load_elf_sections(const struct elfhdr *hdr, struct elf_phdr *phdr, int fd,
    abi_ulong rbase, abi_ulong *baddrp)
{
    struct elf_phdr *elf_ppnt;
    abi_ulong baddr;
    int i;
    bool first;

    /*
     * Now we do a little grungy work by mmaping the ELF image into
     * the correct location in memory.  At this point, we assume that
     * the image should be loaded at fixed address, not at a variable
     * address.
     */
    first = true;
    for (i = 0, elf_ppnt = phdr; i < hdr->e_phnum; i++, elf_ppnt++) {
        int elf_prot = 0;
        abi_ulong error;

        /* XXX Skip memsz == 0. */
        if (elf_ppnt->p_type != PT_LOAD) {
            continue;
        }

        if (elf_ppnt->p_flags & PF_R) {
            elf_prot |= PROT_READ;
        }
        if (elf_ppnt->p_flags & PF_W) {
            elf_prot |= PROT_WRITE;
        }
        if (elf_ppnt->p_flags & PF_X) {
            elf_prot |= PROT_EXEC;
        }

        error = target_mmap(TARGET_ELF_PAGESTART(rbase + elf_ppnt->p_vaddr),
                            (elf_ppnt->p_filesz +
                             TARGET_ELF_PAGEOFFSET(elf_ppnt->p_vaddr)),
                            elf_prot,
                            (MAP_FIXED | MAP_PRIVATE | MAP_DENYWRITE),
                            fd,
                            (elf_ppnt->p_offset -
                             TARGET_ELF_PAGEOFFSET(elf_ppnt->p_vaddr)));
        if (error == -1) {
            perror("mmap");
            exit(-1);
        } else if (elf_ppnt->p_memsz != elf_ppnt->p_filesz) {
            abi_ulong start_bss, end_bss;

            start_bss = rbase + elf_ppnt->p_vaddr + elf_ppnt->p_filesz;
            end_bss = rbase + elf_ppnt->p_vaddr + elf_ppnt->p_memsz;

            /*
             * Calling set_brk effectively mmaps the pages that we need for the
             * bss and break sections.
             */
            set_brk(start_bss, end_bss);
            padzero(start_bss, end_bss);
        }

        if (first) {
            baddr = TARGET_ELF_PAGESTART(rbase + elf_ppnt->p_vaddr);
            first = false;
        }
    }

    if (baddrp != NULL) {
        *baddrp = baddr;
    }
    return 0;
}

int load_elf_binary(struct bsd_binprm *bprm, struct target_pt_regs *regs,
                    struct image_info *info)
{
    struct elfhdr elf_ex;
    struct elfhdr interp_elf_ex;
    int interpreter_fd = -1; /* avoid warning */
    abi_ulong load_addr;
    int i;
    struct elf_phdr *elf_ppnt;
    struct elf_phdr *elf_phdata;
    abi_ulong elf_brk;
    int error, retval;
    char *elf_interpreter;
    abi_ulong baddr, elf_entry, et_dyn_addr, interp_load_addr = 0;
    abi_ulong reloc_func_desc = 0;

    load_addr = 0;
    elf_ex = *((struct elfhdr *) bprm->buf);          /* exec-header */
    bswap_ehdr(&elf_ex);

    /* First of all, some simple consistency checks */
    if ((elf_ex.e_type != ET_EXEC && elf_ex.e_type != ET_DYN) ||
        (!elf_check_arch(elf_ex.e_machine))) {
            return -ENOEXEC;
    }

    bprm->p = copy_elf_strings(1, &bprm->filename, bprm->page, bprm->p);
    bprm->p = copy_elf_strings(bprm->envc, bprm->envp, bprm->page, bprm->p);
    bprm->p = copy_elf_strings(bprm->argc, bprm->argv, bprm->page, bprm->p);
    if (!bprm->p) {
        retval = -E2BIG;
    }

    /* Now read in all of the header information */
    elf_phdata = (struct elf_phdr *)malloc(elf_ex.e_phentsize * elf_ex.e_phnum);
    if (elf_phdata == NULL) {
        return -ENOMEM;
    }

    retval = lseek(bprm->fd, elf_ex.e_phoff, SEEK_SET);
    if (retval > 0) {
        retval = read(bprm->fd, (char *)elf_phdata,
                                elf_ex.e_phentsize * elf_ex.e_phnum);
    }

    if (retval < 0) {
        perror("load_elf_binary");
        exit(-1);
        free(elf_phdata);
        return -errno;
    }

    bswap_phdr(elf_phdata, elf_ex.e_phnum);
    elf_ppnt = elf_phdata;

    elf_brk = 0;


    elf_interpreter = NULL;
    for (i = 0; i < elf_ex.e_phnum; i++) {
        if (elf_ppnt->p_type == PT_INTERP) {
            if (elf_interpreter != NULL) {
                free(elf_phdata);
                free(elf_interpreter);
                close(bprm->fd);
                return -EINVAL;
            }

            elf_interpreter = (char *)malloc(elf_ppnt->p_filesz);
            if (elf_interpreter == NULL) {
                free(elf_phdata);
                close(bprm->fd);
                return -ENOMEM;
            }

            retval = lseek(bprm->fd, elf_ppnt->p_offset, SEEK_SET);
            if (retval >= 0) {
                retval = read(bprm->fd, elf_interpreter, elf_ppnt->p_filesz);
            }
            if (retval < 0) {
                perror("load_elf_binary2");
                exit(-1);
            }

            if (retval >= 0) {
                retval = open(path(elf_interpreter), O_RDONLY);
                if (retval >= 0) {
                    interpreter_fd = retval;
                } else {
                    perror(elf_interpreter);
                    exit(-1);
                    /* retval = -errno; */
                }
            }

            if (retval >= 0) {
                retval = lseek(interpreter_fd, 0, SEEK_SET);
                if (retval >= 0) {
                    retval = read(interpreter_fd, bprm->buf, 128);
                }
            }
            if (retval >= 0) {
                interp_elf_ex = *((struct elfhdr *) bprm->buf);
            }
            if (retval < 0) {
                perror("load_elf_binary3");
                exit(-1);
                free(elf_phdata);
                free(elf_interpreter);
                close(bprm->fd);
                return retval;
            }
        }
        elf_ppnt++;
    }

    /* Some simple consistency checks for the interpreter */
    if (elf_interpreter) {
        if (interp_elf_ex.e_ident[0] != 0x7f ||
            strncmp((char *)&interp_elf_ex.e_ident[1], "ELF", 3) != 0) {
            free(elf_interpreter);
            free(elf_phdata);
            close(bprm->fd);
            return -ELIBBAD;
        }
    }

    /*
     * OK, we are done with that, now set up the arg stuff, and then start this
     * sucker up
     */
    if (!bprm->p) {
        free(elf_interpreter);
        free(elf_phdata);
        close(bprm->fd);
        return -E2BIG;
    }

    /* OK, This is the point of no return */
    info->end_data = 0;
    info->end_code = 0;
    elf_entry = (abi_ulong) elf_ex.e_entry;

    /* XXX Join this with PT_INTERP search? */
    baddr = 0;
    for (i = 0, elf_ppnt = elf_phdata; i < elf_ex.e_phnum; i++, elf_ppnt++) {
        if (elf_ppnt->p_type != PT_LOAD) {
            continue;
        }
        baddr = elf_ppnt->p_vaddr;
        break;
    }

    et_dyn_addr = 0;
    if (elf_ex.e_type == ET_DYN && baddr == 0) {
        et_dyn_addr = ELF_ET_DYN_LOAD_ADDR;
    }

    /*
     * Do this so that we can load the interpreter, if need be.  We will
     * change some of these later
     */
    info->rss = 0;
    setup_arg_pages(bprm, info, &bprm->p, &bprm->stringp);
    info->start_stack = bprm->p;

    info->elf_flags = elf_ex.e_flags;

    error = load_elf_sections(&elf_ex, elf_phdata, bprm->fd, et_dyn_addr,
        &load_addr);
    for (i = 0, elf_ppnt = elf_phdata; i < elf_ex.e_phnum; i++, elf_ppnt++) {
        if (elf_ppnt->p_type != PT_LOAD) {
            continue;
        }
        if (elf_ppnt->p_memsz > elf_ppnt->p_filesz)
            elf_brk = MAX(elf_brk, et_dyn_addr + elf_ppnt->p_vaddr +
                elf_ppnt->p_memsz);
    }
    if (error != 0) {
        perror("load_elf_sections");
        exit(-1);
    }

    if (elf_interpreter) {
        elf_entry = load_elf_interp(&interp_elf_ex, interpreter_fd,
                                    &interp_load_addr);
        reloc_func_desc = interp_load_addr;

        close(interpreter_fd);
        free(elf_interpreter);

        if (elf_entry == ~((abi_ulong)0UL)) {
            printf("Unable to load interpreter\n");
            free(elf_phdata);
            exit(-1);
            return 0;
        }
    } else {
        interp_load_addr = et_dyn_addr;
        elf_entry += interp_load_addr;
    }

    free(elf_phdata);

    if (qemu_log_enabled()) {
        load_symbols(&elf_ex, bprm->fd);
    }

    close(bprm->fd);

    bprm->p = target_create_elf_tables(bprm->p, bprm->argc, bprm->envc,
                                       bprm->stringp, &elf_ex, load_addr,
                                       et_dyn_addr, interp_load_addr, info);
    info->load_addr = reloc_func_desc;
    info->brk = elf_brk;
    info->start_stack = bprm->p;
    info->load_bias = 0;

    info->entry = elf_entry;

#ifdef USE_ELF_CORE_DUMP
    bprm->core_dump = &elf_core_dump;
#else
    bprm->core_dump = NULL;
#endif

    return 0;
}

void do_init_thread(struct target_pt_regs *regs, struct image_info *infop)
{

    target_thread_init(regs, infop);
}
