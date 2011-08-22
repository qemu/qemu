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

#include "sysemu.h"
#include "sysbus.h"
#include "trace.h"

/*
 * This is the auxio port, chip control and system control part of
 * chip STP2001 (Slave I/O), also produced as NCR89C105. See
 * http://www.ibiblio.org/pub/historic-linux/early-ports/Sparc/NCR/NCR89C105.txt
 *
 * This also includes the PMC CPU idle controller.
 */

typedef struct MiscState {
    SysBusDevice busdev;
    qemu_irq irq;
    qemu_irq fdc_tc;
    uint32_t dummy;
    uint8_t config;
    uint8_t aux1, aux2;
    uint8_t diag, mctrl;
    uint8_t sysctrl;
    uint16_t leds;
} MiscState;

typedef struct APCState {
    SysBusDevice busdev;
    qemu_irq cpu_halt;
} APCState;

#define MISC_SIZE 1
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
    MiscState *s = container_of(d, MiscState, busdev.qdev);

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

static void slavio_cfg_mem_writeb(void *opaque, target_phys_addr_t addr,
                                  uint32_t val)
{
    MiscState *s = opaque;

    trace_slavio_cfg_mem_writeb(val & 0xff);
    s->config = val & 0xff;
    slavio_misc_update_irq(s);
}

static uint32_t slavio_cfg_mem_readb(void *opaque, target_phys_addr_t addr)
{
    MiscState *s = opaque;
    uint32_t ret = 0;

    ret = s->config;
    trace_slavio_cfg_mem_readb(ret);
    return ret;
}

static CPUReadMemoryFunc * const slavio_cfg_mem_read[3] = {
    slavio_cfg_mem_readb,
    NULL,
    NULL,
};

static CPUWriteMemoryFunc * const slavio_cfg_mem_write[3] = {
    slavio_cfg_mem_writeb,
    NULL,
    NULL,
};

static void slavio_diag_mem_writeb(void *opaque, target_phys_addr_t addr,
                                   uint32_t val)
{
    MiscState *s = opaque;

    trace_slavio_diag_mem_writeb(val & 0xff);
    s->diag = val & 0xff;
}

static uint32_t slavio_diag_mem_readb(void *opaque, target_phys_addr_t addr)
{
    MiscState *s = opaque;
    uint32_t ret = 0;

    ret = s->diag;
    trace_slavio_diag_mem_readb(ret);
    return ret;
}

static CPUReadMemoryFunc * const slavio_diag_mem_read[3] = {
    slavio_diag_mem_readb,
    NULL,
    NULL,
};

static CPUWriteMemoryFunc * const slavio_diag_mem_write[3] = {
    slavio_diag_mem_writeb,
    NULL,
    NULL,
};

static void slavio_mdm_mem_writeb(void *opaque, target_phys_addr_t addr,
                                  uint32_t val)
{
    MiscState *s = opaque;

    trace_slavio_mdm_mem_writeb(val & 0xff);
    s->mctrl = val & 0xff;
}

static uint32_t slavio_mdm_mem_readb(void *opaque, target_phys_addr_t addr)
{
    MiscState *s = opaque;
    uint32_t ret = 0;

    ret = s->mctrl;
    trace_slavio_mdm_mem_readb(ret);
    return ret;
}

static CPUReadMemoryFunc * const slavio_mdm_mem_read[3] = {
    slavio_mdm_mem_readb,
    NULL,
    NULL,
};

static CPUWriteMemoryFunc * const slavio_mdm_mem_write[3] = {
    slavio_mdm_mem_writeb,
    NULL,
    NULL,
};

static void slavio_aux1_mem_writeb(void *opaque, target_phys_addr_t addr,
                                   uint32_t val)
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

static uint32_t slavio_aux1_mem_readb(void *opaque, target_phys_addr_t addr)
{
    MiscState *s = opaque;
    uint32_t ret = 0;

    ret = s->aux1;
    trace_slavio_aux1_mem_readb(ret);
    return ret;
}

static CPUReadMemoryFunc * const slavio_aux1_mem_read[3] = {
    slavio_aux1_mem_readb,
    NULL,
    NULL,
};

static CPUWriteMemoryFunc * const slavio_aux1_mem_write[3] = {
    slavio_aux1_mem_writeb,
    NULL,
    NULL,
};

static void slavio_aux2_mem_writeb(void *opaque, target_phys_addr_t addr,
                                   uint32_t val)
{
    MiscState *s = opaque;

    val &= AUX2_PWRINTCLR | AUX2_PWROFF;
    trace_slavio_aux2_mem_writeb(val & 0xff);
    val |= s->aux2 & AUX2_PWRFAIL;
    if (val & AUX2_PWRINTCLR) // Clear Power Fail int
        val &= AUX2_PWROFF;
    s->aux2 = val;
    if (val & AUX2_PWROFF)
        qemu_system_shutdown_request();
    slavio_misc_update_irq(s);
}

static uint32_t slavio_aux2_mem_readb(void *opaque, target_phys_addr_t addr)
{
    MiscState *s = opaque;
    uint32_t ret = 0;

    ret = s->aux2;
    trace_slavio_aux2_mem_readb(ret);
    return ret;
}

static CPUReadMemoryFunc * const slavio_aux2_mem_read[3] = {
    slavio_aux2_mem_readb,
    NULL,
    NULL,
};

static CPUWriteMemoryFunc * const slavio_aux2_mem_write[3] = {
    slavio_aux2_mem_writeb,
    NULL,
    NULL,
};

static void apc_mem_writeb(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    APCState *s = opaque;

    trace_apc_mem_writeb(val & 0xff);
    qemu_irq_raise(s->cpu_halt);
}

static uint32_t apc_mem_readb(void *opaque, target_phys_addr_t addr)
{
    uint32_t ret = 0;

    trace_apc_mem_readb(ret);
    return ret;
}

static CPUReadMemoryFunc * const apc_mem_read[3] = {
    apc_mem_readb,
    NULL,
    NULL,
};

static CPUWriteMemoryFunc * const apc_mem_write[3] = {
    apc_mem_writeb,
    NULL,
    NULL,
};

static uint32_t slavio_sysctrl_mem_readl(void *opaque, target_phys_addr_t addr)
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

static void slavio_sysctrl_mem_writel(void *opaque, target_phys_addr_t addr,
                                      uint32_t val)
{
    MiscState *s = opaque;

    trace_slavio_sysctrl_mem_writel(val);
    switch (addr) {
    case 0:
        if (val & SYS_RESET) {
            s->sysctrl = SYS_RESETSTAT;
            qemu_system_reset_request();
        }
        break;
    default:
        break;
    }
}

static CPUReadMemoryFunc * const slavio_sysctrl_mem_read[3] = {
    NULL,
    NULL,
    slavio_sysctrl_mem_readl,
};

static CPUWriteMemoryFunc * const slavio_sysctrl_mem_write[3] = {
    NULL,
    NULL,
    slavio_sysctrl_mem_writel,
};

static uint32_t slavio_led_mem_readw(void *opaque, target_phys_addr_t addr)
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

static void slavio_led_mem_writew(void *opaque, target_phys_addr_t addr,
                                  uint32_t val)
{
    MiscState *s = opaque;

    trace_slavio_led_mem_readw(val & 0xffff);
    switch (addr) {
    case 0:
        s->leds = val;
        break;
    default:
        break;
    }
}

static CPUReadMemoryFunc * const slavio_led_mem_read[3] = {
    NULL,
    slavio_led_mem_readw,
    NULL,
};

static CPUWriteMemoryFunc * const slavio_led_mem_write[3] = {
    NULL,
    slavio_led_mem_writew,
    NULL,
};

static const VMStateDescription vmstate_misc = {
    .name ="slavio_misc",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField []) {
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

static int apc_init1(SysBusDevice *dev)
{
    APCState *s = FROM_SYSBUS(APCState, dev);
    int io;

    sysbus_init_irq(dev, &s->cpu_halt);

    /* Power management (APC) XXX: not a Slavio device */
    io = cpu_register_io_memory(apc_mem_read, apc_mem_write, s,
                                DEVICE_NATIVE_ENDIAN);
    sysbus_init_mmio(dev, MISC_SIZE, io);
    return 0;
}

static int slavio_misc_init1(SysBusDevice *dev)
{
    MiscState *s = FROM_SYSBUS(MiscState, dev);
    int io;

    sysbus_init_irq(dev, &s->irq);
    sysbus_init_irq(dev, &s->fdc_tc);

    /* 8 bit registers */
    /* Slavio control */
    io = cpu_register_io_memory(slavio_cfg_mem_read,
                                slavio_cfg_mem_write, s,
                                DEVICE_NATIVE_ENDIAN);
    sysbus_init_mmio(dev, MISC_SIZE, io);

    /* Diagnostics */
    io = cpu_register_io_memory(slavio_diag_mem_read,
                                slavio_diag_mem_write, s,
                                DEVICE_NATIVE_ENDIAN);
    sysbus_init_mmio(dev, MISC_SIZE, io);

    /* Modem control */
    io = cpu_register_io_memory(slavio_mdm_mem_read,
                                slavio_mdm_mem_write, s,
                                DEVICE_NATIVE_ENDIAN);
    sysbus_init_mmio(dev, MISC_SIZE, io);

    /* 16 bit registers */
    /* ss600mp diag LEDs */
    io = cpu_register_io_memory(slavio_led_mem_read,
                                slavio_led_mem_write, s,
                                DEVICE_NATIVE_ENDIAN);
    sysbus_init_mmio(dev, MISC_SIZE, io);

    /* 32 bit registers */
    /* System control */
    io = cpu_register_io_memory(slavio_sysctrl_mem_read,
                                slavio_sysctrl_mem_write, s,
                                DEVICE_NATIVE_ENDIAN);
    sysbus_init_mmio(dev, SYSCTRL_SIZE, io);

    /* AUX 1 (Misc System Functions) */
    io = cpu_register_io_memory(slavio_aux1_mem_read,
                                slavio_aux1_mem_write, s,
                                DEVICE_NATIVE_ENDIAN);
    sysbus_init_mmio(dev, MISC_SIZE, io);

    /* AUX 2 (Software Powerdown Control) */
    io = cpu_register_io_memory(slavio_aux2_mem_read,
                                slavio_aux2_mem_write, s,
                                DEVICE_NATIVE_ENDIAN);
    sysbus_init_mmio(dev, MISC_SIZE, io);

    qdev_init_gpio_in(&dev->qdev, slavio_set_power_fail, 1);

    return 0;
}

static SysBusDeviceInfo slavio_misc_info = {
    .init = slavio_misc_init1,
    .qdev.name  = "slavio_misc",
    .qdev.size  = sizeof(MiscState),
    .qdev.vmsd  = &vmstate_misc,
    .qdev.reset  = slavio_misc_reset,
};

static SysBusDeviceInfo apc_info = {
    .init = apc_init1,
    .qdev.name  = "apc",
    .qdev.size  = sizeof(MiscState),
};

static void slavio_misc_register_devices(void)
{
    sysbus_register_withprop(&slavio_misc_info);
    sysbus_register_withprop(&apc_info);
}

device_init(slavio_misc_register_devices)
