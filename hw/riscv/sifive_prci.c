/*
 * QEMU SiFive PRCI (Power, Reset, Clock, Interrupt)
 *
 * Copyright (c) 2017 SiFive, Inc.
 *
 * Simple model of the PRCI to emulate register reads made by the SDK BSP
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "target/riscv/cpu.h"
#include "hw/riscv/sifive_prci.h"

/* currently implements enough to mock freedom-e-sdk BSP clock programming */

static uint64_t sifive_prci_read(void *opaque, hwaddr addr, unsigned int size)
{
    if (addr == 0 /* PRCI_HFROSCCFG */) {
        return 1 << 31; /* ROSC_RDY */
    }
    if (addr == 8 /* PRCI_PLLCFG    */) {
        return 1 << 31; /* PLL_LOCK */
    }
    hw_error("%s: read: addr=0x%x\n", __func__, (int)addr);
    return 0;
}

static void sifive_prci_write(void *opaque, hwaddr addr,
           uint64_t val64, unsigned int size)
{
    /* discard writes */
}

static const MemoryRegionOps sifive_prci_ops = {
    .read = sifive_prci_read,
    .write = sifive_prci_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static void sifive_prci_init(Object *obj)
{
    SiFivePRCIState *s = SIFIVE_PRCI(obj);

    memory_region_init_io(&s->mmio, obj, &sifive_prci_ops, s,
                          TYPE_SIFIVE_PRCI, 0x8000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static const TypeInfo sifive_prci_info = {
    .name          = TYPE_SIFIVE_PRCI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SiFivePRCIState),
    .instance_init = sifive_prci_init,
};

static void sifive_prci_register_types(void)
{
    type_register_static(&sifive_prci_info);
}

type_init(sifive_prci_register_types)


/*
 * Create PRCI device.
 */
DeviceState *sifive_prci_create(hwaddr addr)
{
    DeviceState *dev = qdev_create(NULL, TYPE_SIFIVE_PRCI);
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, addr);
    return dev;
}
