/*
 * TI OMAP DMA gigacell.
 *
 * Copyright (C) 2006-2008 Andrzej Zaborowski  <balrog@zabor.org>
 * Copyright (C) 2007-2008 Lauro Ramos Venancio  <lauro.venancio@indt.org.br>
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
#include "qemu-common.h"
#include "qemu-timer.h"
#include "omap.h"
#include "irq.h"

struct omap_dma_channel_s {
    /* transfer data */
    int burst[2];
    int pack[2];
    enum omap_dma_port port[2];
    target_phys_addr_t addr[2];
    omap_dma_addressing_t mode[2];
    uint16_t elements;
    uint16_t frames;
    int16_t frame_index[2];
    int16_t element_index[2];
    int data_type;

    /* transfer type */
    int transparent_copy;
    int constant_fill;
    uint32_t color;

    /* auto init and linked channel data */
    int end_prog;
    int repeat;
    int auto_init;
    int link_enabled;
    int link_next_ch;

    /* interruption data */
    int interrupts;
    int status;

    /* state data */
    int active;
    int enable;
    int sync;
    int pending_request;
    int waiting_end_prog;
    uint16_t cpc;

    /* sync type */
    int fs;
    int bs;

    /* compatibility */
    int omap_3_1_compatible_disable;

    qemu_irq irq;
    struct omap_dma_channel_s *sibling;

    struct omap_dma_reg_set_s {
        target_phys_addr_t src, dest;
        int frame;
        int element;
        int frame_delta[2];
        int elem_delta[2];
        int frames;
        int elements;
    } active_set;

    /* unused parameters */
    int priority;
    int interleave_disabled;
    int type;
};

struct omap_dma_s {
    QEMUTimer *tm;
    struct omap_mpu_state_s *mpu;
    target_phys_addr_t base;
    omap_clk clk;
    int64_t delay;
    uint32_t drq;
    enum omap_dma_model model;
    int omap_3_1_mapping_disabled;

    uint16_t gcr;
    int run_count;

    int chans;
    struct omap_dma_channel_s ch[16];
    struct omap_dma_lcd_channel_s lcd_ch;
};

/* Interrupts */
#define TIMEOUT_INTR    (1 << 0)
#define EVENT_DROP_INTR (1 << 1)
#define HALF_FRAME_INTR (1 << 2)
#define END_FRAME_INTR  (1 << 3)
#define LAST_FRAME_INTR (1 << 4)
#define END_BLOCK_INTR  (1 << 5)
#define SYNC            (1 << 6)

static void omap_dma_interrupts_update(struct omap_dma_s *s)
{
    struct omap_dma_channel_s *ch = s->ch;
    int i;

    if (s->omap_3_1_mapping_disabled) {
        for (i = 0; i < s->chans; i ++, ch ++)
            if (ch->status)
                qemu_irq_raise(ch->irq);
    } else {
        /* First three interrupts are shared between two channels each. */
        for (i = 0; i < 6; i ++, ch ++) {
            if (ch->status || (ch->sibling && ch->sibling->status))
                qemu_irq_raise(ch->irq);
        }
    }
}

static void omap_dma_channel_load(struct omap_dma_s *s,
                struct omap_dma_channel_s *ch)
{
    struct omap_dma_reg_set_s *a = &ch->active_set;
    int i;
    int omap_3_1 = !ch->omap_3_1_compatible_disable;

    /*
     * TODO: verify address ranges and alignment
     * TODO: port endianness
     */

    a->src = ch->addr[0];
    a->dest = ch->addr[1];
    a->frames = ch->frames;
    a->elements = ch->elements;
    a->frame = 0;
    a->element = 0;

    if (unlikely(!ch->elements || !ch->frames)) {
        printf("%s: bad DMA request\n", __FUNCTION__);
        return;
    }

    for (i = 0; i < 2; i ++)
        switch (ch->mode[i]) {
        case constant:
            a->elem_delta[i] = 0;
            a->frame_delta[i] = 0;
            break;
        case post_incremented:
            a->elem_delta[i] = ch->data_type;
            a->frame_delta[i] = 0;
            break;
        case single_index:
            a->elem_delta[i] = ch->data_type +
                    ch->element_index[omap_3_1 ? 0 : i] - 1;
            a->frame_delta[i] = 0;
            break;
        case double_index:
            a->elem_delta[i] = ch->data_type +
                    ch->element_index[omap_3_1 ? 0 : i] - 1;
            a->frame_delta[i] = ch->frame_index[omap_3_1 ? 0 : i] -
                    ch->element_index[omap_3_1 ? 0 : i];
            break;
        default:
            break;
        }
}

static void omap_dma_activate_channel(struct omap_dma_s *s,
                struct omap_dma_channel_s *ch)
{
    if (!ch->active) {
        ch->active = 1;
        if (ch->sync)
            ch->status |= SYNC;
        s->run_count ++;
    }

    if (s->delay && !qemu_timer_pending(s->tm))
        qemu_mod_timer(s->tm, qemu_get_clock(vm_clock) + s->delay);
}

static void omap_dma_deactivate_channel(struct omap_dma_s *s,
                struct omap_dma_channel_s *ch)
{
    /* Update cpc */
    ch->cpc = ch->active_set.dest & 0xffff;

    if (ch->pending_request && !ch->waiting_end_prog) {
        /* Don't deactivate the channel */
        ch->pending_request = 0;
        if (ch->enable)
            return;
    }

    /* Don't deactive the channel if it is synchronized and the DMA request is
       active */
    if (ch->sync && (s->drq & (1 << ch->sync)) && ch->enable)
        return;

    if (ch->active) {
        ch->active = 0;
        ch->status &= ~SYNC;
        s->run_count --;
    }

    if (!s->run_count)
        qemu_del_timer(s->tm);
}

static void omap_dma_enable_channel(struct omap_dma_s *s,
                struct omap_dma_channel_s *ch)
{
    if (!ch->enable) {
        ch->enable = 1;
        ch->waiting_end_prog = 0;
        omap_dma_channel_load(s, ch);
        if ((!ch->sync) || (s->drq & (1 << ch->sync)))
            omap_dma_activate_channel(s, ch);
    }
}

static void omap_dma_disable_channel(struct omap_dma_s *s,
                struct omap_dma_channel_s *ch)
{
    if (ch->enable) {
        ch->enable = 0;
        /* Discard any pending request */
        ch->pending_request = 0;
        omap_dma_deactivate_channel(s, ch);
    }
}

static void omap_dma_channel_end_prog(struct omap_dma_s *s,
                struct omap_dma_channel_s *ch)
{
    if (ch->waiting_end_prog) {
        ch->waiting_end_prog = 0;
        if (!ch->sync || ch->pending_request) {
            ch->pending_request = 0;
            omap_dma_activate_channel(s, ch);
        }
    }
}

static void omap_dma_enable_3_1_mapping(struct omap_dma_s *s)
{
    s->omap_3_1_mapping_disabled = 0;
    s->chans = 9;
}

static void omap_dma_disable_3_1_mapping(struct omap_dma_s *s)
{
    s->omap_3_1_mapping_disabled = 1;
    s->chans = 16;
}

static void omap_dma_process_request(struct omap_dma_s *s, int request)
{
    int channel;
    int drop_event = 0;
    struct omap_dma_channel_s *ch = s->ch;

    for (channel = 0; channel < s->chans; channel ++, ch ++) {
        if (ch->enable && ch->sync == request) {
            if (!ch->active)
                omap_dma_activate_channel(s, ch);
            else if (!ch->pending_request)
                ch->pending_request = 1;
            else {
                /* Request collision */
                /* Second request received while processing other request */
                ch->status |= EVENT_DROP_INTR;
                drop_event = 1;
            }
        }
    }

    if (drop_event)
        omap_dma_interrupts_update(s);
}

static void omap_dma_channel_run(struct omap_dma_s *s)
{
    int n = s->chans;
    uint16_t status;
    uint8_t value[4];
    struct omap_dma_port_if_s *src_p, *dest_p;
    struct omap_dma_reg_set_s *a;
    struct omap_dma_channel_s *ch;

    for (ch = s->ch; n; n --, ch ++) {
        if (!ch->active)
            continue;

        a = &ch->active_set;

        src_p = &s->mpu->port[ch->port[0]];
        dest_p = &s->mpu->port[ch->port[1]];
        if ((!ch->constant_fill && !src_p->addr_valid(s->mpu, a->src)) ||
                        (!dest_p->addr_valid(s->mpu, a->dest))) {
#if 0
            /* Bus time-out */
            if (ch->interrupts & TIMEOUT_INTR)
                ch->status |= TIMEOUT_INTR;
            omap_dma_deactivate_channel(s, ch);
            continue;
#endif
            printf("%s: Bus time-out in DMA%i operation\n",
                            __FUNCTION__, s->chans - n);
        }

        status = ch->status;
        while (status == ch->status && ch->active) {
            /* Transfer a single element */
            /* FIXME: check the endianness */
            if (!ch->constant_fill)
                cpu_physical_memory_read(a->src, value, ch->data_type);
            else
                *(uint32_t *) value = ch->color;

            if (!ch->transparent_copy ||
                    *(uint32_t *) value != ch->color)
                cpu_physical_memory_write(a->dest, value, ch->data_type);

            a->src += a->elem_delta[0];
            a->dest += a->elem_delta[1];
            a->element ++;

            /* If the channel is element synchronized, deactivate it */
            if (ch->sync && !ch->fs && !ch->bs)
                omap_dma_deactivate_channel(s, ch);

            /* If it is the last frame, set the LAST_FRAME interrupt */
            if (a->element == 1 && a->frame == a->frames - 1)
                if (ch->interrupts & LAST_FRAME_INTR)
                    ch->status |= LAST_FRAME_INTR;

            /* If the half of the frame was reached, set the HALF_FRAME
               interrupt */
            if (a->element == (a->elements >> 1))
                if (ch->interrupts & HALF_FRAME_INTR)
                    ch->status |= HALF_FRAME_INTR;

            if (a->element == a->elements) {
                /* End of Frame */
                a->element = 0;
                a->src += a->frame_delta[0];
                a->dest += a->frame_delta[1];
                a->frame ++;

                /* If the channel is frame synchronized, deactivate it */
                if (ch->sync && ch->fs)
                    omap_dma_deactivate_channel(s, ch);

                /* If the channel is async, update cpc */
                if (!ch->sync)
                    ch->cpc = a->dest & 0xffff;

                /* Set the END_FRAME interrupt */
                if (ch->interrupts & END_FRAME_INTR)
                    ch->status |= END_FRAME_INTR;

                if (a->frame == a->frames) {
                    /* End of Block */
                    /* Disable the channel */

                    if (ch->omap_3_1_compatible_disable) {
                        omap_dma_disable_channel(s, ch);
                        if (ch->link_enabled)
                            omap_dma_enable_channel(s,
                                            &s->ch[ch->link_next_ch]);
                    } else {
                        if (!ch->auto_init)
                            omap_dma_disable_channel(s, ch);
                        else if (ch->repeat || ch->end_prog)
                            omap_dma_channel_load(s, ch);
                        else {
                            ch->waiting_end_prog = 1;
                            omap_dma_deactivate_channel(s, ch);
                        }
                    }

                    if (ch->interrupts & END_BLOCK_INTR)
                        ch->status |= END_BLOCK_INTR;
                }
            }
        }
    }

    omap_dma_interrupts_update(s);
    if (s->run_count && s->delay)
        qemu_mod_timer(s->tm, qemu_get_clock(vm_clock) + s->delay);
}

void omap_dma_reset(struct omap_dma_s *s)
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
    omap_dma_enable_3_1_mapping(s);
    for (i = 0; i < s->chans; i ++) {
        memset(&s->ch[i].burst, 0, sizeof(s->ch[i].burst));
        memset(&s->ch[i].port, 0, sizeof(s->ch[i].port));
        memset(&s->ch[i].mode, 0, sizeof(s->ch[i].mode));
        memset(&s->ch[i].elements, 0, sizeof(s->ch[i].elements));
        memset(&s->ch[i].frames, 0, sizeof(s->ch[i].frames));
        memset(&s->ch[i].frame_index, 0, sizeof(s->ch[i].frame_index));
        memset(&s->ch[i].element_index, 0, sizeof(s->ch[i].element_index));
        memset(&s->ch[i].data_type, 0, sizeof(s->ch[i].data_type));
        memset(&s->ch[i].transparent_copy, 0,
                        sizeof(s->ch[i].transparent_copy));
        memset(&s->ch[i].constant_fill, 0, sizeof(s->ch[i].constant_fill));
        memset(&s->ch[i].color, 0, sizeof(s->ch[i].color));
        memset(&s->ch[i].end_prog, 0, sizeof(s->ch[i].end_prog));
        memset(&s->ch[i].repeat, 0, sizeof(s->ch[i].repeat));
        memset(&s->ch[i].auto_init, 0, sizeof(s->ch[i].auto_init));
        memset(&s->ch[i].link_enabled, 0, sizeof(s->ch[i].link_enabled));
        memset(&s->ch[i].link_next_ch, 0, sizeof(s->ch[i].link_next_ch));
        s->ch[i].interrupts = 0x0003;
        memset(&s->ch[i].status, 0, sizeof(s->ch[i].status));
        memset(&s->ch[i].active, 0, sizeof(s->ch[i].active));
        memset(&s->ch[i].enable, 0, sizeof(s->ch[i].enable));
        memset(&s->ch[i].sync, 0, sizeof(s->ch[i].sync));
        memset(&s->ch[i].pending_request, 0, sizeof(s->ch[i].pending_request));
        memset(&s->ch[i].waiting_end_prog, 0,
                        sizeof(s->ch[i].waiting_end_prog));
        memset(&s->ch[i].cpc, 0, sizeof(s->ch[i].cpc));
        memset(&s->ch[i].fs, 0, sizeof(s->ch[i].fs));
        memset(&s->ch[i].bs, 0, sizeof(s->ch[i].bs));
        memset(&s->ch[i].omap_3_1_compatible_disable, 0,
                        sizeof(s->ch[i].omap_3_1_compatible_disable));
        memset(&s->ch[i].active_set, 0, sizeof(s->ch[i].active_set));
        memset(&s->ch[i].priority, 0, sizeof(s->ch[i].priority));
        memset(&s->ch[i].interleave_disabled, 0,
                        sizeof(s->ch[i].interleave_disabled));
        memset(&s->ch[i].type, 0, sizeof(s->ch[i].type));
    }
}

static int omap_dma_ch_reg_read(struct omap_dma_s *s,
                struct omap_dma_channel_s *ch, int reg, uint16_t *value)
{
    switch (reg) {
    case 0x00:	/* SYS_DMA_CSDP_CH0 */
        *value = (ch->burst[1] << 14) |
                (ch->pack[1] << 13) |
                (ch->port[1] << 9) |
                (ch->burst[0] << 7) |
                (ch->pack[0] << 6) |
                (ch->port[0] << 2) |
                (ch->data_type >> 1);
        break;

    case 0x02:	/* SYS_DMA_CCR_CH0 */
        if (s->model == omap_dma_3_1)
            *value = 0 << 10;			/* FIFO_FLUSH reads as 0 */
        else
            *value = ch->omap_3_1_compatible_disable << 10;
        *value |= (ch->mode[1] << 14) |
                (ch->mode[0] << 12) |
                (ch->end_prog << 11) |
                (ch->repeat << 9) |
                (ch->auto_init << 8) |
                (ch->enable << 7) |
                (ch->priority << 6) |
                (ch->fs << 5) | ch->sync;
        break;

    case 0x04:	/* SYS_DMA_CICR_CH0 */
        *value = ch->interrupts;
        break;

    case 0x06:	/* SYS_DMA_CSR_CH0 */
        *value = ch->status;
        ch->status &= SYNC;
        if (!ch->omap_3_1_compatible_disable && ch->sibling) {
            *value |= (ch->sibling->status & 0x3f) << 6;
            ch->sibling->status &= SYNC;
        }
        qemu_irq_lower(ch->irq);
        break;

    case 0x08:	/* SYS_DMA_CSSA_L_CH0 */
        *value = ch->addr[0] & 0x0000ffff;
        break;

    case 0x0a:	/* SYS_DMA_CSSA_U_CH0 */
        *value = ch->addr[0] >> 16;
        break;

    case 0x0c:	/* SYS_DMA_CDSA_L_CH0 */
        *value = ch->addr[1] & 0x0000ffff;
        break;

    case 0x0e:	/* SYS_DMA_CDSA_U_CH0 */
        *value = ch->addr[1] >> 16;
        break;

    case 0x10:	/* SYS_DMA_CEN_CH0 */
        *value = ch->elements;
        break;

    case 0x12:	/* SYS_DMA_CFN_CH0 */
        *value = ch->frames;
        break;

    case 0x14:	/* SYS_DMA_CFI_CH0 */
        *value = ch->frame_index[0];
        break;

    case 0x16:	/* SYS_DMA_CEI_CH0 */
        *value = ch->element_index[0];
        break;

    case 0x18:	/* SYS_DMA_CPC_CH0 or DMA_CSAC */
        if (ch->omap_3_1_compatible_disable)
            *value = ch->active_set.src & 0xffff;	/* CSAC */
        else
            *value = ch->cpc;
        break;

    case 0x1a:	/* DMA_CDAC */
        *value = ch->active_set.dest & 0xffff;	/* CDAC */
        break;

    case 0x1c:	/* DMA_CDEI */
        *value = ch->element_index[1];
        break;

    case 0x1e:	/* DMA_CDFI */
        *value = ch->frame_index[1];
        break;

    case 0x20:	/* DMA_COLOR_L */
        *value = ch->color & 0xffff;
        break;

    case 0x22:	/* DMA_COLOR_U */
        *value = ch->color >> 16;
        break;

    case 0x24:	/* DMA_CCR2 */
        *value = (ch->bs << 2) |
                (ch->transparent_copy << 1) |
                ch->constant_fill;
        break;

    case 0x28:	/* DMA_CLNK_CTRL */
        *value = (ch->link_enabled << 15) |
                (ch->link_next_ch & 0xf);
        break;

    case 0x2a:	/* DMA_LCH_CTRL */
        *value = (ch->interleave_disabled << 15) |
                ch->type;
        break;

    default:
        return 1;
    }
    return 0;
}

static int omap_dma_ch_reg_write(struct omap_dma_s *s,
                struct omap_dma_channel_s *ch, int reg, uint16_t value)
{
    switch (reg) {
    case 0x00:	/* SYS_DMA_CSDP_CH0 */
        ch->burst[1] = (value & 0xc000) >> 14;
        ch->pack[1] = (value & 0x2000) >> 13;
        ch->port[1] = (enum omap_dma_port) ((value & 0x1e00) >> 9);
        ch->burst[0] = (value & 0x0180) >> 7;
        ch->pack[0] = (value & 0x0040) >> 6;
        ch->port[0] = (enum omap_dma_port) ((value & 0x003c) >> 2);
        ch->data_type = (1 << (value & 3));
        if (ch->port[0] >= omap_dma_port_last)
            printf("%s: invalid DMA port %i\n", __FUNCTION__,
                            ch->port[0]);
        if (ch->port[1] >= omap_dma_port_last)
            printf("%s: invalid DMA port %i\n", __FUNCTION__,
                            ch->port[1]);
        if ((value & 3) == 3)
            printf("%s: bad data_type for DMA channel\n", __FUNCTION__);
        break;

    case 0x02:	/* SYS_DMA_CCR_CH0 */
        ch->mode[1] = (omap_dma_addressing_t) ((value & 0xc000) >> 14);
        ch->mode[0] = (omap_dma_addressing_t) ((value & 0x3000) >> 12);
        ch->end_prog = (value & 0x0800) >> 11;
        if (s->model > omap_dma_3_1)
            ch->omap_3_1_compatible_disable  = (value >> 10) & 0x1;
        ch->repeat = (value & 0x0200) >> 9;
        ch->auto_init = (value & 0x0100) >> 8;
        ch->priority = (value & 0x0040) >> 6;
        ch->fs = (value & 0x0020) >> 5;
        ch->sync = value & 0x001f;

        if (value & 0x0080)
            omap_dma_enable_channel(s, ch);
        else
            omap_dma_disable_channel(s, ch);

        if (ch->end_prog)
            omap_dma_channel_end_prog(s, ch);

        break;

    case 0x04:	/* SYS_DMA_CICR_CH0 */
        ch->interrupts = value;
        break;

    case 0x06:	/* SYS_DMA_CSR_CH0 */
        OMAP_RO_REG((target_phys_addr_t) reg);
        break;

    case 0x08:	/* SYS_DMA_CSSA_L_CH0 */
        ch->addr[0] &= 0xffff0000;
        ch->addr[0] |= value;
        break;

    case 0x0a:	/* SYS_DMA_CSSA_U_CH0 */
        ch->addr[0] &= 0x0000ffff;
        ch->addr[0] |= (uint32_t) value << 16;
        break;

    case 0x0c:	/* SYS_DMA_CDSA_L_CH0 */
        ch->addr[1] &= 0xffff0000;
        ch->addr[1] |= value;
        break;

    case 0x0e:	/* SYS_DMA_CDSA_U_CH0 */
        ch->addr[1] &= 0x0000ffff;
        ch->addr[1] |= (uint32_t) value << 16;
        break;

    case 0x10:	/* SYS_DMA_CEN_CH0 */
        ch->elements = value;
        break;

    case 0x12:	/* SYS_DMA_CFN_CH0 */
        ch->frames = value;
        break;

    case 0x14:	/* SYS_DMA_CFI_CH0 */
        ch->frame_index[0] = (int16_t) value;
        break;

    case 0x16:	/* SYS_DMA_CEI_CH0 */
        ch->element_index[0] = (int16_t) value;
        break;

    case 0x18:	/* SYS_DMA_CPC_CH0 or DMA_CSAC */
        OMAP_RO_REG((target_phys_addr_t) reg);
        break;

    case 0x1c:	/* DMA_CDEI */
        ch->element_index[1] = (int16_t) value;
        break;

    case 0x1e:	/* DMA_CDFI */
        ch->frame_index[1] = (int16_t) value;
        break;

    case 0x20:	/* DMA_COLOR_L */
        ch->color &= 0xffff0000;
        ch->color |= value;
        break;

    case 0x22:	/* DMA_COLOR_U */
        ch->color &= 0xffff;
        ch->color |= value << 16;
        break;

    case 0x24:	/* DMA_CCR2 */
        ch->bs  = (value >> 2) & 0x1;
        ch->transparent_copy = (value >> 1) & 0x1;
        ch->constant_fill = value & 0x1;
        break;

    case 0x28:	/* DMA_CLNK_CTRL */
        ch->link_enabled = (value >> 15) & 0x1;
        if (value & (1 << 14)) {			/* Stop_Lnk */
            ch->link_enabled = 0;
            omap_dma_disable_channel(s, ch);
        }
        ch->link_next_ch = value & 0x1f;
        break;

    case 0x2a:	/* DMA_LCH_CTRL */
        ch->interleave_disabled = (value >> 15) & 0x1;
        ch->type = value & 0xf;
        break;

    default:
        return 1;
    }
    return 0;
}

static int omap_dma_3_2_lcd_write(struct omap_dma_lcd_channel_s *s, int offset,
                uint16_t value)
{
    switch (offset) {
    case 0xbc0:	/* DMA_LCD_CSDP */
        s->brust_f2 = (value >> 14) & 0x3;
        s->pack_f2 = (value >> 13) & 0x1;
        s->data_type_f2 = (1 << ((value >> 11) & 0x3));
        s->brust_f1 = (value >> 7) & 0x3;
        s->pack_f1 = (value >> 6) & 0x1;
        s->data_type_f1 = (1 << ((value >> 0) & 0x3));
        break;

    case 0xbc2:	/* DMA_LCD_CCR */
        s->mode_f2 = (value >> 14) & 0x3;
        s->mode_f1 = (value >> 12) & 0x3;
        s->end_prog = (value >> 11) & 0x1;
        s->omap_3_1_compatible_disable = (value >> 10) & 0x1;
        s->repeat = (value >> 9) & 0x1;
        s->auto_init = (value >> 8) & 0x1;
        s->running = (value >> 7) & 0x1;
        s->priority = (value >> 6) & 0x1;
        s->bs = (value >> 4) & 0x1;
        break;

    case 0xbc4:	/* DMA_LCD_CTRL */
        s->dst = (value >> 8) & 0x1;
        s->src = ((value >> 6) & 0x3) << 1;
        s->condition = 0;
        /* Assume no bus errors and thus no BUS_ERROR irq bits.  */
        s->interrupts = (value >> 1) & 1;
        s->dual = value & 1;
        break;

    case 0xbc8:	/* TOP_B1_L */
        s->src_f1_top &= 0xffff0000;
        s->src_f1_top |= 0x0000ffff & value;
        break;

    case 0xbca:	/* TOP_B1_U */
        s->src_f1_top &= 0x0000ffff;
        s->src_f1_top |= value << 16;
        break;

    case 0xbcc:	/* BOT_B1_L */
        s->src_f1_bottom &= 0xffff0000;
        s->src_f1_bottom |= 0x0000ffff & value;
        break;

    case 0xbce:	/* BOT_B1_U */
        s->src_f1_bottom &= 0x0000ffff;
        s->src_f1_bottom |= (uint32_t) value << 16;
        break;

    case 0xbd0:	/* TOP_B2_L */
        s->src_f2_top &= 0xffff0000;
        s->src_f2_top |= 0x0000ffff & value;
        break;

    case 0xbd2:	/* TOP_B2_U */
        s->src_f2_top &= 0x0000ffff;
        s->src_f2_top |= (uint32_t) value << 16;
        break;

    case 0xbd4:	/* BOT_B2_L */
        s->src_f2_bottom &= 0xffff0000;
        s->src_f2_bottom |= 0x0000ffff & value;
        break;

    case 0xbd6:	/* BOT_B2_U */
        s->src_f2_bottom &= 0x0000ffff;
        s->src_f2_bottom |= (uint32_t) value << 16;
        break;

    case 0xbd8:	/* DMA_LCD_SRC_EI_B1 */
        s->element_index_f1 = value;
        break;

    case 0xbda:	/* DMA_LCD_SRC_FI_B1_L */
        s->frame_index_f1 &= 0xffff0000;
        s->frame_index_f1 |= 0x0000ffff & value;
        break;

    case 0xbf4:	/* DMA_LCD_SRC_FI_B1_U */
        s->frame_index_f1 &= 0x0000ffff;
        s->frame_index_f1 |= (uint32_t) value << 16;
        break;

    case 0xbdc:	/* DMA_LCD_SRC_EI_B2 */
        s->element_index_f2 = value;
        break;

    case 0xbde:	/* DMA_LCD_SRC_FI_B2_L */
        s->frame_index_f2 &= 0xffff0000;
        s->frame_index_f2 |= 0x0000ffff & value;
        break;

    case 0xbf6:	/* DMA_LCD_SRC_FI_B2_U */
        s->frame_index_f2 &= 0x0000ffff;
        s->frame_index_f2 |= (uint32_t) value << 16;
        break;

    case 0xbe0:	/* DMA_LCD_SRC_EN_B1 */
        s->elements_f1 = value;
        break;

    case 0xbe4:	/* DMA_LCD_SRC_FN_B1 */
        s->frames_f1 = value;
        break;

    case 0xbe2:	/* DMA_LCD_SRC_EN_B2 */
        s->elements_f2 = value;
        break;

    case 0xbe6:	/* DMA_LCD_SRC_FN_B2 */
        s->frames_f2 = value;
        break;

    case 0xbea:	/* DMA_LCD_LCH_CTRL */
        s->lch_type = value & 0xf;
        break;

    default:
        return 1;
    }
    return 0;
}

static int omap_dma_3_2_lcd_read(struct omap_dma_lcd_channel_s *s, int offset,
                uint16_t *ret)
{
    switch (offset) {
    case 0xbc0:	/* DMA_LCD_CSDP */
        *ret = (s->brust_f2 << 14) |
            (s->pack_f2 << 13) |
            ((s->data_type_f2 >> 1) << 11) |
            (s->brust_f1 << 7) |
            (s->pack_f1 << 6) |
            ((s->data_type_f1 >> 1) << 0);
        break;

    case 0xbc2:	/* DMA_LCD_CCR */
        *ret = (s->mode_f2 << 14) |
            (s->mode_f1 << 12) |
            (s->end_prog << 11) |
            (s->omap_3_1_compatible_disable << 10) |
            (s->repeat << 9) |
            (s->auto_init << 8) |
            (s->running << 7) |
            (s->priority << 6) |
            (s->bs << 4);
        break;

    case 0xbc4:	/* DMA_LCD_CTRL */
        qemu_irq_lower(s->irq);
        *ret = (s->dst << 8) |
            ((s->src & 0x6) << 5) |
            (s->condition << 3) |
            (s->interrupts << 1) |
            s->dual;
        break;

    case 0xbc8:	/* TOP_B1_L */
        *ret = s->src_f1_top & 0xffff;
        break;

    case 0xbca:	/* TOP_B1_U */
        *ret = s->src_f1_top >> 16;
        break;

    case 0xbcc:	/* BOT_B1_L */
        *ret = s->src_f1_bottom & 0xffff;
        break;

    case 0xbce:	/* BOT_B1_U */
        *ret = s->src_f1_bottom >> 16;
        break;

    case 0xbd0:	/* TOP_B2_L */
        *ret = s->src_f2_top & 0xffff;
        break;

    case 0xbd2:	/* TOP_B2_U */
        *ret = s->src_f2_top >> 16;
        break;

    case 0xbd4:	/* BOT_B2_L */
        *ret = s->src_f2_bottom & 0xffff;
        break;

    case 0xbd6:	/* BOT_B2_U */
        *ret = s->src_f2_bottom >> 16;
        break;

    case 0xbd8:	/* DMA_LCD_SRC_EI_B1 */
        *ret = s->element_index_f1;
        break;

    case 0xbda:	/* DMA_LCD_SRC_FI_B1_L */
        *ret = s->frame_index_f1 & 0xffff;
        break;

    case 0xbf4:	/* DMA_LCD_SRC_FI_B1_U */
        *ret = s->frame_index_f1 >> 16;
        break;

    case 0xbdc:	/* DMA_LCD_SRC_EI_B2 */
        *ret = s->element_index_f2;
        break;

    case 0xbde:	/* DMA_LCD_SRC_FI_B2_L */
        *ret = s->frame_index_f2 & 0xffff;
        break;

    case 0xbf6:	/* DMA_LCD_SRC_FI_B2_U */
        *ret = s->frame_index_f2 >> 16;
        break;

    case 0xbe0:	/* DMA_LCD_SRC_EN_B1 */
        *ret = s->elements_f1;
        break;

    case 0xbe4:	/* DMA_LCD_SRC_FN_B1 */
        *ret = s->frames_f1;
        break;

    case 0xbe2:	/* DMA_LCD_SRC_EN_B2 */
        *ret = s->elements_f2;
        break;

    case 0xbe6:	/* DMA_LCD_SRC_FN_B2 */
        *ret = s->frames_f2;
        break;

    case 0xbea:	/* DMA_LCD_LCH_CTRL */
        *ret = s->lch_type;
        break;

    default:
        return 1;
    }
    return 0;
}

static int omap_dma_3_1_lcd_write(struct omap_dma_lcd_channel_s *s, int offset,
                uint16_t value)
{
    switch (offset) {
    case 0x300:	/* SYS_DMA_LCD_CTRL */
        s->src = (value & 0x40) ? imif : emiff;
        s->condition = 0;
        /* Assume no bus errors and thus no BUS_ERROR irq bits.  */
        s->interrupts = (value >> 1) & 1;
        s->dual = value & 1;
        break;

    case 0x302:	/* SYS_DMA_LCD_TOP_F1_L */
        s->src_f1_top &= 0xffff0000;
        s->src_f1_top |= 0x0000ffff & value;
        break;

    case 0x304:	/* SYS_DMA_LCD_TOP_F1_U */
        s->src_f1_top &= 0x0000ffff;
        s->src_f1_top |= value << 16;
        break;

    case 0x306:	/* SYS_DMA_LCD_BOT_F1_L */
        s->src_f1_bottom &= 0xffff0000;
        s->src_f1_bottom |= 0x0000ffff & value;
        break;

    case 0x308:	/* SYS_DMA_LCD_BOT_F1_U */
        s->src_f1_bottom &= 0x0000ffff;
        s->src_f1_bottom |= value << 16;
        break;

    case 0x30a:	/* SYS_DMA_LCD_TOP_F2_L */
        s->src_f2_top &= 0xffff0000;
        s->src_f2_top |= 0x0000ffff & value;
        break;

    case 0x30c:	/* SYS_DMA_LCD_TOP_F2_U */
        s->src_f2_top &= 0x0000ffff;
        s->src_f2_top |= value << 16;
        break;

    case 0x30e:	/* SYS_DMA_LCD_BOT_F2_L */
        s->src_f2_bottom &= 0xffff0000;
        s->src_f2_bottom |= 0x0000ffff & value;
        break;

    case 0x310:	/* SYS_DMA_LCD_BOT_F2_U */
        s->src_f2_bottom &= 0x0000ffff;
        s->src_f2_bottom |= value << 16;
        break;

    default:
        return 1;
    }
    return 0;
}

static int omap_dma_3_1_lcd_read(struct omap_dma_lcd_channel_s *s, int offset,
                uint16_t *ret)
{
    int i;

    switch (offset) {
    case 0x300:	/* SYS_DMA_LCD_CTRL */
        i = s->condition;
        s->condition = 0;
        qemu_irq_lower(s->irq);
        *ret = ((s->src == imif) << 6) | (i << 3) |
                (s->interrupts << 1) | s->dual;
        break;

    case 0x302:	/* SYS_DMA_LCD_TOP_F1_L */
        *ret = s->src_f1_top & 0xffff;
        break;

    case 0x304:	/* SYS_DMA_LCD_TOP_F1_U */
        *ret = s->src_f1_top >> 16;
        break;

    case 0x306:	/* SYS_DMA_LCD_BOT_F1_L */
        *ret = s->src_f1_bottom & 0xffff;
        break;

    case 0x308:	/* SYS_DMA_LCD_BOT_F1_U */
        *ret = s->src_f1_bottom >> 16;
        break;

    case 0x30a:	/* SYS_DMA_LCD_TOP_F2_L */
        *ret = s->src_f2_top & 0xffff;
        break;

    case 0x30c:	/* SYS_DMA_LCD_TOP_F2_U */
        *ret = s->src_f2_top >> 16;
        break;

    case 0x30e:	/* SYS_DMA_LCD_BOT_F2_L */
        *ret = s->src_f2_bottom & 0xffff;
        break;

    case 0x310:	/* SYS_DMA_LCD_BOT_F2_U */
        *ret = s->src_f2_bottom >> 16;
        break;

    default:
        return 1;
    }
    return 0;
}

static int omap_dma_sys_write(struct omap_dma_s *s, int offset, uint16_t value)
{
    switch (offset) {
    case 0x400:	/* SYS_DMA_GCR */
        s->gcr = value;
        break;

    case 0x404:	/* DMA_GSCR */
        if (value & 0x8)
            omap_dma_disable_3_1_mapping(s);
        else
            omap_dma_enable_3_1_mapping(s);
        break;

    case 0x408:	/* DMA_GRST */
        if (value & 0x1)
            omap_dma_reset(s);
        break;

    default:
        return 1;
    }
    return 0;
}

static int omap_dma_sys_read(struct omap_dma_s *s, int offset,
                uint16_t *ret)
{
    switch (offset) {
    case 0x400:	/* SYS_DMA_GCR */
        *ret = s->gcr;
        break;

    case 0x404:	/* DMA_GSCR */
        *ret = s->omap_3_1_mapping_disabled << 3;
        break;

    case 0x408:	/* DMA_GRST */
        *ret = 0;
        break;

    case 0x442:	/* DMA_HW_ID */
    case 0x444:	/* DMA_PCh2_ID */
    case 0x446:	/* DMA_PCh0_ID */
    case 0x448:	/* DMA_PCh1_ID */
    case 0x44a:	/* DMA_PChG_ID */
    case 0x44c:	/* DMA_PChD_ID */
        *ret = 1;
        break;

    case 0x44e:	/* DMA_CAPS_0_U */
        *ret = (1 << 3) | /* Constant Fill Capacity */
            (1 << 2);     /* Transparent BLT Capacity */
        break;

    case 0x450:	/* DMA_CAPS_0_L */
    case 0x452:	/* DMA_CAPS_1_U */
        *ret = 0;
        break;

    case 0x454:	/* DMA_CAPS_1_L */
        *ret = (1 << 1); /* 1-bit palletized capability */
        break;

    case 0x456:	/* DMA_CAPS_2 */
        *ret = (1 << 8) | /* SSDIC */
            (1 << 7) |    /* DDIAC */
            (1 << 6) |    /* DSIAC */
            (1 << 5) |    /* DPIAC */
            (1 << 4) |    /* DCAC  */
            (1 << 3) |    /* SDIAC */
            (1 << 2) |    /* SSIAC */
            (1 << 1) |    /* SPIAC */
            1;            /* SCAC  */
        break;

    case 0x458:	/* DMA_CAPS_3 */
        *ret = (1 << 5) | /* CCC */
            (1 << 4) |    /* IC  */
            (1 << 3) |    /* ARC */
            (1 << 2) |    /* AEC */
            (1 << 1) |    /* FSC */
            1;            /* ESC */
        break;

    case 0x45a:	/* DMA_CAPS_4 */
        *ret = (1 << 6) | /* SSC  */
            (1 << 5) |    /* BIC  */
            (1 << 4) |    /* LFIC */
            (1 << 3) |    /* FIC  */
            (1 << 2) |    /* HFIC */
            (1 << 1) |    /* EDIC */
            1;            /* TOIC */
        break;

    case 0x460:	/* DMA_PCh2_SR */
    case 0x480:	/* DMA_PCh0_SR */
    case 0x482:	/* DMA_PCh1_SR */
    case 0x4c0:	/* DMA_PChD_SR_0 */
        printf("%s: Physical Channel Status Registers not implemented.\n",
               __FUNCTION__);
        *ret = 0xff;
        break;

    default:
        return 1;
    }
    return 0;
}

static uint32_t omap_dma_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_dma_s *s = (struct omap_dma_s *) opaque;
    int reg, ch, offset = addr - s->base;
    uint16_t ret;

    switch (offset) {
    case 0x300 ... 0x3fe:
        if (s->model == omap_dma_3_1 || !s->omap_3_1_mapping_disabled) {
            if (omap_dma_3_1_lcd_read(&s->lcd_ch, offset, &ret))
                break;
            return ret;
        }
        /* Fall through. */
    case 0x000 ... 0x2fe:
        reg = offset & 0x3f;
        ch = (offset >> 6) & 0x0f;
        if (omap_dma_ch_reg_read(s, &s->ch[ch], reg, &ret))
            break;
        return ret;

    case 0x404 ... 0x4fe:
        if (s->model == omap_dma_3_1)
            break;
        /* Fall through. */
    case 0x400:
        if (omap_dma_sys_read(s, offset, &ret))
            break;
        return ret;

    case 0xb00 ... 0xbfe:
        if (s->model == omap_dma_3_2 && s->omap_3_1_mapping_disabled) {
            if (omap_dma_3_2_lcd_read(&s->lcd_ch, offset, &ret))
                break;
            return ret;
        }
        break;
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
    case 0x300 ... 0x3fe:
        if (s->model == omap_dma_3_1 || !s->omap_3_1_mapping_disabled) {
            if (omap_dma_3_1_lcd_write(&s->lcd_ch, offset, value))
                break;
            return;
        }
        /* Fall through.  */
    case 0x000 ... 0x2fe:
        reg = offset & 0x3f;
        ch = (offset >> 6) & 0x0f;
        if (omap_dma_ch_reg_write(s, &s->ch[ch], reg, value))
            break;
        return;

    case 0x404 ... 0x4fe:
        if (s->model == omap_dma_3_1)
            break;
    case 0x400:
        /* Fall through. */
        if (omap_dma_sys_write(s, offset, value))
            break;
        return;

    case 0xb00 ... 0xbfe:
        if (s->model == omap_dma_3_2 && s->omap_3_1_mapping_disabled) {
            if (omap_dma_3_2_lcd_write(&s->lcd_ch, offset, value))
                break;
            return;
        }
        break;
    }

    OMAP_BAD_REG(addr);
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
            omap_dma_process_request(s, drq);
        }
    } else
        s->drq &= ~(1 << drq);
}

static void omap_dma_clk_update(void *opaque, int line, int on)
{
    struct omap_dma_s *s = (struct omap_dma_s *) opaque;

    if (on) {
        /* TODO: make a clever calculation */
        s->delay = ticks_per_sec >> 8;
        if (s->run_count)
            qemu_mod_timer(s->tm, qemu_get_clock(vm_clock) + s->delay);
    } else {
        s->delay = 0;
        qemu_del_timer(s->tm);
    }
}

struct omap_dma_s *omap_dma_init(target_phys_addr_t base, qemu_irq *irqs,
                qemu_irq lcd_irq, struct omap_mpu_state_s *mpu, omap_clk clk,
                enum omap_dma_model model)
{
    int iomemtype, num_irqs, memsize, i;
    struct omap_dma_s *s = (struct omap_dma_s *)
            qemu_mallocz(sizeof(struct omap_dma_s));

    if (model == omap_dma_3_1) {
        num_irqs = 6;
        memsize = 0x800;
    } else {
        num_irqs = 16;
        memsize = 0xc00;
    }
    s->base = base;
    s->model = model;
    s->mpu = mpu;
    s->clk = clk;
    s->lcd_ch.irq = lcd_irq;
    s->lcd_ch.mpu = mpu;
    while (num_irqs --)
        s->ch[num_irqs].irq = irqs[num_irqs];
    for (i = 0; i < 3; i ++) {
        s->ch[i].sibling = &s->ch[i + 6];
        s->ch[i + 6].sibling = &s->ch[i];
    }
    s->tm = qemu_new_timer(vm_clock, (QEMUTimerCB *) omap_dma_channel_run, s);
    omap_clk_adduser(s->clk, qemu_allocate_irqs(omap_dma_clk_update, s, 1)[0]);
    mpu->drq = qemu_allocate_irqs(omap_dma_request, s, 32);
    omap_dma_reset(s);
    omap_dma_clk_update(s, 0, 1);

    iomemtype = cpu_register_io_memory(0, omap_dma_readfn,
                    omap_dma_writefn, s);
    cpu_register_physical_memory(s->base, memsize, iomemtype);

    return s;
}

struct omap_dma_lcd_channel_s *omap_dma_get_lcdch(struct omap_dma_s *s)
{
    return &s->lcd_ch;
}
