/*
 * TI OMAP processors emulation.
 *
 * Copyright (C) 2006-2007 Andrzej Zaborowski  <balrog@zabor.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */
#include "vl.h"
#include "arm_pic.h"

/* Should signal the TCMI */
uint32_t omap_badwidth_read8(void *opaque, target_phys_addr_t addr)
{
    uint8_t ret;

    OMAP_8B_REG(addr);
    cpu_physical_memory_read(addr, (void *) &ret, 1);
    return ret;
}

void omap_badwidth_write8(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    uint8_t val8 = value;

    OMAP_8B_REG(addr);
    cpu_physical_memory_write(addr, (void *) &val8, 1);
}

uint32_t omap_badwidth_read16(void *opaque, target_phys_addr_t addr)
{
    uint16_t ret;

    OMAP_16B_REG(addr);
    cpu_physical_memory_read(addr, (void *) &ret, 2);
    return ret;
}

void omap_badwidth_write16(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    uint16_t val16 = value;

    OMAP_16B_REG(addr);
    cpu_physical_memory_write(addr, (void *) &val16, 2);
}

uint32_t omap_badwidth_read32(void *opaque, target_phys_addr_t addr)
{
    uint32_t ret;

    OMAP_32B_REG(addr);
    cpu_physical_memory_read(addr, (void *) &ret, 4);
    return ret;
}

void omap_badwidth_write32(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    OMAP_32B_REG(addr);
    cpu_physical_memory_write(addr, (void *) &value, 4);
}

/* Interrupt Handlers */
struct omap_intr_handler_s {
    qemu_irq *pins;
    qemu_irq *parent_pic;
    target_phys_addr_t base;

    /* state */
    uint32_t irqs;
    uint32_t mask;
    uint32_t sens_edge;
    uint32_t fiq;
    int priority[32];
    uint32_t new_irq_agr;
    uint32_t new_fiq_agr;
    int sir_irq;
    int sir_fiq;
    int stats[32];
};

static void omap_inth_update(struct omap_intr_handler_s *s)
{
    uint32_t irq = s->irqs & ~s->mask & ~s->fiq;
    uint32_t fiq = s->irqs & ~s->mask & s->fiq;

    if (s->new_irq_agr || !irq) {
       qemu_set_irq(s->parent_pic[ARM_PIC_CPU_IRQ], irq);
       if (irq)
           s->new_irq_agr = 0;
    }

    if (s->new_fiq_agr || !irq) {
        qemu_set_irq(s->parent_pic[ARM_PIC_CPU_FIQ], fiq);
        if (fiq)
            s->new_fiq_agr = 0;
    }
}

static void omap_inth_sir_update(struct omap_intr_handler_s *s)
{
    int i, intr_irq, intr_fiq, p_irq, p_fiq, p, f;
    uint32_t level = s->irqs & ~s->mask;

    intr_irq = 0;
    intr_fiq = 0;
    p_irq = -1;
    p_fiq = -1;
    /* Find the interrupt line with the highest dynamic priority */
    for (f = ffs(level), i = f - 1, level >>= f - 1; f; i += f, level >>= f) {
        p = s->priority[i];
        if (s->fiq & (1 << i)) {
            if (p > p_fiq) {
                p_fiq = p;
                intr_fiq = i;
            }
        } else {
            if (p > p_irq) {
                p_irq = p;
                intr_irq = i;
            }
        }

        f = ffs(level >> 1);
    }

    s->sir_irq = intr_irq;
    s->sir_fiq = intr_fiq;
}

#define INT_FALLING_EDGE	0
#define INT_LOW_LEVEL		1

static void omap_set_intr(void *opaque, int irq, int req)
{
    struct omap_intr_handler_s *ih = (struct omap_intr_handler_s *) opaque;
    uint32_t rise;

    if (req) {
        rise = ~ih->irqs & (1 << irq);
        ih->irqs |= rise;
        ih->stats[irq] += !!rise;
    } else {
        rise = ih->sens_edge & ih->irqs & (1 << irq);
        ih->irqs &= ~rise;
    }

    if (rise & ~ih->mask) {
        omap_inth_sir_update(ih);

        omap_inth_update(ih);
    }
}

static uint32_t omap_inth_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_intr_handler_s *s = (struct omap_intr_handler_s *) opaque;
    int i, offset = addr - s->base;

    switch (offset) {
    case 0x00:	/* ITR */
        return s->irqs;

    case 0x04:	/* MIR */
        return s->mask;

    case 0x10:	/* SIR_IRQ_CODE */
        i = s->sir_irq;
        if (((s->sens_edge >> i) & 1) == INT_FALLING_EDGE && i) {
            s->irqs &= ~(1 << i);
            omap_inth_sir_update(s);
            omap_inth_update(s);
        }
        return i;

    case 0x14:	/* SIR_FIQ_CODE */
        i = s->sir_fiq;
        if (((s->sens_edge >> i) & 1) == INT_FALLING_EDGE && i) {
            s->irqs &= ~(1 << i);
            omap_inth_sir_update(s);
            omap_inth_update(s);
        }
        return i;

    case 0x18:	/* CONTROL_REG */
        return 0;

    case 0x1c:	/* ILR0 */
    case 0x20:	/* ILR1 */
    case 0x24:	/* ILR2 */
    case 0x28:	/* ILR3 */
    case 0x2c:	/* ILR4 */
    case 0x30:	/* ILR5 */
    case 0x34:	/* ILR6 */
    case 0x38:	/* ILR7 */
    case 0x3c:	/* ILR8 */
    case 0x40:	/* ILR9 */
    case 0x44:	/* ILR10 */
    case 0x48:	/* ILR11 */
    case 0x4c:	/* ILR12 */
    case 0x50:	/* ILR13 */
    case 0x54:	/* ILR14 */
    case 0x58:	/* ILR15 */
    case 0x5c:	/* ILR16 */
    case 0x60:	/* ILR17 */
    case 0x64:	/* ILR18 */
    case 0x68:	/* ILR19 */
    case 0x6c:	/* ILR20 */
    case 0x70:	/* ILR21 */
    case 0x74:	/* ILR22 */
    case 0x78:	/* ILR23 */
    case 0x7c:	/* ILR24 */
    case 0x80:	/* ILR25 */
    case 0x84:	/* ILR26 */
    case 0x88:	/* ILR27 */
    case 0x8c:	/* ILR28 */
    case 0x90:	/* ILR29 */
    case 0x94:	/* ILR30 */
    case 0x98:	/* ILR31 */
        i = (offset - 0x1c) >> 2;
        return (s->priority[i] << 2) |
                (((s->sens_edge >> i) & 1) << 1) |
                ((s->fiq >> i) & 1);

    case 0x9c:	/* ISR */
        return 0x00000000;

    default:
        OMAP_BAD_REG(addr);
        break;
    }
    return 0;
}

static void omap_inth_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_intr_handler_s *s = (struct omap_intr_handler_s *) opaque;
    int i, offset = addr - s->base;

    switch (offset) {
    case 0x00:	/* ITR */
        s->irqs &= value | 1;
        omap_inth_sir_update(s);
        omap_inth_update(s);
        return;

    case 0x04:	/* MIR */
        s->mask = value;
        omap_inth_sir_update(s);
        omap_inth_update(s);
        return;

    case 0x10:	/* SIR_IRQ_CODE */
    case 0x14:	/* SIR_FIQ_CODE */
        OMAP_RO_REG(addr);
        break;

    case 0x18:	/* CONTROL_REG */
        if (value & 2)
            s->new_fiq_agr = ~0;
        if (value & 1)
            s->new_irq_agr = ~0;
        omap_inth_update(s);
        return;

    case 0x1c:	/* ILR0 */
    case 0x20:	/* ILR1 */
    case 0x24:	/* ILR2 */
    case 0x28:	/* ILR3 */
    case 0x2c:	/* ILR4 */
    case 0x30:	/* ILR5 */
    case 0x34:	/* ILR6 */
    case 0x38:	/* ILR7 */
    case 0x3c:	/* ILR8 */
    case 0x40:	/* ILR9 */
    case 0x44:	/* ILR10 */
    case 0x48:	/* ILR11 */
    case 0x4c:	/* ILR12 */
    case 0x50:	/* ILR13 */
    case 0x54:	/* ILR14 */
    case 0x58:	/* ILR15 */
    case 0x5c:	/* ILR16 */
    case 0x60:	/* ILR17 */
    case 0x64:	/* ILR18 */
    case 0x68:	/* ILR19 */
    case 0x6c:	/* ILR20 */
    case 0x70:	/* ILR21 */
    case 0x74:	/* ILR22 */
    case 0x78:	/* ILR23 */
    case 0x7c:	/* ILR24 */
    case 0x80:	/* ILR25 */
    case 0x84:	/* ILR26 */
    case 0x88:	/* ILR27 */
    case 0x8c:	/* ILR28 */
    case 0x90:	/* ILR29 */
    case 0x94:	/* ILR30 */
    case 0x98:	/* ILR31 */
        i = (offset - 0x1c) >> 2;
        s->priority[i] = (value >> 2) & 0x1f;
        s->sens_edge &= ~(1 << i);
        s->sens_edge |= ((value >> 1) & 1) << i;
        s->fiq &= ~(1 << i);
        s->fiq |= (value & 1) << i;
        return;

    case 0x9c:	/* ISR */
        for (i = 0; i < 32; i ++)
            if (value & (1 << i)) {
                omap_set_intr(s, i, 1);
                return;
            }
        return;

    default:
        OMAP_BAD_REG(addr);
    }
}

static CPUReadMemoryFunc *omap_inth_readfn[] = {
    omap_badwidth_read32,
    omap_badwidth_read32,
    omap_inth_read,
};

static CPUWriteMemoryFunc *omap_inth_writefn[] = {
    omap_inth_write,
    omap_inth_write,
    omap_inth_write,
};

static void omap_inth_reset(struct omap_intr_handler_s *s)
{
    s->irqs = 0x00000000;
    s->mask = 0xffffffff;
    s->sens_edge = 0x00000000;
    s->fiq = 0x00000000;
    memset(s->priority, 0, sizeof(s->priority));
    s->new_irq_agr = ~0;
    s->new_fiq_agr = ~0;
    s->sir_irq = 0;
    s->sir_fiq = 0;

    omap_inth_update(s);
}

struct omap_intr_handler_s *omap_inth_init(target_phys_addr_t base,
                unsigned long size, qemu_irq parent[2], omap_clk clk)
{
    int iomemtype;
    struct omap_intr_handler_s *s = (struct omap_intr_handler_s *)
            qemu_mallocz(sizeof(struct omap_intr_handler_s));

    s->parent_pic = parent;
    s->base = base;
    s->pins = qemu_allocate_irqs(omap_set_intr, s, 32);
    omap_inth_reset(s);

    iomemtype = cpu_register_io_memory(0, omap_inth_readfn,
                    omap_inth_writefn, s);
    cpu_register_physical_memory(s->base, size, iomemtype);

    return s;
}

/* OMAP1 DMA module */
typedef enum {
    constant = 0,
    post_incremented,
    single_index,
    double_index,
} omap_dma_addressing_t;

struct omap_dma_channel_s {
    int burst[2];
    int pack[2];
    enum omap_dma_port port[2];
    target_phys_addr_t addr[2];
    omap_dma_addressing_t mode[2];
    int data_type;
    int end_prog;
    int repeat;
    int auto_init;
    int priority;
    int fs;
    int sync;
    int running;
    int interrupts;
    int status;
    int signalled;
    int post_sync;
    int transfer;
    uint16_t elements;
    uint16_t frames;
    uint16_t frame_index;
    uint16_t element_index;
    uint16_t cpc;

    struct omap_dma_reg_set_s {
        target_phys_addr_t src, dest;
        int frame;
        int element;
        int frame_delta[2];
        int elem_delta[2];
        int frames;
        int elements;
    } active_set;
};

struct omap_dma_s {
    qemu_irq *ih;
    QEMUTimer *tm;
    struct omap_mpu_state_s *mpu;
    target_phys_addr_t base;
    omap_clk clk;
    int64_t delay;
    uint32_t drq;

    uint16_t gcr;
    int run_count;

    int chans;
    struct omap_dma_channel_s ch[16];
    struct omap_dma_lcd_channel_s lcd_ch;
};

static void omap_dma_interrupts_update(struct omap_dma_s *s)
{
    /* First three interrupts are shared between two channels each.  */
    qemu_set_irq(s->ih[OMAP_INT_DMA_CH0_6],
                    (s->ch[0].status | s->ch[6].status) & 0x3f);
    qemu_set_irq(s->ih[OMAP_INT_DMA_CH1_7],
                    (s->ch[1].status | s->ch[7].status) & 0x3f);
    qemu_set_irq(s->ih[OMAP_INT_DMA_CH2_8],
                    (s->ch[2].status | s->ch[8].status) & 0x3f);
    qemu_set_irq(s->ih[OMAP_INT_DMA_CH3],
                    (s->ch[3].status) & 0x3f);
    qemu_set_irq(s->ih[OMAP_INT_DMA_CH4],
                    (s->ch[4].status) & 0x3f);
    qemu_set_irq(s->ih[OMAP_INT_DMA_CH5],
                    (s->ch[5].status) & 0x3f);
}

static void omap_dma_channel_load(struct omap_dma_s *s, int ch)
{
    struct omap_dma_reg_set_s *a = &s->ch[ch].active_set;
    int i;

    /*
     * TODO: verify address ranges and alignment
     * TODO: port endianness
     */

    a->src = s->ch[ch].addr[0];
    a->dest = s->ch[ch].addr[1];
    a->frames = s->ch[ch].frames;
    a->elements = s->ch[ch].elements;
    a->frame = 0;
    a->element = 0;

    if (unlikely(!s->ch[ch].elements || !s->ch[ch].frames)) {
        printf("%s: bad DMA request\n", __FUNCTION__);
        return;
    }

    for (i = 0; i < 2; i ++)
        switch (s->ch[ch].mode[i]) {
        case constant:
            a->elem_delta[i] = 0;
            a->frame_delta[i] = 0;
            break;
        case post_incremented:
            a->elem_delta[i] = s->ch[ch].data_type;
            a->frame_delta[i] = 0;
            break;
        case single_index:
            a->elem_delta[i] = s->ch[ch].data_type +
                s->ch[ch].element_index - 1;
            if (s->ch[ch].element_index > 0x7fff)
                a->elem_delta[i] -= 0x10000;
            a->frame_delta[i] = 0;
            break;
        case double_index:
            a->elem_delta[i] = s->ch[ch].data_type +
                s->ch[ch].element_index - 1;
            if (s->ch[ch].element_index > 0x7fff)
                a->elem_delta[i] -= 0x10000;
            a->frame_delta[i] = s->ch[ch].frame_index -
                s->ch[ch].element_index;
            if (s->ch[ch].frame_index > 0x7fff)
                a->frame_delta[i] -= 0x10000;
            break;
        default:
            break;
        }
}

static inline void omap_dma_request_run(struct omap_dma_s *s,
                int channel, int request)
{
next_channel:
    if (request > 0)
        for (; channel < 9; channel ++)
            if (s->ch[channel].sync == request && s->ch[channel].running)
                break;
    if (channel >= 9)
        return;

    if (s->ch[channel].transfer) {
        if (request > 0) {
            s->ch[channel ++].post_sync = request;
            goto next_channel;
        }
        s->ch[channel].status |= 0x02;	/* Synchronisation drop */
        omap_dma_interrupts_update(s);
        return;
    }

    if (!s->ch[channel].signalled)
        s->run_count ++;
    s->ch[channel].signalled = 1;

    if (request > 0)
        s->ch[channel].status |= 0x40;	/* External request */

    if (s->delay && !qemu_timer_pending(s->tm))
        qemu_mod_timer(s->tm, qemu_get_clock(vm_clock) + s->delay);

    if (request > 0) {
        channel ++;
        goto next_channel;
    }
}

static inline void omap_dma_request_stop(struct omap_dma_s *s, int channel)
{
    if (s->ch[channel].signalled)
        s->run_count --;
    s->ch[channel].signalled = 0;

    if (!s->run_count)
        qemu_del_timer(s->tm);
}

static void omap_dma_channel_run(struct omap_dma_s *s)
{
    int ch;
    uint16_t status;
    uint8_t value[4];
    struct omap_dma_port_if_s *src_p, *dest_p;
    struct omap_dma_reg_set_s *a;

    for (ch = 0; ch < 9; ch ++) {
        a = &s->ch[ch].active_set;

        src_p = &s->mpu->port[s->ch[ch].port[0]];
        dest_p = &s->mpu->port[s->ch[ch].port[1]];
        if (s->ch[ch].signalled && (!src_p->addr_valid(s->mpu, a->src) ||
                    !dest_p->addr_valid(s->mpu, a->dest))) {
#if 0
            /* Bus time-out */
            if (s->ch[ch].interrupts & 0x01)
                s->ch[ch].status |= 0x01;
            omap_dma_request_stop(s, ch);
            continue;
#endif
            printf("%s: Bus time-out in DMA%i operation\n", __FUNCTION__, ch);
        }

        status = s->ch[ch].status;
        while (status == s->ch[ch].status && s->ch[ch].signalled) {
            /* Transfer a single element */
            s->ch[ch].transfer = 1;
            cpu_physical_memory_read(a->src, value, s->ch[ch].data_type);
            cpu_physical_memory_write(a->dest, value, s->ch[ch].data_type);
            s->ch[ch].transfer = 0;

            a->src += a->elem_delta[0];
            a->dest += a->elem_delta[1];
            a->element ++;

            /* Check interrupt conditions */
            if (a->element == a->elements) {
                a->element = 0;
                a->src += a->frame_delta[0];
                a->dest += a->frame_delta[1];
                a->frame ++;

                if (a->frame == a->frames) {
                    if (!s->ch[ch].repeat || !s->ch[ch].auto_init)
                        s->ch[ch].running = 0;

                    if (s->ch[ch].auto_init &&
                            (s->ch[ch].repeat ||
                             s->ch[ch].end_prog))
                        omap_dma_channel_load(s, ch);

                    if (s->ch[ch].interrupts & 0x20)
                        s->ch[ch].status |= 0x20;

                    if (!s->ch[ch].sync)
                        omap_dma_request_stop(s, ch);
                }

                if (s->ch[ch].interrupts & 0x08)
                    s->ch[ch].status |= 0x08;

                if (s->ch[ch].sync && s->ch[ch].fs &&
                                !(s->drq & (1 << s->ch[ch].sync))) {
                    s->ch[ch].status &= ~0x40;
                    omap_dma_request_stop(s, ch);
                }
            }

            if (a->element == 1 && a->frame == a->frames - 1)
                if (s->ch[ch].interrupts & 0x10)
                    s->ch[ch].status |= 0x10;

            if (a->element == (a->elements >> 1))
                if (s->ch[ch].interrupts & 0x04)
                    s->ch[ch].status |= 0x04;

            if (s->ch[ch].sync && !s->ch[ch].fs &&
                            !(s->drq & (1 << s->ch[ch].sync))) {
                s->ch[ch].status &= ~0x40;
                omap_dma_request_stop(s, ch);
            }

            /*
             * Process requests made while the element was
             * being transferred.
             */
            if (s->ch[ch].post_sync) {
                omap_dma_request_run(s, 0, s->ch[ch].post_sync);
                s->ch[ch].post_sync = 0;
            }

#if 0
            break;
#endif
        }

        s->ch[ch].cpc = a->dest & 0x0000ffff;
    }

    omap_dma_interrupts_update(s);
    if (s->run_count && s->delay)
        qemu_mod_timer(s->tm, qemu_get_clock(vm_clock) + s->delay);
}

static int omap_dma_ch_reg_read(struct omap_dma_s *s,
                int ch, int reg, uint16_t *value) {
    switch (reg) {
    case 0x00:	/* SYS_DMA_CSDP_CH0 */
        *value = (s->ch[ch].burst[1] << 14) |
                (s->ch[ch].pack[1] << 13) |
                (s->ch[ch].port[1] << 9) |
                (s->ch[ch].burst[0] << 7) |
                (s->ch[ch].pack[0] << 6) |
                (s->ch[ch].port[0] << 2) |
                (s->ch[ch].data_type >> 1);
        break;

    case 0x02:	/* SYS_DMA_CCR_CH0 */
        *value = (s->ch[ch].mode[1] << 14) |
                (s->ch[ch].mode[0] << 12) |
                (s->ch[ch].end_prog << 11) |
                (s->ch[ch].repeat << 9) |
                (s->ch[ch].auto_init << 8) |
                (s->ch[ch].running << 7) |
                (s->ch[ch].priority << 6) |
                (s->ch[ch].fs << 5) | s->ch[ch].sync;
        break;

    case 0x04:	/* SYS_DMA_CICR_CH0 */
        *value = s->ch[ch].interrupts;
        break;

    case 0x06:	/* SYS_DMA_CSR_CH0 */
        /* FIXME: shared CSR for channels sharing the interrupts */
        *value = s->ch[ch].status;
        s->ch[ch].status &= 0x40;
        omap_dma_interrupts_update(s);
        break;

    case 0x08:	/* SYS_DMA_CSSA_L_CH0 */
        *value = s->ch[ch].addr[0] & 0x0000ffff;
        break;

    case 0x0a:	/* SYS_DMA_CSSA_U_CH0 */
        *value = s->ch[ch].addr[0] >> 16;
        break;

    case 0x0c:	/* SYS_DMA_CDSA_L_CH0 */
        *value = s->ch[ch].addr[1] & 0x0000ffff;
        break;

    case 0x0e:	/* SYS_DMA_CDSA_U_CH0 */
        *value = s->ch[ch].addr[1] >> 16;
        break;

    case 0x10:	/* SYS_DMA_CEN_CH0 */
        *value = s->ch[ch].elements;
        break;

    case 0x12:	/* SYS_DMA_CFN_CH0 */
        *value = s->ch[ch].frames;
        break;

    case 0x14:	/* SYS_DMA_CFI_CH0 */
        *value = s->ch[ch].frame_index;
        break;

    case 0x16:	/* SYS_DMA_CEI_CH0 */
        *value = s->ch[ch].element_index;
        break;

    case 0x18:	/* SYS_DMA_CPC_CH0 */
        *value = s->ch[ch].cpc;
        break;

    default:
        return 1;
    }
    return 0;
}

static int omap_dma_ch_reg_write(struct omap_dma_s *s,
                int ch, int reg, uint16_t value) {
    switch (reg) {
    case 0x00:	/* SYS_DMA_CSDP_CH0 */
        s->ch[ch].burst[1] = (value & 0xc000) >> 14;
        s->ch[ch].pack[1] = (value & 0x2000) >> 13;
        s->ch[ch].port[1] = (enum omap_dma_port) ((value & 0x1e00) >> 9);
        s->ch[ch].burst[0] = (value & 0x0180) >> 7;
        s->ch[ch].pack[0] = (value & 0x0040) >> 6;
        s->ch[ch].port[0] = (enum omap_dma_port) ((value & 0x003c) >> 2);
        s->ch[ch].data_type = (1 << (value & 3));
        if (s->ch[ch].port[0] >= omap_dma_port_last)
            printf("%s: invalid DMA port %i\n", __FUNCTION__,
                            s->ch[ch].port[0]);
        if (s->ch[ch].port[1] >= omap_dma_port_last)
            printf("%s: invalid DMA port %i\n", __FUNCTION__,
                            s->ch[ch].port[1]);
        if ((value & 3) == 3)
            printf("%s: bad data_type for DMA channel %i\n", __FUNCTION__, ch);
        break;

    case 0x02:	/* SYS_DMA_CCR_CH0 */
        s->ch[ch].mode[1] = (omap_dma_addressing_t) ((value & 0xc000) >> 14);
        s->ch[ch].mode[0] = (omap_dma_addressing_t) ((value & 0x3000) >> 12);
        s->ch[ch].end_prog = (value & 0x0800) >> 11;
        s->ch[ch].repeat = (value & 0x0200) >> 9;
        s->ch[ch].auto_init = (value & 0x0100) >> 8;
        s->ch[ch].priority = (value & 0x0040) >> 6;
        s->ch[ch].fs = (value & 0x0020) >> 5;
        s->ch[ch].sync = value & 0x001f;
        if (value & 0x0080) {
            if (s->ch[ch].running) {
                if (!s->ch[ch].signalled &&
                                s->ch[ch].auto_init && s->ch[ch].end_prog)
                    omap_dma_channel_load(s, ch);
            } else {
                s->ch[ch].running = 1;
                omap_dma_channel_load(s, ch);
            }
            if (!s->ch[ch].sync || (s->drq & (1 << s->ch[ch].sync)))
                omap_dma_request_run(s, ch, 0);
        } else {
            s->ch[ch].running = 0;
            omap_dma_request_stop(s, ch);
        }
        break;

    case 0x04:	/* SYS_DMA_CICR_CH0 */
        s->ch[ch].interrupts = value & 0x003f;
        break;

    case 0x06:	/* SYS_DMA_CSR_CH0 */
        return 1;

    case 0x08:	/* SYS_DMA_CSSA_L_CH0 */
        s->ch[ch].addr[0] &= 0xffff0000;
        s->ch[ch].addr[0] |= value;
        break;

    case 0x0a:	/* SYS_DMA_CSSA_U_CH0 */
        s->ch[ch].addr[0] &= 0x0000ffff;
        s->ch[ch].addr[0] |= (uint32_t) value << 16;
        break;

    case 0x0c:	/* SYS_DMA_CDSA_L_CH0 */
        s->ch[ch].addr[1] &= 0xffff0000;
        s->ch[ch].addr[1] |= value;
        break;

    case 0x0e:	/* SYS_DMA_CDSA_U_CH0 */
        s->ch[ch].addr[1] &= 0x0000ffff;
        s->ch[ch].addr[1] |= (uint32_t) value << 16;
        break;

    case 0x10:	/* SYS_DMA_CEN_CH0 */
        s->ch[ch].elements = value & 0xffff;
        break;

    case 0x12:	/* SYS_DMA_CFN_CH0 */
        s->ch[ch].frames = value & 0xffff;
        break;

    case 0x14:	/* SYS_DMA_CFI_CH0 */
        s->ch[ch].frame_index = value & 0xffff;
        break;

    case 0x16:	/* SYS_DMA_CEI_CH0 */
        s->ch[ch].element_index = value & 0xffff;
        break;

    case 0x18:	/* SYS_DMA_CPC_CH0 */
        return 1;

    default:
        OMAP_BAD_REG((target_phys_addr_t) reg);
    }
    return 0;
}

static uint32_t omap_dma_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_dma_s *s = (struct omap_dma_s *) opaque;
    int i, reg, ch, offset = addr - s->base;
    uint16_t ret;

    switch (offset) {
    case 0x000 ... 0x2fe:
        reg = offset & 0x3f;
        ch = (offset >> 6) & 0x0f;
        if (omap_dma_ch_reg_read(s, ch, reg, &ret))
            break;
        return ret;

    case 0x300:	/* SYS_DMA_LCD_CTRL */
        i = s->lcd_ch.condition;
        s->lcd_ch.condition = 0;
        qemu_irq_lower(s->lcd_ch.irq);
        return ((s->lcd_ch.src == imif) << 6) | (i << 3) |
                (s->lcd_ch.interrupts << 1) | s->lcd_ch.dual;

    case 0x302:	/* SYS_DMA_LCD_TOP_F1_L */
        return s->lcd_ch.src_f1_top & 0xffff;

    case 0x304:	/* SYS_DMA_LCD_TOP_F1_U */
        return s->lcd_ch.src_f1_top >> 16;

    case 0x306:	/* SYS_DMA_LCD_BOT_F1_L */
        return s->lcd_ch.src_f1_bottom & 0xffff;

    case 0x308:	/* SYS_DMA_LCD_BOT_F1_U */
        return s->lcd_ch.src_f1_bottom >> 16;

    case 0x30a:	/* SYS_DMA_LCD_TOP_F2_L */
        return s->lcd_ch.src_f2_top & 0xffff;

    case 0x30c:	/* SYS_DMA_LCD_TOP_F2_U */
        return s->lcd_ch.src_f2_top >> 16;

    case 0x30e:	/* SYS_DMA_LCD_BOT_F2_L */
        return s->lcd_ch.src_f2_bottom & 0xffff;

    case 0x310:	/* SYS_DMA_LCD_BOT_F2_U */
        return s->lcd_ch.src_f2_bottom >> 16;

    case 0x400:	/* SYS_DMA_GCR */
        return s->gcr;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_dma_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_dma_s *s = (struct omap_dma_s *) opaque;
    int reg, ch, offset = addr - s->base;

    switch (offset) {
    case 0x000 ... 0x2fe:
        reg = offset & 0x3f;
        ch = (offset >> 6) & 0x0f;
        if (omap_dma_ch_reg_write(s, ch, reg, value))
            OMAP_RO_REG(addr);
        break;

    case 0x300:	/* SYS_DMA_LCD_CTRL */
        s->lcd_ch.src = (value & 0x40) ? imif : emiff;
        s->lcd_ch.condition = 0;
        /* Assume no bus errors and thus no BUS_ERROR irq bits.  */
        s->lcd_ch.interrupts = (value >> 1) & 1;
        s->lcd_ch.dual = value & 1;
        break;

    case 0x302:	/* SYS_DMA_LCD_TOP_F1_L */
        s->lcd_ch.src_f1_top &= 0xffff0000;
        s->lcd_ch.src_f1_top |= 0x0000ffff & value;
        break;

    case 0x304:	/* SYS_DMA_LCD_TOP_F1_U */
        s->lcd_ch.src_f1_top &= 0x0000ffff;
        s->lcd_ch.src_f1_top |= value << 16;
        break;

    case 0x306:	/* SYS_DMA_LCD_BOT_F1_L */
        s->lcd_ch.src_f1_bottom &= 0xffff0000;
        s->lcd_ch.src_f1_bottom |= 0x0000ffff & value;
        break;

    case 0x308:	/* SYS_DMA_LCD_BOT_F1_U */
        s->lcd_ch.src_f1_bottom &= 0x0000ffff;
        s->lcd_ch.src_f1_bottom |= value << 16;
        break;

    case 0x30a:	/* SYS_DMA_LCD_TOP_F2_L */
        s->lcd_ch.src_f2_top &= 0xffff0000;
        s->lcd_ch.src_f2_top |= 0x0000ffff & value;
        break;

    case 0x30c:	/* SYS_DMA_LCD_TOP_F2_U */
        s->lcd_ch.src_f2_top &= 0x0000ffff;
        s->lcd_ch.src_f2_top |= value << 16;
        break;

    case 0x30e:	/* SYS_DMA_LCD_BOT_F2_L */
        s->lcd_ch.src_f2_bottom &= 0xffff0000;
        s->lcd_ch.src_f2_bottom |= 0x0000ffff & value;
        break;

    case 0x310:	/* SYS_DMA_LCD_BOT_F2_U */
        s->lcd_ch.src_f2_bottom &= 0x0000ffff;
        s->lcd_ch.src_f2_bottom |= value << 16;
        break;

    case 0x400:	/* SYS_DMA_GCR */
        s->gcr = value & 0x000c;
        break;

    default:
        OMAP_BAD_REG(addr);
    }
}

static CPUReadMemoryFunc *omap_dma_readfn[] = {
    omap_badwidth_read16,
    omap_dma_read,
    omap_badwidth_read16,
};

static CPUWriteMemoryFunc *omap_dma_writefn[] = {
    omap_badwidth_write16,
    omap_dma_write,
    omap_badwidth_write16,
};

static void omap_dma_request(void *opaque, int drq, int req)
{
    struct omap_dma_s *s = (struct omap_dma_s *) opaque;
    /* The request pins are level triggered.  */
    if (req) {
        if (~s->drq & (1 << drq)) {
            s->drq |= 1 << drq;
            omap_dma_request_run(s, 0, drq);
        }
    } else
        s->drq &= ~(1 << drq);
}

static void omap_dma_clk_update(void *opaque, int line, int on)
{
    struct omap_dma_s *s = (struct omap_dma_s *) opaque;

    if (on) {
        s->delay = ticks_per_sec >> 7;
        if (s->run_count)
            qemu_mod_timer(s->tm, qemu_get_clock(vm_clock) + s->delay);
    } else {
        s->delay = 0;
        qemu_del_timer(s->tm);
    }
}

static void omap_dma_reset(struct omap_dma_s *s)
{
    int i;

    qemu_del_timer(s->tm);
    s->gcr = 0x0004;
    s->drq = 0x00000000;
    s->run_count = 0;
    s->lcd_ch.src = emiff;
    s->lcd_ch.condition = 0;
    s->lcd_ch.interrupts = 0;
    s->lcd_ch.dual = 0;
    memset(s->ch, 0, sizeof(s->ch));
    for (i = 0; i < s->chans; i ++)
        s->ch[i].interrupts = 0x0003;
}

struct omap_dma_s *omap_dma_init(target_phys_addr_t base,
                qemu_irq pic[], struct omap_mpu_state_s *mpu, omap_clk clk)
{
    int iomemtype;
    struct omap_dma_s *s = (struct omap_dma_s *)
            qemu_mallocz(sizeof(struct omap_dma_s));

    s->ih = pic;
    s->base = base;
    s->chans = 9;
    s->mpu = mpu;
    s->clk = clk;
    s->lcd_ch.irq = pic[OMAP_INT_DMA_LCD];
    s->lcd_ch.mpu = mpu;
    s->tm = qemu_new_timer(vm_clock, (QEMUTimerCB *) omap_dma_channel_run, s);
    omap_clk_adduser(s->clk, qemu_allocate_irqs(omap_dma_clk_update, s, 1)[0]);
    mpu->drq = qemu_allocate_irqs(omap_dma_request, s, 32);
    omap_dma_reset(s);
    omap_dma_clk_update(s, 0, 1);

    iomemtype = cpu_register_io_memory(0, omap_dma_readfn,
                    omap_dma_writefn, s);
    cpu_register_physical_memory(s->base, 0x800, iomemtype);

    return s;
}

/* DMA ports */
static int omap_validate_emiff_addr(struct omap_mpu_state_s *s,
                target_phys_addr_t addr)
{
    return addr >= OMAP_EMIFF_BASE && addr < OMAP_EMIFF_BASE + s->sdram_size;
}

static int omap_validate_emifs_addr(struct omap_mpu_state_s *s,
                target_phys_addr_t addr)
{
    return addr >= OMAP_EMIFS_BASE && addr < OMAP_EMIFF_BASE;
}

static int omap_validate_imif_addr(struct omap_mpu_state_s *s,
                target_phys_addr_t addr)
{
    return addr >= OMAP_IMIF_BASE && addr < OMAP_IMIF_BASE + s->sram_size;
}

static int omap_validate_tipb_addr(struct omap_mpu_state_s *s,
                target_phys_addr_t addr)
{
    return addr >= 0xfffb0000 && addr < 0xffff0000;
}

static int omap_validate_local_addr(struct omap_mpu_state_s *s,
                target_phys_addr_t addr)
{
    return addr >= OMAP_LOCALBUS_BASE && addr < OMAP_LOCALBUS_BASE + 0x1000000;
}

static int omap_validate_tipb_mpui_addr(struct omap_mpu_state_s *s,
                target_phys_addr_t addr)
{
    return addr >= 0xe1010000 && addr < 0xe1020004;
}

/* MPU OS timers */
struct omap_mpu_timer_s {
    qemu_irq irq;
    omap_clk clk;
    target_phys_addr_t base;
    uint32_t val;
    int64_t time;
    QEMUTimer *timer;
    int64_t rate;
    int it_ena;

    int enable;
    int ptv;
    int ar;
    int st;
    uint32_t reset_val;
};

static inline uint32_t omap_timer_read(struct omap_mpu_timer_s *timer)
{
    uint64_t distance = qemu_get_clock(vm_clock) - timer->time;

    if (timer->st && timer->enable && timer->rate)
        return timer->val - muldiv64(distance >> (timer->ptv + 1),
                        timer->rate, ticks_per_sec);
    else
        return timer->val;
}

static inline void omap_timer_sync(struct omap_mpu_timer_s *timer)
{
    timer->val = omap_timer_read(timer);
    timer->time = qemu_get_clock(vm_clock);
}

static inline void omap_timer_update(struct omap_mpu_timer_s *timer)
{
    int64_t expires;

    if (timer->enable && timer->st && timer->rate) {
        timer->val = timer->reset_val;	/* Should skip this on clk enable */
        expires = muldiv64(timer->val << (timer->ptv + 1),
                        ticks_per_sec, timer->rate);

        /* If timer expiry would be sooner than in about 1 ms and
         * auto-reload isn't set, then fire immediately.  This is a hack
         * to make systems like PalmOS run in acceptable time.  PalmOS
         * sets the interval to a very low value and polls the status bit
         * in a busy loop when it wants to sleep just a couple of CPU
         * ticks.  */
        if (expires > (ticks_per_sec >> 10) || timer->ar)
            qemu_mod_timer(timer->timer, timer->time + expires);
        else {
            timer->val = 0;
            timer->st = 0;
            if (timer->it_ena)
                qemu_irq_raise(timer->irq);
        }
    } else
        qemu_del_timer(timer->timer);
}

static void omap_timer_tick(void *opaque)
{
    struct omap_mpu_timer_s *timer = (struct omap_mpu_timer_s *) opaque;
    omap_timer_sync(timer);

    if (!timer->ar) {
        timer->val = 0;
        timer->st = 0;
    }

    if (timer->it_ena)
        qemu_irq_raise(timer->irq);
    omap_timer_update(timer);
}

static void omap_timer_clk_update(void *opaque, int line, int on)
{
    struct omap_mpu_timer_s *timer = (struct omap_mpu_timer_s *) opaque;

    omap_timer_sync(timer);
    timer->rate = on ? omap_clk_getrate(timer->clk) : 0;
    omap_timer_update(timer);
}

static void omap_timer_clk_setup(struct omap_mpu_timer_s *timer)
{
    omap_clk_adduser(timer->clk,
                    qemu_allocate_irqs(omap_timer_clk_update, timer, 1)[0]);
    timer->rate = omap_clk_getrate(timer->clk);
}

static uint32_t omap_mpu_timer_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_mpu_timer_s *s = (struct omap_mpu_timer_s *) opaque;
    int offset = addr - s->base;

    switch (offset) {
    case 0x00:	/* CNTL_TIMER */
        return (s->enable << 5) | (s->ptv << 2) | (s->ar << 1) | s->st;

    case 0x04:	/* LOAD_TIM */
        break;

    case 0x08:	/* READ_TIM */
        return omap_timer_read(s);
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_mpu_timer_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_mpu_timer_s *s = (struct omap_mpu_timer_s *) opaque;
    int offset = addr - s->base;

    switch (offset) {
    case 0x00:	/* CNTL_TIMER */
        omap_timer_sync(s);
        s->enable = (value >> 5) & 1;
        s->ptv = (value >> 2) & 7;
        s->ar = (value >> 1) & 1;
        s->st = value & 1;
        omap_timer_update(s);
        return;

    case 0x04:	/* LOAD_TIM */
        s->reset_val = value;
        return;

    case 0x08:	/* READ_TIM */
        OMAP_RO_REG(addr);
        break;

    default:
        OMAP_BAD_REG(addr);
    }
}

static CPUReadMemoryFunc *omap_mpu_timer_readfn[] = {
    omap_badwidth_read32,
    omap_badwidth_read32,
    omap_mpu_timer_read,
};

static CPUWriteMemoryFunc *omap_mpu_timer_writefn[] = {
    omap_badwidth_write32,
    omap_badwidth_write32,
    omap_mpu_timer_write,
};

static void omap_mpu_timer_reset(struct omap_mpu_timer_s *s)
{
    qemu_del_timer(s->timer);
    s->enable = 0;
    s->reset_val = 31337;
    s->val = 0;
    s->ptv = 0;
    s->ar = 0;
    s->st = 0;
    s->it_ena = 1;
}

struct omap_mpu_timer_s *omap_mpu_timer_init(target_phys_addr_t base,
                qemu_irq irq, omap_clk clk)
{
    int iomemtype;
    struct omap_mpu_timer_s *s = (struct omap_mpu_timer_s *)
            qemu_mallocz(sizeof(struct omap_mpu_timer_s));

    s->irq = irq;
    s->clk = clk;
    s->base = base;
    s->timer = qemu_new_timer(vm_clock, omap_timer_tick, s);
    omap_mpu_timer_reset(s);
    omap_timer_clk_setup(s);

    iomemtype = cpu_register_io_memory(0, omap_mpu_timer_readfn,
                    omap_mpu_timer_writefn, s);
    cpu_register_physical_memory(s->base, 0x100, iomemtype);

    return s;
}

/* Watchdog timer */
struct omap_watchdog_timer_s {
    struct omap_mpu_timer_s timer;
    uint8_t last_wr;
    int mode;
    int free;
    int reset;
};

static uint32_t omap_wd_timer_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_watchdog_timer_s *s = (struct omap_watchdog_timer_s *) opaque;
    int offset = addr - s->timer.base;

    switch (offset) {
    case 0x00:	/* CNTL_TIMER */
        return (s->timer.ptv << 9) | (s->timer.ar << 8) |
                (s->timer.st << 7) | (s->free << 1);

    case 0x04:	/* READ_TIMER */
        return omap_timer_read(&s->timer);

    case 0x08:	/* TIMER_MODE */
        return s->mode << 15;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_wd_timer_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_watchdog_timer_s *s = (struct omap_watchdog_timer_s *) opaque;
    int offset = addr - s->timer.base;

    switch (offset) {
    case 0x00:	/* CNTL_TIMER */
        omap_timer_sync(&s->timer);
        s->timer.ptv = (value >> 9) & 7;
        s->timer.ar = (value >> 8) & 1;
        s->timer.st = (value >> 7) & 1;
        s->free = (value >> 1) & 1;
        omap_timer_update(&s->timer);
        break;

    case 0x04:	/* LOAD_TIMER */
        s->timer.reset_val = value & 0xffff;
        break;

    case 0x08:	/* TIMER_MODE */
        if (!s->mode && ((value >> 15) & 1))
            omap_clk_get(s->timer.clk);
        s->mode |= (value >> 15) & 1;
        if (s->last_wr == 0xf5) {
            if ((value & 0xff) == 0xa0) {
                if (s->mode) {
                    s->mode = 0;
                    omap_clk_put(s->timer.clk);
                }
            } else {
                /* XXX: on T|E hardware somehow this has no effect,
                 * on Zire 71 it works as specified.  */
                s->reset = 1;
                qemu_system_reset_request();
            }
        }
        s->last_wr = value & 0xff;
        break;

    default:
        OMAP_BAD_REG(addr);
    }
}

static CPUReadMemoryFunc *omap_wd_timer_readfn[] = {
    omap_badwidth_read16,
    omap_wd_timer_read,
    omap_badwidth_read16,
};

static CPUWriteMemoryFunc *omap_wd_timer_writefn[] = {
    omap_badwidth_write16,
    omap_wd_timer_write,
    omap_badwidth_write16,
};

static void omap_wd_timer_reset(struct omap_watchdog_timer_s *s)
{
    qemu_del_timer(s->timer.timer);
    if (!s->mode)
        omap_clk_get(s->timer.clk);
    s->mode = 1;
    s->free = 1;
    s->reset = 0;
    s->timer.enable = 1;
    s->timer.it_ena = 1;
    s->timer.reset_val = 0xffff;
    s->timer.val = 0;
    s->timer.st = 0;
    s->timer.ptv = 0;
    s->timer.ar = 0;
    omap_timer_update(&s->timer);
}

struct omap_watchdog_timer_s *omap_wd_timer_init(target_phys_addr_t base,
                qemu_irq irq, omap_clk clk)
{
    int iomemtype;
    struct omap_watchdog_timer_s *s = (struct omap_watchdog_timer_s *)
            qemu_mallocz(sizeof(struct omap_watchdog_timer_s));

    s->timer.irq = irq;
    s->timer.clk = clk;
    s->timer.base = base;
    s->timer.timer = qemu_new_timer(vm_clock, omap_timer_tick, &s->timer);
    omap_wd_timer_reset(s);
    omap_timer_clk_setup(&s->timer);

    iomemtype = cpu_register_io_memory(0, omap_wd_timer_readfn,
                    omap_wd_timer_writefn, s);
    cpu_register_physical_memory(s->timer.base, 0x100, iomemtype);

    return s;
}

/* 32-kHz timer */
struct omap_32khz_timer_s {
    struct omap_mpu_timer_s timer;
};

static uint32_t omap_os_timer_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_32khz_timer_s *s = (struct omap_32khz_timer_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;

    switch (offset) {
    case 0x00:	/* TVR */
        return s->timer.reset_val;

    case 0x04:	/* TCR */
        return omap_timer_read(&s->timer);

    case 0x08:	/* CR */
        return (s->timer.ar << 3) | (s->timer.it_ena << 2) | s->timer.st;

    default:
        break;
    }
    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_os_timer_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_32khz_timer_s *s = (struct omap_32khz_timer_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;

    switch (offset) {
    case 0x00:	/* TVR */
        s->timer.reset_val = value & 0x00ffffff;
        break;

    case 0x04:	/* TCR */
        OMAP_RO_REG(addr);
        break;

    case 0x08:	/* CR */
        s->timer.ar = (value >> 3) & 1;
        s->timer.it_ena = (value >> 2) & 1;
        if (s->timer.st != (value & 1) || (value & 2)) {
            omap_timer_sync(&s->timer);
            s->timer.enable = value & 1;
            s->timer.st = value & 1;
            omap_timer_update(&s->timer);
        }
        break;

    default:
        OMAP_BAD_REG(addr);
    }
}

static CPUReadMemoryFunc *omap_os_timer_readfn[] = {
    omap_badwidth_read32,
    omap_badwidth_read32,
    omap_os_timer_read,
};

static CPUWriteMemoryFunc *omap_os_timer_writefn[] = {
    omap_badwidth_write32,
    omap_badwidth_write32,
    omap_os_timer_write,
};

static void omap_os_timer_reset(struct omap_32khz_timer_s *s)
{
    qemu_del_timer(s->timer.timer);
    s->timer.enable = 0;
    s->timer.it_ena = 0;
    s->timer.reset_val = 0x00ffffff;
    s->timer.val = 0;
    s->timer.st = 0;
    s->timer.ptv = 0;
    s->timer.ar = 1;
}

struct omap_32khz_timer_s *omap_os_timer_init(target_phys_addr_t base,
                qemu_irq irq, omap_clk clk)
{
    int iomemtype;
    struct omap_32khz_timer_s *s = (struct omap_32khz_timer_s *)
            qemu_mallocz(sizeof(struct omap_32khz_timer_s));

    s->timer.irq = irq;
    s->timer.clk = clk;
    s->timer.base = base;
    s->timer.timer = qemu_new_timer(vm_clock, omap_timer_tick, &s->timer);
    omap_os_timer_reset(s);
    omap_timer_clk_setup(&s->timer);

    iomemtype = cpu_register_io_memory(0, omap_os_timer_readfn,
                    omap_os_timer_writefn, s);
    cpu_register_physical_memory(s->timer.base, 0x800, iomemtype);

    return s;
}

/* Ultra Low-Power Device Module */
static uint32_t omap_ulpd_pm_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_mpu_state_s *s = (struct omap_mpu_state_s *) opaque;
    int offset = addr - s->ulpd_pm_base;
    uint16_t ret;

    switch (offset) {
    case 0x14:	/* IT_STATUS */
        ret = s->ulpd_pm_regs[offset >> 2];
        s->ulpd_pm_regs[offset >> 2] = 0;
        qemu_irq_lower(s->irq[1][OMAP_INT_GAUGE_32K]);
        return ret;

    case 0x18:	/* Reserved */
    case 0x1c:	/* Reserved */
    case 0x20:	/* Reserved */
    case 0x28:	/* Reserved */
    case 0x2c:	/* Reserved */
        OMAP_BAD_REG(addr);
    case 0x00:	/* COUNTER_32_LSB */
    case 0x04:	/* COUNTER_32_MSB */
    case 0x08:	/* COUNTER_HIGH_FREQ_LSB */
    case 0x0c:	/* COUNTER_HIGH_FREQ_MSB */
    case 0x10:	/* GAUGING_CTRL */
    case 0x24:	/* SETUP_ANALOG_CELL3_ULPD1 */
    case 0x30:	/* CLOCK_CTRL */
    case 0x34:	/* SOFT_REQ */
    case 0x38:	/* COUNTER_32_FIQ */
    case 0x3c:	/* DPLL_CTRL */
    case 0x40:	/* STATUS_REQ */
        /* XXX: check clk::usecount state for every clock */
    case 0x48:	/* LOCL_TIME */
    case 0x4c:	/* APLL_CTRL */
    case 0x50:	/* POWER_CTRL */
        return s->ulpd_pm_regs[offset >> 2];
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static inline void omap_ulpd_clk_update(struct omap_mpu_state_s *s,
                uint16_t diff, uint16_t value)
{
    if (diff & (1 << 4))				/* USB_MCLK_EN */
        omap_clk_onoff(omap_findclk(s, "usb_clk0"), (value >> 4) & 1);
    if (diff & (1 << 5))				/* DIS_USB_PVCI_CLK */
        omap_clk_onoff(omap_findclk(s, "usb_w2fc_ck"), (~value >> 5) & 1);
}

static inline void omap_ulpd_req_update(struct omap_mpu_state_s *s,
                uint16_t diff, uint16_t value)
{
    if (diff & (1 << 0))				/* SOFT_DPLL_REQ */
        omap_clk_canidle(omap_findclk(s, "dpll4"), (~value >> 0) & 1);
    if (diff & (1 << 1))				/* SOFT_COM_REQ */
        omap_clk_canidle(omap_findclk(s, "com_mclk_out"), (~value >> 1) & 1);
    if (diff & (1 << 2))				/* SOFT_SDW_REQ */
        omap_clk_canidle(omap_findclk(s, "bt_mclk_out"), (~value >> 2) & 1);
    if (diff & (1 << 3))				/* SOFT_USB_REQ */
        omap_clk_canidle(omap_findclk(s, "usb_clk0"), (~value >> 3) & 1);
}

static void omap_ulpd_pm_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_mpu_state_s *s = (struct omap_mpu_state_s *) opaque;
    int offset = addr - s->ulpd_pm_base;
    int64_t now, ticks;
    int div, mult;
    static const int bypass_div[4] = { 1, 2, 4, 4 };
    uint16_t diff;

    switch (offset) {
    case 0x00:	/* COUNTER_32_LSB */
    case 0x04:	/* COUNTER_32_MSB */
    case 0x08:	/* COUNTER_HIGH_FREQ_LSB */
    case 0x0c:	/* COUNTER_HIGH_FREQ_MSB */
    case 0x14:	/* IT_STATUS */
    case 0x40:	/* STATUS_REQ */
        OMAP_RO_REG(addr);
        break;

    case 0x10:	/* GAUGING_CTRL */
        /* Bits 0 and 1 seem to be confused in the OMAP 310 TRM */
        if ((s->ulpd_pm_regs[offset >> 2] ^ value) & 1) {
            now = qemu_get_clock(vm_clock);

            if (value & 1)
                s->ulpd_gauge_start = now;
            else {
                now -= s->ulpd_gauge_start;

                /* 32-kHz ticks */
                ticks = muldiv64(now, 32768, ticks_per_sec);
                s->ulpd_pm_regs[0x00 >> 2] = (ticks >>  0) & 0xffff;
                s->ulpd_pm_regs[0x04 >> 2] = (ticks >> 16) & 0xffff;
                if (ticks >> 32)	/* OVERFLOW_32K */
                    s->ulpd_pm_regs[0x14 >> 2] |= 1 << 2;

                /* High frequency ticks */
                ticks = muldiv64(now, 12000000, ticks_per_sec);
                s->ulpd_pm_regs[0x08 >> 2] = (ticks >>  0) & 0xffff;
                s->ulpd_pm_regs[0x0c >> 2] = (ticks >> 16) & 0xffff;
                if (ticks >> 32)	/* OVERFLOW_HI_FREQ */
                    s->ulpd_pm_regs[0x14 >> 2] |= 1 << 1;

                s->ulpd_pm_regs[0x14 >> 2] |= 1 << 0;	/* IT_GAUGING */
                qemu_irq_raise(s->irq[1][OMAP_INT_GAUGE_32K]);
            }
        }
        s->ulpd_pm_regs[offset >> 2] = value;
        break;

    case 0x18:	/* Reserved */
    case 0x1c:	/* Reserved */
    case 0x20:	/* Reserved */
    case 0x28:	/* Reserved */
    case 0x2c:	/* Reserved */
        OMAP_BAD_REG(addr);
    case 0x24:	/* SETUP_ANALOG_CELL3_ULPD1 */
    case 0x38:	/* COUNTER_32_FIQ */
    case 0x48:	/* LOCL_TIME */
    case 0x50:	/* POWER_CTRL */
        s->ulpd_pm_regs[offset >> 2] = value;
        break;

    case 0x30:	/* CLOCK_CTRL */
        diff = s->ulpd_pm_regs[offset >> 2] ^ value;
        s->ulpd_pm_regs[offset >> 2] = value & 0x3f;
        omap_ulpd_clk_update(s, diff, value);
        break;

    case 0x34:	/* SOFT_REQ */
        diff = s->ulpd_pm_regs[offset >> 2] ^ value;
        s->ulpd_pm_regs[offset >> 2] = value & 0x1f;
        omap_ulpd_req_update(s, diff, value);
        break;

    case 0x3c:	/* DPLL_CTRL */
        /* XXX: OMAP310 TRM claims bit 3 is PLL_ENABLE, and bit 4 is
         * omitted altogether, probably a typo.  */
        /* This register has identical semantics with DPLL(1:3) control
         * registers, see omap_dpll_write() */
        diff = s->ulpd_pm_regs[offset >> 2] & value;
        s->ulpd_pm_regs[offset >> 2] = value & 0x2fff;
        if (diff & (0x3ff << 2)) {
            if (value & (1 << 4)) {			/* PLL_ENABLE */
                div = ((value >> 5) & 3) + 1;		/* PLL_DIV */
                mult = MIN((value >> 7) & 0x1f, 1);	/* PLL_MULT */
            } else {
                div = bypass_div[((value >> 2) & 3)];	/* BYPASS_DIV */
                mult = 1;
            }
            omap_clk_setrate(omap_findclk(s, "dpll4"), div, mult);
        }

        /* Enter the desired mode.  */
        s->ulpd_pm_regs[offset >> 2] =
                (s->ulpd_pm_regs[offset >> 2] & 0xfffe) |
                ((s->ulpd_pm_regs[offset >> 2] >> 4) & 1);

        /* Act as if the lock is restored.  */
        s->ulpd_pm_regs[offset >> 2] |= 2;
        break;

    case 0x4c:	/* APLL_CTRL */
        diff = s->ulpd_pm_regs[offset >> 2] & value;
        s->ulpd_pm_regs[offset >> 2] = value & 0xf;
        if (diff & (1 << 0))				/* APLL_NDPLL_SWITCH */
            omap_clk_reparent(omap_findclk(s, "ck_48m"), omap_findclk(s,
                                    (value & (1 << 0)) ? "apll" : "dpll4"));
        break;

    default:
        OMAP_BAD_REG(addr);
    }
}

static CPUReadMemoryFunc *omap_ulpd_pm_readfn[] = {
    omap_badwidth_read16,
    omap_ulpd_pm_read,
    omap_badwidth_read16,
};

static CPUWriteMemoryFunc *omap_ulpd_pm_writefn[] = {
    omap_badwidth_write16,
    omap_ulpd_pm_write,
    omap_badwidth_write16,
};

static void omap_ulpd_pm_reset(struct omap_mpu_state_s *mpu)
{
    mpu->ulpd_pm_regs[0x00 >> 2] = 0x0001;
    mpu->ulpd_pm_regs[0x04 >> 2] = 0x0000;
    mpu->ulpd_pm_regs[0x08 >> 2] = 0x0001;
    mpu->ulpd_pm_regs[0x0c >> 2] = 0x0000;
    mpu->ulpd_pm_regs[0x10 >> 2] = 0x0000;
    mpu->ulpd_pm_regs[0x18 >> 2] = 0x01;
    mpu->ulpd_pm_regs[0x1c >> 2] = 0x01;
    mpu->ulpd_pm_regs[0x20 >> 2] = 0x01;
    mpu->ulpd_pm_regs[0x24 >> 2] = 0x03ff;
    mpu->ulpd_pm_regs[0x28 >> 2] = 0x01;
    mpu->ulpd_pm_regs[0x2c >> 2] = 0x01;
    omap_ulpd_clk_update(mpu, mpu->ulpd_pm_regs[0x30 >> 2], 0x0000);
    mpu->ulpd_pm_regs[0x30 >> 2] = 0x0000;
    omap_ulpd_req_update(mpu, mpu->ulpd_pm_regs[0x34 >> 2], 0x0000);
    mpu->ulpd_pm_regs[0x34 >> 2] = 0x0000;
    mpu->ulpd_pm_regs[0x38 >> 2] = 0x0001;
    mpu->ulpd_pm_regs[0x3c >> 2] = 0x2211;
    mpu->ulpd_pm_regs[0x40 >> 2] = 0x0000; /* FIXME: dump a real STATUS_REQ */
    mpu->ulpd_pm_regs[0x48 >> 2] = 0x960;
    mpu->ulpd_pm_regs[0x4c >> 2] = 0x08;
    mpu->ulpd_pm_regs[0x50 >> 2] = 0x08;
    omap_clk_setrate(omap_findclk(mpu, "dpll4"), 1, 4);
    omap_clk_reparent(omap_findclk(mpu, "ck_48m"), omap_findclk(mpu, "dpll4"));
}

static void omap_ulpd_pm_init(target_phys_addr_t base,
                struct omap_mpu_state_s *mpu)
{
    int iomemtype = cpu_register_io_memory(0, omap_ulpd_pm_readfn,
                    omap_ulpd_pm_writefn, mpu);

    mpu->ulpd_pm_base = base;
    cpu_register_physical_memory(mpu->ulpd_pm_base, 0x800, iomemtype);
    omap_ulpd_pm_reset(mpu);
}

/* OMAP Pin Configuration */
static uint32_t omap_pin_cfg_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_mpu_state_s *s = (struct omap_mpu_state_s *) opaque;
    int offset = addr - s->pin_cfg_base;

    switch (offset) {
    case 0x00:	/* FUNC_MUX_CTRL_0 */
    case 0x04:	/* FUNC_MUX_CTRL_1 */
    case 0x08:	/* FUNC_MUX_CTRL_2 */
        return s->func_mux_ctrl[offset >> 2];

    case 0x0c:	/* COMP_MODE_CTRL_0 */
        return s->comp_mode_ctrl[0];

    case 0x10:	/* FUNC_MUX_CTRL_3 */
    case 0x14:	/* FUNC_MUX_CTRL_4 */
    case 0x18:	/* FUNC_MUX_CTRL_5 */
    case 0x1c:	/* FUNC_MUX_CTRL_6 */
    case 0x20:	/* FUNC_MUX_CTRL_7 */
    case 0x24:	/* FUNC_MUX_CTRL_8 */
    case 0x28:	/* FUNC_MUX_CTRL_9 */
    case 0x2c:	/* FUNC_MUX_CTRL_A */
    case 0x30:	/* FUNC_MUX_CTRL_B */
    case 0x34:	/* FUNC_MUX_CTRL_C */
    case 0x38:	/* FUNC_MUX_CTRL_D */
        return s->func_mux_ctrl[(offset >> 2) - 1];

    case 0x40:	/* PULL_DWN_CTRL_0 */
    case 0x44:	/* PULL_DWN_CTRL_1 */
    case 0x48:	/* PULL_DWN_CTRL_2 */
    case 0x4c:	/* PULL_DWN_CTRL_3 */
        return s->pull_dwn_ctrl[(offset & 0xf) >> 2];

    case 0x50:	/* GATE_INH_CTRL_0 */
        return s->gate_inh_ctrl[0];

    case 0x60:	/* VOLTAGE_CTRL_0 */
        return s->voltage_ctrl[0];

    case 0x70:	/* TEST_DBG_CTRL_0 */
        return s->test_dbg_ctrl[0];

    case 0x80:	/* MOD_CONF_CTRL_0 */
        return s->mod_conf_ctrl[0];
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static inline void omap_pin_funcmux0_update(struct omap_mpu_state_s *s,
                uint32_t diff, uint32_t value)
{
    if (s->compat1509) {
        if (diff & (1 << 9))			/* BLUETOOTH */
            omap_clk_onoff(omap_findclk(s, "bt_mclk_out"),
                            (~value >> 9) & 1);
        if (diff & (1 << 7))			/* USB.CLKO */
            omap_clk_onoff(omap_findclk(s, "usb.clko"),
                            (value >> 7) & 1);
    }
}

static inline void omap_pin_funcmux1_update(struct omap_mpu_state_s *s,
                uint32_t diff, uint32_t value)
{
    if (s->compat1509) {
        if (diff & (1 << 31))			/* MCBSP3_CLK_HIZ_DI */
            omap_clk_onoff(omap_findclk(s, "mcbsp3.clkx"),
                            (value >> 31) & 1);
        if (diff & (1 << 1))			/* CLK32K */
            omap_clk_onoff(omap_findclk(s, "clk32k_out"),
                            (~value >> 1) & 1);
    }
}

static inline void omap_pin_modconf1_update(struct omap_mpu_state_s *s,
                uint32_t diff, uint32_t value)
{
    if (diff & (1 << 31))			/* CONF_MOD_UART3_CLK_MODE_R */
         omap_clk_reparent(omap_findclk(s, "uart3_ck"),
                         omap_findclk(s, ((value >> 31) & 1) ?
                                 "ck_48m" : "armper_ck"));
    if (diff & (1 << 30))			/* CONF_MOD_UART2_CLK_MODE_R */
         omap_clk_reparent(omap_findclk(s, "uart2_ck"),
                         omap_findclk(s, ((value >> 30) & 1) ?
                                 "ck_48m" : "armper_ck"));
    if (diff & (1 << 29))			/* CONF_MOD_UART1_CLK_MODE_R */
         omap_clk_reparent(omap_findclk(s, "uart1_ck"),
                         omap_findclk(s, ((value >> 29) & 1) ?
                                 "ck_48m" : "armper_ck"));
    if (diff & (1 << 23))			/* CONF_MOD_MMC_SD_CLK_REQ_R */
         omap_clk_reparent(omap_findclk(s, "mmc_ck"),
                         omap_findclk(s, ((value >> 23) & 1) ?
                                 "ck_48m" : "armper_ck"));
    if (diff & (1 << 12))			/* CONF_MOD_COM_MCLK_12_48_S */
         omap_clk_reparent(omap_findclk(s, "com_mclk_out"),
                         omap_findclk(s, ((value >> 12) & 1) ?
                                 "ck_48m" : "armper_ck"));
    if (diff & (1 << 9))			/* CONF_MOD_USB_HOST_HHC_UHO */
         omap_clk_onoff(omap_findclk(s, "usb_hhc_ck"), (value >> 9) & 1);
}

static void omap_pin_cfg_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_mpu_state_s *s = (struct omap_mpu_state_s *) opaque;
    int offset = addr - s->pin_cfg_base;
    uint32_t diff;

    switch (offset) {
    case 0x00:	/* FUNC_MUX_CTRL_0 */
        diff = s->func_mux_ctrl[offset >> 2] ^ value;
        s->func_mux_ctrl[offset >> 2] = value;
        omap_pin_funcmux0_update(s, diff, value);
        return;

    case 0x04:	/* FUNC_MUX_CTRL_1 */
        diff = s->func_mux_ctrl[offset >> 2] ^ value;
        s->func_mux_ctrl[offset >> 2] = value;
        omap_pin_funcmux1_update(s, diff, value);
        return;

    case 0x08:	/* FUNC_MUX_CTRL_2 */
        s->func_mux_ctrl[offset >> 2] = value;
        return;

    case 0x0c:	/* COMP_MODE_CTRL_0 */
        s->comp_mode_ctrl[0] = value;
        s->compat1509 = (value != 0x0000eaef);
        omap_pin_funcmux0_update(s, ~0, s->func_mux_ctrl[0]);
        omap_pin_funcmux1_update(s, ~0, s->func_mux_ctrl[1]);
        return;

    case 0x10:	/* FUNC_MUX_CTRL_3 */
    case 0x14:	/* FUNC_MUX_CTRL_4 */
    case 0x18:	/* FUNC_MUX_CTRL_5 */
    case 0x1c:	/* FUNC_MUX_CTRL_6 */
    case 0x20:	/* FUNC_MUX_CTRL_7 */
    case 0x24:	/* FUNC_MUX_CTRL_8 */
    case 0x28:	/* FUNC_MUX_CTRL_9 */
    case 0x2c:	/* FUNC_MUX_CTRL_A */
    case 0x30:	/* FUNC_MUX_CTRL_B */
    case 0x34:	/* FUNC_MUX_CTRL_C */
    case 0x38:	/* FUNC_MUX_CTRL_D */
        s->func_mux_ctrl[(offset >> 2) - 1] = value;
        return;

    case 0x40:	/* PULL_DWN_CTRL_0 */
    case 0x44:	/* PULL_DWN_CTRL_1 */
    case 0x48:	/* PULL_DWN_CTRL_2 */
    case 0x4c:	/* PULL_DWN_CTRL_3 */
        s->pull_dwn_ctrl[(offset & 0xf) >> 2] = value;
        return;

    case 0x50:	/* GATE_INH_CTRL_0 */
        s->gate_inh_ctrl[0] = value;
        return;

    case 0x60:	/* VOLTAGE_CTRL_0 */
        s->voltage_ctrl[0] = value;
        return;

    case 0x70:	/* TEST_DBG_CTRL_0 */
        s->test_dbg_ctrl[0] = value;
        return;

    case 0x80:	/* MOD_CONF_CTRL_0 */
        diff = s->mod_conf_ctrl[0] ^ value;
        s->mod_conf_ctrl[0] = value;
        omap_pin_modconf1_update(s, diff, value);
        return;

    default:
        OMAP_BAD_REG(addr);
    }
}

static CPUReadMemoryFunc *omap_pin_cfg_readfn[] = {
    omap_badwidth_read32,
    omap_badwidth_read32,
    omap_pin_cfg_read,
};

static CPUWriteMemoryFunc *omap_pin_cfg_writefn[] = {
    omap_badwidth_write32,
    omap_badwidth_write32,
    omap_pin_cfg_write,
};

static void omap_pin_cfg_reset(struct omap_mpu_state_s *mpu)
{
    /* Start in Compatibility Mode.  */
    mpu->compat1509 = 1;
    omap_pin_funcmux0_update(mpu, mpu->func_mux_ctrl[0], 0);
    omap_pin_funcmux1_update(mpu, mpu->func_mux_ctrl[1], 0);
    omap_pin_modconf1_update(mpu, mpu->mod_conf_ctrl[0], 0);
    memset(mpu->func_mux_ctrl, 0, sizeof(mpu->func_mux_ctrl));
    memset(mpu->comp_mode_ctrl, 0, sizeof(mpu->comp_mode_ctrl));
    memset(mpu->pull_dwn_ctrl, 0, sizeof(mpu->pull_dwn_ctrl));
    memset(mpu->gate_inh_ctrl, 0, sizeof(mpu->gate_inh_ctrl));
    memset(mpu->voltage_ctrl, 0, sizeof(mpu->voltage_ctrl));
    memset(mpu->test_dbg_ctrl, 0, sizeof(mpu->test_dbg_ctrl));
    memset(mpu->mod_conf_ctrl, 0, sizeof(mpu->mod_conf_ctrl));
}

static void omap_pin_cfg_init(target_phys_addr_t base,
                struct omap_mpu_state_s *mpu)
{
    int iomemtype = cpu_register_io_memory(0, omap_pin_cfg_readfn,
                    omap_pin_cfg_writefn, mpu);

    mpu->pin_cfg_base = base;
    cpu_register_physical_memory(mpu->pin_cfg_base, 0x800, iomemtype);
    omap_pin_cfg_reset(mpu);
}

/* Device Identification, Die Identification */
static uint32_t omap_id_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_mpu_state_s *s = (struct omap_mpu_state_s *) opaque;

    switch (addr) {
    case 0xfffe1800:	/* DIE_ID_LSB */
        return 0xc9581f0e;
    case 0xfffe1804:	/* DIE_ID_MSB */
        return 0xa8858bfa;

    case 0xfffe2000:	/* PRODUCT_ID_LSB */
        return 0x00aaaafc;
    case 0xfffe2004:	/* PRODUCT_ID_MSB */
        return 0xcafeb574;

    case 0xfffed400:	/* JTAG_ID_LSB */
        switch (s->mpu_model) {
        case omap310:
            return 0x03310315;
        case omap1510:
            return 0x03310115;
        }
        break;

    case 0xfffed404:	/* JTAG_ID_MSB */
        switch (s->mpu_model) {
        case omap310:
            return 0xfb57402f;
        case omap1510:
            return 0xfb47002f;
        }
        break;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_id_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    OMAP_BAD_REG(addr);
}

static CPUReadMemoryFunc *omap_id_readfn[] = {
    omap_badwidth_read32,
    omap_badwidth_read32,
    omap_id_read,
};

static CPUWriteMemoryFunc *omap_id_writefn[] = {
    omap_badwidth_write32,
    omap_badwidth_write32,
    omap_id_write,
};

static void omap_id_init(struct omap_mpu_state_s *mpu)
{
    int iomemtype = cpu_register_io_memory(0, omap_id_readfn,
                    omap_id_writefn, mpu);
    cpu_register_physical_memory(0xfffe1800, 0x800, iomemtype);
    cpu_register_physical_memory(0xfffed400, 0x100, iomemtype);
    if (!cpu_is_omap15xx(mpu))
        cpu_register_physical_memory(0xfffe2000, 0x800, iomemtype);
}

/* MPUI Control (Dummy) */
static uint32_t omap_mpui_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_mpu_state_s *s = (struct omap_mpu_state_s *) opaque;
    int offset = addr - s->mpui_base;

    switch (offset) {
    case 0x00:	/* CTRL */
        return s->mpui_ctrl;
    case 0x04:	/* DEBUG_ADDR */
        return 0x01ffffff;
    case 0x08:	/* DEBUG_DATA */
        return 0xffffffff;
    case 0x0c:	/* DEBUG_FLAG */
        return 0x00000800;
    case 0x10:	/* STATUS */
        return 0x00000000;

    /* Not in OMAP310 */
    case 0x14:	/* DSP_STATUS */
    case 0x18:	/* DSP_BOOT_CONFIG */
        return 0x00000000;
    case 0x1c:	/* DSP_MPUI_CONFIG */
        return 0x0000ffff;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_mpui_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_mpu_state_s *s = (struct omap_mpu_state_s *) opaque;
    int offset = addr - s->mpui_base;

    switch (offset) {
    case 0x00:	/* CTRL */
        s->mpui_ctrl = value & 0x007fffff;
        break;

    case 0x04:	/* DEBUG_ADDR */
    case 0x08:	/* DEBUG_DATA */
    case 0x0c:	/* DEBUG_FLAG */
    case 0x10:	/* STATUS */
    /* Not in OMAP310 */
    case 0x14:	/* DSP_STATUS */
        OMAP_RO_REG(addr);
    case 0x18:	/* DSP_BOOT_CONFIG */
    case 0x1c:	/* DSP_MPUI_CONFIG */
        break;

    default:
        OMAP_BAD_REG(addr);
    }
}

static CPUReadMemoryFunc *omap_mpui_readfn[] = {
    omap_badwidth_read32,
    omap_badwidth_read32,
    omap_mpui_read,
};

static CPUWriteMemoryFunc *omap_mpui_writefn[] = {
    omap_badwidth_write32,
    omap_badwidth_write32,
    omap_mpui_write,
};

static void omap_mpui_reset(struct omap_mpu_state_s *s)
{
    s->mpui_ctrl = 0x0003ff1b;
}

static void omap_mpui_init(target_phys_addr_t base,
                struct omap_mpu_state_s *mpu)
{
    int iomemtype = cpu_register_io_memory(0, omap_mpui_readfn,
                    omap_mpui_writefn, mpu);

    mpu->mpui_base = base;
    cpu_register_physical_memory(mpu->mpui_base, 0x100, iomemtype);

    omap_mpui_reset(mpu);
}

/* TIPB Bridges */
struct omap_tipb_bridge_s {
    target_phys_addr_t base;
    qemu_irq abort;

    int width_intr;
    uint16_t control;
    uint16_t alloc;
    uint16_t buffer;
    uint16_t enh_control;
};

static uint32_t omap_tipb_bridge_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_tipb_bridge_s *s = (struct omap_tipb_bridge_s *) opaque;
    int offset = addr - s->base;

    switch (offset) {
    case 0x00:	/* TIPB_CNTL */
        return s->control;
    case 0x04:	/* TIPB_BUS_ALLOC */
        return s->alloc;
    case 0x08:	/* MPU_TIPB_CNTL */
        return s->buffer;
    case 0x0c:	/* ENHANCED_TIPB_CNTL */
        return s->enh_control;
    case 0x10:	/* ADDRESS_DBG */
    case 0x14:	/* DATA_DEBUG_LOW */
    case 0x18:	/* DATA_DEBUG_HIGH */
        return 0xffff;
    case 0x1c:	/* DEBUG_CNTR_SIG */
        return 0x00f8;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_tipb_bridge_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_tipb_bridge_s *s = (struct omap_tipb_bridge_s *) opaque;
    int offset = addr - s->base;

    switch (offset) {
    case 0x00:	/* TIPB_CNTL */
        s->control = value & 0xffff;
        break;

    case 0x04:	/* TIPB_BUS_ALLOC */
        s->alloc = value & 0x003f;
        break;

    case 0x08:	/* MPU_TIPB_CNTL */
        s->buffer = value & 0x0003;
        break;

    case 0x0c:	/* ENHANCED_TIPB_CNTL */
        s->width_intr = !(value & 2);
        s->enh_control = value & 0x000f;
        break;

    case 0x10:	/* ADDRESS_DBG */
    case 0x14:	/* DATA_DEBUG_LOW */
    case 0x18:	/* DATA_DEBUG_HIGH */
    case 0x1c:	/* DEBUG_CNTR_SIG */
        OMAP_RO_REG(addr);
        break;

    default:
        OMAP_BAD_REG(addr);
    }
}

static CPUReadMemoryFunc *omap_tipb_bridge_readfn[] = {
    omap_badwidth_read16,
    omap_tipb_bridge_read,
    omap_tipb_bridge_read,
};

static CPUWriteMemoryFunc *omap_tipb_bridge_writefn[] = {
    omap_badwidth_write16,
    omap_tipb_bridge_write,
    omap_tipb_bridge_write,
};

static void omap_tipb_bridge_reset(struct omap_tipb_bridge_s *s)
{
    s->control = 0xffff;
    s->alloc = 0x0009;
    s->buffer = 0x0000;
    s->enh_control = 0x000f;
}

struct omap_tipb_bridge_s *omap_tipb_bridge_init(target_phys_addr_t base,
                qemu_irq abort_irq, omap_clk clk)
{
    int iomemtype;
    struct omap_tipb_bridge_s *s = (struct omap_tipb_bridge_s *)
            qemu_mallocz(sizeof(struct omap_tipb_bridge_s));

    s->abort = abort_irq;
    s->base = base;
    omap_tipb_bridge_reset(s);

    iomemtype = cpu_register_io_memory(0, omap_tipb_bridge_readfn,
                    omap_tipb_bridge_writefn, s);
    cpu_register_physical_memory(s->base, 0x100, iomemtype);

    return s;
}

/* Dummy Traffic Controller's Memory Interface */
static uint32_t omap_tcmi_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_mpu_state_s *s = (struct omap_mpu_state_s *) opaque;
    int offset = addr - s->tcmi_base;
    uint32_t ret;

    switch (offset) {
    case 0x00:	/* IMIF_PRIO */
    case 0x04:	/* EMIFS_PRIO */
    case 0x08:	/* EMIFF_PRIO */
    case 0x0c:	/* EMIFS_CONFIG */
    case 0x10:	/* EMIFS_CS0_CONFIG */
    case 0x14:	/* EMIFS_CS1_CONFIG */
    case 0x18:	/* EMIFS_CS2_CONFIG */
    case 0x1c:	/* EMIFS_CS3_CONFIG */
    case 0x24:	/* EMIFF_MRS */
    case 0x28:	/* TIMEOUT1 */
    case 0x2c:	/* TIMEOUT2 */
    case 0x30:	/* TIMEOUT3 */
    case 0x3c:	/* EMIFF_SDRAM_CONFIG_2 */
    case 0x40:	/* EMIFS_CFG_DYN_WAIT */
        return s->tcmi_regs[offset >> 2];

    case 0x20:	/* EMIFF_SDRAM_CONFIG */
        ret = s->tcmi_regs[offset >> 2];
        s->tcmi_regs[offset >> 2] &= ~1; /* XXX: Clear SLRF on SDRAM access */
        /* XXX: We can try using the VGA_DIRTY flag for this */
        return ret;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_tcmi_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_mpu_state_s *s = (struct omap_mpu_state_s *) opaque;
    int offset = addr - s->tcmi_base;

    switch (offset) {
    case 0x00:	/* IMIF_PRIO */
    case 0x04:	/* EMIFS_PRIO */
    case 0x08:	/* EMIFF_PRIO */
    case 0x10:	/* EMIFS_CS0_CONFIG */
    case 0x14:	/* EMIFS_CS1_CONFIG */
    case 0x18:	/* EMIFS_CS2_CONFIG */
    case 0x1c:	/* EMIFS_CS3_CONFIG */
    case 0x20:	/* EMIFF_SDRAM_CONFIG */
    case 0x24:	/* EMIFF_MRS */
    case 0x28:	/* TIMEOUT1 */
    case 0x2c:	/* TIMEOUT2 */
    case 0x30:	/* TIMEOUT3 */
    case 0x3c:	/* EMIFF_SDRAM_CONFIG_2 */
    case 0x40:	/* EMIFS_CFG_DYN_WAIT */
        s->tcmi_regs[offset >> 2] = value;
        break;
    case 0x0c:	/* EMIFS_CONFIG */
        s->tcmi_regs[offset >> 2] = (value & 0xf) | (1 << 4);
        break;

    default:
        OMAP_BAD_REG(addr);
    }
}

static CPUReadMemoryFunc *omap_tcmi_readfn[] = {
    omap_badwidth_read32,
    omap_badwidth_read32,
    omap_tcmi_read,
};

static CPUWriteMemoryFunc *omap_tcmi_writefn[] = {
    omap_badwidth_write32,
    omap_badwidth_write32,
    omap_tcmi_write,
};

static void omap_tcmi_reset(struct omap_mpu_state_s *mpu)
{
    mpu->tcmi_regs[0x00 >> 2] = 0x00000000;
    mpu->tcmi_regs[0x04 >> 2] = 0x00000000;
    mpu->tcmi_regs[0x08 >> 2] = 0x00000000;
    mpu->tcmi_regs[0x0c >> 2] = 0x00000010;
    mpu->tcmi_regs[0x10 >> 2] = 0x0010fffb;
    mpu->tcmi_regs[0x14 >> 2] = 0x0010fffb;
    mpu->tcmi_regs[0x18 >> 2] = 0x0010fffb;
    mpu->tcmi_regs[0x1c >> 2] = 0x0010fffb;
    mpu->tcmi_regs[0x20 >> 2] = 0x00618800;
    mpu->tcmi_regs[0x24 >> 2] = 0x00000037;
    mpu->tcmi_regs[0x28 >> 2] = 0x00000000;
    mpu->tcmi_regs[0x2c >> 2] = 0x00000000;
    mpu->tcmi_regs[0x30 >> 2] = 0x00000000;
    mpu->tcmi_regs[0x3c >> 2] = 0x00000003;
    mpu->tcmi_regs[0x40 >> 2] = 0x00000000;
}

static void omap_tcmi_init(target_phys_addr_t base,
                struct omap_mpu_state_s *mpu)
{
    int iomemtype = cpu_register_io_memory(0, omap_tcmi_readfn,
                    omap_tcmi_writefn, mpu);

    mpu->tcmi_base = base;
    cpu_register_physical_memory(mpu->tcmi_base, 0x100, iomemtype);
    omap_tcmi_reset(mpu);
}

/* Digital phase-locked loops control */
static uint32_t omap_dpll_read(void *opaque, target_phys_addr_t addr)
{
    struct dpll_ctl_s *s = (struct dpll_ctl_s *) opaque;
    int offset = addr - s->base;

    if (offset == 0x00)	/* CTL_REG */
        return s->mode;

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_dpll_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct dpll_ctl_s *s = (struct dpll_ctl_s *) opaque;
    uint16_t diff;
    int offset = addr - s->base;
    static const int bypass_div[4] = { 1, 2, 4, 4 };
    int div, mult;

    if (offset == 0x00) {	/* CTL_REG */
        /* See omap_ulpd_pm_write() too */
        diff = s->mode & value;
        s->mode = value & 0x2fff;
        if (diff & (0x3ff << 2)) {
            if (value & (1 << 4)) {			/* PLL_ENABLE */
                div = ((value >> 5) & 3) + 1;		/* PLL_DIV */
                mult = MIN((value >> 7) & 0x1f, 1);	/* PLL_MULT */
            } else {
                div = bypass_div[((value >> 2) & 3)];	/* BYPASS_DIV */
                mult = 1;
            }
            omap_clk_setrate(s->dpll, div, mult);
        }

        /* Enter the desired mode.  */
        s->mode = (s->mode & 0xfffe) | ((s->mode >> 4) & 1);

        /* Act as if the lock is restored.  */
        s->mode |= 2;
    } else {
        OMAP_BAD_REG(addr);
    }
}

static CPUReadMemoryFunc *omap_dpll_readfn[] = {
    omap_badwidth_read16,
    omap_dpll_read,
    omap_badwidth_read16,
};

static CPUWriteMemoryFunc *omap_dpll_writefn[] = {
    omap_badwidth_write16,
    omap_dpll_write,
    omap_badwidth_write16,
};

static void omap_dpll_reset(struct dpll_ctl_s *s)
{
    s->mode = 0x2002;
    omap_clk_setrate(s->dpll, 1, 1);
}

static void omap_dpll_init(struct dpll_ctl_s *s, target_phys_addr_t base,
                omap_clk clk)
{
    int iomemtype = cpu_register_io_memory(0, omap_dpll_readfn,
                    omap_dpll_writefn, s);

    s->base = base;
    s->dpll = clk;
    omap_dpll_reset(s);

    cpu_register_physical_memory(s->base, 0x100, iomemtype);
}

/* UARTs */
struct omap_uart_s {
    SerialState *serial; /* TODO */
};

static void omap_uart_reset(struct omap_uart_s *s)
{
}

struct omap_uart_s *omap_uart_init(target_phys_addr_t base,
                qemu_irq irq, omap_clk clk, CharDriverState *chr)
{
    struct omap_uart_s *s = (struct omap_uart_s *)
            qemu_mallocz(sizeof(struct omap_uart_s));
    if (chr)
        s->serial = serial_mm_init(base, 2, irq, chr, 1);
    return s;
}

/* MPU Clock/Reset/Power Mode Control */
static uint32_t omap_clkm_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_mpu_state_s *s = (struct omap_mpu_state_s *) opaque;
    int offset = addr - s->clkm.mpu_base;

    switch (offset) {
    case 0x00:	/* ARM_CKCTL */
        return s->clkm.arm_ckctl;

    case 0x04:	/* ARM_IDLECT1 */
        return s->clkm.arm_idlect1;

    case 0x08:	/* ARM_IDLECT2 */
        return s->clkm.arm_idlect2;

    case 0x0c:	/* ARM_EWUPCT */
        return s->clkm.arm_ewupct;

    case 0x10:	/* ARM_RSTCT1 */
        return s->clkm.arm_rstct1;

    case 0x14:	/* ARM_RSTCT2 */
        return s->clkm.arm_rstct2;

    case 0x18:	/* ARM_SYSST */
        return (s->clkm.clocking_scheme << 11) | s->clkm.cold_start;

    case 0x1c:	/* ARM_CKOUT1 */
        return s->clkm.arm_ckout1;

    case 0x20:	/* ARM_CKOUT2 */
        break;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static inline void omap_clkm_ckctl_update(struct omap_mpu_state_s *s,
                uint16_t diff, uint16_t value)
{
    omap_clk clk;

    if (diff & (1 << 14)) {				/* ARM_INTHCK_SEL */
        if (value & (1 << 14))
            /* Reserved */;
        else {
            clk = omap_findclk(s, "arminth_ck");
            omap_clk_reparent(clk, omap_findclk(s, "tc_ck"));
        }
    }
    if (diff & (1 << 12)) {				/* ARM_TIMXO */
        clk = omap_findclk(s, "armtim_ck");
        if (value & (1 << 12))
            omap_clk_reparent(clk, omap_findclk(s, "clkin"));
        else
            omap_clk_reparent(clk, omap_findclk(s, "ck_gen1"));
    }
    /* XXX: en_dspck */
    if (diff & (3 << 10)) {				/* DSPMMUDIV */
        clk = omap_findclk(s, "dspmmu_ck");
        omap_clk_setrate(clk, 1 << ((value >> 10) & 3), 1);
    }
    if (diff & (3 << 8)) {				/* TCDIV */
        clk = omap_findclk(s, "tc_ck");
        omap_clk_setrate(clk, 1 << ((value >> 8) & 3), 1);
    }
    if (diff & (3 << 6)) {				/* DSPDIV */
        clk = omap_findclk(s, "dsp_ck");
        omap_clk_setrate(clk, 1 << ((value >> 6) & 3), 1);
    }
    if (diff & (3 << 4)) {				/* ARMDIV */
        clk = omap_findclk(s, "arm_ck");
        omap_clk_setrate(clk, 1 << ((value >> 4) & 3), 1);
    }
    if (diff & (3 << 2)) {				/* LCDDIV */
        clk = omap_findclk(s, "lcd_ck");
        omap_clk_setrate(clk, 1 << ((value >> 2) & 3), 1);
    }
    if (diff & (3 << 0)) {				/* PERDIV */
        clk = omap_findclk(s, "armper_ck");
        omap_clk_setrate(clk, 1 << ((value >> 0) & 3), 1);
    }
}

static inline void omap_clkm_idlect1_update(struct omap_mpu_state_s *s,
                uint16_t diff, uint16_t value)
{
    omap_clk clk;

    if (value & (1 << 11))				/* SETARM_IDLE */
        cpu_interrupt(s->env, CPU_INTERRUPT_HALT);
    if (!(value & (1 << 10)))				/* WKUP_MODE */
        qemu_system_shutdown_request();	/* XXX: disable wakeup from IRQ */

#define SET_CANIDLE(clock, bit)				\
    if (diff & (1 << bit)) {				\
        clk = omap_findclk(s, clock);			\
        omap_clk_canidle(clk, (value >> bit) & 1);	\
    }
    SET_CANIDLE("mpuwd_ck", 0)				/* IDLWDT_ARM */
    SET_CANIDLE("armxor_ck", 1)				/* IDLXORP_ARM */
    SET_CANIDLE("mpuper_ck", 2)				/* IDLPER_ARM */
    SET_CANIDLE("lcd_ck", 3)				/* IDLLCD_ARM */
    SET_CANIDLE("lb_ck", 4)				/* IDLLB_ARM */
    SET_CANIDLE("hsab_ck", 5)				/* IDLHSAB_ARM */
    SET_CANIDLE("tipb_ck", 6)				/* IDLIF_ARM */
    SET_CANIDLE("dma_ck", 6)				/* IDLIF_ARM */
    SET_CANIDLE("tc_ck", 6)				/* IDLIF_ARM */
    SET_CANIDLE("dpll1", 7)				/* IDLDPLL_ARM */
    SET_CANIDLE("dpll2", 7)				/* IDLDPLL_ARM */
    SET_CANIDLE("dpll3", 7)				/* IDLDPLL_ARM */
    SET_CANIDLE("mpui_ck", 8)				/* IDLAPI_ARM */
    SET_CANIDLE("armtim_ck", 9)				/* IDLTIM_ARM */
}

static inline void omap_clkm_idlect2_update(struct omap_mpu_state_s *s,
                uint16_t diff, uint16_t value)
{
    omap_clk clk;

#define SET_ONOFF(clock, bit)				\
    if (diff & (1 << bit)) {				\
        clk = omap_findclk(s, clock);			\
        omap_clk_onoff(clk, (value >> bit) & 1);	\
    }
    SET_ONOFF("mpuwd_ck", 0)				/* EN_WDTCK */
    SET_ONOFF("armxor_ck", 1)				/* EN_XORPCK */
    SET_ONOFF("mpuper_ck", 2)				/* EN_PERCK */
    SET_ONOFF("lcd_ck", 3)				/* EN_LCDCK */
    SET_ONOFF("lb_ck", 4)				/* EN_LBCK */
    SET_ONOFF("hsab_ck", 5)				/* EN_HSABCK */
    SET_ONOFF("mpui_ck", 6)				/* EN_APICK */
    SET_ONOFF("armtim_ck", 7)				/* EN_TIMCK */
    SET_CANIDLE("dma_ck", 8)				/* DMACK_REQ */
    SET_ONOFF("arm_gpio_ck", 9)				/* EN_GPIOCK */
    SET_ONOFF("lbfree_ck", 10)				/* EN_LBFREECK */
}

static inline void omap_clkm_ckout1_update(struct omap_mpu_state_s *s,
                uint16_t diff, uint16_t value)
{
    omap_clk clk;

    if (diff & (3 << 4)) {				/* TCLKOUT */
        clk = omap_findclk(s, "tclk_out");
        switch ((value >> 4) & 3) {
        case 1:
            omap_clk_reparent(clk, omap_findclk(s, "ck_gen3"));
            omap_clk_onoff(clk, 1);
            break;
        case 2:
            omap_clk_reparent(clk, omap_findclk(s, "tc_ck"));
            omap_clk_onoff(clk, 1);
            break;
        default:
            omap_clk_onoff(clk, 0);
        }
    }
    if (diff & (3 << 2)) {				/* DCLKOUT */
        clk = omap_findclk(s, "dclk_out");
        switch ((value >> 2) & 3) {
        case 0:
            omap_clk_reparent(clk, omap_findclk(s, "dspmmu_ck"));
            break;
        case 1:
            omap_clk_reparent(clk, omap_findclk(s, "ck_gen2"));
            break;
        case 2:
            omap_clk_reparent(clk, omap_findclk(s, "dsp_ck"));
            break;
        case 3:
            omap_clk_reparent(clk, omap_findclk(s, "ck_ref14"));
            break;
        }
    }
    if (diff & (3 << 0)) {				/* ACLKOUT */
        clk = omap_findclk(s, "aclk_out");
        switch ((value >> 0) & 3) {
        case 1:
            omap_clk_reparent(clk, omap_findclk(s, "ck_gen1"));
            omap_clk_onoff(clk, 1);
            break;
        case 2:
            omap_clk_reparent(clk, omap_findclk(s, "arm_ck"));
            omap_clk_onoff(clk, 1);
            break;
        case 3:
            omap_clk_reparent(clk, omap_findclk(s, "ck_ref14"));
            omap_clk_onoff(clk, 1);
            break;
        default:
            omap_clk_onoff(clk, 0);
        }
    }
}

static void omap_clkm_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_mpu_state_s *s = (struct omap_mpu_state_s *) opaque;
    int offset = addr - s->clkm.mpu_base;
    uint16_t diff;
    omap_clk clk;
    static const char *clkschemename[8] = {
        "fully synchronous", "fully asynchronous", "synchronous scalable",
        "mix mode 1", "mix mode 2", "bypass mode", "mix mode 3", "mix mode 4",
    };

    switch (offset) {
    case 0x00:	/* ARM_CKCTL */
        diff = s->clkm.arm_ckctl ^ value;
        s->clkm.arm_ckctl = value & 0x7fff;
        omap_clkm_ckctl_update(s, diff, value);
        return;

    case 0x04:	/* ARM_IDLECT1 */
        diff = s->clkm.arm_idlect1 ^ value;
        s->clkm.arm_idlect1 = value & 0x0fff;
        omap_clkm_idlect1_update(s, diff, value);
        return;

    case 0x08:	/* ARM_IDLECT2 */
        diff = s->clkm.arm_idlect2 ^ value;
        s->clkm.arm_idlect2 = value & 0x07ff;
        omap_clkm_idlect2_update(s, diff, value);
        return;

    case 0x0c:	/* ARM_EWUPCT */
        diff = s->clkm.arm_ewupct ^ value;
        s->clkm.arm_ewupct = value & 0x003f;
        return;

    case 0x10:	/* ARM_RSTCT1 */
        diff = s->clkm.arm_rstct1 ^ value;
        s->clkm.arm_rstct1 = value & 0x0007;
        if (value & 9) {
            qemu_system_reset_request();
            s->clkm.cold_start = 0xa;
        }
        if (diff & ~value & 4) {				/* DSP_RST */
            omap_mpui_reset(s);
            omap_tipb_bridge_reset(s->private_tipb);
            omap_tipb_bridge_reset(s->public_tipb);
        }
        if (diff & 2) {						/* DSP_EN */
            clk = omap_findclk(s, "dsp_ck");
            omap_clk_canidle(clk, (~value >> 1) & 1);
        }
        return;

    case 0x14:	/* ARM_RSTCT2 */
        s->clkm.arm_rstct2 = value & 0x0001;
        return;

    case 0x18:	/* ARM_SYSST */
        if ((s->clkm.clocking_scheme ^ (value >> 11)) & 7) {
            s->clkm.clocking_scheme = (value >> 11) & 7;
            printf("%s: clocking scheme set to %s\n", __FUNCTION__,
                            clkschemename[s->clkm.clocking_scheme]);
        }
        s->clkm.cold_start &= value & 0x3f;
        return;

    case 0x1c:	/* ARM_CKOUT1 */
        diff = s->clkm.arm_ckout1 ^ value;
        s->clkm.arm_ckout1 = value & 0x003f;
        omap_clkm_ckout1_update(s, diff, value);
        return;

    case 0x20:	/* ARM_CKOUT2 */
    default:
        OMAP_BAD_REG(addr);
    }
}

static CPUReadMemoryFunc *omap_clkm_readfn[] = {
    omap_badwidth_read16,
    omap_clkm_read,
    omap_badwidth_read16,
};

static CPUWriteMemoryFunc *omap_clkm_writefn[] = {
    omap_badwidth_write16,
    omap_clkm_write,
    omap_badwidth_write16,
};

static uint32_t omap_clkdsp_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_mpu_state_s *s = (struct omap_mpu_state_s *) opaque;
    int offset = addr - s->clkm.dsp_base;

    switch (offset) {
    case 0x04:	/* DSP_IDLECT1 */
        return s->clkm.dsp_idlect1;

    case 0x08:	/* DSP_IDLECT2 */
        return s->clkm.dsp_idlect2;

    case 0x14:	/* DSP_RSTCT2 */
        return s->clkm.dsp_rstct2;

    case 0x18:	/* DSP_SYSST */
        return (s->clkm.clocking_scheme << 11) | s->clkm.cold_start |
                (s->env->halted << 6);	/* Quite useless... */
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static inline void omap_clkdsp_idlect1_update(struct omap_mpu_state_s *s,
                uint16_t diff, uint16_t value)
{
    omap_clk clk;

    SET_CANIDLE("dspxor_ck", 1);			/* IDLXORP_DSP */
}

static inline void omap_clkdsp_idlect2_update(struct omap_mpu_state_s *s,
                uint16_t diff, uint16_t value)
{
    omap_clk clk;

    SET_ONOFF("dspxor_ck", 1);				/* EN_XORPCK */
}

static void omap_clkdsp_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_mpu_state_s *s = (struct omap_mpu_state_s *) opaque;
    int offset = addr - s->clkm.dsp_base;
    uint16_t diff;

    switch (offset) {
    case 0x04:	/* DSP_IDLECT1 */
        diff = s->clkm.dsp_idlect1 ^ value;
        s->clkm.dsp_idlect1 = value & 0x01f7;
        omap_clkdsp_idlect1_update(s, diff, value);
        break;

    case 0x08:	/* DSP_IDLECT2 */
        s->clkm.dsp_idlect2 = value & 0x0037;
        diff = s->clkm.dsp_idlect1 ^ value;
        omap_clkdsp_idlect2_update(s, diff, value);
        break;

    case 0x14:	/* DSP_RSTCT2 */
        s->clkm.dsp_rstct2 = value & 0x0001;
        break;

    case 0x18:	/* DSP_SYSST */
        s->clkm.cold_start &= value & 0x3f;
        break;

    default:
        OMAP_BAD_REG(addr);
    }
}

static CPUReadMemoryFunc *omap_clkdsp_readfn[] = {
    omap_badwidth_read16,
    omap_clkdsp_read,
    omap_badwidth_read16,
};

static CPUWriteMemoryFunc *omap_clkdsp_writefn[] = {
    omap_badwidth_write16,
    omap_clkdsp_write,
    omap_badwidth_write16,
};

static void omap_clkm_reset(struct omap_mpu_state_s *s)
{
    if (s->wdt && s->wdt->reset)
        s->clkm.cold_start = 0x6;
    s->clkm.clocking_scheme = 0;
    omap_clkm_ckctl_update(s, ~0, 0x3000);
    s->clkm.arm_ckctl = 0x3000;
    omap_clkm_idlect1_update(s, s->clkm.arm_idlect1 ^ 0x0400, 0x0400);
    s->clkm.arm_idlect1 = 0x0400;
    omap_clkm_idlect2_update(s, s->clkm.arm_idlect2 ^ 0x0100, 0x0100);
    s->clkm.arm_idlect2 = 0x0100;
    s->clkm.arm_ewupct = 0x003f;
    s->clkm.arm_rstct1 = 0x0000;
    s->clkm.arm_rstct2 = 0x0000;
    s->clkm.arm_ckout1 = 0x0015;
    s->clkm.dpll1_mode = 0x2002;
    omap_clkdsp_idlect1_update(s, s->clkm.dsp_idlect1 ^ 0x0040, 0x0040);
    s->clkm.dsp_idlect1 = 0x0040;
    omap_clkdsp_idlect2_update(s, ~0, 0x0000);
    s->clkm.dsp_idlect2 = 0x0000;
    s->clkm.dsp_rstct2 = 0x0000;
}

static void omap_clkm_init(target_phys_addr_t mpu_base,
                target_phys_addr_t dsp_base, struct omap_mpu_state_s *s)
{
    int iomemtype[2] = {
        cpu_register_io_memory(0, omap_clkm_readfn, omap_clkm_writefn, s),
        cpu_register_io_memory(0, omap_clkdsp_readfn, omap_clkdsp_writefn, s),
    };

    s->clkm.mpu_base = mpu_base;
    s->clkm.dsp_base = dsp_base;
    s->clkm.arm_idlect1 = 0x03ff;
    s->clkm.arm_idlect2 = 0x0100;
    s->clkm.dsp_idlect1 = 0x0002;
    omap_clkm_reset(s);
    s->clkm.cold_start = 0x3a;

    cpu_register_physical_memory(s->clkm.mpu_base, 0x100, iomemtype[0]);
    cpu_register_physical_memory(s->clkm.dsp_base, 0x1000, iomemtype[1]);
}

/* MPU I/O */
struct omap_mpuio_s {
    target_phys_addr_t base;
    qemu_irq irq;
    qemu_irq kbd_irq;
    qemu_irq *in;
    qemu_irq handler[16];
    qemu_irq wakeup;

    uint16_t inputs;
    uint16_t outputs;
    uint16_t dir;
    uint16_t edge;
    uint16_t mask;
    uint16_t ints;

    uint16_t debounce;
    uint16_t latch;
    uint8_t event;

    uint8_t buttons[5];
    uint8_t row_latch;
    uint8_t cols;
    int kbd_mask;
    int clk;
};

static void omap_mpuio_set(void *opaque, int line, int level)
{
    struct omap_mpuio_s *s = (struct omap_mpuio_s *) opaque;
    uint16_t prev = s->inputs;

    if (level)
        s->inputs |= 1 << line;
    else
        s->inputs &= ~(1 << line);

    if (((1 << line) & s->dir & ~s->mask) && s->clk) {
        if ((s->edge & s->inputs & ~prev) | (~s->edge & ~s->inputs & prev)) {
            s->ints |= 1 << line;
            qemu_irq_raise(s->irq);
            /* TODO: wakeup */
        }
        if ((s->event & (1 << 0)) &&		/* SET_GPIO_EVENT_MODE */
                (s->event >> 1) == line)	/* PIN_SELECT */
            s->latch = s->inputs;
    }
}

static void omap_mpuio_kbd_update(struct omap_mpuio_s *s)
{
    int i;
    uint8_t *row, rows = 0, cols = ~s->cols;

    for (row = s->buttons + 4, i = 1 << 4; i; row --, i >>= 1)
        if (*row & cols)
            rows |= i;

    qemu_set_irq(s->kbd_irq, rows && ~s->kbd_mask && s->clk);
    s->row_latch = rows ^ 0x1f;
}

static uint32_t omap_mpuio_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_mpuio_s *s = (struct omap_mpuio_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;
    uint16_t ret;

    switch (offset) {
    case 0x00:	/* INPUT_LATCH */
        return s->inputs;

    case 0x04:	/* OUTPUT_REG */
        return s->outputs;

    case 0x08:	/* IO_CNTL */
        return s->dir;

    case 0x10:	/* KBR_LATCH */
        return s->row_latch;

    case 0x14:	/* KBC_REG */
        return s->cols;

    case 0x18:	/* GPIO_EVENT_MODE_REG */
        return s->event;

    case 0x1c:	/* GPIO_INT_EDGE_REG */
        return s->edge;

    case 0x20:	/* KBD_INT */
        return (s->row_latch != 0x1f) && !s->kbd_mask;

    case 0x24:	/* GPIO_INT */
        ret = s->ints;
        s->ints &= s->mask;
        if (ret)
            qemu_irq_lower(s->irq);
        return ret;

    case 0x28:	/* KBD_MASKIT */
        return s->kbd_mask;

    case 0x2c:	/* GPIO_MASKIT */
        return s->mask;

    case 0x30:	/* GPIO_DEBOUNCING_REG */
        return s->debounce;

    case 0x34:	/* GPIO_LATCH_REG */
        return s->latch;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_mpuio_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_mpuio_s *s = (struct omap_mpuio_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;
    uint16_t diff;
    int ln;

    switch (offset) {
    case 0x04:	/* OUTPUT_REG */
        diff = (s->outputs ^ value) & ~s->dir;
        s->outputs = value;
        while ((ln = ffs(diff))) {
            ln --;
            if (s->handler[ln])
                qemu_set_irq(s->handler[ln], (value >> ln) & 1);
            diff &= ~(1 << ln);
        }
        break;

    case 0x08:	/* IO_CNTL */
        diff = s->outputs & (s->dir ^ value);
        s->dir = value;

        value = s->outputs & ~s->dir;
        while ((ln = ffs(diff))) {
            ln --;
            if (s->handler[ln])
                qemu_set_irq(s->handler[ln], (value >> ln) & 1);
            diff &= ~(1 << ln);
        }
        break;

    case 0x14:	/* KBC_REG */
        s->cols = value;
        omap_mpuio_kbd_update(s);
        break;

    case 0x18:	/* GPIO_EVENT_MODE_REG */
        s->event = value & 0x1f;
        break;

    case 0x1c:	/* GPIO_INT_EDGE_REG */
        s->edge = value;
        break;

    case 0x28:	/* KBD_MASKIT */
        s->kbd_mask = value & 1;
        omap_mpuio_kbd_update(s);
        break;

    case 0x2c:	/* GPIO_MASKIT */
        s->mask = value;
        break;

    case 0x30:	/* GPIO_DEBOUNCING_REG */
        s->debounce = value & 0x1ff;
        break;

    case 0x00:	/* INPUT_LATCH */
    case 0x10:	/* KBR_LATCH */
    case 0x20:	/* KBD_INT */
    case 0x24:	/* GPIO_INT */
    case 0x34:	/* GPIO_LATCH_REG */
        OMAP_RO_REG(addr);
        return;

    default:
        OMAP_BAD_REG(addr);
        return;
    }
}

static CPUReadMemoryFunc *omap_mpuio_readfn[] = {
    omap_badwidth_read16,
    omap_mpuio_read,
    omap_badwidth_read16,
};

static CPUWriteMemoryFunc *omap_mpuio_writefn[] = {
    omap_badwidth_write16,
    omap_mpuio_write,
    omap_badwidth_write16,
};

void omap_mpuio_reset(struct omap_mpuio_s *s)
{
    s->inputs = 0;
    s->outputs = 0;
    s->dir = ~0;
    s->event = 0;
    s->edge = 0;
    s->kbd_mask = 0;
    s->mask = 0;
    s->debounce = 0;
    s->latch = 0;
    s->ints = 0;
    s->row_latch = 0x1f;
    s->clk = 1;
}

static void omap_mpuio_onoff(void *opaque, int line, int on)
{
    struct omap_mpuio_s *s = (struct omap_mpuio_s *) opaque;

    s->clk = on;
    if (on)
        omap_mpuio_kbd_update(s);
}

struct omap_mpuio_s *omap_mpuio_init(target_phys_addr_t base,
                qemu_irq kbd_int, qemu_irq gpio_int, qemu_irq wakeup,
                omap_clk clk)
{
    int iomemtype;
    struct omap_mpuio_s *s = (struct omap_mpuio_s *)
            qemu_mallocz(sizeof(struct omap_mpuio_s));

    s->base = base;
    s->irq = gpio_int;
    s->kbd_irq = kbd_int;
    s->wakeup = wakeup;
    s->in = qemu_allocate_irqs(omap_mpuio_set, s, 16);
    omap_mpuio_reset(s);

    iomemtype = cpu_register_io_memory(0, omap_mpuio_readfn,
                    omap_mpuio_writefn, s);
    cpu_register_physical_memory(s->base, 0x800, iomemtype);

    omap_clk_adduser(clk, qemu_allocate_irqs(omap_mpuio_onoff, s, 1)[0]);

    return s;
}

qemu_irq *omap_mpuio_in_get(struct omap_mpuio_s *s)
{
    return s->in;
}

void omap_mpuio_out_set(struct omap_mpuio_s *s, int line, qemu_irq handler)
{
    if (line >= 16 || line < 0)
        cpu_abort(cpu_single_env, "%s: No GPIO line %i\n", __FUNCTION__, line);
    s->handler[line] = handler;
}

void omap_mpuio_key(struct omap_mpuio_s *s, int row, int col, int down)
{
    if (row >= 5 || row < 0)
        cpu_abort(cpu_single_env, "%s: No key %i-%i\n",
                        __FUNCTION__, col, row);

    if (down)
        s->buttons[row] |= 1 << col;
    else
        s->buttons[row] &= ~(1 << col);

    omap_mpuio_kbd_update(s);
}

/* General-Purpose I/O */
struct omap_gpio_s {
    target_phys_addr_t base;
    qemu_irq irq;
    qemu_irq *in;
    qemu_irq handler[16];

    uint16_t inputs;
    uint16_t outputs;
    uint16_t dir;
    uint16_t edge;
    uint16_t mask;
    uint16_t ints;
    uint16_t pins;
};

static void omap_gpio_set(void *opaque, int line, int level)
{
    struct omap_gpio_s *s = (struct omap_gpio_s *) opaque;
    uint16_t prev = s->inputs;

    if (level)
        s->inputs |= 1 << line;
    else
        s->inputs &= ~(1 << line);

    if (((s->edge & s->inputs & ~prev) | (~s->edge & ~s->inputs & prev)) &
                    (1 << line) & s->dir & ~s->mask) {
        s->ints |= 1 << line;
        qemu_irq_raise(s->irq);
    }
}

static uint32_t omap_gpio_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_gpio_s *s = (struct omap_gpio_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;

    switch (offset) {
    case 0x00:	/* DATA_INPUT */
        return s->inputs & s->pins;

    case 0x04:	/* DATA_OUTPUT */
        return s->outputs;

    case 0x08:	/* DIRECTION_CONTROL */
        return s->dir;

    case 0x0c:	/* INTERRUPT_CONTROL */
        return s->edge;

    case 0x10:	/* INTERRUPT_MASK */
        return s->mask;

    case 0x14:	/* INTERRUPT_STATUS */
        return s->ints;

    case 0x18:	/* PIN_CONTROL (not in OMAP310) */
        OMAP_BAD_REG(addr);
        return s->pins;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_gpio_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_gpio_s *s = (struct omap_gpio_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;
    uint16_t diff;
    int ln;

    switch (offset) {
    case 0x00:	/* DATA_INPUT */
        OMAP_RO_REG(addr);
        return;

    case 0x04:	/* DATA_OUTPUT */
        diff = (s->outputs ^ value) & ~s->dir;
        s->outputs = value;
        while ((ln = ffs(diff))) {
            ln --;
            if (s->handler[ln])
                qemu_set_irq(s->handler[ln], (value >> ln) & 1);
            diff &= ~(1 << ln);
        }
        break;

    case 0x08:	/* DIRECTION_CONTROL */
        diff = s->outputs & (s->dir ^ value);
        s->dir = value;

        value = s->outputs & ~s->dir;
        while ((ln = ffs(diff))) {
            ln --;
            if (s->handler[ln])
                qemu_set_irq(s->handler[ln], (value >> ln) & 1);
            diff &= ~(1 << ln);
        }
        break;

    case 0x0c:	/* INTERRUPT_CONTROL */
        s->edge = value;
        break;

    case 0x10:	/* INTERRUPT_MASK */
        s->mask = value;
        break;

    case 0x14:	/* INTERRUPT_STATUS */
        s->ints &= ~value;
        if (!s->ints)
            qemu_irq_lower(s->irq);
        break;

    case 0x18:	/* PIN_CONTROL (not in OMAP310 TRM) */
        OMAP_BAD_REG(addr);
        s->pins = value;
        break;

    default:
        OMAP_BAD_REG(addr);
        return;
    }
}

/* *Some* sources say the memory region is 32-bit.  */
static CPUReadMemoryFunc *omap_gpio_readfn[] = {
    omap_badwidth_read16,
    omap_gpio_read,
    omap_badwidth_read16,
};

static CPUWriteMemoryFunc *omap_gpio_writefn[] = {
    omap_badwidth_write16,
    omap_gpio_write,
    omap_badwidth_write16,
};

void omap_gpio_reset(struct omap_gpio_s *s)
{
    s->inputs = 0;
    s->outputs = ~0;
    s->dir = ~0;
    s->edge = ~0;
    s->mask = ~0;
    s->ints = 0;
    s->pins = ~0;
}

struct omap_gpio_s *omap_gpio_init(target_phys_addr_t base,
                qemu_irq irq, omap_clk clk)
{
    int iomemtype;
    struct omap_gpio_s *s = (struct omap_gpio_s *)
            qemu_mallocz(sizeof(struct omap_gpio_s));

    s->base = base;
    s->irq = irq;
    s->in = qemu_allocate_irqs(omap_gpio_set, s, 16);
    omap_gpio_reset(s);

    iomemtype = cpu_register_io_memory(0, omap_gpio_readfn,
                    omap_gpio_writefn, s);
    cpu_register_physical_memory(s->base, 0x1000, iomemtype);

    return s;
}

qemu_irq *omap_gpio_in_get(struct omap_gpio_s *s)
{
    return s->in;
}

void omap_gpio_out_set(struct omap_gpio_s *s, int line, qemu_irq handler)
{
    if (line >= 16 || line < 0)
        cpu_abort(cpu_single_env, "%s: No GPIO line %i\n", __FUNCTION__, line);
    s->handler[line] = handler;
}

/* MicroWire Interface */
struct omap_uwire_s {
    target_phys_addr_t base;
    qemu_irq txirq;
    qemu_irq rxirq;
    qemu_irq txdrq;

    uint16_t txbuf;
    uint16_t rxbuf;
    uint16_t control;
    uint16_t setup[5];

    struct uwire_slave_s *chip[4];
};

static void omap_uwire_transfer_start(struct omap_uwire_s *s)
{
    int chipselect = (s->control >> 10) & 3;		/* INDEX */
    struct uwire_slave_s *slave = s->chip[chipselect];

    if ((s->control >> 5) & 0x1f) {			/* NB_BITS_WR */
        if (s->control & (1 << 12))			/* CS_CMD */
            if (slave && slave->send)
                slave->send(slave->opaque,
                                s->txbuf >> (16 - ((s->control >> 5) & 0x1f)));
        s->control &= ~(1 << 14);			/* CSRB */
        /* TODO: depending on s->setup[4] bits [1:0] assert an IRQ or
         * a DRQ.  When is the level IRQ supposed to be reset?  */
    }

    if ((s->control >> 0) & 0x1f) {			/* NB_BITS_RD */
        if (s->control & (1 << 12))			/* CS_CMD */
            if (slave && slave->receive)
                s->rxbuf = slave->receive(slave->opaque);
        s->control |= 1 << 15;				/* RDRB */
        /* TODO: depending on s->setup[4] bits [1:0] assert an IRQ or
         * a DRQ.  When is the level IRQ supposed to be reset?  */
    }
}

static uint32_t omap_uwire_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_uwire_s *s = (struct omap_uwire_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;

    switch (offset) {
    case 0x00:	/* RDR */
        s->control &= ~(1 << 15);			/* RDRB */
        return s->rxbuf;

    case 0x04:	/* CSR */
        return s->control;

    case 0x08:	/* SR1 */
        return s->setup[0];
    case 0x0c:	/* SR2 */
        return s->setup[1];
    case 0x10:	/* SR3 */
        return s->setup[2];
    case 0x14:	/* SR4 */
        return s->setup[3];
    case 0x18:	/* SR5 */
        return s->setup[4];
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_uwire_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_uwire_s *s = (struct omap_uwire_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;

    switch (offset) {
    case 0x00:	/* TDR */
        s->txbuf = value;				/* TD */
        if ((s->setup[4] & (1 << 2)) &&			/* AUTO_TX_EN */
                        ((s->setup[4] & (1 << 3)) ||	/* CS_TOGGLE_TX_EN */
                         (s->control & (1 << 12)))) {	/* CS_CMD */
            s->control |= 1 << 14;			/* CSRB */
            omap_uwire_transfer_start(s);
        }
        break;

    case 0x04:	/* CSR */
        s->control = value & 0x1fff;
        if (value & (1 << 13))				/* START */
            omap_uwire_transfer_start(s);
        break;

    case 0x08:	/* SR1 */
        s->setup[0] = value & 0x003f;
        break;

    case 0x0c:	/* SR2 */
        s->setup[1] = value & 0x0fc0;
        break;

    case 0x10:	/* SR3 */
        s->setup[2] = value & 0x0003;
        break;

    case 0x14:	/* SR4 */
        s->setup[3] = value & 0x0001;
        break;

    case 0x18:	/* SR5 */
        s->setup[4] = value & 0x000f;
        break;

    default:
        OMAP_BAD_REG(addr);
        return;
    }
}

static CPUReadMemoryFunc *omap_uwire_readfn[] = {
    omap_badwidth_read16,
    omap_uwire_read,
    omap_badwidth_read16,
};

static CPUWriteMemoryFunc *omap_uwire_writefn[] = {
    omap_badwidth_write16,
    omap_uwire_write,
    omap_badwidth_write16,
};

void omap_uwire_reset(struct omap_uwire_s *s)
{
    s->control = 0;
    s->setup[0] = 0;
    s->setup[1] = 0;
    s->setup[2] = 0;
    s->setup[3] = 0;
    s->setup[4] = 0;
}

struct omap_uwire_s *omap_uwire_init(target_phys_addr_t base,
                qemu_irq *irq, qemu_irq dma, omap_clk clk)
{
    int iomemtype;
    struct omap_uwire_s *s = (struct omap_uwire_s *)
            qemu_mallocz(sizeof(struct omap_uwire_s));

    s->base = base;
    s->txirq = irq[0];
    s->rxirq = irq[1];
    s->txdrq = dma;
    omap_uwire_reset(s);

    iomemtype = cpu_register_io_memory(0, omap_uwire_readfn,
                    omap_uwire_writefn, s);
    cpu_register_physical_memory(s->base, 0x800, iomemtype);

    return s;
}

void omap_uwire_attach(struct omap_uwire_s *s,
                struct uwire_slave_s *slave, int chipselect)
{
    if (chipselect < 0 || chipselect > 3)
        cpu_abort(cpu_single_env, "%s: Bad chipselect %i\n", __FUNCTION__,
                        chipselect);

    s->chip[chipselect] = slave;
}

/* Pseudonoise Pulse-Width Light Modulator */
void omap_pwl_update(struct omap_mpu_state_s *s)
{
    int output = (s->pwl.clk && s->pwl.enable) ? s->pwl.level : 0;

    if (output != s->pwl.output) {
        s->pwl.output = output;
        printf("%s: Backlight now at %i/256\n", __FUNCTION__, output);
    }
}

static uint32_t omap_pwl_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_mpu_state_s *s = (struct omap_mpu_state_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;

    switch (offset) {
    case 0x00:	/* PWL_LEVEL */
        return s->pwl.level;
    case 0x04:	/* PWL_CTRL */
        return s->pwl.enable;
    }
    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_pwl_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_mpu_state_s *s = (struct omap_mpu_state_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;

    switch (offset) {
    case 0x00:	/* PWL_LEVEL */
        s->pwl.level = value;
        omap_pwl_update(s);
        break;
    case 0x04:	/* PWL_CTRL */
        s->pwl.enable = value & 1;
        omap_pwl_update(s);
        break;
    default:
        OMAP_BAD_REG(addr);
        return;
    }
}

static CPUReadMemoryFunc *omap_pwl_readfn[] = {
    omap_pwl_read,
    omap_badwidth_read8,
    omap_badwidth_read8,
};

static CPUWriteMemoryFunc *omap_pwl_writefn[] = {
    omap_pwl_write,
    omap_badwidth_write8,
    omap_badwidth_write8,
};

void omap_pwl_reset(struct omap_mpu_state_s *s)
{
    s->pwl.output = 0;
    s->pwl.level = 0;
    s->pwl.enable = 0;
    s->pwl.clk = 1;
    omap_pwl_update(s);
}

static void omap_pwl_clk_update(void *opaque, int line, int on)
{
    struct omap_mpu_state_s *s = (struct omap_mpu_state_s *) opaque;

    s->pwl.clk = on;
    omap_pwl_update(s);
}

static void omap_pwl_init(target_phys_addr_t base, struct omap_mpu_state_s *s,
                omap_clk clk)
{
    int iomemtype;

    omap_pwl_reset(s);

    iomemtype = cpu_register_io_memory(0, omap_pwl_readfn,
                    omap_pwl_writefn, s);
    cpu_register_physical_memory(base, 0x800, iomemtype);

    omap_clk_adduser(clk, qemu_allocate_irqs(omap_pwl_clk_update, s, 1)[0]);
}

/* Pulse-Width Tone module */
static uint32_t omap_pwt_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_mpu_state_s *s = (struct omap_mpu_state_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;

    switch (offset) {
    case 0x00:	/* FRC */
        return s->pwt.frc;
    case 0x04:	/* VCR */
        return s->pwt.vrc;
    case 0x08:	/* GCR */
        return s->pwt.gcr;
    }
    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_pwt_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_mpu_state_s *s = (struct omap_mpu_state_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;

    switch (offset) {
    case 0x00:	/* FRC */
        s->pwt.frc = value & 0x3f;
        break;
    case 0x04:	/* VRC */
        if ((value ^ s->pwt.vrc) & 1) {
            if (value & 1)
                printf("%s: %iHz buzz on\n", __FUNCTION__, (int)
                                /* 1.5 MHz from a 12-MHz or 13-MHz PWT_CLK */
                                ((omap_clk_getrate(s->pwt.clk) >> 3) /
                                 /* Pre-multiplexer divider */
                                 ((s->pwt.gcr & 2) ? 1 : 154) /
                                 /* Octave multiplexer */
                                 (2 << (value & 3)) *
                                 /* 101/107 divider */
                                 ((value & (1 << 2)) ? 101 : 107) *
                                 /*  49/55 divider */
                                 ((value & (1 << 3)) ?  49 : 55) *
                                 /*  50/63 divider */
                                 ((value & (1 << 4)) ?  50 : 63) *
                                 /*  80/127 divider */
                                 ((value & (1 << 5)) ?  80 : 127) /
                                 (107 * 55 * 63 * 127)));
            else
                printf("%s: silence!\n", __FUNCTION__);
        }
        s->pwt.vrc = value & 0x7f;
        break;
    case 0x08:	/* GCR */
        s->pwt.gcr = value & 3;
        break;
    default:
        OMAP_BAD_REG(addr);
        return;
    }
}

static CPUReadMemoryFunc *omap_pwt_readfn[] = {
    omap_pwt_read,
    omap_badwidth_read8,
    omap_badwidth_read8,
};

static CPUWriteMemoryFunc *omap_pwt_writefn[] = {
    omap_pwt_write,
    omap_badwidth_write8,
    omap_badwidth_write8,
};

void omap_pwt_reset(struct omap_mpu_state_s *s)
{
    s->pwt.frc = 0;
    s->pwt.vrc = 0;
    s->pwt.gcr = 0;
}

static void omap_pwt_init(target_phys_addr_t base, struct omap_mpu_state_s *s,
                omap_clk clk)
{
    int iomemtype;

    s->pwt.clk = clk;
    omap_pwt_reset(s);

    iomemtype = cpu_register_io_memory(0, omap_pwt_readfn,
                    omap_pwt_writefn, s);
    cpu_register_physical_memory(base, 0x800, iomemtype);
}

/* Real-time Clock module */
struct omap_rtc_s {
    target_phys_addr_t base;
    qemu_irq irq;
    qemu_irq alarm;
    QEMUTimer *clk;

    uint8_t interrupts;
    uint8_t status;
    int16_t comp_reg;
    int running;
    int pm_am;
    int auto_comp;
    int round;
    struct tm *(*convert)(const time_t *timep, struct tm *result);
    struct tm alarm_tm;
    time_t alarm_ti;

    struct tm current_tm;
    time_t ti;
    uint64_t tick;
};

static void omap_rtc_interrupts_update(struct omap_rtc_s *s)
{
    qemu_set_irq(s->alarm, (s->status >> 6) & 1);
}

static void omap_rtc_alarm_update(struct omap_rtc_s *s)
{
    s->alarm_ti = mktime(&s->alarm_tm);
    if (s->alarm_ti == -1)
        printf("%s: conversion failed\n", __FUNCTION__);
}

static inline uint8_t omap_rtc_bcd(int num)
{
    return ((num / 10) << 4) | (num % 10);
}

static inline int omap_rtc_bin(uint8_t num)
{
    return (num & 15) + 10 * (num >> 4);
}

static uint32_t omap_rtc_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_rtc_s *s = (struct omap_rtc_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;
    uint8_t i;

    switch (offset) {
    case 0x00:	/* SECONDS_REG */
        return omap_rtc_bcd(s->current_tm.tm_sec);

    case 0x04:	/* MINUTES_REG */
        return omap_rtc_bcd(s->current_tm.tm_min);

    case 0x08:	/* HOURS_REG */
        if (s->pm_am)
            return ((s->current_tm.tm_hour > 11) << 7) |
                    omap_rtc_bcd(((s->current_tm.tm_hour - 1) % 12) + 1);
        else
            return omap_rtc_bcd(s->current_tm.tm_hour);

    case 0x0c:	/* DAYS_REG */
        return omap_rtc_bcd(s->current_tm.tm_mday);

    case 0x10:	/* MONTHS_REG */
        return omap_rtc_bcd(s->current_tm.tm_mon + 1);

    case 0x14:	/* YEARS_REG */
        return omap_rtc_bcd(s->current_tm.tm_year % 100);

    case 0x18:	/* WEEK_REG */
        return s->current_tm.tm_wday;

    case 0x20:	/* ALARM_SECONDS_REG */
        return omap_rtc_bcd(s->alarm_tm.tm_sec);

    case 0x24:	/* ALARM_MINUTES_REG */
        return omap_rtc_bcd(s->alarm_tm.tm_min);

    case 0x28:	/* ALARM_HOURS_REG */
        if (s->pm_am)
            return ((s->alarm_tm.tm_hour > 11) << 7) |
                    omap_rtc_bcd(((s->alarm_tm.tm_hour - 1) % 12) + 1);
        else
            return omap_rtc_bcd(s->alarm_tm.tm_hour);

    case 0x2c:	/* ALARM_DAYS_REG */
        return omap_rtc_bcd(s->alarm_tm.tm_mday);

    case 0x30:	/* ALARM_MONTHS_REG */
        return omap_rtc_bcd(s->alarm_tm.tm_mon + 1);

    case 0x34:	/* ALARM_YEARS_REG */
        return omap_rtc_bcd(s->alarm_tm.tm_year % 100);

    case 0x40:	/* RTC_CTRL_REG */
        return (s->pm_am << 3) | (s->auto_comp << 2) |
                (s->round << 1) | s->running;

    case 0x44:	/* RTC_STATUS_REG */
        i = s->status;
        s->status &= ~0x3d;
        return i;

    case 0x48:	/* RTC_INTERRUPTS_REG */
        return s->interrupts;

    case 0x4c:	/* RTC_COMP_LSB_REG */
        return ((uint16_t) s->comp_reg) & 0xff;

    case 0x50:	/* RTC_COMP_MSB_REG */
        return ((uint16_t) s->comp_reg) >> 8;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_rtc_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_rtc_s *s = (struct omap_rtc_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;
    struct tm new_tm;
    time_t ti[2];

    switch (offset) {
    case 0x00:	/* SECONDS_REG */
#if ALMDEBUG
        printf("RTC SEC_REG <-- %02x\n", value);
#endif
        s->ti -= s->current_tm.tm_sec;
        s->ti += omap_rtc_bin(value);
        return;

    case 0x04:	/* MINUTES_REG */
#if ALMDEBUG
        printf("RTC MIN_REG <-- %02x\n", value);
#endif
        s->ti -= s->current_tm.tm_min * 60;
        s->ti += omap_rtc_bin(value) * 60;
        return;

    case 0x08:	/* HOURS_REG */
#if ALMDEBUG
        printf("RTC HRS_REG <-- %02x\n", value);
#endif
        s->ti -= s->current_tm.tm_hour * 3600;
        if (s->pm_am) {
            s->ti += (omap_rtc_bin(value & 0x3f) & 12) * 3600;
            s->ti += ((value >> 7) & 1) * 43200;
        } else
            s->ti += omap_rtc_bin(value & 0x3f) * 3600;
        return;

    case 0x0c:	/* DAYS_REG */
#if ALMDEBUG
        printf("RTC DAY_REG <-- %02x\n", value);
#endif
        s->ti -= s->current_tm.tm_mday * 86400;
        s->ti += omap_rtc_bin(value) * 86400;
        return;

    case 0x10:	/* MONTHS_REG */
#if ALMDEBUG
        printf("RTC MTH_REG <-- %02x\n", value);
#endif
        memcpy(&new_tm, &s->current_tm, sizeof(new_tm));
        new_tm.tm_mon = omap_rtc_bin(value);
        ti[0] = mktime(&s->current_tm);
        ti[1] = mktime(&new_tm);

        if (ti[0] != -1 && ti[1] != -1) {
            s->ti -= ti[0];
            s->ti += ti[1];
        } else {
            /* A less accurate version */
            s->ti -= s->current_tm.tm_mon * 2592000;
            s->ti += omap_rtc_bin(value) * 2592000;
        }
        return;

    case 0x14:	/* YEARS_REG */
#if ALMDEBUG
        printf("RTC YRS_REG <-- %02x\n", value);
#endif
        memcpy(&new_tm, &s->current_tm, sizeof(new_tm));
        new_tm.tm_year += omap_rtc_bin(value) - (new_tm.tm_year % 100);
        ti[0] = mktime(&s->current_tm);
        ti[1] = mktime(&new_tm);

        if (ti[0] != -1 && ti[1] != -1) {
            s->ti -= ti[0];
            s->ti += ti[1];
        } else {
            /* A less accurate version */
            s->ti -= (s->current_tm.tm_year % 100) * 31536000;
            s->ti += omap_rtc_bin(value) * 31536000;
        }
        return;

    case 0x18:	/* WEEK_REG */
        return;	/* Ignored */

    case 0x20:	/* ALARM_SECONDS_REG */
#if ALMDEBUG
        printf("ALM SEC_REG <-- %02x\n", value);
#endif
        s->alarm_tm.tm_sec = omap_rtc_bin(value);
        omap_rtc_alarm_update(s);
        return;

    case 0x24:	/* ALARM_MINUTES_REG */
#if ALMDEBUG
        printf("ALM MIN_REG <-- %02x\n", value);
#endif
        s->alarm_tm.tm_min = omap_rtc_bin(value);
        omap_rtc_alarm_update(s);
        return;

    case 0x28:	/* ALARM_HOURS_REG */
#if ALMDEBUG
        printf("ALM HRS_REG <-- %02x\n", value);
#endif
        if (s->pm_am)
            s->alarm_tm.tm_hour =
                    ((omap_rtc_bin(value & 0x3f)) % 12) +
                    ((value >> 7) & 1) * 12;
        else
            s->alarm_tm.tm_hour = omap_rtc_bin(value);
        omap_rtc_alarm_update(s);
        return;

    case 0x2c:	/* ALARM_DAYS_REG */
#if ALMDEBUG
        printf("ALM DAY_REG <-- %02x\n", value);
#endif
        s->alarm_tm.tm_mday = omap_rtc_bin(value);
        omap_rtc_alarm_update(s);
        return;

    case 0x30:	/* ALARM_MONTHS_REG */
#if ALMDEBUG
        printf("ALM MON_REG <-- %02x\n", value);
#endif
        s->alarm_tm.tm_mon = omap_rtc_bin(value);
        omap_rtc_alarm_update(s);
        return;

    case 0x34:	/* ALARM_YEARS_REG */
#if ALMDEBUG
        printf("ALM YRS_REG <-- %02x\n", value);
#endif
        s->alarm_tm.tm_year = omap_rtc_bin(value);
        omap_rtc_alarm_update(s);
        return;

    case 0x40:	/* RTC_CTRL_REG */
#if ALMDEBUG
        printf("RTC CONTROL <-- %02x\n", value);
#endif
        s->pm_am = (value >> 3) & 1;
        s->auto_comp = (value >> 2) & 1;
        s->round = (value >> 1) & 1;
        s->running = value & 1;
        s->status &= 0xfd;
        s->status |= s->running << 1;
        return;

    case 0x44:	/* RTC_STATUS_REG */
#if ALMDEBUG
        printf("RTC STATUSL <-- %02x\n", value);
#endif
        s->status &= ~((value & 0xc0) ^ 0x80);
        omap_rtc_interrupts_update(s);
        return;

    case 0x48:	/* RTC_INTERRUPTS_REG */
#if ALMDEBUG
        printf("RTC INTRS <-- %02x\n", value);
#endif
        s->interrupts = value;
        return;

    case 0x4c:	/* RTC_COMP_LSB_REG */
#if ALMDEBUG
        printf("RTC COMPLSB <-- %02x\n", value);
#endif
        s->comp_reg &= 0xff00;
        s->comp_reg |= 0x00ff & value;
        return;

    case 0x50:	/* RTC_COMP_MSB_REG */
#if ALMDEBUG
        printf("RTC COMPMSB <-- %02x\n", value);
#endif
        s->comp_reg &= 0x00ff;
        s->comp_reg |= 0xff00 & (value << 8);
        return;

    default:
        OMAP_BAD_REG(addr);
        return;
    }
}

static CPUReadMemoryFunc *omap_rtc_readfn[] = {
    omap_rtc_read,
    omap_badwidth_read8,
    omap_badwidth_read8,
};

static CPUWriteMemoryFunc *omap_rtc_writefn[] = {
    omap_rtc_write,
    omap_badwidth_write8,
    omap_badwidth_write8,
};

static void omap_rtc_tick(void *opaque)
{
    struct omap_rtc_s *s = opaque;

    if (s->round) {
        /* Round to nearest full minute.  */
        if (s->current_tm.tm_sec < 30)
            s->ti -= s->current_tm.tm_sec;
        else
            s->ti += 60 - s->current_tm.tm_sec;

        s->round = 0;
    }

    localtime_r(&s->ti, &s->current_tm);

    if ((s->interrupts & 0x08) && s->ti == s->alarm_ti) {
        s->status |= 0x40;
        omap_rtc_interrupts_update(s);
    }

    if (s->interrupts & 0x04)
        switch (s->interrupts & 3) {
        case 0:
            s->status |= 0x04;
            qemu_irq_raise(s->irq);
            break;
        case 1:
            if (s->current_tm.tm_sec)
                break;
            s->status |= 0x08;
            qemu_irq_raise(s->irq);
            break;
        case 2:
            if (s->current_tm.tm_sec || s->current_tm.tm_min)
                break;
            s->status |= 0x10;
            qemu_irq_raise(s->irq);
            break;
        case 3:
            if (s->current_tm.tm_sec ||
                            s->current_tm.tm_min || s->current_tm.tm_hour)
                break;
            s->status |= 0x20;
            qemu_irq_raise(s->irq);
            break;
        }

    /* Move on */
    if (s->running)
        s->ti ++;
    s->tick += 1000;

    /*
     * Every full hour add a rough approximation of the compensation
     * register to the 32kHz Timer (which drives the RTC) value. 
     */
    if (s->auto_comp && !s->current_tm.tm_sec && !s->current_tm.tm_min)
        s->tick += s->comp_reg * 1000 / 32768;

    qemu_mod_timer(s->clk, s->tick);
}

void omap_rtc_reset(struct omap_rtc_s *s)
{
    s->interrupts = 0;
    s->comp_reg = 0;
    s->running = 0;
    s->pm_am = 0;
    s->auto_comp = 0;
    s->round = 0;
    s->tick = qemu_get_clock(rt_clock);
    memset(&s->alarm_tm, 0, sizeof(s->alarm_tm));
    s->alarm_tm.tm_mday = 0x01;
    s->status = 1 << 7;
    time(&s->ti);
    s->ti = mktime(s->convert(&s->ti, &s->current_tm));

    omap_rtc_alarm_update(s);
    omap_rtc_tick(s);
}

struct omap_rtc_s *omap_rtc_init(target_phys_addr_t base,
                qemu_irq *irq, omap_clk clk)
{
    int iomemtype;
    struct omap_rtc_s *s = (struct omap_rtc_s *)
            qemu_mallocz(sizeof(struct omap_rtc_s));

    s->base = base;
    s->irq = irq[0];
    s->alarm = irq[1];
    s->clk = qemu_new_timer(rt_clock, omap_rtc_tick, s);
    s->convert = rtc_utc ? gmtime_r : localtime_r;

    omap_rtc_reset(s);

    iomemtype = cpu_register_io_memory(0, omap_rtc_readfn,
                    omap_rtc_writefn, s);
    cpu_register_physical_memory(s->base, 0x800, iomemtype);

    return s;
}

/* Multi-channel Buffered Serial Port interfaces */
struct omap_mcbsp_s {
    target_phys_addr_t base;
    qemu_irq txirq;
    qemu_irq rxirq;
    qemu_irq txdrq;
    qemu_irq rxdrq;

    uint16_t spcr[2];
    uint16_t rcr[2];
    uint16_t xcr[2];
    uint16_t srgr[2];
    uint16_t mcr[2];
    uint16_t pcr;
    uint16_t rcer[8];
    uint16_t xcer[8];
    int tx_rate;
    int rx_rate;
    int tx_req;

    struct i2s_codec_s *codec;
};

static void omap_mcbsp_intr_update(struct omap_mcbsp_s *s)
{
    int irq;

    switch ((s->spcr[0] >> 4) & 3) {			/* RINTM */
    case 0:
        irq = (s->spcr[0] >> 1) & 1;			/* RRDY */
        break;
    case 3:
        irq = (s->spcr[0] >> 3) & 1;			/* RSYNCERR */
        break;
    default:
        irq = 0;
        break;
    }

    qemu_set_irq(s->rxirq, irq);

    switch ((s->spcr[1] >> 4) & 3) {			/* XINTM */
    case 0:
        irq = (s->spcr[1] >> 1) & 1;			/* XRDY */
        break;
    case 3:
        irq = (s->spcr[1] >> 3) & 1;			/* XSYNCERR */
        break;
    default:
        irq = 0;
        break;
    }

    qemu_set_irq(s->txirq, irq);
}

static void omap_mcbsp_req_update(struct omap_mcbsp_s *s)
{
    int prev = s->tx_req;

    s->tx_req = (s->tx_rate ||
                    (s->spcr[0] & (1 << 12))) &&	/* CLKSTP */
            (s->spcr[1] & (1 << 6)) &&			/* GRST */
            (s->spcr[1] & (1 << 0));			/* XRST */

    if (!s->tx_req && prev) {
        s->spcr[1] &= ~(1 << 1);			/* XRDY */
        qemu_irq_lower(s->txdrq);
        omap_mcbsp_intr_update(s);

        if (s->codec)
            s->codec->tx_swallow(s->codec->opaque);
    } else if (s->codec && s->tx_req && !prev) {
        s->spcr[1] |= 1 << 1;				/* XRDY */
        qemu_irq_raise(s->txdrq);
        omap_mcbsp_intr_update(s);
    }
}

static void omap_mcbsp_rate_update(struct omap_mcbsp_s *s)
{
    int rx_clk = 0, tx_clk = 0;
    int cpu_rate = 1500000;	/* XXX */
    if (!s->codec)
        return;

    if (s->spcr[1] & (1 << 6)) {			/* GRST */
        if (s->spcr[0] & (1 << 0))			/* RRST */
            if ((s->srgr[1] & (1 << 13)) &&		/* CLKSM */
                            (s->pcr & (1 << 8)))	/* CLKRM */
                if (~s->pcr & (1 << 7))			/* SCLKME */
                    rx_clk = cpu_rate /
                            ((s->srgr[0] & 0xff) + 1);	/* CLKGDV */
        if (s->spcr[1] & (1 << 0))			/* XRST */
            if ((s->srgr[1] & (1 << 13)) &&		/* CLKSM */
                            (s->pcr & (1 << 9)))	/* CLKXM */
                if (~s->pcr & (1 << 7))			/* SCLKME */
                    tx_clk = cpu_rate /
                            ((s->srgr[0] & 0xff) + 1);	/* CLKGDV */
    }

    s->codec->set_rate(s->codec->opaque, rx_clk, tx_clk);
}

static void omap_mcbsp_rx_start(struct omap_mcbsp_s *s)
{
    if (!(s->spcr[0] & 1)) {				/* RRST */
        if (s->codec)
            s->codec->in.len = 0;
        return;
    }

    if ((s->spcr[0] >> 1) & 1)				/* RRDY */
        s->spcr[0] |= 1 << 2;				/* RFULL */
    s->spcr[0] |= 1 << 1;				/* RRDY */
    qemu_irq_raise(s->rxdrq);
    omap_mcbsp_intr_update(s);
}

static void omap_mcbsp_rx_stop(struct omap_mcbsp_s *s)
{
    s->spcr[0] &= ~(1 << 1);				/* RRDY */
    qemu_irq_lower(s->rxdrq);
    omap_mcbsp_intr_update(s);
}

static void omap_mcbsp_tx_start(struct omap_mcbsp_s *s)
{
    if (s->tx_rate)
        return;
    s->tx_rate = 1;
    omap_mcbsp_req_update(s);
}

static void omap_mcbsp_tx_stop(struct omap_mcbsp_s *s)
{
    s->tx_rate = 0;
    omap_mcbsp_req_update(s);
}

static uint32_t omap_mcbsp_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_mcbsp_s *s = (struct omap_mcbsp_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;
    uint16_t ret;

    switch (offset) {
    case 0x00:	/* DRR2 */
        if (((s->rcr[0] >> 5) & 7) < 3)			/* RWDLEN1 */
            return 0x0000;
        /* Fall through.  */
    case 0x02:	/* DRR1 */
        if (!s->codec)
            return 0x0000;
        if (s->codec->in.len < 2) {
            printf("%s: Rx FIFO underrun\n", __FUNCTION__);
            omap_mcbsp_rx_stop(s);
        } else {
            s->codec->in.len -= 2;
            ret = s->codec->in.fifo[s->codec->in.start ++] << 8;
            ret |= s->codec->in.fifo[s->codec->in.start ++];
            if (!s->codec->in.len)
                omap_mcbsp_rx_stop(s);
            return ret;
        }
        return 0x0000;

    case 0x04:	/* DXR2 */
    case 0x06:	/* DXR1 */
        return 0x0000;

    case 0x08:	/* SPCR2 */
        return s->spcr[1];
    case 0x0a:	/* SPCR1 */
        return s->spcr[0];
    case 0x0c:	/* RCR2 */
        return s->rcr[1];
    case 0x0e:	/* RCR1 */
        return s->rcr[0];
    case 0x10:	/* XCR2 */
        return s->xcr[1];
    case 0x12:	/* XCR1 */
        return s->xcr[0];
    case 0x14:	/* SRGR2 */
        return s->srgr[1];
    case 0x16:	/* SRGR1 */
        return s->srgr[0];
    case 0x18:	/* MCR2 */
        return s->mcr[1];
    case 0x1a:	/* MCR1 */
        return s->mcr[0];
    case 0x1c:	/* RCERA */
        return s->rcer[0];
    case 0x1e:	/* RCERB */
        return s->rcer[1];
    case 0x20:	/* XCERA */
        return s->xcer[0];
    case 0x22:	/* XCERB */
        return s->xcer[1];
    case 0x24:	/* PCR0 */
        return s->pcr;
    case 0x26:	/* RCERC */
        return s->rcer[2];
    case 0x28:	/* RCERD */
        return s->rcer[3];
    case 0x2a:	/* XCERC */
        return s->xcer[2];
    case 0x2c:	/* XCERD */
        return s->xcer[3];
    case 0x2e:	/* RCERE */
        return s->rcer[4];
    case 0x30:	/* RCERF */
        return s->rcer[5];
    case 0x32:	/* XCERE */
        return s->xcer[4];
    case 0x34:	/* XCERF */
        return s->xcer[5];
    case 0x36:	/* RCERG */
        return s->rcer[6];
    case 0x38:	/* RCERH */
        return s->rcer[7];
    case 0x3a:	/* XCERG */
        return s->xcer[6];
    case 0x3c:	/* XCERH */
        return s->xcer[7];
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_mcbsp_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_mcbsp_s *s = (struct omap_mcbsp_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;

    switch (offset) {
    case 0x00:	/* DRR2 */
    case 0x02:	/* DRR1 */
        OMAP_RO_REG(addr);
        return;

    case 0x04:	/* DXR2 */
        if (((s->xcr[0] >> 5) & 7) < 3)			/* XWDLEN1 */
            return;
        /* Fall through.  */
    case 0x06:	/* DXR1 */
        if (!s->codec)
            return;
        if (s->tx_req) {
            if (s->codec->out.len > s->codec->out.size - 2) {
                printf("%s: Tx FIFO overrun\n", __FUNCTION__);
                omap_mcbsp_tx_stop(s);
            } else {
                s->codec->out.fifo[s->codec->out.len ++] = (value >> 8) & 0xff;
                s->codec->out.fifo[s->codec->out.len ++] = (value >> 0) & 0xff;
                if (s->codec->out.len >= s->codec->out.size)
                    omap_mcbsp_tx_stop(s);
            }
        } else
            printf("%s: Tx FIFO overrun\n", __FUNCTION__);
        return;

    case 0x08:	/* SPCR2 */
        s->spcr[1] &= 0x0002;
        s->spcr[1] |= 0x03f9 & value;
        s->spcr[1] |= 0x0004 & (value << 2);		/* XEMPTY := XRST */
        if (~value & 1) {				/* XRST */
            s->spcr[1] &= ~6;
            qemu_irq_lower(s->rxdrq);
            if (s->codec)
                s->codec->out.len = 0;
        }
        if (s->codec)
            omap_mcbsp_rate_update(s);
        omap_mcbsp_req_update(s);
        return;
    case 0x0a:	/* SPCR1 */
        s->spcr[0] &= 0x0006;
        s->spcr[0] |= 0xf8f9 & value;
        if (value & (1 << 15))				/* DLB */
            printf("%s: Digital Loopback mode enable attempt\n", __FUNCTION__);
        if (~value & 1) {				/* RRST */
            s->spcr[0] &= ~6;
            qemu_irq_lower(s->txdrq);
            if (s->codec)
                s->codec->in.len = 0;
        }
        if (s->codec)
            omap_mcbsp_rate_update(s);
        omap_mcbsp_req_update(s);
        return;

    case 0x0c:	/* RCR2 */
        s->rcr[1] = value & 0xffff;
        return;
    case 0x0e:	/* RCR1 */
        s->rcr[0] = value & 0x7fe0;
        return;
    case 0x10:	/* XCR2 */
        s->xcr[1] = value & 0xffff;
        return;
    case 0x12:	/* XCR1 */
        s->xcr[0] = value & 0x7fe0;
        return;
    case 0x14:	/* SRGR2 */
        s->srgr[1] = value & 0xffff;
        omap_mcbsp_rate_update(s);
        return;
    case 0x16:	/* SRGR1 */
        s->srgr[0] = value & 0xffff;
        omap_mcbsp_rate_update(s);
        return;
    case 0x18:	/* MCR2 */
        s->mcr[1] = value & 0x03e3;
        if (value & 3)					/* XMCM */
            printf("%s: Tx channel selection mode enable attempt\n",
                            __FUNCTION__);
        return;
    case 0x1a:	/* MCR1 */
        s->mcr[0] = value & 0x03e1;
        if (value & 1)					/* RMCM */
            printf("%s: Rx channel selection mode enable attempt\n",
                            __FUNCTION__);
        return;
    case 0x1c:	/* RCERA */
        s->rcer[0] = value & 0xffff;
        return;
    case 0x1e:	/* RCERB */
        s->rcer[1] = value & 0xffff;
        return;
    case 0x20:	/* XCERA */
        s->xcer[0] = value & 0xffff;
        return;
    case 0x22:	/* XCERB */
        s->xcer[1] = value & 0xffff;
        return;
    case 0x24:	/* PCR0 */
        s->pcr = value & 0x7faf;
        return;
    case 0x26:	/* RCERC */
        s->rcer[2] = value & 0xffff;
        return;
    case 0x28:	/* RCERD */
        s->rcer[3] = value & 0xffff;
        return;
    case 0x2a:	/* XCERC */
        s->xcer[2] = value & 0xffff;
        return;
    case 0x2c:	/* XCERD */
        s->xcer[3] = value & 0xffff;
        return;
    case 0x2e:	/* RCERE */
        s->rcer[4] = value & 0xffff;
        return;
    case 0x30:	/* RCERF */
        s->rcer[5] = value & 0xffff;
        return;
    case 0x32:	/* XCERE */
        s->xcer[4] = value & 0xffff;
        return;
    case 0x34:	/* XCERF */
        s->xcer[5] = value & 0xffff;
        return;
    case 0x36:	/* RCERG */
        s->rcer[6] = value & 0xffff;
        return;
    case 0x38:	/* RCERH */
        s->rcer[7] = value & 0xffff;
        return;
    case 0x3a:	/* XCERG */
        s->xcer[6] = value & 0xffff;
        return;
    case 0x3c:	/* XCERH */
        s->xcer[7] = value & 0xffff;
        return;
    }

    OMAP_BAD_REG(addr);
}

static CPUReadMemoryFunc *omap_mcbsp_readfn[] = {
    omap_badwidth_read16,
    omap_mcbsp_read,
    omap_badwidth_read16,
};

static CPUWriteMemoryFunc *omap_mcbsp_writefn[] = {
    omap_badwidth_write16,
    omap_mcbsp_write,
    omap_badwidth_write16,
};

static void omap_mcbsp_reset(struct omap_mcbsp_s *s)
{
    memset(&s->spcr, 0, sizeof(s->spcr));
    memset(&s->rcr, 0, sizeof(s->rcr));
    memset(&s->xcr, 0, sizeof(s->xcr));
    s->srgr[0] = 0x0001;
    s->srgr[1] = 0x2000;
    memset(&s->mcr, 0, sizeof(s->mcr));
    memset(&s->pcr, 0, sizeof(s->pcr));
    memset(&s->rcer, 0, sizeof(s->rcer));
    memset(&s->xcer, 0, sizeof(s->xcer));
    s->tx_req = 0;
    s->tx_rate = 0;
    s->rx_rate = 0;
}

struct omap_mcbsp_s *omap_mcbsp_init(target_phys_addr_t base,
                qemu_irq *irq, qemu_irq *dma, omap_clk clk)
{
    int iomemtype;
    struct omap_mcbsp_s *s = (struct omap_mcbsp_s *)
            qemu_mallocz(sizeof(struct omap_mcbsp_s));

    s->base = base;
    s->txirq = irq[0];
    s->rxirq = irq[1];
    s->txdrq = dma[0];
    s->rxdrq = dma[1];
    omap_mcbsp_reset(s);

    iomemtype = cpu_register_io_memory(0, omap_mcbsp_readfn,
                    omap_mcbsp_writefn, s);
    cpu_register_physical_memory(s->base, 0x800, iomemtype);

    return s;
}

void omap_mcbsp_i2s_swallow(void *opaque, int line, int level)
{
    struct omap_mcbsp_s *s = (struct omap_mcbsp_s *) opaque;

    omap_mcbsp_rx_start(s);
}

void omap_mcbsp_i2s_start(void *opaque, int line, int level)
{
    struct omap_mcbsp_s *s = (struct omap_mcbsp_s *) opaque;

    omap_mcbsp_tx_start(s);
}

void omap_mcbsp_i2s_attach(struct omap_mcbsp_s *s, struct i2s_codec_s *slave)
{
    s->codec = slave;
    slave->rx_swallow = qemu_allocate_irqs(omap_mcbsp_i2s_swallow, s, 1)[0];
    slave->tx_start = qemu_allocate_irqs(omap_mcbsp_i2s_start, s, 1)[0];
}

/* General chip reset */
static void omap_mpu_reset(void *opaque)
{
    struct omap_mpu_state_s *mpu = (struct omap_mpu_state_s *) opaque;

    omap_clkm_reset(mpu);
    omap_inth_reset(mpu->ih[0]);
    omap_inth_reset(mpu->ih[1]);
    omap_dma_reset(mpu->dma);
    omap_mpu_timer_reset(mpu->timer[0]);
    omap_mpu_timer_reset(mpu->timer[1]);
    omap_mpu_timer_reset(mpu->timer[2]);
    omap_wd_timer_reset(mpu->wdt);
    omap_os_timer_reset(mpu->os_timer);
    omap_lcdc_reset(mpu->lcd);
    omap_ulpd_pm_reset(mpu);
    omap_pin_cfg_reset(mpu);
    omap_mpui_reset(mpu);
    omap_tipb_bridge_reset(mpu->private_tipb);
    omap_tipb_bridge_reset(mpu->public_tipb);
    omap_dpll_reset(&mpu->dpll[0]);
    omap_dpll_reset(&mpu->dpll[1]);
    omap_dpll_reset(&mpu->dpll[2]);
    omap_uart_reset(mpu->uart[0]);
    omap_uart_reset(mpu->uart[1]);
    omap_uart_reset(mpu->uart[2]);
    omap_mmc_reset(mpu->mmc);
    omap_mpuio_reset(mpu->mpuio);
    omap_gpio_reset(mpu->gpio);
    omap_uwire_reset(mpu->microwire);
    omap_pwl_reset(mpu);
    omap_pwt_reset(mpu);
    omap_i2c_reset(mpu->i2c);
    omap_rtc_reset(mpu->rtc);
    omap_mcbsp_reset(mpu->mcbsp1);
    omap_mcbsp_reset(mpu->mcbsp2);
    omap_mcbsp_reset(mpu->mcbsp3);
    cpu_reset(mpu->env);
}

static const struct omap_map_s {
    target_phys_addr_t phys_dsp;
    target_phys_addr_t phys_mpu;
    uint32_t size;
    const char *name;
} omap15xx_dsp_mm[] = {
    /* Strobe 0 */
    { 0xe1010000, 0xfffb0000, 0x800, "UART1 BT" },		/* CS0 */
    { 0xe1010800, 0xfffb0800, 0x800, "UART2 COM" },		/* CS1 */
    { 0xe1011800, 0xfffb1800, 0x800, "McBSP1 audio" },		/* CS3 */
    { 0xe1012000, 0xfffb2000, 0x800, "MCSI2 communication" },	/* CS4 */
    { 0xe1012800, 0xfffb2800, 0x800, "MCSI1 BT u-Law" },	/* CS5 */
    { 0xe1013000, 0xfffb3000, 0x800, "uWire" },			/* CS6 */
    { 0xe1013800, 0xfffb3800, 0x800, "I^2C" },			/* CS7 */
    { 0xe1014000, 0xfffb4000, 0x800, "USB W2FC" },		/* CS8 */
    { 0xe1014800, 0xfffb4800, 0x800, "RTC" },			/* CS9 */
    { 0xe1015000, 0xfffb5000, 0x800, "MPUIO" },			/* CS10 */
    { 0xe1015800, 0xfffb5800, 0x800, "PWL" },			/* CS11 */
    { 0xe1016000, 0xfffb6000, 0x800, "PWT" },			/* CS12 */
    { 0xe1017000, 0xfffb7000, 0x800, "McBSP3" },		/* CS14 */
    { 0xe1017800, 0xfffb7800, 0x800, "MMC" },			/* CS15 */
    { 0xe1019000, 0xfffb9000, 0x800, "32-kHz timer" },		/* CS18 */
    { 0xe1019800, 0xfffb9800, 0x800, "UART3" },			/* CS19 */
    { 0xe101c800, 0xfffbc800, 0x800, "TIPB switches" },		/* CS25 */
    /* Strobe 1 */
    { 0xe101e000, 0xfffce000, 0x800, "GPIOs" },			/* CS28 */

    { 0 }
};

static void omap_setup_dsp_mapping(const struct omap_map_s *map)
{
    int io;

    for (; map->phys_dsp; map ++) {
        io = cpu_get_physical_page_desc(map->phys_mpu);

        cpu_register_physical_memory(map->phys_dsp, map->size, io);
    }
}

static void omap_mpu_wakeup(void *opaque, int irq, int req)
{
    struct omap_mpu_state_s *mpu = (struct omap_mpu_state_s *) opaque;

    if (mpu->env->halted)
        cpu_interrupt(mpu->env, CPU_INTERRUPT_EXITTB);
}

struct omap_mpu_state_s *omap310_mpu_init(unsigned long sdram_size,
                DisplayState *ds, const char *core)
{
    struct omap_mpu_state_s *s = (struct omap_mpu_state_s *)
            qemu_mallocz(sizeof(struct omap_mpu_state_s));
    ram_addr_t imif_base, emiff_base;
    
    if (!core)
        core = "ti925t";

    /* Core */
    s->mpu_model = omap310;
    s->env = cpu_init(core);
    if (!s->env) {
        fprintf(stderr, "Unable to find CPU definition\n");
        exit(1);
    }
    s->sdram_size = sdram_size;
    s->sram_size = OMAP15XX_SRAM_SIZE;

    s->wakeup = qemu_allocate_irqs(omap_mpu_wakeup, s, 1)[0];

    /* Clocks */
    omap_clk_init(s);

    /* Memory-mapped stuff */
    cpu_register_physical_memory(OMAP_EMIFF_BASE, s->sdram_size,
                    (emiff_base = qemu_ram_alloc(s->sdram_size)) | IO_MEM_RAM);
    cpu_register_physical_memory(OMAP_IMIF_BASE, s->sram_size,
                    (imif_base = qemu_ram_alloc(s->sram_size)) | IO_MEM_RAM);

    omap_clkm_init(0xfffece00, 0xe1008000, s);

    s->ih[0] = omap_inth_init(0xfffecb00, 0x100,
                    arm_pic_init_cpu(s->env),
                    omap_findclk(s, "arminth_ck"));
    s->ih[1] = omap_inth_init(0xfffe0000, 0x800,
                    &s->ih[0]->pins[OMAP_INT_15XX_IH2_IRQ],
                    omap_findclk(s, "arminth_ck"));
    s->irq[0] = s->ih[0]->pins;
    s->irq[1] = s->ih[1]->pins;

    s->dma = omap_dma_init(0xfffed800, s->irq[0], s,
                    omap_findclk(s, "dma_ck"));
    s->port[emiff    ].addr_valid = omap_validate_emiff_addr;
    s->port[emifs    ].addr_valid = omap_validate_emifs_addr;
    s->port[imif     ].addr_valid = omap_validate_imif_addr;
    s->port[tipb     ].addr_valid = omap_validate_tipb_addr;
    s->port[local    ].addr_valid = omap_validate_local_addr;
    s->port[tipb_mpui].addr_valid = omap_validate_tipb_mpui_addr;

    s->timer[0] = omap_mpu_timer_init(0xfffec500,
                    s->irq[0][OMAP_INT_TIMER1],
                    omap_findclk(s, "mputim_ck"));
    s->timer[1] = omap_mpu_timer_init(0xfffec600,
                    s->irq[0][OMAP_INT_TIMER2],
                    omap_findclk(s, "mputim_ck"));
    s->timer[2] = omap_mpu_timer_init(0xfffec700,
                    s->irq[0][OMAP_INT_TIMER3],
                    omap_findclk(s, "mputim_ck"));

    s->wdt = omap_wd_timer_init(0xfffec800,
                    s->irq[0][OMAP_INT_WD_TIMER],
                    omap_findclk(s, "armwdt_ck"));

    s->os_timer = omap_os_timer_init(0xfffb9000,
                    s->irq[1][OMAP_INT_OS_TIMER],
                    omap_findclk(s, "clk32-kHz"));

    s->lcd = omap_lcdc_init(0xfffec000, s->irq[0][OMAP_INT_LCD_CTRL],
                    &s->dma->lcd_ch, ds, imif_base, emiff_base,
                    omap_findclk(s, "lcd_ck"));

    omap_ulpd_pm_init(0xfffe0800, s);
    omap_pin_cfg_init(0xfffe1000, s);
    omap_id_init(s);

    omap_mpui_init(0xfffec900, s);

    s->private_tipb = omap_tipb_bridge_init(0xfffeca00,
                    s->irq[0][OMAP_INT_BRIDGE_PRIV],
                    omap_findclk(s, "tipb_ck"));
    s->public_tipb = omap_tipb_bridge_init(0xfffed300,
                    s->irq[0][OMAP_INT_BRIDGE_PUB],
                    omap_findclk(s, "tipb_ck"));

    omap_tcmi_init(0xfffecc00, s);

    s->uart[0] = omap_uart_init(0xfffb0000, s->irq[1][OMAP_INT_UART1],
                    omap_findclk(s, "uart1_ck"),
                    serial_hds[0]);
    s->uart[1] = omap_uart_init(0xfffb0800, s->irq[1][OMAP_INT_UART2],
                    omap_findclk(s, "uart2_ck"),
                    serial_hds[0] ? serial_hds[1] : 0);
    s->uart[2] = omap_uart_init(0xe1019800, s->irq[0][OMAP_INT_UART3],
                    omap_findclk(s, "uart3_ck"),
                    serial_hds[0] && serial_hds[1] ? serial_hds[2] : 0);

    omap_dpll_init(&s->dpll[0], 0xfffecf00, omap_findclk(s, "dpll1"));
    omap_dpll_init(&s->dpll[1], 0xfffed000, omap_findclk(s, "dpll2"));
    omap_dpll_init(&s->dpll[2], 0xfffed100, omap_findclk(s, "dpll3"));

    s->mmc = omap_mmc_init(0xfffb7800, s->irq[1][OMAP_INT_OQN],
                    &s->drq[OMAP_DMA_MMC_TX], omap_findclk(s, "mmc_ck"));

    s->mpuio = omap_mpuio_init(0xfffb5000,
                    s->irq[1][OMAP_INT_KEYBOARD], s->irq[1][OMAP_INT_MPUIO],
                    s->wakeup, omap_findclk(s, "clk32-kHz"));

    s->gpio = omap_gpio_init(0xfffce000, s->irq[0][OMAP_INT_GPIO_BANK1],
                    omap_findclk(s, "arm_gpio_ck"));

    s->microwire = omap_uwire_init(0xfffb3000, &s->irq[1][OMAP_INT_uWireTX],
                    s->drq[OMAP_DMA_UWIRE_TX], omap_findclk(s, "mpuper_ck"));

    omap_pwl_init(0xfffb5800, s, omap_findclk(s, "armxor_ck"));
    omap_pwt_init(0xfffb6000, s, omap_findclk(s, "armxor_ck"));

    s->i2c = omap_i2c_init(0xfffb3800, s->irq[1][OMAP_INT_I2C],
                    &s->drq[OMAP_DMA_I2C_RX], omap_findclk(s, "mpuper_ck"));

    s->rtc = omap_rtc_init(0xfffb4800, &s->irq[1][OMAP_INT_RTC_TIMER],
                    omap_findclk(s, "clk32-kHz"));

    s->mcbsp1 = omap_mcbsp_init(0xfffb1800, &s->irq[1][OMAP_INT_McBSP1TX],
                    &s->drq[OMAP_DMA_MCBSP1_TX], omap_findclk(s, "dspxor_ck"));
    s->mcbsp2 = omap_mcbsp_init(0xfffb1000, &s->irq[0][OMAP_INT_310_McBSP2_TX],
                    &s->drq[OMAP_DMA_MCBSP2_TX], omap_findclk(s, "mpuper_ck"));
    s->mcbsp3 = omap_mcbsp_init(0xfffb7000, &s->irq[1][OMAP_INT_McBSP3TX],
                    &s->drq[OMAP_DMA_MCBSP3_TX], omap_findclk(s, "dspxor_ck"));

    /* Register mappings not currenlty implemented:
     * MCSI2 Comm	fffb2000 - fffb27ff (not mapped on OMAP310)
     * MCSI1 Bluetooth	fffb2800 - fffb2fff (not mapped on OMAP310)
     * USB W2FC		fffb4000 - fffb47ff
     * Camera Interface	fffb6800 - fffb6fff
     * USB Host		fffba000 - fffba7ff
     * FAC		fffba800 - fffbafff
     * HDQ/1-Wire	fffbc000 - fffbc7ff
     * TIPB switches	fffbc800 - fffbcfff
     * LED1		fffbd000 - fffbd7ff
     * LED2		fffbd800 - fffbdfff
     * Mailbox		fffcf000 - fffcf7ff
     * Local bus IF	fffec100 - fffec1ff
     * Local bus MMU	fffec200 - fffec2ff
     * DSP MMU		fffed200 - fffed2ff
     */

    omap_setup_dsp_mapping(omap15xx_dsp_mm);

    qemu_register_reset(omap_mpu_reset, s);

    return s;
}
