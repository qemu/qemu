/*
 * Syscall implementations for semihosting.
 *
 * Copyright (c) 2022 Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "exec/gdbstub.h"
#include "semihosting/guestfd.h"
#include "semihosting/syscalls.h"
#include "semihosting/console.h"
#ifdef CONFIG_USER_ONLY
#include "qemu.h"
#else
#include "semihosting/softmmu-uaccess.h"
#endif


/*
 * Validate or compute the length of the string (including terminator).
 */
static int validate_strlen(CPUState *cs, target_ulong str, target_ulong tlen)
{
    CPUArchState *env G_GNUC_UNUSED = cs->env_ptr;
    char c;

    if (tlen == 0) {
        ssize_t slen = target_strlen(str);

        if (slen < 0) {
            return -EFAULT;
        }
        if (slen >= INT32_MAX) {
            return -ENAMETOOLONG;
        }
        return slen + 1;
    }
    if (tlen > INT32_MAX) {
        return -ENAMETOOLONG;
    }
    if (get_user_u8(c, str + tlen - 1)) {
        return -EFAULT;
    }
    if (c != 0) {
        return -EINVAL;
    }
    return tlen;
}

static int validate_lock_user_string(char **pstr, CPUState *cs,
                                     target_ulong tstr, target_ulong tlen)
{
    int ret = validate_strlen(cs, tstr, tlen);
    CPUArchState *env G_GNUC_UNUSED = cs->env_ptr;
    char *str = NULL;

    if (ret > 0) {
        str = lock_user(VERIFY_READ, tstr, ret, true);
        ret = str ? 0 : -EFAULT;
    }
    *pstr = str;
    return ret;
}

/*
 * TODO: Note that gdb always stores the stat structure big-endian.
 * So far, that's ok, as the only two targets using this are also
 * big-endian.  Until we do something with gdb, also produce the
 * same big-endian result from the host.
 */
static int copy_stat_to_user(CPUState *cs, target_ulong addr,
                             const struct stat *s)
{
    CPUArchState *env G_GNUC_UNUSED = cs->env_ptr;
    struct gdb_stat *p;

    if (s->st_dev != (uint32_t)s->st_dev ||
        s->st_ino != (uint32_t)s->st_ino) {
        return -EOVERFLOW;
    }

    p = lock_user(VERIFY_WRITE, addr, sizeof(struct gdb_stat), 0);
    if (!p) {
        return -EFAULT;
    }

    p->gdb_st_dev = cpu_to_be32(s->st_dev);
    p->gdb_st_ino = cpu_to_be32(s->st_ino);
    p->gdb_st_mode = cpu_to_be32(s->st_mode);
    p->gdb_st_nlink = cpu_to_be32(s->st_nlink);
    p->gdb_st_uid = cpu_to_be32(s->st_uid);
    p->gdb_st_gid = cpu_to_be32(s->st_gid);
    p->gdb_st_rdev = cpu_to_be32(s->st_rdev);
    p->gdb_st_size = cpu_to_be64(s->st_size);
#ifdef _WIN32
    /* Windows stat is missing some fields.  */
    p->gdb_st_blksize = 0;
    p->gdb_st_blocks = 0;
#else
    p->gdb_st_blksize = cpu_to_be64(s->st_blksize);
    p->gdb_st_blocks = cpu_to_be64(s->st_blocks);
#endif
    p->gdb_st_atime = cpu_to_be32(s->st_atime);
    p->gdb_st_mtime = cpu_to_be32(s->st_mtime);
    p->gdb_st_ctime = cpu_to_be32(s->st_ctime);

    unlock_user(p, addr, sizeof(struct gdb_stat));
    return 0;
}

/*
 * GDB semihosting syscall implementations.
 */

static gdb_syscall_complete_cb gdb_open_complete;

static void gdb_open_cb(CPUState *cs, uint64_t ret, int err)
{
    if (!err) {
        int guestfd = alloc_guestfd();
        associate_guestfd(guestfd, ret);
        ret = guestfd;
    }
    gdb_open_complete(cs, ret, err);
}

static void gdb_open(CPUState *cs, gdb_syscall_complete_cb complete,
                     target_ulong fname, target_ulong fname_len,
                     int gdb_flags, int mode)
{
    int len = validate_strlen(cs, fname, fname_len);
    if (len < 0) {
        complete(cs, -1, -len);
        return;
    }

    gdb_open_complete = complete;
    gdb_do_syscall(gdb_open_cb, "open,%s,%x,%x",
                   fname, len, (target_ulong)gdb_flags, (target_ulong)mode);
}

static void gdb_close(CPUState *cs, gdb_syscall_complete_cb complete,
                      GuestFD *gf)
{
    gdb_do_syscall(complete, "close,%x", (target_ulong)gf->hostfd);
}

static void gdb_read(CPUState *cs, gdb_syscall_complete_cb complete,
                     GuestFD *gf, target_ulong buf, target_ulong len)
{
    gdb_do_syscall(complete, "read,%x,%x,%x",
                   (target_ulong)gf->hostfd, buf, len);
}

static void gdb_write(CPUState *cs, gdb_syscall_complete_cb complete,
                      GuestFD *gf, target_ulong buf, target_ulong len)
{
    gdb_do_syscall(complete, "write,%x,%x,%x",
                   (target_ulong)gf->hostfd, buf, len);
}

static void gdb_lseek(CPUState *cs, gdb_syscall_complete_cb complete,
                      GuestFD *gf, int64_t off, int gdb_whence)
{
    gdb_do_syscall(complete, "lseek,%x,%lx,%x",
                   (target_ulong)gf->hostfd, off, (target_ulong)gdb_whence);
}

static void gdb_isatty(CPUState *cs, gdb_syscall_complete_cb complete,
                       GuestFD *gf)
{
    gdb_do_syscall(complete, "isatty,%x", (target_ulong)gf->hostfd);
}

static void gdb_fstat(CPUState *cs, gdb_syscall_complete_cb complete,
                      GuestFD *gf, target_ulong addr)
{
    gdb_do_syscall(complete, "fstat,%x,%x", (target_ulong)gf->hostfd, addr);
}

static void gdb_stat(CPUState *cs, gdb_syscall_complete_cb complete,
                     target_ulong fname, target_ulong fname_len,
                     target_ulong addr)
{
    int len = validate_strlen(cs, fname, fname_len);
    if (len < 0) {
        complete(cs, -1, -len);
        return;
    }

    gdb_do_syscall(complete, "stat,%s,%x", fname, len, addr);
}

static void gdb_remove(CPUState *cs, gdb_syscall_complete_cb complete,
                       target_ulong fname, target_ulong fname_len)
{
    int len = validate_strlen(cs, fname, fname_len);
    if (len < 0) {
        complete(cs, -1, -len);
        return;
    }

    gdb_do_syscall(complete, "unlink,%s", fname, len);
}

static void gdb_rename(CPUState *cs, gdb_syscall_complete_cb complete,
                       target_ulong oname, target_ulong oname_len,
                       target_ulong nname, target_ulong nname_len)
{
    int olen, nlen;

    olen = validate_strlen(cs, oname, oname_len);
    if (olen < 0) {
        complete(cs, -1, -olen);
        return;
    }
    nlen = validate_strlen(cs, nname, nname_len);
    if (nlen < 0) {
        complete(cs, -1, -nlen);
        return;
    }

    gdb_do_syscall(complete, "rename,%s,%s", oname, olen, nname, nlen);
}

static void gdb_system(CPUState *cs, gdb_syscall_complete_cb complete,
                       target_ulong cmd, target_ulong cmd_len)
{
    int len = validate_strlen(cs, cmd, cmd_len);
    if (len < 0) {
        complete(cs, -1, -len);
        return;
    }

    gdb_do_syscall(complete, "system,%s", cmd, len);
}

static void gdb_gettimeofday(CPUState *cs, gdb_syscall_complete_cb complete,
                             target_ulong tv_addr, target_ulong tz_addr)
{
    gdb_do_syscall(complete, "gettimeofday,%x,%x", tv_addr, tz_addr);
}

/*
 * Host semihosting syscall implementations.
 */

static void host_open(CPUState *cs, gdb_syscall_complete_cb complete,
                      target_ulong fname, target_ulong fname_len,
                      int gdb_flags, int mode)
{
    CPUArchState *env G_GNUC_UNUSED = cs->env_ptr;
    char *p;
    int ret, host_flags = O_BINARY;

    ret = validate_lock_user_string(&p, cs, fname, fname_len);
    if (ret < 0) {
        complete(cs, -1, -ret);
        return;
    }

    if (gdb_flags & GDB_O_WRONLY) {
        host_flags |= O_WRONLY;
    } else if (gdb_flags & GDB_O_RDWR) {
        host_flags |= O_RDWR;
    } else {
        host_flags |= O_RDONLY;
    }
    if (gdb_flags & GDB_O_CREAT) {
        host_flags |= O_CREAT;
    }
    if (gdb_flags & GDB_O_TRUNC) {
        host_flags |= O_TRUNC;
    }
    if (gdb_flags & GDB_O_EXCL) {
        host_flags |= O_EXCL;
    }

    ret = open(p, host_flags, mode);
    if (ret < 0) {
        complete(cs, -1, errno);
    } else {
        int guestfd = alloc_guestfd();
        associate_guestfd(guestfd, ret);
        complete(cs, guestfd, 0);
    }
    unlock_user(p, fname, 0);
}

static void host_close(CPUState *cs, gdb_syscall_complete_cb complete,
                       GuestFD *gf)
{
    /*
     * Only close the underlying host fd if it's one we opened on behalf
     * of the guest in SYS_OPEN.
     */
    if (gf->hostfd != STDIN_FILENO &&
        gf->hostfd != STDOUT_FILENO &&
        gf->hostfd != STDERR_FILENO &&
        close(gf->hostfd) < 0) {
        complete(cs, -1, errno);
    } else {
        complete(cs, 0, 0);
    }
}

static void host_read(CPUState *cs, gdb_syscall_complete_cb complete,
                      GuestFD *gf, target_ulong buf, target_ulong len)
{
    CPUArchState *env G_GNUC_UNUSED = cs->env_ptr;
    void *ptr = lock_user(VERIFY_WRITE, buf, len, 0);
    ssize_t ret;

    if (!ptr) {
        complete(cs, -1, EFAULT);
        return;
    }
    ret = RETRY_ON_EINTR(read(gf->hostfd, ptr, len));
    if (ret == -1) {
        unlock_user(ptr, buf, 0);
        complete(cs, -1, errno);
    } else {
        unlock_user(ptr, buf, ret);
        complete(cs, ret, 0);
    }
}

static void host_write(CPUState *cs, gdb_syscall_complete_cb complete,
                       GuestFD *gf, target_ulong buf, target_ulong len)
{
    CPUArchState *env G_GNUC_UNUSED = cs->env_ptr;
    void *ptr = lock_user(VERIFY_READ, buf, len, 1);
    ssize_t ret;

    if (!ptr) {
        complete(cs, -1, EFAULT);
        return;
    }
    ret = write(gf->hostfd, ptr, len);
    unlock_user(ptr, buf, 0);
    complete(cs, ret, ret == -1 ? errno : 0);
}

static void host_lseek(CPUState *cs, gdb_syscall_complete_cb complete,
                       GuestFD *gf, int64_t off, int whence)
{
    /* So far, all hosts use the same values. */
    QEMU_BUILD_BUG_ON(GDB_SEEK_SET != SEEK_SET);
    QEMU_BUILD_BUG_ON(GDB_SEEK_CUR != SEEK_CUR);
    QEMU_BUILD_BUG_ON(GDB_SEEK_END != SEEK_END);

    off_t ret = off;
    int err = 0;

    if (ret == off) {
        ret = lseek(gf->hostfd, ret, whence);
        if (ret == -1) {
            err = errno;
        }
    } else {
        ret = -1;
        err = EINVAL;
    }
    complete(cs, ret, err);
}

static void host_isatty(CPUState *cs, gdb_syscall_complete_cb complete,
                        GuestFD *gf)
{
    int ret = isatty(gf->hostfd);
    complete(cs, ret, ret ? 0 : errno);
}

static void host_flen(CPUState *cs, gdb_syscall_complete_cb complete,
                      GuestFD *gf)
{
    struct stat buf;

    if (fstat(gf->hostfd, &buf) < 0) {
        complete(cs, -1, errno);
    } else {
        complete(cs, buf.st_size, 0);
    }
}

static void host_fstat(CPUState *cs, gdb_syscall_complete_cb complete,
                       GuestFD *gf, target_ulong addr)
{
    struct stat buf;
    int ret;

    ret = fstat(gf->hostfd, &buf);
    if (ret) {
        complete(cs, -1, errno);
        return;
    }
    ret = copy_stat_to_user(cs, addr, &buf);
    complete(cs, ret ? -1 : 0, ret ? -ret : 0);
}

static void host_stat(CPUState *cs, gdb_syscall_complete_cb complete,
                      target_ulong fname, target_ulong fname_len,
                      target_ulong addr)
{
    CPUArchState *env G_GNUC_UNUSED = cs->env_ptr;
    struct stat buf;
    char *name;
    int ret, err;

    ret = validate_lock_user_string(&name, cs, fname, fname_len);
    if (ret < 0) {
        complete(cs, -1, -ret);
        return;
    }

    ret = stat(name, &buf);
    if (ret) {
        err = errno;
    } else {
        ret = copy_stat_to_user(cs, addr, &buf);
        err = 0;
        if (ret < 0) {
            err = -ret;
            ret = -1;
        }
    }
    unlock_user(name, fname, 0);
    complete(cs, ret, err);
}

static void host_remove(CPUState *cs, gdb_syscall_complete_cb complete,
                        target_ulong fname, target_ulong fname_len)
{
    CPUArchState *env G_GNUC_UNUSED = cs->env_ptr;
    char *p;
    int ret;

    ret = validate_lock_user_string(&p, cs, fname, fname_len);
    if (ret < 0) {
        complete(cs, -1, -ret);
        return;
    }

    ret = remove(p);
    unlock_user(p, fname, 0);
    complete(cs, ret, ret ? errno : 0);
}

static void host_rename(CPUState *cs, gdb_syscall_complete_cb complete,
                        target_ulong oname, target_ulong oname_len,
                        target_ulong nname, target_ulong nname_len)
{
    CPUArchState *env G_GNUC_UNUSED = cs->env_ptr;
    char *ostr, *nstr;
    int ret;

    ret = validate_lock_user_string(&ostr, cs, oname, oname_len);
    if (ret < 0) {
        complete(cs, -1, -ret);
        return;
    }
    ret = validate_lock_user_string(&nstr, cs, nname, nname_len);
    if (ret < 0) {
        unlock_user(ostr, oname, 0);
        complete(cs, -1, -ret);
        return;
    }

    ret = rename(ostr, nstr);
    unlock_user(ostr, oname, 0);
    unlock_user(nstr, nname, 0);
    complete(cs, ret, ret ? errno : 0);
}

static void host_system(CPUState *cs, gdb_syscall_complete_cb complete,
                        target_ulong cmd, target_ulong cmd_len)
{
    CPUArchState *env G_GNUC_UNUSED = cs->env_ptr;
    char *p;
    int ret;

    ret = validate_lock_user_string(&p, cs, cmd, cmd_len);
    if (ret < 0) {
        complete(cs, -1, -ret);
        return;
    }

    ret = system(p);
    unlock_user(p, cmd, 0);
    complete(cs, ret, ret == -1 ? errno : 0);
}

static void host_gettimeofday(CPUState *cs, gdb_syscall_complete_cb complete,
                              target_ulong tv_addr, target_ulong tz_addr)
{
    CPUArchState *env G_GNUC_UNUSED = cs->env_ptr;
    struct gdb_timeval *p;
    int64_t rt;

    /* GDB fails on non-null TZ, so be consistent. */
    if (tz_addr != 0) {
        complete(cs, -1, EINVAL);
        return;
    }

    p = lock_user(VERIFY_WRITE, tv_addr, sizeof(struct gdb_timeval), 0);
    if (!p) {
        complete(cs, -1, EFAULT);
        return;
    }

    /* TODO: Like stat, gdb always produces big-endian results; match it. */
    rt = g_get_real_time();
    p->tv_sec = cpu_to_be32(rt / G_USEC_PER_SEC);
    p->tv_usec = cpu_to_be64(rt % G_USEC_PER_SEC);
    unlock_user(p, tv_addr, sizeof(struct gdb_timeval));
}

#ifndef CONFIG_USER_ONLY
static void host_poll_one(CPUState *cs, gdb_syscall_complete_cb complete,
                          GuestFD *gf, GIOCondition cond, int timeout)
{
    /*
     * Since this is only used by xtensa in system mode, and stdio is
     * handled through GuestFDConsole, and there are no semihosting
     * system calls for sockets and the like, that means this descriptor
     * must be a normal file.  Normal files never block and are thus
     * always ready.
     */
    complete(cs, cond & (G_IO_IN | G_IO_OUT), 0);
}
#endif

/*
 * Static file semihosting syscall implementations.
 */

static void staticfile_read(CPUState *cs, gdb_syscall_complete_cb complete,
                            GuestFD *gf, target_ulong buf, target_ulong len)
{
    CPUArchState *env G_GNUC_UNUSED = cs->env_ptr;
    target_ulong rest = gf->staticfile.len - gf->staticfile.off;
    void *ptr;

    if (len > rest) {
        len = rest;
    }
    ptr = lock_user(VERIFY_WRITE, buf, len, 0);
    if (!ptr) {
        complete(cs, -1, EFAULT);
        return;
    }
    memcpy(ptr, gf->staticfile.data + gf->staticfile.off, len);
    gf->staticfile.off += len;
    unlock_user(ptr, buf, len);
    complete(cs, len, 0);
}

static void staticfile_lseek(CPUState *cs, gdb_syscall_complete_cb complete,
                             GuestFD *gf, int64_t off, int gdb_whence)
{
    int64_t ret;

    switch (gdb_whence) {
    case GDB_SEEK_SET:
        ret = off;
        break;
    case GDB_SEEK_CUR:
        ret = gf->staticfile.off + off;
        break;
    case GDB_SEEK_END:
        ret = gf->staticfile.len + off;
        break;
    default:
        ret = -1;
        break;
    }
    if (ret >= 0 && ret <= gf->staticfile.len) {
        gf->staticfile.off = ret;
        complete(cs, ret, 0);
    } else {
        complete(cs, -1, EINVAL);
    }
}

static void staticfile_flen(CPUState *cs, gdb_syscall_complete_cb complete,
                            GuestFD *gf)
{
    complete(cs, gf->staticfile.len, 0);
}

/*
 * Console semihosting syscall implementations.
 */

static void console_read(CPUState *cs, gdb_syscall_complete_cb complete,
                         GuestFD *gf, target_ulong buf, target_ulong len)
{
    CPUArchState *env G_GNUC_UNUSED = cs->env_ptr;
    char *ptr;
    int ret;

    ptr = lock_user(VERIFY_WRITE, buf, len, 0);
    if (!ptr) {
        complete(cs, -1, EFAULT);
        return;
    }
    ret = qemu_semihosting_console_read(cs, ptr, len);
    unlock_user(ptr, buf, ret);
    complete(cs, ret, 0);
}

static void console_write(CPUState *cs, gdb_syscall_complete_cb complete,
                          GuestFD *gf, target_ulong buf, target_ulong len)
{
    CPUArchState *env G_GNUC_UNUSED = cs->env_ptr;
    char *ptr = lock_user(VERIFY_READ, buf, len, 1);
    int ret;

    if (!ptr) {
        complete(cs, -1, EFAULT);
        return;
    }
    ret = qemu_semihosting_console_write(ptr, len);
    unlock_user(ptr, buf, 0);
    complete(cs, ret ? ret : -1, ret ? 0 : EIO);
}

static void console_fstat(CPUState *cs, gdb_syscall_complete_cb complete,
                          GuestFD *gf, target_ulong addr)
{
    static const struct stat tty_buf = {
        .st_mode = 020666,  /* S_IFCHR, ugo+rw */
        .st_rdev = 5,       /* makedev(5, 0) -- linux /dev/tty */
    };
    int ret;

    ret = copy_stat_to_user(cs, addr, &tty_buf);
    complete(cs, ret ? -1 : 0, ret ? -ret : 0);
}

#ifndef CONFIG_USER_ONLY
static void console_poll_one(CPUState *cs, gdb_syscall_complete_cb complete,
                             GuestFD *gf, GIOCondition cond, int timeout)
{
    /* The semihosting console does not support urgent data or errors. */
    cond &= G_IO_IN | G_IO_OUT;

    /*
     * Since qemu_semihosting_console_write never blocks, we can
     * consider output always ready -- leave G_IO_OUT alone.
     * All that remains is to conditionally signal input ready.
     * Since output ready causes an immediate return, only block
     * for G_IO_IN alone.
     *
     * TODO: Implement proper timeout.  For now, only support
     * indefinite wait or immediate poll.
     */
    if (cond == G_IO_IN && timeout < 0) {
        qemu_semihosting_console_block_until_ready(cs);
        /* We returned -- input must be ready. */
    } else if ((cond & G_IO_IN) && !qemu_semihosting_console_ready()) {
        cond &= ~G_IO_IN;
    }

    complete(cs, cond, 0);
}
#endif

/*
 * Syscall entry points.
 */

void semihost_sys_open(CPUState *cs, gdb_syscall_complete_cb complete,
                       target_ulong fname, target_ulong fname_len,
                       int gdb_flags, int mode)
{
    if (use_gdb_syscalls()) {
        gdb_open(cs, complete, fname, fname_len, gdb_flags, mode);
    } else {
        host_open(cs, complete, fname, fname_len, gdb_flags, mode);
    }
}

void semihost_sys_close(CPUState *cs, gdb_syscall_complete_cb complete, int fd)
{
    GuestFD *gf = get_guestfd(fd);

    if (!gf) {
        complete(cs, -1, EBADF);
        return;
    }
    switch (gf->type) {
    case GuestFDGDB:
        gdb_close(cs, complete, gf);
        break;
    case GuestFDHost:
        host_close(cs, complete, gf);
        break;
    case GuestFDStatic:
    case GuestFDConsole:
        complete(cs, 0, 0);
        break;
    default:
        g_assert_not_reached();
    }
    dealloc_guestfd(fd);
}

void semihost_sys_read_gf(CPUState *cs, gdb_syscall_complete_cb complete,
                          GuestFD *gf, target_ulong buf, target_ulong len)
{
    /*
     * Bound length for 64-bit guests on 32-bit hosts, not overlowing ssize_t.
     * Note the Linux kernel does this with MAX_RW_COUNT, so it's not a bad
     * idea to do this unconditionally.
     */
    if (len > INT32_MAX) {
        len = INT32_MAX;
    }
    switch (gf->type) {
    case GuestFDGDB:
        gdb_read(cs, complete, gf, buf, len);
        break;
    case GuestFDHost:
        host_read(cs, complete, gf, buf, len);
        break;
    case GuestFDStatic:
        staticfile_read(cs, complete, gf, buf, len);
        break;
    case GuestFDConsole:
        console_read(cs, complete, gf, buf, len);
        break;
    default:
        g_assert_not_reached();
    }
}

void semihost_sys_read(CPUState *cs, gdb_syscall_complete_cb complete,
                       int fd, target_ulong buf, target_ulong len)
{
    GuestFD *gf = get_guestfd(fd);

    if (gf) {
        semihost_sys_read_gf(cs, complete, gf, buf, len);
    } else {
        complete(cs, -1, EBADF);
    }
}

void semihost_sys_write_gf(CPUState *cs, gdb_syscall_complete_cb complete,
                           GuestFD *gf, target_ulong buf, target_ulong len)
{
    /*
     * Bound length for 64-bit guests on 32-bit hosts, not overlowing ssize_t.
     * Note the Linux kernel does this with MAX_RW_COUNT, so it's not a bad
     * idea to do this unconditionally.
     */
    if (len > INT32_MAX) {
        len = INT32_MAX;
    }
    switch (gf->type) {
    case GuestFDGDB:
        gdb_write(cs, complete, gf, buf, len);
        break;
    case GuestFDHost:
        host_write(cs, complete, gf, buf, len);
        break;
    case GuestFDConsole:
        console_write(cs, complete, gf, buf, len);
        break;
    case GuestFDStatic:
        /* Static files are never open for writing: EBADF. */
        complete(cs, -1, EBADF);
        break;
    default:
        g_assert_not_reached();
    }
}

void semihost_sys_write(CPUState *cs, gdb_syscall_complete_cb complete,
                        int fd, target_ulong buf, target_ulong len)
{
    GuestFD *gf = get_guestfd(fd);

    if (gf) {
        semihost_sys_write_gf(cs, complete, gf, buf, len);
    } else {
        complete(cs, -1, EBADF);
    }
}

void semihost_sys_lseek(CPUState *cs, gdb_syscall_complete_cb complete,
                        int fd, int64_t off, int gdb_whence)
{
    GuestFD *gf = get_guestfd(fd);

    if (!gf) {
        complete(cs, -1, EBADF);
        return;
    }
    switch (gf->type) {
    case GuestFDGDB:
        gdb_lseek(cs, complete, gf, off, gdb_whence);
        return;
    case GuestFDHost:
        host_lseek(cs, complete, gf, off, gdb_whence);
        break;
    case GuestFDStatic:
        staticfile_lseek(cs, complete, gf, off, gdb_whence);
        break;
    case GuestFDConsole:
        complete(cs, -1, ESPIPE);
        break;
    default:
        g_assert_not_reached();
    }
}

void semihost_sys_isatty(CPUState *cs, gdb_syscall_complete_cb complete, int fd)
{
    GuestFD *gf = get_guestfd(fd);

    if (!gf) {
        complete(cs, 0, EBADF);
        return;
    }
    switch (gf->type) {
    case GuestFDGDB:
        gdb_isatty(cs, complete, gf);
        break;
    case GuestFDHost:
        host_isatty(cs, complete, gf);
        break;
    case GuestFDStatic:
        complete(cs, 0, ENOTTY);
        break;
    case GuestFDConsole:
        complete(cs, 1, 0);
        break;
    default:
        g_assert_not_reached();
    }
}

void semihost_sys_flen(CPUState *cs, gdb_syscall_complete_cb fstat_cb,
                       gdb_syscall_complete_cb flen_cb, int fd,
                       target_ulong fstat_addr)
{
    GuestFD *gf = get_guestfd(fd);

    if (!gf) {
        flen_cb(cs, -1, EBADF);
        return;
    }
    switch (gf->type) {
    case GuestFDGDB:
        gdb_fstat(cs, fstat_cb, gf, fstat_addr);
        break;
    case GuestFDHost:
        host_flen(cs, flen_cb, gf);
        break;
    case GuestFDStatic:
        staticfile_flen(cs, flen_cb, gf);
        break;
    case GuestFDConsole:
    default:
        g_assert_not_reached();
    }
}

void semihost_sys_fstat(CPUState *cs, gdb_syscall_complete_cb complete,
                        int fd, target_ulong addr)
{
    GuestFD *gf = get_guestfd(fd);

    if (!gf) {
        complete(cs, -1, EBADF);
        return;
    }
    switch (gf->type) {
    case GuestFDGDB:
        gdb_fstat(cs, complete, gf, addr);
        break;
    case GuestFDHost:
        host_fstat(cs, complete, gf, addr);
        break;
    case GuestFDConsole:
        console_fstat(cs, complete, gf, addr);
        break;
    case GuestFDStatic:
    default:
        g_assert_not_reached();
    }
}

void semihost_sys_stat(CPUState *cs, gdb_syscall_complete_cb complete,
                       target_ulong fname, target_ulong fname_len,
                       target_ulong addr)
{
    if (use_gdb_syscalls()) {
        gdb_stat(cs, complete, fname, fname_len, addr);
    } else {
        host_stat(cs, complete, fname, fname_len, addr);
    }
}

void semihost_sys_remove(CPUState *cs, gdb_syscall_complete_cb complete,
                         target_ulong fname, target_ulong fname_len)
{
    if (use_gdb_syscalls()) {
        gdb_remove(cs, complete, fname, fname_len);
    } else {
        host_remove(cs, complete, fname, fname_len);
    }
}

void semihost_sys_rename(CPUState *cs, gdb_syscall_complete_cb complete,
                         target_ulong oname, target_ulong oname_len,
                         target_ulong nname, target_ulong nname_len)
{
    if (use_gdb_syscalls()) {
        gdb_rename(cs, complete, oname, oname_len, nname, nname_len);
    } else {
        host_rename(cs, complete, oname, oname_len, nname, nname_len);
    }
}

void semihost_sys_system(CPUState *cs, gdb_syscall_complete_cb complete,
                         target_ulong cmd, target_ulong cmd_len)
{
    if (use_gdb_syscalls()) {
        gdb_system(cs, complete, cmd, cmd_len);
    } else {
        host_system(cs, complete, cmd, cmd_len);
    }
}

void semihost_sys_gettimeofday(CPUState *cs, gdb_syscall_complete_cb complete,
                               target_ulong tv_addr, target_ulong tz_addr)
{
    if (use_gdb_syscalls()) {
        gdb_gettimeofday(cs, complete, tv_addr, tz_addr);
    } else {
        host_gettimeofday(cs, complete, tv_addr, tz_addr);
    }
}

#ifndef CONFIG_USER_ONLY
void semihost_sys_poll_one(CPUState *cs, gdb_syscall_complete_cb complete,
                           int fd, GIOCondition cond, int timeout)
{
    GuestFD *gf = get_guestfd(fd);

    if (!gf) {
        complete(cs, G_IO_NVAL, 1);
        return;
    }
    switch (gf->type) {
    case GuestFDGDB:
        complete(cs, G_IO_NVAL, 1);
        break;
    case GuestFDHost:
        host_poll_one(cs, complete, gf, cond, timeout);
        break;
    case GuestFDConsole:
        console_poll_one(cs, complete, gf, cond, timeout);
        break;
    case GuestFDStatic:
    default:
        g_assert_not_reached();
    }
}
#endif
