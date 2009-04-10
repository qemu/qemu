/*
 * Intel XScale PXA255/270 LCDC emulation.
 *
 * Copyright (c) 2006 Openedhand Ltd.
 * Written by Andrzej Zaborowski <balrog@zabor.org>
 *
 * This code is licensed under the GPLv2.
 */

#include "hw.h"
#include "console.h"
#include "pxa.h"
#include "pixel_ops.h"
/* FIXME: For graphic_rotate. Should probably be done in common code.  */
#include "sysemu.h"
#include "framebuffer.h"

struct pxa2xx_lcdc_s {
    qemu_irq irq;
    int irqlevel;

    int invalidated;
    DisplayState *ds;
    drawfn *line_fn[2];
    int dest_width;
    int xres, yres;
    int pal_for;
    int transp;
    enum {
        pxa_lcdc_2bpp = 1,
        pxa_lcdc_4bpp = 2,
        pxa_lcdc_8bpp = 3,
        pxa_lcdc_16bpp = 4,
        pxa_lcdc_18bpp = 5,
        pxa_lcdc_18pbpp = 6,
        pxa_lcdc_19bpp = 7,
        pxa_lcdc_19pbpp = 8,
        pxa_lcdc_24bpp = 9,
        pxa_lcdc_25bpp = 10,
    } bpp;

    uint32_t control[6];
    uint32_t status[2];
    uint32_t ovl1c[2];
    uint32_t ovl2c[2];
    uint32_t ccr;
    uint32_t cmdcr;
    uint32_t trgbr;
    uint32_t tcr;
    uint32_t liidr;
    uint8_t bscntr;

    struct {
        target_phys_addr_t branch;
        int up;
        uint8_t palette[1024];
        uint8_t pbuffer[1024];
        void (*redraw)(struct pxa2xx_lcdc_s *s, target_phys_addr_t addr,
                        int *miny, int *maxy);

        target_phys_addr_t descriptor;
        target_phys_addr_t source;
        uint32_t id;
        uint32_t command;
    } dma_ch[7];

    qemu_irq vsync_cb;
    int orientation;
};

struct __attribute__ ((__packed__)) pxa_frame_descriptor_s {
    uint32_t fdaddr;
    uint32_t fsaddr;
    uint32_t fidr;
    uint32_t ldcmd;
};

#define LCCR0	0x000	/* LCD Controller Control register 0 */
#define LCCR1	0x004	/* LCD Controller Control register 1 */
#define LCCR2	0x008	/* LCD Controller Control register 2 */
#define LCCR3	0x00c	/* LCD Controller Control register 3 */
#define LCCR4	0x010	/* LCD Controller Control register 4 */
#define LCCR5	0x014	/* LCD Controller Control register 5 */

#define FBR0	0x020	/* DMA Channel 0 Frame Branch register */
#define FBR1	0x024	/* DMA Channel 1 Frame Branch register */
#define FBR2	0x028	/* DMA Channel 2 Frame Branch register */
#define FBR3	0x02c	/* DMA Channel 3 Frame Branch register */
#define FBR4	0x030	/* DMA Channel 4 Frame Branch register */
#define FBR5	0x110	/* DMA Channel 5 Frame Branch register */
#define FBR6	0x114	/* DMA Channel 6 Frame Branch register */

#define LCSR1	0x034	/* LCD Controller Status register 1 */
#define LCSR0	0x038	/* LCD Controller Status register 0 */
#define LIIDR	0x03c	/* LCD Controller Interrupt ID register */

#define TRGBR	0x040	/* TMED RGB Seed register */
#define TCR	0x044	/* TMED Control register */

#define OVL1C1	0x050	/* Overlay 1 Control register 1 */
#define OVL1C2	0x060	/* Overlay 1 Control register 2 */
#define OVL2C1	0x070	/* Overlay 2 Control register 1 */
#define OVL2C2	0x080	/* Overlay 2 Control register 2 */
#define CCR	0x090	/* Cursor Control register */

#define CMDCR	0x100	/* Command Control register */
#define PRSR	0x104	/* Panel Read Status register */

#define PXA_LCDDMA_CHANS	7
#define DMA_FDADR		0x00	/* Frame Descriptor Address register */
#define DMA_FSADR		0x04	/* Frame Source Address register */
#define DMA_FIDR		0x08	/* Frame ID register */
#define DMA_LDCMD		0x0c	/* Command register */

/* LCD Buffer Strength Control register */
#define BSCNTR	0x04000054

/* Bitfield masks */
#define LCCR0_ENB	(1 << 0)
#define LCCR0_CMS	(1 << 1)
#define LCCR0_SDS	(1 << 2)
#define LCCR0_LDM	(1 << 3)
#define LCCR0_SOFM0	(1 << 4)
#define LCCR0_IUM	(1 << 5)
#define LCCR0_EOFM0	(1 << 6)
#define LCCR0_PAS	(1 << 7)
#define LCCR0_DPD	(1 << 9)
#define LCCR0_DIS	(1 << 10)
#define LCCR0_QDM	(1 << 11)
#define LCCR0_PDD	(0xff << 12)
#define LCCR0_BSM0	(1 << 20)
#define LCCR0_OUM	(1 << 21)
#define LCCR0_LCDT	(1 << 22)
#define LCCR0_RDSTM	(1 << 23)
#define LCCR0_CMDIM	(1 << 24)
#define LCCR0_OUC	(1 << 25)
#define LCCR0_LDDALT	(1 << 26)
#define LCCR1_PPL(x)	((x) & 0x3ff)
#define LCCR2_LPP(x)	((x) & 0x3ff)
#define LCCR3_API	(15 << 16)
#define LCCR3_BPP(x)	((((x) >> 24) & 7) | (((x) >> 26) & 8))
#define LCCR3_PDFOR(x)	(((x) >> 30) & 3)
#define LCCR4_K1(x)	(((x) >> 0) & 7)
#define LCCR4_K2(x)	(((x) >> 3) & 7)
#define LCCR4_K3(x)	(((x) >> 6) & 7)
#define LCCR4_PALFOR(x)	(((x) >> 15) & 3)
#define LCCR5_SOFM(ch)	(1 << (ch - 1))
#define LCCR5_EOFM(ch)	(1 << (ch + 7))
#define LCCR5_BSM(ch)	(1 << (ch + 15))
#define LCCR5_IUM(ch)	(1 << (ch + 23))
#define OVLC1_EN	(1 << 31)
#define CCR_CEN		(1 << 31)
#define FBR_BRA		(1 << 0)
#define FBR_BINT	(1 << 1)
#define FBR_SRCADDR	(0xfffffff << 4)
#define LCSR0_LDD	(1 << 0)
#define LCSR0_SOF0	(1 << 1)
#define LCSR0_BER	(1 << 2)
#define LCSR0_ABC	(1 << 3)
#define LCSR0_IU0	(1 << 4)
#define LCSR0_IU1	(1 << 5)
#define LCSR0_OU	(1 << 6)
#define LCSR0_QD	(1 << 7)
#define LCSR0_EOF0	(1 << 8)
#define LCSR0_BS0	(1 << 9)
#define LCSR0_SINT	(1 << 10)
#define LCSR0_RDST	(1 << 11)
#define LCSR0_CMDINT	(1 << 12)
#define LCSR0_BERCH(x)	(((x) & 7) << 28)
#define LCSR1_SOF(ch)	(1 << (ch - 1))
#define LCSR1_EOF(ch)	(1 << (ch + 7))
#define LCSR1_BS(ch)	(1 << (ch + 15))
#define LCSR1_IU(ch)	(1 << (ch + 23))
#define LDCMD_LENGTH(x)	((x) & 0x001ffffc)
#define LDCMD_EOFINT	(1 << 21)
#define LDCMD_SOFINT	(1 << 22)
#define LDCMD_PAL	(1 << 26)

/* Route internal interrupt lines to the global IC */
static void pxa2xx_lcdc_int_update(struct pxa2xx_lcdc_s *s)
{
    int level = 0;
    level |= (s->status[0] & LCSR0_LDD)    && !(s->control[0] & LCCR0_LDM);
    level |= (s->status[0] & LCSR0_SOF0)   && !(s->control[0] & LCCR0_SOFM0);
    level |= (s->status[0] & LCSR0_IU0)    && !(s->control[0] & LCCR0_IUM);
    level |= (s->status[0] & LCSR0_IU1)    && !(s->control[5] & LCCR5_IUM(1));
    level |= (s->status[0] & LCSR0_OU)     && !(s->control[0] & LCCR0_OUM);
    level |= (s->status[0] & LCSR0_QD)     && !(s->control[0] & LCCR0_QDM);
    level |= (s->status[0] & LCSR0_EOF0)   && !(s->control[0] & LCCR0_EOFM0);
    level |= (s->status[0] & LCSR0_BS0)    && !(s->control[0] & LCCR0_BSM0);
    level |= (s->status[0] & LCSR0_RDST)   && !(s->control[0] & LCCR0_RDSTM);
    level |= (s->status[0] & LCSR0_CMDINT) && !(s->control[0] & LCCR0_CMDIM);
    level |= (s->status[1] & ~s->control[5]);

    qemu_set_irq(s->irq, !!level);
    s->irqlevel = level;
}

/* Set Branch Status interrupt high and poke associated registers */
static inline void pxa2xx_dma_bs_set(struct pxa2xx_lcdc_s *s, int ch)
{
    int unmasked;
    if (ch == 0) {
        s->status[0] |= LCSR0_BS0;
        unmasked = !(s->control[0] & LCCR0_BSM0);
    } else {
        s->status[1] |= LCSR1_BS(ch);
        unmasked = !(s->control[5] & LCCR5_BSM(ch));
    }

    if (unmasked) {
        if (s->irqlevel)
            s->status[0] |= LCSR0_SINT;
        else
            s->liidr = s->dma_ch[ch].id;
    }
}

/* Set Start Of Frame Status interrupt high and poke associated registers */
static inline void pxa2xx_dma_sof_set(struct pxa2xx_lcdc_s *s, int ch)
{
    int unmasked;
    if (!(s->dma_ch[ch].command & LDCMD_SOFINT))
        return;

    if (ch == 0) {
        s->status[0] |= LCSR0_SOF0;
        unmasked = !(s->control[0] & LCCR0_SOFM0);
    } else {
        s->status[1] |= LCSR1_SOF(ch);
        unmasked = !(s->control[5] & LCCR5_SOFM(ch));
    }

    if (unmasked) {
        if (s->irqlevel)
            s->status[0] |= LCSR0_SINT;
        else
            s->liidr = s->dma_ch[ch].id;
    }
}

/* Set End Of Frame Status interrupt high and poke associated registers */
static inline void pxa2xx_dma_eof_set(struct pxa2xx_lcdc_s *s, int ch)
{
    int unmasked;
    if (!(s->dma_ch[ch].command & LDCMD_EOFINT))
        return;

    if (ch == 0) {
        s->status[0] |= LCSR0_EOF0;
        unmasked = !(s->control[0] & LCCR0_EOFM0);
    } else {
        s->status[1] |= LCSR1_EOF(ch);
        unmasked = !(s->control[5] & LCCR5_EOFM(ch));
    }

    if (unmasked) {
        if (s->irqlevel)
            s->status[0] |= LCSR0_SINT;
        else
            s->liidr = s->dma_ch[ch].id;
    }
}

/* Set Bus Error Status interrupt high and poke associated registers */
static inline void pxa2xx_dma_ber_set(struct pxa2xx_lcdc_s *s, int ch)
{
    s->status[0] |= LCSR0_BERCH(ch) | LCSR0_BER;
    if (s->irqlevel)
        s->status[0] |= LCSR0_SINT;
    else
        s->liidr = s->dma_ch[ch].id;
}

/* Set Read Status interrupt high and poke associated registers */
static inline void pxa2xx_dma_rdst_set(struct pxa2xx_lcdc_s *s)
{
    s->status[0] |= LCSR0_RDST;
    if (s->irqlevel && !(s->control[0] & LCCR0_RDSTM))
        s->status[0] |= LCSR0_SINT;
}

/* Load new Frame Descriptors from DMA */
static void pxa2xx_descriptor_load(struct pxa2xx_lcdc_s *s)
{
    struct pxa_frame_descriptor_s desc;
    target_phys_addr_t descptr;
    int i;

    for (i = 0; i < PXA_LCDDMA_CHANS; i ++) {
        s->dma_ch[i].source = 0;

        if (!s->dma_ch[i].up)
            continue;

        if (s->dma_ch[i].branch & FBR_BRA) {
            descptr = s->dma_ch[i].branch & FBR_SRCADDR;
            if (s->dma_ch[i].branch & FBR_BINT)
                pxa2xx_dma_bs_set(s, i);
            s->dma_ch[i].branch &= ~FBR_BRA;
        } else
            descptr = s->dma_ch[i].descriptor;

        if (!(descptr >= PXA2XX_SDRAM_BASE && descptr +
                    sizeof(desc) <= PXA2XX_SDRAM_BASE + phys_ram_size))
            continue;

        cpu_physical_memory_read(descptr, (void *)&desc, sizeof(desc));
        s->dma_ch[i].descriptor = tswap32(desc.fdaddr);
        s->dma_ch[i].source = tswap32(desc.fsaddr);
        s->dma_ch[i].id = tswap32(desc.fidr);
        s->dma_ch[i].command = tswap32(desc.ldcmd);
    }
}

static uint32_t pxa2xx_lcdc_read(void *opaque, target_phys_addr_t offset)
{
    struct pxa2xx_lcdc_s *s = (struct pxa2xx_lcdc_s *) opaque;
    int ch;

    switch (offset) {
    case LCCR0:
        return s->control[0];
    case LCCR1:
        return s->control[1];
    case LCCR2:
        return s->control[2];
    case LCCR3:
        return s->control[3];
    case LCCR4:
        return s->control[4];
    case LCCR5:
        return s->control[5];

    case OVL1C1:
        return s->ovl1c[0];
    case OVL1C2:
        return s->ovl1c[1];
    case OVL2C1:
        return s->ovl2c[0];
    case OVL2C2:
        return s->ovl2c[1];

    case CCR:
        return s->ccr;

    case CMDCR:
        return s->cmdcr;

    case TRGBR:
        return s->trgbr;
    case TCR:
        return s->tcr;

    case 0x200 ... 0x1000:	/* DMA per-channel registers */
        ch = (offset - 0x200) >> 4;
        if (!(ch >= 0 && ch < PXA_LCDDMA_CHANS))
            goto fail;

        switch (offset & 0xf) {
        case DMA_FDADR:
            return s->dma_ch[ch].descriptor;
        case DMA_FSADR:
            return s->dma_ch[ch].source;
        case DMA_FIDR:
            return s->dma_ch[ch].id;
        case DMA_LDCMD:
            return s->dma_ch[ch].command;
        default:
            goto fail;
        }

    case FBR0:
        return s->dma_ch[0].branch;
    case FBR1:
        return s->dma_ch[1].branch;
    case FBR2:
        return s->dma_ch[2].branch;
    case FBR3:
        return s->dma_ch[3].branch;
    case FBR4:
        return s->dma_ch[4].branch;
    case FBR5:
        return s->dma_ch[5].branch;
    case FBR6:
        return s->dma_ch[6].branch;

    case BSCNTR:
        return s->bscntr;

    case PRSR:
        return 0;

    case LCSR0:
        return s->status[0];
    case LCSR1:
        return s->status[1];
    case LIIDR:
        return s->liidr;

    default:
    fail:
        cpu_abort(cpu_single_env,
                "%s: Bad offset " REG_FMT "\n", __FUNCTION__, offset);
    }

    return 0;
}

static void pxa2xx_lcdc_write(void *opaque,
                target_phys_addr_t offset, uint32_t value)
{
    struct pxa2xx_lcdc_s *s = (struct pxa2xx_lcdc_s *) opaque;
    int ch;

    switch (offset) {
    case LCCR0:
        /* ACK Quick Disable done */
        if ((s->control[0] & LCCR0_ENB) && !(value & LCCR0_ENB))
            s->status[0] |= LCSR0_QD;

        if (!(s->control[0] & LCCR0_LCDT) && (value & LCCR0_LCDT))
            printf("%s: internal frame buffer unsupported\n", __FUNCTION__);

        if ((s->control[3] & LCCR3_API) &&
                (value & LCCR0_ENB) && !(value & LCCR0_LCDT))
            s->status[0] |= LCSR0_ABC;

        s->control[0] = value & 0x07ffffff;
        pxa2xx_lcdc_int_update(s);

        s->dma_ch[0].up = !!(value & LCCR0_ENB);
        s->dma_ch[1].up = (s->ovl1c[0] & OVLC1_EN) || (value & LCCR0_SDS);
        break;

    case LCCR1:
        s->control[1] = value;
        break;

    case LCCR2:
        s->control[2] = value;
        break;

    case LCCR3:
        s->control[3] = value & 0xefffffff;
        s->bpp = LCCR3_BPP(value);
        break;

    case LCCR4:
        s->control[4] = value & 0x83ff81ff;
        break;

    case LCCR5:
        s->control[5] = value & 0x3f3f3f3f;
        break;

    case OVL1C1:
        if (!(s->ovl1c[0] & OVLC1_EN) && (value & OVLC1_EN))
            printf("%s: Overlay 1 not supported\n", __FUNCTION__);

        s->ovl1c[0] = value & 0x80ffffff;
        s->dma_ch[1].up = (value & OVLC1_EN) || (s->control[0] & LCCR0_SDS);
        break;

    case OVL1C2:
        s->ovl1c[1] = value & 0x000fffff;
        break;

    case OVL2C1:
        if (!(s->ovl2c[0] & OVLC1_EN) && (value & OVLC1_EN))
            printf("%s: Overlay 2 not supported\n", __FUNCTION__);

        s->ovl2c[0] = value & 0x80ffffff;
        s->dma_ch[2].up = !!(value & OVLC1_EN);
        s->dma_ch[3].up = !!(value & OVLC1_EN);
        s->dma_ch[4].up = !!(value & OVLC1_EN);
        break;

    case OVL2C2:
        s->ovl2c[1] = value & 0x007fffff;
        break;

    case CCR:
        if (!(s->ccr & CCR_CEN) && (value & CCR_CEN))
            printf("%s: Hardware cursor unimplemented\n", __FUNCTION__);

        s->ccr = value & 0x81ffffe7;
        s->dma_ch[5].up = !!(value & CCR_CEN);
        break;

    case CMDCR:
        s->cmdcr = value & 0xff;
        break;

    case TRGBR:
        s->trgbr = value & 0x00ffffff;
        break;

    case TCR:
        s->tcr = value & 0x7fff;
        break;

    case 0x200 ... 0x1000:	/* DMA per-channel registers */
        ch = (offset - 0x200) >> 4;
        if (!(ch >= 0 && ch < PXA_LCDDMA_CHANS))
            goto fail;

        switch (offset & 0xf) {
        case DMA_FDADR:
            s->dma_ch[ch].descriptor = value & 0xfffffff0;
            break;

        default:
            goto fail;
        }
        break;

    case FBR0:
        s->dma_ch[0].branch = value & 0xfffffff3;
        break;
    case FBR1:
        s->dma_ch[1].branch = value & 0xfffffff3;
        break;
    case FBR2:
        s->dma_ch[2].branch = value & 0xfffffff3;
        break;
    case FBR3:
        s->dma_ch[3].branch = value & 0xfffffff3;
        break;
    case FBR4:
        s->dma_ch[4].branch = value & 0xfffffff3;
        break;
    case FBR5:
        s->dma_ch[5].branch = value & 0xfffffff3;
        break;
    case FBR6:
        s->dma_ch[6].branch = value & 0xfffffff3;
        break;

    case BSCNTR:
        s->bscntr = value & 0xf;
        break;

    case PRSR:
        break;

    case LCSR0:
        s->status[0] &= ~(value & 0xfff);
        if (value & LCSR0_BER)
            s->status[0] &= ~LCSR0_BERCH(7);
        break;

    case LCSR1:
        s->status[1] &= ~(value & 0x3e3f3f);
        break;

    default:
    fail:
        cpu_abort(cpu_single_env,
                "%s: Bad offset " REG_FMT "\n", __FUNCTION__, offset);
    }
}

static CPUReadMemoryFunc *pxa2xx_lcdc_readfn[] = {
    pxa2xx_lcdc_read,
    pxa2xx_lcdc_read,
    pxa2xx_lcdc_read
};

static CPUWriteMemoryFunc *pxa2xx_lcdc_writefn[] = {
    pxa2xx_lcdc_write,
    pxa2xx_lcdc_write,
    pxa2xx_lcdc_write
};

/* Load new palette for a given DMA channel, convert to internal format */
static void pxa2xx_palette_parse(struct pxa2xx_lcdc_s *s, int ch, int bpp)
{
    int i, n, format, r, g, b, alpha;
    uint32_t *dest, *src;
    s->pal_for = LCCR4_PALFOR(s->control[4]);
    format = s->pal_for;

    switch (bpp) {
    case pxa_lcdc_2bpp:
        n = 4;
        break;
    case pxa_lcdc_4bpp:
        n = 16;
        break;
    case pxa_lcdc_8bpp:
        n = 256;
        break;
    default:
        format = 0;
        return;
    }

    src = (uint32_t *) s->dma_ch[ch].pbuffer;
    dest = (uint32_t *) s->dma_ch[ch].palette;
    alpha = r = g = b = 0;

    for (i = 0; i < n; i ++) {
        switch (format) {
        case 0: /* 16 bpp, no transparency */
            alpha = 0;
            if (s->control[0] & LCCR0_CMS)
                r = g = b = *src & 0xff;
            else {
                r = (*src & 0xf800) >> 8;
                g = (*src & 0x07e0) >> 3;
                b = (*src & 0x001f) << 3;
            }
            break;
        case 1: /* 16 bpp plus transparency */
            alpha = *src & (1 << 24);
            if (s->control[0] & LCCR0_CMS)
                r = g = b = *src & 0xff;
            else {
                r = (*src & 0xf800) >> 8;
                g = (*src & 0x07e0) >> 3;
                b = (*src & 0x001f) << 3;
            }
            break;
        case 2: /* 18 bpp plus transparency */
            alpha = *src & (1 << 24);
            if (s->control[0] & LCCR0_CMS)
                r = g = b = *src & 0xff;
            else {
                r = (*src & 0xf80000) >> 16;
                g = (*src & 0x00fc00) >> 8;
                b = (*src & 0x0000f8);
            }
            break;
        case 3: /* 24 bpp plus transparency */
            alpha = *src & (1 << 24);
            if (s->control[0] & LCCR0_CMS)
                r = g = b = *src & 0xff;
            else {
                r = (*src & 0xff0000) >> 16;
                g = (*src & 0x00ff00) >> 8;
                b = (*src & 0x0000ff);
            }
            break;
        }
        switch (ds_get_bits_per_pixel(s->ds)) {
        case 8:
            *dest = rgb_to_pixel8(r, g, b) | alpha;
            break;
        case 15:
            *dest = rgb_to_pixel15(r, g, b) | alpha;
            break;
        case 16:
            *dest = rgb_to_pixel16(r, g, b) | alpha;
            break;
        case 24:
            *dest = rgb_to_pixel24(r, g, b) | alpha;
            break;
        case 32:
            *dest = rgb_to_pixel32(r, g, b) | alpha;
            break;
        }
        src ++;
        dest ++;
    }
}

static void pxa2xx_lcdc_dma0_redraw_horiz(struct pxa2xx_lcdc_s *s,
                target_phys_addr_t addr, int *miny, int *maxy)
{
    int src_width, dest_width;
    drawfn fn = 0;
    if (s->dest_width)
        fn = s->line_fn[s->transp][s->bpp];
    if (!fn)
        return;

    src_width = (s->xres + 3) & ~3;     /* Pad to a 4 pixels multiple */
    if (s->bpp == pxa_lcdc_19pbpp || s->bpp == pxa_lcdc_18pbpp)
        src_width *= 3;
    else if (s->bpp > pxa_lcdc_16bpp)
        src_width *= 4;
    else if (s->bpp > pxa_lcdc_8bpp)
        src_width *= 2;

    dest_width = s->xres * s->dest_width;
    *miny = 0;
    framebuffer_update_display(s->ds,
                               addr, s->xres, s->yres,
                               src_width, dest_width, s->dest_width,
                               s->invalidated,
                               fn, s->dma_ch[0].palette, miny, maxy);
}

static void pxa2xx_lcdc_dma0_redraw_vert(struct pxa2xx_lcdc_s *s,
               target_phys_addr_t addr, int *miny, int *maxy)
{
    int src_width, dest_width;
    drawfn fn = 0;
    if (s->dest_width)
        fn = s->line_fn[s->transp][s->bpp];
    if (!fn)
        return;

    src_width = (s->xres + 3) & ~3;     /* Pad to a 4 pixels multiple */
    if (s->bpp == pxa_lcdc_19pbpp || s->bpp == pxa_lcdc_18pbpp)
        src_width *= 3;
    else if (s->bpp > pxa_lcdc_16bpp)
        src_width *= 4;
    else if (s->bpp > pxa_lcdc_8bpp)
        src_width *= 2;

    dest_width = s->yres * s->dest_width;
    *miny = 0;
    framebuffer_update_display(s->ds,
                               addr, s->xres, s->yres,
                               src_width, s->dest_width, -dest_width,
                               s->invalidated,
                               fn, s->dma_ch[0].palette,
                               miny, maxy);
}

static void pxa2xx_lcdc_resize(struct pxa2xx_lcdc_s *s)
{
    int width, height;
    if (!(s->control[0] & LCCR0_ENB))
        return;

    width = LCCR1_PPL(s->control[1]) + 1;
    height = LCCR2_LPP(s->control[2]) + 1;

    if (width != s->xres || height != s->yres) {
        if (s->orientation)
            qemu_console_resize(s->ds, height, width);
        else
            qemu_console_resize(s->ds, width, height);
        s->invalidated = 1;
        s->xres = width;
        s->yres = height;
    }
}

static void pxa2xx_update_display(void *opaque)
{
    struct pxa2xx_lcdc_s *s = (struct pxa2xx_lcdc_s *) opaque;
    target_phys_addr_t fbptr;
    int miny, maxy;
    int ch;
    if (!(s->control[0] & LCCR0_ENB))
        return;

    pxa2xx_descriptor_load(s);

    pxa2xx_lcdc_resize(s);
    miny = s->yres;
    maxy = 0;
    s->transp = s->dma_ch[2].up || s->dma_ch[3].up;
    /* Note: With overlay planes the order depends on LCCR0 bit 25.  */
    for (ch = 0; ch < PXA_LCDDMA_CHANS; ch ++)
        if (s->dma_ch[ch].up) {
            if (!s->dma_ch[ch].source) {
                pxa2xx_dma_ber_set(s, ch);
                continue;
            }
            fbptr = s->dma_ch[ch].source;
            if (!(fbptr >= PXA2XX_SDRAM_BASE &&
                    fbptr <= PXA2XX_SDRAM_BASE + phys_ram_size)) {
                pxa2xx_dma_ber_set(s, ch);
                continue;
            }

            if (s->dma_ch[ch].command & LDCMD_PAL) {
                cpu_physical_memory_read(fbptr, s->dma_ch[ch].pbuffer,
                    MAX(LDCMD_LENGTH(s->dma_ch[ch].command),
                        sizeof(s->dma_ch[ch].pbuffer)));
                pxa2xx_palette_parse(s, ch, s->bpp);
            } else {
                /* Do we need to reparse palette */
                if (LCCR4_PALFOR(s->control[4]) != s->pal_for)
                    pxa2xx_palette_parse(s, ch, s->bpp);

                /* ACK frame start */
                pxa2xx_dma_sof_set(s, ch);

                s->dma_ch[ch].redraw(s, fbptr, &miny, &maxy);
                s->invalidated = 0;

                /* ACK frame completed */
                pxa2xx_dma_eof_set(s, ch);
            }
        }

    if (s->control[0] & LCCR0_DIS) {
        /* ACK last frame completed */
        s->control[0] &= ~LCCR0_ENB;
        s->status[0] |= LCSR0_LDD;
    }

    if (miny >= 0) {
        if (s->orientation)
            dpy_update(s->ds, miny, 0, maxy, s->xres);
        else
            dpy_update(s->ds, 0, miny, s->xres, maxy);
    }
    pxa2xx_lcdc_int_update(s);

    qemu_irq_raise(s->vsync_cb);
}

static void pxa2xx_invalidate_display(void *opaque)
{
    struct pxa2xx_lcdc_s *s = (struct pxa2xx_lcdc_s *) opaque;
    s->invalidated = 1;
}

static void pxa2xx_screen_dump(void *opaque, const char *filename)
{
    /* TODO */
}

static void pxa2xx_lcdc_orientation(void *opaque, int angle)
{
    struct pxa2xx_lcdc_s *s = (struct pxa2xx_lcdc_s *) opaque;

    if (angle) {
        s->dma_ch[0].redraw = pxa2xx_lcdc_dma0_redraw_vert;
    } else {
        s->dma_ch[0].redraw = pxa2xx_lcdc_dma0_redraw_horiz;
    }

    s->orientation = angle;
    s->xres = s->yres = -1;
    pxa2xx_lcdc_resize(s);
}

static void pxa2xx_lcdc_save(QEMUFile *f, void *opaque)
{
    struct pxa2xx_lcdc_s *s = (struct pxa2xx_lcdc_s *) opaque;
    int i;

    qemu_put_be32(f, s->irqlevel);
    qemu_put_be32(f, s->transp);

    for (i = 0; i < 6; i ++)
        qemu_put_be32s(f, &s->control[i]);
    for (i = 0; i < 2; i ++)
        qemu_put_be32s(f, &s->status[i]);
    for (i = 0; i < 2; i ++)
        qemu_put_be32s(f, &s->ovl1c[i]);
    for (i = 0; i < 2; i ++)
        qemu_put_be32s(f, &s->ovl2c[i]);
    qemu_put_be32s(f, &s->ccr);
    qemu_put_be32s(f, &s->cmdcr);
    qemu_put_be32s(f, &s->trgbr);
    qemu_put_be32s(f, &s->tcr);
    qemu_put_be32s(f, &s->liidr);
    qemu_put_8s(f, &s->bscntr);

    for (i = 0; i < 7; i ++) {
        qemu_put_betl(f, s->dma_ch[i].branch);
        qemu_put_byte(f, s->dma_ch[i].up);
        qemu_put_buffer(f, s->dma_ch[i].pbuffer, sizeof(s->dma_ch[i].pbuffer));

        qemu_put_betl(f, s->dma_ch[i].descriptor);
        qemu_put_betl(f, s->dma_ch[i].source);
        qemu_put_be32s(f, &s->dma_ch[i].id);
        qemu_put_be32s(f, &s->dma_ch[i].command);
    }
}

static int pxa2xx_lcdc_load(QEMUFile *f, void *opaque, int version_id)
{
    struct pxa2xx_lcdc_s *s = (struct pxa2xx_lcdc_s *) opaque;
    int i;

    s->irqlevel = qemu_get_be32(f);
    s->transp = qemu_get_be32(f);

    for (i = 0; i < 6; i ++)
        qemu_get_be32s(f, &s->control[i]);
    for (i = 0; i < 2; i ++)
        qemu_get_be32s(f, &s->status[i]);
    for (i = 0; i < 2; i ++)
        qemu_get_be32s(f, &s->ovl1c[i]);
    for (i = 0; i < 2; i ++)
        qemu_get_be32s(f, &s->ovl2c[i]);
    qemu_get_be32s(f, &s->ccr);
    qemu_get_be32s(f, &s->cmdcr);
    qemu_get_be32s(f, &s->trgbr);
    qemu_get_be32s(f, &s->tcr);
    qemu_get_be32s(f, &s->liidr);
    qemu_get_8s(f, &s->bscntr);

    for (i = 0; i < 7; i ++) {
        s->dma_ch[i].branch = qemu_get_betl(f);
        s->dma_ch[i].up = qemu_get_byte(f);
        qemu_get_buffer(f, s->dma_ch[i].pbuffer, sizeof(s->dma_ch[i].pbuffer));

        s->dma_ch[i].descriptor = qemu_get_betl(f);
        s->dma_ch[i].source = qemu_get_betl(f);
        qemu_get_be32s(f, &s->dma_ch[i].id);
        qemu_get_be32s(f, &s->dma_ch[i].command);
    }

    s->bpp = LCCR3_BPP(s->control[3]);
    s->xres = s->yres = s->pal_for = -1;

    return 0;
}

#define BITS 8
#include "pxa2xx_template.h"
#define BITS 15
#include "pxa2xx_template.h"
#define BITS 16
#include "pxa2xx_template.h"
#define BITS 24
#include "pxa2xx_template.h"
#define BITS 32
#include "pxa2xx_template.h"

struct pxa2xx_lcdc_s *pxa2xx_lcdc_init(target_phys_addr_t base, qemu_irq irq)
{
    int iomemtype;
    struct pxa2xx_lcdc_s *s;

    s = (struct pxa2xx_lcdc_s *) qemu_mallocz(sizeof(struct pxa2xx_lcdc_s));
    s->invalidated = 1;
    s->irq = irq;

    pxa2xx_lcdc_orientation(s, graphic_rotate);

    iomemtype = cpu_register_io_memory(0, pxa2xx_lcdc_readfn,
                    pxa2xx_lcdc_writefn, s);
    cpu_register_physical_memory(base, 0x00100000, iomemtype);

    s->ds = graphic_console_init(pxa2xx_update_display,
                                 pxa2xx_invalidate_display,
                                 pxa2xx_screen_dump, NULL, s);

    switch (ds_get_bits_per_pixel(s->ds)) {
    case 0:
        s->dest_width = 0;
        break;
    case 8:
        s->line_fn[0] = pxa2xx_draw_fn_8;
        s->line_fn[1] = pxa2xx_draw_fn_8t;
        s->dest_width = 1;
        break;
    case 15:
        s->line_fn[0] = pxa2xx_draw_fn_15;
        s->line_fn[1] = pxa2xx_draw_fn_15t;
        s->dest_width = 2;
        break;
    case 16:
        s->line_fn[0] = pxa2xx_draw_fn_16;
        s->line_fn[1] = pxa2xx_draw_fn_16t;
        s->dest_width = 2;
        break;
    case 24:
        s->line_fn[0] = pxa2xx_draw_fn_24;
        s->line_fn[1] = pxa2xx_draw_fn_24t;
        s->dest_width = 3;
        break;
    case 32:
        s->line_fn[0] = pxa2xx_draw_fn_32;
        s->line_fn[1] = pxa2xx_draw_fn_32t;
        s->dest_width = 4;
        break;
    default:
        fprintf(stderr, "%s: Bad color depth\n", __FUNCTION__);
        exit(1);
    }

    register_savevm("pxa2xx_lcdc", 0, 0,
                    pxa2xx_lcdc_save, pxa2xx_lcdc_load, s);

    return s;
}

void pxa2xx_lcd_vsync_notifier(struct pxa2xx_lcdc_s *s, qemu_irq handler)
{
    s->vsync_cb = handler;
}
