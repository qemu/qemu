/* This is the Linux kernel elf-loading code, ported into user space */
#include "qemu/osdep.h"
#include <sys/param.h>

#include <sys/resource.h>

#include "qemu.h"
#include "disas/disas.h"
#include "qemu/path.h"

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

#ifdef TARGET_WORDS_BIGENDIAN
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

#define ELF_PLATFORM get_elf_platform()

static const char *get_elf_platform(void)
{
    static char elf_platform[] = "i386";
    int family = object_property_get_int(OBJECT(thread_cpu), "family", NULL);
    if (family > 6)
        family = 6;
    if (family >= 3)
        elf_platform[1] = '0' + family;
    return elf_platform;
}

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
    (*regs)[0] = env->regs[15];
    (*regs)[1] = env->regs[14];
    (*regs)[2] = env->regs[13];
    (*regs)[3] = env->regs[12];
    (*regs)[4] = env->regs[R_EBP];
    (*regs)[5] = env->regs[R_EBX];
    (*regs)[6] = env->regs[11];
    (*regs)[7] = env->regs[10];
    (*regs)[8] = env->regs[9];
    (*regs)[9] = env->regs[8];
    (*regs)[10] = env->regs[R_EAX];
    (*regs)[11] = env->regs[R_ECX];
    (*regs)[12] = env->regs[R_EDX];
    (*regs)[13] = env->regs[R_ESI];
    (*regs)[14] = env->regs[R_EDI];
    (*regs)[15] = env->regs[R_EAX]; /* XXX */
    (*regs)[16] = env->eip;
    (*regs)[17] = env->segs[R_CS].selector & 0xffff;
    (*regs)[18] = env->eflags;
    (*regs)[19] = env->regs[R_ESP];
    (*regs)[20] = env->segs[R_SS].selector & 0xffff;
    (*regs)[21] = env->segs[R_FS].selector & 0xffff;
    (*regs)[22] = env->segs[R_GS].selector & 0xffff;
    (*regs)[23] = env->segs[R_DS].selector & 0xffff;
    (*regs)[24] = env->segs[R_ES].selector & 0xffff;
    (*regs)[25] = env->segs[R_FS].selector & 0xffff;
    (*regs)[26] = env->segs[R_GS].selector & 0xffff;
}

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
    (*regs)[0] = env->regs[R_EBX];
    (*regs)[1] = env->regs[R_ECX];
    (*regs)[2] = env->regs[R_EDX];
    (*regs)[3] = env->regs[R_ESI];
    (*regs)[4] = env->regs[R_EDI];
    (*regs)[5] = env->regs[R_EBP];
    (*regs)[6] = env->regs[R_EAX];
    (*regs)[7] = env->segs[R_DS].selector & 0xffff;
    (*regs)[8] = env->segs[R_ES].selector & 0xffff;
    (*regs)[9] = env->segs[R_FS].selector & 0xffff;
    (*regs)[10] = env->segs[R_GS].selector & 0xffff;
    (*regs)[11] = env->regs[R_EAX]; /* XXX */
    (*regs)[12] = env->eip;
    (*regs)[13] = env->segs[R_CS].selector & 0xffff;
    (*regs)[14] = env->eflags;
    (*regs)[15] = env->regs[R_ESP];
    (*regs)[16] = env->segs[R_SS].selector & 0xffff;
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

/* Return 1 if the proposed guest space is suitable for the guest.
 * Return 0 if the proposed guest space isn't suitable, but another
 * address space should be tried.
 * Return -1 if there is no way the proposed guest space can be
 * valid regardless of the base.
 * The guest code may leave a page mapped and populate it if the
 * address is suitable.
 */
static int init_guest_commpage(unsigned long guest_base,
                               unsigned long guest_size)
{
    unsigned long real_start, test_page_addr;

    /* We need to check that we can force a fault on access to the
     * commpage at 0xffff0fxx
     */
    test_page_addr = guest_base + (0xffff0f00 & qemu_host_page_mask);

    /* If the commpage lies within the already allocated guest space,
     * then there is no way we can allocate it.
     *
     * You may be thinking that that this check is redundant because
     * we already validated the guest size against MAX_RESERVED_VA;
     * but if qemu_host_page_mask is unusually large, then
     * test_page_addr may be lower.
     */
    if (test_page_addr >= guest_base
        && test_page_addr < (guest_base + guest_size)) {
        return -1;
    }

    /* Note it needs to be writeable to let us initialise it */
    real_start = (unsigned long)
                 mmap((void *)test_page_addr, qemu_host_page_size,
                     PROT_READ | PROT_WRITE,
                     MAP_ANONYMOUS | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    /* If we can't map it then try another address */
    if (real_start == -1ul) {
        return 0;
    }

    if (real_start != test_page_addr) {
        /* OS didn't put the page where we asked - unmap and reject */
        munmap((void *)real_start, qemu_host_page_size);
        return 0;
    }

    /* Leave the page mapped
     * Populate it (mmap should have left it all 0'd)
     */

    /* Kernel helper versions */
    __put_user(5, (uint32_t *)g2h(0xffff0ffcul));

    /* Now it's populated make it RO */
    if (mprotect((void *)test_page_addr, qemu_host_page_size, PROT_READ)) {
        perror("Protecting guest commpage");
        exit(-1);
    }

    return 1; /* All good */
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
    GET_FEATURE(ARM_FEATURE_VFP, ARM_HWCAP_ARM_VFP);
    GET_FEATURE(ARM_FEATURE_IWMMXT, ARM_HWCAP_ARM_IWMMXT);
    GET_FEATURE(ARM_FEATURE_THUMB2EE, ARM_HWCAP_ARM_THUMBEE);
    GET_FEATURE(ARM_FEATURE_NEON, ARM_HWCAP_ARM_NEON);
    GET_FEATURE(ARM_FEATURE_VFP3, ARM_HWCAP_ARM_VFPv3);
    GET_FEATURE(ARM_FEATURE_V6K, ARM_HWCAP_ARM_TLS);
    GET_FEATURE(ARM_FEATURE_VFP4, ARM_HWCAP_ARM_VFPv4);
    GET_FEATURE_ID(arm_div, ARM_HWCAP_ARM_IDIVA);
    GET_FEATURE_ID(thumb_div, ARM_HWCAP_ARM_IDIVT);
    /* All QEMU's VFPv3 CPUs have 32 registers, see VFP_DREG in translate.c.
     * Note that the ARM_HWCAP_ARM_VFPv3D16 bit is always the inverse of
     * ARM_HWCAP_ARM_VFPD32 (and so always clear for QEMU); it is unrelated
     * to our VFP_FP16 feature bit.
     */
    GET_FEATURE(ARM_FEATURE_VFP3, ARM_HWCAP_ARM_VFPD32);
    GET_FEATURE(ARM_FEATURE_LPAE, ARM_HWCAP_ARM_LPAE);

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

#else
/* 64 bit ARM definitions */
#define ELF_START_MMAP 0x80000000

#define ELF_ARCH        EM_AARCH64
#define ELF_CLASS       ELFCLASS64
#define ELF_PLATFORM    "aarch64"

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
};

#define ELF_HWCAP get_elf_hwcap()

static uint32_t get_elf_hwcap(void)
{
    ARMCPU *cpu = ARM_CPU(thread_cpu);
    uint32_t hwcaps = 0;

    hwcaps |= ARM_HWCAP_A64_FP;
    hwcaps |= ARM_HWCAP_A64_ASIMD;

    /* probe for the extra features */
#define GET_FEATURE_ID(feat, hwcap) \
    do { if (cpu_isar_feature(feat, cpu)) { hwcaps |= hwcap; } } while (0)

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

#undef GET_FEATURE_ID

    return hwcaps;
}

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

#define STACK_BIAS              2047

static inline void init_thread(struct target_pt_regs *regs,
                               struct image_info *infop)
{
#ifndef TARGET_ABI32
    regs->tstate = 0;
#endif
    regs->pc = infop->entry;
    regs->npc = regs->pc + 4;
    regs->y = 0;
#ifdef TARGET_ABI32
    regs->u_regs[14] = infop->start_stack - 16 * 4;
#else
    if (personality(infop->personality) == PER_LINUX32)
        regs->u_regs[14] = infop->start_stack - 16 * 4;
    else
        regs->u_regs[14] = infop->start_stack - 16 * 8 - STACK_BIAS;
#endif
}

#else
#define ELF_START_MMAP 0x80000000
#define ELF_HWCAP  (HWCAP_SPARC_FLUSH | HWCAP_SPARC_STBAR | HWCAP_SPARC_SWAP \
                    | HWCAP_SPARC_MULDIV)

#define ELF_CLASS   ELFCLASS32
#define ELF_ARCH    EM_SPARC

static inline void init_thread(struct target_pt_regs *regs,
                               struct image_info *infop)
{
    regs->psr = 0;
    regs->pc = infop->entry;
    regs->npc = regs->pc + 4;
    regs->y = 0;
    regs->u_regs[14] = infop->start_stack - 16 * 4;
}

#endif
#endif

#ifdef TARGET_PPC

#define ELF_MACHINE    PPC_ELF_MACHINE
#define ELF_START_MMAP 0x80000000

#if defined(TARGET_PPC64) && !defined(TARGET_ABI32)

#define elf_check_arch(x) ( (x) == EM_PPC64 )

#define ELF_CLASS       ELFCLASS64

#else

#define ELF_CLASS       ELFCLASS32

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
    QEMU_PPC_FEATURE2_ARCH_3_00 = 0x00800000, /* ISA 3.00 */
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
                  PPC2_ISA207S), QEMU_PPC_FEATURE2_ARCH_2_07);
    GET_FEATURE2(PPC2_ISA300, QEMU_PPC_FEATURE2_ARCH_3_00);

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
#if defined(TARGET_PPC64) && !defined(TARGET_ABI32)
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
    (*regs)[37] = tswapreg(env->xer);

    for (i = 0; i < ARRAY_SIZE(env->crf); i++) {
        ccr |= env->crf[i] << (32 - ((i + 1) * 4));
    }
    (*regs)[38] = tswapreg(ccr);
}

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE       4096

#endif

#ifdef TARGET_MIPS

#define ELF_START_MMAP 0x80000000

#ifdef TARGET_MIPS64
#define ELF_CLASS   ELFCLASS64
#else
#define ELF_CLASS   ELFCLASS32
#endif
#define ELF_ARCH    EM_MIPS

#define elf_check_arch(x) ((x) == EM_MIPS || (x) == EM_NANOMIPS)

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
};

#define ELF_HWCAP get_elf_hwcap()

static uint32_t get_elf_hwcap(void)
{
    MIPSCPU *cpu = MIPS_CPU(thread_cpu);
    uint32_t hwcaps = 0;

#define GET_FEATURE(flag, hwcap) \
    do { if (cpu->env.insn_flags & (flag)) { hwcaps |= hwcap; } } while (0)

    GET_FEATURE(ISA_MIPS32R6 | ISA_MIPS64R6, HWCAP_MIPS_R6);
    GET_FEATURE(ASE_MSA, HWCAP_MIPS_MSA);

#undef GET_FEATURE

    return hwcaps;
}

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

    for (i = 0; i < 6; i++) {
        (*regs)[pos++] = tswapreg(env->sregs[i]);
    }
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
    regs->estatus = 0x3;
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

    (*regs)[32] = tswapreg(env->regs[R_PC]);

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

static inline void init_thread(struct target_pt_regs *regs, struct image_info *infop)
{
    regs->psw.addr = infop->entry;
    regs->psw.mask = PSW_MASK_64 | PSW_MASK_32;
    regs->gprs[15] = infop->start_stack;
}

#endif /* TARGET_S390X */

#ifdef TARGET_TILEGX

/* 42 bits real used address, a half for user mode */
#define ELF_START_MMAP (0x00000020000000000ULL)

#define elf_check_arch(x) ((x) == EM_TILEGX)

#define ELF_CLASS   ELFCLASS64
#define ELF_DATA    ELFDATA2LSB
#define ELF_ARCH    EM_TILEGX

static inline void init_thread(struct target_pt_regs *regs,
                               struct image_info *infop)
{
    regs->pc = infop->entry;
    regs->sp = infop->start_stack;

}

#define ELF_EXEC_PAGESIZE        65536 /* TILE-Gx page size is 64KB */

#endif /* TARGET_TILEGX */

#ifdef TARGET_RISCV

#define ELF_START_MMAP 0x80000000
#define ELF_ARCH  EM_RISCV

#ifdef TARGET_RISCV32
#define ELF_CLASS ELFCLASS32
#else
#define ELF_CLASS ELFCLASS64
#endif

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
    regs->gr[24] = infop->arg_start;
    regs->gr[25] = (infop->arg_end - infop->arg_start) / sizeof(abi_ulong);
    /* The top-of-stack contains a linkage buffer.  */
    regs->gr[30] = infop->start_stack + 64;
    regs->gr[31] = infop->entry;
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

#ifndef ELF_PLATFORM
#define ELF_PLATFORM (NULL)
#endif

#ifndef ELF_MACHINE
#define ELF_MACHINE ELF_ARCH
#endif

#ifndef elf_check_arch
#define elf_check_arch(x) ((x) == ELF_ARCH)
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

#include "elf.h"

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

#define DLINFO_ITEMS 15

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

    size = guest_stack_size;
    if (size < STACK_LOWER_LIMIT) {
        size = STACK_LOWER_LIMIT;
    }
    guard = TARGET_PAGE_SIZE;
    if (guard < qemu_real_host_page_size) {
        guard = qemu_real_host_page_size;
    }

    error = target_mmap(0, size + guard, PROT_READ | PROT_WRITE,
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
        target_mprotect(error + size, guard, PROT_NONE);
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

    host_start = (uintptr_t) g2h(elf_bss);
    host_end = (uintptr_t) g2h(last_bss);
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
        page_set_flags(elf_bss & TARGET_PAGE_MASK, last_bss, prot | PAGE_VALID);
    }

    if (host_start < host_map_start) {
        memset((void *)host_start, 0, host_map_start - host_start);
    }
}

#ifdef TARGET_ARM
static int elf_is_fdpic(struct elfhdr *exec)
{
    return exec->e_ident[EI_OSABI] == ELFOSABI_ARM_FDPIC;
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
    abi_ulong u_platform;
    const char *k_platform;
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
     * Generate 16 random bytes for userspace PRNG seeding (not
     * cryptically secure but it's not the aim of QEMU).
     */
    for (i = 0; i < 16; i++) {
        k_rand_bytes[i] = rand();
    }
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
    info->arg_start = u_argv;
    info->arg_end = u_argv + argc * n;

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

#ifdef ELF_HWCAP2
    NEW_AUX_ENT(AT_HWCAP2, (abi_ulong) ELF_HWCAP2);
#endif

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

unsigned long init_guest_space(unsigned long host_start,
                               unsigned long host_size,
                               unsigned long guest_start,
                               bool fixed)
{
    unsigned long current_start, aligned_start;
    int flags;

    assert(host_start || host_size);

    /* If just a starting address is given, then just verify that
     * address.  */
    if (host_start && !host_size) {
#if defined(TARGET_ARM) && !defined(TARGET_AARCH64)
        if (init_guest_commpage(host_start, host_size) != 1) {
            return (unsigned long)-1;
        }
#endif
        return host_start;
    }

    /* Setup the initial flags and start address.  */
    current_start = host_start & qemu_host_page_mask;
    flags = MAP_ANONYMOUS | MAP_PRIVATE | MAP_NORESERVE;
    if (fixed) {
        flags |= MAP_FIXED;
    }

    /* Otherwise, a non-zero size region of memory needs to be mapped
     * and validated.  */

#if defined(TARGET_ARM) && !defined(TARGET_AARCH64)
    /* On 32-bit ARM, we need to map not just the usable memory, but
     * also the commpage.  Try to find a suitable place by allocating
     * a big chunk for all of it.  If host_start, then the naive
     * strategy probably does good enough.
     */
    if (!host_start) {
        unsigned long guest_full_size, host_full_size, real_start;

        guest_full_size =
            (0xffff0f00 & qemu_host_page_mask) + qemu_host_page_size;
        host_full_size = guest_full_size - guest_start;
        real_start = (unsigned long)
            mmap(NULL, host_full_size, PROT_NONE, flags, -1, 0);
        if (real_start == (unsigned long)-1) {
            if (host_size < host_full_size - qemu_host_page_size) {
                /* We failed to map a continous segment, but we're
                 * allowed to have a gap between the usable memory and
                 * the commpage where other things can be mapped.
                 * This sparseness gives us more flexibility to find
                 * an address range.
                 */
                goto naive;
            }
            return (unsigned long)-1;
        }
        munmap((void *)real_start, host_full_size);
        if (real_start & ~qemu_host_page_mask) {
            /* The same thing again, but with an extra qemu_host_page_size
             * so that we can shift around alignment.
             */
            unsigned long real_size = host_full_size + qemu_host_page_size;
            real_start = (unsigned long)
                mmap(NULL, real_size, PROT_NONE, flags, -1, 0);
            if (real_start == (unsigned long)-1) {
                if (host_size < host_full_size - qemu_host_page_size) {
                    goto naive;
                }
                return (unsigned long)-1;
            }
            munmap((void *)real_start, real_size);
            real_start = HOST_PAGE_ALIGN(real_start);
        }
        current_start = real_start;
    }
 naive:
#endif

    while (1) {
        unsigned long real_start, real_size, aligned_size;
        aligned_size = real_size = host_size;

        /* Do not use mmap_find_vma here because that is limited to the
         * guest address space.  We are going to make the
         * guest address space fit whatever we're given.
         */
        real_start = (unsigned long)
            mmap((void *)current_start, host_size, PROT_NONE, flags, -1, 0);
        if (real_start == (unsigned long)-1) {
            return (unsigned long)-1;
        }

        /* Check to see if the address is valid.  */
        if (host_start && real_start != current_start) {
            goto try_again;
        }

        /* Ensure the address is properly aligned.  */
        if (real_start & ~qemu_host_page_mask) {
            /* Ideally, we adjust like
             *
             *    pages: [  ][  ][  ][  ][  ]
             *      old:   [   real   ]
             *             [ aligned  ]
             *      new:   [     real     ]
             *               [ aligned  ]
             *
             * But if there is something else mapped right after it,
             * then obviously it won't have room to grow, and the
             * kernel will put the new larger real someplace else with
             * unknown alignment (if we made it to here, then
             * fixed=false).  Which is why we grow real by a full page
             * size, instead of by part of one; so that even if we get
             * moved, we can still guarantee alignment.  But this does
             * mean that there is a padding of < 1 page both before
             * and after the aligned range; the "after" could could
             * cause problems for ARM emulation where it could butt in
             * to where we need to put the commpage.
             */
            munmap((void *)real_start, host_size);
            real_size = aligned_size + qemu_host_page_size;
            real_start = (unsigned long)
                mmap((void *)real_start, real_size, PROT_NONE, flags, -1, 0);
            if (real_start == (unsigned long)-1) {
                return (unsigned long)-1;
            }
            aligned_start = HOST_PAGE_ALIGN(real_start);
        } else {
            aligned_start = real_start;
        }

#if defined(TARGET_ARM) && !defined(TARGET_AARCH64)
        /* On 32-bit ARM, we need to also be able to map the commpage.  */
        int valid = init_guest_commpage(aligned_start - guest_start,
                                        aligned_size + guest_start);
        if (valid == -1) {
            munmap((void *)real_start, real_size);
            return (unsigned long)-1;
        } else if (valid == 0) {
            goto try_again;
        }
#endif

        /* If nothing has said `return -1` or `goto try_again` yet,
         * then the address we have is good.
         */
        break;

    try_again:
        /* That address didn't work.  Unmap and try a different one.
         * The address the host picked because is typically right at
         * the top of the host address space and leaves the guest with
         * no usable address space.  Resort to a linear search.  We
         * already compensated for mmap_min_addr, so this should not
         * happen often.  Probably means we got unlucky and host
         * address space randomization put a shared library somewhere
         * inconvenient.
         *
         * This is probably a good strategy if host_start, but is
         * probably a bad strategy if not, which means we got here
         * because of trouble with ARM commpage setup.
         */
        munmap((void *)real_start, real_size);
        current_start += qemu_host_page_size;
        if (host_start == current_start) {
            /* Theoretically possible if host doesn't have any suitably
             * aligned areas.  Normally the first mmap will fail.
             */
            return (unsigned long)-1;
        }
    }

    qemu_log_mask(CPU_LOG_PAGE, "Reserved 0x%lx bytes of guest address space\n", host_size);

    return aligned_start;
}

static void probe_guest_base(const char *image_name,
                             abi_ulong loaddr, abi_ulong hiaddr)
{
    /* Probe for a suitable guest base address, if the user has not set
     * it explicitly, and set guest_base appropriately.
     * In case of error we will print a suitable message and exit.
     */
    const char *errmsg;
    if (!have_guest_base && !reserved_va) {
        unsigned long host_start, real_start, host_size;

        /* Round addresses to page boundaries.  */
        loaddr &= qemu_host_page_mask;
        hiaddr = HOST_PAGE_ALIGN(hiaddr);

        if (loaddr < mmap_min_addr) {
            host_start = HOST_PAGE_ALIGN(mmap_min_addr);
        } else {
            host_start = loaddr;
            if (host_start != loaddr) {
                errmsg = "Address overflow loading ELF binary";
                goto exit_errmsg;
            }
        }
        host_size = hiaddr - loaddr;

        /* Setup the initial guest memory space with ranges gleaned from
         * the ELF image that is being loaded.
         */
        real_start = init_guest_space(host_start, host_size, loaddr, false);
        if (real_start == (unsigned long)-1) {
            errmsg = "Unable to find space for application";
            goto exit_errmsg;
        }
        guest_base = real_start - loaddr;

        qemu_log_mask(CPU_LOG_PAGE, "Relocating guest address space from 0x"
                      TARGET_ABI_FMT_lx " to 0x%lx\n",
                      loaddr, real_start);
    }
    return;

exit_errmsg:
    fprintf(stderr, "%s: %s\n", image_name, errmsg);
    exit(-1);
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
    int i, retval;
    const char *errmsg;

    /* First of all, some simple consistency checks */
    errmsg = "Invalid ELF image for this architecture";
    if (!elf_check_ident(ehdr)) {
        goto exit_errmsg;
    }
    bswap_ehdr(ehdr);
    if (!elf_check_ehdr(ehdr)) {
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

    /* Find the maximum size of the image and allocate an appropriate
       amount of memory to handle that.  */
    loaddr = -1, hiaddr = 0;
    info->alignment = 0;
    for (i = 0; i < ehdr->e_phnum; ++i) {
        if (phdr[i].p_type == PT_LOAD) {
            abi_ulong a = phdr[i].p_vaddr - phdr[i].p_offset;
            if (a < loaddr) {
                loaddr = a;
            }
            a = phdr[i].p_vaddr + phdr[i].p_memsz;
            if (a > hiaddr) {
                hiaddr = a;
            }
            ++info->nsegs;
            info->alignment |= phdr[i].p_align;
        }
    }

    load_addr = loaddr;
    if (ehdr->e_type == ET_DYN) {
        /* The image indicates that it can be loaded anywhere.  Find a
           location that can hold the memory space required.  If the
           image is pre-linked, LOADDR will be non-zero.  Since we do
           not supply MAP_FIXED here we'll use that address if and
           only if it remains available.  */
        load_addr = target_mmap(loaddr, hiaddr - loaddr, PROT_NONE,
                                MAP_PRIVATE | MAP_ANON | MAP_NORESERVE,
                                -1, 0);
        if (load_addr == -1) {
            goto exit_perror;
        }
    } else if (pinterp_name != NULL) {
        /* This is the main executable.  Make sure that the low
           address does not conflict with MMAP_MIN_ADDR or the
           QEMU application itself.  */
        probe_guest_base(image_name, loaddr, hiaddr);
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
    info->load_addr = load_addr;
    info->entry = ehdr->e_entry + load_bias;
    info->start_code = -1;
    info->end_code = 0;
    info->start_data = -1;
    info->end_data = 0;
    info->brk = 0;
    info->elf_flags = ehdr->e_flags;

    for (i = 0; i < ehdr->e_phnum; i++) {
        struct elf_phdr *eppnt = phdr + i;
        if (eppnt->p_type == PT_LOAD) {
            abi_ulong vaddr, vaddr_po, vaddr_ps, vaddr_ef, vaddr_em, vaddr_len;
            int elf_prot = 0;

            if (eppnt->p_flags & PF_R) elf_prot =  PROT_READ;
            if (eppnt->p_flags & PF_W) elf_prot |= PROT_WRITE;
            if (eppnt->p_flags & PF_X) elf_prot |= PROT_EXEC;

            vaddr = load_bias + eppnt->p_vaddr;
            vaddr_po = TARGET_ELF_PAGEOFFSET(vaddr);
            vaddr_ps = TARGET_ELF_PAGESTART(vaddr);
            vaddr_len = TARGET_ELF_PAGELENGTH(eppnt->p_filesz + vaddr_po);

            error = target_mmap(vaddr_ps, vaddr_len,
                                elf_prot, MAP_PRIVATE | MAP_FIXED,
                                image_fd, eppnt->p_offset - vaddr_po);
            if (error == -1) {
                goto exit_perror;
            }

            vaddr_ef = vaddr + eppnt->p_filesz;
            vaddr_em = vaddr + eppnt->p_memsz;

            /* If the load segment requests extra zeros (e.g. bss), map it.  */
            if (vaddr_ef < vaddr_em) {
                zero_bss(vaddr_ef, vaddr_em, elf_prot);
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
                if (vaddr_em > info->brk) {
                    info->brk = vaddr_em;
                }
            }
        } else if (eppnt->p_type == PT_INTERP && pinterp_name) {
            char *interp_name;

            if (*pinterp_name) {
                errmsg = "Multiple PT_INTERP entries";
                goto exit_errmsg;
            }
            interp_name = malloc(eppnt->p_filesz);
            if (!interp_name) {
                goto exit_perror;
            }

            if (eppnt->p_offset + eppnt->p_filesz <= BPRM_BUF_SIZE) {
                memcpy(interp_name, bprm_buf + eppnt->p_offset,
                       eppnt->p_filesz);
            } else {
                retval = pread(image_fd, interp_name, eppnt->p_filesz,
                               eppnt->p_offset);
                if (retval != eppnt->p_filesz) {
                    goto exit_perror;
                }
            }
            if (interp_name[eppnt->p_filesz - 1] != 0) {
                errmsg = "Invalid PT_INTERP entry";
                goto exit_errmsg;
            }
            *pinterp_name = interp_name;
#ifdef TARGET_MIPS
        } else if (eppnt->p_type == PT_MIPS_ABIFLAGS) {
            Mips_elf_abiflags_v0 abiflags;
            if (eppnt->p_filesz < sizeof(Mips_elf_abiflags_v0)) {
                errmsg = "Invalid PT_MIPS_ABIFLAGS entry";
                goto exit_errmsg;
            }
            if (eppnt->p_offset + eppnt->p_filesz <= BPRM_BUF_SIZE) {
                memcpy(&abiflags, bprm_buf + eppnt->p_offset,
                       sizeof(Mips_elf_abiflags_v0));
            } else {
                retval = pread(image_fd, &abiflags, sizeof(Mips_elf_abiflags_v0),
                               eppnt->p_offset);
                if (retval != sizeof(Mips_elf_abiflags_v0)) {
                    goto exit_perror;
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
        info->brk = info->end_code;
    }

    if (qemu_log_enabled()) {
        load_symbols(ehdr, image_fd, load_bias);
    }

    mmap_unlock();

    close(image_fd);
    return;

 exit_read:
    if (retval >= 0) {
        errmsg = "Incomplete read of file header";
        goto exit_errmsg;
    }
 exit_perror:
    errmsg = strerror(errno);
 exit_errmsg:
    fprintf(stderr, "%s: %s\n", image_name, errmsg);
    exit(-1);
}

static void load_elf_interp(const char *filename, struct image_info *info,
                            char bprm_buf[BPRM_BUF_SIZE])
{
    int fd, retval;

    fd = open(path(filename), O_RDONLY);
    if (fd < 0) {
        goto exit_perror;
    }

    retval = read(fd, bprm_buf, BPRM_BUF_SIZE);
    if (retval < 0) {
        goto exit_perror;
    }
    if (retval < BPRM_BUF_SIZE) {
        memset(bprm_buf + retval, 0, BPRM_BUF_SIZE - retval);
    }

    load_elf_image(filename, fd, info, NULL, bprm_buf);
    return;

 exit_perror:
    fprintf(stderr, "%s: %s\n", filename, strerror(errno));
    exit(-1);
}

static int symfind(const void *s0, const void *s1)
{
    target_ulong addr = *(target_ulong *)s0;
    struct elf_sym *sym = (struct elf_sym *)s1;
    int result = 0;
    if (addr < sym->st_value) {
        result = -1;
    } else if (addr >= sym->st_value + sym->st_size) {
        result = 1;
    }
    return result;
}

static const char *lookup_symbolxx(struct syminfo *s, target_ulong orig_addr)
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
        free(elf_interpreter);
    }

#ifdef USE_ELF_CORE_DUMP
    bprm->core_dump = &elf_core_dump;
#endif

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
    char    pr_fname[16];           /* filename of executable */
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

    QTAILQ_HEAD(thread_list_head, elf_thread_status) thread_list;
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
static int core_dump_filename(const TaskState *, char *, size_t);

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
    if (!access_ok(VERIFY_READ, vma->vma_start, TARGET_PAGE_SIZE))
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

        copy_from_user(page, vma->vma_start, sizeof (page));
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

    len = ts->info->arg_end - ts->info->arg_start;
    if (len >= ELF_PRARGSZ)
        len = ELF_PRARGSZ - 1;
    if (copy_from_user(&psinfo->pr_psargs, ts->info->arg_start, len))
        return -EFAULT;
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
 * Returns 0 in case of success, -1 otherwise (errno is set).
 */
static int core_dump_filename(const TaskState *ts, char *buf,
                              size_t bufsize)
{
    char timestamp[64];
    char *base_filename = NULL;
    struct timeval tv;
    struct tm tm;

    assert(bufsize >= PATH_MAX);

    if (gettimeofday(&tv, NULL) < 0) {
        (void) fprintf(stderr, "unable to get current timestamp: %s",
                       strerror(errno));
        return (-1);
    }

    base_filename = g_path_get_basename(ts->bprm->filename);
    (void) strftime(timestamp, sizeof (timestamp), "%Y%m%d-%H%M%S",
                    localtime_r(&tv.tv_sec, &tm));
    (void) snprintf(buf, bufsize, "qemu_%s_%s_%d.core",
                    base_filename, timestamp, (int)getpid());
    g_free(base_filename);

    return (0);
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
    CPUState *cpu = ENV_GET_CPU((CPUArchState *)env);
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
    CPUState *cpu = ENV_GET_CPU((CPUArchState *)env);
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
    cpu_list_lock();
    CPU_FOREACH(cpu) {
        if (cpu == thread_cpu) {
            continue;
        }
        fill_thread_info(info, (CPUArchState *)cpu->env_ptr);
    }
    cpu_list_unlock();

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
    const CPUState *cpu = ENV_GET_CPU((CPUArchState *)env);
    const TaskState *ts = (const TaskState *)cpu->opaque;
    struct vm_area_struct *vma = NULL;
    char corefile[PATH_MAX];
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

    if (core_dump_filename(ts, corefile, sizeof (corefile)) < 0)
        return (-errno);

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
