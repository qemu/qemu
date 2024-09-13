/*
 * Xilinx Display Port Control Data
 *
 *  Copyright (C) 2015 : GreenSocs Ltd
 *      http://www.greensocs.com/ , email: info@greensocs.com
 *
 *  Developed by :
 *  Frederic Konrad   <fred.konrad@greensocs.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option)any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

/*
 * This is a simple AUX slave which emulates a connected screen.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/misc/auxbus.h"
#include "migration/vmstate.h"
#include "hw/display/dpcd.h"
#include "trace.h"

#define DPCD_READABLE_AREA                      0x600

struct DPCDState {
    /*< private >*/
    AUXSlave parent_obj;

    /*< public >*/
    /*
     * The DCPD is 0x7FFFF length but read as 0 after offset 0x5FF.
     */
    uint8_t dpcd_info[DPCD_READABLE_AREA];

    MemoryRegion iomem;
};

static uint64_t dpcd_read(void *opaque, hwaddr offset, unsigned size)
{
    uint8_t ret;
    DPCDState *e = DPCD(opaque);

    if (offset < DPCD_READABLE_AREA) {
        ret = e->dpcd_info[offset];
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "dpcd: Bad offset 0x%" HWADDR_PRIX "\n",
                                       offset);
        ret = 0;
    }
    trace_dpcd_read(offset, ret);

    return ret;
}

static void dpcd_write(void *opaque, hwaddr offset, uint64_t value,
                       unsigned size)
{
    DPCDState *e = DPCD(opaque);

    trace_dpcd_write(offset, value);
    if (offset < DPCD_READABLE_AREA) {
        e->dpcd_info[offset] = value;
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "dpcd: Bad offset 0x%" HWADDR_PRIX "\n",
                                       offset);
    }
}

static const MemoryRegionOps aux_ops = {
    .read = dpcd_read,
    .write = dpcd_write,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void dpcd_reset(DeviceState *dev)
{
    DPCDState *s = DPCD(dev);

    memset(&(s->dpcd_info), 0, sizeof(s->dpcd_info));

    s->dpcd_info[DPCD_REVISION] = DPCD_REV_1_0;
    s->dpcd_info[DPCD_MAX_LINK_RATE] = DPCD_5_4GBPS;
    s->dpcd_info[DPCD_MAX_LANE_COUNT] = DPCD_FOUR_LANES;
    s->dpcd_info[DPCD_RECEIVE_PORT0_CAP_0] = DPCD_EDID_PRESENT;
    /* buffer size */
    s->dpcd_info[DPCD_RECEIVE_PORT0_CAP_1] = 0xFF;

    s->dpcd_info[DPCD_LANE0_1_STATUS] = DPCD_LANE0_CR_DONE
                                      | DPCD_LANE0_CHANNEL_EQ_DONE
                                      | DPCD_LANE0_SYMBOL_LOCKED
                                      | DPCD_LANE1_CR_DONE
                                      | DPCD_LANE1_CHANNEL_EQ_DONE
                                      | DPCD_LANE1_SYMBOL_LOCKED;
    s->dpcd_info[DPCD_LANE2_3_STATUS] = DPCD_LANE2_CR_DONE
                                      | DPCD_LANE2_CHANNEL_EQ_DONE
                                      | DPCD_LANE2_SYMBOL_LOCKED
                                      | DPCD_LANE3_CR_DONE
                                      | DPCD_LANE3_CHANNEL_EQ_DONE
                                      | DPCD_LANE3_SYMBOL_LOCKED;

    s->dpcd_info[DPCD_LANE_ALIGN_STATUS_UPDATED] = DPCD_INTERLANE_ALIGN_DONE;
    s->dpcd_info[DPCD_SINK_STATUS] = DPCD_RECEIVE_PORT_0_STATUS;
}

static void dpcd_init(Object *obj)
{
    DPCDState *s = DPCD(obj);

    memory_region_init_io(&s->iomem, obj, &aux_ops, s, TYPE_DPCD, 0x80000);
    aux_init_mmio(AUX_SLAVE(obj), &s->iomem);
}

static const VMStateDescription vmstate_dpcd = {
    .name = TYPE_DPCD,
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_ARRAY_V(dpcd_info, DPCDState, DPCD_READABLE_AREA, 0),
        VMSTATE_END_OF_LIST()
    }
};

static void dpcd_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_legacy_reset(dc, dpcd_reset);
    dc->vmsd = &vmstate_dpcd;
}

static const TypeInfo dpcd_info = {
    .name          = TYPE_DPCD,
    .parent        = TYPE_AUX_SLAVE,
    .instance_size = sizeof(DPCDState),
    .class_init    = dpcd_class_init,
    .instance_init = dpcd_init,
};

static void dpcd_register_types(void)
{
    type_register_static(&dpcd_info);
}

type_init(dpcd_register_types)
