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

#include "qemu/osdep.h"


#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/prctl.h>

#include <net/if.h>

#include <linux/sockios.h>

#ifndef SIOCBRADDIF
#include <linux/if_bridge.h>
#endif

#include "qemu/queue.h"

#include "net/tap-linux.h"

#ifdef CONFIG_LIBCAP
#include <cap-ng.h>
#endif

#define DEFAULT_ACL_FILE CONFIG_QEMU_CONFDIR "/bridge.conf"

enum {
    ACL_ALLOW = 0,
    ACL_ALLOW_ALL,
    ACL_DENY,
    ACL_DENY_ALL,
};

typedef struct ACLRule {
    int type;
    char iface[IFNAMSIZ];
    QSIMPLEQ_ENTRY(ACLRule) entry;
} ACLRule;

typedef QSIMPLEQ_HEAD(ACLList, ACLRule) ACLList;

static void usage(void)
{
    fprintf(stderr,
            "Usage: qemu-bridge-helper [--use-vnet] --br=bridge --fd=unixfd\n");
}

static int parse_acl_file(const char *filename, ACLList *acl_list)
{
    FILE *f;
    char line[4096];
    ACLRule *acl_rule;

    f = fopen(filename, "r");
    if (f == NULL) {
        return -1;
    }

    while (fgets(line, sizeof(line), f) != NULL) {
        char *ptr = line;
        char *cmd, *arg, *argend;

        while (isspace(*ptr)) {
            ptr++;
        }

        /* skip comments and empty lines */
        if (*ptr == '#' || *ptr == 0) {
            continue;
        }

        cmd = ptr;
        arg = strchr(cmd, ' ');
        if (arg == NULL) {
            arg = strchr(cmd, '\t');
        }

        if (arg == NULL) {
            fprintf(stderr, "Invalid config line:\n  %s\n", line);
            fclose(f);
            errno = EINVAL;
            return -1;
        }

        *arg = 0;
        arg++;
        while (isspace(*arg)) {
            arg++;
        }

        argend = arg + strlen(arg);
        while (arg != argend && isspace(*(argend - 1))) {
            argend--;
        }
        *argend = 0;

        if (strcmp(cmd, "deny") == 0) {
            acl_rule = g_malloc(sizeof(*acl_rule));
            if (strcmp(arg, "all") == 0) {
                acl_rule->type = ACL_DENY_ALL;
            } else {
                acl_rule->type = ACL_DENY;
                snprintf(acl_rule->iface, IFNAMSIZ, "%s", arg);
            }
            QSIMPLEQ_INSERT_TAIL(acl_list, acl_rule, entry);
        } else if (strcmp(cmd, "allow") == 0) {
            acl_rule = g_malloc(sizeof(*acl_rule));
            if (strcmp(arg, "all") == 0) {
                acl_rule->type = ACL_ALLOW_ALL;
            } else {
                acl_rule->type = ACL_ALLOW;
                snprintf(acl_rule->iface, IFNAMSIZ, "%s", arg);
            }
            QSIMPLEQ_INSERT_TAIL(acl_list, acl_rule, entry);
        } else if (strcmp(cmd, "include") == 0) {
            /* ignore errors */
            parse_acl_file(arg, acl_list);
        } else {
            fprintf(stderr, "Unknown command `%s'\n", cmd);
            fclose(f);
            errno = EINVAL;
            return -1;
        }
    }

    fclose(f);

    return 0;
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

#ifdef CONFIG_LIBCAP
static int drop_privileges(void)
{
    /* clear all capabilities */
    capng_clear(CAPNG_SELECT_BOTH);

    if (capng_update(CAPNG_ADD, CAPNG_EFFECTIVE | CAPNG_PERMITTED,
                     CAP_NET_ADMIN) < 0) {
        return -1;
    }

    /* change to calling user's real uid and gid, retaining supplemental
     * groups and CAP_NET_ADMIN */
    if (capng_change_id(getuid(), getgid(), CAPNG_CLEAR_BOUNDING)) {
        return -1;
    }

    return 0;
}
#endif

int main(int argc, char **argv)
{
    struct ifreq ifr;
#ifndef SIOCBRADDIF
    unsigned long ifargs[4];
#endif
    int ifindex;
    int fd = -1, ctlfd = -1, unixfd = -1;
    int use_vnet = 0;
    int mtu;
    const char *bridge = NULL;
    char iface[IFNAMSIZ];
    int index;
    ACLRule *acl_rule;
    ACLList acl_list;
    int access_allowed, access_denied;
    int ret = EXIT_SUCCESS;

#ifdef CONFIG_LIBCAP
    /* if we're run from an suid binary, immediately drop privileges preserving
     * cap_net_admin */
    if (geteuid() == 0 && getuid() != geteuid()) {
        if (drop_privileges() == -1) {
            fprintf(stderr, "failed to drop privileges\n");
            return 1;
        }
    }
#endif

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

    /* parse default acl file */
    QSIMPLEQ_INIT(&acl_list);
    if (parse_acl_file(DEFAULT_ACL_FILE, &acl_list) == -1) {
        fprintf(stderr, "failed to parse default acl file `%s'\n",
                DEFAULT_ACL_FILE);
        ret = EXIT_FAILURE;
        goto cleanup;
    }

    /* validate bridge against acl -- default policy is to deny
     * according acl policy if we have a deny and allow both
     * then deny should always win over allow
     */
    access_allowed = 0;
    access_denied = 0;
    QSIMPLEQ_FOREACH(acl_rule, &acl_list, entry) {
        switch (acl_rule->type) {
        case ACL_ALLOW_ALL:
            access_allowed = 1;
            break;
        case ACL_ALLOW:
            if (strcmp(bridge, acl_rule->iface) == 0) {
                access_allowed = 1;
            }
            break;
        case ACL_DENY_ALL:
            access_denied = 1;
            break;
        case ACL_DENY:
            if (strcmp(bridge, acl_rule->iface) == 0) {
                access_denied = 1;
            }
            break;
        }
    }

    if ((access_allowed == 0) || (access_denied == 1)) {
        fprintf(stderr, "access denied by acl file\n");
        ret = EXIT_FAILURE;
        goto cleanup;
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

    /* Linux uses the lowest enslaved MAC address as the MAC address of
     * the bridge.  Set MAC address to a high value so that it doesn't
     * affect the MAC address of the bridge.
     */
    if (ioctl(ctlfd, SIOCGIFHWADDR, &ifr) < 0) {
        fprintf(stderr, "failed to get MAC address of device `%s': %s\n",
                iface, strerror(errno));
        ret = EXIT_FAILURE;
        goto cleanup;
    }
    ifr.ifr_hwaddr.sa_data[0] = 0xFE;
    if (ioctl(ctlfd, SIOCSIFHWADDR, &ifr) < 0) {
        fprintf(stderr, "failed to set MAC address of device `%s': %s\n",
                iface, strerror(errno));
        ret = EXIT_FAILURE;
        goto cleanup;
    }

    /* add the interface to the bridge */
    prep_ifreq(&ifr, bridge);
    ifindex = if_nametoindex(iface);
#ifndef SIOCBRADDIF
    ifargs[0] = BRCTL_ADD_IF;
    ifargs[1] = ifindex;
    ifargs[2] = 0;
    ifargs[3] = 0;
    ifr.ifr_data = (void *)ifargs;
    ret = ioctl(ctlfd, SIOCDEVPRIVATE, &ifr);
#else
    ifr.ifr_ifindex = ifindex;
    ret = ioctl(ctlfd, SIOCBRADDIF, &ifr);
#endif
    if (ret == -1) {
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
    if (fd >= 0) {
        close(fd);
    }
    if (ctlfd >= 0) {
        close(ctlfd);
    }
    while ((acl_rule = QSIMPLEQ_FIRST(&acl_list)) != NULL) {
        QSIMPLEQ_REMOVE_HEAD(&acl_list, entry);
        g_free(acl_rule);
    }

    return ret;
}
