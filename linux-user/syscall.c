/*
 *  Linux syscalls
 * 
 *  Copyright (c) 2003 Fabrice Bellard
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
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <elf.h>
#include <endian.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <sys/swap.h>
#include <signal.h>
#include <sched.h>
#include <sys/socket.h>
#include <sys/uio.h>
//#include <sys/user.h>

#define termios host_termios
#define winsize host_winsize
#define termio host_termio

#include <linux/termios.h>
#include <linux/unistd.h>
#include <linux/utsname.h>
#include <linux/cdrom.h>
#include <linux/hdreg.h>
#include <linux/soundcard.h>

#include "gemu.h"

//#define DEBUG

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#define PAGE_MASK ~(PAGE_SIZE - 1)
#endif

struct dirent {
        long            d_ino;
        long            d_off;
        unsigned short  d_reclen;
        char            d_name[256]; /* We must not include limits.h! */
};

#include "syscall_defs.h"

#ifdef TARGET_I386
#include "syscall-i386.h"
#endif

#define __NR_sys_uname __NR_uname
#define __NR_sys_getcwd1 __NR_getcwd
#define __NR_sys_statfs __NR_statfs
#define __NR_sys_fstatfs __NR_fstatfs
#define __NR_sys_getdents __NR_getdents

#ifdef __NR_gettid
_syscall0(int, gettid)
#else
static int gettid(void) {
    return -ENOSYS;
}
#endif
_syscall1(int,sys_uname,struct new_utsname *,buf)
_syscall2(int,sys_getcwd1,char *,buf,size_t,size)
_syscall3(int, sys_getdents, uint, fd, struct dirent *, dirp, uint, count);
_syscall5(int, _llseek,  uint,  fd, ulong, hi, ulong, lo,
          loff_t *, res, uint, wh);
_syscall2(int,sys_statfs,const char *,path,struct kernel_statfs *,buf)
_syscall2(int,sys_fstatfs,int,fd,struct kernel_statfs *,buf)

static inline long get_errno(long ret)
{
    if (ret == -1)
        return -errno;
    else
        return ret;
}

static inline int is_error(long ret)
{
    return (unsigned long)ret >= (unsigned long)(-4096);
}

static char *target_brk;
static char *target_original_brk;

void target_set_brk(char *new_brk)
{
    target_brk = new_brk;
    target_original_brk = new_brk;
}

static long do_brk(char *new_brk)
{
    char *brk_page;
    long mapped_addr;
    int	new_alloc_size;

    if (!new_brk)
        return (long)target_brk;
    if (new_brk < target_original_brk)
        return -ENOMEM;
    
    brk_page = (char *)(((unsigned long)target_brk + PAGE_SIZE - 1) & PAGE_MASK);

    /* If the new brk is less than this, set it and we're done... */
    if (new_brk < brk_page) {
	target_brk = new_brk;
    	return (long)target_brk;
    }

    /* We need to allocate more memory after the brk... */
    new_alloc_size = ((new_brk - brk_page + 1)+(PAGE_SIZE-1)) & PAGE_MASK;
    mapped_addr = get_errno((long)mmap((caddr_t)brk_page, new_alloc_size, 
                                       PROT_READ|PROT_WRITE,
                                       MAP_ANON|MAP_FIXED|MAP_PRIVATE, 0, 0));
    
    if (is_error(mapped_addr)) {
	return mapped_addr;
    } else {
	target_brk = new_brk;
    	return (long)target_brk;
    }
}

static inline fd_set *target_to_host_fds(fd_set *fds, 
                                         target_long *target_fds, int n)
{
#if !defined(BSWP_NEEDED) && !defined(WORD_BIGENDIAN)
    return (fd_set *)target_fds;
#else
    int i, b;
    if (target_fds) {
        FD_ZERO(fds);
        for(i = 0;i < n; i++) {
            b = (tswapl(target_fds[i / TARGET_LONG_BITS]) >>
                 (i & (TARGET_LONG_BITS - 1))) & 1;
            if (b)
                FD_SET(i, fds);
        }
        return fds;
    } else {
        return NULL;
    }
#endif
}

static inline void host_to_target_fds(target_long *target_fds, 
                                      fd_set *fds, int n)
{
#if !defined(BSWP_NEEDED) && !defined(WORD_BIGENDIAN)
    /* nothing to do */
#else
    int i, nw, j, k;
    target_long v;

    if (target_fds) {
        nw = n / TARGET_LONG_BITS;
        k = 0;
        for(i = 0;i < nw; i++) {
            v = 0;
            for(j = 0; j < TARGET_LONG_BITS; j++) {
                v |= ((FD_ISSET(k, fds) != 0) << j);
                k++;
            }
            target_fds[i] = tswapl(v);
        }
    }
#endif
}

/* XXX: incorrect for some archs */
static void host_to_target_old_sigset(target_ulong *old_sigset, 
                                      const sigset_t *sigset)
{
    *old_sigset = tswap32(*(unsigned long *)sigset & 0xffffffff);
}

static void target_to_host_old_sigset(sigset_t *sigset, 
                                      const target_ulong *old_sigset)
{
    sigemptyset(sigset);
    *(unsigned long *)sigset = tswapl(*old_sigset);
}


static long do_select(long n, 
                      target_long *target_rfds, target_long *target_wfds, 
                      target_long *target_efds, struct target_timeval *target_tv)
{
    fd_set rfds, wfds, efds;
    fd_set *rfds_ptr, *wfds_ptr, *efds_ptr;
    struct timeval tv, *tv_ptr;
    long ret;

    rfds_ptr = target_to_host_fds(&rfds, target_rfds, n);
    wfds_ptr = target_to_host_fds(&wfds, target_wfds, n);
    efds_ptr = target_to_host_fds(&efds, target_efds, n);
            
    if (target_tv) {
        tv.tv_sec = tswapl(target_tv->tv_sec);
        tv.tv_usec = tswapl(target_tv->tv_usec);
        tv_ptr = &tv;
    } else {
        tv_ptr = NULL;
    }
    ret = get_errno(select(n, rfds_ptr, wfds_ptr, efds_ptr, tv_ptr));
    if (!is_error(ret)) {
        host_to_target_fds(target_rfds, rfds_ptr, n);
        host_to_target_fds(target_wfds, wfds_ptr, n);
        host_to_target_fds(target_efds, efds_ptr, n);

        if (target_tv) {
            target_tv->tv_sec = tswapl(tv.tv_sec);
            target_tv->tv_usec = tswapl(tv.tv_usec);
        }
    }
    return ret;
}

static long do_socketcall(int num, long *vptr)
{
    long ret;

    switch(num) {
    case SOCKOP_socket:
        ret = get_errno(socket(vptr[0], vptr[1], vptr[2]));
        break;
    case SOCKOP_bind:
        ret = get_errno(bind(vptr[0], (struct sockaddr *)vptr[1], vptr[2]));
        break;
    case SOCKOP_connect:
        ret = get_errno(connect(vptr[0], (struct sockaddr *)vptr[1], vptr[2]));
        break;
    case SOCKOP_listen:
        ret = get_errno(listen(vptr[0], vptr[1]));
        break;
    case SOCKOP_accept:
        {
            socklen_t size;
            size = tswap32(*(int32_t *)vptr[2]);
            ret = get_errno(accept(vptr[0], (struct sockaddr *)vptr[1], &size));
            if (!is_error(ret)) 
                *(int32_t *)vptr[2] = size;
        }
        break;
    case SOCKOP_getsockname:
        {
            socklen_t size;
            size = tswap32(*(int32_t *)vptr[2]);
            ret = get_errno(getsockname(vptr[0], (struct sockaddr *)vptr[1], &size));
            if (!is_error(ret)) 
                *(int32_t *)vptr[2] = size;
        }
        break;
    case SOCKOP_getpeername:
        {
            socklen_t size;
            size = tswap32(*(int32_t *)vptr[2]);
            ret = get_errno(getpeername(vptr[0], (struct sockaddr *)vptr[1], &size));
            if (!is_error(ret)) 
                *(int32_t *)vptr[2] = size;
        }
        break;
    case SOCKOP_socketpair:
        {
            int tab[2];
            int32_t *target_tab = (int32_t *)vptr[3];
            ret = get_errno(socketpair(vptr[0], vptr[1], vptr[2], tab));
            if (!is_error(ret)) {
                target_tab[0] = tswap32(tab[0]);
                target_tab[1] = tswap32(tab[1]);
            }
        }
        break;
    case SOCKOP_send:
        ret = get_errno(send(vptr[0], (void *)vptr[1], vptr[2], vptr[3]));
        break;
    case SOCKOP_recv:
        ret = get_errno(recv(vptr[0], (void *)vptr[1], vptr[2], vptr[3]));
        break;
    case SOCKOP_sendto:
        ret = get_errno(sendto(vptr[0], (void *)vptr[1], vptr[2], vptr[3], 
                               (struct sockaddr *)vptr[4], vptr[5]));
        break;
    case SOCKOP_recvfrom:
        {
            socklen_t size;
            size = tswap32(*(int32_t *)vptr[5]);
            ret = get_errno(recvfrom(vptr[0], (void *)vptr[1], vptr[2], 
                                     vptr[3], (struct sockaddr *)vptr[4], &size));
            if (!is_error(ret)) 
                *(int32_t *)vptr[5] = size;
        }
        break;
    case SOCKOP_shutdown:
        ret = get_errno(shutdown(vptr[0], vptr[1]));
        break;
    case SOCKOP_sendmsg:
    case SOCKOP_recvmsg:
    case SOCKOP_setsockopt:
    case SOCKOP_getsockopt:
    default:
        gemu_log("Unsupported socketcall: %d\n", num);
        ret = -ENOSYS;
        break;
    }
    return ret;
}

/* kernel structure types definitions */
#define IFNAMSIZ        16

#define STRUCT(name, list...) STRUCT_ ## name,
#define STRUCT_SPECIAL(name) STRUCT_ ## name,
enum {
#include "syscall_types.h"
};
#undef STRUCT
#undef STRUCT_SPECIAL

#define STRUCT(name, list...) const argtype struct_ ## name ## _def[] = { list, TYPE_NULL };
#define STRUCT_SPECIAL(name)
#include "syscall_types.h"
#undef STRUCT
#undef STRUCT_SPECIAL

typedef struct IOCTLEntry {
    int target_cmd;
    int host_cmd;
    const char *name;
    int access;
    const argtype arg_type[3];
} IOCTLEntry;

#define IOC_R 0x0001
#define IOC_W 0x0002
#define IOC_RW (IOC_R | IOC_W)

#define MAX_STRUCT_SIZE 4096

const IOCTLEntry ioctl_entries[] = {
#define IOCTL(cmd, access, types...) \
    { TARGET_ ## cmd, cmd, #cmd, access, { types } },
#include "ioctls.h"
    { 0, 0, },
};

static long do_ioctl(long fd, long cmd, long arg)
{
    const IOCTLEntry *ie;
    const argtype *arg_type;
    long ret;
    uint8_t buf_temp[MAX_STRUCT_SIZE];

    ie = ioctl_entries;
    for(;;) {
        if (ie->target_cmd == 0) {
            gemu_log("Unsupported ioctl: cmd=0x%04lx\n", cmd);
            return -ENOSYS;
        }
        if (ie->target_cmd == cmd)
            break;
        ie++;
    }
    arg_type = ie->arg_type;
#ifdef DEBUG
    gemu_log("ioctl: cmd=0x%04lx (%s)\n", cmd, ie->name);
#endif
    switch(arg_type[0]) {
    case TYPE_NULL:
        /* no argument */
        ret = get_errno(ioctl(fd, ie->host_cmd));
        break;
    case TYPE_PTRVOID:
    case TYPE_INT:
        /* int argment */
        ret = get_errno(ioctl(fd, ie->host_cmd, arg));
        break;
    case TYPE_PTR:
        arg_type++;
        switch(ie->access) {
        case IOC_R:
            ret = get_errno(ioctl(fd, ie->host_cmd, buf_temp));
            if (!is_error(ret)) {
                thunk_convert((void *)arg, buf_temp, arg_type, THUNK_TARGET);
            }
            break;
        case IOC_W:
            thunk_convert(buf_temp, (void *)arg, arg_type, THUNK_HOST);
            ret = get_errno(ioctl(fd, ie->host_cmd, buf_temp));
            break;
        default:
        case IOC_RW:
            thunk_convert(buf_temp, (void *)arg, arg_type, THUNK_HOST);
            ret = get_errno(ioctl(fd, ie->host_cmd, buf_temp));
            if (!is_error(ret)) {
                thunk_convert((void *)arg, buf_temp, arg_type, THUNK_TARGET);
            }
            break;
        }
        break;
    default:
        gemu_log("Unsupported ioctl type: cmd=0x%04lx type=%d\n", cmd, arg_type[0]);
        ret = -ENOSYS;
        break;
    }
    return ret;
}

bitmask_transtbl iflag_tbl[] = {
        { TARGET_IGNBRK, TARGET_IGNBRK, IGNBRK, IGNBRK },
        { TARGET_BRKINT, TARGET_BRKINT, BRKINT, BRKINT },
        { TARGET_IGNPAR, TARGET_IGNPAR, IGNPAR, IGNPAR },
        { TARGET_PARMRK, TARGET_PARMRK, PARMRK, PARMRK },
        { TARGET_INPCK, TARGET_INPCK, INPCK, INPCK },
        { TARGET_ISTRIP, TARGET_ISTRIP, ISTRIP, ISTRIP },
        { TARGET_INLCR, TARGET_INLCR, INLCR, INLCR },
        { TARGET_IGNCR, TARGET_IGNCR, IGNCR, IGNCR },
        { TARGET_ICRNL, TARGET_ICRNL, ICRNL, ICRNL },
        { TARGET_IUCLC, TARGET_IUCLC, IUCLC, IUCLC },
        { TARGET_IXON, TARGET_IXON, IXON, IXON },
        { TARGET_IXANY, TARGET_IXANY, IXANY, IXANY },
        { TARGET_IXOFF, TARGET_IXOFF, IXOFF, IXOFF },
        { TARGET_IMAXBEL, TARGET_IMAXBEL, IMAXBEL, IMAXBEL },
        { 0, 0, 0, 0 }
};

bitmask_transtbl oflag_tbl[] = {
	{ TARGET_OPOST, TARGET_OPOST, OPOST, OPOST },
	{ TARGET_OLCUC, TARGET_OLCUC, OLCUC, OLCUC },
	{ TARGET_ONLCR, TARGET_ONLCR, ONLCR, ONLCR },
	{ TARGET_OCRNL, TARGET_OCRNL, OCRNL, OCRNL },
	{ TARGET_ONOCR, TARGET_ONOCR, ONOCR, ONOCR },
	{ TARGET_ONLRET, TARGET_ONLRET, ONLRET, ONLRET },
	{ TARGET_OFILL, TARGET_OFILL, OFILL, OFILL },
	{ TARGET_OFDEL, TARGET_OFDEL, OFDEL, OFDEL },
	{ TARGET_NLDLY, TARGET_NL0, NLDLY, NL0 },
	{ TARGET_NLDLY, TARGET_NL1, NLDLY, NL1 },
	{ TARGET_CRDLY, TARGET_CR0, CRDLY, CR0 },
	{ TARGET_CRDLY, TARGET_CR1, CRDLY, CR1 },
	{ TARGET_CRDLY, TARGET_CR2, CRDLY, CR2 },
	{ TARGET_CRDLY, TARGET_CR3, CRDLY, CR3 },
	{ TARGET_TABDLY, TARGET_TAB0, TABDLY, TAB0 },
	{ TARGET_TABDLY, TARGET_TAB1, TABDLY, TAB1 },
	{ TARGET_TABDLY, TARGET_TAB2, TABDLY, TAB2 },
	{ TARGET_TABDLY, TARGET_TAB3, TABDLY, TAB3 },
	{ TARGET_BSDLY, TARGET_BS0, BSDLY, BS0 },
	{ TARGET_BSDLY, TARGET_BS1, BSDLY, BS1 },
	{ TARGET_VTDLY, TARGET_VT0, VTDLY, VT0 },
	{ TARGET_VTDLY, TARGET_VT1, VTDLY, VT1 },
	{ TARGET_FFDLY, TARGET_FF0, FFDLY, FF0 },
	{ TARGET_FFDLY, TARGET_FF1, FFDLY, FF1 },
	{ 0, 0, 0, 0 }
};

bitmask_transtbl cflag_tbl[] = {
	{ TARGET_CBAUD, TARGET_B0, CBAUD, B0 },
	{ TARGET_CBAUD, TARGET_B50, CBAUD, B50 },
	{ TARGET_CBAUD, TARGET_B75, CBAUD, B75 },
	{ TARGET_CBAUD, TARGET_B110, CBAUD, B110 },
	{ TARGET_CBAUD, TARGET_B134, CBAUD, B134 },
	{ TARGET_CBAUD, TARGET_B150, CBAUD, B150 },
	{ TARGET_CBAUD, TARGET_B200, CBAUD, B200 },
	{ TARGET_CBAUD, TARGET_B300, CBAUD, B300 },
	{ TARGET_CBAUD, TARGET_B600, CBAUD, B600 },
	{ TARGET_CBAUD, TARGET_B1200, CBAUD, B1200 },
	{ TARGET_CBAUD, TARGET_B1800, CBAUD, B1800 },
	{ TARGET_CBAUD, TARGET_B2400, CBAUD, B2400 },
	{ TARGET_CBAUD, TARGET_B4800, CBAUD, B4800 },
	{ TARGET_CBAUD, TARGET_B9600, CBAUD, B9600 },
	{ TARGET_CBAUD, TARGET_B19200, CBAUD, B19200 },
	{ TARGET_CBAUD, TARGET_B38400, CBAUD, B38400 },
	{ TARGET_CBAUD, TARGET_B57600, CBAUD, B57600 },
	{ TARGET_CBAUD, TARGET_B115200, CBAUD, B115200 },
	{ TARGET_CBAUD, TARGET_B230400, CBAUD, B230400 },
	{ TARGET_CBAUD, TARGET_B460800, CBAUD, B460800 },
	{ TARGET_CSIZE, TARGET_CS5, CSIZE, CS5 },
	{ TARGET_CSIZE, TARGET_CS6, CSIZE, CS6 },
	{ TARGET_CSIZE, TARGET_CS7, CSIZE, CS7 },
	{ TARGET_CSIZE, TARGET_CS8, CSIZE, CS8 },
	{ TARGET_CSTOPB, TARGET_CSTOPB, CSTOPB, CSTOPB },
	{ TARGET_CREAD, TARGET_CREAD, CREAD, CREAD },
	{ TARGET_PARENB, TARGET_PARENB, PARENB, PARENB },
	{ TARGET_PARODD, TARGET_PARODD, PARODD, PARODD },
	{ TARGET_HUPCL, TARGET_HUPCL, HUPCL, HUPCL },
	{ TARGET_CLOCAL, TARGET_CLOCAL, CLOCAL, CLOCAL },
	{ TARGET_CRTSCTS, TARGET_CRTSCTS, CRTSCTS, CRTSCTS },
	{ 0, 0, 0, 0 }
};

bitmask_transtbl lflag_tbl[] = {
	{ TARGET_ISIG, TARGET_ISIG, ISIG, ISIG },
	{ TARGET_ICANON, TARGET_ICANON, ICANON, ICANON },
	{ TARGET_XCASE, TARGET_XCASE, XCASE, XCASE },
	{ TARGET_ECHO, TARGET_ECHO, ECHO, ECHO },
	{ TARGET_ECHOE, TARGET_ECHOE, ECHOE, ECHOE },
	{ TARGET_ECHOK, TARGET_ECHOK, ECHOK, ECHOK },
	{ TARGET_ECHONL, TARGET_ECHONL, ECHONL, ECHONL },
	{ TARGET_NOFLSH, TARGET_NOFLSH, NOFLSH, NOFLSH },
	{ TARGET_TOSTOP, TARGET_TOSTOP, TOSTOP, TOSTOP },
	{ TARGET_ECHOCTL, TARGET_ECHOCTL, ECHOCTL, ECHOCTL },
	{ TARGET_ECHOPRT, TARGET_ECHOPRT, ECHOPRT, ECHOPRT },
	{ TARGET_ECHOKE, TARGET_ECHOKE, ECHOKE, ECHOKE },
	{ TARGET_FLUSHO, TARGET_FLUSHO, FLUSHO, FLUSHO },
	{ TARGET_PENDIN, TARGET_PENDIN, PENDIN, PENDIN },
	{ TARGET_IEXTEN, TARGET_IEXTEN, IEXTEN, IEXTEN },
	{ 0, 0, 0, 0 }
};

static void target_to_host_termios (void *dst, const void *src)
{
    struct host_termios *host = dst;
    const struct target_termios *target = src;
    
    host->c_iflag = 
        target_to_host_bitmask(tswap32(target->c_iflag), iflag_tbl);
    host->c_oflag = 
        target_to_host_bitmask(tswap32(target->c_oflag), oflag_tbl);
    host->c_cflag = 
        target_to_host_bitmask(tswap32(target->c_cflag), cflag_tbl);
    host->c_lflag = 
        target_to_host_bitmask(tswap32(target->c_lflag), lflag_tbl);
    host->c_line = target->c_line;
    
    host->c_cc[VINTR] = target->c_cc[TARGET_VINTR]; 
    host->c_cc[VQUIT] = target->c_cc[TARGET_VQUIT]; 
    host->c_cc[VERASE] = target->c_cc[TARGET_VERASE];       
    host->c_cc[VKILL] = target->c_cc[TARGET_VKILL]; 
    host->c_cc[VEOF] = target->c_cc[TARGET_VEOF];   
    host->c_cc[VTIME] = target->c_cc[TARGET_VTIME]; 
    host->c_cc[VMIN] = target->c_cc[TARGET_VMIN];   
    host->c_cc[VSWTC] = target->c_cc[TARGET_VSWTC]; 
    host->c_cc[VSTART] = target->c_cc[TARGET_VSTART];       
    host->c_cc[VSTOP] = target->c_cc[TARGET_VSTOP]; 
    host->c_cc[VSUSP] = target->c_cc[TARGET_VSUSP]; 
    host->c_cc[VEOL] = target->c_cc[TARGET_VEOL];   
    host->c_cc[VREPRINT] = target->c_cc[TARGET_VREPRINT];   
    host->c_cc[VDISCARD] = target->c_cc[TARGET_VDISCARD];   
    host->c_cc[VWERASE] = target->c_cc[TARGET_VWERASE];     
    host->c_cc[VLNEXT] = target->c_cc[TARGET_VLNEXT];       
    host->c_cc[VEOL2] = target->c_cc[TARGET_VEOL2]; 
}
  
static void host_to_target_termios (void *dst, const void *src)
{
    struct target_termios *target = dst;
    const struct host_termios *host = src;

    target->c_iflag = 
        tswap32(host_to_target_bitmask(host->c_iflag, iflag_tbl));
    target->c_oflag = 
        tswap32(host_to_target_bitmask(host->c_oflag, oflag_tbl));
    target->c_cflag = 
        tswap32(host_to_target_bitmask(host->c_cflag, cflag_tbl));
    target->c_lflag = 
        tswap32(host_to_target_bitmask(host->c_lflag, lflag_tbl));
    target->c_line = host->c_line;
  
    target->c_cc[TARGET_VINTR] = host->c_cc[VINTR];
    target->c_cc[TARGET_VQUIT] = host->c_cc[VQUIT];
    target->c_cc[TARGET_VERASE] = host->c_cc[VERASE];
    target->c_cc[TARGET_VKILL] = host->c_cc[VKILL];
    target->c_cc[TARGET_VEOF] = host->c_cc[VEOF];
    target->c_cc[TARGET_VTIME] = host->c_cc[VTIME];
    target->c_cc[TARGET_VMIN] = host->c_cc[VMIN];
    target->c_cc[TARGET_VSWTC] = host->c_cc[VSWTC];
    target->c_cc[TARGET_VSTART] = host->c_cc[VSTART];
    target->c_cc[TARGET_VSTOP] = host->c_cc[VSTOP];
    target->c_cc[TARGET_VSUSP] = host->c_cc[VSUSP];
    target->c_cc[TARGET_VEOL] = host->c_cc[VEOL];
    target->c_cc[TARGET_VREPRINT] = host->c_cc[VREPRINT];
    target->c_cc[TARGET_VDISCARD] = host->c_cc[VDISCARD];
    target->c_cc[TARGET_VWERASE] = host->c_cc[VWERASE];
    target->c_cc[TARGET_VLNEXT] = host->c_cc[VLNEXT];
    target->c_cc[TARGET_VEOL2] = host->c_cc[VEOL2];
}

StructEntry struct_termios_def = {
    .convert = { host_to_target_termios, target_to_host_termios },
    .size = { sizeof(struct target_termios), sizeof(struct host_termios) },
    .align = { __alignof__(struct target_termios), __alignof__(struct host_termios) },
};

void syscall_init(void)
{
#define STRUCT(name, list...) thunk_register_struct(STRUCT_ ## name, #name, struct_ ## name ## _def); 
#define STRUCT_SPECIAL(name) thunk_register_struct_direct(STRUCT_ ## name, #name, &struct_ ## name ## _def); 
#include "syscall_types.h"
#undef STRUCT
#undef STRUCT_SPECIAL
}
                                 
long do_syscall(int num, long arg1, long arg2, long arg3, 
                long arg4, long arg5, long arg6)
{
    long ret;
    struct stat st;
    struct kernel_statfs *stfs;
    
#ifdef DEBUG
    gemu_log("syscall %d\n", num);
#endif
    switch(num) {
    case TARGET_NR_exit:
        _exit(arg1);
        ret = 0; /* avoid warning */
        break;
    case TARGET_NR_read:
        ret = get_errno(read(arg1, (void *)arg2, arg3));
        break;
    case TARGET_NR_write:
        ret = get_errno(write(arg1, (void *)arg2, arg3));
        break;
    case TARGET_NR_open:
        ret = get_errno(open((const char *)arg1, arg2, arg3));
        break;
    case TARGET_NR_close:
        ret = get_errno(close(arg1));
        break;
    case TARGET_NR_brk:
        ret = do_brk((char *)arg1);
        break;
    case TARGET_NR_fork:
        ret = get_errno(fork());
        break;
    case TARGET_NR_waitpid:
        {
            int *status = (int *)arg2;
            ret = get_errno(waitpid(arg1, status, arg3));
            if (!is_error(ret) && status)
                tswapls((long *)&status);
        }
        break;
    case TARGET_NR_creat:
        ret = get_errno(creat((const char *)arg1, arg2));
        break;
    case TARGET_NR_link:
        ret = get_errno(link((const char *)arg1, (const char *)arg2));
        break;
    case TARGET_NR_unlink:
        ret = get_errno(unlink((const char *)arg1));
        break;
    case TARGET_NR_execve:
        ret = get_errno(execve((const char *)arg1, (void *)arg2, (void *)arg3));
        break;
    case TARGET_NR_chdir:
        ret = get_errno(chdir((const char *)arg1));
        break;
    case TARGET_NR_time:
        {
            int *time_ptr = (int *)arg1;
            ret = get_errno(time((time_t *)time_ptr));
            if (!is_error(ret) && time_ptr)
                tswap32s(time_ptr);
        }
        break;
    case TARGET_NR_mknod:
        ret = get_errno(mknod((const char *)arg1, arg2, arg3));
        break;
    case TARGET_NR_chmod:
        ret = get_errno(chmod((const char *)arg1, arg2));
        break;
    case TARGET_NR_lchown:
        ret = get_errno(chown((const char *)arg1, arg2, arg3));
        break;
    case TARGET_NR_break:
        goto unimplemented;
    case TARGET_NR_oldstat:
        goto unimplemented;
    case TARGET_NR_lseek:
        ret = get_errno(lseek(arg1, arg2, arg3));
        break;
    case TARGET_NR_getpid:
        ret = get_errno(getpid());
        break;
    case TARGET_NR_mount:
        /* need to look at the data field */
        goto unimplemented;
    case TARGET_NR_umount:
        ret = get_errno(umount((const char *)arg1));
        break;
    case TARGET_NR_setuid:
        ret = get_errno(setuid(arg1));
        break;
    case TARGET_NR_getuid:
        ret = get_errno(getuid());
        break;
    case TARGET_NR_stime:
        {
            int *time_ptr = (int *)arg1;
            if (time_ptr)
                tswap32s(time_ptr);
            ret = get_errno(stime((time_t *)time_ptr));
        }
        break;
    case TARGET_NR_ptrace:
        goto unimplemented;
    case TARGET_NR_alarm:
        ret = alarm(arg1);
        break;
    case TARGET_NR_oldfstat:
        goto unimplemented;
    case TARGET_NR_pause:
        ret = get_errno(pause());
        break;
    case TARGET_NR_utime:
        goto unimplemented;
    case TARGET_NR_stty:
        goto unimplemented;
    case TARGET_NR_gtty:
        goto unimplemented;
    case TARGET_NR_access:
        ret = get_errno(access((const char *)arg1, arg2));
        break;
    case TARGET_NR_nice:
        ret = get_errno(nice(arg1));
        break;
    case TARGET_NR_ftime:
        goto unimplemented;
    case TARGET_NR_sync:
        ret = get_errno(sync());
        break;
    case TARGET_NR_kill:
        ret = get_errno(kill(arg1, arg2));
        break;
    case TARGET_NR_rename:
        ret = get_errno(rename((const char *)arg1, (const char *)arg2));
        break;
    case TARGET_NR_mkdir:
        ret = get_errno(mkdir((const char *)arg1, arg2));
        break;
    case TARGET_NR_rmdir:
        ret = get_errno(rmdir((const char *)arg1));
        break;
    case TARGET_NR_dup:
        ret = get_errno(dup(arg1));
        break;
    case TARGET_NR_pipe:
        {
            int *pipe_ptr = (int *)arg1;
            ret = get_errno(pipe(pipe_ptr));
            if (!is_error(ret)) {
                tswap32s(&pipe_ptr[0]);
                tswap32s(&pipe_ptr[1]);
            }
        }
        break;
    case TARGET_NR_times:
        goto unimplemented;
    case TARGET_NR_prof:
        goto unimplemented;
    case TARGET_NR_setgid:
        ret = get_errno(setgid(arg1));
        break;
    case TARGET_NR_getgid:
        ret = get_errno(getgid());
        break;
    case TARGET_NR_signal:
        goto unimplemented;
    case TARGET_NR_geteuid:
        ret = get_errno(geteuid());
        break;
    case TARGET_NR_getegid:
        ret = get_errno(getegid());
        break;
    case TARGET_NR_acct:
        goto unimplemented;
    case TARGET_NR_umount2:
        ret = get_errno(umount2((const char *)arg1, arg2));
        break;
    case TARGET_NR_lock:
        goto unimplemented;
    case TARGET_NR_ioctl:
        ret = do_ioctl(arg1, arg2, arg3);
        break;
    case TARGET_NR_fcntl:
        switch(arg2) {
        case F_GETLK:
        case F_SETLK:
        case F_SETLKW:
            goto unimplemented;
        default:
            ret = get_errno(fcntl(arg1, arg2, arg3));
            break;
        }
        break;
    case TARGET_NR_mpx:
        goto unimplemented;
    case TARGET_NR_setpgid:
        ret = get_errno(setpgid(arg1, arg2));
        break;
    case TARGET_NR_ulimit:
        goto unimplemented;
    case TARGET_NR_oldolduname:
        goto unimplemented;
    case TARGET_NR_umask:
        ret = get_errno(umask(arg1));
        break;
    case TARGET_NR_chroot:
        ret = get_errno(chroot((const char *)arg1));
        break;
    case TARGET_NR_ustat:
        goto unimplemented;
    case TARGET_NR_dup2:
        ret = get_errno(dup2(arg1, arg2));
        break;
    case TARGET_NR_getppid:
        ret = get_errno(getppid());
        break;
    case TARGET_NR_getpgrp:
        ret = get_errno(getpgrp());
        break;
    case TARGET_NR_setsid:
        ret = get_errno(setsid());
        break;
    case TARGET_NR_sigaction:
#if 0
        {
            int signum = arg1;
            struct target_old_sigaction *tact = arg2, *toldact = arg3;
            ret = get_errno(setsid());
            

        }
        break;
#else
        goto unimplemented;
#endif
    case TARGET_NR_sgetmask:
        goto unimplemented;
    case TARGET_NR_ssetmask:
        goto unimplemented;
    case TARGET_NR_setreuid:
        ret = get_errno(setreuid(arg1, arg2));
        break;
    case TARGET_NR_setregid:
        ret = get_errno(setregid(arg1, arg2));
        break;
    case TARGET_NR_sigsuspend:
        goto unimplemented;
    case TARGET_NR_sigpending:
        goto unimplemented;
    case TARGET_NR_sethostname:
        ret = get_errno(sethostname((const char *)arg1, arg2));
        break;
    case TARGET_NR_setrlimit:
        goto unimplemented;
    case TARGET_NR_getrlimit:
        goto unimplemented;
    case TARGET_NR_getrusage:
        goto unimplemented;
    case TARGET_NR_gettimeofday:
        {
            struct target_timeval *target_tv = (void *)arg1;
            struct timeval tv;
            ret = get_errno(gettimeofday(&tv, NULL));
            if (!is_error(ret)) {
                target_tv->tv_sec = tswapl(tv.tv_sec);
                target_tv->tv_usec = tswapl(tv.tv_usec);
            }
        }
        break;
    case TARGET_NR_settimeofday:
        {
            struct target_timeval *target_tv = (void *)arg1;
            struct timeval tv;
            tv.tv_sec = tswapl(target_tv->tv_sec);
            tv.tv_usec = tswapl(target_tv->tv_usec);
            ret = get_errno(settimeofday(&tv, NULL));
        }
        break;
    case TARGET_NR_getgroups:
        goto unimplemented;
    case TARGET_NR_setgroups:
        goto unimplemented;
    case TARGET_NR_select:
        goto unimplemented;
    case TARGET_NR_symlink:
        ret = get_errno(symlink((const char *)arg1, (const char *)arg2));
        break;
    case TARGET_NR_oldlstat:
        goto unimplemented;
    case TARGET_NR_readlink:
        ret = get_errno(readlink((const char *)arg1, (char *)arg2, arg3));
        break;
    case TARGET_NR_uselib:
        goto unimplemented;
    case TARGET_NR_swapon:
        ret = get_errno(swapon((const char *)arg1, arg2));
        break;
    case TARGET_NR_reboot:
        goto unimplemented;
    case TARGET_NR_readdir:
        goto unimplemented;
#ifdef TARGET_I386
    case TARGET_NR_mmap:
        {
            uint32_t v1, v2, v3, v4, v5, v6, *vptr;
            vptr = (uint32_t *)arg1;
            v1 = tswap32(vptr[0]);
            v2 = tswap32(vptr[1]);
            v3 = tswap32(vptr[2]);
            v4 = tswap32(vptr[3]);
            v5 = tswap32(vptr[4]);
            v6 = tswap32(vptr[5]);
            ret = get_errno((long)mmap((void *)v1, v2, v3, v4, v5, v6));
        }
        break;
#endif
#ifdef TARGET_I386
    case TARGET_NR_mmap2:
#else
    case TARGET_NR_mmap:
#endif
        ret = get_errno((long)mmap((void *)arg1, arg2, arg3, arg4, arg5, arg6));
        break;
    case TARGET_NR_munmap:
        ret = get_errno(munmap((void *)arg1, arg2));
        break;
    case TARGET_NR_truncate:
        ret = get_errno(truncate((const char *)arg1, arg2));
        break;
    case TARGET_NR_ftruncate:
        ret = get_errno(ftruncate(arg1, arg2));
        break;
    case TARGET_NR_fchmod:
        ret = get_errno(fchmod(arg1, arg2));
        break;
    case TARGET_NR_fchown:
        ret = get_errno(fchown(arg1, arg2, arg3));
        break;
    case TARGET_NR_getpriority:
        ret = get_errno(getpriority(arg1, arg2));
        break;
    case TARGET_NR_setpriority:
        ret = get_errno(setpriority(arg1, arg2, arg3));
        break;
    case TARGET_NR_profil:
        goto unimplemented;
    case TARGET_NR_statfs:
        stfs = (void *)arg2;
        ret = get_errno(sys_statfs((const char *)arg1, stfs));
    convert_statfs:
        if (!is_error(ret)) {
            tswap32s(&stfs->f_type);
            tswap32s(&stfs->f_bsize);
            tswap32s(&stfs->f_blocks);
            tswap32s(&stfs->f_bfree);
            tswap32s(&stfs->f_bavail);
            tswap32s(&stfs->f_files);
            tswap32s(&stfs->f_ffree);
            tswap32s(&stfs->f_fsid.val[0]);
            tswap32s(&stfs->f_fsid.val[1]);
            tswap32s(&stfs->f_namelen);
        }
        break;
    case TARGET_NR_fstatfs:
        stfs = (void *)arg2;
        ret = get_errno(sys_fstatfs(arg1, stfs));
        goto convert_statfs;
    case TARGET_NR_ioperm:
        goto unimplemented;
    case TARGET_NR_socketcall:
        ret = do_socketcall(arg1, (long *)arg2);
        break;
    case TARGET_NR_syslog:
        goto unimplemented;
    case TARGET_NR_setitimer:
        goto unimplemented;
    case TARGET_NR_getitimer:
        goto unimplemented;
    case TARGET_NR_stat:
        ret = get_errno(stat((const char *)arg1, &st));
        goto do_stat;
    case TARGET_NR_lstat:
        ret = get_errno(lstat((const char *)arg1, &st));
        goto do_stat;
    case TARGET_NR_fstat:
        {
            ret = get_errno(fstat(arg1, &st));
        do_stat:
            if (!is_error(ret)) {
                struct target_stat *target_st = (void *)arg2;
                target_st->st_dev = tswap16(st.st_dev);
                target_st->st_ino = tswapl(st.st_ino);
                target_st->st_mode = tswap16(st.st_mode);
                target_st->st_nlink = tswap16(st.st_nlink);
                target_st->st_uid = tswap16(st.st_uid);
                target_st->st_gid = tswap16(st.st_gid);
                target_st->st_rdev = tswap16(st.st_rdev);
                target_st->st_size = tswapl(st.st_size);
                target_st->st_blksize = tswapl(st.st_blksize);
                target_st->st_blocks = tswapl(st.st_blocks);
                target_st->st_atime = tswapl(st.st_atime);
                target_st->st_mtime = tswapl(st.st_mtime);
                target_st->st_ctime = tswapl(st.st_ctime);
            }
        }
        break;
    case TARGET_NR_olduname:
        goto unimplemented;
    case TARGET_NR_iopl:
        goto unimplemented;
    case TARGET_NR_vhangup:
        ret = get_errno(vhangup());
        break;
    case TARGET_NR_idle:
        goto unimplemented;
    case TARGET_NR_vm86old:
        goto unimplemented;
    case TARGET_NR_wait4:
        {
            int status;
            target_long *status_ptr = (void *)arg2;
            struct rusage rusage, *rusage_ptr;
            struct target_rusage *target_rusage = (void *)arg4;
            if (target_rusage)
                rusage_ptr = &rusage;
            else
                rusage_ptr = NULL;
            ret = get_errno(wait4(arg1, &status, arg3, rusage_ptr));
            if (!is_error(ret)) {
                if (status_ptr)
                    *status_ptr = tswap32(status);
                if (target_rusage) {
                    target_rusage->ru_utime.tv_sec = tswapl(rusage.ru_utime.tv_sec);
                    target_rusage->ru_utime.tv_usec = tswapl(rusage.ru_utime.tv_usec);
                    target_rusage->ru_stime.tv_sec = tswapl(rusage.ru_stime.tv_sec);
                    target_rusage->ru_stime.tv_usec = tswapl(rusage.ru_stime.tv_usec);
                    target_rusage->ru_maxrss = tswapl(rusage.ru_maxrss);
                    target_rusage->ru_ixrss = tswapl(rusage.ru_ixrss);
                    target_rusage->ru_idrss = tswapl(rusage.ru_idrss);
                    target_rusage->ru_isrss = tswapl(rusage.ru_isrss);
                    target_rusage->ru_minflt = tswapl(rusage.ru_minflt);
                    target_rusage->ru_majflt = tswapl(rusage.ru_majflt);
                    target_rusage->ru_nswap = tswapl(rusage.ru_nswap);
                    target_rusage->ru_inblock = tswapl(rusage.ru_inblock);
                    target_rusage->ru_oublock = tswapl(rusage.ru_oublock);
                    target_rusage->ru_msgsnd = tswapl(rusage.ru_msgsnd);
                    target_rusage->ru_msgrcv = tswapl(rusage.ru_msgrcv);
                    target_rusage->ru_nsignals = tswapl(rusage.ru_nsignals);
                    target_rusage->ru_nvcsw = tswapl(rusage.ru_nvcsw);
                    target_rusage->ru_nivcsw = tswapl(rusage.ru_nivcsw);
                }
            }
        }
        break;
    case TARGET_NR_swapoff:
        ret = get_errno(swapoff((const char *)arg1));
        break;
    case TARGET_NR_sysinfo:
        goto unimplemented;
    case TARGET_NR_ipc:
        goto unimplemented;
    case TARGET_NR_fsync:
        ret = get_errno(fsync(arg1));
        break;
    case TARGET_NR_sigreturn:
        goto unimplemented;
    case TARGET_NR_clone:
        goto unimplemented;
    case TARGET_NR_setdomainname:
        ret = get_errno(setdomainname((const char *)arg1, arg2));
        break;
    case TARGET_NR_uname:
        /* no need to transcode because we use the linux syscall */
        ret = get_errno(sys_uname((struct new_utsname *)arg1));
        break;
    case TARGET_NR_modify_ldt:
        goto unimplemented;
    case TARGET_NR_adjtimex:
        goto unimplemented;
    case TARGET_NR_mprotect:
        ret = get_errno(mprotect((void *)arg1, arg2, arg3));
        break;
    case TARGET_NR_sigprocmask:
        {
            int how = arg1;
            sigset_t set, oldset, *set_ptr;
            target_ulong *pset = (void *)arg2, *poldset = (void *)arg3;
            
            switch(how) {
            case TARGET_SIG_BLOCK:
                how = SIG_BLOCK;
                break;
            case TARGET_SIG_UNBLOCK:
                how = SIG_UNBLOCK;
                break;
            case TARGET_SIG_SETMASK:
                how = SIG_SETMASK;
                break;
            default:
                ret = -EINVAL;
                goto fail;
            }
            
            if (pset) {
                target_to_host_old_sigset(&set, pset);
                set_ptr = &set;
            } else {
                set_ptr = NULL;
            }
            ret = get_errno(sigprocmask(arg1, set_ptr, &oldset));
            if (!is_error(ret) && poldset) {
                host_to_target_old_sigset(poldset, &oldset);
            }
        }
        break;
    case TARGET_NR_create_module:
    case TARGET_NR_init_module:
    case TARGET_NR_delete_module:
    case TARGET_NR_get_kernel_syms:
        goto unimplemented;
    case TARGET_NR_quotactl:
        goto unimplemented;
    case TARGET_NR_getpgid:
        ret = get_errno(getpgid(arg1));
        break;
    case TARGET_NR_fchdir:
        ret = get_errno(fchdir(arg1));
        break;
    case TARGET_NR_bdflush:
        goto unimplemented;
    case TARGET_NR_sysfs:
        goto unimplemented;
    case TARGET_NR_personality:
        ret = get_errno(mprotect((void *)arg1, arg2, arg3));
        break;
    case TARGET_NR_afs_syscall:
        goto unimplemented;
    case TARGET_NR_setfsuid:
        goto unimplemented;
    case TARGET_NR_setfsgid:
        goto unimplemented;
    case TARGET_NR__llseek:
        {
            int64_t res;
            ret = get_errno(_llseek(arg1, arg2, arg3, &res, arg5));
            *(int64_t *)arg4 = tswap64(res);
        }
        break;
    case TARGET_NR_getdents:
#if TARGET_LONG_SIZE != 4
#error not supported
#endif
        {
            struct dirent *dirp = (void *)arg2;
            long count = arg3;
            ret = get_errno(sys_getdents(arg1, dirp, count));
            if (!is_error(ret)) {
                struct dirent *de;
                int len = ret;
                int reclen;
                de = dirp;
                while (len > 0) {
                    reclen = tswap16(de->d_reclen);
                    if (reclen > len)
                        break;
                    de->d_reclen = reclen;
                    tswapls(&de->d_ino);
                    tswapls(&de->d_off);
                    de = (struct dirent *)((char *)de + reclen);
                    len -= reclen;
                }
            }
        }
        break;
    case TARGET_NR__newselect:
        ret = do_select(arg1, (void *)arg2, (void *)arg3, (void *)arg4, 
                        (void *)arg5);
        break;
    case TARGET_NR_flock:
        goto unimplemented;
    case TARGET_NR_msync:
        ret = get_errno(msync((void *)arg1, arg2, arg3));
        break;
    case TARGET_NR_readv:
        {
            int count = arg3;
            int i;
            struct iovec *vec;
            struct target_iovec *target_vec = (void *)arg2;

            vec = alloca(count * sizeof(struct iovec));
            for(i = 0;i < count; i++) {
                vec[i].iov_base = (void *)tswapl(target_vec[i].iov_base);
                vec[i].iov_len = tswapl(target_vec[i].iov_len);
            }
            ret = get_errno(readv(arg1, vec, count));
        }
        break;
    case TARGET_NR_writev:
        {
            int count = arg3;
            int i;
            struct iovec *vec;
            struct target_iovec *target_vec = (void *)arg2;

            vec = alloca(count * sizeof(struct iovec));
            for(i = 0;i < count; i++) {
                vec[i].iov_base = (void *)tswapl(target_vec[i].iov_base);
                vec[i].iov_len = tswapl(target_vec[i].iov_len);
            }
            ret = get_errno(writev(arg1, vec, count));
        }
        break;
    case TARGET_NR_getsid:
        ret = get_errno(getsid(arg1));
        break;
    case TARGET_NR_fdatasync:
        goto unimplemented;
    case TARGET_NR__sysctl:
        goto unimplemented;
    case TARGET_NR_mlock:
        ret = get_errno(mlock((void *)arg1, arg2));
        break;
    case TARGET_NR_munlock:
        ret = get_errno(munlock((void *)arg1, arg2));
        break;
    case TARGET_NR_mlockall:
        ret = get_errno(mlockall(arg1));
        break;
    case TARGET_NR_munlockall:
        ret = get_errno(munlockall());
        break;
    case TARGET_NR_sched_setparam:
        goto unimplemented;
    case TARGET_NR_sched_getparam:
        goto unimplemented;
    case TARGET_NR_sched_setscheduler:
        goto unimplemented;
    case TARGET_NR_sched_getscheduler:
        goto unimplemented;
    case TARGET_NR_sched_yield:
        ret = get_errno(sched_yield());
        break;
    case TARGET_NR_sched_get_priority_max:
    case TARGET_NR_sched_get_priority_min:
    case TARGET_NR_sched_rr_get_interval:
    case TARGET_NR_nanosleep:
    case TARGET_NR_mremap:
    case TARGET_NR_setresuid:
    case TARGET_NR_getresuid:
    case TARGET_NR_vm86:
    case TARGET_NR_query_module:
    case TARGET_NR_poll:
    case TARGET_NR_nfsservctl:
    case TARGET_NR_setresgid:
    case TARGET_NR_getresgid:
    case TARGET_NR_prctl:
    case TARGET_NR_rt_sigreturn:
    case TARGET_NR_rt_sigaction:
    case TARGET_NR_rt_sigprocmask:
    case TARGET_NR_rt_sigpending:
    case TARGET_NR_rt_sigtimedwait:
    case TARGET_NR_rt_sigqueueinfo:
    case TARGET_NR_rt_sigsuspend:
    case TARGET_NR_pread:
    case TARGET_NR_pwrite:
        goto unimplemented;
    case TARGET_NR_chown:
        ret = get_errno(chown((const char *)arg1, arg2, arg3));
        break;
    case TARGET_NR_getcwd:
        ret = get_errno(sys_getcwd1((char *)arg1, arg2));
        break;
    case TARGET_NR_capget:
    case TARGET_NR_capset:
    case TARGET_NR_sigaltstack:
    case TARGET_NR_sendfile:
    case TARGET_NR_getpmsg:
    case TARGET_NR_putpmsg:
    case TARGET_NR_vfork:
        ret = get_errno(vfork());
        break;
    case TARGET_NR_ugetrlimit:
    case TARGET_NR_truncate64:
    case TARGET_NR_ftruncate64:
    case TARGET_NR_stat64:
    case TARGET_NR_lstat64:
    case TARGET_NR_fstat64:
    case TARGET_NR_lchown32:
    case TARGET_NR_getuid32:
    case TARGET_NR_getgid32:
    case TARGET_NR_geteuid32:
    case TARGET_NR_getegid32:
    case TARGET_NR_setreuid32:
    case TARGET_NR_setregid32:
    case TARGET_NR_getgroups32:
    case TARGET_NR_setgroups32:
    case TARGET_NR_fchown32:
    case TARGET_NR_setresuid32:
    case TARGET_NR_getresuid32:
    case TARGET_NR_setresgid32:
    case TARGET_NR_getresgid32:
    case TARGET_NR_chown32:
    case TARGET_NR_setuid32:
    case TARGET_NR_setgid32:
    case TARGET_NR_setfsuid32:
    case TARGET_NR_setfsgid32:
    case TARGET_NR_pivot_root:
    case TARGET_NR_mincore:
    case TARGET_NR_madvise:
    case TARGET_NR_getdents64:
    case TARGET_NR_fcntl64:
    case TARGET_NR_security:
        goto unimplemented;
    case TARGET_NR_gettid:
        ret = get_errno(gettid());
        break;
    case TARGET_NR_readahead:
    case TARGET_NR_setxattr:
    case TARGET_NR_lsetxattr:
    case TARGET_NR_fsetxattr:
    case TARGET_NR_getxattr:
    case TARGET_NR_lgetxattr:
    case TARGET_NR_fgetxattr:
    case TARGET_NR_listxattr:
    case TARGET_NR_llistxattr:
    case TARGET_NR_flistxattr:
    case TARGET_NR_removexattr:
    case TARGET_NR_lremovexattr:
    case TARGET_NR_fremovexattr:
        goto unimplemented;
    default:
    unimplemented:
        gemu_log("Unsupported syscall: %d\n", num);
        ret = -ENOSYS;
        break;
    }
 fail:
    return ret;
}

