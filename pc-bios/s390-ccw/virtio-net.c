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
#include "virtio-ccw.h"
#include "virtio-pci.h"
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
};
typedef struct VirtioNetHdr VirtioNetHdr;

struct VirtioNetHdrMrgRxbuf {
    struct VirtioNetHdr hdr;
    uint16_t num_buffers; /* Only with VIRTIO_NET_F_MRG_RXBUF or VIRTIO1 */
};
typedef struct VirtioNetHdrMrgRxbuf VirtioNetHdrMrgRxbuf;

/* Header sizes for different modes */
#define VIRTIO_NET_HDR_SIZE_LEGACY  sizeof(VirtioNetHdr)
#define VIRTIO_NET_HDR_SIZE_V1      sizeof(VirtioNetHdrMrgRxbuf)

static uint16_t rx_last_idx;  /* Last index in receive queue "used" ring */
static int virtio_net_hdr_size;

int virtio_net_init(void *mac_addr)
{
    VDev *vdev = virtio_get_device();
    VRing *rxvq = &vdev->vrings[VQ_RX];
    void *buf;
    int i;

    rx_last_idx = 0;

    vdev->guest_features[0] = VIRTIO_NET_F_MAC_BIT;
    switch (virtio_get_device()->ipl_type) {
    case S390_IPL_TYPE_CCW:
        if (virtio_ccw_setup(vdev)) {
            puts("Setup failed for virtio-net-ccw");
            return -1;
        }
        break;
    case S390_IPL_TYPE_PCI:
        if (virtio_pci_setup(vdev)) {
            puts("Setup failed for virtio-net-pci");
            return -1;
        }
        break;
    default:
        return -1;
    }

    if (!(vdev->guest_features[0] & VIRTIO_NET_F_MAC_BIT)) {
        puts("virtio-net device does not support the MAC address feature");
        return -1;
    }

    virtio_net_hdr_size = vdev->guest_features[1] & VIRTIO_F_VERSION_1
                          ? VIRTIO_NET_HDR_SIZE_V1
                          : VIRTIO_NET_HDR_SIZE_LEGACY;

    memcpy(mac_addr, vdev->config.net.mac, ETH_ALEN);

    for (i = 0; i < 64; i++) {
        buf = malloc(ETH_MTU_SIZE + virtio_net_hdr_size);
        IPL_assert(buf != NULL, "Can not allocate memory for receive buffers");
        vring_send_buf(rxvq, buf, ETH_MTU_SIZE + virtio_net_hdr_size,
                       VRING_DESC_F_WRITE);
    }
    vring_notify(rxvq);

    return 0;
}

int send(int fd, const void *buf, int len, int flags)
{
    VirtioNetHdrMrgRxbuf tx_hdr;
    VDev *vdev = virtio_get_device();
    VRing *txvq = &vdev->vrings[VQ_TX];

    /* Set up header - we do not use anything special, so simply clear it */
    memset(&tx_hdr, 0, sizeof(tx_hdr));

    vring_send_buf(txvq, &tx_hdr, virtio_net_hdr_size, VRING_DESC_F_NEXT);
    vring_send_buf(txvq, (void *)buf, len, VRING_HIDDEN_IS_CHAIN);
    while (!vr_poll(txvq)) {
        yield();
    }
    if (drain_irqs()) {
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
    uint16_t rx_used_idx, rx_avail_idx;
    uint32_t rx_used_len, rx_used_id;
    uint64_t rx_desc_addr;

    rx_used_idx = virtio_tswap16(rxvq->used->idx);
    if (rx_last_idx == rx_used_idx) {
        return 0;
    }

    rx_used_len = virtio_tswap32(rxvq->used->ring[rx_last_idx % rxvq->num].len);
    rx_used_id = virtio_tswap32(rxvq->used->ring[rx_last_idx % rxvq->num].id);

    len = rx_used_len - virtio_net_hdr_size;
    if (len > maxlen) {
        puts("virtio-net: Receive buffer too small");
        len = maxlen;
    }
    id = rx_used_id % rxvq->num;
    rx_desc_addr = virtio_tswap64(rxvq->desc[id].addr);
    pkt = (uint8_t *)(rx_desc_addr + virtio_net_hdr_size);

#if DEBUG_VIRTIO_NET   /* Dump packet */
    int i;
    printf("\nbuf %p: len=%i\n", (void *)rx_desc_addr, len);
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
    rx_avail_idx = virtio_tswap16(rxvq->avail->idx);
    rxvq->avail->ring[rx_avail_idx % rxvq->num] = virtio_tswap16(id);
    rx_avail_idx++;
    rxvq->avail->idx = virtio_tswap16(rx_avail_idx);
    vring_notify(rxvq);

    /* Move index to next entry */
    rx_last_idx = rx_last_idx + 1;

    return len;
}

void virtio_net_deinit(void)
{
    virtio_reset(virtio_get_device());
}
