/*
 * QEMU Guest Agent BSD-specific command implementations
 *
 * Copyright (c) Virtuozzo International GmbH.
 *
 * Authors:
 *  Alexander Ivanov  <alexander.ivanov@virtuozzo.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qga-qapi-commands.h"
#include "qapi/error.h"
#include "qemu/queue.h"
#include "block/block-common.h"
#include "commands-common.h"
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/ucred.h>
#include <sys/mount.h>
#include <net/if_dl.h>
#include <sys/statvfs.h>
#if defined(__FreeBSD__)
#include <sys/sysctl.h>
#include <net/route.h>
#include <net/if.h>
#endif
#if defined(__NetBSD__) || defined(__OpenBSD__)
#include <net/if_arp.h>
#include <netinet/if_ether.h>
#else
#include <net/ethernet.h>
#endif
#include <paths.h>
#ifdef __FreeBSD__
#include <ufs/ffs/fs.h>
#endif
#ifdef __NetBSD__
typedef struct statvfs bsd_fsinfo_t;
#else
typedef struct statfs bsd_fsinfo_t;
#endif
#ifdef CONFIG_FREEBSD
#include <devstat.h>
#endif

bool build_fs_mount_list(FsMountList *mounts, Error **errp)
{
    FsMount *mount;
    bsd_fsinfo_t *mntbuf, *mntp;
    struct stat statbuf;
    int i, count, ret;

    count = getmntinfo(&mntbuf, MNT_NOWAIT);
    if (count == 0) {
        error_setg_errno(errp, errno, "getmntinfo failed");
        return false;
    }

    for (i = 0; i < count; i++) {
        mntp = &mntbuf[i];
        ret = stat(mntp->f_mntonname, &statbuf);
        if (ret != 0) {
            error_setg_errno(errp, errno, "stat failed on %s",
                             mntp->f_mntonname);
            return false;
        }

        mount = g_new0(FsMount, 1);

        mount->dirname = g_strdup(mntp->f_mntonname);
        mount->devtype = g_strdup(mntp->f_fstypename);
        mount->fromname = g_strdup(mntp->f_mntfromname);
        mount->dev = statbuf.st_dev;
        mount->devmajor = major(mount->dev);
        mount->devminor = minor(mount->dev);
#ifdef CONFIG_FSFREEZE
        mount->fsid = mntp->f_fsid;
#endif

        QTAILQ_INSERT_TAIL(mounts, mount, next);
    }
    return true;
}

#if defined(CONFIG_FSFREEZE)
static int ufssuspend_fd = -1;
static int ufssuspend_cnt;

int64_t qmp_guest_fsfreeze_do_freeze_list(bool has_mountpoints,
                                          strList *mountpoints,
                                          FsMountList mounts,
                                          Error **errp)
{
    int ret;
    strList *list;
    struct FsMount *mount;

    if (ufssuspend_fd != -1) {
        error_setg(errp, "filesystems have already frozen");
        return -1;
    }

    ufssuspend_cnt = 0;
    ufssuspend_fd = qemu_open(_PATH_UFSSUSPEND, O_RDWR, errp);
    if (ufssuspend_fd == -1) {
        return -1;
    }

    QTAILQ_FOREACH_REVERSE(mount, &mounts, next) {
        /*
         * To issue fsfreeze in the reverse order of mounts, check if the
         * mount is listed in the list here
         */
        if (has_mountpoints) {
            for (list = mountpoints; list; list = list->next) {
                if (g_str_equal(list->value, mount->dirname)) {
                    break;
                }
            }
            if (!list) {
                continue;
            }
        }

        /* Only UFS supports suspend */
        if (!g_str_equal(mount->devtype, "ufs")) {
            continue;
        }

        ret = ioctl(ufssuspend_fd, UFSSUSPEND, &mount->fsid);
        if (ret == -1) {
            /*
             * ioctl returns EBUSY for all the FS except the first one
             * that was suspended
             */
            if (errno == EBUSY) {
                continue;
            }
            error_setg_errno(errp, errno, "failed to freeze %s",
                             mount->dirname);
            goto error;
        }
        ufssuspend_cnt++;
    }
    return ufssuspend_cnt;
error:
    close(ufssuspend_fd);
    ufssuspend_fd = -1;
    return -1;

}

/*
 * We don't need to call UFSRESUME ioctl because all the frozen FS
 * are thawed on /dev/ufssuspend closing.
 */
int qmp_guest_fsfreeze_do_thaw(Error **errp)
{
    int ret = ufssuspend_cnt;
    ufssuspend_cnt = 0;
    if (ufssuspend_fd != -1) {
        close(ufssuspend_fd);
        ufssuspend_fd = -1;
    }
    return ret;
}
#endif /* CONFIG_FSFREEZE */

#ifdef HAVE_GETIFADDRS
/*
 * Fill "buf" with MAC address by ifaddrs. Pointer buf must point to a
 * buffer with ETHER_ADDR_LEN length at least.
 *
 * Returns false in case of an error, otherwise true. "obtained" arguument
 * is true if a MAC address was obtained successful, otherwise false.
 */
bool guest_get_hw_addr(struct ifaddrs *ifa, unsigned char *buf,
                       bool *obtained, Error **errp)
{
    struct sockaddr_dl *sdp;

    *obtained = false;

    if (ifa->ifa_addr->sa_family != AF_LINK) {
        /* We can get HW address only for AF_LINK family. */
        g_debug("failed to get MAC address of %s", ifa->ifa_name);
        return true;
    }

    sdp = (struct sockaddr_dl *)ifa->ifa_addr;
    memcpy(buf, sdp->sdl_data + sdp->sdl_nlen, ETHER_ADDR_LEN);
    *obtained = true;

    return true;
}
#endif /* HAVE_GETIFADDRS */

static GuestFilesystemInfo *build_guest_fsinfo(struct FsMount *mount)
{
    GuestFilesystemInfo *fs = g_new0(GuestFilesystemInfo, 1);
    struct statvfs buf;
    unsigned long used, nonroot_total, fr_size;

    fs->mountpoint = g_strdup(mount->dirname);
    fs->type = g_strdup(mount->devtype);
    fs->name = g_path_get_basename(mount->fromname);

    if (statvfs(fs->mountpoint, &buf) == 0) {
        fr_size = buf.f_frsize;
        used = buf.f_blocks - buf.f_bfree;
        nonroot_total = used + buf.f_bavail;
        fs->used_bytes = used * fr_size;
        fs->total_bytes = nonroot_total * fr_size;
        fs->total_bytes_privileged = buf.f_blocks * fr_size;

        fs->has_total_bytes = true;
        fs->has_total_bytes_privileged = true;
        fs->has_used_bytes = true;
    }

    return fs;
}

GuestFilesystemInfoList *qmp_guest_get_fsinfo(Error **errp)
{
    FsMountList mounts;
    struct FsMount *mount;
    GuestFilesystemInfoList *ret = NULL;

    QTAILQ_INIT(&mounts);
    if (!build_fs_mount_list(&mounts, errp)) {
        return NULL;
    }

    QTAILQ_FOREACH(mount, &mounts, next) {
        g_debug("Building guest fsinfo for '%s'", mount->dirname);

        QAPI_LIST_PREPEND(ret, build_guest_fsinfo(mount));
    }

    free_fs_mount_list(&mounts);
    return ret;
}

#ifdef CONFIG_FREEBSD
static uint64_t bintime_to_msec(const struct bintime *bt)
{
    return (uint64_t)bt->sec * 1000ULL + (((bt->frac >> 32) * 1000ULL) >> 32);
}

static GuestDiskStatsInfo *
guest_diskstats_info_new(const struct devstat *dev)
{
    GuestDiskStatsInfo *diskstatinfo;
    GuestDiskStats *diskstat;

    diskstatinfo = g_new0(GuestDiskStatsInfo, 1);
    diskstatinfo->name = g_strdup_printf("%s%d", dev->device_name,
                                         dev->unit_number);
    /*
     * devstat does not expose Linux-style major/minor numbers.  Report the
     * devstat device number and unit number in these mandatory QAPI fields.
     */
    diskstatinfo->major = dev->device_number;
    diskstatinfo->minor = dev->unit_number;

    diskstat = g_new0(GuestDiskStats, 1);
    diskstat->has_read_ios = true;
    diskstat->read_ios = dev->operations[DEVSTAT_READ];
    diskstat->has_read_sectors = true;
    diskstat->read_sectors = dev->bytes[DEVSTAT_READ] / BDRV_SECTOR_SIZE;
    diskstat->has_read_ticks = true;
    diskstat->read_ticks = bintime_to_msec(&dev->duration[DEVSTAT_READ]);

    diskstat->has_write_ios = true;
    diskstat->write_ios = dev->operations[DEVSTAT_WRITE];
    diskstat->has_write_sectors = true;
    diskstat->write_sectors = dev->bytes[DEVSTAT_WRITE] / BDRV_SECTOR_SIZE;
    diskstat->has_write_ticks = true;
    diskstat->write_ticks = bintime_to_msec(&dev->duration[DEVSTAT_WRITE]);

    diskstat->has_discard_ios = true;
    diskstat->discard_ios = dev->operations[DEVSTAT_FREE];
    diskstat->has_discard_sectors = true;
    diskstat->discard_sectors = dev->bytes[DEVSTAT_FREE] / BDRV_SECTOR_SIZE;
    diskstat->has_discard_ticks = true;
    diskstat->discard_ticks = bintime_to_msec(&dev->duration[DEVSTAT_FREE]);

    diskstat->has_ios_pgr = true;
    diskstat->ios_pgr = dev->start_count - dev->end_count;

    diskstat->has_total_ticks = true;
    diskstat->total_ticks = bintime_to_msec(&dev->busy_time);

    diskstatinfo->stats = diskstat;
    return diskstatinfo;
}

GuestDiskStatsInfoList *qmp_guest_get_diskstats(Error **errp)
{
    GuestDiskStatsInfoList *head = NULL, **tail = &head;
    struct devinfo dinfo = { 0 };
    struct statinfo stats = { .dinfo = &dinfo };
    struct device_selection *dev_select = NULL;
    struct devstat_match matches[] = {
        {
            .match_fields = DEVSTAT_MATCH_TYPE,
            .device_type = DEVSTAT_TYPE_DIRECT,
            .num_match_categories = 1,
        },
    };
    int num_selected = 0;
    int num_selections = 0;
    long select_generation = 0;
    int i;

    if (devstat_checkversion(NULL) == -1) {
        error_setg(errp, "%s", devstat_errbuf);
        return NULL;
    }

    if (devstat_getdevs(NULL, &stats) == -1) {
        error_setg(errp, "%s", devstat_errbuf);
        goto error;
    }

    if (devstat_selectdevs(&dev_select, &num_selected, &num_selections,
                           &select_generation, stats.dinfo->generation,
                           stats.dinfo->devices, stats.dinfo->numdevs,
                           matches, ARRAY_SIZE(matches), NULL, 0,
                           DS_SELECT_ONLY,
                           stats.dinfo->numdevs, 0) == -1) {
        error_setg(errp, "%s", devstat_errbuf);
        goto error;
    }

    for (i = 0; i < num_selections; i++) {
        const struct devstat *dev;

        if (dev_select[i].selected == 0) {
            continue;
        }

        dev = &stats.dinfo->devices[dev_select[i].position];
        QAPI_LIST_APPEND(tail, guest_diskstats_info_new(dev));
    }

    free(stats.dinfo->mem_ptr);
    free(dev_select);
    return head;

error:
    qapi_free_GuestDiskStatsInfoList(head);
    free(stats.dinfo->mem_ptr);
    free(dev_select);
    return NULL;
}

GuestNetworkRouteList *qmp_guest_network_get_route(Error **errp)
{
    GuestNetworkRouteList *head = NULL, **tail = &head;
    int mib[] = { CTL_NET, PF_ROUTE, 0, AF_UNSPEC, NET_RT_DUMP, 0, 0 };
    g_autofree char *buf = NULL;
    size_t len;

    if (sysctl(mib, nitems(mib), NULL, &len, NULL, 0) == -1) {
        error_setg_errno(errp, errno, "sysctl size");
        return NULL;
    }

    buf = g_malloc0(len);

    if (sysctl(mib, nitems(mib), buf, &len, NULL, 0) == -1) {
        error_setg_errno(errp, errno, "sysctl routes");
        return NULL;
    }

    for (char *p = buf, *end = buf + len; p < end; ) {
        struct rt_msghdr *rtm = (struct rt_msghdr *)p;
        struct sockaddr *addresses[RTAX_MAX] = { NULL };
        struct sockaddr *sa;
        char destination[NI_MAXHOST];
        char gateway[NI_MAXHOST];
        char ifname[IFNAMSIZ];
        const struct sockaddr *dst = NULL;
        const struct sockaddr *gw = NULL;
        const struct sockaddr *netmask = NULL;
        struct sockaddr_in normalized_mask = { 0 };
        char netmask_buf[INET_ADDRSTRLEN];

        g_autoptr(GuestNetworkRoute) route = g_new0(GuestNetworkRoute, 1);

        if ((size_t)(end - p) < sizeof(*rtm) ||
            rtm->rtm_msglen < sizeof(*rtm) ||
            rtm->rtm_msglen > (size_t)(end - p)) {
            break;
        }

        p += rtm->rtm_msglen;

        if (rtm->rtm_version != RTM_VERSION) {
            continue;
        }

        sa = (struct sockaddr *)(rtm + 1);
        for (int i = 0; i < RTAX_MAX; i++) {
            if (rtm->rtm_addrs & (1U << i)) {
                addresses[i] = sa;
                sa = (struct sockaddr *)((char *)sa + SA_SIZE(sa));
            }
        }

        if (if_indextoname(rtm->rtm_index, ifname)) {
            route->iface = g_strdup(ifname);
        } else {
            route->iface = g_strdup_printf("if%u", rtm->rtm_index);
        }

#if defined(HAVE_STRUCT_RT_METRICS_RMX_METRIC)
        route->metric = rtm->rtm_rmx.rmx_metric;
#endif

        dst = addresses[RTAX_DST];
        if (!dst) {
            continue;
        }

        if (dst->sa_family == AF_INET) {
            route->version = 4;
        } else if (dst->sa_family == AF_INET6) {
            route->version = 6;
        } else {
            continue;
        }
        if (getnameinfo(dst, dst->sa_len,
                        destination, NI_MAXHOST,
                        NULL, 0, NI_NUMERICHOST) != 0)
            continue;
        route->destination = g_strdup(destination);

        gw = addresses[RTAX_GATEWAY];
        if (gw && (gw->sa_family == AF_INET || gw->sa_family == AF_INET6)) {
            if (getnameinfo(gw, gw->sa_len,
                            gateway, NI_MAXHOST,
                            NULL, 0, NI_NUMERICHOST) != 0) {
                continue;
            }
            if (gw->sa_family == AF_INET) {
                route->gateway = g_strdup(gateway);
            } else if (gw->sa_family == AF_INET6) {
                route->nexthop = g_strdup(gateway);
            }
        }

        netmask = addresses[RTAX_NETMASK];
        if (dst->sa_family == AF_INET) {
            if (netmask && netmask->sa_len != 0) {
                memcpy(&normalized_mask, netmask,
                       MIN(netmask->sa_len, sizeof(normalized_mask)));
            } else if (rtm->rtm_flags & RTF_HOST) {
                normalized_mask.sin_addr.s_addr = 0xFFFFFFFF;
            }

            if (inet_ntop(AF_INET, &normalized_mask.sin_addr,
                          netmask_buf, sizeof(netmask_buf)) != NULL) {
                route->mask = g_strdup(netmask_buf);
            } else {
                route->mask = NULL;
            }
        } else if (dst->sa_family == AF_INET6) {
            struct sockaddr_in6 normalized_mask6;
            memset(&normalized_mask6, 0, sizeof(normalized_mask6));

            if (netmask && netmask->sa_len != 0) {
                int prefix_len = 0;
                bool valid_mask = true;
                bool zero_found = false;

                memcpy(&normalized_mask6, netmask,
                       MIN(netmask->sa_len, sizeof(normalized_mask6)));

                for (int i = 0; i < sizeof(normalized_mask6.sin6_addr); i++) {
                    uint8_t byte = normalized_mask6.sin6_addr.s6_addr[i];
                    for (int bit = 7; bit >= 0; bit--) {
                        if ((byte >> bit) & 1) {
                            if (zero_found) {
                                valid_mask = false;
                                break;
                            }
                            prefix_len++;
                        } else {
                            zero_found = true;
                        }
                    }
                    if (!valid_mask) {
                        break;
                    }
                }

                if (valid_mask) {
                    route->desprefixlen = g_strdup_printf("%d", prefix_len);
                } else {
                    route->desprefixlen = NULL;
                }
            } else if (rtm->rtm_flags & RTF_HOST) {
                route->desprefixlen = g_strdup("128");
            } else {
                route->desprefixlen = g_strdup("0");
            }
        }

        if (rtm->rtm_rmx.rmx_mtu) {
            route->mtu = rtm->rtm_rmx.rmx_mtu;
            route->has_mtu = true;
        }

        route->flags = (uint32_t)rtm->rtm_flags;
        route->has_flags = true;

        QAPI_LIST_APPEND(tail, route);
        route = NULL;
    }

    return head;
}
#endif /* __FreeBSD__ */
