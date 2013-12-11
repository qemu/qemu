/*
 * mxs_dma.c
 *
 * Copyright: Michel Pollet <buserror@gmail.com>
 *
 * QEMU Licence
 */

/*
 * Implements the DMA block of the mxs.
 * The current implementation can run chains of commands etc, however it's only
 * been tested with SSP for SD/MMC card access. It ought to work with normal SPI
 * too, and possibly other peripherals, however it's entirely untested
 */
#include "hw/sysbus.h"
#include "hw/arm/mxs.h"

/*
 * DMA IO block register numbers
 */
enum {
    DMA_CTRL0 = 0x0,
    DMA_CTRL1 = 0x1,
    DMA_CTRL2 = 0x2,
    DMA_DEVSEL1 = 0x3,
    DMA_DEVSEL2 = 0x4,
    DMA_MAX,

    /*
     * The DMA block for APBH and APBX have a different base address,
     * but they share a 7 words stride between channels.
     */
    DMA_STRIDE = 0x70,
    /*
     * Neither blocks uses that many, but there is space for them...
     */
    DMA_MAX_CHANNELS = 16,
};

/*
 * DMA channel register numbers
 */
enum {
    CH_CURCMD = 0,
    CH_NEXTCMD = 1,
    CH_CMD = 2,
    CH_BUFFER_ADDR = 3,
    CH_SEMA = 4,
    CH_DEBUG1 = 5,
    CH_DEBUG2 = 6,
};

/*
 * Channel command bit numbers
 */
enum {
    CH_CMD_IRQ_COMPLETE = 3,
    CH_CMD_SEMAPHORE = 6,
};

/*
 * nicked from linux
 * this is the memory representation of a DMA request
 */
struct mxs_dma_ccw {
    uint32_t next;
    uint16_t bits;
    uint16_t xfer_bytes;
#define MAX_XFER_BYTES	0xff00
    uint32_t bufaddr;
#define MXS_PIO_WORDS	16
    uint32_t pio_words[MXS_PIO_WORDS];
}__attribute__((packed));

/*
 * Per channel DMA description
 */
typedef struct mxs_dma_channel {
    QEMUTimer *timer;
    struct mxs_dma_state *dma;
    int channel; // channel index
    hwaddr base; // base of peripheral
    hwaddr dataoffset; // offset of the true in/out data latch register
    uint32_t r[10];
    qemu_irq irq;
} mxs_dma_channel;


typedef struct mxs_dma_state {
    SysBusDevice busdev;
    MemoryRegion iomem;
    const char * name;

    struct soc_dma_s * dma;
    uint32_t r[DMA_MAX];

    hwaddr base; // base of peripheral
    mxs_dma_channel channel[DMA_MAX_CHANNELS];
} mxs_dma_state;

static void mxs_dma_ch_update(mxs_dma_channel *s)
{
    struct mxs_dma_ccw req;
    int i;

    /* increment the semaphore, if needed */
    s->r[CH_SEMA] = (((s->r[CH_SEMA] >> 16) & 0xff) +
            (s->r[CH_SEMA] & 0xff)) << 16;
    if (!((s->r[CH_SEMA] >> 16) & 0xff)) {
        return;
    }
    /* read the request from memory */
    cpu_physical_memory_read(s->r[CH_NEXTCMD], &req, sizeof(req));
    /* update the latch registers accordingly */
    s->r[CH_CURCMD] = s->r[CH_NEXTCMD];
    s->r[CH_NEXTCMD] = req.next;
    s->r[CH_CMD] = (req.xfer_bytes << 16) | req.bits;
    s->r[CH_BUFFER_ADDR] = req.bufaddr;

    /* write PIO registers first, if any */
    for (i = 0; i < (req.bits >> 12); i++) {
        cpu_physical_memory_rw(s->base + (i << 4),
                (uint8_t*) &req.pio_words[i], 4, 1);
    }
    /* next handle any "data" requests */
    switch (req.bits & 0x3) {
        case 0:
            break; // PIO only
        case 0x1: { // WRITE (from periph to memory)
            uint32_t buf = req.bufaddr;
            uint8_t b = 0;
            while (req.xfer_bytes--) {
                cpu_physical_memory_rw(s->base + s->dataoffset, &b, 1, 0);
                cpu_physical_memory_rw(buf, &b, 1, 1);
                buf++;
            }
        }   break;
        case 0x2: { // READ (from memory to periph)
            uint32_t buf = req.bufaddr;
            uint8_t b = 0;
            while (req.xfer_bytes--) {
                cpu_physical_memory_rw(buf, &b, 1, 0);
                cpu_physical_memory_rw(s->base + s->dataoffset, &b, 1, 1);
                buf++;
            }
        }   break;
    }

    s->dma->r[DMA_CTRL1] |= 1 << s->channel;
    /* trigger IRQ if requested */
    if ((s->dma->r[DMA_CTRL1] >> 16) & (1 << s->channel)) {
        if (req.bits & (1 << CH_CMD_IRQ_COMPLETE)) {
            qemu_set_irq(s->irq, 1);
        }
    }

    /* decrement semaphore if requested */
    if (s->r[CH_CMD] & (1 << CH_CMD_SEMAPHORE)) {
        s->r[CH_SEMA] = (((s->r[CH_SEMA] >> 16) & 0xff) - 1) << 16;
    }
    /* If the semaphore is still on, try to trigger a chained request */
    if ((s->r[CH_SEMA] >> 16) & 0xff) {
        int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        timer_mod(s->timer, now + 10);
    }
}

/* called on one shot timer activation */
static void mxs_dma_ch_run(void *opaque)
{
    mxs_dma_channel *s = opaque;
    mxs_dma_ch_update(s);
}

static uint64_t mxs_dma_read(void *opaque, hwaddr offset, unsigned size)
{
    mxs_dma_state *s = (mxs_dma_state *) opaque;
    uint32_t res = 0;

    switch (offset >> 4) {
        case 0 ... DMA_MAX - 1:
            res = s->r[offset >> 4];
            break;
        default:
            if (offset >= s->base) {
                offset -= s->base;
                int channel = offset / DMA_STRIDE;
                int word = (offset % DMA_STRIDE) >> 4;
                res = s->channel[channel].r[word];
            } else
                qemu_log_mask(LOG_GUEST_ERROR,
                        "%s: bad offset 0x%x\n", __func__, (int) offset);
            break;
    }

    return res;
}

static void mxs_dma_write(void *opaque, hwaddr offset, uint64_t value,
        unsigned size)
{
    mxs_dma_state *s = (mxs_dma_state *) opaque;
    uint32_t oldvalue = 0;
    int channel, word, i;

    switch (offset >> 4) {
        case 0 ... DMA_MAX - 1:
            oldvalue = mxs_write(&s->r[offset >> 4], offset, value, size);
            break;
        default:
            if (offset >= s->base) {
                channel = (offset - s->base) / DMA_STRIDE;
                word = (offset - s->base) % DMA_STRIDE;
                oldvalue = mxs_write(
                        &s->channel[channel].r[word >> 4], word,
                        value, size);
                switch (word >> 4) {
                    case CH_SEMA:
                        // mask the new semaphore value, as only the lowest 8 bits are RW
                        s->channel[channel].r[CH_SEMA] =
                                (oldvalue & ~0xff) |
                                (s->channel[channel].r[CH_SEMA] & 0xff);
                        mxs_dma_ch_update(&s->channel[channel]);
                        break;
                }
            } else {
                qemu_log_mask(LOG_GUEST_ERROR,
                        "%s: bad offset 0x%x\n", __func__, (int) offset);
            }
            break;
    }
    switch (offset >> 4) {
        case DMA_CTRL0:
            if ((oldvalue ^ s->r[DMA_CTRL0]) == 0x80000000
                    && !(oldvalue & 0x80000000)) {
                // printf("%s write reseting, anding clockgate\n", s->name);
                s->r[DMA_CTRL0] |= 0x40000000;
            }
            break;
        case DMA_CTRL1:
            for (i = 0; i < DMA_MAX_CHANNELS; i++)
                if (s->channel[i].r[CH_NEXTCMD] &&
                        !(s->r[DMA_CTRL1] & (1 << i))) {
                    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
                    /* add a bit of latency to the timer. Ideally would
                     * do some calculation proportional to the transfer
                     * size. TODO ?
                     */
                    timer_mod(s->channel[i].timer, now + 100000);
                }
            break;
    }
}


static const MemoryRegionOps mxs_dma_ops = {
    .read = mxs_dma_read,
    .write = mxs_dma_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void mxs_dma_common_init(mxs_dma_state *s)
{
    int i;
    memory_region_init_io(&s->iomem, OBJECT(s), &mxs_dma_ops, s, "mxs_dma", 0x2000);
    sysbus_init_mmio(&s->busdev, &s->iomem);
    for (i = 0; i < DMA_MAX_CHANNELS; i++) {
        s->channel[i].dma = s;
        s->channel[i].channel = i;
        s->channel[i].timer =
                timer_new_ns(QEMU_CLOCK_VIRTUAL, mxs_dma_ch_run, &s->channel[i]);
    }
}

static int mxs_apbh_dma_init(SysBusDevice *dev)
{
    mxs_dma_state *s = OBJECT_CHECK(mxs_dma_state, dev, "mxs_apbh_dma");

    mxs_dma_common_init(s);
    s->name = "dma_apbh";
    s->base = 0x40;
    sysbus_init_irq(dev, &s->channel[MX23_DMA_SSP1].irq);
    sysbus_init_irq(dev, &s->channel[MX23_DMA_SSP2].irq);
    s->channel[MX23_DMA_SSP1].base = MX23_SSP1_BASE_ADDR;
    s->channel[MX23_DMA_SSP1].dataoffset = 0x70;
    s->channel[MX23_DMA_SSP2].base = MX23_SSP2_BASE_ADDR;
    s->channel[MX23_DMA_SSP2].dataoffset = 0x70;

    return 0;
}

static int mxs_apbx_dma_init(SysBusDevice *dev)
{
//    mxs_dma_state *s = FROM_SYSBUS(mxs_dma_state, dev);
    mxs_dma_state *s = OBJECT_CHECK(mxs_dma_state, dev, "mxs_apbx_dma");

    mxs_dma_common_init(s);
    s->name = "dma_apbx";
    s->base = 0x100;
    sysbus_init_irq(dev, &s->channel[MX23_DMA_ADC].irq);
    sysbus_init_irq(dev, &s->channel[MX23_DMA_DAC].irq);
    sysbus_init_irq(dev, &s->channel[MX23_DMA_SPDIF].irq);
    sysbus_init_irq(dev, &s->channel[MX23_DMA_I2C].irq);
    sysbus_init_irq(dev, &s->channel[MX23_DMA_SAIF0].irq);
    sysbus_init_irq(dev, &s->channel[MX23_DMA_UART0_RX].irq);
    sysbus_init_irq(dev, &s->channel[MX23_DMA_UART0_TX].irq);
    sysbus_init_irq(dev, &s->channel[MX23_DMA_UART1_RX].irq);
    sysbus_init_irq(dev, &s->channel[MX23_DMA_UART1_TX].irq);
    sysbus_init_irq(dev, &s->channel[MX23_DMA_SAIF1].irq);

    return 0;
}

static void mxs_apbh_dma_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *sdc = SYS_BUS_DEVICE_CLASS(klass);

    sdc->init = mxs_apbh_dma_init;
}

static void mxs_apbx_dma_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *sdc = SYS_BUS_DEVICE_CLASS(klass);

    sdc->init = mxs_apbx_dma_init;
}

static TypeInfo apbh_dma_info = {
    .name          = "mxs_apbh_dma",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(mxs_dma_state),
    .class_init    = mxs_apbh_dma_class_init,
};
static TypeInfo apbx_dma_info = {
    .name          = "mxs_apbx_dma",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(mxs_dma_state),
    .class_init    = mxs_apbx_dma_class_init,
};

static void mxs_dma_register(void)
{
    type_register_static(&apbh_dma_info);
    type_register_static(&apbx_dma_info);
}

type_init(mxs_dma_register)
