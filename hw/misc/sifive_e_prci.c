/*
 * QEMU SiFive E PRCI (Power, Reset, Clock, Interrupt)
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
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/misc/sifive_e_prci.h"

static uint64_t sifive_e_prci_read(void *opaque, hwaddr addr, unsigned int size)
{
    SiFiveEPRCIState *s = opaque;
    switch (addr) {
    case SIFIVE_E_PRCI_HFROSCCFG:
        return s->hfrosccfg;
    case SIFIVE_E_PRCI_HFXOSCCFG:
        return s->hfxosccfg;
    case SIFIVE_E_PRCI_PLLCFG:
        return s->pllcfg;
    case SIFIVE_E_PRCI_PLLOUTDIV:
        return s->plloutdiv;
    }
    qemu_log_mask(LOG_GUEST_ERROR, "%s: read: addr=0x%x\n",
                  __func__, (int)addr);
    return 0;
}

static void sifive_e_prci_write(void *opaque, hwaddr addr,
                                uint64_t val64, unsigned int size)
{
    SiFiveEPRCIState *s = opaque;
    switch (addr) {
    case SIFIVE_E_PRCI_HFROSCCFG:
        s->hfrosccfg = (uint32_t) val64;
        /* OSC stays ready */
        s->hfrosccfg |= SIFIVE_E_PRCI_HFROSCCFG_RDY;
        break;
    case SIFIVE_E_PRCI_HFXOSCCFG:
        s->hfxosccfg = (uint32_t) val64;
        /* OSC stays ready */
        s->hfxosccfg |= SIFIVE_E_PRCI_HFXOSCCFG_RDY;
        break;
    case SIFIVE_E_PRCI_PLLCFG:
        s->pllcfg = (uint32_t) val64;
        /* PLL stays locked */
        s->pllcfg |= SIFIVE_E_PRCI_PLLCFG_LOCK;
        break;
    case SIFIVE_E_PRCI_PLLOUTDIV:
        s->plloutdiv = (uint32_t) val64;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write: addr=0x%x v=0x%x\n",
                      __func__, (int)addr, (int)val64);
    }
}

static const MemoryRegionOps sifive_e_prci_ops = {
    .read = sifive_e_prci_read,
    .write = sifive_e_prci_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static void sifive_e_prci_init(Object *obj)
{
    SiFiveEPRCIState *s = SIFIVE_E_PRCI(obj);

    memory_region_init_io(&s->mmio, obj, &sifive_e_prci_ops, s,
                          TYPE_SIFIVE_E_PRCI, SIFIVE_E_PRCI_REG_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);

    s->hfrosccfg = (SIFIVE_E_PRCI_HFROSCCFG_RDY | SIFIVE_E_PRCI_HFROSCCFG_EN);
    s->hfxosccfg = (SIFIVE_E_PRCI_HFXOSCCFG_RDY | SIFIVE_E_PRCI_HFXOSCCFG_EN);
    s->pllcfg = (SIFIVE_E_PRCI_PLLCFG_REFSEL | SIFIVE_E_PRCI_PLLCFG_BYPASS |
                 SIFIVE_E_PRCI_PLLCFG_LOCK);
    s->plloutdiv = SIFIVE_E_PRCI_PLLOUTDIV_DIV1;
}

static const TypeInfo sifive_e_prci_info = {
    .name          = TYPE_SIFIVE_E_PRCI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SiFiveEPRCIState),
    .instance_init = sifive_e_prci_init,
};

static void sifive_e_prci_register_types(void)
{
    type_register_static(&sifive_e_prci_info);
}

type_init(sifive_e_prci_register_types)


/*
 * Create PRCI device.
 */
DeviceState *sifive_e_prci_create(hwaddr addr)
{
    DeviceState *dev = qdev_new(TYPE_SIFIVE_E_PRCI);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, addr);
    return dev;
}
