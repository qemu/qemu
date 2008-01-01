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
#include "hw.h"
#include "sun4m.h"
#include "sysemu.h"

/* debug misc */
//#define DEBUG_MISC

/*
 * This is the auxio port, chip control and system control part of
 * chip STP2001 (Slave I/O), also produced as NCR89C105. See
 * http://www.ibiblio.org/pub/historic-linux/early-ports/Sparc/NCR/NCR89C105.txt
 *
 * This also includes the PMC CPU idle controller.
 */

#ifdef DEBUG_MISC
#define MISC_DPRINTF(fmt, args...) \
do { printf("MISC: " fmt , ##args); } while (0)
#else
#define MISC_DPRINTF(fmt, args...)
#endif

typedef struct MiscState {
    qemu_irq irq;
    uint8_t config;
    uint8_t aux1, aux2;
    uint8_t diag, mctrl;
    uint32_t sysctrl;
    uint16_t leds;
    target_phys_addr_t power_base;
} MiscState;

#define MISC_SIZE 1
#define SYSCTRL_MAXADDR 3
#define SYSCTRL_SIZE (SYSCTRL_MAXADDR + 1)
#define LED_MAXADDR 1
#define LED_SIZE (LED_MAXADDR + 1)

#define MISC_MASK 0x0fff0000
#define MISC_LEDS 0x01600000
#define MISC_CFG  0x01800000
#define MISC_AUX1 0x01900000
#define MISC_AUX2 0x01910000
#define MISC_DIAG 0x01a00000
#define MISC_MDM  0x01b00000
#define MISC_SYS  0x01f00000

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
        MISC_DPRINTF("Raise IRQ\n");
        qemu_irq_raise(s->irq);
    } else {
        MISC_DPRINTF("Lower IRQ\n");
        qemu_irq_lower(s->irq);
    }
}

static void slavio_misc_reset(void *opaque)
{
    MiscState *s = opaque;

    // Diagnostic and system control registers not cleared in reset
    s->config = s->aux1 = s->aux2 = s->mctrl = 0;
}

void slavio_set_power_fail(void *opaque, int power_failing)
{
    MiscState *s = opaque;

    MISC_DPRINTF("Power fail: %d, config: %d\n", power_failing, s->config);
    if (power_failing && (s->config & CFG_PWRINTEN)) {
        s->aux2 |= AUX2_PWRFAIL;
    } else {
        s->aux2 &= ~AUX2_PWRFAIL;
    }
    slavio_misc_update_irq(s);
}

static void slavio_misc_mem_writeb(void *opaque, target_phys_addr_t addr,
                                   uint32_t val)
{
    MiscState *s = opaque;

    switch (addr & MISC_MASK) {
    case MISC_CFG:
        MISC_DPRINTF("Write config %2.2x\n", val & 0xff);
        s->config = val & 0xff;
        slavio_misc_update_irq(s);
        break;
    case MISC_AUX1:
        MISC_DPRINTF("Write aux1 %2.2x\n", val & 0xff);
        s->aux1 = val & 0xff;
        break;
    case MISC_AUX2:
        val &= AUX2_PWRINTCLR | AUX2_PWROFF;
        MISC_DPRINTF("Write aux2 %2.2x\n", val);
        val |= s->aux2 & AUX2_PWRFAIL;
        if (val & AUX2_PWRINTCLR) // Clear Power Fail int
            val &= AUX2_PWROFF;
        s->aux2 = val;
        if (val & AUX2_PWROFF)
            qemu_system_shutdown_request();
        slavio_misc_update_irq(s);
        break;
    case MISC_DIAG:
        MISC_DPRINTF("Write diag %2.2x\n", val & 0xff);
        s->diag = val & 0xff;
        break;
    case MISC_MDM:
        MISC_DPRINTF("Write modem control %2.2x\n", val & 0xff);
        s->mctrl = val & 0xff;
        break;
    default:
        if (addr == s->power_base) {
            MISC_DPRINTF("Write power management %2.2x\n", val & 0xff);
            cpu_interrupt(cpu_single_env, CPU_INTERRUPT_HALT);
        }
        break;
    }
}

static uint32_t slavio_misc_mem_readb(void *opaque, target_phys_addr_t addr)
{
    MiscState *s = opaque;
    uint32_t ret = 0;

    switch (addr & MISC_MASK) {
    case MISC_CFG:
        ret = s->config;
        MISC_DPRINTF("Read config %2.2x\n", ret);
        break;
    case MISC_AUX1:
        ret = s->aux1;
        MISC_DPRINTF("Read aux1 %2.2x\n", ret);
        break;
    case MISC_AUX2:
        ret = s->aux2;
        MISC_DPRINTF("Read aux2 %2.2x\n", ret);
        break;
    case MISC_DIAG:
        ret = s->diag;
        MISC_DPRINTF("Read diag %2.2x\n", ret);
        break;
    case MISC_MDM:
        ret = s->mctrl;
        MISC_DPRINTF("Read modem control %2.2x\n", ret);
        break;
    default:
        if (addr == s->power_base) {
            MISC_DPRINTF("Read power management %2.2x\n", ret);
        }
        break;
    }
    return ret;
}

static CPUReadMemoryFunc *slavio_misc_mem_read[3] = {
    slavio_misc_mem_readb,
    NULL,
    NULL,
};

static CPUWriteMemoryFunc *slavio_misc_mem_write[3] = {
    slavio_misc_mem_writeb,
    NULL,
    NULL,
};

static uint32_t slavio_sysctrl_mem_readl(void *opaque, target_phys_addr_t addr)
{
    MiscState *s = opaque;
    uint32_t ret = 0, saddr;

    saddr = addr & SYSCTRL_MAXADDR;
    switch (saddr) {
    case 0:
        ret = s->sysctrl;
        break;
    default:
        break;
    }
    MISC_DPRINTF("Read system control reg 0x" TARGET_FMT_plx " = %x\n", addr,
                 ret);
    return ret;
}

static void slavio_sysctrl_mem_writel(void *opaque, target_phys_addr_t addr,
                                      uint32_t val)
{
    MiscState *s = opaque;
    uint32_t saddr;

    saddr = addr & SYSCTRL_MAXADDR;
    MISC_DPRINTF("Write system control reg 0x" TARGET_FMT_plx " =  %x\n", addr,
                 val);
    switch (saddr) {
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

static CPUReadMemoryFunc *slavio_sysctrl_mem_read[3] = {
    NULL,
    NULL,
    slavio_sysctrl_mem_readl,
};

static CPUWriteMemoryFunc *slavio_sysctrl_mem_write[3] = {
    NULL,
    NULL,
    slavio_sysctrl_mem_writel,
};

static uint32_t slavio_led_mem_readw(void *opaque, target_phys_addr_t addr)
{
    MiscState *s = opaque;
    uint32_t ret = 0, saddr;

    saddr = addr & LED_MAXADDR;
    switch (saddr) {
    case 0:
        ret = s->leds;
        break;
    default:
        break;
    }
    MISC_DPRINTF("Read diagnostic LED reg 0x" TARGET_FMT_plx " = %x\n", addr,
                 ret);
    return ret;
}

static void slavio_led_mem_writew(void *opaque, target_phys_addr_t addr,
                                  uint32_t val)
{
    MiscState *s = opaque;
    uint32_t saddr;

    saddr = addr & LED_MAXADDR;
    MISC_DPRINTF("Write diagnostic LED reg 0x" TARGET_FMT_plx " =  %x\n", addr,
                 val);
    switch (saddr) {
    case 0:
        s->leds = val;
        break;
    default:
        break;
    }
}

static CPUReadMemoryFunc *slavio_led_mem_read[3] = {
    NULL,
    slavio_led_mem_readw,
    NULL,
};

static CPUWriteMemoryFunc *slavio_led_mem_write[3] = {
    NULL,
    slavio_led_mem_writew,
    NULL,
};

static void slavio_misc_save(QEMUFile *f, void *opaque)
{
    MiscState *s = opaque;
    int tmp;
    uint8_t tmp8;

    tmp = 0;
    qemu_put_be32s(f, &tmp); /* ignored, was IRQ.  */
    qemu_put_8s(f, &s->config);
    qemu_put_8s(f, &s->aux1);
    qemu_put_8s(f, &s->aux2);
    qemu_put_8s(f, &s->diag);
    qemu_put_8s(f, &s->mctrl);
    tmp8 = s->sysctrl & 0xff;
    qemu_put_8s(f, &tmp8);
}

static int slavio_misc_load(QEMUFile *f, void *opaque, int version_id)
{
    MiscState *s = opaque;
    int tmp;
    uint8_t tmp8;

    if (version_id != 1)
        return -EINVAL;

    qemu_get_be32s(f, &tmp);
    qemu_get_8s(f, &s->config);
    qemu_get_8s(f, &s->aux1);
    qemu_get_8s(f, &s->aux2);
    qemu_get_8s(f, &s->diag);
    qemu_get_8s(f, &s->mctrl);
    qemu_get_8s(f, &tmp8);
    s->sysctrl = (uint32_t)tmp8;
    return 0;
}

void *slavio_misc_init(target_phys_addr_t base, target_phys_addr_t power_base,
                       qemu_irq irq)
{
    int slavio_misc_io_memory;
    MiscState *s;

    s = qemu_mallocz(sizeof(MiscState));
    if (!s)
        return NULL;

    /* 8 bit registers */
    slavio_misc_io_memory = cpu_register_io_memory(0, slavio_misc_mem_read,
                                                   slavio_misc_mem_write, s);
    // Slavio control
    cpu_register_physical_memory(base + MISC_CFG, MISC_SIZE,
                                 slavio_misc_io_memory);
    // AUX 1
    cpu_register_physical_memory(base + MISC_AUX1, MISC_SIZE,
                                 slavio_misc_io_memory);
    // AUX 2
    cpu_register_physical_memory(base + MISC_AUX2, MISC_SIZE,
                                 slavio_misc_io_memory);
    // Diagnostics
    cpu_register_physical_memory(base + MISC_DIAG, MISC_SIZE,
                                 slavio_misc_io_memory);
    // Modem control
    cpu_register_physical_memory(base + MISC_MDM, MISC_SIZE,
                                 slavio_misc_io_memory);
    // Power management
    cpu_register_physical_memory(power_base, MISC_SIZE, slavio_misc_io_memory);
    s->power_base = power_base;

    /* 16 bit registers */
    slavio_misc_io_memory = cpu_register_io_memory(0, slavio_led_mem_read,
                                                   slavio_led_mem_write, s);
    /* ss600mp diag LEDs */
    cpu_register_physical_memory(base + MISC_LEDS, MISC_SIZE,
                                 slavio_misc_io_memory);

    /* 32 bit registers */
    slavio_misc_io_memory = cpu_register_io_memory(0, slavio_sysctrl_mem_read,
                                                   slavio_sysctrl_mem_write,
                                                   s);
    // System control
    cpu_register_physical_memory(base + MISC_SYS, SYSCTRL_SIZE,
                                 slavio_misc_io_memory);

    s->irq = irq;

    register_savevm("slavio_misc", base, 1, slavio_misc_save, slavio_misc_load,
                    s);
    qemu_register_reset(slavio_misc_reset, s);
    slavio_misc_reset(s);
    return s;
}
