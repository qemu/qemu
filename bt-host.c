/*
 * Wrap a host Bluetooth HCI socket in a struct HCIInfo.
 *
 * Copyright (C) 2008 Andrzej Zaborowski  <balrog@zabor.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu-common.h"
#include "qemu-char.h"
#include "net.h"
#include "bt-host.h"

#ifndef _WIN32
# include <errno.h>
# include <sys/ioctl.h>
# include <sys/uio.h>
# ifdef CONFIG_BLUEZ
#  include <bluetooth/bluetooth.h>
#  include <bluetooth/hci.h>
#  include <bluetooth/hci_lib.h>
# else
#  include "hw/bt.h"
#  define HCI_MAX_FRAME_SIZE	1028
# endif

struct bt_host_hci_s {
    struct HCIInfo hci;
    int fd;

    uint8_t hdr[HCI_MAX_FRAME_SIZE];
    int len;
};

static void bt_host_send(struct HCIInfo *hci,
                int type, const uint8_t *data, int len)
{
    struct bt_host_hci_s *s = (struct bt_host_hci_s *) hci;
    uint8_t pkt = type;
    struct iovec iv[2];

    iv[0].iov_base = (void *)&pkt;
    iv[0].iov_len  = 1;
    iv[1].iov_base = (void *) data;
    iv[1].iov_len  = len;

    while (writev(s->fd, iv, 2) < 0) {
        if (errno != EAGAIN && errno != EINTR) {
            fprintf(stderr, "qemu: error %i writing bluetooth packet.\n",
                            errno);
            return;
        }
    }
}

static void bt_host_cmd(struct HCIInfo *hci, const uint8_t *data, int len)
{
    bt_host_send(hci, HCI_COMMAND_PKT, data, len);
}

static void bt_host_acl(struct HCIInfo *hci, const uint8_t *data, int len)
{
    bt_host_send(hci, HCI_ACLDATA_PKT, data, len);
}

static void bt_host_sco(struct HCIInfo *hci, const uint8_t *data, int len)
{
    bt_host_send(hci, HCI_SCODATA_PKT, data, len);
}

static void bt_host_read(void *opaque)
{
    struct bt_host_hci_s *s = (struct bt_host_hci_s *) opaque;
    uint8_t *pkt;
    int pktlen;

    /* Seems that we can't read only the header first and then the amount
     * of data indicated in the header because Linux will discard everything
     * that's not been read in one go.  */
    s->len = read(s->fd, s->hdr, sizeof(s->hdr));

    if (s->len < 0) {
        fprintf(stderr, "qemu: error %i reading HCI frame\n", errno);
        return;
    }

    pkt = s->hdr;
    while (s->len --)
        switch (*pkt ++) {
        case HCI_EVENT_PKT:
            if (s->len < 2)
                goto bad_pkt;

            pktlen = MIN(pkt[1] + 2, s->len);
            s->hci.evt_recv(s->hci.opaque, pkt, pktlen);
            s->len -= pktlen;
            pkt += pktlen;

            /* TODO: if this is an Inquiry Result event, it's also
             * interpreted by Linux kernel before we received it, possibly
             * we should clean the kernel Inquiry cache through
             * ioctl(s->fd, HCI_INQUIRY, ...).  */
            break;

        case HCI_ACLDATA_PKT:
            if (s->len < 4)
                goto bad_pkt;

            pktlen = MIN(((pkt[3] << 8) | pkt[2]) + 4, s->len);
            s->hci.acl_recv(s->hci.opaque, pkt, pktlen);
            s->len -= pktlen;
            pkt += pktlen;
            break;

        case HCI_SCODATA_PKT:
            if (s->len < 3)
                goto bad_pkt;

            pktlen = MIN(pkt[2] + 3, s->len);
            s->len -= pktlen;
            pkt += pktlen;
            break;

        default:
        bad_pkt:
            fprintf(stderr, "qemu: bad HCI packet type %02x\n", pkt[-1]);
        }
}

static int bt_host_bdaddr_set(struct HCIInfo *hci, const uint8_t *bd_addr)
{
    return -ENOTSUP;
}

struct HCIInfo *bt_host_hci(const char *id)
{
    struct bt_host_hci_s *s;
    int fd = -1;
# ifdef CONFIG_BLUEZ
    int dev_id = hci_devid(id);
    struct hci_filter flt;

    if (dev_id < 0) {
        fprintf(stderr, "qemu: `%s' not available\n", id);
        return 0;
    }

    fd = hci_open_dev(dev_id);

    /* XXX: can we ensure nobody else has the device opened?  */
# endif

    if (fd < 0) {
        fprintf(stderr, "qemu: Can't open `%s': %s (%i)\n",
                        id, strerror(errno), errno);
        return NULL;
    }

# ifdef CONFIG_BLUEZ
    hci_filter_clear(&flt);
    hci_filter_all_ptypes(&flt);
    hci_filter_all_events(&flt);

    if (setsockopt(fd, SOL_HCI, HCI_FILTER, &flt, sizeof(flt)) < 0) {
        fprintf(stderr, "qemu: Can't set HCI filter on socket (%i)\n", errno);
        return 0;
    }
# endif

    s = g_malloc0(sizeof(struct bt_host_hci_s));
    s->fd = fd;
    s->hci.cmd_send = bt_host_cmd;
    s->hci.sco_send = bt_host_sco;
    s->hci.acl_send = bt_host_acl;
    s->hci.bdaddr_set = bt_host_bdaddr_set;

    qemu_set_fd_handler(s->fd, bt_host_read, NULL, s);

    return &s->hci;
}
#else
struct HCIInfo *bt_host_hci(const char *id)
{
    fprintf(stderr, "qemu: bluetooth passthrough not supported (yet)\n");

    return 0;
}
#endif
