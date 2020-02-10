/*
 * QEMU PAPR Storage Class Memory Interfaces
 *
 * Copyright (c) 2019-2020, IBM Corporation.
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
#include "qapi/error.h"
#include "hw/ppc/spapr_drc.h"
#include "hw/ppc/spapr_nvdimm.h"
#include "hw/mem/nvdimm.h"
#include "qemu/nvdimm-utils.h"
#include "hw/ppc/fdt.h"

void spapr_nvdimm_validate_opts(NVDIMMDevice *nvdimm, uint64_t size,
                                Error **errp)
{
    char *uuidstr = NULL;
    QemuUUID uuid;

    if (size % SPAPR_MINIMUM_SCM_BLOCK_SIZE) {
        error_setg(errp, "NVDIMM memory size excluding the label area"
                   " must be a multiple of %" PRIu64 "MB",
                   SPAPR_MINIMUM_SCM_BLOCK_SIZE / MiB);
        return;
    }

    uuidstr = object_property_get_str(OBJECT(nvdimm), NVDIMM_UUID_PROP, NULL);
    qemu_uuid_parse(uuidstr, &uuid);
    g_free(uuidstr);

    if (qemu_uuid_is_null(&uuid)) {
        error_setg(errp, "NVDIMM device requires the uuid to be set");
        return;
    }
}


void spapr_add_nvdimm(DeviceState *dev, uint64_t slot, Error **errp)
{
    SpaprDrc *drc;
    bool hotplugged = spapr_drc_hotplugged(dev);
    Error *local_err = NULL;

    drc = spapr_drc_by_id(TYPE_SPAPR_DRC_PMEM, slot);
    g_assert(drc);

    spapr_drc_attach(drc, dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    if (hotplugged) {
        spapr_hotplug_req_add_by_index(drc);
    }
}

int spapr_pmem_dt_populate(SpaprDrc *drc, SpaprMachineState *spapr,
                           void *fdt, int *fdt_start_offset, Error **errp)
{
    NVDIMMDevice *nvdimm = NVDIMM(drc->dev);

    *fdt_start_offset = spapr_dt_nvdimm(fdt, 0, nvdimm);

    return 0;
}

void spapr_create_nvdimm_dr_connectors(SpaprMachineState *spapr)
{
    MachineState *machine = MACHINE(spapr);
    int i;

    for (i = 0; i < machine->ram_slots; i++) {
        spapr_dr_connector_new(OBJECT(spapr), TYPE_SPAPR_DRC_PMEM, i);
    }
}


int spapr_dt_nvdimm(void *fdt, int parent_offset,
                           NVDIMMDevice *nvdimm)
{
    int child_offset;
    char *buf;
    SpaprDrc *drc;
    uint32_t drc_idx;
    uint32_t node = object_property_get_uint(OBJECT(nvdimm), PC_DIMM_NODE_PROP,
                                             &error_abort);
    uint64_t slot = object_property_get_uint(OBJECT(nvdimm), PC_DIMM_SLOT_PROP,
                                             &error_abort);
    uint32_t associativity[] = {
        cpu_to_be32(0x4), /* length */
        cpu_to_be32(0x0), cpu_to_be32(0x0),
        cpu_to_be32(0x0), cpu_to_be32(node)
    };
    uint64_t lsize = nvdimm->label_size;
    uint64_t size = object_property_get_int(OBJECT(nvdimm), PC_DIMM_SIZE_PROP,
                                            NULL);

    drc = spapr_drc_by_id(TYPE_SPAPR_DRC_PMEM, slot);
    g_assert(drc);

    drc_idx = spapr_drc_index(drc);

    buf = g_strdup_printf("ibm,pmemory@%x", drc_idx);
    child_offset = fdt_add_subnode(fdt, parent_offset, buf);
    g_free(buf);

    _FDT(child_offset);

    _FDT((fdt_setprop_cell(fdt, child_offset, "reg", drc_idx)));
    _FDT((fdt_setprop_string(fdt, child_offset, "compatible", "ibm,pmemory")));
    _FDT((fdt_setprop_string(fdt, child_offset, "device_type", "ibm,pmemory")));

    _FDT((fdt_setprop(fdt, child_offset, "ibm,associativity", associativity,
                      sizeof(associativity))));

    buf = qemu_uuid_unparse_strdup(&nvdimm->uuid);
    _FDT((fdt_setprop_string(fdt, child_offset, "ibm,unit-guid", buf)));
    g_free(buf);

    _FDT((fdt_setprop_cell(fdt, child_offset, "ibm,my-drc-index", drc_idx)));

    _FDT((fdt_setprop_u64(fdt, child_offset, "ibm,block-size",
                          SPAPR_MINIMUM_SCM_BLOCK_SIZE)));
    _FDT((fdt_setprop_u64(fdt, child_offset, "ibm,number-of-blocks",
                          size / SPAPR_MINIMUM_SCM_BLOCK_SIZE)));
    _FDT((fdt_setprop_cell(fdt, child_offset, "ibm,metadata-size", lsize)));

    _FDT((fdt_setprop_string(fdt, child_offset, "ibm,pmem-application",
                             "operating-system")));
    _FDT(fdt_setprop(fdt, child_offset, "ibm,cache-flush-required", NULL, 0));

    return child_offset;
}

void spapr_dt_persistent_memory(void *fdt)
{
    int offset = fdt_subnode_offset(fdt, 0, "persistent-memory");
    GSList *iter, *nvdimms = nvdimm_get_device_list();

    if (offset < 0) {
        offset = fdt_add_subnode(fdt, 0, "persistent-memory");
        _FDT(offset);
        _FDT((fdt_setprop_cell(fdt, offset, "#address-cells", 0x1)));
        _FDT((fdt_setprop_cell(fdt, offset, "#size-cells", 0x0)));
        _FDT((fdt_setprop_string(fdt, offset, "device_type",
                                 "ibm,persistent-memory")));
    }

    /* Create DT entries for cold plugged NVDIMM devices */
    for (iter = nvdimms; iter; iter = iter->next) {
        NVDIMMDevice *nvdimm = iter->data;

        spapr_dt_nvdimm(fdt, offset, nvdimm);
    }
    g_slist_free(nvdimms);

    return;
}
