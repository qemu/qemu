/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2009 Red Hat, Inc.
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

#include "net/tap.h"
#include "net/tap-linux.h"

#include <net/if.h>
#include <sys/ioctl.h>

#include "sysemu.h"
#include "qemu-common.h"
#include "qemu-error.h"

#define PATH_NET_TUN "/dev/net/tun"

int tap_open(char *ifname, int ifname_size, int *vnet_hdr, int vnet_hdr_required)
{
    struct ifreq ifr;
    int fd, ret;

    TFR(fd = open(PATH_NET_TUN, O_RDWR));
    if (fd < 0) {
        error_report("could not open %s: %m", PATH_NET_TUN);
        return -1;
    }
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;

    if (*vnet_hdr) {
        unsigned int features;

        if (ioctl(fd, TUNGETFEATURES, &features) == 0 &&
            features & IFF_VNET_HDR) {
            *vnet_hdr = 1;
            ifr.ifr_flags |= IFF_VNET_HDR;
        } else {
            *vnet_hdr = 0;
        }

        if (vnet_hdr_required && !*vnet_hdr) {
            error_report("vnet_hdr=1 requested, but no kernel "
                         "support for IFF_VNET_HDR available");
            close(fd);
            return -1;
        }
    }

    if (ifname[0] != '\0')
        pstrcpy(ifr.ifr_name, IFNAMSIZ, ifname);
    else
        pstrcpy(ifr.ifr_name, IFNAMSIZ, "tap%d");
    ret = ioctl(fd, TUNSETIFF, (void *) &ifr);
    if (ret != 0) {
        error_report("could not configure %s (%s): %m", PATH_NET_TUN, ifr.ifr_name);
        close(fd);
        return -1;
    }
    pstrcpy(ifname, ifname_size, ifr.ifr_name);
    fcntl(fd, F_SETFL, O_NONBLOCK);
    return fd;
}

/* sndbuf implements a kind of flow control for tap.
 * Unfortunately when it's enabled, and packets are sent
 * to other guests on the same host, the receiver
 * can lock up the transmitter indefinitely.
 *
 * To avoid packet loss, sndbuf should be set to a value lower than the tx
 * queue capacity of any destination network interface.
 * Ethernet NICs generally have txqueuelen=1000, so 1Mb is
 * a good value, given a 1500 byte MTU.
 */
#define TAP_DEFAULT_SNDBUF 0

int tap_set_sndbuf(int fd, QemuOpts *opts)
{
    int sndbuf;

    sndbuf = qemu_opt_get_size(opts, "sndbuf", TAP_DEFAULT_SNDBUF);
    if (!sndbuf) {
        sndbuf = INT_MAX;
    }

    if (ioctl(fd, TUNSETSNDBUF, &sndbuf) == -1 && qemu_opt_get(opts, "sndbuf")) {
        error_report("TUNSETSNDBUF ioctl failed: %s", strerror(errno));
        return -1;
    }
    return 0;
}

int tap_probe_vnet_hdr(int fd)
{
    struct ifreq ifr;

    if (ioctl(fd, TUNGETIFF, &ifr) != 0) {
        error_report("TUNGETIFF ioctl() failed: %s", strerror(errno));
        return 0;
    }

    return ifr.ifr_flags & IFF_VNET_HDR;
}

int tap_probe_has_ufo(int fd)
{
    unsigned offload;

    offload = TUN_F_CSUM | TUN_F_UFO;

    if (ioctl(fd, TUNSETOFFLOAD, offload) < 0)
        return 0;

    return 1;
}

/* Verify that we can assign given length */
int tap_probe_vnet_hdr_len(int fd, int len)
{
    int orig;
    if (ioctl(fd, TUNGETVNETHDRSZ, &orig) == -1) {
        return 0;
    }
    if (ioctl(fd, TUNSETVNETHDRSZ, &len) == -1) {
        return 0;
    }
    /* Restore original length: we can't handle failure. */
    if (ioctl(fd, TUNSETVNETHDRSZ, &orig) == -1) {
        fprintf(stderr, "TUNGETVNETHDRSZ ioctl() failed: %s. Exiting.\n",
                strerror(errno));
        assert(0);
        return -errno;
    }
    return 1;
}

void tap_fd_set_vnet_hdr_len(int fd, int len)
{
    if (ioctl(fd, TUNSETVNETHDRSZ, &len) == -1) {
        fprintf(stderr, "TUNSETVNETHDRSZ ioctl() failed: %s. Exiting.\n",
                strerror(errno));
        assert(0);
    }
}

void tap_fd_set_offload(int fd, int csum, int tso4,
                        int tso6, int ecn, int ufo)
{
    unsigned int offload = 0;

    /* Check if our kernel supports TUNSETOFFLOAD */
    if (ioctl(fd, TUNSETOFFLOAD, 0) != 0 && errno == EINVAL) {
        return;
    }

    if (csum) {
        offload |= TUN_F_CSUM;
        if (tso4)
            offload |= TUN_F_TSO4;
        if (tso6)
            offload |= TUN_F_TSO6;
        if ((tso4 || tso6) && ecn)
            offload |= TUN_F_TSO_ECN;
        if (ufo)
            offload |= TUN_F_UFO;
    }

    if (ioctl(fd, TUNSETOFFLOAD, offload) != 0) {
        offload &= ~TUN_F_UFO;
        if (ioctl(fd, TUNSETOFFLOAD, offload) != 0) {
            fprintf(stderr, "TUNSETOFFLOAD ioctl() failed: %s\n",
                    strerror(errno));
        }
    }
}
