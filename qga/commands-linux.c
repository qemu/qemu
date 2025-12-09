/*
 * QEMU Guest Agent Linux-specific command implementations
 *
 * Copyright IBM Corp. 2011
 *
 * Authors:
 *  Michael Roth      <mdroth@linux.vnet.ibm.com>
 *  Michal Privoznik  <mprivozn@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qga-qapi-commands.h"
#include "qapi/error.h"
#include "commands-common.h"
#include "cutils.h"
#include <mntent.h>
#include <sys/ioctl.h>
#include <mntent.h>
#include <linux/nvme_ioctl.h>
#include "block/nvme.h"

#ifdef CONFIG_LIBUDEV
#include <libudev.h>
#endif

#ifdef HAVE_GETIFADDRS
#include <net/if.h>
#endif

#include <sys/statvfs.h>

#if defined(CONFIG_FSFREEZE) || defined(CONFIG_FSTRIM)
static int dev_major_minor(const char *devpath,
                           unsigned int *devmajor, unsigned int *devminor)
{
    struct stat st;

    *devmajor = 0;
    *devminor = 0;

    if (stat(devpath, &st) < 0) {
        slog("failed to stat device file '%s': %s", devpath, strerror(errno));
        return -1;
    }
    if (S_ISDIR(st.st_mode)) {
        /* It is bind mount */
        return -2;
    }
    if (S_ISBLK(st.st_mode)) {
        *devmajor = major(st.st_rdev);
        *devminor = minor(st.st_rdev);
        return 0;
    }
    return -1;
}

/*
 * Check if we already have the devmajor:devminor in the mounts
 * If thats the case return true.
 */
static bool dev_exists(FsMountList *mounts, unsigned int devmajor, unsigned int devminor)
{
    FsMount *mount;

    QTAILQ_FOREACH(mount, mounts, next) {
        if (mount->devmajor == devmajor && mount->devminor == devminor) {
            return true;
        }
    }
    return false;
}

static bool build_fs_mount_list_from_mtab(FsMountList *mounts, Error **errp)
{
    struct mntent *ment;
    FsMount *mount;
    char const *mtab = "/proc/self/mounts";
    FILE *fp;
    unsigned int devmajor, devminor;

    fp = setmntent(mtab, "r");
    if (!fp) {
        error_setg(errp, "failed to open mtab file: '%s'", mtab);
        return false;
    }

    while ((ment = getmntent(fp))) {
        /*
         * An entry which device name doesn't start with a '/' is
         * either a dummy file system or a network file system.
         * Add special handling for smbfs and cifs as is done by
         * coreutils as well.
         */
        if ((ment->mnt_fsname[0] != '/') ||
            (strcmp(ment->mnt_type, "smbfs") == 0) ||
            (strcmp(ment->mnt_type, "cifs") == 0)) {
            continue;
        }
        if (dev_major_minor(ment->mnt_fsname, &devmajor, &devminor) == -2) {
            /* Skip bind mounts */
            continue;
        }
        if (dev_exists(mounts, devmajor, devminor)) {
            /* Skip already existing devices (bind mounts) */
            continue;
        }

        mount = g_new0(FsMount, 1);
        mount->dirname = g_strdup(ment->mnt_dir);
        mount->devtype = g_strdup(ment->mnt_type);
        mount->devmajor = devmajor;
        mount->devminor = devminor;

        QTAILQ_INSERT_TAIL(mounts, mount, next);
    }

    endmntent(fp);
    return true;
}

static void decode_mntname(char *name, int len)
{
    int i, j = 0;
    for (i = 0; i <= len; i++) {
        if (name[i] != '\\') {
            name[j++] = name[i];
        } else if (name[i + 1] == '\\') {
            name[j++] = '\\';
            i++;
        } else if (name[i + 1] >= '0' && name[i + 1] <= '3' &&
                   name[i + 2] >= '0' && name[i + 2] <= '7' &&
                   name[i + 3] >= '0' && name[i + 3] <= '7') {
            name[j++] = (name[i + 1] - '0') * 64 +
                        (name[i + 2] - '0') * 8 +
                        (name[i + 3] - '0');
            i += 3;
        } else {
            name[j++] = name[i];
        }
    }
}

/*
 * Walk the mount table and build a list of local file systems
 */
bool build_fs_mount_list(FsMountList *mounts, Error **errp)
{
    FsMount *mount;
    char const *mountinfo = "/proc/self/mountinfo";
    FILE *fp;
    char *line = NULL, *dash;
    size_t n;
    char check;
    unsigned int devmajor, devminor;
    int ret, dir_s, dir_e, type_s, type_e, dev_s, dev_e;

    fp = fopen(mountinfo, "r");
    if (!fp) {
        return build_fs_mount_list_from_mtab(mounts, errp);
    }

    while (getline(&line, &n, fp) != -1) {
        ret = sscanf(line, "%*u %*u %u:%u %*s %n%*s%n%c",
                     &devmajor, &devminor, &dir_s, &dir_e, &check);
        if (ret < 3) {
            continue;
        }
        dash = strstr(line + dir_e, " - ");
        if (!dash) {
            continue;
        }
        ret = sscanf(dash, " - %n%*s%n %n%*s%n%c",
                     &type_s, &type_e, &dev_s, &dev_e, &check);
        if (ret < 1) {
            continue;
        }
        line[dir_e] = 0;
        dash[type_e] = 0;
        dash[dev_e] = 0;
        decode_mntname(line + dir_s, dir_e - dir_s);
        decode_mntname(dash + dev_s, dev_e - dev_s);
        if (devmajor == 0) {
            /* btrfs reports major number = 0 */
            if (strcmp("btrfs", dash + type_s) != 0 ||
                dev_major_minor(dash + dev_s, &devmajor, &devminor) < 0) {
                continue;
            }
        }

        if (dev_exists(mounts, devmajor, devminor)) {
            /* Skip already existing devices (bind mounts) */
            continue;
        }

        mount = g_new0(FsMount, 1);
        mount->dirname = g_strdup(line + dir_s);
        mount->devtype = g_strdup(dash + type_s);
        mount->devmajor = devmajor;
        mount->devminor = devminor;

        QTAILQ_INSERT_TAIL(mounts, mount, next);
    }
    free(line);

    fclose(fp);
    return true;
}
#endif /* CONFIG_FSFREEZE || CONFIG_FSTRIM */

#ifdef CONFIG_FSFREEZE
/*
 * Walk list of mounted file systems in the guest, and freeze the ones which
 * are real local file systems.
 */
int64_t qmp_guest_fsfreeze_do_freeze_list(bool has_mountpoints,
                                          strList *mountpoints,
                                          FsMountList mounts,
                                          Error **errp)
{
    struct FsMount *mount;
    strList *list;
    int fd, ret, i = 0;

    QTAILQ_FOREACH_REVERSE(mount, &mounts, next) {
        /* To issue fsfreeze in the reverse order of mounts, check if the
         * mount is listed in the list here */
        if (has_mountpoints) {
            for (list = mountpoints; list; list = list->next) {
                if (strcmp(list->value, mount->dirname) == 0) {
                    break;
                }
            }
            if (!list) {
                continue;
            }
        }

        fd = qga_open_cloexec(mount->dirname, O_RDONLY, 0);
        if (fd == -1) {
            error_setg_errno(errp, errno, "failed to open %s", mount->dirname);
            return -1;
        }

        /* we try to cull filesystems we know won't work in advance, but other
         * filesystems may not implement fsfreeze for less obvious reasons.
         * these will report EOPNOTSUPP. we simply ignore these when tallying
         * the number of frozen filesystems.
         * if a filesystem is mounted more than once (aka bind mount) a
         * consecutive attempt to freeze an already frozen filesystem will
         * return EBUSY.
         *
         * any other error means a failure to freeze a filesystem we
         * expect to be freezable, so return an error in those cases
         * and return system to thawed state.
         */
        ret = ioctl(fd, FIFREEZE);
        if (ret == -1) {
            if (errno != EOPNOTSUPP && errno != EBUSY) {
                error_setg_errno(errp, errno, "failed to freeze %s",
                                 mount->dirname);
                close(fd);
                return -1;
            }
        } else {
            i++;
        }
        close(fd);
    }
    return i;
}

int qmp_guest_fsfreeze_do_thaw(Error **errp)
{
    int ret;
    FsMountList mounts;
    FsMount *mount;
    int fd, i = 0, logged;
    Error *local_err = NULL;

    QTAILQ_INIT(&mounts);
    if (!build_fs_mount_list(&mounts, &local_err)) {
        error_propagate(errp, local_err);
        return -1;
    }

    QTAILQ_FOREACH(mount, &mounts, next) {
        logged = false;
        fd = qga_open_cloexec(mount->dirname, O_RDONLY, 0);
        if (fd == -1) {
            continue;
        }
        /* we have no way of knowing whether a filesystem was actually unfrozen
         * as a result of a successful call to FITHAW, only that if an error
         * was returned the filesystem was *not* unfrozen by that particular
         * call.
         *
         * since multiple preceding FIFREEZEs require multiple calls to FITHAW
         * to unfreeze, continuing issuing FITHAW until an error is returned,
         * in which case either the filesystem is in an unfreezable state, or,
         * more likely, it was thawed previously (and remains so afterward).
         *
         * also, since the most recent successful call is the one that did
         * the actual unfreeze, we can use this to provide an accurate count
         * of the number of filesystems unfrozen by guest-fsfreeze-thaw, which
         * may * be useful for determining whether a filesystem was unfrozen
         * during the freeze/thaw phase by a process other than qemu-ga.
         */
        do {
            ret = ioctl(fd, FITHAW);
            if (ret == 0 && !logged) {
                i++;
                logged = true;
            }
        } while (ret == 0);
        close(fd);
    }

    free_fs_mount_list(&mounts);

    return i;
}
#endif /* CONFIG_FSFREEZE */

#if defined(CONFIG_FSFREEZE)

static char *get_pci_driver(char const *syspath, int pathlen, Error **errp)
{
    char *path;
    char *dpath;
    char *driver = NULL;
    char buf[PATH_MAX];
    ssize_t len;

    path = g_strndup(syspath, pathlen);
    dpath = g_strdup_printf("%s/driver", path);
    len = readlink(dpath, buf, sizeof(buf) - 1);
    if (len != -1) {
        buf[len] = 0;
        driver = g_path_get_basename(buf);
    }
    g_free(dpath);
    g_free(path);
    return driver;
}

static int compare_uint(const void *_a, const void *_b)
{
    unsigned int a = *(unsigned int *)_a;
    unsigned int b = *(unsigned int *)_b;

    return a < b ? -1 : a > b ? 1 : 0;
}

/* Walk the specified sysfs and build a sorted list of host or ata numbers */
static int build_hosts(char const *syspath, char const *host, bool ata,
                       unsigned int *hosts, int hosts_max, Error **errp)
{
    char *path;
    DIR *dir;
    struct dirent *entry;
    int i = 0;

    path = g_strndup(syspath, host - syspath);
    dir = opendir(path);
    if (!dir) {
        error_setg_errno(errp, errno, "opendir(\"%s\")", path);
        g_free(path);
        return -1;
    }

    while (i < hosts_max) {
        entry = readdir(dir);
        if (!entry) {
            break;
        }
        if (ata && sscanf(entry->d_name, "ata%d", hosts + i) == 1) {
            ++i;
        } else if (!ata && sscanf(entry->d_name, "host%d", hosts + i) == 1) {
            ++i;
        }
    }

    qsort(hosts, i, sizeof(hosts[0]), compare_uint);

    g_free(path);
    closedir(dir);
    return i;
}

/*
 * Store disk device info for devices on the PCI bus.
 * Returns true if information has been stored, or false for failure.
 */
static bool build_guest_fsinfo_for_pci_dev(char const *syspath,
                                           GuestDiskAddress *disk,
                                           Error **errp)
{
    unsigned int pci[4], host, hosts[8], tgt[3];
    int i, offset, nhosts = 0, pcilen;
    GuestPCIAddress *pciaddr = disk->pci_controller;
    bool has_ata = false, has_host = false, has_tgt = false;
    const char *p;
    char *driver = NULL;
    bool ret = false;

    p = strstr(syspath, "/devices/pci");
    if (!p || sscanf(p + 12, "%*x:%*x/%x:%x:%x.%x%n",
                     pci, pci + 1, pci + 2, pci + 3, &pcilen) < 4) {
        g_debug("only pci device is supported: sysfs path '%s'", syspath);
        return false;
    }

    p += 12 + pcilen;
    while (true) {
        driver = get_pci_driver(syspath, p - syspath, errp);
        if (driver && (g_str_equal(driver, "ata_piix") ||
                       g_str_equal(driver, "sym53c8xx") ||
                       g_str_equal(driver, "virtio-pci") ||
                       g_str_equal(driver, "ahci") ||
                       g_str_equal(driver, "nvme") ||
                       g_str_equal(driver, "xhci_hcd") ||
                       g_str_equal(driver, "ehci-pci"))) {
            break;
        }

        g_free(driver);
        if (sscanf(p, "/%x:%x:%x.%x%n",
                          pci, pci + 1, pci + 2, pci + 3, &pcilen) == 4) {
            p += pcilen;
            continue;
        }

        g_debug("unsupported driver or sysfs path '%s'", syspath);
        return false;
    }

    p = strstr(syspath, "/target");
    if (p && sscanf(p + 7, "%*u:%*u:%*u/%*u:%u:%u:%u",
                    tgt, tgt + 1, tgt + 2) == 3) {
        has_tgt = true;
    }

    p = strstr(syspath, "/ata");
    if (p) {
        offset = 4;
        has_ata = true;
    } else {
        p = strstr(syspath, "/host");
        offset = 5;
    }
    if (p && sscanf(p + offset, "%u", &host) == 1) {
        has_host = true;
        nhosts = build_hosts(syspath, p, has_ata, hosts,
                             ARRAY_SIZE(hosts), errp);
        if (nhosts < 0) {
            goto cleanup;
        }
    }

    pciaddr->domain = pci[0];
    pciaddr->bus = pci[1];
    pciaddr->slot = pci[2];
    pciaddr->function = pci[3];

    if (strcmp(driver, "ata_piix") == 0) {
        /* a host per ide bus, target*:0:<unit>:0 */
        if (!has_host || !has_tgt) {
            g_debug("invalid sysfs path '%s' (driver '%s')", syspath, driver);
            goto cleanup;
        }
        for (i = 0; i < nhosts; i++) {
            if (host == hosts[i]) {
                disk->bus_type = GUEST_DISK_BUS_TYPE_IDE;
                disk->bus = i;
                disk->unit = tgt[1];
                break;
            }
        }
        if (i >= nhosts) {
            g_debug("no host for '%s' (driver '%s')", syspath, driver);
            goto cleanup;
        }
    } else if (strcmp(driver, "sym53c8xx") == 0) {
        /* scsi(LSI Logic): target*:0:<unit>:0 */
        if (!has_tgt) {
            g_debug("invalid sysfs path '%s' (driver '%s')", syspath, driver);
            goto cleanup;
        }
        disk->bus_type = GUEST_DISK_BUS_TYPE_SCSI;
        disk->unit = tgt[1];
    } else if (strcmp(driver, "virtio-pci") == 0) {
        if (has_tgt) {
            /* virtio-scsi: target*:0:0:<unit> */
            disk->bus_type = GUEST_DISK_BUS_TYPE_SCSI;
            disk->unit = tgt[2];
        } else {
            /* virtio-blk: 1 disk per 1 device */
            disk->bus_type = GUEST_DISK_BUS_TYPE_VIRTIO;
        }
    } else if (strcmp(driver, "ahci") == 0) {
        /* ahci: 1 host per 1 unit */
        if (!has_host || !has_tgt) {
            g_debug("invalid sysfs path '%s' (driver '%s')", syspath, driver);
            goto cleanup;
        }
        for (i = 0; i < nhosts; i++) {
            if (host == hosts[i]) {
                disk->unit = i;
                disk->bus_type = GUEST_DISK_BUS_TYPE_SATA;
                break;
            }
        }
        if (i >= nhosts) {
            g_debug("no host for '%s' (driver '%s')", syspath, driver);
            goto cleanup;
        }
    } else if (strcmp(driver, "nvme") == 0) {
        disk->bus_type = GUEST_DISK_BUS_TYPE_NVME;
    } else if (strcmp(driver, "ehci-pci") == 0 || strcmp(driver, "xhci_hcd") == 0) {
        disk->bus_type = GUEST_DISK_BUS_TYPE_USB;
    } else {
        g_debug("unknown driver '%s' (sysfs path '%s')", driver, syspath);
        goto cleanup;
    }

    ret = true;

cleanup:
    g_free(driver);
    return ret;
}

/*
 * Store disk device info for non-PCI virtio devices (for example s390x
 * channel I/O devices). Returns true if information has been stored, or
 * false for failure.
 */
static bool build_guest_fsinfo_for_nonpci_virtio(char const *syspath,
                                                 GuestDiskAddress *disk,
                                                 Error **errp)
{
    unsigned int tgt[3];
    const char *p;

    if (!strstr(syspath, "/virtio") || !strstr(syspath, "/block")) {
        g_debug("Unsupported virtio device '%s'", syspath);
        return false;
    }

    p = strstr(syspath, "/target");
    if (p && sscanf(p + 7, "%*u:%*u:%*u/%*u:%u:%u:%u",
                    &tgt[0], &tgt[1], &tgt[2]) == 3) {
        /* virtio-scsi: target*:0:<target>:<unit> */
        disk->bus_type = GUEST_DISK_BUS_TYPE_SCSI;
        disk->bus = tgt[0];
        disk->target = tgt[1];
        disk->unit = tgt[2];
    } else {
        /* virtio-blk: 1 disk per 1 device */
        disk->bus_type = GUEST_DISK_BUS_TYPE_VIRTIO;
    }

    return true;
}

/*
 * Store disk device info for CCW devices (s390x channel I/O devices).
 * Returns true if information has been stored, or false for failure.
 */
static bool build_guest_fsinfo_for_ccw_dev(char const *syspath,
                                           GuestDiskAddress *disk,
                                           Error **errp)
{
    unsigned int cssid, ssid, subchno, devno;
    const char *p;

    p = strstr(syspath, "/devices/css");
    if (!p || sscanf(p + 12, "%*x/%x.%x.%x/%*x.%*x.%x/",
                     &cssid, &ssid, &subchno, &devno) < 4) {
        g_debug("could not parse ccw device sysfs path: %s", syspath);
        return false;
    }

    disk->ccw_address = g_new0(GuestCCWAddress, 1);
    disk->ccw_address->cssid = cssid;
    disk->ccw_address->ssid = ssid;
    disk->ccw_address->subchno = subchno;
    disk->ccw_address->devno = devno;

    if (strstr(p, "/virtio")) {
        build_guest_fsinfo_for_nonpci_virtio(syspath, disk, errp);
    }

    return true;
}

/* Store disk device info specified by @sysfs into @fs */
static void build_guest_fsinfo_for_real_device(char const *syspath,
                                               GuestFilesystemInfo *fs,
                                               Error **errp)
{
    GuestDiskAddress *disk;
    GuestPCIAddress *pciaddr;
    bool has_hwinf;
#ifdef CONFIG_LIBUDEV
    struct udev *udev = NULL;
    struct udev_device *udevice = NULL;
#endif

    pciaddr = g_new0(GuestPCIAddress, 1);
    pciaddr->domain = -1;                       /* -1 means field is invalid */
    pciaddr->bus = -1;
    pciaddr->slot = -1;
    pciaddr->function = -1;

    disk = g_new0(GuestDiskAddress, 1);
    disk->pci_controller = pciaddr;
    disk->bus_type = GUEST_DISK_BUS_TYPE_UNKNOWN;

#ifdef CONFIG_LIBUDEV
    udev = udev_new();
    udevice = udev_device_new_from_syspath(udev, syspath);
    if (udev == NULL || udevice == NULL) {
        g_debug("failed to query udev");
    } else {
        const char *devnode, *serial;
        devnode = udev_device_get_devnode(udevice);
        if (devnode != NULL) {
            disk->dev = g_strdup(devnode);
        }
        serial = udev_device_get_property_value(udevice, "ID_SERIAL");
        if (serial != NULL && *serial != 0) {
            disk->serial = g_strdup(serial);
        }
    }

    udev_unref(udev);
    udev_device_unref(udevice);
#endif

    if (strstr(syspath, "/devices/pci")) {
        has_hwinf = build_guest_fsinfo_for_pci_dev(syspath, disk, errp);
    } else if (strstr(syspath, "/devices/css")) {
        has_hwinf = build_guest_fsinfo_for_ccw_dev(syspath, disk, errp);
    } else if (strstr(syspath, "/virtio")) {
        has_hwinf = build_guest_fsinfo_for_nonpci_virtio(syspath, disk, errp);
    } else {
        g_debug("Unsupported device type for '%s'", syspath);
        has_hwinf = false;
    }

    if (has_hwinf || disk->dev || disk->serial) {
        QAPI_LIST_PREPEND(fs->disk, disk);
    } else {
        qapi_free_GuestDiskAddress(disk);
    }
}

static void build_guest_fsinfo_for_device(char const *devpath,
                                          GuestFilesystemInfo *fs,
                                          Error **errp);

/* Store a list of slave devices of virtual volume specified by @syspath into
 * @fs */
static void build_guest_fsinfo_for_virtual_device(char const *syspath,
                                                  GuestFilesystemInfo *fs,
                                                  Error **errp)
{
    Error *err = NULL;
    DIR *dir;
    char *dirpath;
    struct dirent *entry;

    dirpath = g_strdup_printf("%s/slaves", syspath);
    dir = opendir(dirpath);
    if (!dir) {
        if (errno != ENOENT) {
            error_setg_errno(errp, errno, "opendir(\"%s\")", dirpath);
        }
        g_free(dirpath);
        return;
    }

    for (;;) {
        errno = 0;
        entry = readdir(dir);
        if (entry == NULL) {
            if (errno) {
                error_setg_errno(errp, errno, "readdir(\"%s\")", dirpath);
            }
            break;
        }

        if (entry->d_type == DT_LNK) {
            char *path;

            g_debug(" slave device '%s'", entry->d_name);
            path = g_strdup_printf("%s/slaves/%s", syspath, entry->d_name);
            build_guest_fsinfo_for_device(path, fs, &err);
            g_free(path);

            if (err) {
                error_propagate(errp, err);
                break;
            }
        }
    }

    g_free(dirpath);
    closedir(dir);
}

static bool is_disk_virtual(const char *devpath, Error **errp)
{
    g_autofree char *syspath = realpath(devpath, NULL);

    if (!syspath) {
        error_setg_errno(errp, errno, "realpath(\"%s\")", devpath);
        return false;
    }
    return strstr(syspath, "/devices/virtual/block/") != NULL;
}

/* Dispatch to functions for virtual/real device */
static void build_guest_fsinfo_for_device(char const *devpath,
                                          GuestFilesystemInfo *fs,
                                          Error **errp)
{
    ERRP_GUARD();
    g_autofree char *syspath = NULL;
    bool is_virtual = false;

    syspath = realpath(devpath, NULL);
    if (!syspath) {
        if (errno != ENOENT) {
            error_setg_errno(errp, errno, "realpath(\"%s\")", devpath);
            return;
        }

        /* ENOENT: This devpath may not exist because of container config */
        if (!fs->name) {
            fs->name = g_path_get_basename(devpath);
        }
        return;
    }

    if (!fs->name) {
        fs->name = g_path_get_basename(syspath);
    }

    g_debug("  parse sysfs path '%s'", syspath);
    is_virtual = is_disk_virtual(syspath, errp);
    if (*errp != NULL) {
        return;
    }
    if (is_virtual) {
        build_guest_fsinfo_for_virtual_device(syspath, fs, errp);
    } else {
        build_guest_fsinfo_for_real_device(syspath, fs, errp);
    }
}

#ifdef CONFIG_LIBUDEV

/*
 * Wrapper around build_guest_fsinfo_for_device() for getting just
 * the disk address.
 */
static GuestDiskAddress *get_disk_address(const char *syspath, Error **errp)
{
    g_autoptr(GuestFilesystemInfo) fs = NULL;

    fs = g_new0(GuestFilesystemInfo, 1);
    build_guest_fsinfo_for_device(syspath, fs, errp);
    if (fs->disk != NULL) {
        return g_steal_pointer(&fs->disk->value);
    }
    return NULL;
}

static char *get_alias_for_syspath(const char *syspath)
{
    struct udev *udev = NULL;
    struct udev_device *udevice = NULL;
    char *ret = NULL;

    udev = udev_new();
    if (udev == NULL) {
        g_debug("failed to query udev");
        goto out;
    }
    udevice = udev_device_new_from_syspath(udev, syspath);
    if (udevice == NULL) {
        g_debug("failed to query udev for path: %s", syspath);
        goto out;
    } else {
        const char *alias = udev_device_get_property_value(
            udevice, "DM_NAME");
        /*
         * NULL means there was an error and empty string means there is no
         * alias. In case of no alias we return NULL instead of empty string.
         */
        if (alias == NULL) {
            g_debug("failed to query udev for device alias for: %s",
                syspath);
        } else if (*alias != 0) {
            ret = g_strdup(alias);
        }
    }

out:
    udev_unref(udev);
    udev_device_unref(udevice);
    return ret;
}

static char *get_device_for_syspath(const char *syspath)
{
    struct udev *udev = NULL;
    struct udev_device *udevice = NULL;
    char *ret = NULL;

    udev = udev_new();
    if (udev == NULL) {
        g_debug("failed to query udev");
        goto out;
    }
    udevice = udev_device_new_from_syspath(udev, syspath);
    if (udevice == NULL) {
        g_debug("failed to query udev for path: %s", syspath);
        goto out;
    } else {
        ret = g_strdup(udev_device_get_devnode(udevice));
    }

out:
    udev_unref(udev);
    udev_device_unref(udevice);
    return ret;
}

static void get_disk_deps(const char *disk_dir, GuestDiskInfo *disk)
{
    g_autofree char *deps_dir = NULL;
    const gchar *dep;
    GDir *dp_deps = NULL;

    /* List dependent disks */
    deps_dir = g_strdup_printf("%s/slaves", disk_dir);
    g_debug("  listing entries in: %s", deps_dir);
    dp_deps = g_dir_open(deps_dir, 0, NULL);
    if (dp_deps == NULL) {
        g_debug("failed to list entries in %s", deps_dir);
        return;
    }
    disk->has_dependencies = true;
    while ((dep = g_dir_read_name(dp_deps)) != NULL) {
        g_autofree char *dep_dir = NULL;
        char *dev_name;

        /* Add dependent disks */
        dep_dir = g_strdup_printf("%s/%s", deps_dir, dep);
        dev_name = get_device_for_syspath(dep_dir);
        if (dev_name != NULL) {
            g_debug("  adding dependent device: %s", dev_name);
            QAPI_LIST_PREPEND(disk->dependencies, dev_name);
        }
    }
    g_dir_close(dp_deps);
}

/*
 * Detect partitions subdirectory, name is "<disk_name><number>" or
 * "<disk_name>p<number>"
 *
 * @disk_name -- last component of /sys path (e.g. sda)
 * @disk_dir -- sys path of the disk (e.g. /sys/block/sda)
 * @disk_dev -- device node of the disk (e.g. /dev/sda)
 */
static GuestDiskInfoList *get_disk_partitions(
    GuestDiskInfoList *list,
    const char *disk_name, const char *disk_dir,
    const char *disk_dev)
{
    GuestDiskInfoList *ret = list;
    struct dirent *de_disk;
    DIR *dp_disk = NULL;
    size_t len = strlen(disk_name);

    dp_disk = opendir(disk_dir);
    while ((de_disk = readdir(dp_disk)) != NULL) {
        g_autofree char *partition_dir = NULL;
        char *dev_name;
        GuestDiskInfo *partition;

        if (!(de_disk->d_type & DT_DIR)) {
            continue;
        }

        if (!(strncmp(disk_name, de_disk->d_name, len) == 0 &&
            ((*(de_disk->d_name + len) == 'p' &&
            isdigit(*(de_disk->d_name + len + 1))) ||
                isdigit(*(de_disk->d_name + len))))) {
            continue;
        }

        partition_dir = g_strdup_printf("%s/%s",
            disk_dir, de_disk->d_name);
        dev_name = get_device_for_syspath(partition_dir);
        if (dev_name == NULL) {
            g_debug("Failed to get device name for syspath: %s",
                disk_dir);
            continue;
        }
        partition = g_new0(GuestDiskInfo, 1);
        partition->name = dev_name;
        partition->partition = true;
        partition->has_dependencies = true;
        /* Add parent disk as dependent for easier tracking of hierarchy */
        QAPI_LIST_PREPEND(partition->dependencies, g_strdup(disk_dev));

        QAPI_LIST_PREPEND(ret, partition);
    }
    closedir(dp_disk);

    return ret;
}

static void get_nvme_smart(GuestDiskInfo *disk)
{
    int fd;
    GuestNVMeSmart *smart;
    NvmeSmartLog log = {0};
    struct nvme_admin_cmd cmd = {
        .opcode = NVME_ADM_CMD_GET_LOG_PAGE,
        .nsid = NVME_NSID_BROADCAST,
        .addr = (uintptr_t)&log,
        .data_len = sizeof(log),
        .cdw10 = NVME_LOG_SMART_INFO | (1 << 15) /* RAE bit */
                 | (((sizeof(log) >> 2) - 1) << 16)
    };

    fd = qga_open_cloexec(disk->name, O_RDONLY, 0);
    if (fd == -1) {
        g_debug("Failed to open device: %s: %s", disk->name, g_strerror(errno));
        return;
    }

    if (ioctl(fd, NVME_IOCTL_ADMIN_CMD, &cmd)) {
        g_debug("Failed to get smart: %s: %s", disk->name, g_strerror(errno));
        close(fd);
        return;
    }

    disk->smart = g_new0(GuestDiskSmart, 1);
    disk->smart->type = GUEST_DISK_BUS_TYPE_NVME;

    smart = &disk->smart->u.nvme;
    smart->critical_warning = log.critical_warning;
    smart->temperature = lduw_le_p(&log.temperature); /* unaligned field */
    smart->available_spare = log.available_spare;
    smart->available_spare_threshold = log.available_spare_threshold;
    smart->percentage_used = log.percentage_used;
    smart->data_units_read_lo = le64_to_cpu(log.data_units_read[0]);
    smart->data_units_read_hi = le64_to_cpu(log.data_units_read[1]);
    smart->data_units_written_lo = le64_to_cpu(log.data_units_written[0]);
    smart->data_units_written_hi = le64_to_cpu(log.data_units_written[1]);
    smart->host_read_commands_lo = le64_to_cpu(log.host_read_commands[0]);
    smart->host_read_commands_hi = le64_to_cpu(log.host_read_commands[1]);
    smart->host_write_commands_lo = le64_to_cpu(log.host_write_commands[0]);
    smart->host_write_commands_hi = le64_to_cpu(log.host_write_commands[1]);
    smart->controller_busy_time_lo = le64_to_cpu(log.controller_busy_time[0]);
    smart->controller_busy_time_hi = le64_to_cpu(log.controller_busy_time[1]);
    smart->power_cycles_lo = le64_to_cpu(log.power_cycles[0]);
    smart->power_cycles_hi = le64_to_cpu(log.power_cycles[1]);
    smart->power_on_hours_lo = le64_to_cpu(log.power_on_hours[0]);
    smart->power_on_hours_hi = le64_to_cpu(log.power_on_hours[1]);
    smart->unsafe_shutdowns_lo = le64_to_cpu(log.unsafe_shutdowns[0]);
    smart->unsafe_shutdowns_hi = le64_to_cpu(log.unsafe_shutdowns[1]);
    smart->media_errors_lo = le64_to_cpu(log.media_errors[0]);
    smart->media_errors_hi = le64_to_cpu(log.media_errors[1]);
    smart->number_of_error_log_entries_lo =
        le64_to_cpu(log.number_of_error_log_entries[0]);
    smart->number_of_error_log_entries_hi =
        le64_to_cpu(log.number_of_error_log_entries[1]);

    close(fd);
}

static void get_disk_smart(GuestDiskInfo *disk)
{
    if (disk->address
        && (disk->address->bus_type == GUEST_DISK_BUS_TYPE_NVME)) {
        get_nvme_smart(disk);
    }
}

GuestDiskInfoList *qmp_guest_get_disks(Error **errp)
{
    GuestDiskInfoList *ret = NULL;
    GuestDiskInfo *disk;
    DIR *dp = NULL;
    struct dirent *de = NULL;

    g_debug("listing /sys/block directory");
    dp = opendir("/sys/block");
    if (dp == NULL) {
        error_setg_errno(errp, errno, "Can't open directory \"/sys/block\"");
        return NULL;
    }
    while ((de = readdir(dp)) != NULL) {
        g_autofree char *disk_dir = NULL, *line = NULL,
            *size_path = NULL;
        char *dev_name;
        Error *local_err = NULL;
        if (de->d_type != DT_LNK) {
            g_debug("  skipping entry: %s", de->d_name);
            continue;
        }

        /* Check size and skip zero-sized disks */
        g_debug("  checking disk size");
        size_path = g_strdup_printf("/sys/block/%s/size", de->d_name);
        if (!g_file_get_contents(size_path, &line, NULL, NULL)) {
            g_debug("  failed to read disk size");
            continue;
        }
        if (g_strcmp0(line, "0\n") == 0) {
            g_debug("  skipping zero-sized disk");
            continue;
        }

        g_debug("  adding %s", de->d_name);
        disk_dir = g_strdup_printf("/sys/block/%s", de->d_name);
        dev_name = get_device_for_syspath(disk_dir);
        if (dev_name == NULL) {
            g_debug("Failed to get device name for syspath: %s",
                disk_dir);
            continue;
        }
        disk = g_new0(GuestDiskInfo, 1);
        disk->name = dev_name;
        disk->partition = false;
        disk->alias = get_alias_for_syspath(disk_dir);
        QAPI_LIST_PREPEND(ret, disk);

        /* Get address for non-virtual devices */
        bool is_virtual = is_disk_virtual(disk_dir, &local_err);
        if (local_err != NULL) {
            g_debug("  failed to check disk path, ignoring error: %s",
                error_get_pretty(local_err));
            error_free(local_err);
            local_err = NULL;
            /* Don't try to get the address */
            is_virtual = true;
        }
        if (!is_virtual) {
            disk->address = get_disk_address(disk_dir, &local_err);
            if (local_err != NULL) {
                g_debug("  failed to get device info, ignoring error: %s",
                    error_get_pretty(local_err));
                error_free(local_err);
                local_err = NULL;
            }
        }

        get_disk_deps(disk_dir, disk);
        get_disk_smart(disk);
        ret = get_disk_partitions(ret, de->d_name, disk_dir, dev_name);
    }

    closedir(dp);

    return ret;
}

#endif

/* Return a list of the disk device(s)' info which @mount lies on */
static GuestFilesystemInfo *build_guest_fsinfo(struct FsMount *mount,
                                               Error **errp)
{
    GuestFilesystemInfo *fs = g_malloc0(sizeof(*fs));
    struct statvfs buf;
    unsigned long used, nonroot_total, fr_size;
    char *devpath = g_strdup_printf("/sys/dev/block/%u:%u",
                                    mount->devmajor, mount->devminor);

    fs->mountpoint = g_strdup(mount->dirname);
    fs->type = g_strdup(mount->devtype);
    build_guest_fsinfo_for_device(devpath, fs, errp);

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

    g_free(devpath);

    return fs;
}

GuestFilesystemInfoList *qmp_guest_get_fsinfo(Error **errp)
{
    FsMountList mounts;
    struct FsMount *mount;
    GuestFilesystemInfoList *ret = NULL;
    Error *local_err = NULL;

    QTAILQ_INIT(&mounts);
    if (!build_fs_mount_list(&mounts, &local_err)) {
        error_propagate(errp, local_err);
        return NULL;
    }

    QTAILQ_FOREACH(mount, &mounts, next) {
        g_debug("Building guest fsinfo for '%s'", mount->dirname);

        QAPI_LIST_PREPEND(ret, build_guest_fsinfo(mount, &local_err));
        if (local_err) {
            error_propagate(errp, local_err);
            qapi_free_GuestFilesystemInfoList(ret);
            ret = NULL;
            break;
        }
    }

    free_fs_mount_list(&mounts);
    return ret;
}
#endif /* CONFIG_FSFREEZE */

#if defined(CONFIG_FSTRIM)
/*
 * Walk list of mounted file systems in the guest, and trim them.
 */
GuestFilesystemTrimResponse *
qmp_guest_fstrim(bool has_minimum, int64_t minimum, Error **errp)
{
    GuestFilesystemTrimResponse *response;
    GuestFilesystemTrimResult *result;
    int ret = 0;
    FsMountList mounts;
    struct FsMount *mount;
    int fd;
    struct fstrim_range r;

    slog("guest-fstrim called");

    QTAILQ_INIT(&mounts);
    if (!build_fs_mount_list(&mounts, errp)) {
        return NULL;
    }

    response = g_malloc0(sizeof(*response));

    QTAILQ_FOREACH(mount, &mounts, next) {
        result = g_malloc0(sizeof(*result));
        result->path = g_strdup(mount->dirname);

        QAPI_LIST_PREPEND(response->paths, result);

        fd = qga_open_cloexec(mount->dirname, O_RDONLY, 0);
        if (fd == -1) {
            result->error = g_strdup_printf("failed to open: %s",
                                            strerror(errno));
            continue;
        }

        /* We try to cull filesystems we know won't work in advance, but other
         * filesystems may not implement fstrim for less obvious reasons.
         * These will report EOPNOTSUPP; while in some other cases ENOTTY
         * will be reported (e.g. CD-ROMs).
         * Any other error means an unexpected error.
         */
        r.start = 0;
        r.len = -1;
        r.minlen = has_minimum ? minimum : 0;
        ret = ioctl(fd, FITRIM, &r);
        if (ret == -1) {
            if (errno == ENOTTY || errno == EOPNOTSUPP) {
                result->error = g_strdup("trim not supported");
            } else {
                result->error = g_strdup_printf("failed to trim: %s",
                                                strerror(errno));
            }
            close(fd);
            continue;
        }

        result->has_minimum = true;
        result->minimum = r.minlen;
        result->has_trimmed = true;
        result->trimmed = r.len;
        close(fd);
    }

    free_fs_mount_list(&mounts);
    return response;
}
#endif /* CONFIG_FSTRIM */

#define LINUX_SYS_STATE_FILE "/sys/power/state"
#define SUSPEND_SUPPORTED 0
#define SUSPEND_NOT_SUPPORTED 1

typedef enum {
    SUSPEND_MODE_DISK = 0,
    SUSPEND_MODE_RAM = 1,
    SUSPEND_MODE_HYBRID = 2,
} SuspendMode;

/*
 * Executes a command in a child process using g_spawn_sync,
 * returning an int >= 0 representing the exit status of the
 * process.
 *
 * If the program wasn't found in path, returns -1.
 *
 * If a problem happened when creating the child process,
 * returns -1 and errp is set.
 */
static int run_process_child(const char *command[], Error **errp)
{
    int exit_status, spawn_flag;
    GError *g_err = NULL;
    bool success;

    spawn_flag = G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL |
                 G_SPAWN_STDERR_TO_DEV_NULL;

    success =  g_spawn_sync(NULL, (char **)command, NULL, spawn_flag,
                            NULL, NULL, NULL, NULL,
                            &exit_status, &g_err);

    if (success) {
        return WEXITSTATUS(exit_status);
    }

    if (g_err && (g_err->code != G_SPAWN_ERROR_NOENT)) {
        error_setg(errp, "failed to create child process, error '%s'",
                   g_err->message);
    }

    g_error_free(g_err);
    return -1;
}

static bool systemd_supports_mode(SuspendMode mode, Error **errp)
{
    const char *systemctl_args[3] = {"systemd-hibernate", "systemd-suspend",
                                     "systemd-hybrid-sleep"};
    const char *cmd[4] = {"systemctl", "status", systemctl_args[mode], NULL};
    int status;

    status = run_process_child(cmd, errp);

    /*
     * systemctl status uses LSB return codes so we can expect
     * status > 0 and be ok. To assert if the guest has support
     * for the selected suspend mode, status should be < 4. 4 is
     * the code for unknown service status, the return value when
     * the service does not exist. A common value is status = 3
     * (program is not running).
     */
    if (status > 0 && status < 4) {
        return true;
    }

    return false;
}

static void systemd_suspend(SuspendMode mode, Error **errp)
{
    Error *local_err = NULL;
    const char *systemctl_args[3] = {"hibernate", "suspend", "hybrid-sleep"};
    const char *cmd[3] = {"systemctl", systemctl_args[mode], NULL};
    int status;

    status = run_process_child(cmd, &local_err);

    if (status == 0) {
        return;
    }

    if ((status == -1) && !local_err) {
        error_setg(errp, "the helper program 'systemctl %s' was not found",
                   systemctl_args[mode]);
        return;
    }

    if (local_err) {
        error_propagate(errp, local_err);
    } else {
        error_setg(errp, "the helper program 'systemctl %s' returned an "
                   "unexpected exit status code (%d)",
                   systemctl_args[mode], status);
    }
}

static bool pmutils_supports_mode(SuspendMode mode, Error **errp)
{
    Error *local_err = NULL;
    const char *pmutils_args[3] = {"--hibernate", "--suspend",
                                   "--suspend-hybrid"};
    const char *cmd[3] = {"pm-is-supported", pmutils_args[mode], NULL};
    int status;

    status = run_process_child(cmd, &local_err);

    if (status == SUSPEND_SUPPORTED) {
        return true;
    }

    if ((status == -1) && !local_err) {
        return false;
    }

    if (local_err) {
        error_propagate(errp, local_err);
    } else {
        error_setg(errp,
                   "the helper program '%s' returned an unexpected exit"
                   " status code (%d)", "pm-is-supported", status);
    }

    return false;
}

static void pmutils_suspend(SuspendMode mode, Error **errp)
{
    Error *local_err = NULL;
    const char *pmutils_binaries[3] = {"pm-hibernate", "pm-suspend",
                                       "pm-suspend-hybrid"};
    const char *cmd[2] = {pmutils_binaries[mode], NULL};
    int status;

    status = run_process_child(cmd, &local_err);

    if (status == 0) {
        return;
    }

    if ((status == -1) && !local_err) {
        error_setg(errp, "the helper program '%s' was not found",
                   pmutils_binaries[mode]);
        return;
    }

    if (local_err) {
        error_propagate(errp, local_err);
    } else {
        error_setg(errp,
                   "the helper program '%s' returned an unexpected exit"
                   " status code (%d)", pmutils_binaries[mode], status);
    }
}

static bool linux_sys_state_supports_mode(SuspendMode mode, Error **errp)
{
    const char *sysfile_strs[3] = {"disk", "mem", NULL};
    const char *sysfile_str = sysfile_strs[mode];
    char buf[32]; /* hopefully big enough */
    int fd;
    ssize_t ret;

    if (!sysfile_str) {
        error_setg(errp, "unknown guest suspend mode");
        return false;
    }

    fd = open(LINUX_SYS_STATE_FILE, O_RDONLY);
    if (fd < 0) {
        return false;
    }

    ret = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (ret <= 0) {
        return false;
    }
    buf[ret] = '\0';

    if (strstr(buf, sysfile_str)) {
        return true;
    }
    return false;
}

static void linux_sys_state_suspend(SuspendMode mode, Error **errp)
{
    const char *sysfile_strs[3] = {"disk", "mem", NULL};
    const char *sysfile_str = sysfile_strs[mode];
    int fd;

    if (!sysfile_str) {
        error_setg(errp, "unknown guest suspend mode");
        return;
    }

    fd = open(LINUX_SYS_STATE_FILE, O_WRONLY);
    if (fd < 0 || write(fd, sysfile_str, strlen(sysfile_str)) < 0) {
        error_setg(errp, "suspend: cannot write to '%s': %m",
                   LINUX_SYS_STATE_FILE);
    }
    if (fd >= 0) {
        close(fd);
    }
}

static void guest_suspend(SuspendMode mode, Error **errp)
{
    Error *local_err = NULL;
    bool mode_supported = false;

    if (systemd_supports_mode(mode, &local_err)) {
        mode_supported = true;
        systemd_suspend(mode, &local_err);

        if (!local_err) {
            return;
        }
    }

    error_free(local_err);
    local_err = NULL;

    if (pmutils_supports_mode(mode, &local_err)) {
        mode_supported = true;
        pmutils_suspend(mode, &local_err);

        if (!local_err) {
            return;
        }
    }

    error_free(local_err);
    local_err = NULL;

    if (linux_sys_state_supports_mode(mode, &local_err)) {
        mode_supported = true;
        linux_sys_state_suspend(mode, &local_err);
    }

    if (!mode_supported) {
        error_free(local_err);
        error_setg(errp,
                   "the requested suspend mode is not supported by the guest");
    } else {
        error_propagate(errp, local_err);
    }
}

void qmp_guest_suspend_disk(Error **errp)
{
    guest_suspend(SUSPEND_MODE_DISK, errp);
}

void qmp_guest_suspend_ram(Error **errp)
{
    guest_suspend(SUSPEND_MODE_RAM, errp);
}

void qmp_guest_suspend_hybrid(Error **errp)
{
    guest_suspend(SUSPEND_MODE_HYBRID, errp);
}

/* Transfer online/offline status between @vcpu and the guest system.
 *
 * On input either @errp or *@errp must be NULL.
 *
 * In system-to-@vcpu direction, the following @vcpu fields are accessed:
 * - R: vcpu->logical_id
 * - W: vcpu->online
 * - W: vcpu->can_offline
 *
 * In @vcpu-to-system direction, the following @vcpu fields are accessed:
 * - R: vcpu->logical_id
 * - R: vcpu->online
 *
 * Written members remain unmodified on error.
 */
static void transfer_vcpu(GuestLogicalProcessor *vcpu, bool sys2vcpu,
                          char *dirpath, Error **errp)
{
    int fd;
    int res;
    int dirfd;
    static const char fn[] = "online";

    dirfd = open(dirpath, O_RDONLY | O_DIRECTORY);
    if (dirfd == -1) {
        error_setg_errno(errp, errno, "open(\"%s\")", dirpath);
        return;
    }

    fd = openat(dirfd, fn, sys2vcpu ? O_RDONLY : O_RDWR);
    if (fd == -1) {
        if (errno != ENOENT) {
            error_setg_errno(errp, errno, "open(\"%s/%s\")", dirpath, fn);
        } else if (sys2vcpu) {
            vcpu->online = true;
            vcpu->can_offline = false;
        } else if (!vcpu->online) {
            error_setg(errp, "logical processor #%" PRId64 " can't be "
                       "offlined", vcpu->logical_id);
        } /* otherwise pretend successful re-onlining */
    } else {
        unsigned char status;

        res = pread(fd, &status, 1, 0);
        if (res == -1) {
            error_setg_errno(errp, errno, "pread(\"%s/%s\")", dirpath, fn);
        } else if (res == 0) {
            error_setg(errp, "pread(\"%s/%s\"): unexpected EOF", dirpath,
                       fn);
        } else if (sys2vcpu) {
            vcpu->online = (status != '0');
            vcpu->can_offline = true;
        } else if (vcpu->online != (status != '0')) {
            status = '0' + vcpu->online;
            if (pwrite(fd, &status, 1, 0) == -1) {
                error_setg_errno(errp, errno, "pwrite(\"%s/%s\")", dirpath,
                                 fn);
            }
        } /* otherwise pretend successful re-(on|off)-lining */

        res = close(fd);
        g_assert(res == 0);
    }

    res = close(dirfd);
    g_assert(res == 0);
}

GuestLogicalProcessorList *qmp_guest_get_vcpus(Error **errp)
{
    GuestLogicalProcessorList *head, **tail;
    const char *cpu_dir = "/sys/devices/system/cpu";
    const gchar *line;
    g_autoptr(GDir) cpu_gdir = NULL;
    Error *local_err = NULL;

    head = NULL;
    tail = &head;
    cpu_gdir = g_dir_open(cpu_dir, 0, NULL);

    if (cpu_gdir == NULL) {
        error_setg_errno(errp, errno, "failed to list entries: %s", cpu_dir);
        return NULL;
    }

    while (local_err == NULL && (line = g_dir_read_name(cpu_gdir)) != NULL) {
        GuestLogicalProcessor *vcpu;
        int64_t id;
        if (sscanf(line, "cpu%" PRId64, &id)) {
            g_autofree char *path = g_strdup_printf("/sys/devices/system/cpu/"
                                                    "cpu%" PRId64 "/", id);
            vcpu = g_malloc0(sizeof *vcpu);
            vcpu->logical_id = id;
            vcpu->has_can_offline = true; /* lolspeak ftw */
            transfer_vcpu(vcpu, true, path, &local_err);
            QAPI_LIST_APPEND(tail, vcpu);
        }
    }

    if (local_err == NULL) {
        /* there's no guest with zero VCPUs */
        g_assert(head != NULL);
        return head;
    }

    qapi_free_GuestLogicalProcessorList(head);
    error_propagate(errp, local_err);
    return NULL;
}

int64_t qmp_guest_set_vcpus(GuestLogicalProcessorList *vcpus, Error **errp)
{
    int64_t processed;
    Error *local_err = NULL;

    processed = 0;
    while (vcpus != NULL) {
        char *path = g_strdup_printf("/sys/devices/system/cpu/cpu%" PRId64 "/",
                                     vcpus->value->logical_id);

        transfer_vcpu(vcpus->value, false, path, &local_err);
        g_free(path);
        if (local_err != NULL) {
            break;
        }
        ++processed;
        vcpus = vcpus->next;
    }

    if (local_err != NULL) {
        if (processed == 0) {
            error_propagate(errp, local_err);
        } else {
            error_free(local_err);
        }
    }

    return processed;
}


static void ga_read_sysfs_file(int dirfd, const char *pathname, char *buf,
                               int size, Error **errp)
{
    int fd;
    int res;

    errno = 0;
    fd = openat(dirfd, pathname, O_RDONLY);
    if (fd == -1) {
        error_setg_errno(errp, errno, "open sysfs file \"%s\"", pathname);
        return;
    }

    res = pread(fd, buf, size, 0);
    if (res == -1) {
        error_setg_errno(errp, errno, "pread sysfs file \"%s\"", pathname);
    } else if (res == 0) {
        error_setg(errp, "pread sysfs file \"%s\": unexpected EOF", pathname);
    }
    close(fd);
}

static void ga_write_sysfs_file(int dirfd, const char *pathname,
                                const char *buf, int size, Error **errp)
{
    int fd;

    errno = 0;
    fd = openat(dirfd, pathname, O_WRONLY);
    if (fd == -1) {
        error_setg_errno(errp, errno, "open sysfs file \"%s\"", pathname);
        return;
    }

    if (pwrite(fd, buf, size, 0) == -1) {
        error_setg_errno(errp, errno, "pwrite sysfs file \"%s\"", pathname);
    }

    close(fd);
}

/* Transfer online/offline status between @mem_blk and the guest system.
 *
 * On input either @errp or *@errp must be NULL.
 *
 * In system-to-@mem_blk direction, the following @mem_blk fields are accessed:
 * - R: mem_blk->phys_index
 * - W: mem_blk->online
 * - W: mem_blk->can_offline
 *
 * In @mem_blk-to-system direction, the following @mem_blk fields are accessed:
 * - R: mem_blk->phys_index
 * - R: mem_blk->online
 *-  R: mem_blk->can_offline
 * Written members remain unmodified on error.
 */
static void transfer_memory_block(GuestMemoryBlock *mem_blk, bool sys2memblk,
                                  GuestMemoryBlockResponse *result,
                                  Error **errp)
{
    char *dirpath;
    int dirfd;
    char *status;
    Error *local_err = NULL;

    if (!sys2memblk) {
        DIR *dp;

        if (!result) {
            error_setg(errp, "Internal error, 'result' should not be NULL");
            return;
        }
        errno = 0;
        dp = opendir("/sys/devices/system/memory/");
         /* if there is no 'memory' directory in sysfs,
         * we think this VM does not support online/offline memory block,
         * any other solution?
         */
        if (!dp) {
            if (errno == ENOENT) {
                result->response =
                    GUEST_MEMORY_BLOCK_RESPONSE_TYPE_OPERATION_NOT_SUPPORTED;
            }
            goto out1;
        }
        closedir(dp);
    }

    dirpath = g_strdup_printf("/sys/devices/system/memory/memory%" PRId64 "/",
                              mem_blk->phys_index);
    dirfd = open(dirpath, O_RDONLY | O_DIRECTORY);
    if (dirfd == -1) {
        if (sys2memblk) {
            error_setg_errno(errp, errno, "open(\"%s\")", dirpath);
        } else {
            if (errno == ENOENT) {
                result->response = GUEST_MEMORY_BLOCK_RESPONSE_TYPE_NOT_FOUND;
            } else {
                result->response =
                    GUEST_MEMORY_BLOCK_RESPONSE_TYPE_OPERATION_FAILED;
            }
        }
        g_free(dirpath);
        goto out1;
    }
    g_free(dirpath);

    status = g_malloc0(10);
    ga_read_sysfs_file(dirfd, "state", status, 10, &local_err);
    if (local_err) {
        /* treat with sysfs file that not exist in old kernel */
        if (errno == ENOENT) {
            error_free(local_err);
            if (sys2memblk) {
                mem_blk->online = true;
                mem_blk->can_offline = false;
            } else if (!mem_blk->online) {
                result->response =
                    GUEST_MEMORY_BLOCK_RESPONSE_TYPE_OPERATION_NOT_SUPPORTED;
            }
        } else {
            if (sys2memblk) {
                error_propagate(errp, local_err);
            } else {
                error_free(local_err);
                result->response =
                    GUEST_MEMORY_BLOCK_RESPONSE_TYPE_OPERATION_FAILED;
            }
        }
        goto out2;
    }

    if (sys2memblk) {
        char removable = '0';

        mem_blk->online = (strncmp(status, "online", 6) == 0);

        ga_read_sysfs_file(dirfd, "removable", &removable, 1, &local_err);
        if (local_err) {
            /* if no 'removable' file, it doesn't support offline mem blk */
            if (errno == ENOENT) {
                error_free(local_err);
                mem_blk->can_offline = false;
            } else {
                error_propagate(errp, local_err);
            }
        } else {
            mem_blk->can_offline = (removable != '0');
        }
    } else {
        if (mem_blk->online != (strncmp(status, "online", 6) == 0)) {
            const char *new_state = mem_blk->online ? "online" : "offline";

            ga_write_sysfs_file(dirfd, "state", new_state, strlen(new_state),
                                &local_err);
            if (local_err) {
                error_free(local_err);
                result->response =
                    GUEST_MEMORY_BLOCK_RESPONSE_TYPE_OPERATION_FAILED;
                goto out2;
            }

            result->response = GUEST_MEMORY_BLOCK_RESPONSE_TYPE_SUCCESS;
            result->has_error_code = false;
        } /* otherwise pretend successful re-(on|off)-lining */
    }
    g_free(status);
    close(dirfd);
    return;

out2:
    g_free(status);
    close(dirfd);
out1:
    if (!sys2memblk) {
        result->has_error_code = true;
        result->error_code = errno;
    }
}

GuestMemoryBlockList *qmp_guest_get_memory_blocks(Error **errp)
{
    GuestMemoryBlockList *head, **tail;
    Error *local_err = NULL;
    struct dirent *de;
    DIR *dp;

    head = NULL;
    tail = &head;

    dp = opendir("/sys/devices/system/memory/");
    if (!dp) {
        /* it's ok if this happens to be a system that doesn't expose
         * memory blocks via sysfs, but otherwise we should report
         * an error
         */
        if (errno != ENOENT) {
            error_setg_errno(errp, errno, "Can't open directory"
                             "\"/sys/devices/system/memory/\"");
        }
        return NULL;
    }

    /* Note: the phys_index of memory block may be discontinuous,
     * this is because a memblk is the unit of the Sparse Memory design, which
     * allows discontinuous memory ranges (ex. NUMA), so here we should
     * traverse the memory block directory.
     */
    while ((de = readdir(dp)) != NULL) {
        GuestMemoryBlock *mem_blk;

        if ((strncmp(de->d_name, "memory", 6) != 0) ||
            !(de->d_type & DT_DIR)) {
            continue;
        }

        mem_blk = g_malloc0(sizeof *mem_blk);
        /* The d_name is "memoryXXX",  phys_index is block id, same as XXX */
        mem_blk->phys_index = strtoul(&de->d_name[6], NULL, 10);
        mem_blk->has_can_offline = true; /* lolspeak ftw */
        transfer_memory_block(mem_blk, true, NULL, &local_err);
        if (local_err) {
            break;
        }

        QAPI_LIST_APPEND(tail, mem_blk);
    }

    closedir(dp);
    if (local_err == NULL) {
        /* there's no guest with zero memory blocks */
        if (head == NULL) {
            error_setg(errp, "guest reported zero memory blocks!");
        }
        return head;
    }

    qapi_free_GuestMemoryBlockList(head);
    error_propagate(errp, local_err);
    return NULL;
}

GuestMemoryBlockResponseList *
qmp_guest_set_memory_blocks(GuestMemoryBlockList *mem_blks, Error **errp)
{
    GuestMemoryBlockResponseList *head, **tail;
    Error *local_err = NULL;

    head = NULL;
    tail = &head;

    while (mem_blks != NULL) {
        GuestMemoryBlockResponse *result;
        GuestMemoryBlock *current_mem_blk = mem_blks->value;

        result = g_malloc0(sizeof(*result));
        result->phys_index = current_mem_blk->phys_index;
        transfer_memory_block(current_mem_blk, false, result, &local_err);
        if (local_err) { /* should never happen */
            goto err;
        }

        QAPI_LIST_APPEND(tail, result);
        mem_blks = mem_blks->next;
    }

    return head;
err:
    qapi_free_GuestMemoryBlockResponseList(head);
    error_propagate(errp, local_err);
    return NULL;
}

GuestMemoryBlockInfo *qmp_guest_get_memory_block_info(Error **errp)
{
    Error *local_err = NULL;
    char *dirpath;
    int dirfd;
    char *buf;
    GuestMemoryBlockInfo *info;

    dirpath = g_strdup_printf("/sys/devices/system/memory/");
    dirfd = open(dirpath, O_RDONLY | O_DIRECTORY);
    if (dirfd == -1) {
        error_setg_errno(errp, errno, "open(\"%s\")", dirpath);
        g_free(dirpath);
        return NULL;
    }
    g_free(dirpath);

    buf = g_malloc0(20);
    ga_read_sysfs_file(dirfd, "block_size_bytes", buf, 20, &local_err);
    close(dirfd);
    if (local_err) {
        g_free(buf);
        error_propagate(errp, local_err);
        return NULL;
    }

    info = g_new0(GuestMemoryBlockInfo, 1);
    info->size = strtol(buf, NULL, 16); /* the unit is bytes */

    g_free(buf);

    return info;
}

#define MAX_NAME_LEN 128
static GuestDiskStatsInfoList *guest_get_diskstats(Error **errp)
{
    GuestDiskStatsInfoList *head = NULL, **tail = &head;
    const char *diskstats = "/proc/diskstats";
    FILE *fp;
    size_t n;
    char *line = NULL;

    fp = fopen(diskstats, "r");
    if (fp  == NULL) {
        error_setg_errno(errp, errno, "open(\"%s\")", diskstats);
        return NULL;
    }

    while (getline(&line, &n, fp) != -1) {
        g_autofree GuestDiskStatsInfo *diskstatinfo = NULL;
        g_autofree GuestDiskStats *diskstat = NULL;
        char dev_name[MAX_NAME_LEN];
        unsigned int ios_pgr, tot_ticks, rq_ticks, wr_ticks, dc_ticks, fl_ticks;
        unsigned long rd_ios, rd_merges_or_rd_sec, rd_ticks_or_wr_sec, wr_ios;
        unsigned long wr_merges, rd_sec_or_wr_ios, wr_sec;
        unsigned long dc_ios, dc_merges, dc_sec, fl_ios;
        unsigned int major, minor;
        int i;

        i = sscanf(line, "%u %u %s %lu %lu %lu"
                   "%lu %lu %lu %lu %u %u %u %u"
                   "%lu %lu %lu %u %lu %u",
                   &major, &minor, dev_name,
                   &rd_ios, &rd_merges_or_rd_sec, &rd_sec_or_wr_ios,
                   &rd_ticks_or_wr_sec, &wr_ios, &wr_merges, &wr_sec,
                   &wr_ticks, &ios_pgr, &tot_ticks, &rq_ticks,
                   &dc_ios, &dc_merges, &dc_sec, &dc_ticks,
                   &fl_ios, &fl_ticks);

        if (i < 7) {
            continue;
        }

        diskstatinfo = g_new0(GuestDiskStatsInfo, 1);
        diskstatinfo->name = g_strdup(dev_name);
        diskstatinfo->major = major;
        diskstatinfo->minor = minor;

        diskstat = g_new0(GuestDiskStats, 1);
        if (i == 7) {
            diskstat->has_read_ios = true;
            diskstat->read_ios = rd_ios;
            diskstat->has_read_sectors = true;
            diskstat->read_sectors = rd_merges_or_rd_sec;
            diskstat->has_write_ios = true;
            diskstat->write_ios = rd_sec_or_wr_ios;
            diskstat->has_write_sectors = true;
            diskstat->write_sectors = rd_ticks_or_wr_sec;
        }
        if (i >= 14) {
            diskstat->has_read_ios = true;
            diskstat->read_ios = rd_ios;
            diskstat->has_read_sectors = true;
            diskstat->read_sectors = rd_sec_or_wr_ios;
            diskstat->has_read_merges = true;
            diskstat->read_merges = rd_merges_or_rd_sec;
            diskstat->has_read_ticks = true;
            diskstat->read_ticks = rd_ticks_or_wr_sec;
            diskstat->has_write_ios = true;
            diskstat->write_ios = wr_ios;
            diskstat->has_write_sectors = true;
            diskstat->write_sectors = wr_sec;
            diskstat->has_write_merges = true;
            diskstat->write_merges = wr_merges;
            diskstat->has_write_ticks = true;
            diskstat->write_ticks = wr_ticks;
            diskstat->has_ios_pgr = true;
            diskstat->ios_pgr = ios_pgr;
            diskstat->has_total_ticks = true;
            diskstat->total_ticks = tot_ticks;
            diskstat->has_weight_ticks = true;
            diskstat->weight_ticks = rq_ticks;
        }
        if (i >= 18) {
            diskstat->has_discard_ios = true;
            diskstat->discard_ios = dc_ios;
            diskstat->has_discard_merges = true;
            diskstat->discard_merges = dc_merges;
            diskstat->has_discard_sectors = true;
            diskstat->discard_sectors = dc_sec;
            diskstat->has_discard_ticks = true;
            diskstat->discard_ticks = dc_ticks;
        }
        if (i >= 20) {
            diskstat->has_flush_ios = true;
            diskstat->flush_ios = fl_ios;
            diskstat->has_flush_ticks = true;
            diskstat->flush_ticks = fl_ticks;
        }

        diskstatinfo->stats = g_steal_pointer(&diskstat);
        QAPI_LIST_APPEND(tail, diskstatinfo);
        diskstatinfo = NULL;
    }
    free(line);
    fclose(fp);
    return head;
}

GuestDiskStatsInfoList *qmp_guest_get_diskstats(Error **errp)
{
    return guest_get_diskstats(errp);
}

GuestCpuStatsList *qmp_guest_get_cpustats(Error **errp)
{
    GuestCpuStatsList *head = NULL, **tail = &head;
    const char *cpustats = "/proc/stat";
    int clk_tck = sysconf(_SC_CLK_TCK);
    FILE *fp;
    size_t n;
    char *line = NULL;

    fp = fopen(cpustats, "r");
    if (fp  == NULL) {
        error_setg_errno(errp, errno, "open(\"%s\")", cpustats);
        return NULL;
    }

    while (getline(&line, &n, fp) != -1) {
        GuestCpuStats *cpustat = NULL;
        GuestLinuxCpuStats *linuxcpustat;
        int i;
        unsigned long user, system, idle, iowait, irq, softirq, steal, guest;
        unsigned long nice, guest_nice;
        char name[64];

        i = sscanf(line, "%s %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
                   name, &user, &nice, &system, &idle, &iowait, &irq, &softirq,
                   &steal, &guest, &guest_nice);

        /* drop "cpu 1 2 3 ...", get "cpuX 1 2 3 ..." only */
        if ((i == EOF) || strncmp(name, "cpu", 3) || (name[3] == '\0')) {
            continue;
        }

        if (i < 5) {
            slog("Parsing cpu stat from %s failed, see \"man proc\"", cpustats);
            break;
        }

        cpustat = g_new0(GuestCpuStats, 1);
        cpustat->type = GUEST_CPU_STATS_TYPE_LINUX;

        linuxcpustat = &cpustat->u.q_linux;
        linuxcpustat->cpu = atoi(&name[3]);
        linuxcpustat->user = user * 1000 / clk_tck;
        linuxcpustat->nice = nice * 1000 / clk_tck;
        linuxcpustat->system = system * 1000 / clk_tck;
        linuxcpustat->idle = idle * 1000 / clk_tck;

        if (i > 5) {
            linuxcpustat->has_iowait = true;
            linuxcpustat->iowait = iowait * 1000 / clk_tck;
        }

        if (i > 6) {
            linuxcpustat->has_irq = true;
            linuxcpustat->irq = irq * 1000 / clk_tck;
            linuxcpustat->has_softirq = true;
            linuxcpustat->softirq = softirq * 1000 / clk_tck;
        }

        if (i > 8) {
            linuxcpustat->has_steal = true;
            linuxcpustat->steal = steal * 1000 / clk_tck;
        }

        if (i > 9) {
            linuxcpustat->has_guest = true;
            linuxcpustat->guest = guest * 1000 / clk_tck;
        }

        if (i > 10) {
            linuxcpustat->has_guest = true;
            linuxcpustat->guest = guest * 1000 / clk_tck;
            linuxcpustat->has_guestnice = true;
            linuxcpustat->guestnice = guest_nice * 1000 / clk_tck;
        }

        QAPI_LIST_APPEND(tail, cpustat);
    }

    free(line);
    fclose(fp);
    return head;
}

static char *hex_to_ip_address(const void *hex_value, int is_ipv6)
{
    if (is_ipv6) {
        char addr[INET6_ADDRSTRLEN];
        struct in6_addr in6;
        const char *hex_str = (const char *)hex_value;
        int i;

        for (i = 0; i < 16; i++) {
            if (sscanf(&hex_str[i * 2], "%02hhx", &in6.s6_addr[i]) != 1) {
                return NULL;
            }
        }
        inet_ntop(AF_INET6, &in6, addr, INET6_ADDRSTRLEN);

        return g_strdup(addr);
    } else {
        unsigned int hex_int = *(unsigned int *)hex_value;
        unsigned int byte1 = (hex_int >> 24) & 0xFF;
        unsigned int byte2 = (hex_int >> 16) & 0xFF;
        unsigned int byte3 = (hex_int >> 8) & 0xFF;
        unsigned int byte4 = hex_int & 0xFF;

        return g_strdup_printf("%u.%u.%u.%u", byte4, byte3, byte2, byte1);
    }
}

GuestNetworkRouteList *qmp_guest_network_get_route(Error **errp)
{
    GuestNetworkRouteList *head = NULL, **tail = &head;
    const char *route_files[] = {"/proc/net/route", "/proc/net/ipv6_route"};
    FILE *fp;
    size_t n = 0;
    char *line = NULL;
    int firstLine;
    int is_ipv6;
    int i;
    char iface[IFNAMSIZ];

    for (i = 0; i < 2; i++) {
        firstLine = 1;
        is_ipv6 = (i == 1);
        fp = fopen(route_files[i], "r");
        if (fp == NULL) {
            error_setg_errno(errp, errno, "open(\"%s\")", route_files[i]);
            continue;
        }

        while (getline(&line, &n, fp) != -1) {
            if (firstLine && !is_ipv6) {
                firstLine = 0;
                continue;
            }
            g_autoptr(GuestNetworkRoute) route = g_new0(GuestNetworkRoute, 1);

            if (is_ipv6) {
                char destination[33], source[33], next_hop[33];
                int des_prefixlen, src_prefixlen, metric, refcnt, use, flags;
                if (sscanf(line, "%32s %x %32s %x %32s %x %x %x %x %s",
                           destination, &des_prefixlen, source,
                           &src_prefixlen, next_hop, &metric, &refcnt,
                           &use, &flags, iface) != 10) {
                    continue;
                }

                route->destination = hex_to_ip_address(destination, 1);
                if (route->destination == NULL) {
                    continue;
                }
                route->iface = g_strdup(iface);
                route->source = hex_to_ip_address(source, 1);
                route->nexthop = hex_to_ip_address(next_hop, 1);
                route->desprefixlen = g_strdup_printf("%d", des_prefixlen);
                route->srcprefixlen = g_strdup_printf("%d", src_prefixlen);
                route->metric = metric;
                route->has_flags = true;
                route->flags = flags;
                route->has_refcnt = true;
                route->refcnt = refcnt;
                route->has_use = true;
                route->use = use;
                route->version = 6;
            } else {
                unsigned int destination, gateway, mask, flags;
                int refcnt, use, metric, mtu, window, irtt;
                if (sscanf(line, "%s %X %X %x %d %d %d %X %d %d %d",
                           iface, &destination, &gateway, &flags, &refcnt,
                           &use, &metric, &mask, &mtu, &window, &irtt) != 11) {
                    continue;
                }

                route->destination = hex_to_ip_address(&destination, 0);
                if (route->destination == NULL) {
                    continue;
                }
                route->iface = g_strdup(iface);
                route->gateway = hex_to_ip_address(&gateway, 0);
                route->mask = hex_to_ip_address(&mask, 0);
                route->metric = metric;
                route->has_flags = true;
                route->flags = flags;
                route->has_refcnt = true;
                route->refcnt = refcnt;
                route->has_use = true;
                route->use = use;
                route->has_mtu = true;
                route->mtu = mtu;
                route->has_window = true;
                route->window = window;
                route->has_irtt = true;
                route->irtt = irtt;
                route->version = 4;
            }

            QAPI_LIST_APPEND(tail, route);
            route = NULL;
        }

        fclose(fp);
    }

    free(line);
    return head;
}
