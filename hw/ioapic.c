/*
 *  ioapic.c IOAPIC emulation logic
 *
 *  Copyright (c) 2004-2005 Fabrice Bellard
 *
 *  Split the ioapic logic from apic.c
 *  Xiantao Zhang <xiantao.zhang@intel.com>
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
#include "pc.h"
#include "apic.h"
#include "qemu-timer.h"
#include "host-utils.h"
#include "sysbus.h"

//#define DEBUG_IOAPIC

#ifdef DEBUG_IOAPIC
#define DPRINTF(fmt, ...)                                       \
    do { printf("ioapic: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...)
#endif

#define IOAPIC_LVT_MASKED 		(1<<16)

#define IOAPIC_TRIGGER_EDGE		0
#define IOAPIC_TRIGGER_LEVEL		1

/*io{apic,sapic} delivery mode*/
#define IOAPIC_DM_FIXED			0x0
#define IOAPIC_DM_LOWEST_PRIORITY	0x1
#define IOAPIC_DM_PMI			0x2
#define IOAPIC_DM_NMI			0x4
#define IOAPIC_DM_INIT			0x5
#define IOAPIC_DM_SIPI			0x5
#define IOAPIC_DM_EXTINT		0x7

typedef struct IOAPICState IOAPICState;

struct IOAPICState {
    SysBusDevice busdev;
    uint8_t id;
    uint8_t ioregsel;

    uint32_t irr;
    uint64_t ioredtbl[IOAPIC_NUM_PINS];
};

static void ioapic_service(IOAPICState *s)
{
    uint8_t i;
    uint8_t trig_mode;
    uint8_t vector;
    uint8_t delivery_mode;
    uint32_t mask;
    uint64_t entry;
    uint8_t dest;
    uint8_t dest_mode;
    uint8_t polarity;

    for (i = 0; i < IOAPIC_NUM_PINS; i++) {
        mask = 1 << i;
        if (s->irr & mask) {
            entry = s->ioredtbl[i];
            if (!(entry & IOAPIC_LVT_MASKED)) {
                trig_mode = ((entry >> 15) & 1);
                dest = entry >> 56;
                dest_mode = (entry >> 11) & 1;
                delivery_mode = (entry >> 8) & 7;
                polarity = (entry >> 13) & 1;
                if (trig_mode == IOAPIC_TRIGGER_EDGE)
                    s->irr &= ~mask;
                if (delivery_mode == IOAPIC_DM_EXTINT)
                    vector = pic_read_irq(isa_pic);
                else
                    vector = entry & 0xff;

                apic_deliver_irq(dest, dest_mode, delivery_mode,
                                 vector, polarity, trig_mode);
            }
        }
    }
}

static void ioapic_set_irq(void *opaque, int vector, int level)
{
    IOAPICState *s = opaque;

    /* ISA IRQs map to GSI 1-1 except for IRQ0 which maps
     * to GSI 2.  GSI maps to ioapic 1-1.  This is not
     * the cleanest way of doing it but it should work. */

    DPRINTF("%s: %s vec %x\n", __func__, level? "raise" : "lower", vector);
    if (vector == 0)
        vector = 2;

    if (vector >= 0 && vector < IOAPIC_NUM_PINS) {
        uint32_t mask = 1 << vector;
        uint64_t entry = s->ioredtbl[vector];

        if ((entry >> 15) & 1) {
            /* level triggered */
            if (level) {
                s->irr |= mask;
                ioapic_service(s);
            } else {
                s->irr &= ~mask;
            }
        } else {
            /* edge triggered */
            if (level) {
                s->irr |= mask;
                ioapic_service(s);
            }
        }
    }
}

static uint32_t ioapic_mem_readl(void *opaque, target_phys_addr_t addr)
{
    IOAPICState *s = opaque;
    int index;
    uint32_t val = 0;

    addr &= 0xff;
    if (addr == 0x00) {
        val = s->ioregsel;
    } else if (addr == 0x10) {
        switch (s->ioregsel) {
            case 0x00:
                val = s->id << 24;
                break;
            case 0x01:
                val = 0x11 | ((IOAPIC_NUM_PINS - 1) << 16); /* version 0x11 */
                break;
            case 0x02:
                val = 0;
                break;
            default:
                index = (s->ioregsel - 0x10) >> 1;
                if (index >= 0 && index < IOAPIC_NUM_PINS) {
                    if (s->ioregsel & 1)
                        val = s->ioredtbl[index] >> 32;
                    else
                        val = s->ioredtbl[index] & 0xffffffff;
                }
        }
        DPRINTF("read: %08x = %08x\n", s->ioregsel, val);
    }
    return val;
}

static void ioapic_mem_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    IOAPICState *s = opaque;
    int index;

    addr &= 0xff;
    if (addr == 0x00)  {
        s->ioregsel = val;
        return;
    } else if (addr == 0x10) {
        DPRINTF("write: %08x = %08x\n", s->ioregsel, val);
        switch (s->ioregsel) {
            case 0x00:
                s->id = (val >> 24) & 0xff;
                return;
            case 0x01:
            case 0x02:
                return;
            default:
                index = (s->ioregsel - 0x10) >> 1;
                if (index >= 0 && index < IOAPIC_NUM_PINS) {
                    if (s->ioregsel & 1) {
                        s->ioredtbl[index] &= 0xffffffff;
                        s->ioredtbl[index] |= (uint64_t)val << 32;
                    } else {
                        s->ioredtbl[index] &= ~0xffffffffULL;
                        s->ioredtbl[index] |= val;
                    }
                    ioapic_service(s);
                }
        }
    }
}

static const VMStateDescription vmstate_ioapic = {
    .name = "ioapic",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField []) {
        VMSTATE_UINT8(id, IOAPICState),
        VMSTATE_UINT8(ioregsel, IOAPICState),
        VMSTATE_UINT64_ARRAY(ioredtbl, IOAPICState, IOAPIC_NUM_PINS),
        VMSTATE_END_OF_LIST()
    }
};

static void ioapic_reset(DeviceState *d)
{
    IOAPICState *s = DO_UPCAST(IOAPICState, busdev.qdev, d);
    int i;

    s->id = 0;
    s->ioregsel = 0;
    s->irr = 0;
    for(i = 0; i < IOAPIC_NUM_PINS; i++)
        s->ioredtbl[i] = 1 << 16; /* mask LVT */
}

static CPUReadMemoryFunc * const ioapic_mem_read[3] = {
    ioapic_mem_readl,
    ioapic_mem_readl,
    ioapic_mem_readl,
};

static CPUWriteMemoryFunc * const ioapic_mem_write[3] = {
    ioapic_mem_writel,
    ioapic_mem_writel,
    ioapic_mem_writel,
};

static int ioapic_init1(SysBusDevice *dev)
{
    IOAPICState *s = FROM_SYSBUS(IOAPICState, dev);
    int io_memory;

    io_memory = cpu_register_io_memory(ioapic_mem_read,
                                       ioapic_mem_write, s);
    sysbus_init_mmio(dev, 0x1000, io_memory);

    qdev_init_gpio_in(&dev->qdev, ioapic_set_irq, IOAPIC_NUM_PINS);

    return 0;
}

static SysBusDeviceInfo ioapic_info = {
    .init = ioapic_init1,
    .qdev.name = "ioapic",
    .qdev.size = sizeof(IOAPICState),
    .qdev.vmsd = &vmstate_ioapic,
    .qdev.reset = ioapic_reset,
    .qdev.no_user = 1,
};

static void ioapic_register_devices(void)
{
    sysbus_register_withprop(&ioapic_info);
}

device_init(ioapic_register_devices)
