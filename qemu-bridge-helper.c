/*
 * QEMU Bridge Helper
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 * Anthony Liguori   <aliguori@us.ibm.com>
 * Richa Marwaha     <rmarwah@linux.vnet.ibm.com>
 * Corey Bryant      <coreyb@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "config-host.h"

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/prctl.h>

#include <net/if.h>

#include <linux/sockios.h>

#include "net/tap-linux.h"

static void usage(void)
{
    fprintf(stderr,
            "Usage: qemu-bridge-helper [--use-vnet] --br=bridge --fd=unixfd\n");
}

static bool has_vnet_hdr(int fd)
{
    unsigned int features = 0;

    if (ioctl(fd, TUNGETFEATURES, &features) == -1) {
        return false;
    }

    if (!(features & IFF_VNET_HDR)) {
        return false;
    }

    return true;
}

static void prep_ifreq(struct ifreq *ifr, const char *ifname)
{
    memset(ifr, 0, sizeof(*ifr));
    snprintf(ifr->ifr_name, IFNAMSIZ, "%s", ifname);
}

static int send_fd(int c, int fd)
{
    char msgbuf[CMSG_SPACE(sizeof(fd))];
    struct msghdr msg = {
        .msg_control = msgbuf,
        .msg_controllen = sizeof(msgbuf),
    };
    struct cmsghdr *cmsg;
    struct iovec iov;
    char req[1] = { 0x00 };

    cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(fd));
    msg.msg_controllen = cmsg->cmsg_len;

    iov.iov_base = req;
    iov.iov_len = sizeof(req);

    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    memcpy(CMSG_DATA(cmsg), &fd, sizeof(fd));

    return sendmsg(c, &msg, 0);
}

int main(int argc, char **argv)
{
    struct ifreq ifr;
    int fd, ctlfd, unixfd = -1;
    int use_vnet = 0;
    int mtu;
    const char *bridge = NULL;
    char iface[IFNAMSIZ];
    int index;
    int ret = EXIT_SUCCESS;

    /* parse arguments */
    for (index = 1; index < argc; index++) {
        if (strcmp(argv[index], "--use-vnet") == 0) {
            use_vnet = 1;
        } else if (strncmp(argv[index], "--br=", 5) == 0) {
            bridge = &argv[index][5];
        } else if (strncmp(argv[index], "--fd=", 5) == 0) {
            unixfd = atoi(&argv[index][5]);
        } else {
            usage();
            return EXIT_FAILURE;
        }
    }

    if (bridge == NULL || unixfd == -1) {
        usage();
        return EXIT_FAILURE;
    }

    /* open a socket to use to control the network interfaces */
    ctlfd = socket(AF_INET, SOCK_STREAM, 0);
    if (ctlfd == -1) {
        fprintf(stderr, "failed to open control socket: %s\n", strerror(errno));
        ret = EXIT_FAILURE;
        goto cleanup;
    }

    /* open the tap device */
    fd = open("/dev/net/tun", O_RDWR);
    if (fd == -1) {
        fprintf(stderr, "failed to open /dev/net/tun: %s\n", strerror(errno));
        ret = EXIT_FAILURE;
        goto cleanup;
    }

    /* request a tap device, disable PI, and add vnet header support if
     * requested and it's available. */
    prep_ifreq(&ifr, "tap%d");
    ifr.ifr_flags = IFF_TAP|IFF_NO_PI;
    if (use_vnet && has_vnet_hdr(fd)) {
        ifr.ifr_flags |= IFF_VNET_HDR;
    }

    if (ioctl(fd, TUNSETIFF, &ifr) == -1) {
        fprintf(stderr, "failed to create tun device: %s\n", strerror(errno));
        ret = EXIT_FAILURE;
        goto cleanup;
    }

    /* save tap device name */
    snprintf(iface, sizeof(iface), "%s", ifr.ifr_name);

    /* get the mtu of the bridge */
    prep_ifreq(&ifr, bridge);
    if (ioctl(ctlfd, SIOCGIFMTU, &ifr) == -1) {
        fprintf(stderr, "failed to get mtu of bridge `%s': %s\n",
                bridge, strerror(errno));
        ret = EXIT_FAILURE;
        goto cleanup;
    }

    /* save mtu */
    mtu = ifr.ifr_mtu;

    /* set the mtu of the interface based on the bridge */
    prep_ifreq(&ifr, iface);
    ifr.ifr_mtu = mtu;
    if (ioctl(ctlfd, SIOCSIFMTU, &ifr) == -1) {
        fprintf(stderr, "failed to set mtu of device `%s' to %d: %s\n",
                iface, mtu, strerror(errno));
        ret = EXIT_FAILURE;
        goto cleanup;
    }

    /* add the interface to the bridge */
    prep_ifreq(&ifr, bridge);
    ifr.ifr_ifindex = if_nametoindex(iface);

    if (ioctl(ctlfd, SIOCBRADDIF, &ifr) == -1) {
        fprintf(stderr, "failed to add interface `%s' to bridge `%s': %s\n",
                iface, bridge, strerror(errno));
        ret = EXIT_FAILURE;
        goto cleanup;
    }

    /* bring the interface up */
    prep_ifreq(&ifr, iface);
    if (ioctl(ctlfd, SIOCGIFFLAGS, &ifr) == -1) {
        fprintf(stderr, "failed to get interface flags for `%s': %s\n",
                iface, strerror(errno));
        ret = EXIT_FAILURE;
        goto cleanup;
    }

    ifr.ifr_flags |= IFF_UP;
    if (ioctl(ctlfd, SIOCSIFFLAGS, &ifr) == -1) {
        fprintf(stderr, "failed to bring up interface `%s': %s\n",
                iface, strerror(errno));
        ret = EXIT_FAILURE;
        goto cleanup;
    }

    /* write fd to the domain socket */
    if (send_fd(unixfd, fd) == -1) {
        fprintf(stderr, "failed to write fd to unix socket: %s\n",
                strerror(errno));
        ret = EXIT_FAILURE;
        goto cleanup;
    }

    /* ... */

    /* profit! */

cleanup:

    return ret;
}
