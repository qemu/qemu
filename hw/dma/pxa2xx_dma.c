/*
 * Intel XScale PXA255/270 DMA controller.
 *
 * Copyright (c) 2006 Openedhand Ltd.
 * Copyright (c) 2006 Thorsten Zitterell
 * Written by Andrzej Zaborowski <balrog@zabor.org>
 *
 * This code is licensed under the GPL.
 */

#include "hw/hw.h"
#include "hw/arm/pxa.h"
#include "hw/sysbus.h"

#define PXA255_DMA_NUM_CHANNELS 16
#define PXA27X_DMA_NUM_CHANNELS 32

#define PXA2XX_DMA_NUM_REQUESTS 75

typedef struct {
    uint32_t descr;
    uint32_t src;
    uint32_t dest;
    uint32_t cmd;
    uint32_t state;
    int request;
} PXA2xxDMAChannel;

#define TYPE_PXA2XX_DMA "pxa2xx-dma"
#define PXA2XX_DMA(obj) OBJECT_CHECK(PXA2xxDMAState, (obj), TYPE_PXA2XX_DMA)

typedef struct PXA2xxDMAState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t stopintr;
    uint32_t eorintr;
    uint32_t rasintr;
    uint32_t startintr;
    uint32_t endintr;

    uint32_t align;
    uint32_t pio;

    int channels;
    PXA2xxDMAChannel *chan;

    uint8_t req[PXA2XX_DMA_NUM_REQUESTS];

    /* Flag to avoid recursive DMA invocations.  */
    int running;
} PXA2xxDMAState;

#define DCSR0	0x0000	/* DMA Control / Status register for Channel 0 */
#define DCSR31	0x007c	/* DMA Control / Status register for Channel 31 */
#define DALGN	0x00a0	/* DMA Alignment register */
#define DPCSR	0x00a4	/* DMA Programmed I/O Control Status register */
#define DRQSR0	0x00e0	/* DMA DREQ<0> Status register */
#define DRQSR1	0x00e4	/* DMA DREQ<1> Status register */
#define DRQSR2	0x00e8	/* DMA DREQ<2> Status register */
#define DINT	0x00f0	/* DMA Interrupt register */
#define DRCMR0	0x0100	/* Request to Channel Map register 0 */
#define DRCMR63	0x01fc	/* Request to Channel Map register 63 */
#define D_CH0	0x0200	/* Channel 0 Descriptor start */
#define DRCMR64	0x1100	/* Request to Channel Map register 64 */
#define DRCMR74	0x1128	/* Request to Channel Map register 74 */

/* Per-channel register */
#define DDADR	0x00
#define DSADR	0x01
#define DTADR	0x02
#define DCMD	0x03

/* Bit-field masks */
#define DRCMR_CHLNUM		0x1f
#define DRCMR_MAPVLD		(1 << 7)
#define DDADR_STOP		(1 << 0)
#define DDADR_BREN		(1 << 1)
#define DCMD_LEN		0x1fff
#define DCMD_WIDTH(x)		(1 << ((((x) >> 14) & 3) - 1))
#define DCMD_SIZE(x)		(4 << (((x) >> 16) & 3))
#define DCMD_FLYBYT		(1 << 19)
#define DCMD_FLYBYS		(1 << 20)
#define DCMD_ENDIRQEN		(1 << 21)
#define DCMD_STARTIRQEN		(1 << 22)
#define DCMD_CMPEN		(1 << 25)
#define DCMD_FLOWTRG		(1 << 28)
#define DCMD_FLOWSRC		(1 << 29)
#define DCMD_INCTRGADDR		(1 << 30)
#define DCMD_INCSRCADDR		(1 << 31)
#define DCSR_BUSERRINTR		(1 << 0)
#define DCSR_STARTINTR		(1 << 1)
#define DCSR_ENDINTR		(1 << 2)
#define DCSR_STOPINTR		(1 << 3)
#define DCSR_RASINTR		(1 << 4)
#define DCSR_REQPEND		(1 << 8)
#define DCSR_EORINT		(1 << 9)
#define DCSR_CMPST		(1 << 10)
#define DCSR_MASKRUN		(1 << 22)
#define DCSR_RASIRQEN		(1 << 23)
#define DCSR_CLRCMPST		(1 << 24)
#define DCSR_SETCMPST		(1 << 25)
#define DCSR_EORSTOPEN		(1 << 26)
#define DCSR_EORJMPEN		(1 << 27)
#define DCSR_EORIRQEN		(1 << 28)
#define DCSR_STOPIRQEN		(1 << 29)
#define DCSR_NODESCFETCH	(1 << 30)
#define DCSR_RUN		(1 << 31)

static inline void pxa2xx_dma_update(PXA2xxDMAState *s, int ch)
{
    if (ch >= 0) {
        if ((s->chan[ch].state & DCSR_STOPIRQEN) &&
                (s->chan[ch].state & DCSR_STOPINTR))
            s->stopintr |= 1 << ch;
        else
            s->stopintr &= ~(1 << ch);

        if ((s->chan[ch].state & DCSR_EORIRQEN) &&
                (s->chan[ch].state & DCSR_EORINT))
            s->eorintr |= 1 << ch;
        else
            s->eorintr &= ~(1 << ch);

        if ((s->chan[ch].state & DCSR_RASIRQEN) &&
                (s->chan[ch].state & DCSR_RASINTR))
            s->rasintr |= 1 << ch;
        else
            s->rasintr &= ~(1 << ch);

        if (s->chan[ch].state & DCSR_STARTINTR)
            s->startintr |= 1 << ch;
        else
            s->startintr &= ~(1 << ch);

        if (s->chan[ch].state & DCSR_ENDINTR)
            s->endintr |= 1 << ch;
        else
            s->endintr &= ~(1 << ch);
    }

    if (s->stopintr | s->eorintr | s->rasintr | s->startintr | s->endintr)
        qemu_irq_raise(s->irq);
    else
        qemu_irq_lower(s->irq);
}

static inline void pxa2xx_dma_descriptor_fetch(
                PXA2xxDMAState *s, int ch)
{
    uint32_t desc[4];
    hwaddr daddr = s->chan[ch].descr & ~0xf;
    if ((s->chan[ch].descr & DDADR_BREN) && (s->chan[ch].state & DCSR_CMPST))
        daddr += 32;

    cpu_physical_memory_read(daddr, desc, 16);
    s->chan[ch].descr = desc[DDADR];
    s->chan[ch].src = desc[DSADR];
    s->chan[ch].dest = desc[DTADR];
    s->chan[ch].cmd = desc[DCMD];

    if (s->chan[ch].cmd & DCMD_FLOWSRC)
        s->chan[ch].src &= ~3;
    if (s->chan[ch].cmd & DCMD_FLOWTRG)
        s->chan[ch].dest &= ~3;

    if (s->chan[ch].cmd & (DCMD_CMPEN | DCMD_FLYBYS | DCMD_FLYBYT))
        printf("%s: unsupported mode in channel %i\n", __FUNCTION__, ch);

    if (s->chan[ch].cmd & DCMD_STARTIRQEN)
        s->chan[ch].state |= DCSR_STARTINTR;
}

static void pxa2xx_dma_run(PXA2xxDMAState *s)
{
    int c, srcinc, destinc;
    uint32_t n, size;
    uint32_t width;
    uint32_t length;
    uint8_t buffer[32];
    PXA2xxDMAChannel *ch;

    if (s->running ++)
        return;

    while (s->running) {
        s->running = 1;
        for (c = 0; c < s->channels; c ++) {
            ch = &s->chan[c];

            while ((ch->state & DCSR_RUN) && !(ch->state & DCSR_STOPINTR)) {
                /* Test for pending requests */
                if ((ch->cmd & (DCMD_FLOWSRC | DCMD_FLOWTRG)) && !ch->request)
                    break;

                length = ch->cmd & DCMD_LEN;
                size = DCMD_SIZE(ch->cmd);
                width = DCMD_WIDTH(ch->cmd);

                srcinc = (ch->cmd & DCMD_INCSRCADDR) ? width : 0;
                destinc = (ch->cmd & DCMD_INCTRGADDR) ? width : 0;

                while (length) {
                    size = MIN(length, size);

                    for (n = 0; n < size; n += width) {
                        cpu_physical_memory_read(ch->src, buffer + n, width);
                        ch->src += srcinc;
                    }

                    for (n = 0; n < size; n += width) {
                        cpu_physical_memory_write(ch->dest, buffer + n, width);
                        ch->dest += destinc;
                    }

                    length -= size;

                    if ((ch->cmd & (DCMD_FLOWSRC | DCMD_FLOWTRG)) &&
                            !ch->request) {
                        ch->state |= DCSR_EORINT;
                        if (ch->state & DCSR_EORSTOPEN)
                            ch->state |= DCSR_STOPINTR;
                        if ((ch->state & DCSR_EORJMPEN) &&
                                        !(ch->state & DCSR_NODESCFETCH))
                            pxa2xx_dma_descriptor_fetch(s, c);
                        break;
		    }
                }

                ch->cmd = (ch->cmd & ~DCMD_LEN) | length;

                /* Is the transfer complete now? */
                if (!length) {
                    if (ch->cmd & DCMD_ENDIRQEN)
                        ch->state |= DCSR_ENDINTR;

                    if ((ch->state & DCSR_NODESCFETCH) ||
                                (ch->descr & DDADR_STOP) ||
                                (ch->state & DCSR_EORSTOPEN)) {
                        ch->state |= DCSR_STOPINTR;
                        ch->state &= ~DCSR_RUN;

                        break;
                    }

                    ch->state |= DCSR_STOPINTR;
                    break;
                }
            }
        }

        s->running --;
    }
}

static uint64_t pxa2xx_dma_read(void *opaque, hwaddr offset,
                                unsigned size)
{
    PXA2xxDMAState *s = (PXA2xxDMAState *) opaque;
    unsigned int channel;

    if (size != 4) {
        hw_error("%s: Bad access width\n", __FUNCTION__);
        return 5;
    }

    switch (offset) {
    case DRCMR64 ... DRCMR74:
        offset -= DRCMR64 - DRCMR0 - (64 << 2);
        /* Fall through */
    case DRCMR0 ... DRCMR63:
        channel = (offset - DRCMR0) >> 2;
        return s->req[channel];

    case DRQSR0:
    case DRQSR1:
    case DRQSR2:
        return 0;

    case DCSR0 ... DCSR31:
        channel = offset >> 2;
	if (s->chan[channel].request)
            return s->chan[channel].state | DCSR_REQPEND;
        return s->chan[channel].state;

    case DINT:
        return s->stopintr | s->eorintr | s->rasintr |
                s->startintr | s->endintr;

    case DALGN:
        return s->align;

    case DPCSR:
        return s->pio;
    }

    if (offset >= D_CH0 && offset < D_CH0 + (s->channels << 4)) {
        channel = (offset - D_CH0) >> 4;
        switch ((offset & 0x0f) >> 2) {
        case DDADR:
            return s->chan[channel].descr;
        case DSADR:
            return s->chan[channel].src;
        case DTADR:
            return s->chan[channel].dest;
        case DCMD:
            return s->chan[channel].cmd;
        }
    }

    hw_error("%s: Bad offset 0x" TARGET_FMT_plx "\n", __FUNCTION__, offset);
    return 7;
}

static void pxa2xx_dma_write(void *opaque, hwaddr offset,
                             uint64_t value, unsigned size)
{
    PXA2xxDMAState *s = (PXA2xxDMAState *) opaque;
    unsigned int channel;

    if (size != 4) {
        hw_error("%s: Bad access width\n", __FUNCTION__);
        return;
    }

    switch (offset) {
    case DRCMR64 ... DRCMR74:
        offset -= DRCMR64 - DRCMR0 - (64 << 2);
        /* Fall through */
    case DRCMR0 ... DRCMR63:
        channel = (offset - DRCMR0) >> 2;

        if (value & DRCMR_MAPVLD)
            if ((value & DRCMR_CHLNUM) > s->channels)
                hw_error("%s: Bad DMA channel %i\n",
                         __FUNCTION__, (unsigned)value & DRCMR_CHLNUM);

        s->req[channel] = value;
        break;

    case DRQSR0:
    case DRQSR1:
    case DRQSR2:
        /* Nothing to do */
        break;

    case DCSR0 ... DCSR31:
        channel = offset >> 2;
        s->chan[channel].state &= 0x0000071f & ~(value &
                        (DCSR_EORINT | DCSR_ENDINTR |
                         DCSR_STARTINTR | DCSR_BUSERRINTR));
        s->chan[channel].state |= value & 0xfc800000;

        if (s->chan[channel].state & DCSR_STOPIRQEN)
            s->chan[channel].state &= ~DCSR_STOPINTR;

        if (value & DCSR_NODESCFETCH) {
            /* No-descriptor-fetch mode */
            if (value & DCSR_RUN) {
                s->chan[channel].state &= ~DCSR_STOPINTR;
                pxa2xx_dma_run(s);
            }
        } else {
            /* Descriptor-fetch mode */
            if (value & DCSR_RUN) {
                s->chan[channel].state &= ~DCSR_STOPINTR;
                pxa2xx_dma_descriptor_fetch(s, channel);
                pxa2xx_dma_run(s);
            }
        }

        /* Shouldn't matter as our DMA is synchronous.  */
        if (!(value & (DCSR_RUN | DCSR_MASKRUN)))
            s->chan[channel].state |= DCSR_STOPINTR;

        if (value & DCSR_CLRCMPST)
            s->chan[channel].state &= ~DCSR_CMPST;
        if (value & DCSR_SETCMPST)
            s->chan[channel].state |= DCSR_CMPST;

        pxa2xx_dma_update(s, channel);
        break;

    case DALGN:
        s->align = value;
        break;

    case DPCSR:
        s->pio = value & 0x80000001;
        break;

    default:
        if (offset >= D_CH0 && offset < D_CH0 + (s->channels << 4)) {
            channel = (offset - D_CH0) >> 4;
            switch ((offset & 0x0f) >> 2) {
            case DDADR:
                s->chan[channel].descr = value;
                break;
            case DSADR:
                s->chan[channel].src = value;
                break;
            case DTADR:
                s->chan[channel].dest = value;
                break;
            case DCMD:
                s->chan[channel].cmd = value;
                break;
            default:
                goto fail;
            }

            break;
        }
    fail:
        hw_error("%s: Bad offset " TARGET_FMT_plx "\n", __FUNCTION__, offset);
    }
}

static const MemoryRegionOps pxa2xx_dma_ops = {
    .read = pxa2xx_dma_read,
    .write = pxa2xx_dma_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void pxa2xx_dma_request(void *opaque, int req_num, int on)
{
    PXA2xxDMAState *s = opaque;
    int ch;
    if (req_num < 0 || req_num >= PXA2XX_DMA_NUM_REQUESTS)
        hw_error("%s: Bad DMA request %i\n", __FUNCTION__, req_num);

    if (!(s->req[req_num] & DRCMR_MAPVLD))
        return;
    ch = s->req[req_num] & DRCMR_CHLNUM;

    if (!s->chan[ch].request && on)
        s->chan[ch].state |= DCSR_RASINTR;
    else
        s->chan[ch].state &= ~DCSR_RASINTR;
    if (s->chan[ch].request && !on)
        s->chan[ch].state |= DCSR_EORINT;

    s->chan[ch].request = on;
    if (on) {
        pxa2xx_dma_run(s);
        pxa2xx_dma_update(s, ch);
    }
}

static int pxa2xx_dma_init(SysBusDevice *sbd)
{
    DeviceState *dev = DEVICE(sbd);
    PXA2xxDMAState *s = PXA2XX_DMA(dev);
    int i;

    if (s->channels <= 0) {
        return -1;
    }

    s->chan = g_malloc0(sizeof(PXA2xxDMAChannel) * s->channels);

    memset(s->chan, 0, sizeof(PXA2xxDMAChannel) * s->channels);
    for (i = 0; i < s->channels; i ++)
        s->chan[i].state = DCSR_STOPINTR;

    memset(s->req, 0, sizeof(uint8_t) * PXA2XX_DMA_NUM_REQUESTS);

    qdev_init_gpio_in(dev, pxa2xx_dma_request, PXA2XX_DMA_NUM_REQUESTS);

    memory_region_init_io(&s->iomem, OBJECT(s), &pxa2xx_dma_ops, s,
                          "pxa2xx.dma", 0x00010000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    return 0;
}

DeviceState *pxa27x_dma_init(hwaddr base, qemu_irq irq)
{
    DeviceState *dev;

    dev = qdev_create(NULL, "pxa2xx-dma");
    qdev_prop_set_int32(dev, "channels", PXA27X_DMA_NUM_CHANNELS);
    qdev_init_nofail(dev);

    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq);

    return dev;
}

DeviceState *pxa255_dma_init(hwaddr base, qemu_irq irq)
{
    DeviceState *dev;

    dev = qdev_create(NULL, "pxa2xx-dma");
    qdev_prop_set_int32(dev, "channels", PXA27X_DMA_NUM_CHANNELS);
    qdev_init_nofail(dev);

    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq);

    return dev;
}

static bool is_version_0(void *opaque, int version_id)
{
    return version_id == 0;
}

static VMStateDescription vmstate_pxa2xx_dma_chan = {
    .name = "pxa2xx_dma_chan",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(descr, PXA2xxDMAChannel),
        VMSTATE_UINT32(src, PXA2xxDMAChannel),
        VMSTATE_UINT32(dest, PXA2xxDMAChannel),
        VMSTATE_UINT32(cmd, PXA2xxDMAChannel),
        VMSTATE_UINT32(state, PXA2xxDMAChannel),
        VMSTATE_INT32(request, PXA2xxDMAChannel),
        VMSTATE_END_OF_LIST(),
    },
};

static VMStateDescription vmstate_pxa2xx_dma = {
    .name = "pxa2xx_dma",
    .version_id = 1,
    .minimum_version_id = 0,
    .minimum_version_id_old = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UNUSED_TEST(is_version_0, 4),
        VMSTATE_UINT32(stopintr, PXA2xxDMAState),
        VMSTATE_UINT32(eorintr, PXA2xxDMAState),
        VMSTATE_UINT32(rasintr, PXA2xxDMAState),
        VMSTATE_UINT32(startintr, PXA2xxDMAState),
        VMSTATE_UINT32(endintr, PXA2xxDMAState),
        VMSTATE_UINT32(align, PXA2xxDMAState),
        VMSTATE_UINT32(pio, PXA2xxDMAState),
        VMSTATE_BUFFER(req, PXA2xxDMAState),
        VMSTATE_STRUCT_VARRAY_POINTER_INT32(chan, PXA2xxDMAState, channels,
                vmstate_pxa2xx_dma_chan, PXA2xxDMAChannel),
        VMSTATE_END_OF_LIST(),
    },
};

static Property pxa2xx_dma_properties[] = {
    DEFINE_PROP_INT32("channels", PXA2xxDMAState, channels, -1),
    DEFINE_PROP_END_OF_LIST(),
};

static void pxa2xx_dma_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = pxa2xx_dma_init;
    dc->desc = "PXA2xx DMA controller";
    dc->vmsd = &vmstate_pxa2xx_dma;
    dc->props = pxa2xx_dma_properties;
}

static const TypeInfo pxa2xx_dma_info = {
    .name          = TYPE_PXA2XX_DMA,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PXA2xxDMAState),
    .class_init    = pxa2xx_dma_class_init,
};

static void pxa2xx_dma_register_types(void)
{
    type_register_static(&pxa2xx_dma_info);
}

type_init(pxa2xx_dma_register_types)
