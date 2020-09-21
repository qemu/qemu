/*
 * QEMU Sparc SLAVIO aux io port emulation
 *
 * Copyright (c) 2005 Fabrice Bellard
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

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/module.h"
#include "sysemu/runstate.h"
#include "trace.h"
#include "qom/object.h"

/*
 * This is the auxio port, chip control and system control part of
 * chip STP2001 (Slave I/O), also produced as NCR89C105. See
 * http://www.ibiblio.org/pub/historic-linux/early-ports/Sparc/NCR/NCR89C105.txt
 *
 * This also includes the PMC CPU idle controller.
 */

#define TYPE_SLAVIO_MISC "slavio_misc"
OBJECT_DECLARE_SIMPLE_TYPE(MiscState, SLAVIO_MISC)

struct MiscState {
    SysBusDevice parent_obj;

    MemoryRegion cfg_iomem;
    MemoryRegion diag_iomem;
    MemoryRegion mdm_iomem;
    MemoryRegion led_iomem;
    MemoryRegion sysctrl_iomem;
    MemoryRegion aux1_iomem;
    MemoryRegion aux2_iomem;
    qemu_irq irq;
    qemu_irq fdc_tc;
    uint32_t dummy;
    uint8_t config;
    uint8_t aux1, aux2;
    uint8_t diag, mctrl;
    uint8_t sysctrl;
    uint16_t leds;
};

#define TYPE_APC "apc"
typedef struct APCState APCState;
DECLARE_INSTANCE_CHECKER(APCState, APC,
                         TYPE_APC)

struct APCState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq cpu_halt;
};

#define MISC_SIZE 1
#define LED_SIZE 2
#define SYSCTRL_SIZE 4

#define AUX1_TC        0x02

#define AUX2_PWROFF    0x01
#define AUX2_PWRINTCLR 0x02
#define AUX2_PWRFAIL   0x20

#define CFG_PWRINTEN   0x08

#define SYS_RESET      0x01
#define SYS_RESETSTAT  0x02

static void slavio_misc_update_irq(void *opaque)
{
    MiscState *s = opaque;

    if ((s->aux2 & AUX2_PWRFAIL) && (s->config & CFG_PWRINTEN)) {
        trace_slavio_misc_update_irq_raise();
        qemu_irq_raise(s->irq);
    } else {
        trace_slavio_misc_update_irq_lower();
        qemu_irq_lower(s->irq);
    }
}

static void slavio_misc_reset(DeviceState *d)
{
    MiscState *s = SLAVIO_MISC(d);

    // Diagnostic and system control registers not cleared in reset
    s->config = s->aux1 = s->aux2 = s->mctrl = 0;
}

static void slavio_set_power_fail(void *opaque, int irq, int power_failing)
{
    MiscState *s = opaque;

    trace_slavio_set_power_fail(power_failing, s->config);
    if (power_failing && (s->config & CFG_PWRINTEN)) {
        s->aux2 |= AUX2_PWRFAIL;
    } else {
        s->aux2 &= ~AUX2_PWRFAIL;
    }
    slavio_misc_update_irq(s);
}

static void slavio_cfg_mem_writeb(void *opaque, hwaddr addr,
                                  uint64_t val, unsigned size)
{
    MiscState *s = opaque;

    trace_slavio_cfg_mem_writeb(val & 0xff);
    s->config = val & 0xff;
    slavio_misc_update_irq(s);
}

static uint64_t slavio_cfg_mem_readb(void *opaque, hwaddr addr,
                                     unsigned size)
{
    MiscState *s = opaque;
    uint32_t ret = 0;

    ret = s->config;
    trace_slavio_cfg_mem_readb(ret);
    return ret;
}

static const MemoryRegionOps slavio_cfg_mem_ops = {
    .read = slavio_cfg_mem_readb,
    .write = slavio_cfg_mem_writeb,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void slavio_diag_mem_writeb(void *opaque, hwaddr addr,
                                   uint64_t val, unsigned size)
{
    MiscState *s = opaque;

    trace_slavio_diag_mem_writeb(val & 0xff);
    s->diag = val & 0xff;
}

static uint64_t slavio_diag_mem_readb(void *opaque, hwaddr addr,
                                      unsigned size)
{
    MiscState *s = opaque;
    uint32_t ret = 0;

    ret = s->diag;
    trace_slavio_diag_mem_readb(ret);
    return ret;
}

static const MemoryRegionOps slavio_diag_mem_ops = {
    .read = slavio_diag_mem_readb,
    .write = slavio_diag_mem_writeb,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void slavio_mdm_mem_writeb(void *opaque, hwaddr addr,
                                  uint64_t val, unsigned size)
{
    MiscState *s = opaque;

    trace_slavio_mdm_mem_writeb(val & 0xff);
    s->mctrl = val & 0xff;
}

static uint64_t slavio_mdm_mem_readb(void *opaque, hwaddr addr,
                                     unsigned size)
{
    MiscState *s = opaque;
    uint32_t ret = 0;

    ret = s->mctrl;
    trace_slavio_mdm_mem_readb(ret);
    return ret;
}

static const MemoryRegionOps slavio_mdm_mem_ops = {
    .read = slavio_mdm_mem_readb,
    .write = slavio_mdm_mem_writeb,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void slavio_aux1_mem_writeb(void *opaque, hwaddr addr,
                                   uint64_t val, unsigned size)
{
    MiscState *s = opaque;

    trace_slavio_aux1_mem_writeb(val & 0xff);
    if (val & AUX1_TC) {
        // Send a pulse to floppy terminal count line
        if (s->fdc_tc) {
            qemu_irq_raise(s->fdc_tc);
            qemu_irq_lower(s->fdc_tc);
        }
        val &= ~AUX1_TC;
    }
    s->aux1 = val & 0xff;
}

static uint64_t slavio_aux1_mem_readb(void *opaque, hwaddr addr,
                                      unsigned size)
{
    MiscState *s = opaque;
    uint32_t ret = 0;

    ret = s->aux1;
    trace_slavio_aux1_mem_readb(ret);
    return ret;
}

static const MemoryRegionOps slavio_aux1_mem_ops = {
    .read = slavio_aux1_mem_readb,
    .write = slavio_aux1_mem_writeb,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void slavio_aux2_mem_writeb(void *opaque, hwaddr addr,
                                   uint64_t val, unsigned size)
{
    MiscState *s = opaque;

    val &= AUX2_PWRINTCLR | AUX2_PWROFF;
    trace_slavio_aux2_mem_writeb(val & 0xff);
    val |= s->aux2 & AUX2_PWRFAIL;
    if (val & AUX2_PWRINTCLR) // Clear Power Fail int
        val &= AUX2_PWROFF;
    s->aux2 = val;
    if (val & AUX2_PWROFF)
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
    slavio_misc_update_irq(s);
}

static uint64_t slavio_aux2_mem_readb(void *opaque, hwaddr addr,
                                      unsigned size)
{
    MiscState *s = opaque;
    uint32_t ret = 0;

    ret = s->aux2;
    trace_slavio_aux2_mem_readb(ret);
    return ret;
}

static const MemoryRegionOps slavio_aux2_mem_ops = {
    .read = slavio_aux2_mem_readb,
    .write = slavio_aux2_mem_writeb,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void apc_mem_writeb(void *opaque, hwaddr addr,
                           uint64_t val, unsigned size)
{
    APCState *s = opaque;

    trace_apc_mem_writeb(val & 0xff);
    qemu_irq_raise(s->cpu_halt);
}

static uint64_t apc_mem_readb(void *opaque, hwaddr addr,
                              unsigned size)
{
    uint32_t ret = 0;

    trace_apc_mem_readb(ret);
    return ret;
}

static const MemoryRegionOps apc_mem_ops = {
    .read = apc_mem_readb,
    .write = apc_mem_writeb,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    }
};

static uint64_t slavio_sysctrl_mem_readl(void *opaque, hwaddr addr,
                                         unsigned size)
{
    MiscState *s = opaque;
    uint32_t ret = 0;

    switch (addr) {
    case 0:
        ret = s->sysctrl;
        break;
    default:
        break;
    }
    trace_slavio_sysctrl_mem_readl(ret);
    return ret;
}

static void slavio_sysctrl_mem_writel(void *opaque, hwaddr addr,
                                      uint64_t val, unsigned size)
{
    MiscState *s = opaque;

    trace_slavio_sysctrl_mem_writel(val);
    switch (addr) {
    case 0:
        if (val & SYS_RESET) {
            s->sysctrl = SYS_RESETSTAT;
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        }
        break;
    default:
        break;
    }
}

static const MemoryRegionOps slavio_sysctrl_mem_ops = {
    .read = slavio_sysctrl_mem_readl,
    .write = slavio_sysctrl_mem_writel,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static uint64_t slavio_led_mem_readw(void *opaque, hwaddr addr,
                                     unsigned size)
{
    MiscState *s = opaque;
    uint32_t ret = 0;

    switch (addr) {
    case 0:
        ret = s->leds;
        break;
    default:
        break;
    }
    trace_slavio_led_mem_readw(ret);
    return ret;
}

static void slavio_led_mem_writew(void *opaque, hwaddr addr,
                                  uint64_t val, unsigned size)
{
    MiscState *s = opaque;

    trace_slavio_led_mem_writew(val & 0xffff);
    switch (addr) {
    case 0:
        s->leds = val;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps slavio_led_mem_ops = {
    .read = slavio_led_mem_readw,
    .write = slavio_led_mem_writew,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 2,
        .max_access_size = 2,
    },
};

static const VMStateDescription vmstate_misc = {
    .name ="slavio_misc",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(dummy, MiscState),
        VMSTATE_UINT8(config, MiscState),
        VMSTATE_UINT8(aux1, MiscState),
        VMSTATE_UINT8(aux2, MiscState),
        VMSTATE_UINT8(diag, MiscState),
        VMSTATE_UINT8(mctrl, MiscState),
        VMSTATE_UINT8(sysctrl, MiscState),
        VMSTATE_END_OF_LIST()
    }
};

static void apc_init(Object *obj)
{
    APCState *s = APC(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);

    sysbus_init_irq(dev, &s->cpu_halt);

    /* Power management (APC) XXX: not a Slavio device */
    memory_region_init_io(&s->iomem, obj, &apc_mem_ops, s,
                          "apc", MISC_SIZE);
    sysbus_init_mmio(dev, &s->iomem);
}

static void slavio_misc_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    MiscState *s = SLAVIO_MISC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    sysbus_init_irq(sbd, &s->irq);
    sysbus_init_irq(sbd, &s->fdc_tc);

    /* 8 bit registers */
    /* Slavio control */
    memory_region_init_io(&s->cfg_iomem, obj, &slavio_cfg_mem_ops, s,
                          "configuration", MISC_SIZE);
    sysbus_init_mmio(sbd, &s->cfg_iomem);

    /* Diagnostics */
    memory_region_init_io(&s->diag_iomem, obj, &slavio_diag_mem_ops, s,
                          "diagnostic", MISC_SIZE);
    sysbus_init_mmio(sbd, &s->diag_iomem);

    /* Modem control */
    memory_region_init_io(&s->mdm_iomem, obj, &slavio_mdm_mem_ops, s,
                          "modem", MISC_SIZE);
    sysbus_init_mmio(sbd, &s->mdm_iomem);

    /* 16 bit registers */
    /* ss600mp diag LEDs */
    memory_region_init_io(&s->led_iomem, obj, &slavio_led_mem_ops, s,
                          "leds", LED_SIZE);
    sysbus_init_mmio(sbd, &s->led_iomem);

    /* 32 bit registers */
    /* System control */
    memory_region_init_io(&s->sysctrl_iomem, obj, &slavio_sysctrl_mem_ops, s,
                          "system-control", SYSCTRL_SIZE);
    sysbus_init_mmio(sbd, &s->sysctrl_iomem);

    /* AUX 1 (Misc System Functions) */
    memory_region_init_io(&s->aux1_iomem, obj, &slavio_aux1_mem_ops, s,
                          "misc-system-functions", MISC_SIZE);
    sysbus_init_mmio(sbd, &s->aux1_iomem);

    /* AUX 2 (Software Powerdown Control) */
    memory_region_init_io(&s->aux2_iomem, obj, &slavio_aux2_mem_ops, s,
                          "software-powerdown-control", MISC_SIZE);
    sysbus_init_mmio(sbd, &s->aux2_iomem);

    qdev_init_gpio_in(dev, slavio_set_power_fail, 1);
}

static void slavio_misc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = slavio_misc_reset;
    dc->vmsd = &vmstate_misc;
}

static const TypeInfo slavio_misc_info = {
    .name          = TYPE_SLAVIO_MISC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MiscState),
    .instance_init = slavio_misc_init,
    .class_init    = slavio_misc_class_init,
};

static const TypeInfo apc_info = {
    .name          = TYPE_APC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MiscState),
    .instance_init = apc_init,
};

static void slavio_misc_register_types(void)
{
    type_register_static(&slavio_misc_info);
    type_register_static(&apc_info);
}

type_init(slavio_misc_register_types)
