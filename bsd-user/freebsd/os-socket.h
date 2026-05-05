/*
 * FreeBSD socket related system call shims
 *
 * Copyright (c) 2013-2014 Stacey D. Son
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef BSD_USER_FREEBSD_OS_SOCKET_H
#define BSD_USER_FREEBSD_OS_SOCKET_H

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>

#include "qemu-os.h"
#include "os-sockopt.h"

ssize_t safe_recvmsg(int s, struct msghdr *msg, int flags);
ssize_t safe_sendmsg(int s, const struct msghdr *msg, int flags);

/* do_sendrecvmsg_locked() Must return target values and target errnos. */
static abi_long do_sendrecvmsg_locked(int fd, struct target_msghdr *msgp,
                                      int flags, int send)
{
    abi_long ret, len;
    struct msghdr msg;
    abi_ulong count;
    struct iovec *vec;
    abi_ulong target_vec;

    if (msgp->msg_name) {
        msg.msg_namelen = tswap32(msgp->msg_namelen);
        msg.msg_name = alloca(msg.msg_namelen + 1);
        ret = target_to_host_sockaddr(msg.msg_name,
                                      tswapal(msgp->msg_name),
                                      msg.msg_namelen);
        if (ret == -TARGET_EFAULT) {
            /*
             * For connected sockets msg_name and msg_namelen must be ignored,
             * so returning EFAULT immediately is wrong.  Instead, pass a bad
             * msg_name to the host kernel, and let it decide whether to return
             * EFAULT or not.
             */
            msg.msg_name = (void *)-1;
        } else if (ret) {
            goto out2;
        }
    } else {
        msg.msg_name = NULL;
        msg.msg_namelen = 0;
    }
    msg.msg_controllen = 2 * tswap32(msgp->msg_controllen);
    if (msgp->msg_control) {
        msg.msg_control = alloca(msg.msg_controllen);
        memset(msg.msg_control, 0, msg.msg_controllen);
    } else {
        msg.msg_control = NULL;
    }

    msg.msg_flags = tswap32(msgp->msg_flags);

    count = tswap32(msgp->msg_iovlen);
    target_vec = tswapal(msgp->msg_iov);

    if (count > IOV_MAX) {
        /*
         * sendrcvmsg returns a different errno for this condition than
         * readv/writev, so we must catch it here before lock_iovec() does.
         */
        ret = -TARGET_EMSGSIZE;
        goto out2;
    }

    vec = lock_iovec(send ? VERIFY_READ : VERIFY_WRITE,
                     target_vec, count, send);
    if (vec == NULL) {
        ret = -host_to_target_errno(errno);
        goto out2;
    }
    msg.msg_iovlen = count;
    msg.msg_iov = vec;

    if (send) {
        if (msg.msg_control != NULL) {
            ret = t2h_freebsd_cmsg(&msg, msgp);
        } else {
            ret = 0;
        }
        if (ret == 0) {
            ret = get_errno(safe_sendmsg(fd, &msg, flags));
        }
    } else {
        ret = get_errno(safe_recvmsg(fd, &msg, flags));
        if (!is_error(ret)) {
            len = ret;
            if (msg.msg_control != NULL) {
                ret = h2t_freebsd_cmsg(msgp, &msg);
            } else {
                ret = 0;
            }
            if (!is_error(ret)) {
                msgp->msg_namelen = tswap32(msg.msg_namelen);
                msgp->msg_flags = tswap32(msg.msg_flags);
                if (msg.msg_name != NULL && msg.msg_name != (void *)-1) {
                    ret = host_to_target_sockaddr(tswapal(msgp->msg_name),
                                    msg.msg_name, msg.msg_namelen);
                    if (ret) {
                        goto out;
                    }
                }

                ret = len;
            }
        }
    }

out:
    unlock_iovec(vec, target_vec, count, !send);
out2:
    return ret;
}


static abi_long do_sendrecvmsg(int fd, abi_ulong target_msg,
                               int flags, int send)
{
    abi_long ret;
    struct target_msghdr *msgp;

    if (!lock_user_struct(send ? VERIFY_READ : VERIFY_WRITE,
                          msgp,
                          target_msg,
                          send ? 1 : 0)) {
        return -TARGET_EFAULT;
    }
    ret = do_sendrecvmsg_locked(fd, msgp, flags, send);
    unlock_user_struct(msgp, target_msg, send ? 0 : 1);
    return ret;
}

static inline struct sockopt_entry *get_sockopt_entry(int level)
{
    for (int i = 0; i < ARRAY_SIZE(so_cache); i++) {
        if (so_cache[i].level == level) {
            return so_cache[i].entry;
        }
    }
    return NULL;
}

/* setsockopt(2) */
static inline abi_long do_bsd_setsockopt(int sockfd, int level, int optname,
        abi_ulong optval_addr, socklen_t optlen)
{
    abi_long ret;
    int val;
    u_char ch;
    uint64_t val64;
    struct sockopt_entry *e = get_sockopt_entry(level);
    void *p;

    if (e == NULL) {
        gemu_log("Unsupported setsockopt level=%d optname=%d\n",
                 level, optname);
        return -TARGET_ENOPROTOOPT;
    }

    while (e->level != SOCK_LEVEL_NONE && e->optname != optname) {
        e++;
    }

    if (e->level == SOCK_LEVEL_NONE) {
        gemu_log("Unsupported setsockopt level=%d optname=%d\n",
                 level, optname);
        return -TARGET_ENOPROTOOPT;
    }

    for (int i = 0; i < ARRAY_SIZE(e->type) && e->type[i] != 0; i++) {
        switch (e->type[i]) {
        case SOCKOPT_TYPE_BOOL:
        case SOCKOPT_TYPE_INT:
        case SOCKOPT_TYPE_U_INT:
        case SOCKOPT_TYPE_UINT32_T:
            if (optlen != sizeof(int)) {
                continue;
            }
            if (get_user_u32(val, optval_addr)) {
                return -TARGET_EFAULT;
            }
            return get_errno(setsockopt(sockfd, level, optname, &val, sizeof(val)));
        case SOCKOPT_TYPE_U_CHAR:
            if (optlen != sizeof(u_char)) {
                continue;
            }
            if (get_user_u8(ch, optval_addr)) {
                return -TARGET_EFAULT;
            }
            return get_errno(setsockopt(sockfd, level, optname, &ch, sizeof(ch)));
        case SOCKOPT_TYPE_UINT64_T:
            if (optlen != sizeof(uint64_t)) {
                continue;
            }
            if (get_user_u64(val64, optval_addr)) {
                return -TARGET_EFAULT;
            }
            return get_errno(setsockopt(sockfd, level, optname, &val64, sizeof(val64)));
        default:
#if HOST_LONG_BITS != TARGET_ABI_BITS || HOST_BIG_ENDIAN != TARGET_BIG_ENDIAN
            gemu_log("Unsupported setsockopt level=%d optname=%d\n",
                     level, optname);
            return -TARGET_ENOPROTOOPT;
#endif
            break;
        }
    }

    /*
     * When bit size and endian are the same, the structures are the same (we hope).
     */
    p = lock_user(VERIFY_READ, optval_addr, optlen, 1);
    if (p == NULL) {
        return -TARGET_EFAULT;
    }
    ret = get_errno(setsockopt(sockfd, level, optname, p, optlen));
    unlock_user(p, optval_addr, 0);
    return ret;
}

/* getsockopt(2) */
static inline abi_long do_bsd_getsockopt(int sockfd, int level, int optname,
        abi_ulong optval_addr, abi_ulong optlen)
{
    abi_long ret;
    int val;
    u_char ch;
    socklen_t len;
    uint64_t val64;
    struct sockopt_entry *e = get_sockopt_entry(level);
    void *p;

    if (e == NULL) {
        gemu_log("Unsupported getsockopt level=%d optname=%d\n",
                 level, optname);
        return -TARGET_ENOPROTOOPT;
    }

    while (e->level != SOCK_LEVEL_NONE && e->optname != optname) {
        e++;
    }

    if (e->level == SOCK_LEVEL_NONE) {
        gemu_log("Unsupported getsockopt level=%d optname=%d\n",
                 level, optname);
        return -TARGET_ENOPROTOOPT;
    }

    if (get_user_u32(len, optlen)) {
        return -TARGET_EFAULT;
    }

    for (int i = 0; i < ARRAY_SIZE(e->type) && e->type[i] != 0; i++) {
        switch (e->type[i]) {
        case SOCKOPT_TYPE_BOOL:
        case SOCKOPT_TYPE_INT:
        case SOCKOPT_TYPE_U_INT:
        case SOCKOPT_TYPE_UINT32_T:
            if (len != sizeof(int)) {
                continue;
            }
            ret = get_errno(getsockopt(sockfd, level, optname, &val, &len));
            if (ret < 0) {
                return ret;
            }
            if (len == sizeof(int)) {
                if (put_user_u32(val, optval_addr)) {
                    return -TARGET_EFAULT;
                }
                goto done;
            }
            continue;
        case SOCKOPT_TYPE_U_CHAR:
            if (len != sizeof(u_char)) {
                continue;
            }
            ret = get_errno(getsockopt(sockfd, level, optname, &ch, &len));
            if (ret < 0) {
                return ret;
            }
            if (len == sizeof(u_char)) {
                if (put_user_u8(ch, optval_addr)) {
                    return -TARGET_EFAULT;
                }
                goto done;
            }
            continue;
        case SOCKOPT_TYPE_UINT64_T:
            if (len != sizeof(uint64_t)) {
                continue;
            }
            ret = get_errno(getsockopt(sockfd, level, optname, &val64, &len));
            if (ret < 0) {
                return ret;
            }
            if (len == sizeof(uint64_t)) {
                if (put_user_u64(val64, optval_addr)) {
                    return -TARGET_EFAULT;
                }
                goto done;
            }
            continue;
        default:
#if HOST_LONG_BITS != TARGET_ABI_BITS || HOST_BIG_ENDIAN != TARGET_BIG_ENDIAN
            gemu_log("Unsupported getsockopt level=%d optname=%d\n",
                     level, optname);
            return -TARGET_ENOPROTOOPT;
#endif
            break;
        }
    }

    /*
     * When bit size and endian are the same, the structures are the same, and
     * if not that is handled above.
     */
    p = lock_user(VERIFY_WRITE, optval_addr, len, 0);
    if (p == NULL) {
        return -TARGET_EFAULT;
    }
    ret = get_errno(getsockopt(sockfd, level, optname, p, &len));
    unlock_user(p, optval_addr, ret < 0 ? 0 : len);
done:
    if (put_user_u32(len, optlen)) {
        return -TARGET_EFAULT;
    }
    return ret;
}

/* setfib(2) */
static inline abi_long do_freebsd_setfib(abi_long fib)
{

    return get_errno(setfib(fib));
}

/* bindat(2) */
static inline abi_long do_freebsd_bindat(int fd, int sockfd,
        abi_ulong target_addr, socklen_t addrlen)
{
    abi_long ret;
    void *addr;

    if ((int)addrlen < 0) {
        return -TARGET_EINVAL;
    }

    addr = alloca(addrlen + 1);
    ret = target_to_host_sockaddr(addr, target_addr, addrlen);
    if (is_error(ret)) {
        return ret;
    }

    return get_errno(bindat(fd, sockfd, addr, addrlen));
}

/* connectat(2) */
static inline abi_long do_freebsd_connectat(int fd, int sockfd,
        abi_ulong target_addr, socklen_t addrlen)
{
    abi_long ret;
    void *addr;

    if ((int)addrlen < 0) {
        return -TARGET_EINVAL;
    }
    addr = alloca(addrlen + 1);

    ret = target_to_host_sockaddr(addr, target_addr, addrlen);

    if (is_error(ret)) {
        return ret;
    }

    return get_errno(connectat(fd, sockfd, addr, addrlen));
}

/* accept4(2) */
static inline abi_long do_freebsd_accept4(int fd, abi_ulong target_addr,
        abi_ulong target_addrlen_addr, int flags)
{
    socklen_t addrlen;
    void *addr;
    abi_long ret;

    if (target_addr == 0) {
        return get_errno(accept(fd, NULL, NULL));
    }
    /* return EINVAL if addrlen pointer is invalid */
    if (get_user_u32(addrlen, target_addrlen_addr)) {
        return -TARGET_EINVAL;
    }
    if ((int)addrlen < 0) {
        return -TARGET_EINVAL;
    }
    if (!access_ok(VERIFY_WRITE, target_addr, addrlen)) {
        return -TARGET_EINVAL;
    }
    addr = alloca(addrlen);

    ret = get_errno(accept4(fd, addr, &addrlen, flags));
    if (!is_error(ret)) {
        if (is_error(host_to_target_sockaddr(target_addr, addr, addrlen))) {
            close(ret);
            ret = -TARGET_EFAULT;
        } else if (put_user_u32(addrlen, target_addrlen_addr)) {
            close(ret);
            ret = -TARGET_EFAULT;
        }
    }
    return ret;
}

#endif /* BSD_USER_FREEBSD_OS_SOCKET_H */
