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
#include "hw.h"
#include "net.h"
#include "hw/qdev.h"
#include "hw/spapr.h"
#include "hw/spapr_vio.h"

#include <libfdt.h>

#define ETH_ALEN        6
#define MAX_PACKET_SIZE 65536

/*#define DEBUG*/

#ifdef DEBUG
#define dprintf(fmt...) do { fprintf(stderr, fmt); } while (0)
#else
#define dprintf(fmt...)
#endif

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
#define VLAN_MAX_BUFS        ((SPAPR_VIO_TCE_PAGE_SIZE - VLAN_RX_BDS_OFF) / 8)

typedef struct VIOsPAPRVLANDevice {
    VIOsPAPRDevice sdev;
    NICConf nicconf;
    NICState *nic;
    int isopen;
    target_ulong buf_list;
    int add_buf_ptr, use_buf_ptr, rx_bufs;
    target_ulong rxq_ptr;
} VIOsPAPRVLANDevice;

static int spapr_vlan_can_receive(VLANClientState *nc)
{
    VIOsPAPRVLANDevice *dev = DO_UPCAST(NICState, nc, nc)->opaque;

    return (dev->isopen && dev->rx_bufs > 0);
}

static ssize_t spapr_vlan_receive(VLANClientState *nc, const uint8_t *buf,
                                  size_t size)
{
    VIOsPAPRDevice *sdev = DO_UPCAST(NICState, nc, nc)->opaque;
    VIOsPAPRVLANDevice *dev = (VIOsPAPRVLANDevice *)sdev;
    vlan_bd_t rxq_bd = ldq_tce(sdev, dev->buf_list + VLAN_RXQ_BD_OFF);
    vlan_bd_t bd;
    int buf_ptr = dev->use_buf_ptr;
    uint64_t handle;
    uint8_t control;

    dprintf("spapr_vlan_receive() [%s] rx_bufs=%d\n", sdev->qdev.id,
            dev->rx_bufs);

    if (!dev->isopen) {
        return -1;
    }

    if (!dev->rx_bufs) {
        return -1;
    }

    do {
        buf_ptr += 8;
        if (buf_ptr >= SPAPR_VIO_TCE_PAGE_SIZE) {
            buf_ptr = VLAN_RX_BDS_OFF;
        }

        bd = ldq_tce(sdev, dev->buf_list + buf_ptr);
        dprintf("use_buf_ptr=%d bd=0x%016llx\n",
                buf_ptr, (unsigned long long)bd);
    } while ((!(bd & VLAN_BD_VALID) || (VLAN_BD_LEN(bd) < (size + 8)))
             && (buf_ptr != dev->use_buf_ptr));

    if (!(bd & VLAN_BD_VALID) || (VLAN_BD_LEN(bd) < (size + 8))) {
        /* Failed to find a suitable buffer */
        return -1;
    }

    /* Remove the buffer from the pool */
    dev->rx_bufs--;
    dev->use_buf_ptr = buf_ptr;
    stq_tce(sdev, dev->buf_list + dev->use_buf_ptr, 0);

    dprintf("Found buffer: ptr=%d num=%d\n", dev->use_buf_ptr, dev->rx_bufs);

    /* Transfer the packet data */
    if (spapr_tce_dma_write(sdev, VLAN_BD_ADDR(bd) + 8, buf, size) < 0) {
        return -1;
    }

    dprintf("spapr_vlan_receive: DMA write completed\n");

    /* Update the receive queue */
    control = VLAN_RXQC_TOGGLE | VLAN_RXQC_VALID;
    if (rxq_bd & VLAN_BD_TOGGLE) {
        control ^= VLAN_RXQC_TOGGLE;
    }

    handle = ldq_tce(sdev, VLAN_BD_ADDR(bd));
    stq_tce(sdev, VLAN_BD_ADDR(rxq_bd) + dev->rxq_ptr + 8, handle);
    stw_tce(sdev, VLAN_BD_ADDR(rxq_bd) + dev->rxq_ptr + 4, size);
    sth_tce(sdev, VLAN_BD_ADDR(rxq_bd) + dev->rxq_ptr + 2, 8);
    stb_tce(sdev, VLAN_BD_ADDR(rxq_bd) + dev->rxq_ptr, control);

    dprintf("wrote rxq entry (ptr=0x%llx): 0x%016llx 0x%016llx\n",
            (unsigned long long)dev->rxq_ptr,
            (unsigned long long)ldq_tce(sdev, VLAN_BD_ADDR(rxq_bd) +
                                        dev->rxq_ptr),
            (unsigned long long)ldq_tce(sdev, VLAN_BD_ADDR(rxq_bd) +
                                        dev->rxq_ptr + 8));

    dev->rxq_ptr += 16;
    if (dev->rxq_ptr >= VLAN_BD_LEN(rxq_bd)) {
        dev->rxq_ptr = 0;
        stq_tce(sdev, dev->buf_list + VLAN_RXQ_BD_OFF, rxq_bd ^ VLAN_BD_TOGGLE);
    }

    if (sdev->signal_state & 1) {
        qemu_irq_pulse(sdev->qirq);
    }

    return size;
}

static NetClientInfo net_spapr_vlan_info = {
    .type = NET_CLIENT_TYPE_NIC,
    .size = sizeof(NICState),
    .can_receive = spapr_vlan_can_receive,
    .receive = spapr_vlan_receive,
};

static int spapr_vlan_init(VIOsPAPRDevice *sdev)
{
    VIOsPAPRVLANDevice *dev = (VIOsPAPRVLANDevice *)sdev;
    VIOsPAPRBus *bus;

    bus = DO_UPCAST(VIOsPAPRBus, bus, sdev->qdev.parent_bus);

    qemu_macaddr_default_if_unset(&dev->nicconf.macaddr);

    dev->nic = qemu_new_nic(&net_spapr_vlan_info, &dev->nicconf,
                            sdev->qdev.info->name, sdev->qdev.id, dev);
    qemu_format_nic_info_str(&dev->nic->nc, dev->nicconf.macaddr.a);

    return 0;
}

void spapr_vlan_create(VIOsPAPRBus *bus, uint32_t reg, NICInfo *nd,
                       qemu_irq qirq, uint32_t vio_irq_num)
{
    DeviceState *dev;
    VIOsPAPRDevice *sdev;

    dev = qdev_create(&bus->bus, "spapr-vlan");
    qdev_prop_set_uint32(dev, "reg", reg);

    qdev_set_nic_properties(dev, nd);

    qdev_init_nofail(dev);
    sdev = (VIOsPAPRDevice *)dev;
    sdev->qirq = qirq;
    sdev->vio_irq_num = vio_irq_num;
}

static int spapr_vlan_devnode(VIOsPAPRDevice *dev, void *fdt, int node_off)
{
    VIOsPAPRVLANDevice *vdev = (VIOsPAPRVLANDevice *)dev;
    uint8_t padded_mac[8] = {0, 0};
    int ret;

    /* Some old phyp versions give the mac address in an 8-byte
     * property.  The kernel driver has an insane workaround for this;
     * rather than doing the obvious thing and checking the property
     * length, it checks whether the first byte has 0b10 in the low
     * bits.  If a correct 6-byte property has a different first byte
     * the kernel will get the wrong mac address, overrunning its
     * buffer in the process (read only, thank goodness).
     *
     * Here we workaround the kernel workaround by always supplying an
     * 8-byte property, with the mac address in the last six bytes */
    memcpy(&padded_mac[2], &vdev->nicconf.macaddr, ETH_ALEN);
    ret = fdt_setprop(fdt, node_off, "local-mac-address",
                      padded_mac, sizeof(padded_mac));
    if (ret < 0) {
        return ret;
    }

    ret = fdt_setprop_cell(fdt, node_off, "ibm,mac-address-filters", 0);
    if (ret < 0) {
        return ret;
    }

    return 0;
}

static int check_bd(VIOsPAPRVLANDevice *dev, vlan_bd_t bd,
                    target_ulong alignment)
{
    if ((VLAN_BD_ADDR(bd) % alignment)
        || (VLAN_BD_LEN(bd) % alignment)) {
        return -1;
    }

    if (spapr_vio_check_tces(&dev->sdev, VLAN_BD_ADDR(bd),
                             VLAN_BD_LEN(bd), SPAPR_TCE_RW) != 0) {
        return -1;
    }

    return 0;
}

static target_ulong h_register_logical_lan(CPUState *env,
                                           sPAPREnvironment *spapr,
                                           target_ulong opcode,
                                           target_ulong *args)
{
    target_ulong reg = args[0];
    target_ulong buf_list = args[1];
    target_ulong rec_queue = args[2];
    target_ulong filter_list = args[3];
    VIOsPAPRDevice *sdev = spapr_vio_find_by_reg(spapr->vio_bus, reg);
    VIOsPAPRVLANDevice *dev = (VIOsPAPRVLANDevice *)sdev;
    vlan_bd_t filter_list_bd;

    if (!dev) {
        return H_PARAMETER;
    }

    if (dev->isopen) {
        hcall_dprintf("H_REGISTER_LOGICAL_LAN called twice without "
                      "H_FREE_LOGICAL_LAN\n");
        return H_RESOURCE;
    }

    if (check_bd(dev, VLAN_VALID_BD(buf_list, SPAPR_VIO_TCE_PAGE_SIZE),
                 SPAPR_VIO_TCE_PAGE_SIZE) < 0) {
        hcall_dprintf("Bad buf_list 0x" TARGET_FMT_lx " for "
                      "H_REGISTER_LOGICAL_LAN\n", buf_list);
        return H_PARAMETER;
    }

    filter_list_bd = VLAN_VALID_BD(filter_list, SPAPR_VIO_TCE_PAGE_SIZE);
    if (check_bd(dev, filter_list_bd, SPAPR_VIO_TCE_PAGE_SIZE) < 0) {
        hcall_dprintf("Bad filter_list 0x" TARGET_FMT_lx " for "
                      "H_REGISTER_LOGICAL_LAN\n", filter_list);
        return H_PARAMETER;
    }

    if (!(rec_queue & VLAN_BD_VALID)
        || (check_bd(dev, rec_queue, VLAN_RQ_ALIGNMENT) < 0)) {
        hcall_dprintf("Bad receive queue for H_REGISTER_LOGICAL_LAN\n");
        return H_PARAMETER;
    }

    dev->buf_list = buf_list;
    sdev->signal_state = 0;

    rec_queue &= ~VLAN_BD_TOGGLE;

    /* Initialize the buffer list */
    stq_tce(sdev, buf_list, rec_queue);
    stq_tce(sdev, buf_list + 8, filter_list_bd);
    spapr_tce_dma_zero(sdev, buf_list + VLAN_RX_BDS_OFF,
                       SPAPR_VIO_TCE_PAGE_SIZE - VLAN_RX_BDS_OFF);
    dev->add_buf_ptr = VLAN_RX_BDS_OFF - 8;
    dev->use_buf_ptr = VLAN_RX_BDS_OFF - 8;
    dev->rx_bufs = 0;
    dev->rxq_ptr = 0;

    /* Initialize the receive queue */
    spapr_tce_dma_zero(sdev, VLAN_BD_ADDR(rec_queue), VLAN_BD_LEN(rec_queue));

    dev->isopen = 1;
    return H_SUCCESS;
}


static target_ulong h_free_logical_lan(CPUState *env, sPAPREnvironment *spapr,
                                       target_ulong opcode, target_ulong *args)
{
    target_ulong reg = args[0];
    VIOsPAPRDevice *sdev = spapr_vio_find_by_reg(spapr->vio_bus, reg);
    VIOsPAPRVLANDevice *dev = (VIOsPAPRVLANDevice *)sdev;

    if (!dev) {
        return H_PARAMETER;
    }

    if (!dev->isopen) {
        hcall_dprintf("H_FREE_LOGICAL_LAN called without "
                      "H_REGISTER_LOGICAL_LAN\n");
        return H_RESOURCE;
    }

    dev->buf_list = 0;
    dev->rx_bufs = 0;
    dev->isopen = 0;
    return H_SUCCESS;
}

static target_ulong h_add_logical_lan_buffer(CPUState *env,
                                             sPAPREnvironment *spapr,
                                             target_ulong opcode,
                                             target_ulong *args)
{
    target_ulong reg = args[0];
    target_ulong buf = args[1];
    VIOsPAPRDevice *sdev = spapr_vio_find_by_reg(spapr->vio_bus, reg);
    VIOsPAPRVLANDevice *dev = (VIOsPAPRVLANDevice *)sdev;
    vlan_bd_t bd;

    dprintf("H_ADD_LOGICAL_LAN_BUFFER(0x" TARGET_FMT_lx
            ", 0x" TARGET_FMT_lx ")\n", reg, buf);

    if (!sdev) {
        hcall_dprintf("Wrong device in h_add_logical_lan_buffer\n");
        return H_PARAMETER;
    }

    if ((check_bd(dev, buf, 4) < 0)
        || (VLAN_BD_LEN(buf) < 16)) {
        hcall_dprintf("Bad buffer enqueued in h_add_logical_lan_buffer\n");
        return H_PARAMETER;
    }

    if (!dev->isopen || dev->rx_bufs >= VLAN_MAX_BUFS) {
        return H_RESOURCE;
    }

    do {
        dev->add_buf_ptr += 8;
        if (dev->add_buf_ptr >= SPAPR_VIO_TCE_PAGE_SIZE) {
            dev->add_buf_ptr = VLAN_RX_BDS_OFF;
        }

        bd = ldq_tce(sdev, dev->buf_list + dev->add_buf_ptr);
    } while (bd & VLAN_BD_VALID);

    stq_tce(sdev, dev->buf_list + dev->add_buf_ptr, buf);

    dev->rx_bufs++;

    dprintf("h_add_logical_lan_buffer():  Added buf  ptr=%d  rx_bufs=%d"
            " bd=0x%016llx\n", dev->add_buf_ptr, dev->rx_bufs,
            (unsigned long long)buf);

    return H_SUCCESS;
}

static target_ulong h_send_logical_lan(CPUState *env, sPAPREnvironment *spapr,
                                       target_ulong opcode, target_ulong *args)
{
    target_ulong reg = args[0];
    target_ulong *bufs = args + 1;
    target_ulong continue_token = args[7];
    VIOsPAPRDevice *sdev = spapr_vio_find_by_reg(spapr->vio_bus, reg);
    VIOsPAPRVLANDevice *dev = (VIOsPAPRVLANDevice *)sdev;
    unsigned total_len;
    uint8_t *lbuf, *p;
    int i, nbufs;
    int ret;

    dprintf("H_SEND_LOGICAL_LAN(0x" TARGET_FMT_lx ", <bufs>, 0x"
            TARGET_FMT_lx ")\n", reg, continue_token);

    if (!sdev) {
        return H_PARAMETER;
    }

    dprintf("rxbufs = %d\n", dev->rx_bufs);

    if (!dev->isopen) {
        return H_DROPPED;
    }

    if (continue_token) {
        return H_HARDWARE; /* FIXME actually handle this */
    }

    total_len = 0;
    for (i = 0; i < 6; i++) {
        dprintf("   buf desc: 0x" TARGET_FMT_lx "\n", bufs[i]);
        if (!(bufs[i] & VLAN_BD_VALID)) {
            break;
        }
        total_len += VLAN_BD_LEN(bufs[i]);
    }

    nbufs = i;
    dprintf("h_send_logical_lan() %d buffers, total length 0x%x\n",
            nbufs, total_len);

    if (total_len == 0) {
        return H_SUCCESS;
    }

    if (total_len > MAX_PACKET_SIZE) {
        /* Don't let the guest force too large an allocation */
        return H_RESOURCE;
    }

    lbuf = alloca(total_len);
    p = lbuf;
    for (i = 0; i < nbufs; i++) {
        ret = spapr_tce_dma_read(sdev, VLAN_BD_ADDR(bufs[i]),
                                 p, VLAN_BD_LEN(bufs[i]));
        if (ret < 0) {
            return ret;
        }

        p += VLAN_BD_LEN(bufs[i]);
    }

    qemu_send_packet(&dev->nic->nc, lbuf, total_len);

    return H_SUCCESS;
}

static target_ulong h_multicast_ctrl(CPUState *env, sPAPREnvironment *spapr,
                                     target_ulong opcode, target_ulong *args)
{
    target_ulong reg = args[0];
    VIOsPAPRDevice *dev = spapr_vio_find_by_reg(spapr->vio_bus, reg);

    if (!dev) {
        return H_PARAMETER;
    }

    return H_SUCCESS;
}

static void vlan_hcalls(VIOsPAPRBus *bus)
{
    spapr_register_hypercall(H_REGISTER_LOGICAL_LAN, h_register_logical_lan);
    spapr_register_hypercall(H_FREE_LOGICAL_LAN, h_free_logical_lan);
    spapr_register_hypercall(H_SEND_LOGICAL_LAN, h_send_logical_lan);
    spapr_register_hypercall(H_ADD_LOGICAL_LAN_BUFFER,
                             h_add_logical_lan_buffer);
    spapr_register_hypercall(H_MULTICAST_CTRL, h_multicast_ctrl);
}

static VIOsPAPRDeviceInfo spapr_vlan = {
    .init = spapr_vlan_init,
    .devnode = spapr_vlan_devnode,
    .dt_name = "l-lan",
    .dt_type = "network",
    .dt_compatible = "IBM,l-lan",
    .signal_mask = 0x1,
    .hcalls = vlan_hcalls,
    .qdev.name = "spapr-vlan",
    .qdev.size = sizeof(VIOsPAPRVLANDevice),
    .qdev.props = (Property[]) {
        DEFINE_PROP_UINT32("reg", VIOsPAPRDevice, reg, 0x1000),
        DEFINE_PROP_UINT32("dma-window", VIOsPAPRDevice, rtce_window_size,
                           0x10000000),
        DEFINE_NIC_PROPERTIES(VIOsPAPRVLANDevice, nicconf),
        DEFINE_PROP_END_OF_LIST(),
    },
};

static void spapr_vlan_register(void)
{
    spapr_vio_bus_register_withprop(&spapr_vlan);
}
device_init(spapr_vlan_register);
