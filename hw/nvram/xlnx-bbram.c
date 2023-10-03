/*
 * QEMU model of the Xilinx BBRAM Battery Backed RAM
 *
 * Copyright (c) 2014-2021 Xilinx Inc.
 * Copyright (c) 2023 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "hw/nvram/xlnx-bbram.h"

#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "sysemu/blockdev.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/nvram/xlnx-efuse.h"

#ifndef XLNX_BBRAM_ERR_DEBUG
#define XLNX_BBRAM_ERR_DEBUG 0
#endif

REG32(BBRAM_STATUS, 0x0)
    FIELD(BBRAM_STATUS, AES_CRC_PASS, 9, 1)
    FIELD(BBRAM_STATUS, AES_CRC_DONE, 8, 1)
    FIELD(BBRAM_STATUS, BBRAM_ZEROIZED, 4, 1)
    FIELD(BBRAM_STATUS, PGM_MODE, 0, 1)
REG32(BBRAM_CTRL, 0x4)
    FIELD(BBRAM_CTRL, ZEROIZE, 0, 1)
REG32(PGM_MODE, 0x8)
REG32(BBRAM_AES_CRC, 0xc)
REG32(BBRAM_0, 0x10)
REG32(BBRAM_1, 0x14)
REG32(BBRAM_2, 0x18)
REG32(BBRAM_3, 0x1c)
REG32(BBRAM_4, 0x20)
REG32(BBRAM_5, 0x24)
REG32(BBRAM_6, 0x28)
REG32(BBRAM_7, 0x2c)
REG32(BBRAM_8, 0x30)
REG32(BBRAM_SLVERR, 0x34)
    FIELD(BBRAM_SLVERR, ENABLE, 0, 1)
REG32(BBRAM_ISR, 0x38)
    FIELD(BBRAM_ISR, APB_SLVERR, 0, 1)
REG32(BBRAM_IMR, 0x3c)
    FIELD(BBRAM_IMR, APB_SLVERR, 0, 1)
REG32(BBRAM_IER, 0x40)
    FIELD(BBRAM_IER, APB_SLVERR, 0, 1)
REG32(BBRAM_IDR, 0x44)
    FIELD(BBRAM_IDR, APB_SLVERR, 0, 1)
REG32(BBRAM_MSW_LOCK, 0x4c)
    FIELD(BBRAM_MSW_LOCK, VAL, 0, 1)

#define R_MAX (R_BBRAM_MSW_LOCK + 1)

#define RAM_MAX (A_BBRAM_8 + 4 - A_BBRAM_0)

#define BBRAM_PGM_MAGIC 0x757bdf0d

QEMU_BUILD_BUG_ON(R_MAX != ARRAY_SIZE(((XlnxBBRam *)0)->regs));

static bool bbram_msw_locked(XlnxBBRam *s)
{
    return ARRAY_FIELD_EX32(s->regs, BBRAM_MSW_LOCK, VAL) != 0;
}

static bool bbram_pgm_enabled(XlnxBBRam *s)
{
    return ARRAY_FIELD_EX32(s->regs, BBRAM_STATUS, PGM_MODE) != 0;
}

static void bbram_bdrv_error(XlnxBBRam *s, int rc, gchar *detail)
{
    Error *errp = NULL;

    error_setg_errno(&errp, -rc, "%s: BBRAM backstore %s failed.",
                     blk_name(s->blk), detail);
    error_report("%s", error_get_pretty(errp));
    error_free(errp);

    g_free(detail);
}

static void bbram_bdrv_read(XlnxBBRam *s, Error **errp)
{
    uint32_t *ram = &s->regs[R_BBRAM_0];
    int nr = RAM_MAX;

    if (!s->blk) {
        return;
    }

    s->blk_ro = !blk_supports_write_perm(s->blk);
    if (!s->blk_ro) {
        int rc;

        rc = blk_set_perm(s->blk,
                          (BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE),
                          BLK_PERM_ALL, NULL);
        if (rc) {
            s->blk_ro = true;
        }
    }
    if (s->blk_ro) {
        warn_report("%s: Skip saving updates to read-only BBRAM backstore.",
                    blk_name(s->blk));
    }

    if (blk_pread(s->blk, 0, nr, ram, 0) < 0) {
        error_setg(errp,
                   "%s: Failed to read %u bytes from BBRAM backstore.",
                   blk_name(s->blk), nr);
        return;
    }

    /* Convert from little-endian backstore for each 32-bit word */
    nr /= 4;
    while (nr--) {
        ram[nr] = le32_to_cpu(ram[nr]);
    }
}

static void bbram_bdrv_sync(XlnxBBRam *s, uint64_t hwaddr)
{
    uint32_t le32;
    unsigned offset;
    int rc;

    assert(A_BBRAM_0 <= hwaddr && hwaddr <= A_BBRAM_8);

    /* Backstore is always in little-endian */
    le32 = cpu_to_le32(s->regs[hwaddr / 4]);

    /* Update zeroized flag */
    if (le32 && (hwaddr != A_BBRAM_8 || s->bbram8_wo)) {
        ARRAY_FIELD_DP32(s->regs, BBRAM_STATUS, BBRAM_ZEROIZED, 0);
    }

    if (!s->blk || s->blk_ro) {
        return;
    }

    offset = hwaddr - A_BBRAM_0;
    rc = blk_pwrite(s->blk, offset, 4, &le32, 0);
    if (rc < 0) {
        bbram_bdrv_error(s, rc, g_strdup_printf("write to offset %u", offset));
    }
}

static void bbram_bdrv_zero(XlnxBBRam *s)
{
    int rc;

    ARRAY_FIELD_DP32(s->regs, BBRAM_STATUS, BBRAM_ZEROIZED, 1);

    if (!s->blk || s->blk_ro) {
        return;
    }

    rc = blk_make_zero(s->blk, 0);
    if (rc < 0) {
        bbram_bdrv_error(s, rc, g_strdup("zeroizing"));
    }

    /* Restore bbram8 if it is non-zero */
    if (s->regs[R_BBRAM_8]) {
        bbram_bdrv_sync(s, A_BBRAM_8);
    }
}

static void bbram_zeroize(XlnxBBRam *s)
{
    int nr = RAM_MAX - (s->bbram8_wo ? 0 : 4); /* only wo bbram8 is cleared */

    memset(&s->regs[R_BBRAM_0], 0, nr);
    bbram_bdrv_zero(s);
}

static void bbram_update_irq(XlnxBBRam *s)
{
    bool pending = s->regs[R_BBRAM_ISR] & ~s->regs[R_BBRAM_IMR];

    qemu_set_irq(s->irq_bbram, pending);
}

static void bbram_ctrl_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxBBRam *s = XLNX_BBRAM(reg->opaque);
    uint32_t val = val64;

    if (val & R_BBRAM_CTRL_ZEROIZE_MASK) {
        bbram_zeroize(s);
        /* The bit is self clearing */
        s->regs[R_BBRAM_CTRL] &= ~R_BBRAM_CTRL_ZEROIZE_MASK;
    }
}

static void bbram_pgm_mode_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxBBRam *s = XLNX_BBRAM(reg->opaque);
    uint32_t val = val64;

    if (val == BBRAM_PGM_MAGIC) {
        bbram_zeroize(s);

        /* The status bit is cleared only by POR */
        ARRAY_FIELD_DP32(s->regs, BBRAM_STATUS, PGM_MODE, 1);
    }
}

static void bbram_aes_crc_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxBBRam *s = XLNX_BBRAM(reg->opaque);
    uint32_t calc_crc;

    if (!bbram_pgm_enabled(s)) {
        /* We are not in programming mode, don't do anything */
        return;
    }

    /* Perform the AES integrity check */
    s->regs[R_BBRAM_STATUS] |= R_BBRAM_STATUS_AES_CRC_DONE_MASK;

    /*
     * Set check status.
     *
     * ZynqMP BBRAM check has a zero-u32 prepended; see:
     *  https://github.com/Xilinx/embeddedsw/blob/release-2019.2/lib/sw_services/xilskey/src/xilskey_bbramps_zynqmp.c#L311
     */
    calc_crc = xlnx_efuse_calc_crc(&s->regs[R_BBRAM_0],
                                   (R_BBRAM_8 - R_BBRAM_0), s->crc_zpads);

    ARRAY_FIELD_DP32(s->regs, BBRAM_STATUS, AES_CRC_PASS,
                     (s->regs[R_BBRAM_AES_CRC] == calc_crc));
}

static uint64_t bbram_key_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxBBRam *s = XLNX_BBRAM(reg->opaque);
    uint32_t original_data = *(uint32_t *) reg->data;

    if (bbram_pgm_enabled(s)) {
        return val64;
    } else {
        /* We are not in programming mode, don't do anything */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Not in programming mode, dropping the write\n");
        return original_data;
    }
}

static void bbram_key_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxBBRam *s = XLNX_BBRAM(reg->opaque);

    bbram_bdrv_sync(s, reg->access->addr);
}

static uint64_t bbram_wo_postr(RegisterInfo *reg, uint64_t val)
{
    return 0;
}

static uint64_t bbram_r8_postr(RegisterInfo *reg, uint64_t val)
{
    XlnxBBRam *s = XLNX_BBRAM(reg->opaque);

    return s->bbram8_wo ? bbram_wo_postr(reg, val) : val;
}

static bool bbram_r8_readonly(XlnxBBRam *s)
{
    return !bbram_pgm_enabled(s) || bbram_msw_locked(s);
}

static uint64_t bbram_r8_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxBBRam *s = XLNX_BBRAM(reg->opaque);

    if (bbram_r8_readonly(s)) {
        val64 = *(uint32_t *)reg->data;
    }

    return val64;
}

static void bbram_r8_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxBBRam *s = XLNX_BBRAM(reg->opaque);

    if (!bbram_r8_readonly(s)) {
        bbram_bdrv_sync(s, A_BBRAM_8);
    }
}

static uint64_t bbram_msw_lock_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxBBRam *s = XLNX_BBRAM(reg->opaque);

    /* Never lock if bbram8 is wo; and, only POR can clear the lock */
    if (s->bbram8_wo) {
        val64 = 0;
    } else {
        val64 |= s->regs[R_BBRAM_MSW_LOCK];
    }

    return val64;
}

static void bbram_isr_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxBBRam *s = XLNX_BBRAM(reg->opaque);

    bbram_update_irq(s);
}

static uint64_t bbram_ier_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxBBRam *s = XLNX_BBRAM(reg->opaque);
    uint32_t val = val64;

    s->regs[R_BBRAM_IMR] &= ~val;
    bbram_update_irq(s);
    return 0;
}

static uint64_t bbram_idr_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxBBRam *s = XLNX_BBRAM(reg->opaque);
    uint32_t val = val64;

    s->regs[R_BBRAM_IMR] |= val;
    bbram_update_irq(s);
    return 0;
}

static RegisterAccessInfo bbram_ctrl_regs_info[] = {
    {   .name = "BBRAM_STATUS",  .addr = A_BBRAM_STATUS,
        .rsvd = 0xee,
        .ro = 0x3ff,
    },{ .name = "BBRAM_CTRL",  .addr = A_BBRAM_CTRL,
        .post_write = bbram_ctrl_postw,
    },{ .name = "PGM_MODE",  .addr = A_PGM_MODE,
        .post_write = bbram_pgm_mode_postw,
    },{ .name = "BBRAM_AES_CRC",  .addr = A_BBRAM_AES_CRC,
        .post_write = bbram_aes_crc_postw,
        .post_read = bbram_wo_postr,
    },{ .name = "BBRAM_0",  .addr = A_BBRAM_0,
        .pre_write = bbram_key_prew,
        .post_write = bbram_key_postw,
        .post_read = bbram_wo_postr,
    },{ .name = "BBRAM_1",  .addr = A_BBRAM_1,
        .pre_write = bbram_key_prew,
        .post_write = bbram_key_postw,
        .post_read = bbram_wo_postr,
    },{ .name = "BBRAM_2",  .addr = A_BBRAM_2,
        .pre_write = bbram_key_prew,
        .post_write = bbram_key_postw,
        .post_read = bbram_wo_postr,
    },{ .name = "BBRAM_3",  .addr = A_BBRAM_3,
        .pre_write = bbram_key_prew,
        .post_write = bbram_key_postw,
        .post_read = bbram_wo_postr,
    },{ .name = "BBRAM_4",  .addr = A_BBRAM_4,
        .pre_write = bbram_key_prew,
        .post_write = bbram_key_postw,
        .post_read = bbram_wo_postr,
    },{ .name = "BBRAM_5",  .addr = A_BBRAM_5,
        .pre_write = bbram_key_prew,
        .post_write = bbram_key_postw,
        .post_read = bbram_wo_postr,
    },{ .name = "BBRAM_6",  .addr = A_BBRAM_6,
        .pre_write = bbram_key_prew,
        .post_write = bbram_key_postw,
        .post_read = bbram_wo_postr,
    },{ .name = "BBRAM_7",  .addr = A_BBRAM_7,
        .pre_write = bbram_key_prew,
        .post_write = bbram_key_postw,
        .post_read = bbram_wo_postr,
    },{ .name = "BBRAM_8",  .addr = A_BBRAM_8,
        .pre_write = bbram_r8_prew,
        .post_write = bbram_r8_postw,
        .post_read = bbram_r8_postr,
    },{ .name = "BBRAM_SLVERR",  .addr = A_BBRAM_SLVERR,
        .rsvd = ~1,
    },{ .name = "BBRAM_ISR",  .addr = A_BBRAM_ISR,
        .w1c = 0x1,
        .post_write = bbram_isr_postw,
    },{ .name = "BBRAM_IMR",  .addr = A_BBRAM_IMR,
        .ro = 0x1,
    },{ .name = "BBRAM_IER",  .addr = A_BBRAM_IER,
        .pre_write = bbram_ier_prew,
    },{ .name = "BBRAM_IDR",  .addr = A_BBRAM_IDR,
        .pre_write = bbram_idr_prew,
    },{ .name = "BBRAM_MSW_LOCK",  .addr = A_BBRAM_MSW_LOCK,
        .pre_write = bbram_msw_lock_prew,
        .ro = ~R_BBRAM_MSW_LOCK_VAL_MASK,
    }
};

static void bbram_ctrl_reset_hold(Object *obj)
{
    XlnxBBRam *s = XLNX_BBRAM(obj);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        if (i < R_BBRAM_0 || i > R_BBRAM_8) {
            register_reset(&s->regs_info[i]);
        }
    }

    bbram_update_irq(s);
}

static const MemoryRegionOps bbram_ctrl_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void bbram_ctrl_realize(DeviceState *dev, Error **errp)
{
    XlnxBBRam *s = XLNX_BBRAM(dev);

    if (s->crc_zpads) {
        s->bbram8_wo = true;
    }

    bbram_bdrv_read(s, errp);
}

static void bbram_ctrl_init(Object *obj)
{
    XlnxBBRam *s = XLNX_BBRAM(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    RegisterInfoArray *reg_array;

    reg_array =
        register_init_block32(DEVICE(obj), bbram_ctrl_regs_info,
                              ARRAY_SIZE(bbram_ctrl_regs_info),
                              s->regs_info, s->regs,
                              &bbram_ctrl_ops,
                              XLNX_BBRAM_ERR_DEBUG,
                              R_MAX * 4);

    sysbus_init_mmio(sbd, &reg_array->mem);
    sysbus_init_irq(sbd, &s->irq_bbram);
}

static void bbram_prop_set_drive(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    DeviceState *dev = DEVICE(obj);

    qdev_prop_drive.set(obj, v, name, opaque, errp);

    /* Fill initial data if backend is attached after realized */
    if (dev->realized) {
        bbram_bdrv_read(XLNX_BBRAM(obj), errp);
    }
}

static void bbram_prop_get_drive(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    qdev_prop_drive.get(obj, v, name, opaque, errp);
}

static void bbram_prop_release_drive(Object *obj, const char *name,
                                     void *opaque)
{
    qdev_prop_drive.release(obj, name, opaque);
}

static const PropertyInfo bbram_prop_drive = {
    .name  = "str",
    .description = "Node name or ID of a block device to use as BBRAM backend",
    .realized_set_allowed = true,
    .get = bbram_prop_get_drive,
    .set = bbram_prop_set_drive,
    .release = bbram_prop_release_drive,
};

static const VMStateDescription vmstate_bbram_ctrl = {
    .name = TYPE_XLNX_BBRAM,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, XlnxBBRam, R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static Property bbram_ctrl_props[] = {
    DEFINE_PROP("drive", XlnxBBRam, blk, bbram_prop_drive, BlockBackend *),
    DEFINE_PROP_UINT32("crc-zpads", XlnxBBRam, crc_zpads, 1),
    DEFINE_PROP_END_OF_LIST(),
};

static void bbram_ctrl_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = bbram_ctrl_reset_hold;
    dc->realize = bbram_ctrl_realize;
    dc->vmsd = &vmstate_bbram_ctrl;
    device_class_set_props(dc, bbram_ctrl_props);
}

static const TypeInfo bbram_ctrl_info = {
    .name          = TYPE_XLNX_BBRAM,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XlnxBBRam),
    .class_init    = bbram_ctrl_class_init,
    .instance_init = bbram_ctrl_init,
};

static void bbram_ctrl_register_types(void)
{
    type_register_static(&bbram_ctrl_info);
}

type_init(bbram_ctrl_register_types)
