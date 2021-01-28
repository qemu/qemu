/*
 *  Semihosting support for systems modeled on the Arm "Angel"
 *  semihosting syscalls design. This includes Arm and RISC-V processors
 *
 *  Copyright (c) 2005, 2007 CodeSourcery.
 *  Copyright (c) 2019 Linaro
 *  Written by Paul Brook.
 *
 *  Copyright © 2020 by Keith Packard <keithp@keithp.com>
 *  Adapted for systems other than ARM, including RISC-V, by Keith Packard
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
 *
 *  ARM Semihosting is documented in:
 *     Semihosting for AArch32 and AArch64 Release 2.0
 *     https://static.docs.arm.com/100863/0200/semihosting.pdf
 *
 *  RISC-V Semihosting is documented in:
 *     RISC-V Semihosting
 *     https://github.com/riscv/riscv-semihosting-spec/blob/main/riscv-semihosting-spec.adoc
 */

#include "qemu/osdep.h"

#include "cpu.h"
#include "hw/semihosting/semihost.h"
#include "hw/semihosting/console.h"
#include "hw/semihosting/common-semi.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#ifdef CONFIG_USER_ONLY
#include "qemu.h"

#define COMMON_SEMI_HEAP_SIZE (128 * 1024 * 1024)
#else
#include "exec/gdbstub.h"
#include "qemu/cutils.h"
#ifdef TARGET_ARM
#include "hw/arm/boot.h"
#endif
#include "hw/boards.h"
#endif

#define TARGET_SYS_OPEN        0x01
#define TARGET_SYS_CLOSE       0x02
#define TARGET_SYS_WRITEC      0x03
#define TARGET_SYS_WRITE0      0x04
#define TARGET_SYS_WRITE       0x05
#define TARGET_SYS_READ        0x06
#define TARGET_SYS_READC       0x07
#define TARGET_SYS_ISERROR     0x08
#define TARGET_SYS_ISTTY       0x09
#define TARGET_SYS_SEEK        0x0a
#define TARGET_SYS_FLEN        0x0c
#define TARGET_SYS_TMPNAM      0x0d
#define TARGET_SYS_REMOVE      0x0e
#define TARGET_SYS_RENAME      0x0f
#define TARGET_SYS_CLOCK       0x10
#define TARGET_SYS_TIME        0x11
#define TARGET_SYS_SYSTEM      0x12
#define TARGET_SYS_ERRNO       0x13
#define TARGET_SYS_GET_CMDLINE 0x15
#define TARGET_SYS_HEAPINFO    0x16
#define TARGET_SYS_EXIT        0x18
#define TARGET_SYS_SYNCCACHE   0x19
#define TARGET_SYS_EXIT_EXTENDED 0x20
#define TARGET_SYS_ELAPSED     0x30
#define TARGET_SYS_TICKFREQ    0x31

/* ADP_Stopped_ApplicationExit is used for exit(0),
 * anything else is implemented as exit(1) */
#define ADP_Stopped_ApplicationExit     (0x20026)

#ifndef O_BINARY
#define O_BINARY 0
#endif

#define GDB_O_RDONLY  0x000
#define GDB_O_WRONLY  0x001
#define GDB_O_RDWR    0x002
#define GDB_O_APPEND  0x008
#define GDB_O_CREAT   0x200
#define GDB_O_TRUNC   0x400
#define GDB_O_BINARY  0

static int gdb_open_modeflags[12] = {
    GDB_O_RDONLY,
    GDB_O_RDONLY | GDB_O_BINARY,
    GDB_O_RDWR,
    GDB_O_RDWR | GDB_O_BINARY,
    GDB_O_WRONLY | GDB_O_CREAT | GDB_O_TRUNC,
    GDB_O_WRONLY | GDB_O_CREAT | GDB_O_TRUNC | GDB_O_BINARY,
    GDB_O_RDWR | GDB_O_CREAT | GDB_O_TRUNC,
    GDB_O_RDWR | GDB_O_CREAT | GDB_O_TRUNC | GDB_O_BINARY,
    GDB_O_WRONLY | GDB_O_CREAT | GDB_O_APPEND,
    GDB_O_WRONLY | GDB_O_CREAT | GDB_O_APPEND | GDB_O_BINARY,
    GDB_O_RDWR | GDB_O_CREAT | GDB_O_APPEND,
    GDB_O_RDWR | GDB_O_CREAT | GDB_O_APPEND | GDB_O_BINARY
};

static int open_modeflags[12] = {
    O_RDONLY,
    O_RDONLY | O_BINARY,
    O_RDWR,
    O_RDWR | O_BINARY,
    O_WRONLY | O_CREAT | O_TRUNC,
    O_WRONLY | O_CREAT | O_TRUNC | O_BINARY,
    O_RDWR | O_CREAT | O_TRUNC,
    O_RDWR | O_CREAT | O_TRUNC | O_BINARY,
    O_WRONLY | O_CREAT | O_APPEND,
    O_WRONLY | O_CREAT | O_APPEND | O_BINARY,
    O_RDWR | O_CREAT | O_APPEND,
    O_RDWR | O_CREAT | O_APPEND | O_BINARY
};

typedef enum GuestFDType {
    GuestFDUnused = 0,
    GuestFDHost = 1,
    GuestFDGDB = 2,
    GuestFDFeatureFile = 3,
} GuestFDType;

/*
 * Guest file descriptors are integer indexes into an array of
 * these structures (we will dynamically resize as necessary).
 */
typedef struct GuestFD {
    GuestFDType type;
    union {
        int hostfd;
        target_ulong featurefile_offset;
    };
} GuestFD;

static GArray *guestfd_array;

#ifndef CONFIG_USER_ONLY
#include "exec/address-spaces.h"
/*
 * Find the base of a RAM region containing the specified address
 */
static inline hwaddr
common_semi_find_region_base(hwaddr addr)
{
    MemoryRegion *subregion;

    /*
     * Find the chunk of R/W memory containing the address.  This is
     * used for the SYS_HEAPINFO semihosting call, which should
     * probably be using information from the loaded application.
     */
    QTAILQ_FOREACH(subregion, &get_system_memory()->subregions,
                   subregions_link) {
        if (subregion->ram && !subregion->readonly) {
            Int128 top128 = int128_add(int128_make64(subregion->addr),
                                       subregion->size);
            Int128 addr128 = int128_make64(addr);
            if (subregion->addr <= addr && int128_lt(addr128, top128)) {
                return subregion->addr;
            }
        }
    }
    return 0;
}
#endif

#ifdef TARGET_ARM
static inline target_ulong
common_semi_arg(CPUState *cs, int argno)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    if (is_a64(env)) {
        return env->xregs[argno];
    } else {
        return env->regs[argno];
    }
}

static inline void
common_semi_set_ret(CPUState *cs, target_ulong ret)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    if (is_a64(env)) {
        env->xregs[0] = ret;
    } else {
        env->regs[0] = ret;
    }
}

static inline bool
common_semi_sys_exit_extended(CPUState *cs, int nr)
{
    return (nr == TARGET_SYS_EXIT_EXTENDED || is_a64(cs->env_ptr));
}

#ifndef CONFIG_USER_ONLY
#include "hw/arm/boot.h"
static inline target_ulong
common_semi_rambase(CPUState *cs)
{
    CPUArchState *env = cs->env_ptr;
    const struct arm_boot_info *info = env->boot_info;
    target_ulong sp;

    if (info) {
        return info->loader_start;
    }

    if (is_a64(env)) {
        sp = env->xregs[31];
    } else {
        sp = env->regs[13];
    }
    return common_semi_find_region_base(sp);
}
#endif

#endif /* TARGET_ARM */

#ifdef TARGET_RISCV
static inline target_ulong
common_semi_arg(CPUState *cs, int argno)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    return env->gpr[xA0 + argno];
}

static inline void
common_semi_set_ret(CPUState *cs, target_ulong ret)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    env->gpr[xA0] = ret;
}

static inline bool
common_semi_sys_exit_extended(CPUState *cs, int nr)
{
    return (nr == TARGET_SYS_EXIT_EXTENDED || sizeof(target_ulong) == 8);
}

#ifndef CONFIG_USER_ONLY

static inline target_ulong
common_semi_rambase(CPUState *cs)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    return common_semi_find_region_base(env->gpr[xSP]);
}
#endif

#endif

/*
 * Allocate a new guest file descriptor and return it; if we
 * couldn't allocate a new fd then return -1.
 * This is a fairly simplistic implementation because we don't
 * expect that most semihosting guest programs will make very
 * heavy use of opening and closing fds.
 */
static int alloc_guestfd(void)
{
    guint i;

    if (!guestfd_array) {
        /* New entries zero-initialized, i.e. type GuestFDUnused */
        guestfd_array = g_array_new(FALSE, TRUE, sizeof(GuestFD));
    }

    /* SYS_OPEN should return nonzero handle on success. Start guestfd from 1 */
    for (i = 1; i < guestfd_array->len; i++) {
        GuestFD *gf = &g_array_index(guestfd_array, GuestFD, i);

        if (gf->type == GuestFDUnused) {
            return i;
        }
    }

    /* All elements already in use: expand the array */
    g_array_set_size(guestfd_array, i + 1);
    return i;
}

/*
 * Look up the guestfd in the data structure; return NULL
 * for out of bounds, but don't check whether the slot is unused.
 * This is used internally by the other guestfd functions.
 */
static GuestFD *do_get_guestfd(int guestfd)
{
    if (!guestfd_array) {
        return NULL;
    }

    if (guestfd <= 0 || guestfd >= guestfd_array->len) {
        return NULL;
    }

    return &g_array_index(guestfd_array, GuestFD, guestfd);
}

/*
 * Associate the specified guest fd (which must have been
 * allocated via alloc_fd() and not previously used) with
 * the specified host/gdb fd.
 */
static void associate_guestfd(int guestfd, int hostfd)
{
    GuestFD *gf = do_get_guestfd(guestfd);

    assert(gf);
    gf->type = use_gdb_syscalls() ? GuestFDGDB : GuestFDHost;
    gf->hostfd = hostfd;
}

/*
 * Deallocate the specified guest file descriptor. This doesn't
 * close the host fd, it merely undoes the work of alloc_fd().
 */
static void dealloc_guestfd(int guestfd)
{
    GuestFD *gf = do_get_guestfd(guestfd);

    assert(gf);
    gf->type = GuestFDUnused;
}

/*
 * Given a guest file descriptor, get the associated struct.
 * If the fd is not valid, return NULL. This is the function
 * used by the various semihosting calls to validate a handle
 * from the guest.
 * Note: calling alloc_guestfd() or dealloc_guestfd() will
 * invalidate any GuestFD* obtained by calling this function.
 */
static GuestFD *get_guestfd(int guestfd)
{
    GuestFD *gf = do_get_guestfd(guestfd);

    if (!gf || gf->type == GuestFDUnused) {
        return NULL;
    }
    return gf;
}

/*
 * The semihosting API has no concept of its errno being thread-safe,
 * as the API design predates SMP CPUs and was intended as a simple
 * real-hardware set of debug functionality. For QEMU, we make the
 * errno be per-thread in linux-user mode; in softmmu it is a simple
 * global, and we assume that the guest takes care of avoiding any races.
 */
#ifndef CONFIG_USER_ONLY
static target_ulong syscall_err;

#include "exec/softmmu-semi.h"
#endif

static inline uint32_t set_swi_errno(CPUState *cs, uint32_t code)
{
    if (code == (uint32_t)-1) {
#ifdef CONFIG_USER_ONLY
        TaskState *ts = cs->opaque;

        ts->swi_errno = errno;
#else
        syscall_err = errno;
#endif
    }
    return code;
}

static inline uint32_t get_swi_errno(CPUState *cs)
{
#ifdef CONFIG_USER_ONLY
    TaskState *ts = cs->opaque;

    return ts->swi_errno;
#else
    return syscall_err;
#endif
}

static target_ulong common_semi_syscall_len;

static void common_semi_cb(CPUState *cs, target_ulong ret, target_ulong err)
{
    target_ulong reg0 = common_semi_arg(cs, 0);

    if (ret == (target_ulong)-1) {
        errno = err;
        set_swi_errno(cs, -1);
        reg0 = ret;
    } else {
        /* Fixup syscalls that use nonstardard return conventions.  */
        switch (reg0) {
        case TARGET_SYS_WRITE:
        case TARGET_SYS_READ:
            reg0 = common_semi_syscall_len - ret;
            break;
        case TARGET_SYS_SEEK:
            reg0 = 0;
            break;
        default:
            reg0 = ret;
            break;
        }
    }
    common_semi_set_ret(cs, reg0);
}

static target_ulong common_semi_flen_buf(CPUState *cs)
{
    target_ulong sp;
#ifdef TARGET_ARM
    /* Return an address in target memory of 64 bytes where the remote
     * gdb should write its stat struct. (The format of this structure
     * is defined by GDB's remote protocol and is not target-specific.)
     * We put this on the guest's stack just below SP.
     */
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    if (is_a64(env)) {
        sp = env->xregs[31];
    } else {
        sp = env->regs[13];
    }
#endif
#ifdef TARGET_RISCV
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;

    sp = env->gpr[xSP];
#endif

    return sp - 64;
}

static void
common_semi_flen_cb(CPUState *cs, target_ulong ret, target_ulong err)
{
    /* The size is always stored in big-endian order, extract
       the value. We assume the size always fit in 32 bits.  */
    uint32_t size;
    cpu_memory_rw_debug(cs, common_semi_flen_buf(cs) + 32,
                        (uint8_t *)&size, 4, 0);
    size = be32_to_cpu(size);
    common_semi_set_ret(cs, size);
    errno = err;
    set_swi_errno(cs, -1);
}

static int common_semi_open_guestfd;

static void
common_semi_open_cb(CPUState *cs, target_ulong ret, target_ulong err)
{
    if (ret == (target_ulong)-1) {
        errno = err;
        set_swi_errno(cs, -1);
        dealloc_guestfd(common_semi_open_guestfd);
    } else {
        associate_guestfd(common_semi_open_guestfd, ret);
        ret = common_semi_open_guestfd;
    }
    common_semi_set_ret(cs, ret);
}

static target_ulong
common_semi_gdb_syscall(CPUState *cs, gdb_syscall_complete_cb cb,
                        const char *fmt, ...)
{
    va_list va;

    va_start(va, fmt);
    gdb_do_syscallv(cb, fmt, va);
    va_end(va);

    /*
     * FIXME: in softmmu mode, the gdbstub will schedule our callback
     * to occur, but will not actually call it to complete the syscall
     * until after this function has returned and we are back in the
     * CPU main loop. Therefore callers to this function must not
     * do anything with its return value, because it is not necessarily
     * the result of the syscall, but could just be the old value of X0.
     * The only thing safe to do with this is that the callers of
     * do_common_semihosting() will write it straight back into X0.
     * (In linux-user mode, the callback will have happened before
     * gdb_do_syscallv() returns.)
     *
     * We should tidy this up so neither this function nor
     * do_common_semihosting() return a value, so the mistake of
     * doing something with the return value is not possible to make.
     */

    return common_semi_arg(cs, 0);
}

/*
 * Types for functions implementing various semihosting calls
 * for specific types of guest file descriptor. These must all
 * do the work and return the required return value for the guest,
 * setting the guest errno if appropriate.
 */
typedef uint32_t sys_closefn(CPUState *cs, GuestFD *gf);
typedef uint32_t sys_writefn(CPUState *cs, GuestFD *gf,
                             target_ulong buf, uint32_t len);
typedef uint32_t sys_readfn(CPUState *cs, GuestFD *gf,
                            target_ulong buf, uint32_t len);
typedef uint32_t sys_isattyfn(CPUState *cs, GuestFD *gf);
typedef uint32_t sys_seekfn(CPUState *cs, GuestFD *gf,
                            target_ulong offset);
typedef uint32_t sys_flenfn(CPUState *cs, GuestFD *gf);

static uint32_t host_closefn(CPUState *cs, GuestFD *gf)
{
    /*
     * Only close the underlying host fd if it's one we opened on behalf
     * of the guest in SYS_OPEN.
     */
    if (gf->hostfd == STDIN_FILENO ||
        gf->hostfd == STDOUT_FILENO ||
        gf->hostfd == STDERR_FILENO) {
        return 0;
    }
    return set_swi_errno(cs, close(gf->hostfd));
}

static uint32_t host_writefn(CPUState *cs, GuestFD *gf,
                             target_ulong buf, uint32_t len)
{
    CPUArchState *env = cs->env_ptr;
    uint32_t ret;
    char *s = lock_user(VERIFY_READ, buf, len, 1);
    (void) env; /* Used in arm softmmu lock_user implicitly */
    if (!s) {
        /* Return bytes not written on error */
        return len;
    }
    ret = set_swi_errno(cs, write(gf->hostfd, s, len));
    unlock_user(s, buf, 0);
    if (ret == (uint32_t)-1) {
        ret = 0;
    }
    /* Return bytes not written */
    return len - ret;
}

static uint32_t host_readfn(CPUState *cs, GuestFD *gf,
                            target_ulong buf, uint32_t len)
{
    CPUArchState *env = cs->env_ptr;
    uint32_t ret;
    char *s = lock_user(VERIFY_WRITE, buf, len, 0);
    (void) env; /* Used in arm softmmu lock_user implicitly */
    if (!s) {
        /* return bytes not read */
        return len;
    }
    do {
        ret = set_swi_errno(cs, read(gf->hostfd, s, len));
    } while (ret == -1 && errno == EINTR);
    unlock_user(s, buf, len);
    if (ret == (uint32_t)-1) {
        ret = 0;
    }
    /* Return bytes not read */
    return len - ret;
}

static uint32_t host_isattyfn(CPUState *cs, GuestFD *gf)
{
    return isatty(gf->hostfd);
}

static uint32_t host_seekfn(CPUState *cs, GuestFD *gf, target_ulong offset)
{
    uint32_t ret = set_swi_errno(cs, lseek(gf->hostfd, offset, SEEK_SET));
    if (ret == (uint32_t)-1) {
        return -1;
    }
    return 0;
}

static uint32_t host_flenfn(CPUState *cs, GuestFD *gf)
{
    struct stat buf;
    uint32_t ret = set_swi_errno(cs, fstat(gf->hostfd, &buf));
    if (ret == (uint32_t)-1) {
        return -1;
    }
    return buf.st_size;
}

static uint32_t gdb_closefn(CPUState *cs, GuestFD *gf)
{
    return common_semi_gdb_syscall(cs, common_semi_cb, "close,%x", gf->hostfd);
}

static uint32_t gdb_writefn(CPUState *cs, GuestFD *gf,
                            target_ulong buf, uint32_t len)
{
    common_semi_syscall_len = len;
    return common_semi_gdb_syscall(cs, common_semi_cb, "write,%x,%x,%x",
                                   gf->hostfd, buf, len);
}

static uint32_t gdb_readfn(CPUState *cs, GuestFD *gf,
                           target_ulong buf, uint32_t len)
{
    common_semi_syscall_len = len;
    return common_semi_gdb_syscall(cs, common_semi_cb, "read,%x,%x,%x",
                                   gf->hostfd, buf, len);
}

static uint32_t gdb_isattyfn(CPUState *cs, GuestFD *gf)
{
    return common_semi_gdb_syscall(cs, common_semi_cb, "isatty,%x", gf->hostfd);
}

static uint32_t gdb_seekfn(CPUState *cs, GuestFD *gf, target_ulong offset)
{
    return common_semi_gdb_syscall(cs, common_semi_cb, "lseek,%x,%x,0",
                                   gf->hostfd, offset);
}

static uint32_t gdb_flenfn(CPUState *cs, GuestFD *gf)
{
    return common_semi_gdb_syscall(cs, common_semi_flen_cb, "fstat,%x,%x",
                                   gf->hostfd, common_semi_flen_buf(cs));
}

#define SHFB_MAGIC_0 0x53
#define SHFB_MAGIC_1 0x48
#define SHFB_MAGIC_2 0x46
#define SHFB_MAGIC_3 0x42

/* Feature bits reportable in feature byte 0 */
#define SH_EXT_EXIT_EXTENDED (1 << 0)
#define SH_EXT_STDOUT_STDERR (1 << 1)

static const uint8_t featurefile_data[] = {
    SHFB_MAGIC_0,
    SHFB_MAGIC_1,
    SHFB_MAGIC_2,
    SHFB_MAGIC_3,
    SH_EXT_EXIT_EXTENDED | SH_EXT_STDOUT_STDERR, /* Feature byte 0 */
};

static void init_featurefile_guestfd(int guestfd)
{
    GuestFD *gf = do_get_guestfd(guestfd);

    assert(gf);
    gf->type = GuestFDFeatureFile;
    gf->featurefile_offset = 0;
}

static uint32_t featurefile_closefn(CPUState *cs, GuestFD *gf)
{
    /* Nothing to do */
    return 0;
}

static uint32_t featurefile_writefn(CPUState *cs, GuestFD *gf,
                                    target_ulong buf, uint32_t len)
{
    /* This fd can never be open for writing */

    errno = EBADF;
    return set_swi_errno(cs, -1);
}

static uint32_t featurefile_readfn(CPUState *cs, GuestFD *gf,
                                   target_ulong buf, uint32_t len)
{
    CPUArchState *env = cs->env_ptr;
    uint32_t i;
    char *s;

    (void) env; /* Used in arm softmmu lock_user implicitly */
    s = lock_user(VERIFY_WRITE, buf, len, 0);
    if (!s) {
        return len;
    }

    for (i = 0; i < len; i++) {
        if (gf->featurefile_offset >= sizeof(featurefile_data)) {
            break;
        }
        s[i] = featurefile_data[gf->featurefile_offset];
        gf->featurefile_offset++;
    }

    unlock_user(s, buf, len);

    /* Return number of bytes not read */
    return len - i;
}

static uint32_t featurefile_isattyfn(CPUState *cs, GuestFD *gf)
{
    return 0;
}

static uint32_t featurefile_seekfn(CPUState *cs, GuestFD *gf,
                                   target_ulong offset)
{
    gf->featurefile_offset = offset;
    return 0;
}

static uint32_t featurefile_flenfn(CPUState *cs, GuestFD *gf)
{
    return sizeof(featurefile_data);
}

typedef struct GuestFDFunctions {
    sys_closefn *closefn;
    sys_writefn *writefn;
    sys_readfn *readfn;
    sys_isattyfn *isattyfn;
    sys_seekfn *seekfn;
    sys_flenfn *flenfn;
} GuestFDFunctions;

static const GuestFDFunctions guestfd_fns[] = {
    [GuestFDHost] = {
        .closefn = host_closefn,
        .writefn = host_writefn,
        .readfn = host_readfn,
        .isattyfn = host_isattyfn,
        .seekfn = host_seekfn,
        .flenfn = host_flenfn,
    },
    [GuestFDGDB] = {
        .closefn = gdb_closefn,
        .writefn = gdb_writefn,
        .readfn = gdb_readfn,
        .isattyfn = gdb_isattyfn,
        .seekfn = gdb_seekfn,
        .flenfn = gdb_flenfn,
    },
    [GuestFDFeatureFile] = {
        .closefn = featurefile_closefn,
        .writefn = featurefile_writefn,
        .readfn = featurefile_readfn,
        .isattyfn = featurefile_isattyfn,
        .seekfn = featurefile_seekfn,
        .flenfn = featurefile_flenfn,
    },
};

/* Read the input value from the argument block; fail the semihosting
 * call if the memory read fails.
 */
#ifdef TARGET_ARM
#define GET_ARG(n) do {                                 \
    if (is_a64(env)) {                                  \
        if (get_user_u64(arg ## n, args + (n) * 8)) {   \
            errno = EFAULT;                             \
            return set_swi_errno(cs, -1);              \
        }                                               \
    } else {                                            \
        if (get_user_u32(arg ## n, args + (n) * 4)) {   \
            errno = EFAULT;                             \
            return set_swi_errno(cs, -1);              \
        }                                               \
    }                                                   \
} while (0)

#define SET_ARG(n, val)                                 \
    (is_a64(env) ?                                      \
     put_user_u64(val, args + (n) * 8) :                \
     put_user_u32(val, args + (n) * 4))
#endif

#ifdef TARGET_RISCV

/*
 * get_user_ual is defined as get_user_u32 in softmmu-semi.h,
 * we need a macro that fetches a target_ulong
 */
#define get_user_utl(arg, p)                    \
    ((sizeof(target_ulong) == 8) ?              \
     get_user_u64(arg, p) :                     \
     get_user_u32(arg, p))

/*
 * put_user_ual is defined as put_user_u32 in softmmu-semi.h,
 * we need a macro that stores a target_ulong
 */
#define put_user_utl(arg, p)                    \
    ((sizeof(target_ulong) == 8) ?              \
     put_user_u64(arg, p) :                     \
     put_user_u32(arg, p))

#define GET_ARG(n) do {                                                 \
        if (get_user_utl(arg ## n, args + (n) * sizeof(target_ulong))) { \
            errno = EFAULT;                                             \
            return set_swi_errno(cs, -1);                              \
        }                                                               \
    } while (0)

#define SET_ARG(n, val)                                 \
    put_user_utl(val, args + (n) * sizeof(target_ulong))
#endif

/*
 * Do a semihosting call.
 *
 * The specification always says that the "return register" either
 * returns a specific value or is corrupted, so we don't need to
 * report to our caller whether we are returning a value or trying to
 * leave the register unchanged. We use 0xdeadbeef as the return value
 * when there isn't a defined return value for the call.
 */
target_ulong do_common_semihosting(CPUState *cs)
{
    CPUArchState *env = cs->env_ptr;
    target_ulong args;
    target_ulong arg0, arg1, arg2, arg3;
    target_ulong ul_ret;
    char * s;
    int nr;
    uint32_t ret;
    uint32_t len;
    GuestFD *gf;
    int64_t elapsed;

    (void) env; /* Used implicitly by arm lock_user macro */
    nr = common_semi_arg(cs, 0) & 0xffffffffU;
    args = common_semi_arg(cs, 1);

    switch (nr) {
    case TARGET_SYS_OPEN:
    {
        int guestfd;

        GET_ARG(0);
        GET_ARG(1);
        GET_ARG(2);
        s = lock_user_string(arg0);
        if (!s) {
            errno = EFAULT;
            return set_swi_errno(cs, -1);
        }
        if (arg1 >= 12) {
            unlock_user(s, arg0, 0);
            errno = EINVAL;
            return set_swi_errno(cs, -1);
        }

        guestfd = alloc_guestfd();
        if (guestfd < 0) {
            unlock_user(s, arg0, 0);
            errno = EMFILE;
            return set_swi_errno(cs, -1);
        }

        if (strcmp(s, ":tt") == 0) {
            int result_fileno;

            /*
             * We implement SH_EXT_STDOUT_STDERR, so:
             *  open for read == stdin
             *  open for write == stdout
             *  open for append == stderr
             */
            if (arg1 < 4) {
                result_fileno = STDIN_FILENO;
            } else if (arg1 < 8) {
                result_fileno = STDOUT_FILENO;
            } else {
                result_fileno = STDERR_FILENO;
            }
            associate_guestfd(guestfd, result_fileno);
            unlock_user(s, arg0, 0);
            return guestfd;
        }
        if (strcmp(s, ":semihosting-features") == 0) {
            unlock_user(s, arg0, 0);
            /* We must fail opens for modes other than 0 ('r') or 1 ('rb') */
            if (arg1 != 0 && arg1 != 1) {
                dealloc_guestfd(guestfd);
                errno = EACCES;
                return set_swi_errno(cs, -1);
            }
            init_featurefile_guestfd(guestfd);
            return guestfd;
        }

        if (use_gdb_syscalls()) {
            common_semi_open_guestfd = guestfd;
            ret = common_semi_gdb_syscall(cs, common_semi_open_cb,
                                          "open,%s,%x,1a4", arg0, (int)arg2 + 1,
                                          gdb_open_modeflags[arg1]);
        } else {
            ret = set_swi_errno(cs, open(s, open_modeflags[arg1], 0644));
            if (ret == (uint32_t)-1) {
                dealloc_guestfd(guestfd);
            } else {
                associate_guestfd(guestfd, ret);
                ret = guestfd;
            }
        }
        unlock_user(s, arg0, 0);
        return ret;
    }
    case TARGET_SYS_CLOSE:
        GET_ARG(0);

        gf = get_guestfd(arg0);
        if (!gf) {
            errno = EBADF;
            return set_swi_errno(cs, -1);
        }

        ret = guestfd_fns[gf->type].closefn(cs, gf);
        dealloc_guestfd(arg0);
        return ret;
    case TARGET_SYS_WRITEC:
        qemu_semihosting_console_outc(cs->env_ptr, args);
        return 0xdeadbeef;
    case TARGET_SYS_WRITE0:
        return qemu_semihosting_console_outs(cs->env_ptr, args);
    case TARGET_SYS_WRITE:
        GET_ARG(0);
        GET_ARG(1);
        GET_ARG(2);
        len = arg2;

        gf = get_guestfd(arg0);
        if (!gf) {
            errno = EBADF;
            return set_swi_errno(cs, -1);
        }

        return guestfd_fns[gf->type].writefn(cs, gf, arg1, len);
    case TARGET_SYS_READ:
        GET_ARG(0);
        GET_ARG(1);
        GET_ARG(2);
        len = arg2;

        gf = get_guestfd(arg0);
        if (!gf) {
            errno = EBADF;
            return set_swi_errno(cs, -1);
        }

        return guestfd_fns[gf->type].readfn(cs, gf, arg1, len);
    case TARGET_SYS_READC:
        return qemu_semihosting_console_inc(cs->env_ptr);
    case TARGET_SYS_ISERROR:
        GET_ARG(0);
        return (target_long) arg0 < 0 ? 1 : 0;
    case TARGET_SYS_ISTTY:
        GET_ARG(0);

        gf = get_guestfd(arg0);
        if (!gf) {
            errno = EBADF;
            return set_swi_errno(cs, -1);
        }

        return guestfd_fns[gf->type].isattyfn(cs, gf);
    case TARGET_SYS_SEEK:
        GET_ARG(0);
        GET_ARG(1);

        gf = get_guestfd(arg0);
        if (!gf) {
            errno = EBADF;
            return set_swi_errno(cs, -1);
        }

        return guestfd_fns[gf->type].seekfn(cs, gf, arg1);
    case TARGET_SYS_FLEN:
        GET_ARG(0);

        gf = get_guestfd(arg0);
        if (!gf) {
            errno = EBADF;
            return set_swi_errno(cs, -1);
        }

        return guestfd_fns[gf->type].flenfn(cs, gf);
    case TARGET_SYS_TMPNAM:
        GET_ARG(0);
        GET_ARG(1);
        GET_ARG(2);
        if (asprintf(&s, "/tmp/qemu-%x%02x", getpid(),
                     (int) (arg1 & 0xff)) < 0) {
            return -1;
        }
        ul_ret = (target_ulong) -1;

        /* Make sure there's enough space in the buffer */
        if (strlen(s) < arg2) {
            char *output = lock_user(VERIFY_WRITE, arg0, arg2, 0);
            strcpy(output, s);
            unlock_user(output, arg0, arg2);
            ul_ret = 0;
        }
        free(s);
        return ul_ret;
    case TARGET_SYS_REMOVE:
        GET_ARG(0);
        GET_ARG(1);
        if (use_gdb_syscalls()) {
            ret = common_semi_gdb_syscall(cs, common_semi_cb, "unlink,%s",
                                          arg0, (int)arg1 + 1);
        } else {
            s = lock_user_string(arg0);
            if (!s) {
                errno = EFAULT;
                return set_swi_errno(cs, -1);
            }
            ret =  set_swi_errno(cs, remove(s));
            unlock_user(s, arg0, 0);
        }
        return ret;
    case TARGET_SYS_RENAME:
        GET_ARG(0);
        GET_ARG(1);
        GET_ARG(2);
        GET_ARG(3);
        if (use_gdb_syscalls()) {
            return common_semi_gdb_syscall(cs, common_semi_cb, "rename,%s,%s",
                                           arg0, (int)arg1 + 1, arg2,
                                           (int)arg3 + 1);
        } else {
            char *s2;
            s = lock_user_string(arg0);
            s2 = lock_user_string(arg2);
            if (!s || !s2) {
                errno = EFAULT;
                ret = set_swi_errno(cs, -1);
            } else {
                ret = set_swi_errno(cs, rename(s, s2));
            }
            if (s2)
                unlock_user(s2, arg2, 0);
            if (s)
                unlock_user(s, arg0, 0);
            return ret;
        }
    case TARGET_SYS_CLOCK:
        return clock() / (CLOCKS_PER_SEC / 100);
    case TARGET_SYS_TIME:
        return set_swi_errno(cs, time(NULL));
    case TARGET_SYS_SYSTEM:
        GET_ARG(0);
        GET_ARG(1);
        if (use_gdb_syscalls()) {
            return common_semi_gdb_syscall(cs, common_semi_cb, "system,%s",
                                           arg0, (int)arg1 + 1);
        } else {
            s = lock_user_string(arg0);
            if (!s) {
                errno = EFAULT;
                return set_swi_errno(cs, -1);
            }
            ret = set_swi_errno(cs, system(s));
            unlock_user(s, arg0, 0);
            return ret;
        }
    case TARGET_SYS_ERRNO:
        return get_swi_errno(cs);
    case TARGET_SYS_GET_CMDLINE:
        {
            /* Build a command-line from the original argv.
             *
             * The inputs are:
             *     * arg0, pointer to a buffer of at least the size
             *               specified in arg1.
             *     * arg1, size of the buffer pointed to by arg0 in
             *               bytes.
             *
             * The outputs are:
             *     * arg0, pointer to null-terminated string of the
             *               command line.
             *     * arg1, length of the string pointed to by arg0.
             */

            char *output_buffer;
            size_t input_size;
            size_t output_size;
            int status = 0;
#if !defined(CONFIG_USER_ONLY)
            const char *cmdline;
#else
            TaskState *ts = cs->opaque;
#endif
            GET_ARG(0);
            GET_ARG(1);
            input_size = arg1;
            /* Compute the size of the output string.  */
#if !defined(CONFIG_USER_ONLY)
            cmdline = semihosting_get_cmdline();
            if (cmdline == NULL) {
                cmdline = ""; /* Default to an empty line. */
            }
            output_size = strlen(cmdline) + 1; /* Count terminating 0. */
#else
            unsigned int i;

            output_size = ts->info->arg_end - ts->info->arg_start;
            if (!output_size) {
                /*
                 * We special-case the "empty command line" case (argc==0).
                 * Just provide the terminating 0.
                 */
                output_size = 1;
            }
#endif

            if (output_size > input_size) {
                /* Not enough space to store command-line arguments.  */
                errno = E2BIG;
                return set_swi_errno(cs, -1);
            }

            /* Adjust the command-line length.  */
            if (SET_ARG(1, output_size - 1)) {
                /* Couldn't write back to argument block */
                errno = EFAULT;
                return set_swi_errno(cs, -1);
            }

            /* Lock the buffer on the ARM side.  */
            output_buffer = lock_user(VERIFY_WRITE, arg0, output_size, 0);
            if (!output_buffer) {
                errno = EFAULT;
                return set_swi_errno(cs, -1);
            }

            /* Copy the command-line arguments.  */
#if !defined(CONFIG_USER_ONLY)
            pstrcpy(output_buffer, output_size, cmdline);
#else
            if (output_size == 1) {
                /* Empty command-line.  */
                output_buffer[0] = '\0';
                goto out;
            }

            if (copy_from_user(output_buffer, ts->info->arg_start,
                               output_size)) {
                errno = EFAULT;
                status = set_swi_errno(cs, -1);
                goto out;
            }

            /* Separate arguments by white spaces.  */
            for (i = 0; i < output_size - 1; i++) {
                if (output_buffer[i] == 0) {
                    output_buffer[i] = ' ';
                }
            }
        out:
#endif
            /* Unlock the buffer on the ARM side.  */
            unlock_user(output_buffer, arg0, output_size);

            return status;
        }
    case TARGET_SYS_HEAPINFO:
        {
            target_ulong retvals[4];
            target_ulong limit;
            int i;
#ifdef CONFIG_USER_ONLY
            TaskState *ts = cs->opaque;
#else
            target_ulong rambase = common_semi_rambase(cs);
#endif

            GET_ARG(0);

#ifdef CONFIG_USER_ONLY
            /*
             * Some C libraries assume the heap immediately follows .bss, so
             * allocate it using sbrk.
             */
            if (!ts->heap_limit) {
                abi_ulong ret;

                ts->heap_base = do_brk(0);
                limit = ts->heap_base + COMMON_SEMI_HEAP_SIZE;
                /* Try a big heap, and reduce the size if that fails.  */
                for (;;) {
                    ret = do_brk(limit);
                    if (ret >= limit) {
                        break;
                    }
                    limit = (ts->heap_base >> 1) + (limit >> 1);
                }
                ts->heap_limit = limit;
            }

            retvals[0] = ts->heap_base;
            retvals[1] = ts->heap_limit;
            retvals[2] = ts->stack_base;
            retvals[3] = 0; /* Stack limit.  */
#else
            limit = current_machine->ram_size;
            /* TODO: Make this use the limit of the loaded application.  */
            retvals[0] = rambase + limit / 2;
            retvals[1] = rambase + limit;
            retvals[2] = rambase + limit; /* Stack base */
            retvals[3] = rambase; /* Stack limit.  */
#endif

            for (i = 0; i < ARRAY_SIZE(retvals); i++) {
                bool fail;

                fail = SET_ARG(i, retvals[i]);

                if (fail) {
                    /* Couldn't write back to argument block */
                    errno = EFAULT;
                    return set_swi_errno(cs, -1);
                }
            }
            return 0;
        }
    case TARGET_SYS_EXIT:
    case TARGET_SYS_EXIT_EXTENDED:
        if (common_semi_sys_exit_extended(cs, nr)) {
            /*
             * The A64 version of SYS_EXIT takes a parameter block,
             * so the application-exit type can return a subcode which
             * is the exit status code from the application.
             * SYS_EXIT_EXTENDED is an a new-in-v2.0 optional function
             * which allows A32/T32 guests to also provide a status code.
             */
            GET_ARG(0);
            GET_ARG(1);

            if (arg0 == ADP_Stopped_ApplicationExit) {
                ret = arg1;
            } else {
                ret = 1;
            }
        } else {
            /*
             * The A32/T32 version of SYS_EXIT specifies only
             * Stopped_ApplicationExit as normal exit, but does not
             * allow the guest to specify the exit status code.
             * Everything else is considered an error.
             */
            ret = (args == ADP_Stopped_ApplicationExit) ? 0 : 1;
        }
        gdb_exit(ret);
        exit(ret);
    case TARGET_SYS_ELAPSED:
        elapsed = get_clock() - clock_start;
        if (sizeof(target_ulong) == 8) {
            SET_ARG(0, elapsed);
        } else {
            SET_ARG(0, (uint32_t) elapsed);
            SET_ARG(1, (uint32_t) (elapsed >> 32));
        }
        return 0;
    case TARGET_SYS_TICKFREQ:
        /* qemu always uses nsec */
        return 1000000000;
    case TARGET_SYS_SYNCCACHE:
        /*
         * Clean the D-cache and invalidate the I-cache for the specified
         * virtual address range. This is a nop for us since we don't
         * implement caches. This is only present on A64.
         */
#ifdef TARGET_ARM
        if (is_a64(cs->env_ptr)) {
            return 0;
        }
#endif
#ifdef TARGET_RISCV
        return 0;
#endif
        /* fall through -- invalid for A32/T32 */
    default:
        fprintf(stderr, "qemu: Unsupported SemiHosting SWI 0x%02x\n", nr);
        cpu_dump_state(cs, stderr, 0);
        abort();
    }
}
