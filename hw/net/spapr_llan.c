/*
 * QEMU PowerPC pSeries Logical Partition (aka sPAPR) hardware System Emulator
 *
 * PAPR Inter-VM Logical Lan, aka ibmveth
 *
 * Copyright (c) 2010,2011 David Gibson, IBM Corporation.
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
 *
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "net/net.h"
#include "migration/vmstate.h"
#include "hw/ppc/spapr.h"
#include "hw/ppc/spapr_vio.h"
#include "hw/qdev-properties.h"
#include "sysemu/sysemu.h"
#include "trace.h"

#include <libfdt.h>
#include "qom/object.h"

#define ETH_ALEN        6
#define MAX_PACKET_SIZE 65536

/* Compatibility flags for migration */
#define SPAPRVLAN_FLAG_RX_BUF_POOLS_BIT  0
#define SPAPRVLAN_FLAG_RX_BUF_POOLS      (1 << SPAPRVLAN_FLAG_RX_BUF_POOLS_BIT)

/*
 * Virtual LAN device
 */

typedef uint64_t vlan_bd_t;

#define VLAN_BD_VALID        0x8000000000000000ULL
#define VLAN_BD_TOGGLE       0x4000000000000000ULL
#define VLAN_BD_NO_CSUM      0x0200000000000000ULL
#define VLAN_BD_CSUM_GOOD    0x0100000000000000ULL
#define VLAN_BD_LEN_MASK     0x00ffffff00000000ULL
#define VLAN_BD_LEN(bd)      (((bd) & VLAN_BD_LEN_MASK) >> 32)
#define VLAN_BD_ADDR_MASK    0x00000000ffffffffULL
#define VLAN_BD_ADDR(bd)     ((bd) & VLAN_BD_ADDR_MASK)

#define VLAN_VALID_BD(addr, len) (VLAN_BD_VALID | \
                                  (((len) << 32) & VLAN_BD_LEN_MASK) |  \
                                  (addr & VLAN_BD_ADDR_MASK))

#define VLAN_RXQC_TOGGLE     0x80
#define VLAN_RXQC_VALID      0x40
#define VLAN_RXQC_NO_CSUM    0x02
#define VLAN_RXQC_CSUM_GOOD  0x01

#define VLAN_RQ_ALIGNMENT    16
#define VLAN_RXQ_BD_OFF      0
#define VLAN_FILTER_BD_OFF   8
#define VLAN_RX_BDS_OFF      16
/*
 * The final 8 bytes of the buffer list is a counter of frames dropped
 * because there was not a buffer in the buffer list capable of holding
 * the frame. We must avoid it, or the operating system will report garbage
 * for this statistic.
 */
#define VLAN_RX_BDS_LEN      (SPAPR_TCE_PAGE_SIZE - VLAN_RX_BDS_OFF - 8)
#define VLAN_MAX_BUFS        (VLAN_RX_BDS_LEN / 8)

#define TYPE_VIO_SPAPR_VLAN_DEVICE "spapr-vlan"
OBJECT_DECLARE_SIMPLE_TYPE(SpaprVioVlan, VIO_SPAPR_VLAN_DEVICE)

#define RX_POOL_MAX_BDS 4096
#define RX_MAX_POOLS 5

typedef struct {
    int32_t bufsize;
    int32_t count;
    vlan_bd_t bds[RX_POOL_MAX_BDS];
} RxBufPool;

struct SpaprVioVlan {
    SpaprVioDevice sdev;
    NICConf nicconf;
    NICState *nic;
    MACAddr perm_mac;
    bool isopen;
    hwaddr buf_list;
    uint32_t add_buf_ptr, use_buf_ptr, rx_bufs;
    hwaddr rxq_ptr;
    QEMUTimer *rxp_timer;
    uint32_t compat_flags;             /* Compatibility flags for migration */
    RxBufPool *rx_pool[RX_MAX_POOLS];  /* Receive buffer descriptor pools */
};

static bool spapr_vlan_can_receive(NetClientState *nc)
{
    SpaprVioVlan *dev = qemu_get_nic_opaque(nc);

    return dev->isopen && dev->rx_bufs > 0;
}

/**
 * The last 8 bytes of the receive buffer list page (that has been
 * supplied by the guest with the H_REGISTER_LOGICAL_LAN call) contain
 * a counter for frames that have been dropped because there was no
 * suitable receive buffer available. This function is used to increase
 * this counter by one.
 */
static void spapr_vlan_record_dropped_rx_frame(SpaprVioVlan *dev)
{
    uint64_t cnt;

    cnt = vio_ldq(&dev->sdev, dev->buf_list + 4096 - 8);
    vio_stq(&dev->sdev, dev->buf_list + 4096 - 8, cnt + 1);
}

/**
 * Get buffer descriptor from one of our receive buffer pools
 */
static vlan_bd_t spapr_vlan_get_rx_bd_from_pool(SpaprVioVlan *dev,
                                                size_t size)
{
    vlan_bd_t bd;
    int pool;

    for (pool = 0; pool < RX_MAX_POOLS; pool++) {
        if (dev->rx_pool[pool]->count > 0 &&
            dev->rx_pool[pool]->bufsize >= size + 8) {
            break;
        }
    }
    if (pool == RX_MAX_POOLS) {
        /* Failed to find a suitable buffer */
        return 0;
    }


    trace_spapr_vlan_get_rx_bd_from_pool_found(pool,
                                               dev->rx_pool[pool]->count,
                                               dev->rx_bufs);

    /* Remove the buffer from the pool */
    dev->rx_pool[pool]->count--;
    bd = dev->rx_pool[pool]->bds[dev->rx_pool[pool]->count];
    dev->rx_pool[pool]->bds[dev->rx_pool[pool]->count] = 0;

    return bd;
}

/**
 * Get buffer descriptor from the receive buffer list page that has been
 * supplied by the guest with the H_REGISTER_LOGICAL_LAN call
 */
static vlan_bd_t spapr_vlan_get_rx_bd_from_page(SpaprVioVlan *dev,
                                                size_t size)
{
    int buf_ptr = dev->use_buf_ptr;
    vlan_bd_t bd;

    do {
        buf_ptr += 8;
        if (buf_ptr >= VLAN_RX_BDS_LEN + VLAN_RX_BDS_OFF) {
            buf_ptr = VLAN_RX_BDS_OFF;
        }

        bd = vio_ldq(&dev->sdev, dev->buf_list + buf_ptr);

        trace_spapr_vlan_get_rx_bd_from_page(buf_ptr, (uint64_t)bd);
    } while ((!(bd & VLAN_BD_VALID) || VLAN_BD_LEN(bd) < size + 8)
             && buf_ptr != dev->use_buf_ptr);

    if (!(bd & VLAN_BD_VALID) || VLAN_BD_LEN(bd) < size + 8) {
        /* Failed to find a suitable buffer */
        return 0;
    }

    /* Remove the buffer from the pool */
    dev->use_buf_ptr = buf_ptr;
    vio_stq(&dev->sdev, dev->buf_list + dev->use_buf_ptr, 0);

    trace_spapr_vlan_get_rx_bd_from_page_found(dev->use_buf_ptr, dev->rx_bufs);

    return bd;
}

static ssize_t spapr_vlan_receive(NetClientState *nc, const uint8_t *buf,
                                  size_t size)
{
    SpaprVioVlan *dev = qemu_get_nic_opaque(nc);
    SpaprVioDevice *sdev = VIO_SPAPR_DEVICE(dev);
    vlan_bd_t rxq_bd = vio_ldq(sdev, dev->buf_list + VLAN_RXQ_BD_OFF);
    vlan_bd_t bd;
    uint64_t handle;
    uint8_t control;

    trace_spapr_vlan_receive(sdev->qdev.id, dev->rx_bufs);

    if (!dev->isopen) {
        return -1;
    }

    if (!dev->rx_bufs) {
        spapr_vlan_record_dropped_rx_frame(dev);
        return 0;
    }

    if (dev->compat_flags & SPAPRVLAN_FLAG_RX_BUF_POOLS) {
        bd = spapr_vlan_get_rx_bd_from_pool(dev, size);
    } else {
        bd = spapr_vlan_get_rx_bd_from_page(dev, size);
    }
    if (!bd) {
        spapr_vlan_record_dropped_rx_frame(dev);
        return 0;
    }

    dev->rx_bufs--;

    /* Transfer the packet data */
    if (spapr_vio_dma_write(sdev, VLAN_BD_ADDR(bd) + 8, buf, size) < 0) {
        return -1;
    }

    trace_spapr_vlan_receive_dma_completed();

    /* Update the receive queue */
    control = VLAN_RXQC_TOGGLE | VLAN_RXQC_VALID;
    if (rxq_bd & VLAN_BD_TOGGLE) {
        control ^= VLAN_RXQC_TOGGLE;
    }

    handle = vio_ldq(sdev, VLAN_BD_ADDR(bd));
    vio_stq(sdev, VLAN_BD_ADDR(rxq_bd) + dev->rxq_ptr + 8, handle);
    vio_stl(sdev, VLAN_BD_ADDR(rxq_bd) + dev->rxq_ptr + 4, size);
    vio_sth(sdev, VLAN_BD_ADDR(rxq_bd) + dev->rxq_ptr + 2, 8);
    vio_stb(sdev, VLAN_BD_ADDR(rxq_bd) + dev->rxq_ptr, control);

    trace_spapr_vlan_receive_wrote(dev->rxq_ptr,
                                   vio_ldq(sdev, VLAN_BD_ADDR(rxq_bd) +
                                                 dev->rxq_ptr),
                                   vio_ldq(sdev, VLAN_BD_ADDR(rxq_bd) +
                                                 dev->rxq_ptr + 8));

    dev->rxq_ptr += 16;
    if (dev->rxq_ptr >= VLAN_BD_LEN(rxq_bd)) {
        dev->rxq_ptr = 0;
        vio_stq(sdev, dev->buf_list + VLAN_RXQ_BD_OFF, rxq_bd ^ VLAN_BD_TOGGLE);
    }

    if (sdev->signal_state & 1) {
        spapr_vio_irq_pulse(sdev);
    }

    return size;
}

static NetClientInfo net_spapr_vlan_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .can_receive = spapr_vlan_can_receive,
    .receive = spapr_vlan_receive,
};

static void spapr_vlan_flush_rx_queue(void *opaque)
{
    SpaprVioVlan *dev = opaque;

    qemu_flush_queued_packets(qemu_get_queue(dev->nic));
}

static void spapr_vlan_reset_rx_pool(RxBufPool *rxp)
{
    /*
     * Use INT_MAX as bufsize so that unused buffers are moved to the end
     * of the list during the qsort in spapr_vlan_add_rxbuf_to_pool() later.
     */
    rxp->bufsize = INT_MAX;
    rxp->count = 0;
    memset(rxp->bds, 0, sizeof(rxp->bds));
}

static void spapr_vlan_reset(SpaprVioDevice *sdev)
{
    SpaprVioVlan *dev = VIO_SPAPR_VLAN_DEVICE(sdev);
    int i;

    dev->buf_list = 0;
    dev->rx_bufs = 0;
    dev->isopen = 0;

    if (dev->compat_flags & SPAPRVLAN_FLAG_RX_BUF_POOLS) {
        for (i = 0; i < RX_MAX_POOLS; i++) {
            spapr_vlan_reset_rx_pool(dev->rx_pool[i]);
        }
    }

    memcpy(&dev->nicconf.macaddr.a, &dev->perm_mac.a,
           sizeof(dev->nicconf.macaddr.a));
    qemu_format_nic_info_str(qemu_get_queue(dev->nic), dev->nicconf.macaddr.a);
}

static void spapr_vlan_realize(SpaprVioDevice *sdev, Error **errp)
{
    SpaprVioVlan *dev = VIO_SPAPR_VLAN_DEVICE(sdev);

    qemu_macaddr_default_if_unset(&dev->nicconf.macaddr);

    memcpy(&dev->perm_mac.a, &dev->nicconf.macaddr.a, sizeof(dev->perm_mac.a));

    dev->nic = qemu_new_nic(&net_spapr_vlan_info, &dev->nicconf,
                            object_get_typename(OBJECT(sdev)), sdev->qdev.id,
                            &sdev->qdev.mem_reentrancy_guard, dev);
    qemu_format_nic_info_str(qemu_get_queue(dev->nic), dev->nicconf.macaddr.a);

    dev->rxp_timer = timer_new_us(QEMU_CLOCK_VIRTUAL, spapr_vlan_flush_rx_queue,
                                  dev);
}

static void spapr_vlan_instance_init(Object *obj)
{
    SpaprVioVlan *dev = VIO_SPAPR_VLAN_DEVICE(obj);
    int i;

    device_add_bootindex_property(obj, &dev->nicconf.bootindex,
                                  "bootindex", "",
                                  DEVICE(dev));

    if (dev->compat_flags & SPAPRVLAN_FLAG_RX_BUF_POOLS) {
        for (i = 0; i < RX_MAX_POOLS; i++) {
            dev->rx_pool[i] = g_new(RxBufPool, 1);
            spapr_vlan_reset_rx_pool(dev->rx_pool[i]);
        }
    }
}

static void spapr_vlan_instance_finalize(Object *obj)
{
    SpaprVioVlan *dev = VIO_SPAPR_VLAN_DEVICE(obj);
    int i;

    if (dev->compat_flags & SPAPRVLAN_FLAG_RX_BUF_POOLS) {
        for (i = 0; i < RX_MAX_POOLS; i++) {
            g_free(dev->rx_pool[i]);
            dev->rx_pool[i] = NULL;
        }
    }

    if (dev->rxp_timer) {
        timer_free(dev->rxp_timer);
    }
}

void spapr_vlan_create(SpaprVioBus *bus, NICInfo *nd)
{
    DeviceState *dev;

    dev = qdev_new("spapr-vlan");

    qdev_set_nic_properties(dev, nd);

    qdev_realize_and_unref(dev, &bus->bus, &error_fatal);
}

static int spapr_vlan_devnode(SpaprVioDevice *dev, void *fdt, int node_off)
{
    SpaprVioVlan *vdev = VIO_SPAPR_VLAN_DEVICE(dev);
    uint8_t padded_mac[8] = {0, 0};
    int ret;

    /* Some old phyp versions give the mac address in an 8-byte
     * property.  The kernel driver (before 3.10) has an insane workaround;
     * rather than doing the obvious thing and checking the property
     * length, it checks whether the first byte has 0b10 in the low
     * bits.  If a correct 6-byte property has a different first byte
     * the kernel will get the wrong mac address, overrunning its
     * buffer in the process (read only, thank goodness).
     *
     * Here we return a 6-byte address unless that would break a pre-3.10
     * driver.  In that case we return a padded 8-byte address to allow the old
     * workaround to succeed. */
    if ((vdev->nicconf.macaddr.a[0] & 0x3) == 0x2) {
        ret = fdt_setprop(fdt, node_off, "local-mac-address",
                          &vdev->nicconf.macaddr, ETH_ALEN);
    } else {
        memcpy(&padded_mac[2], &vdev->nicconf.macaddr, ETH_ALEN);
        ret = fdt_setprop(fdt, node_off, "local-mac-address",
                          padded_mac, sizeof(padded_mac));
    }
    if (ret < 0) {
        return ret;
    }

    ret = fdt_setprop_cell(fdt, node_off, "ibm,mac-address-filters", 0);
    if (ret < 0) {
        return ret;
    }

    return 0;
}

static int check_bd(SpaprVioVlan *dev, vlan_bd_t bd,
                    target_ulong alignment)
{
    if ((VLAN_BD_ADDR(bd) % alignment)
        || (VLAN_BD_LEN(bd) % alignment)) {
        return -1;
    }

    if (!spapr_vio_dma_valid(&dev->sdev, VLAN_BD_ADDR(bd),
                             VLAN_BD_LEN(bd), DMA_DIRECTION_FROM_DEVICE)
        || !spapr_vio_dma_valid(&dev->sdev, VLAN_BD_ADDR(bd),
                                VLAN_BD_LEN(bd), DMA_DIRECTION_TO_DEVICE)) {
        return -1;
    }

    return 0;
}

static target_ulong h_register_logical_lan(PowerPCCPU *cpu,
                                           SpaprMachineState *spapr,
                                           target_ulong opcode,
                                           target_ulong *args)
{
    target_ulong reg = args[0];
    target_ulong buf_list = args[1];
    target_ulong rec_queue = args[2];
    target_ulong filter_list = args[3];
    SpaprVioDevice *sdev = spapr_vio_find_by_reg(spapr->vio_bus, reg);
    SpaprVioVlan *dev = VIO_SPAPR_VLAN_DEVICE(sdev);
    vlan_bd_t filter_list_bd;

    if (!dev) {
        return H_PARAMETER;
    }

    if (dev->isopen) {
        hcall_dprintf("H_REGISTER_LOGICAL_LAN called twice without "
                      "H_FREE_LOGICAL_LAN\n");
        return H_RESOURCE;
    }

    if (check_bd(dev, VLAN_VALID_BD(buf_list, SPAPR_TCE_PAGE_SIZE),
                 SPAPR_TCE_PAGE_SIZE) < 0) {
        hcall_dprintf("Bad buf_list 0x" TARGET_FMT_lx "\n", buf_list);
        return H_PARAMETER;
    }

    filter_list_bd = VLAN_VALID_BD(filter_list, SPAPR_TCE_PAGE_SIZE);
    if (check_bd(dev, filter_list_bd, SPAPR_TCE_PAGE_SIZE) < 0) {
        hcall_dprintf("Bad filter_list 0x" TARGET_FMT_lx "\n", filter_list);
        return H_PARAMETER;
    }

    if (!(rec_queue & VLAN_BD_VALID)
        || (check_bd(dev, rec_queue, VLAN_RQ_ALIGNMENT) < 0)) {
        hcall_dprintf("Bad receive queue\n");
        return H_PARAMETER;
    }

    dev->buf_list = buf_list;
    sdev->signal_state = 0;

    rec_queue &= ~VLAN_BD_TOGGLE;

    /* Initialize the buffer list */
    vio_stq(sdev, buf_list, rec_queue);
    vio_stq(sdev, buf_list + 8, filter_list_bd);
    spapr_vio_dma_set(sdev, buf_list + VLAN_RX_BDS_OFF, 0,
                      SPAPR_TCE_PAGE_SIZE - VLAN_RX_BDS_OFF);
    dev->add_buf_ptr = VLAN_RX_BDS_OFF - 8;
    dev->use_buf_ptr = VLAN_RX_BDS_OFF - 8;
    dev->rx_bufs = 0;
    dev->rxq_ptr = 0;

    /* Initialize the receive queue */
    spapr_vio_dma_set(sdev, VLAN_BD_ADDR(rec_queue), 0, VLAN_BD_LEN(rec_queue));

    dev->isopen = 1;
    qemu_flush_queued_packets(qemu_get_queue(dev->nic));

    return H_SUCCESS;
}


static target_ulong h_free_logical_lan(PowerPCCPU *cpu,
                                       SpaprMachineState *spapr,
                                       target_ulong opcode, target_ulong *args)
{
    target_ulong reg = args[0];
    SpaprVioDevice *sdev = spapr_vio_find_by_reg(spapr->vio_bus, reg);
    SpaprVioVlan *dev = VIO_SPAPR_VLAN_DEVICE(sdev);

    if (!dev) {
        return H_PARAMETER;
    }

    if (!dev->isopen) {
        hcall_dprintf("H_FREE_LOGICAL_LAN called without "
                      "H_REGISTER_LOGICAL_LAN\n");
        return H_RESOURCE;
    }

    spapr_vlan_reset(sdev);
    return H_SUCCESS;
}

/**
 * Used for qsort, this function compares two RxBufPools by size.
 */
static int rx_pool_size_compare(const void *p1, const void *p2)
{
    const RxBufPool *pool1 = *(RxBufPool **)p1;
    const RxBufPool *pool2 = *(RxBufPool **)p2;

    if (pool1->bufsize < pool2->bufsize) {
        return -1;
    }
    return pool1->bufsize > pool2->bufsize;
}

/**
 * Search for a matching buffer pool with exact matching size,
 * or return -1 if no matching pool has been found.
 */
static int spapr_vlan_get_rx_pool_id(SpaprVioVlan *dev, int size)
{
    int pool;

    for (pool = 0; pool < RX_MAX_POOLS; pool++) {
        if (dev->rx_pool[pool]->bufsize == size) {
            return pool;
        }
    }

    return -1;
}

/**
 * Enqueuing receive buffer by adding it to one of our receive buffer pools
 */
static target_long spapr_vlan_add_rxbuf_to_pool(SpaprVioVlan *dev,
                                                target_ulong buf)
{
    int size = VLAN_BD_LEN(buf);
    int pool;

    pool = spapr_vlan_get_rx_pool_id(dev, size);
    if (pool < 0) {
        /*
         * No matching pool found? Try to use a new one. If the guest used all
         * pools before, but changed the size of one pool in the meantime, we might
         * need to recycle that pool here (if it's empty already). Thus scan
         * all buffer pools now, starting with the last (likely empty) one.
         */
        for (pool = RX_MAX_POOLS - 1; pool >= 0 ; pool--) {
            if (dev->rx_pool[pool]->count == 0) {
                dev->rx_pool[pool]->bufsize = size;
                /*
                 * Sort pools by size so that spapr_vlan_receive()
                 * can later find the smallest buffer pool easily.
                 */
                qsort(dev->rx_pool, RX_MAX_POOLS, sizeof(dev->rx_pool[0]),
                      rx_pool_size_compare);
                pool = spapr_vlan_get_rx_pool_id(dev, size);
                trace_spapr_vlan_add_rxbuf_to_pool_create(pool,
                                                          VLAN_BD_LEN(buf));
                break;
            }
        }
    }
    /* Still no usable pool? Give up */
    if (pool < 0 || dev->rx_pool[pool]->count >= RX_POOL_MAX_BDS) {
        return H_RESOURCE;
    }

    trace_spapr_vlan_add_rxbuf_to_pool(pool, VLAN_BD_LEN(buf),
                                       dev->rx_pool[pool]->count);

    dev->rx_pool[pool]->bds[dev->rx_pool[pool]->count++] = buf;

    return 0;
}

/**
 * This is the old way of enqueuing receive buffers: Add it to the rx queue
 * page that has been supplied by the guest (which is quite limited in size).
 */
static target_long spapr_vlan_add_rxbuf_to_page(SpaprVioVlan *dev,
                                                target_ulong buf)
{
    vlan_bd_t bd;

    if (dev->rx_bufs >= VLAN_MAX_BUFS) {
        return H_RESOURCE;
    }

    do {
        dev->add_buf_ptr += 8;
        if (dev->add_buf_ptr >= VLAN_RX_BDS_LEN + VLAN_RX_BDS_OFF) {
            dev->add_buf_ptr = VLAN_RX_BDS_OFF;
        }

        bd = vio_ldq(&dev->sdev, dev->buf_list + dev->add_buf_ptr);
    } while (bd & VLAN_BD_VALID);

    vio_stq(&dev->sdev, dev->buf_list + dev->add_buf_ptr, buf);

    trace_spapr_vlan_add_rxbuf_to_page(dev->add_buf_ptr, dev->rx_bufs, buf);

    return 0;
}

static target_ulong h_add_logical_lan_buffer(PowerPCCPU *cpu,
                                             SpaprMachineState *spapr,
                                             target_ulong opcode,
                                             target_ulong *args)
{
    target_ulong reg = args[0];
    target_ulong buf = args[1];
    SpaprVioDevice *sdev = spapr_vio_find_by_reg(spapr->vio_bus, reg);
    SpaprVioVlan *dev = VIO_SPAPR_VLAN_DEVICE(sdev);
    target_long ret;

    trace_spapr_vlan_h_add_logical_lan_buffer(reg, buf);

    if (!sdev) {
        hcall_dprintf("Bad device\n");
        return H_PARAMETER;
    }

    if ((check_bd(dev, buf, 4) < 0)
        || (VLAN_BD_LEN(buf) < 16)) {
        hcall_dprintf("Bad buffer enqueued\n");
        return H_PARAMETER;
    }

    if (!dev->isopen) {
        return H_RESOURCE;
    }

    if (dev->compat_flags & SPAPRVLAN_FLAG_RX_BUF_POOLS) {
        ret = spapr_vlan_add_rxbuf_to_pool(dev, buf);
    } else {
        ret = spapr_vlan_add_rxbuf_to_page(dev, buf);
    }
    if (ret) {
        return ret;
    }

    dev->rx_bufs++;

    /*
     * Give guest some more time to add additional RX buffers before we
     * flush the receive queue, so that e.g. fragmented IP packets can
     * be passed to the guest in one go later (instead of passing single
     * fragments if there is only one receive buffer available).
     */
    timer_mod(dev->rxp_timer, qemu_clock_get_us(QEMU_CLOCK_VIRTUAL) + 500);

    return H_SUCCESS;
}

static target_ulong h_send_logical_lan(PowerPCCPU *cpu,
                                       SpaprMachineState *spapr,
                                       target_ulong opcode, target_ulong *args)
{
    target_ulong reg = args[0];
    target_ulong *bufs = args + 1;
    target_ulong continue_token = args[7];
    SpaprVioDevice *sdev = spapr_vio_find_by_reg(spapr->vio_bus, reg);
    SpaprVioVlan *dev = VIO_SPAPR_VLAN_DEVICE(sdev);
    unsigned total_len;
    uint8_t *p;
    g_autofree uint8_t *lbuf = NULL;
    int i, nbufs;
    int ret;

    trace_spapr_vlan_h_send_logical_lan(reg, continue_token);

    if (!sdev) {
        return H_PARAMETER;
    }

    trace_spapr_vlan_h_send_logical_lan_rxbufs(dev->rx_bufs);

    if (!dev->isopen) {
        return H_DROPPED;
    }

    if (continue_token) {
        return H_HARDWARE; /* FIXME actually handle this */
    }

    total_len = 0;
    for (i = 0; i < 6; i++) {
        trace_spapr_vlan_h_send_logical_lan_buf_desc(bufs[i]);
        if (!(bufs[i] & VLAN_BD_VALID)) {
            break;
        }
        total_len += VLAN_BD_LEN(bufs[i]);
    }

    nbufs = i;
    trace_spapr_vlan_h_send_logical_lan_total(nbufs, total_len);

    if (total_len == 0) {
        return H_SUCCESS;
    }

    if (total_len > MAX_PACKET_SIZE) {
        /* Don't let the guest force too large an allocation */
        return H_RESOURCE;
    }

    lbuf = g_malloc(total_len);
    p = lbuf;
    for (i = 0; i < nbufs; i++) {
        ret = spapr_vio_dma_read(sdev, VLAN_BD_ADDR(bufs[i]),
                                 p, VLAN_BD_LEN(bufs[i]));
        if (ret < 0) {
            return ret;
        }

        p += VLAN_BD_LEN(bufs[i]);
    }

    qemu_send_packet(qemu_get_queue(dev->nic), lbuf, total_len);

    return H_SUCCESS;
}

static target_ulong h_multicast_ctrl(PowerPCCPU *cpu, SpaprMachineState *spapr,
                                     target_ulong opcode, target_ulong *args)
{
    target_ulong reg = args[0];
    SpaprVioDevice *dev = spapr_vio_find_by_reg(spapr->vio_bus, reg);

    if (!dev) {
        return H_PARAMETER;
    }

    return H_SUCCESS;
}

static target_ulong h_change_logical_lan_mac(PowerPCCPU *cpu,
                                             SpaprMachineState *spapr,
                                             target_ulong opcode,
                                             target_ulong *args)
{
    target_ulong reg = args[0];
    target_ulong macaddr = args[1];
    SpaprVioDevice *sdev = spapr_vio_find_by_reg(spapr->vio_bus, reg);
    SpaprVioVlan *dev = VIO_SPAPR_VLAN_DEVICE(sdev);
    int i;

    for (i = 0; i < ETH_ALEN; i++) {
        dev->nicconf.macaddr.a[ETH_ALEN - i - 1] = macaddr & 0xff;
        macaddr >>= 8;
    }

    qemu_format_nic_info_str(qemu_get_queue(dev->nic), dev->nicconf.macaddr.a);

    return H_SUCCESS;
}

static Property spapr_vlan_properties[] = {
    DEFINE_SPAPR_PROPERTIES(SpaprVioVlan, sdev),
    DEFINE_NIC_PROPERTIES(SpaprVioVlan, nicconf),
    DEFINE_PROP_BIT("use-rx-buffer-pools", SpaprVioVlan,
                    compat_flags, SPAPRVLAN_FLAG_RX_BUF_POOLS_BIT, true),
    DEFINE_PROP_END_OF_LIST(),
};

static bool spapr_vlan_rx_buffer_pools_needed(void *opaque)
{
    SpaprVioVlan *dev = opaque;

    return (dev->compat_flags & SPAPRVLAN_FLAG_RX_BUF_POOLS) != 0;
}

static const VMStateDescription vmstate_rx_buffer_pool = {
    .name = "spapr_llan/rx_buffer_pool",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = spapr_vlan_rx_buffer_pools_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_INT32(bufsize, RxBufPool),
        VMSTATE_INT32(count, RxBufPool),
        VMSTATE_UINT64_ARRAY(bds, RxBufPool, RX_POOL_MAX_BDS),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_rx_pools = {
    .name = "spapr_llan/rx_pools",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = spapr_vlan_rx_buffer_pools_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_ARRAY_OF_POINTER_TO_STRUCT(rx_pool, SpaprVioVlan,
                                           RX_MAX_POOLS, 1,
                                           vmstate_rx_buffer_pool, RxBufPool),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_spapr_llan = {
    .name = "spapr_llan",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_SPAPR_VIO(sdev, SpaprVioVlan),
        /* LLAN state */
        VMSTATE_BOOL(isopen, SpaprVioVlan),
        VMSTATE_UINT64(buf_list, SpaprVioVlan),
        VMSTATE_UINT32(add_buf_ptr, SpaprVioVlan),
        VMSTATE_UINT32(use_buf_ptr, SpaprVioVlan),
        VMSTATE_UINT32(rx_bufs, SpaprVioVlan),
        VMSTATE_UINT64(rxq_ptr, SpaprVioVlan),

        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * const []) {
        &vmstate_rx_pools,
        NULL
    }
};

static void spapr_vlan_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SpaprVioDeviceClass *k = VIO_SPAPR_DEVICE_CLASS(klass);

    k->realize = spapr_vlan_realize;
    k->reset = spapr_vlan_reset;
    k->devnode = spapr_vlan_devnode;
    k->dt_name = "l-lan";
    k->dt_type = "network";
    k->dt_compatible = "IBM,l-lan";
    k->signal_mask = 0x1;
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
    device_class_set_props(dc, spapr_vlan_properties);
    k->rtce_window_size = 0x10000000;
    dc->vmsd = &vmstate_spapr_llan;
}

static const TypeInfo spapr_vlan_info = {
    .name          = TYPE_VIO_SPAPR_VLAN_DEVICE,
    .parent        = TYPE_VIO_SPAPR_DEVICE,
    .instance_size = sizeof(SpaprVioVlan),
    .class_init    = spapr_vlan_class_init,
    .instance_init = spapr_vlan_instance_init,
    .instance_finalize = spapr_vlan_instance_finalize,
};

static void spapr_vlan_register_types(void)
{
    spapr_register_hypercall(H_REGISTER_LOGICAL_LAN, h_register_logical_lan);
    spapr_register_hypercall(H_FREE_LOGICAL_LAN, h_free_logical_lan);
    spapr_register_hypercall(H_SEND_LOGICAL_LAN, h_send_logical_lan);
    spapr_register_hypercall(H_ADD_LOGICAL_LAN_BUFFER,
                             h_add_logical_lan_buffer);
    spapr_register_hypercall(H_MULTICAST_CTRL, h_multicast_ctrl);
    spapr_register_hypercall(H_CHANGE_LOGICAL_LAN_MAC,
                             h_change_logical_lan_mac);
    type_register_static(&spapr_vlan_info);
}

type_init(spapr_vlan_register_types)
