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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "hw/arm/omap.h"
#include "hw/core/irq.h"
#include "hw/arm/soc_dma.h"
#include "exec/cpu-common.h"

struct omap_dma_channel_s {
    /* transfer data */
    int burst[2];
    int pack[2];
    int endian[2];
    int endian_lock[2];
    int translate[2];
    enum omap_dma_port port[2];
    hwaddr addr[2];
    omap_dma_addressing_t mode[2];
    uint32_t elements;
    uint16_t frames;
    int32_t frame_index[2];
    int16_t element_index[2];
    int data_type;

    /* transfer type */
    int transparent_copy;
    int constant_fill;
    uint32_t color;
    int prefetch;

    /* auto init and linked channel data */
    int end_prog;
    int repeat;
    int auto_init;
    int link_enabled;
    int link_next_ch;

    /* interruption data */
    int interrupts;
    int status;
    int cstatus;

    /* state data */
    int active;
    int enable;
    int sync;
    int src_sync;
    int pending_request;
    int waiting_end_prog;
    uint16_t cpc;
    int set_update;

    /* sync type */
    int fs;
    int bs;

    qemu_irq irq;
    struct omap_dma_channel_s *sibling;

    struct omap_dma_reg_set_s {
        hwaddr src, dest;
        int frame;
        int element;
        int pck_element;
        int frame_delta[2];
        int elem_delta[2];
        int frames;
        int elements;
        int pck_elements;
    } active_set;

    struct soc_dma_ch_s *dma;

    /* unused parameters */
    int write_mode;
    int priority;
    int interleave_disabled;
    int type;
    int suspend;
    int buf_disable;
};

struct omap_dma_s {
    struct soc_dma_s *dma;
    MemoryRegion iomem;

    struct omap_mpu_state_s *mpu;
    omap_clk clk;
    qemu_irq irq[4];

    uint32_t gcr;
    uint32_t ocp;
    uint32_t caps[5];
    uint32_t irqen[4];
    uint32_t irqstat[4];

    int chans;
    struct omap_dma_channel_s ch[32];
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
#define END_PKT_INTR    (1 << 7)
#define TRANS_ERR_INTR  (1 << 8)
#define MISALIGN_INTR   (1 << 11)

static inline void omap_dma_interrupts_update(struct omap_dma_s *s)
{
    struct omap_dma_channel_s *ch = s->ch;

    /* First three interrupts are shared between two channels each. */
    if (ch[0].status | ch[6].status) {
        qemu_irq_raise(ch[0].irq);
    }
    if (ch[1].status | ch[7].status) {
        qemu_irq_raise(ch[1].irq);
    }
    if (ch[2].status | ch[8].status) {
        qemu_irq_raise(ch[2].irq);
    }
    if (ch[3].status) {
        qemu_irq_raise(ch[3].irq);
    }
    if (ch[4].status) {
        qemu_irq_raise(ch[4].irq);
    }
    if (ch[5].status) {
        qemu_irq_raise(ch[5].irq);
    }
}

static void omap_dma_channel_load(struct omap_dma_channel_s *ch)
{
    struct omap_dma_reg_set_s *a = &ch->active_set;
    int i, normal;

    /*
     * TODO: verify address ranges and alignment
     * TODO: port endianness
     */

    a->src = ch->addr[0];
    a->dest = ch->addr[1];
    a->frames = ch->frames;
    a->elements = ch->elements;
    a->pck_elements = ch->frame_index[!ch->src_sync];
    a->frame = 0;
    a->element = 0;
    a->pck_element = 0;

    if (unlikely(!ch->elements || !ch->frames)) {
        printf("%s: bad DMA request\n", __func__);
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
                    ch->element_index[0] - 1;
            a->frame_delta[i] = 0;
            break;
        case double_index:
            a->elem_delta[i] = ch->data_type +
                    ch->element_index[0] - 1;
            a->frame_delta[i] = ch->frame_index[0] -
                    ch->element_index[0];
            break;
        default:
            break;
        }

    normal = !ch->transparent_copy && !ch->constant_fill &&
            /* FIFO is big-endian so either (ch->endian[n] == 1) OR
             * (ch->endian_lock[n] == 1) mean no endianism conversion.  */
            (ch->endian[0] | ch->endian_lock[0]) ==
            (ch->endian[1] | ch->endian_lock[1]);
    for (i = 0; i < 2; i ++) {
        /* TODO: for a->frame_delta[i] > 0 still use the fast path, just
         * limit min_elems in omap_dma_transfer_setup to the nearest frame
         * end.  */
        if (!a->elem_delta[i] && normal &&
                        (a->frames == 1 || !a->frame_delta[i]))
            ch->dma->type[i] = soc_dma_access_const;
        else if (a->elem_delta[i] == ch->data_type && normal &&
                        (a->frames == 1 || !a->frame_delta[i]))
            ch->dma->type[i] = soc_dma_access_linear;
        else
            ch->dma->type[i] = soc_dma_access_other;

        ch->dma->vaddr[i] = ch->addr[i];
    }
    soc_dma_ch_update(ch->dma);
}

static void omap_dma_activate_channel(struct omap_dma_s *s,
                struct omap_dma_channel_s *ch)
{
    if (!ch->active) {
        if (ch->set_update) {
            /* It's not clear when the active set is supposed to be
             * loaded from registers.  We're already loading it when the
             * channel is enabled, and for some guests this is not enough
             * but that may be also because of a race condition (no
             * delays in qemu) in the guest code, which we're just
             * working around here.  */
            omap_dma_channel_load(ch);
            ch->set_update = 0;
        }

        ch->active = 1;
        soc_dma_set_request(ch->dma, 1);
        if (ch->sync)
            ch->status |= SYNC;
    }
}

static void omap_dma_deactivate_channel(struct omap_dma_s *s,
                struct omap_dma_channel_s *ch)
{
    /* Update cpc */
    ch->cpc = ch->active_set.dest & 0xffff;

    if (ch->pending_request && !ch->waiting_end_prog && ch->enable) {
        /* Don't deactivate the channel */
        ch->pending_request = 0;
        return;
    }

    /* Don't deactivate the channel if it is synchronized and the DMA request is
       active */
    if (ch->sync && ch->enable && (s->dma->drqbmp & (1ULL << ch->sync)))
        return;

    if (ch->active) {
        ch->active = 0;
        ch->status &= ~SYNC;
        soc_dma_set_request(ch->dma, 0);
    }
}

static void omap_dma_enable_channel(struct omap_dma_s *s,
                struct omap_dma_channel_s *ch)
{
    if (!ch->enable) {
        ch->enable = 1;
        ch->waiting_end_prog = 0;
        omap_dma_channel_load(ch);
        /* TODO: theoretically if ch->sync && ch->prefetch &&
         * !s->dma->drqbmp[ch->sync], we should also activate and fetch
         * from source and then stall until signalled.  */
        if ((!ch->sync) || (s->dma->drqbmp & (1ULL << ch->sync))) {
            omap_dma_activate_channel(s, ch);
        }
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

static void omap_dma_transfer_generic(struct soc_dma_ch_s *dma)
{
    uint8_t value[4];
    struct omap_dma_channel_s *ch = dma->opaque;
    struct omap_dma_reg_set_s *a = &ch->active_set;
    int bytes = dma->bytes;

    do {
        /* Transfer a single element */
        /* FIXME: check the endianness */
        if (!ch->constant_fill)
            cpu_physical_memory_read(a->src, value, ch->data_type);
        else
            *(uint32_t *) value = ch->color;

        if (!ch->transparent_copy || *(uint32_t *) value != ch->color)
            cpu_physical_memory_write(a->dest, value, ch->data_type);

        a->src += a->elem_delta[0];
        a->dest += a->elem_delta[1];
        a->element ++;

        if (a->element == a->elements) {
            /* End of Frame */
            a->element = 0;
            a->src += a->frame_delta[0];
            a->dest += a->frame_delta[1];
            a->frame ++;

            /* If the channel is async, update cpc */
            if (!ch->sync)
                ch->cpc = a->dest & 0xffff;
        }
    } while ((bytes -= ch->data_type));
}

enum {
    omap_dma_intr_element_sync,
    omap_dma_intr_last_frame,
    omap_dma_intr_half_frame,
    omap_dma_intr_frame,
    omap_dma_intr_frame_sync,
    omap_dma_intr_packet,
    omap_dma_intr_packet_sync,
    omap_dma_intr_block,
    __omap_dma_intr_last,
};

static void omap_dma_transfer_setup(struct soc_dma_ch_s *dma)
{
    struct omap_dma_port_if_s *src_p, *dest_p;
    struct omap_dma_reg_set_s *a;
    struct omap_dma_channel_s *ch = dma->opaque;
    struct omap_dma_s *s = dma->dma->opaque;
    int frames, min_elems, elements[__omap_dma_intr_last];

    a = &ch->active_set;

    src_p = &s->mpu->port[ch->port[0]];
    dest_p = &s->mpu->port[ch->port[1]];
    if ((!ch->constant_fill && !src_p->addr_valid(s->mpu, a->src)) ||
                    (!dest_p->addr_valid(s->mpu, a->dest))) {
        printf("%s: Bus time-out in DMA%i operation\n",
                        __func__, dma->num);
    }

    min_elems = INT_MAX;

    /* Check all the conditions that terminate the transfer starting
     * with those that can occur the soonest.  */
#define INTR_CHECK(cond, id, nelements) \
    if (cond) {         \
        elements[id] = nelements;   \
        if (elements[id] < min_elems)   \
            min_elems = elements[id];   \
    } else              \
        elements[id] = INT_MAX;

    /* Elements */
    INTR_CHECK(
                    ch->sync && !ch->fs && !ch->bs,
                    omap_dma_intr_element_sync,
                    1)

    /* Frames */
    /* TODO: for transfers where entire frames can be read and written
     * using memcpy() but a->frame_delta is non-zero, try to still do
     * transfers using soc_dma but limit min_elems to a->elements - ...
     * See also the TODO in omap_dma_channel_load.  */
    INTR_CHECK(
                    (ch->interrupts & LAST_FRAME_INTR) &&
                    ((a->frame < a->frames - 1) || !a->element),
                    omap_dma_intr_last_frame,
                    (a->frames - a->frame - 2) * a->elements +
                    (a->elements - a->element + 1))
    INTR_CHECK(
                    ch->interrupts & HALF_FRAME_INTR,
                    omap_dma_intr_half_frame,
                    (a->elements >> 1) +
                    (a->element >= (a->elements >> 1) ? a->elements : 0) -
                    a->element)
    INTR_CHECK(
                    ch->sync && ch->fs && (ch->interrupts & END_FRAME_INTR),
                    omap_dma_intr_frame,
                    a->elements - a->element)
    INTR_CHECK(
                    ch->sync && ch->fs && !ch->bs,
                    omap_dma_intr_frame_sync,
                    a->elements - a->element)

    /* Packets */
    INTR_CHECK(
                    ch->fs && ch->bs &&
                    (ch->interrupts & END_PKT_INTR) && !ch->src_sync,
                    omap_dma_intr_packet,
                    a->pck_elements - a->pck_element)
    INTR_CHECK(
                    ch->fs && ch->bs && ch->sync,
                    omap_dma_intr_packet_sync,
                    a->pck_elements - a->pck_element)

    /* Blocks */
    INTR_CHECK(
                    1,
                    omap_dma_intr_block,
                    (a->frames - a->frame - 1) * a->elements +
                    (a->elements - a->element))

    dma->bytes = min_elems * ch->data_type;

    /* Set appropriate interrupts and/or deactivate channels */


        /* If the channel is element synchronized, deactivate it */
        if (min_elems == elements[omap_dma_intr_element_sync])
            omap_dma_deactivate_channel(s, ch);

        /* If it is the last frame, set the LAST_FRAME interrupt */
        if (min_elems == elements[omap_dma_intr_last_frame])
            ch->status |= LAST_FRAME_INTR;

        /* If exactly half of the frame was reached, set the HALF_FRAME
           interrupt */
        if (min_elems == elements[omap_dma_intr_half_frame])
            ch->status |= HALF_FRAME_INTR;

        /* If a full packet has been transferred, set the END_PKT interrupt */
        if (min_elems == elements[omap_dma_intr_packet])
            ch->status |= END_PKT_INTR;

        /* If the channel is packet-synchronized, deactivate it */
        if (min_elems == elements[omap_dma_intr_packet_sync])
            omap_dma_deactivate_channel(s, ch);

        /* If the channel is frame synchronized, deactivate it */
        if (min_elems == elements[omap_dma_intr_frame_sync])
            omap_dma_deactivate_channel(s, ch);

        /* Set the END_FRAME interrupt */
        if (min_elems == elements[omap_dma_intr_frame])
            ch->status |= END_FRAME_INTR;

        if (min_elems == elements[omap_dma_intr_block]) {
            /* End of Block */
            /* Disable the channel */

            if (!ch->auto_init)
                omap_dma_disable_channel(s, ch);
            else if (ch->repeat || ch->end_prog)
                omap_dma_channel_load(ch);
            else {
                ch->waiting_end_prog = 1;
                omap_dma_deactivate_channel(s, ch);
            }

            if (ch->interrupts & END_BLOCK_INTR)
                ch->status |= END_BLOCK_INTR;
        }

        /* Update packet number */
        if (ch->fs && ch->bs) {
            a->pck_element += min_elems;
            a->pck_element %= a->pck_elements;
        }

        /* TODO: check if we really need to update anything here or perhaps we
         * can skip part of this.  */
        if (dma->update) {
            a->element += min_elems;

            frames = a->element / a->elements;
            a->element = a->element % a->elements;
            a->frame += frames;
            a->src += min_elems * a->elem_delta[0] + frames * a->frame_delta[0];
            a->dest += min_elems * a->elem_delta[1] + frames * a->frame_delta[1];

            /* If the channel is async, update cpc */
            if (!ch->sync && frames)
                ch->cpc = a->dest & 0xffff;

            /* TODO: if the destination port is IMIF or EMIFF, set the dirty
             * bits on it.  */
        }

    omap_dma_interrupts_update(s);
}

void omap_dma_reset(struct soc_dma_s *dma)
{
    int i;
    struct omap_dma_s *s = dma->opaque;

    soc_dma_reset(s->dma);
    s->gcr = 0x0004;
    s->ocp = 0x00000000;
    memset(&s->irqstat, 0, sizeof(s->irqstat));
    memset(&s->irqen, 0, sizeof(s->irqen));
    s->lcd_ch.src = emiff;
    s->lcd_ch.condition = 0;
    s->lcd_ch.interrupts = 0;
    s->lcd_ch.dual = 0;
    for (i = 0; i < s->chans; i ++) {
        s->ch[i].suspend = 0;
        s->ch[i].prefetch = 0;
        s->ch[i].buf_disable = 0;
        s->ch[i].src_sync = 0;
        memset(&s->ch[i].burst, 0, sizeof(s->ch[i].burst));
        memset(&s->ch[i].port, 0, sizeof(s->ch[i].port));
        memset(&s->ch[i].mode, 0, sizeof(s->ch[i].mode));
        memset(&s->ch[i].frame_index, 0, sizeof(s->ch[i].frame_index));
        memset(&s->ch[i].element_index, 0, sizeof(s->ch[i].element_index));
        memset(&s->ch[i].endian, 0, sizeof(s->ch[i].endian));
        memset(&s->ch[i].endian_lock, 0, sizeof(s->ch[i].endian_lock));
        memset(&s->ch[i].translate, 0, sizeof(s->ch[i].translate));
        s->ch[i].write_mode = 0;
        s->ch[i].data_type = 0;
        s->ch[i].transparent_copy = 0;
        s->ch[i].constant_fill = 0;
        s->ch[i].color = 0x00000000;
        s->ch[i].end_prog = 0;
        s->ch[i].repeat = 0;
        s->ch[i].auto_init = 0;
        s->ch[i].link_enabled = 0;
        s->ch[i].interrupts = 0x0003;
        s->ch[i].status = 0;
        s->ch[i].cstatus = 0;
        s->ch[i].active = 0;
        s->ch[i].enable = 0;
        s->ch[i].sync = 0;
        s->ch[i].pending_request = 0;
        s->ch[i].waiting_end_prog = 0;
        s->ch[i].cpc = 0x0000;
        s->ch[i].fs = 0;
        s->ch[i].bs = 0;
        memset(&s->ch[i].active_set, 0, sizeof(s->ch[i].active_set));
        s->ch[i].priority = 0;
        s->ch[i].interleave_disabled = 0;
        s->ch[i].type = 0;
    }
}

static int omap_dma_ch_reg_read(struct omap_dma_s *s,
                struct omap_dma_channel_s *ch, int reg, uint16_t *value)
{
    switch (reg) {
    case 0x00:  /* SYS_DMA_CSDP_CH0 */
        *value = (ch->burst[1] << 14) |
                (ch->pack[1] << 13) |
                (ch->port[1] << 9) |
                (ch->burst[0] << 7) |
                (ch->pack[0] << 6) |
                (ch->port[0] << 2) |
                (ch->data_type >> 1);
        break;

    case 0x02:  /* SYS_DMA_CCR_CH0 */
        *value = 0 << 10;           /* FIFO_FLUSH reads as 0 */
        *value |= (ch->mode[1] << 14) |
                (ch->mode[0] << 12) |
                (ch->end_prog << 11) |
                (ch->repeat << 9) |
                (ch->auto_init << 8) |
                (ch->enable << 7) |
                (ch->priority << 6) |
                (ch->fs << 5) | ch->sync;
        break;

    case 0x04:  /* SYS_DMA_CICR_CH0 */
        *value = ch->interrupts;
        break;

    case 0x06:  /* SYS_DMA_CSR_CH0 */
        *value = ch->status;
        ch->status &= SYNC;
        if (ch->sibling) {
            *value |= (ch->sibling->status & 0x3f) << 6;
            ch->sibling->status &= SYNC;
        }
        qemu_irq_lower(ch->irq);
        break;

    case 0x08:  /* SYS_DMA_CSSA_L_CH0 */
        *value = ch->addr[0] & 0x0000ffff;
        break;

    case 0x0a:  /* SYS_DMA_CSSA_U_CH0 */
        *value = ch->addr[0] >> 16;
        break;

    case 0x0c:  /* SYS_DMA_CDSA_L_CH0 */
        *value = ch->addr[1] & 0x0000ffff;
        break;

    case 0x0e:  /* SYS_DMA_CDSA_U_CH0 */
        *value = ch->addr[1] >> 16;
        break;

    case 0x10:  /* SYS_DMA_CEN_CH0 */
        *value = ch->elements;
        break;

    case 0x12:  /* SYS_DMA_CFN_CH0 */
        *value = ch->frames;
        break;

    case 0x14:  /* SYS_DMA_CFI_CH0 */
        *value = ch->frame_index[0];
        break;

    case 0x16:  /* SYS_DMA_CEI_CH0 */
        *value = ch->element_index[0];
        break;

    case 0x18:  /* SYS_DMA_CPC_CH0 */
        *value = ch->cpc;
        break;

    case 0x1a:  /* DMA_CDAC */
        *value = ch->active_set.dest & 0xffff;  /* CDAC */
        break;

    case 0x1c:  /* DMA_CDEI */
        *value = ch->element_index[1];
        break;

    case 0x1e:  /* DMA_CDFI */
        *value = ch->frame_index[1];
        break;

    case 0x20:  /* DMA_COLOR_L */
        *value = ch->color & 0xffff;
        break;

    case 0x22:  /* DMA_COLOR_U */
        *value = ch->color >> 16;
        break;

    case 0x24:  /* DMA_CCR2 */
        *value = (ch->bs << 2) |
                (ch->transparent_copy << 1) |
                ch->constant_fill;
        break;

    case 0x28:  /* DMA_CLNK_CTRL */
        *value = (ch->link_enabled << 15) |
                (ch->link_next_ch & 0xf);
        break;

    case 0x2a:  /* DMA_LCH_CTRL */
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
    case 0x00:  /* SYS_DMA_CSDP_CH0 */
        ch->burst[1] = (value & 0xc000) >> 14;
        ch->pack[1] = (value & 0x2000) >> 13;
        ch->port[1] = (enum omap_dma_port) ((value & 0x1e00) >> 9);
        ch->burst[0] = (value & 0x0180) >> 7;
        ch->pack[0] = (value & 0x0040) >> 6;
        ch->port[0] = (enum omap_dma_port) ((value & 0x003c) >> 2);
        if (ch->port[0] >= __omap_dma_port_last) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid DMA port %i\n",
                          __func__, ch->port[0]);
        }
        if (ch->port[1] >= __omap_dma_port_last) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid DMA port %i\n",
                          __func__, ch->port[1]);
        }
        ch->data_type = 1 << (value & 3);
        if ((value & 3) == 3) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: bad data_type for DMA channel\n", __func__);
            ch->data_type >>= 1;
        }
        break;

    case 0x02:  /* SYS_DMA_CCR_CH0 */
        ch->mode[1] = (omap_dma_addressing_t) ((value & 0xc000) >> 14);
        ch->mode[0] = (omap_dma_addressing_t) ((value & 0x3000) >> 12);
        ch->end_prog = (value & 0x0800) >> 11;
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

    case 0x04:  /* SYS_DMA_CICR_CH0 */
        ch->interrupts = value & 0x3f;
        break;

    case 0x06:  /* SYS_DMA_CSR_CH0 */
        OMAP_RO_REG((hwaddr) reg);
        break;

    case 0x08:  /* SYS_DMA_CSSA_L_CH0 */
        ch->addr[0] &= 0xffff0000;
        ch->addr[0] |= value;
        break;

    case 0x0a:  /* SYS_DMA_CSSA_U_CH0 */
        ch->addr[0] &= 0x0000ffff;
        ch->addr[0] |= (uint32_t) value << 16;
        break;

    case 0x0c:  /* SYS_DMA_CDSA_L_CH0 */
        ch->addr[1] &= 0xffff0000;
        ch->addr[1] |= value;
        break;

    case 0x0e:  /* SYS_DMA_CDSA_U_CH0 */
        ch->addr[1] &= 0x0000ffff;
        ch->addr[1] |= (uint32_t) value << 16;
        break;

    case 0x10:  /* SYS_DMA_CEN_CH0 */
        ch->elements = value;
        break;

    case 0x12:  /* SYS_DMA_CFN_CH0 */
        ch->frames = value;
        break;

    case 0x14:  /* SYS_DMA_CFI_CH0 */
        ch->frame_index[0] = (int16_t) value;
        break;

    case 0x16:  /* SYS_DMA_CEI_CH0 */
        ch->element_index[0] = (int16_t) value;
        break;

    case 0x18:  /* SYS_DMA_CPC_CH0 or DMA_CSAC */
        OMAP_RO_REG((hwaddr) reg);
        break;

    case 0x1c:  /* DMA_CDEI */
        ch->element_index[1] = (int16_t) value;
        break;

    case 0x1e:  /* DMA_CDFI */
        ch->frame_index[1] = (int16_t) value;
        break;

    case 0x20:  /* DMA_COLOR_L */
        ch->color &= 0xffff0000;
        ch->color |= value;
        break;

    case 0x22:  /* DMA_COLOR_U */
        ch->color &= 0xffff;
        ch->color |= (uint32_t)value << 16;
        break;

    case 0x24:  /* DMA_CCR2 */
        ch->bs = (value >> 2) & 0x1;
        ch->transparent_copy = (value >> 1) & 0x1;
        ch->constant_fill = value & 0x1;
        break;

    case 0x28:  /* DMA_CLNK_CTRL */
        ch->link_enabled = (value >> 15) & 0x1;
        if (value & (1 << 14)) {            /* Stop_Lnk */
            ch->link_enabled = 0;
            omap_dma_disable_channel(s, ch);
        }
        ch->link_next_ch = value & 0x1f;
        break;

    case 0x2a:  /* DMA_LCH_CTRL */
        ch->interleave_disabled = (value >> 15) & 0x1;
        ch->type = value & 0xf;
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
    case 0x300: /* SYS_DMA_LCD_CTRL */
        s->src = (value & 0x40) ? imif : emiff;
        s->condition = 0;
        /* Assume no bus errors and thus no BUS_ERROR irq bits.  */
        s->interrupts = (value >> 1) & 1;
        s->dual = value & 1;
        break;

    case 0x302: /* SYS_DMA_LCD_TOP_F1_L */
        s->src_f1_top &= 0xffff0000;
        s->src_f1_top |= 0x0000ffff & value;
        break;

    case 0x304: /* SYS_DMA_LCD_TOP_F1_U */
        s->src_f1_top &= 0x0000ffff;
        s->src_f1_top |= (uint32_t)value << 16;
        break;

    case 0x306: /* SYS_DMA_LCD_BOT_F1_L */
        s->src_f1_bottom &= 0xffff0000;
        s->src_f1_bottom |= 0x0000ffff & value;
        break;

    case 0x308: /* SYS_DMA_LCD_BOT_F1_U */
        s->src_f1_bottom &= 0x0000ffff;
        s->src_f1_bottom |= (uint32_t)value << 16;
        break;

    case 0x30a: /* SYS_DMA_LCD_TOP_F2_L */
        s->src_f2_top &= 0xffff0000;
        s->src_f2_top |= 0x0000ffff & value;
        break;

    case 0x30c: /* SYS_DMA_LCD_TOP_F2_U */
        s->src_f2_top &= 0x0000ffff;
        s->src_f2_top |= (uint32_t)value << 16;
        break;

    case 0x30e: /* SYS_DMA_LCD_BOT_F2_L */
        s->src_f2_bottom &= 0xffff0000;
        s->src_f2_bottom |= 0x0000ffff & value;
        break;

    case 0x310: /* SYS_DMA_LCD_BOT_F2_U */
        s->src_f2_bottom &= 0x0000ffff;
        s->src_f2_bottom |= (uint32_t)value << 16;
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
    case 0x300: /* SYS_DMA_LCD_CTRL */
        i = s->condition;
        s->condition = 0;
        qemu_irq_lower(s->irq);
        *ret = ((s->src == imif) << 6) | (i << 3) |
                (s->interrupts << 1) | s->dual;
        break;

    case 0x302: /* SYS_DMA_LCD_TOP_F1_L */
        *ret = s->src_f1_top & 0xffff;
        break;

    case 0x304: /* SYS_DMA_LCD_TOP_F1_U */
        *ret = s->src_f1_top >> 16;
        break;

    case 0x306: /* SYS_DMA_LCD_BOT_F1_L */
        *ret = s->src_f1_bottom & 0xffff;
        break;

    case 0x308: /* SYS_DMA_LCD_BOT_F1_U */
        *ret = s->src_f1_bottom >> 16;
        break;

    case 0x30a: /* SYS_DMA_LCD_TOP_F2_L */
        *ret = s->src_f2_top & 0xffff;
        break;

    case 0x30c: /* SYS_DMA_LCD_TOP_F2_U */
        *ret = s->src_f2_top >> 16;
        break;

    case 0x30e: /* SYS_DMA_LCD_BOT_F2_L */
        *ret = s->src_f2_bottom & 0xffff;
        break;

    case 0x310: /* SYS_DMA_LCD_BOT_F2_U */
        *ret = s->src_f2_bottom >> 16;
        break;

    default:
        return 1;
    }
    return 0;
}

static uint64_t omap_dma_read(void *opaque, hwaddr addr, unsigned size)
{
    struct omap_dma_s *s = opaque;
    int reg, ch;
    uint16_t ret;

    if (size != 2) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read at offset 0x%" HWADDR_PRIx
                      " with bad width %d\n", __func__, addr, size);
        return 0;
    }

    switch (addr) {
    case 0x300 ... 0x3fe:
        if (omap_dma_3_1_lcd_read(&s->lcd_ch, addr, &ret)) {
            break;
        }
        return ret;
    case 0x000 ... 0x2fe:
        reg = addr & 0x3f;
        ch = (addr >> 6) & 0x0f;
        if (omap_dma_ch_reg_read(s, &s->ch[ch], reg, &ret))
            break;
        return ret;

    case 0x404 ... 0x4fe:
        break;
    case 0x400: /* SYS_DMA_GCR */
        return s->gcr;
        break;

    case 0xb00 ... 0xbfe:
        break;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_dma_write(void *opaque, hwaddr addr,
                           uint64_t value, unsigned size)
{
    struct omap_dma_s *s = opaque;
    int reg, ch;

    if (size != 2) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write at offset 0x%" HWADDR_PRIx
                      " with bad width %d\n", __func__, addr, size);
        return;
    }

    switch (addr) {
    case 0x300 ... 0x3fe:
        if (omap_dma_3_1_lcd_write(&s->lcd_ch, addr, value)) {
            break;
        }
        return;
    case 0x000 ... 0x2fe:
        reg = addr & 0x3f;
        ch = (addr >> 6) & 0x0f;
        if (omap_dma_ch_reg_write(s, &s->ch[ch], reg, value))
            break;
        return;

    case 0x404 ... 0x4fe:
        break;
    case 0x400: /* SYS_DMA_GCR */
        s->gcr = value;
        return;

    case 0xb00 ... 0xbfe:
        break;
    }

    OMAP_BAD_REG(addr);
}

static const MemoryRegionOps omap_dma_ops = {
    .read = omap_dma_read,
    .write = omap_dma_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void omap_dma_request(void *opaque, int drq, int req)
{
    struct omap_dma_s *s = opaque;
    /* The request pins are level triggered in QEMU.  */
    if (req) {
        if (~s->dma->drqbmp & (1ULL << drq)) {
            s->dma->drqbmp |= 1ULL << drq;
            omap_dma_process_request(s, drq);
        }
    } else
        s->dma->drqbmp &= ~(1ULL << drq);
}

/* XXX: this won't be needed once soc_dma knows about clocks.  */
static void omap_dma_clk_update(void *opaque, int line, int on)
{
    struct omap_dma_s *s = opaque;
    int i;

    s->dma->freq = omap_clk_getrate(s->clk);

    for (i = 0; i < s->chans; i ++)
        if (s->ch[i].active)
            soc_dma_set_request(s->ch[i].dma, on);
}

struct soc_dma_s *omap_dma_init(hwaddr base, qemu_irq *irqs,
                                MemoryRegion *sysmem,
                                qemu_irq lcd_irq,
                                struct omap_mpu_state_s *mpu, omap_clk clk)
{
    int num_irqs, memsize, i;
    struct omap_dma_s *s = g_new0(struct omap_dma_s, 1);

    num_irqs = 6;
    memsize = 0x800;
    s->mpu = mpu;
    s->clk = clk;
    s->lcd_ch.irq = lcd_irq;
    s->lcd_ch.mpu = mpu;
    s->chans = 9;

    s->dma = soc_dma_init(9);
    s->dma->freq = omap_clk_getrate(clk);
    s->dma->transfer_fn = omap_dma_transfer_generic;
    s->dma->setup_fn = omap_dma_transfer_setup;
    s->dma->drq = qemu_allocate_irqs(omap_dma_request, s, 32);
    s->dma->opaque = s;

    while (num_irqs --)
        s->ch[num_irqs].irq = irqs[num_irqs];
    for (i = 0; i < 3; i ++) {
        s->ch[i].sibling = &s->ch[i + 6];
        s->ch[i + 6].sibling = &s->ch[i];
    }
    for (i = 8; i >= 0; i--) {
        s->ch[i].dma = &s->dma->ch[i];
        s->dma->ch[i].opaque = &s->ch[i];
    }

    omap_clk_adduser(s->clk, qemu_allocate_irq(omap_dma_clk_update, s, 0));
    omap_dma_reset(s->dma);
    omap_dma_clk_update(s, 0, 1);

    memory_region_init_io(&s->iomem, NULL, &omap_dma_ops, s, "omap.dma", memsize);
    memory_region_add_subregion(sysmem, base, &s->iomem);

    mpu->drq = s->dma->drq;

    return s->dma;
}

struct omap_dma_lcd_channel_s *omap_dma_get_lcdch(struct soc_dma_s *dma)
{
    struct omap_dma_s *s = dma->opaque;

    return &s->lcd_ch;
}
