/*
 *  QEMU model of the Milkymist SD Card Controller.
 *
 *  Copyright (c) 2010 Michael Walle <michael@walle.cc>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Specification available at:
 *   http://www.milkymist.org/socdoc/memcard.pdf
 */

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "sysemu/sysemu.h"
#include "trace.h"
#include "qemu/error-report.h"
#include "sysemu/block-backend.h"
#include "sysemu/blockdev.h"
#include "hw/sd/sd.h"

enum {
    ENABLE_CMD_TX   = (1<<0),
    ENABLE_CMD_RX   = (1<<1),
    ENABLE_DAT_TX   = (1<<2),
    ENABLE_DAT_RX   = (1<<3),
};

enum {
    PENDING_CMD_TX   = (1<<0),
    PENDING_CMD_RX   = (1<<1),
    PENDING_DAT_TX   = (1<<2),
    PENDING_DAT_RX   = (1<<3),
};

enum {
    START_CMD_TX    = (1<<0),
    START_DAT_RX    = (1<<1),
};

enum {
    R_CLK2XDIV = 0,
    R_ENABLE,
    R_PENDING,
    R_START,
    R_CMD,
    R_DAT,
    R_MAX
};

#define TYPE_MILKYMIST_MEMCARD "milkymist-memcard"
#define MILKYMIST_MEMCARD(obj) \
    OBJECT_CHECK(MilkymistMemcardState, (obj), TYPE_MILKYMIST_MEMCARD)

struct MilkymistMemcardState {
    SysBusDevice parent_obj;

    MemoryRegion regs_region;
    SDState *card;

    int command_write_ptr;
    int response_read_ptr;
    int response_len;
    int ignore_next_cmd;
    int enabled;
    uint8_t command[6];
    uint8_t response[17];
    uint32_t regs[R_MAX];
};
typedef struct MilkymistMemcardState MilkymistMemcardState;

static void update_pending_bits(MilkymistMemcardState *s)
{
    /* transmits are instantaneous, thus tx pending bits are never set */
    s->regs[R_PENDING] = 0;
    /* if rx is enabled the corresponding pending bits are always set */
    if (s->regs[R_ENABLE] & ENABLE_CMD_RX) {
        s->regs[R_PENDING] |= PENDING_CMD_RX;
    }
    if (s->regs[R_ENABLE] & ENABLE_DAT_RX) {
        s->regs[R_PENDING] |= PENDING_DAT_RX;
    }
}

static void memcard_sd_command(MilkymistMemcardState *s)
{
    SDRequest req;

    req.cmd = s->command[0] & 0x3f;
    req.arg = (s->command[1] << 24) | (s->command[2] << 16)
              | (s->command[3] << 8) | s->command[4];
    req.crc = s->command[5];

    s->response[0] = req.cmd;
    s->response_len = sd_do_command(s->card, &req, s->response+1);
    s->response_read_ptr = 0;

    if (s->response_len == 16) {
        /* R2 response */
        s->response[0] = 0x3f;
        s->response_len += 1;
    } else if (s->response_len == 4) {
        /* no crc calculation, insert dummy byte */
        s->response[5] = 0;
        s->response_len += 2;
    }

    if (req.cmd == 0) {
        /* next write is a dummy byte to clock the initialization of the sd
         * card */
        s->ignore_next_cmd = 1;
    }
}

static uint64_t memcard_read(void *opaque, hwaddr addr,
                             unsigned size)
{
    MilkymistMemcardState *s = opaque;
    uint32_t r = 0;

    addr >>= 2;
    switch (addr) {
    case R_CMD:
        if (!s->enabled) {
            r = 0xff;
        } else {
            r = s->response[s->response_read_ptr++];
            if (s->response_read_ptr > s->response_len) {
                error_report("milkymist_memcard: "
                        "read more cmd bytes than available. Clipping.");
                s->response_read_ptr = 0;
            }
        }
        break;
    case R_DAT:
        if (!s->enabled) {
            r = 0xffffffff;
        } else {
            r = 0;
            r |= sd_read_data(s->card) << 24;
            r |= sd_read_data(s->card) << 16;
            r |= sd_read_data(s->card) << 8;
            r |= sd_read_data(s->card);
        }
        break;
    case R_CLK2XDIV:
    case R_ENABLE:
    case R_PENDING:
    case R_START:
        r = s->regs[addr];
        break;

    default:
        error_report("milkymist_memcard: read access to unknown register 0x"
                TARGET_FMT_plx, addr << 2);
        break;
    }

    trace_milkymist_memcard_memory_read(addr << 2, r);

    return r;
}

static void memcard_write(void *opaque, hwaddr addr, uint64_t value,
                          unsigned size)
{
    MilkymistMemcardState *s = opaque;

    trace_milkymist_memcard_memory_write(addr, value);

    addr >>= 2;
    switch (addr) {
    case R_PENDING:
        /* clear rx pending bits */
        s->regs[R_PENDING] &= ~(value & (PENDING_CMD_RX | PENDING_DAT_RX));
        update_pending_bits(s);
        break;
    case R_CMD:
        if (!s->enabled) {
            break;
        }
        if (s->ignore_next_cmd) {
            s->ignore_next_cmd = 0;
            break;
        }
        s->command[s->command_write_ptr] = value & 0xff;
        s->command_write_ptr = (s->command_write_ptr + 1) % 6;
        if (s->command_write_ptr == 0) {
            memcard_sd_command(s);
        }
        break;
    case R_DAT:
        if (!s->enabled) {
            break;
        }
        sd_write_data(s->card, (value >> 24) & 0xff);
        sd_write_data(s->card, (value >> 16) & 0xff);
        sd_write_data(s->card, (value >> 8) & 0xff);
        sd_write_data(s->card, value & 0xff);
        break;
    case R_ENABLE:
        s->regs[addr] = value;
        update_pending_bits(s);
        break;
    case R_CLK2XDIV:
    case R_START:
        s->regs[addr] = value;
        break;

    default:
        error_report("milkymist_memcard: write access to unknown register 0x"
                TARGET_FMT_plx, addr << 2);
        break;
    }
}

static const MemoryRegionOps memcard_mmio_ops = {
    .read = memcard_read,
    .write = memcard_write,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void milkymist_memcard_reset(DeviceState *d)
{
    MilkymistMemcardState *s = MILKYMIST_MEMCARD(d);
    int i;

    s->command_write_ptr = 0;
    s->response_read_ptr = 0;
    s->response_len = 0;

    for (i = 0; i < R_MAX; i++) {
        s->regs[i] = 0;
    }
}

static int milkymist_memcard_init(SysBusDevice *dev)
{
    MilkymistMemcardState *s = MILKYMIST_MEMCARD(dev);
    DriveInfo *dinfo;
    BlockBackend *blk;

    /* FIXME use a qdev drive property instead of drive_get_next() */
    dinfo = drive_get_next(IF_SD);
    blk = dinfo ? blk_by_legacy_dinfo(dinfo) : NULL;
    s->card = sd_init(blk, false);
    if (s->card == NULL) {
        return -1;
    }

    s->enabled = blk && blk_is_inserted(blk);

    memory_region_init_io(&s->regs_region, OBJECT(s), &memcard_mmio_ops, s,
            "milkymist-memcard", R_MAX * 4);
    sysbus_init_mmio(dev, &s->regs_region);

    return 0;
}

static const VMStateDescription vmstate_milkymist_memcard = {
    .name = "milkymist-memcard",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_INT32(command_write_ptr, MilkymistMemcardState),
        VMSTATE_INT32(response_read_ptr, MilkymistMemcardState),
        VMSTATE_INT32(response_len, MilkymistMemcardState),
        VMSTATE_INT32(ignore_next_cmd, MilkymistMemcardState),
        VMSTATE_INT32(enabled, MilkymistMemcardState),
        VMSTATE_UINT8_ARRAY(command, MilkymistMemcardState, 6),
        VMSTATE_UINT8_ARRAY(response, MilkymistMemcardState, 17),
        VMSTATE_UINT32_ARRAY(regs, MilkymistMemcardState, R_MAX),
        VMSTATE_END_OF_LIST()
    }
};

static void milkymist_memcard_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = milkymist_memcard_init;
    dc->reset = milkymist_memcard_reset;
    dc->vmsd = &vmstate_milkymist_memcard;
    /* Reason: init() method uses drive_get_next() */
    dc->cannot_instantiate_with_device_add_yet = true;
}

static const TypeInfo milkymist_memcard_info = {
    .name          = TYPE_MILKYMIST_MEMCARD,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MilkymistMemcardState),
    .class_init    = milkymist_memcard_class_init,
};

static void milkymist_memcard_register_types(void)
{
    type_register_static(&milkymist_memcard_info);
}

type_init(milkymist_memcard_register_types)
