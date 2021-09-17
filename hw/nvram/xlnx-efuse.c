/*
 * QEMU model of the EFUSE eFuse
 *
 * Copyright (c) 2015 Xilinx Inc.
 *
 * Written by Edgar E. Iglesias <edgari@xilinx.com>
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
#include "hw/nvram/xlnx-efuse.h"

#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "sysemu/blockdev.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"

#define TBIT0_OFFSET     28
#define TBIT1_OFFSET     29
#define TBIT2_OFFSET     30
#define TBIT3_OFFSET     31
#define TBITS_PATTERN    (0x0AU << TBIT0_OFFSET)
#define TBITS_MASK       (0x0FU << TBIT0_OFFSET)

bool xlnx_efuse_get_bit(XlnxEFuse *s, unsigned int bit)
{
    bool b = s->fuse32[bit / 32] & (1 << (bit % 32));
    return b;
}

static int efuse_bytes(XlnxEFuse *s)
{
    return ROUND_UP((s->efuse_nr * s->efuse_size) / 8, 4);
}

static int efuse_bdrv_read(XlnxEFuse *s, Error **errp)
{
    uint32_t *ram = s->fuse32;
    int nr = efuse_bytes(s);

    if (!s->blk) {
        return 0;
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
        warn_report("%s: Skip saving updates to read-only eFUSE backstore.",
                    blk_name(s->blk));
    }

    if (blk_pread(s->blk, 0, ram, nr) < 0) {
        error_setg(errp, "%s: Failed to read %u bytes from eFUSE backstore.",
                   blk_name(s->blk), nr);
        return -1;
    }

    /* Convert from little-endian backstore for each 32-bit row */
    nr /= 4;
    while (nr--) {
        ram[nr] = le32_to_cpu(ram[nr]);
    }

    return 0;
}

static void efuse_bdrv_sync(XlnxEFuse *s, unsigned int bit)
{
    unsigned int row_offset;
    uint32_t le32;

    if (!s->blk || s->blk_ro) {
        return;  /* Silent on read-only backend to avoid message flood */
    }

    /* Backstore is always in little-endian */
    le32 = cpu_to_le32(xlnx_efuse_get_row(s, bit));

    row_offset = (bit / 32) * 4;
    if (blk_pwrite(s->blk, row_offset, &le32, 4, 0) < 0) {
        error_report("%s: Failed to write offset %u of eFUSE backstore.",
                     blk_name(s->blk), row_offset);
    }
}

static int efuse_ro_bits_cmp(const void *a, const void *b)
{
    uint32_t i = *(const uint32_t *)a;
    uint32_t j = *(const uint32_t *)b;

    return (i > j) - (i < j);
}

static void efuse_ro_bits_sort(XlnxEFuse *s)
{
    uint32_t *ary = s->ro_bits;
    const uint32_t cnt = s->ro_bits_cnt;

    if (ary && cnt > 1) {
        qsort(ary, cnt, sizeof(ary[0]), efuse_ro_bits_cmp);
    }
}

static bool efuse_ro_bits_find(XlnxEFuse *s, uint32_t k)
{
    const uint32_t *ary = s->ro_bits;
    const uint32_t cnt = s->ro_bits_cnt;

    if (!ary || !cnt) {
        return false;
    }

    return bsearch(&k, ary, cnt, sizeof(ary[0]), efuse_ro_bits_cmp) != NULL;
}

bool xlnx_efuse_set_bit(XlnxEFuse *s, unsigned int bit)
{
    if (efuse_ro_bits_find(s, bit)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: WARN: "
                      "Ignored setting of readonly efuse bit<%u,%u>!\n",
                      object_get_canonical_path(OBJECT(s)),
                      (bit / 32), (bit % 32));
        return false;
    }

    s->fuse32[bit / 32] |= 1 << (bit % 32);
    efuse_bdrv_sync(s, bit);
    return true;
}

bool xlnx_efuse_k256_check(XlnxEFuse *s, uint32_t crc, unsigned start)
{
    uint32_t calc;

    /* A key always occupies multiple of whole rows */
    assert((start % 32) == 0);

    calc = xlnx_efuse_calc_crc(&s->fuse32[start / 32], (256 / 32), 0);
    return calc == crc;
}

uint32_t xlnx_efuse_tbits_check(XlnxEFuse *s)
{
    int nr;
    uint32_t check = 0;

    for (nr = s->efuse_nr; nr-- > 0; ) {
        int efuse_start_row_num = (s->efuse_size * nr) / 32;
        uint32_t data = s->fuse32[efuse_start_row_num];

        /*
         * If the option is on, auto-init blank T-bits.
         * (non-blank will still be reported as '0' in the check, e.g.,
         *  for error-injection tests)
         */
        if ((data & TBITS_MASK) == 0 && s->init_tbits) {
            data |= TBITS_PATTERN;

            s->fuse32[efuse_start_row_num] = data;
            efuse_bdrv_sync(s, (efuse_start_row_num * 32 + TBIT0_OFFSET));
        }

        check = (check << 1) | ((data & TBITS_MASK) == TBITS_PATTERN);
    }

    return check;
}

static void efuse_realize(DeviceState *dev, Error **errp)
{
    XlnxEFuse *s = XLNX_EFUSE(dev);

    /* Sort readonly-list for bsearch lookup */
    efuse_ro_bits_sort(s);

    if ((s->efuse_size % 32) != 0) {
        error_setg(errp,
                   "%s.efuse-size: %u: property value not multiple of 32.",
                   object_get_canonical_path(OBJECT(dev)), s->efuse_size);
        return;
    }

    s->fuse32 = g_malloc0(efuse_bytes(s));
    if (efuse_bdrv_read(s, errp)) {
        g_free(s->fuse32);
    }
}

static void efuse_prop_set_drive(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    DeviceState *dev = DEVICE(obj);

    qdev_prop_drive.set(obj, v, name, opaque, errp);

    /* Fill initial data if backend is attached after realized */
    if (dev->realized) {
        efuse_bdrv_read(XLNX_EFUSE(obj), errp);
    }
}

static void efuse_prop_get_drive(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    qdev_prop_drive.get(obj, v, name, opaque, errp);
}

static void efuse_prop_release_drive(Object *obj, const char *name,
                                     void *opaque)
{
    qdev_prop_drive.release(obj, name, opaque);
}

static const PropertyInfo efuse_prop_drive = {
    .name  = "str",
    .description = "Node name or ID of a block device to use as eFUSE backend",
    .realized_set_allowed = true,
    .get = efuse_prop_get_drive,
    .set = efuse_prop_set_drive,
    .release = efuse_prop_release_drive,
};

static Property efuse_properties[] = {
    DEFINE_PROP("drive", XlnxEFuse, blk, efuse_prop_drive, BlockBackend *),
    DEFINE_PROP_UINT8("efuse-nr", XlnxEFuse, efuse_nr, 3),
    DEFINE_PROP_UINT32("efuse-size", XlnxEFuse, efuse_size, 64 * 32),
    DEFINE_PROP_BOOL("init-factory-tbits", XlnxEFuse, init_tbits, true),
    DEFINE_PROP_ARRAY("read-only", XlnxEFuse, ro_bits_cnt, ro_bits,
                      qdev_prop_uint32, uint32_t),
    DEFINE_PROP_END_OF_LIST(),
};

static void efuse_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = efuse_realize;
    device_class_set_props(dc, efuse_properties);
}

static const TypeInfo efuse_info = {
    .name          = TYPE_XLNX_EFUSE,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(XlnxEFuse),
    .class_init    = efuse_class_init,
};

static void efuse_register_types(void)
{
    type_register_static(&efuse_info);
}
type_init(efuse_register_types)
