/*
 * Kendryte K230 GZIP decompression engine
 *
 * Copyright (c) 2026 Tao Ding <dingtao0430@163.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Decompression accelerator is mainly used to implement hardware
 * GZIP decompression function. (K230 TRM section 15)
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"
#include "system/address-spaces.h"
#include "hw/core/irq.h"
#include "hw/misc/k230_decomp_gzip.h"
#include "trace.h"

#define GZIP_METHOD_DEFLATE    8
#define GZIP_METHOD_VENDOR_9   9

#define DEFLATE_BTYPE_DYNAMIC  2

#define K230_DECOMP_GZIP_OUTPUT_SLOTS 4
#define K230_DECOMP_GZIP_INPUT_SLOTS 2

static void k230_decomp_gzip_set_output(K230DecompGzipState *s, int line,
                                        int level)
{
    qemu_set_irq(s->signal_out[line], level);
}

static bool k230_decomp_gzip_read_mem(K230DecompGzipState *s, hwaddr addr,
                                      void *buf, size_t len)
{
    MemTxResult ret = address_space_read(&address_space_memory,
                                         s->sram_base + addr,
                                         MEMTXATTRS_UNSPECIFIED, buf, len);

    return ret == MEMTX_OK;
}

static bool k230_decomp_gzip_write_mem(K230DecompGzipState *s, hwaddr addr,
                                       const void *buf, size_t len)
{
    return address_space_write(&address_space_memory, s->sram_base + addr,
                               MEMTXATTRS_UNSPECIFIED, buf, len) == MEMTX_OK;
}

static hwaddr k230_decomp_gzip_output_addr(K230DecompGzipState *s)
{
    return K230_DECOMP_GZIP_SRAM_OUT_BASE +
           s->output.slot * K230_DECOMP_GZIP_BLOCK_SIZE +
           s->output.current_offset;
}

static hwaddr k230_decomp_gzip_input_addr(K230DecompGzipState *s)
{
    return s->input.slot * K230_DECOMP_GZIP_BLOCK_SIZE +
           K230_DECOMP_GZIP_SRAM_IN_BASE;
}

static uint32_t k230_decomp_gzip_current_input_size(K230DecompGzipState *s)
{
    if (s->total_requested == 0) {
        return 0;
    }

    return ((s->total_requested - 1) % K230_DECOMP_GZIP_BLOCK_SIZE) + 1;
}

static void k230_decomp_gzip_update_ctrl_en(K230DecompGzipState *s)
{
    int level = s->active && !!(s->gzip_src_size & K230_DECOMP_GZIP_CTRL_EN);

    k230_decomp_gzip_set_output(s, K230_DECOMP_GZIP_GPIO_DECOMP_CTRL_EN,
                                level);
}

static void k230_decomp_gzip_reset_stream(K230DecompGzipState *s)
{
    if (s->zstream_inited) {
        inflateEnd(&s->zs);
        s->zstream_inited = false;
    }
    memset(&s->zs, 0, sizeof(s->zs));
}

static void k230_decomp_gzip_finish(K230DecompGzipState *s, bool crc_ok)
{
    s->active = false;
    k230_decomp_gzip_reset_stream(s);
    k230_decomp_gzip_update_ctrl_en(s);
    if (crc_ok) {
        s->decomp_stat |= K230_DECOMP_GZIP_STAT_CRC_OK;
    } else {
        s->decomp_stat &= ~K230_DECOMP_GZIP_STAT_CRC_OK;
    }
}

static bool k230_decomp_gzip_load_input(K230DecompGzipState *s,
                                        uint32_t addr, uint32_t size)
{
    s->input.current_offset = 0;

    return k230_decomp_gzip_read_mem(s, addr, s->input_buf, size);
}

static bool k230_decomp_gzip_request_input(K230DecompGzipState *s)
{
    uint32_t total_requested = s->total_requested;

    k230_decomp_gzip_set_output(s, K230_DECOMP_GZIP_GPIO_DMA_WRITE_REQ, 1);
    if (s->total_requested == total_requested) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: dma write request not acknowledged\n",
                      TYPE_K230_DECOMP_GZIP);
        return false;
    }
    return true;
}

static bool k230_decomp_gzip_request_output(K230DecompGzipState *s)
{
    uint32_t slot = s->output.slot;
    uint32_t current_offset = s->output.current_offset;

    k230_decomp_gzip_set_output(s, K230_DECOMP_GZIP_GPIO_DMA_READ_REQ, 1);
    if (s->output.slot == slot &&
        s->output.current_offset == current_offset) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: dma read request not acknowledged\n",
                      TYPE_K230_DECOMP_GZIP);
        return false;
    }
    return true;
}

static bool k230_decomp_gzip_init_stream(K230DecompGzipState *s)
{
    uint8_t btype = (s->input_buf[10] >> 1) & 0x3;
    if (btype != DEFLATE_BTYPE_DYNAMIC) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: unsupported DEFLATE BTYPE %u\n",
                      TYPE_K230_DECOMP_GZIP, btype);
        return false;
    }

    /* The compression script for K230 will modify the third byte to 0x9 */
    if (s->input_buf[2] == GZIP_METHOD_VENDOR_9) {
        s->input_buf[2] = GZIP_METHOD_DEFLATE;
    }

    if (inflateInit2(&s->zs, 15 + 16) != Z_OK) {
        return false;
    }

    s->zstream_inited = true;
    return true;
}

static bool k230_decomp_gzip_run_inflate(K230DecompGzipState *s)
{
    uint32_t output_size = s->gzip_out_size;
    uint32_t current_input_size = k230_decomp_gzip_current_input_size(s);
    uint32_t avail_in;
    uint32_t avail_out;
    uint32_t consumed;
    uint32_t produced;
    int ret;

    avail_in = current_input_size - s->input.current_offset;
    avail_out = K230_DECOMP_GZIP_BLOCK_SIZE - s->output.current_offset;

    s->zs.next_in = s->input_buf + s->input.current_offset;
    s->zs.avail_in = avail_in;
    s->zs.next_out = s->output_buf;
    s->zs.avail_out = avail_out;

    ret = inflate(&s->zs, Z_NO_FLUSH);
    if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) {
        return false;
    }

    consumed = avail_in - s->zs.avail_in;
    produced = avail_out - s->zs.avail_out;

    if (produced) {
        if (s->total_produced + produced > output_size ||
            !k230_decomp_gzip_write_mem(s, k230_decomp_gzip_output_addr(s),
                                        s->output_buf, produced)) {
            return false;
        }
        s->output.current_offset += produced;
        s->total_produced += produced;
    }

    if (consumed) {
        s->input.current_offset += consumed;
    }

    if (ret == Z_STREAM_END) {
        s->stream_end = true;
        if (s->total_produced != output_size) {
            return false;
        }
    }

    return true;
}

static void k230_decomp_gzip_kick(K230DecompGzipState *s)
{
    if (!s->active || s->in_kick) {
        return;
    }

    uint32_t input_size = s->gzip_src_size & K230_DECOMP_GZIP_DMA_IN_MASK;
    s->in_kick = true;
    while (s->active) {
        uint32_t current_input_size = k230_decomp_gzip_current_input_size(s);
        /* Output is full or last block */
        if (s->output.current_offset == K230_DECOMP_GZIP_BLOCK_SIZE ||
            (s->stream_end && s->output.current_offset != 0)) {
            if (!k230_decomp_gzip_request_output(s)) {
                k230_decomp_gzip_finish(s, false);
                break;
            }
            continue;
        }

        if (!s->zstream_inited) {
            if (!k230_decomp_gzip_request_input(s) ||
                !k230_decomp_gzip_init_stream(s)) {
                k230_decomp_gzip_finish(s, false);
                break;
            }
            continue;
        }

        /* Transfer finish */
        if (s->stream_end) {
            k230_decomp_gzip_finish(s,
                                    s->total_produced == s->gzip_out_size);
            break;
        }

        /* Input buf has has been fully decompreed, need request more */
        if (s->input.current_offset == current_input_size) {
            if (s->total_requested < input_size) {
                if (!k230_decomp_gzip_request_input(s)) {
                    k230_decomp_gzip_finish(s, false);
                    break;
                }
                continue;
            }
            k230_decomp_gzip_finish(s, false);
            break;
        }

        /* Decompress the data, input buf or output buf are consumed in one block */
        if (!k230_decomp_gzip_run_inflate(s)) {
            k230_decomp_gzip_finish(s, false);
            break;
        }
    }
    s->in_kick = false;
}

static void k230_decomp_gzip_handle_ack(void *opaque, int n, int level)
{
    K230DecompGzipState *s = opaque;

    if (!level || !s->active) {
        return;
    }

    switch (n) {
    case K230_DECOMP_GZIP_GPIO_DMA_WRITE_ACK:
    {
        uint32_t input_size = s->gzip_src_size & K230_DECOMP_GZIP_DMA_IN_MASK;
        uint32_t chunk = MIN(K230_DECOMP_GZIP_BLOCK_SIZE,
                             input_size - s->total_requested);

        if (!k230_decomp_gzip_load_input(s, k230_decomp_gzip_input_addr(s),
                                         chunk)) {
            k230_decomp_gzip_finish(s, false);
            return;
        }
        s->total_requested += chunk;
        s->input.slot = (s->input.slot + 1) % K230_DECOMP_GZIP_INPUT_SLOTS;
        break;
    }
    case K230_DECOMP_GZIP_GPIO_DMA_READ_ACK:
        if (s->output.current_offset == 0) {
            k230_decomp_gzip_finish(s, false);
            return;
        }
        s->output.slot = (s->output.slot + 1) % K230_DECOMP_GZIP_OUTPUT_SLOTS;
        s->output.current_offset = 0;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid DMA acknowledgment signal %d\n",
                      TYPE_K230_DECOMP_GZIP, n);
        return;
    }

    k230_decomp_gzip_kick(s);
}

static void k230_decomp_gzip_start(K230DecompGzipState *s)
{
    uint32_t input_size = s->gzip_src_size & K230_DECOMP_GZIP_DMA_IN_MASK;

    k230_decomp_gzip_reset_stream(s);
    s->decomp_stat &= ~K230_DECOMP_GZIP_STAT_CRC_OK;
    s->total_requested = 0;
    s->total_produced = 0;
    s->stream_end = false;
    memset(&s->input, 0, sizeof(s->input));
    memset(&s->output, 0, sizeof(s->output));

    if (!(s->gzip_src_size & K230_DECOMP_GZIP_CTRL_EN) ||
        input_size == 0 || s->gzip_out_size == 0) {
        k230_decomp_gzip_finish(s, false);
        return;
    }

    s->active = true;
    k230_decomp_gzip_update_ctrl_en(s);
    k230_decomp_gzip_kick(s);
}

static uint64_t k230_decomp_gzip_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    K230DecompGzipState *s = opaque;
    uint64_t value = 0;

    switch (offset) {
    case K230_DECOMP_GZIP_DECOMP_START:
        value = s->decomp_start & ~K230_DECOMP_GZIP_START; /* start bit is write only */
        break;
    case K230_DECOMP_GZIP_GZIP_SRC_SIZE:
        value = s->gzip_src_size;
        break;
    case K230_DECOMP_GZIP_GZIP_OUT_SIZE:
        value = s->gzip_out_size;
        break;
    case K230_DECOMP_GZIP_DECOMP_STAT:
        value = s->decomp_stat;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad read offset 0x%" HWADDR_PRIx "\n",
                      TYPE_K230_DECOMP_GZIP, offset);
        break;
    }

    trace_k230_decomp_gzip_read(offset, size, value);
    return value;
}

static void k230_decomp_gzip_write(void *opaque, hwaddr offset,
                                   uint64_t value, unsigned size)
{
    K230DecompGzipState *s = opaque;

    trace_k230_decomp_gzip_write(offset, size, value);

    switch (offset) {
    case K230_DECOMP_GZIP_DECOMP_START:
        s->decomp_start = value;
        if (value & K230_DECOMP_GZIP_START) {
            k230_decomp_gzip_start(s);
        }
        break;
    case K230_DECOMP_GZIP_GZIP_SRC_SIZE:
        s->gzip_src_size = value;
        k230_decomp_gzip_update_ctrl_en(s);
        break;
    case K230_DECOMP_GZIP_GZIP_OUT_SIZE:
        s->gzip_out_size = value;
        break;
    case K230_DECOMP_GZIP_DECOMP_STAT:
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad write offset 0x%" HWADDR_PRIx "\n",
                      TYPE_K230_DECOMP_GZIP, offset);
        break;
    }
}

static const MemoryRegionOps k230_decomp_gzip_ops = {
    .read = k230_decomp_gzip_read,
    .write = k230_decomp_gzip_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static const VMStateDescription vmstate_k230_decomp_gzip_slot = {
    .name = "k230.decomp-gzip.slot",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(slot, K230DecompGzipSlotState),
        VMSTATE_UINT32(current_offset, K230DecompGzipSlotState),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_k230_decomp_gzip = {
    .name = "k230.decomp-gzip",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(decomp_start, K230DecompGzipState),
        VMSTATE_UINT32(gzip_src_size, K230DecompGzipState),
        VMSTATE_UINT32(gzip_out_size, K230DecompGzipState),
        VMSTATE_UINT32(decomp_stat, K230DecompGzipState),
        VMSTATE_STRUCT(input, K230DecompGzipState, 1,
                       vmstate_k230_decomp_gzip_slot,
                       K230DecompGzipSlotState),
        VMSTATE_STRUCT(output, K230DecompGzipState, 1,
                       vmstate_k230_decomp_gzip_slot,
                       K230DecompGzipSlotState),
        VMSTATE_UINT32(total_requested, K230DecompGzipState),
        VMSTATE_UINT32(total_produced, K230DecompGzipState),
        VMSTATE_BOOL(active, K230DecompGzipState),
        VMSTATE_BOOL(in_kick, K230DecompGzipState),
        VMSTATE_BOOL(zstream_inited, K230DecompGzipState),
        VMSTATE_BOOL(stream_end, K230DecompGzipState),
        VMSTATE_UINT8_ARRAY(input_buf, K230DecompGzipState,
                            K230_DECOMP_GZIP_BLOCK_SIZE),
        VMSTATE_UINT8_ARRAY(output_buf, K230DecompGzipState,
                            K230_DECOMP_GZIP_BLOCK_SIZE),
        VMSTATE_END_OF_LIST()
    }
};

static void k230_decomp_gzip_reset_hold(Object *obj, ResetType type)
{
    K230DecompGzipState *s = K230_DECOMP_GZIP(obj);

    k230_decomp_gzip_reset_stream(s);
    memset(&s->zs, 0, sizeof(s->zs));
    s->decomp_start = 0;
    s->gzip_src_size = 0;
    s->gzip_out_size = 0;
    s->decomp_stat = 0;
    memset(&s->input, 0, sizeof(s->input));
    memset(&s->output, 0, sizeof(s->output));
    s->total_requested = 0;
    s->total_produced = 0;
    s->active = false;
    s->in_kick = false;
    s->stream_end = false;
}

static void k230_decomp_gzip_realize(DeviceState *dev, Error **errp)
{
    K230DecompGzipState *s = K230_DECOMP_GZIP(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem, OBJECT(dev), &k230_decomp_gzip_ops, s,
                          TYPE_K230_DECOMP_GZIP,
                          K230_DECOMP_GZIP_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    qdev_init_gpio_in(dev, k230_decomp_gzip_handle_ack,
                      K230_DECOMP_GZIP_NUM_GPIOS_IN);
    qdev_init_gpio_out(dev, s->signal_out,
                       K230_DECOMP_GZIP_NUM_GPIOS_OUT);
}

static const Property k230_decomp_gzip_properties[] = {
    DEFINE_PROP_UINT64("sram-base", K230DecompGzipState, sram_base, 0),
};

static void k230_decomp_gzip_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    dc->realize = k230_decomp_gzip_realize;
    rc->phases.hold = k230_decomp_gzip_reset_hold;
    device_class_set_props(dc, k230_decomp_gzip_properties);
    dc->vmsd = &vmstate_k230_decomp_gzip;
    dc->desc = "Kendryte K230 GZIP decompression engine";
}

static const TypeInfo k230_decomp_gzip_info = {
    .name = TYPE_K230_DECOMP_GZIP,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(K230DecompGzipState),
    .class_init = k230_decomp_gzip_class_init,
};

static void k230_decomp_gzip_register_types(void)
{
    type_register_static(&k230_decomp_gzip_info);
}

type_init(k230_decomp_gzip_register_types)
