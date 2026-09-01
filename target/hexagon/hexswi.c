/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "cpu_helper.h"
#include "exec/helper-proto.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "arch.h"
#include "internal.h"
#include "macros.h"
#include "sys_macros.h"
#include "accel/tcg/cpu-loop.h"
#include "tcg/tcg-op.h"
#include "hex_mmu.h"
#include "hexswi.h"
#include "hw/hexagon/hexagon_globalreg.h"

#ifdef CONFIG_USER_ONLY
#error "This file is only used in system emulation"
#endif

#include "semihosting/common-semi.h"
#include "semihosting/console.h"
#include "semihosting/syscalls.h"
#include "semihosting/guestfd.h"
#include "system/runstate.h"

/* non-arm-compatible semihosting calls */
#define HEXAGON_SPECIFIC_SWI_FLAGS \
    DEF_SWI_FLAG(OPEN,             0x01) \
    DEF_SWI_FLAG(ISTTY,            0x09) \
    DEF_SWI_FLAG(HEAPINFO,         0x16) \
    DEF_SWI_FLAG(EXCEPTION,        0x18) \
    DEF_SWI_FLAG(SEEK,             0x0A) \
    DEF_SWI_FLAG(READ_CYCLES,      0x40) \
    DEF_SWI_FLAG(PROF_ON,          0x41) \
    DEF_SWI_FLAG(PROF_OFF,         0x42) \
    DEF_SWI_FLAG(WRITECREG,        0x43) \
    DEF_SWI_FLAG(READ_TCYCLES,     0x44) \
    DEF_SWI_FLAG(LOG_EVENT,        0x45) \
    DEF_SWI_FLAG(REDRAW,           0x46) \
    DEF_SWI_FLAG(READ_ICOUNT,      0x47) \
    DEF_SWI_FLAG(PROF_STATSRESET,  0x48) \
    DEF_SWI_FLAG(DUMP_PMU_STATS,   0x4a) \
    DEF_SWI_FLAG(READ_PCYCLES,     0x52) \
    DEF_SWI_FLAG(COREDUMP,         0xCD) \
    DEF_SWI_FLAG(FTELL,            0x100) \
    DEF_SWI_FLAG(FSTAT,            0x101) \
    DEF_SWI_FLAG(STAT,             0x103) \
    DEF_SWI_FLAG(GETCWD,           0x104) \
    DEF_SWI_FLAG(ACCESS,           0x105) \
    DEF_SWI_FLAG(OPENDIR,          0x180) \
    DEF_SWI_FLAG(CLOSEDIR,         0x181) \
    DEF_SWI_FLAG(READDIR,          0x182) \
    DEF_SWI_FLAG(EXEC,             0x185) \
    DEF_SWI_FLAG(FTRUNC,           0x186)

/*
 * We use the arm-compatible semihosting routines for these ones, but we do
 * need some hexagon-specific preprocessing.
 */
#define HEX_SYS_WRITE       0x05
#define HEX_SYS_READ        0x06
#define HEX_SYS_READC       0x07

enum hex_swi_flag {
    HEX_SYS_OPEN = 0x01,
    HEX_SYS_ISTTY = 0x09,
    HEX_SYS_HEAPINFO = 0x16,
    HEX_SYS_EXCEPTION = 0x18,
    HEX_SYS_SEEK = 0x0A,
    HEX_SYS_READ_CYCLES = 0x40,
    HEX_SYS_PROF_ON = 0x41,
    HEX_SYS_PROF_OFF = 0x42,
    HEX_SYS_WRITECREG = 0x43,
    HEX_SYS_READ_TCYCLES = 0x44,
    HEX_SYS_LOG_EVENT = 0x45,
    HEX_SYS_REDRAW = 0x46,
    HEX_SYS_READ_ICOUNT = 0x47,
    HEX_SYS_PROF_STATSRESET = 0x48,
    HEX_SYS_DUMP_PMU_STATS = 0x4a,
    HEX_SYS_READ_PCYCLES = 0x52,
    HEX_SYS_COREDUMP = 0xCD,
    HEX_SYS_FTELL = 0x100,
    HEX_SYS_FSTAT = 0x101,
    HEX_SYS_STAT = 0x103,
    HEX_SYS_GETCWD = 0x104,
    HEX_SYS_ACCESS = 0x105,
    HEX_SYS_OPENDIR = 0x180,
    HEX_SYS_CLOSEDIR = 0x181,
    HEX_SYS_READDIR = 0x182,
    HEX_SYS_EXEC = 0x185,
    HEX_SYS_FTRUNC = 0x186,
};

#define DEF_SWI_FLAG(_, val) case val:
static inline bool is_hexagon_specific_swi_flag(enum hex_swi_flag what_swi)
{
    switch (what_swi) {
    HEXAGON_SPECIFIC_SWI_FLAGS
        return true;
    }
    return false;
}
#undef DEF_SWI_FLAG

static const unsigned int angel_to_host_filemode_table[] = {
    O_RDONLY,
    O_RDONLY | O_BINARY,
    O_RDWR,
    O_RDWR | O_BINARY,
    O_WRONLY | O_CREAT | O_TRUNC,
    O_WRONLY | O_CREAT | O_TRUNC | O_BINARY,
    O_RDWR | O_CREAT | O_TRUNC,
    O_RDWR | O_CREAT | O_TRUNC | O_BINARY,
    O_WRONLY | O_APPEND | O_CREAT,
    O_WRONLY | O_APPEND | O_CREAT | O_BINARY,
    O_RDWR | O_APPEND | O_CREAT,
    O_RDWR | O_APPEND | O_CREAT | O_BINARY,
    O_RDWR | O_CREAT,
    O_RDWR | O_CREAT | O_EXCL
};

/*
 * This must match the caller's definition, it would be in the
 * caller's angel.h or equivalent header.
 */
struct __SYS_STAT {
    uint64_t dev;
    uint64_t ino;
    uint32_t mode;
    uint32_t nlink;
    uint64_t rdev;
    uint32_t size;
    uint32_t __pad1;
    uint32_t atime;
    uint32_t mtime;
    uint32_t ctime;
    uint32_t __pad2;
} sys_stat;

static void init_semihosting_guestfds(void)
{
    static gsize initialized;

    if (g_once_init_enter(&initialized)) {
        if (qemu_semihosting_console_has_chardev()) {
            alloc_guestfd();
            console_guestfd(0);
            alloc_guestfd();
            console_guestfd(1);
            alloc_guestfd();
            console_guestfd(2);
        } else {
            alloc_guestfd();
            associate_guestfd(0, 0);
            alloc_guestfd();
            associate_guestfd(1, 1);
            alloc_guestfd();
            associate_guestfd(2, 2);
        }
        g_once_init_leave(&initialized, 1);
    }
}

static void do_preload(CPUHexagonState *env, target_ulong swi_info, bool load)
{
    uint32_t addr, count;
    uintptr_t retaddr = 0;

    hexagon_read_memory(env, swi_info + 4, 4, &addr, retaddr);
    hexagon_read_memory(env, swi_info + 8, 4, &count, retaddr);
    hexagon_peek_memory_range(env, addr, count, retaddr);
}

/* Hexagon semihosting errno values */
enum {
    TARGET_EPERM        =  1,
    TARGET_ENOENT       =  2,
    TARGET_EINTR        =  4,
    TARGET_EIO          =  5,
    TARGET_ENXIO        =  6,
    TARGET_EBADF        =  9,
    TARGET_EAGAIN       = 11,
    TARGET_ENOMEM       = 12,
    TARGET_EACCES       = 13,
    TARGET_EFAULT       = 14,
    TARGET_EBUSY        = 16,
    TARGET_EEXIST       = 17,
    TARGET_EXDEV        = 18,
    TARGET_ENODEV       = 19,
    TARGET_ENOTDIR      = 20,
    TARGET_EISDIR       = 21,
    TARGET_EINVAL       = 22,
    TARGET_ENFILE       = 23,
    TARGET_EMFILE       = 24,
    TARGET_ENOTTY       = 25,
    TARGET_ETXTBSY      = 26,
    TARGET_EFBIG        = 27,
    TARGET_ENOSPC       = 28,
    TARGET_ESPIPE       = 29,
    TARGET_EROFS        = 30,
    TARGET_EMLINK       = 31,
    TARGET_EPIPE        = 32,
    TARGET_ERANGE       = 34,
    TARGET_ENAMETOOLONG = 36,
    TARGET_ENOSYS       = 38,
    TARGET_ELOOP        = 40,
    TARGET_EOVERFLOW    = 75,
};

/* Map host errno to hexagon semihosting errno */
static uint32_t errno_h2g(uint32_t err)
{
    switch (err) {
    case 0:            return 0;
    case EPERM:        return TARGET_EPERM;
    case ENOENT:       return TARGET_ENOENT;
    case EINTR:        return TARGET_EINTR;
    case EIO:          return TARGET_EIO;
    case ENXIO:        return TARGET_ENXIO;
    case EBADF:        return TARGET_EBADF;
    case EAGAIN:       return TARGET_EAGAIN;
    case ENOMEM:       return TARGET_ENOMEM;
    case EACCES:       return TARGET_EACCES;
    case EFAULT:       return TARGET_EFAULT;
    case EBUSY:        return TARGET_EBUSY;
    case EEXIST:       return TARGET_EEXIST;
    case EXDEV:        return TARGET_EXDEV;
    case ENODEV:       return TARGET_ENODEV;
    case ENOTDIR:      return TARGET_ENOTDIR;
    case EISDIR:       return TARGET_EISDIR;
    case EINVAL:       return TARGET_EINVAL;
    case ENFILE:       return TARGET_ENFILE;
    case EMFILE:       return TARGET_EMFILE;
    case ENOTTY:       return TARGET_ENOTTY;
    case ETXTBSY:      return TARGET_ETXTBSY;
    case EFBIG:        return TARGET_EFBIG;
    case ENOSPC:       return TARGET_ENOSPC;
    case ESPIPE:       return TARGET_ESPIPE;
    case EROFS:        return TARGET_EROFS;
    case EMLINK:       return TARGET_EMLINK;
    case EPIPE:        return TARGET_EPIPE;
    case ERANGE:       return TARGET_ERANGE;
    case ENAMETOOLONG: return TARGET_ENAMETOOLONG;
    case ENOSYS:       return TARGET_ENOSYS;
    case ELOOP:        return TARGET_ELOOP;
    case EOVERFLOW:    return TARGET_EOVERFLOW;
    }
    return TARGET_EINVAL;
}

static void semi_cb(CPUState *cs, uint64_t ret, int err)
{
    common_semi_cb(cs, ret, errno_h2g(err));
}

static void common_semi_ftell_cb(CPUState *cs, uint64_t ret, int err)
{
    if (err) {
        ret = -1;
    }
    semi_cb(cs, ret, err);
}

static void coredump(CPUHexagonState *env)
{
    uint32_t ssr = env->t_sreg[HEX_SREG_SSR];
    FILE *f = qemu_log_trylock();

    if (!f) {
        return;
    }

    fprintf(f, "CRASH!\n");
    fprintf(f, "I think the exception was: ");
    switch (GET_SSR_FIELD(SSR_CAUSE, ssr)) {
    case 0x43:
        fprintf(f, "0x43, NMI");
        break;
    case 0x42:
        fprintf(f, "0x42, Data abort");
        break;
    case 0x44:
        fprintf(f, "0x44, Multi TLB match");
        break;
    case HEX_CAUSE_BIU_PRECISE:
        fprintf(f, "0x%x, Bus Error (Precise BIU error)",
                HEX_CAUSE_BIU_PRECISE);
        break;
    case HEX_CAUSE_DOUBLE_EXCEPT:
        fprintf(f, "0x%x, Exception observed when EX = 1"
                " (double exception)",
                HEX_CAUSE_DOUBLE_EXCEPT);
        break;
    case HEX_CAUSE_FETCH_NO_XPAGE:
        fprintf(f, "0x%x, Privilege violation: User/Guest mode execute"
                " to page with no execute permissions",
                HEX_CAUSE_FETCH_NO_XPAGE);
        break;
    case HEX_CAUSE_FETCH_NO_UPAGE:
        fprintf(f, "0x%x, Privilege violation: "
                "User mode execute to page with no user permissions",
                HEX_CAUSE_FETCH_NO_UPAGE);
        break;
    case HEX_CAUSE_INVALID_PACKET:
        fprintf(f, "0x%x, Invalid packet",
                HEX_CAUSE_INVALID_PACKET);
        break;
    case HEX_CAUSE_PRIV_USER_NO_GINSN:
        fprintf(f, "0x%x, Privilege violation:"
                " guest mode insn in user mode",
                HEX_CAUSE_PRIV_USER_NO_GINSN);
        break;
    case HEX_CAUSE_PRIV_USER_NO_SINSN:
        fprintf(f, "0x%x, Privilege violation: "
                "monitor mode insn in user/guest mode",
                HEX_CAUSE_PRIV_USER_NO_SINSN);
        break;
    case HEX_CAUSE_REG_WRITE_CONFLICT:
        fprintf(f, "0x%x, Multiple writes to same register",
                HEX_CAUSE_REG_WRITE_CONFLICT);
        break;
    case HEX_CAUSE_PC_NOT_ALIGNED:
        fprintf(f, "0x%x, PC not aligned",
                HEX_CAUSE_PC_NOT_ALIGNED);
        break;
    case HEX_CAUSE_MISALIGNED_LOAD:
        fprintf(f, "0x%x, Misaligned Load @ 0x%" PRIx32,
                HEX_CAUSE_MISALIGNED_LOAD, env->t_sreg[HEX_SREG_BADVA]);
        break;
    case HEX_CAUSE_MISALIGNED_STORE:
        fprintf(f, "0x%x, Misaligned Store @ 0x%" PRIx32,
                HEX_CAUSE_MISALIGNED_STORE, env->t_sreg[HEX_SREG_BADVA]);
        break;
    case HEX_CAUSE_PRIV_NO_READ:
        fprintf(f, "0x%x, Privilege violation: "
                "user/guest read permission @ 0x%" PRIx32,
                HEX_CAUSE_PRIV_NO_READ, env->t_sreg[HEX_SREG_BADVA]);
        break;
    case HEX_CAUSE_PRIV_NO_WRITE:
        fprintf(f, "0x%x, Privilege violation: "
                "user/guest write permission @ 0x%" PRIx32,
                HEX_CAUSE_PRIV_NO_WRITE, env->t_sreg[HEX_SREG_BADVA]);
        break;
    case HEX_CAUSE_PRIV_NO_UREAD:
        fprintf(f, "0x%x, Privilege violation:"
                " user read permission @ 0x%" PRIx32,
                HEX_CAUSE_PRIV_NO_UREAD, env->t_sreg[HEX_SREG_BADVA]);
        break;
    case HEX_CAUSE_PRIV_NO_UWRITE:
        fprintf(f, "0x%x, Privilege violation:"
                " user write permission @ 0x%" PRIx32,
                HEX_CAUSE_PRIV_NO_UWRITE, env->t_sreg[HEX_SREG_BADVA]);
        break;
    case HEX_CAUSE_COPROC_LDST:
        fprintf(f, "0x%x, Coprocessor VMEM address error @ 0x%" PRIx32,
                HEX_CAUSE_COPROC_LDST, env->t_sreg[HEX_SREG_BADVA]);
        break;
    case HEX_CAUSE_STACK_LIMIT:
        fprintf(f, "0x%x, Stack limit check error",
                HEX_CAUSE_STACK_LIMIT);
        break;
    case HEX_CAUSE_FPTRAP_CAUSE_BADFLOAT:
        fprintf(f, "0x%x, Floating-Point: Execution of Floating-Point "
                "instruction resulted in exception",
                HEX_CAUSE_FPTRAP_CAUSE_BADFLOAT);
        break;
    case HEX_CAUSE_NO_COPROC_ENABLE:
        fprintf(f, "0x%x, Illegal Execution of Coprocessor Instruction",
                HEX_CAUSE_NO_COPROC_ENABLE);
        break;
    case HEX_CAUSE_NO_COPROC2_ENABLE:
        fprintf(f, "0x%x, Illegal Execution of Secondary"
                " Coprocessor Instruction",
                HEX_CAUSE_NO_COPROC2_ENABLE);
        break;
    case HEX_CAUSE_UNSUPPORTED_HVX_64B:
        fprintf(f, "0x%x, Unsupported Execution of"
                " Coprocessor Instruction with 64bits Mode On",
                HEX_CAUSE_UNSUPPORTED_HVX_64B);
        break;
    case HEX_CAUSE_VWCTRL_WINDOW_MISS:
        fprintf(f, "0x%x, Thread accessing a region"
                " outside VWCTRL window",
                HEX_CAUSE_VWCTRL_WINDOW_MISS);
        break;
    default:
        fprintf(f, "unknown cause 0x%" PRIx32,
                GET_SSR_FIELD(SSR_CAUSE, ssr));
        break;
    }
    fprintf(f, "\nRegister Dump:\n");
    hexagon_dump(env, f, 0);
    qemu_log_unlock(f);
}

static void sim_handle_trap0(CPUHexagonState *env)
{
    target_ulong what_swi, swi_info;
    CPUState *cs = env_cpu(env);
    uintptr_t retaddr = 0;

    g_assert(bql_locked());
    init_semihosting_guestfds();

    what_swi = env->gpr[HEX_REG_R00];
    swi_info = env->gpr[HEX_REG_R01];

    qemu_log_mask(CPU_LOG_INT,
                  "sim_handle_trap0: swi=0x%" PRIx32
                  " info=0x%" PRIx32 " PC=0x%" PRIx32
                  " thread=%" PRId32 "\n",
                  (uint32_t)what_swi, (uint32_t)swi_info,
                  (uint32_t)env->gpr[HEX_REG_PC],
                  (uint32_t)env->threadId);

    if (!is_hexagon_specific_swi_flag(what_swi)) {
        if (what_swi == HEX_SYS_READ || what_swi == HEX_SYS_READC ||
            what_swi == HEX_SYS_WRITE) {
            /*
             * Avoid page faults if the buffer is not in memory yet.
             * NOTE: Counterintuitive, but a WRITE must be able to LOAD from
             * the input address. The contents of that buffer will be
             * directed to the SWI interface.
             */
            do_preload(env, swi_info, (what_swi == HEX_SYS_WRITE));
        }
        /*
         * ARM-compat semihosting SWI numbers are all <= 0x31.
         * If R0 holds a value outside that range (e.g. guest code
         * executing trap0(#0) with an arbitrary R0), treat it as an
         * unrecognized request rather than forwarding to
         * do_common_semihosting() which would abort.
         */
        if (what_swi > 0x31) {
            qemu_log_mask(LOG_UNIMP,
                          "trap0(#0): unrecognized request in r0: "
                          "0x" TARGET_FMT_lx "\n", what_swi);
            return;
        }
        do_common_semihosting(cs);
        return;
    }

    switch (what_swi) {

    case HEX_SYS_EXCEPTION:
    {
        uint32_t ret = env->gpr[HEX_REG_R02];
        env->gpr[HEX_SREG_MODECTL] = 0;
        gdb_exit(ret);
        exit(ret);
    }
    break;

    case HEX_SYS_OPEN:
    {
        char filename[BUFSIZ];
        target_ulong physical_filename_addr;
        unsigned int filemode;
        int length;
        int real_openmode;
        int ret, err = 0;
        int i = 0;

        hexagon_read_memory(env, swi_info, 4, &physical_filename_addr, retaddr);
        hexagon_read_memory(env, swi_info + 4, 4, &filemode, retaddr);
        hexagon_read_memory(env, swi_info + 8, 4, &length, retaddr);

        if (length >= BUFSIZ) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: filename too large (%d)\n",
                          __func__, length);
            semi_cb(cs, -1, ENAMETOOLONG);
            break;
        }

        do {
            hexagon_read_memory(env, physical_filename_addr + i, 1,
                                &filename[i], retaddr);
            i++;
        } while (filename[i - 1]);

        /* convert ARM ANGEL filemode into host filemode */
        if (filemode < ARRAY_SIZE(angel_to_host_filemode_table)) {
            real_openmode = angel_to_host_filemode_table[filemode];
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: invalid OPEN mode: %u\n",
                          __func__, filemode);
            semi_cb(cs, -1, EINVAL);
            break;
        }

        if (strcmp(filename, ":tt") == 0 &&
            qemu_semihosting_console_has_chardev()) {
            ret = alloc_guestfd();
            console_guestfd(ret);
        } else {
            ret = open(filename, real_openmode | O_BINARY, 0644);

            if (ret == -1) {
                err = errno;
            } else {
                int guestfd = alloc_guestfd();
                associate_guestfd(guestfd, ret);
                ret = guestfd;
            }
        }
        semi_cb(cs, ret, err);
    }
    break;

    case HEX_SYS_WRITECREG:
    {
        char c = swi_info;
        qemu_semihosting_console_write(&c, 1);
    }
    break;

    /*
     * Hexagon's SYS_ISTTY is a bit different than arm's: we do not return -1
     * on error, neither errno. So we override with our own implementation.
     */
    case HEX_SYS_ISTTY:
    {
        int fd;
        hexagon_read_memory(env, swi_info, 4, &fd, retaddr);
        semi_cb(cs, isatty(fd), 0);
    }
    break;

    case HEX_SYS_SEEK:
    {
        int fd;
        target_ulong off;
        hexagon_read_memory(env, swi_info, 4, &fd, retaddr);
        hexagon_read_memory(env, swi_info + 4, 4, &off, retaddr);
        semihost_sys_lseek(env_cpu(env), common_semi_ftell_cb, fd, off,
                          GDB_SEEK_SET);
    }
    break;

    case HEX_SYS_STAT:
    case HEX_SYS_FSTAT:
    {
        struct stat st_buf;
        uint8_t *st_bufptr = (uint8_t *)&sys_stat;
        int rc, err = 0;
        char filename[BUFSIZ];
        target_ulong physical_filename_addr;
        target_ulong statBufferAddr;
        hexagon_read_memory(env, swi_info, 4, &physical_filename_addr, retaddr);

        if (what_swi == HEX_SYS_STAT) {
            int i = 0;
            do {
                hexagon_read_memory(env, physical_filename_addr + i, 1,
                                    &filename[i], retaddr);
                i++;
            } while ((i < BUFSIZ) && filename[i - 1]);
            rc = stat(filename, &st_buf);
            err = errno;
        } else {
            int fd = physical_filename_addr;
            GuestFD *gf = get_guestfd(fd);
            if (!gf || gf->type != GuestFDHost) {
                qemu_log_mask(LOG_UNIMP,
                              "fstat semihosting only implemented"
                              " for native mode\n");
                g_assert_not_reached();
            }
            rc = fstat(gf->hostfd, &st_buf);
            err = errno;
        }
        if (rc == 0) {
            sys_stat.dev   = cpu_to_le64(st_buf.st_dev);
            sys_stat.ino   = cpu_to_le64(st_buf.st_ino);
            sys_stat.mode  = cpu_to_le32(st_buf.st_mode);
            sys_stat.nlink = cpu_to_le32((uint32_t) st_buf.st_nlink);
            sys_stat.rdev  = cpu_to_le64(st_buf.st_rdev);
            sys_stat.size  = cpu_to_le32((uint32_t) st_buf.st_size);
            sys_stat.atime = cpu_to_le32(st_buf.st_atime);
            sys_stat.mtime = cpu_to_le32(st_buf.st_mtime);
            sys_stat.ctime = cpu_to_le32(st_buf.st_ctime);
        }
        hexagon_read_memory(env, swi_info + 4, 4, &statBufferAddr, retaddr);

        for (int i = 0; i < sizeof(sys_stat); i++) {
            hexagon_write_memory(env, statBufferAddr + i, 1, st_bufptr[i],
                                 retaddr);
        }
        semi_cb(cs, rc, rc == 0 ? 0 : err);
    }
    break;

    case HEX_SYS_FTRUNC:
    {
        int fd;
        off_t size_limit;
        hexagon_read_memory(env, swi_info, 4, &fd, retaddr);
        hexagon_read_memory(env, swi_info + 4, 8, &size_limit, retaddr);
        semihost_sys_ftruncate(cs, semi_cb, fd, size_limit);
    }
    break;

    case HEX_SYS_ACCESS:
    {
        char filename[BUFSIZ];
        uint32_t FileNameAddr;
        uint32_t BufferMode;
        int rc;

        int i = 0;

        hexagon_read_memory(env, swi_info, 4, &FileNameAddr, retaddr);
        do {
            hexagon_read_memory(env, FileNameAddr + i, 1, &filename[i],
                                retaddr);
            i++;
        } while ((i < BUFSIZ) && (filename[i - 1]));
        filename[i] = 0;

        hexagon_read_memory(env, swi_info + 4, 4, &BufferMode, retaddr);

        rc = access(filename, BufferMode);
        semi_cb(cs, rc,  rc == 0 ? 0 : errno);
    }
    break;

    case HEX_SYS_GETCWD:
    {
        char cwdPtr[PATH_MAX];
        uint32_t BufferAddr;
        uint32_t BufferSize;
        uint32_t rc = 0, err = 0;

        hexagon_read_memory(env, swi_info, 4, &BufferAddr, retaddr);
        hexagon_read_memory(env, swi_info + 4, 4, &BufferSize, retaddr);

        if (!getcwd(cwdPtr, PATH_MAX)) {
            err = errno;
        } else {
            size_t cwd_size = strlen(cwdPtr);
            if (cwd_size > BufferSize) {
                err = ERANGE;
            } else {
                for (int i = 0; i < cwd_size; i++) {
                    hexagon_write_memory(env, BufferAddr + i, 1,
                                         (uint64_t)cwdPtr[i], retaddr);
                }
                rc = BufferAddr;
            }
        }
        semi_cb(cs, rc, rc != 0 ? 0 : err);
        break;
    }

    case HEX_SYS_EXEC:
    {
        qemu_log_mask(LOG_UNIMP, "SYS_EXEC is deprecated\n");
        semi_cb(cs, -1, ENOSYS);
    }
    break;

    case HEX_SYS_COREDUMP:
        coredump(env);
        break;

    case HEX_SYS_FTELL:
    {
        int fd;
        hexagon_read_memory(env, swi_info, 4, &fd, retaddr);
        semihost_sys_lseek(cs, common_semi_ftell_cb, fd, 0, GDB_SEEK_CUR);
    }
    break;

    case HEX_SYS_READ_CYCLES:
    case HEX_SYS_READ_TCYCLES:
    case HEX_SYS_READ_ICOUNT:
    {
        env->gpr[HEX_REG_R00] = 0;
        env->gpr[HEX_REG_R01] = 0;
        break;
    }

    case HEX_SYS_READ_PCYCLES:
    {
        env->gpr[HEX_REG_R00] = hexagon_get_sys_pcycle_count_low(env);
        env->gpr[HEX_REG_R01] = hexagon_get_sys_pcycle_count_high(env);
        break;
    }

    case HEX_SYS_PROF_ON:
    case HEX_SYS_PROF_OFF:
    case HEX_SYS_PROF_STATSRESET:
    case HEX_SYS_DUMP_PMU_STATS:
    case HEX_SYS_HEAPINFO:
        semi_cb(cs, -1, ENOSYS);
        qemu_log_mask(LOG_UNIMP,
                      "SWI call %" PRIx32
                      " is unimplemented in QEMU\n",
                      (uint32_t)what_swi);
        break;

    default:
        qemu_log_mask(LOG_UNIMP,
                      "unknown swi request: 0x%" PRIx32 "\n",
                      (uint32_t)what_swi);
        semi_cb(cs, -1, ENOSYS);
    }
}

static void set_addresses(CPUHexagonState *env, uint32_t pc_offset,
                          uint32_t exception_index)
{
    HexagonCPU *cpu = env_archcpu(env);
    uint32_t evb = cpu->globalregs ?
        hexagon_globalreg_read(cpu->globalregs, HEX_SREG_EVB,
                               env->threadId) :
        cpu->boot_addr;
    env->t_sreg[HEX_SREG_ELR] = env->gpr[HEX_REG_PC] + pc_offset;
    env->gpr[HEX_REG_PC] = evb | (exception_index << 2);
}

static const char *event_name[] = {
    [HEX_EVENT_RESET] = "HEX_EVENT_RESET",
    [HEX_EVENT_IMPRECISE] = "HEX_EVENT_IMPRECISE",
    [HEX_EVENT_PRECISE] = "HEX_EVENT_PRECISE",
    [HEX_EVENT_TLB_MISS_X] = "HEX_EVENT_TLB_MISS_X",
    [HEX_EVENT_TLB_MISS_RW] = "HEX_EVENT_TLB_MISS_RW",
    [HEX_EVENT_TRAP0] = "HEX_EVENT_TRAP0",
    [HEX_EVENT_TRAP1] = "HEX_EVENT_TRAP1",
    [HEX_EVENT_FPTRAP] = "HEX_EVENT_FPTRAP",
    [HEX_EVENT_DEBUG] = "HEX_EVENT_DEBUG",
    [HEX_EVENT_INT0] = "HEX_EVENT_INT0",
    [HEX_EVENT_INT1] = "HEX_EVENT_INT1",
    [HEX_EVENT_INT2] = "HEX_EVENT_INT2",
    [HEX_EVENT_INT3] = "HEX_EVENT_INT3",
    [HEX_EVENT_INT4] = "HEX_EVENT_INT4",
    [HEX_EVENT_INT5] = "HEX_EVENT_INT5",
    [HEX_EVENT_INT6] = "HEX_EVENT_INT6",
    [HEX_EVENT_INT7] = "HEX_EVENT_INT7",
    [HEX_EVENT_INT8] = "HEX_EVENT_INT8",
    [HEX_EVENT_INT9] = "HEX_EVENT_INT9",
    [HEX_EVENT_INTA] = "HEX_EVENT_INTA",
    [HEX_EVENT_INTB] = "HEX_EVENT_INTB",
    [HEX_EVENT_INTC] = "HEX_EVENT_INTC",
    [HEX_EVENT_INTD] = "HEX_EVENT_INTD",
    [HEX_EVENT_INTE] = "HEX_EVENT_INTE",
    [HEX_EVENT_INTF] = "HEX_EVENT_INTF"
};

void hexagon_cpu_do_interrupt(CPUState *cs)

{
    CPUHexagonState *env = cpu_env(cs);
    HexagonCPU *cpu = HEXAGON_CPU(cs);
    uint32_t ssr;

    BQL_LOCK_GUARD();

    qemu_log_mask(CPU_LOG_INT,
                  "\t%s: event 0x%02x:%s, cause 0x%" PRIx32 "(%" PRIu32 ")\n",
                  __func__, (unsigned)cs->exception_index,
                  event_name[cs->exception_index], env->cause_code,
                  env->cause_code);

    env->llsc_addr = ~0;

    ssr = env->t_sreg[HEX_SREG_SSR];
    if (GET_SSR_FIELD(SSR_EX, ssr) == 1) {
        if (cpu->globalregs) {
            hexagon_globalreg_write(cpu->globalregs, HEX_SREG_DIAG,
                                    env->cause_code, env->threadId);
        }
        env->cause_code = HEX_CAUSE_DOUBLE_EXCEPT;
        cs->exception_index = HEX_EVENT_PRECISE;
    }

    switch (cs->exception_index) {
    case HEX_EVENT_TRAP0:
        if (env->cause_code == 0) {
            sim_handle_trap0(env);
        }

        hexagon_ssr_set_cause(env, env->cause_code);
        set_addresses(env, 4, cs->exception_index);
        break;

    case HEX_EVENT_TRAP1:
        hexagon_ssr_set_cause(env, env->cause_code);
        set_addresses(env, 4, cs->exception_index);
        break;

    case HEX_EVENT_TLB_MISS_X:
        switch (env->cause_code) {
        case HEX_CAUSE_TLBMISSX_CAUSE_NORMAL:
        case HEX_CAUSE_TLBMISSX_CAUSE_NEXTPAGE:
            qemu_log_mask(CPU_LOG_MMU,
                          "TLB miss EX exception (0x%02" PRIx32 ") caught: "
                          "Cause code (0x%" PRIx32 ") "
                          "TID = 0x%" PRIx32 ", PC = 0x%" PRIx32
                          ", BADVA = 0x%" PRIx32 "\n",
                          (uint32_t)cs->exception_index,
                          env->cause_code, env->threadId,
                          env->gpr[HEX_REG_PC],
                          env->t_sreg[HEX_SREG_BADVA]);

            hexagon_ssr_set_cause(env, env->cause_code);
            set_addresses(env, 0, cs->exception_index);
            break;

        default:
            cpu_abort(cs,
                      "1:Hexagon exception %" PRId32 "/0x%02" PRIx32 ": "
                      "Unknown cause code %" PRIu32 "/0x%" PRIx32 "\n",
                      (uint32_t)cs->exception_index,
                      (uint32_t)cs->exception_index,
                      env->cause_code,
                      env->cause_code);
            break;
        }
        break;

    case HEX_EVENT_TLB_MISS_RW:
        switch (env->cause_code) {
        case HEX_CAUSE_TLBMISSRW_CAUSE_READ:
        case HEX_CAUSE_TLBMISSRW_CAUSE_WRITE:
            qemu_log_mask(CPU_LOG_MMU,
                          "TLB miss RW exception (0x%02" PRIx32 ") caught: "
                          "Cause code (0x%" PRIx32 ") "
                          "TID = 0x%" PRIx32 ", PC = 0x%" PRIx32
                          ", BADVA = 0x%" PRIx32 "\n",
                          (uint32_t)cs->exception_index,
                          env->cause_code, env->threadId,
                          env->gpr[HEX_REG_PC],
                          env->t_sreg[HEX_SREG_BADVA]);

            hexagon_ssr_set_cause(env, env->cause_code);
            set_addresses(env, 0, cs->exception_index);
            /* env->sreg[HEX_SREG_BADVA] is set when the exception is raised */
            break;

        default:
            cpu_abort(cs,
                      "2:Hexagon exception %" PRId32 "/0x%02" PRIx32 ": "
                      "Unknown cause code %" PRIu32 "/0x%" PRIx32 "\n",
                      (uint32_t)cs->exception_index,
                      (uint32_t)cs->exception_index,
                      env->cause_code,
                      env->cause_code);
            break;
        }
        break;

    case HEX_EVENT_FPTRAP:
        hexagon_ssr_set_cause(env, env->cause_code);
        set_addresses(env, 0, cs->exception_index);
        break;

    case HEX_EVENT_DEBUG:
        hexagon_ssr_set_cause(env, env->cause_code);
        set_addresses(env, 0, cs->exception_index);
        qemu_log_mask(LOG_UNIMP, "single-step exception is not handled\n");
        break;

    case HEX_EVENT_PRECISE:
        switch (env->cause_code) {
        case HEX_CAUSE_FETCH_NO_XPAGE:
        case HEX_CAUSE_FETCH_NO_UPAGE:
        case HEX_CAUSE_PRIV_NO_READ:
        case HEX_CAUSE_PRIV_NO_UREAD:
        case HEX_CAUSE_PRIV_NO_WRITE:
        case HEX_CAUSE_PRIV_NO_UWRITE:
        case HEX_CAUSE_MISALIGNED_LOAD:
        case HEX_CAUSE_MISALIGNED_STORE:
        case HEX_CAUSE_PC_NOT_ALIGNED:
            qemu_log_mask(CPU_LOG_MMU,
                          "MMU permission exception (0x%02" PRIx32 ") caught: "
                          "Cause code (0x%" PRIx32 ") "
                          "TID = 0x%" PRIx32 ", PC = 0x%" PRIx32
                          ", BADVA = 0x%" PRIx32 "\n",
                          (uint32_t)cs->exception_index,
                          env->cause_code, env->threadId,
                          env->gpr[HEX_REG_PC],
                          env->t_sreg[HEX_SREG_BADVA]);


            hexagon_ssr_set_cause(env, env->cause_code);
            set_addresses(env, 0, cs->exception_index);
            /* env->sreg[HEX_SREG_BADVA] is set when the exception is raised */
            break;

        case HEX_CAUSE_DOUBLE_EXCEPT:
        case HEX_CAUSE_PRIV_USER_NO_SINSN:
        case HEX_CAUSE_PRIV_USER_NO_GINSN:
        case HEX_CAUSE_INVALID_OPCODE:
        case HEX_CAUSE_NO_COPROC_ENABLE:
        case HEX_CAUSE_NO_COPROC2_ENABLE:
        case HEX_CAUSE_UNSUPPORTED_HVX_64B:
        case HEX_CAUSE_REG_WRITE_CONFLICT:
        case HEX_CAUSE_VWCTRL_WINDOW_MISS:
            hexagon_ssr_set_cause(env, env->cause_code);
            set_addresses(env, 0, cs->exception_index);
            break;

        case HEX_CAUSE_COPROC_LDST:
            hexagon_ssr_set_cause(env, env->cause_code);
            set_addresses(env, 0, cs->exception_index);
            break;

        case HEX_CAUSE_STACK_LIMIT:
            hexagon_ssr_set_cause(env, env->cause_code);
            set_addresses(env, 0, cs->exception_index);
            break;

        default:
            cpu_abort(cs,
                      "3:Hexagon exception %" PRId32 "/0x%02" PRIx32 ": "
                      "Unknown cause code %" PRIu32 "/0x%" PRIx32 "\n",
                      (uint32_t)cs->exception_index,
                      (uint32_t)cs->exception_index,
                      env->cause_code,
                      env->cause_code);
            break;
        }
        break;

    case HEX_EVENT_IMPRECISE:
        if (get_exe_mode(env) == HEX_EXE_MODE_WAIT) {
            env->gpr[HEX_REG_PC] = env->wait_next_pc - 4;
            clear_wait_mode(env);
        }
        switch (env->cause_code) {
        case HEX_CAUSE_IMPRECISE_MULTI_TLB_MATCH:
            hexagon_ssr_set_cause(env, env->cause_code);
            set_addresses(env, 4, cs->exception_index);
            if (cpu->globalregs) {
                hexagon_globalreg_write(cpu->globalregs, HEX_SREG_DIAG,
                    (0x4 << 4) | (env->t_sreg[HEX_SREG_HTID] & 0xF),
                    env->threadId);
            }
            break;

        case HEX_CAUSE_IMPRECISE_NMI:
            hexagon_ssr_set_cause(env, env->cause_code);
            set_addresses(env, 4, cs->exception_index);
            if (cpu->globalregs) {
                hexagon_globalreg_write(cpu->globalregs, HEX_SREG_DIAG,
                    (0x3 << 4) | (env->t_sreg[HEX_SREG_HTID] & 0xF),
                    env->threadId);
            }
            break;

        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                    "Imprecise exception with unhandled cause 0x%x\n",
                    env->cause_code);
            hexagon_ssr_set_cause(env, env->cause_code);
            set_addresses(env, 4, cs->exception_index);
            break;
        }
        break;

    default:
        qemu_log_mask(LOG_UNIMP,
                "Hexagon Unsupported exception 0x%02x/0x%" PRIx32 "\n",
                (unsigned)cs->exception_index, env->cause_code);
        break;
    }

    cs->exception_index = HEX_EVENT_NONE;
}

void register_trap_exception(CPUHexagonState *env, int traptype, int imm,
                             uint32_t PC)
{
    CPUState *cs = env_cpu(env);

    cs->exception_index = (traptype == 0) ? HEX_EVENT_TRAP0 : HEX_EVENT_TRAP1;
    ASSERT_DIRECT_TO_GUEST_UNSET(env, cs->exception_index);

    env->cause_code = imm;
    env->gpr[HEX_REG_PC] = PC;
    cpu_loop_exit(cs);
}
