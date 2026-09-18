/*
 * BSD socket system call related helpers
 *
 * Copyright (c) 2013 Stacey D. Son
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>

#include "qemu.h"
#include "qemu-bsd.h"

/*
 * socket conversion
 */
abi_long target_to_host_sockaddr(struct sockaddr *addr, abi_ulong target_addr,
                                 socklen_t len)
{
    const socklen_t unix_maxlen = sizeof(struct sockaddr_un);
    sa_family_t sa_family;
    struct target_sockaddr *target_saddr;

    target_saddr = lock_user(VERIFY_READ, target_addr, len, 1);
    if (target_saddr == 0) {
        return -TARGET_EFAULT;
    }

    sa_family = target_saddr->sa_family;

    /*
     * Oops. The caller might send a incomplete sun_path; sun_path
     * must be terminated by \0 (see the manual page), but unfortunately
     * it is quite common to specify sockaddr_un length as
     * "strlen(x->sun_path)" while it should be "strlen(...) + 1". We will
     * fix that here if needed.
     */
    if (target_saddr->sa_family == AF_UNIX) {
        if (len < unix_maxlen && len > 0) {
            char *cp = (char *)target_saddr;

            if (cp[len - 1] && !cp[len]) {
                len++;
            }
        }
        if (len > unix_maxlen) {
            len = unix_maxlen;
        }
    }

    memcpy(addr, target_saddr, len);
    addr->sa_family = sa_family;        /* type uint8_t */
    addr->sa_len = target_saddr->sa_len;    /* type uint8_t */
    unlock_user(target_saddr, target_addr, 0);

    return 0;
}

abi_long host_to_target_sockaddr(abi_ulong target_addr, struct sockaddr *addr,
                                 socklen_t len)
{
    struct target_sockaddr *target_saddr;

    target_saddr = lock_user(VERIFY_WRITE, target_addr, len, 0);
    if (target_saddr == 0) {
        return -TARGET_EFAULT;
    }
    memcpy(target_saddr, addr, len);
    target_saddr->sa_family = addr->sa_family;  /* type uint8_t */
    target_saddr->sa_len = addr->sa_len;        /* type uint8_t */
    unlock_user(target_saddr, target_addr, len);

    return 0;
}

abi_long target_to_host_ip_mreq(struct ip_mreqn *mreqn, abi_ulong target_addr,
                                socklen_t len)
{
    struct target_ip_mreqn *target_smreqn;

    target_smreqn = lock_user(VERIFY_READ, target_addr, len, 1);
    if (target_smreqn == 0) {
        return -TARGET_EFAULT;
    }
    mreqn->imr_multiaddr.s_addr = target_smreqn->imr_multiaddr.s_addr;
    mreqn->imr_address.s_addr = target_smreqn->imr_address.s_addr;
    if (len == sizeof(struct target_ip_mreqn)) {
        mreqn->imr_ifindex = tswap32(target_smreqn->imr_ifindex);
    }
    unlock_user(target_smreqn, target_addr, 0);

    return 0;
}

