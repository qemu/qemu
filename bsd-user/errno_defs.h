/*
 * Copyright (c) 1982, 1986, 1989, 1993
 *      The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *      @(#)errno.h     8.5 (Berkeley) 1/21/94
 */

#ifndef _ERRNO_DEFS_H_
#define _ERRNO_DEFS_H_

#define TARGET_EPERM            1               /* Operation not permitted */
#define TARGET_ENOENT           2               /* No such file or directory */
#define TARGET_ESRCH            3               /* No such process */
#define TARGET_EINTR            4               /* Interrupted system call */
#define TARGET_EIO              5               /* Input/output error */
#define TARGET_ENXIO            6               /* Device not configured */
#define TARGET_E2BIG            7               /* Argument list too long */
#define TARGET_ENOEXEC          8               /* Exec format error */
#define TARGET_EBADF            9               /* Bad file descriptor */
#define TARGET_ECHILD           10              /* No child processes */
#define TARGET_EDEADLK          11              /* Resource deadlock avoided */
                                        /* 11 was EAGAIN */
#define TARGET_ENOMEM           12              /* Cannot allocate memory */
#define TARGET_EACCES           13              /* Permission denied */
#define TARGET_EFAULT           14              /* Bad address */
#define TARGET_ENOTBLK          15              /* Block device required */
#define TARGET_EBUSY            16              /* Device busy */
#define TARGET_EEXIST           17              /* File exists */
#define TARGET_EXDEV            18              /* Cross-device link */
#define TARGET_ENODEV           19              /* Operation not supported by device */
#define TARGET_ENOTDIR          20              /* Not a directory */
#define TARGET_EISDIR           21              /* Is a directory */
#define TARGET_EINVAL           22              /* Invalid argument */
#define TARGET_ENFILE           23              /* Too many open files in system */
#define TARGET_EMFILE           24              /* Too many open files */
#define TARGET_ENOTTY           25              /* Inappropriate ioctl for device */
#define TARGET_ETXTBSY          26              /* Text file busy */
#define TARGET_EFBIG            27              /* File too large */
#define TARGET_ENOSPC           28              /* No space left on device */
#define TARGET_ESPIPE           29              /* Illegal seek */
#define TARGET_EROFS            30              /* Read-only file system */
#define TARGET_EMLINK           31              /* Too many links */
#define TARGET_EPIPE            32              /* Broken pipe */

/* math software */
#define TARGET_EDOM             33              /* Numerical argument out of domain */
#define TARGET_ERANGE           34              /* Result too large */

/* non-blocking and interrupt i/o */
#define TARGET_EAGAIN           35              /* Resource temporarily unavailable */
#define TARGET_EWOULDBLOCK      EAGAIN          /* Operation would block */
#define TARGET_EINPROGRESS      36              /* Operation now in progress */
#define TARGET_EALREADY 37              /* Operation already in progress */

/* ipc/network software -- argument errors */
#define TARGET_ENOTSOCK 38              /* Socket operation on non-socket */
#define TARGET_EDESTADDRREQ     39              /* Destination address required */
#define TARGET_EMSGSIZE 40              /* Message too long */
#define TARGET_EPROTOTYPE       41              /* Protocol wrong type for socket */
#define TARGET_ENOPROTOOPT      42              /* Protocol not available */
#define TARGET_EPROTONOSUPPORT  43              /* Protocol not supported */
#define TARGET_ESOCKTNOSUPPORT  44              /* Socket type not supported */
#define TARGET_EOPNOTSUPP       45              /* Operation not supported */
#define TARGET_EPFNOSUPPORT     46              /* Protocol family not supported */
#define TARGET_EAFNOSUPPORT     47              /* Address family not supported by protocol family */
#define TARGET_EADDRINUSE       48              /* Address already in use */
#define TARGET_EADDRNOTAVAIL    49              /* Can't assign requested address */

/* ipc/network software -- operational errors */
#define TARGET_ENETDOWN 50              /* Network is down */
#define TARGET_ENETUNREACH      51              /* Network is unreachable */
#define TARGET_ENETRESET        52              /* Network dropped connection on reset */
#define TARGET_ECONNABORTED     53              /* Software caused connection abort */
#define TARGET_ECONNRESET       54              /* Connection reset by peer */
#define TARGET_ENOBUFS          55              /* No buffer space available */
#define TARGET_EISCONN          56              /* Socket is already connected */
#define TARGET_ENOTCONN 57              /* Socket is not connected */
#define TARGET_ESHUTDOWN        58              /* Can't send after socket shutdown */
#define TARGET_ETOOMANYREFS     59              /* Too many references: can't splice */
#define TARGET_ETIMEDOUT        60              /* Operation timed out */
#define TARGET_ECONNREFUSED     61              /* Connection refused */

#define TARGET_ELOOP            62              /* Too many levels of symbolic links */
#define TARGET_ENAMETOOLONG     63              /* File name too long */

/* should be rearranged */
#define TARGET_EHOSTDOWN        64              /* Host is down */
#define TARGET_EHOSTUNREACH     65              /* No route to host */
#define TARGET_ENOTEMPTY        66              /* Directory not empty */

/* quotas & mush */
#define TARGET_EPROCLIM 67              /* Too many processes */
#define TARGET_EUSERS           68              /* Too many users */
#define TARGET_EDQUOT           69              /* Disk quota exceeded */

/* Network File System */
#define TARGET_ESTALE           70              /* Stale NFS file handle */
#define TARGET_EREMOTE          71              /* Too many levels of remote in path */
#define TARGET_EBADRPC          72              /* RPC struct is bad */
#define TARGET_ERPCMISMATCH     73              /* RPC version wrong */
#define TARGET_EPROGUNAVAIL     74              /* RPC prog. not avail */
#define TARGET_EPROGMISMATCH    75              /* Program version wrong */
#define TARGET_EPROCUNAVAIL     76              /* Bad procedure for program */

#define TARGET_ENOLCK           77              /* No locks available */
#define TARGET_ENOSYS           78              /* Function not implemented */

#define TARGET_EFTYPE           79              /* Inappropriate file type or format */
#define TARGET_EAUTH            80              /* Authentication error */
#define TARGET_ENEEDAUTH        81              /* Need authenticator */
#define TARGET_EIPSEC           82              /* IPsec processing failure */
#define TARGET_ENOATTR          83              /* Attribute not found */
#define TARGET_EILSEQ           84              /* Illegal byte sequence */
#define TARGET_ENOMEDIUM        85              /* No medium found */
#define TARGET_EMEDIUMTYPE      86              /* Wrong Medium Type */
#define TARGET_EOVERFLOW        87              /* Conversion overflow */
#define TARGET_ECANCELED        88              /* Operation canceled */
#define TARGET_EIDRM            89              /* Identifier removed */
#define TARGET_ENOMSG           90              /* No message of desired type */
#define TARGET_ELAST            90              /* Must be equal largest errno */

/* Internal errors: */
#define TARGET_EJUSTRETURN      254             /* Just return without modifing regs */
#define TARGET_ERESTART         255             /* Restart syscall */

#include "special-errno.h"

_Static_assert(TARGET_ERESTART == QEMU_ERESTARTSYS,
               "TARGET_ERESTART and QEMU_ERESTARTSYS expected to match");

#endif /* !  _ERRNO_DEFS_H_ */
