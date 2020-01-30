/*
 *  Arm "Angel" semihosting syscalls
 *
 *  Copyright (c) 2005, 2007 CodeSourcery.
 *  Copyright (c) 2019 Linaro
 *  Written by Paul Brook.
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
 */

#include "qemu/osdep.h"

#include "cpu.h"
#include "hw/semihosting/semihost.h"
#include "hw/semihosting/console.h"
#include "qemu/log.h"
#ifdef CONFIG_USER_ONLY
#include "qemu.h"

#define ARM_ANGEL_HEAP_SIZE (128 * 1024 * 1024)
#else
#include "exec/gdbstub.h"
#include "qemu/cutils.h"
#endif

#define TARGET_SYS_OPEN        0x01
#define TARGET_SYS_CLOSE       0x02
#define TARGET_SYS_WRITEC      0x03
#define TARGET_SYS_WRITE0      0x04
#define TARGET_SYS_WRITE       0x05
#define TARGET_SYS_READ        0x06
#define TARGET_SYS_READC       0x07
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

static inline uint32_t set_swi_errno(CPUARMState *env, uint32_t code)
{
    if (code == (uint32_t)-1) {
#ifdef CONFIG_USER_ONLY
        CPUState *cs = env_cpu(env);
        TaskState *ts = cs->opaque;

        ts->swi_errno = errno;
#else
        syscall_err = errno;
#endif
    }
    return code;
}

static inline uint32_t get_swi_errno(CPUARMState *env)
{
#ifdef CONFIG_USER_ONLY
    CPUState *cs = env_cpu(env);
    TaskState *ts = cs->opaque;

    return ts->swi_errno;
#else
    return syscall_err;
#endif
}

static target_ulong arm_semi_syscall_len;

static void arm_semi_cb(CPUState *cs, target_ulong ret, target_ulong err)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    target_ulong reg0 = is_a64(env) ? env->xregs[0] : env->regs[0];

    if (ret == (target_ulong)-1) {
        errno = err;
        set_swi_errno(env, -1);
        reg0 = ret;
    } else {
        /* Fixup syscalls that use nonstardard return conventions.  */
        switch (reg0) {
        case TARGET_SYS_WRITE:
        case TARGET_SYS_READ:
            reg0 = arm_semi_syscall_len - ret;
            break;
        case TARGET_SYS_SEEK:
            reg0 = 0;
            break;
        default:
            reg0 = ret;
            break;
        }
    }
    if (is_a64(env)) {
        env->xregs[0] = reg0;
    } else {
        env->regs[0] = reg0;
    }
}

static target_ulong arm_flen_buf(ARMCPU *cpu)
{
    /* Return an address in target memory of 64 bytes where the remote
     * gdb should write its stat struct. (The format of this structure
     * is defined by GDB's remote protocol and is not target-specific.)
     * We put this on the guest's stack just below SP.
     */
    CPUARMState *env = &cpu->env;
    target_ulong sp;

    if (is_a64(env)) {
        sp = env->xregs[31];
    } else {
        sp = env->regs[13];
    }

    return sp - 64;
}

static void arm_semi_flen_cb(CPUState *cs, target_ulong ret, target_ulong err)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    /* The size is always stored in big-endian order, extract
       the value. We assume the size always fit in 32 bits.  */
    uint32_t size;
    cpu_memory_rw_debug(cs, arm_flen_buf(cpu) + 32, (uint8_t *)&size, 4, 0);
    size = be32_to_cpu(size);
    if (is_a64(env)) {
        env->xregs[0] = size;
    } else {
        env->regs[0] = size;
    }
    errno = err;
    set_swi_errno(env, -1);
}

static int arm_semi_open_guestfd;

static void arm_semi_open_cb(CPUState *cs, target_ulong ret, target_ulong err)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    if (ret == (target_ulong)-1) {
        errno = err;
        set_swi_errno(env, -1);
        dealloc_guestfd(arm_semi_open_guestfd);
    } else {
        associate_guestfd(arm_semi_open_guestfd, ret);
        ret = arm_semi_open_guestfd;
    }

    if (is_a64(env)) {
        env->xregs[0] = ret;
    } else {
        env->regs[0] = ret;
    }
}

static target_ulong arm_gdb_syscall(ARMCPU *cpu, gdb_syscall_complete_cb cb,
                                    const char *fmt, ...)
{
    va_list va;
    CPUARMState *env = &cpu->env;

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
     * do_arm_semihosting() will write it straight back into X0.
     * (In linux-user mode, the callback will have happened before
     * gdb_do_syscallv() returns.)
     *
     * We should tidy this up so neither this function nor
     * do_arm_semihosting() return a value, so the mistake of
     * doing something with the return value is not possible to make.
     */

    return is_a64(env) ? env->xregs[0] : env->regs[0];
}

/*
 * Types for functions implementing various semihosting calls
 * for specific types of guest file descriptor. These must all
 * do the work and return the required return value for the guest,
 * setting the guest errno if appropriate.
 */
typedef uint32_t sys_closefn(ARMCPU *cpu, GuestFD *gf);
typedef uint32_t sys_writefn(ARMCPU *cpu, GuestFD *gf,
                             target_ulong buf, uint32_t len);
typedef uint32_t sys_readfn(ARMCPU *cpu, GuestFD *gf,
                            target_ulong buf, uint32_t len);
typedef uint32_t sys_isattyfn(ARMCPU *cpu, GuestFD *gf);
typedef uint32_t sys_seekfn(ARMCPU *cpu, GuestFD *gf,
                            target_ulong offset);
typedef uint32_t sys_flenfn(ARMCPU *cpu, GuestFD *gf);

static uint32_t host_closefn(ARMCPU *cpu, GuestFD *gf)
{
    CPUARMState *env = &cpu->env;

    /*
     * Only close the underlying host fd if it's one we opened on behalf
     * of the guest in SYS_OPEN.
     */
    if (gf->hostfd == STDIN_FILENO ||
        gf->hostfd == STDOUT_FILENO ||
        gf->hostfd == STDERR_FILENO) {
        return 0;
    }
    return set_swi_errno(env, close(gf->hostfd));
}

static uint32_t host_writefn(ARMCPU *cpu, GuestFD *gf,
                             target_ulong buf, uint32_t len)
{
    uint32_t ret;
    CPUARMState *env = &cpu->env;
    char *s = lock_user(VERIFY_READ, buf, len, 1);
    if (!s) {
        /* Return bytes not written on error */
        return len;
    }
    ret = set_swi_errno(env, write(gf->hostfd, s, len));
    unlock_user(s, buf, 0);
    if (ret == (uint32_t)-1) {
        ret = 0;
    }
    /* Return bytes not written */
    return len - ret;
}

static uint32_t host_readfn(ARMCPU *cpu, GuestFD *gf,
                            target_ulong buf, uint32_t len)
{
    uint32_t ret;
    CPUARMState *env = &cpu->env;
    char *s = lock_user(VERIFY_WRITE, buf, len, 0);
    if (!s) {
        /* return bytes not read */
        return len;
    }
    do {
        ret = set_swi_errno(env, read(gf->hostfd, s, len));
    } while (ret == -1 && errno == EINTR);
    unlock_user(s, buf, len);
    if (ret == (uint32_t)-1) {
        ret = 0;
    }
    /* Return bytes not read */
    return len - ret;
}

static uint32_t host_isattyfn(ARMCPU *cpu, GuestFD *gf)
{
    return isatty(gf->hostfd);
}

static uint32_t host_seekfn(ARMCPU *cpu, GuestFD *gf, target_ulong offset)
{
    CPUARMState *env = &cpu->env;
    uint32_t ret = set_swi_errno(env, lseek(gf->hostfd, offset, SEEK_SET));
    if (ret == (uint32_t)-1) {
        return -1;
    }
    return 0;
}

static uint32_t host_flenfn(ARMCPU *cpu, GuestFD *gf)
{
    CPUARMState *env = &cpu->env;
    struct stat buf;
    uint32_t ret = set_swi_errno(env, fstat(gf->hostfd, &buf));
    if (ret == (uint32_t)-1) {
        return -1;
    }
    return buf.st_size;
}

static uint32_t gdb_closefn(ARMCPU *cpu, GuestFD *gf)
{
    return arm_gdb_syscall(cpu, arm_semi_cb, "close,%x", gf->hostfd);
}

static uint32_t gdb_writefn(ARMCPU *cpu, GuestFD *gf,
                            target_ulong buf, uint32_t len)
{
    arm_semi_syscall_len = len;
    return arm_gdb_syscall(cpu, arm_semi_cb, "write,%x,%x,%x",
                           gf->hostfd, buf, len);
}

static uint32_t gdb_readfn(ARMCPU *cpu, GuestFD *gf,
                           target_ulong buf, uint32_t len)
{
    arm_semi_syscall_len = len;
    return arm_gdb_syscall(cpu, arm_semi_cb, "read,%x,%x,%x",
                           gf->hostfd, buf, len);
}

static uint32_t gdb_isattyfn(ARMCPU *cpu, GuestFD *gf)
{
    return arm_gdb_syscall(cpu, arm_semi_cb, "isatty,%x", gf->hostfd);
}

static uint32_t gdb_seekfn(ARMCPU *cpu, GuestFD *gf, target_ulong offset)
{
    return arm_gdb_syscall(cpu, arm_semi_cb, "lseek,%x,%x,0",
                           gf->hostfd, offset);
}

static uint32_t gdb_flenfn(ARMCPU *cpu, GuestFD *gf)
{
    return arm_gdb_syscall(cpu, arm_semi_flen_cb, "fstat,%x,%x",
                           gf->hostfd, arm_flen_buf(cpu));
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

static uint32_t featurefile_closefn(ARMCPU *cpu, GuestFD *gf)
{
    /* Nothing to do */
    return 0;
}

static uint32_t featurefile_writefn(ARMCPU *cpu, GuestFD *gf,
                                    target_ulong buf, uint32_t len)
{
    /* This fd can never be open for writing */
    CPUARMState *env = &cpu->env;

    errno = EBADF;
    return set_swi_errno(env, -1);
}

static uint32_t featurefile_readfn(ARMCPU *cpu, GuestFD *gf,
                                   target_ulong buf, uint32_t len)
{
    uint32_t i;
#ifndef CONFIG_USER_ONLY
    CPUARMState *env = &cpu->env;
#endif
    char *s;

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

static uint32_t featurefile_isattyfn(ARMCPU *cpu, GuestFD *gf)
{
    return 0;
}

static uint32_t featurefile_seekfn(ARMCPU *cpu, GuestFD *gf,
                                   target_ulong offset)
{
    gf->featurefile_offset = offset;
    return 0;
}

static uint32_t featurefile_flenfn(ARMCPU *cpu, GuestFD *gf)
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
#define GET_ARG(n) do {                                 \
    if (is_a64(env)) {                                  \
        if (get_user_u64(arg ## n, args + (n) * 8)) {   \
            errno = EFAULT;                             \
            return set_swi_errno(env, -1);              \
        }                                               \
    } else {                                            \
        if (get_user_u32(arg ## n, args + (n) * 4)) {   \
            errno = EFAULT;                             \
            return set_swi_errno(env, -1);              \
        }                                               \
    }                                                   \
} while (0)

#define SET_ARG(n, val)                                 \
    (is_a64(env) ?                                      \
     put_user_u64(val, args + (n) * 8) :                \
     put_user_u32(val, args + (n) * 4))

/*
 * Do a semihosting call.
 *
 * The specification always says that the "return register" either
 * returns a specific value or is corrupted, so we don't need to
 * report to our caller whether we are returning a value or trying to
 * leave the register unchanged. We use 0xdeadbeef as the return value
 * when there isn't a defined return value for the call.
 */
target_ulong do_arm_semihosting(CPUARMState *env)
{
    ARMCPU *cpu = env_archcpu(env);
    CPUState *cs = env_cpu(env);
    target_ulong args;
    target_ulong arg0, arg1, arg2, arg3;
    char * s;
    int nr;
    uint32_t ret;
    uint32_t len;
    GuestFD *gf;

    if (is_a64(env)) {
        /* Note that the syscall number is in W0, not X0 */
        nr = env->xregs[0] & 0xffffffffU;
        args = env->xregs[1];
    } else {
        nr = env->regs[0];
        args = env->regs[1];
    }

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
            return set_swi_errno(env, -1);
        }
        if (arg1 >= 12) {
            unlock_user(s, arg0, 0);
            errno = EINVAL;
            return set_swi_errno(env, -1);
        }

        guestfd = alloc_guestfd();
        if (guestfd < 0) {
            unlock_user(s, arg0, 0);
            errno = EMFILE;
            return set_swi_errno(env, -1);
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
                return set_swi_errno(env, -1);
            }
            init_featurefile_guestfd(guestfd);
            return guestfd;
        }

        if (use_gdb_syscalls()) {
            arm_semi_open_guestfd = guestfd;
            ret = arm_gdb_syscall(cpu, arm_semi_open_cb, "open,%s,%x,1a4", arg0,
                                  (int)arg2+1, gdb_open_modeflags[arg1]);
        } else {
            ret = set_swi_errno(env, open(s, open_modeflags[arg1], 0644));
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
            return set_swi_errno(env, -1);
        }

        ret = guestfd_fns[gf->type].closefn(cpu, gf);
        dealloc_guestfd(arg0);
        return ret;
    case TARGET_SYS_WRITEC:
        qemu_semihosting_console_outc(env, args);
        return 0xdeadbeef;
    case TARGET_SYS_WRITE0:
        return qemu_semihosting_console_outs(env, args);
    case TARGET_SYS_WRITE:
        GET_ARG(0);
        GET_ARG(1);
        GET_ARG(2);
        len = arg2;

        gf = get_guestfd(arg0);
        if (!gf) {
            errno = EBADF;
            return set_swi_errno(env, -1);
        }

        return guestfd_fns[gf->type].writefn(cpu, gf, arg1, len);
    case TARGET_SYS_READ:
        GET_ARG(0);
        GET_ARG(1);
        GET_ARG(2);
        len = arg2;

        gf = get_guestfd(arg0);
        if (!gf) {
            errno = EBADF;
            return set_swi_errno(env, -1);
        }

        return guestfd_fns[gf->type].readfn(cpu, gf, arg1, len);
    case TARGET_SYS_READC:
        return qemu_semihosting_console_inc(env);
    case TARGET_SYS_ISTTY:
        GET_ARG(0);

        gf = get_guestfd(arg0);
        if (!gf) {
            errno = EBADF;
            return set_swi_errno(env, -1);
        }

        return guestfd_fns[gf->type].isattyfn(cpu, gf);
    case TARGET_SYS_SEEK:
        GET_ARG(0);
        GET_ARG(1);

        gf = get_guestfd(arg0);
        if (!gf) {
            errno = EBADF;
            return set_swi_errno(env, -1);
        }

        return guestfd_fns[gf->type].seekfn(cpu, gf, arg1);
    case TARGET_SYS_FLEN:
        GET_ARG(0);

        gf = get_guestfd(arg0);
        if (!gf) {
            errno = EBADF;
            return set_swi_errno(env, -1);
        }

        return guestfd_fns[gf->type].flenfn(cpu, gf);
    case TARGET_SYS_TMPNAM:
        qemu_log_mask(LOG_UNIMP, "%s: SYS_TMPNAM not implemented", __func__);
        return -1;
    case TARGET_SYS_REMOVE:
        GET_ARG(0);
        GET_ARG(1);
        if (use_gdb_syscalls()) {
            ret = arm_gdb_syscall(cpu, arm_semi_cb, "unlink,%s",
                                  arg0, (int)arg1+1);
        } else {
            s = lock_user_string(arg0);
            if (!s) {
                errno = EFAULT;
                return set_swi_errno(env, -1);
            }
            ret =  set_swi_errno(env, remove(s));
            unlock_user(s, arg0, 0);
        }
        return ret;
    case TARGET_SYS_RENAME:
        GET_ARG(0);
        GET_ARG(1);
        GET_ARG(2);
        GET_ARG(3);
        if (use_gdb_syscalls()) {
            return arm_gdb_syscall(cpu, arm_semi_cb, "rename,%s,%s",
                                   arg0, (int)arg1+1, arg2, (int)arg3+1);
        } else {
            char *s2;
            s = lock_user_string(arg0);
            s2 = lock_user_string(arg2);
            if (!s || !s2) {
                errno = EFAULT;
                ret = set_swi_errno(env, -1);
            } else {
                ret = set_swi_errno(env, rename(s, s2));
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
        return set_swi_errno(env, time(NULL));
    case TARGET_SYS_SYSTEM:
        GET_ARG(0);
        GET_ARG(1);
        if (use_gdb_syscalls()) {
            return arm_gdb_syscall(cpu, arm_semi_cb, "system,%s",
                                   arg0, (int)arg1+1);
        } else {
            s = lock_user_string(arg0);
            if (!s) {
                errno = EFAULT;
                return set_swi_errno(env, -1);
            }
            ret = set_swi_errno(env, system(s));
            unlock_user(s, arg0, 0);
            return ret;
        }
    case TARGET_SYS_ERRNO:
        return get_swi_errno(env);
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
                return set_swi_errno(env, -1);
            }

            /* Adjust the command-line length.  */
            if (SET_ARG(1, output_size - 1)) {
                /* Couldn't write back to argument block */
                errno = EFAULT;
                return set_swi_errno(env, -1);
            }

            /* Lock the buffer on the ARM side.  */
            output_buffer = lock_user(VERIFY_WRITE, arg0, output_size, 0);
            if (!output_buffer) {
                errno = EFAULT;
                return set_swi_errno(env, -1);
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
                status = set_swi_errno(env, -1);
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
                limit = ts->heap_base + ARM_ANGEL_HEAP_SIZE;
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
            limit = ram_size;
            /* TODO: Make this use the limit of the loaded application.  */
            retvals[0] = limit / 2;
            retvals[1] = limit;
            retvals[2] = limit; /* Stack base */
            retvals[3] = 0; /* Stack limit.  */
#endif

            for (i = 0; i < ARRAY_SIZE(retvals); i++) {
                bool fail;

                if (is_a64(env)) {
                    fail = put_user_u64(retvals[i], arg0 + i * 8);
                } else {
                    fail = put_user_u32(retvals[i], arg0 + i * 4);
                }

                if (fail) {
                    /* Couldn't write back to argument block */
                    errno = EFAULT;
                    return set_swi_errno(env, -1);
                }
            }
            return 0;
        }
    case TARGET_SYS_EXIT:
    case TARGET_SYS_EXIT_EXTENDED:
        if (nr == TARGET_SYS_EXIT_EXTENDED || is_a64(env)) {
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
        gdb_exit(env, ret);
        exit(ret);
    case TARGET_SYS_SYNCCACHE:
        /*
         * Clean the D-cache and invalidate the I-cache for the specified
         * virtual address range. This is a nop for us since we don't
         * implement caches. This is only present on A64.
         */
        if (is_a64(env)) {
            return 0;
        }
        /* fall through -- invalid for A32/T32 */
    default:
        fprintf(stderr, "qemu: Unsupported SemiHosting SWI 0x%02x\n", nr);
        cpu_dump_state(cs, stderr, 0);
        abort();
    }
}
