/*
 *  Mach-O object file loading
 *
 *  Copyright (c) 2006 Pierre d'Herbemont
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include <stdio.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>

#include "qemu.h"
#include "disas.h"

#include <mach-o/loader.h>
#include <mach-o/fat.h>
#include <mach-o/nlist.h>
#include <mach-o/reloc.h>
#include <mach-o/ppc/reloc.h>

//#define DEBUG_MACHLOAD

#ifdef DEBUG_MACHLOAD
# define DPRINTF(...) do { qemu_log(__VA_ARGS__); printf(__VA_ARGS__); } while(0)
#else
# define DPRINTF(...) do { qemu_log(__VA_ARGS__); } while(0)
#endif

# define check_mach_header(x) (x.magic == MH_CIGAM)

extern const char *interp_prefix;

/* we don't have a good implementation for this */
#define DONT_USE_DYLD_SHARED_MAP

/* Pass extra arg to DYLD for debug */
//#define ACTIVATE_DYLD_TRACE

//#define OVERRIDE_DYLINKER

#ifdef OVERRIDE_DYLINKER
# ifdef TARGET_I386
#  define DYLINKER_NAME "/Users/steg/qemu/tests/i386-darwin-env/usr/lib/dyld"
# else
#  define DYLINKER_NAME "/usr/lib/dyld"
# endif
#endif

/* XXX: in an include */
struct nlist_extended
{
    union {
        char *n_name;
        long  n_strx;
    } n_un;
    unsigned char n_type;
    unsigned char n_sect;
    short st_desc;
    unsigned long st_value;
    unsigned long st_size;
};

/* Print symbols in gdb */
void *macho_text_sect = 0;
int   macho_offset = 0;

int load_object(const char *filename, struct target_pt_regs * regs, void ** mh);

#ifdef TARGET_I386
typedef struct mach_i386_thread_state {
    unsigned int    eax;
    unsigned int    ebx;
    unsigned int    ecx;
    unsigned int    edx;
    unsigned int    edi;
    unsigned int    esi;
    unsigned int    ebp;
    unsigned int    esp;
    unsigned int    ss;
    unsigned int    eflags;
    unsigned int    eip;
    unsigned int    cs;
    unsigned int    ds;
    unsigned int    es;
    unsigned int    fs;
    unsigned int    gs;
} mach_i386_thread_state_t;

void bswap_i386_thread_state(struct mach_i386_thread_state *ts)
{
    bswap32s((uint32_t*)&ts->eax);
    bswap32s((uint32_t*)&ts->ebx);
    bswap32s((uint32_t*)&ts->ecx);
    bswap32s((uint32_t*)&ts->edx);
    bswap32s((uint32_t*)&ts->edi);
    bswap32s((uint32_t*)&ts->esi);
    bswap32s((uint32_t*)&ts->ebp);
    bswap32s((uint32_t*)&ts->esp);
    bswap32s((uint32_t*)&ts->ss);
    bswap32s((uint32_t*)&ts->eflags);
    bswap32s((uint32_t*)&ts->eip);
    bswap32s((uint32_t*)&ts->cs);
    bswap32s((uint32_t*)&ts->ds);
    bswap32s((uint32_t*)&ts->es);
    bswap32s((uint32_t*)&ts->fs);
    bswap32s((uint32_t*)&ts->gs);
}
#define target_thread_state mach_i386_thread_state
#define TARGET_CPU_TYPE CPU_TYPE_I386
#define TARGET_CPU_NAME "i386"
#endif

#ifdef TARGET_PPC
struct mach_ppc_thread_state {
    unsigned int srr0;      /* Instruction address register (PC) */
    unsigned int srr1;    /* Machine state register (supervisor) */
    unsigned int r0;
    unsigned int r1;
    unsigned int r2;
    unsigned int r3;
    unsigned int r4;
    unsigned int r5;
    unsigned int r6;
    unsigned int r7;
    unsigned int r8;
    unsigned int r9;
    unsigned int r10;
    unsigned int r11;
    unsigned int r12;
    unsigned int r13;
    unsigned int r14;
    unsigned int r15;
    unsigned int r16;
    unsigned int r17;
    unsigned int r18;
    unsigned int r19;
    unsigned int r20;
    unsigned int r21;
    unsigned int r22;
    unsigned int r23;
    unsigned int r24;
    unsigned int r25;
    unsigned int r26;
    unsigned int r27;
    unsigned int r28;
    unsigned int r29;
    unsigned int r30;
    unsigned int r31;

    unsigned int cr;        /* Condition register */
    unsigned int xer;    /* User's integer exception register */
    unsigned int lr;    /* Link register */
    unsigned int ctr;    /* Count register */
    unsigned int mq;    /* MQ register (601 only) */

    unsigned int vrsave;    /* Vector Save Register */
};

void bswap_ppc_thread_state(struct mach_ppc_thread_state *ts)
{
    bswap32s((uint32_t*)&ts->srr0);
    bswap32s((uint32_t*)&ts->srr1);
    bswap32s((uint32_t*)&ts->r0);
    bswap32s((uint32_t*)&ts->r1);
    bswap32s((uint32_t*)&ts->r2);
    bswap32s((uint32_t*)&ts->r3);
    bswap32s((uint32_t*)&ts->r4);
    bswap32s((uint32_t*)&ts->r5);
    bswap32s((uint32_t*)&ts->r6);
    bswap32s((uint32_t*)&ts->r7);
    bswap32s((uint32_t*)&ts->r8);
    bswap32s((uint32_t*)&ts->r9);
    bswap32s((uint32_t*)&ts->r10);
    bswap32s((uint32_t*)&ts->r11);
    bswap32s((uint32_t*)&ts->r12);
    bswap32s((uint32_t*)&ts->r13);
    bswap32s((uint32_t*)&ts->r14);
    bswap32s((uint32_t*)&ts->r15);
    bswap32s((uint32_t*)&ts->r16);
    bswap32s((uint32_t*)&ts->r17);
    bswap32s((uint32_t*)&ts->r18);
    bswap32s((uint32_t*)&ts->r19);
    bswap32s((uint32_t*)&ts->r20);
    bswap32s((uint32_t*)&ts->r21);
    bswap32s((uint32_t*)&ts->r22);
    bswap32s((uint32_t*)&ts->r23);
    bswap32s((uint32_t*)&ts->r24);
    bswap32s((uint32_t*)&ts->r25);
    bswap32s((uint32_t*)&ts->r26);
    bswap32s((uint32_t*)&ts->r27);
    bswap32s((uint32_t*)&ts->r28);
    bswap32s((uint32_t*)&ts->r29);
    bswap32s((uint32_t*)&ts->r30);
    bswap32s((uint32_t*)&ts->r31);

    bswap32s((uint32_t*)&ts->cr);
    bswap32s((uint32_t*)&ts->xer);
    bswap32s((uint32_t*)&ts->lr);
    bswap32s((uint32_t*)&ts->ctr);
    bswap32s((uint32_t*)&ts->mq);

    bswap32s((uint32_t*)&ts->vrsave);
}

#define target_thread_state mach_ppc_thread_state
#define TARGET_CPU_TYPE CPU_TYPE_POWERPC
#define TARGET_CPU_NAME "PowerPC"
#endif

struct target_thread_command {
    unsigned long    cmd;    /* LC_THREAD or  LC_UNIXTHREAD */
    unsigned long    cmdsize;    /* total size of this command */
    unsigned long flavor;    /* flavor of thread state */
    unsigned long count;        /* count of longs in thread state */
    struct target_thread_state state;  /* thread state for this flavor */
};

void bswap_tc(struct target_thread_command *tc)
{
    bswap32s((uint32_t*)(&tc->flavor));
    bswap32s((uint32_t*)&tc->count);
#if defined(TARGET_I386)
    bswap_i386_thread_state(&tc->state);
#elif defined(TARGET_PPC)
    bswap_ppc_thread_state(&tc->state);
#else
# error unknown TARGET_CPU_TYPE
#endif
}

void bswap_mh(struct mach_header *mh)
{
    bswap32s((uint32_t*)(&mh->magic));
    bswap32s((uint32_t*)&mh->cputype);
    bswap32s((uint32_t*)&mh->cpusubtype);
    bswap32s((uint32_t*)&mh->filetype);
    bswap32s((uint32_t*)&mh->ncmds);
    bswap32s((uint32_t*)&mh->sizeofcmds);
    bswap32s((uint32_t*)&mh->flags);
}

void bswap_lc(struct load_command *lc)
{
    bswap32s((uint32_t*)&lc->cmd);
    bswap32s((uint32_t*)&lc->cmdsize);
}


void bswap_fh(struct fat_header *fh)
{
    bswap32s((uint32_t*)&fh->magic);
    bswap32s((uint32_t*)&fh->nfat_arch);
}

void bswap_fa(struct fat_arch *fa)
{
    bswap32s((uint32_t*)&fa->cputype);
    bswap32s((uint32_t*)&fa->cpusubtype);
    bswap32s((uint32_t*)&fa->offset);
    bswap32s((uint32_t*)&fa->size);
    bswap32s((uint32_t*)&fa->align);
}

void bswap_segcmd(struct segment_command *sc)
{
    bswap32s((uint32_t*)&sc->vmaddr);
    bswap32s((uint32_t*)&sc->vmsize);
    bswap32s((uint32_t*)&sc->fileoff);
    bswap32s((uint32_t*)&sc->filesize);
    bswap32s((uint32_t*)&sc->maxprot);
    bswap32s((uint32_t*)&sc->initprot);
    bswap32s((uint32_t*)&sc->nsects);
    bswap32s((uint32_t*)&sc->flags);
}

void bswap_symtabcmd(struct symtab_command *stc)
{
    bswap32s((uint32_t*)&stc->cmd);
    bswap32s((uint32_t*)&stc->cmdsize);
    bswap32s((uint32_t*)&stc->symoff);
    bswap32s((uint32_t*)&stc->nsyms);
    bswap32s((uint32_t*)&stc->stroff);
    bswap32s((uint32_t*)&stc->strsize);
}

void bswap_sym(struct nlist *n)
{
    bswap32s((uint32_t*)&n->n_un.n_strx);
    bswap16s((uint16_t*)&n->n_desc);
    bswap32s((uint32_t*)&n->n_value);
}

int load_thread(struct mach_header *mh, struct target_thread_command *tc, struct target_pt_regs * regs, int fd, int mh_pos, int need_bswap)
{
    int entry;
    if(need_bswap)
        bswap_tc(tc);
#if defined(TARGET_I386)
    entry = tc->state.eip;
    DPRINTF(" eax 0x%.8x\n ebx 0x%.8x\n ecx 0x%.8x\n edx 0x%.8x\n edi 0x%.8x\n esi 0x%.8x\n ebp 0x%.8x\n esp 0x%.8x\n ss 0x%.8x\n eflags 0x%.8x\n eip 0x%.8x\n cs 0x%.8x\n ds 0x%.8x\n es 0x%.8x\n fs 0x%.8x\n gs 0x%.8x\n",
            tc->state.eax, tc->state.ebx, tc->state.ecx, tc->state.edx, tc->state.edi, tc->state.esi, tc->state.ebp,
            tc->state.esp, tc->state.ss, tc->state.eflags, tc->state.eip, tc->state.cs, tc->state.ds, tc->state.es,
            tc->state.fs, tc->state.gs );
#define reg_copy(reg)   regs->reg = tc->state.reg
    if(regs)
    {
        reg_copy(eax);
        reg_copy(ebx);
        reg_copy(ecx);
        reg_copy(edx);

        reg_copy(edi);
        reg_copy(esi);

        reg_copy(ebp);
        reg_copy(esp);

        reg_copy(eflags);
        reg_copy(eip);
    /*
        reg_copy(ss);
        reg_copy(cs);
        reg_copy(ds);
        reg_copy(es);
        reg_copy(fs);
        reg_copy(gs);*/
    }
#undef reg_copy
#elif defined(TARGET_PPC)
    entry =  tc->state.srr0;
#endif
    DPRINTF("load_thread: entry 0x%x\n", entry);
    return entry;
}

int load_dylinker(struct mach_header *mh, struct dylinker_command *dc, int fd, int mh_pos, int need_bswap)
{
    int size;
    char * dylinker_name;
    size = dc->cmdsize - sizeof(struct dylinker_command);

    if(need_bswap)
        dylinker_name = (char*)(bswap_32(dc->name.offset)+(int)dc);
    else
        dylinker_name = (char*)((dc->name.offset)+(int)dc);

#ifdef OVERRIDE_DYLINKER
    dylinker_name = DYLINKER_NAME;
#else
    if(asprintf(&dylinker_name, "%s%s", interp_prefix, dylinker_name) == -1)
        qerror("can't allocate the new dylinker name\n");
#endif

    DPRINTF("dylinker_name %s\n", dylinker_name);
    return load_object(dylinker_name, NULL, NULL);
}

int load_segment(struct mach_header *mh, struct segment_command *sc, int fd, int mh_pos, int need_bswap, int fixed, int slide)
{
    unsigned long addr = sc->vmaddr;
    unsigned long size = sc->filesize;
    unsigned long error = 0;

    if(need_bswap)
        bswap_segcmd(sc);

    if(sc->vmaddr == 0)
    {
        DPRINTF("load_segment: sc->vmaddr == 0 returning\n");
        return -1;
    }

    if (strcmp(sc->segname, "__PAGEZERO") == 0)
    {
        DPRINTF("load_segment: __PAGEZERO returning\n");
        return -1;
    }

    /* Right now mmap memory */
    /* XXX: should check to see that the space is free, because MAP_FIXED is dangerous */
    DPRINTF("load_segment: mmaping %s to 0x%x-(0x%x|0x%x) + 0x%x\n", sc->segname, sc->vmaddr, sc->filesize, sc->vmsize, slide);

    if(sc->filesize > 0)
    {
        int opt = 0;

        if(fixed)
            opt |= MAP_FIXED;

        DPRINTF("sc->vmaddr 0x%x slide 0x%x add 0x%x\n", slide, sc->vmaddr, sc->vmaddr+slide);

        addr = target_mmap(sc->vmaddr+slide, sc->filesize,  sc->initprot, opt, fd, mh_pos + sc->fileoff);

        if(addr==-1)
            qerror("load_segment: can't mmap at 0x%x\n", sc->vmaddr+slide);

        error = addr-sc->vmaddr;
    }
    else
    {
        addr = sc->vmaddr+slide;
        error = slide;
    }

    if(sc->vmsize > sc->filesize)
    {
        addr += sc->filesize;
        size = sc->vmsize-sc->filesize;
        addr = target_mmap(addr, size, sc->initprot, MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        if(addr==-1)
            qerror("load_segment: can't mmap at 0x%x\n", sc->vmaddr+slide);
    }

    return error;
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

/* load a mach-o object file */
int load_object(const char *filename, struct target_pt_regs * regs, void ** mh)
{
    int need_bswap = 0;
    int entry_point = 0;
    int dyld_entry_point = 0;
    int slide, mmapfixed;
    int fd;
    struct load_command *lcmds, *lc;
    int is_fat = 0;
    unsigned int i, magic;
    int mach_hdr_pos = 0;
    struct mach_header mach_hdr;

    /* for symbol lookup whith -d flag. */
    struct symtab_command *    symtabcmd = 0;
    struct nlist_extended *symtab, *sym;
    struct nlist     *symtab_std, *syment;
    char            *strtab;

    fd = open(filename, O_RDONLY);
    if (fd < 0)
        qerror("can't open file '%s'", filename);

    /* Read magic header.  */
    if (read(fd, &magic, sizeof (magic)) != sizeof (magic))
        qerror("unable to read Magic of '%s'", filename);

    /* Check Mach identification.  */
    if(magic == MH_MAGIC)
    {
        is_fat = 0;
        need_bswap = 0;
    } else if (magic == MH_CIGAM)
    {
        is_fat = 0;
        need_bswap = 1;
    } else if (magic == FAT_MAGIC)
    {
        is_fat = 1;
        need_bswap = 0;
    } else if (magic == FAT_CIGAM)
    {
        is_fat = 1;
        need_bswap = 1;
    }
    else
        qerror("Not a Mach-O file.", filename);

    DPRINTF("loading %s %s...\n", filename, is_fat ? "[FAT]": "[REGULAR]");
    if(is_fat)
    {
        int found = 0;
        struct fat_header fh;
        struct fat_arch *fa;

        lseek(fd, 0, SEEK_SET);

        /* Read Fat header.  */
        if (read(fd, &fh, sizeof (fh)) != sizeof (fh))
            qerror("unable to read file header");

        if(need_bswap)
            bswap_fh(&fh);

        /* Read Fat Arch.  */
        fa = malloc(sizeof(struct fat_arch)*fh.nfat_arch);

        if (read(fd, fa, sizeof(struct fat_arch)*fh.nfat_arch) != sizeof(struct fat_arch)*fh.nfat_arch)
            qerror("unable to read file header");

        for( i = 0; i < fh.nfat_arch; i++, fa++)
        {
            if(need_bswap)
                bswap_fa(fa);
            if(fa->cputype == TARGET_CPU_TYPE)
            {
                mach_hdr_pos = fa->offset;
                lseek(fd, mach_hdr_pos, SEEK_SET);

                /* Read Mach header.  */

                if (read(fd, &mach_hdr, sizeof(struct mach_header)) != sizeof (struct mach_header))
                    qerror("unable to read file header");

                if(mach_hdr.magic == MH_MAGIC)
                    need_bswap = 0;
                else if (mach_hdr.magic == MH_CIGAM)
                    need_bswap = 1;
                else
                    qerror("Invalid mach header in Fat Mach-O File");
                found = 1;
                break;
            }
        }
        if(!found)
            qerror("%s: No %s CPU found in FAT Header", filename, TARGET_CPU_NAME);
    }
    else
    {
        lseek(fd, 0, SEEK_SET);
        /* Read Mach header */
        if (read(fd, &mach_hdr, sizeof (mach_hdr)) != sizeof (mach_hdr))
            qerror("%s: unable to read file header", filename);
    }

    if(need_bswap)
        bswap_mh(&mach_hdr);

    if ((mach_hdr.cputype) != TARGET_CPU_TYPE)
        qerror("%s: Unsupported CPU 0x%x (only 0x%x(%s) supported)", filename, mach_hdr.cputype, TARGET_CPU_TYPE, TARGET_CPU_NAME);


    switch(mach_hdr.filetype)
    {
        case MH_EXECUTE:  break;
        case MH_FVMLIB:
        case MH_DYLIB:
        case MH_DYLINKER: break;
        default:
            qerror("%s: Unsupported Mach type (0x%x)", filename, mach_hdr.filetype);
    }

    /* read segment headers */
    lcmds = malloc(mach_hdr.sizeofcmds);

    if(read(fd, lcmds, mach_hdr.sizeofcmds) != mach_hdr.sizeofcmds)
            qerror("%s: unable to read load_command", filename);
    slide = 0;
    mmapfixed = 0;
    for(i=0, lc = lcmds; i < (mach_hdr.ncmds) ; i++)
    {

        if(need_bswap)
            bswap_lc(lc);
        switch(lc->cmd)
        {
            case LC_SEGMENT:
                /* The main_exe can't be relocated */
                if(mach_hdr.filetype == MH_EXECUTE)
                    mmapfixed = 1;

                slide = load_segment(&mach_hdr, (struct segment_command*)lc, fd, mach_hdr_pos, need_bswap, mmapfixed, slide);

                /* other segment must be mapped according to slide exactly, if load_segment did something */
                if(slide != -1)
                    mmapfixed = 1;
                else
                    slide = 0; /* load_segment didn't map the segment */

                if(mach_hdr.filetype == MH_EXECUTE && slide != 0)
                    qerror("%s: Warning executable can't be mapped at the right address (offset: 0x%x)\n", filename, slide);

                if(strcmp(((struct segment_command*)(lc))->segname, "__TEXT") == 0)
                {
                    /* Text section */
                    if(mach_hdr.filetype == MH_EXECUTE)
                    {
                        /* return the mach_header */
                        *mh = (void*)(((struct segment_command*)(lc))->vmaddr + slide);
                    }
                    else
                    {
                        /* it is dyld save the section for gdb, we will be interested in dyld symbol
                           while debuging */
                        macho_text_sect = (void*)(((struct segment_command*)(lc))->vmaddr + slide);
                        macho_offset = slide;
                    }
                }
                break;
            case LC_LOAD_DYLINKER:
                dyld_entry_point = load_dylinker( &mach_hdr, (struct dylinker_command*)lc, fd, mach_hdr_pos, need_bswap );
                break;
            case LC_LOAD_DYLIB:
                /* dyld will do that for us */
                break;
            case LC_THREAD:
            case LC_UNIXTHREAD:
                {
                struct target_pt_regs * _regs;
                if(mach_hdr.filetype == MH_DYLINKER)
                    _regs = regs;
                else
                    _regs = 0;
                entry_point = load_thread( &mach_hdr, (struct target_thread_command*)lc, _regs, fd, mach_hdr_pos, need_bswap );
                }
                break;
            case LC_SYMTAB:
                /* Save the symtab and strtab */
                symtabcmd = (struct symtab_command *)lc;
                break;
            case LC_ID_DYLINKER:
            case LC_ID_DYLIB:
            case LC_UUID:
            case LC_DYSYMTAB:
            case LC_TWOLEVEL_HINTS:
            case LC_PREBIND_CKSUM:
            case LC_SUB_LIBRARY:
                break;
            default: fprintf(stderr, "warning: unkown command 0x%x in '%s'\n", lc->cmd, filename);
        }
        lc = (struct load_command*)((int)(lc)+(lc->cmdsize));
    }

    if(symtabcmd)
    {
        if(need_bswap)
            bswap_symtabcmd(symtabcmd);

        symtab_std = load_data(fd, symtabcmd->symoff+mach_hdr_pos, symtabcmd->nsyms * sizeof(struct nlist));
        strtab = load_data(fd, symtabcmd->stroff+mach_hdr_pos, symtabcmd->strsize);

        symtab = malloc(sizeof(struct nlist_extended) * symtabcmd->nsyms);

        if(need_bswap)
        {
            for(i = 0, syment = symtab_std; i < symtabcmd->nsyms; i++, syment++)
                bswap_sym(syment);
        }

        for(i = 0, sym = symtab, syment = symtab_std; i < symtabcmd->nsyms; i++, sym++, syment++)
        {
            struct nlist *sym_follow, *sym_next = 0;
            unsigned int j;
            memset(sym, 0, sizeof(*sym));

            sym->n_type = syment->n_type;
            if ( syment->n_type & N_STAB ) /* Debug symbols are skipped */
                continue;

            memcpy(sym, syment, sizeof(*syment));

            /* Find the following symbol in order to get the current symbol size */
            for(j = 0, sym_follow = symtab_std; j < symtabcmd->nsyms; j++, sym_follow++) {
                if ( sym_follow->n_type & N_STAB || !(sym_follow->n_value > sym->st_value))
                    continue;
                if(!sym_next) {
                    sym_next = sym_follow;
                    continue;
                }
                if(!(sym_next->n_value > sym_follow->n_value))
                    continue;
                sym_next = sym_follow;
            }
            if(sym_next)
                sym->st_size = sym_next->n_value - sym->st_value;
            else
                sym->st_size = 10; /* XXX: text_sec_hdr->size + text_sec_hdr->offset - sym->st_value; */

            sym->st_value += slide;
        }

        free((void*)symtab_std);

        {
            DPRINTF("saving symtab of %s (%d symbol(s))\n", filename, symtabcmd->nsyms);
            struct syminfo *s;
            s = malloc(sizeof(*s));
            s->disas_symtab = symtab;
            s->disas_strtab = strtab;
            s->disas_num_syms = symtabcmd->nsyms;
            s->next = syminfos;
            syminfos = s;
        }
    }
    close(fd);
    if(mach_hdr.filetype == MH_EXECUTE && dyld_entry_point)
        return dyld_entry_point;
    else
        return entry_point+slide;
}

extern unsigned long stack_size;

unsigned long setup_arg_pages(void * mh, char ** argv, char ** env)
{
    unsigned long stack_base, error, size;
    int i;
    int * stack;
    int argc, envc;

    /* Create enough stack to hold everything.  If we don't use
     * it for args, we'll use it for something else...
     */
    size = stack_size;

    error = target_mmap(0,
                        size + qemu_host_page_size,
                        PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS,
                        -1, 0);
    if (error == -1)
        qerror("stk mmap");

    /* we reserve one extra page at the top of the stack as guard */
    target_mprotect(error + size, qemu_host_page_size, PROT_NONE);

    stack_base = error + size;
    stack = (void*)stack_base;
/*
 *    | STRING AREA |
 *    +-------------+
 *    |      0      |
*    +-------------+
 *    |  apple[n]   |
 *    +-------------+
 *           :
 *    +-------------+
 *    |  apple[0]   |
 *    +-------------+
 *    |      0      |
 *    +-------------+
 *    |    env[n]   |
 *    +-------------+
 *           :
 *           :
 *    +-------------+
 *    |    env[0]   |
 *    +-------------+
 *    |      0      |
 *    +-------------+
 *    | arg[argc-1] |
 *    +-------------+
 *           :
 *           :
 *    +-------------+
 *    |    arg[0]   |
 *    +-------------+
 *    |     argc    |
 *    +-------------+
 * sp->    |      mh     | address of where the a.out's file offset 0 is in memory
 *    +-------------+
*/
    /* Construct the stack Stack grows down */
    stack--;

    /* XXX: string should go up there */

    *stack = 0;
    stack--;

    /* Push the absolute path of our executable */
    DPRINTF("pushing apple %s (0x%x)\n", (char*)argv[0], (int)argv[0]);
    stl(stack, (int) argv[0]);

    stack--;

    stl(stack, 0);
    stack--;

    /* Get envc */
    for(envc = 0; env[envc]; envc++);

    for(i = envc-1; i >= 0; i--)
    {
        DPRINTF("pushing env %s (0x%x)\n", (char*)env[i], (int)env[i]);
        stl(stack, (int)env[i]);
        stack--;

        /* XXX: remove that when string will be on top of the stack */
        page_set_flags((int)env[i], (int)(env[i]+strlen(env[i])), PROT_READ | PAGE_VALID);
    }

    /* Add on the stack the interp_prefix choosen if so */
    if(interp_prefix[0])
    {
        char *dyld_root;
        asprintf(&dyld_root, "DYLD_ROOT_PATH=%s", interp_prefix);
        page_set_flags((int)dyld_root, (int)(dyld_root+strlen(interp_prefix)+1), PROT_READ | PAGE_VALID);

        stl(stack, (int)dyld_root);
        stack--;
    }

#ifdef DONT_USE_DYLD_SHARED_MAP
    {
        char *shared_map_mode;
        asprintf(&shared_map_mode, "DYLD_SHARED_REGION=avoid");
        page_set_flags((int)shared_map_mode, (int)(shared_map_mode+strlen(shared_map_mode)+1), PROT_READ | PAGE_VALID);

        stl(stack, (int)shared_map_mode);
        stack--;
    }
#endif

#ifdef ACTIVATE_DYLD_TRACE
    char * extra_env_static[] = {"DYLD_DEBUG_TRACE=yes",
    "DYLD_PREBIND_DEBUG=3", "DYLD_UNKNOW_TRACE=yes",
    "DYLD_PRINT_INITIALIZERS=yes",
    "DYLD_PRINT_SEGMENTS=yes", "DYLD_PRINT_REBASINGS=yes", "DYLD_PRINT_BINDINGS=yes", "DYLD_PRINT_INITIALIZERS=yes", "DYLD_PRINT_WARNINGS=yes" };

    char ** extra_env = malloc(sizeof(extra_env_static));
    bcopy(extra_env_static, extra_env, sizeof(extra_env_static));
    page_set_flags((int)extra_env, (int)((void*)extra_env+sizeof(extra_env_static)), PROT_READ | PAGE_VALID);

    for(i = 0; i<9; i++)
    {
        DPRINTF("pushing (extra) env %s (0x%x)\n", (char*)extra_env[i], (int)extra_env[i]);
        stl(stack, (int) extra_env[i]);
        stack--;
    }
#endif

    stl(stack, 0);
    stack--;

    /* Get argc */
    for(argc = 0; argv[argc]; argc++);

    for(i = argc-1; i >= 0; i--)
    {
        DPRINTF("pushing arg %s (0x%x)\n", (char*)argv[i], (int)argv[i]);
        stl(stack, (int) argv[i]);
        stack--;

        /* XXX: remove that when string will be on top of the stack */
        page_set_flags((int)argv[i], (int)(argv[i]+strlen(argv[i])), PROT_READ | PAGE_VALID);
    }

    DPRINTF("pushing argc %d\n", argc);
    stl(stack, argc);
    stack--;

    DPRINTF("pushing mh 0x%x\n", (int)mh);
    stl(stack, (int) mh);

    /* Stack points on the mh */
    return (unsigned long)stack;
}

int mach_exec(const char * filename, char ** argv, char ** envp,
             struct target_pt_regs * regs)
{
    int entrypoint, stack;
    void * mh; /* the Mach Header that will be  used by dyld */

    DPRINTF("mach_exec at 0x%x\n", (int)mach_exec);

    entrypoint = load_object(filename, regs, &mh);
    stack = setup_arg_pages(mh, argv, envp);
#if defined(TARGET_I386)
    regs->eip = entrypoint;
    regs->esp = stack;
#elif defined(TARGET_PPC)
    regs->nip = entrypoint;
    regs->gpr[1] = stack;
#endif
    DPRINTF("mach_exec returns eip set to 0x%x esp 0x%x mh 0x%x\n", entrypoint, stack, (int)mh);

    if(!entrypoint)
        qerror("%s: no entry point!\n", filename);

    return 0;
}
