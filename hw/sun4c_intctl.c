/*
 * QEMU Sparc Sun4c interrupt controller emulation
 *
 * Based on slavio_intctl, copyright (c) 2003-2005 Fabrice Bellard
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

#include "hw.h"
#include "sun4m.h"
#include "monitor.h"
#include "sysbus.h"

//#define DEBUG_IRQ_COUNT
//#define DEBUG_IRQ

#ifdef DEBUG_IRQ
#define DPRINTF(fmt, ...)                                       \
    do { printf("IRQ: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...)
#endif

/*
 * Registers of interrupt controller in sun4c.
 *
 */

#define MAX_PILS 16

typedef struct Sun4c_INTCTLState {
    SysBusDevice busdev;
#ifdef DEBUG_IRQ_COUNT
    uint64_t irq_count;
#endif
    qemu_irq cpu_irqs[MAX_PILS];
    const uint32_t *intbit_to_level;
    uint32_t pil_out;
    uint8_t reg;
    uint8_t pending;
} Sun4c_INTCTLState;

#define INTCTL_SIZE 1

static void sun4c_check_interrupts(void *opaque);

static uint32_t sun4c_intctl_mem_readb(void *opaque, target_phys_addr_t addr)
{
    Sun4c_INTCTLState *s = opaque;
    uint32_t ret;

    ret = s->reg;
    DPRINTF("read reg 0x" TARGET_FMT_plx " = %x\n", addr, ret);

    return ret;
}

static void sun4c_intctl_mem_writeb(void *opaque, target_phys_addr_t addr,
                                    uint32_t val)
{
    Sun4c_INTCTLState *s = opaque;

    DPRINTF("write reg 0x" TARGET_FMT_plx " = %x\n", addr, val);
    val &= 0xbf;
    s->reg = val;
    sun4c_check_interrupts(s);
}

static CPUReadMemoryFunc * const sun4c_intctl_mem_read[3] = {
    sun4c_intctl_mem_readb,
    NULL,
    NULL,
};

static CPUWriteMemoryFunc * const sun4c_intctl_mem_write[3] = {
    sun4c_intctl_mem_writeb,
    NULL,
    NULL,
};

void sun4c_pic_info(Monitor *mon, void *opaque)
{
    Sun4c_INTCTLState *s = opaque;

    monitor_printf(mon, "master: pending 0x%2.2x, enabled 0x%2.2x\n",
                   s->pending, s->reg);
}

void sun4c_irq_info(Monitor *mon, void *opaque)
{
#ifndef DEBUG_IRQ_COUNT
    monitor_printf(mon, "irq statistic code not compiled.\n");
#else
    Sun4c_INTCTLState *s = opaque;
    int64_t count;

    monitor_printf(mon, "IRQ statistics:\n");
    count = s->irq_count;
    if (count > 0)
        monitor_printf(mon, " %" PRId64 "\n", count);
#endif
}

static const uint32_t intbit_to_level[] = { 0, 1, 4, 6, 8, 10, 0, 14, };

static void sun4c_check_interrupts(void *opaque)
{
    Sun4c_INTCTLState *s = opaque;
    uint32_t pil_pending;
    unsigned int i;

    pil_pending = 0;
    if (s->pending && !(s->reg & 0x80000000)) {
        for (i = 0; i < 8; i++) {
            if (s->pending & (1 << i))
                pil_pending |= 1 << intbit_to_level[i];
        }
    }

    for (i = 0; i < MAX_PILS; i++) {
        if (pil_pending & (1 << i)) {
            if (!(s->pil_out & (1 << i)))
                qemu_irq_raise(s->cpu_irqs[i]);
        } else {
            if (s->pil_out & (1 << i))
                qemu_irq_lower(s->cpu_irqs[i]);
        }
    }
    s->pil_out = pil_pending;
}

/*
 * "irq" here is the bit number in the system interrupt register
 */
static void sun4c_set_irq(void *opaque, int irq, int level)
{
    Sun4c_INTCTLState *s = opaque;
    uint32_t mask = 1 << irq;
    uint32_t pil = intbit_to_level[irq];

    DPRINTF("Set irq %d -> pil %d level %d\n", irq, pil,
            level);
    if (pil > 0) {
        if (level) {
#ifdef DEBUG_IRQ_COUNT
            s->irq_count++;
#endif
            s->pending |= mask;
        } else {
            s->pending &= ~mask;
        }
        sun4c_check_interrupts(s);
    }
}

static const VMStateDescription vmstate_sun4c_intctl = {
    .name ="sun4c_intctl",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField []) {
        VMSTATE_UINT8(reg, Sun4c_INTCTLState),
        VMSTATE_UINT8(pending, Sun4c_INTCTLState),
        VMSTATE_END_OF_LIST()
    }
};

static void sun4c_intctl_reset(DeviceState *d)
{
    Sun4c_INTCTLState *s = container_of(d, Sun4c_INTCTLState, busdev.qdev);

    s->reg = 1;
    s->pending = 0;
}

static int sun4c_intctl_init1(SysBusDevice *dev)
{
    Sun4c_INTCTLState *s = FROM_SYSBUS(Sun4c_INTCTLState, dev);
    int io_memory;
    unsigned int i;

    io_memory = cpu_register_io_memory(sun4c_intctl_mem_read,
                                       sun4c_intctl_mem_write, s,
                                       DEVICE_NATIVE_ENDIAN);
    sysbus_init_mmio(dev, INTCTL_SIZE, io_memory);
    qdev_init_gpio_in(&dev->qdev, sun4c_set_irq, 8);

    for (i = 0; i < MAX_PILS; i++) {
        sysbus_init_irq(dev, &s->cpu_irqs[i]);
    }

    return 0;
}

static SysBusDeviceInfo sun4c_intctl_info = {
    .init = sun4c_intctl_init1,
    .qdev.name  = "sun4c_intctl",
    .qdev.size  = sizeof(Sun4c_INTCTLState),
    .qdev.vmsd  = &vmstate_sun4c_intctl,
    .qdev.reset = sun4c_intctl_reset,
};

static void sun4c_intctl_register_devices(void)
{
    sysbus_register_withprop(&sun4c_intctl_info);
}

device_init(sun4c_intctl_register_devices)
