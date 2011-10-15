/*
 *  QEMU model of the Milkymist SoftUSB block.
 *
 *  Copyright (c) 2010 Michael Walle <michael@walle.cc>
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
 *
 *
 * Specification available at:
 *   not available yet
 */

#include "hw.h"
#include "sysbus.h"
#include "trace.h"
#include "console.h"
#include "hid.h"
#include "qemu-error.h"

enum {
    R_CTRL = 0,
    R_MAX
};

enum {
    CTRL_RESET = (1<<0),
};

#define COMLOC_DEBUG_PRODUCE 0x1000
#define COMLOC_DEBUG_BASE    0x1001
#define COMLOC_MEVT_PRODUCE  0x1101
#define COMLOC_MEVT_BASE     0x1102
#define COMLOC_KEVT_PRODUCE  0x1142
#define COMLOC_KEVT_BASE     0x1143

struct MilkymistSoftUsbState {
    SysBusDevice busdev;
    HIDState hid_kbd;
    HIDState hid_mouse;

    MemoryRegion regs_region;
    MemoryRegion pmem;
    MemoryRegion dmem;
    qemu_irq irq;

    /* device properties */
    uint32_t pmem_base;
    uint32_t pmem_size;
    uint32_t dmem_base;
    uint32_t dmem_size;

    /* device registers */
    uint32_t regs[R_MAX];

    /* mouse state */
    uint8_t mouse_hid_buffer[4];

    /* keyboard state */
    uint8_t kbd_hid_buffer[8];
};
typedef struct MilkymistSoftUsbState MilkymistSoftUsbState;

static uint64_t softusb_read(void *opaque, target_phys_addr_t addr,
                             unsigned size)
{
    MilkymistSoftUsbState *s = opaque;
    uint32_t r = 0;

    addr >>= 2;
    switch (addr) {
    case R_CTRL:
        r = s->regs[addr];
        break;

    default:
        error_report("milkymist_softusb: read access to unknown register 0x"
                TARGET_FMT_plx, addr << 2);
        break;
    }

    trace_milkymist_softusb_memory_read(addr << 2, r);

    return r;
}

static void
softusb_write(void *opaque, target_phys_addr_t addr, uint64_t value,
              unsigned size)
{
    MilkymistSoftUsbState *s = opaque;

    trace_milkymist_softusb_memory_write(addr, value);

    addr >>= 2;
    switch (addr) {
    case R_CTRL:
        s->regs[addr] = value;
        break;

    default:
        error_report("milkymist_softusb: write access to unknown register 0x"
                TARGET_FMT_plx, addr << 2);
        break;
    }
}

static const MemoryRegionOps softusb_mmio_ops = {
    .read = softusb_read,
    .write = softusb_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static inline void softusb_read_dmem(MilkymistSoftUsbState *s,
        uint32_t offset, uint8_t *buf, uint32_t len)
{
    if (offset + len >= s->dmem_size) {
        error_report("milkymist_softusb: read dmem out of bounds "
                "at offset 0x%x, len %d", offset, len);
        return;
    }

    cpu_physical_memory_read(s->dmem_base + offset, buf, len);
}

static inline void softusb_write_dmem(MilkymistSoftUsbState *s,
        uint32_t offset, uint8_t *buf, uint32_t len)
{
    if (offset + len >= s->dmem_size) {
        error_report("milkymist_softusb: write dmem out of bounds "
                "at offset 0x%x, len %d", offset, len);
        return;
    }

    cpu_physical_memory_write(s->dmem_base + offset, buf, len);
}

static inline void softusb_read_pmem(MilkymistSoftUsbState *s,
        uint32_t offset, uint8_t *buf, uint32_t len)
{
    if (offset + len >= s->pmem_size) {
        error_report("milkymist_softusb: read pmem out of bounds "
                "at offset 0x%x, len %d", offset, len);
        return;
    }

    cpu_physical_memory_read(s->pmem_base + offset, buf, len);
}

static inline void softusb_write_pmem(MilkymistSoftUsbState *s,
        uint32_t offset, uint8_t *buf, uint32_t len)
{
    if (offset + len >= s->pmem_size) {
        error_report("milkymist_softusb: write pmem out of bounds "
                "at offset 0x%x, len %d", offset, len);
        return;
    }

    cpu_physical_memory_write(s->pmem_base + offset, buf, len);
}

static void softusb_mouse_changed(MilkymistSoftUsbState *s)
{
    uint8_t m;

    softusb_read_dmem(s, COMLOC_MEVT_PRODUCE, &m, 1);
    trace_milkymist_softusb_mevt(m);
    softusb_write_dmem(s, COMLOC_MEVT_BASE + 4 * m, s->mouse_hid_buffer, 4);
    m = (m + 1) & 0xf;
    softusb_write_dmem(s, COMLOC_MEVT_PRODUCE, &m, 1);

    trace_milkymist_softusb_pulse_irq();
    qemu_irq_pulse(s->irq);
}

static void softusb_kbd_changed(MilkymistSoftUsbState *s)
{
    uint8_t m;

    softusb_read_dmem(s, COMLOC_KEVT_PRODUCE, &m, 1);
    trace_milkymist_softusb_kevt(m);
    softusb_write_dmem(s, COMLOC_KEVT_BASE + 8 * m, s->kbd_hid_buffer, 8);
    m = (m + 1) & 0x7;
    softusb_write_dmem(s, COMLOC_KEVT_PRODUCE, &m, 1);

    trace_milkymist_softusb_pulse_irq();
    qemu_irq_pulse(s->irq);
}

static void softusb_kbd_hid_datain(HIDState *hs)
{
    MilkymistSoftUsbState *s = container_of(hs, MilkymistSoftUsbState, hid_kbd);
    int len;

    /* if device is in reset, do nothing */
    if (s->regs[R_CTRL] & CTRL_RESET) {
        return;
    }

    len = hid_keyboard_poll(hs, s->kbd_hid_buffer, sizeof(s->kbd_hid_buffer));

    if (len == 8) {
        softusb_kbd_changed(s);
    }
}

static void softusb_mouse_hid_datain(HIDState *hs)
{
    MilkymistSoftUsbState *s =
            container_of(hs, MilkymistSoftUsbState, hid_mouse);
    int len;

    /* if device is in reset, do nothing */
    if (s->regs[R_CTRL] & CTRL_RESET) {
        return;
    }

    len = hid_pointer_poll(hs, s->mouse_hid_buffer,
            sizeof(s->mouse_hid_buffer));

    if (len == 4) {
        softusb_mouse_changed(s);
    }
}

static void milkymist_softusb_reset(DeviceState *d)
{
    MilkymistSoftUsbState *s =
            container_of(d, MilkymistSoftUsbState, busdev.qdev);
    int i;

    for (i = 0; i < R_MAX; i++) {
        s->regs[i] = 0;
    }
    memset(s->kbd_hid_buffer, 0, sizeof(s->kbd_hid_buffer));
    memset(s->mouse_hid_buffer, 0, sizeof(s->mouse_hid_buffer));

    hid_reset(&s->hid_kbd);
    hid_reset(&s->hid_mouse);

    /* defaults */
    s->regs[R_CTRL] = CTRL_RESET;
}

static int milkymist_softusb_init(SysBusDevice *dev)
{
    MilkymistSoftUsbState *s = FROM_SYSBUS(typeof(*s), dev);

    sysbus_init_irq(dev, &s->irq);

    memory_region_init_io(&s->regs_region, &softusb_mmio_ops, s,
                          "milkymist-softusb", R_MAX * 4);
    sysbus_init_mmio_region(dev, &s->regs_region);

    /* register pmem and dmem */
    memory_region_init_ram(&s->pmem, NULL, "milkymist-softusb.pmem",
                           s->pmem_size);
    sysbus_add_memory(dev, s->pmem_base, &s->pmem);
    memory_region_init_ram(&s->dmem, NULL, "milkymist-softusb.dmem",
                           s->dmem_size);
    sysbus_add_memory(dev, s->dmem_base, &s->dmem);

    hid_init(&s->hid_kbd, HID_KEYBOARD, softusb_kbd_hid_datain);
    hid_init(&s->hid_mouse, HID_MOUSE, softusb_mouse_hid_datain);

    return 0;
}

static const VMStateDescription vmstate_milkymist_softusb = {
    .name = "milkymist-softusb",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, MilkymistSoftUsbState, R_MAX),
        VMSTATE_HID_KEYBOARD_DEVICE(hid_kbd, MilkymistSoftUsbState),
        VMSTATE_HID_POINTER_DEVICE(hid_mouse, MilkymistSoftUsbState),
        VMSTATE_BUFFER(kbd_hid_buffer, MilkymistSoftUsbState),
        VMSTATE_BUFFER(mouse_hid_buffer, MilkymistSoftUsbState),
        VMSTATE_END_OF_LIST()
    }
};

static SysBusDeviceInfo milkymist_softusb_info = {
    .init = milkymist_softusb_init,
    .qdev.name  = "milkymist-softusb",
    .qdev.size  = sizeof(MilkymistSoftUsbState),
    .qdev.vmsd  = &vmstate_milkymist_softusb,
    .qdev.reset = milkymist_softusb_reset,
    .qdev.props = (Property[]) {
        DEFINE_PROP_UINT32(
                "pmem_base", MilkymistSoftUsbState, pmem_base, 0xa0000000
        ),
        DEFINE_PROP_UINT32(
                "pmem_size", MilkymistSoftUsbState, pmem_size, 0x00001000
        ),
        DEFINE_PROP_UINT32(
                "dmem_base", MilkymistSoftUsbState, dmem_base, 0xa0020000
        ),
        DEFINE_PROP_UINT32(
                "dmem_size", MilkymistSoftUsbState, dmem_size, 0x00002000
        ),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void milkymist_softusb_register(void)
{
    sysbus_register_withprop(&milkymist_softusb_info);
}

device_init(milkymist_softusb_register)
