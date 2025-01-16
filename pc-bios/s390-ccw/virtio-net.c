/*
 * Virtio-net driver for the s390-ccw firmware
 *
 * Copyright 2017 Thomas Huth, Red Hat Inc.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <ethernet.h>
#include "s390-ccw.h"
#include "virtio.h"
#include "s390-time.h"
#include "helper.h"

#ifndef DEBUG_VIRTIO_NET
#define DEBUG_VIRTIO_NET 0
#endif

#define VIRTIO_NET_F_MAC_BIT  (1 << 5)

#define VQ_RX 0         /* Receive queue */
#define VQ_TX 1         /* Transmit queue */

struct VirtioNetHdr {
    uint8_t flags;
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
    /*uint16_t num_buffers;*/ /* Only with VIRTIO_NET_F_MRG_RXBUF or VIRTIO1 */
};
typedef struct VirtioNetHdr VirtioNetHdr;

static uint16_t rx_last_idx;  /* Last index in receive queue "used" ring */

int virtio_net_init(void *mac_addr)
{
    VDev *vdev = virtio_get_device();
    VRing *rxvq = &vdev->vrings[VQ_RX];
    void *buf;
    int i;

    rx_last_idx = 0;

    vdev->guest_features[0] = VIRTIO_NET_F_MAC_BIT;
    virtio_setup_ccw(vdev);

    if (!(vdev->guest_features[0] & VIRTIO_NET_F_MAC_BIT)) {
        puts("virtio-net device does not support the MAC address feature");
        return -1;
    }

    memcpy(mac_addr, vdev->config.net.mac, ETH_ALEN);

    for (i = 0; i < 64; i++) {
        buf = malloc(ETH_MTU_SIZE + sizeof(VirtioNetHdr));
        IPL_assert(buf != NULL, "Can not allocate memory for receive buffers");
        vring_send_buf(rxvq, buf, ETH_MTU_SIZE + sizeof(VirtioNetHdr),
                       VRING_DESC_F_WRITE);
    }
    vring_notify(rxvq);

    return 0;
}

int send(int fd, const void *buf, int len, int flags)
{
    VirtioNetHdr tx_hdr;
    VDev *vdev = virtio_get_device();
    VRing *txvq = &vdev->vrings[VQ_TX];

    /* Set up header - we do not use anything special, so simply clear it */
    memset(&tx_hdr, 0, sizeof(tx_hdr));

    vring_send_buf(txvq, &tx_hdr, sizeof(tx_hdr), VRING_DESC_F_NEXT);
    vring_send_buf(txvq, (void *)buf, len, VRING_HIDDEN_IS_CHAIN);
    while (!vr_poll(txvq)) {
        yield();
    }
    if (drain_irqs(txvq->schid)) {
        puts("send: drain irqs failed");
        return -1;
    }

    return len;
}

int recv(int fd, void *buf, int maxlen, int flags)
{
    VDev *vdev = virtio_get_device();
    VRing *rxvq = &vdev->vrings[VQ_RX];
    int len, id;
    uint8_t *pkt;

    if (rx_last_idx == rxvq->used->idx) {
        return 0;
    }

    len = rxvq->used->ring[rx_last_idx % rxvq->num].len - sizeof(VirtioNetHdr);
    if (len > maxlen) {
        puts("virtio-net: Receive buffer too small");
        len = maxlen;
    }
    id = rxvq->used->ring[rx_last_idx % rxvq->num].id % rxvq->num;
    pkt = (uint8_t *)(rxvq->desc[id].addr + sizeof(VirtioNetHdr));

#if DEBUG_VIRTIO_NET   /* Dump packet */
    int i;
    printf("\nbuf %p: len=%i\n", (void *)rxvq->desc[id].addr, len);
    for (i = 0; i < 64; i++) {
        printf(" %02x", pkt[i]);
        if ((i % 16) == 15) {
            printf("\n");
        }
    }
    printf("\n");
#endif

    /* Copy data to destination buffer */
    memcpy(buf, pkt, len);

    /* Mark buffer as available to the host again */
    rxvq->avail->ring[rxvq->avail->idx % rxvq->num] = id;
    rxvq->avail->idx = rxvq->avail->idx + 1;
    vring_notify(rxvq);

    /* Move index to next entry */
    rx_last_idx = rx_last_idx + 1;

    return len;
}

void virtio_net_deinit(void)
{
    virtio_reset(virtio_get_device());
}
