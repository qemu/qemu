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
#include "sysemu/dma.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "trace.h"

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
        case 0x00000000: /* End tag */
            break;
        case 0x00000001: /* Get firmware revision */
            stl_le_phys(&s->dma_as, value + 12, 346337);
            resplen = 4;
            break;
        case 0x00010001: /* Get board model */
            qemu_log_mask(LOG_UNIMP,
                          "bcm2835_property: 0x%08x get board model NYI\n",
                          tag);
            resplen = 4;
            break;
        case 0x00010002: /* Get board revision */
            stl_le_phys(&s->dma_as, value + 12, s->board_rev);
            resplen = 4;
            break;
        case 0x00010003: /* Get board MAC address */
            resplen = sizeof(s->macaddr.a);
            dma_memory_write(&s->dma_as, value + 12, s->macaddr.a, resplen,
                             MEMTXATTRS_UNSPECIFIED);
            break;
        case 0x00010004: /* Get board serial */
            qemu_log_mask(LOG_UNIMP,
                          "bcm2835_property: 0x%08x get board serial NYI\n",
                          tag);
            resplen = 8;
            break;
        case 0x00010005: /* Get ARM memory */
            /* base */
            stl_le_phys(&s->dma_as, value + 12, 0);
            /* size */
            stl_le_phys(&s->dma_as, value + 16, s->fbdev->vcram_base);
            resplen = 8;
            break;
        case 0x00010006: /* Get VC memory */
            /* base */
            stl_le_phys(&s->dma_as, value + 12, s->fbdev->vcram_base);
            /* size */
            stl_le_phys(&s->dma_as, value + 16, s->fbdev->vcram_size);
            resplen = 8;
            break;
        case 0x00028001: /* Set power state */
            /* Assume that whatever device they asked for exists,
             * and we'll just claim we set it to the desired state
             */
            tmp = ldl_le_phys(&s->dma_as, value + 16);
            stl_le_phys(&s->dma_as, value + 16, (tmp & 1));
            resplen = 8;
            break;

        /* Clocks */

        case 0x00030001: /* Get clock state */
            stl_le_phys(&s->dma_as, value + 16, 0x1);
            resplen = 8;
            break;

        case 0x00038001: /* Set clock state */
            qemu_log_mask(LOG_UNIMP,
                          "bcm2835_property: 0x%08x set clock state NYI\n",
                          tag);
            resplen = 8;
            break;

        case 0x00030002: /* Get clock rate */
        case 0x00030004: /* Get max clock rate */
        case 0x00030007: /* Get min clock rate */
            switch (ldl_le_phys(&s->dma_as, value + 12)) {
            case 1: /* EMMC */
                stl_le_phys(&s->dma_as, value + 16, 50000000);
                break;
            case 2: /* UART */
                stl_le_phys(&s->dma_as, value + 16, 3000000);
                break;
            default:
                stl_le_phys(&s->dma_as, value + 16, 700000000);
                break;
            }
            resplen = 8;
            break;

        case 0x00038002: /* Set clock rate */
        case 0x00038004: /* Set max clock rate */
        case 0x00038007: /* Set min clock rate */
            qemu_log_mask(LOG_UNIMP,
                          "bcm2835_property: 0x%08x set clock rate NYI\n",
                          tag);
            resplen = 8;
            break;

        /* Temperature */

        case 0x00030006: /* Get temperature */
            stl_le_phys(&s->dma_as, value + 16, 25000);
            resplen = 8;
            break;

        case 0x0003000A: /* Get max temperature */
            stl_le_phys(&s->dma_as, value + 16, 99000);
            resplen = 8;
            break;

        /* Frame buffer */

        case 0x00040001: /* Allocate buffer */
            stl_le_phys(&s->dma_as, value + 12, fbconfig.base);
            stl_le_phys(&s->dma_as, value + 16,
                        bcm2835_fb_get_size(&fbconfig));
            resplen = 8;
            break;
        case 0x00048001: /* Release buffer */
            resplen = 0;
            break;
        case 0x00040002: /* Blank screen */
            resplen = 4;
            break;
        case 0x00044003: /* Test physical display width/height */
        case 0x00044004: /* Test virtual display width/height */
            resplen = 8;
            break;
        case 0x00048003: /* Set physical display width/height */
            fbconfig.xres = ldl_le_phys(&s->dma_as, value + 12);
            fbconfig.yres = ldl_le_phys(&s->dma_as, value + 16);
            bcm2835_fb_validate_config(&fbconfig);
            fbconfig_updated = true;
            /* fall through */
        case 0x00040003: /* Get physical display width/height */
            stl_le_phys(&s->dma_as, value + 12, fbconfig.xres);
            stl_le_phys(&s->dma_as, value + 16, fbconfig.yres);
            resplen = 8;
            break;
        case 0x00048004: /* Set virtual display width/height */
            fbconfig.xres_virtual = ldl_le_phys(&s->dma_as, value + 12);
            fbconfig.yres_virtual = ldl_le_phys(&s->dma_as, value + 16);
            bcm2835_fb_validate_config(&fbconfig);
            fbconfig_updated = true;
            /* fall through */
        case 0x00040004: /* Get virtual display width/height */
            stl_le_phys(&s->dma_as, value + 12, fbconfig.xres_virtual);
            stl_le_phys(&s->dma_as, value + 16, fbconfig.yres_virtual);
            resplen = 8;
            break;
        case 0x00044005: /* Test depth */
            resplen = 4;
            break;
        case 0x00048005: /* Set depth */
            fbconfig.bpp = ldl_le_phys(&s->dma_as, value + 12);
            bcm2835_fb_validate_config(&fbconfig);
            fbconfig_updated = true;
            /* fall through */
        case 0x00040005: /* Get depth */
            stl_le_phys(&s->dma_as, value + 12, fbconfig.bpp);
            resplen = 4;
            break;
        case 0x00044006: /* Test pixel order */
            resplen = 4;
            break;
        case 0x00048006: /* Set pixel order */
            fbconfig.pixo = ldl_le_phys(&s->dma_as, value + 12);
            bcm2835_fb_validate_config(&fbconfig);
            fbconfig_updated = true;
            /* fall through */
        case 0x00040006: /* Get pixel order */
            stl_le_phys(&s->dma_as, value + 12, fbconfig.pixo);
            resplen = 4;
            break;
        case 0x00044007: /* Test pixel alpha */
            resplen = 4;
            break;
        case 0x00048007: /* Set alpha */
            fbconfig.alpha = ldl_le_phys(&s->dma_as, value + 12);
            bcm2835_fb_validate_config(&fbconfig);
            fbconfig_updated = true;
            /* fall through */
        case 0x00040007: /* Get alpha */
            stl_le_phys(&s->dma_as, value + 12, fbconfig.alpha);
            resplen = 4;
            break;
        case 0x00040008: /* Get pitch */
            stl_le_phys(&s->dma_as, value + 12,
                        bcm2835_fb_get_pitch(&fbconfig));
            resplen = 4;
            break;
        case 0x00044009: /* Test virtual offset */
            resplen = 8;
            break;
        case 0x00048009: /* Set virtual offset */
            fbconfig.xoffset = ldl_le_phys(&s->dma_as, value + 12);
            fbconfig.yoffset = ldl_le_phys(&s->dma_as, value + 16);
            bcm2835_fb_validate_config(&fbconfig);
            fbconfig_updated = true;
            /* fall through */
        case 0x00040009: /* Get virtual offset */
            stl_le_phys(&s->dma_as, value + 12, fbconfig.xoffset);
            stl_le_phys(&s->dma_as, value + 16, fbconfig.yoffset);
            resplen = 8;
            break;
        case 0x0004000a: /* Get/Test/Set overscan */
        case 0x0004400a:
        case 0x0004800a:
            stl_le_phys(&s->dma_as, value + 12, 0);
            stl_le_phys(&s->dma_as, value + 16, 0);
            stl_le_phys(&s->dma_as, value + 20, 0);
            stl_le_phys(&s->dma_as, value + 24, 0);
            resplen = 16;
            break;
        case 0x0004800b: /* Set palette */
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
        case 0x00040013: /* Get number of displays */
            stl_le_phys(&s->dma_as, value + 12, 1);
            resplen = 4;
            break;

        case 0x00060001: /* Get DMA channels */
            /* channels 2-5 */
            stl_le_phys(&s->dma_as, value + 12, 0x003C);
            resplen = 4;
            break;

        case 0x00050001: /* Get command line */
            resplen = 0;
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
