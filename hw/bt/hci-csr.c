/*
 * Bluetooth serial HCI transport.
 * CSR41814 HCI with H4p vendor extensions.
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

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "sysemu/char.h"
#include "qemu/timer.h"
#include "qemu/bswap.h"
#include "hw/irq.h"
#include "sysemu/bt.h"
#include "hw/bt.h"

struct csrhci_s {
    int enable;
    qemu_irq *pins;
    int pin_state;
    int modem_state;
    CharDriverState chr;
#define FIFO_LEN	4096
    int out_start;
    int out_len;
    int out_size;
    uint8_t outfifo[FIFO_LEN * 2];
    uint8_t inpkt[FIFO_LEN];
    enum {
        CSR_HDR_LEN,
        CSR_DATA_LEN,
        CSR_DATA
    } in_state;
    int in_len;
    int in_hdr;
    int in_needed;
    QEMUTimer *out_tm;
    int64_t baud_delay;

    bdaddr_t bd_addr;
    struct HCIInfo *hci;
};

/* H4+ packet types */
enum {
    H4_CMD_PKT   = 1,
    H4_ACL_PKT   = 2,
    H4_SCO_PKT   = 3,
    H4_EVT_PKT   = 4,
    H4_NEG_PKT   = 6,
    H4_ALIVE_PKT = 7,
};

/* CSR41814 negotiation start magic packet */
static const uint8_t csrhci_neg_packet[] = {
    H4_NEG_PKT, 10,
    0x00, 0xa0, 0x01, 0x00, 0x00,
    0x4c, 0x00, 0x96, 0x00, 0x00,
};

/* CSR41814 vendor-specific command OCFs */
enum {
    OCF_CSR_SEND_FIRMWARE = 0x000,
};

static inline void csrhci_fifo_wake(struct csrhci_s *s)
{
    if (!s->enable || !s->out_len)
        return;

    /* XXX: Should wait for s->modem_state & CHR_TIOCM_RTS? */
    if (s->chr.chr_can_read && s->chr.chr_can_read(s->chr.handler_opaque) &&
                    s->chr.chr_read) {
        s->chr.chr_read(s->chr.handler_opaque,
                        s->outfifo + s->out_start ++, 1);
        s->out_len --;
        if (s->out_start >= s->out_size) {
            s->out_start = 0;
            s->out_size = FIFO_LEN;
        }
    }

    if (s->out_len)
        timer_mod(s->out_tm, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + s->baud_delay);
}

#define csrhci_out_packetz(s, len) memset(csrhci_out_packet(s, len), 0, len)
static uint8_t *csrhci_out_packet(struct csrhci_s *s, int len)
{
    int off = s->out_start + s->out_len;

    /* TODO: do the padding here, i.e. align len */
    s->out_len += len;

    if (off < FIFO_LEN) {
        if (off + len > FIFO_LEN && (s->out_size = off + len) > FIFO_LEN * 2) {
            fprintf(stderr, "%s: can't alloc %i bytes\n", __FUNCTION__, len);
            exit(-1);
        }
        return s->outfifo + off;
    }

    if (s->out_len > s->out_size) {
        fprintf(stderr, "%s: can't alloc %i bytes\n", __FUNCTION__, len);
        exit(-1);
    }

    return s->outfifo + off - s->out_size;
}

static inline uint8_t *csrhci_out_packet_csr(struct csrhci_s *s,
                int type, int len)
{
    uint8_t *ret = csrhci_out_packetz(s, len + 2);

    *ret ++ = type;
    *ret ++ = len;

    return ret;
}

static inline uint8_t *csrhci_out_packet_event(struct csrhci_s *s,
                int evt, int len)
{
    uint8_t *ret = csrhci_out_packetz(s,
                    len + 1 + sizeof(struct hci_event_hdr));

    *ret ++ = H4_EVT_PKT;
    ((struct hci_event_hdr *) ret)->evt = evt;
    ((struct hci_event_hdr *) ret)->plen = len;

    return ret + sizeof(struct hci_event_hdr);
}

static void csrhci_in_packet_vendor(struct csrhci_s *s, int ocf,
                uint8_t *data, int len)
{
    int offset;
    uint8_t *rpkt;

    switch (ocf) {
    case OCF_CSR_SEND_FIRMWARE:
        /* Check if this is the bd_address packet */
        if (len >= 18 + 8 && data[12] == 0x01 && data[13] == 0x00) {
            offset = 18;
            s->bd_addr.b[0] = data[offset + 7];	/* Beyond cmd packet end(!?) */
            s->bd_addr.b[1] = data[offset + 6];
            s->bd_addr.b[2] = data[offset + 4];
            s->bd_addr.b[3] = data[offset + 0];
            s->bd_addr.b[4] = data[offset + 3];
            s->bd_addr.b[5] = data[offset + 2];

            s->hci->bdaddr_set(s->hci, s->bd_addr.b);
            fprintf(stderr, "%s: bd_address loaded from firmware: "
                            "%02x:%02x:%02x:%02x:%02x:%02x\n", __FUNCTION__,
                            s->bd_addr.b[0], s->bd_addr.b[1], s->bd_addr.b[2],
                            s->bd_addr.b[3], s->bd_addr.b[4], s->bd_addr.b[5]);
        }

        rpkt = csrhci_out_packet_event(s, EVT_VENDOR, 11);
        /* Status bytes: no error */
        rpkt[9] = 0x00;
        rpkt[10] = 0x00;
        break;

    default:
        fprintf(stderr, "%s: got a bad CMD packet\n", __FUNCTION__);
        return;
    }

    csrhci_fifo_wake(s);
}

static void csrhci_in_packet(struct csrhci_s *s, uint8_t *pkt)
{
    uint8_t *rpkt;
    int opc;

    switch (*pkt ++) {
    case H4_CMD_PKT:
        opc = le16_to_cpu(((struct hci_command_hdr *) pkt)->opcode);
        if (cmd_opcode_ogf(opc) == OGF_VENDOR_CMD) {
            csrhci_in_packet_vendor(s, cmd_opcode_ocf(opc),
                            pkt + sizeof(struct hci_command_hdr),
                            s->in_len - sizeof(struct hci_command_hdr) - 1);
            return;
        }

        /* TODO: if the command is OCF_READ_LOCAL_COMMANDS or the likes,
         * we need to send it to the HCI layer and then add our supported
         * commands to the returned mask (such as OGF_VENDOR_CMD).  With
         * bt-hci.c we could just have hooks for this kind of commands but
         * we can't with bt-host.c.  */

        s->hci->cmd_send(s->hci, pkt, s->in_len - 1);
        break;

    case H4_EVT_PKT:
        goto bad_pkt;

    case H4_ACL_PKT:
        s->hci->acl_send(s->hci, pkt, s->in_len - 1);
        break;

    case H4_SCO_PKT:
        s->hci->sco_send(s->hci, pkt, s->in_len - 1);
        break;

    case H4_NEG_PKT:
        if (s->in_hdr != sizeof(csrhci_neg_packet) ||
                        memcmp(pkt - 1, csrhci_neg_packet, s->in_hdr)) {
            fprintf(stderr, "%s: got a bad NEG packet\n", __FUNCTION__);
            return;
        }
        pkt += 2;

        rpkt = csrhci_out_packet_csr(s, H4_NEG_PKT, 10);

        *rpkt ++ = 0x20;	/* Operational settings negotiation Ok */
        memcpy(rpkt, pkt, 7); rpkt += 7;
        *rpkt ++ = 0xff;
        *rpkt = 0xff;
        break;

    case H4_ALIVE_PKT:
        if (s->in_hdr != 4 || pkt[1] != 0x55 || pkt[2] != 0x00) {
            fprintf(stderr, "%s: got a bad ALIVE packet\n", __FUNCTION__);
            return;
        }

        rpkt = csrhci_out_packet_csr(s, H4_ALIVE_PKT, 2);

        *rpkt ++ = 0xcc;
        *rpkt = 0x00;
        break;

    default:
    bad_pkt:
        /* TODO: error out */
        fprintf(stderr, "%s: got a bad packet\n", __FUNCTION__);
        break;
    }

    csrhci_fifo_wake(s);
}

static int csrhci_header_len(const uint8_t *pkt)
{
    switch (pkt[0]) {
    case H4_CMD_PKT:
        return HCI_COMMAND_HDR_SIZE;
    case H4_EVT_PKT:
        return HCI_EVENT_HDR_SIZE;
    case H4_ACL_PKT:
        return HCI_ACL_HDR_SIZE;
    case H4_SCO_PKT:
        return HCI_SCO_HDR_SIZE;
    case H4_NEG_PKT:
        return pkt[1] + 1;
    case H4_ALIVE_PKT:
        return 3;
    }

    exit(-1);
}

static int csrhci_data_len(const uint8_t *pkt)
{
    switch (*pkt ++) {
    case H4_CMD_PKT:
        /* It seems that vendor-specific command packets for H4+ are all
         * one byte longer than indicated in the standard header.  */
        if (le16_to_cpu(((struct hci_command_hdr *) pkt)->opcode) == 0xfc00)
            return (((struct hci_command_hdr *) pkt)->plen + 1) & ~1;

        return ((struct hci_command_hdr *) pkt)->plen;
    case H4_EVT_PKT:
        return ((struct hci_event_hdr *) pkt)->plen;
    case H4_ACL_PKT:
        return le16_to_cpu(((struct hci_acl_hdr *) pkt)->dlen);
    case H4_SCO_PKT:
        return ((struct hci_sco_hdr *) pkt)->dlen;
    case H4_NEG_PKT:
    case H4_ALIVE_PKT:
        return 0;
    }

    exit(-1);
}

static void csrhci_ready_for_next_inpkt(struct csrhci_s *s)
{
    s->in_state = CSR_HDR_LEN;
    s->in_len = 0;
    s->in_needed = 2;
    s->in_hdr = INT_MAX;
}

static int csrhci_write(struct CharDriverState *chr,
                const uint8_t *buf, int len)
{
    struct csrhci_s *s = (struct csrhci_s *) chr->opaque;
    int total = 0;

    if (!s->enable)
        return 0;

    for (;;) {
        int cnt = MIN(len, s->in_needed - s->in_len);
        if (cnt) {
            memcpy(s->inpkt + s->in_len, buf, cnt);
            s->in_len += cnt;
            buf += cnt;
            len -= cnt;
            total += cnt;
        }

        if (s->in_len < s->in_needed) {
            break;
        }

        if (s->in_state == CSR_HDR_LEN) {
            s->in_hdr = csrhci_header_len(s->inpkt) + 1;
            assert(s->in_hdr >= s->in_needed);
            s->in_needed = s->in_hdr;
            s->in_state = CSR_DATA_LEN;
            continue;
        }

        if (s->in_state == CSR_DATA_LEN) {
            s->in_needed += csrhci_data_len(s->inpkt);
            /* hci_acl_hdr could specify more than 4096 bytes, so assert.  */
            assert(s->in_needed <= sizeof(s->inpkt));
            s->in_state = CSR_DATA;
            continue;
        }

        if (s->in_state == CSR_DATA) {
            csrhci_in_packet(s, s->inpkt);
            csrhci_ready_for_next_inpkt(s);
        }
    }

    return total;
}

static void csrhci_out_hci_packet_event(void *opaque,
                const uint8_t *data, int len)
{
    struct csrhci_s *s = (struct csrhci_s *) opaque;
    uint8_t *pkt = csrhci_out_packet(s, (len + 2) & ~1);	/* Align */

    *pkt ++ = H4_EVT_PKT;
    memcpy(pkt, data, len);

    csrhci_fifo_wake(s);
}

static void csrhci_out_hci_packet_acl(void *opaque,
                const uint8_t *data, int len)
{
    struct csrhci_s *s = (struct csrhci_s *) opaque;
    uint8_t *pkt = csrhci_out_packet(s, (len + 2) & ~1);	/* Align */

    *pkt ++ = H4_ACL_PKT;
    pkt[len & ~1] = 0;
    memcpy(pkt, data, len);

    csrhci_fifo_wake(s);
}

static int csrhci_ioctl(struct CharDriverState *chr, int cmd, void *arg)
{
    QEMUSerialSetParams *ssp;
    struct csrhci_s *s = (struct csrhci_s *) chr->opaque;
    int prev_state = s->modem_state;

    switch (cmd) {
    case CHR_IOCTL_SERIAL_SET_PARAMS:
        ssp = (QEMUSerialSetParams *) arg;
        s->baud_delay = NANOSECONDS_PER_SECOND / ssp->speed;
        /* Moments later... (but shorter than 100ms) */
        s->modem_state |= CHR_TIOCM_CTS;
        break;

    case CHR_IOCTL_SERIAL_GET_TIOCM:
        *(int *) arg = s->modem_state;
        break;

    case CHR_IOCTL_SERIAL_SET_TIOCM:
        s->modem_state = *(int *) arg;
        if (~s->modem_state & prev_state & CHR_TIOCM_RTS)
            s->modem_state &= ~CHR_TIOCM_CTS;
        break;

    default:
        return -ENOTSUP;
    }
    return 0;
}

static void csrhci_reset(struct csrhci_s *s)
{
    s->out_len = 0;
    s->out_size = FIFO_LEN;
    csrhci_ready_for_next_inpkt(s);
    s->baud_delay = NANOSECONDS_PER_SECOND;
    s->enable = 0;

    s->modem_state = 0;
    /* After a while... (but sooner than 10ms) */
    s->modem_state |= CHR_TIOCM_CTS;

    memset(&s->bd_addr, 0, sizeof(bdaddr_t));
}

static void csrhci_out_tick(void *opaque)
{
    csrhci_fifo_wake((struct csrhci_s *) opaque);
}

static void csrhci_pins(void *opaque, int line, int level)
{
    struct csrhci_s *s = (struct csrhci_s *) opaque;
    int state = s->pin_state;

    s->pin_state &= ~(1 << line);
    s->pin_state |= (!!level) << line;

    if ((state & ~s->pin_state) & (1 << csrhci_pin_reset)) {
        /* TODO: Disappear from lower layers */
        csrhci_reset(s);
    }

    if (s->pin_state == 3 && state != 3) {
        s->enable = 1;
        /* TODO: Wake lower layers up */
    }
}

qemu_irq *csrhci_pins_get(CharDriverState *chr)
{
    struct csrhci_s *s = (struct csrhci_s *) chr->opaque;

    return s->pins;
}

CharDriverState *uart_hci_init(qemu_irq wakeup)
{
    struct csrhci_s *s = (struct csrhci_s *)
            g_malloc0(sizeof(struct csrhci_s));

    s->chr.opaque = s;
    s->chr.chr_write = csrhci_write;
    s->chr.chr_ioctl = csrhci_ioctl;
    s->chr.avail_connections = 1;

    s->hci = qemu_next_hci();
    s->hci->opaque = s;
    s->hci->evt_recv = csrhci_out_hci_packet_event;
    s->hci->acl_recv = csrhci_out_hci_packet_acl;

    s->out_tm = timer_new_ns(QEMU_CLOCK_VIRTUAL, csrhci_out_tick, s);
    s->pins = qemu_allocate_irqs(csrhci_pins, s, __csrhci_pins);
    csrhci_reset(s);

    return &s->chr;
}
