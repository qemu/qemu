/*
 * QEMU PowerNV, BMC related functions
 *
 * Copyright (c) 2016-2017, IBM Corporation.
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
#include "qemu-common.h"
#include "qapi/error.h"
#include "target/ppc/cpu.h"
#include "qemu/log.h"
#include "hw/ipmi/ipmi.h"
#include "hw/ppc/fdt.h"

#include "hw/ppc/pnv.h"

#include <libfdt.h>

/* TODO: include definition in ipmi.h */
#define IPMI_SDR_FULL_TYPE 1

/*
 * OEM SEL Event data packet sent by BMC in response of a Read Event
 * Message Buffer command
 */
typedef struct OemSel {
    /* SEL header */
    uint8_t id[2];
    uint8_t type;
    uint8_t timestamp[4];
    uint8_t manuf_id[3];

    /* OEM SEL data (6 bytes) follows */
    uint8_t netfun;
    uint8_t cmd;
    uint8_t data[4];
} OemSel;

#define SOFT_OFF        0x00
#define SOFT_REBOOT     0x01

static void pnv_gen_oem_sel(IPMIBmc *bmc, uint8_t reboot)
{
    /* IPMI SEL Event are 16 bytes long */
    OemSel sel = {
        .id        = { 0x55 , 0x55 },
        .type      = 0xC0, /* OEM */
        .manuf_id  = { 0x0, 0x0, 0x0 },
        .timestamp = { 0x0, 0x0, 0x0, 0x0 },
        .netfun    = 0x3A, /* IBM */
        .cmd       = 0x04, /* AMI OEM SEL Power Notification */
        .data      = { reboot, 0xFF, 0xFF, 0xFF },
    };

    ipmi_bmc_gen_event(bmc, (uint8_t *) &sel, 0 /* do not log the event */);
}

void pnv_bmc_powerdown(IPMIBmc *bmc)
{
    pnv_gen_oem_sel(bmc, SOFT_OFF);
}

void pnv_dt_bmc_sensors(IPMIBmc *bmc, void *fdt)
{
    int offset;
    int i;
    const struct ipmi_sdr_compact *sdr;
    uint16_t nextrec;

    offset = fdt_add_subnode(fdt, 0, "bmc");
    _FDT(offset);

    _FDT((fdt_setprop_string(fdt, offset, "name", "bmc")));
    offset = fdt_add_subnode(fdt, offset, "sensors");
    _FDT(offset);

    _FDT((fdt_setprop_cell(fdt, offset, "#address-cells", 0x1)));
    _FDT((fdt_setprop_cell(fdt, offset, "#size-cells", 0x0)));

    for (i = 0; !ipmi_bmc_sdr_find(bmc, i, &sdr, &nextrec); i++) {
        int off;
        char *name;

        if (sdr->header.rec_type != IPMI_SDR_COMPACT_TYPE &&
            sdr->header.rec_type != IPMI_SDR_FULL_TYPE) {
            continue;
        }

        name = g_strdup_printf("sensor@%x", sdr->sensor_owner_number);
        off = fdt_add_subnode(fdt, offset, name);
        _FDT(off);
        g_free(name);

        _FDT((fdt_setprop_cell(fdt, off, "reg", sdr->sensor_owner_number)));
        _FDT((fdt_setprop_string(fdt, off, "name", "sensor")));
        _FDT((fdt_setprop_string(fdt, off, "compatible", "ibm,ipmi-sensor")));
        _FDT((fdt_setprop_cell(fdt, off, "ipmi-sensor-reading-type",
                               sdr->reading_type)));
        _FDT((fdt_setprop_cell(fdt, off, "ipmi-entity-id",
                               sdr->entity_id)));
        _FDT((fdt_setprop_cell(fdt, off, "ipmi-entity-instance",
                               sdr->entity_instance)));
        _FDT((fdt_setprop_cell(fdt, off, "ipmi-sensor-type",
                               sdr->sensor_type)));
    }
}

/*
 * HIOMAP protocol handler
 */
#define HIOMAP_C_RESET                  1
#define HIOMAP_C_GET_INFO               2
#define HIOMAP_C_GET_FLASH_INFO         3
#define HIOMAP_C_CREATE_READ_WINDOW     4
#define HIOMAP_C_CLOSE_WINDOW           5
#define HIOMAP_C_CREATE_WRITE_WINDOW    6
#define HIOMAP_C_MARK_DIRTY             7
#define HIOMAP_C_FLUSH                  8
#define HIOMAP_C_ACK                    9
#define HIOMAP_C_ERASE                  10
#define HIOMAP_C_DEVICE_NAME            11
#define HIOMAP_C_LOCK                   12

#define BLOCK_SHIFT                     12 /* 4K */

static uint16_t bytes_to_blocks(uint32_t bytes)
{
    return bytes >> BLOCK_SHIFT;
}

static void hiomap_cmd(IPMIBmcSim *ibs, uint8_t *cmd, unsigned int cmd_len,
                       RspBuffer *rsp)
{
    PnvPnor *pnor = PNV_PNOR(object_property_get_link(OBJECT(ibs), "pnor",
                                                      &error_abort));
    uint32_t pnor_size = pnor->size;
    uint32_t pnor_addr = PNOR_SPI_OFFSET;
    bool readonly = false;

    rsp_buffer_push(rsp, cmd[2]);
    rsp_buffer_push(rsp, cmd[3]);

    switch (cmd[2]) {
    case HIOMAP_C_MARK_DIRTY:
    case HIOMAP_C_FLUSH:
    case HIOMAP_C_ERASE:
    case HIOMAP_C_ACK:
        break;

    case HIOMAP_C_GET_INFO:
        rsp_buffer_push(rsp, 2);  /* Version 2 */
        rsp_buffer_push(rsp, BLOCK_SHIFT); /* block size */
        rsp_buffer_push(rsp, 0);  /* Timeout */
        rsp_buffer_push(rsp, 0);  /* Timeout */
        break;

    case HIOMAP_C_GET_FLASH_INFO:
        rsp_buffer_push(rsp, bytes_to_blocks(pnor_size) & 0xFF);
        rsp_buffer_push(rsp, bytes_to_blocks(pnor_size) >> 8);
        rsp_buffer_push(rsp, 0x01);  /* erase size */
        rsp_buffer_push(rsp, 0x00);  /* erase size */
        break;

    case HIOMAP_C_CREATE_READ_WINDOW:
        readonly = true;
        /* Fall through */

    case HIOMAP_C_CREATE_WRITE_WINDOW:
        memory_region_set_readonly(&pnor->mmio, readonly);
        memory_region_set_enabled(&pnor->mmio, true);

        rsp_buffer_push(rsp, bytes_to_blocks(pnor_addr) & 0xFF);
        rsp_buffer_push(rsp, bytes_to_blocks(pnor_addr) >> 8);
        rsp_buffer_push(rsp, bytes_to_blocks(pnor_size) & 0xFF);
        rsp_buffer_push(rsp, bytes_to_blocks(pnor_size) >> 8);
        rsp_buffer_push(rsp, 0x00); /* offset */
        rsp_buffer_push(rsp, 0x00); /* offset */
        break;

    case HIOMAP_C_CLOSE_WINDOW:
        memory_region_set_enabled(&pnor->mmio, false);
        break;

    case HIOMAP_C_DEVICE_NAME:
    case HIOMAP_C_RESET:
    case HIOMAP_C_LOCK:
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "HIOMAP: unknow command %02X\n", cmd[2]);
        break;
    }
}

#define HIOMAP   0x5a

static const IPMICmdHandler hiomap_cmds[] = {
    [HIOMAP] = { hiomap_cmd, 3 },
};

static const IPMINetfn hiomap_netfn = {
    .cmd_nums = ARRAY_SIZE(hiomap_cmds),
    .cmd_handlers = hiomap_cmds
};


void pnv_bmc_set_pnor(IPMIBmc *bmc, PnvPnor *pnor)
{
    object_ref(OBJECT(pnor));
    object_property_add_const_link(OBJECT(bmc), "pnor", OBJECT(pnor),
                                   &error_abort);

    /* Install the HIOMAP protocol handlers to access the PNOR */
    ipmi_sim_register_netfn(IPMI_BMC_SIMULATOR(bmc), IPMI_NETFN_OEM,
                            &hiomap_netfn);
}

/*
 * Instantiate the machine BMC. PowerNV uses the QEMU internal
 * simulator but it could also be external.
 */
IPMIBmc *pnv_bmc_create(PnvPnor *pnor)
{
    Object *obj;

    obj = object_new(TYPE_IPMI_BMC_SIMULATOR);
    object_ref(OBJECT(pnor));
    object_property_add_const_link(obj, "pnor", OBJECT(pnor), &error_abort);
    object_property_set_bool(obj, true, "realized", &error_fatal);

    /* Install the HIOMAP protocol handlers to access the PNOR */
    ipmi_sim_register_netfn(IPMI_BMC_SIMULATOR(obj), IPMI_NETFN_OEM,
                            &hiomap_netfn);

    return IPMI_BMC(obj);
}

typedef struct ForeachArgs {
    const char *name;
    Object *obj;
} ForeachArgs;

static int bmc_find(Object *child, void *opaque)
{
    ForeachArgs *args = opaque;

    if (object_dynamic_cast(child, args->name)) {
        if (args->obj) {
            return 1;
        }
        args->obj = child;
    }
    return 0;
}

IPMIBmc *pnv_bmc_find(Error **errp)
{
    ForeachArgs args = { TYPE_IPMI_BMC_SIMULATOR, NULL };
    int ret;

    ret = object_child_foreach_recursive(object_get_root(), bmc_find, &args);
    if (ret) {
        error_setg(errp, "machine should have only one BMC device. "
                   "Use '-nodefaults'");
        return NULL;
    }

    return args.obj ? IPMI_BMC(args.obj) : NULL;
}
