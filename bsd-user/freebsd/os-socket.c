/*
 * FreeBSD socket related system call helpers
 *
 * Copyright (c) 2013 Stacey D. Son
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "qemu.h"
#include "qemu-os.h"

abi_long t2h_freebsd_cmsg(struct msghdr *msgh,
        struct target_msghdr *target_msgh);
abi_long h2t_freebsd_cmsg(struct target_msghdr *target_msgh,
        struct msghdr *msgh);

abi_long t2h_freebsd_cmsg(struct msghdr *msgh,
        struct target_msghdr *target_msgh)
{
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(msgh);
    socklen_t msg_controllen;
    abi_ulong target_cmsg_addr;
    struct target_cmsghdr *target_cmsg, *target_cmsg_start;
    socklen_t space = 0;

    msg_controllen = tswap32(target_msgh->msg_controllen);
    if (msg_controllen < sizeof(struct target_cmsghdr)) {
        goto the_end;
    }
    target_cmsg_addr = tswapal(target_msgh->msg_control);
    target_cmsg = lock_user(VERIFY_READ, target_cmsg_addr, msg_controllen, 1);
    target_cmsg_start = target_cmsg;
    if (!target_cmsg) {
        return -TARGET_EFAULT;
    }

    while (cmsg && target_cmsg) {
        void *data = CMSG_DATA(cmsg);
        void *target_data = TARGET_CMSG_DATA(target_cmsg);
        int len = (unsigned char *)(target_cmsg) +
            tswap32(target_cmsg->cmsg_len) - (unsigned char *)target_data;

        space += CMSG_SPACE(len);
        if (space > msgh->msg_controllen) {
            space -= CMSG_SPACE(len);
            /*
             * This is a QEMU bug, since we allocated the payload area ourselves
             * (unlike overflow in host-to-target conversion, which is just the
             * guest giving us a buffer that's too small). It can't happen for
             * the payload types we currently support; if it becomes an issue in
             * future we would need to improve our allocation strategy to
             * something more intelligent than "twice the size of the target
             * buffer we're reading from".
             */
            gemu_log("Host cmsg overflow\n");
            break;
        }

        if (tswap32(target_cmsg->cmsg_level) == TARGET_SOL_SOCKET) {
            cmsg->cmsg_level = SOL_SOCKET;
        } else {
            cmsg->cmsg_level = tswap32(target_cmsg->cmsg_level);
        }
        cmsg->cmsg_type = tswap32(target_cmsg->cmsg_type);
        cmsg->cmsg_len = CMSG_LEN(len);

        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
            int *fd = (int *)data;
            int *target_fd = (int *)target_data;
            int i, numfds = len / sizeof(int);

            for (i = 0; i < numfds; i++) {
                __get_user(fd[i], target_fd + i);
            }
        } else if ((cmsg->cmsg_level == SOL_SOCKET) &&
            (cmsg->cmsg_type == SCM_TIMESTAMP) &&
            (len == sizeof(struct target_freebsd_timeval)))  {
            /* copy struct timeval to host */
            struct timeval *tv = (struct timeval *)data;
            struct target_freebsd_timeval *target_tv =
                (struct target_freebsd_timeval *)target_data;
            __get_user(tv->tv_sec, &target_tv->tv_sec);
            __get_user(tv->tv_usec, &target_tv->tv_usec);
        } else {
            gemu_log("Unsupported target ancillary data: %d/%d\n",
                     cmsg->cmsg_level, cmsg->cmsg_type);
            memcpy(data, target_data, len);
        }

        cmsg = CMSG_NXTHDR(msgh, cmsg);
        target_cmsg = TARGET_CMSG_NXTHDR(target_msgh, target_cmsg,
                                         target_cmsg_start);
    }
    unlock_user(target_cmsg_start, target_cmsg_addr, 0);
the_end:
    msgh->msg_controllen = space;
    return 0;
}

abi_long h2t_freebsd_cmsg(struct target_msghdr *target_msgh,
        struct msghdr *msgh)
{
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(msgh);
    socklen_t msg_controllen;
    abi_ulong target_cmsg_addr;
    struct target_cmsghdr *target_cmsg, *target_cmsg_start;
    socklen_t space = 0;

    msg_controllen = tswap32(target_msgh->msg_controllen);
    if (msg_controllen < sizeof(struct target_cmsghdr)) {
        goto the_end;
    }
    target_cmsg_addr = tswapal(target_msgh->msg_control);
    target_cmsg = lock_user(VERIFY_WRITE, target_cmsg_addr, msg_controllen, 0);
    target_cmsg_start = target_cmsg;
    if (!target_cmsg) {
        return -TARGET_EFAULT;
    }

    while (cmsg && target_cmsg) {
        void *data = CMSG_DATA(cmsg);
        void *target_data = TARGET_CMSG_DATA(target_cmsg);
        int len = (unsigned char *)(cmsg) + cmsg->cmsg_len -
            (unsigned char *)data;

        int tgt_len, tgt_space;

        /*
         * We never copy a half-header but may copy half-data; this is Linux's
         * behaviour in put_cmsg(). Note that truncation here is a guest problem
         * (which we report to the guest via the CTRUNC bit), unlike truncation
         * in target_to_host_cmsg, which is a QEMU bug.
         */
        if (msg_controllen < sizeof(struct target_cmsghdr)) {
            target_msgh->msg_flags |= tswap32(MSG_CTRUNC);
            break;
        }

        if (cmsg->cmsg_level == SOL_SOCKET) {
            target_cmsg->cmsg_level = tswap32(TARGET_SOL_SOCKET);
        } else {
            target_cmsg->cmsg_level = tswap32(cmsg->cmsg_level);
        }
        target_cmsg->cmsg_type = tswap32(cmsg->cmsg_type);

        /*
         * Payload types which need a different size of payload on the target
         * must adjust tgt_len here.
         */
        tgt_len = len;
        switch (cmsg->cmsg_level) {
        case SOL_SOCKET:
            switch (cmsg->cmsg_type) {
            case SCM_TIMESTAMP:
                tgt_len = sizeof(struct target_freebsd_timeval);
                break;
            default:
                break;
            }
            break;
        default:
            break;
        }

        if (msg_controllen < TARGET_CMSG_LEN(tgt_len)) {
            target_msgh->msg_flags |= tswap32(MSG_CTRUNC);
            tgt_len = msg_controllen - sizeof(struct target_cmsghdr);
        }

        /*
         * We must now copy-and-convert len bytes of payload into tgt_len bytes
         * of destination space. Bear in mind that in both source and
         * destination we may be dealing with a truncated value!
         */
        switch (cmsg->cmsg_level) {
        case SOL_SOCKET:
            switch (cmsg->cmsg_type) {
            case SCM_RIGHTS:
            {
                int *fd = (int *)data;
                int *target_fd = (int *)target_data;
                int i, numfds = tgt_len / sizeof(int);

                for (i = 0; i < numfds; i++) {
                    __put_user(fd[i], target_fd + i);
                }
                break;
            }
            case SCM_TIMESTAMP:
            {
                struct timeval *tv = (struct timeval *)data;
                struct target_freebsd_timeval *target_tv =
                    (struct target_freebsd_timeval *)target_data;

                if (len != sizeof(struct timeval) ||
                    tgt_len != sizeof(struct target_freebsd_timeval)) {
                    goto unimplemented;
                }

                /* copy struct timeval to target */
                __put_user(tv->tv_sec, &target_tv->tv_sec);
                __put_user(tv->tv_usec, &target_tv->tv_usec);
                break;
            }
            default:
                goto unimplemented;
            }
            break;
        default:
        unimplemented:
            gemu_log("Unsupported host ancillary data: %d/%d\n",
                     cmsg->cmsg_level, cmsg->cmsg_type);
            memcpy(target_data, data, MIN(len, tgt_len));
            if (tgt_len > len) {
                memset(target_data + len, 0, tgt_len - len);
            }
        }

        target_cmsg->cmsg_len = tswap32(TARGET_CMSG_LEN(tgt_len));
        tgt_space = TARGET_CMSG_SPACE(tgt_len);
        if (msg_controllen < tgt_space) {
            tgt_space = msg_controllen;
        }
        msg_controllen -= tgt_space;
        space += tgt_space;
        cmsg = CMSG_NXTHDR(msgh, cmsg);
        target_cmsg = TARGET_CMSG_NXTHDR(target_msgh, target_cmsg,
                                         target_cmsg_start);
    }
    unlock_user(target_cmsg_start, target_cmsg_addr, space);
the_end:
    target_msgh->msg_controllen = tswap32(space);
    return 0;
}
