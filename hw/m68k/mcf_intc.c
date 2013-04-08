/*
 * ColdFire Interrupt Controller emulation.
 *
 * Copyright (c) 2007 CodeSourcery.
 *
 * This code is licensed under the GPL
 */
#include "hw/hw.h"
#include "hw/m68k/mcf.h"
#include "exec/address-spaces.h"

typedef struct {
    MemoryRegion iomem;
    uint64_t ipr;
    uint64_t imr;
    uint64_t ifr;
    uint64_t enabled;
    uint8_t icr[64];
    M68kCPU *cpu;
    int active_vector;
} mcf_intc_state;

static void mcf_intc_update(mcf_intc_state *s)
{
    uint64_t active;
    int i;
    int best;
    int best_level;

    active = (s->ipr | s->ifr) & s->enabled & ~s->imr;
    best_level = 0;
    best = 64;
    if (active) {
        for (i = 0; i < 64; i++) {
            if ((active & 1) != 0 && s->icr[i] >= best_level) {
                best_level = s->icr[i];
                best = i;
            }
            active >>= 1;
        }
    }
    s->active_vector = ((best == 64) ? 24 : (best + 64));
    m68k_set_irq_level(s->cpu, best_level, s->active_vector);
}

static uint64_t mcf_intc_read(void *opaque, hwaddr addr,
                              unsigned size)
{
    int offset;
    mcf_intc_state *s = (mcf_intc_state *)opaque;
    offset = addr & 0xff;
    if (offset >= 0x40 && offset < 0x80) {
        return s->icr[offset - 0x40];
    }
    switch (offset) {
    case 0x00:
        return (uint32_t)(s->ipr >> 32);
    case 0x04:
        return (uint32_t)s->ipr;
    case 0x08:
        return (uint32_t)(s->imr >> 32);
    case 0x0c:
        return (uint32_t)s->imr;
    case 0x10:
        return (uint32_t)(s->ifr >> 32);
    case 0x14:
        return (uint32_t)s->ifr;
    case 0xe0: /* SWIACK.  */
        return s->active_vector;
    case 0xe1: case 0xe2: case 0xe3: case 0xe4:
    case 0xe5: case 0xe6: case 0xe7:
        /* LnIACK */
        hw_error("mcf_intc_read: LnIACK not implemented\n");
    default:
        return 0;
    }
}

static void mcf_intc_write(void *opaque, hwaddr addr,
                           uint64_t val, unsigned size)
{
    int offset;
    mcf_intc_state *s = (mcf_intc_state *)opaque;
    offset = addr & 0xff;
    if (offset >= 0x40 && offset < 0x80) {
        int n = offset - 0x40;
        s->icr[n] = val;
        if (val == 0)
            s->enabled &= ~(1ull << n);
        else
            s->enabled |= (1ull << n);
        mcf_intc_update(s);
        return;
    }
    switch (offset) {
    case 0x00: case 0x04:
        /* Ignore IPR writes.  */
        return;
    case 0x08:
        s->imr = (s->imr & 0xffffffff) | ((uint64_t)val << 32);
        break;
    case 0x0c:
        s->imr = (s->imr & 0xffffffff00000000ull) | (uint32_t)val;
        break;
    default:
        hw_error("mcf_intc_write: Bad write offset %d\n", offset);
        break;
    }
    mcf_intc_update(s);
}

static void mcf_intc_set_irq(void *opaque, int irq, int level)
{
    mcf_intc_state *s = (mcf_intc_state *)opaque;
    if (irq >= 64)
        return;
    if (level)
        s->ipr |= 1ull << irq;
    else
        s->ipr &= ~(1ull << irq);
    mcf_intc_update(s);
}

static void mcf_intc_reset(mcf_intc_state *s)
{
    s->imr = ~0ull;
    s->ipr = 0;
    s->ifr = 0;
    s->enabled = 0;
    memset(s->icr, 0, 64);
    s->active_vector = 24;
}

static const MemoryRegionOps mcf_intc_ops = {
    .read = mcf_intc_read,
    .write = mcf_intc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

qemu_irq *mcf_intc_init(MemoryRegion *sysmem,
                        hwaddr base,
                        M68kCPU *cpu)
{
    mcf_intc_state *s;

    s = g_malloc0(sizeof(mcf_intc_state));
    s->cpu = cpu;
    mcf_intc_reset(s);

    memory_region_init_io(&s->iomem, &mcf_intc_ops, s, "mcf", 0x100);
    memory_region_add_subregion(sysmem, base, &s->iomem);

    return qemu_allocate_irqs(mcf_intc_set_irq, s, 64);
}
