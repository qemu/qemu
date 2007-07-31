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
uint32_t omap_badwidth_read16(void *opaque, target_phys_addr_t addr)
{
    OMAP_16B_REG(addr);
    return 0;
}

void omap_badwidth_write16(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    OMAP_16B_REG(addr);
}

uint32_t omap_badwidth_read32(void *opaque, target_phys_addr_t addr)
{
    OMAP_32B_REG(addr);
    return 0;
}

void omap_badwidth_write32(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    OMAP_32B_REG(addr);
}

#define likely
#define unlikely

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
        s->irqs &= value;
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
        s->ch[ch].addr[0] |= value << 16;
        break;

    case 0x0c:	/* SYS_DMA_CDSA_L_CH0 */
        s->ch[ch].addr[1] &= 0xffff0000;
        s->ch[ch].addr[1] |= value;
        break;

    case 0x0e:	/* SYS_DMA_CDSA_U_CH0 */
        s->ch[ch].addr[1] &= 0x0000ffff;
        s->ch[ch].addr[1] |= value << 16;
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
        OMAP_BAD_REG((unsigned long) reg);
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
        s->delay = ticks_per_sec >> 5;
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
int omap_validate_emiff_addr(struct omap_mpu_state_s *s,
                target_phys_addr_t addr)
{
    return addr >= OMAP_EMIFF_BASE && addr < OMAP_EMIFF_BASE + s->sdram_size;
}

int omap_validate_emifs_addr(struct omap_mpu_state_s *s,
                target_phys_addr_t addr)
{
    return addr >= OMAP_EMIFS_BASE && addr < OMAP_EMIFF_BASE;
}

int omap_validate_imif_addr(struct omap_mpu_state_s *s,
                target_phys_addr_t addr)
{
    return addr >= OMAP_IMIF_BASE && addr < OMAP_IMIF_BASE + s->sram_size;
}

int omap_validate_tipb_addr(struct omap_mpu_state_s *s,
                target_phys_addr_t addr)
{
    return addr >= 0xfffb0000 && addr < 0xffff0000;
}

int omap_validate_local_addr(struct omap_mpu_state_s *s,
                target_phys_addr_t addr)
{
    return addr >= OMAP_LOCALBUS_BASE && addr < OMAP_LOCALBUS_BASE + 0x1000000;
}

int omap_validate_tipb_mpui_addr(struct omap_mpu_state_s *s,
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
        expires = timer->time + muldiv64(timer->val << (timer->ptv + 1),
                        ticks_per_sec, timer->rate);
        qemu_mod_timer(timer->timer, expires);
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
                s->mode = 0;
                omap_clk_put(s->timer.clk);
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
    int offset = addr - s->timer.base;

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
    int offset = addr - s->timer.base;

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
    case 0xfffecc00:	/* IMIF_PRIO */
    case 0xfffecc04:	/* EMIFS_PRIO */
    case 0xfffecc08:	/* EMIFF_PRIO */
    case 0xfffecc0c:	/* EMIFS_CONFIG */
    case 0xfffecc10:	/* EMIFS_CS0_CONFIG */
    case 0xfffecc14:	/* EMIFS_CS1_CONFIG */
    case 0xfffecc18:	/* EMIFS_CS2_CONFIG */
    case 0xfffecc1c:	/* EMIFS_CS3_CONFIG */
    case 0xfffecc24:	/* EMIFF_MRS */
    case 0xfffecc28:	/* TIMEOUT1 */
    case 0xfffecc2c:	/* TIMEOUT2 */
    case 0xfffecc30:	/* TIMEOUT3 */
    case 0xfffecc3c:	/* EMIFF_SDRAM_CONFIG_2 */
    case 0xfffecc40:	/* EMIFS_CFG_DYN_WAIT */
        return s->tcmi_regs[offset >> 2];

    case 0xfffecc20:	/* EMIFF_SDRAM_CONFIG */
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
    case 0xfffecc00:	/* IMIF_PRIO */
    case 0xfffecc04:	/* EMIFS_PRIO */
    case 0xfffecc08:	/* EMIFF_PRIO */
    case 0xfffecc10:	/* EMIFS_CS0_CONFIG */
    case 0xfffecc14:	/* EMIFS_CS1_CONFIG */
    case 0xfffecc18:	/* EMIFS_CS2_CONFIG */
    case 0xfffecc1c:	/* EMIFS_CS3_CONFIG */
    case 0xfffecc20:	/* EMIFF_SDRAM_CONFIG */
    case 0xfffecc24:	/* EMIFF_MRS */
    case 0xfffecc28:	/* TIMEOUT1 */
    case 0xfffecc2c:	/* TIMEOUT2 */
    case 0xfffecc30:	/* TIMEOUT3 */
    case 0xfffecc3c:	/* EMIFF_SDRAM_CONFIG_2 */
    case 0xfffecc40:	/* EMIFS_CFG_DYN_WAIT */
        s->tcmi_regs[offset >> 2] = value;
        break;
    case 0xfffecc0c:	/* EMIFS_CONFIG */
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
        return (s->clkm.clocking_scheme < 11) | s->clkm.cold_start;

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
        return (s->clkm.clocking_scheme < 11) | s->clkm.cold_start |
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
    omap_clkm_idlect1_update(s, s->clkm.arm_idlect1 & 0x0400, 0x0400);
    s->clkm.arm_idlect1 = 0x0400;
    omap_clkm_idlect2_update(s, s->clkm.arm_idlect2 & 0x0100, 0x0100);
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
    s->clkm.cold_start = 0x3a;
    omap_clkm_reset(s);

    cpu_register_physical_memory(s->clkm.mpu_base, 0x100, iomemtype[0]);
    cpu_register_physical_memory(s->clkm.dsp_base, 0x1000, iomemtype[1]);
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
    omap_uart_reset(mpu->uart1);
    omap_uart_reset(mpu->uart2);
    omap_uart_reset(mpu->uart3);
    omap_mmc_reset(mpu->mmc);
    cpu_reset(mpu->env);
}

static void omap_mpu_wakeup(void *opaque, int irq, int req)
{
    struct omap_mpu_state_s *mpu = (struct omap_mpu_state_s *) opaque;

    cpu_interrupt(mpu->env, CPU_INTERRUPT_EXITTB);
}

struct omap_mpu_state_s *omap310_mpu_init(unsigned long sdram_size,
                DisplayState *ds, const char *core)
{
    struct omap_mpu_state_s *s = (struct omap_mpu_state_s *)
            qemu_mallocz(sizeof(struct omap_mpu_state_s));
    ram_addr_t imif_base, emiff_base;

    /* Core */
    s->mpu_model = omap310;
    s->env = cpu_init();
    s->sdram_size = sdram_size;
    s->sram_size = OMAP15XX_SRAM_SIZE;

    cpu_arm_set_model(s->env, core ?: "ti925t");

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

    s->uart1 = omap_uart_init(0xfffb0000, s->irq[1][OMAP_INT_UART1],
                    omap_findclk(s, "uart1_ck"),
                    serial_hds[0]);
    s->uart2 = omap_uart_init(0xfffb0800, s->irq[1][OMAP_INT_UART2],
                    omap_findclk(s, "uart2_ck"),
                    serial_hds[0] ? serial_hds[1] : 0);
    s->uart3 = omap_uart_init(0xe1019800, s->irq[0][OMAP_INT_UART3],
                    omap_findclk(s, "uart3_ck"),
                    serial_hds[0] && serial_hds[1] ? serial_hds[2] : 0);

    omap_dpll_init(&s->dpll[0], 0xfffecf00, omap_findclk(s, "dpll1"));
    omap_dpll_init(&s->dpll[1], 0xfffed000, omap_findclk(s, "dpll2"));
    omap_dpll_init(&s->dpll[2], 0xfffed100, omap_findclk(s, "dpll3"));

    s->mmc = omap_mmc_init(0xfffb7800, s->irq[1][OMAP_INT_OQN],
                    &s->drq[OMAP_DMA_MMC_TX], omap_findclk(s, "mmc_ck"));

    qemu_register_reset(omap_mpu_reset, s);
    s->wakeup = qemu_allocate_irqs(omap_mpu_wakeup, s, 1)[0];

    return s;
}
