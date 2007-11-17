/*
 * QEMU Crystal CS4231 audio chip emulation
 *
 * Copyright (c) 2006 Fabrice Bellard
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

/* debug CS4231 */
//#define DEBUG_CS

/*
 * In addition to Crystal CS4231 there is a DMA controller on Sparc.
 */
#define CS_MAXADDR 0x3f
#define CS_SIZE (CS_MAXADDR + 1)
#define CS_REGS 16
#define CS_DREGS 32
#define CS_MAXDREG (CS_DREGS - 1)

typedef struct CSState {
    uint32_t regs[CS_REGS];
    uint8_t dregs[CS_DREGS];
    void *intctl;
} CSState;

#define CS_RAP(s) ((s)->regs[0] & CS_MAXDREG)
#define CS_VER 0xa0
#define CS_CDC_VER 0x8a

#ifdef DEBUG_CS
#define DPRINTF(fmt, args...)                           \
    do { printf("CS: " fmt , ##args); } while (0)
#else
#define DPRINTF(fmt, args...)
#endif

static void cs_reset(void *opaque)
{
    CSState *s = opaque;

    memset(s->regs, 0, CS_REGS * 4);
    memset(s->dregs, 0, CS_DREGS);
    s->dregs[12] = CS_CDC_VER;
    s->dregs[25] = CS_VER;
}

static uint32_t cs_mem_readl(void *opaque, target_phys_addr_t addr)
{
    CSState *s = opaque;
    uint32_t saddr, ret;

    saddr = (addr & CS_MAXADDR) >> 2;
    switch (saddr) {
    case 1:
        switch (CS_RAP(s)) {
        case 3: // Write only
            ret = 0;
            break;
        default:
            ret = s->dregs[CS_RAP(s)];
            break;
        }
        DPRINTF("read dreg[%d]: 0x%8.8x\n", CS_RAP(s), ret);
        break;
    default:
        ret = s->regs[saddr];
        DPRINTF("read reg[%d]: 0x%8.8x\n", saddr, ret);
        break;
    }
    return ret;
}

static void cs_mem_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    CSState *s = opaque;
    uint32_t saddr;

    saddr = (addr & CS_MAXADDR) >> 2;
    DPRINTF("write reg[%d]: 0x%8.8x -> 0x%8.8x\n", saddr, s->regs[saddr], val);
    switch (saddr) {
    case 1:
        DPRINTF("write dreg[%d]: 0x%2.2x -> 0x%2.2x\n", CS_RAP(s), s->dregs[CS_RAP(s)], val);
        switch(CS_RAP(s)) {
        case 11:
        case 25: // Read only
            break;
        case 12:
            val &= 0x40;
            val |= CS_CDC_VER; // Codec version
            s->dregs[CS_RAP(s)] = val;
            break;
        default:
            s->dregs[CS_RAP(s)] = val;
            break;
        }
        break;
    case 2: // Read only
        break;
    case 4:
        if (val & 1)
            cs_reset(s);
        val &= 0x7f;
        s->regs[saddr] = val;
        break;
    default:
        s->regs[saddr] = val;
        break;
    }
}

static CPUReadMemoryFunc *cs_mem_read[3] = {
    cs_mem_readl,
    cs_mem_readl,
    cs_mem_readl,
};

static CPUWriteMemoryFunc *cs_mem_write[3] = {
    cs_mem_writel,
    cs_mem_writel,
    cs_mem_writel,
};

static void cs_save(QEMUFile *f, void *opaque)
{
    CSState *s = opaque;
    unsigned int i;

    for (i = 0; i < CS_REGS; i++)
        qemu_put_be32s(f, &s->regs[i]);

    qemu_put_buffer(f, s->dregs, CS_DREGS);
}

static int cs_load(QEMUFile *f, void *opaque, int version_id)
{
    CSState *s = opaque;
    unsigned int i;

    if (version_id > 1)
        return -EINVAL;

    for (i = 0; i < CS_REGS; i++)
        qemu_get_be32s(f, &s->regs[i]);

    qemu_get_buffer(f, s->dregs, CS_DREGS);
    return 0;
}

void cs_init(target_phys_addr_t base, int irq, void *intctl)
{
    int cs_io_memory;
    CSState *s;

    s = qemu_mallocz(sizeof(CSState));
    if (!s)
        return;

    cs_io_memory = cpu_register_io_memory(0, cs_mem_read, cs_mem_write, s);
    cpu_register_physical_memory(base, CS_SIZE, cs_io_memory);
    register_savevm("cs4231", base, 1, cs_save, cs_load, s);
    qemu_register_reset(cs_reset, s);
    cs_reset(s);
}
