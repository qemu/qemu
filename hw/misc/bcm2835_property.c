/*
 * Raspberry Pi emulation (c) 2012 Gregory Estrade
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/misc/bcm2835_property.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "hw/irq.h"
#include "hw/misc/bcm2835_mbox_defs.h"
#include "hw/misc/raspberrypi-fw-defs.h"
#include "sysemu/dma.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "trace.h"
#include "hw/arm/raspi_platform.h"

/* https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface */

static void bcm2835_property_mbox_push(BCM2835PropertyState *s, uint32_t value)
{
    uint32_t tag;
    uint32_t bufsize;
    uint32_t tot_len;
    size_t resplen;
    uint32_t tmp;
    int n;
    uint32_t offset, length, color;

    /*
     * Copy the current state of the framebuffer config; we will update
     * this copy as we process tags and then ask the framebuffer to use
     * it at the end.
     */
    BCM2835FBConfig fbconfig = s->fbdev->config;
    bool fbconfig_updated = false;

    value &= ~0xf;

    s->addr = value;

    tot_len = ldl_le_phys(&s->dma_as, value);

    /* @(addr + 4) : Buffer response code */
    value = s->addr + 8;
    while (value + 8 <= s->addr + tot_len) {
        tag = ldl_le_phys(&s->dma_as, value);
        bufsize = ldl_le_phys(&s->dma_as, value + 4);
        /* @(value + 8) : Request/response indicator */
        resplen = 0;
        switch (tag) {
        case RPI_FWREQ_PROPERTY_END:
            break;
        case RPI_FWREQ_GET_FIRMWARE_REVISION:
            stl_le_phys(&s->dma_as, value + 12, 346337);
            resplen = 4;
            break;
        case RPI_FWREQ_GET_BOARD_MODEL:
            qemu_log_mask(LOG_UNIMP,
                          "bcm2835_property: 0x%08x get board model NYI\n",
                          tag);
            resplen = 4;
            break;
        case RPI_FWREQ_GET_BOARD_REVISION:
            stl_le_phys(&s->dma_as, value + 12, s->board_rev);
            resplen = 4;
            break;
        case RPI_FWREQ_GET_BOARD_MAC_ADDRESS:
            resplen = sizeof(s->macaddr.a);
            dma_memory_write(&s->dma_as, value + 12, s->macaddr.a, resplen,
                             MEMTXATTRS_UNSPECIFIED);
            break;
        case RPI_FWREQ_GET_BOARD_SERIAL:
            qemu_log_mask(LOG_UNIMP,
                          "bcm2835_property: 0x%08x get board serial NYI\n",
                          tag);
            resplen = 8;
            break;
        case RPI_FWREQ_GET_ARM_MEMORY:
            /* base */
            stl_le_phys(&s->dma_as, value + 12, 0);
            /* size */
            stl_le_phys(&s->dma_as, value + 16, s->fbdev->vcram_base);
            resplen = 8;
            break;
        case RPI_FWREQ_GET_VC_MEMORY:
            /* base */
            stl_le_phys(&s->dma_as, value + 12, s->fbdev->vcram_base);
            /* size */
            stl_le_phys(&s->dma_as, value + 16, s->fbdev->vcram_size);
            resplen = 8;
            break;
        case RPI_FWREQ_SET_POWER_STATE:
            /* Assume that whatever device they asked for exists,
             * and we'll just claim we set it to the desired state
             */
            tmp = ldl_le_phys(&s->dma_as, value + 16);
            stl_le_phys(&s->dma_as, value + 16, (tmp & 1));
            resplen = 8;
            break;

        /* Clocks */

        case RPI_FWREQ_GET_CLOCK_STATE:
            stl_le_phys(&s->dma_as, value + 16, 0x1);
            resplen = 8;
            break;

        case RPI_FWREQ_SET_CLOCK_STATE:
            qemu_log_mask(LOG_UNIMP,
                          "bcm2835_property: 0x%08x set clock state NYI\n",
                          tag);
            resplen = 8;
            break;

        case RPI_FWREQ_GET_CLOCK_RATE:
        case RPI_FWREQ_GET_MAX_CLOCK_RATE:
        case RPI_FWREQ_GET_MIN_CLOCK_RATE:
            switch (ldl_le_phys(&s->dma_as, value + 12)) {
            case RPI_FIRMWARE_EMMC_CLK_ID:
                stl_le_phys(&s->dma_as, value + 16, RPI_FIRMWARE_EMMC_CLK_RATE);
                break;
            case RPI_FIRMWARE_UART_CLK_ID:
                stl_le_phys(&s->dma_as, value + 16, RPI_FIRMWARE_UART_CLK_RATE);
                break;
            case RPI_FIRMWARE_CORE_CLK_ID:
                stl_le_phys(&s->dma_as, value + 16, RPI_FIRMWARE_CORE_CLK_RATE);
                break;
            default:
                stl_le_phys(&s->dma_as, value + 16,
                            RPI_FIRMWARE_DEFAULT_CLK_RATE);
                break;
            }
            resplen = 8;
            break;

        case RPI_FWREQ_SET_CLOCK_RATE:
        case RPI_FWREQ_SET_MAX_CLOCK_RATE:
        case RPI_FWREQ_SET_MIN_CLOCK_RATE:
            qemu_log_mask(LOG_UNIMP,
                          "bcm2835_property: 0x%08x set clock rate NYI\n",
                          tag);
            resplen = 8;
            break;

        /* Temperature */

        case RPI_FWREQ_GET_TEMPERATURE:
            stl_le_phys(&s->dma_as, value + 16, 25000);
            resplen = 8;
            break;

        case RPI_FWREQ_GET_MAX_TEMPERATURE:
            stl_le_phys(&s->dma_as, value + 16, 99000);
            resplen = 8;
            break;

        /* Frame buffer */

        case RPI_FWREQ_FRAMEBUFFER_ALLOCATE:
            stl_le_phys(&s->dma_as, value + 12, fbconfig.base);
            stl_le_phys(&s->dma_as, value + 16,
                        bcm2835_fb_get_size(&fbconfig));
            resplen = 8;
            break;
        case RPI_FWREQ_FRAMEBUFFER_RELEASE:
            resplen = 0;
            break;
        case RPI_FWREQ_FRAMEBUFFER_BLANK:
            resplen = 4;
            break;
        case RPI_FWREQ_FRAMEBUFFER_TEST_PHYSICAL_WIDTH_HEIGHT:
        case RPI_FWREQ_FRAMEBUFFER_TEST_VIRTUAL_WIDTH_HEIGHT:
            resplen = 8;
            break;
        case RPI_FWREQ_FRAMEBUFFER_SET_PHYSICAL_WIDTH_HEIGHT:
            fbconfig.xres = ldl_le_phys(&s->dma_as, value + 12);
            fbconfig.yres = ldl_le_phys(&s->dma_as, value + 16);
            bcm2835_fb_validate_config(&fbconfig);
            fbconfig_updated = true;
            /* fall through */
        case RPI_FWREQ_FRAMEBUFFER_GET_PHYSICAL_WIDTH_HEIGHT:
            stl_le_phys(&s->dma_as, value + 12, fbconfig.xres);
            stl_le_phys(&s->dma_as, value + 16, fbconfig.yres);
            resplen = 8;
            break;
        case RPI_FWREQ_FRAMEBUFFER_SET_VIRTUAL_WIDTH_HEIGHT:
            fbconfig.xres_virtual = ldl_le_phys(&s->dma_as, value + 12);
            fbconfig.yres_virtual = ldl_le_phys(&s->dma_as, value + 16);
            bcm2835_fb_validate_config(&fbconfig);
            fbconfig_updated = true;
            /* fall through */
        case RPI_FWREQ_FRAMEBUFFER_GET_VIRTUAL_WIDTH_HEIGHT:
            stl_le_phys(&s->dma_as, value + 12, fbconfig.xres_virtual);
            stl_le_phys(&s->dma_as, value + 16, fbconfig.yres_virtual);
            resplen = 8;
            break;
        case RPI_FWREQ_FRAMEBUFFER_TEST_DEPTH:
            resplen = 4;
            break;
        case RPI_FWREQ_FRAMEBUFFER_SET_DEPTH:
            fbconfig.bpp = ldl_le_phys(&s->dma_as, value + 12);
            bcm2835_fb_validate_config(&fbconfig);
            fbconfig_updated = true;
            /* fall through */
        case RPI_FWREQ_FRAMEBUFFER_GET_DEPTH:
            stl_le_phys(&s->dma_as, value + 12, fbconfig.bpp);
            resplen = 4;
            break;
        case RPI_FWREQ_FRAMEBUFFER_TEST_PIXEL_ORDER:
            resplen = 4;
            break;
        case RPI_FWREQ_FRAMEBUFFER_SET_PIXEL_ORDER:
            fbconfig.pixo = ldl_le_phys(&s->dma_as, value + 12);
            bcm2835_fb_validate_config(&fbconfig);
            fbconfig_updated = true;
            /* fall through */
        case RPI_FWREQ_FRAMEBUFFER_GET_PIXEL_ORDER:
            stl_le_phys(&s->dma_as, value + 12, fbconfig.pixo);
            resplen = 4;
            break;
        case RPI_FWREQ_FRAMEBUFFER_TEST_ALPHA_MODE:
            resplen = 4;
            break;
        case RPI_FWREQ_FRAMEBUFFER_SET_ALPHA_MODE:
            fbconfig.alpha = ldl_le_phys(&s->dma_as, value + 12);
            bcm2835_fb_validate_config(&fbconfig);
            fbconfig_updated = true;
            /* fall through */
        case RPI_FWREQ_FRAMEBUFFER_GET_ALPHA_MODE:
            stl_le_phys(&s->dma_as, value + 12, fbconfig.alpha);
            resplen = 4;
            break;
        case RPI_FWREQ_FRAMEBUFFER_GET_PITCH:
            stl_le_phys(&s->dma_as, value + 12,
                        bcm2835_fb_get_pitch(&fbconfig));
            resplen = 4;
            break;
        case RPI_FWREQ_FRAMEBUFFER_TEST_VIRTUAL_OFFSET:
            resplen = 8;
            break;
        case RPI_FWREQ_FRAMEBUFFER_SET_VIRTUAL_OFFSET:
            fbconfig.xoffset = ldl_le_phys(&s->dma_as, value + 12);
            fbconfig.yoffset = ldl_le_phys(&s->dma_as, value + 16);
            bcm2835_fb_validate_config(&fbconfig);
            fbconfig_updated = true;
            /* fall through */
        case RPI_FWREQ_FRAMEBUFFER_GET_VIRTUAL_OFFSET:
            stl_le_phys(&s->dma_as, value + 12, fbconfig.xoffset);
            stl_le_phys(&s->dma_as, value + 16, fbconfig.yoffset);
            resplen = 8;
            break;
        case RPI_FWREQ_FRAMEBUFFER_GET_OVERSCAN:
        case RPI_FWREQ_FRAMEBUFFER_TEST_OVERSCAN:
        case RPI_FWREQ_FRAMEBUFFER_SET_OVERSCAN:
            stl_le_phys(&s->dma_as, value + 12, 0);
            stl_le_phys(&s->dma_as, value + 16, 0);
            stl_le_phys(&s->dma_as, value + 20, 0);
            stl_le_phys(&s->dma_as, value + 24, 0);
            resplen = 16;
            break;
        case RPI_FWREQ_FRAMEBUFFER_SET_PALETTE:
            offset = ldl_le_phys(&s->dma_as, value + 12);
            length = ldl_le_phys(&s->dma_as, value + 16);
            n = 0;
            while (n < length - offset) {
                color = ldl_le_phys(&s->dma_as, value + 20 + (n << 2));
                stl_le_phys(&s->dma_as,
                            s->fbdev->vcram_base + ((offset + n) << 2), color);
                n++;
            }
            stl_le_phys(&s->dma_as, value + 12, 0);
            resplen = 4;
            break;
        case RPI_FWREQ_FRAMEBUFFER_GET_NUM_DISPLAYS:
            stl_le_phys(&s->dma_as, value + 12, 1);
            resplen = 4;
            break;

        case RPI_FWREQ_GET_DMA_CHANNELS:
            /* channels 2-5 */
            stl_le_phys(&s->dma_as, value + 12, 0x003C);
            resplen = 4;
            break;

        case RPI_FWREQ_GET_COMMAND_LINE:
            /*
             * We follow the firmware behaviour: no NUL terminator is
             * written to the buffer, and if the buffer is too short
             * we report the required length in the response header
             * and copy nothing to the buffer.
             */
            resplen = strlen(s->command_line);
            if (bufsize >= resplen)
                address_space_write(&s->dma_as, value + 12,
                                    MEMTXATTRS_UNSPECIFIED, s->command_line,
                                    resplen);
            break;

        default:
            qemu_log_mask(LOG_UNIMP,
                          "bcm2835_property: unhandled tag 0x%08x\n", tag);
            break;
        }

        trace_bcm2835_mbox_property(tag, bufsize, resplen);
        if (tag == 0) {
            break;
        }

        stl_le_phys(&s->dma_as, value + 8, (1 << 31) | resplen);
        value += bufsize + 12;
    }

    /* Reconfigure framebuffer if required */
    if (fbconfig_updated) {
        bcm2835_fb_reconfigure(s->fbdev, &fbconfig);
    }

    /* Buffer response code */
    stl_le_phys(&s->dma_as, s->addr + 4, (1 << 31));
}

static uint64_t bcm2835_property_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    BCM2835PropertyState *s = opaque;
    uint32_t res = 0;

    switch (offset) {
    case MBOX_AS_DATA:
        res = MBOX_CHAN_PROPERTY | s->addr;
        s->pending = false;
        qemu_set_irq(s->mbox_irq, 0);
        break;

    case MBOX_AS_PENDING:
        res = s->pending;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset %"HWADDR_PRIx"\n",
                      __func__, offset);
        return 0;
    }

    return res;
}

static void bcm2835_property_write(void *opaque, hwaddr offset,
                                   uint64_t value, unsigned size)
{
    BCM2835PropertyState *s = opaque;

    switch (offset) {
    case MBOX_AS_DATA:
        /* bcm2835_mbox should check our pending status before pushing */
        assert(!s->pending);
        s->pending = true;
        bcm2835_property_mbox_push(s, value);
        qemu_set_irq(s->mbox_irq, 1);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset %"HWADDR_PRIx"\n",
                      __func__, offset);
        return;
    }
}

static const MemoryRegionOps bcm2835_property_ops = {
    .read = bcm2835_property_read,
    .write = bcm2835_property_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static const VMStateDescription vmstate_bcm2835_property = {
    .name = TYPE_BCM2835_PROPERTY,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_MACADDR(macaddr, BCM2835PropertyState),
        VMSTATE_UINT32(addr, BCM2835PropertyState),
        VMSTATE_BOOL(pending, BCM2835PropertyState),
        VMSTATE_END_OF_LIST()
    }
};

static void bcm2835_property_init(Object *obj)
{
    BCM2835PropertyState *s = BCM2835_PROPERTY(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &bcm2835_property_ops, s,
                          TYPE_BCM2835_PROPERTY, 0x10);

    /*
     * bcm2835_property_ops call into bcm2835_mbox, which in-turn reads from
     * iomem. As such, mark iomem as re-entracy safe.
     */
    s->iomem.disable_reentrancy_guard = true;

    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(s), &s->mbox_irq);
}

static void bcm2835_property_reset(DeviceState *dev)
{
    BCM2835PropertyState *s = BCM2835_PROPERTY(dev);

    s->pending = false;
}

static void bcm2835_property_realize(DeviceState *dev, Error **errp)
{
    BCM2835PropertyState *s = BCM2835_PROPERTY(dev);
    Object *obj;

    obj = object_property_get_link(OBJECT(dev), "fb", &error_abort);
    s->fbdev = BCM2835_FB(obj);

    obj = object_property_get_link(OBJECT(dev), "dma-mr", &error_abort);
    s->dma_mr = MEMORY_REGION(obj);
    address_space_init(&s->dma_as, s->dma_mr, TYPE_BCM2835_PROPERTY "-memory");

    /* TODO: connect to MAC address of USB NIC device, once we emulate it */
    qemu_macaddr_default_if_unset(&s->macaddr);

    bcm2835_property_reset(dev);
}

static Property bcm2835_property_props[] = {
    DEFINE_PROP_UINT32("board-rev", BCM2835PropertyState, board_rev, 0),
    DEFINE_PROP_STRING("command-line", BCM2835PropertyState, command_line),
    DEFINE_PROP_END_OF_LIST()
};

static void bcm2835_property_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, bcm2835_property_props);
    dc->realize = bcm2835_property_realize;
    dc->vmsd = &vmstate_bcm2835_property;
}

static const TypeInfo bcm2835_property_info = {
    .name          = TYPE_BCM2835_PROPERTY,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2835PropertyState),
    .class_init    = bcm2835_property_class_init,
    .instance_init = bcm2835_property_init,
};

static void bcm2835_property_register_types(void)
{
    type_register_static(&bcm2835_property_info);
}

type_init(bcm2835_property_register_types)
