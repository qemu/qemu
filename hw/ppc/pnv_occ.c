/*
 * QEMU PowerPC PowerNV Emulation of a few OCC related registers
 *
 * Copyright (c) 2015-2017, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "target/ppc/cpu.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/ppc/pnv.h"
#include "hw/ppc/pnv_chip.h"
#include "hw/ppc/pnv_xscom.h"
#include "hw/ppc/pnv_occ.h"

#define P8_HOMER_OPAL_DATA_OFFSET    0x1F8000
#define P9_HOMER_OPAL_DATA_OFFSET    0x0E2000

#define OCB_OCI_OCCMISC         0x4020
#define OCB_OCI_OCCMISC_AND     0x4021
#define OCB_OCI_OCCMISC_OR      0x4022
#define   OCCMISC_PSI_IRQ       PPC_BIT(0)

/* OCC sensors */
#define OCC_SENSOR_DATA_BLOCK_OFFSET          0x0000
#define OCC_SENSOR_DATA_VALID                 0x0001
#define OCC_SENSOR_DATA_VERSION               0x0002
#define OCC_SENSOR_DATA_READING_VERSION       0x0004
#define OCC_SENSOR_DATA_NR_SENSORS            0x0008
#define OCC_SENSOR_DATA_NAMES_OFFSET          0x0010
#define OCC_SENSOR_DATA_READING_PING_OFFSET   0x0014
#define OCC_SENSOR_DATA_READING_PONG_OFFSET   0x000c
#define OCC_SENSOR_DATA_NAME_LENGTH           0x000d
#define OCC_SENSOR_NAME_STRUCTURE_TYPE        0x0023
#define OCC_SENSOR_LOC_CORE                   0x0022
#define OCC_SENSOR_LOC_GPU                    0x0020
#define OCC_SENSOR_TYPE_POWER                 0x0003
#define OCC_SENSOR_NAME                       0x0005
#define HWMON_SENSORS_MASK                    0x001e

static void pnv_occ_set_misc(PnvOCC *occ, uint64_t val)
{
    val &= PPC_BITMASK(0, 18); /* Mask out unimplemented bits */

    occ->occmisc = val;

    /*
     * OCCMISC IRQ bit triggers the interrupt on a 0->1 edge, but not clear
     * how that is handled in PSI so it is level-triggered here, which is not
     * really correct (but skiboot is okay with it).
     */
    qemu_set_irq(occ->psi_irq, !!(val & OCCMISC_PSI_IRQ));
}

static uint64_t pnv_occ_power8_xscom_read(void *opaque, hwaddr addr,
                                          unsigned size)
{
    PnvOCC *occ = PNV_OCC(opaque);
    uint32_t offset = addr >> 3;
    uint64_t val = 0;

    switch (offset) {
    case OCB_OCI_OCCMISC:
        val = occ->occmisc;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "OCC Unimplemented register: Ox%"
                      HWADDR_PRIx "\n", addr >> 3);
    }
    return val;
}

static void pnv_occ_power8_xscom_write(void *opaque, hwaddr addr,
                                       uint64_t val, unsigned size)
{
    PnvOCC *occ = PNV_OCC(opaque);
    uint32_t offset = addr >> 3;

    switch (offset) {
    case OCB_OCI_OCCMISC_AND:
        pnv_occ_set_misc(occ, occ->occmisc & val);
        break;
    case OCB_OCI_OCCMISC_OR:
        pnv_occ_set_misc(occ, occ->occmisc | val);
        break;
    case OCB_OCI_OCCMISC:
        pnv_occ_set_misc(occ, val);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "OCC Unimplemented register: Ox%"
                      HWADDR_PRIx "\n", addr >> 3);
    }
}

static uint64_t pnv_occ_common_area_read(void *opaque, hwaddr addr,
                                         unsigned width)
{
    switch (addr) {
    /*
     * occ-sensor sanity check that asserts the sensor
     * header block
     */
    case OCC_SENSOR_DATA_BLOCK_OFFSET:
    case OCC_SENSOR_DATA_VALID:
    case OCC_SENSOR_DATA_VERSION:
    case OCC_SENSOR_DATA_READING_VERSION:
    case OCC_SENSOR_DATA_NR_SENSORS:
    case OCC_SENSOR_DATA_NAMES_OFFSET:
    case OCC_SENSOR_DATA_READING_PING_OFFSET:
    case OCC_SENSOR_DATA_READING_PONG_OFFSET:
    case OCC_SENSOR_NAME_STRUCTURE_TYPE:
        return 1;
    case OCC_SENSOR_DATA_NAME_LENGTH:
        return 0x30;
    case OCC_SENSOR_LOC_CORE:
        return 0x0040;
    case OCC_SENSOR_TYPE_POWER:
        return 0x0080;
    case OCC_SENSOR_NAME:
        return 0x1000;
    case HWMON_SENSORS_MASK:
    case OCC_SENSOR_LOC_GPU:
        return 0x8e00;
    }
    return 0;
}

static void pnv_occ_common_area_write(void *opaque, hwaddr addr,
                                             uint64_t val, unsigned width)
{
    /* callback function defined to occ common area write */
    return;
}

static const MemoryRegionOps pnv_occ_power8_xscom_ops = {
    .read = pnv_occ_power8_xscom_read,
    .write = pnv_occ_power8_xscom_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

const MemoryRegionOps pnv_occ_sram_ops = {
    .read = pnv_occ_common_area_read,
    .write = pnv_occ_common_area_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 8,
    .impl.min_access_size = 1,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void pnv_occ_power8_class_init(ObjectClass *klass, void *data)
{
    PnvOCCClass *poc = PNV_OCC_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "PowerNV OCC Controller (POWER8)";
    poc->opal_shared_memory_offset = P8_HOMER_OPAL_DATA_OFFSET;
    poc->opal_shared_memory_version = 0x02;
    poc->xscom_size = PNV_XSCOM_OCC_SIZE;
    poc->xscom_ops = &pnv_occ_power8_xscom_ops;
}

static const TypeInfo pnv_occ_power8_type_info = {
    .name          = TYPE_PNV8_OCC,
    .parent        = TYPE_PNV_OCC,
    .instance_size = sizeof(PnvOCC),
    .class_init    = pnv_occ_power8_class_init,
};

#define P9_OCB_OCI_OCCMISC              0x6080
#define P9_OCB_OCI_OCCMISC_CLEAR        0x6081
#define P9_OCB_OCI_OCCMISC_OR           0x6082


static uint64_t pnv_occ_power9_xscom_read(void *opaque, hwaddr addr,
                                          unsigned size)
{
    PnvOCC *occ = PNV_OCC(opaque);
    uint32_t offset = addr >> 3;
    uint64_t val = 0;

    switch (offset) {
    case P9_OCB_OCI_OCCMISC:
        val = occ->occmisc;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "OCC Unimplemented register: Ox%"
                      HWADDR_PRIx "\n", addr >> 3);
    }
    return val;
}

static void pnv_occ_power9_xscom_write(void *opaque, hwaddr addr,
                                       uint64_t val, unsigned size)
{
    PnvOCC *occ = PNV_OCC(opaque);
    uint32_t offset = addr >> 3;

    switch (offset) {
    case P9_OCB_OCI_OCCMISC_CLEAR:
        pnv_occ_set_misc(occ, 0);
        break;
    case P9_OCB_OCI_OCCMISC_OR:
        pnv_occ_set_misc(occ, occ->occmisc | val);
        break;
    case P9_OCB_OCI_OCCMISC:
        pnv_occ_set_misc(occ, val);
       break;
    default:
        qemu_log_mask(LOG_UNIMP, "OCC Unimplemented register: Ox%"
                      HWADDR_PRIx "\n", addr >> 3);
    }
}

static const MemoryRegionOps pnv_occ_power9_xscom_ops = {
    .read = pnv_occ_power9_xscom_read,
    .write = pnv_occ_power9_xscom_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void pnv_occ_power9_class_init(ObjectClass *klass, void *data)
{
    PnvOCCClass *poc = PNV_OCC_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "PowerNV OCC Controller (POWER9)";
    poc->opal_shared_memory_offset = P9_HOMER_OPAL_DATA_OFFSET;
    poc->opal_shared_memory_version = 0x90;
    poc->xscom_size = PNV9_XSCOM_OCC_SIZE;
    poc->xscom_ops = &pnv_occ_power9_xscom_ops;
    assert(!dc->user_creatable);
}

static const TypeInfo pnv_occ_power9_type_info = {
    .name          = TYPE_PNV9_OCC,
    .parent        = TYPE_PNV_OCC,
    .instance_size = sizeof(PnvOCC),
    .class_init    = pnv_occ_power9_class_init,
};

static void pnv_occ_power10_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "PowerNV OCC Controller (POWER10)";
}

static const TypeInfo pnv_occ_power10_type_info = {
    .name          = TYPE_PNV10_OCC,
    .parent        = TYPE_PNV9_OCC,
    .class_init    = pnv_occ_power10_class_init,
};

static bool occ_init_homer_memory(PnvOCC *occ, Error **errp);

static void pnv_occ_realize(DeviceState *dev, Error **errp)
{
    PnvOCC *occ = PNV_OCC(dev);
    PnvOCCClass *poc = PNV_OCC_GET_CLASS(occ);
    PnvHomer *homer = occ->homer;

    assert(homer);

    if (!occ_init_homer_memory(occ, errp)) {
        return;
    }

    occ->occmisc = 0;

    /* XScom region for OCC registers */
    pnv_xscom_region_init(&occ->xscom_regs, OBJECT(dev), poc->xscom_ops,
                          occ, "xscom-occ", poc->xscom_size);

    /* OCC common area mmio region for OCC SRAM registers */
    memory_region_init_io(&occ->sram_regs, OBJECT(dev), &pnv_occ_sram_ops,
                          occ, "occ-common-area",
                          PNV_OCC_SENSOR_DATA_BLOCK_SIZE);

    qdev_init_gpio_out(dev, &occ->psi_irq, 1);
}

static const Property pnv_occ_properties[] = {
    DEFINE_PROP_LINK("homer", PnvOCC, homer, TYPE_PNV_HOMER, PnvHomer *),
};

static void pnv_occ_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = pnv_occ_realize;
    device_class_set_props(dc, pnv_occ_properties);
    dc->user_creatable = false;
}

static const TypeInfo pnv_occ_type_info = {
    .name          = TYPE_PNV_OCC,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(PnvOCC),
    .class_init    = pnv_occ_class_init,
    .class_size    = sizeof(PnvOCCClass),
    .abstract      = true,
};

static void pnv_occ_register_types(void)
{
    type_register_static(&pnv_occ_type_info);
    type_register_static(&pnv_occ_power8_type_info);
    type_register_static(&pnv_occ_power9_type_info);
    type_register_static(&pnv_occ_power10_type_info);
}

type_init(pnv_occ_register_types);

/* From skiboot/hw/occ.c with tab to space conversion */
/* OCC Communication Area for PStates */

#define OPAL_DYNAMIC_DATA_OFFSET        0x0B80
/* relative to HOMER_OPAL_DATA_OFFSET */

#define MAX_PSTATES                     256
#define MAX_P8_CORES                    12
#define MAX_P9_CORES                    24
#define MAX_P10_CORES                   32

#define MAX_OPAL_CMD_DATA_LENGTH        4090
#define MAX_OCC_RSP_DATA_LENGTH         8698

#define P8_PIR_CORE_MASK                0xFFF8
#define P9_PIR_QUAD_MASK                0xFFF0
#define P10_PIR_CHIP_MASK               0x0000
#define FREQ_MAX_IN_DOMAIN              0
#define FREQ_MOST_RECENTLY_SET          1

#define u8 uint8_t
#define s8 int8_t
#define u16 uint16_t
#define s16 int16_t
#define u32 uint32_t
#define s32 int32_t
#define u64 uint64_t
#define s64 int64_t
#define __be16 uint16_t
#define __be32 uint32_t
#define __packed QEMU_PACKED

/**
 * OCC-OPAL Shared Memory Region
 *
 * Reference document :
 * https://github.com/open-power/docs/blob/master/occ/OCC_OpenPwr_FW_Interfaces.pdf
 *
 * Supported layout versions:
 * - 0x01, 0x02 : P8
 * https://github.com/open-power/occ/blob/master_p8/src/occ/proc/proc_pstate.h
 *
 * - 0x90 : P9
 * https://github.com/open-power/occ/blob/master/src/occ_405/proc/proc_pstate.h
 *   In 0x90 the data is separated into :-
 *   -- Static Data (struct occ_pstate_table): Data is written once by OCC
 *   -- Dynamic Data (struct occ_dynamic_data): Data is updated at runtime
 *
 * struct occ_pstate_table -    Pstate table layout
 * @valid:                      Indicates if data is valid
 * @version:                    Layout version [Major/Minor]
 * @v2.throttle:                Reason for limiting the max pstate
 * @v9.occ_role:                OCC role (Master/Slave)
 * @v#.pstate_min:              Minimum pstate ever allowed
 * @v#.pstate_nom:              Nominal pstate
 * @v#.pstate_turbo:            Maximum turbo pstate
 * @v#.pstate_ultra_turbo:      Maximum ultra turbo pstate and the maximum
 *                              pstate ever allowed
 * @v#.pstates:                 Pstate-id and frequency list from Pmax to Pmin
 * @v#.pstates.id:              Pstate-id
 * @v#.pstates.flags:           Pstate-flag(reserved)
 * @v2.pstates.vdd:             Voltage Identifier
 * @v2.pstates.vcs:             Voltage Identifier
 * @v#.pstates.freq_khz:        Frequency in KHz
 * @v#.core_max[1..N]:          Max pstate with N active cores
 * @spare/reserved/pad:         Unused data
 */
struct occ_pstate_table {
    u8 valid;
    u8 version;
    union __packed {
        struct __packed { /* Version 0x01 and 0x02 */
            u8 throttle;
            s8 pstate_min;
            s8 pstate_nom;
            s8 pstate_turbo;
            s8 pstate_ultra_turbo;
            u8 spare;
            u64 reserved;
            struct __packed {
                s8 id;
                u8 flags;
                u8 vdd;
                u8 vcs;
                __be32 freq_khz;
            } pstates[MAX_PSTATES];
            s8 core_max[MAX_P8_CORES];
            u8 pad[100];
        } v2;
        struct __packed { /* Version 0x90 */
            u8 occ_role;
            u8 pstate_min;
            u8 pstate_nom;
            u8 pstate_turbo;
            u8 pstate_ultra_turbo;
            u8 spare;
            u64 reserved1;
            u64 reserved2;
            struct __packed {
                u8 id;
                u8 flags;
                u16 reserved;
                __be32 freq_khz;
            } pstates[MAX_PSTATES];
            u8 core_max[MAX_P9_CORES];
            u8 pad[56];
        } v9;
        struct __packed { /* Version 0xA0 */
            u8 occ_role;
            u8 pstate_min;
            u8 pstate_fixed_freq;
            u8 pstate_base;
            u8 pstate_ultra_turbo;
            u8 pstate_fmax;
            u8 minor;
            u8 pstate_bottom_throttle;
            u8 spare;
            u8 spare1;
            u32 reserved_32;
            u64 reserved_64;
            struct __packed {
                u8 id;
                u8 valid;
                u16 reserved;
                __be32 freq_khz;
            } pstates[MAX_PSTATES];
            u8 core_max[MAX_P10_CORES];
            u8 pad[48];
        } v10;
    };
} __packed;

/**
 * OPAL-OCC Command Response Interface
 *
 * OPAL-OCC Command Buffer
 *
 * ---------------------------------------------------------------------
 * | OPAL  |  Cmd    | OPAL |          | Cmd Data | Cmd Data | OPAL    |
 * | Cmd   | Request | OCC  | Reserved | Length   | Length   | Cmd     |
 * | Flags |   ID    | Cmd  |          | (MSB)    | (LSB)    | Data... |
 * ---------------------------------------------------------------------
 * |  ….OPAL Command Data up to max of Cmd Data Length 4090 bytes      |
 * |                                                                   |
 * ---------------------------------------------------------------------
 *
 * OPAL Command Flag
 *
 * -----------------------------------------------------------------
 * | Bit 7 | Bit 6 | Bit 5 | Bit 4 | Bit 3 | Bit 2 | Bit 1 | Bit 0 |
 * | (msb) |       |       |       |       |       |       | (lsb) |
 * -----------------------------------------------------------------
 * |Cmd    |       |       |       |       |       |       |       |
 * |Ready  |       |       |       |       |       |       |       |
 * -----------------------------------------------------------------
 *
 * struct opal_command_buffer - Defines the layout of OPAL command buffer
 * @flag:                       Provides general status of the command
 * @request_id:                 Token to identify request
 * @cmd:                        Command sent
 * @data_size:                  Command data length
 * @data:                       Command specific data
 * @spare:                      Unused byte
 */
struct opal_command_buffer {
    u8 flag;
    u8 request_id;
    u8 cmd;
    u8 spare;
    __be16 data_size;
    u8 data[MAX_OPAL_CMD_DATA_LENGTH];
} __packed;

/**
 * OPAL-OCC Response Buffer
 *
 * ---------------------------------------------------------------------
 * | OCC   |  Cmd    | OPAL | Response | Rsp Data | Rsp Data | OPAL    |
 * | Rsp   | Request | OCC  |  Status  | Length   | Length   | Rsp     |
 * | Flags |   ID    | Cmd  |          | (MSB)    | (LSB)    | Data... |
 * ---------------------------------------------------------------------
 * |  ….OPAL Response Data up to max of Rsp Data Length 8698 bytes     |
 * |                                                                   |
 * ---------------------------------------------------------------------
 *
 * OCC Response Flag
 *
 * -----------------------------------------------------------------
 * | Bit 7 | Bit 6 | Bit 5 | Bit 4 | Bit 3 | Bit 2 | Bit 1 | Bit 0 |
 * | (msb) |       |       |       |       |       |       | (lsb) |
 * -----------------------------------------------------------------
 * |       |       |       |       |       |       |OCC in  | Rsp  |
 * |       |       |       |       |       |       |progress|Ready |
 * -----------------------------------------------------------------
 *
 * struct occ_response_buffer - Defines the layout of OCC response buffer
 * @flag:                       Provides general status of the response
 * @request_id:                 Token to identify request
 * @cmd:                        Command requested
 * @status:                     Indicates success/failure status of
 *                              the command
 * @data_size:                  Response data length
 * @data:                       Response specific data
 */
struct occ_response_buffer {
    u8 flag;
    u8 request_id;
    u8 cmd;
    u8 status;
    __be16 data_size;
    u8 data[MAX_OCC_RSP_DATA_LENGTH];
} __packed;

/**
 * OCC-OPAL Shared Memory Interface Dynamic Data Vx90
 *
 * struct occ_dynamic_data -    Contains runtime attributes
 * @occ_state:                  Current state of OCC
 * @major_version:              Major version number
 * @minor_version:              Minor version number (backwards compatible)
 *                              Version 1 indicates GPU presence populated
 * @gpus_present:               Bitmask of GPUs present (on systems where GPU
 *                              presence is detected through APSS)
 * @cpu_throttle:               Reason for limiting the max pstate
 * @mem_throttle:               Reason for throttling memory
 * @quick_pwr_drop:             Indicates if QPD is asserted
 * @pwr_shifting_ratio:         Indicates the current percentage of power to
 *                              take away from the CPU vs GPU when shifting
 *                              power to maintain a power cap. Value of 100
 *                              means take all power from CPU.
 * @pwr_cap_type:               Indicates type of power cap in effect
 * @hard_min_pwr_cap:           Hard minimum system power cap in Watts.
 *                              Guaranteed unless hardware failure
 * @max_pwr_cap:                Maximum allowed system power cap in Watts
 * @cur_pwr_cap:                Current system power cap
 * @soft_min_pwr_cap:           Soft powercap minimum. OCC may or may not be
 *                              able to maintain this
 * @spare/reserved:             Unused data
 * @cmd:                        Opal Command Buffer
 * @rsp:                        OCC Response Buffer
 */
struct occ_dynamic_data {
    u8 occ_state;
    u8 major_version;
    u8 minor_version;
    u8 gpus_present;
    union __packed {
        struct __packed { /* Version 0x90 */
            u8 spare1;
        } v9;
        struct __packed { /* Version 0xA0 */
            u8 wof_enabled;
        } v10;
    };
    u8 cpu_throttle;
    u8 mem_throttle;
    u8 quick_pwr_drop;
    u8 pwr_shifting_ratio;
    u8 pwr_cap_type;
    __be16 hard_min_pwr_cap;
    __be16 max_pwr_cap;
    __be16 cur_pwr_cap;
    __be16 soft_min_pwr_cap;
    u8 pad[110];
    struct opal_command_buffer cmd;
    struct occ_response_buffer rsp;
} __packed;

enum occ_response_status {
    OCC_RSP_SUCCESS                 = 0x00,
    OCC_RSP_INVALID_COMMAND         = 0x11,
    OCC_RSP_INVALID_CMD_DATA_LENGTH = 0x12,
    OCC_RSP_INVALID_DATA            = 0x13,
    OCC_RSP_INTERNAL_ERROR          = 0x15,
};

#define OCC_ROLE_SLAVE                  0x00
#define OCC_ROLE_MASTER                 0x01

#define OCC_FLAG_RSP_READY              0x01
#define OCC_FLAG_CMD_IN_PROGRESS        0x02
#define OPAL_FLAG_CMD_READY             0x80

#define PCAP_MAX_POWER_W                100
#define PCAP_SOFT_MIN_POWER_W            20
#define PCAP_HARD_MIN_POWER_W            10

static bool occ_write_static_data(PnvOCC *occ,
                                 struct occ_pstate_table *static_data,
                                 Error **errp)
{
    PnvOCCClass *poc = PNV_OCC_GET_CLASS(occ);
    PnvHomer *homer = occ->homer;
    hwaddr static_addr = homer->base + poc->opal_shared_memory_offset;
    MemTxResult ret;

    ret = address_space_write(&address_space_memory, static_addr,
                             MEMTXATTRS_UNSPECIFIED, static_data,
                             sizeof(*static_data));
    if (ret != MEMTX_OK) {
        error_setg(errp, "OCC: cannot write OCC-OPAL static data");
        return false;
    }

    return true;
}

static bool occ_write_dynamic_data(PnvOCC *occ,
                                  struct occ_dynamic_data *dynamic_data,
                                  Error **errp)
{
    PnvOCCClass *poc = PNV_OCC_GET_CLASS(occ);
    PnvHomer *homer = occ->homer;
    hwaddr static_addr = homer->base + poc->opal_shared_memory_offset;
    hwaddr dynamic_addr = static_addr + OPAL_DYNAMIC_DATA_OFFSET;
    MemTxResult ret;

    ret = address_space_write(&address_space_memory, dynamic_addr,
                             MEMTXATTRS_UNSPECIFIED, dynamic_data,
                             sizeof(*dynamic_data));
    if (ret != MEMTX_OK) {
        error_setg(errp, "OCC: cannot write OCC-OPAL dynamic data");
        return false;
    }

    return true;
}

static bool occ_init_homer_memory(PnvOCC *occ, Error **errp)
{
    PnvOCCClass *poc = PNV_OCC_GET_CLASS(occ);
    PnvHomer *homer = occ->homer;
    PnvChip *chip = homer->chip;
    struct occ_pstate_table static_data;
    struct occ_dynamic_data dynamic_data;
    int i;

    memset(&static_data, 0, sizeof(static_data));
    static_data.valid = 1;
    static_data.version = poc->opal_shared_memory_version;
    switch (poc->opal_shared_memory_version) {
    case 0x02:
        static_data.v2.throttle = 0;
        static_data.v2.pstate_min = -2;
        static_data.v2.pstate_nom = -1;
        static_data.v2.pstate_turbo = -1;
        static_data.v2.pstate_ultra_turbo = 0;
        static_data.v2.pstates[0].id = 0;
        static_data.v2.pstates[1].freq_khz = cpu_to_be32(4000000);
        static_data.v2.pstates[1].id = -1;
        static_data.v2.pstates[1].freq_khz = cpu_to_be32(3000000);
        static_data.v2.pstates[2].id = -2;
        static_data.v2.pstates[2].freq_khz = cpu_to_be32(2000000);
        for (i = 0; i < chip->nr_cores; i++) {
            static_data.v2.core_max[i] = 1;
        }
        break;
    case 0x90:
        if (chip->chip_id == 0) {
            static_data.v9.occ_role = OCC_ROLE_MASTER;
        } else {
            static_data.v9.occ_role = OCC_ROLE_SLAVE;
        }
        static_data.v9.pstate_min = 2;
        static_data.v9.pstate_nom = 1;
        static_data.v9.pstate_turbo = 1;
        static_data.v9.pstate_ultra_turbo = 0;
        static_data.v9.pstates[0].id = 0;
        static_data.v9.pstates[0].freq_khz = cpu_to_be32(4000000);
        static_data.v9.pstates[1].id = 1;
        static_data.v9.pstates[1].freq_khz = cpu_to_be32(3000000);
        static_data.v9.pstates[2].id = 2;
        static_data.v9.pstates[2].freq_khz = cpu_to_be32(2000000);
        for (i = 0; i < chip->nr_cores; i++) {
            static_data.v9.core_max[i] = 1;
        }
        break;
    default:
        g_assert_not_reached();
    }
    if (!occ_write_static_data(occ, &static_data, errp)) {
        return false;
    }

    memset(&dynamic_data, 0, sizeof(dynamic_data));
    dynamic_data.occ_state = 0x3; /* active */
    dynamic_data.major_version = 0x0;
    dynamic_data.hard_min_pwr_cap = cpu_to_be16(PCAP_HARD_MIN_POWER_W);
    dynamic_data.max_pwr_cap = cpu_to_be16(PCAP_MAX_POWER_W);
    dynamic_data.cur_pwr_cap = cpu_to_be16(PCAP_MAX_POWER_W);
    dynamic_data.soft_min_pwr_cap = cpu_to_be16(PCAP_SOFT_MIN_POWER_W);
    switch (poc->opal_shared_memory_version) {
    case 0x90:
        dynamic_data.minor_version = 0x1;
        break;
    case 0x02:
        dynamic_data.minor_version = 0x0;
        break;
    default:
        g_assert_not_reached();
    }
    if (!occ_write_dynamic_data(occ, &dynamic_data, errp)) {
        return false;
    }

    return true;
}
