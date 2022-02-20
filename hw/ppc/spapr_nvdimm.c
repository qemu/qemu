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
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "hw/ppc/spapr_drc.h"
#include "hw/ppc/spapr_nvdimm.h"
#include "hw/mem/nvdimm.h"
#include "qemu/nvdimm-utils.h"
#include "hw/ppc/fdt.h"
#include "qemu/range.h"
#include "hw/ppc/spapr_numa.h"
#include "block/thread-pool.h"
#include "migration/vmstate.h"
#include "qemu/pmem.h"
#include "hw/qdev-properties.h"

/* DIMM health bitmap bitmap indicators. Taken from kernel's papr_scm.c */
/* SCM device is unable to persist memory contents */
#define PAPR_PMEM_UNARMED PPC_BIT(0)

/*
 * The nvdimm size should be aligned to SCM block size.
 * The SCM block size should be aligned to SPAPR_MEMORY_BLOCK_SIZE
 * in order to have SCM regions not to overlap with dimm memory regions.
 * The SCM devices can have variable block sizes. For now, fixing the
 * block size to the minimum value.
 */
#define SPAPR_MINIMUM_SCM_BLOCK_SIZE SPAPR_MEMORY_BLOCK_SIZE

/* Have an explicit check for alignment */
QEMU_BUILD_BUG_ON(SPAPR_MINIMUM_SCM_BLOCK_SIZE % SPAPR_MEMORY_BLOCK_SIZE);

#define TYPE_SPAPR_NVDIMM "spapr-nvdimm"
OBJECT_DECLARE_TYPE(SpaprNVDIMMDevice, SPAPRNVDIMMClass, SPAPR_NVDIMM)

struct SPAPRNVDIMMClass {
    /* private */
    NVDIMMClass parent_class;

    /* public */
    void (*realize)(NVDIMMDevice *dimm, Error **errp);
    void (*unrealize)(NVDIMMDevice *dimm, Error **errp);
};

bool spapr_nvdimm_validate(HotplugHandler *hotplug_dev, NVDIMMDevice *nvdimm,
                           uint64_t size, Error **errp)
{
    const MachineClass *mc = MACHINE_GET_CLASS(hotplug_dev);
    const MachineState *ms = MACHINE(hotplug_dev);
    PCDIMMDevice *dimm = PC_DIMM(nvdimm);
    MemoryRegion *mr = host_memory_backend_get_memory(dimm->hostmem);
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

    if (object_dynamic_cast(OBJECT(nvdimm), TYPE_SPAPR_NVDIMM) &&
        (memory_region_get_fd(mr) < 0)) {
        error_setg(errp, "spapr-nvdimm device requires the "
                   "memdev %s to be of memory-backend-file type",
                   object_get_canonical_path_component(OBJECT(dimm->hostmem)));
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

    if (object_dynamic_cast(OBJECT(nvdimm), TYPE_SPAPR_NVDIMM)) {
        bool is_pmem = false, pmem_override = false;
        PCDIMMDevice *dimm = PC_DIMM(nvdimm);
        HostMemoryBackend *hostmem = dimm->hostmem;

        is_pmem = object_property_get_bool(OBJECT(hostmem), "pmem", NULL);
        pmem_override = object_property_get_bool(OBJECT(nvdimm),
                                                 "pmem-override", NULL);
        if (!is_pmem || pmem_override) {
            _FDT(fdt_setprop(fdt, child_offset, "ibm,hcall-flush-required",
                             NULL, 0));
        }
    }

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
    int offset = fdt_subnode_offset(fdt, 0, "ibm,persistent-memory");
    GSList *iter, *nvdimms = nvdimm_get_device_list();

    if (offset < 0) {
        offset = fdt_add_subnode(fdt, 0, "ibm,persistent-memory");
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

typedef struct SpaprNVDIMMDeviceFlushState {
    uint64_t continue_token;
    int64_t hcall_ret;
    uint32_t drcidx;

    QLIST_ENTRY(SpaprNVDIMMDeviceFlushState) node;
} SpaprNVDIMMDeviceFlushState;

typedef struct SpaprNVDIMMDevice SpaprNVDIMMDevice;
struct SpaprNVDIMMDevice {
    /* private */
    NVDIMMDevice parent_obj;

    bool hcall_flush_required;
    uint64_t nvdimm_flush_token;
    QLIST_HEAD(, SpaprNVDIMMDeviceFlushState) pending_nvdimm_flush_states;
    QLIST_HEAD(, SpaprNVDIMMDeviceFlushState) completed_nvdimm_flush_states;

    /* public */

    /*
     * The 'on' value for this property forced the qemu to enable the hcall
     * flush for the nvdimm device even if the backend is a pmem
     */
    bool pmem_override;
};

static int flush_worker_cb(void *opaque)
{
    SpaprNVDIMMDeviceFlushState *state = opaque;
    SpaprDrc *drc = spapr_drc_by_index(state->drcidx);
    PCDIMMDevice *dimm = PC_DIMM(drc->dev);
    HostMemoryBackend *backend = MEMORY_BACKEND(dimm->hostmem);
    int backend_fd = memory_region_get_fd(&backend->mr);

    if (object_property_get_bool(OBJECT(backend), "pmem", NULL)) {
        MemoryRegion *mr = host_memory_backend_get_memory(dimm->hostmem);
        void *ptr = memory_region_get_ram_ptr(mr);
        size_t size = object_property_get_uint(OBJECT(dimm), PC_DIMM_SIZE_PROP,
                                               NULL);

        /* flush pmem backend */
        pmem_persist(ptr, size);
    } else {
        /* flush raw backing image */
        if (qemu_fdatasync(backend_fd) < 0) {
            error_report("papr_scm: Could not sync nvdimm to backend file: %s",
                         strerror(errno));
            return H_HARDWARE;
        }
    }

    return H_SUCCESS;
}

static void spapr_nvdimm_flush_completion_cb(void *opaque, int hcall_ret)
{
    SpaprNVDIMMDeviceFlushState *state = opaque;
    SpaprDrc *drc = spapr_drc_by_index(state->drcidx);
    SpaprNVDIMMDevice *s_nvdimm = SPAPR_NVDIMM(drc->dev);

    state->hcall_ret = hcall_ret;
    QLIST_REMOVE(state, node);
    QLIST_INSERT_HEAD(&s_nvdimm->completed_nvdimm_flush_states, state, node);
}

static int spapr_nvdimm_flush_post_load(void *opaque, int version_id)
{
    SpaprNVDIMMDevice *s_nvdimm = (SpaprNVDIMMDevice *)opaque;
    SpaprNVDIMMDeviceFlushState *state;
    ThreadPool *pool = aio_get_thread_pool(qemu_get_aio_context());
    HostMemoryBackend *backend = MEMORY_BACKEND(PC_DIMM(s_nvdimm)->hostmem);
    bool is_pmem = object_property_get_bool(OBJECT(backend), "pmem", NULL);
    bool pmem_override = object_property_get_bool(OBJECT(s_nvdimm),
                                                  "pmem-override", NULL);
    bool dest_hcall_flush_required = pmem_override || !is_pmem;

    if (!s_nvdimm->hcall_flush_required && dest_hcall_flush_required) {
        error_report("The file backend for the spapr-nvdimm device %s at "
                     "source is a pmem, use pmem=on and pmem-override=off to "
                     "continue.", DEVICE(s_nvdimm)->id);
        return -EINVAL;
    }
    if (s_nvdimm->hcall_flush_required && !dest_hcall_flush_required) {
        error_report("The guest expects hcall-flush support for the "
                     "spapr-nvdimm device %s, use pmem_override=on to "
                     "continue.", DEVICE(s_nvdimm)->id);
        return -EINVAL;
    }

    QLIST_FOREACH(state, &s_nvdimm->pending_nvdimm_flush_states, node) {
        thread_pool_submit_aio(pool, flush_worker_cb, state,
                               spapr_nvdimm_flush_completion_cb, state);
    }

    return 0;
}

static const VMStateDescription vmstate_spapr_nvdimm_flush_state = {
     .name = "spapr_nvdimm_flush_state",
     .version_id = 1,
     .minimum_version_id = 1,
     .fields = (VMStateField[]) {
         VMSTATE_UINT64(continue_token, SpaprNVDIMMDeviceFlushState),
         VMSTATE_INT64(hcall_ret, SpaprNVDIMMDeviceFlushState),
         VMSTATE_UINT32(drcidx, SpaprNVDIMMDeviceFlushState),
         VMSTATE_END_OF_LIST()
     },
};

const VMStateDescription vmstate_spapr_nvdimm_states = {
    .name = "spapr_nvdimm_states",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = spapr_nvdimm_flush_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_BOOL(hcall_flush_required, SpaprNVDIMMDevice),
        VMSTATE_UINT64(nvdimm_flush_token, SpaprNVDIMMDevice),
        VMSTATE_QLIST_V(completed_nvdimm_flush_states, SpaprNVDIMMDevice, 1,
                        vmstate_spapr_nvdimm_flush_state,
                        SpaprNVDIMMDeviceFlushState, node),
        VMSTATE_QLIST_V(pending_nvdimm_flush_states, SpaprNVDIMMDevice, 1,
                        vmstate_spapr_nvdimm_flush_state,
                        SpaprNVDIMMDeviceFlushState, node),
        VMSTATE_END_OF_LIST()
    },
};

/*
 * Assign a token and reserve it for the new flush state.
 */
static SpaprNVDIMMDeviceFlushState *spapr_nvdimm_init_new_flush_state(
                                                SpaprNVDIMMDevice *spapr_nvdimm)
{
    SpaprNVDIMMDeviceFlushState *state;

    state = g_malloc0(sizeof(*state));

    spapr_nvdimm->nvdimm_flush_token++;
    /* Token zero is presumed as no job pending. Assert on overflow to zero */
    g_assert(spapr_nvdimm->nvdimm_flush_token != 0);

    state->continue_token = spapr_nvdimm->nvdimm_flush_token;

    QLIST_INSERT_HEAD(&spapr_nvdimm->pending_nvdimm_flush_states, state, node);

    return state;
}

/*
 * spapr_nvdimm_finish_flushes
 *      Waits for all pending flush requests to complete
 *      their execution and free the states
 */
void spapr_nvdimm_finish_flushes(void)
{
    SpaprNVDIMMDeviceFlushState *state, *next;
    GSList *list, *nvdimms;

    /*
     * Called on reset path, the main loop thread which calls
     * the pending BHs has gotten out running in the reset path,
     * finally reaching here. Other code path being guest
     * h_client_architecture_support, thats early boot up.
     */
    nvdimms = nvdimm_get_device_list();
    for (list = nvdimms; list; list = list->next) {
        NVDIMMDevice *nvdimm = list->data;
        if (object_dynamic_cast(OBJECT(nvdimm), TYPE_SPAPR_NVDIMM)) {
            SpaprNVDIMMDevice *s_nvdimm = SPAPR_NVDIMM(nvdimm);
            while (!QLIST_EMPTY(&s_nvdimm->pending_nvdimm_flush_states)) {
                aio_poll(qemu_get_aio_context(), true);
            }

            QLIST_FOREACH_SAFE(state, &s_nvdimm->completed_nvdimm_flush_states,
                               node, next) {
                QLIST_REMOVE(state, node);
                g_free(state);
            }
        }
    }
    g_slist_free(nvdimms);
}

/*
 * spapr_nvdimm_get_flush_status
 *      Fetches the status of the hcall worker and returns
 *      H_LONG_BUSY_ORDER_10_MSEC if the worker is still running.
 */
static int spapr_nvdimm_get_flush_status(SpaprNVDIMMDevice *s_nvdimm,
                                         uint64_t token)
{
    SpaprNVDIMMDeviceFlushState *state, *node;

    QLIST_FOREACH(state, &s_nvdimm->pending_nvdimm_flush_states, node) {
        if (state->continue_token == token) {
            return H_LONG_BUSY_ORDER_10_MSEC;
        }
    }

    QLIST_FOREACH_SAFE(state, &s_nvdimm->completed_nvdimm_flush_states,
                       node, node) {
        if (state->continue_token == token) {
            int ret = state->hcall_ret;
            QLIST_REMOVE(state, node);
            g_free(state);
            return ret;
        }
    }

    /* If not found in complete list too, invalid token */
    return H_P2;
}

/*
 * H_SCM_FLUSH
 * Input: drc_index, continue-token
 * Out: continue-token
 * Return Value: H_SUCCESS, H_Parameter, H_P2, H_LONG_BUSY_ORDER_10_MSEC,
 *               H_UNSUPPORTED
 *
 * Given a DRC Index Flush the data to backend NVDIMM device. The hcall returns
 * H_LONG_BUSY_ORDER_10_MSEC when the flush takes longer time and the hcall
 * needs to be issued multiple times in order to be completely serviced. The
 * continue-token from the output to be passed in the argument list of
 * subsequent hcalls until the hcall is completely serviced at which point
 * H_SUCCESS or other error is returned.
 */
static target_ulong h_scm_flush(PowerPCCPU *cpu, SpaprMachineState *spapr,
                                target_ulong opcode, target_ulong *args)
{
    int ret;
    uint32_t drc_index = args[0];
    uint64_t continue_token = args[1];
    SpaprDrc *drc = spapr_drc_by_index(drc_index);
    PCDIMMDevice *dimm;
    HostMemoryBackend *backend = NULL;
    SpaprNVDIMMDeviceFlushState *state;
    ThreadPool *pool = aio_get_thread_pool(qemu_get_aio_context());
    int fd;

    if (!drc || !drc->dev ||
        spapr_drc_type(drc) != SPAPR_DR_CONNECTOR_TYPE_PMEM) {
        return H_PARAMETER;
    }

    dimm = PC_DIMM(drc->dev);
    if (!object_dynamic_cast(OBJECT(dimm), TYPE_SPAPR_NVDIMM)) {
        return H_PARAMETER;
    }
    if (continue_token == 0) {
        bool is_pmem = false, pmem_override = false;
        backend = MEMORY_BACKEND(dimm->hostmem);
        fd = memory_region_get_fd(&backend->mr);

        if (fd < 0) {
            return H_UNSUPPORTED;
        }

        is_pmem = object_property_get_bool(OBJECT(backend), "pmem", NULL);
        pmem_override = object_property_get_bool(OBJECT(dimm),
                                                "pmem-override", NULL);
        if (is_pmem && !pmem_override) {
            return H_UNSUPPORTED;
        }

        state = spapr_nvdimm_init_new_flush_state(SPAPR_NVDIMM(dimm));
        if (!state) {
            return H_HARDWARE;
        }

        state->drcidx = drc_index;

        thread_pool_submit_aio(pool, flush_worker_cb, state,
                               spapr_nvdimm_flush_completion_cb, state);

        continue_token = state->continue_token;
    }

    ret = spapr_nvdimm_get_flush_status(SPAPR_NVDIMM(dimm), continue_token);
    if (H_IS_LONG_BUSY(ret)) {
        args[0] = continue_token;
    }

    return ret;
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

static target_ulong h_scm_health(PowerPCCPU *cpu, SpaprMachineState *spapr,
                                 target_ulong opcode, target_ulong *args)
{

    NVDIMMDevice *nvdimm;
    uint64_t hbitmap = 0;
    uint32_t drc_index = args[0];
    SpaprDrc *drc = spapr_drc_by_index(drc_index);
    const uint64_t hbitmap_mask = PAPR_PMEM_UNARMED;


    /* Ensure that the drc is valid & is valid PMEM dimm and is plugged in */
    if (!drc || !drc->dev ||
        spapr_drc_type(drc) != SPAPR_DR_CONNECTOR_TYPE_PMEM) {
        return H_PARAMETER;
    }

    nvdimm = NVDIMM(drc->dev);

    /* Update if the nvdimm is unarmed and send its status via health bitmaps */
    if (object_property_get_bool(OBJECT(nvdimm), NVDIMM_UNARMED_PROP, NULL)) {
        hbitmap |= PAPR_PMEM_UNARMED;
    }

    /* Update the out args with health bitmap/mask */
    args[0] = hbitmap;
    args[1] = hbitmap_mask;

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
    spapr_register_hypercall(H_SCM_HEALTH, h_scm_health);
    spapr_register_hypercall(H_SCM_FLUSH, h_scm_flush);
}

type_init(spapr_scm_register_types)

static void spapr_nvdimm_realize(NVDIMMDevice *dimm, Error **errp)
{
    SpaprNVDIMMDevice *s_nvdimm = SPAPR_NVDIMM(dimm);
    HostMemoryBackend *backend = MEMORY_BACKEND(PC_DIMM(dimm)->hostmem);
    bool is_pmem = object_property_get_bool(OBJECT(backend),  "pmem", NULL);
    bool pmem_override = object_property_get_bool(OBJECT(dimm), "pmem-override",
                                             NULL);
    if (!is_pmem || pmem_override) {
        s_nvdimm->hcall_flush_required = true;
    }

    vmstate_register(NULL, VMSTATE_INSTANCE_ID_ANY,
                     &vmstate_spapr_nvdimm_states, dimm);
}

static void spapr_nvdimm_unrealize(NVDIMMDevice *dimm)
{
    vmstate_unregister(NULL, &vmstate_spapr_nvdimm_states, dimm);
}

static Property spapr_nvdimm_properties[] = {
#ifdef CONFIG_LIBPMEM
    DEFINE_PROP_BOOL("pmem-override", SpaprNVDIMMDevice, pmem_override, false),
#endif
    DEFINE_PROP_END_OF_LIST(),
};

static void spapr_nvdimm_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    NVDIMMClass *nvc = NVDIMM_CLASS(oc);

    nvc->realize = spapr_nvdimm_realize;
    nvc->unrealize = spapr_nvdimm_unrealize;

    device_class_set_props(dc, spapr_nvdimm_properties);
}

static void spapr_nvdimm_init(Object *obj)
{
    SpaprNVDIMMDevice *s_nvdimm = SPAPR_NVDIMM(obj);

    s_nvdimm->hcall_flush_required = false;
    QLIST_INIT(&s_nvdimm->pending_nvdimm_flush_states);
    QLIST_INIT(&s_nvdimm->completed_nvdimm_flush_states);
}

static TypeInfo spapr_nvdimm_info = {
    .name          = TYPE_SPAPR_NVDIMM,
    .parent        = TYPE_NVDIMM,
    .class_init    = spapr_nvdimm_class_init,
    .class_size    = sizeof(SPAPRNVDIMMClass),
    .instance_size = sizeof(SpaprNVDIMMDevice),
    .instance_init = spapr_nvdimm_init,
};

static void spapr_nvdimm_register_types(void)
{
    type_register_static(&spapr_nvdimm_info);
}

type_init(spapr_nvdimm_register_types)
