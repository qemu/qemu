/*
 * Copyright (c) 2011, Max Filippov, Open Source and Linux Lab.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Open Source and Linux Lab nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "chardev/char-fe.h"
#include "exec/helper-proto.h"
#include "hw/semihosting/semihost.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "sysemu/sysemu.h"

enum {
    TARGET_SYS_exit = 1,
    TARGET_SYS_read = 3,
    TARGET_SYS_write = 4,
    TARGET_SYS_open = 5,
    TARGET_SYS_close = 6,
    TARGET_SYS_lseek = 19,
    TARGET_SYS_select_one = 29,

    TARGET_SYS_argc = 1000,
    TARGET_SYS_argv_sz = 1001,
    TARGET_SYS_argv = 1002,
    TARGET_SYS_memset = 1004,
};

enum {
    SELECT_ONE_READ   = 1,
    SELECT_ONE_WRITE  = 2,
    SELECT_ONE_EXCEPT = 3,
};

enum {
    TARGET_EPERM        =  1,
    TARGET_ENOENT       =  2,
    TARGET_ESRCH        =  3,
    TARGET_EINTR        =  4,
    TARGET_EIO          =  5,
    TARGET_ENXIO        =  6,
    TARGET_E2BIG        =  7,
    TARGET_ENOEXEC      =  8,
    TARGET_EBADF        =  9,
    TARGET_ECHILD       = 10,
    TARGET_EAGAIN       = 11,
    TARGET_ENOMEM       = 12,
    TARGET_EACCES       = 13,
    TARGET_EFAULT       = 14,
    TARGET_ENOTBLK      = 15,
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
    TARGET_EDOM         = 33,
    TARGET_ERANGE       = 34,
    TARGET_ENOSYS       = 88,
    TARGET_ELOOP        = 92,
};

static uint32_t errno_h2g(int host_errno)
{
    static const uint32_t guest_errno[] = {
        [EPERM]         = TARGET_EPERM,
        [ENOENT]        = TARGET_ENOENT,
        [ESRCH]         = TARGET_ESRCH,
        [EINTR]         = TARGET_EINTR,
        [EIO]           = TARGET_EIO,
        [ENXIO]         = TARGET_ENXIO,
        [E2BIG]         = TARGET_E2BIG,
        [ENOEXEC]       = TARGET_ENOEXEC,
        [EBADF]         = TARGET_EBADF,
        [ECHILD]        = TARGET_ECHILD,
        [EAGAIN]        = TARGET_EAGAIN,
        [ENOMEM]        = TARGET_ENOMEM,
        [EACCES]        = TARGET_EACCES,
        [EFAULT]        = TARGET_EFAULT,
#ifdef ENOTBLK
        [ENOTBLK]       = TARGET_ENOTBLK,
#endif
        [EBUSY]         = TARGET_EBUSY,
        [EEXIST]        = TARGET_EEXIST,
        [EXDEV]         = TARGET_EXDEV,
        [ENODEV]        = TARGET_ENODEV,
        [ENOTDIR]       = TARGET_ENOTDIR,
        [EISDIR]        = TARGET_EISDIR,
        [EINVAL]        = TARGET_EINVAL,
        [ENFILE]        = TARGET_ENFILE,
        [EMFILE]        = TARGET_EMFILE,
        [ENOTTY]        = TARGET_ENOTTY,
#ifdef ETXTBSY
        [ETXTBSY]       = TARGET_ETXTBSY,
#endif
        [EFBIG]         = TARGET_EFBIG,
        [ENOSPC]        = TARGET_ENOSPC,
        [ESPIPE]        = TARGET_ESPIPE,
        [EROFS]         = TARGET_EROFS,
        [EMLINK]        = TARGET_EMLINK,
        [EPIPE]         = TARGET_EPIPE,
        [EDOM]          = TARGET_EDOM,
        [ERANGE]        = TARGET_ERANGE,
        [ENOSYS]        = TARGET_ENOSYS,
#ifdef ELOOP
        [ELOOP]         = TARGET_ELOOP,
#endif
    };

    if (host_errno == 0) {
        return 0;
    } else if (host_errno > 0 && host_errno < ARRAY_SIZE(guest_errno) &&
            guest_errno[host_errno]) {
        return guest_errno[host_errno];
    } else {
        return TARGET_EINVAL;
    }
}

typedef struct XtensaSimConsole {
    CharBackend be;
    struct {
        char buffer[16];
        size_t offset;
    } input;
} XtensaSimConsole;

static XtensaSimConsole *sim_console;

static IOCanReadHandler sim_console_can_read;
static int sim_console_can_read(void *opaque)
{
    XtensaSimConsole *p = opaque;

    return sizeof(p->input.buffer) - p->input.offset;
}

static IOReadHandler sim_console_read;
static void sim_console_read(void *opaque, const uint8_t *buf, int size)
{
    XtensaSimConsole *p = opaque;
    size_t copy = sizeof(p->input.buffer) - p->input.offset;

    if (size < copy) {
        copy = size;
    }
    memcpy(p->input.buffer + p->input.offset, buf, copy);
    p->input.offset += copy;
}

void xtensa_sim_open_console(Chardev *chr)
{
    static XtensaSimConsole console;

    qemu_chr_fe_init(&console.be, chr, &error_abort);
    qemu_chr_fe_set_handlers(&console.be,
                             sim_console_can_read,
                             sim_console_read,
                             NULL, NULL, &console,
                             NULL, true);
    sim_console = &console;
}

void HELPER(simcall)(CPUXtensaState *env)
{
    CPUState *cs = env_cpu(env);
    uint32_t *regs = env->regs;

    switch (regs[2]) {
    case TARGET_SYS_exit:
        exit(regs[3]);
        break;

    case TARGET_SYS_read:
    case TARGET_SYS_write:
        {
            bool is_write = regs[2] == TARGET_SYS_write;
            uint32_t fd = regs[3];
            uint32_t vaddr = regs[4];
            uint32_t len = regs[5];
            uint32_t len_done = 0;

            while (len > 0) {
                hwaddr paddr = cpu_get_phys_page_debug(cs, vaddr);
                uint32_t page_left =
                    TARGET_PAGE_SIZE - (vaddr & (TARGET_PAGE_SIZE - 1));
                uint32_t io_sz = page_left < len ? page_left : len;
                hwaddr sz = io_sz;
                void *buf = cpu_physical_memory_map(paddr, &sz, !is_write);
                uint32_t io_done;
                bool error = false;

                if (buf) {
                    vaddr += io_sz;
                    len -= io_sz;
                    if (fd < 3 && sim_console) {
                        if (is_write && (fd == 1 || fd == 2)) {
                            io_done = qemu_chr_fe_write_all(&sim_console->be,
                                                            buf, io_sz);
                            regs[3] = errno_h2g(errno);
                        } else if (!is_write && fd == 0) {
                            if (sim_console->input.offset) {
                                io_done = sim_console->input.offset;
                                if (io_sz < io_done) {
                                    io_done = io_sz;
                                }
                                memcpy(buf, sim_console->input.buffer, io_done);
                                memmove(sim_console->input.buffer,
                                        sim_console->input.buffer + io_done,
                                        sim_console->input.offset - io_done);
                                sim_console->input.offset -= io_done;
                                qemu_chr_fe_accept_input(&sim_console->be);
                            } else {
                                io_done = -1;
                                regs[3] = TARGET_EAGAIN;
                            }
                        } else {
                            qemu_log_mask(LOG_GUEST_ERROR,
                                          "%s fd %d is not supported with chardev console\n",
                                          is_write ?
                                          "writing to" : "reading from", fd);
                            io_done = -1;
                            regs[3] = TARGET_EBADF;
                        }
                    } else {
                        io_done = is_write ?
                            write(fd, buf, io_sz) :
                            read(fd, buf, io_sz);
                        regs[3] = errno_h2g(errno);
                    }
                    if (io_done == -1) {
                        error = true;
                        io_done = 0;
                    }
                    cpu_physical_memory_unmap(buf, sz, !is_write, io_done);
                } else {
                    error = true;
                    regs[3] = TARGET_EINVAL;
                    break;
                }
                if (error) {
                    if (!len_done) {
                        len_done = -1;
                    }
                    break;
                }
                len_done += io_done;
                if (io_done < io_sz) {
                    break;
                }
            }
            regs[2] = len_done;
        }
        break;

    case TARGET_SYS_open:
        {
            char name[1024];
            int rc;
            int i;

            for (i = 0; i < ARRAY_SIZE(name); ++i) {
                rc = cpu_memory_rw_debug(cs, regs[3] + i,
                                         (uint8_t *)name + i, 1, 0);
                if (rc != 0 || name[i] == 0) {
                    break;
                }
            }

            if (rc == 0 && i < ARRAY_SIZE(name)) {
                regs[2] = open(name, regs[4], regs[5]);
                regs[3] = errno_h2g(errno);
            } else {
                regs[2] = -1;
                regs[3] = TARGET_EINVAL;
            }
        }
        break;

    case TARGET_SYS_close:
        if (regs[3] < 3) {
            regs[2] = regs[3] = 0;
        } else {
            regs[2] = close(regs[3]);
            regs[3] = errno_h2g(errno);
        }
        break;

    case TARGET_SYS_lseek:
        regs[2] = lseek(regs[3], (off_t)(int32_t)regs[4], regs[5]);
        regs[3] = errno_h2g(errno);
        break;

    case TARGET_SYS_select_one:
        {
            uint32_t fd = regs[3];
            uint32_t rq = regs[4];
            uint32_t target_tv = regs[5];
            uint32_t target_tvv[2];

            struct timeval tv = {0};

            if (target_tv) {
                cpu_memory_rw_debug(cs, target_tv,
                        (uint8_t *)target_tvv, sizeof(target_tvv), 0);
                tv.tv_sec = (int32_t)tswap32(target_tvv[0]);
                tv.tv_usec = (int32_t)tswap32(target_tvv[1]);
            }
            if (fd < 3 && sim_console) {
                if ((fd == 1 || fd == 2) && rq == SELECT_ONE_WRITE) {
                    regs[2] = 1;
                } else if (fd == 0 && rq == SELECT_ONE_READ) {
                    regs[2] = sim_console->input.offset > 0;
                } else {
                    regs[2] = 0;
                }
                regs[3] = 0;
            } else {
                fd_set fdset;

                FD_ZERO(&fdset);
                FD_SET(fd, &fdset);
                regs[2] = select(fd + 1,
                                 rq == SELECT_ONE_READ   ? &fdset : NULL,
                                 rq == SELECT_ONE_WRITE  ? &fdset : NULL,
                                 rq == SELECT_ONE_EXCEPT ? &fdset : NULL,
                                 target_tv ? &tv : NULL);
                regs[3] = errno_h2g(errno);
            }
        }
        break;

    case TARGET_SYS_argc:
        regs[2] = semihosting_get_argc();
        regs[3] = 0;
        break;

    case TARGET_SYS_argv_sz:
        {
            int argc = semihosting_get_argc();
            int sz = (argc + 1) * sizeof(uint32_t);
            int i;

            for (i = 0; i < argc; ++i) {
                sz += 1 + strlen(semihosting_get_arg(i));
            }
            regs[2] = sz;
            regs[3] = 0;
        }
        break;

    case TARGET_SYS_argv:
        {
            int argc = semihosting_get_argc();
            int str_offset = (argc + 1) * sizeof(uint32_t);
            int i;
            uint32_t argptr;

            for (i = 0; i < argc; ++i) {
                const char *str = semihosting_get_arg(i);
                int str_size = strlen(str) + 1;

                argptr = tswap32(regs[3] + str_offset);

                cpu_memory_rw_debug(cs,
                                    regs[3] + i * sizeof(uint32_t),
                                    (uint8_t *)&argptr, sizeof(argptr), 1);
                cpu_memory_rw_debug(cs,
                                    regs[3] + str_offset,
                                    (uint8_t *)str, str_size, 1);
                str_offset += str_size;
            }
            argptr = 0;
            cpu_memory_rw_debug(cs,
                                regs[3] + i * sizeof(uint32_t),
                                (uint8_t *)&argptr, sizeof(argptr), 1);
            regs[3] = 0;
        }
        break;

    case TARGET_SYS_memset:
        {
            uint32_t base = regs[3];
            uint32_t sz = regs[5];

            while (sz) {
                hwaddr len = sz;
                void *buf = cpu_physical_memory_map(base, &len, 1);

                if (buf && len) {
                    memset(buf, regs[4], len);
                    cpu_physical_memory_unmap(buf, len, 1, len);
                } else {
                    len = 1;
                }
                base += len;
                sz -= len;
            }
            regs[2] = regs[3];
            regs[3] = 0;
        }
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s(%d): not implemented\n", __func__, regs[2]);
        regs[2] = -1;
        regs[3] = TARGET_ENOSYS;
        break;
    }
}
