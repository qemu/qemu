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
 *     https://github.com/ARM-software/abi-aa/blob/main/semihosting/semihosting.rst
 *
 *  RISC-V Semihosting is documented in:
 *     RISC-V Semihosting
 *     https://github.com/riscv/riscv-semihosting-spec/blob/main/riscv-semihosting-spec.adoc
 */

#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "exec/gdbstub.h"
#include "gdbstub/syscalls.h"
#include "semihosting/semihost.h"
#include "semihosting/console.h"
#include "semihosting/common-semi.h"
#include "semihosting/guestfd.h"
#include "semihosting/syscalls.h"

#ifdef CONFIG_USER_ONLY
#include "qemu.h"

#define COMMON_SEMI_HEAP_SIZE (128 * 1024 * 1024)
#else
#include "qemu/cutils.h"
#include "hw/loader.h"
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

static int gdb_open_modeflags[12] = {
    GDB_O_RDONLY,
    GDB_O_RDONLY,
    GDB_O_RDWR,
    GDB_O_RDWR,
    GDB_O_WRONLY | GDB_O_CREAT | GDB_O_TRUNC,
    GDB_O_WRONLY | GDB_O_CREAT | GDB_O_TRUNC,
    GDB_O_RDWR | GDB_O_CREAT | GDB_O_TRUNC,
    GDB_O_RDWR | GDB_O_CREAT | GDB_O_TRUNC,
    GDB_O_WRONLY | GDB_O_CREAT | GDB_O_APPEND,
    GDB_O_WRONLY | GDB_O_CREAT | GDB_O_APPEND,
    GDB_O_RDWR | GDB_O_CREAT | GDB_O_APPEND,
    GDB_O_RDWR | GDB_O_CREAT | GDB_O_APPEND,
};

/*
 * For ARM semihosting, we have a separate structure for routing
 * data for the console which is outside the guest fd address space.
 */
static GuestFD console_in_gf;
static GuestFD console_out_gf;

#ifndef CONFIG_USER_ONLY

/**
 * common_semi_find_bases: find information about ram and heap base
 *
 * This function attempts to provide meaningful numbers for RAM and
 * HEAP base addresses. The rambase is simply the lowest addressable
 * RAM position. For the heapbase we ask the loader to scan the
 * address space and the largest available gap by querying the "ROM"
 * regions.
 *
 * Returns: a structure with the numbers we need.
 */

typedef struct LayoutInfo {
    vaddr rambase;
    size_t ramsize;
    hwaddr heapbase;
    hwaddr heaplimit;
} LayoutInfo;

static bool find_ram_cb(Int128 start, Int128 len, const MemoryRegion *mr,
                        hwaddr offset_in_region, void *opaque)
{
    LayoutInfo *info = (LayoutInfo *) opaque;
    uint64_t size = int128_get64(len);

    if (!mr->ram || mr->readonly) {
        return false;
    }

    if (size > info->ramsize) {
        info->rambase = int128_get64(start);
        info->ramsize = size;
    }

    /* search exhaustively for largest RAM */
    return false;
}

static LayoutInfo common_semi_find_bases(CPUState *cs)
{
    FlatView *fv;
    LayoutInfo info = { 0, 0, 0, 0 };

    RCU_READ_LOCK_GUARD();

    fv = address_space_to_flatview(cs->as);
    flatview_for_each_range(fv, find_ram_cb, &info);

    /*
     * If we have found the RAM lets iterate through the ROM blobs to
     * work out the best place for the remainder of RAM and split it
     * equally between stack and heap.
     */
    if (info.rambase || info.ramsize > 0) {
        RomGap gap = rom_find_largest_gap_between(info.rambase, info.ramsize);
        info.heapbase = gap.base;
        info.heaplimit = gap.base + gap.size;
    }

    return info;
}

#endif

#include "semihosting/common-semi.h"

/*
 * Read the input value from the argument block; fail the semihosting
 * call if the memory read fails. Eventually we could use a generic
 * CPUState helper function here.
 * Note that GET_ARG() handles memory access errors by jumping to
 * do_fault, so must be used as the first thing done in handling a
 * semihosting call, to avoid accidentally leaking allocated resources.
 * SET_ARG(), since it unavoidably happens late, instead returns an
 * error indication (0 on success, non-0 for error) which the caller
 * should check.
 */

#define GET_ARG(n) do {                                 \
    if (is_64bit_semihosting(env)) {                    \
        if (get_user_u64(arg ## n, args + (n) * 8)) {   \
            goto do_fault;                              \
        }                                               \
    } else {                                            \
        if (get_user_u32(arg ## n, args + (n) * 4)) {   \
            goto do_fault;                              \
        }                                               \
    }                                                   \
} while (0)

#define SET_ARG(n, val)                                 \
    (is_64bit_semihosting(env) ?                        \
     put_user_u64(val, args + (n) * 8) :                \
     put_user_u32(val, args + (n) * 4))


/*
 * The semihosting API has no concept of its errno being thread-safe,
 * as the API design predates SMP CPUs and was intended as a simple
 * real-hardware set of debug functionality. For QEMU, we make the
 * errno be per-thread in linux-user mode; in system-mode it is a simple
 * global, and we assume that the guest takes care of avoiding any races.
 */
#ifndef CONFIG_USER_ONLY
static uint64_t syscall_err;

#include "semihosting/uaccess.h"
#endif

static inline uint32_t get_swi_errno(CPUState *cs)
{
#ifdef CONFIG_USER_ONLY
    TaskState *ts = get_task_state(cs);

    return ts->swi_errno;
#else
    return syscall_err;
#endif
}

static void common_semi_cb(CPUState *cs, uint64_t ret, int err)
{
    if (err) {
#ifdef CONFIG_USER_ONLY
        TaskState *ts = get_task_state(cs);
        ts->swi_errno = err;
#else
        syscall_err = err;
#endif
    }
    common_semi_set_ret(cs, ret);
}

/*
 * Use 0xdeadbeef as the return value when there isn't a defined
 * return value for the call.
 */
static void common_semi_dead_cb(CPUState *cs, uint64_t ret, int err)
{
    common_semi_set_ret(cs, 0xdeadbeef);
}

/*
 * SYS_READ and SYS_WRITE always return the number of bytes not read/written.
 * There is no error condition, other than returning the original length.
 */
static void common_semi_rw_cb(CPUState *cs, uint64_t ret, int err)
{
    /* Recover the original length from the third argument. */
    CPUArchState *env G_GNUC_UNUSED = cpu_env(cs);
    uint64_t args = common_semi_arg(cs, 1);
    uint64_t arg2;
    GET_ARG(2);

    if (err) {
 do_fault:
        ret = 0; /* error: no bytes transmitted */
    }
    common_semi_set_ret(cs, arg2 - ret);
}

/*
 * Convert from Posix ret+errno to Arm SYS_ISTTY return values.
 * With gdbstub, err is only ever set for protocol errors to EIO.
 */
static void common_semi_istty_cb(CPUState *cs, uint64_t ret, int err)
{
    if (err) {
        ret = (err == ENOTTY ? 0 : -1);
    }
    common_semi_cb(cs, ret, err);
}

/*
 * SYS_SEEK returns 0 on success, not the resulting offset.
 */
static void common_semi_seek_cb(CPUState *cs, uint64_t ret, int err)
{
    if (!err) {
        ret = 0;
    }
    common_semi_cb(cs, ret, err);
}

/*
 * Return an address in target memory of 64 bytes where the remote
 * gdb should write its stat struct. (The format of this structure
 * is defined by GDB's remote protocol and is not target-specific.)
 * We put this on the guest's stack just below SP.
 */
static uint64_t common_semi_flen_buf(CPUState *cs)
{
    vaddr sp = common_semi_stack_bottom(cs);
    return sp - 64;
}

static void
common_semi_flen_fstat_cb(CPUState *cs, uint64_t ret, int err)
{
    if (!err) {
        /* The size is always stored in big-endian order, extract the value. */
        uint64_t size;
        if (cpu_memory_rw_debug(cs, common_semi_flen_buf(cs) +
                                offsetof(struct gdb_stat, gdb_st_size),
                                &size, 8, 0)) {
            ret = -1, err = EFAULT;
        } else {
            ret = be64_to_cpu(size);
        }
    }
    common_semi_cb(cs, ret, err);
}

static void
common_semi_readc_cb(CPUState *cs, uint64_t ret, int err)
{
    if (!err) {
        CPUArchState *env G_GNUC_UNUSED = cpu_env(cs);
        uint8_t ch;

        if (get_user_u8(ch, common_semi_stack_bottom(cs) - 1)) {
            ret = -1, err = EFAULT;
        } else {
            ret = ch;
        }
    }
    common_semi_cb(cs, ret, err);
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

bool semihosting_arm_compatible(void)
{
    return true;
}

void semihosting_arm_compatible_init(void)
{
    /* For ARM-compat, the console is in a separate namespace. */
    if (use_gdb_syscalls()) {
        console_in_gf.type = GuestFDGDB;
        console_in_gf.hostfd = 0;
        console_out_gf.type = GuestFDGDB;
        console_out_gf.hostfd = 2;
    } else {
        console_in_gf.type = GuestFDConsole;
        console_out_gf.type = GuestFDConsole;
    }
}

/*
 * Do a semihosting call.
 *
 * The specification always says that the "return register" either
 * returns a specific value or is corrupted, so we don't need to
 * report to our caller whether we are returning a value or trying to
 * leave the register unchanged.
 */
void do_common_semihosting(CPUState *cs)
{
    CPUArchState *env = cpu_env(cs);
    uint64_t args;
    uint64_t arg0, arg1, arg2, arg3;
    uint64_t ul_ret;
    char * s;
    int nr;
    int64_t elapsed;

    nr = common_semi_arg(cs, 0) & 0xffffffffU;
    args = common_semi_arg(cs, 1);

    switch (nr) {
    case TARGET_SYS_OPEN:
    {
        int ret, err = 0;
        int hostfd;

        GET_ARG(0);
        GET_ARG(1);
        GET_ARG(2);
        s = lock_user_string(arg0);
        if (!s) {
            goto do_fault;
        }
        if (arg1 >= 12) {
            unlock_user(s, arg0, 0);
            common_semi_cb(cs, -1, EINVAL);
            break;
        }

        if (strcmp(s, ":tt") == 0) {
            /*
             * We implement SH_EXT_STDOUT_STDERR, so:
             *  open for read == stdin
             *  open for write == stdout
             *  open for append == stderr
             */
            if (arg1 < 4) {
                hostfd = STDIN_FILENO;
            } else if (arg1 < 8) {
                hostfd = STDOUT_FILENO;
            } else {
                hostfd = STDERR_FILENO;
            }
            ret = alloc_guestfd();
            associate_guestfd(ret, hostfd);
        } else if (strcmp(s, ":semihosting-features") == 0) {
            /* We must fail opens for modes other than 0 ('r') or 1 ('rb') */
            if (arg1 != 0 && arg1 != 1) {
                ret = -1;
                err = EACCES;
            } else {
                ret = alloc_guestfd();
                staticfile_guestfd(ret, featurefile_data,
                                   sizeof(featurefile_data));
            }
        } else {
            unlock_user(s, arg0, 0);
            semihost_sys_open(cs, common_semi_cb, arg0, arg2 + 1,
                              gdb_open_modeflags[arg1], 0644);
            break;
        }
        unlock_user(s, arg0, 0);
        common_semi_cb(cs, ret, err);
        break;
    }

    case TARGET_SYS_CLOSE:
        GET_ARG(0);
        semihost_sys_close(cs, common_semi_cb, arg0);
        break;

    case TARGET_SYS_WRITEC:
        /*
         * FIXME: the byte to be written is in a uint64_t slot,
         * which means this is wrong for a big-endian guest.
         */
        semihost_sys_write_gf(cs, common_semi_dead_cb,
                              &console_out_gf, args, 1);
        break;

    case TARGET_SYS_WRITE0:
        {
            ssize_t len = target_strlen(args);
            if (len < 0) {
                common_semi_dead_cb(cs, -1, EFAULT);
            } else {
                semihost_sys_write_gf(cs, common_semi_dead_cb,
                                      &console_out_gf, args, len);
            }
        }
        break;

    case TARGET_SYS_WRITE:
        GET_ARG(0);
        GET_ARG(1);
        GET_ARG(2);
        semihost_sys_write(cs, common_semi_rw_cb, arg0, arg1, arg2);
        break;

    case TARGET_SYS_READ:
        GET_ARG(0);
        GET_ARG(1);
        GET_ARG(2);
        semihost_sys_read(cs, common_semi_rw_cb, arg0, arg1, arg2);
        break;

    case TARGET_SYS_READC:
        semihost_sys_read_gf(cs, common_semi_readc_cb, &console_in_gf,
                             common_semi_stack_bottom(cs) - 1, 1);
        break;

    case TARGET_SYS_ISERROR:
    {
        GET_ARG(0);
        bool ret = is_64bit_semihosting(env) ?
                   (int64_t)arg0 < 0 : (int32_t)arg0 < 0;
        common_semi_set_ret(cs, ret);
        break;
    }
    case TARGET_SYS_ISTTY:
        GET_ARG(0);
        semihost_sys_isatty(cs, common_semi_istty_cb, arg0);
        break;

    case TARGET_SYS_SEEK:
        GET_ARG(0);
        GET_ARG(1);
        semihost_sys_lseek(cs, common_semi_seek_cb, arg0, arg1, GDB_SEEK_SET);
        break;

    case TARGET_SYS_FLEN:
        GET_ARG(0);
        semihost_sys_flen(cs, common_semi_flen_fstat_cb, common_semi_cb,
                          arg0, common_semi_flen_buf(cs));
        break;

    case TARGET_SYS_TMPNAM:
    {
        int len;
        char *p;

        GET_ARG(0);
        GET_ARG(1);
        GET_ARG(2);
        len = asprintf(&s, "%s/qemu-%x%02x", g_get_tmp_dir(),
                       getpid(), (int)arg1 & 0xff);
        if (len < 0) {
            common_semi_set_ret(cs, -1);
            break;
        }

        /* Allow for trailing NUL */
        len++;
        /* Make sure there's enough space in the buffer */
        if (len > arg2) {
            free(s);
            common_semi_set_ret(cs, -1);
            break;
        }
        p = lock_user(VERIFY_WRITE, arg0, len, 0);
        if (!p) {
            free(s);
            goto do_fault;
        }
        memcpy(p, s, len);
        unlock_user(p, arg0, len);
        free(s);
        common_semi_set_ret(cs, 0);
        break;
    }

    case TARGET_SYS_REMOVE:
        GET_ARG(0);
        GET_ARG(1);
        semihost_sys_remove(cs, common_semi_cb, arg0, arg1 + 1);
        break;

    case TARGET_SYS_RENAME:
        GET_ARG(0);
        GET_ARG(1);
        GET_ARG(2);
        GET_ARG(3);
        semihost_sys_rename(cs, common_semi_cb, arg0, arg1 + 1, arg2, arg3 + 1);
        break;

    case TARGET_SYS_CLOCK:
        common_semi_set_ret(cs, clock() / (CLOCKS_PER_SEC / 100));
        break;

    case TARGET_SYS_TIME:
        ul_ret = time(NULL);
        common_semi_cb(cs, ul_ret, ul_ret == -1 ? errno : 0);
        break;

    case TARGET_SYS_SYSTEM:
        GET_ARG(0);
        GET_ARG(1);
        semihost_sys_system(cs, common_semi_cb, arg0, arg1 + 1);
        break;

    case TARGET_SYS_ERRNO:
        common_semi_set_ret(cs, get_swi_errno(cs));
        break;

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
            TaskState *ts = get_task_state(cs);
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

            output_size = ts->info->env_strings - ts->info->arg_strings;
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
                common_semi_cb(cs, -1, E2BIG);
                break;
            }

            /* Adjust the command-line length.  */
            if (SET_ARG(1, output_size - 1)) {
                /* Couldn't write back to argument block */
                goto do_fault;
            }

            /* Lock the buffer on the ARM side.  */
            output_buffer = lock_user(VERIFY_WRITE, arg0, output_size, 0);
            if (!output_buffer) {
                goto do_fault;
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

            if (copy_from_user(output_buffer, ts->info->arg_strings,
                               output_size)) {
                unlock_user(output_buffer, arg0, 0);
                goto do_fault;
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
            common_semi_cb(cs, status, 0);
        }
        break;

    case TARGET_SYS_HEAPINFO:
        {
            uint64_t retvals[4];
            int i;
#ifdef CONFIG_USER_ONLY
            TaskState *ts = get_task_state(cs);
            static abi_ulong heapbase, heaplimit;
#else
            LayoutInfo info = common_semi_find_bases(cs);
#endif

            GET_ARG(0);

#ifdef CONFIG_USER_ONLY
            /*
             * Some C libraries assume the heap immediately follows .bss, so
             * allocate it using sbrk.
             */
            if (!heaplimit) {
                heapbase = do_brk(0);
                /* Try a big heap, and reduce the size if that fails.  */
                for (abi_ulong size = COMMON_SEMI_HEAP_SIZE; ; size >>= 1) {
                    abi_ulong limit = heapbase + size;
                    abi_ulong ret = do_brk(limit);
                    if (ret >= limit) {
                        heaplimit = limit;
                        break;
                    }
                }
            }
            retvals[0] = heapbase;
            retvals[1] = heaplimit;
            /*
             * Note that semihosting is *not* thread aware.
             * Always return the stack base of the main thread.
             */
            retvals[2] = ts->info->start_stack;
            retvals[3] = 0; /* Stack limit.  */
#else
            retvals[0] = info.heapbase;  /* Heap Base */
            retvals[1] = info.heaplimit; /* Heap Limit */
            retvals[2] = info.heaplimit; /* Stack base */
            retvals[3] = info.heapbase;  /* Stack limit.  */
#endif

            for (i = 0; i < ARRAY_SIZE(retvals); i++) {
                bool fail;

                if (is_64bit_semihosting(env)) {
                    fail = put_user_u64(retvals[i], arg0 + i * 8);
                } else {
                    fail = put_user_u32(retvals[i], arg0 + i * 4);
                }

                if (fail) {
                    /* Couldn't write back to argument block */
                    goto do_fault;
                }
            }
            common_semi_set_ret(cs, 0);
        }
        break;

    case TARGET_SYS_EXIT:
    case TARGET_SYS_EXIT_EXTENDED:
    {
        uint32_t ret;

        if (nr == TARGET_SYS_EXIT_EXTENDED ||
            common_semi_sys_exit_is_extended(cs)) {
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
    }

    case TARGET_SYS_ELAPSED:
        elapsed = get_clock() - clock_start;
        if (is_64bit_semihosting(env)) {
            if (SET_ARG(0, elapsed)) {
                goto do_fault;
            }
        } else {
            if (SET_ARG(0, (uint32_t) elapsed) ||
                SET_ARG(1, (uint32_t) (elapsed >> 32))) {
                goto do_fault;
            }
        }
        common_semi_set_ret(cs, 0);
        break;

    case TARGET_SYS_TICKFREQ:
        /* qemu always uses nsec */
        common_semi_set_ret(cs, 1000000000);
        break;

    case TARGET_SYS_SYNCCACHE:
        /*
         * Clean the D-cache and invalidate the I-cache for the specified
         * virtual address range. This is a nop for us since we don't
         * implement caches. This is only present on A64.
         */
        if (common_semi_has_synccache(env)) {
            common_semi_set_ret(cs, 0);
            break;
        }
        /* fall through */
    default:
        fprintf(stderr, "qemu: Unsupported SemiHosting SWI 0x%02x\n", nr);
        cpu_dump_state(cs, stderr, 0);
        abort();

    do_fault:
        common_semi_cb(cs, -1, EFAULT);
        break;
    }
}
