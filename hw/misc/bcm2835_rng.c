/*
 * BCM2835 Random Number Generator emulation
 *
 * Copyright (C) 2017 Marcin Chojnacki <marcinch7@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/guest-random.h"
#include "qemu/module.h"
#include "hw/misc/bcm2835_rng.h"
#include "migration/vmstate.h"

static uint32_t get_random_bytes(void)
{
    uint32_t res;

    /*
     * On failure we don't want to return the guest a non-random
     * value in case they're really using it for cryptographic
     * purposes, so the best we can do is die here.
     * This shouldn't happen unless something's broken.
     * In theory we could implement this device's full FIFO
     * and interrupt semantics and then just stop filling the
     * FIFO. That's a lot of work, though, so we assume any
     * errors are systematic problems and trust that if we didn't
     * fail as the guest inited then we won't fail later on
     * mid-run.
     */
    qemu_guest_getrandom_nofail(&res, sizeof(res));
    return res;
}

static uint64_t bcm2835_rng_read(void *opaque, hwaddr offset,
                                 unsigned size)
{
    BCM2835RngState *s = (BCM2835RngState *)opaque;
    uint32_t res = 0;

    assert(size == 4);

    switch (offset) {
    case 0x0:    /* rng_ctrl */
        res = s->rng_ctrl;
        break;
    case 0x4:    /* rng_status */
        res = s->rng_status | (1 << 24);
        break;
    case 0x8:    /* rng_data */
        res = get_random_bytes();
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "bcm2835_rng_read: Bad offset %x\n",
                      (int)offset);
        res = 0;
        break;
    }

    return res;
}

static void bcm2835_rng_write(void *opaque, hwaddr offset,
                              uint64_t value, unsigned size)
{
    BCM2835RngState *s = (BCM2835RngState *)opaque;

    assert(size == 4);

    switch (offset) {
    case 0x0:    /* rng_ctrl */
        s->rng_ctrl = value;
        break;
    case 0x4:    /* rng_status */
        /* we shouldn't let the guest write to bits [31..20] */
        s->rng_status &= ~0xFFFFF;        /* clear 20 lower bits */
        s->rng_status |= value & 0xFFFFF; /* set them to new value */
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "bcm2835_rng_write: Bad offset %x\n",
                      (int)offset);
        break;
    }
}

static const MemoryRegionOps bcm2835_rng_ops = {
    .read = bcm2835_rng_read,
    .write = bcm2835_rng_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_bcm2835_rng = {
    .name = TYPE_BCM2835_RNG,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(rng_ctrl, BCM2835RngState),
        VMSTATE_UINT32(rng_status, BCM2835RngState),
        VMSTATE_END_OF_LIST()
    }
};

static void bcm2835_rng_init(Object *obj)
{
    BCM2835RngState *s = BCM2835_RNG(obj);

    memory_region_init_io(&s->iomem, obj, &bcm2835_rng_ops, s,
                          TYPE_BCM2835_RNG, 0x10);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
}

static void bcm2835_rng_reset(DeviceState *dev)
{
    BCM2835RngState *s = BCM2835_RNG(dev);

    s->rng_ctrl = 0;
    s->rng_status = 0;
}

static void bcm2835_rng_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = bcm2835_rng_reset;
    dc->vmsd = &vmstate_bcm2835_rng;
}

static TypeInfo bcm2835_rng_info = {
    .name          = TYPE_BCM2835_RNG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2835RngState),
    .class_init    = bcm2835_rng_class_init,
    .instance_init = bcm2835_rng_init,
};

static void bcm2835_rng_register_types(void)
{
    type_register_static(&bcm2835_rng_info);
}

type_init(bcm2835_rng_register_types)
