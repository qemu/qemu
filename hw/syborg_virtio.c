/*
 * Virtio Syborg bindings
 *
 * Copyright (c) 2009 CodeSourcery
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

#include "syborg.h"
#include "sysbus.h"
#include "virtio.h"
#include "virtio-net.h"

//#define DEBUG_SYBORG_VIRTIO

#ifdef DEBUG_SYBORG_VIRTIO
#define DPRINTF(fmt, ...) \
do { printf("syborg_virtio: " fmt , ## __VA_ARGS__); } while (0)
#define BADF(fmt, ...) \
do { fprintf(stderr, "syborg_virtio: error: " fmt , ## __VA_ARGS__); \
    exit(1);} while (0)
#else
#define DPRINTF(fmt, ...) do {} while(0)
#define BADF(fmt, ...) \
do { fprintf(stderr, "syborg_virtio: error: " fmt , ## __VA_ARGS__);} while (0)
#endif

enum {
    SYBORG_VIRTIO_ID             = 0,
    SYBORG_VIRTIO_DEVTYPE        = 1,
    SYBORG_VIRTIO_HOST_FEATURES  = 2,
    SYBORG_VIRTIO_GUEST_FEATURES = 3,
    SYBORG_VIRTIO_QUEUE_BASE     = 4,
    SYBORG_VIRTIO_QUEUE_NUM      = 5,
    SYBORG_VIRTIO_QUEUE_SEL      = 6,
    SYBORG_VIRTIO_QUEUE_NOTIFY   = 7,
    SYBORG_VIRTIO_STATUS         = 8,
    SYBORG_VIRTIO_INT_ENABLE     = 9,
    SYBORG_VIRTIO_INT_STATUS     = 10
};

#define SYBORG_VIRTIO_CONFIG 0x100

/* Device independent interface.  */

typedef struct {
    SysBusDevice busdev;
    VirtIODevice *vdev;
    qemu_irq irq;
    uint32_t int_enable;
    uint32_t id;
    NICConf nic;
    uint32_t host_features;
    virtio_net_conf net;
} SyborgVirtIOProxy;

static uint32_t syborg_virtio_readl(void *opaque, target_phys_addr_t offset)
{
    SyborgVirtIOProxy *s = opaque;
    VirtIODevice *vdev = s->vdev;
    uint32_t ret;

    DPRINTF("readl 0x%x\n", (int)offset);
    if (offset >= SYBORG_VIRTIO_CONFIG) {
        return virtio_config_readl(vdev, offset - SYBORG_VIRTIO_CONFIG);
    }
    switch(offset >> 2) {
    case SYBORG_VIRTIO_ID:
        ret = SYBORG_ID_VIRTIO;
        break;
    case SYBORG_VIRTIO_DEVTYPE:
        ret = s->id;
        break;
    case SYBORG_VIRTIO_HOST_FEATURES:
        ret = s->host_features;
        break;
    case SYBORG_VIRTIO_GUEST_FEATURES:
        ret = vdev->guest_features;
        break;
    case SYBORG_VIRTIO_QUEUE_BASE:
        ret = virtio_queue_get_addr(vdev, vdev->queue_sel);
        break;
    case SYBORG_VIRTIO_QUEUE_NUM:
        ret = virtio_queue_get_num(vdev, vdev->queue_sel);
        break;
    case SYBORG_VIRTIO_QUEUE_SEL:
        ret = vdev->queue_sel;
        break;
    case SYBORG_VIRTIO_STATUS:
        ret = vdev->status;
        break;
    case SYBORG_VIRTIO_INT_ENABLE:
        ret = s->int_enable;
        break;
    case SYBORG_VIRTIO_INT_STATUS:
        ret = vdev->isr;
        break;
    default:
        BADF("Bad read offset 0x%x\n", (int)offset);
        return 0;
    }
    return ret;
}

static void syborg_virtio_writel(void *opaque, target_phys_addr_t offset,
                                 uint32_t value)
{
    SyborgVirtIOProxy *s = opaque;
    VirtIODevice *vdev = s->vdev;

    DPRINTF("writel 0x%x = 0x%x\n", (int)offset, value);
    if (offset >= SYBORG_VIRTIO_CONFIG) {
        return virtio_config_writel(vdev, offset - SYBORG_VIRTIO_CONFIG,
                                    value);
    }
    switch (offset >> 2) {
    case SYBORG_VIRTIO_GUEST_FEATURES:
        if (vdev->set_features)
            vdev->set_features(vdev, value);
        vdev->guest_features = value;
        break;
    case SYBORG_VIRTIO_QUEUE_BASE:
        if (value == 0)
            virtio_reset(vdev);
        else
            virtio_queue_set_addr(vdev, vdev->queue_sel, value);
        break;
    case SYBORG_VIRTIO_QUEUE_SEL:
        if (value < VIRTIO_PCI_QUEUE_MAX)
            vdev->queue_sel = value;
        break;
    case SYBORG_VIRTIO_QUEUE_NOTIFY:
        virtio_queue_notify(vdev, value);
        break;
    case SYBORG_VIRTIO_STATUS:
        virtio_set_status(vdev, value & 0xFF);
        if (vdev->status == 0)
            virtio_reset(vdev);
        break;
    case SYBORG_VIRTIO_INT_ENABLE:
        s->int_enable = value;
        virtio_update_irq(vdev);
        break;
    case SYBORG_VIRTIO_INT_STATUS:
        vdev->isr &= ~value;
        virtio_update_irq(vdev);
        break;
    default:
        BADF("Bad write offset 0x%x\n", (int)offset);
        break;
    }
}

static uint32_t syborg_virtio_readw(void *opaque, target_phys_addr_t offset)
{
    SyborgVirtIOProxy *s = opaque;
    VirtIODevice *vdev = s->vdev;

    DPRINTF("readw 0x%x\n", (int)offset);
    if (offset >= SYBORG_VIRTIO_CONFIG) {
        return virtio_config_readw(vdev, offset - SYBORG_VIRTIO_CONFIG);
    }
    BADF("Bad halfword read offset 0x%x\n", (int)offset);
    return -1;
}

static void syborg_virtio_writew(void *opaque, target_phys_addr_t offset,
                                 uint32_t value)
{
    SyborgVirtIOProxy *s = opaque;
    VirtIODevice *vdev = s->vdev;

    DPRINTF("writew 0x%x = 0x%x\n", (int)offset, value);
    if (offset >= SYBORG_VIRTIO_CONFIG) {
        return virtio_config_writew(vdev, offset - SYBORG_VIRTIO_CONFIG,
                                    value);
    }
    BADF("Bad halfword write offset 0x%x\n", (int)offset);
}

static uint32_t syborg_virtio_readb(void *opaque, target_phys_addr_t offset)
{
    SyborgVirtIOProxy *s = opaque;
    VirtIODevice *vdev = s->vdev;

    DPRINTF("readb 0x%x\n", (int)offset);
    if (offset >= SYBORG_VIRTIO_CONFIG) {
        return virtio_config_readb(vdev, offset - SYBORG_VIRTIO_CONFIG);
    }
    BADF("Bad byte read offset 0x%x\n", (int)offset);
    return -1;
}

static void syborg_virtio_writeb(void *opaque, target_phys_addr_t offset,
                                 uint32_t value)
{
    SyborgVirtIOProxy *s = opaque;
    VirtIODevice *vdev = s->vdev;

    DPRINTF("writeb 0x%x = 0x%x\n", (int)offset, value);
    if (offset >= SYBORG_VIRTIO_CONFIG) {
        return virtio_config_writeb(vdev, offset - SYBORG_VIRTIO_CONFIG,
                                    value);
    }
    BADF("Bad byte write offset 0x%x\n", (int)offset);
}

static CPUReadMemoryFunc * const syborg_virtio_readfn[] = {
     syborg_virtio_readb,
     syborg_virtio_readw,
     syborg_virtio_readl
};

static CPUWriteMemoryFunc * const syborg_virtio_writefn[] = {
     syborg_virtio_writeb,
     syborg_virtio_writew,
     syborg_virtio_writel
};

static void syborg_virtio_update_irq(void *opaque, uint16_t vector)
{
    SyborgVirtIOProxy *proxy = opaque;
    int level;

    level = proxy->int_enable & proxy->vdev->isr;
    DPRINTF("IRQ %d\n", level);
    qemu_set_irq(proxy->irq, level != 0);
}

static unsigned syborg_virtio_get_features(void *opaque)
{
    SyborgVirtIOProxy *proxy = opaque;
    return proxy->host_features;
}

static VirtIOBindings syborg_virtio_bindings = {
    .notify = syborg_virtio_update_irq,
    .get_features = syborg_virtio_get_features,
};

static int syborg_virtio_init(SyborgVirtIOProxy *proxy, VirtIODevice *vdev)
{
    int iomemtype;

    proxy->vdev = vdev;

    /* Don't support multiple vectors */
    proxy->vdev->nvectors = 0;
    sysbus_init_irq(&proxy->busdev, &proxy->irq);
    iomemtype = cpu_register_io_memory(syborg_virtio_readfn,
                                       syborg_virtio_writefn, proxy,
                                       DEVICE_NATIVE_ENDIAN);
    sysbus_init_mmio(&proxy->busdev, 0x1000, iomemtype);

    proxy->id = ((uint32_t)0x1af4 << 16) | vdev->device_id;

    qemu_register_reset(virtio_reset, vdev);

    virtio_bind_device(vdev, &syborg_virtio_bindings, proxy);
    proxy->host_features |= (0x1 << VIRTIO_F_NOTIFY_ON_EMPTY);
    proxy->host_features = vdev->get_features(vdev, proxy->host_features);
    return 0;
}

/* Device specific bindings.  */

static int syborg_virtio_net_init(SysBusDevice *dev)
{
    VirtIODevice *vdev;
    SyborgVirtIOProxy *proxy = FROM_SYSBUS(SyborgVirtIOProxy, dev);

    vdev = virtio_net_init(&dev->qdev, &proxy->nic, &proxy->net);
    return syborg_virtio_init(proxy, vdev);
}

static SysBusDeviceInfo syborg_virtio_net_info = {
    .init = syborg_virtio_net_init,
    .qdev.name  = "syborg,virtio-net",
    .qdev.size  = sizeof(SyborgVirtIOProxy),
    .qdev.props = (Property[]) {
        DEFINE_NIC_PROPERTIES(SyborgVirtIOProxy, nic),
        DEFINE_VIRTIO_NET_FEATURES(SyborgVirtIOProxy, host_features),
        DEFINE_PROP_UINT32("x-txtimer", SyborgVirtIOProxy,
                           net.txtimer, TX_TIMER_INTERVAL),
        DEFINE_PROP_INT32("x-txburst", SyborgVirtIOProxy,
                          net.txburst, TX_BURST),
        DEFINE_PROP_STRING("tx", SyborgVirtIOProxy, net.tx),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void syborg_virtio_register_devices(void)
{
    sysbus_register_withprop(&syborg_virtio_net_info);
}

device_init(syborg_virtio_register_devices)
