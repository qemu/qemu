/*
 * QEMU model of the EFuse_Cache
 *
 * Copyright (c) 2017 Xilinx Inc.
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
#include "hw/nvram/xlnx-versal-efuse.h"

#include "qemu/log.h"
#include "hw/qdev-properties.h"

#define MR_SIZE 0xC00

static uint64_t efuse_cache_read(void *opaque, hwaddr addr, unsigned size)
{
    XlnxVersalEFuseCache *s = XLNX_VERSAL_EFUSE_CACHE(opaque);
    unsigned int w0 = QEMU_ALIGN_DOWN(addr * 8, 32);
    unsigned int w1 = QEMU_ALIGN_DOWN((addr + size - 1) * 8, 32);

    uint64_t ret;

    assert(w0 == w1 || (w0 + 32) == w1);

    ret = xlnx_versal_efuse_read_row(s->efuse, w1, NULL);
    if (w0 < w1) {
        ret <<= 32;
        ret |= xlnx_versal_efuse_read_row(s->efuse, w0, NULL);
    }

    /* If 'addr' unaligned, the guest is always assumed to be little-endian. */
    addr &= 3;
    if (addr) {
        ret >>= 8 * addr;
    }

    return ret;
}

static void efuse_cache_write(void *opaque, hwaddr addr, uint64_t value,
                              unsigned size)
{
    /* No Register Writes allowed */
    qemu_log_mask(LOG_GUEST_ERROR, "%s: efuse cache registers are read-only",
                  __func__);
}

static const MemoryRegionOps efuse_cache_ops = {
    .read = efuse_cache_read,
    .write = efuse_cache_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void efuse_cache_init(Object *obj)
{
    XlnxVersalEFuseCache *s = XLNX_VERSAL_EFUSE_CACHE(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &efuse_cache_ops, s,
                          TYPE_XLNX_VERSAL_EFUSE_CACHE, MR_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const Property efuse_cache_props[] = {
    DEFINE_PROP_LINK("efuse",
                     XlnxVersalEFuseCache, efuse,
                     TYPE_XLNX_EFUSE, XlnxEFuse *),
};

static void efuse_cache_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, efuse_cache_props);
}

static const TypeInfo efuse_cache_info = {
    .name          = TYPE_XLNX_VERSAL_EFUSE_CACHE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XlnxVersalEFuseCache),
    .class_init    = efuse_cache_class_init,
    .instance_init = efuse_cache_init,
};

static void efuse_cache_register_types(void)
{
    type_register_static(&efuse_cache_info);
}

type_init(efuse_cache_register_types)
