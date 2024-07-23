/*
 * QEMU Guest Agent common/cross-platform common commands
 *
 * Copyright (c) 2020 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef QGA_COMMANDS_COMMON_H
#define QGA_COMMANDS_COMMON_H

#include "qga-qapi-types.h"
#include "guest-agent-core.h"
#include "qemu/queue.h"

#if defined(__linux__)
#include <linux/fs.h>
#endif /* __linux__ */

#ifdef __FreeBSD__
#include <ufs/ffs/fs.h>
#endif /* __FreeBSD__ */

#if defined(CONFIG_FSFREEZE) || defined(CONFIG_FSTRIM)
typedef struct FsMount {
    char *dirname;
    char *devtype;
    unsigned int devmajor, devminor;
#if defined(__FreeBSD__)
    dev_t dev;
    fsid_t fsid;
#endif
    QTAILQ_ENTRY(FsMount) next;
} FsMount;

typedef QTAILQ_HEAD(FsMountList, FsMount) FsMountList;

bool build_fs_mount_list(FsMountList *mounts, Error **errp);
void free_fs_mount_list(FsMountList *mounts);
#endif /* CONFIG_FSFREEZE || CONFIG_FSTRIM */

#if defined(CONFIG_FSFREEZE)
int64_t qmp_guest_fsfreeze_do_freeze_list(bool has_mountpoints,
                                          strList *mountpoints,
                                          FsMountList mounts,
                                          Error **errp);
int qmp_guest_fsfreeze_do_thaw(Error **errp);
#endif /* CONFIG_FSFREEZE */

#ifdef HAVE_GETIFADDRS
#include <ifaddrs.h>
bool guest_get_hw_addr(struct ifaddrs *ifa, unsigned char *buf,
                       bool *obtained, Error **errp);
#endif

typedef struct GuestFileHandle GuestFileHandle;

GuestFileHandle *guest_file_handle_find(int64_t id, Error **errp);

GuestFileRead *guest_file_read_unsafe(GuestFileHandle *gfh,
                                      int64_t count, Error **errp);

/**
 * qga_get_host_name:
 * @errp: Error object
 *
 * Operating system agnostic way of querying host name.
 * Compared to g_get_host_name(), it doesn't cache the result.
 *
 * Returns allocated hostname (caller should free), NULL on failure.
 */
char *qga_get_host_name(Error **errp);

#endif
