/* This is the Linux kernel elf-loading code, ported into user space */
#include "qemu/osdep.h"
#include <sys/param.h>

#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/shm.h>

#include "qemu.h"
#include "user/tswap-target.h"
#include "user/page-protection.h"
#include "exec/page-protection.h"
#include "exec/mmap-lock.h"
#include "exec/translation-block.h"
#include "exec/tswap.h"
#include "user/guest-base.h"
#include "user-internals.h"
#include "signal-common.h"
#include "loader.h"
#include "user-mmap.h"
#include "disas/disas.h"
#include "qemu/bitops.h"
#include "qemu/path.h"
#include "qemu/queue.h"
#include "qemu/guest-random.h"
#include "qemu/units.h"
#include "qemu/selfmap.h"
#include "qemu/lockable.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "target_elf.h"
#include "target_signal.h"
#include "tcg/debuginfo.h"

#ifdef TARGET_ARM
#include "target/arm/cpu-features.h"
#endif

#ifndef TARGET_ARCH_HAS_SIGTRAMP_PAGE
#define TARGET_ARCH_HAS_SIGTRAMP_PAGE 0
#endif

#define ELF_OSABI   ELFOSABI_SYSV

/* from personality.h */

/*
 * Flags for bug emulation.
 *
 * These occupy the top three bytes.
 */
enum {
    ADDR_NO_RANDOMIZE = 0x0040000,      /* disable randomization of VA space */
    FDPIC_FUNCPTRS =    0x0080000,      /* userspace function ptrs point to
                                           descriptors (signal handling) */
    MMAP_PAGE_ZERO =    0x0100000,
    ADDR_COMPAT_LAYOUT = 0x0200000,
    READ_IMPLIES_EXEC = 0x0400000,
    ADDR_LIMIT_32BIT =  0x0800000,
    SHORT_INODE =       0x1000000,
    WHOLE_SECONDS =     0x2000000,
    STICKY_TIMEOUTS =   0x4000000,
    ADDR_LIMIT_3GB =    0x8000000,
};

/*
 * Personality types.
 *
 * These go in the low byte.  Avoid using the top bit, it will
 * conflict with error returns.
 */
enum {
    PER_LINUX =         0x0000,
    PER_LINUX_32BIT =   0x0000 | ADDR_LIMIT_32BIT,
    PER_LINUX_FDPIC =   0x0000 | FDPIC_FUNCPTRS,
    PER_SVR4 =          0x0001 | STICKY_TIMEOUTS | MMAP_PAGE_ZERO,
    PER_SVR3 =          0x0002 | STICKY_TIMEOUTS | SHORT_INODE,
    PER_SCOSVR3 =       0x0003 | STICKY_TIMEOUTS | WHOLE_SECONDS | SHORT_INODE,
    PER_OSR5 =          0x0003 | STICKY_TIMEOUTS | WHOLE_SECONDS,
    PER_WYSEV386 =      0x0004 | STICKY_TIMEOUTS | SHORT_INODE,
    PER_ISCR4 =         0x0005 | STICKY_TIMEOUTS,
    PER_BSD =           0x0006,
    PER_SUNOS =         0x0006 | STICKY_TIMEOUTS,
    PER_XENIX =         0x0007 | STICKY_TIMEOUTS | SHORT_INODE,
    PER_LINUX32 =       0x0008,
    PER_LINUX32_3GB =   0x0008 | ADDR_LIMIT_3GB,
    PER_IRIX32 =        0x0009 | STICKY_TIMEOUTS,/* IRIX5 32-bit */
    PER_IRIXN32 =       0x000a | STICKY_TIMEOUTS,/* IRIX6 new 32-bit */
    PER_IRIX64 =        0x000b | STICKY_TIMEOUTS,/* IRIX6 64-bit */
    PER_RISCOS =        0x000c,
    PER_SOLARIS =       0x000d | STICKY_TIMEOUTS,
    PER_UW7 =           0x000e | STICKY_TIMEOUTS | MMAP_PAGE_ZERO,
    PER_OSF4 =          0x000f,                  /* OSF/1 v4 */
    PER_HPUX =          0x0010,
    PER_MASK =          0x00ff,
};

/*
 * Return the base personality without flags.
 */
#define personality(pers)       (pers & PER_MASK)

int info_is_fdpic(struct image_info *info)
{
    return info->personality == PER_LINUX_FDPIC;
}

#if TARGET_BIG_ENDIAN
#define ELF_DATA        ELFDATA2MSB
#else
#define ELF_DATA        ELFDATA2LSB
#endif

#ifdef USE_UID16
typedef abi_ushort      target_uid_t;
typedef abi_ushort      target_gid_t;
#else
typedef abi_uint        target_uid_t;
typedef abi_uint        target_gid_t;
#endif
typedef abi_int         target_pid_t;

#ifndef elf_check_machine
#define elf_check_machine(x) ((x) == ELF_MACHINE)
#endif

#ifndef elf_check_abi
#define elf_check_abi(x) (1)
#endif

#ifndef STACK_GROWS_DOWN
#define STACK_GROWS_DOWN 1
#endif

#ifndef STACK_ALIGNMENT
#define STACK_ALIGNMENT 16
#endif

#ifdef TARGET_ABI32
#undef ELF_CLASS
#define ELF_CLASS ELFCLASS32
#undef bswaptls
#define bswaptls(ptr) bswap32s(ptr)
#endif

#ifndef EXSTACK_DEFAULT
#define EXSTACK_DEFAULT false
#endif

/*
 * Provide fallback definitions that the target may omit.
 * One way or another, we'll get a link error if the setting of
 * HAVE_* doesn't match the implementation.
 */
#ifndef HAVE_ELF_HWCAP
abi_ulong get_elf_hwcap(CPUState *cs) { return 0; }
#endif
#ifndef HAVE_ELF_HWCAP2
abi_ulong get_elf_hwcap2(CPUState *cs) { g_assert_not_reached(); }
#define HAVE_ELF_HWCAP2 0
#endif
#ifndef HAVE_ELF_PLATFORM
const char *get_elf_platform(CPUState *cs) { return NULL; }
#endif
#ifndef HAVE_ELF_BASE_PLATFORM
const char *get_elf_base_platform(CPUState *cs) { return NULL; }
#endif

#ifndef HAVE_ELF_GNU_PROPERTY
bool arch_parse_elf_property(uint32_t pr_type, uint32_t pr_datasz,
                             const uint32_t *data, struct image_info *info,
                             Error **errp)
{
    g_assert_not_reached();
}
#define HAVE_ELF_GNU_PROPERTY 0
#endif

#include "elf.h"

#define DLINFO_ITEMS 16

static inline void memcpy_fromfs(void * to, const void * from, unsigned long n)
{
    memcpy(to, from, n);
}

static void bswap_ehdr(struct elfhdr *ehdr)
{
    if (!target_needs_bswap()) {
        return;
    }

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
    if (!target_needs_bswap()) {
        return;
    }

    for (int i = 0; i < phnum; ++i, ++phdr) {
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
    if (!target_needs_bswap()) {
        return;
    }

    for (int i = 0; i < shnum; ++i, ++shdr) {
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
    if (!target_needs_bswap()) {
        return;
    }

    bswap32s(&sym->st_name);
    bswaptls(&sym->st_value);
    bswaptls(&sym->st_size);
    bswap16s(&sym->st_shndx);
}

#ifdef TARGET_MIPS
static void bswap_mips_abiflags(Mips_elf_abiflags_v0 *abiflags)
{
    if (!target_needs_bswap()) {
        return;
    }

    bswap16s(&abiflags->version);
    bswap32s(&abiflags->ases);
    bswap32s(&abiflags->isa_ext);
    bswap32s(&abiflags->flags1);
    bswap32s(&abiflags->flags2);
}
#endif

#ifdef HAVE_ELF_CORE_DUMP
static int elf_core_dump(int, const CPUArchState *);
#endif /* HAVE_ELF_CORE_DUMP */
static void load_symbols(struct elfhdr *hdr, const ImageSource *src,
                         abi_ulong load_bias);

/* Verify the portions of EHDR within E_IDENT for the target.
   This can be performed before bswapping the entire header.  */
static bool elf_check_ident(struct elfhdr *ehdr)
{
    return (ehdr->e_ident[EI_MAG0] == ELFMAG0
            && ehdr->e_ident[EI_MAG1] == ELFMAG1
            && ehdr->e_ident[EI_MAG2] == ELFMAG2
            && ehdr->e_ident[EI_MAG3] == ELFMAG3
            && ehdr->e_ident[EI_CLASS] == ELF_CLASS
            && ehdr->e_ident[EI_DATA] == ELF_DATA
            && ehdr->e_ident[EI_VERSION] == EV_CURRENT);
}

/* Verify the portions of EHDR outside of E_IDENT for the target.
   This has to wait until after bswapping the header.  */
static bool elf_check_ehdr(struct elfhdr *ehdr)
{
    return (elf_check_machine(ehdr->e_machine)
            && elf_check_abi(ehdr->e_flags)
            && ehdr->e_ehsize == sizeof(struct elfhdr)
            && ehdr->e_phentsize == sizeof(struct elf_phdr)
            && (ehdr->e_type == ET_EXEC || ehdr->e_type == ET_DYN));
}

/*
 * 'copy_elf_strings()' copies argument/envelope strings from user
 * memory to free pages in kernel mem. These are in a format ready
 * to be put directly into the top of new user memory.
 *
 */
static abi_ulong copy_elf_strings(int argc, char **argv, char *scratch,
                                  abi_ulong p, abi_ulong stack_limit)
{
    char *tmp;
    int len, i;
    abi_ulong top = p;

    if (!p) {
        return 0;       /* bullet-proofing */
    }

    if (STACK_GROWS_DOWN) {
        int offset = ((p - 1) % TARGET_PAGE_SIZE) + 1;
        for (i = argc - 1; i >= 0; --i) {
            tmp = argv[i];
            if (!tmp) {
                fprintf(stderr, "VFS: argc is wrong");
                exit(-1);
            }
            len = strlen(tmp) + 1;
            tmp += len;

            if (len > (p - stack_limit)) {
                return 0;
            }
            while (len) {
                int bytes_to_copy = (len > offset) ? offset : len;
                tmp -= bytes_to_copy;
                p -= bytes_to_copy;
                offset -= bytes_to_copy;
                len -= bytes_to_copy;

                memcpy_fromfs(scratch + offset, tmp, bytes_to_copy);

                if (offset == 0) {
                    memcpy_to_target(p, scratch, top - p);
                    top = p;
                    offset = TARGET_PAGE_SIZE;
                }
            }
        }
        if (p != top) {
            memcpy_to_target(p, scratch + offset, top - p);
        }
    } else {
        int remaining = TARGET_PAGE_SIZE - (p % TARGET_PAGE_SIZE);
        for (i = 0; i < argc; ++i) {
            tmp = argv[i];
            if (!tmp) {
                fprintf(stderr, "VFS: argc is wrong");
                exit(-1);
            }
            len = strlen(tmp) + 1;
            if (len > (stack_limit - p)) {
                return 0;
            }
            while (len) {
                int bytes_to_copy = (len > remaining) ? remaining : len;

                memcpy_fromfs(scratch + (p - top), tmp, bytes_to_copy);

                tmp += bytes_to_copy;
                remaining -= bytes_to_copy;
                p += bytes_to_copy;
                len -= bytes_to_copy;

                if (remaining == 0) {
                    memcpy_to_target(top, scratch, p - top);
                    top = p;
                    remaining = TARGET_PAGE_SIZE;
                }
            }
        }
        if (p != top) {
            memcpy_to_target(top, scratch, p - top);
        }
    }

    return p;
}

/* Older linux kernels provide up to MAX_ARG_PAGES (default: 32) of
 * argument/environment space. Newer kernels (>2.6.33) allow more,
 * dependent on stack size, but guarantee at least 32 pages for
 * backwards compatibility.
 */
#define STACK_LOWER_LIMIT (32 * TARGET_PAGE_SIZE)

static abi_ulong setup_arg_pages(struct linux_binprm *bprm,
                                 struct image_info *info)
{
    abi_ulong size, error, guard;
    int prot;

    size = guest_stack_size;
    if (size < STACK_LOWER_LIMIT) {
        size = STACK_LOWER_LIMIT;
    }

    if (STACK_GROWS_DOWN) {
        guard = TARGET_PAGE_SIZE;
        if (guard < qemu_real_host_page_size()) {
            guard = qemu_real_host_page_size();
        }
    } else {
        /* no guard page for hppa target where stack grows upwards. */
        guard = 0;
    }

    prot = PROT_READ | PROT_WRITE;
    if (info->exec_stack) {
        prot |= PROT_EXEC;
    }
    error = target_mmap(0, size + guard, prot,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (error == -1) {
        perror("mmap stack");
        exit(-1);
    }

    /* We reserve one extra page at the top of the stack as guard.  */
    if (STACK_GROWS_DOWN) {
        target_mprotect(error, guard, PROT_NONE);
        info->stack_limit = error + guard;
        return info->stack_limit + size - sizeof(void *);
    } else {
        info->stack_limit = error + size;
        return error;
    }
}

/**
 * zero_bss:
 *
 * Map and zero the bss.  We need to explicitly zero any fractional pages
 * after the data section (i.e. bss).  Return false on mapping failure.
 */
static bool zero_bss(abi_ulong start_bss, abi_ulong end_bss,
                     int prot, Error **errp)
{
    abi_ulong align_bss;

    /* We only expect writable bss; the code segment shouldn't need this. */
    if (!(prot & PROT_WRITE)) {
        error_setg(errp, "PT_LOAD with non-writable bss");
        return false;
    }

    align_bss = TARGET_PAGE_ALIGN(start_bss);
    end_bss = TARGET_PAGE_ALIGN(end_bss);

    if (start_bss < align_bss) {
        int flags = page_get_flags(start_bss);

        if (!(flags & PAGE_RWX)) {
            /*
             * The whole address space of the executable was reserved
             * at the start, therefore all pages will be VALID.
             * But assuming there are no PROT_NONE PT_LOAD segments,
             * a PROT_NONE page means no data all bss, and we can
             * simply extend the new anon mapping back to the start
             * of the page of bss.
             */
            align_bss -= TARGET_PAGE_SIZE;
        } else {
            /*
             * The start of the bss shares a page with something.
             * The only thing that we expect is the data section,
             * which would already be marked writable.
             * Overlapping the RX code segment seems malformed.
             */
            if (!(flags & PAGE_WRITE)) {
                error_setg(errp, "PT_LOAD with bss overlapping "
                           "non-writable page");
                return false;
            }

            /* The page is already mapped and writable. */
            memset(g2h_untagged(start_bss), 0, align_bss - start_bss);
        }
    }

    if (align_bss < end_bss &&
        target_mmap(align_bss, end_bss - align_bss, prot,
                    MAP_FIXED | MAP_PRIVATE | MAP_ANON, -1, 0) == -1) {
        error_setg_errno(errp, errno, "Error mapping bss");
        return false;
    }
    return true;
}

#if defined(TARGET_ARM)
static int elf_is_fdpic(struct elfhdr *exec)
{
    return exec->e_ident[EI_OSABI] == ELFOSABI_ARM_FDPIC;
}
#elif defined(TARGET_XTENSA)
static int elf_is_fdpic(struct elfhdr *exec)
{
    return exec->e_ident[EI_OSABI] == ELFOSABI_XTENSA_FDPIC;
}
#else
/* Default implementation, always false.  */
static int elf_is_fdpic(struct elfhdr *exec)
{
    return 0;
}
#endif

static abi_ulong loader_build_fdpic_loadmap(struct image_info *info, abi_ulong sp)
{
    uint16_t n;
    struct elf32_fdpic_loadseg *loadsegs = info->loadsegs;

    /* elf32_fdpic_loadseg */
    n = info->nsegs;
    while (n--) {
        sp -= 12;
        put_user_u32(loadsegs[n].addr, sp+0);
        put_user_u32(loadsegs[n].p_vaddr, sp+4);
        put_user_u32(loadsegs[n].p_memsz, sp+8);
    }

    /* elf32_fdpic_loadmap */
    sp -= 4;
    put_user_u16(0, sp+0); /* version */
    put_user_u16(info->nsegs, sp+2); /* nsegs */

    info->personality = PER_LINUX_FDPIC;
    info->loadmap_addr = sp;

    return sp;
}

static abi_ulong create_elf_tables(abi_ulong p, int argc, int envc,
                                   struct elfhdr *exec,
                                   struct image_info *info,
                                   struct image_info *interp_info,
                                   struct image_info *vdso_info)
{
    abi_ulong sp;
    abi_ulong u_argc, u_argv, u_envp, u_auxv;
    int size;
    int i;
    abi_ulong u_rand_bytes;
    uint8_t k_rand_bytes[16];
    abi_ulong u_platform, u_base_platform;
    const char *k_platform, *k_base_platform;
    const int n = sizeof(elf_addr_t);

    sp = p;

    /* Needs to be before we load the env/argc/... */
    if (elf_is_fdpic(exec)) {
        /* Need 4 byte alignment for these structs */
        sp &= ~3;
        sp = loader_build_fdpic_loadmap(info, sp);
        info->other_info = interp_info;
        if (interp_info) {
            interp_info->other_info = info;
            sp = loader_build_fdpic_loadmap(interp_info, sp);
            info->interpreter_loadmap_addr = interp_info->loadmap_addr;
            info->interpreter_pt_dynamic_addr = interp_info->pt_dynamic_addr;
        } else {
            info->interpreter_loadmap_addr = 0;
            info->interpreter_pt_dynamic_addr = 0;
        }
    }

    u_base_platform = 0;
    k_base_platform = get_elf_base_platform(thread_cpu);
    if (k_base_platform) {
        size_t len = strlen(k_base_platform) + 1;
        if (STACK_GROWS_DOWN) {
            sp -= (len + n - 1) & ~(n - 1);
            u_base_platform = sp;
            /* FIXME - check return value of memcpy_to_target() for failure */
            memcpy_to_target(sp, k_base_platform, len);
        } else {
            memcpy_to_target(sp, k_base_platform, len);
            u_base_platform = sp;
            sp += len + 1;
        }
    }

    u_platform = 0;
    k_platform = get_elf_platform(thread_cpu);
    if (k_platform) {
        size_t len = strlen(k_platform) + 1;
        if (STACK_GROWS_DOWN) {
            sp -= (len + n - 1) & ~(n - 1);
            u_platform = sp;
            /* FIXME - check return value of memcpy_to_target() for failure */
            memcpy_to_target(sp, k_platform, len);
        } else {
            memcpy_to_target(sp, k_platform, len);
            u_platform = sp;
            sp += len + 1;
        }
    }

    /* Provide 16 byte alignment for the PRNG, and basic alignment for
     * the argv and envp pointers.
     */
    if (STACK_GROWS_DOWN) {
        sp = QEMU_ALIGN_DOWN(sp, 16);
    } else {
        sp = QEMU_ALIGN_UP(sp, 16);
    }

    /*
     * Generate 16 random bytes for userspace PRNG seeding.
     */
    qemu_guest_getrandom_nofail(k_rand_bytes, sizeof(k_rand_bytes));
    if (STACK_GROWS_DOWN) {
        sp -= 16;
        u_rand_bytes = sp;
        /* FIXME - check return value of memcpy_to_target() for failure */
        memcpy_to_target(sp, k_rand_bytes, 16);
    } else {
        memcpy_to_target(sp, k_rand_bytes, 16);
        u_rand_bytes = sp;
        sp += 16;
    }

    size = (DLINFO_ITEMS + 1) * 2;
    if (k_base_platform) {
        size += 2;
    }
    if (k_platform) {
        size += 2;
    }
    if (vdso_info) {
        size += 2;
    }
#ifdef DLINFO_ARCH_ITEMS
    size += DLINFO_ARCH_ITEMS * 2;
#endif
    if (HAVE_ELF_HWCAP2) {
        size += 2;
    }
    info->auxv_len = size * n;

    size += envc + argc + 2;
    size += 1;  /* argc itself */
    size *= n;

    /* Allocate space and finalize stack alignment for entry now.  */
    if (STACK_GROWS_DOWN) {
        u_argc = QEMU_ALIGN_DOWN(sp - size, STACK_ALIGNMENT);
        sp = u_argc;
    } else {
        u_argc = sp;
        sp = QEMU_ALIGN_UP(sp + size, STACK_ALIGNMENT);
    }

    u_argv = u_argc + n;
    u_envp = u_argv + (argc + 1) * n;
    u_auxv = u_envp + (envc + 1) * n;
    info->saved_auxv = u_auxv;
    info->argc = argc;
    info->envc = envc;
    info->argv = u_argv;
    info->envp = u_envp;

    /* This is correct because Linux defines
     * elf_addr_t as Elf32_Off / Elf64_Off
     */
#define NEW_AUX_ENT(id, val) do {               \
        put_user_ual(id, u_auxv);  u_auxv += n; \
        put_user_ual(val, u_auxv); u_auxv += n; \
    } while(0)

#ifdef ARCH_DLINFO
    /*
     * ARCH_DLINFO must come first so platform specific code can enforce
     * special alignment requirements on the AUXV if necessary (eg. PPC).
     */
    ARCH_DLINFO;
#endif
    /* There must be exactly DLINFO_ITEMS entries here, or the assert
     * on info->auxv_len will trigger.
     */
    NEW_AUX_ENT(AT_PHDR, (abi_ulong)(info->load_addr + exec->e_phoff));
    NEW_AUX_ENT(AT_PHENT, (abi_ulong)(sizeof (struct elf_phdr)));
    NEW_AUX_ENT(AT_PHNUM, (abi_ulong)(exec->e_phnum));
    NEW_AUX_ENT(AT_PAGESZ, (abi_ulong)(TARGET_PAGE_SIZE));
    NEW_AUX_ENT(AT_BASE, (abi_ulong)(interp_info ? interp_info->load_addr : 0));
    NEW_AUX_ENT(AT_FLAGS, (abi_ulong)0);
    NEW_AUX_ENT(AT_ENTRY, info->entry);
    NEW_AUX_ENT(AT_UID, (abi_ulong) getuid());
    NEW_AUX_ENT(AT_EUID, (abi_ulong) geteuid());
    NEW_AUX_ENT(AT_GID, (abi_ulong) getgid());
    NEW_AUX_ENT(AT_EGID, (abi_ulong) getegid());
    NEW_AUX_ENT(AT_HWCAP, get_elf_hwcap(thread_cpu));
    NEW_AUX_ENT(AT_CLKTCK, (abi_ulong) sysconf(_SC_CLK_TCK));
    NEW_AUX_ENT(AT_RANDOM, (abi_ulong) u_rand_bytes);
    NEW_AUX_ENT(AT_SECURE, (abi_ulong) qemu_getauxval(AT_SECURE));
    NEW_AUX_ENT(AT_EXECFN, info->file_string);

    if (HAVE_ELF_HWCAP2) {
        NEW_AUX_ENT(AT_HWCAP2, get_elf_hwcap(thread_cpu));
    }
    if (u_base_platform) {
        NEW_AUX_ENT(AT_BASE_PLATFORM, u_base_platform);
    }
    if (u_platform) {
        NEW_AUX_ENT(AT_PLATFORM, u_platform);
    }
    if (vdso_info) {
        NEW_AUX_ENT(AT_SYSINFO_EHDR, vdso_info->load_addr);
    }
    NEW_AUX_ENT (AT_NULL, 0);
#undef NEW_AUX_ENT

    /* Check that our initial calculation of the auxv length matches how much
     * we actually put into it.
     */
    assert(info->auxv_len == u_auxv - info->saved_auxv);

    put_user_ual(argc, u_argc);

    p = info->arg_strings;
    for (i = 0; i < argc; ++i) {
        put_user_ual(p, u_argv);
        u_argv += n;
        p += target_strlen(p) + 1;
    }
    put_user_ual(0, u_argv);

    p = info->env_strings;
    for (i = 0; i < envc; ++i) {
        put_user_ual(p, u_envp);
        u_envp += n;
        p += target_strlen(p) + 1;
    }
    put_user_ual(0, u_envp);

    return sp;
}

#if defined(HI_COMMPAGE)
#define LO_COMMPAGE -1
#elif defined(LO_COMMPAGE)
#define HI_COMMPAGE 0
#else
#define HI_COMMPAGE 0
#define LO_COMMPAGE -1
#ifndef HAVE_GUEST_COMMPAGE
bool init_guest_commpage(void) { return true; }
#endif
#endif

/**
 * pgb_try_mmap:
 * @addr: host start address
 * @addr_last: host last address
 * @keep: do not unmap the probe region
 *
 * Return 1 if [@addr, @addr_last] is not mapped in the host,
 * return 0 if it is not available to map, and -1 on mmap error.
 * If @keep, the region is left mapped on success, otherwise unmapped.
 */
static int pgb_try_mmap(uintptr_t addr, uintptr_t addr_last, bool keep)
{
    size_t size = addr_last - addr + 1;
    void *p = mmap((void *)addr, size, PROT_NONE,
                   MAP_ANONYMOUS | MAP_PRIVATE |
                   MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    int ret;

    if (p == MAP_FAILED) {
        return errno == EEXIST ? 0 : -1;
    }
    ret = p == (void *)addr;
    if (!keep || !ret) {
        munmap(p, size);
    }
    return ret;
}

/**
 * pgb_try_mmap_skip_brk(uintptr_t addr, uintptr_t size, uintptr_t brk)
 * @addr: host address
 * @addr_last: host last address
 * @brk: host brk
 *
 * Like pgb_try_mmap, but additionally reserve some memory following brk.
 */
static int pgb_try_mmap_skip_brk(uintptr_t addr, uintptr_t addr_last,
                                 uintptr_t brk, bool keep)
{
    uintptr_t brk_last = brk + 16 * MiB - 1;

    /* Do not map anything close to the host brk. */
    if (addr <= brk_last && brk <= addr_last) {
        return 0;
    }
    return pgb_try_mmap(addr, addr_last, keep);
}

/**
 * pgb_try_mmap_set:
 * @ga: set of guest addrs
 * @base: guest_base
 * @brk: host brk
 *
 * Return true if all @ga can be mapped by the host at @base.
 * On success, retain the mapping at index 0 for reserved_va.
 */

typedef struct PGBAddrs {
    uintptr_t bounds[3][2]; /* start/last pairs */
    int nbounds;
} PGBAddrs;

static bool pgb_try_mmap_set(const PGBAddrs *ga, uintptr_t base, uintptr_t brk)
{
    for (int i = ga->nbounds - 1; i >= 0; --i) {
        if (pgb_try_mmap_skip_brk(ga->bounds[i][0] + base,
                                  ga->bounds[i][1] + base,
                                  brk, i == 0 && reserved_va) <= 0) {
            return false;
        }
    }
    return true;
}

/**
 * pgb_addr_set:
 * @ga: output set of guest addrs
 * @guest_loaddr: guest image low address
 * @guest_loaddr: guest image high address
 * @identity: create for identity mapping
 *
 * Fill in @ga with the image, COMMPAGE and NULL page.
 */
static bool pgb_addr_set(PGBAddrs *ga, abi_ulong guest_loaddr,
                         abi_ulong guest_hiaddr, bool try_identity)
{
    int n;

    /*
     * With a low commpage, or a guest mapped very low,
     * we may not be able to use the identity map.
     */
    if (try_identity) {
        if (LO_COMMPAGE != -1 && LO_COMMPAGE < mmap_min_addr) {
            return false;
        }
        if (guest_loaddr != 0 && guest_loaddr < mmap_min_addr) {
            return false;
        }
    }

    memset(ga, 0, sizeof(*ga));
    n = 0;

    if (reserved_va) {
        ga->bounds[n][0] = try_identity ? mmap_min_addr : 0;
        ga->bounds[n][1] = reserved_va;
        n++;
        /* LO_COMMPAGE and NULL handled by reserving from 0. */
    } else {
        /* Add any LO_COMMPAGE or NULL page. */
        if (LO_COMMPAGE != -1) {
            ga->bounds[n][0] = 0;
            ga->bounds[n][1] = LO_COMMPAGE + TARGET_PAGE_SIZE - 1;
            n++;
        } else if (!try_identity) {
            ga->bounds[n][0] = 0;
            ga->bounds[n][1] = TARGET_PAGE_SIZE - 1;
            n++;
        }

        /* Add the guest image for ET_EXEC. */
        if (guest_loaddr) {
            ga->bounds[n][0] = guest_loaddr;
            ga->bounds[n][1] = guest_hiaddr;
            n++;
        }
    }

    /*
     * Temporarily disable
     *   "comparison is always false due to limited range of data type"
     * due to comparison between unsigned and (possible) 0.
     */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wtype-limits"

    /* Add any HI_COMMPAGE not covered by reserved_va. */
    if (reserved_va < HI_COMMPAGE) {
        ga->bounds[n][0] = HI_COMMPAGE & qemu_real_host_page_mask();
        ga->bounds[n][1] = HI_COMMPAGE + TARGET_PAGE_SIZE - 1;
        n++;
    }

#pragma GCC diagnostic pop

    ga->nbounds = n;
    return true;
}

static void pgb_fail_in_use(const char *image_name)
{
    error_report("%s: requires virtual address space that is in use "
                 "(omit the -B option or choose a different value)",
                 image_name);
    exit(EXIT_FAILURE);
}

static void pgb_fixed(const char *image_name, uintptr_t guest_loaddr,
                      uintptr_t guest_hiaddr, uintptr_t align)
{
    PGBAddrs ga;
    uintptr_t brk = (uintptr_t)sbrk(0);

    if (!QEMU_IS_ALIGNED(guest_base, align)) {
        fprintf(stderr, "Requested guest base %p does not satisfy "
                "host minimum alignment (0x%" PRIxPTR ")\n",
                (void *)guest_base, align);
        exit(EXIT_FAILURE);
    }

    if (!pgb_addr_set(&ga, guest_loaddr, guest_hiaddr, !guest_base)
        || !pgb_try_mmap_set(&ga, guest_base, brk)) {
        pgb_fail_in_use(image_name);
    }
}

/**
 * pgb_find_fallback:
 *
 * This is a fallback method for finding holes in the host address space
 * if we don't have the benefit of being able to access /proc/self/map.
 * It can potentially take a very long time as we can only dumbly iterate
 * up the host address space seeing if the allocation would work.
 */
static uintptr_t pgb_find_fallback(const PGBAddrs *ga, uintptr_t align,
                                   uintptr_t brk)
{
    /* TODO: come up with a better estimate of how much to skip. */
    uintptr_t skip = sizeof(uintptr_t) == 4 ? MiB : GiB;

    for (uintptr_t base = skip; ; base += skip) {
        base = ROUND_UP(base, align);
        if (pgb_try_mmap_set(ga, base, brk)) {
            return base;
        }
        if (base >= -skip) {
            return -1;
        }
    }
}

static uintptr_t pgb_try_itree(const PGBAddrs *ga, uintptr_t base,
                               IntervalTreeRoot *root)
{
    for (int i = ga->nbounds - 1; i >= 0; --i) {
        uintptr_t s = base + ga->bounds[i][0];
        uintptr_t l = base + ga->bounds[i][1];
        IntervalTreeNode *n;

        if (l < s) {
            /* Wraparound. Skip to advance S to mmap_min_addr. */
            return mmap_min_addr - s;
        }

        n = interval_tree_iter_first(root, s, l);
        if (n != NULL) {
            /* Conflict.  Skip to advance S to LAST + 1. */
            return n->last - s + 1;
        }
    }
    return 0;  /* success */
}

static uintptr_t pgb_find_itree(const PGBAddrs *ga, IntervalTreeRoot *root,
                                uintptr_t align, uintptr_t brk)
{
    uintptr_t last = sizeof(uintptr_t) == 4 ? MiB : GiB;
    uintptr_t base, skip;

    while (true) {
        base = ROUND_UP(last, align);
        if (base < last) {
            return -1;
        }

        skip = pgb_try_itree(ga, base, root);
        if (skip == 0) {
            break;
        }

        last = base + skip;
        if (last < base) {
            return -1;
        }
    }

    /*
     * We've chosen 'base' based on holes in the interval tree,
     * but we don't yet know if it is a valid host address.
     * Because it is the first matching hole, if the host addresses
     * are invalid we know there are no further matches.
     */
    return pgb_try_mmap_set(ga, base, brk) ? base : -1;
}

static void pgb_dynamic(const char *image_name, uintptr_t guest_loaddr,
                        uintptr_t guest_hiaddr, uintptr_t align)
{
    IntervalTreeRoot *root;
    uintptr_t brk, ret;
    PGBAddrs ga;

    /* Try the identity map first. */
    if (pgb_addr_set(&ga, guest_loaddr, guest_hiaddr, true)) {
        brk = (uintptr_t)sbrk(0);
        if (pgb_try_mmap_set(&ga, 0, brk)) {
            guest_base = 0;
            return;
        }
    }

    /*
     * Rebuild the address set for non-identity map.
     * This differs in the mapping of the guest NULL page.
     */
    pgb_addr_set(&ga, guest_loaddr, guest_hiaddr, false);

    root = read_self_maps();

    /* Read brk after we've read the maps, which will malloc. */
    brk = (uintptr_t)sbrk(0);

    if (!root) {
        ret = pgb_find_fallback(&ga, align, brk);
    } else {
        /*
         * Reserve the area close to the host brk.
         * This will be freed with the rest of the tree.
         */
        IntervalTreeNode *b = g_new0(IntervalTreeNode, 1);
        b->start = brk;
        b->last = brk + 16 * MiB - 1;
        interval_tree_insert(b, root);

        ret = pgb_find_itree(&ga, root, align, brk);
        free_self_maps(root);
    }

    if (ret == -1) {
        int w = TARGET_LONG_BITS / 4;

        error_report("%s: Unable to find a guest_base to satisfy all "
                     "guest address mapping requirements", image_name);

        for (int i = 0; i < ga.nbounds; ++i) {
            error_printf("  %0*" PRIx64 "-%0*" PRIx64 "\n",
                         w, (uint64_t)ga.bounds[i][0],
                         w, (uint64_t)ga.bounds[i][1]);
        }
        exit(EXIT_FAILURE);
    }
    guest_base = ret;
}

void probe_guest_base(const char *image_name, abi_ulong guest_loaddr,
                      abi_ulong guest_hiaddr)
{
    /* In order to use host shmat, we must be able to honor SHMLBA.  */
    uintptr_t align = MAX(SHMLBA, TARGET_PAGE_SIZE);

    /* Sanity check the guest binary. */
    if (reserved_va) {
        if (guest_hiaddr > reserved_va) {
            error_report("%s: requires more than reserved virtual "
                         "address space (0x%" PRIx64 " > 0x%lx)",
                         image_name, (uint64_t)guest_hiaddr, reserved_va);
            exit(EXIT_FAILURE);
        }
    } else {
        if (guest_hiaddr != (uintptr_t)guest_hiaddr) {
            error_report("%s: requires more virtual address space "
                         "than the host can provide (0x%" PRIx64 ")",
                         image_name, (uint64_t)guest_hiaddr + 1);
            exit(EXIT_FAILURE);
        }
    }

    if (have_guest_base) {
        pgb_fixed(image_name, guest_loaddr, guest_hiaddr, align);
    } else {
        pgb_dynamic(image_name, guest_loaddr, guest_hiaddr, align);
    }

    /* Reserve and initialize the commpage. */
    if (!init_guest_commpage()) {
        /* We have already probed for the commpage being free. */
        g_assert_not_reached();
    }

    assert(QEMU_IS_ALIGNED(guest_base, align));
    qemu_log_mask(CPU_LOG_PAGE, "Locating guest address space "
                  "@ 0x%" PRIx64 "\n", (uint64_t)guest_base);
}

enum {
    /* The string "GNU\0" as a magic number. */
    GNU0_MAGIC = const_le32('G' | 'N' << 8 | 'U' << 16),
    NOTE_DATA_SZ = 1 * KiB,
    NOTE_NAME_SZ = 4,
    ELF_GNU_PROPERTY_ALIGN = ELF_CLASS == ELFCLASS32 ? 4 : 8,
};

/*
 * Process a single gnu_property entry.
 * Return false for error.
 */
static bool parse_elf_property(const uint32_t *data, int *off, int datasz,
                               struct image_info *info, bool have_prev_type,
                               uint32_t *prev_type, Error **errp)
{
    uint32_t pr_type, pr_datasz, step;

    if (*off > datasz || !QEMU_IS_ALIGNED(*off, ELF_GNU_PROPERTY_ALIGN)) {
        goto error_data;
    }
    datasz -= *off;
    data += *off / sizeof(uint32_t);

    if (datasz < 2 * sizeof(uint32_t)) {
        goto error_data;
    }
    pr_type = data[0];
    pr_datasz = data[1];
    data += 2;
    datasz -= 2 * sizeof(uint32_t);
    step = ROUND_UP(pr_datasz, ELF_GNU_PROPERTY_ALIGN);
    if (step > datasz) {
        goto error_data;
    }

    /* Properties are supposed to be unique and sorted on pr_type. */
    if (have_prev_type && pr_type <= *prev_type) {
        if (pr_type == *prev_type) {
            error_setg(errp, "Duplicate property in PT_GNU_PROPERTY");
        } else {
            error_setg(errp, "Unsorted property in PT_GNU_PROPERTY");
        }
        return false;
    }
    *prev_type = pr_type;

    if (!arch_parse_elf_property(pr_type, pr_datasz, data, info, errp)) {
        return false;
    }

    *off += 2 * sizeof(uint32_t) + step;
    return true;

 error_data:
    error_setg(errp, "Ill-formed property in PT_GNU_PROPERTY");
    return false;
}

/* Process NT_GNU_PROPERTY_TYPE_0. */
static bool parse_elf_properties(const ImageSource *src,
                                 struct image_info *info,
                                 const struct elf_phdr *phdr,
                                 Error **errp)
{
    union {
        struct elf_note nhdr;
        uint32_t data[NOTE_DATA_SZ / sizeof(uint32_t)];
    } note;

    int n, off, datasz;
    bool have_prev_type;
    uint32_t prev_type;

    /* Unless the arch requires properties, ignore them. */
    if (!HAVE_ELF_GNU_PROPERTY) {
        return true;
    }

    /* If the properties are crazy large, that's too bad. */
    n = phdr->p_filesz;
    if (n > sizeof(note)) {
        error_setg(errp, "PT_GNU_PROPERTY too large");
        return false;
    }
    if (n < sizeof(note.nhdr)) {
        error_setg(errp, "PT_GNU_PROPERTY too small");
        return false;
    }

    if (!imgsrc_read(&note, phdr->p_offset, n, src, errp)) {
        return false;
    }

    /*
     * The contents of a valid PT_GNU_PROPERTY is a sequence of uint32_t.
     * Swap most of them now, beyond the header and namesz.
     */
    if (target_needs_bswap()) {
        for (int i = 4; i < n / 4; i++) {
            bswap32s(note.data + i);
        }
    }

    /*
     * Note that nhdr is 3 words, and that the "name" described by namesz
     * immediately follows nhdr and is thus at the 4th word.  Further, all
     * of the inputs to the kernel's round_up are multiples of 4.
     */
    if (tswap32(note.nhdr.n_type) != NT_GNU_PROPERTY_TYPE_0 ||
        tswap32(note.nhdr.n_namesz) != NOTE_NAME_SZ ||
        note.data[3] != GNU0_MAGIC) {
        error_setg(errp, "Invalid note in PT_GNU_PROPERTY");
        return false;
    }
    off = sizeof(note.nhdr) + NOTE_NAME_SZ;

    datasz = tswap32(note.nhdr.n_descsz) + off;
    if (datasz > n) {
        error_setg(errp, "Invalid note size in PT_GNU_PROPERTY");
        return false;
    }

    have_prev_type = false;
    prev_type = 0;
    while (1) {
        if (off == datasz) {
            return true;  /* end, exit ok */
        }
        if (!parse_elf_property(note.data, &off, datasz, info,
                                have_prev_type, &prev_type, errp)) {
            return false;
        }
        have_prev_type = true;
    }
}

/**
 * load_elf_image: Load an ELF image into the address space.
 * @image_name: the filename of the image, to use in error messages.
 * @src: the ImageSource from which to read.
 * @info: info collected from the loaded image.
 * @ehdr: the ELF header, not yet bswapped.
 * @pinterp_name: record any PT_INTERP string found.
 *
 * On return: @info values will be filled in, as necessary or available.
 */

static void load_elf_image(const char *image_name, const ImageSource *src,
                           struct image_info *info, struct elfhdr *ehdr,
                           char **pinterp_name)
{
    g_autofree struct elf_phdr *phdr = NULL;
    abi_ulong load_addr, load_bias, loaddr, hiaddr, error, align;
    size_t reserve_size, align_size;
    int i, prot_exec;
    Error *err = NULL;

    /*
     * First of all, some simple consistency checks.
     * Note that we rely on the bswapped ehdr staying in bprm_buf,
     * for later use by load_elf_binary and create_elf_tables.
     */
    if (!imgsrc_read(ehdr, 0, sizeof(*ehdr), src, &err)) {
        goto exit_errmsg;
    }
    if (!elf_check_ident(ehdr)) {
        error_setg(&err, "Invalid ELF image for this architecture");
        goto exit_errmsg;
    }
    bswap_ehdr(ehdr);
    if (!elf_check_ehdr(ehdr)) {
        error_setg(&err, "Invalid ELF image for this architecture");
        goto exit_errmsg;
    }

    phdr = imgsrc_read_alloc(ehdr->e_phoff,
                             ehdr->e_phnum * sizeof(struct elf_phdr),
                             src, &err);
    if (phdr == NULL) {
        goto exit_errmsg;
    }
    bswap_phdr(phdr, ehdr->e_phnum);

    info->nsegs = 0;
    info->pt_dynamic_addr = 0;

    mmap_lock();

    /*
     * Find the maximum size of the image and allocate an appropriate
     * amount of memory to handle that.  Locate the interpreter, if any.
     */
    loaddr = -1, hiaddr = 0;
    align = 0;
    info->exec_stack = EXSTACK_DEFAULT;
    for (i = 0; i < ehdr->e_phnum; ++i) {
        struct elf_phdr *eppnt = phdr + i;
        if (eppnt->p_type == PT_LOAD) {
            abi_ulong a = eppnt->p_vaddr & TARGET_PAGE_MASK;
            if (a < loaddr) {
                loaddr = a;
            }
            a = eppnt->p_vaddr + eppnt->p_memsz - 1;
            if (a > hiaddr) {
                hiaddr = a;
            }
            ++info->nsegs;
            align |= eppnt->p_align;
        } else if (eppnt->p_type == PT_INTERP && pinterp_name) {
            g_autofree char *interp_name = NULL;

            if (*pinterp_name) {
                error_setg(&err, "Multiple PT_INTERP entries");
                goto exit_errmsg;
            }

            interp_name = imgsrc_read_alloc(eppnt->p_offset, eppnt->p_filesz,
                                            src, &err);
            if (interp_name == NULL) {
                goto exit_errmsg;
            }
            if (interp_name[eppnt->p_filesz - 1] != 0) {
                error_setg(&err, "Invalid PT_INTERP entry");
                goto exit_errmsg;
            }
            *pinterp_name = g_steal_pointer(&interp_name);
        } else if (eppnt->p_type == PT_GNU_PROPERTY) {
            if (!parse_elf_properties(src, info, eppnt, &err)) {
                goto exit_errmsg;
            }
        } else if (eppnt->p_type == PT_GNU_STACK) {
            info->exec_stack = eppnt->p_flags & PF_X;
        }
    }

    load_addr = loaddr;

    align = pow2ceil(align);

    if (pinterp_name != NULL) {
        if (ehdr->e_type == ET_EXEC) {
            /*
             * Make sure that the low address does not conflict with
             * MMAP_MIN_ADDR or the QEMU application itself.
             */
            probe_guest_base(image_name, loaddr, hiaddr);
        } else {
            /*
             * The binary is dynamic, but we still need to
             * select guest_base.  In this case we pass a size.
             */
            probe_guest_base(image_name, 0, hiaddr - loaddr);

            /*
             * Avoid collision with the loader by providing a different
             * default load address.
             */
            load_addr += elf_et_dyn_base;

            /*
             * TODO: Better support for mmap alignment is desirable.
             * Since we do not have complete control over the guest
             * address space, we prefer the kernel to choose some address
             * rather than force the use of LOAD_ADDR via MAP_FIXED.
             */
            if (align) {
                load_addr &= -align;
            }
        }
    }

    /*
     * Reserve address space for all of this.
     *
     * In the case of ET_EXEC, we supply MAP_FIXED_NOREPLACE so that we get
     * exactly the address range that is required.  Without reserved_va,
     * the guest address space is not isolated.  We have attempted to avoid
     * conflict with the host program itself via probe_guest_base, but using
     * MAP_FIXED_NOREPLACE instead of MAP_FIXED provides an extra check.
     *
     * Otherwise this is ET_DYN, and we are searching for a location
     * that can hold the memory space required.  If the image is
     * pre-linked, LOAD_ADDR will be non-zero, and the kernel should
     * honor that address if it happens to be free.
     *
     * In both cases, we will overwrite pages in this range with mappings
     * from the executable.
     */
    reserve_size = (size_t)hiaddr - loaddr + 1;
    align_size = reserve_size;

    if (ehdr->e_type != ET_EXEC && align > qemu_real_host_page_size()) {
        align_size += align - 1;
    }

    load_addr = target_mmap(load_addr, align_size, PROT_NONE,
                            MAP_PRIVATE | MAP_ANON | MAP_NORESERVE |
                            (ehdr->e_type == ET_EXEC ? MAP_FIXED_NOREPLACE : 0),
                            -1, 0);
    if (load_addr == -1) {
        goto exit_mmap;
    }

    if (align_size != reserve_size) {
        abi_ulong align_addr = ROUND_UP(load_addr, align);
        abi_ulong align_end = TARGET_PAGE_ALIGN(align_addr + reserve_size);
        abi_ulong load_end = TARGET_PAGE_ALIGN(load_addr + align_size);

        if (align_addr != load_addr) {
            target_munmap(load_addr, align_addr - load_addr);
        }
        if (align_end != load_end) {
            target_munmap(align_end, load_end - align_end);
        }
        load_addr = align_addr;
    }

    load_bias = load_addr - loaddr;

    if (elf_is_fdpic(ehdr)) {
        struct elf32_fdpic_loadseg *loadsegs = info->loadsegs =
            g_malloc(sizeof(*loadsegs) * info->nsegs);

        for (i = 0; i < ehdr->e_phnum; ++i) {
            switch (phdr[i].p_type) {
            case PT_DYNAMIC:
                info->pt_dynamic_addr = phdr[i].p_vaddr + load_bias;
                break;
            case PT_LOAD:
                loadsegs->addr = phdr[i].p_vaddr + load_bias;
                loadsegs->p_vaddr = phdr[i].p_vaddr;
                loadsegs->p_memsz = phdr[i].p_memsz;
                ++loadsegs;
                break;
            }
        }
    }

    info->load_bias = load_bias;
    info->code_offset = load_bias;
    info->data_offset = load_bias;
    info->load_addr = load_addr;
    info->entry = ehdr->e_entry + load_bias;
    info->start_code = -1;
    info->end_code = 0;
    info->start_data = -1;
    info->end_data = 0;
    /* Usual start for brk is after all sections of the main executable. */
    info->brk = TARGET_PAGE_ALIGN(hiaddr + load_bias);
    info->elf_flags = ehdr->e_flags;

    prot_exec = PROT_EXEC;
#ifdef TARGET_AARCH64
    /*
     * If the BTI feature is present, this indicates that the executable
     * pages of the startup binary should be mapped with PROT_BTI, so that
     * branch targets are enforced.
     *
     * The startup binary is either the interpreter or the static executable.
     * The interpreter is responsible for all pages of a dynamic executable.
     *
     * Elf notes are backward compatible to older cpus.
     * Do not enable BTI unless it is supported.
     */
    if ((info->note_flags & GNU_PROPERTY_AARCH64_FEATURE_1_BTI)
        && (pinterp_name == NULL || *pinterp_name == 0)
        && cpu_isar_feature(aa64_bti, ARM_CPU(thread_cpu))) {
        prot_exec |= TARGET_PROT_BTI;
    }
#endif

    for (i = 0; i < ehdr->e_phnum; i++) {
        struct elf_phdr *eppnt = phdr + i;
        if (eppnt->p_type == PT_LOAD) {
            abi_ulong vaddr, vaddr_po, vaddr_ps, vaddr_ef, vaddr_em;
            int elf_prot = 0;

            if (eppnt->p_flags & PF_R) {
                elf_prot |= PROT_READ;
            }
            if (eppnt->p_flags & PF_W) {
                elf_prot |= PROT_WRITE;
            }
            if (eppnt->p_flags & PF_X) {
                elf_prot |= prot_exec;
            }

            vaddr = load_bias + eppnt->p_vaddr;
            vaddr_po = vaddr & ~TARGET_PAGE_MASK;
            vaddr_ps = vaddr & TARGET_PAGE_MASK;

            vaddr_ef = vaddr + eppnt->p_filesz;
            vaddr_em = vaddr + eppnt->p_memsz;

            /*
             * Some segments may be completely empty, with a non-zero p_memsz
             * but no backing file segment.
             */
            if (eppnt->p_filesz != 0) {
                error = imgsrc_mmap(vaddr_ps, eppnt->p_filesz + vaddr_po,
                                    elf_prot, MAP_PRIVATE | MAP_FIXED,
                                    src, eppnt->p_offset - vaddr_po);
                if (error == -1) {
                    goto exit_mmap;
                }
            }

            /* If the load segment requests extra zeros (e.g. bss), map it. */
            if (vaddr_ef < vaddr_em &&
                !zero_bss(vaddr_ef, vaddr_em, elf_prot, &err)) {
                goto exit_errmsg;
            }

            /* Find the full program boundaries.  */
            if (elf_prot & PROT_EXEC) {
                if (vaddr < info->start_code) {
                    info->start_code = vaddr;
                }
                if (vaddr_ef > info->end_code) {
                    info->end_code = vaddr_ef;
                }
            }
            if (elf_prot & PROT_WRITE) {
                if (vaddr < info->start_data) {
                    info->start_data = vaddr;
                }
                if (vaddr_ef > info->end_data) {
                    info->end_data = vaddr_ef;
                }
            }
#ifdef TARGET_MIPS
        } else if (eppnt->p_type == PT_MIPS_ABIFLAGS) {
            Mips_elf_abiflags_v0 abiflags;

            if (!imgsrc_read(&abiflags, eppnt->p_offset, sizeof(abiflags),
                             src, &err)) {
                goto exit_errmsg;
            }
            bswap_mips_abiflags(&abiflags);
            info->fp_abi = abiflags.fp_abi;
#endif
        }
    }

    if (info->end_data == 0) {
        info->start_data = info->end_code;
        info->end_data = info->end_code;
    }

    if (qemu_log_enabled()) {
        load_symbols(ehdr, src, load_bias);
    }

    debuginfo_report_elf(image_name, src->fd, load_bias);

    mmap_unlock();

    close(src->fd);
    return;

 exit_mmap:
    error_setg_errno(&err, errno, "Error mapping file");
    goto exit_errmsg;
 exit_errmsg:
    error_reportf_err(err, "%s: ", image_name);
    exit(-1);
}

static void load_elf_interp(const char *filename, struct image_info *info,
                            char bprm_buf[BPRM_BUF_SIZE])
{
    struct elfhdr ehdr;
    ImageSource src;
    int fd, retval;
    Error *err = NULL;

    fd = open(path(filename), O_RDONLY);
    if (fd < 0) {
        error_setg_file_open(&err, errno, filename);
        error_report_err(err);
        exit(-1);
    }

    retval = read(fd, bprm_buf, BPRM_BUF_SIZE);
    if (retval < 0) {
        error_setg_errno(&err, errno, "Error reading file header");
        error_reportf_err(err, "%s: ", filename);
        exit(-1);
    }

    src.fd = fd;
    src.cache = bprm_buf;
    src.cache_size = retval;

    load_elf_image(filename, &src, info, &ehdr, NULL);
}

#ifndef HAVE_VDSO_IMAGE_INFO
const VdsoImageInfo *get_vdso_image_info(uint32_t elf_flags)
{
#ifdef VDSO_HEADER
#include VDSO_HEADER
    return &vdso_image_info;
#else
    return NULL;
#endif
}
#endif /* HAVE_VDSO_IMAGE_INFO */

static void load_elf_vdso(struct image_info *info, const VdsoImageInfo *vdso)
{
    ImageSource src;
    struct elfhdr ehdr;
    abi_ulong load_bias, load_addr;

    src.fd = -1;
    src.cache = vdso->image;
    src.cache_size = vdso->image_size;

    load_elf_image("<internal-vdso>", &src, info, &ehdr, NULL);
    load_addr = info->load_addr;
    load_bias = info->load_bias;

    /*
     * We need to relocate the VDSO image.  The one built into the kernel
     * is built for a fixed address.  The one built for QEMU is not, since
     * that requires close control of the guest address space.
     * We pre-processed the image to locate all of the addresses that need
     * to be updated.
     */
    for (unsigned i = 0, n = vdso->reloc_count; i < n; i++) {
        abi_ulong *addr = g2h_untagged(load_addr + vdso->relocs[i]);
        *addr = tswapal(tswapal(*addr) + load_bias);
    }

    /* Install signal trampolines, if present. */
    if (vdso->sigreturn_ofs) {
        default_sigreturn = load_addr + vdso->sigreturn_ofs;
    }
    if (vdso->rt_sigreturn_ofs) {
        default_rt_sigreturn = load_addr + vdso->rt_sigreturn_ofs;
    }
    if (vdso->sigreturn_region_start_ofs) {
        vdso_sigreturn_region_start =
            load_addr + vdso->sigreturn_region_start_ofs;
        vdso_sigreturn_region_end = load_addr + vdso->sigreturn_region_end_ofs;
    }

    /* Remove write from VDSO segment. */
    target_mprotect(info->start_data, info->end_data - info->start_data,
                    PROT_READ | PROT_EXEC);
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

    // binary search
    struct elf_sym *sym;

    sym = bsearch(&orig_addr, syms, s->disas_num_syms, sizeof(*syms), symfind);
    if (sym != NULL) {
        return s->disas_strtab + sym->st_name;
    }

    return "";
}

/* FIXME: This should use elf_ops.h.inc  */
static int symcmp(const void *s0, const void *s1)
{
    struct elf_sym *sym0 = (struct elf_sym *)s0;
    struct elf_sym *sym1 = (struct elf_sym *)s1;
    return (sym0->st_value < sym1->st_value)
        ? -1
        : ((sym0->st_value > sym1->st_value) ? 1 : 0);
}

/* Best attempt to load symbols from this ELF object. */
static void load_symbols(struct elfhdr *hdr, const ImageSource *src,
                         abi_ulong load_bias)
{
    int i, shnum, nsyms, sym_idx = 0, str_idx = 0;
    g_autofree struct elf_shdr *shdr = NULL;
    char *strings = NULL;
    struct elf_sym *syms = NULL;
    struct elf_sym *new_syms;
    uint64_t segsz;

    shnum = hdr->e_shnum;
    shdr = imgsrc_read_alloc(hdr->e_shoff, shnum * sizeof(struct elf_shdr),
                             src, NULL);
    if (shdr == NULL) {
        return;
    }

    bswap_shdr(shdr, shnum);
    for (i = 0; i < shnum; ++i) {
        if (shdr[i].sh_type == SHT_SYMTAB) {
            sym_idx = i;
            str_idx = shdr[i].sh_link;
            goto found;
        }
    }

    /* There will be no symbol table if the file was stripped.  */
    return;

 found:
    /* Now know where the strtab and symtab are.  Snarf them.  */

    segsz = shdr[str_idx].sh_size;
    strings = g_try_malloc(segsz);
    if (!strings) {
        goto give_up;
    }
    if (!imgsrc_read(strings, shdr[str_idx].sh_offset, segsz, src, NULL)) {
        goto give_up;
    }

    segsz = shdr[sym_idx].sh_size;
    if (segsz / sizeof(struct elf_sym) > INT_MAX) {
        /*
         * Implausibly large symbol table: give up rather than ploughing
         * on with the number of symbols calculation overflowing.
         */
        goto give_up;
    }
    nsyms = segsz / sizeof(struct elf_sym);
    syms = g_try_malloc(segsz);
    if (!syms) {
        goto give_up;
    }
    if (!imgsrc_read(syms, shdr[sym_idx].sh_offset, segsz, src, NULL)) {
        goto give_up;
    }

    for (i = 0; i < nsyms; ) {
        bswap_sym(syms + i);
        /* Throw away entries which we do not need.  */
        if (syms[i].st_shndx == SHN_UNDEF
            || syms[i].st_shndx >= SHN_LORESERVE
            || ELF_ST_TYPE(syms[i].st_info) != STT_FUNC) {
            if (i < --nsyms) {
                syms[i] = syms[nsyms];
            }
        } else {
#if defined(TARGET_ARM) || defined (TARGET_MIPS)
            /* The bottom address bit marks a Thumb or MIPS16 symbol.  */
            syms[i].st_value &= ~(target_ulong)1;
#endif
            syms[i].st_value += load_bias;
            i++;
        }
    }

    /* No "useful" symbol.  */
    if (nsyms == 0) {
        goto give_up;
    }

    /*
     * Attempt to free the storage associated with the local symbols
     * that we threw away.  Whether or not this has any effect on the
     * memory allocation depends on the malloc implementation and how
     * many symbols we managed to discard.
     */
    new_syms = g_try_renew(struct elf_sym, syms, nsyms);
    if (new_syms == NULL) {
        goto give_up;
    }
    syms = new_syms;

    qsort(syms, nsyms, sizeof(*syms), symcmp);

    {
        struct syminfo *s = g_new(struct syminfo, 1);

        s->disas_strtab = strings;
        s->disas_num_syms = nsyms;
#if ELF_CLASS == ELFCLASS32
        s->disas_symtab.elf32 = syms;
#else
        s->disas_symtab.elf64 = syms;
#endif
        s->lookup_symbol = lookup_symbolxx;
        s->next = syminfos;
        syminfos = s;
    }
    return;

 give_up:
    g_free(strings);
    g_free(syms);
}

uint32_t get_elf_eflags(int fd)
{
    struct elfhdr ehdr;
    off_t offset;
    int ret;

    /* Read ELF header */
    offset = lseek(fd, 0, SEEK_SET);
    if (offset == (off_t) -1) {
        return 0;
    }
    ret = read(fd, &ehdr, sizeof(ehdr));
    if (ret < sizeof(ehdr)) {
        return 0;
    }
    offset = lseek(fd, offset, SEEK_SET);
    if (offset == (off_t) -1) {
        return 0;
    }

    /* Check ELF signature */
    if (!elf_check_ident(&ehdr)) {
        return 0;
    }

    /* check header */
    bswap_ehdr(&ehdr);
    if (!elf_check_ehdr(&ehdr)) {
        return 0;
    }

    /* return architecture id */
    return ehdr.e_flags;
}

int load_elf_binary(struct linux_binprm *bprm, struct image_info *info)
{
    /*
     * We need a copy of the elf header for passing to create_elf_tables.
     * We will have overwritten the original when we re-use bprm->buf
     * while loading the interpreter.  Allocate the storage for this now
     * and let elf_load_image do any swapping that may be required.
     */
    struct elfhdr ehdr;
    struct image_info interp_info, vdso_info;
    char *elf_interpreter = NULL;
    char *scratch;

    memset(&interp_info, 0, sizeof(interp_info));
#ifdef TARGET_MIPS
    interp_info.fp_abi = MIPS_ABI_FP_UNKNOWN;
#endif

    load_elf_image(bprm->filename, &bprm->src, info, &ehdr, &elf_interpreter);

    /* Do this so that we can load the interpreter, if need be.  We will
       change some of these later */
    bprm->p = setup_arg_pages(bprm, info);

    scratch = g_new0(char, TARGET_PAGE_SIZE);
    if (STACK_GROWS_DOWN) {
        bprm->p = copy_elf_strings(1, &bprm->filename, scratch,
                                   bprm->p, info->stack_limit);
        info->file_string = bprm->p;
        bprm->p = copy_elf_strings(bprm->envc, bprm->envp, scratch,
                                   bprm->p, info->stack_limit);
        info->env_strings = bprm->p;
        bprm->p = copy_elf_strings(bprm->argc, bprm->argv, scratch,
                                   bprm->p, info->stack_limit);
        info->arg_strings = bprm->p;
    } else {
        info->arg_strings = bprm->p;
        bprm->p = copy_elf_strings(bprm->argc, bprm->argv, scratch,
                                   bprm->p, info->stack_limit);
        info->env_strings = bprm->p;
        bprm->p = copy_elf_strings(bprm->envc, bprm->envp, scratch,
                                   bprm->p, info->stack_limit);
        info->file_string = bprm->p;
        bprm->p = copy_elf_strings(1, &bprm->filename, scratch,
                                   bprm->p, info->stack_limit);
    }

    g_free(scratch);

    if (!bprm->p) {
        fprintf(stderr, "%s: %s\n", bprm->filename, strerror(E2BIG));
        exit(-1);
    }

    if (elf_interpreter) {
        load_elf_interp(elf_interpreter, &interp_info, bprm->buf);

        /*
         * While unusual because of ELF_ET_DYN_BASE, if we are unlucky
         * with the mappings the interpreter can be loaded above but
         * near the main executable, which can leave very little room
         * for the heap.
         * If the current brk has less than 16MB, use the end of the
         * interpreter.
         */
        if (interp_info.brk > info->brk &&
            interp_info.load_bias - info->brk < 16 * MiB)  {
            info->brk = interp_info.brk;
        }

        /* If the program interpreter is one of these two, then assume
           an iBCS2 image.  Otherwise assume a native linux image.  */

        if (strcmp(elf_interpreter, "/usr/lib/libc.so.1") == 0
            || strcmp(elf_interpreter, "/usr/lib/ld.so.1") == 0) {
            info->personality = PER_SVR4;

            /* Why this, you ask???  Well SVr4 maps page 0 as read-only,
               and some applications "depend" upon this behavior.  Since
               we do not have the power to recompile these, we emulate
               the SVr4 behavior.  Sigh.  */
            target_mmap(0, TARGET_PAGE_SIZE, PROT_READ | PROT_EXEC,
                        MAP_FIXED_NOREPLACE | MAP_PRIVATE | MAP_ANONYMOUS,
                        -1, 0);
        }
#ifdef TARGET_MIPS
        info->interp_fp_abi = interp_info.fp_abi;
#endif
    }

    /*
     * Load a vdso if available, which will amongst other things contain the
     * signal trampolines.  Otherwise, allocate a separate page for them.
     */
    const VdsoImageInfo *vdso = get_vdso_image_info(info->elf_flags);
    if (vdso) {
        load_elf_vdso(&vdso_info, vdso);
        info->vdso = vdso_info.load_bias;
    } else if (TARGET_ARCH_HAS_SIGTRAMP_PAGE) {
        abi_long tramp_page = target_mmap(0, TARGET_PAGE_SIZE,
                                          PROT_READ | PROT_WRITE,
                                          MAP_PRIVATE | MAP_ANON, -1, 0);
        if (tramp_page == -1) {
            return -errno;
        }

        setup_sigtramp(tramp_page);
        target_mprotect(tramp_page, TARGET_PAGE_SIZE, PROT_READ | PROT_EXEC);
        vdso_sigreturn_region_start = tramp_page;
        vdso_sigreturn_region_end = tramp_page + TARGET_PAGE_SIZE;
    }

    bprm->p = create_elf_tables(bprm->p, bprm->argc, bprm->envc, &ehdr, info,
                                elf_interpreter ? &interp_info : NULL,
                                vdso ? &vdso_info : NULL);
    info->start_stack = bprm->p;

    /* If we have an interpreter, set that as the program's entry point.
       Copy the load_bias as well, to help PPC64 interpret the entry
       point as a function descriptor.  Do this after creating elf tables
       so that we copy the original program entry point into the AUXV.  */
    if (elf_interpreter) {
        info->load_bias = interp_info.load_bias;
        info->entry = interp_info.entry;
        g_free(elf_interpreter);
    }

#ifdef HAVE_ELF_CORE_DUMP
    bprm->core_dump = &elf_core_dump;
#endif

    return 0;
}

#ifdef HAVE_ELF_CORE_DUMP

/*
 * Definitions to generate Intel SVR4-like core files.
 * These mostly have the same names as the SVR4 types with "target_elf_"
 * tacked on the front to prevent clashes with linux definitions,
 * and the typedef forms have been avoided.  This is mostly like
 * the SVR4 structure, but more Linuxy, with things that Linux does
 * not support and which gdb doesn't really use excluded.
 *
 * Fields we don't dump (their contents is zero) in linux-user qemu
 * are marked with XXX.
 *
 * Core dump code is copied from linux kernel (fs/binfmt_elf.c).
 *
 * Porting ELF coredump for target is (quite) simple process.  First you
 * define HAVE_ELF_CORE_DUMP in target ELF code (where init_thread() for
 * the target resides):
 *
 * #define HAVE_ELF_CORE_DUMP
 *
 * Next you define type of register set used for dumping:
 * typedef struct target_elf_gregset_t { ... } target_elf_gregset_t;
 *
 * Last step is to implement target specific function that copies registers
 * from given cpu into just specified register set.  Prototype is:
 *
 * void elf_core_copy_regs(target_elf_gregset_t *regs, const CPUArchState *env);
 *
 * Parameters:
 *     regs - copy register values into here (allocated and zeroed by caller)
 *     env - copy registers from here
 *
 * Example for ARM target is provided in this file.
 */

struct target_elf_siginfo {
    abi_int    si_signo; /* signal number */
    abi_int    si_code;  /* extra code */
    abi_int    si_errno; /* errno */
};

struct target_elf_prstatus {
    struct target_elf_siginfo pr_info;      /* Info associated with signal */
    abi_short          pr_cursig;    /* Current signal */
    abi_ulong          pr_sigpend;   /* XXX */
    abi_ulong          pr_sighold;   /* XXX */
    target_pid_t       pr_pid;
    target_pid_t       pr_ppid;
    target_pid_t       pr_pgrp;
    target_pid_t       pr_sid;
    struct target_timeval pr_utime;  /* XXX User time */
    struct target_timeval pr_stime;  /* XXX System time */
    struct target_timeval pr_cutime; /* XXX Cumulative user time */
    struct target_timeval pr_cstime; /* XXX Cumulative system time */
    target_elf_gregset_t      pr_reg;       /* GP registers */
    abi_int            pr_fpvalid;   /* XXX */
};

#define ELF_PRARGSZ     (80) /* Number of chars for args */

struct target_elf_prpsinfo {
    char         pr_state;       /* numeric process state */
    char         pr_sname;       /* char for pr_state */
    char         pr_zomb;        /* zombie */
    char         pr_nice;        /* nice val */
    abi_ulong    pr_flag;        /* flags */
    target_uid_t pr_uid;
    target_gid_t pr_gid;
    target_pid_t pr_pid, pr_ppid, pr_pgrp, pr_sid;
    /* Lots missing */
    char    pr_fname[16] QEMU_NONSTRING; /* filename of executable */
    char    pr_psargs[ELF_PRARGSZ]; /* initial part of arg list */
};

static void bswap_prstatus(struct target_elf_prstatus *prstatus)
{
    if (!target_needs_bswap()) {
        return;
    }

    prstatus->pr_info.si_signo = tswap32(prstatus->pr_info.si_signo);
    prstatus->pr_info.si_code = tswap32(prstatus->pr_info.si_code);
    prstatus->pr_info.si_errno = tswap32(prstatus->pr_info.si_errno);
    prstatus->pr_cursig = tswap16(prstatus->pr_cursig);
    prstatus->pr_sigpend = tswapal(prstatus->pr_sigpend);
    prstatus->pr_sighold = tswapal(prstatus->pr_sighold);
    prstatus->pr_pid = tswap32(prstatus->pr_pid);
    prstatus->pr_ppid = tswap32(prstatus->pr_ppid);
    prstatus->pr_pgrp = tswap32(prstatus->pr_pgrp);
    prstatus->pr_sid = tswap32(prstatus->pr_sid);
    /* cpu times are not filled, so we skip them */
    /* regs should be in correct format already */
    prstatus->pr_fpvalid = tswap32(prstatus->pr_fpvalid);
}

static void bswap_psinfo(struct target_elf_prpsinfo *psinfo)
{
    if (!target_needs_bswap()) {
        return;
    }

    psinfo->pr_flag = tswapal(psinfo->pr_flag);
    psinfo->pr_uid = tswap16(psinfo->pr_uid);
    psinfo->pr_gid = tswap16(psinfo->pr_gid);
    psinfo->pr_pid = tswap32(psinfo->pr_pid);
    psinfo->pr_ppid = tswap32(psinfo->pr_ppid);
    psinfo->pr_pgrp = tswap32(psinfo->pr_pgrp);
    psinfo->pr_sid = tswap32(psinfo->pr_sid);
}

static void bswap_note(struct elf_note *en)
{
    if (!target_needs_bswap()) {
        return;
    }

    bswap32s(&en->n_namesz);
    bswap32s(&en->n_descsz);
    bswap32s(&en->n_type);
}

/*
 * Calculate file (dump) size of given memory region.
 */
static size_t vma_dump_size(vaddr start, vaddr end, int flags)
{
    /* The area must be readable and dumpable. */
    if (!(flags & PAGE_READ) || (flags & PAGE_DONTDUMP)) {
        return 0;
    }

    /*
     * Usually we don't dump executable pages as they contain
     * non-writable code that debugger can read directly from
     * target library etc. If there is no elf header, we dump it.
     */
    if (!(flags & PAGE_WRITE_ORG) &&
        (flags & PAGE_EXEC) &&
        memcmp(g2h_untagged(start), ELFMAG, SELFMAG) == 0) {
        return 0;
    }

    return end - start;
}

static size_t size_note(const char *name, size_t datasz)
{
    size_t namesz = strlen(name) + 1;

    namesz = ROUND_UP(namesz, 4);
    datasz = ROUND_UP(datasz, 4);

    return sizeof(struct elf_note) + namesz + datasz;
}

static void *fill_note(void **pptr, int type, const char *name, size_t datasz)
{
    void *ptr = *pptr;
    struct elf_note *n = ptr;
    size_t namesz = strlen(name) + 1;

    n->n_namesz = namesz;
    n->n_descsz = datasz;
    n->n_type = type;
    bswap_note(n);

    ptr += sizeof(*n);
    memcpy(ptr, name, namesz);

    namesz = ROUND_UP(namesz, 4);
    datasz = ROUND_UP(datasz, 4);

    *pptr = ptr + namesz + datasz;
    return ptr + namesz;
}

static void fill_elf_header(struct elfhdr *elf, int segs, uint16_t machine,
                            uint32_t flags)
{
    memcpy(elf->e_ident, ELFMAG, SELFMAG);

    elf->e_ident[EI_CLASS] = ELF_CLASS;
    elf->e_ident[EI_DATA] = ELF_DATA;
    elf->e_ident[EI_VERSION] = EV_CURRENT;
    elf->e_ident[EI_OSABI] = ELF_OSABI;

    elf->e_type = ET_CORE;
    elf->e_machine = machine;
    elf->e_version = EV_CURRENT;
    elf->e_phoff = sizeof(struct elfhdr);
    elf->e_flags = flags;
    elf->e_ehsize = sizeof(struct elfhdr);
    elf->e_phentsize = sizeof(struct elf_phdr);
    elf->e_phnum = segs;

    bswap_ehdr(elf);
}

static void fill_elf_note_phdr(struct elf_phdr *phdr, size_t sz, off_t offset)
{
    phdr->p_type = PT_NOTE;
    phdr->p_offset = offset;
    phdr->p_filesz = sz;

    bswap_phdr(phdr, 1);
}

static void fill_prstatus_note(void *data, CPUState *cpu, int signr)
{
    /*
     * Because note memory is only aligned to 4, and target_elf_prstatus
     * may well have higher alignment requirements, fill locally and
     * memcpy to the destination afterward.
     */
    struct target_elf_prstatus prstatus = {
        .pr_info.si_signo = signr,
        .pr_cursig = signr,
        .pr_pid = get_task_state(cpu)->ts_tid,
        .pr_ppid = getppid(),
        .pr_pgrp = getpgrp(),
        .pr_sid = getsid(0),
    };

    elf_core_copy_regs(&prstatus.pr_reg, cpu_env(cpu));
    bswap_prstatus(&prstatus);
    memcpy(data, &prstatus, sizeof(prstatus));
}

static void fill_prpsinfo_note(void *data, const TaskState *ts)
{
    /*
     * Because note memory is only aligned to 4, and target_elf_prpsinfo
     * may well have higher alignment requirements, fill locally and
     * memcpy to the destination afterward.
     */
    struct target_elf_prpsinfo psinfo = {
        .pr_pid = getpid(),
        .pr_ppid = getppid(),
        .pr_pgrp = getpgrp(),
        .pr_sid = getsid(0),
        .pr_uid = getuid(),
        .pr_gid = getgid(),
    };
    char *base_filename;
    size_t len;

    len = ts->info->env_strings - ts->info->arg_strings;
    len = MIN(len, ELF_PRARGSZ);
    memcpy(&psinfo.pr_psargs, g2h_untagged(ts->info->arg_strings), len);
    for (size_t i = 0; i < len; i++) {
        if (psinfo.pr_psargs[i] == 0) {
            psinfo.pr_psargs[i] = ' ';
        }
    }

    base_filename = g_path_get_basename(ts->bprm->filename);
    /*
     * Using strncpy here is fine: at max-length,
     * this field is not NUL-terminated.
     */
    strncpy(psinfo.pr_fname, base_filename, sizeof(psinfo.pr_fname));
    g_free(base_filename);

    bswap_psinfo(&psinfo);
    memcpy(data, &psinfo, sizeof(psinfo));
}

static void fill_auxv_note(void *data, const TaskState *ts)
{
    memcpy(data, g2h_untagged(ts->info->saved_auxv), ts->info->auxv_len);
}

/*
 * Constructs name of coredump file.  We have following convention
 * for the name:
 *     qemu_<basename-of-target-binary>_<date>-<time>_<pid>.core
 *
 * Returns the filename
 */
static char *core_dump_filename(const TaskState *ts)
{
    g_autoptr(GDateTime) now = g_date_time_new_now_local();
    g_autofree char *nowstr = g_date_time_format(now, "%Y%m%d-%H%M%S");
    g_autofree char *base_filename = g_path_get_basename(ts->bprm->filename);

    return g_strdup_printf("qemu_%s_%s_%d.core",
                           base_filename, nowstr, (int)getpid());
}

static int dump_write(int fd, const void *ptr, size_t size)
{
    const char *bufp = (const char *)ptr;
    ssize_t bytes_written, bytes_left;

    bytes_written = 0;
    bytes_left = size;

    /*
     * In normal conditions, single write(2) should do but
     * in case of socket etc. this mechanism is more portable.
     */
    do {
        bytes_written = write(fd, bufp, bytes_left);
        if (bytes_written < 0) {
            if (errno == EINTR)
                continue;
            return (-1);
        } else if (bytes_written == 0) { /* eof */
            return (-1);
        }
        bufp += bytes_written;
        bytes_left -= bytes_written;
    } while (bytes_left > 0);

    return (0);
}

static int wmr_page_unprotect_regions(void *opaque, vaddr start,
                                      vaddr end, int flags)
{
    if ((flags & (PAGE_WRITE | PAGE_WRITE_ORG)) == PAGE_WRITE_ORG) {
        size_t step = MAX(TARGET_PAGE_SIZE, qemu_real_host_page_size());

        while (1) {
            page_unprotect(NULL, start, 0);
            if (end - start <= step) {
                break;
            }
            start += step;
        }
    }
    return 0;
}

typedef struct {
    unsigned count;
    size_t size;
} CountAndSizeRegions;

static int wmr_count_and_size_regions(void *opaque, vaddr start,
                                      vaddr end, int flags)
{
    CountAndSizeRegions *css = opaque;

    css->count++;
    css->size += vma_dump_size(start, end, flags);
    return 0;
}

typedef struct {
    struct elf_phdr *phdr;
    off_t offset;
} FillRegionPhdr;

static int wmr_fill_region_phdr(void *opaque, vaddr start,
                                vaddr end, int flags)
{
    FillRegionPhdr *d = opaque;
    struct elf_phdr *phdr = d->phdr;

    phdr->p_type = PT_LOAD;
    phdr->p_vaddr = start;
    phdr->p_paddr = 0;
    phdr->p_filesz = vma_dump_size(start, end, flags);
    phdr->p_offset = d->offset;
    d->offset += phdr->p_filesz;
    phdr->p_memsz = end - start;
    phdr->p_flags = (flags & PAGE_READ ? PF_R : 0)
                  | (flags & PAGE_WRITE_ORG ? PF_W : 0)
                  | (flags & PAGE_EXEC ? PF_X : 0);
    phdr->p_align = TARGET_PAGE_SIZE;

    bswap_phdr(phdr, 1);
    d->phdr = phdr + 1;
    return 0;
}

static int wmr_write_region(void *opaque, vaddr start,
                            vaddr end, int flags)
{
    int fd = *(int *)opaque;
    size_t size = vma_dump_size(start, end, flags);

    if (!size) {
        return 0;
    }
    return dump_write(fd, g2h_untagged(start), size);
}

/*
 * Write out ELF coredump.
 *
 * See documentation of ELF object file format in:
 * http://www.caldera.com/developers/devspecs/gabi41.pdf
 *
 * Coredump format in linux is following:
 *
 * 0   +----------------------+         \
 *     | ELF header           | ET_CORE  |
 *     +----------------------+          |
 *     | ELF program headers  |          |--- headers
 *     | - NOTE section       |          |
 *     | - PT_LOAD sections   |          |
 *     +----------------------+         /
 *     | NOTEs:               |
 *     | - NT_PRSTATUS        |
 *     | - NT_PRSINFO         |
 *     | - NT_AUXV            |
 *     +----------------------+ <-- aligned to target page
 *     | Process memory dump  |
 *     :                      :
 *     .                      .
 *     :                      :
 *     |                      |
 *     +----------------------+
 *
 * NT_PRSTATUS -> struct elf_prstatus (per thread)
 * NT_PRSINFO  -> struct elf_prpsinfo
 * NT_AUXV is array of { type, value } pairs (see fill_auxv_note()).
 *
 * Format follows System V format as close as possible.  Current
 * version limitations are as follows:
 *     - no floating point registers are dumped
 *
 * Function returns 0 in case of success, negative errno otherwise.
 *
 * TODO: make this work also during runtime: it should be
 * possible to force coredump from running process and then
 * continue processing.  For example qemu could set up SIGUSR2
 * handler (provided that target process haven't registered
 * handler for that) that does the dump when signal is received.
 */
static int elf_core_dump(int signr, const CPUArchState *env)
{
    const CPUState *cpu = env_cpu_const(env);
    const TaskState *ts = (const TaskState *)get_task_state((CPUState *)cpu);
    struct rlimit dumpsize;
    CountAndSizeRegions css;
    off_t offset, note_offset, data_offset;
    size_t note_size;
    int cpus, ret;
    int fd = -1;
    CPUState *cpu_iter;

    if (prctl(PR_GET_DUMPABLE) == 0) {
        return 0;
    }

    if (getrlimit(RLIMIT_CORE, &dumpsize) < 0 || dumpsize.rlim_cur == 0) {
        return 0;
    }

    cpu_list_lock();
    mmap_lock();

    /* By unprotecting, we merge vmas that might be split. */
    walk_memory_regions(NULL, wmr_page_unprotect_regions);

    /*
     * Walk through target process memory mappings and
     * set up structure containing this information.
     */
    memset(&css, 0, sizeof(css));
    walk_memory_regions(&css, wmr_count_and_size_regions);

    cpus = 0;
    CPU_FOREACH(cpu_iter) {
        cpus++;
    }

    offset = sizeof(struct elfhdr);
    offset += (css.count + 1) * sizeof(struct elf_phdr);
    note_offset = offset;

    offset += size_note("CORE", ts->info->auxv_len);
    offset += size_note("CORE", sizeof(struct target_elf_prpsinfo));
    offset += size_note("CORE", sizeof(struct target_elf_prstatus)) * cpus;
    note_size = offset - note_offset;
    data_offset = TARGET_PAGE_ALIGN(offset);

    /* Do not dump if the corefile size exceeds the limit. */
    if (dumpsize.rlim_cur != RLIM_INFINITY
        && dumpsize.rlim_cur < data_offset + css.size) {
        errno = 0;
        goto out;
    }

    {
        g_autofree char *corefile = core_dump_filename(ts);
        fd = open(corefile, O_WRONLY | O_CREAT | O_TRUNC,
                  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    }
    if (fd < 0) {
        goto out;
    }

    /*
     * There is a fair amount of alignment padding within the notes
     * as well as preceeding the process memory.  Allocate a zeroed
     * block to hold it all.  Write all of the headers directly into
     * this buffer and then write it out as a block.
     */
    {
        g_autofree void *header = g_malloc0(data_offset);
        FillRegionPhdr frp;
        void *hptr, *dptr;

        /* Create elf file header. */
        hptr = header;
        fill_elf_header(hptr, css.count + 1, ELF_MACHINE, 0);
        hptr += sizeof(struct elfhdr);

        /* Create elf program headers. */
        fill_elf_note_phdr(hptr, note_size, note_offset);
        hptr += sizeof(struct elf_phdr);

        frp.phdr = hptr;
        frp.offset = data_offset;
        walk_memory_regions(&frp, wmr_fill_region_phdr);
        hptr = frp.phdr;

        /* Create the notes. */
        dptr = fill_note(&hptr, NT_AUXV, "CORE", ts->info->auxv_len);
        fill_auxv_note(dptr, ts);

        dptr = fill_note(&hptr, NT_PRPSINFO, "CORE",
                         sizeof(struct target_elf_prpsinfo));
        fill_prpsinfo_note(dptr, ts);

        CPU_FOREACH(cpu_iter) {
            dptr = fill_note(&hptr, NT_PRSTATUS, "CORE",
                             sizeof(struct target_elf_prstatus));
            fill_prstatus_note(dptr, cpu_iter, cpu_iter == cpu ? signr : 0);
        }

        if (dump_write(fd, header, data_offset) < 0) {
            goto out;
        }
    }

    /*
     * Finally write process memory into the corefile as well.
     */
    if (walk_memory_regions(&fd, wmr_write_region) < 0) {
        goto out;
    }
    errno = 0;

 out:
    ret = -errno;
    mmap_unlock();
    cpu_list_unlock();
    if (fd >= 0) {
        close(fd);
    }
    return ret;
}
#endif /* HAVE_ELF_CORE_DUMP */
