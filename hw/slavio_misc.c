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
#include "vl.h"
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
    int irq;
    uint8_t config;
    uint8_t aux1, aux2;
    uint8_t diag, mctrl, sysctrl;
} MiscState;

#define MISC_MAXADDR 1

static void slavio_misc_update_irq(void *opaque)
{
    MiscState *s = opaque;

    if ((s->aux2 & 0x4) && (s->config & 0x8)) {
        pic_set_irq(s->irq, 1);
    } else {
        pic_set_irq(s->irq, 0);
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
    if (power_failing && (s->config & 0x8)) {
	s->aux2 |= 0x4;
    } else {
	s->aux2 &= ~0x4;
    }
    slavio_misc_update_irq(s);
}

static void slavio_misc_mem_writeb(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    MiscState *s = opaque;

    switch (addr & 0xfff0000) {
    case 0x1800000:
	MISC_DPRINTF("Write config %2.2x\n", val & 0xff);
	s->config = val & 0xff;
	slavio_misc_update_irq(s);
	break;
    case 0x1900000:
	MISC_DPRINTF("Write aux1 %2.2x\n", val & 0xff);
	s->aux1 = val & 0xff;
	break;
    case 0x1910000:
	val &= 0x3;
	MISC_DPRINTF("Write aux2 %2.2x\n", val);
	val |= s->aux2 & 0x4;
	if (val & 0x2) // Clear Power Fail int
	    val &= 0x1;
	s->aux2 = val;
	if (val & 1)
	    qemu_system_shutdown_request();
	slavio_misc_update_irq(s);
	break;
    case 0x1a00000:
	MISC_DPRINTF("Write diag %2.2x\n", val & 0xff);
	s->diag = val & 0xff;
	break;
    case 0x1b00000:
	MISC_DPRINTF("Write modem control %2.2x\n", val & 0xff);
	s->mctrl = val & 0xff;
	break;
    case 0x1f00000:
	MISC_DPRINTF("Write system control %2.2x\n", val & 0xff);
	if (val & 1) {
	    s->sysctrl = 0x2;
	    qemu_system_reset_request();
	}
	break;
    case 0xa000000:
	MISC_DPRINTF("Write power management %2.2x\n", val & 0xff);
        cpu_interrupt(cpu_single_env, CPU_INTERRUPT_HALT);
	break;
    }
}

static uint32_t slavio_misc_mem_readb(void *opaque, target_phys_addr_t addr)
{
    MiscState *s = opaque;
    uint32_t ret = 0;

    switch (addr & 0xfff0000) {
    case 0x1800000:
	ret = s->config;
	MISC_DPRINTF("Read config %2.2x\n", ret);
	break;
    case 0x1900000:
	ret = s->aux1;
	MISC_DPRINTF("Read aux1 %2.2x\n", ret);
	break;
    case 0x1910000:
	ret = s->aux2;
	MISC_DPRINTF("Read aux2 %2.2x\n", ret);
	break;
    case 0x1a00000:
	ret = s->diag;
	MISC_DPRINTF("Read diag %2.2x\n", ret);
	break;
    case 0x1b00000:
	ret = s->mctrl;
	MISC_DPRINTF("Read modem control %2.2x\n", ret);
	break;
    case 0x1f00000:
	MISC_DPRINTF("Read system control %2.2x\n", ret);
	ret = s->sysctrl;
	break;
    case 0xa000000:
	MISC_DPRINTF("Read power management %2.2x\n", ret);
	break;
    }
    return ret;
}

static CPUReadMemoryFunc *slavio_misc_mem_read[3] = {
    slavio_misc_mem_readb,
    slavio_misc_mem_readb,
    slavio_misc_mem_readb,
};

static CPUWriteMemoryFunc *slavio_misc_mem_write[3] = {
    slavio_misc_mem_writeb,
    slavio_misc_mem_writeb,
    slavio_misc_mem_writeb,
};

static void slavio_misc_save(QEMUFile *f, void *opaque)
{
    MiscState *s = opaque;

    qemu_put_be32s(f, &s->irq);
    qemu_put_8s(f, &s->config);
    qemu_put_8s(f, &s->aux1);
    qemu_put_8s(f, &s->aux2);
    qemu_put_8s(f, &s->diag);
    qemu_put_8s(f, &s->mctrl);
    qemu_put_8s(f, &s->sysctrl);
}

static int slavio_misc_load(QEMUFile *f, void *opaque, int version_id)
{
    MiscState *s = opaque;

    if (version_id != 1)
        return -EINVAL;

    qemu_get_be32s(f, &s->irq);
    qemu_get_8s(f, &s->config);
    qemu_get_8s(f, &s->aux1);
    qemu_get_8s(f, &s->aux2);
    qemu_get_8s(f, &s->diag);
    qemu_get_8s(f, &s->mctrl);
    qemu_get_8s(f, &s->sysctrl);
    return 0;
}

void *slavio_misc_init(uint32_t base, int irq)
{
    int slavio_misc_io_memory;
    MiscState *s;

    s = qemu_mallocz(sizeof(MiscState));
    if (!s)
        return NULL;

    slavio_misc_io_memory = cpu_register_io_memory(0, slavio_misc_mem_read, slavio_misc_mem_write, s);
    // Slavio control
    cpu_register_physical_memory(base + 0x1800000, MISC_MAXADDR, slavio_misc_io_memory);
    // AUX 1
    cpu_register_physical_memory(base + 0x1900000, MISC_MAXADDR, slavio_misc_io_memory);
    // AUX 2
    cpu_register_physical_memory(base + 0x1910000, MISC_MAXADDR, slavio_misc_io_memory);
    // Diagnostics
    cpu_register_physical_memory(base + 0x1a00000, MISC_MAXADDR, slavio_misc_io_memory);
    // Modem control
    cpu_register_physical_memory(base + 0x1b00000, MISC_MAXADDR, slavio_misc_io_memory);
    // System control
    cpu_register_physical_memory(base + 0x1f00000, MISC_MAXADDR, slavio_misc_io_memory);
    // Power management
    cpu_register_physical_memory(base + 0xa000000, MISC_MAXADDR, slavio_misc_io_memory);

    s->irq = irq;

    register_savevm("slavio_misc", base, 1, slavio_misc_save, slavio_misc_load, s);
    qemu_register_reset(slavio_misc_reset, s);
    slavio_misc_reset(s);
    return s;
}
