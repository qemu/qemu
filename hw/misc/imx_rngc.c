/*
 * Freescale i.MX RNGC emulation
 *
 * Copyright (C) 2020 Martin Kaiser <martin@kaiser.cx>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * This driver provides the minimum functionality to initialize and seed
 * an rngc and to read random numbers. The rngb that is found in imx25
 * chipsets is also supported.
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qemu/guest-random.h"
#include "hw/irq.h"
#include "hw/misc/imx_rngc.h"
#include "migration/vmstate.h"

#define RNGC_NAME "i.MX RNGC"

#define RNGC_VER_ID  0x00
#define RNGC_COMMAND 0x04
#define RNGC_CONTROL 0x08
#define RNGC_STATUS  0x0C
#define RNGC_FIFO    0x14

/* These version info are reported by the rngb in an imx258 chip. */
#define RNG_TYPE_RNGB 0x1
#define V_MAJ 0x2
#define V_MIN 0x40

#define RNGC_CMD_BIT_SW_RST    0x40
#define RNGC_CMD_BIT_CLR_ERR   0x20
#define RNGC_CMD_BIT_CLR_INT   0x10
#define RNGC_CMD_BIT_SEED      0x02
#define RNGC_CMD_BIT_SELF_TEST 0x01

#define RNGC_CTRL_BIT_MASK_ERR  0x40
#define RNGC_CTRL_BIT_MASK_DONE 0x20
#define RNGC_CTRL_BIT_AUTO_SEED 0x10

/* the current status for self-test and seed operations */
#define OP_IDLE 0
#define OP_RUN  1
#define OP_DONE 2

static uint64_t imx_rngc_read(void *opaque, hwaddr offset, unsigned size)
{
    IMXRNGCState *s = IMX_RNGC(opaque);
    uint64_t val = 0;

    switch (offset) {
    case RNGC_VER_ID:
        val |= RNG_TYPE_RNGB << 28 | V_MAJ << 8 | V_MIN;
        break;

    case RNGC_COMMAND:
        if (s->op_seed == OP_RUN) {
            val |= RNGC_CMD_BIT_SEED;
        }
        if (s->op_self_test == OP_RUN) {
            val |= RNGC_CMD_BIT_SELF_TEST;
        }
        break;

    case RNGC_CONTROL:
        /*
         * The CTL_ACC and VERIF_MODE bits are not supported yet.
         * They read as 0.
         */
        val |= s->mask;
        if (s->auto_seed) {
            val |= RNGC_CTRL_BIT_AUTO_SEED;
        }
        /*
         * We don't have an internal fifo like the real hardware.
         * There's no need for strategy to handle fifo underflows.
         * We return the FIFO_UFLOW_RESPONSE bits as 0.
         */
        break;

    case RNGC_STATUS:
        /*
         * We never report any statistics test or self-test errors or any
         * other errors. STAT_TEST_PF, ST_PF and ERROR are always 0.
         */

        /*
         * We don't have an internal fifo, see above. Therefore, we
         * report back the default fifo size (5 32-bit words) and
         * indicate that our fifo is always full.
         */
        val |= 5 << 12 | 5 << 8;

        /* We always have a new seed available. */
        val |= 1 << 6;

        if (s->op_seed == OP_DONE) {
            val |= 1 << 5;
        }
        if (s->op_self_test == OP_DONE) {
            val |= 1 << 4;
        }
        if (s->op_seed == OP_RUN || s->op_self_test == OP_RUN) {
            /*
             * We're busy if self-test is running or if we're
             * seeding the prng.
             */
            val |= 1 << 1;
        } else {
            /*
             * We're ready to provide secure random numbers whenever
             * we're not busy.
             */
            val |= 1;
        }
        break;

    case RNGC_FIFO:
        qemu_guest_getrandom_nofail(&val, sizeof(val));
        break;
    }

    return val;
}

static void imx_rngc_do_reset(IMXRNGCState *s)
{
    s->op_self_test = OP_IDLE;
    s->op_seed = OP_IDLE;
    s->mask = 0;
    s->auto_seed = false;
}

static void imx_rngc_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned size)
{
    IMXRNGCState *s = IMX_RNGC(opaque);

    switch (offset) {
    case RNGC_COMMAND:
        if (value & RNGC_CMD_BIT_SW_RST) {
            imx_rngc_do_reset(s);
        }

        /*
         * For now, both CLR_ERR and CLR_INT clear the interrupt. We
         * don't report any errors yet.
         */
        if (value & (RNGC_CMD_BIT_CLR_ERR | RNGC_CMD_BIT_CLR_INT)) {
            qemu_irq_lower(s->irq);
        }

        if (value & RNGC_CMD_BIT_SEED) {
            s->op_seed = OP_RUN;
            qemu_bh_schedule(s->seed_bh);
        }

        if (value & RNGC_CMD_BIT_SELF_TEST) {
            s->op_self_test = OP_RUN;
            qemu_bh_schedule(s->self_test_bh);
        }
        break;

    case RNGC_CONTROL:
        /*
         * The CTL_ACC and VERIF_MODE bits are not supported yet.
         * We ignore them if they're set by the caller.
         */

        if (value & RNGC_CTRL_BIT_MASK_ERR) {
            s->mask |= RNGC_CTRL_BIT_MASK_ERR;
        } else {
            s->mask &= ~RNGC_CTRL_BIT_MASK_ERR;
        }

        if (value & RNGC_CTRL_BIT_MASK_DONE) {
            s->mask |= RNGC_CTRL_BIT_MASK_DONE;
        } else {
            s->mask &= ~RNGC_CTRL_BIT_MASK_DONE;
        }

        if (value & RNGC_CTRL_BIT_AUTO_SEED) {
            s->auto_seed = true;
        } else {
            s->auto_seed = false;
        }
        break;
    }
}

static const MemoryRegionOps imx_rngc_ops = {
    .read  = imx_rngc_read,
    .write = imx_rngc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void imx_rngc_self_test(void *opaque)
{
    IMXRNGCState *s = IMX_RNGC(opaque);

    s->op_self_test = OP_DONE;
    if (!(s->mask & RNGC_CTRL_BIT_MASK_DONE)) {
        qemu_irq_raise(s->irq);
    }
}

static void imx_rngc_seed(void *opaque)
{
    IMXRNGCState *s = IMX_RNGC(opaque);

    s->op_seed = OP_DONE;
    if (!(s->mask & RNGC_CTRL_BIT_MASK_DONE)) {
        qemu_irq_raise(s->irq);
    }
}

static void imx_rngc_realize(DeviceState *dev, Error **errp)
{
    IMXRNGCState *s = IMX_RNGC(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &imx_rngc_ops, s,
                          TYPE_IMX_RNGC, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);

    sysbus_init_irq(sbd, &s->irq);
    s->self_test_bh = qemu_bh_new(imx_rngc_self_test, s);
    s->seed_bh = qemu_bh_new(imx_rngc_seed, s);
}

static void imx_rngc_reset(DeviceState *dev)
{
    IMXRNGCState *s = IMX_RNGC(dev);

    imx_rngc_do_reset(s);
}

static const VMStateDescription vmstate_imx_rngc = {
    .name = RNGC_NAME,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(op_self_test, IMXRNGCState),
        VMSTATE_UINT8(op_seed, IMXRNGCState),
        VMSTATE_UINT8(mask, IMXRNGCState),
        VMSTATE_BOOL(auto_seed, IMXRNGCState),
        VMSTATE_END_OF_LIST()
    }
};

static void imx_rngc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = imx_rngc_realize;
    dc->reset = imx_rngc_reset;
    dc->desc = RNGC_NAME,
    dc->vmsd = &vmstate_imx_rngc;
}

static const TypeInfo imx_rngc_info = {
    .name          = TYPE_IMX_RNGC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IMXRNGCState),
    .class_init    = imx_rngc_class_init,
};

static void imx_rngc_register_types(void)
{
    type_register_static(&imx_rngc_info);
}

type_init(imx_rngc_register_types)
