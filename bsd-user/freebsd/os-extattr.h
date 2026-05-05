/*
 * FreeBSD extended attributes and ACL system call support
 *
 * Copyright (c) 2013 Stacey D. Son
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <sys/extattr.h>
#include <sys/acl.h>

/* extattrctl() */
static inline abi_long do_freebsd_extattrctl(abi_ulong arg1, abi_ulong arg2,
                                             abi_ulong arg3, abi_ulong arg4,
                                             abi_ulong arg5)
{
    abi_long ret;
    void *p, *a, *f;

    p = lock_user_string(arg1);
    if (p == NULL) {
        return -TARGET_EFAULT;
    }
    f = lock_user_string(arg3);
    if (f == NULL) {
        unlock_user(p, arg1, 0);
        return -TARGET_EFAULT;
    }
    a = lock_user_string(arg5);
    if (a == NULL) {
        unlock_user(f, arg3, 0);
        unlock_user(p, arg1, 0);
        return -TARGET_EFAULT;
    }
    ret = get_errno(extattrctl(path(p), arg2, f, arg4, a));
    unlock_user(a, arg5, 0);
    unlock_user(f, arg3, 0);
    unlock_user(p, arg1, 0);

    return ret;
}

/* extattr_set_file(2) */
static inline abi_long do_freebsd_extattr_set_file(abi_long arg1,
                                                   abi_long arg2,
                                                   abi_long arg3,
                                                   abi_long arg4,
                                                   abi_long arg5)
{
    abi_long ret;
    void *p, *a, *d;

    p = lock_user_string(arg1);
    if (p == NULL) {
        return -TARGET_EFAULT;
    }
    a = lock_user_string(arg3);
    if (a == NULL) {
        unlock_user(p, arg1, 0);
        return -TARGET_EFAULT;
    }
    d = lock_user(VERIFY_READ, arg4, arg5, 1);
    if (d == NULL) {
        unlock_user(a, arg3, 0);
        unlock_user(p, arg1, 0);
        return -TARGET_EFAULT;
    }
    ret = get_errno(extattr_set_file(path(p), arg2, a, d, arg5));
    unlock_user(d, arg4, arg5);
    unlock_user(a, arg3, 0);
    unlock_user(p, arg1, 0);

    return ret;
}

/* extattr_get_file(2) */
static inline abi_long do_freebsd_extattr_get_file(abi_long arg1,
                                                   abi_long arg2,
                                                   abi_long arg3,
                                                   abi_long arg4,
                                                   abi_long arg5)
{
    abi_long ret;
    void *p, *a, *d;

    p = lock_user_string(arg1);
    if (p == NULL) {
        return -TARGET_EFAULT;
    }
    a = lock_user_string(arg3);
    if (a == NULL) {
        unlock_user(p, arg1, 0);
        return -TARGET_EFAULT;
    }
    if (arg4 && arg5 > 0) {
        d = lock_user(VERIFY_WRITE, arg4, arg5, 0);
        if (d == NULL) {
            unlock_user(a, arg3, 0);
            unlock_user(p, arg1, 0);
            return -TARGET_EFAULT;
        }
        ret = get_errno(extattr_get_file(path(p), arg2, a, d, arg5));
        unlock_user(d, arg4, arg5);
    } else {
        ret = get_errno(extattr_get_file(path(p), arg2, a, NULL, arg5));
    }
    unlock_user(a, arg3, 0);
    unlock_user(p, arg1, 0);

    return ret;
}

/* extattr_delete_file(2) */
static inline abi_long do_freebsd_extattr_delete_file(abi_long arg1,
                                                      abi_long arg2,
                                                      abi_long arg3)
{
    abi_long ret;
    void *p, *a;

    p = lock_user_string(arg1);
    if (p == NULL) {
        return -TARGET_EFAULT;
    }
    a = lock_user_string(arg3);
    if (a == NULL) {
        unlock_user(p, arg1, 0);
        return -TARGET_EFAULT;
    }
    ret = get_errno(extattr_delete_file(path(p), arg2, a));
    unlock_user(a, arg3, 0);
    unlock_user(p, arg1, 0);

    return ret;
}

/* extattr_set_fd(2) */
static inline abi_long do_freebsd_extattr_set_fd(abi_long arg1, abi_long arg2,
                                                 abi_long arg3, abi_long arg4,
                                                 abi_long arg5)
{
    abi_long ret;
    void *a, *d;

    a = lock_user_string(arg3);
    if (a == NULL) {
        return -TARGET_EFAULT;
    }
    d = lock_user(VERIFY_READ, arg4, arg5, 1);
    if (d == NULL) {
        unlock_user(a, arg3, 0);
        return -TARGET_EFAULT;
    }
    ret = get_errno(extattr_set_fd(arg1, arg2, a, d, arg5));
    unlock_user(d, arg4, arg5);
    unlock_user(a, arg3, 0);

    return ret;
}

/* extattr_get_fd(2) */
static inline abi_long do_freebsd_extattr_get_fd(abi_long arg1, abi_long arg2,
                                                 abi_long arg3, abi_long arg4,
                                                 abi_long arg5)
{
    abi_long ret;
    void *a, *d;

    a = lock_user_string(arg3);
    if (a == NULL) {
        return -TARGET_EFAULT;
    }

    if (arg4 && arg5 > 0) {
        d = lock_user(VERIFY_WRITE, arg4, arg5, 0);
        if (d == NULL) {
            unlock_user(a, arg3, 0);
            return -TARGET_EFAULT;
        }
        ret = get_errno(extattr_get_fd(arg1, arg2, a, d, arg5));
        unlock_user(d, arg4, arg5);
    } else {
        ret = get_errno(extattr_get_fd(arg1, arg2, a, NULL, arg5));
    }
    unlock_user(a, arg3, 0);

    return ret;
}

/* extattr_delete_fd(2) */
static inline abi_long do_freebsd_extattr_delete_fd(abi_long arg1,
                                                    abi_long arg2,
                                                    abi_long arg3)
{
    abi_long ret;
    void *a;

    a = lock_user_string(arg3);
    if (a == NULL) {
        return -TARGET_EFAULT;
    }
    ret = get_errno(extattr_delete_fd(arg1, arg2, a));
    unlock_user(a, arg3, 0);

    return ret;
}

/* extattr_get_link(2) */
static inline abi_long do_freebsd_extattr_get_link(abi_long arg1,
                                                   abi_long arg2,
                                                   abi_long arg3,
                                                   abi_long arg4,
                                                   abi_long arg5)
{
    abi_long ret;
    void *p, *a, *d;

    p = lock_user_string(arg1);
    if (p == NULL) {
        return -TARGET_EFAULT;
    }
    a = lock_user_string(arg3);
    if (a == NULL) {
        unlock_user(p, arg1, 0);
        return -TARGET_EFAULT;
    }
    if (arg4 && arg5 > 0) {
        d = lock_user(VERIFY_WRITE, arg4, arg5, 0);
        if (d == NULL) {
            unlock_user(a, arg3, 0);
            unlock_user(p, arg1, 0);
            return -TARGET_EFAULT;
        }
        ret = get_errno(extattr_get_link(path(p), arg2, a, d, arg5));
        unlock_user(d, arg4, arg5);
    } else {
        ret = get_errno(extattr_get_link(path(p), arg2, a, NULL, arg5));
    }
    unlock_user(a, arg3, 0);
    unlock_user(p, arg1, 0);

    return ret;
}

/* extattr_set_link(2) */
static inline abi_long do_freebsd_extattr_set_link(abi_long arg1,
                                                   abi_long arg2,
                                                   abi_long arg3,
                                                   abi_long arg4,
                                                   abi_long arg5)
{
    abi_long ret;
    void *p, *a, *d;

    p = lock_user_string(arg1);
    if (p == NULL) {
        return -TARGET_EFAULT;
    }
    a = lock_user_string(arg3);
    if (a == NULL) {
        unlock_user(p, arg1, 0);
        return -TARGET_EFAULT;
    }
    d = lock_user(VERIFY_READ, arg4, arg5, 1);
    if (d == NULL) {
        unlock_user(a, arg3, 0);
        unlock_user(p, arg1, 0);
        return -TARGET_EFAULT;
    }
    ret = get_errno(extattr_set_link(path(p), arg2, a, d, arg5));
    unlock_user(d, arg4, arg5);
    unlock_user(a, arg3, 0);
    unlock_user(p, arg1, 0);

    return ret;
}

/* extattr_delete_link(2) */
static inline abi_long do_freebsd_extattr_delete_link(abi_long arg1,
                                                      abi_long arg2,
                                                      abi_long arg3)
{
    abi_long ret;
    void *p, *a;

    p = lock_user_string(arg1);
    if (p == NULL) {
        return -TARGET_EFAULT;
    }
    a = lock_user_string(arg3);
    if (a == NULL) {
        unlock_user(p, arg1, 0);
        return -TARGET_EFAULT;
    }
    ret = get_errno(extattr_delete_link(path(p), arg2, a));
    unlock_user(a, arg3, 0);
    unlock_user(p, arg1, 0);

    return ret;
}

/* extattr_list_fd(2) */
static inline abi_long do_freebsd_extattr_list_fd(
    abi_long arg1, abi_long arg2, abi_long arg3, abi_long arg4)
{
    abi_long ret;
    void *d;

    if (arg3 && arg4 > 0) {
        d = lock_user(VERIFY_WRITE, arg3, arg4, 0);
        if (d == NULL) {
            return -TARGET_EFAULT;
        }
        ret = get_errno(extattr_list_fd(arg1, arg2, d, arg4));
        unlock_user(d, arg3, arg4);
    } else {
        ret = get_errno(extattr_list_fd(arg1, arg2, NULL, arg4));
    }
    return ret;
}

/* extattr_list_file(2) */
static inline abi_long do_freebsd_extattr_list_file(
    abi_long arg1, abi_long arg2, abi_long arg3, abi_long arg4)
{
    abi_long ret;
    void *p, *d;

    p = lock_user_string(arg1);
    if (p == NULL) {
        return -TARGET_EFAULT;
    }
    if (arg3 && arg4 > 0) {
        d = lock_user(VERIFY_WRITE, arg3, arg4, 0);
        if (d == NULL) {
            unlock_user(p, arg1, 0);
            return -TARGET_EFAULT;
        }
        ret = get_errno(extattr_list_file(path(p), arg2, d, arg4));
        unlock_user(d, arg3, arg4);
    } else {
        ret = get_errno(extattr_list_file(path(p), arg2, NULL, arg4));
    }
    unlock_user(p, arg1, 0);

    return ret;
}

/* extattr_list_link(2) */
static inline abi_long do_freebsd_extattr_list_link(
    abi_long arg1, abi_long arg2, abi_long arg3, abi_long arg4)
{
    abi_long ret;
    void *p, *d;

    p = lock_user_string(arg1);
    if (p == NULL) {
        return -TARGET_EFAULT;
    }
    if (arg3 && arg4 > 0) {
        d = lock_user(VERIFY_WRITE, arg3, arg4, 0);
        if (d == NULL) {
            unlock_user(p, arg1, 0);
            return -TARGET_EFAULT;
        }
        ret = get_errno(extattr_list_link(path(p), arg2, d, arg4));
        unlock_user(d, arg3, arg4);
    } else {
        ret = get_errno(extattr_list_link(path(p), arg2, NULL, arg4));
    }
    unlock_user(p, arg1, 0);

    return ret;
}

/*
 *  Access Control Lists
 */


#endif /* FREEBSD_OS_EXTATTR_H */
