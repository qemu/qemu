/*
 * LASI wrapper for the NCR 53C710 SCSI controller
 *
 * Copyright (c) 2025 Soumyajyotii Ssarkar <soumyajyotisarkar23@gmail.com>
 * Developed during Google Summer of Code 2025, mentored by
 * Helge Deller <deller@gmx.de>.
 *
 * LASI module glue mapping the 53C710 core in ncr53c710.c onto the HP 715
 * (-M 715) LASI bus.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/scsi/lasi_ncr710.h"
#include "hw/scsi/ncr53c710.h"
#include "hw/core/sysbus.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "trace.h"
#include "system/blockdev.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "system/dma.h"

#define LASI_710_SVERSION    0x00082
#define LASI_710_HVERSION    0x3D
#define HPHW_FIO             5        /* Fixed I/O module */

static uint64_t lasi_ncr710_reg_read(void *opaque, hwaddr addr,
                                    unsigned size)
{
    LasiNCR710State *s = LASI_NCR710(opaque);
    uint64_t val = 0;

    if (addr == 0x00) {  /* Device ID */
        val = (HPHW_FIO << 24) | LASI_710_SVERSION;
        trace_lasi_ncr710_reg_read_id(HPHW_FIO, LASI_710_SVERSION, val);
        return val;
    }

    if (addr == 0x08) {  /* HVersion */
        val = LASI_710_HVERSION;
        trace_lasi_ncr710_reg_read_hversion(val);
        return val;
    }

    /* 0x100..0x1ff goes to the chip core. */
    if (addr >= 0x100 && addr + size <= 0x200) {
        hwaddr ncr_addr = addr - 0x100;
        if (size == 1) {
            /*
             * Single byte access: flip the byte lane (PA-RISC big endian
             * access -> the chip's little endian register order).
             */
            ncr_addr ^= 3;
            val = ncr710_reg_read(&s->ncr710, ncr_addr, size);
        } else {
            /*
             * Multibyte access: gather little endian, no lane flip, so
             * the 24/32 bit registers assemble correctly.
             */
            val = 0;
            for (unsigned i = 0; i < size; i++) {
                uint8_t byte_val = ncr710_reg_read(&s->ncr710, ncr_addr + i, 1);
                val |= ((uint64_t)byte_val) << (i * 8);
            }
        }

        trace_lasi_ncr710_reg_forward_read(addr, val);
    } else {
        /*
         * The rest of the module page reads as 0.  HP-UX reads a board strap
         * at 0xd85 (bit 0x20: differential bus, 25 MHz clock); 0 is single
         * ended, 50 MHz.
         */
        val = 0;
        trace_lasi_ncr710_reg_read(addr, val, size);
    }
    return val;
}

static void lasi_ncr710_reg_write(void *opaque, hwaddr addr,
                                   uint64_t val, unsigned size)
{
    LasiNCR710State *s = LASI_NCR710(opaque);

    trace_lasi_ncr710_reg_write(addr, val, size);

    /*
     * 0x00..0x0f is the read only LASI module header (ID/SVERSION/HVERSION);
     * 0x10..0xff and 0x200..0xfff are unmapped module space.  Writes to all
     * of them are ignored.
     */
    if (addr <= 0x0f) {
        return;
    }

    if (addr >= 0x100 && addr + size <= 0x200) {
        hwaddr ncr_addr = addr - 0x100;

        if (size == 1) {
            ncr_addr ^= 3;
            ncr710_reg_write(&s->ncr710, ncr_addr, val, size);
        } else {
            for (unsigned i = 0; i < size; i++) {
                uint8_t byte_val = (val >> (i * 8)) & 0xff;
                ncr710_reg_write(&s->ncr710, ncr_addr + i, byte_val, 1);
            }
        }

        trace_lasi_ncr710_reg_forward_write(addr, val);
    } else {
        trace_lasi_ncr710_reg_write(addr, val, size);
    }
}

/* SCSIBusInfo callbacks: trace, then forward to the ncr53c710 core. */
static void lasi_ncr710_request_cancelled(SCSIRequest *req)
{
    trace_lasi_ncr710_request_cancelled(req);
    ncr710_request_cancelled(req);
}

static void lasi_ncr710_command_complete(SCSIRequest *req, size_t resid)
{
    trace_lasi_ncr710_command_complete(req->status, resid);
    ncr710_command_complete(req, resid);
}

static void lasi_ncr710_transfer_data(SCSIRequest *req, uint32_t len)
{
    trace_lasi_ncr710_transfer_data(len);
    ncr710_transfer_data(req, len);
}

static void lasi_ncr710_free_request(SCSIBus *bus, void *priv)
{
    g_free(priv);
}

static const struct SCSIBusInfo lasi_ncr710_scsi_info = {
    .tcq = true,
    /* 8-bit bus: targets and LUNs 0-7 (scsi-bus.c maxima are inclusive). */
    .max_target = 7,
    .max_lun = 7,

    .transfer_data = lasi_ncr710_transfer_data,
    .complete = lasi_ncr710_command_complete,
    .cancel = lasi_ncr710_request_cancelled,
    .free_request = lasi_ncr710_free_request,
};

static const MemoryRegionOps lasi_ncr710_mmio_ops = {
    .read = lasi_ncr710_reg_read,
    .write = lasi_ncr710_reg_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static const VMStateDescription vmstate_lasi_ncr710 = {
    .name = "lasi-ncr710",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT(ncr710, LasiNCR710State, 1, vmstate_ncr710, NCR710State),
        VMSTATE_END_OF_LIST()
    }
};

static void lasi_ncr710_realize(DeviceState *dev, Error **errp)
{
    LasiNCR710State *s = LASI_NCR710(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    trace_lasi_ncr710_device_realize();

    scsi_bus_init(&s->ncr710.bus, sizeof(s->ncr710.bus), dev,
                  &lasi_ncr710_scsi_info);
    s->ncr710.as = &address_space_memory;
    s->ncr710.irq = s->lasi_irq;
    QTAILQ_INIT(&s->ncr710.queue);

    s->ncr710.scripts_timer =
        timer_new_ns(QEMU_CLOCK_VIRTUAL,
                     ncr710_scripts_timer_callback,
                     &s->ncr710);

    ncr710_soft_reset(&s->ncr710);
    /* A device reset clears EA and FC1; only ISTAT.RST keeps them. */
    s->ncr710.dcntl = 0;
    s->ncr710.dmode = 0;

    trace_lasi_ncr710_timers_initialized(
        (uint64_t)s->ncr710.scripts_timer);

    memory_region_init_io(&s->mmio, OBJECT(dev), &lasi_ncr710_mmio_ops, s,
                          "lasi-ncr710", 0x1000);
    sysbus_init_mmio(sbd, &s->mmio);
}

static void lasi_ncr710_unrealize(DeviceState *dev)
{
    LasiNCR710State *s = LASI_NCR710(dev);

    timer_free(s->ncr710.scripts_timer);
    s->ncr710.scripts_timer = NULL;
}

void lasi_ncr710_handle_legacy_cmdline(DeviceState *lasi_dev)
{
    LasiNCR710State *s = LASI_NCR710(lasi_dev);

    scsi_bus_legacy_handle_cmdline(&s->ncr710.bus);
}

DeviceState *lasi_ncr710_init(MemoryRegion *addr_space, hwaddr hpa,
                               qemu_irq irq)
{
    DeviceState *dev;
    LasiNCR710State *s;
    SysBusDevice *sbd;

    dev = qdev_new(TYPE_LASI_NCR710);
    s = LASI_NCR710(dev);
    sbd = SYS_BUS_DEVICE(dev);
    s->lasi_irq = irq;
    sysbus_realize_and_unref(sbd, &error_fatal);
    memory_region_add_subregion(addr_space, hpa,
                               sysbus_mmio_get_region(sbd, 0));
    return dev;
}

static void lasi_ncr710_reset(DeviceState *dev)
{
    LasiNCR710State *s = LASI_NCR710(dev);
    trace_lasi_ncr710_device_reset();
    ncr710_soft_reset(&s->ncr710);
    /* A device reset clears EA and FC1; only ISTAT.RST keeps them. */
    s->ncr710.dcntl = 0;
    s->ncr710.dmode = 0;
}

static void lasi_ncr710_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = lasi_ncr710_realize;
    dc->unrealize = lasi_ncr710_unrealize;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    dc->fw_name = "scsi";
    dc->desc = "HP-PARISC LASI NCR710 SCSI adapter";
    device_class_set_legacy_reset(dc, lasi_ncr710_reset);
    dc->vmsd = &vmstate_lasi_ncr710;
    dc->user_creatable = false;
}

static const TypeInfo lasi_ncr710_info = {
    .name          = TYPE_LASI_NCR710,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(LasiNCR710State),
    .class_init    = lasi_ncr710_class_init,
};

static void lasi_ncr710_register_types(void)
{
    type_register_static(&lasi_ncr710_info);
}

type_init(lasi_ncr710_register_types)
