/*
 * QEMU sPAPR VIO code
 *
 * Copyright (c) 2010 David Gibson, IBM Corporation <dwg@au1.ibm.com>
 * Based on the s390 virtio bus code:
 * Copyright (c) 2009 Alexander Graf <agraf@suse.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw.h"
#include "sysemu.h"
#include "boards.h"
#include "monitor.h"
#include "loader.h"
#include "elf.h"
#include "hw/sysbus.h"
#include "kvm.h"
#include "device_tree.h"
#include "kvm_ppc.h"

#include "hw/spapr.h"
#include "hw/spapr_vio.h"
#include "hw/xics.h"

#ifdef CONFIG_FDT
#include <libfdt.h>
#endif /* CONFIG_FDT */

/* #define DEBUG_SPAPR */
/* #define DEBUG_TCE */

#ifdef DEBUG_SPAPR
#define dprintf(fmt, ...) \
    do { fprintf(stderr, fmt, ## __VA_ARGS__); } while (0)
#else
#define dprintf(fmt, ...) \
    do { } while (0)
#endif

static struct BusInfo spapr_vio_bus_info = {
    .name       = "spapr-vio",
    .size       = sizeof(VIOsPAPRBus),
    .props = (Property[]) {
        DEFINE_PROP_UINT32("irq", VIOsPAPRDevice, vio_irq_num, 0), \
        DEFINE_PROP_END_OF_LIST(),
    },
};

VIOsPAPRDevice *spapr_vio_find_by_reg(VIOsPAPRBus *bus, uint32_t reg)
{
    DeviceState *qdev;
    VIOsPAPRDevice *dev = NULL;

    QTAILQ_FOREACH(qdev, &bus->bus.children, sibling) {
        dev = (VIOsPAPRDevice *)qdev;
        if (dev->reg == reg) {
            return dev;
        }
    }

    return NULL;
}

static char *vio_format_dev_name(VIOsPAPRDevice *dev)
{
    VIOsPAPRDeviceInfo *info = (VIOsPAPRDeviceInfo *)dev->qdev.info;
    char *name;

    /* Device tree style name device@reg */
    if (asprintf(&name, "%s@%x", info->dt_name, dev->reg) < 0) {
        return NULL;
    }

    return name;
}

#ifdef CONFIG_FDT
static int vio_make_devnode(VIOsPAPRDevice *dev,
                            void *fdt)
{
    VIOsPAPRDeviceInfo *info = (VIOsPAPRDeviceInfo *)dev->qdev.info;
    int vdevice_off, node_off, ret;
    char *dt_name;

    vdevice_off = fdt_path_offset(fdt, "/vdevice");
    if (vdevice_off < 0) {
        return vdevice_off;
    }

    dt_name = vio_format_dev_name(dev);
    if (!dt_name) {
        return -ENOMEM;
    }

    node_off = fdt_add_subnode(fdt, vdevice_off, dt_name);
    free(dt_name);
    if (node_off < 0) {
        return node_off;
    }

    ret = fdt_setprop_cell(fdt, node_off, "reg", dev->reg);
    if (ret < 0) {
        return ret;
    }

    if (info->dt_type) {
        ret = fdt_setprop_string(fdt, node_off, "device_type",
                                 info->dt_type);
        if (ret < 0) {
            return ret;
        }
    }

    if (info->dt_compatible) {
        ret = fdt_setprop_string(fdt, node_off, "compatible",
                                 info->dt_compatible);
        if (ret < 0) {
            return ret;
        }
    }

    if (dev->qirq) {
        uint32_t ints_prop[] = {cpu_to_be32(dev->vio_irq_num), 0};

        ret = fdt_setprop(fdt, node_off, "interrupts", ints_prop,
                          sizeof(ints_prop));
        if (ret < 0) {
            return ret;
        }
    }

    if (dev->rtce_window_size) {
        uint32_t dma_prop[] = {cpu_to_be32(dev->reg),
                               0, 0,
                               0, cpu_to_be32(dev->rtce_window_size)};

        ret = fdt_setprop_cell(fdt, node_off, "ibm,#dma-address-cells", 2);
        if (ret < 0) {
            return ret;
        }

        ret = fdt_setprop_cell(fdt, node_off, "ibm,#dma-size-cells", 2);
        if (ret < 0) {
            return ret;
        }

        ret = fdt_setprop(fdt, node_off, "ibm,my-dma-window", dma_prop,
                          sizeof(dma_prop));
        if (ret < 0) {
            return ret;
        }
    }

    if (info->devnode) {
        ret = (info->devnode)(dev, fdt, node_off);
        if (ret < 0) {
            return ret;
        }
    }

    return node_off;
}
#endif /* CONFIG_FDT */

/*
 * RTCE handling
 */

static void rtce_init(VIOsPAPRDevice *dev)
{
    size_t size = (dev->rtce_window_size >> SPAPR_VIO_TCE_PAGE_SHIFT)
        * sizeof(VIOsPAPR_RTCE);

    if (size) {
        dev->rtce_table = kvmppc_create_spapr_tce(dev->reg,
                                                  dev->rtce_window_size,
                                                  &dev->kvmtce_fd);

        if (!dev->rtce_table) {
            dev->rtce_table = g_malloc0(size);
        }
    }
}

static target_ulong h_put_tce(CPUState *env, sPAPREnvironment *spapr,
                              target_ulong opcode, target_ulong *args)
{
    target_ulong liobn = args[0];
    target_ulong ioba = args[1];
    target_ulong tce = args[2];
    VIOsPAPRDevice *dev = spapr_vio_find_by_reg(spapr->vio_bus, liobn);
    VIOsPAPR_RTCE *rtce;

    if (!dev) {
        hcall_dprintf("spapr_vio_put_tce on non-existent LIOBN "
                      TARGET_FMT_lx "\n", liobn);
        return H_PARAMETER;
    }

    ioba &= ~(SPAPR_VIO_TCE_PAGE_SIZE - 1);

#ifdef DEBUG_TCE
    fprintf(stderr, "spapr_vio_put_tce on %s  ioba 0x" TARGET_FMT_lx
            "  TCE 0x" TARGET_FMT_lx "\n", dev->qdev.id, ioba, tce);
#endif

    if (ioba >= dev->rtce_window_size) {
        hcall_dprintf("spapr_vio_put_tce on out-of-boards IOBA 0x"
                      TARGET_FMT_lx "\n", ioba);
        return H_PARAMETER;
    }

    rtce = dev->rtce_table + (ioba >> SPAPR_VIO_TCE_PAGE_SHIFT);
    rtce->tce = tce;

    return H_SUCCESS;
}

int spapr_vio_check_tces(VIOsPAPRDevice *dev, target_ulong ioba,
                         target_ulong len, enum VIOsPAPR_TCEAccess access)
{
    int start, end, i;

    start = ioba >> SPAPR_VIO_TCE_PAGE_SHIFT;
    end = (ioba + len - 1) >> SPAPR_VIO_TCE_PAGE_SHIFT;

    for (i = start; i <= end; i++) {
        if ((dev->rtce_table[i].tce & access) != access) {
#ifdef DEBUG_TCE
            fprintf(stderr, "FAIL on %d\n", i);
#endif
            return -1;
        }
    }

    return 0;
}

int spapr_tce_dma_write(VIOsPAPRDevice *dev, uint64_t taddr, const void *buf,
                        uint32_t size)
{
#ifdef DEBUG_TCE
    fprintf(stderr, "spapr_tce_dma_write taddr=0x%llx size=0x%x\n",
            (unsigned long long)taddr, size);
#endif

    /* Check for bypass */
    if (dev->flags & VIO_PAPR_FLAG_DMA_BYPASS) {
        cpu_physical_memory_write(taddr, buf, size);
        return 0;
    }

    while (size) {
        uint64_t tce;
        uint32_t lsize;
        uint64_t txaddr;

        /* Check if we are in bound */
        if (taddr >= dev->rtce_window_size) {
#ifdef DEBUG_TCE
            fprintf(stderr, "spapr_tce_dma_write out of bounds\n");
#endif
            return H_DEST_PARM;
        }
        tce = dev->rtce_table[taddr >> SPAPR_VIO_TCE_PAGE_SHIFT].tce;

        /* How much til end of page ? */
        lsize = MIN(size, ((~taddr) & SPAPR_VIO_TCE_PAGE_MASK) + 1);

        /* Check TCE */
        if (!(tce & 2)) {
            return H_DEST_PARM;
        }

        /* Translate */
        txaddr = (tce & ~SPAPR_VIO_TCE_PAGE_MASK) |
            (taddr & SPAPR_VIO_TCE_PAGE_MASK);

#ifdef DEBUG_TCE
        fprintf(stderr, " -> write to txaddr=0x%llx, size=0x%x\n",
                (unsigned long long)txaddr, lsize);
#endif

        /* Do it */
        cpu_physical_memory_write(txaddr, buf, lsize);
        buf += lsize;
        taddr += lsize;
        size -= lsize;
    }
    return 0;
}

int spapr_tce_dma_zero(VIOsPAPRDevice *dev, uint64_t taddr, uint32_t size)
{
    /* FIXME: allocating a temp buffer is nasty, but just stepping
     * through writing zeroes is awkward.  This will do for now. */
    uint8_t zeroes[size];

#ifdef DEBUG_TCE
    fprintf(stderr, "spapr_tce_dma_zero taddr=0x%llx size=0x%x\n",
            (unsigned long long)taddr, size);
#endif

    memset(zeroes, 0, size);
    return spapr_tce_dma_write(dev, taddr, zeroes, size);
}

void stb_tce(VIOsPAPRDevice *dev, uint64_t taddr, uint8_t val)
{
    spapr_tce_dma_write(dev, taddr, &val, sizeof(val));
}

void sth_tce(VIOsPAPRDevice *dev, uint64_t taddr, uint16_t val)
{
    val = tswap16(val);
    spapr_tce_dma_write(dev, taddr, &val, sizeof(val));
}


void stw_tce(VIOsPAPRDevice *dev, uint64_t taddr, uint32_t val)
{
    val = tswap32(val);
    spapr_tce_dma_write(dev, taddr, &val, sizeof(val));
}

void stq_tce(VIOsPAPRDevice *dev, uint64_t taddr, uint64_t val)
{
    val = tswap64(val);
    spapr_tce_dma_write(dev, taddr, &val, sizeof(val));
}

int spapr_tce_dma_read(VIOsPAPRDevice *dev, uint64_t taddr, void *buf,
                       uint32_t size)
{
#ifdef DEBUG_TCE
    fprintf(stderr, "spapr_tce_dma_write taddr=0x%llx size=0x%x\n",
            (unsigned long long)taddr, size);
#endif

    /* Check for bypass */
    if (dev->flags & VIO_PAPR_FLAG_DMA_BYPASS) {
        cpu_physical_memory_read(taddr, buf, size);
        return 0;
    }

    while (size) {
        uint64_t tce;
        uint32_t lsize;
        uint64_t txaddr;

        /* Check if we are in bound */
        if (taddr >= dev->rtce_window_size) {
#ifdef DEBUG_TCE
            fprintf(stderr, "spapr_tce_dma_read out of bounds\n");
#endif
            return H_DEST_PARM;
        }
        tce = dev->rtce_table[taddr >> SPAPR_VIO_TCE_PAGE_SHIFT].tce;

        /* How much til end of page ? */
        lsize = MIN(size, ((~taddr) & SPAPR_VIO_TCE_PAGE_MASK) + 1);

        /* Check TCE */
        if (!(tce & 1)) {
            return H_DEST_PARM;
        }

        /* Translate */
        txaddr = (tce & ~SPAPR_VIO_TCE_PAGE_MASK) |
            (taddr & SPAPR_VIO_TCE_PAGE_MASK);

#ifdef DEBUG_TCE
        fprintf(stderr, " -> write to txaddr=0x%llx, size=0x%x\n",
                (unsigned long long)txaddr, lsize);
#endif
        /* Do it */
        cpu_physical_memory_read(txaddr, buf, lsize);
        buf += lsize;
        taddr += lsize;
        size -= lsize;
    }
    return H_SUCCESS;
}

uint64_t ldq_tce(VIOsPAPRDevice *dev, uint64_t taddr)
{
    uint64_t val;

    spapr_tce_dma_read(dev, taddr, &val, sizeof(val));
    return tswap64(val);
}

/*
 * CRQ handling
 */
static target_ulong h_reg_crq(CPUState *env, sPAPREnvironment *spapr,
                              target_ulong opcode, target_ulong *args)
{
    target_ulong reg = args[0];
    target_ulong queue_addr = args[1];
    target_ulong queue_len = args[2];
    VIOsPAPRDevice *dev = spapr_vio_find_by_reg(spapr->vio_bus, reg);

    if (!dev) {
        hcall_dprintf("h_reg_crq on non-existent unit 0x"
                      TARGET_FMT_lx "\n", reg);
        return H_PARAMETER;
    }

    /* We can't grok a queue size bigger than 256M for now */
    if (queue_len < 0x1000 || queue_len > 0x10000000) {
        hcall_dprintf("h_reg_crq, queue size too small or too big (0x%llx)\n",
                      (unsigned long long)queue_len);
        return H_PARAMETER;
    }

    /* Check queue alignment */
    if (queue_addr & 0xfff) {
        hcall_dprintf("h_reg_crq, queue not aligned (0x%llx)\n",
                      (unsigned long long)queue_addr);
        return H_PARAMETER;
    }

    /* Check if device supports CRQs */
    if (!dev->crq.SendFunc) {
        return H_NOT_FOUND;
    }


    /* Already a queue ? */
    if (dev->crq.qsize) {
        return H_RESOURCE;
    }
    dev->crq.qladdr = queue_addr;
    dev->crq.qsize = queue_len;
    dev->crq.qnext = 0;

    dprintf("CRQ for dev 0x" TARGET_FMT_lx " registered at 0x"
            TARGET_FMT_lx "/0x" TARGET_FMT_lx "\n",
            reg, queue_addr, queue_len);
    return H_SUCCESS;
}

static target_ulong h_free_crq(CPUState *env, sPAPREnvironment *spapr,
                               target_ulong opcode, target_ulong *args)
{
    target_ulong reg = args[0];
    VIOsPAPRDevice *dev = spapr_vio_find_by_reg(spapr->vio_bus, reg);

    if (!dev) {
        hcall_dprintf("h_free_crq on non-existent unit 0x"
                      TARGET_FMT_lx "\n", reg);
        return H_PARAMETER;
    }

    dev->crq.qladdr = 0;
    dev->crq.qsize = 0;
    dev->crq.qnext = 0;

    dprintf("CRQ for dev 0x" TARGET_FMT_lx " freed\n", reg);

    return H_SUCCESS;
}

static target_ulong h_send_crq(CPUState *env, sPAPREnvironment *spapr,
                               target_ulong opcode, target_ulong *args)
{
    target_ulong reg = args[0];
    target_ulong msg_hi = args[1];
    target_ulong msg_lo = args[2];
    VIOsPAPRDevice *dev = spapr_vio_find_by_reg(spapr->vio_bus, reg);
    uint64_t crq_mangle[2];

    if (!dev) {
        hcall_dprintf("h_send_crq on non-existent unit 0x"
                      TARGET_FMT_lx "\n", reg);
        return H_PARAMETER;
    }
    crq_mangle[0] = cpu_to_be64(msg_hi);
    crq_mangle[1] = cpu_to_be64(msg_lo);

    if (dev->crq.SendFunc) {
        return dev->crq.SendFunc(dev, (uint8_t *)crq_mangle);
    }

    return H_HARDWARE;
}

static target_ulong h_enable_crq(CPUState *env, sPAPREnvironment *spapr,
                                 target_ulong opcode, target_ulong *args)
{
    target_ulong reg = args[0];
    VIOsPAPRDevice *dev = spapr_vio_find_by_reg(spapr->vio_bus, reg);

    if (!dev) {
        hcall_dprintf("h_enable_crq on non-existent unit 0x"
                      TARGET_FMT_lx "\n", reg);
        return H_PARAMETER;
    }

    return 0;
}

/* Returns negative error, 0 success, or positive: queue full */
int spapr_vio_send_crq(VIOsPAPRDevice *dev, uint8_t *crq)
{
    int rc;
    uint8_t byte;

    if (!dev->crq.qsize) {
        fprintf(stderr, "spapr_vio_send_creq on uninitialized queue\n");
        return -1;
    }

    /* Maybe do a fast path for KVM just writing to the pages */
    rc = spapr_tce_dma_read(dev, dev->crq.qladdr + dev->crq.qnext, &byte, 1);
    if (rc) {
        return rc;
    }
    if (byte != 0) {
        return 1;
    }

    rc = spapr_tce_dma_write(dev, dev->crq.qladdr + dev->crq.qnext + 8,
                             &crq[8], 8);
    if (rc) {
        return rc;
    }

    kvmppc_eieio();

    rc = spapr_tce_dma_write(dev, dev->crq.qladdr + dev->crq.qnext, crq, 8);
    if (rc) {
        return rc;
    }

    dev->crq.qnext = (dev->crq.qnext + 16) % dev->crq.qsize;

    if (dev->signal_state & 1) {
        qemu_irq_pulse(dev->qirq);
    }

    return 0;
}

/* "quiesce" handling */

static void spapr_vio_quiesce_one(VIOsPAPRDevice *dev)
{
    dev->flags &= ~VIO_PAPR_FLAG_DMA_BYPASS;

    if (dev->rtce_table) {
        size_t size = (dev->rtce_window_size >> SPAPR_VIO_TCE_PAGE_SHIFT)
            * sizeof(VIOsPAPR_RTCE);
        memset(dev->rtce_table, 0, size);
    }

    dev->crq.qladdr = 0;
    dev->crq.qsize = 0;
    dev->crq.qnext = 0;
}

static void rtas_set_tce_bypass(sPAPREnvironment *spapr, uint32_t token,
                                uint32_t nargs, target_ulong args,
                                uint32_t nret, target_ulong rets)
{
    VIOsPAPRBus *bus = spapr->vio_bus;
    VIOsPAPRDevice *dev;
    uint32_t unit, enable;

    if (nargs != 2) {
        rtas_st(rets, 0, -3);
        return;
    }
    unit = rtas_ld(args, 0);
    enable = rtas_ld(args, 1);
    dev = spapr_vio_find_by_reg(bus, unit);
    if (!dev) {
        rtas_st(rets, 0, -3);
        return;
    }
    if (enable) {
        dev->flags |= VIO_PAPR_FLAG_DMA_BYPASS;
    } else {
        dev->flags &= ~VIO_PAPR_FLAG_DMA_BYPASS;
    }

    rtas_st(rets, 0, 0);
}

static void rtas_quiesce(sPAPREnvironment *spapr, uint32_t token,
                         uint32_t nargs, target_ulong args,
                         uint32_t nret, target_ulong rets)
{
    VIOsPAPRBus *bus = spapr->vio_bus;
    DeviceState *qdev;
    VIOsPAPRDevice *dev = NULL;

    if (nargs != 0) {
        rtas_st(rets, 0, -3);
        return;
    }

    QTAILQ_FOREACH(qdev, &bus->bus.children, sibling) {
        dev = (VIOsPAPRDevice *)qdev;
        spapr_vio_quiesce_one(dev);
    }

    rtas_st(rets, 0, 0);
}

static int spapr_vio_busdev_init(DeviceState *qdev, DeviceInfo *qinfo)
{
    VIOsPAPRDeviceInfo *info = (VIOsPAPRDeviceInfo *)qinfo;
    VIOsPAPRDevice *dev = (VIOsPAPRDevice *)qdev;
    char *id;

    /* Don't overwrite ids assigned on the command line */
    if (!dev->qdev.id) {
        id = vio_format_dev_name(dev);
        if (!id) {
            return -1;
        }
        dev->qdev.id = id;
    }

    dev->qirq = spapr_allocate_irq(dev->vio_irq_num, &dev->vio_irq_num);
    if (!dev->qirq) {
        return -1;
    }

    rtce_init(dev);

    return info->init(dev);
}

void spapr_vio_bus_register_withprop(VIOsPAPRDeviceInfo *info)
{
    info->qdev.init = spapr_vio_busdev_init;
    info->qdev.bus_info = &spapr_vio_bus_info;

    assert(info->qdev.size >= sizeof(VIOsPAPRDevice));
    qdev_register(&info->qdev);
}

static target_ulong h_vio_signal(CPUState *env, sPAPREnvironment *spapr,
                                 target_ulong opcode,
                                 target_ulong *args)
{
    target_ulong reg = args[0];
    target_ulong mode = args[1];
    VIOsPAPRDevice *dev = spapr_vio_find_by_reg(spapr->vio_bus, reg);
    VIOsPAPRDeviceInfo *info;

    if (!dev) {
        return H_PARAMETER;
    }

    info = (VIOsPAPRDeviceInfo *)dev->qdev.info;

    if (mode & ~info->signal_mask) {
        return H_PARAMETER;
    }

    dev->signal_state = mode;

    return H_SUCCESS;
}

VIOsPAPRBus *spapr_vio_bus_init(void)
{
    VIOsPAPRBus *bus;
    BusState *qbus;
    DeviceState *dev;
    DeviceInfo *qinfo;

    /* Create bridge device */
    dev = qdev_create(NULL, "spapr-vio-bridge");
    qdev_init_nofail(dev);

    /* Create bus on bridge device */

    qbus = qbus_create(&spapr_vio_bus_info, dev, "spapr-vio");
    bus = DO_UPCAST(VIOsPAPRBus, bus, qbus);

    /* hcall-vio */
    spapr_register_hypercall(H_VIO_SIGNAL, h_vio_signal);

    /* hcall-tce */
    spapr_register_hypercall(H_PUT_TCE, h_put_tce);

    /* hcall-crq */
    spapr_register_hypercall(H_REG_CRQ, h_reg_crq);
    spapr_register_hypercall(H_FREE_CRQ, h_free_crq);
    spapr_register_hypercall(H_SEND_CRQ, h_send_crq);
    spapr_register_hypercall(H_ENABLE_CRQ, h_enable_crq);

    /* RTAS calls */
    spapr_rtas_register("ibm,set-tce-bypass", rtas_set_tce_bypass);
    spapr_rtas_register("quiesce", rtas_quiesce);

    for (qinfo = device_info_list; qinfo; qinfo = qinfo->next) {
        VIOsPAPRDeviceInfo *info = (VIOsPAPRDeviceInfo *)qinfo;

        if (qinfo->bus_info != &spapr_vio_bus_info) {
            continue;
        }

        if (info->hcalls) {
            info->hcalls(bus);
        }
    }

    return bus;
}

/* Represents sPAPR hcall VIO devices */

static int spapr_vio_bridge_init(SysBusDevice *dev)
{
    /* nothing */
    return 0;
}

static SysBusDeviceInfo spapr_vio_bridge_info = {
    .init = spapr_vio_bridge_init,
    .qdev.name  = "spapr-vio-bridge",
    .qdev.size  = sizeof(SysBusDevice),
    .qdev.no_user = 1,
};

static void spapr_vio_register_devices(void)
{
    sysbus_register_withprop(&spapr_vio_bridge_info);
}

device_init(spapr_vio_register_devices)

#ifdef CONFIG_FDT
int spapr_populate_vdevice(VIOsPAPRBus *bus, void *fdt)
{
    DeviceState *qdev;
    int ret = 0;

    QTAILQ_FOREACH(qdev, &bus->bus.children, sibling) {
        VIOsPAPRDevice *dev = (VIOsPAPRDevice *)qdev;

        ret = vio_make_devnode(dev, fdt);

        if (ret < 0) {
            return ret;
        }
    }

    return 0;
}
#endif /* CONFIG_FDT */
