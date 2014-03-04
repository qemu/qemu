/*
 * mxs_ssp.c
 *
 * Copyright: Michel Pollet <buserror@gmail.com>
 *
 * QEMU Licence
 */

/*
 * This implements the SSP port(s) of the mxs. Currently hardcoded for the
 * SD card interface, but as TODO it could rather easily be made to be generic
 * and support 'generic' SPI too.
 * It is geared toward working with DMA, as the linux drivers uses it that way.
 */
#include "hw/sysbus.h"
#include "hw/arm/mxs.h"
#include "sysemu/blockdev.h"
#include "hw/sd.h"

/*
 * SSP register indexes, most of the useful ones
 */
enum {
    SSP_CTRL = 0x0,
    SSP_SD_CMD0 = 0x1,
    SSP_SD_CMD1 = 0x2,
    SSP_COMPREF = 0x3,
    SSP_COMPMASK = 0x4,
    SSP_TIMING = 0x5,
    SSP_CTRL1 = 0x6,
    SSP_DATA = 0x7,
    SSP_SDRESP0 = 0x8,
    SSP_SDRESP1 = 0x9,
    SSP_SDRESP2 = 0xa,
    SSP_SDRESP3 = 0xb,
    SSP_STATUS = 0xc,

    SSP_VERSION = 0x11,
    SSP_MAX,
};

/*
 * SSP_CTRL bit numbers
 */
enum {
    CTRL_READ = 25,
    CTRL_DATA_XFER = 24,
    CTRL_ENABLE = 16,
    CTRL_LONG_REST = 19,
};
/*
 * SSP_STAT bit numbers
 */
enum {
    STAT_BUSY = 0,
    STAT_DATA_BUSY = 2,
    STAT_CMD_BUSY = 3,
    STAT_CARD_DETECT = 28,
};

typedef struct mxs_ssp_state {
    SysBusDevice busdev;
    MemoryRegion iomem;

    uint32_t r[SSP_MAX];
    qemu_irq irq_dma, irq_error;
    SDState *sd;
} mxs_ssp_state;

static uint64_t mxs_ssp_read(
        void *opaque, hwaddr offset, unsigned size)
{
    mxs_ssp_state *s = (mxs_ssp_state *) opaque;
    uint32_t res = 0;

    switch (offset >> 4) {
        case 0 ... SSP_MAX:
            res = s->r[offset >> 4];
            switch (offset >> 4) {
                case SSP_STATUS:
                    s->r[SSP_STATUS] &= ~((1 << STAT_BUSY) |
                            (1 << STAT_DATA_BUSY) | (1 << STAT_CMD_BUSY));
                    break;
                    /* dma polls this register to read the data from the card
                     * this is not very efficient, perhaps a better data conduit
                     * is available. It does work as the real hardware tho...
                     */
                case SSP_DATA:
                    res = sd_read_data(s->sd);
                    break;
            }
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                    "%s: bad offset 0x%x\n", __func__, (int) offset);
            break;
    }

    return res;
}

static uint32_t __swap(uint32_t w)
{
	return (w >> 24) | ((w & 0x00ff0000) >> 8) |
			((w & 0x0000ff00) << 8) | (w << 24);
}

/*
 * processes one SD/MMC command train. It always have a 'command' but
 * can also have datas attached, this case is not handled here, it's
 * handled by the SD layer.
 * The command can either be short or long, wierdly, the mxs returns
 * the bytes in some funky order that needs to be restored.
 * TODO: Make big endian compatible
 */
static void mxs_process_cmd(mxs_ssp_state *s)
{
    if (!(s->r[SSP_CTRL] & (1 << CTRL_ENABLE)))
        return;
    uint32_t r[4]; // temporary buffer

    s->r[SSP_SDRESP0] = s->r[SSP_SDRESP1] =
            s->r[SSP_SDRESP2] = s->r[SSP_SDRESP3] = 0;

    SDRequest cmd = {
            .cmd = s->r[SSP_SD_CMD0] & 0xff,
            .arg = s->r[SSP_SD_CMD1],
            .crc = 0,
    };
    sd_enable(s->sd, 1);
    sd_do_command(s->sd, &cmd, (uint8_t*) r);
    if (s->r[SSP_CTRL] & (1 << CTRL_LONG_REST)) {
        s->r[SSP_SDRESP0] = __swap(r[3]);
        s->r[SSP_SDRESP1] = __swap(r[2]);
        s->r[SSP_SDRESP2] = __swap(r[1]);
        s->r[SSP_SDRESP3] = __swap(r[0]);
    } else
        s->r[SSP_SDRESP0] = __swap(r[0]);

    /* mark these flags as busy, they will be read once
     * as 'busy' before being cleared by a read. */
    s->r[SSP_STATUS] |= (1 << STAT_CMD_BUSY);
    s->r[SSP_STATUS] |= (1 << STAT_BUSY);
    if (s->r[SSP_CTRL] & (1 << CTRL_DATA_XFER))
        s->r[SSP_STATUS] |= (1 << STAT_DATA_BUSY);
}

static void mxs_ssp_write(void *opaque, hwaddr offset,
        uint64_t value, unsigned size)
{
    mxs_ssp_state *s = (mxs_ssp_state *) opaque;
    uint32_t oldvalue = 0;

    switch (offset >> 4) {
        case 0 ... SSP_MAX:
            oldvalue = mxs_write(&s->r[offset >> 4], offset, value, size);
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                    "%s: bad offset 0x%x\n", __func__, (int) offset);
            break;
    }
    switch (offset >> 4) {
        case SSP_CTRL:
            if ((oldvalue ^ s->r[SSP_CTRL]) == 0x80000000
                    && !(oldvalue & 0x80000000)) {
             //   printf("%s reseting, anding clockgate\n", __func__);
                // TODO: Implement a reset function?
                s->r[SSP_CTRL] |= 0x40000000;
            }
            break;
        case SSP_SD_CMD1:
            mxs_process_cmd(s);
            break;
            /*
             * Write from DMA
             * TODO: Handle case were it's not a SD/MMC but a normal SPI
             */
        case SSP_DATA:
            sd_write_data(s->sd, s->r[SSP_DATA]);
            break;
        case SSP_STATUS:
            s->r[SSP_STATUS] = oldvalue;
            qemu_log_mask(LOG_GUEST_ERROR,
                    "%s: invalid write to SSP_STATUS\n", __func__);
            break;
    }
}


static const MemoryRegionOps mxs_ssp_ops = {
    .read = mxs_ssp_read,
    .write = mxs_ssp_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int mxs_ssp_init(SysBusDevice *dev)
{
    mxs_ssp_state *s = OBJECT_CHECK(mxs_ssp_state, dev, "mxs_ssp");

    sysbus_init_irq(dev, &s->irq_dma);
    sysbus_init_irq(dev, &s->irq_error);
    memory_region_init_io(&s->iomem, OBJECT(s), &mxs_ssp_ops, s,
            "mxs_ssp", 0x2000);
    sysbus_init_mmio(dev, &s->iomem);

    s->r[SSP_CTRL] = 0xc0000000;
    s->r[SSP_STATUS] = 0xe0000000;
    s->r[SSP_VERSION] = 0x03000000;

    DriveInfo *dinfo = drive_get_next(IF_SD);

    s->sd = sd_init(dinfo ? dinfo->bdrv : NULL, 0);

    return 0;
}


static void mxs_ssp_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *sdc = SYS_BUS_DEVICE_CLASS(klass);

    sdc->init = mxs_ssp_init;
}

static TypeInfo ssp_info = {
    .name          = "mxs_ssp",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(mxs_ssp_state),
    .class_init    = mxs_ssp_class_init,
};

static void mxs_ssp_register(void)
{
    type_register_static(&ssp_info);
}

type_init(mxs_ssp_register)

