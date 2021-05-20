/*
 *  System call tracing and debugging
 *
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
 */

#include "qemu/osdep.h"
#include <sys/select.h>
#include <sys/syscall.h>
#include <sys/ioccom.h>
#include <ctype.h>

#include "qemu.h"

#include "os-strace.h"  /* OS dependent strace print functions */

int do_strace;

/*
 * Utility functions
 */

static void print_sysctl(const struct syscallname *name, abi_long arg1,
        abi_long arg2, abi_long arg3, abi_long arg4, abi_long arg5,
        abi_long arg6)
{
    uint32_t i;
    int32_t *namep;

    gemu_log("%s({ ", name->name);
    namep = lock_user(VERIFY_READ, arg1, sizeof(int32_t) * arg2, 1);
    if (namep) {
        int32_t *p = namep;

        for (i = 0; i < (uint32_t)arg2; i++) {
            gemu_log("%d ", tswap32(*p++));
        }
        unlock_user(namep, arg1, 0);
    }
    gemu_log("}, %u, 0x" TARGET_ABI_FMT_lx ", 0x" TARGET_ABI_FMT_lx ", 0x"
        TARGET_ABI_FMT_lx ", 0x" TARGET_ABI_FMT_lx ")",
        (uint32_t)arg2, arg3, arg4, arg5, arg6);
}

static void print_execve(const struct syscallname *name, abi_long arg1,
        abi_long arg2, abi_long arg3, abi_long arg4, abi_long arg5,
        abi_long arg6)
{
    abi_ulong arg_ptr_addr;
    char *s;

    s = lock_user_string(arg1);
    if (s == NULL) {
        return;
    }
    gemu_log("%s(\"%s\",{", name->name, s);
    unlock_user(s, arg1, 0);

    for (arg_ptr_addr = arg2; ; arg_ptr_addr += sizeof(abi_ulong)) {
        abi_ulong *arg_ptr, arg_addr;

        arg_ptr = lock_user(VERIFY_READ, arg_ptr_addr, sizeof(abi_ulong), 1);
        if (!arg_ptr) {
            return;
        }
        arg_addr = tswapl(*arg_ptr);
        unlock_user(arg_ptr, arg_ptr_addr, 0);
        if (!arg_addr) {
            break;
        }
        if ((s = lock_user_string(arg_addr))) {
            gemu_log("\"%s\",", s);
            unlock_user(s, arg_addr, 0);
        }
    }
    gemu_log("NULL})");
}

static void print_ioctl(const struct syscallname *name,
        abi_long arg1, abi_long arg2, abi_long arg3, abi_long arg4,
        abi_long arg5, abi_long arg6)
{
    /* Decode the ioctl request */
    gemu_log("%s(%d, 0x%0lx { IO%s%s GRP:0x%x('%c') CMD:%d LEN:%d }, 0x"
            TARGET_ABI_FMT_lx ", ...)",
            name->name,
            (int)arg1,
            (unsigned long)arg2,
            arg2 & IOC_OUT ? "R" : "",
            arg2 & IOC_IN ? "W" : "",
            (unsigned)IOCGROUP(arg2),
            isprint(IOCGROUP(arg2)) ? (char)IOCGROUP(arg2) : '?',
            (int)arg2 & 0xFF,
            (int)IOCPARM_LEN(arg2),
            arg3);
}

static void print_sysarch(const struct syscallname *name, abi_long arg1,
        abi_long arg2, abi_long arg3, abi_long arg4, abi_long arg5,
        abi_long arg6)
{
    /* This is os dependent. */
    do_os_print_sysarch(name, arg1, arg2, arg3, arg4, arg5, arg6);
}

/*
 * Variants for the return value output function
 */

static void print_syscall_ret_addr(const struct syscallname *name, abi_long ret)
{
    if (ret == -1) {
        gemu_log(" = -1 errno=%d (%s)\n", errno, strerror(errno));
    } else {
        gemu_log(" = 0x" TARGET_ABI_FMT_lx "\n", ret);
    }
}

/*
 * An array of all of the syscalls we know about
 */

static const struct syscallname freebsd_scnames[] = {
#include "freebsd/strace.list"
};
static const struct syscallname netbsd_scnames[] = {
#include "netbsd/strace.list"
};
static const struct syscallname openbsd_scnames[] = {
#include "openbsd/strace.list"
};

static void print_syscall(int num, const struct syscallname *scnames,
        unsigned int nscnames, abi_long arg1, abi_long arg2, abi_long arg3,
        abi_long arg4, abi_long arg5, abi_long arg6)
{
    unsigned int i;
    const char *format="%s(" TARGET_ABI_FMT_ld "," TARGET_ABI_FMT_ld ","
        TARGET_ABI_FMT_ld "," TARGET_ABI_FMT_ld "," TARGET_ABI_FMT_ld ","
        TARGET_ABI_FMT_ld ")";

    gemu_log("%d ", getpid() );

    for (i = 0; i < nscnames; i++) {
        if (scnames[i].nr == num) {
            if (scnames[i].call != NULL) {
                scnames[i].call(&scnames[i], arg1, arg2, arg3, arg4, arg5,
                        arg6);
            } else {
                /* XXX: this format system is broken because it uses
                   host types and host pointers for strings */
                if (scnames[i].format != NULL) {
                    format = scnames[i].format;
                }
                gemu_log(format, scnames[i].name, arg1, arg2, arg3, arg4, arg5,
                        arg6);
            }
            return;
        }
    }
    gemu_log("Unknown syscall %d\n", num);
}

static void print_syscall_ret(int num, abi_long ret,
        const struct syscallname *scnames, unsigned int nscnames)
{
    unsigned int i;

    for (i = 0; i < nscnames; i++) {
        if (scnames[i].nr == num) {
            if (scnames[i].result != NULL) {
                scnames[i].result(&scnames[i], ret);
            } else {
                if (ret < 0) {
                    gemu_log(" = -1 errno=" TARGET_ABI_FMT_ld " (%s)\n", -ret,
                             strerror(-ret));
                } else {
                    gemu_log(" = " TARGET_ABI_FMT_ld "\n", ret);
                }
            }
            break;
        }
    }
}

/*
 * The public interface to this module.
 */
void print_freebsd_syscall(int num, abi_long arg1, abi_long arg2, abi_long arg3,
        abi_long arg4, abi_long arg5, abi_long arg6)
{

    print_syscall(num, freebsd_scnames, ARRAY_SIZE(freebsd_scnames), arg1, arg2,
            arg3, arg4, arg5, arg6);
}

void print_freebsd_syscall_ret(int num, abi_long ret)
{

    print_syscall_ret(num, ret, freebsd_scnames, ARRAY_SIZE(freebsd_scnames));
}

void print_netbsd_syscall(int num, abi_long arg1, abi_long arg2, abi_long arg3,
        abi_long arg4, abi_long arg5, abi_long arg6)
{

    print_syscall(num, netbsd_scnames, ARRAY_SIZE(netbsd_scnames),
                  arg1, arg2, arg3, arg4, arg5, arg6);
}

void print_netbsd_syscall_ret(int num, abi_long ret)
{

    print_syscall_ret(num, ret, netbsd_scnames, ARRAY_SIZE(netbsd_scnames));
}

void print_openbsd_syscall(int num, abi_long arg1, abi_long arg2, abi_long arg3,
        abi_long arg4, abi_long arg5, abi_long arg6)
{

    print_syscall(num, openbsd_scnames, ARRAY_SIZE(openbsd_scnames), arg1, arg2,
            arg3, arg4, arg5, arg6);
}

void print_openbsd_syscall_ret(int num, abi_long ret)
{

    print_syscall_ret(num, ret, openbsd_scnames, ARRAY_SIZE(openbsd_scnames));
}
