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
#include "qemu/range.h"
#include "hw/ppc/spapr_numa.h"

bool spapr_nvdimm_validate(HotplugHandler *hotplug_dev, NVDIMMDevice *nvdimm,
                           uint64_t size, Error **errp)
{
    const MachineClass *mc = MACHINE_GET_CLASS(hotplug_dev);
    const MachineState *ms = MACHINE(hotplug_dev);
    g_autofree char *uuidstr = NULL;
    QemuUUID uuid;
    int ret;

    if (!mc->nvdimm_supported) {
        error_setg(errp, "NVDIMM hotplug not supported for this machine");
        return false;
    }

    if (!ms->nvdimms_state->is_enabled) {
        error_setg(errp, "nvdimm device found but 'nvdimm=off' was set");
        return false;
    }

    if (object_property_get_int(OBJECT(nvdimm), NVDIMM_LABEL_SIZE_PROP,
                                &error_abort) == 0) {
        error_setg(errp, "PAPR requires NVDIMM devices to have label-size set");
        return false;
    }

    if (size % SPAPR_MINIMUM_SCM_BLOCK_SIZE) {
        error_setg(errp, "PAPR requires NVDIMM memory size (excluding label)"
                   " to be a multiple of %" PRIu64 "MB",
                   SPAPR_MINIMUM_SCM_BLOCK_SIZE / MiB);
        return false;
    }

    uuidstr = object_property_get_str(OBJECT(nvdimm), NVDIMM_UUID_PROP,
                                      &error_abort);
    ret = qemu_uuid_parse(uuidstr, &uuid);
    g_assert(!ret);

    if (qemu_uuid_is_null(&uuid)) {
        error_setg(errp, "NVDIMM device requires the uuid to be set");
        return false;
    }

    return true;
}


void spapr_add_nvdimm(DeviceState *dev, uint64_t slot)
{
    SpaprDrc *drc;
    bool hotplugged = spapr_drc_hotplugged(dev);

    drc = spapr_drc_by_id(TYPE_SPAPR_DRC_PMEM, slot);
    g_assert(drc);

    /*
     * pc_dimm_get_free_slot() provided a free slot at pre-plug. The
     * corresponding DRC is thus assumed to be attachable.
     */
    spapr_drc_attach(drc, dev);

    if (hotplugged) {
        spapr_hotplug_req_add_by_index(drc);
    }
}

static int spapr_dt_nvdimm(SpaprMachineState *spapr, void *fdt,
                           int parent_offset, NVDIMMDevice *nvdimm)
{
    int child_offset;
    char *buf;
    SpaprDrc *drc;
    uint32_t drc_idx;
    uint32_t node = object_property_get_uint(OBJECT(nvdimm), PC_DIMM_NODE_PROP,
                                             &error_abort);
    uint64_t slot = object_property_get_uint(OBJECT(nvdimm), PC_DIMM_SLOT_PROP,
                                             &error_abort);
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

    spapr_numa_write_associativity_dt(spapr, fdt, child_offset, node);

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

int spapr_pmem_dt_populate(SpaprDrc *drc, SpaprMachineState *spapr,
                           void *fdt, int *fdt_start_offset, Error **errp)
{
    NVDIMMDevice *nvdimm = NVDIMM(drc->dev);

    *fdt_start_offset = spapr_dt_nvdimm(spapr, fdt, 0, nvdimm);

    return 0;
}

void spapr_dt_persistent_memory(SpaprMachineState *spapr, void *fdt)
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

        spapr_dt_nvdimm(spapr, fdt, offset, nvdimm);
    }
    g_slist_free(nvdimms);

    return;
}

static target_ulong h_scm_read_metadata(PowerPCCPU *cpu,
                                        SpaprMachineState *spapr,
                                        target_ulong opcode,
                                        target_ulong *args)
{
    uint32_t drc_index = args[0];
    uint64_t offset = args[1];
    uint64_t len = args[2];
    SpaprDrc *drc = spapr_drc_by_index(drc_index);
    NVDIMMDevice *nvdimm;
    NVDIMMClass *ddc;
    uint64_t data = 0;
    uint8_t buf[8] = { 0 };

    if (!drc || !drc->dev ||
        spapr_drc_type(drc) != SPAPR_DR_CONNECTOR_TYPE_PMEM) {
        return H_PARAMETER;
    }

    if (len != 1 && len != 2 &&
        len != 4 && len != 8) {
        return H_P3;
    }

    nvdimm = NVDIMM(drc->dev);
    if ((offset + len < offset) ||
        (nvdimm->label_size < len + offset)) {
        return H_P2;
    }

    ddc = NVDIMM_GET_CLASS(nvdimm);
    ddc->read_label_data(nvdimm, buf, len, offset);

    switch (len) {
    case 1:
        data = ldub_p(buf);
        break;
    case 2:
        data = lduw_be_p(buf);
        break;
    case 4:
        data = ldl_be_p(buf);
        break;
    case 8:
        data = ldq_be_p(buf);
        break;
    default:
        g_assert_not_reached();
    }

    args[0] = data;

    return H_SUCCESS;
}

static target_ulong h_scm_write_metadata(PowerPCCPU *cpu,
                                         SpaprMachineState *spapr,
                                         target_ulong opcode,
                                         target_ulong *args)
{
    uint32_t drc_index = args[0];
    uint64_t offset = args[1];
    uint64_t data = args[2];
    uint64_t len = args[3];
    SpaprDrc *drc = spapr_drc_by_index(drc_index);
    NVDIMMDevice *nvdimm;
    NVDIMMClass *ddc;
    uint8_t buf[8] = { 0 };

    if (!drc || !drc->dev ||
        spapr_drc_type(drc) != SPAPR_DR_CONNECTOR_TYPE_PMEM) {
        return H_PARAMETER;
    }

    if (len != 1 && len != 2 &&
        len != 4 && len != 8) {
        return H_P4;
    }

    nvdimm = NVDIMM(drc->dev);
    if ((offset + len < offset) ||
        (nvdimm->label_size < len + offset)) {
        return H_P2;
    }

    switch (len) {
    case 1:
        if (data & 0xffffffffffffff00) {
            return H_P2;
        }
        stb_p(buf, data);
        break;
    case 2:
        if (data & 0xffffffffffff0000) {
            return H_P2;
        }
        stw_be_p(buf, data);
        break;
    case 4:
        if (data & 0xffffffff00000000) {
            return H_P2;
        }
        stl_be_p(buf, data);
        break;
    case 8:
        stq_be_p(buf, data);
        break;
    default:
            g_assert_not_reached();
    }

    ddc = NVDIMM_GET_CLASS(nvdimm);
    ddc->write_label_data(nvdimm, buf, len, offset);

    return H_SUCCESS;
}

static target_ulong h_scm_bind_mem(PowerPCCPU *cpu, SpaprMachineState *spapr,
                                   target_ulong opcode, target_ulong *args)
{
    uint32_t drc_index = args[0];
    uint64_t starting_idx = args[1];
    uint64_t no_of_scm_blocks_to_bind = args[2];
    uint64_t target_logical_mem_addr = args[3];
    uint64_t continue_token = args[4];
    uint64_t size;
    uint64_t total_no_of_scm_blocks;
    SpaprDrc *drc = spapr_drc_by_index(drc_index);
    hwaddr addr;
    NVDIMMDevice *nvdimm;

    if (!drc || !drc->dev ||
        spapr_drc_type(drc) != SPAPR_DR_CONNECTOR_TYPE_PMEM) {
        return H_PARAMETER;
    }

    /*
     * Currently continue token should be zero qemu has already bound
     * everything and this hcall doesnt return H_BUSY.
     */
    if (continue_token > 0) {
        return H_P5;
    }

    /* Currently qemu assigns the address. */
    if (target_logical_mem_addr != 0xffffffffffffffff) {
        return H_OVERLAP;
    }

    nvdimm = NVDIMM(drc->dev);

    size = object_property_get_uint(OBJECT(nvdimm),
                                    PC_DIMM_SIZE_PROP, &error_abort);

    total_no_of_scm_blocks = size / SPAPR_MINIMUM_SCM_BLOCK_SIZE;

    if (starting_idx > total_no_of_scm_blocks) {
        return H_P2;
    }

    if (((starting_idx + no_of_scm_blocks_to_bind) < starting_idx) ||
        ((starting_idx + no_of_scm_blocks_to_bind) > total_no_of_scm_blocks)) {
        return H_P3;
    }

    addr = object_property_get_uint(OBJECT(nvdimm),
                                    PC_DIMM_ADDR_PROP, &error_abort);

    addr += starting_idx * SPAPR_MINIMUM_SCM_BLOCK_SIZE;

    /* Already bound, Return target logical address in R5 */
    args[1] = addr;
    args[2] = no_of_scm_blocks_to_bind;

    return H_SUCCESS;
}

static target_ulong h_scm_unbind_mem(PowerPCCPU *cpu, SpaprMachineState *spapr,
                                     target_ulong opcode, target_ulong *args)
{
    uint32_t drc_index = args[0];
    uint64_t starting_scm_logical_addr = args[1];
    uint64_t no_of_scm_blocks_to_unbind = args[2];
    uint64_t continue_token = args[3];
    uint64_t size_to_unbind;
    Range blockrange = range_empty;
    Range nvdimmrange = range_empty;
    SpaprDrc *drc = spapr_drc_by_index(drc_index);
    NVDIMMDevice *nvdimm;
    uint64_t size, addr;

    if (!drc || !drc->dev ||
        spapr_drc_type(drc) != SPAPR_DR_CONNECTOR_TYPE_PMEM) {
        return H_PARAMETER;
    }

    /* continue_token should be zero as this hcall doesn't return H_BUSY. */
    if (continue_token > 0) {
        return H_P4;
    }

    /* Check if starting_scm_logical_addr is block aligned */
    if (!QEMU_IS_ALIGNED(starting_scm_logical_addr,
                         SPAPR_MINIMUM_SCM_BLOCK_SIZE)) {
        return H_P2;
    }

    size_to_unbind = no_of_scm_blocks_to_unbind * SPAPR_MINIMUM_SCM_BLOCK_SIZE;
    if (no_of_scm_blocks_to_unbind == 0 || no_of_scm_blocks_to_unbind !=
                               size_to_unbind / SPAPR_MINIMUM_SCM_BLOCK_SIZE) {
        return H_P3;
    }

    nvdimm = NVDIMM(drc->dev);
    size = object_property_get_int(OBJECT(nvdimm), PC_DIMM_SIZE_PROP,
                                   &error_abort);
    addr = object_property_get_int(OBJECT(nvdimm), PC_DIMM_ADDR_PROP,
                                   &error_abort);

    range_init_nofail(&nvdimmrange, addr, size);
    range_init_nofail(&blockrange, starting_scm_logical_addr, size_to_unbind);

    if (!range_contains_range(&nvdimmrange, &blockrange)) {
        return H_P3;
    }

    args[1] = no_of_scm_blocks_to_unbind;

    /* let unplug take care of actual unbind */
    return H_SUCCESS;
}

#define H_UNBIND_SCOPE_ALL 0x1
#define H_UNBIND_SCOPE_DRC 0x2

static target_ulong h_scm_unbind_all(PowerPCCPU *cpu, SpaprMachineState *spapr,
                                     target_ulong opcode, target_ulong *args)
{
    uint64_t target_scope = args[0];
    uint32_t drc_index = args[1];
    uint64_t continue_token = args[2];
    NVDIMMDevice *nvdimm;
    uint64_t size;
    uint64_t no_of_scm_blocks_unbound = 0;

    /* continue_token should be zero as this hcall doesn't return H_BUSY. */
    if (continue_token > 0) {
        return H_P4;
    }

    if (target_scope == H_UNBIND_SCOPE_DRC) {
        SpaprDrc *drc = spapr_drc_by_index(drc_index);

        if (!drc || !drc->dev ||
            spapr_drc_type(drc) != SPAPR_DR_CONNECTOR_TYPE_PMEM) {
            return H_P2;
        }

        nvdimm = NVDIMM(drc->dev);
        size = object_property_get_int(OBJECT(nvdimm), PC_DIMM_SIZE_PROP,
                                       &error_abort);

        no_of_scm_blocks_unbound = size / SPAPR_MINIMUM_SCM_BLOCK_SIZE;
    } else if (target_scope ==  H_UNBIND_SCOPE_ALL) {
        GSList *list, *nvdimms;

        nvdimms = nvdimm_get_device_list();
        for (list = nvdimms; list; list = list->next) {
            nvdimm = list->data;
            size = object_property_get_int(OBJECT(nvdimm), PC_DIMM_SIZE_PROP,
                                           &error_abort);

            no_of_scm_blocks_unbound += size / SPAPR_MINIMUM_SCM_BLOCK_SIZE;
        }
        g_slist_free(nvdimms);
    } else {
        return H_PARAMETER;
    }

    args[1] = no_of_scm_blocks_unbound;

    /* let unplug take care of actual unbind */
    return H_SUCCESS;
}

static void spapr_scm_register_types(void)
{
    /* qemu/scm specific hcalls */
    spapr_register_hypercall(H_SCM_READ_METADATA, h_scm_read_metadata);
    spapr_register_hypercall(H_SCM_WRITE_METADATA, h_scm_write_metadata);
    spapr_register_hypercall(H_SCM_BIND_MEM, h_scm_bind_mem);
    spapr_register_hypercall(H_SCM_UNBIND_MEM, h_scm_unbind_mem);
    spapr_register_hypercall(H_SCM_UNBIND_ALL, h_scm_unbind_all);
}

type_init(spapr_scm_register_types)
