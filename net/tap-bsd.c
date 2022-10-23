/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "tap_int.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"

#if defined(__NetBSD__) || defined(__FreeBSD__)
#include <sys/ioctl.h>
#include <net/if.h>
#include <net/if_tap.h>
#endif

#ifndef __FreeBSD__
int tap_open(char *ifname, int ifname_size, int *vnet_hdr,
             int vnet_hdr_required, int mq_required, Error **errp)
{
    int fd;
#ifdef TAPGIFNAME
    struct ifreq ifr;
#else
    char *dev;
    struct stat s;
#endif

    /* if no ifname is given, always start the search from tap0/tun0. */
    int i;
    char dname[100];

    for (i = 0; i < 10; i++) {
        if (*ifname) {
            snprintf(dname, sizeof dname, "/dev/%s", ifname);
        } else {
            snprintf(dname, sizeof dname, "/dev/tap%d", i);
        }
        fd = RETRY_ON_EINTR(open(dname, O_RDWR));
        if (fd >= 0) {
            break;
        }
        else if (errno == ENXIO || errno == ENOENT) {
            break;
        }
        if (*ifname) {
            break;
        }
    }
    if (fd < 0) {
        error_setg_errno(errp, errno, "could not open %s", dname);
        return -1;
    }

#ifdef TAPGIFNAME
    if (ioctl(fd, TAPGIFNAME, (void *)&ifr) < 0) {
        error_setg_errno(errp, errno, "could not get tap name");
        return -1;
    }
    pstrcpy(ifname, ifname_size, ifr.ifr_name);
#else
    if (fstat(fd, &s) < 0) {
        error_setg_errno(errp, errno, "could not stat %s", dname);
        return -1;
    }
    dev = devname(s.st_rdev, S_IFCHR);
    pstrcpy(ifname, ifname_size, dev);
#endif

    if (*vnet_hdr) {
        /* BSD doesn't have IFF_VNET_HDR */
        *vnet_hdr = 0;

        if (vnet_hdr_required && !*vnet_hdr) {
            error_setg(errp, "vnet_hdr=1 requested, but no kernel "
                       "support for IFF_VNET_HDR available");
            close(fd);
            return -1;
        }
    }
    g_unix_set_fd_nonblocking(fd, true, NULL);
    return fd;
}

#else /* __FreeBSD__ */

#define PATH_NET_TAP "/dev/tap"

static int tap_open_clone(char *ifname, int ifname_size, Error **errp)
{
    int fd, s, ret;
    struct ifreq ifr;

    fd = RETRY_ON_EINTR(open(PATH_NET_TAP, O_RDWR));
    if (fd < 0) {
        error_setg_errno(errp, errno, "could not open %s", PATH_NET_TAP);
        return -1;
    }

    memset(&ifr, 0, sizeof(ifr));

    ret = ioctl(fd, TAPGIFNAME, (void *)&ifr);
    if (ret < 0) {
        error_setg_errno(errp, errno, "could not get tap interface name");
        close(fd);
        return -1;
    }

    if (ifname[0] != '\0') {
        /* User requested the interface to have a specific name */
        s = socket(AF_LOCAL, SOCK_DGRAM, 0);
        if (s < 0) {
            error_setg_errno(errp, errno,
                             "could not open socket to set interface name");
            close(fd);
            return -1;
        }
        ifr.ifr_data = ifname;
        ret = ioctl(s, SIOCSIFNAME, (void *)&ifr);
        close(s);
        if (ret < 0) {
            error_setg(errp, "could not set tap interface name");
            close(fd);
            return -1;
        }
    } else {
        pstrcpy(ifname, ifname_size, ifr.ifr_name);
    }

    return fd;
}

int tap_open(char *ifname, int ifname_size, int *vnet_hdr,
             int vnet_hdr_required, int mq_required, Error **errp)
{
    int fd = -1;

    /* If the specified tap device already exists just use it. */
    if (ifname[0] != '\0') {
        char dname[100];
        snprintf(dname, sizeof dname, "/dev/%s", ifname);
        fd = RETRY_ON_EINTR(open(dname, O_RDWR));
        if (fd < 0 && errno != ENOENT) {
            error_setg_errno(errp, errno, "could not open %s", dname);
            return -1;
        }
    }

    if (fd < 0) {
        /* Tap device not specified or does not exist. */
        if ((fd = tap_open_clone(ifname, ifname_size, errp)) < 0) {
            return -1;
        }
    }

    if (*vnet_hdr) {
        /* BSD doesn't have IFF_VNET_HDR */
        *vnet_hdr = 0;

        if (vnet_hdr_required && !*vnet_hdr) {
            error_setg(errp, "vnet_hdr=1 requested, but no kernel "
                       "support for IFF_VNET_HDR available");
            goto error;
        }
    }
    if (mq_required) {
        error_setg(errp, "mq_required requested, but no kernel support"
                   " for IFF_MULTI_QUEUE available");
        goto error;
    }

    g_unix_set_fd_nonblocking(fd, true, NULL);
    return fd;

error:
    close(fd);
    return -1;
}
#endif /* __FreeBSD__ */

void tap_set_sndbuf(int fd, const NetdevTapOptions *tap, Error **errp)
{
}

int tap_probe_vnet_hdr(int fd, Error **errp)
{
    return 0;
}

int tap_probe_has_ufo(int fd)
{
    return 0;
}

int tap_probe_vnet_hdr_len(int fd, int len)
{
    return 0;
}

void tap_fd_set_vnet_hdr_len(int fd, int len)
{
}

int tap_fd_set_vnet_le(int fd, int is_le)
{
    return -EINVAL;
}

int tap_fd_set_vnet_be(int fd, int is_be)
{
    return -EINVAL;
}

void tap_fd_set_offload(int fd, int csum, int tso4,
                        int tso6, int ecn, int ufo)
{
}

int tap_fd_enable(int fd)
{
    return -1;
}

int tap_fd_disable(int fd)
{
    return -1;
}

int tap_fd_get_ifname(int fd, char *ifname)
{
    return -1;
}

int tap_fd_set_steering_ebpf(int fd, int prog_fd)
{
    return -1;
}
