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
#include "usb.h"
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
    USBBus usbbus;
    USBPort usbport[2];
    USBDevice *usbdev;

    qemu_irq irq;

    /* device properties */
    uint32_t pmem_base;
    uint32_t pmem_size;
    uint32_t dmem_base;
    uint32_t dmem_size;

    /* device registers */
    uint32_t regs[R_MAX];

    /* mouse state */
    int mouse_dx;
    int mouse_dy;
    int mouse_dz;
    uint8_t mouse_buttons_state;

    /* keyboard state */
    uint8_t kbd_usb_buffer[8];
};
typedef struct MilkymistSoftUsbState MilkymistSoftUsbState;

static uint32_t softusb_read(void *opaque, target_phys_addr_t addr)
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
softusb_write(void *opaque, target_phys_addr_t addr, uint32_t value)
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

static CPUReadMemoryFunc * const softusb_read_fn[] = {
    NULL,
    NULL,
    &softusb_read,
};

static CPUWriteMemoryFunc * const softusb_write_fn[] = {
    NULL,
    NULL,
    &softusb_write,
};

static inline void softusb_read_dmem(MilkymistSoftUsbState *s,
        uint32_t offset, uint8_t *buf, uint32_t len)
{
    if (offset + len >= s->dmem_size) {
        error_report("milkymist_softusb: read dmem out of bounds "
                "at offset 0x%x, len %d\n", offset, len);
        return;
    }

    cpu_physical_memory_read(s->dmem_base + offset, buf, len);
}

static inline void softusb_write_dmem(MilkymistSoftUsbState *s,
        uint32_t offset, uint8_t *buf, uint32_t len)
{
    if (offset + len >= s->dmem_size) {
        error_report("milkymist_softusb: write dmem out of bounds "
                "at offset 0x%x, len %d\n", offset, len);
        return;
    }

    cpu_physical_memory_write(s->dmem_base + offset, buf, len);
}

static inline void softusb_read_pmem(MilkymistSoftUsbState *s,
        uint32_t offset, uint8_t *buf, uint32_t len)
{
    if (offset + len >= s->pmem_size) {
        error_report("milkymist_softusb: read pmem out of bounds "
                "at offset 0x%x, len %d\n", offset, len);
        return;
    }

    cpu_physical_memory_read(s->pmem_base + offset, buf, len);
}

static inline void softusb_write_pmem(MilkymistSoftUsbState *s,
        uint32_t offset, uint8_t *buf, uint32_t len)
{
    if (offset + len >= s->pmem_size) {
        error_report("milkymist_softusb: write pmem out of bounds "
                "at offset 0x%x, len %d\n", offset, len);
        return;
    }

    cpu_physical_memory_write(s->pmem_base + offset, buf, len);
}

static void softusb_mouse_changed(MilkymistSoftUsbState *s)
{
    uint8_t m;
    uint8_t buf[4];

    buf[0] = s->mouse_buttons_state;
    buf[1] = s->mouse_dx;
    buf[2] = s->mouse_dy;
    buf[3] = s->mouse_dz;

    softusb_read_dmem(s, COMLOC_MEVT_PRODUCE, &m, 1);
    trace_milkymist_softusb_mevt(m);
    softusb_write_dmem(s, COMLOC_MEVT_BASE + 4 * m, buf, 4);
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
    softusb_write_dmem(s, COMLOC_KEVT_BASE + 8 * m, s->kbd_usb_buffer, 8);
    m = (m + 1) & 0x7;
    softusb_write_dmem(s, COMLOC_KEVT_PRODUCE, &m, 1);

    trace_milkymist_softusb_pulse_irq();
    qemu_irq_pulse(s->irq);
}

static void softusb_mouse_event(void *opaque,
       int dx, int dy, int dz, int buttons_state)
{
    MilkymistSoftUsbState *s = opaque;

    /* if device is in reset, do nothing */
    if (s->regs[R_CTRL] & CTRL_RESET) {
        return;
    }

    trace_milkymist_softusb_mouse_event(dx, dy, dz, buttons_state);

    s->mouse_dx = dx;
    s->mouse_dy = dy;
    s->mouse_dz = dz;
    s->mouse_buttons_state = buttons_state;

    softusb_mouse_changed(s);
}

static void softusb_usbdev_datain(void *opaque)
{
    MilkymistSoftUsbState *s = opaque;

    USBPacket p;

    p.pid = USB_TOKEN_IN;
    p.devep = 1;
    p.data = s->kbd_usb_buffer;
    p.len = sizeof(s->kbd_usb_buffer);
    s->usbdev->info->handle_data(s->usbdev, &p);

    softusb_kbd_changed(s);
}

static void softusb_attach(USBPort *port)
{
}

static void softusb_device_destroy(USBBus *bus, USBDevice *dev)
{
}

static USBPortOps softusb_ops = {
    .attach = softusb_attach,
};

static USBBusOps softusb_bus_ops = {
    .device_destroy = softusb_device_destroy,
};

static void milkymist_softusb_reset(DeviceState *d)
{
    MilkymistSoftUsbState *s =
            container_of(d, MilkymistSoftUsbState, busdev.qdev);
    int i;

    for (i = 0; i < R_MAX; i++) {
        s->regs[i] = 0;
    }
    s->mouse_dx = 0;
    s->mouse_dy = 0;
    s->mouse_dz = 0;
    s->mouse_buttons_state = 0;
    memset(s->kbd_usb_buffer, 0, sizeof(s->kbd_usb_buffer));

    /* defaults */
    s->regs[R_CTRL] = CTRL_RESET;
}

static int milkymist_softusb_init(SysBusDevice *dev)
{
    MilkymistSoftUsbState *s = FROM_SYSBUS(typeof(*s), dev);
    int softusb_regs;
    ram_addr_t pmem_ram;
    ram_addr_t dmem_ram;

    sysbus_init_irq(dev, &s->irq);

    softusb_regs = cpu_register_io_memory(softusb_read_fn, softusb_write_fn, s,
            DEVICE_NATIVE_ENDIAN);
    sysbus_init_mmio(dev, R_MAX * 4, softusb_regs);

    /* register pmem and dmem */
    pmem_ram = qemu_ram_alloc(NULL, "milkymist_softusb.pmem", s->pmem_size);
    cpu_register_physical_memory(s->pmem_base, s->pmem_size,
            pmem_ram | IO_MEM_RAM);
    dmem_ram = qemu_ram_alloc(NULL, "milkymist_softusb.dmem", s->dmem_size);
    cpu_register_physical_memory(s->dmem_base, s->dmem_size,
            dmem_ram | IO_MEM_RAM);

    qemu_add_mouse_event_handler(softusb_mouse_event, s, 0, "Milkymist Mouse");

    /* create our usb bus */
    usb_bus_new(&s->usbbus, &softusb_bus_ops, NULL);

    /* our two ports */
    usb_register_port(&s->usbbus, &s->usbport[0], NULL, 0, &softusb_ops,
            USB_SPEED_MASK_LOW);
    usb_register_port(&s->usbbus, &s->usbport[1], NULL, 1, &softusb_ops,
            USB_SPEED_MASK_LOW);

    /* and finally create an usb keyboard */
    s->usbdev = usb_create_simple(&s->usbbus, "usb-kbd");
    usb_hid_datain_cb(s->usbdev, s, softusb_usbdev_datain);
    s->usbdev->info->handle_reset(s->usbdev);

    return 0;
}

static const VMStateDescription vmstate_milkymist_softusb = {
    .name = "milkymist-softusb",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, MilkymistSoftUsbState, R_MAX),
        VMSTATE_INT32(mouse_dx, MilkymistSoftUsbState),
        VMSTATE_INT32(mouse_dy, MilkymistSoftUsbState),
        VMSTATE_INT32(mouse_dz, MilkymistSoftUsbState),
        VMSTATE_UINT8(mouse_buttons_state, MilkymistSoftUsbState),
        VMSTATE_BUFFER(kbd_usb_buffer, MilkymistSoftUsbState),
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
