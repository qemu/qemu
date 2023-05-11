/* This is the Linux kernel elf-loading code, ported into user space */
#include "qemu/osdep.h"
#include <sys/param.h>

#include <sys/resource.h>
#include <sys/shm.h>

#include "qemu.h"
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
#include "target_signal.h"
#include "accel/tcg/debuginfo.h"

#ifdef _ARCH_PPC64
#undef ARCH_DLINFO
#undef ELF_PLATFORM
#undef ELF_HWCAP
#undef ELF_HWCAP2
#undef ELF_CLASS
#undef ELF_DATA
#undef ELF_ARCH
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

/* this flag is uneffective under linux too, should be deleted */
#ifndef MAP_DENYWRITE
#define MAP_DENYWRITE 0
#endif

/* should probably go in elf.h */
#ifndef ELIBBAD
#define ELIBBAD 80
#endif

#if TARGET_BIG_ENDIAN
#define ELF_DATA        ELFDATA2MSB
#else
#define ELF_DATA        ELFDATA2LSB
#endif

#ifdef TARGET_ABI_MIPSN32
typedef abi_ullong      target_elf_greg_t;
#define tswapreg(ptr)   tswap64(ptr)
#else
typedef abi_ulong       target_elf_greg_t;
#define tswapreg(ptr)   tswapal(ptr)
#endif

#ifdef USE_UID16
typedef abi_ushort      target_uid_t;
typedef abi_ushort      target_gid_t;
#else
typedef abi_uint        target_uid_t;
typedef abi_uint        target_gid_t;
#endif
typedef abi_int         target_pid_t;

#ifdef TARGET_I386

#define ELF_HWCAP get_elf_hwcap()

static uint32_t get_elf_hwcap(void)
{
    X86CPU *cpu = X86_CPU(thread_cpu);

    return cpu->env.features[FEAT_1_EDX];
}

#ifdef TARGET_X86_64
#define ELF_START_MMAP 0x2aaaaab000ULL

#define ELF_CLASS      ELFCLASS64
#define ELF_ARCH       EM_X86_64

#define ELF_PLATFORM   "x86_64"

static inline void init_thread(struct target_pt_regs *regs, struct image_info *infop)
{
    regs->rax = 0;
    regs->rsp = infop->start_stack;
    regs->rip = infop->entry;
}

#define ELF_NREG    27
typedef target_elf_greg_t  target_elf_gregset_t[ELF_NREG];

/*
 * Note that ELF_NREG should be 29 as there should be place for
 * TRAPNO and ERR "registers" as well but linux doesn't dump
 * those.
 *
 * See linux kernel: arch/x86/include/asm/elf.h
 */
static void elf_core_copy_regs(target_elf_gregset_t *regs, const CPUX86State *env)
{
    (*regs)[0] = tswapreg(env->regs[15]);
    (*regs)[1] = tswapreg(env->regs[14]);
    (*regs)[2] = tswapreg(env->regs[13]);
    (*regs)[3] = tswapreg(env->regs[12]);
    (*regs)[4] = tswapreg(env->regs[R_EBP]);
    (*regs)[5] = tswapreg(env->regs[R_EBX]);
    (*regs)[6] = tswapreg(env->regs[11]);
    (*regs)[7] = tswapreg(env->regs[10]);
    (*regs)[8] = tswapreg(env->regs[9]);
    (*regs)[9] = tswapreg(env->regs[8]);
    (*regs)[10] = tswapreg(env->regs[R_EAX]);
    (*regs)[11] = tswapreg(env->regs[R_ECX]);
    (*regs)[12] = tswapreg(env->regs[R_EDX]);
    (*regs)[13] = tswapreg(env->regs[R_ESI]);
    (*regs)[14] = tswapreg(env->regs[R_EDI]);
    (*regs)[15] = tswapreg(env->regs[R_EAX]); /* XXX */
    (*regs)[16] = tswapreg(env->eip);
    (*regs)[17] = tswapreg(env->segs[R_CS].selector & 0xffff);
    (*regs)[18] = tswapreg(env->eflags);
    (*regs)[19] = tswapreg(env->regs[R_ESP]);
    (*regs)[20] = tswapreg(env->segs[R_SS].selector & 0xffff);
    (*regs)[21] = tswapreg(env->segs[R_FS].selector & 0xffff);
    (*regs)[22] = tswapreg(env->segs[R_GS].selector & 0xffff);
    (*regs)[23] = tswapreg(env->segs[R_DS].selector & 0xffff);
    (*regs)[24] = tswapreg(env->segs[R_ES].selector & 0xffff);
    (*regs)[25] = tswapreg(env->segs[R_FS].selector & 0xffff);
    (*regs)[26] = tswapreg(env->segs[R_GS].selector & 0xffff);
}

#if ULONG_MAX > UINT32_MAX
#define INIT_GUEST_COMMPAGE
static bool init_guest_commpage(void)
{
    /*
     * The vsyscall page is at a high negative address aka kernel space,
     * which means that we cannot actually allocate it with target_mmap.
     * We still should be able to use page_set_flags, unless the user
     * has specified -R reserved_va, which would trigger an assert().
     */
    if (reserved_va != 0 &&
        TARGET_VSYSCALL_PAGE + TARGET_PAGE_SIZE - 1 > reserved_va) {
        error_report("Cannot allocate vsyscall page");
        exit(EXIT_FAILURE);
    }
    page_set_flags(TARGET_VSYSCALL_PAGE,
                   TARGET_VSYSCALL_PAGE | ~TARGET_PAGE_MASK,
                   PAGE_EXEC | PAGE_VALID);
    return true;
}
#endif
#else

#define ELF_START_MMAP 0x80000000

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(x) ( ((x) == EM_386) || ((x) == EM_486) )

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_CLASS       ELFCLASS32
#define ELF_ARCH        EM_386

#define ELF_PLATFORM get_elf_platform()
#define EXSTACK_DEFAULT true

static const char *get_elf_platform(void)
{
    static char elf_platform[] = "i386";
    int family = object_property_get_int(OBJECT(thread_cpu), "family", NULL);
    if (family > 6) {
        family = 6;
    }
    if (family >= 3) {
        elf_platform[1] = '0' + family;
    }
    return elf_platform;
}

static inline void init_thread(struct target_pt_regs *regs,
                               struct image_info *infop)
{
    regs->esp = infop->start_stack;
    regs->eip = infop->entry;

    /* SVR4/i386 ABI (pages 3-31, 3-32) says that when the program
       starts %edx contains a pointer to a function which might be
       registered using `atexit'.  This provides a mean for the
       dynamic linker to call DT_FINI functions for shared libraries
       that have been loaded before the code runs.

       A value of 0 tells we have no such handler.  */
    regs->edx = 0;
}

#define ELF_NREG    17
typedef target_elf_greg_t  target_elf_gregset_t[ELF_NREG];

/*
 * Note that ELF_NREG should be 19 as there should be place for
 * TRAPNO and ERR "registers" as well but linux doesn't dump
 * those.
 *
 * See linux kernel: arch/x86/include/asm/elf.h
 */
static void elf_core_copy_regs(target_elf_gregset_t *regs, const CPUX86State *env)
{
    (*regs)[0] = tswapreg(env->regs[R_EBX]);
    (*regs)[1] = tswapreg(env->regs[R_ECX]);
    (*regs)[2] = tswapreg(env->regs[R_EDX]);
    (*regs)[3] = tswapreg(env->regs[R_ESI]);
    (*regs)[4] = tswapreg(env->regs[R_EDI]);
    (*regs)[5] = tswapreg(env->regs[R_EBP]);
    (*regs)[6] = tswapreg(env->regs[R_EAX]);
    (*regs)[7] = tswapreg(env->segs[R_DS].selector & 0xffff);
    (*regs)[8] = tswapreg(env->segs[R_ES].selector & 0xffff);
    (*regs)[9] = tswapreg(env->segs[R_FS].selector & 0xffff);
    (*regs)[10] = tswapreg(env->segs[R_GS].selector & 0xffff);
    (*regs)[11] = tswapreg(env->regs[R_EAX]); /* XXX */
    (*regs)[12] = tswapreg(env->eip);
    (*regs)[13] = tswapreg(env->segs[R_CS].selector & 0xffff);
    (*regs)[14] = tswapreg(env->eflags);
    (*regs)[15] = tswapreg(env->regs[R_ESP]);
    (*regs)[16] = tswapreg(env->segs[R_SS].selector & 0xffff);
}
#endif

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE       4096

#endif

#ifdef TARGET_ARM

#ifndef TARGET_AARCH64
/* 32 bit ARM definitions */

#define ELF_START_MMAP 0x80000000

#define ELF_ARCH        EM_ARM
#define ELF_CLASS       ELFCLASS32
#define EXSTACK_DEFAULT true

static inline void init_thread(struct target_pt_regs *regs,
                               struct image_info *infop)
{
    abi_long stack = infop->start_stack;
    memset(regs, 0, sizeof(*regs));

    regs->uregs[16] = ARM_CPU_MODE_USR;
    if (infop->entry & 1) {
        regs->uregs[16] |= CPSR_T;
    }
    regs->uregs[15] = infop->entry & 0xfffffffe;
    regs->uregs[13] = infop->start_stack;
    /* FIXME - what to for failure of get_user()? */
    get_user_ual(regs->uregs[2], stack + 8); /* envp */
    get_user_ual(regs->uregs[1], stack + 4); /* envp */
    /* XXX: it seems that r0 is zeroed after ! */
    regs->uregs[0] = 0;
    /* For uClinux PIC binaries.  */
    /* XXX: Linux does this only on ARM with no MMU (do we care ?) */
    regs->uregs[10] = infop->start_data;

    /* Support ARM FDPIC.  */
    if (info_is_fdpic(infop)) {
        /* As described in the ABI document, r7 points to the loadmap info
         * prepared by the kernel. If an interpreter is needed, r8 points
         * to the interpreter loadmap and r9 points to the interpreter
         * PT_DYNAMIC info. If no interpreter is needed, r8 is zero, and
         * r9 points to the main program PT_DYNAMIC info.
         */
        regs->uregs[7] = infop->loadmap_addr;
        if (infop->interpreter_loadmap_addr) {
            /* Executable is dynamically loaded.  */
            regs->uregs[8] = infop->interpreter_loadmap_addr;
            regs->uregs[9] = infop->interpreter_pt_dynamic_addr;
        } else {
            regs->uregs[8] = 0;
            regs->uregs[9] = infop->pt_dynamic_addr;
        }
    }
}

#define ELF_NREG    18
typedef target_elf_greg_t  target_elf_gregset_t[ELF_NREG];

static void elf_core_copy_regs(target_elf_gregset_t *regs, const CPUARMState *env)
{
    (*regs)[0] = tswapreg(env->regs[0]);
    (*regs)[1] = tswapreg(env->regs[1]);
    (*regs)[2] = tswapreg(env->regs[2]);
    (*regs)[3] = tswapreg(env->regs[3]);
    (*regs)[4] = tswapreg(env->regs[4]);
    (*regs)[5] = tswapreg(env->regs[5]);
    (*regs)[6] = tswapreg(env->regs[6]);
    (*regs)[7] = tswapreg(env->regs[7]);
    (*regs)[8] = tswapreg(env->regs[8]);
    (*regs)[9] = tswapreg(env->regs[9]);
    (*regs)[10] = tswapreg(env->regs[10]);
    (*regs)[11] = tswapreg(env->regs[11]);
    (*regs)[12] = tswapreg(env->regs[12]);
    (*regs)[13] = tswapreg(env->regs[13]);
    (*regs)[14] = tswapreg(env->regs[14]);
    (*regs)[15] = tswapreg(env->regs[15]);

    (*regs)[16] = tswapreg(cpsr_read((CPUARMState *)env));
    (*regs)[17] = tswapreg(env->regs[0]); /* XXX */
}

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE       4096

enum
{
    ARM_HWCAP_ARM_SWP       = 1 << 0,
    ARM_HWCAP_ARM_HALF      = 1 << 1,
    ARM_HWCAP_ARM_THUMB     = 1 << 2,
    ARM_HWCAP_ARM_26BIT     = 1 << 3,
    ARM_HWCAP_ARM_FAST_MULT = 1 << 4,
    ARM_HWCAP_ARM_FPA       = 1 << 5,
    ARM_HWCAP_ARM_VFP       = 1 << 6,
    ARM_HWCAP_ARM_EDSP      = 1 << 7,
    ARM_HWCAP_ARM_JAVA      = 1 << 8,
    ARM_HWCAP_ARM_IWMMXT    = 1 << 9,
    ARM_HWCAP_ARM_CRUNCH    = 1 << 10,
    ARM_HWCAP_ARM_THUMBEE   = 1 << 11,
    ARM_HWCAP_ARM_NEON      = 1 << 12,
    ARM_HWCAP_ARM_VFPv3     = 1 << 13,
    ARM_HWCAP_ARM_VFPv3D16  = 1 << 14,
    ARM_HWCAP_ARM_TLS       = 1 << 15,
    ARM_HWCAP_ARM_VFPv4     = 1 << 16,
    ARM_HWCAP_ARM_IDIVA     = 1 << 17,
    ARM_HWCAP_ARM_IDIVT     = 1 << 18,
    ARM_HWCAP_ARM_VFPD32    = 1 << 19,
    ARM_HWCAP_ARM_LPAE      = 1 << 20,
    ARM_HWCAP_ARM_EVTSTRM   = 1 << 21,
};

enum {
    ARM_HWCAP2_ARM_AES      = 1 << 0,
    ARM_HWCAP2_ARM_PMULL    = 1 << 1,
    ARM_HWCAP2_ARM_SHA1     = 1 << 2,
    ARM_HWCAP2_ARM_SHA2     = 1 << 3,
    ARM_HWCAP2_ARM_CRC32    = 1 << 4,
};

/* The commpage only exists for 32 bit kernels */

#define HI_COMMPAGE (intptr_t)0xffff0f00u

static bool init_guest_commpage(void)
{
    abi_ptr commpage = HI_COMMPAGE & -qemu_host_page_size;
    void *want = g2h_untagged(commpage);
    void *addr = mmap(want, qemu_host_page_size, PROT_READ | PROT_WRITE,
                      MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED, -1, 0);

    if (addr == MAP_FAILED) {
        perror("Allocating guest commpage");
        exit(EXIT_FAILURE);
    }
    if (addr != want) {
        return false;
    }

    /* Set kernel helper versions; rest of page is 0.  */
    __put_user(5, (uint32_t *)g2h_untagged(0xffff0ffcu));

    if (mprotect(addr, qemu_host_page_size, PROT_READ)) {
        perror("Protecting guest commpage");
        exit(EXIT_FAILURE);
    }

    page_set_flags(commpage, commpage | ~qemu_host_page_mask,
                   PAGE_READ | PAGE_EXEC | PAGE_VALID);
    return true;
}

#define ELF_HWCAP get_elf_hwcap()
#define ELF_HWCAP2 get_elf_hwcap2()

static uint32_t get_elf_hwcap(void)
{
    ARMCPU *cpu = ARM_CPU(thread_cpu);
    uint32_t hwcaps = 0;

    hwcaps |= ARM_HWCAP_ARM_SWP;
    hwcaps |= ARM_HWCAP_ARM_HALF;
    hwcaps |= ARM_HWCAP_ARM_THUMB;
    hwcaps |= ARM_HWCAP_ARM_FAST_MULT;

    /* probe for the extra features */
#define GET_FEATURE(feat, hwcap) \
    do { if (arm_feature(&cpu->env, feat)) { hwcaps |= hwcap; } } while (0)

#define GET_FEATURE_ID(feat, hwcap) \
    do { if (cpu_isar_feature(feat, cpu)) { hwcaps |= hwcap; } } while (0)

    /* EDSP is in v5TE and above, but all our v5 CPUs are v5TE */
    GET_FEATURE(ARM_FEATURE_V5, ARM_HWCAP_ARM_EDSP);
    GET_FEATURE(ARM_FEATURE_IWMMXT, ARM_HWCAP_ARM_IWMMXT);
    GET_FEATURE(ARM_FEATURE_THUMB2EE, ARM_HWCAP_ARM_THUMBEE);
    GET_FEATURE(ARM_FEATURE_NEON, ARM_HWCAP_ARM_NEON);
    GET_FEATURE(ARM_FEATURE_V6K, ARM_HWCAP_ARM_TLS);
    GET_FEATURE(ARM_FEATURE_LPAE, ARM_HWCAP_ARM_LPAE);
    GET_FEATURE_ID(aa32_arm_div, ARM_HWCAP_ARM_IDIVA);
    GET_FEATURE_ID(aa32_thumb_div, ARM_HWCAP_ARM_IDIVT);
    GET_FEATURE_ID(aa32_vfp, ARM_HWCAP_ARM_VFP);

    if (cpu_isar_feature(aa32_fpsp_v3, cpu) ||
        cpu_isar_feature(aa32_fpdp_v3, cpu)) {
        hwcaps |= ARM_HWCAP_ARM_VFPv3;
        if (cpu_isar_feature(aa32_simd_r32, cpu)) {
            hwcaps |= ARM_HWCAP_ARM_VFPD32;
        } else {
            hwcaps |= ARM_HWCAP_ARM_VFPv3D16;
        }
    }
    GET_FEATURE_ID(aa32_simdfmac, ARM_HWCAP_ARM_VFPv4);

    return hwcaps;
}

static uint32_t get_elf_hwcap2(void)
{
    ARMCPU *cpu = ARM_CPU(thread_cpu);
    uint32_t hwcaps = 0;

    GET_FEATURE_ID(aa32_aes, ARM_HWCAP2_ARM_AES);
    GET_FEATURE_ID(aa32_pmull, ARM_HWCAP2_ARM_PMULL);
    GET_FEATURE_ID(aa32_sha1, ARM_HWCAP2_ARM_SHA1);
    GET_FEATURE_ID(aa32_sha2, ARM_HWCAP2_ARM_SHA2);
    GET_FEATURE_ID(aa32_crc32, ARM_HWCAP2_ARM_CRC32);
    return hwcaps;
}

#undef GET_FEATURE
#undef GET_FEATURE_ID

#define ELF_PLATFORM get_elf_platform()

static const char *get_elf_platform(void)
{
    CPUARMState *env = thread_cpu->env_ptr;

#if TARGET_BIG_ENDIAN
# define END  "b"
#else
# define END  "l"
#endif

    if (arm_feature(env, ARM_FEATURE_V8)) {
        return "v8" END;
    } else if (arm_feature(env, ARM_FEATURE_V7)) {
        if (arm_feature(env, ARM_FEATURE_M)) {
            return "v7m" END;
        } else {
            return "v7" END;
        }
    } else if (arm_feature(env, ARM_FEATURE_V6)) {
        return "v6" END;
    } else if (arm_feature(env, ARM_FEATURE_V5)) {
        return "v5" END;
    } else {
        return "v4" END;
    }

#undef END
}

#else
/* 64 bit ARM definitions */
#define ELF_START_MMAP 0x80000000

#define ELF_ARCH        EM_AARCH64
#define ELF_CLASS       ELFCLASS64
#if TARGET_BIG_ENDIAN
# define ELF_PLATFORM    "aarch64_be"
#else
# define ELF_PLATFORM    "aarch64"
#endif

static inline void init_thread(struct target_pt_regs *regs,
                               struct image_info *infop)
{
    abi_long stack = infop->start_stack;
    memset(regs, 0, sizeof(*regs));

    regs->pc = infop->entry & ~0x3ULL;
    regs->sp = stack;
}

#define ELF_NREG    34
typedef target_elf_greg_t  target_elf_gregset_t[ELF_NREG];

static void elf_core_copy_regs(target_elf_gregset_t *regs,
                               const CPUARMState *env)
{
    int i;

    for (i = 0; i < 32; i++) {
        (*regs)[i] = tswapreg(env->xregs[i]);
    }
    (*regs)[32] = tswapreg(env->pc);
    (*regs)[33] = tswapreg(pstate_read((CPUARMState *)env));
}

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE       4096

enum {
    ARM_HWCAP_A64_FP            = 1 << 0,
    ARM_HWCAP_A64_ASIMD         = 1 << 1,
    ARM_HWCAP_A64_EVTSTRM       = 1 << 2,
    ARM_HWCAP_A64_AES           = 1 << 3,
    ARM_HWCAP_A64_PMULL         = 1 << 4,
    ARM_HWCAP_A64_SHA1          = 1 << 5,
    ARM_HWCAP_A64_SHA2          = 1 << 6,
    ARM_HWCAP_A64_CRC32         = 1 << 7,
    ARM_HWCAP_A64_ATOMICS       = 1 << 8,
    ARM_HWCAP_A64_FPHP          = 1 << 9,
    ARM_HWCAP_A64_ASIMDHP       = 1 << 10,
    ARM_HWCAP_A64_CPUID         = 1 << 11,
    ARM_HWCAP_A64_ASIMDRDM      = 1 << 12,
    ARM_HWCAP_A64_JSCVT         = 1 << 13,
    ARM_HWCAP_A64_FCMA          = 1 << 14,
    ARM_HWCAP_A64_LRCPC         = 1 << 15,
    ARM_HWCAP_A64_DCPOP         = 1 << 16,
    ARM_HWCAP_A64_SHA3          = 1 << 17,
    ARM_HWCAP_A64_SM3           = 1 << 18,
    ARM_HWCAP_A64_SM4           = 1 << 19,
    ARM_HWCAP_A64_ASIMDDP       = 1 << 20,
    ARM_HWCAP_A64_SHA512        = 1 << 21,
    ARM_HWCAP_A64_SVE           = 1 << 22,
    ARM_HWCAP_A64_ASIMDFHM      = 1 << 23,
    ARM_HWCAP_A64_DIT           = 1 << 24,
    ARM_HWCAP_A64_USCAT         = 1 << 25,
    ARM_HWCAP_A64_ILRCPC        = 1 << 26,
    ARM_HWCAP_A64_FLAGM         = 1 << 27,
    ARM_HWCAP_A64_SSBS          = 1 << 28,
    ARM_HWCAP_A64_SB            = 1 << 29,
    ARM_HWCAP_A64_PACA          = 1 << 30,
    ARM_HWCAP_A64_PACG          = 1UL << 31,

    ARM_HWCAP2_A64_DCPODP       = 1 << 0,
    ARM_HWCAP2_A64_SVE2         = 1 << 1,
    ARM_HWCAP2_A64_SVEAES       = 1 << 2,
    ARM_HWCAP2_A64_SVEPMULL     = 1 << 3,
    ARM_HWCAP2_A64_SVEBITPERM   = 1 << 4,
    ARM_HWCAP2_A64_SVESHA3      = 1 << 5,
    ARM_HWCAP2_A64_SVESM4       = 1 << 6,
    ARM_HWCAP2_A64_FLAGM2       = 1 << 7,
    ARM_HWCAP2_A64_FRINT        = 1 << 8,
    ARM_HWCAP2_A64_SVEI8MM      = 1 << 9,
    ARM_HWCAP2_A64_SVEF32MM     = 1 << 10,
    ARM_HWCAP2_A64_SVEF64MM     = 1 << 11,
    ARM_HWCAP2_A64_SVEBF16      = 1 << 12,
    ARM_HWCAP2_A64_I8MM         = 1 << 13,
    ARM_HWCAP2_A64_BF16         = 1 << 14,
    ARM_HWCAP2_A64_DGH          = 1 << 15,
    ARM_HWCAP2_A64_RNG          = 1 << 16,
    ARM_HWCAP2_A64_BTI          = 1 << 17,
    ARM_HWCAP2_A64_MTE          = 1 << 18,
    ARM_HWCAP2_A64_ECV          = 1 << 19,
    ARM_HWCAP2_A64_AFP          = 1 << 20,
    ARM_HWCAP2_A64_RPRES        = 1 << 21,
    ARM_HWCAP2_A64_MTE3         = 1 << 22,
    ARM_HWCAP2_A64_SME          = 1 << 23,
    ARM_HWCAP2_A64_SME_I16I64   = 1 << 24,
    ARM_HWCAP2_A64_SME_F64F64   = 1 << 25,
    ARM_HWCAP2_A64_SME_I8I32    = 1 << 26,
    ARM_HWCAP2_A64_SME_F16F32   = 1 << 27,
    ARM_HWCAP2_A64_SME_B16F32   = 1 << 28,
    ARM_HWCAP2_A64_SME_F32F32   = 1 << 29,
    ARM_HWCAP2_A64_SME_FA64     = 1 << 30,
};

#define ELF_HWCAP   get_elf_hwcap()
#define ELF_HWCAP2  get_elf_hwcap2()

#define GET_FEATURE_ID(feat, hwcap) \
    do { if (cpu_isar_feature(feat, cpu)) { hwcaps |= hwcap; } } while (0)

static uint32_t get_elf_hwcap(void)
{
    ARMCPU *cpu = ARM_CPU(thread_cpu);
    uint32_t hwcaps = 0;

    hwcaps |= ARM_HWCAP_A64_FP;
    hwcaps |= ARM_HWCAP_A64_ASIMD;
    hwcaps |= ARM_HWCAP_A64_CPUID;

    /* probe for the extra features */

    GET_FEATURE_ID(aa64_aes, ARM_HWCAP_A64_AES);
    GET_FEATURE_ID(aa64_pmull, ARM_HWCAP_A64_PMULL);
    GET_FEATURE_ID(aa64_sha1, ARM_HWCAP_A64_SHA1);
    GET_FEATURE_ID(aa64_sha256, ARM_HWCAP_A64_SHA2);
    GET_FEATURE_ID(aa64_sha512, ARM_HWCAP_A64_SHA512);
    GET_FEATURE_ID(aa64_crc32, ARM_HWCAP_A64_CRC32);
    GET_FEATURE_ID(aa64_sha3, ARM_HWCAP_A64_SHA3);
    GET_FEATURE_ID(aa64_sm3, ARM_HWCAP_A64_SM3);
    GET_FEATURE_ID(aa64_sm4, ARM_HWCAP_A64_SM4);
    GET_FEATURE_ID(aa64_fp16, ARM_HWCAP_A64_FPHP | ARM_HWCAP_A64_ASIMDHP);
    GET_FEATURE_ID(aa64_atomics, ARM_HWCAP_A64_ATOMICS);
    GET_FEATURE_ID(aa64_rdm, ARM_HWCAP_A64_ASIMDRDM);
    GET_FEATURE_ID(aa64_dp, ARM_HWCAP_A64_ASIMDDP);
    GET_FEATURE_ID(aa64_fcma, ARM_HWCAP_A64_FCMA);
    GET_FEATURE_ID(aa64_sve, ARM_HWCAP_A64_SVE);
    GET_FEATURE_ID(aa64_pauth, ARM_HWCAP_A64_PACA | ARM_HWCAP_A64_PACG);
    GET_FEATURE_ID(aa64_fhm, ARM_HWCAP_A64_ASIMDFHM);
    GET_FEATURE_ID(aa64_jscvt, ARM_HWCAP_A64_JSCVT);
    GET_FEATURE_ID(aa64_sb, ARM_HWCAP_A64_SB);
    GET_FEATURE_ID(aa64_condm_4, ARM_HWCAP_A64_FLAGM);
    GET_FEATURE_ID(aa64_dcpop, ARM_HWCAP_A64_DCPOP);
    GET_FEATURE_ID(aa64_rcpc_8_3, ARM_HWCAP_A64_LRCPC);
    GET_FEATURE_ID(aa64_rcpc_8_4, ARM_HWCAP_A64_ILRCPC);

    return hwcaps;
}

static uint32_t get_elf_hwcap2(void)
{
    ARMCPU *cpu = ARM_CPU(thread_cpu);
    uint32_t hwcaps = 0;

    GET_FEATURE_ID(aa64_dcpodp, ARM_HWCAP2_A64_DCPODP);
    GET_FEATURE_ID(aa64_sve2, ARM_HWCAP2_A64_SVE2);
    GET_FEATURE_ID(aa64_sve2_aes, ARM_HWCAP2_A64_SVEAES);
    GET_FEATURE_ID(aa64_sve2_pmull128, ARM_HWCAP2_A64_SVEPMULL);
    GET_FEATURE_ID(aa64_sve2_bitperm, ARM_HWCAP2_A64_SVEBITPERM);
    GET_FEATURE_ID(aa64_sve2_sha3, ARM_HWCAP2_A64_SVESHA3);
    GET_FEATURE_ID(aa64_sve2_sm4, ARM_HWCAP2_A64_SVESM4);
    GET_FEATURE_ID(aa64_condm_5, ARM_HWCAP2_A64_FLAGM2);
    GET_FEATURE_ID(aa64_frint, ARM_HWCAP2_A64_FRINT);
    GET_FEATURE_ID(aa64_sve_i8mm, ARM_HWCAP2_A64_SVEI8MM);
    GET_FEATURE_ID(aa64_sve_f32mm, ARM_HWCAP2_A64_SVEF32MM);
    GET_FEATURE_ID(aa64_sve_f64mm, ARM_HWCAP2_A64_SVEF64MM);
    GET_FEATURE_ID(aa64_sve_bf16, ARM_HWCAP2_A64_SVEBF16);
    GET_FEATURE_ID(aa64_i8mm, ARM_HWCAP2_A64_I8MM);
    GET_FEATURE_ID(aa64_bf16, ARM_HWCAP2_A64_BF16);
    GET_FEATURE_ID(aa64_rndr, ARM_HWCAP2_A64_RNG);
    GET_FEATURE_ID(aa64_bti, ARM_HWCAP2_A64_BTI);
    GET_FEATURE_ID(aa64_mte, ARM_HWCAP2_A64_MTE);
    GET_FEATURE_ID(aa64_sme, (ARM_HWCAP2_A64_SME |
                              ARM_HWCAP2_A64_SME_F32F32 |
                              ARM_HWCAP2_A64_SME_B16F32 |
                              ARM_HWCAP2_A64_SME_F16F32 |
                              ARM_HWCAP2_A64_SME_I8I32));
    GET_FEATURE_ID(aa64_sme_f64f64, ARM_HWCAP2_A64_SME_F64F64);
    GET_FEATURE_ID(aa64_sme_i16i64, ARM_HWCAP2_A64_SME_I16I64);
    GET_FEATURE_ID(aa64_sme_fa64, ARM_HWCAP2_A64_SME_FA64);

    return hwcaps;
}

#undef GET_FEATURE_ID

#endif /* not TARGET_AARCH64 */
#endif /* TARGET_ARM */

#ifdef TARGET_SPARC
#ifdef TARGET_SPARC64

#define ELF_START_MMAP 0x80000000
#define ELF_HWCAP  (HWCAP_SPARC_FLUSH | HWCAP_SPARC_STBAR | HWCAP_SPARC_SWAP \
                    | HWCAP_SPARC_MULDIV | HWCAP_SPARC_V9)
#ifndef TARGET_ABI32
#define elf_check_arch(x) ( (x) == EM_SPARCV9 || (x) == EM_SPARC32PLUS )
#else
#define elf_check_arch(x) ( (x) == EM_SPARC32PLUS || (x) == EM_SPARC )
#endif

#define ELF_CLASS   ELFCLASS64
#define ELF_ARCH    EM_SPARCV9
#else
#define ELF_START_MMAP 0x80000000
#define ELF_HWCAP  (HWCAP_SPARC_FLUSH | HWCAP_SPARC_STBAR | HWCAP_SPARC_SWAP \
                    | HWCAP_SPARC_MULDIV)
#define ELF_CLASS   ELFCLASS32
#define ELF_ARCH    EM_SPARC
#endif /* TARGET_SPARC64 */

static inline void init_thread(struct target_pt_regs *regs,
                               struct image_info *infop)
{
    /* Note that target_cpu_copy_regs does not read psr/tstate. */
    regs->pc = infop->entry;
    regs->npc = regs->pc + 4;
    regs->y = 0;
    regs->u_regs[14] = (infop->start_stack - 16 * sizeof(abi_ulong)
                        - TARGET_STACK_BIAS);
}
#endif /* TARGET_SPARC */

#ifdef TARGET_PPC

#define ELF_MACHINE    PPC_ELF_MACHINE
#define ELF_START_MMAP 0x80000000

#if defined(TARGET_PPC64)

#define elf_check_arch(x) ( (x) == EM_PPC64 )

#define ELF_CLASS       ELFCLASS64

#else

#define ELF_CLASS       ELFCLASS32
#define EXSTACK_DEFAULT true

#endif

#define ELF_ARCH        EM_PPC

/* Feature masks for the Aux Vector Hardware Capabilities (AT_HWCAP).
   See arch/powerpc/include/asm/cputable.h.  */
enum {
    QEMU_PPC_FEATURE_32 = 0x80000000,
    QEMU_PPC_FEATURE_64 = 0x40000000,
    QEMU_PPC_FEATURE_601_INSTR = 0x20000000,
    QEMU_PPC_FEATURE_HAS_ALTIVEC = 0x10000000,
    QEMU_PPC_FEATURE_HAS_FPU = 0x08000000,
    QEMU_PPC_FEATURE_HAS_MMU = 0x04000000,
    QEMU_PPC_FEATURE_HAS_4xxMAC = 0x02000000,
    QEMU_PPC_FEATURE_UNIFIED_CACHE = 0x01000000,
    QEMU_PPC_FEATURE_HAS_SPE = 0x00800000,
    QEMU_PPC_FEATURE_HAS_EFP_SINGLE = 0x00400000,
    QEMU_PPC_FEATURE_HAS_EFP_DOUBLE = 0x00200000,
    QEMU_PPC_FEATURE_NO_TB = 0x00100000,
    QEMU_PPC_FEATURE_POWER4 = 0x00080000,
    QEMU_PPC_FEATURE_POWER5 = 0x00040000,
    QEMU_PPC_FEATURE_POWER5_PLUS = 0x00020000,
    QEMU_PPC_FEATURE_CELL = 0x00010000,
    QEMU_PPC_FEATURE_BOOKE = 0x00008000,
    QEMU_PPC_FEATURE_SMT = 0x00004000,
    QEMU_PPC_FEATURE_ICACHE_SNOOP = 0x00002000,
    QEMU_PPC_FEATURE_ARCH_2_05 = 0x00001000,
    QEMU_PPC_FEATURE_PA6T = 0x00000800,
    QEMU_PPC_FEATURE_HAS_DFP = 0x00000400,
    QEMU_PPC_FEATURE_POWER6_EXT = 0x00000200,
    QEMU_PPC_FEATURE_ARCH_2_06 = 0x00000100,
    QEMU_PPC_FEATURE_HAS_VSX = 0x00000080,
    QEMU_PPC_FEATURE_PSERIES_PERFMON_COMPAT = 0x00000040,

    QEMU_PPC_FEATURE_TRUE_LE = 0x00000002,
    QEMU_PPC_FEATURE_PPC_LE = 0x00000001,

    /* Feature definitions in AT_HWCAP2.  */
    QEMU_PPC_FEATURE2_ARCH_2_07 = 0x80000000, /* ISA 2.07 */
    QEMU_PPC_FEATURE2_HAS_HTM = 0x40000000, /* Hardware Transactional Memory */
    QEMU_PPC_FEATURE2_HAS_DSCR = 0x20000000, /* Data Stream Control Register */
    QEMU_PPC_FEATURE2_HAS_EBB = 0x10000000, /* Event Base Branching */
    QEMU_PPC_FEATURE2_HAS_ISEL = 0x08000000, /* Integer Select */
    QEMU_PPC_FEATURE2_HAS_TAR = 0x04000000, /* Target Address Register */
    QEMU_PPC_FEATURE2_VEC_CRYPTO = 0x02000000,
    QEMU_PPC_FEATURE2_HTM_NOSC = 0x01000000,
    QEMU_PPC_FEATURE2_ARCH_3_00 = 0x00800000, /* ISA 3.00 */
    QEMU_PPC_FEATURE2_HAS_IEEE128 = 0x00400000, /* VSX IEEE Bin Float 128-bit */
    QEMU_PPC_FEATURE2_DARN = 0x00200000, /* darn random number insn */
    QEMU_PPC_FEATURE2_SCV = 0x00100000, /* scv syscall */
    QEMU_PPC_FEATURE2_HTM_NO_SUSPEND = 0x00080000, /* TM w/o suspended state */
    QEMU_PPC_FEATURE2_ARCH_3_1 = 0x00040000, /* ISA 3.1 */
    QEMU_PPC_FEATURE2_MMA = 0x00020000, /* Matrix-Multiply Assist */
};

#define ELF_HWCAP get_elf_hwcap()

static uint32_t get_elf_hwcap(void)
{
    PowerPCCPU *cpu = POWERPC_CPU(thread_cpu);
    uint32_t features = 0;

    /* We don't have to be terribly complete here; the high points are
       Altivec/FP/SPE support.  Anything else is just a bonus.  */
#define GET_FEATURE(flag, feature)                                      \
    do { if (cpu->env.insns_flags & flag) { features |= feature; } } while (0)
#define GET_FEATURE2(flags, feature) \
    do { \
        if ((cpu->env.insns_flags2 & flags) == flags) { \
            features |= feature; \
        } \
    } while (0)
    GET_FEATURE(PPC_64B, QEMU_PPC_FEATURE_64);
    GET_FEATURE(PPC_FLOAT, QEMU_PPC_FEATURE_HAS_FPU);
    GET_FEATURE(PPC_ALTIVEC, QEMU_PPC_FEATURE_HAS_ALTIVEC);
    GET_FEATURE(PPC_SPE, QEMU_PPC_FEATURE_HAS_SPE);
    GET_FEATURE(PPC_SPE_SINGLE, QEMU_PPC_FEATURE_HAS_EFP_SINGLE);
    GET_FEATURE(PPC_SPE_DOUBLE, QEMU_PPC_FEATURE_HAS_EFP_DOUBLE);
    GET_FEATURE(PPC_BOOKE, QEMU_PPC_FEATURE_BOOKE);
    GET_FEATURE(PPC_405_MAC, QEMU_PPC_FEATURE_HAS_4xxMAC);
    GET_FEATURE2(PPC2_DFP, QEMU_PPC_FEATURE_HAS_DFP);
    GET_FEATURE2(PPC2_VSX, QEMU_PPC_FEATURE_HAS_VSX);
    GET_FEATURE2((PPC2_PERM_ISA206 | PPC2_DIVE_ISA206 | PPC2_ATOMIC_ISA206 |
                  PPC2_FP_CVT_ISA206 | PPC2_FP_TST_ISA206),
                  QEMU_PPC_FEATURE_ARCH_2_06);
#undef GET_FEATURE
#undef GET_FEATURE2

    return features;
}

#define ELF_HWCAP2 get_elf_hwcap2()

static uint32_t get_elf_hwcap2(void)
{
    PowerPCCPU *cpu = POWERPC_CPU(thread_cpu);
    uint32_t features = 0;

#define GET_FEATURE(flag, feature)                                      \
    do { if (cpu->env.insns_flags & flag) { features |= feature; } } while (0)
#define GET_FEATURE2(flag, feature)                                      \
    do { if (cpu->env.insns_flags2 & flag) { features |= feature; } } while (0)

    GET_FEATURE(PPC_ISEL, QEMU_PPC_FEATURE2_HAS_ISEL);
    GET_FEATURE2(PPC2_BCTAR_ISA207, QEMU_PPC_FEATURE2_HAS_TAR);
    GET_FEATURE2((PPC2_BCTAR_ISA207 | PPC2_LSQ_ISA207 | PPC2_ALTIVEC_207 |
                  PPC2_ISA207S), QEMU_PPC_FEATURE2_ARCH_2_07 |
                  QEMU_PPC_FEATURE2_VEC_CRYPTO);
    GET_FEATURE2(PPC2_ISA300, QEMU_PPC_FEATURE2_ARCH_3_00 |
                 QEMU_PPC_FEATURE2_DARN | QEMU_PPC_FEATURE2_HAS_IEEE128);
    GET_FEATURE2(PPC2_ISA310, QEMU_PPC_FEATURE2_ARCH_3_1 |
                 QEMU_PPC_FEATURE2_MMA);

#undef GET_FEATURE
#undef GET_FEATURE2

    return features;
}

/*
 * The requirements here are:
 * - keep the final alignment of sp (sp & 0xf)
 * - make sure the 32-bit value at the first 16 byte aligned position of
 *   AUXV is greater than 16 for glibc compatibility.
 *   AT_IGNOREPPC is used for that.
 * - for compatibility with glibc ARCH_DLINFO must always be defined on PPC,
 *   even if DLINFO_ARCH_ITEMS goes to zero or is undefined.
 */
#define DLINFO_ARCH_ITEMS       5
#define ARCH_DLINFO                                     \
    do {                                                \
        PowerPCCPU *cpu = POWERPC_CPU(thread_cpu);              \
        /*                                              \
         * Handle glibc compatibility: these magic entries must \
         * be at the lowest addresses in the final auxv.        \
         */                                             \
        NEW_AUX_ENT(AT_IGNOREPPC, AT_IGNOREPPC);        \
        NEW_AUX_ENT(AT_IGNOREPPC, AT_IGNOREPPC);        \
        NEW_AUX_ENT(AT_DCACHEBSIZE, cpu->env.dcache_line_size); \
        NEW_AUX_ENT(AT_ICACHEBSIZE, cpu->env.icache_line_size); \
        NEW_AUX_ENT(AT_UCACHEBSIZE, 0);                 \
    } while (0)

static inline void init_thread(struct target_pt_regs *_regs, struct image_info *infop)
{
    _regs->gpr[1] = infop->start_stack;
#if defined(TARGET_PPC64)
    if (get_ppc64_abi(infop) < 2) {
        uint64_t val;
        get_user_u64(val, infop->entry + 8);
        _regs->gpr[2] = val + infop->load_bias;
        get_user_u64(val, infop->entry);
        infop->entry = val + infop->load_bias;
    } else {
        _regs->gpr[12] = infop->entry;  /* r12 set to global entry address */
    }
#endif
    _regs->nip = infop->entry;
}

/* See linux kernel: arch/powerpc/include/asm/elf.h.  */
#define ELF_NREG 48
typedef target_elf_greg_t target_elf_gregset_t[ELF_NREG];

static void elf_core_copy_regs(target_elf_gregset_t *regs, const CPUPPCState *env)
{
    int i;
    target_ulong ccr = 0;

    for (i = 0; i < ARRAY_SIZE(env->gpr); i++) {
        (*regs)[i] = tswapreg(env->gpr[i]);
    }

    (*regs)[32] = tswapreg(env->nip);
    (*regs)[33] = tswapreg(env->msr);
    (*regs)[35] = tswapreg(env->ctr);
    (*regs)[36] = tswapreg(env->lr);
    (*regs)[37] = tswapreg(cpu_read_xer(env));

    ccr = ppc_get_cr(env);
    (*regs)[38] = tswapreg(ccr);
}

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE       4096

#endif

#ifdef TARGET_LOONGARCH64

#define ELF_START_MMAP 0x80000000

#define ELF_CLASS   ELFCLASS64
#define ELF_ARCH    EM_LOONGARCH
#define EXSTACK_DEFAULT true

#define elf_check_arch(x) ((x) == EM_LOONGARCH)

static inline void init_thread(struct target_pt_regs *regs,
                               struct image_info *infop)
{
    /*Set crmd PG,DA = 1,0 */
    regs->csr.crmd = 2 << 3;
    regs->csr.era = infop->entry;
    regs->regs[3] = infop->start_stack;
}

/* See linux kernel: arch/loongarch/include/asm/elf.h */
#define ELF_NREG 45
typedef target_elf_greg_t target_elf_gregset_t[ELF_NREG];

enum {
    TARGET_EF_R0 = 0,
    TARGET_EF_CSR_ERA = TARGET_EF_R0 + 33,
    TARGET_EF_CSR_BADV = TARGET_EF_R0 + 34,
};

static void elf_core_copy_regs(target_elf_gregset_t *regs,
                               const CPULoongArchState *env)
{
    int i;

    (*regs)[TARGET_EF_R0] = 0;

    for (i = 1; i < ARRAY_SIZE(env->gpr); i++) {
        (*regs)[TARGET_EF_R0 + i] = tswapreg(env->gpr[i]);
    }

    (*regs)[TARGET_EF_CSR_ERA] = tswapreg(env->pc);
    (*regs)[TARGET_EF_CSR_BADV] = tswapreg(env->CSR_BADV);
}

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE        4096

#define ELF_HWCAP get_elf_hwcap()

/* See arch/loongarch/include/uapi/asm/hwcap.h */
enum {
    HWCAP_LOONGARCH_CPUCFG   = (1 << 0),
    HWCAP_LOONGARCH_LAM      = (1 << 1),
    HWCAP_LOONGARCH_UAL      = (1 << 2),
    HWCAP_LOONGARCH_FPU      = (1 << 3),
    HWCAP_LOONGARCH_LSX      = (1 << 4),
    HWCAP_LOONGARCH_LASX     = (1 << 5),
    HWCAP_LOONGARCH_CRC32    = (1 << 6),
    HWCAP_LOONGARCH_COMPLEX  = (1 << 7),
    HWCAP_LOONGARCH_CRYPTO   = (1 << 8),
    HWCAP_LOONGARCH_LVZ      = (1 << 9),
    HWCAP_LOONGARCH_LBT_X86  = (1 << 10),
    HWCAP_LOONGARCH_LBT_ARM  = (1 << 11),
    HWCAP_LOONGARCH_LBT_MIPS = (1 << 12),
};

static uint32_t get_elf_hwcap(void)
{
    LoongArchCPU *cpu = LOONGARCH_CPU(thread_cpu);
    uint32_t hwcaps = 0;

    hwcaps |= HWCAP_LOONGARCH_CRC32;

    if (FIELD_EX32(cpu->env.cpucfg[1], CPUCFG1, UAL)) {
        hwcaps |= HWCAP_LOONGARCH_UAL;
    }

    if (FIELD_EX32(cpu->env.cpucfg[2], CPUCFG2, FP)) {
        hwcaps |= HWCAP_LOONGARCH_FPU;
    }

    if (FIELD_EX32(cpu->env.cpucfg[2], CPUCFG2, LAM)) {
        hwcaps |= HWCAP_LOONGARCH_LAM;
    }

    return hwcaps;
}

#define ELF_PLATFORM "loongarch"

#endif /* TARGET_LOONGARCH64 */

#ifdef TARGET_MIPS

#define ELF_START_MMAP 0x80000000

#ifdef TARGET_MIPS64
#define ELF_CLASS   ELFCLASS64
#else
#define ELF_CLASS   ELFCLASS32
#endif
#define ELF_ARCH    EM_MIPS
#define EXSTACK_DEFAULT true

#ifdef TARGET_ABI_MIPSN32
#define elf_check_abi(x) ((x) & EF_MIPS_ABI2)
#else
#define elf_check_abi(x) (!((x) & EF_MIPS_ABI2))
#endif

#define ELF_BASE_PLATFORM get_elf_base_platform()

#define MATCH_PLATFORM_INSN(_flags, _base_platform)      \
    do { if ((cpu->env.insn_flags & (_flags)) == _flags) \
    { return _base_platform; } } while (0)

static const char *get_elf_base_platform(void)
{
    MIPSCPU *cpu = MIPS_CPU(thread_cpu);

    /* 64 bit ISAs goes first */
    MATCH_PLATFORM_INSN(CPU_MIPS64R6, "mips64r6");
    MATCH_PLATFORM_INSN(CPU_MIPS64R5, "mips64r5");
    MATCH_PLATFORM_INSN(CPU_MIPS64R2, "mips64r2");
    MATCH_PLATFORM_INSN(CPU_MIPS64R1, "mips64");
    MATCH_PLATFORM_INSN(CPU_MIPS5, "mips5");
    MATCH_PLATFORM_INSN(CPU_MIPS4, "mips4");
    MATCH_PLATFORM_INSN(CPU_MIPS3, "mips3");

    /* 32 bit ISAs */
    MATCH_PLATFORM_INSN(CPU_MIPS32R6, "mips32r6");
    MATCH_PLATFORM_INSN(CPU_MIPS32R5, "mips32r5");
    MATCH_PLATFORM_INSN(CPU_MIPS32R2, "mips32r2");
    MATCH_PLATFORM_INSN(CPU_MIPS32R1, "mips32");
    MATCH_PLATFORM_INSN(CPU_MIPS2, "mips2");

    /* Fallback */
    return "mips";
}
#undef MATCH_PLATFORM_INSN

static inline void init_thread(struct target_pt_regs *regs,
                               struct image_info *infop)
{
    regs->cp0_status = 2 << CP0St_KSU;
    regs->cp0_epc = infop->entry;
    regs->regs[29] = infop->start_stack;
}

/* See linux kernel: arch/mips/include/asm/elf.h.  */
#define ELF_NREG 45
typedef target_elf_greg_t target_elf_gregset_t[ELF_NREG];

/* See linux kernel: arch/mips/include/asm/reg.h.  */
enum {
#ifdef TARGET_MIPS64
    TARGET_EF_R0 = 0,
#else
    TARGET_EF_R0 = 6,
#endif
    TARGET_EF_R26 = TARGET_EF_R0 + 26,
    TARGET_EF_R27 = TARGET_EF_R0 + 27,
    TARGET_EF_LO = TARGET_EF_R0 + 32,
    TARGET_EF_HI = TARGET_EF_R0 + 33,
    TARGET_EF_CP0_EPC = TARGET_EF_R0 + 34,
    TARGET_EF_CP0_BADVADDR = TARGET_EF_R0 + 35,
    TARGET_EF_CP0_STATUS = TARGET_EF_R0 + 36,
    TARGET_EF_CP0_CAUSE = TARGET_EF_R0 + 37
};

/* See linux kernel: arch/mips/kernel/process.c:elf_dump_regs.  */
static void elf_core_copy_regs(target_elf_gregset_t *regs, const CPUMIPSState *env)
{
    int i;

    for (i = 0; i < TARGET_EF_R0; i++) {
        (*regs)[i] = 0;
    }
    (*regs)[TARGET_EF_R0] = 0;

    for (i = 1; i < ARRAY_SIZE(env->active_tc.gpr); i++) {
        (*regs)[TARGET_EF_R0 + i] = tswapreg(env->active_tc.gpr[i]);
    }

    (*regs)[TARGET_EF_R26] = 0;
    (*regs)[TARGET_EF_R27] = 0;
    (*regs)[TARGET_EF_LO] = tswapreg(env->active_tc.LO[0]);
    (*regs)[TARGET_EF_HI] = tswapreg(env->active_tc.HI[0]);
    (*regs)[TARGET_EF_CP0_EPC] = tswapreg(env->active_tc.PC);
    (*regs)[TARGET_EF_CP0_BADVADDR] = tswapreg(env->CP0_BadVAddr);
    (*regs)[TARGET_EF_CP0_STATUS] = tswapreg(env->CP0_Status);
    (*regs)[TARGET_EF_CP0_CAUSE] = tswapreg(env->CP0_Cause);
}

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE        4096

/* See arch/mips/include/uapi/asm/hwcap.h.  */
enum {
    HWCAP_MIPS_R6           = (1 << 0),
    HWCAP_MIPS_MSA          = (1 << 1),
    HWCAP_MIPS_CRC32        = (1 << 2),
    HWCAP_MIPS_MIPS16       = (1 << 3),
    HWCAP_MIPS_MDMX         = (1 << 4),
    HWCAP_MIPS_MIPS3D       = (1 << 5),
    HWCAP_MIPS_SMARTMIPS    = (1 << 6),
    HWCAP_MIPS_DSP          = (1 << 7),
    HWCAP_MIPS_DSP2         = (1 << 8),
    HWCAP_MIPS_DSP3         = (1 << 9),
    HWCAP_MIPS_MIPS16E2     = (1 << 10),
    HWCAP_LOONGSON_MMI      = (1 << 11),
    HWCAP_LOONGSON_EXT      = (1 << 12),
    HWCAP_LOONGSON_EXT2     = (1 << 13),
    HWCAP_LOONGSON_CPUCFG   = (1 << 14),
};

#define ELF_HWCAP get_elf_hwcap()

#define GET_FEATURE_INSN(_flag, _hwcap) \
    do { if (cpu->env.insn_flags & (_flag)) { hwcaps |= _hwcap; } } while (0)

#define GET_FEATURE_REG_SET(_reg, _mask, _hwcap) \
    do { if (cpu->env._reg & (_mask)) { hwcaps |= _hwcap; } } while (0)

#define GET_FEATURE_REG_EQU(_reg, _start, _length, _val, _hwcap) \
    do { \
        if (extract32(cpu->env._reg, (_start), (_length)) == (_val)) { \
            hwcaps |= _hwcap; \
        } \
    } while (0)

static uint32_t get_elf_hwcap(void)
{
    MIPSCPU *cpu = MIPS_CPU(thread_cpu);
    uint32_t hwcaps = 0;

    GET_FEATURE_REG_EQU(CP0_Config0, CP0C0_AR, CP0C0_AR_LENGTH,
                        2, HWCAP_MIPS_R6);
    GET_FEATURE_REG_SET(CP0_Config3, 1 << CP0C3_MSAP, HWCAP_MIPS_MSA);
    GET_FEATURE_INSN(ASE_LMMI, HWCAP_LOONGSON_MMI);
    GET_FEATURE_INSN(ASE_LEXT, HWCAP_LOONGSON_EXT);

    return hwcaps;
}

#undef GET_FEATURE_REG_EQU
#undef GET_FEATURE_REG_SET
#undef GET_FEATURE_INSN

#endif /* TARGET_MIPS */

#ifdef TARGET_MICROBLAZE

#define ELF_START_MMAP 0x80000000

#define elf_check_arch(x) ( (x) == EM_MICROBLAZE || (x) == EM_MICROBLAZE_OLD)

#define ELF_CLASS   ELFCLASS32
#define ELF_ARCH    EM_MICROBLAZE

static inline void init_thread(struct target_pt_regs *regs,
                               struct image_info *infop)
{
    regs->pc = infop->entry;
    regs->r1 = infop->start_stack;

}

#define ELF_EXEC_PAGESIZE        4096

#define USE_ELF_CORE_DUMP
#define ELF_NREG 38
typedef target_elf_greg_t target_elf_gregset_t[ELF_NREG];

/* See linux kernel: arch/mips/kernel/process.c:elf_dump_regs.  */
static void elf_core_copy_regs(target_elf_gregset_t *regs, const CPUMBState *env)
{
    int i, pos = 0;

    for (i = 0; i < 32; i++) {
        (*regs)[pos++] = tswapreg(env->regs[i]);
    }

    (*regs)[pos++] = tswapreg(env->pc);
    (*regs)[pos++] = tswapreg(mb_cpu_read_msr(env));
    (*regs)[pos++] = 0;
    (*regs)[pos++] = tswapreg(env->ear);
    (*regs)[pos++] = 0;
    (*regs)[pos++] = tswapreg(env->esr);
}

#endif /* TARGET_MICROBLAZE */

#ifdef TARGET_NIOS2

#define ELF_START_MMAP 0x80000000

#define elf_check_arch(x) ((x) == EM_ALTERA_NIOS2)

#define ELF_CLASS   ELFCLASS32
#define ELF_ARCH    EM_ALTERA_NIOS2

static void init_thread(struct target_pt_regs *regs, struct image_info *infop)
{
    regs->ea = infop->entry;
    regs->sp = infop->start_stack;
}

#define LO_COMMPAGE  TARGET_PAGE_SIZE

static bool init_guest_commpage(void)
{
    static const uint8_t kuser_page[4 + 2 * 64] = {
        /* __kuser_helper_version */
        [0x00] = 0x02, 0x00, 0x00, 0x00,

        /* __kuser_cmpxchg */
        [0x04] = 0x3a, 0x6c, 0x3b, 0x00,  /* trap 16 */
                 0x3a, 0x28, 0x00, 0xf8,  /* ret */

        /* __kuser_sigtramp */
        [0x44] = 0xc4, 0x22, 0x80, 0x00,  /* movi r2, __NR_rt_sigreturn */
                 0x3a, 0x68, 0x3b, 0x00,  /* trap 0 */
    };

    void *want = g2h_untagged(LO_COMMPAGE & -qemu_host_page_size);
    void *addr = mmap(want, qemu_host_page_size, PROT_READ | PROT_WRITE,
                      MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED, -1, 0);

    if (addr == MAP_FAILED) {
        perror("Allocating guest commpage");
        exit(EXIT_FAILURE);
    }
    if (addr != want) {
        return false;
    }

    memcpy(addr, kuser_page, sizeof(kuser_page));

    if (mprotect(addr, qemu_host_page_size, PROT_READ)) {
        perror("Protecting guest commpage");
        exit(EXIT_FAILURE);
    }

    page_set_flags(LO_COMMPAGE, LO_COMMPAGE | ~TARGET_PAGE_MASK,
                   PAGE_READ | PAGE_EXEC | PAGE_VALID);
    return true;
}

#define ELF_EXEC_PAGESIZE        4096

#define USE_ELF_CORE_DUMP
#define ELF_NREG 49
typedef target_elf_greg_t target_elf_gregset_t[ELF_NREG];

/* See linux kernel: arch/mips/kernel/process.c:elf_dump_regs.  */
static void elf_core_copy_regs(target_elf_gregset_t *regs,
                               const CPUNios2State *env)
{
    int i;

    (*regs)[0] = -1;
    for (i = 1; i < 8; i++)    /* r0-r7 */
        (*regs)[i] = tswapreg(env->regs[i + 7]);

    for (i = 8; i < 16; i++)   /* r8-r15 */
        (*regs)[i] = tswapreg(env->regs[i - 8]);

    for (i = 16; i < 24; i++)  /* r16-r23 */
        (*regs)[i] = tswapreg(env->regs[i + 7]);
    (*regs)[24] = -1;    /* R_ET */
    (*regs)[25] = -1;    /* R_BT */
    (*regs)[26] = tswapreg(env->regs[R_GP]);
    (*regs)[27] = tswapreg(env->regs[R_SP]);
    (*regs)[28] = tswapreg(env->regs[R_FP]);
    (*regs)[29] = tswapreg(env->regs[R_EA]);
    (*regs)[30] = -1;    /* R_SSTATUS */
    (*regs)[31] = tswapreg(env->regs[R_RA]);

    (*regs)[32] = tswapreg(env->pc);

    (*regs)[33] = -1; /* R_STATUS */
    (*regs)[34] = tswapreg(env->regs[CR_ESTATUS]);

    for (i = 35; i < 49; i++)    /* ... */
        (*regs)[i] = -1;
}

#endif /* TARGET_NIOS2 */

#ifdef TARGET_OPENRISC

#define ELF_START_MMAP 0x08000000

#define ELF_ARCH EM_OPENRISC
#define ELF_CLASS ELFCLASS32
#define ELF_DATA  ELFDATA2MSB

static inline void init_thread(struct target_pt_regs *regs,
                               struct image_info *infop)
{
    regs->pc = infop->entry;
    regs->gpr[1] = infop->start_stack;
}

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE 8192

/* See linux kernel arch/openrisc/include/asm/elf.h.  */
#define ELF_NREG 34 /* gprs and pc, sr */
typedef target_elf_greg_t target_elf_gregset_t[ELF_NREG];

static void elf_core_copy_regs(target_elf_gregset_t *regs,
                               const CPUOpenRISCState *env)
{
    int i;

    for (i = 0; i < 32; i++) {
        (*regs)[i] = tswapreg(cpu_get_gpr(env, i));
    }
    (*regs)[32] = tswapreg(env->pc);
    (*regs)[33] = tswapreg(cpu_get_sr(env));
}
#define ELF_HWCAP 0
#define ELF_PLATFORM NULL

#endif /* TARGET_OPENRISC */

#ifdef TARGET_SH4

#define ELF_START_MMAP 0x80000000

#define ELF_CLASS ELFCLASS32
#define ELF_ARCH  EM_SH

static inline void init_thread(struct target_pt_regs *regs,
                               struct image_info *infop)
{
    /* Check other registers XXXXX */
    regs->pc = infop->entry;
    regs->regs[15] = infop->start_stack;
}

/* See linux kernel: arch/sh/include/asm/elf.h.  */
#define ELF_NREG 23
typedef target_elf_greg_t target_elf_gregset_t[ELF_NREG];

/* See linux kernel: arch/sh/include/asm/ptrace.h.  */
enum {
    TARGET_REG_PC = 16,
    TARGET_REG_PR = 17,
    TARGET_REG_SR = 18,
    TARGET_REG_GBR = 19,
    TARGET_REG_MACH = 20,
    TARGET_REG_MACL = 21,
    TARGET_REG_SYSCALL = 22
};

static inline void elf_core_copy_regs(target_elf_gregset_t *regs,
                                      const CPUSH4State *env)
{
    int i;

    for (i = 0; i < 16; i++) {
        (*regs)[i] = tswapreg(env->gregs[i]);
    }

    (*regs)[TARGET_REG_PC] = tswapreg(env->pc);
    (*regs)[TARGET_REG_PR] = tswapreg(env->pr);
    (*regs)[TARGET_REG_SR] = tswapreg(env->sr);
    (*regs)[TARGET_REG_GBR] = tswapreg(env->gbr);
    (*regs)[TARGET_REG_MACH] = tswapreg(env->mach);
    (*regs)[TARGET_REG_MACL] = tswapreg(env->macl);
    (*regs)[TARGET_REG_SYSCALL] = 0; /* FIXME */
}

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE        4096

enum {
    SH_CPU_HAS_FPU            = 0x0001, /* Hardware FPU support */
    SH_CPU_HAS_P2_FLUSH_BUG   = 0x0002, /* Need to flush the cache in P2 area */
    SH_CPU_HAS_MMU_PAGE_ASSOC = 0x0004, /* SH3: TLB way selection bit support */
    SH_CPU_HAS_DSP            = 0x0008, /* SH-DSP: DSP support */
    SH_CPU_HAS_PERF_COUNTER   = 0x0010, /* Hardware performance counters */
    SH_CPU_HAS_PTEA           = 0x0020, /* PTEA register */
    SH_CPU_HAS_LLSC           = 0x0040, /* movli.l/movco.l */
    SH_CPU_HAS_L2_CACHE       = 0x0080, /* Secondary cache / URAM */
    SH_CPU_HAS_OP32           = 0x0100, /* 32-bit instruction support */
    SH_CPU_HAS_PTEAEX         = 0x0200, /* PTE ASID Extension support */
};

#define ELF_HWCAP get_elf_hwcap()

static uint32_t get_elf_hwcap(void)
{
    SuperHCPU *cpu = SUPERH_CPU(thread_cpu);
    uint32_t hwcap = 0;

    hwcap |= SH_CPU_HAS_FPU;

    if (cpu->env.features & SH_FEATURE_SH4A) {
        hwcap |= SH_CPU_HAS_LLSC;
    }

    return hwcap;
}

#endif

#ifdef TARGET_CRIS

#define ELF_START_MMAP 0x80000000

#define ELF_CLASS ELFCLASS32
#define ELF_ARCH  EM_CRIS

static inline void init_thread(struct target_pt_regs *regs,
                               struct image_info *infop)
{
    regs->erp = infop->entry;
}

#define ELF_EXEC_PAGESIZE        8192

#endif

#ifdef TARGET_M68K

#define ELF_START_MMAP 0x80000000

#define ELF_CLASS       ELFCLASS32
#define ELF_ARCH        EM_68K

/* ??? Does this need to do anything?
   #define ELF_PLAT_INIT(_r) */

static inline void init_thread(struct target_pt_regs *regs,
                               struct image_info *infop)
{
    regs->usp = infop->start_stack;
    regs->sr = 0;
    regs->pc = infop->entry;
}

/* See linux kernel: arch/m68k/include/asm/elf.h.  */
#define ELF_NREG 20
typedef target_elf_greg_t target_elf_gregset_t[ELF_NREG];

static void elf_core_copy_regs(target_elf_gregset_t *regs, const CPUM68KState *env)
{
    (*regs)[0] = tswapreg(env->dregs[1]);
    (*regs)[1] = tswapreg(env->dregs[2]);
    (*regs)[2] = tswapreg(env->dregs[3]);
    (*regs)[3] = tswapreg(env->dregs[4]);
    (*regs)[4] = tswapreg(env->dregs[5]);
    (*regs)[5] = tswapreg(env->dregs[6]);
    (*regs)[6] = tswapreg(env->dregs[7]);
    (*regs)[7] = tswapreg(env->aregs[0]);
    (*regs)[8] = tswapreg(env->aregs[1]);
    (*regs)[9] = tswapreg(env->aregs[2]);
    (*regs)[10] = tswapreg(env->aregs[3]);
    (*regs)[11] = tswapreg(env->aregs[4]);
    (*regs)[12] = tswapreg(env->aregs[5]);
    (*regs)[13] = tswapreg(env->aregs[6]);
    (*regs)[14] = tswapreg(env->dregs[0]);
    (*regs)[15] = tswapreg(env->aregs[7]);
    (*regs)[16] = tswapreg(env->dregs[0]); /* FIXME: orig_d0 */
    (*regs)[17] = tswapreg(env->sr);
    (*regs)[18] = tswapreg(env->pc);
    (*regs)[19] = 0;  /* FIXME: regs->format | regs->vector */
}

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE       8192

#endif

#ifdef TARGET_ALPHA

#define ELF_START_MMAP (0x30000000000ULL)

#define ELF_CLASS      ELFCLASS64
#define ELF_ARCH       EM_ALPHA

static inline void init_thread(struct target_pt_regs *regs,
                               struct image_info *infop)
{
    regs->pc = infop->entry;
    regs->ps = 8;
    regs->usp = infop->start_stack;
}

#define ELF_EXEC_PAGESIZE        8192

#endif /* TARGET_ALPHA */

#ifdef TARGET_S390X

#define ELF_START_MMAP (0x20000000000ULL)

#define ELF_CLASS	ELFCLASS64
#define ELF_DATA	ELFDATA2MSB
#define ELF_ARCH	EM_S390

#include "elf.h"

#define ELF_HWCAP get_elf_hwcap()

#define GET_FEATURE(_feat, _hwcap) \
    do { if (s390_has_feat(_feat)) { hwcap |= _hwcap; } } while (0)

static uint32_t get_elf_hwcap(void)
{
    /*
     * Let's assume we always have esan3 and zarch.
     * 31-bit processes can use 64-bit registers (high gprs).
     */
    uint32_t hwcap = HWCAP_S390_ESAN3 | HWCAP_S390_ZARCH | HWCAP_S390_HIGH_GPRS;

    GET_FEATURE(S390_FEAT_STFLE, HWCAP_S390_STFLE);
    GET_FEATURE(S390_FEAT_MSA, HWCAP_S390_MSA);
    GET_FEATURE(S390_FEAT_LONG_DISPLACEMENT, HWCAP_S390_LDISP);
    GET_FEATURE(S390_FEAT_EXTENDED_IMMEDIATE, HWCAP_S390_EIMM);
    if (s390_has_feat(S390_FEAT_EXTENDED_TRANSLATION_3) &&
        s390_has_feat(S390_FEAT_ETF3_ENH)) {
        hwcap |= HWCAP_S390_ETF3EH;
    }
    GET_FEATURE(S390_FEAT_VECTOR, HWCAP_S390_VXRS);
    GET_FEATURE(S390_FEAT_VECTOR_ENH, HWCAP_S390_VXRS_EXT);

    return hwcap;
}

static inline void init_thread(struct target_pt_regs *regs, struct image_info *infop)
{
    regs->psw.addr = infop->entry;
    regs->psw.mask = PSW_MASK_64 | PSW_MASK_32;
    regs->gprs[15] = infop->start_stack;
}

/* See linux kernel: arch/s390/include/uapi/asm/ptrace.h (s390_regs).  */
#define ELF_NREG 27
typedef target_elf_greg_t target_elf_gregset_t[ELF_NREG];

enum {
    TARGET_REG_PSWM = 0,
    TARGET_REG_PSWA = 1,
    TARGET_REG_GPRS = 2,
    TARGET_REG_ARS = 18,
    TARGET_REG_ORIG_R2 = 26,
};

static void elf_core_copy_regs(target_elf_gregset_t *regs,
                               const CPUS390XState *env)
{
    int i;
    uint32_t *aregs;

    (*regs)[TARGET_REG_PSWM] = tswapreg(env->psw.mask);
    (*regs)[TARGET_REG_PSWA] = tswapreg(env->psw.addr);
    for (i = 0; i < 16; i++) {
        (*regs)[TARGET_REG_GPRS + i] = tswapreg(env->regs[i]);
    }
    aregs = (uint32_t *)&((*regs)[TARGET_REG_ARS]);
    for (i = 0; i < 16; i++) {
        aregs[i] = tswap32(env->aregs[i]);
    }
    (*regs)[TARGET_REG_ORIG_R2] = 0;
}

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE 4096

#endif /* TARGET_S390X */

#ifdef TARGET_RISCV

#define ELF_START_MMAP 0x80000000
#define ELF_ARCH  EM_RISCV

#ifdef TARGET_RISCV32
#define ELF_CLASS ELFCLASS32
#else
#define ELF_CLASS ELFCLASS64
#endif

#define ELF_HWCAP get_elf_hwcap()

static uint32_t get_elf_hwcap(void)
{
#define MISA_BIT(EXT) (1 << (EXT - 'A'))
    RISCVCPU *cpu = RISCV_CPU(thread_cpu);
    uint32_t mask = MISA_BIT('I') | MISA_BIT('M') | MISA_BIT('A')
                    | MISA_BIT('F') | MISA_BIT('D') | MISA_BIT('C');

    return cpu->env.misa_ext & mask;
#undef MISA_BIT
}

static inline void init_thread(struct target_pt_regs *regs,
                               struct image_info *infop)
{
    regs->sepc = infop->entry;
    regs->sp = infop->start_stack;
}

#define ELF_EXEC_PAGESIZE 4096

#endif /* TARGET_RISCV */

#ifdef TARGET_HPPA

#define ELF_START_MMAP  0x80000000
#define ELF_CLASS       ELFCLASS32
#define ELF_ARCH        EM_PARISC
#define ELF_PLATFORM    "PARISC"
#define STACK_GROWS_DOWN 0
#define STACK_ALIGNMENT  64

static inline void init_thread(struct target_pt_regs *regs,
                               struct image_info *infop)
{
    regs->iaoq[0] = infop->entry;
    regs->iaoq[1] = infop->entry + 4;
    regs->gr[23] = 0;
    regs->gr[24] = infop->argv;
    regs->gr[25] = infop->argc;
    /* The top-of-stack contains a linkage buffer.  */
    regs->gr[30] = infop->start_stack + 64;
    regs->gr[31] = infop->entry;
}

#define LO_COMMPAGE  0

static bool init_guest_commpage(void)
{
    void *want = g2h_untagged(LO_COMMPAGE);
    void *addr = mmap(want, qemu_host_page_size, PROT_NONE,
                      MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED, -1, 0);

    if (addr == MAP_FAILED) {
        perror("Allocating guest commpage");
        exit(EXIT_FAILURE);
    }
    if (addr != want) {
        return false;
    }

    /*
     * On Linux, page zero is normally marked execute only + gateway.
     * Normal read or write is supposed to fail (thus PROT_NONE above),
     * but specific offsets have kernel code mapped to raise permissions
     * and implement syscalls.  Here, simply mark the page executable.
     * Special case the entry points during translation (see do_page_zero).
     */
    page_set_flags(LO_COMMPAGE, LO_COMMPAGE | ~TARGET_PAGE_MASK,
                   PAGE_EXEC | PAGE_VALID);
    return true;
}

#endif /* TARGET_HPPA */

#ifdef TARGET_XTENSA

#define ELF_START_MMAP 0x20000000

#define ELF_CLASS       ELFCLASS32
#define ELF_ARCH        EM_XTENSA

static inline void init_thread(struct target_pt_regs *regs,
                               struct image_info *infop)
{
    regs->windowbase = 0;
    regs->windowstart = 1;
    regs->areg[1] = infop->start_stack;
    regs->pc = infop->entry;
    if (info_is_fdpic(infop)) {
        regs->areg[4] = infop->loadmap_addr;
        regs->areg[5] = infop->interpreter_loadmap_addr;
        if (infop->interpreter_loadmap_addr) {
            regs->areg[6] = infop->interpreter_pt_dynamic_addr;
        } else {
            regs->areg[6] = infop->pt_dynamic_addr;
        }
    }
}

/* See linux kernel: arch/xtensa/include/asm/elf.h.  */
#define ELF_NREG 128
typedef target_elf_greg_t target_elf_gregset_t[ELF_NREG];

enum {
    TARGET_REG_PC,
    TARGET_REG_PS,
    TARGET_REG_LBEG,
    TARGET_REG_LEND,
    TARGET_REG_LCOUNT,
    TARGET_REG_SAR,
    TARGET_REG_WINDOWSTART,
    TARGET_REG_WINDOWBASE,
    TARGET_REG_THREADPTR,
    TARGET_REG_AR0 = 64,
};

static void elf_core_copy_regs(target_elf_gregset_t *regs,
                               const CPUXtensaState *env)
{
    unsigned i;

    (*regs)[TARGET_REG_PC] = tswapreg(env->pc);
    (*regs)[TARGET_REG_PS] = tswapreg(env->sregs[PS] & ~PS_EXCM);
    (*regs)[TARGET_REG_LBEG] = tswapreg(env->sregs[LBEG]);
    (*regs)[TARGET_REG_LEND] = tswapreg(env->sregs[LEND]);
    (*regs)[TARGET_REG_LCOUNT] = tswapreg(env->sregs[LCOUNT]);
    (*regs)[TARGET_REG_SAR] = tswapreg(env->sregs[SAR]);
    (*regs)[TARGET_REG_WINDOWSTART] = tswapreg(env->sregs[WINDOW_START]);
    (*regs)[TARGET_REG_WINDOWBASE] = tswapreg(env->sregs[WINDOW_BASE]);
    (*regs)[TARGET_REG_THREADPTR] = tswapreg(env->uregs[THREADPTR]);
    xtensa_sync_phys_from_window((CPUXtensaState *)env);
    for (i = 0; i < env->config->nareg; ++i) {
        (*regs)[TARGET_REG_AR0 + i] = tswapreg(env->phys_regs[i]);
    }
}

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE       4096

#endif /* TARGET_XTENSA */

#ifdef TARGET_HEXAGON

#define ELF_START_MMAP 0x20000000

#define ELF_CLASS       ELFCLASS32
#define ELF_ARCH        EM_HEXAGON

static inline void init_thread(struct target_pt_regs *regs,
                               struct image_info *infop)
{
    regs->sepc = infop->entry;
    regs->sp = infop->start_stack;
}

#endif /* TARGET_HEXAGON */

#ifndef ELF_BASE_PLATFORM
#define ELF_BASE_PLATFORM (NULL)
#endif

#ifndef ELF_PLATFORM
#define ELF_PLATFORM (NULL)
#endif

#ifndef ELF_MACHINE
#define ELF_MACHINE ELF_ARCH
#endif

#ifndef elf_check_arch
#define elf_check_arch(x) ((x) == ELF_ARCH)
#endif

#ifndef elf_check_abi
#define elf_check_abi(x) (1)
#endif

#ifndef ELF_HWCAP
#define ELF_HWCAP 0
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

#include "elf.h"

/* We must delay the following stanzas until after "elf.h". */
#if defined(TARGET_AARCH64)

static bool arch_parse_elf_property(uint32_t pr_type, uint32_t pr_datasz,
                                    const uint32_t *data,
                                    struct image_info *info,
                                    Error **errp)
{
    if (pr_type == GNU_PROPERTY_AARCH64_FEATURE_1_AND) {
        if (pr_datasz != sizeof(uint32_t)) {
            error_setg(errp, "Ill-formed GNU_PROPERTY_AARCH64_FEATURE_1_AND");
            return false;
        }
        /* We will extract GNU_PROPERTY_AARCH64_FEATURE_1_BTI later. */
        info->note_flags = *data;
    }
    return true;
}
#define ARCH_USE_GNU_PROPERTY 1

#else

static bool arch_parse_elf_property(uint32_t pr_type, uint32_t pr_datasz,
                                    const uint32_t *data,
                                    struct image_info *info,
                                    Error **errp)
{
    g_assert_not_reached();
}
#define ARCH_USE_GNU_PROPERTY 0

#endif

struct exec
{
    unsigned int a_info;   /* Use macros N_MAGIC, etc for access */
    unsigned int a_text;   /* length of text, in bytes */
    unsigned int a_data;   /* length of data, in bytes */
    unsigned int a_bss;    /* length of uninitialized data area, in bytes */
    unsigned int a_syms;   /* length of symbol table data in file, in bytes */
    unsigned int a_entry;  /* start address */
    unsigned int a_trsize; /* length of relocation info for text, in bytes */
    unsigned int a_drsize; /* length of relocation info for data, in bytes */
};


#define N_MAGIC(exec) ((exec).a_info & 0xffff)
#define OMAGIC 0407
#define NMAGIC 0410
#define ZMAGIC 0413
#define QMAGIC 0314

/* Necessary parameters */
#define TARGET_ELF_EXEC_PAGESIZE \
        (((eppnt->p_align & ~qemu_host_page_mask) != 0) ? \
         TARGET_PAGE_SIZE : MAX(qemu_host_page_size, TARGET_PAGE_SIZE))
#define TARGET_ELF_PAGELENGTH(_v) ROUND_UP((_v), TARGET_ELF_EXEC_PAGESIZE)
#define TARGET_ELF_PAGESTART(_v) ((_v) & \
                                 ~(abi_ulong)(TARGET_ELF_EXEC_PAGESIZE-1))
#define TARGET_ELF_PAGEOFFSET(_v) ((_v) & (TARGET_ELF_EXEC_PAGESIZE-1))

#define DLINFO_ITEMS 16

static inline void memcpy_fromfs(void * to, const void * from, unsigned long n)
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
    for (i = 0; i < phnum; ++i, ++phdr) {
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
    for (i = 0; i < shnum; ++i, ++shdr) {
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

#ifdef TARGET_MIPS
static void bswap_mips_abiflags(Mips_elf_abiflags_v0 *abiflags)
{
    bswap16s(&abiflags->version);
    bswap32s(&abiflags->ases);
    bswap32s(&abiflags->isa_ext);
    bswap32s(&abiflags->flags1);
    bswap32s(&abiflags->flags2);
}
#endif
#else
static inline void bswap_ehdr(struct elfhdr *ehdr) { }
static inline void bswap_phdr(struct elf_phdr *phdr, int phnum) { }
static inline void bswap_shdr(struct elf_shdr *shdr, int shnum) { }
static inline void bswap_sym(struct elf_sym *sym) { }
#ifdef TARGET_MIPS
static inline void bswap_mips_abiflags(Mips_elf_abiflags_v0 *abiflags) { }
#endif
#endif

#ifdef USE_ELF_CORE_DUMP
static int elf_core_dump(int, const CPUArchState *);
#endif /* USE_ELF_CORE_DUMP */
static void load_symbols(struct elfhdr *hdr, int fd, abi_ulong load_bias);

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
    return (elf_check_arch(ehdr->e_machine)
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

/* Map and zero the bss.  We need to explicitly zero any fractional pages
   after the data section (i.e. bss).  */
static void zero_bss(abi_ulong elf_bss, abi_ulong last_bss, int prot)
{
    uintptr_t host_start, host_map_start, host_end;

    last_bss = TARGET_PAGE_ALIGN(last_bss);

    /* ??? There is confusion between qemu_real_host_page_size and
       qemu_host_page_size here and elsewhere in target_mmap, which
       may lead to the end of the data section mapping from the file
       not being mapped.  At least there was an explicit test and
       comment for that here, suggesting that "the file size must
       be known".  The comment probably pre-dates the introduction
       of the fstat system call in target_mmap which does in fact
       find out the size.  What isn't clear is if the workaround
       here is still actually needed.  For now, continue with it,
       but merge it with the "normal" mmap that would allocate the bss.  */

    host_start = (uintptr_t) g2h_untagged(elf_bss);
    host_end = (uintptr_t) g2h_untagged(last_bss);
    host_map_start = REAL_HOST_PAGE_ALIGN(host_start);

    if (host_map_start < host_end) {
        void *p = mmap((void *)host_map_start, host_end - host_map_start,
                       prot, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) {
            perror("cannot mmap brk");
            exit(-1);
        }
    }

    /* Ensure that the bss page(s) are valid */
    if ((page_get_flags(last_bss-1) & prot) != prot) {
        page_set_flags(elf_bss & TARGET_PAGE_MASK, last_bss - 1,
                       prot | PAGE_VALID);
    }

    if (host_start < host_map_start) {
        memset((void *)host_start, 0, host_map_start - host_start);
    }
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
                                   struct image_info *interp_info)
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
    k_base_platform = ELF_BASE_PLATFORM;
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
    k_platform = ELF_PLATFORM;
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
    if (k_base_platform)
        size += 2;
    if (k_platform)
        size += 2;
#ifdef DLINFO_ARCH_ITEMS
    size += DLINFO_ARCH_ITEMS * 2;
#endif
#ifdef ELF_HWCAP2
    size += 2;
#endif
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
    if ((info->alignment & ~qemu_host_page_mask) != 0) {
        /* Target doesn't support host page size alignment */
        NEW_AUX_ENT(AT_PAGESZ, (abi_ulong)(TARGET_PAGE_SIZE));
    } else {
        NEW_AUX_ENT(AT_PAGESZ, (abi_ulong)(MAX(TARGET_PAGE_SIZE,
                                               qemu_host_page_size)));
    }
    NEW_AUX_ENT(AT_BASE, (abi_ulong)(interp_info ? interp_info->load_addr : 0));
    NEW_AUX_ENT(AT_FLAGS, (abi_ulong)0);
    NEW_AUX_ENT(AT_ENTRY, info->entry);
    NEW_AUX_ENT(AT_UID, (abi_ulong) getuid());
    NEW_AUX_ENT(AT_EUID, (abi_ulong) geteuid());
    NEW_AUX_ENT(AT_GID, (abi_ulong) getgid());
    NEW_AUX_ENT(AT_EGID, (abi_ulong) getegid());
    NEW_AUX_ENT(AT_HWCAP, (abi_ulong) ELF_HWCAP);
    NEW_AUX_ENT(AT_CLKTCK, (abi_ulong) sysconf(_SC_CLK_TCK));
    NEW_AUX_ENT(AT_RANDOM, (abi_ulong) u_rand_bytes);
    NEW_AUX_ENT(AT_SECURE, (abi_ulong) qemu_getauxval(AT_SECURE));
    NEW_AUX_ENT(AT_EXECFN, info->file_string);

#ifdef ELF_HWCAP2
    NEW_AUX_ENT(AT_HWCAP2, (abi_ulong) ELF_HWCAP2);
#endif

    if (u_base_platform) {
        NEW_AUX_ENT(AT_BASE_PLATFORM, u_base_platform);
    }
    if (u_platform) {
        NEW_AUX_ENT(AT_PLATFORM, u_platform);
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
#ifndef INIT_GUEST_COMMPAGE
#define init_guest_commpage() true
#endif
#endif

static void pgb_fail_in_use(const char *image_name)
{
    error_report("%s: requires virtual address space that is in use "
                 "(omit the -B option or choose a different value)",
                 image_name);
    exit(EXIT_FAILURE);
}

static void pgb_have_guest_base(const char *image_name, abi_ulong guest_loaddr,
                                abi_ulong guest_hiaddr, long align)
{
    const int flags = MAP_ANONYMOUS | MAP_PRIVATE | MAP_NORESERVE;
    void *addr, *test;

    if (!QEMU_IS_ALIGNED(guest_base, align)) {
        fprintf(stderr, "Requested guest base %p does not satisfy "
                "host minimum alignment (0x%lx)\n",
                (void *)guest_base, align);
        exit(EXIT_FAILURE);
    }

    /* Sanity check the guest binary. */
    if (reserved_va) {
        if (guest_hiaddr > reserved_va) {
            error_report("%s: requires more than reserved virtual "
                         "address space (0x%" PRIx64 " > 0x%lx)",
                         image_name, (uint64_t)guest_hiaddr, reserved_va);
            exit(EXIT_FAILURE);
        }
    } else {
#if HOST_LONG_BITS < TARGET_ABI_BITS
        if ((guest_hiaddr - guest_base) > ~(uintptr_t)0) {
            error_report("%s: requires more virtual address space "
                         "than the host can provide (0x%" PRIx64 ")",
                         image_name, (uint64_t)guest_hiaddr + 1 - guest_base);
            exit(EXIT_FAILURE);
        }
#endif
    }

    /*
     * Expand the allocation to the entire reserved_va.
     * Exclude the mmap_min_addr hole.
     */
    if (reserved_va) {
        guest_loaddr = (guest_base >= mmap_min_addr ? 0
                        : mmap_min_addr - guest_base);
        guest_hiaddr = reserved_va;
    }

    /* Reserve the address space for the binary, or reserved_va. */
    test = g2h_untagged(guest_loaddr);
    addr = mmap(test, guest_hiaddr - guest_loaddr + 1, PROT_NONE, flags, -1, 0);
    if (test != addr) {
        pgb_fail_in_use(image_name);
    }
    qemu_log_mask(CPU_LOG_PAGE,
                  "%s: base @ %p for %" PRIu64 " bytes\n",
                  __func__, addr, (uint64_t)guest_hiaddr - guest_loaddr + 1);
}

/**
 * pgd_find_hole_fallback: potential mmap address
 * @guest_size: size of available space
 * @brk: location of break
 * @align: memory alignment
 *
 * This is a fallback method for finding a hole in the host address
 * space if we don't have the benefit of being able to access
 * /proc/self/map. It can potentially take a very long time as we can
 * only dumbly iterate up the host address space seeing if the
 * allocation would work.
 */
static uintptr_t pgd_find_hole_fallback(uintptr_t guest_size, uintptr_t brk,
                                        long align, uintptr_t offset)
{
    uintptr_t base;

    /* Start (aligned) at the bottom and work our way up */
    base = ROUND_UP(mmap_min_addr, align);

    while (true) {
        uintptr_t align_start, end;
        align_start = ROUND_UP(base, align);
        end = align_start + guest_size + offset;

        /* if brk is anywhere in the range give ourselves some room to grow. */
        if (align_start <= brk && brk < end) {
            base = brk + (16 * MiB);
            continue;
        } else if (align_start + guest_size < align_start) {
            /* we have run out of space */
            return -1;
        } else {
            int flags = MAP_ANONYMOUS | MAP_PRIVATE | MAP_NORESERVE |
                MAP_FIXED_NOREPLACE;
            void * mmap_start = mmap((void *) align_start, guest_size,
                                     PROT_NONE, flags, -1, 0);
            if (mmap_start != MAP_FAILED) {
                munmap(mmap_start, guest_size);
                if (mmap_start == (void *) align_start) {
                    qemu_log_mask(CPU_LOG_PAGE,
                                  "%s: base @ %p for %" PRIdPTR" bytes\n",
                                  __func__, mmap_start + offset, guest_size);
                    return (uintptr_t) mmap_start + offset;
                }
            }
            base += qemu_host_page_size;
        }
    }
}

/* Return value for guest_base, or -1 if no hole found. */
static uintptr_t pgb_find_hole(uintptr_t guest_loaddr, uintptr_t guest_size,
                               long align, uintptr_t offset)
{
    GSList *maps, *iter;
    uintptr_t this_start, this_end, next_start, brk;
    intptr_t ret = -1;

    assert(QEMU_IS_ALIGNED(guest_loaddr, align));

    maps = read_self_maps();

    /* Read brk after we've read the maps, which will malloc. */
    brk = (uintptr_t)sbrk(0);

    if (!maps) {
        return pgd_find_hole_fallback(guest_size, brk, align, offset);
    }

    /* The first hole is before the first map entry. */
    this_start = mmap_min_addr;

    for (iter = maps; iter;
         this_start = next_start, iter = g_slist_next(iter)) {
        uintptr_t align_start, hole_size;

        this_end = ((MapInfo *)iter->data)->start;
        next_start = ((MapInfo *)iter->data)->end;
        align_start = ROUND_UP(this_start + offset, align);

        /* Skip holes that are too small. */
        if (align_start >= this_end) {
            continue;
        }
        hole_size = this_end - align_start;
        if (hole_size < guest_size) {
            continue;
        }

        /* If this hole contains brk, give ourselves some room to grow. */
        if (this_start <= brk && brk < this_end) {
            hole_size -= guest_size;
            if (sizeof(uintptr_t) == 8 && hole_size >= 1 * GiB) {
                align_start += 1 * GiB;
            } else if (hole_size >= 16 * MiB) {
                align_start += 16 * MiB;
            } else {
                align_start = (this_end - guest_size) & -align;
                if (align_start < this_start) {
                    continue;
                }
            }
        }

        /* Record the lowest successful match. */
        if (ret < 0) {
            ret = align_start;
        }
        /* If this hole contains the identity map, select it. */
        if (align_start <= guest_loaddr &&
            guest_loaddr + guest_size <= this_end) {
            ret = 0;
        }
        /* If this hole ends above the identity map, stop looking. */
        if (this_end >= guest_loaddr) {
            break;
        }
    }
    free_self_maps(maps);

    if (ret != -1) {
        qemu_log_mask(CPU_LOG_PAGE, "%s: base @ %" PRIxPTR
                      " for %" PRIuPTR " bytes\n",
                      __func__, ret, guest_size);
    }

    return ret;
}

static void pgb_static(const char *image_name, abi_ulong orig_loaddr,
                       abi_ulong orig_hiaddr, long align)
{
    uintptr_t loaddr = orig_loaddr;
    uintptr_t hiaddr = orig_hiaddr;
    uintptr_t offset = 0;
    uintptr_t addr;

    if (hiaddr != orig_hiaddr) {
        error_report("%s: requires virtual address space that the "
                     "host cannot provide (0x%" PRIx64 ")",
                     image_name, (uint64_t)orig_hiaddr + 1);
        exit(EXIT_FAILURE);
    }

    loaddr &= -align;
    if (HI_COMMPAGE) {
        /*
         * Extend the allocation to include the commpage.
         * For a 64-bit host, this is just 4GiB; for a 32-bit host we
         * need to ensure there is space bellow the guest_base so we
         * can map the commpage in the place needed when the address
         * arithmetic wraps around.
         */
        if (sizeof(uintptr_t) == 8 || loaddr >= 0x80000000u) {
            hiaddr = UINT32_MAX;
        } else {
            offset = -(HI_COMMPAGE & -align);
        }
    } else if (LO_COMMPAGE != -1) {
        loaddr = MIN(loaddr, LO_COMMPAGE & -align);
    }

    addr = pgb_find_hole(loaddr, hiaddr - loaddr + 1, align, offset);
    if (addr == -1) {
        /*
         * If HI_COMMPAGE, there *might* be a non-consecutive allocation
         * that can satisfy both.  But as the normal arm32 link base address
         * is ~32k, and we extend down to include the commpage, making the
         * overhead only ~96k, this is unlikely.
         */
        error_report("%s: Unable to allocate %#zx bytes of "
                     "virtual address space", image_name,
                     (size_t)(hiaddr - loaddr));
        exit(EXIT_FAILURE);
    }

    guest_base = addr;

    qemu_log_mask(CPU_LOG_PAGE, "%s: base @ %"PRIxPTR" for %" PRIuPTR" bytes\n",
                  __func__, addr, hiaddr - loaddr);
}

static void pgb_dynamic(const char *image_name, long align)
{
    /*
     * The executable is dynamic and does not require a fixed address.
     * All we need is a commpage that satisfies align.
     * If we do not need a commpage, leave guest_base == 0.
     */
    if (HI_COMMPAGE) {
        uintptr_t addr, commpage;

        /* 64-bit hosts should have used reserved_va. */
        assert(sizeof(uintptr_t) == 4);

        /*
         * By putting the commpage at the first hole, that puts guest_base
         * just above that, and maximises the positive guest addresses.
         */
        commpage = HI_COMMPAGE & -align;
        addr = pgb_find_hole(commpage, -commpage, align, 0);
        assert(addr != -1);
        guest_base = addr;
    }
}

static void pgb_reserved_va(const char *image_name, abi_ulong guest_loaddr,
                            abi_ulong guest_hiaddr, long align)
{
    int flags = MAP_ANONYMOUS | MAP_PRIVATE | MAP_NORESERVE;
    void *addr, *test;

    if (guest_hiaddr > reserved_va) {
        error_report("%s: requires more than reserved virtual "
                     "address space (0x%" PRIx64 " > 0x%lx)",
                     image_name, (uint64_t)guest_hiaddr, reserved_va);
        exit(EXIT_FAILURE);
    }

    /* Widen the "image" to the entire reserved address space. */
    pgb_static(image_name, 0, reserved_va, align);

    /* osdep.h defines this as 0 if it's missing */
    flags |= MAP_FIXED_NOREPLACE;

    /* Reserve the memory on the host. */
    assert(guest_base != 0);
    test = g2h_untagged(0);
    addr = mmap(test, reserved_va + 1, PROT_NONE, flags, -1, 0);
    if (addr == MAP_FAILED || addr != test) {
        error_report("Unable to reserve 0x%lx bytes of virtual address "
                     "space at %p (%s) for use as guest address space (check your "
                     "virtual memory ulimit setting, min_mmap_addr or reserve less "
                     "using -R option)", reserved_va + 1, test, strerror(errno));
        exit(EXIT_FAILURE);
    }

    qemu_log_mask(CPU_LOG_PAGE, "%s: base @ %p for %lu bytes\n",
                  __func__, addr, reserved_va + 1);
}

void probe_guest_base(const char *image_name, abi_ulong guest_loaddr,
                      abi_ulong guest_hiaddr)
{
    /* In order to use host shmat, we must be able to honor SHMLBA.  */
    uintptr_t align = MAX(SHMLBA, qemu_host_page_size);

    if (have_guest_base) {
        pgb_have_guest_base(image_name, guest_loaddr, guest_hiaddr, align);
    } else if (reserved_va) {
        pgb_reserved_va(image_name, guest_loaddr, guest_hiaddr, align);
    } else if (guest_loaddr) {
        pgb_static(image_name, guest_loaddr, guest_hiaddr, align);
    } else {
        pgb_dynamic(image_name, align);
    }

    /* Reserve and initialize the commpage. */
    if (!init_guest_commpage()) {
        /*
         * With have_guest_base, the user has selected the address and
         * we are trying to work with that.  Otherwise, we have selected
         * free space and init_guest_commpage must succeeded.
         */
        assert(have_guest_base);
        pgb_fail_in_use(image_name);
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
static bool parse_elf_properties(int image_fd,
                                 struct image_info *info,
                                 const struct elf_phdr *phdr,
                                 char bprm_buf[BPRM_BUF_SIZE],
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
    if (!ARCH_USE_GNU_PROPERTY) {
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

    if (phdr->p_offset + n <= BPRM_BUF_SIZE) {
        memcpy(&note, bprm_buf + phdr->p_offset, n);
    } else {
        ssize_t len = pread(image_fd, &note, n, phdr->p_offset);
        if (len != n) {
            error_setg_errno(errp, errno, "Error reading file header");
            return false;
        }
    }

    /*
     * The contents of a valid PT_GNU_PROPERTY is a sequence
     * of uint32_t -- swap them all now.
     */
#ifdef BSWAP_NEEDED
    for (int i = 0; i < n / 4; i++) {
        bswap32s(note.data + i);
    }
#endif

    /*
     * Note that nhdr is 3 words, and that the "name" described by namesz
     * immediately follows nhdr and is thus at the 4th word.  Further, all
     * of the inputs to the kernel's round_up are multiples of 4.
     */
    if (note.nhdr.n_type != NT_GNU_PROPERTY_TYPE_0 ||
        note.nhdr.n_namesz != NOTE_NAME_SZ ||
        note.data[3] != GNU0_MAGIC) {
        error_setg(errp, "Invalid note in PT_GNU_PROPERTY");
        return false;
    }
    off = sizeof(note.nhdr) + NOTE_NAME_SZ;

    datasz = note.nhdr.n_descsz + off;
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

/* Load an ELF image into the address space.

   IMAGE_NAME is the filename of the image, to use in error messages.
   IMAGE_FD is the open file descriptor for the image.

   BPRM_BUF is a copy of the beginning of the file; this of course
   contains the elf file header at offset 0.  It is assumed that this
   buffer is sufficiently aligned to present no problems to the host
   in accessing data at aligned offsets within the buffer.

   On return: INFO values will be filled in, as necessary or available.  */

static void load_elf_image(const char *image_name, int image_fd,
                           struct image_info *info, char **pinterp_name,
                           char bprm_buf[BPRM_BUF_SIZE])
{
    struct elfhdr *ehdr = (struct elfhdr *)bprm_buf;
    struct elf_phdr *phdr;
    abi_ulong load_addr, load_bias, loaddr, hiaddr, error;
    int i, retval, prot_exec;
    Error *err = NULL;

    /* First of all, some simple consistency checks */
    if (!elf_check_ident(ehdr)) {
        error_setg(&err, "Invalid ELF image for this architecture");
        goto exit_errmsg;
    }
    bswap_ehdr(ehdr);
    if (!elf_check_ehdr(ehdr)) {
        error_setg(&err, "Invalid ELF image for this architecture");
        goto exit_errmsg;
    }

    i = ehdr->e_phnum * sizeof(struct elf_phdr);
    if (ehdr->e_phoff + i <= BPRM_BUF_SIZE) {
        phdr = (struct elf_phdr *)(bprm_buf + ehdr->e_phoff);
    } else {
        phdr = (struct elf_phdr *) alloca(i);
        retval = pread(image_fd, phdr, i, ehdr->e_phoff);
        if (retval != i) {
            goto exit_read;
        }
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
    info->alignment = 0;
    info->exec_stack = EXSTACK_DEFAULT;
    for (i = 0; i < ehdr->e_phnum; ++i) {
        struct elf_phdr *eppnt = phdr + i;
        if (eppnt->p_type == PT_LOAD) {
            abi_ulong a = eppnt->p_vaddr - eppnt->p_offset;
            if (a < loaddr) {
                loaddr = a;
            }
            a = eppnt->p_vaddr + eppnt->p_memsz - 1;
            if (a > hiaddr) {
                hiaddr = a;
            }
            ++info->nsegs;
            info->alignment |= eppnt->p_align;
        } else if (eppnt->p_type == PT_INTERP && pinterp_name) {
            g_autofree char *interp_name = NULL;

            if (*pinterp_name) {
                error_setg(&err, "Multiple PT_INTERP entries");
                goto exit_errmsg;
            }

            interp_name = g_malloc(eppnt->p_filesz);

            if (eppnt->p_offset + eppnt->p_filesz <= BPRM_BUF_SIZE) {
                memcpy(interp_name, bprm_buf + eppnt->p_offset,
                       eppnt->p_filesz);
            } else {
                retval = pread(image_fd, interp_name, eppnt->p_filesz,
                               eppnt->p_offset);
                if (retval != eppnt->p_filesz) {
                    goto exit_read;
                }
            }
            if (interp_name[eppnt->p_filesz - 1] != 0) {
                error_setg(&err, "Invalid PT_INTERP entry");
                goto exit_errmsg;
            }
            *pinterp_name = g_steal_pointer(&interp_name);
        } else if (eppnt->p_type == PT_GNU_PROPERTY) {
            if (!parse_elf_properties(image_fd, info, eppnt, bprm_buf, &err)) {
                goto exit_errmsg;
            }
        } else if (eppnt->p_type == PT_GNU_STACK) {
            info->exec_stack = eppnt->p_flags & PF_X;
        }
    }

    if (pinterp_name != NULL) {
        /*
         * This is the main executable.
         *
         * Reserve extra space for brk.
         * We hold on to this space while placing the interpreter
         * and the stack, lest they be placed immediately after
         * the data segment and block allocation from the brk.
         *
         * 16MB is chosen as "large enough" without being so large as
         * to allow the result to not fit with a 32-bit guest on a
         * 32-bit host. However some 64 bit guests (e.g. s390x)
         * attempt to place their heap further ahead and currently
         * nothing stops them smashing into QEMUs address space.
         */
#if TARGET_LONG_BITS == 64
        info->reserve_brk = 32 * MiB;
#else
        info->reserve_brk = 16 * MiB;
#endif
        hiaddr += info->reserve_brk;

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
        }
    }

    /*
     * Reserve address space for all of this.
     *
     * In the case of ET_EXEC, we supply MAP_FIXED so that we get
     * exactly the address range that is required.
     *
     * Otherwise this is ET_DYN, and we are searching for a location
     * that can hold the memory space required.  If the image is
     * pre-linked, LOADDR will be non-zero, and the kernel should
     * honor that address if it happens to be free.
     *
     * In both cases, we will overwrite pages in this range with mappings
     * from the executable.
     */
    load_addr = target_mmap(loaddr, (size_t)hiaddr - loaddr + 1, PROT_NONE,
                            MAP_PRIVATE | MAP_ANON | MAP_NORESERVE |
                            (ehdr->e_type == ET_EXEC ? MAP_FIXED : 0),
                            -1, 0);
    if (load_addr == -1) {
        goto exit_mmap;
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
    info->brk = 0;
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
            abi_ulong vaddr, vaddr_po, vaddr_ps, vaddr_ef, vaddr_em, vaddr_len;
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
            vaddr_po = TARGET_ELF_PAGEOFFSET(vaddr);
            vaddr_ps = TARGET_ELF_PAGESTART(vaddr);

            vaddr_ef = vaddr + eppnt->p_filesz;
            vaddr_em = vaddr + eppnt->p_memsz;

            /*
             * Some segments may be completely empty, with a non-zero p_memsz
             * but no backing file segment.
             */
            if (eppnt->p_filesz != 0) {
                vaddr_len = TARGET_ELF_PAGELENGTH(eppnt->p_filesz + vaddr_po);
                error = target_mmap(vaddr_ps, vaddr_len, elf_prot,
                                    MAP_PRIVATE | MAP_FIXED,
                                    image_fd, eppnt->p_offset - vaddr_po);

                if (error == -1) {
                    goto exit_mmap;
                }

                /*
                 * If the load segment requests extra zeros (e.g. bss), map it.
                 */
                if (eppnt->p_filesz < eppnt->p_memsz) {
                    zero_bss(vaddr_ef, vaddr_em, elf_prot);
                }
            } else if (eppnt->p_memsz != 0) {
                vaddr_len = TARGET_ELF_PAGELENGTH(eppnt->p_memsz + vaddr_po);
                error = target_mmap(vaddr_ps, vaddr_len, elf_prot,
                                    MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS,
                                    -1, 0);

                if (error == -1) {
                    goto exit_mmap;
                }
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
            if (vaddr_em > info->brk) {
                info->brk = vaddr_em;
            }
#ifdef TARGET_MIPS
        } else if (eppnt->p_type == PT_MIPS_ABIFLAGS) {
            Mips_elf_abiflags_v0 abiflags;
            if (eppnt->p_filesz < sizeof(Mips_elf_abiflags_v0)) {
                error_setg(&err, "Invalid PT_MIPS_ABIFLAGS entry");
                goto exit_errmsg;
            }
            if (eppnt->p_offset + eppnt->p_filesz <= BPRM_BUF_SIZE) {
                memcpy(&abiflags, bprm_buf + eppnt->p_offset,
                       sizeof(Mips_elf_abiflags_v0));
            } else {
                retval = pread(image_fd, &abiflags, sizeof(Mips_elf_abiflags_v0),
                               eppnt->p_offset);
                if (retval != sizeof(Mips_elf_abiflags_v0)) {
                    goto exit_read;
                }
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
        load_symbols(ehdr, image_fd, load_bias);
    }

    debuginfo_report_elf(image_name, image_fd, load_bias);

    mmap_unlock();

    close(image_fd);
    return;

 exit_read:
    if (retval >= 0) {
        error_setg(&err, "Incomplete read of file header");
    } else {
        error_setg_errno(&err, errno, "Error reading file header");
    }
    goto exit_errmsg;
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

    if (retval < BPRM_BUF_SIZE) {
        memset(bprm_buf + retval, 0, BPRM_BUF_SIZE - retval);
    }

    load_elf_image(filename, fd, info, NULL, bprm_buf);
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

/* FIXME: This should use elf_ops.h  */
static int symcmp(const void *s0, const void *s1)
{
    struct elf_sym *sym0 = (struct elf_sym *)s0;
    struct elf_sym *sym1 = (struct elf_sym *)s1;
    return (sym0->st_value < sym1->st_value)
        ? -1
        : ((sym0->st_value > sym1->st_value) ? 1 : 0);
}

/* Best attempt to load symbols from this ELF object. */
static void load_symbols(struct elfhdr *hdr, int fd, abi_ulong load_bias)
{
    int i, shnum, nsyms, sym_idx = 0, str_idx = 0;
    uint64_t segsz;
    struct elf_shdr *shdr;
    char *strings = NULL;
    struct syminfo *s = NULL;
    struct elf_sym *new_syms, *syms = NULL;

    shnum = hdr->e_shnum;
    i = shnum * sizeof(struct elf_shdr);
    shdr = (struct elf_shdr *)alloca(i);
    if (pread(fd, shdr, i, hdr->e_shoff) != i) {
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
    s = g_try_new(struct syminfo, 1);
    if (!s) {
        goto give_up;
    }

    segsz = shdr[str_idx].sh_size;
    s->disas_strtab = strings = g_try_malloc(segsz);
    if (!strings ||
        pread(fd, strings, segsz, shdr[str_idx].sh_offset) != segsz) {
        goto give_up;
    }

    segsz = shdr[sym_idx].sh_size;
    syms = g_try_malloc(segsz);
    if (!syms || pread(fd, syms, segsz, shdr[sym_idx].sh_offset) != segsz) {
        goto give_up;
    }

    if (segsz / sizeof(struct elf_sym) > INT_MAX) {
        /* Implausibly large symbol table: give up rather than ploughing
         * on with the number of symbols calculation overflowing
         */
        goto give_up;
    }
    nsyms = segsz / sizeof(struct elf_sym);
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

    /* Attempt to free the storage associated with the local symbols
       that we threw away.  Whether or not this has any effect on the
       memory allocation depends on the malloc implementation and how
       many symbols we managed to discard.  */
    new_syms = g_try_renew(struct elf_sym, syms, nsyms);
    if (new_syms == NULL) {
        goto give_up;
    }
    syms = new_syms;

    qsort(syms, nsyms, sizeof(*syms), symcmp);

    s->disas_num_syms = nsyms;
#if ELF_CLASS == ELFCLASS32
    s->disas_symtab.elf32 = syms;
#else
    s->disas_symtab.elf64 = syms;
#endif
    s->lookup_symbol = lookup_symbolxx;
    s->next = syminfos;
    syminfos = s;

    return;

give_up:
    g_free(s);
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
    struct image_info interp_info;
    struct elfhdr elf_ex;
    char *elf_interpreter = NULL;
    char *scratch;

    memset(&interp_info, 0, sizeof(interp_info));
#ifdef TARGET_MIPS
    interp_info.fp_abi = MIPS_ABI_FP_UNKNOWN;
#endif

    info->start_mmap = (abi_ulong)ELF_START_MMAP;

    load_elf_image(bprm->filename, bprm->fd, info,
                   &elf_interpreter, bprm->buf);

    /* ??? We need a copy of the elf header for passing to create_elf_tables.
       If we do nothing, we'll have overwritten this when we re-use bprm->buf
       when we load the interpreter.  */
    elf_ex = *(struct elfhdr *)bprm->buf;

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

        /* If the program interpreter is one of these two, then assume
           an iBCS2 image.  Otherwise assume a native linux image.  */

        if (strcmp(elf_interpreter, "/usr/lib/libc.so.1") == 0
            || strcmp(elf_interpreter, "/usr/lib/ld.so.1") == 0) {
            info->personality = PER_SVR4;

            /* Why this, you ask???  Well SVr4 maps page 0 as read-only,
               and some applications "depend" upon this behavior.  Since
               we do not have the power to recompile these, we emulate
               the SVr4 behavior.  Sigh.  */
            target_mmap(0, qemu_host_page_size, PROT_READ | PROT_EXEC,
                        MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        }
#ifdef TARGET_MIPS
        info->interp_fp_abi = interp_info.fp_abi;
#endif
    }

    /*
     * TODO: load a vdso, which would also contain the signal trampolines.
     * Otherwise, allocate a private page to hold them.
     */
    if (TARGET_ARCH_HAS_SIGTRAMP_PAGE) {
        abi_long tramp_page = target_mmap(0, TARGET_PAGE_SIZE,
                                          PROT_READ | PROT_WRITE,
                                          MAP_PRIVATE | MAP_ANON, -1, 0);
        if (tramp_page == -1) {
            return -errno;
        }

        setup_sigtramp(tramp_page);
        target_mprotect(tramp_page, TARGET_PAGE_SIZE, PROT_READ | PROT_EXEC);
    }

    bprm->p = create_elf_tables(bprm->p, bprm->argc, bprm->envc, &elf_ex,
                                info, (elf_interpreter ? &interp_info : NULL));
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

#ifdef USE_ELF_CORE_DUMP
    bprm->core_dump = &elf_core_dump;
#endif

    /*
     * If we reserved extra space for brk, release it now.
     * The implementation of do_brk in syscalls.c expects to be able
     * to mmap pages in this space.
     */
    if (info->reserve_brk) {
        abi_ulong start_brk = HOST_PAGE_ALIGN(info->brk);
        abi_ulong end_brk = HOST_PAGE_ALIGN(info->brk + info->reserve_brk);
        target_munmap(start_brk, end_brk - start_brk);
    }

    return 0;
}

#ifdef USE_ELF_CORE_DUMP
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
 * define USE_ELF_CORE_DUMP in target ELF code (where init_thread() for
 * the target resides):
 *
 * #define USE_ELF_CORE_DUMP
 *
 * Next you define type of register set used for dumping.  ELF specification
 * says that it needs to be array of elf_greg_t that has size of ELF_NREG.
 *
 * typedef <target_regtype> target_elf_greg_t;
 * #define ELF_NREG <number of registers>
 * typedef taret_elf_greg_t target_elf_gregset_t[ELF_NREG];
 *
 * Last step is to implement target specific function that copies registers
 * from given cpu into just specified register set.  Prototype is:
 *
 * static void elf_core_copy_regs(taret_elf_gregset_t *regs,
 *                                const CPUArchState *env);
 *
 * Parameters:
 *     regs - copy register values into here (allocated and zeroed by caller)
 *     env - copy registers from here
 *
 * Example for ARM target is provided in this file.
 */

/* An ELF note in memory */
struct memelfnote {
    const char *name;
    size_t     namesz;
    size_t     namesz_rounded;
    int        type;
    size_t     datasz;
    size_t     datasz_rounded;
    void       *data;
    size_t     notesz;
};

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

/* Here is the structure in which status of each thread is captured. */
struct elf_thread_status {
    QTAILQ_ENTRY(elf_thread_status)  ets_link;
    struct target_elf_prstatus prstatus;   /* NT_PRSTATUS */
#if 0
    elf_fpregset_t fpu;             /* NT_PRFPREG */
    struct task_struct *thread;
    elf_fpxregset_t xfpu;           /* ELF_CORE_XFPREG_TYPE */
#endif
    struct memelfnote notes[1];
    int num_notes;
};

struct elf_note_info {
    struct memelfnote   *notes;
    struct target_elf_prstatus *prstatus;  /* NT_PRSTATUS */
    struct target_elf_prpsinfo *psinfo;    /* NT_PRPSINFO */

    QTAILQ_HEAD(, elf_thread_status) thread_list;
#if 0
    /*
     * Current version of ELF coredump doesn't support
     * dumping fp regs etc.
     */
    elf_fpregset_t *fpu;
    elf_fpxregset_t *xfpu;
    int thread_status_size;
#endif
    int notes_size;
    int numnote;
};

struct vm_area_struct {
    target_ulong   vma_start;  /* start vaddr of memory region */
    target_ulong   vma_end;    /* end vaddr of memory region */
    abi_ulong      vma_flags;  /* protection etc. flags for the region */
    QTAILQ_ENTRY(vm_area_struct) vma_link;
};

struct mm_struct {
    QTAILQ_HEAD(, vm_area_struct) mm_mmap;
    int mm_count;           /* number of mappings */
};

static struct mm_struct *vma_init(void);
static void vma_delete(struct mm_struct *);
static int vma_add_mapping(struct mm_struct *, target_ulong,
                           target_ulong, abi_ulong);
static int vma_get_mapping_count(const struct mm_struct *);
static struct vm_area_struct *vma_first(const struct mm_struct *);
static struct vm_area_struct *vma_next(struct vm_area_struct *);
static abi_ulong vma_dump_size(const struct vm_area_struct *);
static int vma_walker(void *priv, target_ulong start, target_ulong end,
                      unsigned long flags);

static void fill_elf_header(struct elfhdr *, int, uint16_t, uint32_t);
static void fill_note(struct memelfnote *, const char *, int,
                      unsigned int, void *);
static void fill_prstatus(struct target_elf_prstatus *, const TaskState *, int);
static int fill_psinfo(struct target_elf_prpsinfo *, const TaskState *);
static void fill_auxv_note(struct memelfnote *, const TaskState *);
static void fill_elf_note_phdr(struct elf_phdr *, int, off_t);
static size_t note_size(const struct memelfnote *);
static void free_note_info(struct elf_note_info *);
static int fill_note_info(struct elf_note_info *, long, const CPUArchState *);
static void fill_thread_info(struct elf_note_info *, const CPUArchState *);

static int dump_write(int, const void *, size_t);
static int write_note(struct memelfnote *, int);
static int write_note_info(struct elf_note_info *, int);

#ifdef BSWAP_NEEDED
static void bswap_prstatus(struct target_elf_prstatus *prstatus)
{
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
    bswap32s(&en->n_namesz);
    bswap32s(&en->n_descsz);
    bswap32s(&en->n_type);
}
#else
static inline void bswap_prstatus(struct target_elf_prstatus *p) { }
static inline void bswap_psinfo(struct target_elf_prpsinfo *p) {}
static inline void bswap_note(struct elf_note *en) { }
#endif /* BSWAP_NEEDED */

/*
 * Minimal support for linux memory regions.  These are needed
 * when we are finding out what memory exactly belongs to
 * emulated process.  No locks needed here, as long as
 * thread that received the signal is stopped.
 */

static struct mm_struct *vma_init(void)
{
    struct mm_struct *mm;

    if ((mm = g_malloc(sizeof (*mm))) == NULL)
        return (NULL);

    mm->mm_count = 0;
    QTAILQ_INIT(&mm->mm_mmap);

    return (mm);
}

static void vma_delete(struct mm_struct *mm)
{
    struct vm_area_struct *vma;

    while ((vma = vma_first(mm)) != NULL) {
        QTAILQ_REMOVE(&mm->mm_mmap, vma, vma_link);
        g_free(vma);
    }
    g_free(mm);
}

static int vma_add_mapping(struct mm_struct *mm, target_ulong start,
                           target_ulong end, abi_ulong flags)
{
    struct vm_area_struct *vma;

    if ((vma = g_malloc0(sizeof (*vma))) == NULL)
        return (-1);

    vma->vma_start = start;
    vma->vma_end = end;
    vma->vma_flags = flags;

    QTAILQ_INSERT_TAIL(&mm->mm_mmap, vma, vma_link);
    mm->mm_count++;

    return (0);
}

static struct vm_area_struct *vma_first(const struct mm_struct *mm)
{
    return (QTAILQ_FIRST(&mm->mm_mmap));
}

static struct vm_area_struct *vma_next(struct vm_area_struct *vma)
{
    return (QTAILQ_NEXT(vma, vma_link));
}

static int vma_get_mapping_count(const struct mm_struct *mm)
{
    return (mm->mm_count);
}

/*
 * Calculate file (dump) size of given memory region.
 */
static abi_ulong vma_dump_size(const struct vm_area_struct *vma)
{
    /* if we cannot even read the first page, skip it */
    if (!access_ok_untagged(VERIFY_READ, vma->vma_start, TARGET_PAGE_SIZE))
        return (0);

    /*
     * Usually we don't dump executable pages as they contain
     * non-writable code that debugger can read directly from
     * target library etc.  However, thread stacks are marked
     * also executable so we read in first page of given region
     * and check whether it contains elf header.  If there is
     * no elf header, we dump it.
     */
    if (vma->vma_flags & PROT_EXEC) {
        char page[TARGET_PAGE_SIZE];

        if (copy_from_user(page, vma->vma_start, sizeof (page))) {
            return 0;
        }
        if ((page[EI_MAG0] == ELFMAG0) &&
            (page[EI_MAG1] == ELFMAG1) &&
            (page[EI_MAG2] == ELFMAG2) &&
            (page[EI_MAG3] == ELFMAG3)) {
            /*
             * Mappings are possibly from ELF binary.  Don't dump
             * them.
             */
            return (0);
        }
    }

    return (vma->vma_end - vma->vma_start);
}

static int vma_walker(void *priv, target_ulong start, target_ulong end,
                      unsigned long flags)
{
    struct mm_struct *mm = (struct mm_struct *)priv;

    vma_add_mapping(mm, start, end, flags);
    return (0);
}

static void fill_note(struct memelfnote *note, const char *name, int type,
                      unsigned int sz, void *data)
{
    unsigned int namesz;

    namesz = strlen(name) + 1;
    note->name = name;
    note->namesz = namesz;
    note->namesz_rounded = roundup(namesz, sizeof (int32_t));
    note->type = type;
    note->datasz = sz;
    note->datasz_rounded = roundup(sz, sizeof (int32_t));

    note->data = data;

    /*
     * We calculate rounded up note size here as specified by
     * ELF document.
     */
    note->notesz = sizeof (struct elf_note) +
        note->namesz_rounded + note->datasz_rounded;
}

static void fill_elf_header(struct elfhdr *elf, int segs, uint16_t machine,
                            uint32_t flags)
{
    (void) memset(elf, 0, sizeof(*elf));

    (void) memcpy(elf->e_ident, ELFMAG, SELFMAG);
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

static void fill_elf_note_phdr(struct elf_phdr *phdr, int sz, off_t offset)
{
    phdr->p_type = PT_NOTE;
    phdr->p_offset = offset;
    phdr->p_vaddr = 0;
    phdr->p_paddr = 0;
    phdr->p_filesz = sz;
    phdr->p_memsz = 0;
    phdr->p_flags = 0;
    phdr->p_align = 0;

    bswap_phdr(phdr, 1);
}

static size_t note_size(const struct memelfnote *note)
{
    return (note->notesz);
}

static void fill_prstatus(struct target_elf_prstatus *prstatus,
                          const TaskState *ts, int signr)
{
    (void) memset(prstatus, 0, sizeof (*prstatus));
    prstatus->pr_info.si_signo = prstatus->pr_cursig = signr;
    prstatus->pr_pid = ts->ts_tid;
    prstatus->pr_ppid = getppid();
    prstatus->pr_pgrp = getpgrp();
    prstatus->pr_sid = getsid(0);

    bswap_prstatus(prstatus);
}

static int fill_psinfo(struct target_elf_prpsinfo *psinfo, const TaskState *ts)
{
    char *base_filename;
    unsigned int i, len;

    (void) memset(psinfo, 0, sizeof (*psinfo));

    len = ts->info->env_strings - ts->info->arg_strings;
    if (len >= ELF_PRARGSZ)
        len = ELF_PRARGSZ - 1;
    if (copy_from_user(&psinfo->pr_psargs, ts->info->arg_strings, len)) {
        return -EFAULT;
    }
    for (i = 0; i < len; i++)
        if (psinfo->pr_psargs[i] == 0)
            psinfo->pr_psargs[i] = ' ';
    psinfo->pr_psargs[len] = 0;

    psinfo->pr_pid = getpid();
    psinfo->pr_ppid = getppid();
    psinfo->pr_pgrp = getpgrp();
    psinfo->pr_sid = getsid(0);
    psinfo->pr_uid = getuid();
    psinfo->pr_gid = getgid();

    base_filename = g_path_get_basename(ts->bprm->filename);
    /*
     * Using strncpy here is fine: at max-length,
     * this field is not NUL-terminated.
     */
    (void) strncpy(psinfo->pr_fname, base_filename,
                   sizeof(psinfo->pr_fname));

    g_free(base_filename);
    bswap_psinfo(psinfo);
    return (0);
}

static void fill_auxv_note(struct memelfnote *note, const TaskState *ts)
{
    elf_addr_t auxv = (elf_addr_t)ts->info->saved_auxv;
    elf_addr_t orig_auxv = auxv;
    void *ptr;
    int len = ts->info->auxv_len;

    /*
     * Auxiliary vector is stored in target process stack.  It contains
     * {type, value} pairs that we need to dump into note.  This is not
     * strictly necessary but we do it here for sake of completeness.
     */

    /* read in whole auxv vector and copy it to memelfnote */
    ptr = lock_user(VERIFY_READ, orig_auxv, len, 0);
    if (ptr != NULL) {
        fill_note(note, "CORE", NT_AUXV, len, ptr);
        unlock_user(ptr, auxv, len);
    }
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
    struct rlimit dumpsize;
    off_t pos;

    bytes_written = 0;
    getrlimit(RLIMIT_CORE, &dumpsize);
    if ((pos = lseek(fd, 0, SEEK_CUR))==-1) {
        if (errno == ESPIPE) { /* not a seekable stream */
            bytes_left = size;
        } else {
            return pos;
        }
    } else {
        if (dumpsize.rlim_cur <= pos) {
            return -1;
        } else if (dumpsize.rlim_cur == RLIM_INFINITY) {
            bytes_left = size;
        } else {
            size_t limit_left=dumpsize.rlim_cur - pos;
            bytes_left = limit_left >= size ? size : limit_left ;
        }
    }

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

static int write_note(struct memelfnote *men, int fd)
{
    struct elf_note en;

    en.n_namesz = men->namesz;
    en.n_type = men->type;
    en.n_descsz = men->datasz;

    bswap_note(&en);

    if (dump_write(fd, &en, sizeof(en)) != 0)
        return (-1);
    if (dump_write(fd, men->name, men->namesz_rounded) != 0)
        return (-1);
    if (dump_write(fd, men->data, men->datasz_rounded) != 0)
        return (-1);

    return (0);
}

static void fill_thread_info(struct elf_note_info *info, const CPUArchState *env)
{
    CPUState *cpu = env_cpu((CPUArchState *)env);
    TaskState *ts = (TaskState *)cpu->opaque;
    struct elf_thread_status *ets;

    ets = g_malloc0(sizeof (*ets));
    ets->num_notes = 1; /* only prstatus is dumped */
    fill_prstatus(&ets->prstatus, ts, 0);
    elf_core_copy_regs(&ets->prstatus.pr_reg, env);
    fill_note(&ets->notes[0], "CORE", NT_PRSTATUS, sizeof (ets->prstatus),
              &ets->prstatus);

    QTAILQ_INSERT_TAIL(&info->thread_list, ets, ets_link);

    info->notes_size += note_size(&ets->notes[0]);
}

static void init_note_info(struct elf_note_info *info)
{
    /* Initialize the elf_note_info structure so that it is at
     * least safe to call free_note_info() on it. Must be
     * called before calling fill_note_info().
     */
    memset(info, 0, sizeof (*info));
    QTAILQ_INIT(&info->thread_list);
}

static int fill_note_info(struct elf_note_info *info,
                          long signr, const CPUArchState *env)
{
#define NUMNOTES 3
    CPUState *cpu = env_cpu((CPUArchState *)env);
    TaskState *ts = (TaskState *)cpu->opaque;
    int i;

    info->notes = g_new0(struct memelfnote, NUMNOTES);
    if (info->notes == NULL)
        return (-ENOMEM);
    info->prstatus = g_malloc0(sizeof (*info->prstatus));
    if (info->prstatus == NULL)
        return (-ENOMEM);
    info->psinfo = g_malloc0(sizeof (*info->psinfo));
    if (info->prstatus == NULL)
        return (-ENOMEM);

    /*
     * First fill in status (and registers) of current thread
     * including process info & aux vector.
     */
    fill_prstatus(info->prstatus, ts, signr);
    elf_core_copy_regs(&info->prstatus->pr_reg, env);
    fill_note(&info->notes[0], "CORE", NT_PRSTATUS,
              sizeof (*info->prstatus), info->prstatus);
    fill_psinfo(info->psinfo, ts);
    fill_note(&info->notes[1], "CORE", NT_PRPSINFO,
              sizeof (*info->psinfo), info->psinfo);
    fill_auxv_note(&info->notes[2], ts);
    info->numnote = 3;

    info->notes_size = 0;
    for (i = 0; i < info->numnote; i++)
        info->notes_size += note_size(&info->notes[i]);

    /* read and fill status of all threads */
    WITH_QEMU_LOCK_GUARD(&qemu_cpu_list_lock) {
        CPU_FOREACH(cpu) {
            if (cpu == thread_cpu) {
                continue;
            }
            fill_thread_info(info, cpu->env_ptr);
        }
    }

    return (0);
}

static void free_note_info(struct elf_note_info *info)
{
    struct elf_thread_status *ets;

    while (!QTAILQ_EMPTY(&info->thread_list)) {
        ets = QTAILQ_FIRST(&info->thread_list);
        QTAILQ_REMOVE(&info->thread_list, ets, ets_link);
        g_free(ets);
    }

    g_free(info->prstatus);
    g_free(info->psinfo);
    g_free(info->notes);
}

static int write_note_info(struct elf_note_info *info, int fd)
{
    struct elf_thread_status *ets;
    int i, error = 0;

    /* write prstatus, psinfo and auxv for current thread */
    for (i = 0; i < info->numnote; i++)
        if ((error = write_note(&info->notes[i], fd)) != 0)
            return (error);

    /* write prstatus for each thread */
    QTAILQ_FOREACH(ets, &info->thread_list, ets_link) {
        if ((error = write_note(&ets->notes[0], fd)) != 0)
            return (error);
    }

    return (0);
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
    const CPUState *cpu = env_cpu((CPUArchState *)env);
    const TaskState *ts = (const TaskState *)cpu->opaque;
    struct vm_area_struct *vma = NULL;
    g_autofree char *corefile = NULL;
    struct elf_note_info info;
    struct elfhdr elf;
    struct elf_phdr phdr;
    struct rlimit dumpsize;
    struct mm_struct *mm = NULL;
    off_t offset = 0, data_offset = 0;
    int segs = 0;
    int fd = -1;

    init_note_info(&info);

    errno = 0;
    getrlimit(RLIMIT_CORE, &dumpsize);
    if (dumpsize.rlim_cur == 0)
        return 0;

    corefile = core_dump_filename(ts);

    if ((fd = open(corefile, O_WRONLY | O_CREAT,
                   S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH)) < 0)
        return (-errno);

    /*
     * Walk through target process memory mappings and
     * set up structure containing this information.  After
     * this point vma_xxx functions can be used.
     */
    if ((mm = vma_init()) == NULL)
        goto out;

    walk_memory_regions(mm, vma_walker);
    segs = vma_get_mapping_count(mm);

    /*
     * Construct valid coredump ELF header.  We also
     * add one more segment for notes.
     */
    fill_elf_header(&elf, segs + 1, ELF_MACHINE, 0);
    if (dump_write(fd, &elf, sizeof (elf)) != 0)
        goto out;

    /* fill in the in-memory version of notes */
    if (fill_note_info(&info, signr, env) < 0)
        goto out;

    offset += sizeof (elf);                             /* elf header */
    offset += (segs + 1) * sizeof (struct elf_phdr);    /* program headers */

    /* write out notes program header */
    fill_elf_note_phdr(&phdr, info.notes_size, offset);

    offset += info.notes_size;
    if (dump_write(fd, &phdr, sizeof (phdr)) != 0)
        goto out;

    /*
     * ELF specification wants data to start at page boundary so
     * we align it here.
     */
    data_offset = offset = roundup(offset, ELF_EXEC_PAGESIZE);

    /*
     * Write program headers for memory regions mapped in
     * the target process.
     */
    for (vma = vma_first(mm); vma != NULL; vma = vma_next(vma)) {
        (void) memset(&phdr, 0, sizeof (phdr));

        phdr.p_type = PT_LOAD;
        phdr.p_offset = offset;
        phdr.p_vaddr = vma->vma_start;
        phdr.p_paddr = 0;
        phdr.p_filesz = vma_dump_size(vma);
        offset += phdr.p_filesz;
        phdr.p_memsz = vma->vma_end - vma->vma_start;
        phdr.p_flags = vma->vma_flags & PROT_READ ? PF_R : 0;
        if (vma->vma_flags & PROT_WRITE)
            phdr.p_flags |= PF_W;
        if (vma->vma_flags & PROT_EXEC)
            phdr.p_flags |= PF_X;
        phdr.p_align = ELF_EXEC_PAGESIZE;

        bswap_phdr(&phdr, 1);
        if (dump_write(fd, &phdr, sizeof(phdr)) != 0) {
            goto out;
        }
    }

    /*
     * Next we write notes just after program headers.  No
     * alignment needed here.
     */
    if (write_note_info(&info, fd) < 0)
        goto out;

    /* align data to page boundary */
    if (lseek(fd, data_offset, SEEK_SET) != data_offset)
        goto out;

    /*
     * Finally we can dump process memory into corefile as well.
     */
    for (vma = vma_first(mm); vma != NULL; vma = vma_next(vma)) {
        abi_ulong addr;
        abi_ulong end;

        end = vma->vma_start + vma_dump_size(vma);

        for (addr = vma->vma_start; addr < end;
             addr += TARGET_PAGE_SIZE) {
            char page[TARGET_PAGE_SIZE];
            int error;

            /*
             *  Read in page from target process memory and
             *  write it to coredump file.
             */
            error = copy_from_user(page, addr, sizeof (page));
            if (error != 0) {
                (void) fprintf(stderr, "unable to dump " TARGET_ABI_FMT_lx "\n",
                               addr);
                errno = -error;
                goto out;
            }
            if (dump_write(fd, page, TARGET_PAGE_SIZE) < 0)
                goto out;
        }
    }

 out:
    free_note_info(&info);
    if (mm != NULL)
        vma_delete(mm);
    (void) close(fd);

    if (errno != 0)
        return (-errno);
    return (0);
}
#endif /* USE_ELF_CORE_DUMP */

void do_init_thread(struct target_pt_regs *regs, struct image_info *infop)
{
    init_thread(regs, infop);
}
