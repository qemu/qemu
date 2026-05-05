/*
 * FreeBSD extend attributes and ACL conversions
 *
 * Copyright (c) 2013 Stacey D. Son
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"

#ifndef _ACL_PRIVATE
#define _ACL_PRIVATE
#endif
#include <sys/acl.h>

#include "qemu.h"
#include "qemu-os.h"

/*
 * FreeBSD ACL conversion.
 */
abi_long t2h_freebsd_acl(struct acl *host_acl, abi_ulong target_addr)
{
    uint32_t i;
    struct target_freebsd_acl *target_acl;

    if (!lock_user_struct(VERIFY_READ, target_acl, target_addr, 1)) {
        return -TARGET_EFAULT;
    }
    __get_user(host_acl->acl_maxcnt, &target_acl->acl_maxcnt);
    __get_user(host_acl->acl_cnt, &target_acl->acl_cnt);

    if (host_acl->acl_maxcnt > ACL_MAX_ENTRIES ||
        host_acl->acl_cnt > ACL_MAX_ENTRIES) {
        unlock_user_struct(target_acl, target_addr, 0);
        return -TARGET_EINVAL;
    }

    for (i = 0; i < host_acl->acl_cnt; i++) {
        __get_user(host_acl->acl_entry[i].ae_tag,
            &target_acl->acl_entry[i].ae_tag);
        __get_user(host_acl->acl_entry[i].ae_id,
            &target_acl->acl_entry[i].ae_id);
        __get_user(host_acl->acl_entry[i].ae_perm,
            &target_acl->acl_entry[i].ae_perm);
        __get_user(host_acl->acl_entry[i].ae_entry_type,
            &target_acl->acl_entry[i].ae_entry_type);
        __get_user(host_acl->acl_entry[i].ae_flags,
            &target_acl->acl_entry[i].ae_flags);
    }

    unlock_user_struct(target_acl, target_addr, 0);
    return 0;
}

abi_long h2t_freebsd_acl(abi_ulong target_addr, struct acl *host_acl)
{
    uint32_t i;
    struct target_freebsd_acl *target_acl;

    if (host_acl->acl_maxcnt > ACL_MAX_ENTRIES ||
        host_acl->acl_cnt > ACL_MAX_ENTRIES) {
        return -TARGET_EINVAL;
    }

    if (!lock_user_struct(VERIFY_WRITE, target_acl, target_addr, 0)) {
        return -TARGET_EFAULT;
    }

    __put_user(host_acl->acl_maxcnt, &target_acl->acl_maxcnt);
    __put_user(host_acl->acl_cnt, &target_acl->acl_cnt);

    for (i = 0; i < host_acl->acl_cnt; i++) {
        __put_user(host_acl->acl_entry[i].ae_tag,
            &target_acl->acl_entry[i].ae_tag);
        __put_user(host_acl->acl_entry[i].ae_id,
            &target_acl->acl_entry[i].ae_id);
        __put_user(host_acl->acl_entry[i].ae_perm,
            &target_acl->acl_entry[i].ae_perm);
        __put_user(host_acl->acl_entry[i].ae_entry_type,
            &target_acl->acl_entry[i].ae_entry_type);
        __put_user(host_acl->acl_entry[i].ae_flags,
            &target_acl->acl_entry[i].ae_flags);
    }

    unlock_user_struct(target_acl, target_addr, 1);
    return 0;
}

abi_long t2h_freebsd_acl_type(acl_type_t *host_type, abi_long target_type)
{

    switch (target_type) {
    case TARGET_FREEBSD_ACL_TYPE_ACCESS_OLD:
        *host_type = ACL_TYPE_ACCESS_OLD;
        break;

    case TARGET_FREEBSD_ACL_TYPE_DEFAULT_OLD:
        *host_type = ACL_TYPE_DEFAULT_OLD;
        break;

    case TARGET_FREEBSD_ACL_TYPE_ACCESS:
        *host_type = ACL_TYPE_ACCESS;
        break;

    case TARGET_FREEBSD_ACL_TYPE_DEFAULT:
        *host_type = ACL_TYPE_DEFAULT;
        break;

    case TARGET_FREEBSD_ACL_TYPE_NFS4:
        *host_type = ACL_TYPE_NFS4;
        break;

    default:
        return -TARGET_EINVAL;
    }
    return 0;
}

