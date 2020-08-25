/*
 * QEMU Allwinner AHCI Emulation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "sysemu/dma.h"
#include "hw/ide/internal.h"
#include "migration/vmstate.h"
#include "ahci_internal.h"

#include "trace.h"

#define ALLWINNER_AHCI_BISTAFR    ((0xa0 - ALLWINNER_AHCI_MMIO_OFF) / 4)
#define ALLWINNER_AHCI_BISTCR     ((0xa4 - ALLWINNER_AHCI_MMIO_OFF) / 4)
#define ALLWINNER_AHCI_BISTFCTR   ((0xa8 - ALLWINNER_AHCI_MMIO_OFF) / 4)
#define ALLWINNER_AHCI_BISTSR     ((0xac - ALLWINNER_AHCI_MMIO_OFF) / 4)
#define ALLWINNER_AHCI_BISTDECR   ((0xb0 - ALLWINNER_AHCI_MMIO_OFF) / 4)
#define ALLWINNER_AHCI_DIAGNR0    ((0xb4 - ALLWINNER_AHCI_MMIO_OFF) / 4)
#define ALLWINNER_AHCI_DIAGNR1    ((0xb8 - ALLWINNER_AHCI_MMIO_OFF) / 4)
#define ALLWINNER_AHCI_OOBR       ((0xbc - ALLWINNER_AHCI_MMIO_OFF) / 4)
#define ALLWINNER_AHCI_PHYCS0R    ((0xc0 - ALLWINNER_AHCI_MMIO_OFF) / 4)
#define ALLWINNER_AHCI_PHYCS1R    ((0xc4 - ALLWINNER_AHCI_MMIO_OFF) / 4)
#define ALLWINNER_AHCI_PHYCS2R    ((0xc8 - ALLWINNER_AHCI_MMIO_OFF) / 4)
#define ALLWINNER_AHCI_TIMER1MS   ((0xe0 - ALLWINNER_AHCI_MMIO_OFF) / 4)
#define ALLWINNER_AHCI_GPARAM1R   ((0xe8 - ALLWINNER_AHCI_MMIO_OFF) / 4)
#define ALLWINNER_AHCI_GPARAM2R   ((0xec - ALLWINNER_AHCI_MMIO_OFF) / 4)
#define ALLWINNER_AHCI_PPARAMR    ((0xf0 - ALLWINNER_AHCI_MMIO_OFF) / 4)
#define ALLWINNER_AHCI_TESTR      ((0xf4 - ALLWINNER_AHCI_MMIO_OFF) / 4)
#define ALLWINNER_AHCI_VERSIONR   ((0xf8 - ALLWINNER_AHCI_MMIO_OFF) / 4)
#define ALLWINNER_AHCI_IDR        ((0xfc - ALLWINNER_AHCI_MMIO_OFF) / 4)
#define ALLWINNER_AHCI_RWCR       ((0xfc - ALLWINNER_AHCI_MMIO_OFF) / 4)

static uint64_t allwinner_ahci_mem_read(void *opaque, hwaddr addr,
                                        unsigned size)
{
    AllwinnerAHCIState *a = opaque;
    AHCIState *s = &(SYSBUS_AHCI(a)->ahci);
    uint64_t val = a->regs[addr / 4];

    switch (addr / 4) {
    case ALLWINNER_AHCI_PHYCS0R:
        val |= 0x2 << 28;
        break;
    case ALLWINNER_AHCI_PHYCS2R:
        val &= ~(0x1 << 24);
        break;
    }
    trace_allwinner_ahci_mem_read(s, a, addr, val, size);
    return  val;
}

static void allwinner_ahci_mem_write(void *opaque, hwaddr addr,
                                     uint64_t val, unsigned size)
{
    AllwinnerAHCIState *a = opaque;
    AHCIState *s = &(SYSBUS_AHCI(a)->ahci);

    trace_allwinner_ahci_mem_write(s, a, addr, val, size);
    a->regs[addr / 4] = val;
}

static const MemoryRegionOps allwinner_ahci_mem_ops = {
    .read = allwinner_ahci_mem_read,
    .write = allwinner_ahci_mem_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void allwinner_ahci_init(Object *obj)
{
    SysbusAHCIState *s = SYSBUS_AHCI(obj);
    AllwinnerAHCIState *a = ALLWINNER_AHCI(obj);

    memory_region_init_io(&a->mmio, obj, &allwinner_ahci_mem_ops, a,
                          "allwinner-ahci", ALLWINNER_AHCI_MMIO_SIZE);
    memory_region_add_subregion(&s->ahci.mem, ALLWINNER_AHCI_MMIO_OFF,
                                &a->mmio);
}

static const VMStateDescription vmstate_allwinner_ahci = {
    .name = "allwinner-ahci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, AllwinnerAHCIState,
                             ALLWINNER_AHCI_MMIO_SIZE / 4),
        VMSTATE_END_OF_LIST()
    }
};

static void allwinner_ahci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_allwinner_ahci;
}

static const TypeInfo allwinner_ahci_info = {
    .name          = TYPE_ALLWINNER_AHCI,
    .parent        = TYPE_SYSBUS_AHCI,
    .instance_size = sizeof(AllwinnerAHCIState),
    .instance_init = allwinner_ahci_init,
    .class_init    = allwinner_ahci_class_init,
};

static void sysbus_ahci_register_types(void)
{
    type_register_static(&allwinner_ahci_info);
}

type_init(sysbus_ahci_register_types)
