/*
 * QEMU PowerPC pSeries Logical Partition (aka sPAPR) hardware System Emulator
 *
 * Copyright (c) 2004-2007 Fabrice Bellard
 * Copyright (c) 2007 Jocelyn Mayer
 * Copyright (c) 2010 David Gibson, IBM Corporation.
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
#include "qemu/datadir.h"
#include "qemu/memalign.h"
#include "qemu/guest-random.h"
#include "qapi/error.h"
#include "qapi/qapi-events-machine.h"
#include "qapi/qapi-events-qdev.h"
#include "qapi/visitor.h"
#include "sysemu/sysemu.h"
#include "sysemu/hostmem.h"
#include "sysemu/numa.h"
#include "sysemu/qtest.h"
#include "sysemu/reset.h"
#include "sysemu/runstate.h"
#include "qemu/log.h"
#include "hw/fw-path-provider.h"
#include "elf.h"
#include "net/net.h"
#include "sysemu/device_tree.h"
#include "sysemu/cpus.h"
#include "sysemu/hw_accel.h"
#include "kvm_ppc.h"
#include "migration/misc.h"
#include "migration/qemu-file-types.h"
#include "migration/global_state.h"
#include "migration/register.h"
#include "migration/blocker.h"
#include "mmu-hash64.h"
#include "mmu-book3s-v3.h"
#include "cpu-models.h"
#include "hw/core/cpu.h"

#include "hw/ppc/ppc.h"
#include "hw/loader.h"

#include "hw/ppc/fdt.h"
#include "hw/ppc/spapr.h"
#include "hw/ppc/spapr_nested.h"
#include "hw/ppc/spapr_vio.h"
#include "hw/ppc/vof.h"
#include "hw/qdev-properties.h"
#include "hw/pci-host/spapr.h"
#include "hw/pci/msi.h"

#include "hw/pci/pci.h"
#include "hw/scsi/scsi.h"
#include "hw/virtio/virtio-scsi.h"
#include "hw/virtio/vhost-scsi-common.h"

#include "exec/ram_addr.h"
#include "hw/usb.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "trace.h"
#include "hw/nmi.h"
#include "hw/intc/intc.h"

#include "hw/ppc/spapr_cpu_core.h"
#include "hw/mem/memory-device.h"
#include "hw/ppc/spapr_tpm_proxy.h"
#include "hw/ppc/spapr_nvdimm.h"
#include "hw/ppc/spapr_numa.h"
#include "hw/ppc/pef.h"

#include "monitor/monitor.h"

#include <libfdt.h>

/* SLOF memory layout:
 *
 * SLOF raw image loaded at 0, copies its romfs right below the flat
 * device-tree, then position SLOF itself 31M below that
 *
 * So we set FW_OVERHEAD to 40MB which should account for all of that
 * and more
 *
 * We load our kernel at 4M, leaving space for SLOF initial image
 */
#define FDT_MAX_ADDR            0x80000000 /* FDT must stay below that */
#define FW_MAX_SIZE             0x400000
#define FW_FILE_NAME            "slof.bin"
#define FW_FILE_NAME_VOF        "vof.bin"
#define FW_OVERHEAD             0x2800000
#define KERNEL_LOAD_ADDR        FW_MAX_SIZE

#define MIN_RMA_SLOF            (128 * MiB)

#define PHANDLE_INTC            0x00001111

/* These two functions implement the VCPU id numbering: one to compute them
 * all and one to identify thread 0 of a VCORE. Any change to the first one
 * is likely to have an impact on the second one, so let's keep them close.
 */
static int spapr_vcpu_id(SpaprMachineState *spapr, int cpu_index)
{
    MachineState *ms = MACHINE(spapr);
    unsigned int smp_threads = ms->smp.threads;

    assert(spapr->vsmt);
    return
        (cpu_index / smp_threads) * spapr->vsmt + cpu_index % smp_threads;
}
static bool spapr_is_thread0_in_vcore(SpaprMachineState *spapr,
                                      PowerPCCPU *cpu)
{
    assert(spapr->vsmt);
    return spapr_get_vcpu_id(cpu) % spapr->vsmt == 0;
}

static bool pre_2_10_vmstate_dummy_icp_needed(void *opaque)
{
    /* Dummy entries correspond to unused ICPState objects in older QEMUs,
     * and newer QEMUs don't even have them. In both cases, we don't want
     * to send anything on the wire.
     */
    return false;
}

static const VMStateDescription pre_2_10_vmstate_dummy_icp = {
    /*
     * Hack ahead.  We can't have two devices with the same name and
     * instance id.  So I rename this to pass make check.
     * Real help from people who knows the hardware is needed.
     */
    .name = "icp/server",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = pre_2_10_vmstate_dummy_icp_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UNUSED(4), /* uint32_t xirr */
        VMSTATE_UNUSED(1), /* uint8_t pending_priority */
        VMSTATE_UNUSED(1), /* uint8_t mfrr */
        VMSTATE_END_OF_LIST()
    },
};

/*
 * See comment in hw/intc/xics.c:icp_realize()
 *
 * You have to remove vmstate_replace_hack_for_ppc() when you remove
 * the machine types that need the following function.
 */
static void pre_2_10_vmstate_register_dummy_icp(int i)
{
    vmstate_register(NULL, i, &pre_2_10_vmstate_dummy_icp,
                     (void *)(uintptr_t) i);
}

/*
 * See comment in hw/intc/xics.c:icp_realize()
 *
 * You have to remove vmstate_replace_hack_for_ppc() when you remove
 * the machine types that need the following function.
 */
static void pre_2_10_vmstate_unregister_dummy_icp(int i)
{
    /*
     * This used to be:
     *
     *    vmstate_unregister(NULL, &pre_2_10_vmstate_dummy_icp,
     *                      (void *)(uintptr_t) i);
     */
}

int spapr_max_server_number(SpaprMachineState *spapr)
{
    MachineState *ms = MACHINE(spapr);

    assert(spapr->vsmt);
    return DIV_ROUND_UP(ms->smp.max_cpus * spapr->vsmt, ms->smp.threads);
}

static int spapr_fixup_cpu_smt_dt(void *fdt, int offset, PowerPCCPU *cpu,
                                  int smt_threads)
{
    int i, ret = 0;
    g_autofree uint32_t *servers_prop = g_new(uint32_t, smt_threads);
    g_autofree uint32_t *gservers_prop = g_new(uint32_t, smt_threads * 2);
    int index = spapr_get_vcpu_id(cpu);

    if (cpu->compat_pvr) {
        ret = fdt_setprop_cell(fdt, offset, "cpu-version", cpu->compat_pvr);
        if (ret < 0) {
            return ret;
        }
    }

    /* Build interrupt servers and gservers properties */
    for (i = 0; i < smt_threads; i++) {
        servers_prop[i] = cpu_to_be32(index + i);
        /* Hack, direct the group queues back to cpu 0 */
        gservers_prop[i*2] = cpu_to_be32(index + i);
        gservers_prop[i*2 + 1] = 0;
    }
    ret = fdt_setprop(fdt, offset, "ibm,ppc-interrupt-server#s",
                      servers_prop, sizeof(*servers_prop) * smt_threads);
    if (ret < 0) {
        return ret;
    }
    ret = fdt_setprop(fdt, offset, "ibm,ppc-interrupt-gserver#s",
                      gservers_prop, sizeof(*gservers_prop) * smt_threads * 2);

    return ret;
}

static void spapr_dt_pa_features(SpaprMachineState *spapr,
                                 PowerPCCPU *cpu,
                                 void *fdt, int offset)
{
    uint8_t pa_features_206[] = { 6, 0,
        0xf6, 0x1f, 0xc7, 0x00, 0x80, 0xc0 };
    uint8_t pa_features_207[] = { 24, 0,
        0xf6, 0x1f, 0xc7, 0xc0, 0x80, 0xf0,
        0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x80, 0x00,
        0x80, 0x00, 0x80, 0x00, 0x00, 0x00 };
    uint8_t pa_features_300[] = { 66, 0,
        /* 0: MMU|FPU|SLB|RUN|DABR|NX, 1: fri[nzpm]|DABRX|SPRG3|SLB0|PP110 */
        /* 2: VPM|DS205|PPR|DS202|DS206, 3: LSD|URG, SSO, 5: LE|CFAR|EB|LSQ */
        0xf6, 0x1f, 0xc7, 0xc0, 0x80, 0xf0, /* 0 - 5 */
        /* 6: DS207 */
        0x80, 0x00, 0x00, 0x00, 0x00, 0x00, /* 6 - 11 */
        /* 16: Vector */
        0x00, 0x00, 0x00, 0x00, 0x80, 0x00, /* 12 - 17 */
        /* 18: Vec. Scalar, 20: Vec. XOR, 22: HTM */
        0x80, 0x00, 0x80, 0x00, 0x00, 0x00, /* 18 - 23 */
        /* 24: Ext. Dec, 26: 64 bit ftrs, 28: PM ftrs */
        0x80, 0x00, 0x80, 0x00, 0x80, 0x00, /* 24 - 29 */
        /* 30: MMR, 32: LE atomic, 34: EBB + ext EBB */
        0x80, 0x00, 0x80, 0x00, 0xC0, 0x00, /* 30 - 35 */
        /* 36: SPR SO, 38: Copy/Paste, 40: Radix MMU */
        0x80, 0x00, 0x80, 0x00, 0x80, 0x00, /* 36 - 41 */
        /* 42: PM, 44: PC RA, 46: SC vec'd */
        0x80, 0x00, 0x80, 0x00, 0x80, 0x00, /* 42 - 47 */
        /* 48: SIMD, 50: QP BFP, 52: String */
        0x80, 0x00, 0x80, 0x00, 0x80, 0x00, /* 48 - 53 */
        /* 54: DecFP, 56: DecI, 58: SHA */
        0x80, 0x00, 0x80, 0x00, 0x80, 0x00, /* 54 - 59 */
        /* 60: NM atomic, 62: RNG */
        0x80, 0x00, 0x80, 0x00, 0x00, 0x00, /* 60 - 65 */
    };
    uint8_t *pa_features = NULL;
    size_t pa_size;

    if (ppc_check_compat(cpu, CPU_POWERPC_LOGICAL_2_06, 0, cpu->compat_pvr)) {
        pa_features = pa_features_206;
        pa_size = sizeof(pa_features_206);
    }
    if (ppc_check_compat(cpu, CPU_POWERPC_LOGICAL_2_07, 0, cpu->compat_pvr)) {
        pa_features = pa_features_207;
        pa_size = sizeof(pa_features_207);
    }
    if (ppc_check_compat(cpu, CPU_POWERPC_LOGICAL_3_00, 0, cpu->compat_pvr)) {
        pa_features = pa_features_300;
        pa_size = sizeof(pa_features_300);
    }
    if (!pa_features) {
        return;
    }

    if (ppc_hash64_has(cpu, PPC_HASH64_CI_LARGEPAGE)) {
        /*
         * Note: we keep CI large pages off by default because a 64K capable
         * guest provisioned with large pages might otherwise try to map a qemu
         * framebuffer (or other kind of memory mapped PCI BAR) using 64K pages
         * even if that qemu runs on a 4k host.
         * We dd this bit back here if we are confident this is not an issue
         */
        pa_features[3] |= 0x20;
    }
    if ((spapr_get_cap(spapr, SPAPR_CAP_HTM) != 0) && pa_size > 24) {
        pa_features[24] |= 0x80;    /* Transactional memory support */
    }
    if (spapr->cas_pre_isa3_guest && pa_size > 40) {
        /* Workaround for broken kernels that attempt (guest) radix
         * mode when they can't handle it, if they see the radix bit set
         * in pa-features. So hide it from them. */
        pa_features[40 + 2] &= ~0x80; /* Radix MMU */
    }

    _FDT((fdt_setprop(fdt, offset, "ibm,pa-features", pa_features, pa_size)));
}

static hwaddr spapr_node0_size(MachineState *machine)
{
    if (machine->numa_state->num_nodes) {
        int i;
        for (i = 0; i < machine->numa_state->num_nodes; ++i) {
            if (machine->numa_state->nodes[i].node_mem) {
                return MIN(pow2floor(machine->numa_state->nodes[i].node_mem),
                           machine->ram_size);
            }
        }
    }
    return machine->ram_size;
}

static void add_str(GString *s, const gchar *s1)
{
    g_string_append_len(s, s1, strlen(s1) + 1);
}

static int spapr_dt_memory_node(SpaprMachineState *spapr, void *fdt, int nodeid,
                                hwaddr start, hwaddr size)
{
    char mem_name[32];
    uint64_t mem_reg_property[2];
    int off;

    mem_reg_property[0] = cpu_to_be64(start);
    mem_reg_property[1] = cpu_to_be64(size);

    sprintf(mem_name, "memory@%" HWADDR_PRIx, start);
    off = fdt_add_subnode(fdt, 0, mem_name);
    _FDT(off);
    _FDT((fdt_setprop_string(fdt, off, "device_type", "memory")));
    _FDT((fdt_setprop(fdt, off, "reg", mem_reg_property,
                      sizeof(mem_reg_property))));
    spapr_numa_write_associativity_dt(spapr, fdt, off, nodeid);
    return off;
}

static uint32_t spapr_pc_dimm_node(MemoryDeviceInfoList *list, ram_addr_t addr)
{
    MemoryDeviceInfoList *info;

    for (info = list; info; info = info->next) {
        MemoryDeviceInfo *value = info->value;

        if (value && value->type == MEMORY_DEVICE_INFO_KIND_DIMM) {
            PCDIMMDeviceInfo *pcdimm_info = value->u.dimm.data;

            if (addr >= pcdimm_info->addr &&
                addr < (pcdimm_info->addr + pcdimm_info->size)) {
                return pcdimm_info->node;
            }
        }
    }

    return -1;
}

struct sPAPRDrconfCellV2 {
     uint32_t seq_lmbs;
     uint64_t base_addr;
     uint32_t drc_index;
     uint32_t aa_index;
     uint32_t flags;
} QEMU_PACKED;

typedef struct DrconfCellQueue {
    struct sPAPRDrconfCellV2 cell;
    QSIMPLEQ_ENTRY(DrconfCellQueue) entry;
} DrconfCellQueue;

static DrconfCellQueue *
spapr_get_drconf_cell(uint32_t seq_lmbs, uint64_t base_addr,
                      uint32_t drc_index, uint32_t aa_index,
                      uint32_t flags)
{
    DrconfCellQueue *elem;

    elem = g_malloc0(sizeof(*elem));
    elem->cell.seq_lmbs = cpu_to_be32(seq_lmbs);
    elem->cell.base_addr = cpu_to_be64(base_addr);
    elem->cell.drc_index = cpu_to_be32(drc_index);
    elem->cell.aa_index = cpu_to_be32(aa_index);
    elem->cell.flags = cpu_to_be32(flags);

    return elem;
}

static int spapr_dt_dynamic_memory_v2(SpaprMachineState *spapr, void *fdt,
                                      int offset, MemoryDeviceInfoList *dimms)
{
    MachineState *machine = MACHINE(spapr);
    uint8_t *int_buf, *cur_index;
    int ret;
    uint64_t lmb_size = SPAPR_MEMORY_BLOCK_SIZE;
    uint64_t addr, cur_addr, size;
    uint32_t nr_boot_lmbs = (machine->device_memory->base / lmb_size);
    uint64_t mem_end = machine->device_memory->base +
                       memory_region_size(&machine->device_memory->mr);
    uint32_t node, buf_len, nr_entries = 0;
    SpaprDrc *drc;
    DrconfCellQueue *elem, *next;
    MemoryDeviceInfoList *info;
    QSIMPLEQ_HEAD(, DrconfCellQueue) drconf_queue
        = QSIMPLEQ_HEAD_INITIALIZER(drconf_queue);

    /* Entry to cover RAM and the gap area */
    elem = spapr_get_drconf_cell(nr_boot_lmbs, 0, 0, -1,
                                 SPAPR_LMB_FLAGS_RESERVED |
                                 SPAPR_LMB_FLAGS_DRC_INVALID);
    QSIMPLEQ_INSERT_TAIL(&drconf_queue, elem, entry);
    nr_entries++;

    cur_addr = machine->device_memory->base;
    for (info = dimms; info; info = info->next) {
        PCDIMMDeviceInfo *di = info->value->u.dimm.data;

        addr = di->addr;
        size = di->size;
        node = di->node;

        /*
         * The NVDIMM area is hotpluggable after the NVDIMM is unplugged. The
         * area is marked hotpluggable in the next iteration for the bigger
         * chunk including the NVDIMM occupied area.
         */
        if (info->value->type == MEMORY_DEVICE_INFO_KIND_NVDIMM)
            continue;

        /* Entry for hot-pluggable area */
        if (cur_addr < addr) {
            drc = spapr_drc_by_id(TYPE_SPAPR_DRC_LMB, cur_addr / lmb_size);
            g_assert(drc);
            elem = spapr_get_drconf_cell((addr - cur_addr) / lmb_size,
                                         cur_addr, spapr_drc_index(drc), -1, 0);
            QSIMPLEQ_INSERT_TAIL(&drconf_queue, elem, entry);
            nr_entries++;
        }

        /* Entry for DIMM */
        drc = spapr_drc_by_id(TYPE_SPAPR_DRC_LMB, addr / lmb_size);
        g_assert(drc);
        elem = spapr_get_drconf_cell(size / lmb_size, addr,
                                     spapr_drc_index(drc), node,
                                     (SPAPR_LMB_FLAGS_ASSIGNED |
                                      SPAPR_LMB_FLAGS_HOTREMOVABLE));
        QSIMPLEQ_INSERT_TAIL(&drconf_queue, elem, entry);
        nr_entries++;
        cur_addr = addr + size;
    }

    /* Entry for remaining hotpluggable area */
    if (cur_addr < mem_end) {
        drc = spapr_drc_by_id(TYPE_SPAPR_DRC_LMB, cur_addr / lmb_size);
        g_assert(drc);
        elem = spapr_get_drconf_cell((mem_end - cur_addr) / lmb_size,
                                     cur_addr, spapr_drc_index(drc), -1, 0);
        QSIMPLEQ_INSERT_TAIL(&drconf_queue, elem, entry);
        nr_entries++;
    }

    buf_len = nr_entries * sizeof(struct sPAPRDrconfCellV2) + sizeof(uint32_t);
    int_buf = cur_index = g_malloc0(buf_len);
    *(uint32_t *)int_buf = cpu_to_be32(nr_entries);
    cur_index += sizeof(nr_entries);

    QSIMPLEQ_FOREACH_SAFE(elem, &drconf_queue, entry, next) {
        memcpy(cur_index, &elem->cell, sizeof(elem->cell));
        cur_index += sizeof(elem->cell);
        QSIMPLEQ_REMOVE(&drconf_queue, elem, DrconfCellQueue, entry);
        g_free(elem);
    }

    ret = fdt_setprop(fdt, offset, "ibm,dynamic-memory-v2", int_buf, buf_len);
    g_free(int_buf);
    if (ret < 0) {
        return -1;
    }
    return 0;
}

static int spapr_dt_dynamic_memory(SpaprMachineState *spapr, void *fdt,
                                   int offset, MemoryDeviceInfoList *dimms)
{
    MachineState *machine = MACHINE(spapr);
    int i, ret;
    uint64_t lmb_size = SPAPR_MEMORY_BLOCK_SIZE;
    uint32_t device_lmb_start = machine->device_memory->base / lmb_size;
    uint32_t nr_lmbs = (machine->device_memory->base +
                       memory_region_size(&machine->device_memory->mr)) /
                       lmb_size;
    uint32_t *int_buf, *cur_index, buf_len;

    /*
     * Allocate enough buffer size to fit in ibm,dynamic-memory
     */
    buf_len = (nr_lmbs * SPAPR_DR_LMB_LIST_ENTRY_SIZE + 1) * sizeof(uint32_t);
    cur_index = int_buf = g_malloc0(buf_len);
    int_buf[0] = cpu_to_be32(nr_lmbs);
    cur_index++;
    for (i = 0; i < nr_lmbs; i++) {
        uint64_t addr = i * lmb_size;
        uint32_t *dynamic_memory = cur_index;

        if (i >= device_lmb_start) {
            SpaprDrc *drc;

            drc = spapr_drc_by_id(TYPE_SPAPR_DRC_LMB, i);
            g_assert(drc);

            dynamic_memory[0] = cpu_to_be32(addr >> 32);
            dynamic_memory[1] = cpu_to_be32(addr & 0xffffffff);
            dynamic_memory[2] = cpu_to_be32(spapr_drc_index(drc));
            dynamic_memory[3] = cpu_to_be32(0); /* reserved */
            dynamic_memory[4] = cpu_to_be32(spapr_pc_dimm_node(dimms, addr));
            if (memory_region_present(get_system_memory(), addr)) {
                dynamic_memory[5] = cpu_to_be32(SPAPR_LMB_FLAGS_ASSIGNED);
            } else {
                dynamic_memory[5] = cpu_to_be32(0);
            }
        } else {
            /*
             * LMB information for RMA, boot time RAM and gap b/n RAM and
             * device memory region -- all these are marked as reserved
             * and as having no valid DRC.
             */
            dynamic_memory[0] = cpu_to_be32(addr >> 32);
            dynamic_memory[1] = cpu_to_be32(addr & 0xffffffff);
            dynamic_memory[2] = cpu_to_be32(0);
            dynamic_memory[3] = cpu_to_be32(0); /* reserved */
            dynamic_memory[4] = cpu_to_be32(-1);
            dynamic_memory[5] = cpu_to_be32(SPAPR_LMB_FLAGS_RESERVED |
                                            SPAPR_LMB_FLAGS_DRC_INVALID);
        }

        cur_index += SPAPR_DR_LMB_LIST_ENTRY_SIZE;
    }
    ret = fdt_setprop(fdt, offset, "ibm,dynamic-memory", int_buf, buf_len);
    g_free(int_buf);
    if (ret < 0) {
        return -1;
    }
    return 0;
}

/*
 * Adds ibm,dynamic-reconfiguration-memory node.
 * Refer to docs/specs/ppc-spapr-hotplug.txt for the documentation
 * of this device tree node.
 */
static int spapr_dt_dynamic_reconfiguration_memory(SpaprMachineState *spapr,
                                                   void *fdt)
{
    MachineState *machine = MACHINE(spapr);
    int ret, offset;
    uint64_t lmb_size = SPAPR_MEMORY_BLOCK_SIZE;
    uint32_t prop_lmb_size[] = {cpu_to_be32(lmb_size >> 32),
                                cpu_to_be32(lmb_size & 0xffffffff)};
    MemoryDeviceInfoList *dimms = NULL;

    /* Don't create the node if there is no device memory. */
    if (!machine->device_memory) {
        return 0;
    }

    offset = fdt_add_subnode(fdt, 0, "ibm,dynamic-reconfiguration-memory");

    ret = fdt_setprop(fdt, offset, "ibm,lmb-size", prop_lmb_size,
                    sizeof(prop_lmb_size));
    if (ret < 0) {
        return ret;
    }

    ret = fdt_setprop_cell(fdt, offset, "ibm,memory-flags-mask", 0xff);
    if (ret < 0) {
        return ret;
    }

    ret = fdt_setprop_cell(fdt, offset, "ibm,memory-preservation-time", 0x0);
    if (ret < 0) {
        return ret;
    }

    /* ibm,dynamic-memory or ibm,dynamic-memory-v2 */
    dimms = qmp_memory_device_list();
    if (spapr_ovec_test(spapr->ov5_cas, OV5_DRMEM_V2)) {
        ret = spapr_dt_dynamic_memory_v2(spapr, fdt, offset, dimms);
    } else {
        ret = spapr_dt_dynamic_memory(spapr, fdt, offset, dimms);
    }
    qapi_free_MemoryDeviceInfoList(dimms);

    if (ret < 0) {
        return ret;
    }

    ret = spapr_numa_write_assoc_lookup_arrays(spapr, fdt, offset);

    return ret;
}

static int spapr_dt_memory(SpaprMachineState *spapr, void *fdt)
{
    MachineState *machine = MACHINE(spapr);
    SpaprMachineClass *smc = SPAPR_MACHINE_GET_CLASS(spapr);
    hwaddr mem_start, node_size;
    int i, nb_nodes = machine->numa_state->num_nodes;
    NodeInfo *nodes = machine->numa_state->nodes;

    for (i = 0, mem_start = 0; i < nb_nodes; ++i) {
        if (!nodes[i].node_mem) {
            continue;
        }
        if (mem_start >= machine->ram_size) {
            node_size = 0;
        } else {
            node_size = nodes[i].node_mem;
            if (node_size > machine->ram_size - mem_start) {
                node_size = machine->ram_size - mem_start;
            }
        }
        if (!mem_start) {
            /* spapr_machine_init() checks for rma_size <= node0_size
             * already */
            spapr_dt_memory_node(spapr, fdt, i, 0, spapr->rma_size);
            mem_start += spapr->rma_size;
            node_size -= spapr->rma_size;
        }
        for ( ; node_size; ) {
            hwaddr sizetmp = pow2floor(node_size);

            /* mem_start != 0 here */
            if (ctzl(mem_start) < ctzl(sizetmp)) {
                sizetmp = 1ULL << ctzl(mem_start);
            }

            spapr_dt_memory_node(spapr, fdt, i, mem_start, sizetmp);
            node_size -= sizetmp;
            mem_start += sizetmp;
        }
    }

    /* Generate ibm,dynamic-reconfiguration-memory node if required */
    if (spapr_ovec_test(spapr->ov5_cas, OV5_DRCONF_MEMORY)) {
        int ret;

        g_assert(smc->dr_lmb_enabled);
        ret = spapr_dt_dynamic_reconfiguration_memory(spapr, fdt);
        if (ret) {
            return ret;
        }
    }

    return 0;
}

static void spapr_dt_cpu(CPUState *cs, void *fdt, int offset,
                         SpaprMachineState *spapr)
{
    MachineState *ms = MACHINE(spapr);
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cs);
    int index = spapr_get_vcpu_id(cpu);
    uint32_t segs[] = {cpu_to_be32(28), cpu_to_be32(40),
                       0xffffffff, 0xffffffff};
    uint32_t tbfreq = kvm_enabled() ? kvmppc_get_tbfreq()
        : SPAPR_TIMEBASE_FREQ;
    uint32_t cpufreq = kvm_enabled() ? kvmppc_get_clockfreq() : 1000000000;
    uint32_t page_sizes_prop[64];
    size_t page_sizes_prop_size;
    unsigned int smp_threads = ms->smp.threads;
    uint32_t vcpus_per_socket = smp_threads * ms->smp.cores;
    uint32_t pft_size_prop[] = {0, cpu_to_be32(spapr->htab_shift)};
    int compat_smt = MIN(smp_threads, ppc_compat_max_vthreads(cpu));
    SpaprDrc *drc;
    int drc_index;
    uint32_t radix_AP_encodings[PPC_PAGE_SIZES_MAX_SZ];
    int i;

    drc = spapr_drc_by_id(TYPE_SPAPR_DRC_CPU, index);
    if (drc) {
        drc_index = spapr_drc_index(drc);
        _FDT((fdt_setprop_cell(fdt, offset, "ibm,my-drc-index", drc_index)));
    }

    _FDT((fdt_setprop_cell(fdt, offset, "reg", index)));
    _FDT((fdt_setprop_string(fdt, offset, "device_type", "cpu")));

    _FDT((fdt_setprop_cell(fdt, offset, "cpu-version", env->spr[SPR_PVR])));
    _FDT((fdt_setprop_cell(fdt, offset, "d-cache-block-size",
                           env->dcache_line_size)));
    _FDT((fdt_setprop_cell(fdt, offset, "d-cache-line-size",
                           env->dcache_line_size)));
    _FDT((fdt_setprop_cell(fdt, offset, "i-cache-block-size",
                           env->icache_line_size)));
    _FDT((fdt_setprop_cell(fdt, offset, "i-cache-line-size",
                           env->icache_line_size)));

    if (pcc->l1_dcache_size) {
        _FDT((fdt_setprop_cell(fdt, offset, "d-cache-size",
                               pcc->l1_dcache_size)));
    } else {
        warn_report("Unknown L1 dcache size for cpu");
    }
    if (pcc->l1_icache_size) {
        _FDT((fdt_setprop_cell(fdt, offset, "i-cache-size",
                               pcc->l1_icache_size)));
    } else {
        warn_report("Unknown L1 icache size for cpu");
    }

    _FDT((fdt_setprop_cell(fdt, offset, "timebase-frequency", tbfreq)));
    _FDT((fdt_setprop_cell(fdt, offset, "clock-frequency", cpufreq)));
    _FDT((fdt_setprop_cell(fdt, offset, "slb-size", cpu->hash64_opts->slb_size)));
    _FDT((fdt_setprop_cell(fdt, offset, "ibm,slb-size", cpu->hash64_opts->slb_size)));
    _FDT((fdt_setprop_string(fdt, offset, "status", "okay")));
    _FDT((fdt_setprop(fdt, offset, "64-bit", NULL, 0)));

    if (ppc_has_spr(cpu, SPR_PURR)) {
        _FDT((fdt_setprop_cell(fdt, offset, "ibm,purr", 1)));
    }
    if (ppc_has_spr(cpu, SPR_PURR)) {
        _FDT((fdt_setprop_cell(fdt, offset, "ibm,spurr", 1)));
    }

    if (ppc_hash64_has(cpu, PPC_HASH64_1TSEG)) {
        _FDT((fdt_setprop(fdt, offset, "ibm,processor-segment-sizes",
                          segs, sizeof(segs))));
    }

    /* Advertise VSX (vector extensions) if available
     *   1               == VMX / Altivec available
     *   2               == VSX available
     *
     * Only CPUs for which we create core types in spapr_cpu_core.c
     * are possible, and all of those have VMX */
    if (env->insns_flags & PPC_ALTIVEC) {
        if (spapr_get_cap(spapr, SPAPR_CAP_VSX) != 0) {
            _FDT((fdt_setprop_cell(fdt, offset, "ibm,vmx", 2)));
        } else {
            _FDT((fdt_setprop_cell(fdt, offset, "ibm,vmx", 1)));
        }
    }

    /* Advertise DFP (Decimal Floating Point) if available
     *   0 / no property == no DFP
     *   1               == DFP available */
    if (spapr_get_cap(spapr, SPAPR_CAP_DFP) != 0) {
        _FDT((fdt_setprop_cell(fdt, offset, "ibm,dfp", 1)));
    }

    page_sizes_prop_size = ppc_create_page_sizes_prop(cpu, page_sizes_prop,
                                                      sizeof(page_sizes_prop));
    if (page_sizes_prop_size) {
        _FDT((fdt_setprop(fdt, offset, "ibm,segment-page-sizes",
                          page_sizes_prop, page_sizes_prop_size)));
    }

    spapr_dt_pa_features(spapr, cpu, fdt, offset);

    _FDT((fdt_setprop_cell(fdt, offset, "ibm,chip-id",
                           cs->cpu_index / vcpus_per_socket)));

    _FDT((fdt_setprop(fdt, offset, "ibm,pft-size",
                      pft_size_prop, sizeof(pft_size_prop))));

    if (ms->numa_state->num_nodes > 1) {
        _FDT(spapr_numa_fixup_cpu_dt(spapr, fdt, offset, cpu));
    }

    _FDT(spapr_fixup_cpu_smt_dt(fdt, offset, cpu, compat_smt));

    if (pcc->radix_page_info) {
        for (i = 0; i < pcc->radix_page_info->count; i++) {
            radix_AP_encodings[i] =
                cpu_to_be32(pcc->radix_page_info->entries[i]);
        }
        _FDT((fdt_setprop(fdt, offset, "ibm,processor-radix-AP-encodings",
                          radix_AP_encodings,
                          pcc->radix_page_info->count *
                          sizeof(radix_AP_encodings[0]))));
    }

    /*
     * We set this property to let the guest know that it can use the large
     * decrementer and its width in bits.
     */
    if (spapr_get_cap(spapr, SPAPR_CAP_LARGE_DECREMENTER) != SPAPR_CAP_OFF)
        _FDT((fdt_setprop_u32(fdt, offset, "ibm,dec-bits",
                              pcc->lrg_decr_bits)));
}

static void spapr_dt_one_cpu(void *fdt, SpaprMachineState *spapr, CPUState *cs,
                             int cpus_offset)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    int index = spapr_get_vcpu_id(cpu);
    DeviceClass *dc = DEVICE_GET_CLASS(cs);
    g_autofree char *nodename = NULL;
    int offset;

    if (!spapr_is_thread0_in_vcore(spapr, cpu)) {
        return;
    }

    nodename = g_strdup_printf("%s@%x", dc->fw_name, index);
    offset = fdt_add_subnode(fdt, cpus_offset, nodename);
    _FDT(offset);
    spapr_dt_cpu(cs, fdt, offset, spapr);
}


static void spapr_dt_cpus(void *fdt, SpaprMachineState *spapr)
{
    CPUState **rev;
    CPUState *cs;
    int n_cpus;
    int cpus_offset;
    int i;

    cpus_offset = fdt_add_subnode(fdt, 0, "cpus");
    _FDT(cpus_offset);
    _FDT((fdt_setprop_cell(fdt, cpus_offset, "#address-cells", 0x1)));
    _FDT((fdt_setprop_cell(fdt, cpus_offset, "#size-cells", 0x0)));

    /*
     * We walk the CPUs in reverse order to ensure that CPU DT nodes
     * created by fdt_add_subnode() end up in the right order in FDT
     * for the guest kernel the enumerate the CPUs correctly.
     *
     * The CPU list cannot be traversed in reverse order, so we need
     * to do extra work.
     */
    n_cpus = 0;
    rev = NULL;
    CPU_FOREACH(cs) {
        rev = g_renew(CPUState *, rev, n_cpus + 1);
        rev[n_cpus++] = cs;
    }

    for (i = n_cpus - 1; i >= 0; i--) {
        spapr_dt_one_cpu(fdt, spapr, rev[i], cpus_offset);
    }

    g_free(rev);
}

static int spapr_dt_rng(void *fdt)
{
    int node;
    int ret;

    node = qemu_fdt_add_subnode(fdt, "/ibm,platform-facilities");
    if (node <= 0) {
        return -1;
    }
    ret = fdt_setprop_string(fdt, node, "device_type",
                             "ibm,platform-facilities");
    ret |= fdt_setprop_cell(fdt, node, "#address-cells", 0x1);
    ret |= fdt_setprop_cell(fdt, node, "#size-cells", 0x0);

    node = fdt_add_subnode(fdt, node, "ibm,random-v1");
    if (node <= 0) {
        return -1;
    }
    ret |= fdt_setprop_string(fdt, node, "compatible", "ibm,random");

    return ret ? -1 : 0;
}

static void spapr_dt_rtas(SpaprMachineState *spapr, void *fdt)
{
    MachineState *ms = MACHINE(spapr);
    int rtas;
    GString *hypertas = g_string_sized_new(256);
    GString *qemu_hypertas = g_string_sized_new(256);
    uint32_t lrdr_capacity[] = {
        0,
        0,
        cpu_to_be32(SPAPR_MEMORY_BLOCK_SIZE >> 32),
        cpu_to_be32(SPAPR_MEMORY_BLOCK_SIZE & 0xffffffff),
        cpu_to_be32(ms->smp.max_cpus / ms->smp.threads),
    };

    /* Do we have device memory? */
    if (MACHINE(spapr)->device_memory) {
        uint64_t max_device_addr = MACHINE(spapr)->device_memory->base +
            memory_region_size(&MACHINE(spapr)->device_memory->mr);

        lrdr_capacity[0] = cpu_to_be32(max_device_addr >> 32);
        lrdr_capacity[1] = cpu_to_be32(max_device_addr & 0xffffffff);
    }

    _FDT(rtas = fdt_add_subnode(fdt, 0, "rtas"));

    /* hypertas */
    add_str(hypertas, "hcall-pft");
    add_str(hypertas, "hcall-term");
    add_str(hypertas, "hcall-dabr");
    add_str(hypertas, "hcall-interrupt");
    add_str(hypertas, "hcall-tce");
    add_str(hypertas, "hcall-vio");
    add_str(hypertas, "hcall-splpar");
    add_str(hypertas, "hcall-join");
    add_str(hypertas, "hcall-bulk");
    add_str(hypertas, "hcall-set-mode");
    add_str(hypertas, "hcall-sprg0");
    add_str(hypertas, "hcall-copy");
    add_str(hypertas, "hcall-debug");
    add_str(hypertas, "hcall-vphn");
    if (spapr_get_cap(spapr, SPAPR_CAP_RPT_INVALIDATE) == SPAPR_CAP_ON) {
        add_str(hypertas, "hcall-rpt-invalidate");
    }

    add_str(qemu_hypertas, "hcall-memop1");

    if (!kvm_enabled() || kvmppc_spapr_use_multitce()) {
        add_str(hypertas, "hcall-multi-tce");
    }

    if (spapr->resize_hpt != SPAPR_RESIZE_HPT_DISABLED) {
        add_str(hypertas, "hcall-hpt-resize");
    }

    add_str(hypertas, "hcall-watchdog");

    _FDT(fdt_setprop(fdt, rtas, "ibm,hypertas-functions",
                     hypertas->str, hypertas->len));
    g_string_free(hypertas, TRUE);
    _FDT(fdt_setprop(fdt, rtas, "qemu,hypertas-functions",
                     qemu_hypertas->str, qemu_hypertas->len));
    g_string_free(qemu_hypertas, TRUE);

    spapr_numa_write_rtas_dt(spapr, fdt, rtas);

    /*
     * FWNMI reserves RTAS_ERROR_LOG_MAX for the machine check error log,
     * and 16 bytes per CPU for system reset error log plus an extra 8 bytes.
     *
     * The system reset requirements are driven by existing Linux and PowerVM
     * implementation which (contrary to PAPR) saves r3 in the error log
     * structure like machine check, so Linux expects to find the saved r3
     * value at the address in r3 upon FWNMI-enabled sreset interrupt (and
     * does not look at the error value).
     *
     * System reset interrupts are not subject to interlock like machine
     * check, so this memory area could be corrupted if the sreset is
     * interrupted by a machine check (or vice versa) if it was shared. To
     * prevent this, system reset uses per-CPU areas for the sreset save
     * area. A system reset that interrupts a system reset handler could
     * still overwrite this area, but Linux doesn't try to recover in that
     * case anyway.
     *
     * The extra 8 bytes is required because Linux's FWNMI error log check
     * is off-by-one.
     *
     * RTAS_MIN_SIZE is required for the RTAS blob itself.
     */
    _FDT(fdt_setprop_cell(fdt, rtas, "rtas-size", RTAS_MIN_SIZE +
                          RTAS_ERROR_LOG_MAX +
                          ms->smp.max_cpus * sizeof(uint64_t) * 2 +
                          sizeof(uint64_t)));
    _FDT(fdt_setprop_cell(fdt, rtas, "rtas-error-log-max",
                          RTAS_ERROR_LOG_MAX));
    _FDT(fdt_setprop_cell(fdt, rtas, "rtas-event-scan-rate",
                          RTAS_EVENT_SCAN_RATE));

    g_assert(msi_nonbroken);
    _FDT(fdt_setprop(fdt, rtas, "ibm,change-msix-capable", NULL, 0));

    /*
     * According to PAPR, rtas ibm,os-term does not guarantee a return
     * back to the guest cpu.
     *
     * While an additional ibm,extended-os-term property indicates
     * that rtas call return will always occur. Set this property.
     */
    _FDT(fdt_setprop(fdt, rtas, "ibm,extended-os-term", NULL, 0));

    _FDT(fdt_setprop(fdt, rtas, "ibm,lrdr-capacity",
                     lrdr_capacity, sizeof(lrdr_capacity)));

    spapr_dt_rtas_tokens(fdt, rtas);
}

/*
 * Prepare ibm,arch-vec-5-platform-support, which indicates the MMU
 * and the XIVE features that the guest may request and thus the valid
 * values for bytes 23..26 of option vector 5:
 */
static void spapr_dt_ov5_platform_support(SpaprMachineState *spapr, void *fdt,
                                          int chosen)
{
    PowerPCCPU *first_ppc_cpu = POWERPC_CPU(first_cpu);

    char val[2 * 4] = {
        23, 0x00, /* XICS / XIVE mode */
        24, 0x00, /* Hash/Radix, filled in below. */
        25, 0x00, /* Hash options: Segment Tables == no, GTSE == no. */
        26, 0x40, /* Radix options: GTSE == yes. */
    };

    if (spapr->irq->xics && spapr->irq->xive) {
        val[1] = SPAPR_OV5_XIVE_BOTH;
    } else if (spapr->irq->xive) {
        val[1] = SPAPR_OV5_XIVE_EXPLOIT;
    } else {
        assert(spapr->irq->xics);
        val[1] = SPAPR_OV5_XIVE_LEGACY;
    }

    if (!ppc_check_compat(first_ppc_cpu, CPU_POWERPC_LOGICAL_3_00, 0,
                          first_ppc_cpu->compat_pvr)) {
        /*
         * If we're in a pre POWER9 compat mode then the guest should
         * do hash and use the legacy interrupt mode
         */
        val[1] = SPAPR_OV5_XIVE_LEGACY; /* XICS */
        val[3] = 0x00; /* Hash */
        spapr_check_mmu_mode(false);
    } else if (kvm_enabled()) {
        if (kvmppc_has_cap_mmu_radix() && kvmppc_has_cap_mmu_hash_v3()) {
            val[3] = 0x80; /* OV5_MMU_BOTH */
        } else if (kvmppc_has_cap_mmu_radix()) {
            val[3] = 0x40; /* OV5_MMU_RADIX_300 */
        } else {
            val[3] = 0x00; /* Hash */
        }
    } else {
        /* V3 MMU supports both hash and radix in tcg (with dynamic switching) */
        val[3] = 0xC0;
    }
    _FDT(fdt_setprop(fdt, chosen, "ibm,arch-vec-5-platform-support",
                     val, sizeof(val)));
}

static void spapr_dt_chosen(SpaprMachineState *spapr, void *fdt, bool reset)
{
    MachineState *machine = MACHINE(spapr);
    SpaprMachineClass *smc = SPAPR_MACHINE_GET_CLASS(machine);
    int chosen;

    _FDT(chosen = fdt_add_subnode(fdt, 0, "chosen"));

    if (reset) {
        const char *boot_device = spapr->boot_device;
        g_autofree char *stdout_path = spapr_vio_stdout_path(spapr->vio_bus);
        size_t cb = 0;
        g_autofree char *bootlist = get_boot_devices_list(&cb);

        if (machine->kernel_cmdline && machine->kernel_cmdline[0]) {
            _FDT(fdt_setprop_string(fdt, chosen, "bootargs",
                                    machine->kernel_cmdline));
        }

        if (spapr->initrd_size) {
            _FDT(fdt_setprop_cell(fdt, chosen, "linux,initrd-start",
                                  spapr->initrd_base));
            _FDT(fdt_setprop_cell(fdt, chosen, "linux,initrd-end",
                                  spapr->initrd_base + spapr->initrd_size));
        }

        if (spapr->kernel_size) {
            uint64_t kprop[2] = { cpu_to_be64(spapr->kernel_addr),
                                  cpu_to_be64(spapr->kernel_size) };

            _FDT(fdt_setprop(fdt, chosen, "qemu,boot-kernel",
                         &kprop, sizeof(kprop)));
            if (spapr->kernel_le) {
                _FDT(fdt_setprop(fdt, chosen, "qemu,boot-kernel-le", NULL, 0));
            }
        }
        if (machine->boot_config.has_menu && machine->boot_config.menu) {
            _FDT((fdt_setprop_cell(fdt, chosen, "qemu,boot-menu", true)));
        }
        _FDT(fdt_setprop_cell(fdt, chosen, "qemu,graphic-width", graphic_width));
        _FDT(fdt_setprop_cell(fdt, chosen, "qemu,graphic-height", graphic_height));
        _FDT(fdt_setprop_cell(fdt, chosen, "qemu,graphic-depth", graphic_depth));

        if (cb && bootlist) {
            int i;

            for (i = 0; i < cb; i++) {
                if (bootlist[i] == '\n') {
                    bootlist[i] = ' ';
                }
            }
            _FDT(fdt_setprop_string(fdt, chosen, "qemu,boot-list", bootlist));
        }

        if (boot_device && strlen(boot_device)) {
            _FDT(fdt_setprop_string(fdt, chosen, "qemu,boot-device", boot_device));
        }

        if (spapr->want_stdout_path && stdout_path) {
            /*
             * "linux,stdout-path" and "stdout" properties are
             * deprecated by linux kernel. New platforms should only
             * use the "stdout-path" property. Set the new property
             * and continue using older property to remain compatible
             * with the existing firmware.
             */
            _FDT(fdt_setprop_string(fdt, chosen, "linux,stdout-path", stdout_path));
            _FDT(fdt_setprop_string(fdt, chosen, "stdout-path", stdout_path));
        }

        /*
         * We can deal with BAR reallocation just fine, advertise it
         * to the guest
         */
        if (smc->linux_pci_probe) {
            _FDT(fdt_setprop_cell(fdt, chosen, "linux,pci-probe-only", 0));
        }

        spapr_dt_ov5_platform_support(spapr, fdt, chosen);
    }

    _FDT(fdt_setprop(fdt, chosen, "rng-seed", spapr->fdt_rng_seed, 32));

    _FDT(spapr_dt_ovec(fdt, chosen, spapr->ov5_cas, "ibm,architecture-vec-5"));
}

static void spapr_dt_hypervisor(SpaprMachineState *spapr, void *fdt)
{
    /* The /hypervisor node isn't in PAPR - this is a hack to allow PR
     * KVM to work under pHyp with some guest co-operation */
    int hypervisor;
    uint8_t hypercall[16];

    _FDT(hypervisor = fdt_add_subnode(fdt, 0, "hypervisor"));
    /* indicate KVM hypercall interface */
    _FDT(fdt_setprop_string(fdt, hypervisor, "compatible", "linux,kvm"));
    if (kvmppc_has_cap_fixup_hcalls()) {
        /*
         * Older KVM versions with older guest kernels were broken
         * with the magic page, don't allow the guest to map it.
         */
        if (!kvmppc_get_hypercall(cpu_env(first_cpu), hypercall,
                                  sizeof(hypercall))) {
            _FDT(fdt_setprop(fdt, hypervisor, "hcall-instructions",
                             hypercall, sizeof(hypercall)));
        }
    }
}

void *spapr_build_fdt(SpaprMachineState *spapr, bool reset, size_t space)
{
    MachineState *machine = MACHINE(spapr);
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    SpaprMachineClass *smc = SPAPR_MACHINE_GET_CLASS(machine);
    uint32_t root_drc_type_mask = 0;
    int ret;
    void *fdt;
    SpaprPhbState *phb;
    char *buf;

    fdt = g_malloc0(space);
    _FDT((fdt_create_empty_tree(fdt, space)));

    /* Root node */
    _FDT(fdt_setprop_string(fdt, 0, "device_type", "chrp"));
    _FDT(fdt_setprop_string(fdt, 0, "model", "IBM pSeries (emulated by qemu)"));
    _FDT(fdt_setprop_string(fdt, 0, "compatible", "qemu,pseries"));

    /* Guest UUID & Name*/
    buf = qemu_uuid_unparse_strdup(&qemu_uuid);
    _FDT(fdt_setprop_string(fdt, 0, "vm,uuid", buf));
    if (qemu_uuid_set) {
        _FDT(fdt_setprop_string(fdt, 0, "system-id", buf));
    }
    g_free(buf);

    if (qemu_get_vm_name()) {
        _FDT(fdt_setprop_string(fdt, 0, "ibm,partition-name",
                                qemu_get_vm_name()));
    }

    /* Host Model & Serial Number */
    if (spapr->host_model) {
        _FDT(fdt_setprop_string(fdt, 0, "host-model", spapr->host_model));
    } else if (smc->broken_host_serial_model && kvmppc_get_host_model(&buf)) {
        _FDT(fdt_setprop_string(fdt, 0, "host-model", buf));
        g_free(buf);
    }

    if (spapr->host_serial) {
        _FDT(fdt_setprop_string(fdt, 0, "host-serial", spapr->host_serial));
    } else if (smc->broken_host_serial_model && kvmppc_get_host_serial(&buf)) {
        _FDT(fdt_setprop_string(fdt, 0, "host-serial", buf));
        g_free(buf);
    }

    _FDT(fdt_setprop_cell(fdt, 0, "#address-cells", 2));
    _FDT(fdt_setprop_cell(fdt, 0, "#size-cells", 2));

    /* /interrupt controller */
    spapr_irq_dt(spapr, spapr_max_server_number(spapr), fdt, PHANDLE_INTC);

    ret = spapr_dt_memory(spapr, fdt);
    if (ret < 0) {
        error_report("couldn't setup memory nodes in fdt");
        exit(1);
    }

    /* /vdevice */
    spapr_dt_vdevice(spapr->vio_bus, fdt);

    if (object_resolve_path_type("", TYPE_SPAPR_RNG, NULL)) {
        ret = spapr_dt_rng(fdt);
        if (ret < 0) {
            error_report("could not set up rng device in the fdt");
            exit(1);
        }
    }

    QLIST_FOREACH(phb, &spapr->phbs, list) {
        ret = spapr_dt_phb(spapr, phb, PHANDLE_INTC, fdt, NULL);
        if (ret < 0) {
            error_report("couldn't setup PCI devices in fdt");
            exit(1);
        }
    }

    spapr_dt_cpus(fdt, spapr);

    /* ibm,drc-indexes and friends */
    if (smc->dr_lmb_enabled) {
        root_drc_type_mask |= SPAPR_DR_CONNECTOR_TYPE_LMB;
    }
    if (smc->dr_phb_enabled) {
        root_drc_type_mask |= SPAPR_DR_CONNECTOR_TYPE_PHB;
    }
    if (mc->nvdimm_supported) {
        root_drc_type_mask |= SPAPR_DR_CONNECTOR_TYPE_PMEM;
    }
    if (root_drc_type_mask) {
        _FDT(spapr_dt_drc(fdt, 0, NULL, root_drc_type_mask));
    }

    if (mc->has_hotpluggable_cpus) {
        int offset = fdt_path_offset(fdt, "/cpus");
        ret = spapr_dt_drc(fdt, offset, NULL, SPAPR_DR_CONNECTOR_TYPE_CPU);
        if (ret < 0) {
            error_report("Couldn't set up CPU DR device tree properties");
            exit(1);
        }
    }

    /* /event-sources */
    spapr_dt_events(spapr, fdt);

    /* /rtas */
    spapr_dt_rtas(spapr, fdt);

    /* /chosen */
    spapr_dt_chosen(spapr, fdt, reset);

    /* /hypervisor */
    if (kvm_enabled()) {
        spapr_dt_hypervisor(spapr, fdt);
    }

    /* Build memory reserve map */
    if (reset) {
        if (spapr->kernel_size) {
            _FDT((fdt_add_mem_rsv(fdt, spapr->kernel_addr,
                                  spapr->kernel_size)));
        }
        if (spapr->initrd_size) {
            _FDT((fdt_add_mem_rsv(fdt, spapr->initrd_base,
                                  spapr->initrd_size)));
        }
    }

    /* NVDIMM devices */
    if (mc->nvdimm_supported) {
        spapr_dt_persistent_memory(spapr, fdt);
    }

    return fdt;
}

static uint64_t translate_kernel_address(void *opaque, uint64_t addr)
{
    SpaprMachineState *spapr = opaque;

    return (addr & 0x0fffffff) + spapr->kernel_addr;
}

static void emulate_spapr_hypercall(PPCVirtualHypervisor *vhyp,
                                    PowerPCCPU *cpu)
{
    CPUPPCState *env = &cpu->env;

    /* The TCG path should also be holding the BQL at this point */
    g_assert(qemu_mutex_iothread_locked());

    g_assert(!vhyp_cpu_in_nested(cpu));

    if (FIELD_EX64(env->msr, MSR, PR)) {
        hcall_dprintf("Hypercall made with MSR[PR]=1\n");
        env->gpr[3] = H_PRIVILEGE;
    } else {
        env->gpr[3] = spapr_hypercall(cpu, env->gpr[3], &env->gpr[4]);
    }
}

struct LPCRSyncState {
    target_ulong value;
    target_ulong mask;
};

static void do_lpcr_sync(CPUState *cs, run_on_cpu_data arg)
{
    struct LPCRSyncState *s = arg.host_ptr;
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;
    target_ulong lpcr;

    cpu_synchronize_state(cs);
    lpcr = env->spr[SPR_LPCR];
    lpcr &= ~s->mask;
    lpcr |= s->value;
    ppc_store_lpcr(cpu, lpcr);
}

void spapr_set_all_lpcrs(target_ulong value, target_ulong mask)
{
    CPUState *cs;
    struct LPCRSyncState s = {
        .value = value,
        .mask = mask
    };
    CPU_FOREACH(cs) {
        run_on_cpu(cs, do_lpcr_sync, RUN_ON_CPU_HOST_PTR(&s));
    }
}

/* May be used when the machine is not running */
void spapr_init_all_lpcrs(target_ulong value, target_ulong mask)
{
    CPUState *cs;
    CPU_FOREACH(cs) {
        PowerPCCPU *cpu = POWERPC_CPU(cs);
        CPUPPCState *env = &cpu->env;
        target_ulong lpcr;

        lpcr = env->spr[SPR_LPCR];
        lpcr &= ~(LPCR_HR | LPCR_UPRT);
        ppc_store_lpcr(cpu, lpcr);
    }
}


static bool spapr_get_pate(PPCVirtualHypervisor *vhyp, PowerPCCPU *cpu,
                           target_ulong lpid, ppc_v3_pate_t *entry)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(vhyp);
    SpaprCpuState *spapr_cpu = spapr_cpu_state(cpu);

    if (!spapr_cpu->in_nested) {
        assert(lpid == 0);

        /* Copy PATE1:GR into PATE0:HR */
        entry->dw0 = spapr->patb_entry & PATE0_HR;
        entry->dw1 = spapr->patb_entry;

    } else {
        uint64_t patb, pats;

        assert(lpid != 0);

        patb = spapr->nested_ptcr & PTCR_PATB;
        pats = spapr->nested_ptcr & PTCR_PATS;

        /* Check if partition table is properly aligned */
        if (patb & MAKE_64BIT_MASK(0, pats + 12)) {
            return false;
        }

        /* Calculate number of entries */
        pats = 1ull << (pats + 12 - 4);
        if (pats <= lpid) {
            return false;
        }

        /* Grab entry */
        patb += 16 * lpid;
        entry->dw0 = ldq_phys(CPU(cpu)->as, patb);
        entry->dw1 = ldq_phys(CPU(cpu)->as, patb + 8);
    }

    return true;
}

#define HPTE(_table, _i)   (void *)(((uint64_t *)(_table)) + ((_i) * 2))
#define HPTE_VALID(_hpte)  (tswap64(*((uint64_t *)(_hpte))) & HPTE64_V_VALID)
#define HPTE_DIRTY(_hpte)  (tswap64(*((uint64_t *)(_hpte))) & HPTE64_V_HPTE_DIRTY)
#define CLEAN_HPTE(_hpte)  ((*(uint64_t *)(_hpte)) &= tswap64(~HPTE64_V_HPTE_DIRTY))
#define DIRTY_HPTE(_hpte)  ((*(uint64_t *)(_hpte)) |= tswap64(HPTE64_V_HPTE_DIRTY))

/*
 * Get the fd to access the kernel htab, re-opening it if necessary
 */
static int get_htab_fd(SpaprMachineState *spapr)
{
    Error *local_err = NULL;

    if (spapr->htab_fd >= 0) {
        return spapr->htab_fd;
    }

    spapr->htab_fd = kvmppc_get_htab_fd(false, 0, &local_err);
    if (spapr->htab_fd < 0) {
        error_report_err(local_err);
    }

    return spapr->htab_fd;
}

void close_htab_fd(SpaprMachineState *spapr)
{
    if (spapr->htab_fd >= 0) {
        close(spapr->htab_fd);
    }
    spapr->htab_fd = -1;
}

static hwaddr spapr_hpt_mask(PPCVirtualHypervisor *vhyp)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(vhyp);

    return HTAB_SIZE(spapr) / HASH_PTEG_SIZE_64 - 1;
}

static target_ulong spapr_encode_hpt_for_kvm_pr(PPCVirtualHypervisor *vhyp)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(vhyp);

    assert(kvm_enabled());

    if (!spapr->htab) {
        return 0;
    }

    return (target_ulong)(uintptr_t)spapr->htab | (spapr->htab_shift - 18);
}

static const ppc_hash_pte64_t *spapr_map_hptes(PPCVirtualHypervisor *vhyp,
                                                hwaddr ptex, int n)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(vhyp);
    hwaddr pte_offset = ptex * HASH_PTE_SIZE_64;

    if (!spapr->htab) {
        /*
         * HTAB is controlled by KVM. Fetch into temporary buffer
         */
        ppc_hash_pte64_t *hptes = g_malloc(n * HASH_PTE_SIZE_64);
        kvmppc_read_hptes(hptes, ptex, n);
        return hptes;
    }

    /*
     * HTAB is controlled by QEMU. Just point to the internally
     * accessible PTEG.
     */
    return (const ppc_hash_pte64_t *)(spapr->htab + pte_offset);
}

static void spapr_unmap_hptes(PPCVirtualHypervisor *vhyp,
                              const ppc_hash_pte64_t *hptes,
                              hwaddr ptex, int n)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(vhyp);

    if (!spapr->htab) {
        g_free((void *)hptes);
    }

    /* Nothing to do for qemu managed HPT */
}

void spapr_store_hpte(PowerPCCPU *cpu, hwaddr ptex,
                      uint64_t pte0, uint64_t pte1)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(cpu->vhyp);
    hwaddr offset = ptex * HASH_PTE_SIZE_64;

    if (!spapr->htab) {
        kvmppc_write_hpte(ptex, pte0, pte1);
    } else {
        if (pte0 & HPTE64_V_VALID) {
            stq_p(spapr->htab + offset + HPTE64_DW1, pte1);
            /*
             * When setting valid, we write PTE1 first. This ensures
             * proper synchronization with the reading code in
             * ppc_hash64_pteg_search()
             */
            smp_wmb();
            stq_p(spapr->htab + offset, pte0);
        } else {
            stq_p(spapr->htab + offset, pte0);
            /*
             * When clearing it we set PTE0 first. This ensures proper
             * synchronization with the reading code in
             * ppc_hash64_pteg_search()
             */
            smp_wmb();
            stq_p(spapr->htab + offset + HPTE64_DW1, pte1);
        }
    }
}

static void spapr_hpte_set_c(PPCVirtualHypervisor *vhyp, hwaddr ptex,
                             uint64_t pte1)
{
    hwaddr offset = ptex * HASH_PTE_SIZE_64 + HPTE64_DW1_C;
    SpaprMachineState *spapr = SPAPR_MACHINE(vhyp);

    if (!spapr->htab) {
        /* There should always be a hash table when this is called */
        error_report("spapr_hpte_set_c called with no hash table !");
        return;
    }

    /* The HW performs a non-atomic byte update */
    stb_p(spapr->htab + offset, (pte1 & 0xff) | 0x80);
}

static void spapr_hpte_set_r(PPCVirtualHypervisor *vhyp, hwaddr ptex,
                             uint64_t pte1)
{
    hwaddr offset = ptex * HASH_PTE_SIZE_64 + HPTE64_DW1_R;
    SpaprMachineState *spapr = SPAPR_MACHINE(vhyp);

    if (!spapr->htab) {
        /* There should always be a hash table when this is called */
        error_report("spapr_hpte_set_r called with no hash table !");
        return;
    }

    /* The HW performs a non-atomic byte update */
    stb_p(spapr->htab + offset, ((pte1 >> 8) & 0xff) | 0x01);
}

int spapr_hpt_shift_for_ramsize(uint64_t ramsize)
{
    int shift;

    /* We aim for a hash table of size 1/128 the size of RAM (rounded
     * up).  The PAPR recommendation is actually 1/64 of RAM size, but
     * that's much more than is needed for Linux guests */
    shift = ctz64(pow2ceil(ramsize)) - 7;
    shift = MAX(shift, 18); /* Minimum architected size */
    shift = MIN(shift, 46); /* Maximum architected size */
    return shift;
}

void spapr_free_hpt(SpaprMachineState *spapr)
{
    qemu_vfree(spapr->htab);
    spapr->htab = NULL;
    spapr->htab_shift = 0;
    close_htab_fd(spapr);
}

int spapr_reallocate_hpt(SpaprMachineState *spapr, int shift, Error **errp)
{
    ERRP_GUARD();
    long rc;

    /* Clean up any HPT info from a previous boot */
    spapr_free_hpt(spapr);

    rc = kvmppc_reset_htab(shift);

    if (rc == -EOPNOTSUPP) {
        error_setg(errp, "HPT not supported in nested guests");
        return -EOPNOTSUPP;
    }

    if (rc < 0) {
        /* kernel-side HPT needed, but couldn't allocate one */
        error_setg_errno(errp, errno, "Failed to allocate KVM HPT of order %d",
                         shift);
        error_append_hint(errp, "Try smaller maxmem?\n");
        return -errno;
    } else if (rc > 0) {
        /* kernel-side HPT allocated */
        if (rc != shift) {
            error_setg(errp,
                       "Requested order %d HPT, but kernel allocated order %ld",
                       shift, rc);
            error_append_hint(errp, "Try smaller maxmem?\n");
            return -ENOSPC;
        }

        spapr->htab_shift = shift;
        spapr->htab = NULL;
    } else {
        /* kernel-side HPT not needed, allocate in userspace instead */
        size_t size = 1ULL << shift;
        int i;

        spapr->htab = qemu_memalign(size, size);
        memset(spapr->htab, 0, size);
        spapr->htab_shift = shift;

        for (i = 0; i < size / HASH_PTE_SIZE_64; i++) {
            DIRTY_HPTE(HPTE(spapr->htab, i));
        }
    }
    /* We're setting up a hash table, so that means we're not radix */
    spapr->patb_entry = 0;
    spapr_init_all_lpcrs(0, LPCR_HR | LPCR_UPRT);
    return 0;
}

void spapr_setup_hpt(SpaprMachineState *spapr)
{
    int hpt_shift;

    if (spapr->resize_hpt == SPAPR_RESIZE_HPT_DISABLED) {
        hpt_shift = spapr_hpt_shift_for_ramsize(MACHINE(spapr)->maxram_size);
    } else {
        uint64_t current_ram_size;

        current_ram_size = MACHINE(spapr)->ram_size + get_plugged_memory_size();
        hpt_shift = spapr_hpt_shift_for_ramsize(current_ram_size);
    }
    spapr_reallocate_hpt(spapr, hpt_shift, &error_fatal);

    if (kvm_enabled()) {
        hwaddr vrma_limit = kvmppc_vrma_limit(spapr->htab_shift);

        /* Check our RMA fits in the possible VRMA */
        if (vrma_limit < spapr->rma_size) {
            error_report("Unable to create %" HWADDR_PRIu
                         "MiB RMA (VRMA only allows %" HWADDR_PRIu "MiB",
                         spapr->rma_size / MiB, vrma_limit / MiB);
            exit(EXIT_FAILURE);
        }
    }
}

void spapr_check_mmu_mode(bool guest_radix)
{
    if (guest_radix) {
        if (kvm_enabled() && !kvmppc_has_cap_mmu_radix()) {
            error_report("Guest requested unavailable MMU mode (radix).");
            exit(EXIT_FAILURE);
        }
    } else {
        if (kvm_enabled() && kvmppc_has_cap_mmu_radix()
            && !kvmppc_has_cap_mmu_hash_v3()) {
            error_report("Guest requested unavailable MMU mode (hash).");
            exit(EXIT_FAILURE);
        }
    }
}

static void spapr_machine_reset(MachineState *machine, ShutdownCause reason)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(machine);
    PowerPCCPU *first_ppc_cpu;
    hwaddr fdt_addr;
    void *fdt;
    int rc;

    if (reason != SHUTDOWN_CAUSE_SNAPSHOT_LOAD) {
        /*
         * Record-replay snapshot load must not consume random, this was
         * already replayed from initial machine reset.
         */
        qemu_guest_getrandom_nofail(spapr->fdt_rng_seed, 32);
    }

    pef_kvm_reset(machine->cgs, &error_fatal);
    spapr_caps_apply(spapr);

    first_ppc_cpu = POWERPC_CPU(first_cpu);
    if (kvm_enabled() && kvmppc_has_cap_mmu_radix() &&
        ppc_type_check_compat(machine->cpu_type, CPU_POWERPC_LOGICAL_3_00, 0,
                              spapr->max_compat_pvr)) {
        /*
         * If using KVM with radix mode available, VCPUs can be started
         * without a HPT because KVM will start them in radix mode.
         * Set the GR bit in PATE so that we know there is no HPT.
         */
        spapr->patb_entry = PATE1_GR;
        spapr_set_all_lpcrs(LPCR_HR | LPCR_UPRT, LPCR_HR | LPCR_UPRT);
    } else {
        spapr_setup_hpt(spapr);
    }

    qemu_devices_reset(reason);

    spapr_ovec_cleanup(spapr->ov5_cas);
    spapr->ov5_cas = spapr_ovec_new();

    ppc_init_compat_all(spapr->max_compat_pvr, &error_fatal);

    /*
     * This is fixing some of the default configuration of the XIVE
     * devices. To be called after the reset of the machine devices.
     */
    spapr_irq_reset(spapr, &error_fatal);

    /*
     * There is no CAS under qtest. Simulate one to please the code that
     * depends on spapr->ov5_cas. This is especially needed to test device
     * unplug, so we do that before resetting the DRCs.
     */
    if (qtest_enabled()) {
        spapr_ovec_cleanup(spapr->ov5_cas);
        spapr->ov5_cas = spapr_ovec_clone(spapr->ov5);
    }

    spapr_nvdimm_finish_flushes();

    /* DRC reset may cause a device to be unplugged. This will cause troubles
     * if this device is used by another device (eg, a running vhost backend
     * will crash QEMU if the DIMM holding the vring goes away). To avoid such
     * situations, we reset DRCs after all devices have been reset.
     */
    spapr_drc_reset_all(spapr);

    spapr_clear_pending_events(spapr);

    /*
     * We place the device tree just below either the top of the RMA,
     * or just below 2GB, whichever is lower, so that it can be
     * processed with 32-bit real mode code if necessary
     */
    fdt_addr = MIN(spapr->rma_size, FDT_MAX_ADDR) - FDT_MAX_SIZE;

    fdt = spapr_build_fdt(spapr, true, FDT_MAX_SIZE);
    if (spapr->vof) {
        spapr_vof_reset(spapr, fdt, &error_fatal);
        /*
         * Do not pack the FDT as the client may change properties.
         * VOF client does not expect the FDT so we do not load it to the VM.
         */
    } else {
        rc = fdt_pack(fdt);
        /* Should only fail if we've built a corrupted tree */
        assert(rc == 0);

        spapr_cpu_set_entry_state(first_ppc_cpu, SPAPR_ENTRY_POINT,
                                  0, fdt_addr, 0);
        cpu_physical_memory_write(fdt_addr, fdt, fdt_totalsize(fdt));
    }
    qemu_fdt_dumpdtb(fdt, fdt_totalsize(fdt));

    g_free(spapr->fdt_blob);
    spapr->fdt_size = fdt_totalsize(fdt);
    spapr->fdt_initial_size = spapr->fdt_size;
    spapr->fdt_blob = fdt;

    /* Set machine->fdt for 'dumpdtb' QMP/HMP command */
    machine->fdt = fdt;

    /* Set up the entry state */
    first_ppc_cpu->env.gpr[5] = 0;

    spapr->fwnmi_system_reset_addr = -1;
    spapr->fwnmi_machine_check_addr = -1;
    spapr->fwnmi_machine_check_interlock = -1;

    /* Signal all vCPUs waiting on this condition */
    qemu_cond_broadcast(&spapr->fwnmi_machine_check_interlock_cond);

    migrate_del_blocker(&spapr->fwnmi_migration_blocker);
}

static void spapr_create_nvram(SpaprMachineState *spapr)
{
    DeviceState *dev = qdev_new("spapr-nvram");
    DriveInfo *dinfo = drive_get(IF_PFLASH, 0, 0);

    if (dinfo) {
        qdev_prop_set_drive_err(dev, "drive", blk_by_legacy_dinfo(dinfo),
                                &error_fatal);
    }

    qdev_realize_and_unref(dev, &spapr->vio_bus->bus, &error_fatal);

    spapr->nvram = (struct SpaprNvram *)dev;
}

static void spapr_rtc_create(SpaprMachineState *spapr)
{
    object_initialize_child_with_props(OBJECT(spapr), "rtc", &spapr->rtc,
                                       sizeof(spapr->rtc), TYPE_SPAPR_RTC,
                                       &error_fatal, NULL);
    qdev_realize(DEVICE(&spapr->rtc), NULL, &error_fatal);
    object_property_add_alias(OBJECT(spapr), "rtc-time", OBJECT(&spapr->rtc),
                              "date");
}

/* Returns whether we want to use VGA or not */
static bool spapr_vga_init(PCIBus *pci_bus, Error **errp)
{
    vga_interface_created = true;
    switch (vga_interface_type) {
    case VGA_NONE:
        return false;
    case VGA_DEVICE:
        return true;
    case VGA_STD:
    case VGA_VIRTIO:
    case VGA_CIRRUS:
        return pci_vga_init(pci_bus) != NULL;
    default:
        error_setg(errp,
                   "Unsupported VGA mode, only -vga std or -vga virtio is supported");
        return false;
    }
}

static int spapr_pre_load(void *opaque)
{
    int rc;

    rc = spapr_caps_pre_load(opaque);
    if (rc) {
        return rc;
    }

    return 0;
}

static int spapr_post_load(void *opaque, int version_id)
{
    SpaprMachineState *spapr = (SpaprMachineState *)opaque;
    int err = 0;

    err = spapr_caps_post_migration(spapr);
    if (err) {
        return err;
    }

    /*
     * In earlier versions, there was no separate qdev for the PAPR
     * RTC, so the RTC offset was stored directly in sPAPREnvironment.
     * So when migrating from those versions, poke the incoming offset
     * value into the RTC device
     */
    if (version_id < 3) {
        err = spapr_rtc_import_offset(&spapr->rtc, spapr->rtc_offset);
        if (err) {
            return err;
        }
    }

    if (kvm_enabled() && spapr->patb_entry) {
        PowerPCCPU *cpu = POWERPC_CPU(first_cpu);
        bool radix = !!(spapr->patb_entry & PATE1_GR);
        bool gtse = !!(cpu->env.spr[SPR_LPCR] & LPCR_GTSE);

        /*
         * Update LPCR:HR and UPRT as they may not be set properly in
         * the stream
         */
        spapr_set_all_lpcrs(radix ? (LPCR_HR | LPCR_UPRT) : 0,
                            LPCR_HR | LPCR_UPRT);

        err = kvmppc_configure_v3_mmu(cpu, radix, gtse, spapr->patb_entry);
        if (err) {
            error_report("Process table config unsupported by the host");
            return -EINVAL;
        }
    }

    err = spapr_irq_post_load(spapr, version_id);
    if (err) {
        return err;
    }

    return err;
}

static int spapr_pre_save(void *opaque)
{
    int rc;

    rc = spapr_caps_pre_save(opaque);
    if (rc) {
        return rc;
    }

    return 0;
}

static bool version_before_3(void *opaque, int version_id)
{
    return version_id < 3;
}

static bool spapr_pending_events_needed(void *opaque)
{
    SpaprMachineState *spapr = (SpaprMachineState *)opaque;
    return !QTAILQ_EMPTY(&spapr->pending_events);
}

static const VMStateDescription vmstate_spapr_event_entry = {
    .name = "spapr_event_log_entry",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(summary, SpaprEventLogEntry),
        VMSTATE_UINT32(extended_length, SpaprEventLogEntry),
        VMSTATE_VBUFFER_ALLOC_UINT32(extended_log, SpaprEventLogEntry, 0,
                                     NULL, extended_length),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_spapr_pending_events = {
    .name = "spapr_pending_events",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = spapr_pending_events_needed,
    .fields = (VMStateField[]) {
        VMSTATE_QTAILQ_V(pending_events, SpaprMachineState, 1,
                         vmstate_spapr_event_entry, SpaprEventLogEntry, next),
        VMSTATE_END_OF_LIST()
    },
};

static bool spapr_ov5_cas_needed(void *opaque)
{
    SpaprMachineState *spapr = opaque;
    SpaprOptionVector *ov5_mask = spapr_ovec_new();
    bool cas_needed;

    /* Prior to the introduction of SpaprOptionVector, we had two option
     * vectors we dealt with: OV5_FORM1_AFFINITY, and OV5_DRCONF_MEMORY.
     * Both of these options encode machine topology into the device-tree
     * in such a way that the now-booted OS should still be able to interact
     * appropriately with QEMU regardless of what options were actually
     * negotiatied on the source side.
     *
     * As such, we can avoid migrating the CAS-negotiated options if these
     * are the only options available on the current machine/platform.
     * Since these are the only options available for pseries-2.7 and
     * earlier, this allows us to maintain old->new/new->old migration
     * compatibility.
     *
     * For QEMU 2.8+, there are additional CAS-negotiatable options available
     * via default pseries-2.8 machines and explicit command-line parameters.
     * Some of these options, like OV5_HP_EVT, *do* require QEMU to be aware
     * of the actual CAS-negotiated values to continue working properly. For
     * example, availability of memory unplug depends on knowing whether
     * OV5_HP_EVT was negotiated via CAS.
     *
     * Thus, for any cases where the set of available CAS-negotiatable
     * options extends beyond OV5_FORM1_AFFINITY and OV5_DRCONF_MEMORY, we
     * include the CAS-negotiated options in the migration stream, unless
     * if they affect boot time behaviour only.
     */
    spapr_ovec_set(ov5_mask, OV5_FORM1_AFFINITY);
    spapr_ovec_set(ov5_mask, OV5_DRCONF_MEMORY);
    spapr_ovec_set(ov5_mask, OV5_DRMEM_V2);

    /* We need extra information if we have any bits outside the mask
     * defined above */
    cas_needed = !spapr_ovec_subset(spapr->ov5, ov5_mask);

    spapr_ovec_cleanup(ov5_mask);

    return cas_needed;
}

static const VMStateDescription vmstate_spapr_ov5_cas = {
    .name = "spapr_option_vector_ov5_cas",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = spapr_ov5_cas_needed,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT_POINTER_V(ov5_cas, SpaprMachineState, 1,
                                 vmstate_spapr_ovec, SpaprOptionVector),
        VMSTATE_END_OF_LIST()
    },
};

static bool spapr_patb_entry_needed(void *opaque)
{
    SpaprMachineState *spapr = opaque;

    return !!spapr->patb_entry;
}

static const VMStateDescription vmstate_spapr_patb_entry = {
    .name = "spapr_patb_entry",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = spapr_patb_entry_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(patb_entry, SpaprMachineState),
        VMSTATE_END_OF_LIST()
    },
};

static bool spapr_irq_map_needed(void *opaque)
{
    SpaprMachineState *spapr = opaque;

    return spapr->irq_map && !bitmap_empty(spapr->irq_map, spapr->irq_map_nr);
}

static const VMStateDescription vmstate_spapr_irq_map = {
    .name = "spapr_irq_map",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = spapr_irq_map_needed,
    .fields = (VMStateField[]) {
        VMSTATE_BITMAP(irq_map, SpaprMachineState, 0, irq_map_nr),
        VMSTATE_END_OF_LIST()
    },
};

static bool spapr_dtb_needed(void *opaque)
{
    SpaprMachineClass *smc = SPAPR_MACHINE_GET_CLASS(opaque);

    return smc->update_dt_enabled;
}

static int spapr_dtb_pre_load(void *opaque)
{
    SpaprMachineState *spapr = (SpaprMachineState *)opaque;

    g_free(spapr->fdt_blob);
    spapr->fdt_blob = NULL;
    spapr->fdt_size = 0;

    return 0;
}

static const VMStateDescription vmstate_spapr_dtb = {
    .name = "spapr_dtb",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = spapr_dtb_needed,
    .pre_load = spapr_dtb_pre_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(fdt_initial_size, SpaprMachineState),
        VMSTATE_UINT32(fdt_size, SpaprMachineState),
        VMSTATE_VBUFFER_ALLOC_UINT32(fdt_blob, SpaprMachineState, 0, NULL,
                                     fdt_size),
        VMSTATE_END_OF_LIST()
    },
};

static bool spapr_fwnmi_needed(void *opaque)
{
    SpaprMachineState *spapr = (SpaprMachineState *)opaque;

    return spapr->fwnmi_machine_check_addr != -1;
}

static int spapr_fwnmi_pre_save(void *opaque)
{
    SpaprMachineState *spapr = (SpaprMachineState *)opaque;

    /*
     * Check if machine check handling is in progress and print a
     * warning message.
     */
    if (spapr->fwnmi_machine_check_interlock != -1) {
        warn_report("A machine check is being handled during migration. The"
                "handler may run and log hardware error on the destination");
    }

    return 0;
}

static const VMStateDescription vmstate_spapr_fwnmi = {
    .name = "spapr_fwnmi",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = spapr_fwnmi_needed,
    .pre_save = spapr_fwnmi_pre_save,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(fwnmi_system_reset_addr, SpaprMachineState),
        VMSTATE_UINT64(fwnmi_machine_check_addr, SpaprMachineState),
        VMSTATE_INT32(fwnmi_machine_check_interlock, SpaprMachineState),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_spapr = {
    .name = "spapr",
    .version_id = 3,
    .minimum_version_id = 1,
    .pre_load = spapr_pre_load,
    .post_load = spapr_post_load,
    .pre_save = spapr_pre_save,
    .fields = (VMStateField[]) {
        /* used to be @next_irq */
        VMSTATE_UNUSED_BUFFER(version_before_3, 0, 4),

        /* RTC offset */
        VMSTATE_UINT64_TEST(rtc_offset, SpaprMachineState, version_before_3),

        VMSTATE_PPC_TIMEBASE_V(tb, SpaprMachineState, 2),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription*[]) {
        &vmstate_spapr_ov5_cas,
        &vmstate_spapr_patb_entry,
        &vmstate_spapr_pending_events,
        &vmstate_spapr_cap_htm,
        &vmstate_spapr_cap_vsx,
        &vmstate_spapr_cap_dfp,
        &vmstate_spapr_cap_cfpc,
        &vmstate_spapr_cap_sbbc,
        &vmstate_spapr_cap_ibs,
        &vmstate_spapr_cap_hpt_maxpagesize,
        &vmstate_spapr_irq_map,
        &vmstate_spapr_cap_nested_kvm_hv,
        &vmstate_spapr_dtb,
        &vmstate_spapr_cap_large_decr,
        &vmstate_spapr_cap_ccf_assist,
        &vmstate_spapr_cap_fwnmi,
        &vmstate_spapr_fwnmi,
        &vmstate_spapr_cap_rpt_invalidate,
        NULL
    }
};

static int htab_save_setup(QEMUFile *f, void *opaque)
{
    SpaprMachineState *spapr = opaque;

    /* "Iteration" header */
    if (!spapr->htab_shift) {
        qemu_put_be32(f, -1);
    } else {
        qemu_put_be32(f, spapr->htab_shift);
    }

    if (spapr->htab) {
        spapr->htab_save_index = 0;
        spapr->htab_first_pass = true;
    } else {
        if (spapr->htab_shift) {
            assert(kvm_enabled());
        }
    }


    return 0;
}

static void htab_save_chunk(QEMUFile *f, SpaprMachineState *spapr,
                            int chunkstart, int n_valid, int n_invalid)
{
    qemu_put_be32(f, chunkstart);
    qemu_put_be16(f, n_valid);
    qemu_put_be16(f, n_invalid);
    qemu_put_buffer(f, HPTE(spapr->htab, chunkstart),
                    HASH_PTE_SIZE_64 * n_valid);
}

static void htab_save_end_marker(QEMUFile *f)
{
    qemu_put_be32(f, 0);
    qemu_put_be16(f, 0);
    qemu_put_be16(f, 0);
}

static void htab_save_first_pass(QEMUFile *f, SpaprMachineState *spapr,
                                 int64_t max_ns)
{
    bool has_timeout = max_ns != -1;
    int htabslots = HTAB_SIZE(spapr) / HASH_PTE_SIZE_64;
    int index = spapr->htab_save_index;
    int64_t starttime = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

    assert(spapr->htab_first_pass);

    do {
        int chunkstart;

        /* Consume invalid HPTEs */
        while ((index < htabslots)
               && !HPTE_VALID(HPTE(spapr->htab, index))) {
            CLEAN_HPTE(HPTE(spapr->htab, index));
            index++;
        }

        /* Consume valid HPTEs */
        chunkstart = index;
        while ((index < htabslots) && (index - chunkstart < USHRT_MAX)
               && HPTE_VALID(HPTE(spapr->htab, index))) {
            CLEAN_HPTE(HPTE(spapr->htab, index));
            index++;
        }

        if (index > chunkstart) {
            int n_valid = index - chunkstart;

            htab_save_chunk(f, spapr, chunkstart, n_valid, 0);

            if (has_timeout &&
                (qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - starttime) > max_ns) {
                break;
            }
        }
    } while ((index < htabslots) && !migration_rate_exceeded(f));

    if (index >= htabslots) {
        assert(index == htabslots);
        index = 0;
        spapr->htab_first_pass = false;
    }
    spapr->htab_save_index = index;
}

static int htab_save_later_pass(QEMUFile *f, SpaprMachineState *spapr,
                                int64_t max_ns)
{
    bool final = max_ns < 0;
    int htabslots = HTAB_SIZE(spapr) / HASH_PTE_SIZE_64;
    int examined = 0, sent = 0;
    int index = spapr->htab_save_index;
    int64_t starttime = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

    assert(!spapr->htab_first_pass);

    do {
        int chunkstart, invalidstart;

        /* Consume non-dirty HPTEs */
        while ((index < htabslots)
               && !HPTE_DIRTY(HPTE(spapr->htab, index))) {
            index++;
            examined++;
        }

        chunkstart = index;
        /* Consume valid dirty HPTEs */
        while ((index < htabslots) && (index - chunkstart < USHRT_MAX)
               && HPTE_DIRTY(HPTE(spapr->htab, index))
               && HPTE_VALID(HPTE(spapr->htab, index))) {
            CLEAN_HPTE(HPTE(spapr->htab, index));
            index++;
            examined++;
        }

        invalidstart = index;
        /* Consume invalid dirty HPTEs */
        while ((index < htabslots) && (index - invalidstart < USHRT_MAX)
               && HPTE_DIRTY(HPTE(spapr->htab, index))
               && !HPTE_VALID(HPTE(spapr->htab, index))) {
            CLEAN_HPTE(HPTE(spapr->htab, index));
            index++;
            examined++;
        }

        if (index > chunkstart) {
            int n_valid = invalidstart - chunkstart;
            int n_invalid = index - invalidstart;

            htab_save_chunk(f, spapr, chunkstart, n_valid, n_invalid);
            sent += index - chunkstart;

            if (!final && (qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - starttime) > max_ns) {
                break;
            }
        }

        if (examined >= htabslots) {
            break;
        }

        if (index >= htabslots) {
            assert(index == htabslots);
            index = 0;
        }
    } while ((examined < htabslots) && (!migration_rate_exceeded(f) || final));

    if (index >= htabslots) {
        assert(index == htabslots);
        index = 0;
    }

    spapr->htab_save_index = index;

    return (examined >= htabslots) && (sent == 0) ? 1 : 0;
}

#define MAX_ITERATION_NS    5000000 /* 5 ms */
#define MAX_KVM_BUF_SIZE    2048

static int htab_save_iterate(QEMUFile *f, void *opaque)
{
    SpaprMachineState *spapr = opaque;
    int fd;
    int rc = 0;

    /* Iteration header */
    if (!spapr->htab_shift) {
        qemu_put_be32(f, -1);
        return 1;
    } else {
        qemu_put_be32(f, 0);
    }

    if (!spapr->htab) {
        assert(kvm_enabled());

        fd = get_htab_fd(spapr);
        if (fd < 0) {
            return fd;
        }

        rc = kvmppc_save_htab(f, fd, MAX_KVM_BUF_SIZE, MAX_ITERATION_NS);
        if (rc < 0) {
            return rc;
        }
    } else  if (spapr->htab_first_pass) {
        htab_save_first_pass(f, spapr, MAX_ITERATION_NS);
    } else {
        rc = htab_save_later_pass(f, spapr, MAX_ITERATION_NS);
    }

    htab_save_end_marker(f);

    return rc;
}

static int htab_save_complete(QEMUFile *f, void *opaque)
{
    SpaprMachineState *spapr = opaque;
    int fd;

    /* Iteration header */
    if (!spapr->htab_shift) {
        qemu_put_be32(f, -1);
        return 0;
    } else {
        qemu_put_be32(f, 0);
    }

    if (!spapr->htab) {
        int rc;

        assert(kvm_enabled());

        fd = get_htab_fd(spapr);
        if (fd < 0) {
            return fd;
        }

        rc = kvmppc_save_htab(f, fd, MAX_KVM_BUF_SIZE, -1);
        if (rc < 0) {
            return rc;
        }
    } else {
        if (spapr->htab_first_pass) {
            htab_save_first_pass(f, spapr, -1);
        }
        htab_save_later_pass(f, spapr, -1);
    }

    /* End marker */
    htab_save_end_marker(f);

    return 0;
}

static int htab_load(QEMUFile *f, void *opaque, int version_id)
{
    SpaprMachineState *spapr = opaque;
    uint32_t section_hdr;
    int fd = -1;
    Error *local_err = NULL;

    if (version_id < 1 || version_id > 1) {
        error_report("htab_load() bad version");
        return -EINVAL;
    }

    section_hdr = qemu_get_be32(f);

    if (section_hdr == -1) {
        spapr_free_hpt(spapr);
        return 0;
    }

    if (section_hdr) {
        int ret;

        /* First section gives the htab size */
        ret = spapr_reallocate_hpt(spapr, section_hdr, &local_err);
        if (ret < 0) {
            error_report_err(local_err);
            return ret;
        }
        return 0;
    }

    if (!spapr->htab) {
        assert(kvm_enabled());

        fd = kvmppc_get_htab_fd(true, 0, &local_err);
        if (fd < 0) {
            error_report_err(local_err);
            return fd;
        }
    }

    while (true) {
        uint32_t index;
        uint16_t n_valid, n_invalid;

        index = qemu_get_be32(f);
        n_valid = qemu_get_be16(f);
        n_invalid = qemu_get_be16(f);

        if ((index == 0) && (n_valid == 0) && (n_invalid == 0)) {
            /* End of Stream */
            break;
        }

        if ((index + n_valid + n_invalid) >
            (HTAB_SIZE(spapr) / HASH_PTE_SIZE_64)) {
            /* Bad index in stream */
            error_report(
                "htab_load() bad index %d (%hd+%hd entries) in htab stream (htab_shift=%d)",
                index, n_valid, n_invalid, spapr->htab_shift);
            return -EINVAL;
        }

        if (spapr->htab) {
            if (n_valid) {
                qemu_get_buffer(f, HPTE(spapr->htab, index),
                                HASH_PTE_SIZE_64 * n_valid);
            }
            if (n_invalid) {
                memset(HPTE(spapr->htab, index + n_valid), 0,
                       HASH_PTE_SIZE_64 * n_invalid);
            }
        } else {
            int rc;

            assert(fd >= 0);

            rc = kvmppc_load_htab_chunk(f, fd, index, n_valid, n_invalid,
                                        &local_err);
            if (rc < 0) {
                error_report_err(local_err);
                return rc;
            }
        }
    }

    if (!spapr->htab) {
        assert(fd >= 0);
        close(fd);
    }

    return 0;
}

static void htab_save_cleanup(void *opaque)
{
    SpaprMachineState *spapr = opaque;

    close_htab_fd(spapr);
}

static SaveVMHandlers savevm_htab_handlers = {
    .save_setup = htab_save_setup,
    .save_live_iterate = htab_save_iterate,
    .save_live_complete_precopy = htab_save_complete,
    .save_cleanup = htab_save_cleanup,
    .load_state = htab_load,
};

static void spapr_boot_set(void *opaque, const char *boot_device,
                           Error **errp)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(opaque);

    g_free(spapr->boot_device);
    spapr->boot_device = g_strdup(boot_device);
}

static void spapr_create_lmb_dr_connectors(SpaprMachineState *spapr)
{
    MachineState *machine = MACHINE(spapr);
    uint64_t lmb_size = SPAPR_MEMORY_BLOCK_SIZE;
    uint32_t nr_lmbs = (machine->maxram_size - machine->ram_size)/lmb_size;
    int i;

    g_assert(!nr_lmbs || machine->device_memory);
    for (i = 0; i < nr_lmbs; i++) {
        uint64_t addr;

        addr = i * lmb_size + machine->device_memory->base;
        spapr_dr_connector_new(OBJECT(spapr), TYPE_SPAPR_DRC_LMB,
                               addr / lmb_size);
    }
}

/*
 * If RAM size, maxmem size and individual node mem sizes aren't aligned
 * to SPAPR_MEMORY_BLOCK_SIZE(256MB), then refuse to start the guest
 * since we can't support such unaligned sizes with DRCONF_MEMORY.
 */
static void spapr_validate_node_memory(MachineState *machine, Error **errp)
{
    int i;

    if (machine->ram_size % SPAPR_MEMORY_BLOCK_SIZE) {
        error_setg(errp, "Memory size 0x" RAM_ADDR_FMT
                   " is not aligned to %" PRIu64 " MiB",
                   machine->ram_size,
                   SPAPR_MEMORY_BLOCK_SIZE / MiB);
        return;
    }

    if (machine->maxram_size % SPAPR_MEMORY_BLOCK_SIZE) {
        error_setg(errp, "Maximum memory size 0x" RAM_ADDR_FMT
                   " is not aligned to %" PRIu64 " MiB",
                   machine->ram_size,
                   SPAPR_MEMORY_BLOCK_SIZE / MiB);
        return;
    }

    for (i = 0; i < machine->numa_state->num_nodes; i++) {
        if (machine->numa_state->nodes[i].node_mem % SPAPR_MEMORY_BLOCK_SIZE) {
            error_setg(errp,
                       "Node %d memory size 0x%" PRIx64
                       " is not aligned to %" PRIu64 " MiB",
                       i, machine->numa_state->nodes[i].node_mem,
                       SPAPR_MEMORY_BLOCK_SIZE / MiB);
            return;
        }
    }
}

/* find cpu slot in machine->possible_cpus by core_id */
static CPUArchId *spapr_find_cpu_slot(MachineState *ms, uint32_t id, int *idx)
{
    int index = id / ms->smp.threads;

    if (index >= ms->possible_cpus->len) {
        return NULL;
    }
    if (idx) {
        *idx = index;
    }
    return &ms->possible_cpus->cpus[index];
}

static void spapr_set_vsmt_mode(SpaprMachineState *spapr, Error **errp)
{
    MachineState *ms = MACHINE(spapr);
    SpaprMachineClass *smc = SPAPR_MACHINE_GET_CLASS(spapr);
    Error *local_err = NULL;
    bool vsmt_user = !!spapr->vsmt;
    int kvm_smt = kvmppc_smt_threads();
    int ret;
    unsigned int smp_threads = ms->smp.threads;

    if (tcg_enabled()) {
        if (smp_threads > 1 &&
            !ppc_type_check_compat(ms->cpu_type, CPU_POWERPC_LOGICAL_2_07, 0,
                                   spapr->max_compat_pvr)) {
            error_setg(errp, "TCG only supports SMT on POWER8 or newer CPUs");
            return;
        }

        if (smp_threads > 8) {
            error_setg(errp, "TCG cannot support more than 8 threads/core "
                       "on a pseries machine");
            return;
        }
    }
    if (!is_power_of_2(smp_threads)) {
        error_setg(errp, "Cannot support %d threads/core on a pseries "
                   "machine because it must be a power of 2", smp_threads);
        return;
    }

    /* Determine the VSMT mode to use: */
    if (vsmt_user) {
        if (spapr->vsmt < smp_threads) {
            error_setg(errp, "Cannot support VSMT mode %d"
                       " because it must be >= threads/core (%d)",
                       spapr->vsmt, smp_threads);
            return;
        }
        /* In this case, spapr->vsmt has been set by the command line */
    } else if (!smc->smp_threads_vsmt) {
        /*
         * Default VSMT value is tricky, because we need it to be as
         * consistent as possible (for migration), but this requires
         * changing it for at least some existing cases.  We pick 8 as
         * the value that we'd get with KVM on POWER8, the
         * overwhelmingly common case in production systems.
         */
        spapr->vsmt = MAX(8, smp_threads);
    } else {
        spapr->vsmt = smp_threads;
    }

    /* KVM: If necessary, set the SMT mode: */
    if (kvm_enabled() && (spapr->vsmt != kvm_smt)) {
        ret = kvmppc_set_smt_threads(spapr->vsmt);
        if (ret) {
            /* Looks like KVM isn't able to change VSMT mode */
            error_setg(&local_err,
                       "Failed to set KVM's VSMT mode to %d (errno %d)",
                       spapr->vsmt, ret);
            /* We can live with that if the default one is big enough
             * for the number of threads, and a submultiple of the one
             * we want.  In this case we'll waste some vcpu ids, but
             * behaviour will be correct */
            if ((kvm_smt >= smp_threads) && ((spapr->vsmt % kvm_smt) == 0)) {
                warn_report_err(local_err);
            } else {
                if (!vsmt_user) {
                    error_append_hint(&local_err,
                                      "On PPC, a VM with %d threads/core"
                                      " on a host with %d threads/core"
                                      " requires the use of VSMT mode %d.\n",
                                      smp_threads, kvm_smt, spapr->vsmt);
                }
                kvmppc_error_append_smt_possible_hint(&local_err);
                error_propagate(errp, local_err);
            }
        }
    }
    /* else TCG: nothing to do currently */
}

static void spapr_init_cpus(SpaprMachineState *spapr)
{
    MachineState *machine = MACHINE(spapr);
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    SpaprMachineClass *smc = SPAPR_MACHINE_GET_CLASS(machine);
    const char *type = spapr_get_cpu_core_type(machine->cpu_type);
    const CPUArchIdList *possible_cpus;
    unsigned int smp_cpus = machine->smp.cpus;
    unsigned int smp_threads = machine->smp.threads;
    unsigned int max_cpus = machine->smp.max_cpus;
    int boot_cores_nr = smp_cpus / smp_threads;
    int i;

    possible_cpus = mc->possible_cpu_arch_ids(machine);
    if (mc->has_hotpluggable_cpus) {
        if (smp_cpus % smp_threads) {
            error_report("smp_cpus (%u) must be multiple of threads (%u)",
                         smp_cpus, smp_threads);
            exit(1);
        }
        if (max_cpus % smp_threads) {
            error_report("max_cpus (%u) must be multiple of threads (%u)",
                         max_cpus, smp_threads);
            exit(1);
        }
    } else {
        if (max_cpus != smp_cpus) {
            error_report("This machine version does not support CPU hotplug");
            exit(1);
        }
        boot_cores_nr = possible_cpus->len;
    }

    if (smc->pre_2_10_has_unused_icps) {
        for (i = 0; i < spapr_max_server_number(spapr); i++) {
            /* Dummy entries get deregistered when real ICPState objects
             * are registered during CPU core hotplug.
             */
            pre_2_10_vmstate_register_dummy_icp(i);
        }
    }

    for (i = 0; i < possible_cpus->len; i++) {
        int core_id = i * smp_threads;

        if (mc->has_hotpluggable_cpus) {
            spapr_dr_connector_new(OBJECT(spapr), TYPE_SPAPR_DRC_CPU,
                                   spapr_vcpu_id(spapr, core_id));
        }

        if (i < boot_cores_nr) {
            Object *core  = object_new(type);
            int nr_threads = smp_threads;

            /* Handle the partially filled core for older machine types */
            if ((i + 1) * smp_threads >= smp_cpus) {
                nr_threads = smp_cpus - i * smp_threads;
            }

            object_property_set_int(core, "nr-threads", nr_threads,
                                    &error_fatal);
            object_property_set_int(core, CPU_CORE_PROP_CORE_ID, core_id,
                                    &error_fatal);
            qdev_realize(DEVICE(core), NULL, &error_fatal);

            object_unref(core);
        }
    }
}

static PCIHostState *spapr_create_default_phb(void)
{
    DeviceState *dev;

    dev = qdev_new(TYPE_SPAPR_PCI_HOST_BRIDGE);
    qdev_prop_set_uint32(dev, "index", 0);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    return PCI_HOST_BRIDGE(dev);
}

static hwaddr spapr_rma_size(SpaprMachineState *spapr, Error **errp)
{
    MachineState *machine = MACHINE(spapr);
    SpaprMachineClass *smc = SPAPR_MACHINE_GET_CLASS(spapr);
    hwaddr rma_size = machine->ram_size;
    hwaddr node0_size = spapr_node0_size(machine);

    /* RMA has to fit in the first NUMA node */
    rma_size = MIN(rma_size, node0_size);

    /*
     * VRMA access is via a special 1TiB SLB mapping, so the RMA can
     * never exceed that
     */
    rma_size = MIN(rma_size, 1 * TiB);

    /*
     * Clamp the RMA size based on machine type.  This is for
     * migration compatibility with older qemu versions, which limited
     * the RMA size for complicated and mostly bad reasons.
     */
    if (smc->rma_limit) {
        rma_size = MIN(rma_size, smc->rma_limit);
    }

    if (rma_size < MIN_RMA_SLOF) {
        error_setg(errp,
                   "pSeries SLOF firmware requires >= %" HWADDR_PRIx
                   "ldMiB guest RMA (Real Mode Area memory)",
                   MIN_RMA_SLOF / MiB);
        return 0;
    }

    return rma_size;
}

static void spapr_create_nvdimm_dr_connectors(SpaprMachineState *spapr)
{
    MachineState *machine = MACHINE(spapr);
    int i;

    for (i = 0; i < machine->ram_slots; i++) {
        spapr_dr_connector_new(OBJECT(spapr), TYPE_SPAPR_DRC_PMEM, i);
    }
}

/* pSeries LPAR / sPAPR hardware init */
static void spapr_machine_init(MachineState *machine)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(machine);
    SpaprMachineClass *smc = SPAPR_MACHINE_GET_CLASS(machine);
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    const char *bios_default = spapr->vof ? FW_FILE_NAME_VOF : FW_FILE_NAME;
    const char *bios_name = machine->firmware ?: bios_default;
    g_autofree char *filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
    const char *kernel_filename = machine->kernel_filename;
    const char *initrd_filename = machine->initrd_filename;
    PCIHostState *phb;
    bool has_vga;
    int i;
    MemoryRegion *sysmem = get_system_memory();
    long load_limit, fw_size;
    Error *resize_hpt_err = NULL;

    if (!filename) {
        error_report("Could not find LPAR firmware '%s'", bios_name);
        exit(1);
    }
    fw_size = load_image_targphys(filename, 0, FW_MAX_SIZE);
    if (fw_size <= 0) {
        error_report("Could not load LPAR firmware '%s'", filename);
        exit(1);
    }

    /*
     * if Secure VM (PEF) support is configured, then initialize it
     */
    pef_kvm_init(machine->cgs, &error_fatal);

    msi_nonbroken = true;

    QLIST_INIT(&spapr->phbs);
    QTAILQ_INIT(&spapr->pending_dimm_unplugs);

    /* Determine capabilities to run with */
    spapr_caps_init(spapr);

    kvmppc_check_papr_resize_hpt(&resize_hpt_err);
    if (spapr->resize_hpt == SPAPR_RESIZE_HPT_DEFAULT) {
        /*
         * If the user explicitly requested a mode we should either
         * supply it, or fail completely (which we do below).  But if
         * it's not set explicitly, we reset our mode to something
         * that works
         */
        if (resize_hpt_err) {
            spapr->resize_hpt = SPAPR_RESIZE_HPT_DISABLED;
            error_free(resize_hpt_err);
            resize_hpt_err = NULL;
        } else {
            spapr->resize_hpt = smc->resize_hpt_default;
        }
    }

    assert(spapr->resize_hpt != SPAPR_RESIZE_HPT_DEFAULT);

    if ((spapr->resize_hpt != SPAPR_RESIZE_HPT_DISABLED) && resize_hpt_err) {
        /*
         * User requested HPT resize, but this host can't supply it.  Bail out
         */
        error_report_err(resize_hpt_err);
        exit(1);
    }
    error_free(resize_hpt_err);

    spapr->rma_size = spapr_rma_size(spapr, &error_fatal);

    /* Setup a load limit for the ramdisk leaving room for SLOF and FDT */
    load_limit = MIN(spapr->rma_size, FDT_MAX_ADDR) - FW_OVERHEAD;

    /*
     * VSMT must be set in order to be able to compute VCPU ids, ie to
     * call spapr_max_server_number() or spapr_vcpu_id().
     */
    spapr_set_vsmt_mode(spapr, &error_fatal);

    /* Set up Interrupt Controller before we create the VCPUs */
    spapr_irq_init(spapr, &error_fatal);

    /* Set up containers for ibm,client-architecture-support negotiated options
     */
    spapr->ov5 = spapr_ovec_new();
    spapr->ov5_cas = spapr_ovec_new();

    if (smc->dr_lmb_enabled) {
        spapr_ovec_set(spapr->ov5, OV5_DRCONF_MEMORY);
        spapr_validate_node_memory(machine, &error_fatal);
    }

    spapr_ovec_set(spapr->ov5, OV5_FORM1_AFFINITY);

    /* Do not advertise FORM2 NUMA support for pseries-6.1 and older */
    if (!smc->pre_6_2_numa_affinity) {
        spapr_ovec_set(spapr->ov5, OV5_FORM2_AFFINITY);
    }

    /* advertise support for dedicated HP event source to guests */
    if (spapr->use_hotplug_event_source) {
        spapr_ovec_set(spapr->ov5, OV5_HP_EVT);
    }

    /* advertise support for HPT resizing */
    if (spapr->resize_hpt != SPAPR_RESIZE_HPT_DISABLED) {
        spapr_ovec_set(spapr->ov5, OV5_HPT_RESIZE);
    }

    /* advertise support for ibm,dyamic-memory-v2 */
    spapr_ovec_set(spapr->ov5, OV5_DRMEM_V2);

    /* advertise XIVE on POWER9 machines */
    if (spapr->irq->xive) {
        spapr_ovec_set(spapr->ov5, OV5_XIVE_EXPLOIT);
    }

    /* init CPUs */
    spapr_init_cpus(spapr);

    /* Init numa_assoc_array */
    spapr_numa_associativity_init(spapr, machine);

    if ((!kvm_enabled() || kvmppc_has_cap_mmu_radix()) &&
        ppc_type_check_compat(machine->cpu_type, CPU_POWERPC_LOGICAL_3_00, 0,
                              spapr->max_compat_pvr)) {
        spapr_ovec_set(spapr->ov5, OV5_MMU_RADIX_300);
        /* KVM and TCG always allow GTSE with radix... */
        spapr_ovec_set(spapr->ov5, OV5_MMU_RADIX_GTSE);
    }
    /* ... but not with hash (currently). */

    if (kvm_enabled()) {
        /* Enable H_LOGICAL_CI_* so SLOF can talk to in-kernel devices */
        kvmppc_enable_logical_ci_hcalls();
        kvmppc_enable_set_mode_hcall();

        /* H_CLEAR_MOD/_REF are mandatory in PAPR, but off by default */
        kvmppc_enable_clear_ref_mod_hcalls();

        /* Enable H_PAGE_INIT */
        kvmppc_enable_h_page_init();
    }

    /* map RAM */
    memory_region_add_subregion(sysmem, 0, machine->ram);

    /* initialize hotplug memory address space */
    if (machine->ram_size < machine->maxram_size) {
        ram_addr_t device_mem_size = machine->maxram_size - machine->ram_size;
        hwaddr device_mem_base;

        /*
         * Limit the number of hotpluggable memory slots to half the number
         * slots that KVM supports, leaving the other half for PCI and other
         * devices. However ensure that number of slots doesn't drop below 32.
         */
        int max_memslots = kvm_enabled() ? kvm_get_max_memslots() / 2 :
                           SPAPR_MAX_RAM_SLOTS;

        if (max_memslots < SPAPR_MAX_RAM_SLOTS) {
            max_memslots = SPAPR_MAX_RAM_SLOTS;
        }
        if (machine->ram_slots > max_memslots) {
            error_report("Specified number of memory slots %"
                         PRIu64" exceeds max supported %d",
                         machine->ram_slots, max_memslots);
            exit(1);
        }

        device_mem_base = ROUND_UP(machine->ram_size, SPAPR_DEVICE_MEM_ALIGN);
        machine_memory_devices_init(machine, device_mem_base, device_mem_size);
    }

    if (smc->dr_lmb_enabled) {
        spapr_create_lmb_dr_connectors(spapr);
    }

    if (mc->nvdimm_supported) {
        spapr_create_nvdimm_dr_connectors(spapr);
    }

    /* Set up RTAS event infrastructure */
    spapr_events_init(spapr);

    /* Set up the RTC RTAS interfaces */
    spapr_rtc_create(spapr);

    /* Set up VIO bus */
    spapr->vio_bus = spapr_vio_bus_init();

    for (i = 0; serial_hd(i); i++) {
        spapr_vty_create(spapr->vio_bus, serial_hd(i));
    }

    /* We always have at least the nvram device on VIO */
    spapr_create_nvram(spapr);

    /*
     * Setup hotplug / dynamic-reconfiguration connectors. top-level
     * connectors (described in root DT node's "ibm,drc-types" property)
     * are pre-initialized here. additional child connectors (such as
     * connectors for a PHBs PCI slots) are added as needed during their
     * parent's realization.
     */
    if (smc->dr_phb_enabled) {
        for (i = 0; i < SPAPR_MAX_PHBS; i++) {
            spapr_dr_connector_new(OBJECT(machine), TYPE_SPAPR_DRC_PHB, i);
        }
    }

    /* Set up PCI */
    spapr_pci_rtas_init();

    phb = spapr_create_default_phb();

    for (i = 0; i < nb_nics; i++) {
        NICInfo *nd = &nd_table[i];

        if (!nd->model) {
            nd->model = g_strdup("spapr-vlan");
        }

        if (g_str_equal(nd->model, "spapr-vlan") ||
            g_str_equal(nd->model, "ibmveth")) {
            spapr_vlan_create(spapr->vio_bus, nd);
        } else {
            pci_nic_init_nofail(&nd_table[i], phb->bus, nd->model, NULL);
        }
    }

    for (i = 0; i <= drive_get_max_bus(IF_SCSI); i++) {
        spapr_vscsi_create(spapr->vio_bus);
    }

    /* Graphics */
    has_vga = spapr_vga_init(phb->bus, &error_fatal);
    if (has_vga) {
        spapr->want_stdout_path = !machine->enable_graphics;
        machine->usb |= defaults_enabled() && !machine->usb_disabled;
    } else {
        spapr->want_stdout_path = true;
    }

    if (machine->usb) {
        if (smc->use_ohci_by_default) {
            pci_create_simple(phb->bus, -1, "pci-ohci");
        } else {
            pci_create_simple(phb->bus, -1, "nec-usb-xhci");
        }

        if (has_vga) {
            USBBus *usb_bus = usb_bus_find(-1);

            usb_create_simple(usb_bus, "usb-kbd");
            usb_create_simple(usb_bus, "usb-mouse");
        }
    }

    if (kernel_filename) {
        uint64_t loaded_addr = 0;

        spapr->kernel_size = load_elf(kernel_filename, NULL,
                                      translate_kernel_address, spapr,
                                      NULL, &loaded_addr, NULL, NULL, 1,
                                      PPC_ELF_MACHINE, 0, 0);
        if (spapr->kernel_size == ELF_LOAD_WRONG_ENDIAN) {
            spapr->kernel_size = load_elf(kernel_filename, NULL,
                                          translate_kernel_address, spapr,
                                          NULL, &loaded_addr, NULL, NULL, 0,
                                          PPC_ELF_MACHINE, 0, 0);
            spapr->kernel_le = spapr->kernel_size > 0;
        }
        if (spapr->kernel_size < 0) {
            error_report("error loading %s: %s", kernel_filename,
                         load_elf_strerror(spapr->kernel_size));
            exit(1);
        }

        if (spapr->kernel_addr != loaded_addr) {
            warn_report("spapr: kernel_addr changed from 0x%"PRIx64
                        " to 0x%"PRIx64,
                        spapr->kernel_addr, loaded_addr);
            spapr->kernel_addr = loaded_addr;
        }

        /* load initrd */
        if (initrd_filename) {
            /* Try to locate the initrd in the gap between the kernel
             * and the firmware. Add a bit of space just in case
             */
            spapr->initrd_base = (spapr->kernel_addr + spapr->kernel_size
                                  + 0x1ffff) & ~0xffff;
            spapr->initrd_size = load_image_targphys(initrd_filename,
                                                     spapr->initrd_base,
                                                     load_limit
                                                     - spapr->initrd_base);
            if (spapr->initrd_size < 0) {
                error_report("could not load initial ram disk '%s'",
                             initrd_filename);
                exit(1);
            }
        }
    }

    /* FIXME: Should register things through the MachineState's qdev
     * interface, this is a legacy from the sPAPREnvironment structure
     * which predated MachineState but had a similar function */
    vmstate_register(NULL, 0, &vmstate_spapr, spapr);
    register_savevm_live("spapr/htab", VMSTATE_INSTANCE_ID_ANY, 1,
                         &savevm_htab_handlers, spapr);

    qbus_set_hotplug_handler(sysbus_get_default(), OBJECT(machine));

    qemu_register_boot_set(spapr_boot_set, spapr);

    /*
     * Nothing needs to be done to resume a suspended guest because
     * suspending does not change the machine state, so no need for
     * a ->wakeup method.
     */
    qemu_register_wakeup_support();

    if (kvm_enabled()) {
        /* to stop and start vmclock */
        qemu_add_vm_change_state_handler(cpu_ppc_clock_vm_state_change,
                                         &spapr->tb);

        kvmppc_spapr_enable_inkernel_multitce();
    }

    qemu_cond_init(&spapr->fwnmi_machine_check_interlock_cond);
    if (spapr->vof) {
        spapr->vof->fw_size = fw_size; /* for claim() on itself */
        spapr_register_hypercall(KVMPPC_H_VOF_CLIENT, spapr_h_vof_client);
    }

    spapr_watchdog_init(spapr);
}

#define DEFAULT_KVM_TYPE "auto"
static int spapr_kvm_type(MachineState *machine, const char *vm_type)
{
    /*
     * The use of g_ascii_strcasecmp() for 'hv' and 'pr' is to
     * accommodate the 'HV' and 'PV' formats that exists in the
     * wild. The 'auto' mode is being introduced already as
     * lower-case, thus we don't need to bother checking for
     * "AUTO".
     */
    if (!vm_type || !strcmp(vm_type, DEFAULT_KVM_TYPE)) {
        return 0;
    }

    if (!g_ascii_strcasecmp(vm_type, "hv")) {
        return 1;
    }

    if (!g_ascii_strcasecmp(vm_type, "pr")) {
        return 2;
    }

    error_report("Unknown kvm-type specified '%s'", vm_type);
    return -1;
}

/*
 * Implementation of an interface to adjust firmware path
 * for the bootindex property handling.
 */
static char *spapr_get_fw_dev_path(FWPathProvider *p, BusState *bus,
                                   DeviceState *dev)
{
#define CAST(type, obj, name) \
    ((type *)object_dynamic_cast(OBJECT(obj), (name)))
    SCSIDevice *d = CAST(SCSIDevice,  dev, TYPE_SCSI_DEVICE);
    SpaprPhbState *phb = CAST(SpaprPhbState, dev, TYPE_SPAPR_PCI_HOST_BRIDGE);
    VHostSCSICommon *vsc = CAST(VHostSCSICommon, dev, TYPE_VHOST_SCSI_COMMON);
    PCIDevice *pcidev = CAST(PCIDevice, dev, TYPE_PCI_DEVICE);

    if (d && bus) {
        void *spapr = CAST(void, bus->parent, "spapr-vscsi");
        VirtIOSCSI *virtio = CAST(VirtIOSCSI, bus->parent, TYPE_VIRTIO_SCSI);
        USBDevice *usb = CAST(USBDevice, bus->parent, TYPE_USB_DEVICE);

        if (spapr) {
            /*
             * Replace "channel@0/disk@0,0" with "disk@8000000000000000":
             * In the top 16 bits of the 64-bit LUN, we use SRP luns of the form
             * 0x8000 | (target << 8) | (bus << 5) | lun
             * (see the "Logical unit addressing format" table in SAM5)
             */
            unsigned id = 0x8000 | (d->id << 8) | (d->channel << 5) | d->lun;
            return g_strdup_printf("%s@%"PRIX64, qdev_fw_name(dev),
                                   (uint64_t)id << 48);
        } else if (virtio) {
            /*
             * We use SRP luns of the form 01000000 | (target << 8) | lun
             * in the top 32 bits of the 64-bit LUN
             * Note: the quote above is from SLOF and it is wrong,
             * the actual binding is:
             * swap 0100 or 10 << or 20 << ( target lun-id -- srplun )
             */
            unsigned id = 0x1000000 | (d->id << 16) | d->lun;
            if (d->lun >= 256) {
                /* Use the LUN "flat space addressing method" */
                id |= 0x4000;
            }
            return g_strdup_printf("%s@%"PRIX64, qdev_fw_name(dev),
                                   (uint64_t)id << 32);
        } else if (usb) {
            /*
             * We use SRP luns of the form 01000000 | (usb-port << 16) | lun
             * in the top 32 bits of the 64-bit LUN
             */
            unsigned usb_port = atoi(usb->port->path);
            unsigned id = 0x1000000 | (usb_port << 16) | d->lun;
            return g_strdup_printf("%s@%"PRIX64, qdev_fw_name(dev),
                                   (uint64_t)id << 32);
        }
    }

    /*
     * SLOF probes the USB devices, and if it recognizes that the device is a
     * storage device, it changes its name to "storage" instead of "usb-host",
     * and additionally adds a child node for the SCSI LUN, so the correct
     * boot path in SLOF is something like .../storage@1/disk@xxx" instead.
     */
    if (strcmp("usb-host", qdev_fw_name(dev)) == 0) {
        USBDevice *usbdev = CAST(USBDevice, dev, TYPE_USB_DEVICE);
        if (usb_device_is_scsi_storage(usbdev)) {
            return g_strdup_printf("storage@%s/disk", usbdev->port->path);
        }
    }

    if (phb) {
        /* Replace "pci" with "pci@800000020000000" */
        return g_strdup_printf("pci@%"PRIX64, phb->buid);
    }

    if (vsc) {
        /* Same logic as virtio above */
        unsigned id = 0x1000000 | (vsc->target << 16) | vsc->lun;
        return g_strdup_printf("disk@%"PRIX64, (uint64_t)id << 32);
    }

    if (g_str_equal("pci-bridge", qdev_fw_name(dev))) {
        /* SLOF uses "pci" instead of "pci-bridge" for PCI bridges */
        PCIDevice *pdev = CAST(PCIDevice, dev, TYPE_PCI_DEVICE);
        return g_strdup_printf("pci@%x", PCI_SLOT(pdev->devfn));
    }

    if (pcidev) {
        return spapr_pci_fw_dev_name(pcidev);
    }

    return NULL;
}

static char *spapr_get_kvm_type(Object *obj, Error **errp)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(obj);

    return g_strdup(spapr->kvm_type);
}

static void spapr_set_kvm_type(Object *obj, const char *value, Error **errp)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(obj);

    g_free(spapr->kvm_type);
    spapr->kvm_type = g_strdup(value);
}

static bool spapr_get_modern_hotplug_events(Object *obj, Error **errp)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(obj);

    return spapr->use_hotplug_event_source;
}

static void spapr_set_modern_hotplug_events(Object *obj, bool value,
                                            Error **errp)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(obj);

    spapr->use_hotplug_event_source = value;
}

static bool spapr_get_msix_emulation(Object *obj, Error **errp)
{
    return true;
}

static char *spapr_get_resize_hpt(Object *obj, Error **errp)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(obj);

    switch (spapr->resize_hpt) {
    case SPAPR_RESIZE_HPT_DEFAULT:
        return g_strdup("default");
    case SPAPR_RESIZE_HPT_DISABLED:
        return g_strdup("disabled");
    case SPAPR_RESIZE_HPT_ENABLED:
        return g_strdup("enabled");
    case SPAPR_RESIZE_HPT_REQUIRED:
        return g_strdup("required");
    }
    g_assert_not_reached();
}

static void spapr_set_resize_hpt(Object *obj, const char *value, Error **errp)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(obj);

    if (strcmp(value, "default") == 0) {
        spapr->resize_hpt = SPAPR_RESIZE_HPT_DEFAULT;
    } else if (strcmp(value, "disabled") == 0) {
        spapr->resize_hpt = SPAPR_RESIZE_HPT_DISABLED;
    } else if (strcmp(value, "enabled") == 0) {
        spapr->resize_hpt = SPAPR_RESIZE_HPT_ENABLED;
    } else if (strcmp(value, "required") == 0) {
        spapr->resize_hpt = SPAPR_RESIZE_HPT_REQUIRED;
    } else {
        error_setg(errp, "Bad value for \"resize-hpt\" property");
    }
}

static bool spapr_get_vof(Object *obj, Error **errp)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(obj);

    return spapr->vof != NULL;
}

static void spapr_set_vof(Object *obj, bool value, Error **errp)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(obj);

    if (spapr->vof) {
        vof_cleanup(spapr->vof);
        g_free(spapr->vof);
        spapr->vof = NULL;
    }
    if (!value) {
        return;
    }
    spapr->vof = g_malloc0(sizeof(*spapr->vof));
}

static char *spapr_get_ic_mode(Object *obj, Error **errp)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(obj);

    if (spapr->irq == &spapr_irq_xics_legacy) {
        return g_strdup("legacy");
    } else if (spapr->irq == &spapr_irq_xics) {
        return g_strdup("xics");
    } else if (spapr->irq == &spapr_irq_xive) {
        return g_strdup("xive");
    } else if (spapr->irq == &spapr_irq_dual) {
        return g_strdup("dual");
    }
    g_assert_not_reached();
}

static void spapr_set_ic_mode(Object *obj, const char *value, Error **errp)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(obj);

    if (SPAPR_MACHINE_GET_CLASS(spapr)->legacy_irq_allocation) {
        error_setg(errp, "This machine only uses the legacy XICS backend, don't pass ic-mode");
        return;
    }

    /* The legacy IRQ backend can not be set */
    if (strcmp(value, "xics") == 0) {
        spapr->irq = &spapr_irq_xics;
    } else if (strcmp(value, "xive") == 0) {
        spapr->irq = &spapr_irq_xive;
    } else if (strcmp(value, "dual") == 0) {
        spapr->irq = &spapr_irq_dual;
    } else {
        error_setg(errp, "Bad value for \"ic-mode\" property");
    }
}

static char *spapr_get_host_model(Object *obj, Error **errp)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(obj);

    return g_strdup(spapr->host_model);
}

static void spapr_set_host_model(Object *obj, const char *value, Error **errp)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(obj);

    g_free(spapr->host_model);
    spapr->host_model = g_strdup(value);
}

static char *spapr_get_host_serial(Object *obj, Error **errp)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(obj);

    return g_strdup(spapr->host_serial);
}

static void spapr_set_host_serial(Object *obj, const char *value, Error **errp)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(obj);

    g_free(spapr->host_serial);
    spapr->host_serial = g_strdup(value);
}

static void spapr_instance_init(Object *obj)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(obj);
    SpaprMachineClass *smc = SPAPR_MACHINE_GET_CLASS(spapr);
    MachineState *ms = MACHINE(spapr);
    MachineClass *mc = MACHINE_GET_CLASS(ms);

    /*
     * NVDIMM support went live in 5.1 without considering that, in
     * other archs, the user needs to enable NVDIMM support with the
     * 'nvdimm' machine option and the default behavior is NVDIMM
     * support disabled. It is too late to roll back to the standard
     * behavior without breaking 5.1 guests.
     */
    if (mc->nvdimm_supported) {
        ms->nvdimms_state->is_enabled = true;
    }

    spapr->htab_fd = -1;
    spapr->use_hotplug_event_source = true;
    spapr->kvm_type = g_strdup(DEFAULT_KVM_TYPE);
    object_property_add_str(obj, "kvm-type",
                            spapr_get_kvm_type, spapr_set_kvm_type);
    object_property_set_description(obj, "kvm-type",
                                    "Specifies the KVM virtualization mode (auto,"
                                    " hv, pr). Defaults to 'auto'. This mode will use"
                                    " any available KVM module loaded in the host,"
                                    " where kvm_hv takes precedence if both kvm_hv and"
                                    " kvm_pr are loaded.");
    object_property_add_bool(obj, "modern-hotplug-events",
                            spapr_get_modern_hotplug_events,
                            spapr_set_modern_hotplug_events);
    object_property_set_description(obj, "modern-hotplug-events",
                                    "Use dedicated hotplug event mechanism in"
                                    " place of standard EPOW events when possible"
                                    " (required for memory hot-unplug support)");
    ppc_compat_add_property(obj, "max-cpu-compat", &spapr->max_compat_pvr,
                            "Maximum permitted CPU compatibility mode");

    object_property_add_str(obj, "resize-hpt",
                            spapr_get_resize_hpt, spapr_set_resize_hpt);
    object_property_set_description(obj, "resize-hpt",
                                    "Resizing of the Hash Page Table (enabled, disabled, required)");
    object_property_add_uint32_ptr(obj, "vsmt",
                                   &spapr->vsmt, OBJ_PROP_FLAG_READWRITE);
    object_property_set_description(obj, "vsmt",
                                    "Virtual SMT: KVM behaves as if this were"
                                    " the host's SMT mode");

    object_property_add_bool(obj, "vfio-no-msix-emulation",
                             spapr_get_msix_emulation, NULL);

    object_property_add_uint64_ptr(obj, "kernel-addr",
                                   &spapr->kernel_addr, OBJ_PROP_FLAG_READWRITE);
    object_property_set_description(obj, "kernel-addr",
                                    stringify(KERNEL_LOAD_ADDR)
                                    " for -kernel is the default");
    spapr->kernel_addr = KERNEL_LOAD_ADDR;

    object_property_add_bool(obj, "x-vof", spapr_get_vof, spapr_set_vof);
    object_property_set_description(obj, "x-vof",
                                    "Enable Virtual Open Firmware (experimental)");

    /* The machine class defines the default interrupt controller mode */
    spapr->irq = smc->irq;
    object_property_add_str(obj, "ic-mode", spapr_get_ic_mode,
                            spapr_set_ic_mode);
    object_property_set_description(obj, "ic-mode",
                 "Specifies the interrupt controller mode (xics, xive, dual)");

    object_property_add_str(obj, "host-model",
        spapr_get_host_model, spapr_set_host_model);
    object_property_set_description(obj, "host-model",
        "Host model to advertise in guest device tree");
    object_property_add_str(obj, "host-serial",
        spapr_get_host_serial, spapr_set_host_serial);
    object_property_set_description(obj, "host-serial",
        "Host serial number to advertise in guest device tree");
}

static void spapr_machine_finalizefn(Object *obj)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(obj);

    g_free(spapr->kvm_type);
}

void spapr_do_system_reset_on_cpu(CPUState *cs, run_on_cpu_data arg)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(qdev_get_machine());
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;

    cpu_synchronize_state(cs);
    /* If FWNMI is inactive, addr will be -1, which will deliver to 0x100 */
    if (spapr->fwnmi_system_reset_addr != -1) {
        uint64_t rtas_addr, addr;

        /* get rtas addr from fdt */
        rtas_addr = spapr_get_rtas_addr();
        if (!rtas_addr) {
            qemu_system_guest_panicked(NULL);
            return;
        }

        addr = rtas_addr + RTAS_ERROR_LOG_MAX + cs->cpu_index * sizeof(uint64_t)*2;
        stq_be_phys(&address_space_memory, addr, env->gpr[3]);
        stq_be_phys(&address_space_memory, addr + sizeof(uint64_t), 0);
        env->gpr[3] = addr;
    }
    ppc_cpu_do_system_reset(cs);
    if (spapr->fwnmi_system_reset_addr != -1) {
        env->nip = spapr->fwnmi_system_reset_addr;
    }
}

static void spapr_nmi(NMIState *n, int cpu_index, Error **errp)
{
    CPUState *cs;

    CPU_FOREACH(cs) {
        async_run_on_cpu(cs, spapr_do_system_reset_on_cpu, RUN_ON_CPU_NULL);
    }
}

int spapr_lmb_dt_populate(SpaprDrc *drc, SpaprMachineState *spapr,
                          void *fdt, int *fdt_start_offset, Error **errp)
{
    uint64_t addr;
    uint32_t node;

    addr = spapr_drc_index(drc) * SPAPR_MEMORY_BLOCK_SIZE;
    node = object_property_get_uint(OBJECT(drc->dev), PC_DIMM_NODE_PROP,
                                    &error_abort);
    *fdt_start_offset = spapr_dt_memory_node(spapr, fdt, node, addr,
                                             SPAPR_MEMORY_BLOCK_SIZE);
    return 0;
}

static void spapr_add_lmbs(DeviceState *dev, uint64_t addr_start, uint64_t size,
                           bool dedicated_hp_event_source)
{
    SpaprDrc *drc;
    uint32_t nr_lmbs = size/SPAPR_MEMORY_BLOCK_SIZE;
    int i;
    uint64_t addr = addr_start;
    bool hotplugged = spapr_drc_hotplugged(dev);

    for (i = 0; i < nr_lmbs; i++) {
        drc = spapr_drc_by_id(TYPE_SPAPR_DRC_LMB,
                              addr / SPAPR_MEMORY_BLOCK_SIZE);
        g_assert(drc);

        /*
         * memory_device_get_free_addr() provided a range of free addresses
         * that doesn't overlap with any existing mapping at pre-plug. The
         * corresponding LMB DRCs are thus assumed to be all attachable.
         */
        spapr_drc_attach(drc, dev);
        if (!hotplugged) {
            spapr_drc_reset(drc);
        }
        addr += SPAPR_MEMORY_BLOCK_SIZE;
    }
    /* send hotplug notification to the
     * guest only in case of hotplugged memory
     */
    if (hotplugged) {
        if (dedicated_hp_event_source) {
            drc = spapr_drc_by_id(TYPE_SPAPR_DRC_LMB,
                                  addr_start / SPAPR_MEMORY_BLOCK_SIZE);
            g_assert(drc);
            spapr_hotplug_req_add_by_count_indexed(SPAPR_DR_CONNECTOR_TYPE_LMB,
                                                   nr_lmbs,
                                                   spapr_drc_index(drc));
        } else {
            spapr_hotplug_req_add_by_count(SPAPR_DR_CONNECTOR_TYPE_LMB,
                                           nr_lmbs);
        }
    }
}

static void spapr_memory_plug(HotplugHandler *hotplug_dev, DeviceState *dev)
{
    SpaprMachineState *ms = SPAPR_MACHINE(hotplug_dev);
    PCDIMMDevice *dimm = PC_DIMM(dev);
    uint64_t size, addr;
    int64_t slot;
    bool is_nvdimm = object_dynamic_cast(OBJECT(dev), TYPE_NVDIMM);

    size = memory_device_get_region_size(MEMORY_DEVICE(dev), &error_abort);

    pc_dimm_plug(dimm, MACHINE(ms));

    if (!is_nvdimm) {
        addr = object_property_get_uint(OBJECT(dimm),
                                        PC_DIMM_ADDR_PROP, &error_abort);
        spapr_add_lmbs(dev, addr, size,
                       spapr_ovec_test(ms->ov5_cas, OV5_HP_EVT));
    } else {
        slot = object_property_get_int(OBJECT(dimm),
                                       PC_DIMM_SLOT_PROP, &error_abort);
        /* We should have valid slot number at this point */
        g_assert(slot >= 0);
        spapr_add_nvdimm(dev, slot);
    }
}

static void spapr_memory_pre_plug(HotplugHandler *hotplug_dev, DeviceState *dev,
                                  Error **errp)
{
    const SpaprMachineClass *smc = SPAPR_MACHINE_GET_CLASS(hotplug_dev);
    SpaprMachineState *spapr = SPAPR_MACHINE(hotplug_dev);
    bool is_nvdimm = object_dynamic_cast(OBJECT(dev), TYPE_NVDIMM);
    PCDIMMDevice *dimm = PC_DIMM(dev);
    Error *local_err = NULL;
    uint64_t size;
    Object *memdev;
    hwaddr pagesize;

    if (!smc->dr_lmb_enabled) {
        error_setg(errp, "Memory hotplug not supported for this machine");
        return;
    }

    size = memory_device_get_region_size(MEMORY_DEVICE(dimm), &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    if (is_nvdimm) {
        if (!spapr_nvdimm_validate(hotplug_dev, NVDIMM(dev), size, errp)) {
            return;
        }
    } else if (size % SPAPR_MEMORY_BLOCK_SIZE) {
        error_setg(errp, "Hotplugged memory size must be a multiple of "
                   "%" PRIu64 " MB", SPAPR_MEMORY_BLOCK_SIZE / MiB);
        return;
    }

    memdev = object_property_get_link(OBJECT(dimm), PC_DIMM_MEMDEV_PROP,
                                      &error_abort);
    pagesize = host_memory_backend_pagesize(MEMORY_BACKEND(memdev));
    if (!spapr_check_pagesize(spapr, pagesize, errp)) {
        return;
    }

    pc_dimm_pre_plug(dimm, MACHINE(hotplug_dev), NULL, errp);
}

struct SpaprDimmState {
    PCDIMMDevice *dimm;
    uint32_t nr_lmbs;
    QTAILQ_ENTRY(SpaprDimmState) next;
};

static SpaprDimmState *spapr_pending_dimm_unplugs_find(SpaprMachineState *s,
                                                       PCDIMMDevice *dimm)
{
    SpaprDimmState *dimm_state = NULL;

    QTAILQ_FOREACH(dimm_state, &s->pending_dimm_unplugs, next) {
        if (dimm_state->dimm == dimm) {
            break;
        }
    }
    return dimm_state;
}

static SpaprDimmState *spapr_pending_dimm_unplugs_add(SpaprMachineState *spapr,
                                                      uint32_t nr_lmbs,
                                                      PCDIMMDevice *dimm)
{
    SpaprDimmState *ds = NULL;

    /*
     * If this request is for a DIMM whose removal had failed earlier
     * (due to guest's refusal to remove the LMBs), we would have this
     * dimm already in the pending_dimm_unplugs list. In that
     * case don't add again.
     */
    ds = spapr_pending_dimm_unplugs_find(spapr, dimm);
    if (!ds) {
        ds = g_new0(SpaprDimmState, 1);
        ds->nr_lmbs = nr_lmbs;
        ds->dimm = dimm;
        QTAILQ_INSERT_HEAD(&spapr->pending_dimm_unplugs, ds, next);
    }
    return ds;
}

static void spapr_pending_dimm_unplugs_remove(SpaprMachineState *spapr,
                                              SpaprDimmState *dimm_state)
{
    QTAILQ_REMOVE(&spapr->pending_dimm_unplugs, dimm_state, next);
    g_free(dimm_state);
}

static SpaprDimmState *spapr_recover_pending_dimm_state(SpaprMachineState *ms,
                                                        PCDIMMDevice *dimm)
{
    SpaprDrc *drc;
    uint64_t size = memory_device_get_region_size(MEMORY_DEVICE(dimm),
                                                  &error_abort);
    uint32_t nr_lmbs = size / SPAPR_MEMORY_BLOCK_SIZE;
    uint32_t avail_lmbs = 0;
    uint64_t addr_start, addr;
    int i;

    addr_start = object_property_get_uint(OBJECT(dimm), PC_DIMM_ADDR_PROP,
                                          &error_abort);

    addr = addr_start;
    for (i = 0; i < nr_lmbs; i++) {
        drc = spapr_drc_by_id(TYPE_SPAPR_DRC_LMB,
                              addr / SPAPR_MEMORY_BLOCK_SIZE);
        g_assert(drc);
        if (drc->dev) {
            avail_lmbs++;
        }
        addr += SPAPR_MEMORY_BLOCK_SIZE;
    }

    return spapr_pending_dimm_unplugs_add(ms, avail_lmbs, dimm);
}

void spapr_memory_unplug_rollback(SpaprMachineState *spapr, DeviceState *dev)
{
    SpaprDimmState *ds;
    PCDIMMDevice *dimm;
    SpaprDrc *drc;
    uint32_t nr_lmbs;
    uint64_t size, addr_start, addr;
    g_autofree char *qapi_error = NULL;
    int i;

    if (!dev) {
        return;
    }

    dimm = PC_DIMM(dev);
    ds = spapr_pending_dimm_unplugs_find(spapr, dimm);

    /*
     * 'ds == NULL' would mean that the DIMM doesn't have a pending
     * unplug state, but one of its DRC is marked as unplug_requested.
     * This is bad and weird enough to g_assert() out.
     */
    g_assert(ds);

    spapr_pending_dimm_unplugs_remove(spapr, ds);

    size = memory_device_get_region_size(MEMORY_DEVICE(dimm), &error_abort);
    nr_lmbs = size / SPAPR_MEMORY_BLOCK_SIZE;

    addr_start = object_property_get_uint(OBJECT(dimm), PC_DIMM_ADDR_PROP,
                                          &error_abort);

    addr = addr_start;
    for (i = 0; i < nr_lmbs; i++) {
        drc = spapr_drc_by_id(TYPE_SPAPR_DRC_LMB,
                              addr / SPAPR_MEMORY_BLOCK_SIZE);
        g_assert(drc);

        drc->unplug_requested = false;
        addr += SPAPR_MEMORY_BLOCK_SIZE;
    }

    /*
     * Tell QAPI that something happened and the memory
     * hotunplug wasn't successful. Keep sending
     * MEM_UNPLUG_ERROR even while sending
     * DEVICE_UNPLUG_GUEST_ERROR until the deprecation of
     * MEM_UNPLUG_ERROR is due.
     */
    qapi_error = g_strdup_printf("Memory hotunplug rejected by the guest "
                                 "for device %s", dev->id);

    qapi_event_send_mem_unplug_error(dev->id ? : "", qapi_error);

    qapi_event_send_device_unplug_guest_error(dev->id,
                                              dev->canonical_path);
}

/* Callback to be called during DRC release. */
void spapr_lmb_release(DeviceState *dev)
{
    HotplugHandler *hotplug_ctrl = qdev_get_hotplug_handler(dev);
    SpaprMachineState *spapr = SPAPR_MACHINE(hotplug_ctrl);
    SpaprDimmState *ds = spapr_pending_dimm_unplugs_find(spapr, PC_DIMM(dev));

    /* This information will get lost if a migration occurs
     * during the unplug process. In this case recover it. */
    if (ds == NULL) {
        ds = spapr_recover_pending_dimm_state(spapr, PC_DIMM(dev));
        g_assert(ds);
        /* The DRC being examined by the caller at least must be counted */
        g_assert(ds->nr_lmbs);
    }

    if (--ds->nr_lmbs) {
        return;
    }

    /*
     * Now that all the LMBs have been removed by the guest, call the
     * unplug handler chain. This can never fail.
     */
    hotplug_handler_unplug(hotplug_ctrl, dev, &error_abort);
    object_unparent(OBJECT(dev));
}

static void spapr_memory_unplug(HotplugHandler *hotplug_dev, DeviceState *dev)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(hotplug_dev);
    SpaprDimmState *ds = spapr_pending_dimm_unplugs_find(spapr, PC_DIMM(dev));

    /* We really shouldn't get this far without anything to unplug */
    g_assert(ds);

    pc_dimm_unplug(PC_DIMM(dev), MACHINE(hotplug_dev));
    qdev_unrealize(dev);
    spapr_pending_dimm_unplugs_remove(spapr, ds);
}

static void spapr_memory_unplug_request(HotplugHandler *hotplug_dev,
                                        DeviceState *dev, Error **errp)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(hotplug_dev);
    PCDIMMDevice *dimm = PC_DIMM(dev);
    uint32_t nr_lmbs;
    uint64_t size, addr_start, addr;
    int i;
    SpaprDrc *drc;

    if (object_dynamic_cast(OBJECT(dev), TYPE_NVDIMM)) {
        error_setg(errp, "nvdimm device hot unplug is not supported yet.");
        return;
    }

    size = memory_device_get_region_size(MEMORY_DEVICE(dimm), &error_abort);
    nr_lmbs = size / SPAPR_MEMORY_BLOCK_SIZE;

    addr_start = object_property_get_uint(OBJECT(dimm), PC_DIMM_ADDR_PROP,
                                          &error_abort);

    /*
     * An existing pending dimm state for this DIMM means that there is an
     * unplug operation in progress, waiting for the spapr_lmb_release
     * callback to complete the job (BQL can't cover that far). In this case,
     * bail out to avoid detaching DRCs that were already released.
     */
    if (spapr_pending_dimm_unplugs_find(spapr, dimm)) {
        error_setg(errp, "Memory unplug already in progress for device %s",
                   dev->id);
        return;
    }

    spapr_pending_dimm_unplugs_add(spapr, nr_lmbs, dimm);

    addr = addr_start;
    for (i = 0; i < nr_lmbs; i++) {
        drc = spapr_drc_by_id(TYPE_SPAPR_DRC_LMB,
                              addr / SPAPR_MEMORY_BLOCK_SIZE);
        g_assert(drc);

        spapr_drc_unplug_request(drc);
        addr += SPAPR_MEMORY_BLOCK_SIZE;
    }

    drc = spapr_drc_by_id(TYPE_SPAPR_DRC_LMB,
                          addr_start / SPAPR_MEMORY_BLOCK_SIZE);
    spapr_hotplug_req_remove_by_count_indexed(SPAPR_DR_CONNECTOR_TYPE_LMB,
                                              nr_lmbs, spapr_drc_index(drc));
}

/* Callback to be called during DRC release. */
void spapr_core_release(DeviceState *dev)
{
    HotplugHandler *hotplug_ctrl = qdev_get_hotplug_handler(dev);

    /* Call the unplug handler chain. This can never fail. */
    hotplug_handler_unplug(hotplug_ctrl, dev, &error_abort);
    object_unparent(OBJECT(dev));
}

static void spapr_core_unplug(HotplugHandler *hotplug_dev, DeviceState *dev)
{
    MachineState *ms = MACHINE(hotplug_dev);
    SpaprMachineClass *smc = SPAPR_MACHINE_GET_CLASS(ms);
    CPUCore *cc = CPU_CORE(dev);
    CPUArchId *core_slot = spapr_find_cpu_slot(ms, cc->core_id, NULL);

    if (smc->pre_2_10_has_unused_icps) {
        SpaprCpuCore *sc = SPAPR_CPU_CORE(OBJECT(dev));
        int i;

        for (i = 0; i < cc->nr_threads; i++) {
            CPUState *cs = CPU(sc->threads[i]);

            pre_2_10_vmstate_register_dummy_icp(cs->cpu_index);
        }
    }

    assert(core_slot);
    core_slot->cpu = NULL;
    qdev_unrealize(dev);
}

static
void spapr_core_unplug_request(HotplugHandler *hotplug_dev, DeviceState *dev,
                               Error **errp)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(OBJECT(hotplug_dev));
    int index;
    SpaprDrc *drc;
    CPUCore *cc = CPU_CORE(dev);

    if (!spapr_find_cpu_slot(MACHINE(hotplug_dev), cc->core_id, &index)) {
        error_setg(errp, "Unable to find CPU core with core-id: %d",
                   cc->core_id);
        return;
    }
    if (index == 0) {
        error_setg(errp, "Boot CPU core may not be unplugged");
        return;
    }

    drc = spapr_drc_by_id(TYPE_SPAPR_DRC_CPU,
                          spapr_vcpu_id(spapr, cc->core_id));
    g_assert(drc);

    if (!spapr_drc_unplug_requested(drc)) {
        spapr_drc_unplug_request(drc);
    }

    /*
     * spapr_hotplug_req_remove_by_index is left unguarded, out of the
     * "!spapr_drc_unplug_requested" check, to allow for multiple IRQ
     * pulses removing the same CPU. Otherwise, in an failed hotunplug
     * attempt (e.g. the kernel will refuse to remove the last online
     * CPU), we will never attempt it again because unplug_requested
     * will still be 'true' in that case.
     */
    spapr_hotplug_req_remove_by_index(drc);
}

int spapr_core_dt_populate(SpaprDrc *drc, SpaprMachineState *spapr,
                           void *fdt, int *fdt_start_offset, Error **errp)
{
    SpaprCpuCore *core = SPAPR_CPU_CORE(drc->dev);
    CPUState *cs = CPU(core->threads[0]);
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    DeviceClass *dc = DEVICE_GET_CLASS(cs);
    int id = spapr_get_vcpu_id(cpu);
    g_autofree char *nodename = NULL;
    int offset;

    nodename = g_strdup_printf("%s@%x", dc->fw_name, id);
    offset = fdt_add_subnode(fdt, 0, nodename);

    spapr_dt_cpu(cs, fdt, offset, spapr);

    /*
     * spapr_dt_cpu() does not fill the 'name' property in the
     * CPU node. The function is called during boot process, before
     * and after CAS, and overwriting the 'name' property written
     * by SLOF is not allowed.
     *
     * Write it manually after spapr_dt_cpu(). This makes the hotplug
     * CPUs more compatible with the coldplugged ones, which have
     * the 'name' property. Linux Kernel also relies on this
     * property to identify CPU nodes.
     */
    _FDT((fdt_setprop_string(fdt, offset, "name", nodename)));

    *fdt_start_offset = offset;
    return 0;
}

static void spapr_core_plug(HotplugHandler *hotplug_dev, DeviceState *dev)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(OBJECT(hotplug_dev));
    MachineClass *mc = MACHINE_GET_CLASS(spapr);
    SpaprMachineClass *smc = SPAPR_MACHINE_CLASS(mc);
    SpaprCpuCore *core = SPAPR_CPU_CORE(OBJECT(dev));
    CPUCore *cc = CPU_CORE(dev);
    CPUState *cs;
    SpaprDrc *drc;
    CPUArchId *core_slot;
    int index;
    bool hotplugged = spapr_drc_hotplugged(dev);
    int i;

    core_slot = spapr_find_cpu_slot(MACHINE(hotplug_dev), cc->core_id, &index);
    g_assert(core_slot); /* Already checked in spapr_core_pre_plug() */

    drc = spapr_drc_by_id(TYPE_SPAPR_DRC_CPU,
                          spapr_vcpu_id(spapr, cc->core_id));

    g_assert(drc || !mc->has_hotpluggable_cpus);

    if (drc) {
        /*
         * spapr_core_pre_plug() already buys us this is a brand new
         * core being plugged into a free slot. Nothing should already
         * be attached to the corresponding DRC.
         */
        spapr_drc_attach(drc, dev);

        if (hotplugged) {
            /*
             * Send hotplug notification interrupt to the guest only
             * in case of hotplugged CPUs.
             */
            spapr_hotplug_req_add_by_index(drc);
        } else {
            spapr_drc_reset(drc);
        }
    }

    core_slot->cpu = OBJECT(dev);

    /*
     * Set compatibility mode to match the boot CPU, which was either set
     * by the machine reset code or by CAS. This really shouldn't fail at
     * this point.
     */
    if (hotplugged) {
        for (i = 0; i < cc->nr_threads; i++) {
            ppc_set_compat(core->threads[i], POWERPC_CPU(first_cpu)->compat_pvr,
                           &error_abort);
        }
    }

    if (smc->pre_2_10_has_unused_icps) {
        for (i = 0; i < cc->nr_threads; i++) {
            cs = CPU(core->threads[i]);
            pre_2_10_vmstate_unregister_dummy_icp(cs->cpu_index);
        }
    }
}

static void spapr_core_pre_plug(HotplugHandler *hotplug_dev, DeviceState *dev,
                                Error **errp)
{
    MachineState *machine = MACHINE(OBJECT(hotplug_dev));
    MachineClass *mc = MACHINE_GET_CLASS(hotplug_dev);
    CPUCore *cc = CPU_CORE(dev);
    const char *base_core_type = spapr_get_cpu_core_type(machine->cpu_type);
    const char *type = object_get_typename(OBJECT(dev));
    CPUArchId *core_slot;
    int index;
    unsigned int smp_threads = machine->smp.threads;

    if (dev->hotplugged && !mc->has_hotpluggable_cpus) {
        error_setg(errp, "CPU hotplug not supported for this machine");
        return;
    }

    if (strcmp(base_core_type, type)) {
        error_setg(errp, "CPU core type should be %s", base_core_type);
        return;
    }

    if (cc->core_id % smp_threads) {
        error_setg(errp, "invalid core id %d", cc->core_id);
        return;
    }

    /*
     * In general we should have homogeneous threads-per-core, but old
     * (pre hotplug support) machine types allow the last core to have
     * reduced threads as a compatibility hack for when we allowed
     * total vcpus not a multiple of threads-per-core.
     */
    if (mc->has_hotpluggable_cpus && (cc->nr_threads != smp_threads)) {
        error_setg(errp, "invalid nr-threads %d, must be %d", cc->nr_threads,
                   smp_threads);
        return;
    }

    core_slot = spapr_find_cpu_slot(MACHINE(hotplug_dev), cc->core_id, &index);
    if (!core_slot) {
        error_setg(errp, "core id %d out of range", cc->core_id);
        return;
    }

    if (core_slot->cpu) {
        error_setg(errp, "core %d already populated", cc->core_id);
        return;
    }

    numa_cpu_pre_plug(core_slot, dev, errp);
}

int spapr_phb_dt_populate(SpaprDrc *drc, SpaprMachineState *spapr,
                          void *fdt, int *fdt_start_offset, Error **errp)
{
    SpaprPhbState *sphb = SPAPR_PCI_HOST_BRIDGE(drc->dev);
    int intc_phandle;

    intc_phandle = spapr_irq_get_phandle(spapr, spapr->fdt_blob, errp);
    if (intc_phandle <= 0) {
        return -1;
    }

    if (spapr_dt_phb(spapr, sphb, intc_phandle, fdt, fdt_start_offset)) {
        error_setg(errp, "unable to create FDT node for PHB %d", sphb->index);
        return -1;
    }

    /* generally SLOF creates these, for hotplug it's up to QEMU */
    _FDT(fdt_setprop_string(fdt, *fdt_start_offset, "name", "pci"));

    return 0;
}

static bool spapr_phb_pre_plug(HotplugHandler *hotplug_dev, DeviceState *dev,
                               Error **errp)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(OBJECT(hotplug_dev));
    SpaprPhbState *sphb = SPAPR_PCI_HOST_BRIDGE(dev);
    SpaprMachineClass *smc = SPAPR_MACHINE_GET_CLASS(spapr);
    const unsigned windows_supported = spapr_phb_windows_supported(sphb);
    SpaprDrc *drc;

    if (dev->hotplugged && !smc->dr_phb_enabled) {
        error_setg(errp, "PHB hotplug not supported for this machine");
        return false;
    }

    if (sphb->index == (uint32_t)-1) {
        error_setg(errp, "\"index\" for PAPR PHB is mandatory");
        return false;
    }

    drc = spapr_drc_by_id(TYPE_SPAPR_DRC_PHB, sphb->index);
    if (drc && drc->dev) {
        error_setg(errp, "PHB %d already attached", sphb->index);
        return false;
    }

    /*
     * This will check that sphb->index doesn't exceed the maximum number of
     * PHBs for the current machine type.
     */
    return
        smc->phb_placement(spapr, sphb->index,
                           &sphb->buid, &sphb->io_win_addr,
                           &sphb->mem_win_addr, &sphb->mem64_win_addr,
                           windows_supported, sphb->dma_liobn,
                           errp);
}

static void spapr_phb_plug(HotplugHandler *hotplug_dev, DeviceState *dev)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(OBJECT(hotplug_dev));
    SpaprMachineClass *smc = SPAPR_MACHINE_GET_CLASS(spapr);
    SpaprPhbState *sphb = SPAPR_PCI_HOST_BRIDGE(dev);
    SpaprDrc *drc;
    bool hotplugged = spapr_drc_hotplugged(dev);

    if (!smc->dr_phb_enabled) {
        return;
    }

    drc = spapr_drc_by_id(TYPE_SPAPR_DRC_PHB, sphb->index);
    /* hotplug hooks should check it's enabled before getting this far */
    assert(drc);

    /* spapr_phb_pre_plug() already checked the DRC is attachable */
    spapr_drc_attach(drc, dev);

    if (hotplugged) {
        spapr_hotplug_req_add_by_index(drc);
    } else {
        spapr_drc_reset(drc);
    }
}

void spapr_phb_release(DeviceState *dev)
{
    HotplugHandler *hotplug_ctrl = qdev_get_hotplug_handler(dev);

    hotplug_handler_unplug(hotplug_ctrl, dev, &error_abort);
    object_unparent(OBJECT(dev));
}

static void spapr_phb_unplug(HotplugHandler *hotplug_dev, DeviceState *dev)
{
    qdev_unrealize(dev);
}

static void spapr_phb_unplug_request(HotplugHandler *hotplug_dev,
                                     DeviceState *dev, Error **errp)
{
    SpaprPhbState *sphb = SPAPR_PCI_HOST_BRIDGE(dev);
    SpaprDrc *drc;

    drc = spapr_drc_by_id(TYPE_SPAPR_DRC_PHB, sphb->index);
    assert(drc);

    if (!spapr_drc_unplug_requested(drc)) {
        spapr_drc_unplug_request(drc);
        spapr_hotplug_req_remove_by_index(drc);
    } else {
        error_setg(errp,
                   "PCI Host Bridge unplug already in progress for device %s",
                   dev->id);
    }
}

static
bool spapr_tpm_proxy_pre_plug(HotplugHandler *hotplug_dev, DeviceState *dev,
                              Error **errp)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(OBJECT(hotplug_dev));

    if (spapr->tpm_proxy != NULL) {
        error_setg(errp, "Only one TPM proxy can be specified for this machine");
        return false;
    }

    return true;
}

static void spapr_tpm_proxy_plug(HotplugHandler *hotplug_dev, DeviceState *dev)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(OBJECT(hotplug_dev));
    SpaprTpmProxy *tpm_proxy = SPAPR_TPM_PROXY(dev);

    /* Already checked in spapr_tpm_proxy_pre_plug() */
    g_assert(spapr->tpm_proxy == NULL);

    spapr->tpm_proxy = tpm_proxy;
}

static void spapr_tpm_proxy_unplug(HotplugHandler *hotplug_dev, DeviceState *dev)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(OBJECT(hotplug_dev));

    qdev_unrealize(dev);
    object_unparent(OBJECT(dev));
    spapr->tpm_proxy = NULL;
}

static void spapr_machine_device_plug(HotplugHandler *hotplug_dev,
                                      DeviceState *dev, Error **errp)
{
    if (object_dynamic_cast(OBJECT(dev), TYPE_PC_DIMM)) {
        spapr_memory_plug(hotplug_dev, dev);
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_SPAPR_CPU_CORE)) {
        spapr_core_plug(hotplug_dev, dev);
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_SPAPR_PCI_HOST_BRIDGE)) {
        spapr_phb_plug(hotplug_dev, dev);
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_SPAPR_TPM_PROXY)) {
        spapr_tpm_proxy_plug(hotplug_dev, dev);
    }
}

static void spapr_machine_device_unplug(HotplugHandler *hotplug_dev,
                                        DeviceState *dev, Error **errp)
{
    if (object_dynamic_cast(OBJECT(dev), TYPE_PC_DIMM)) {
        spapr_memory_unplug(hotplug_dev, dev);
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_SPAPR_CPU_CORE)) {
        spapr_core_unplug(hotplug_dev, dev);
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_SPAPR_PCI_HOST_BRIDGE)) {
        spapr_phb_unplug(hotplug_dev, dev);
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_SPAPR_TPM_PROXY)) {
        spapr_tpm_proxy_unplug(hotplug_dev, dev);
    }
}

bool spapr_memory_hot_unplug_supported(SpaprMachineState *spapr)
{
    return spapr_ovec_test(spapr->ov5_cas, OV5_HP_EVT) ||
        /*
         * CAS will process all pending unplug requests.
         *
         * HACK: a guest could theoretically have cleared all bits in OV5,
         * but none of the guests we care for do.
         */
        spapr_ovec_empty(spapr->ov5_cas);
}

static void spapr_machine_device_unplug_request(HotplugHandler *hotplug_dev,
                                                DeviceState *dev, Error **errp)
{
    SpaprMachineState *sms = SPAPR_MACHINE(OBJECT(hotplug_dev));
    MachineClass *mc = MACHINE_GET_CLASS(sms);
    SpaprMachineClass *smc = SPAPR_MACHINE_CLASS(mc);

    if (object_dynamic_cast(OBJECT(dev), TYPE_PC_DIMM)) {
        if (spapr_memory_hot_unplug_supported(sms)) {
            spapr_memory_unplug_request(hotplug_dev, dev, errp);
        } else {
            error_setg(errp, "Memory hot unplug not supported for this guest");
        }
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_SPAPR_CPU_CORE)) {
        if (!mc->has_hotpluggable_cpus) {
            error_setg(errp, "CPU hot unplug not supported on this machine");
            return;
        }
        spapr_core_unplug_request(hotplug_dev, dev, errp);
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_SPAPR_PCI_HOST_BRIDGE)) {
        if (!smc->dr_phb_enabled) {
            error_setg(errp, "PHB hot unplug not supported on this machine");
            return;
        }
        spapr_phb_unplug_request(hotplug_dev, dev, errp);
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_SPAPR_TPM_PROXY)) {
        spapr_tpm_proxy_unplug(hotplug_dev, dev);
    }
}

static void spapr_machine_device_pre_plug(HotplugHandler *hotplug_dev,
                                          DeviceState *dev, Error **errp)
{
    if (object_dynamic_cast(OBJECT(dev), TYPE_PC_DIMM)) {
        spapr_memory_pre_plug(hotplug_dev, dev, errp);
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_SPAPR_CPU_CORE)) {
        spapr_core_pre_plug(hotplug_dev, dev, errp);
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_SPAPR_PCI_HOST_BRIDGE)) {
        spapr_phb_pre_plug(hotplug_dev, dev, errp);
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_SPAPR_TPM_PROXY)) {
        spapr_tpm_proxy_pre_plug(hotplug_dev, dev, errp);
    }
}

static HotplugHandler *spapr_get_hotplug_handler(MachineState *machine,
                                                 DeviceState *dev)
{
    if (object_dynamic_cast(OBJECT(dev), TYPE_PC_DIMM) ||
        object_dynamic_cast(OBJECT(dev), TYPE_SPAPR_CPU_CORE) ||
        object_dynamic_cast(OBJECT(dev), TYPE_SPAPR_PCI_HOST_BRIDGE) ||
        object_dynamic_cast(OBJECT(dev), TYPE_SPAPR_TPM_PROXY)) {
        return HOTPLUG_HANDLER(machine);
    }
    if (object_dynamic_cast(OBJECT(dev), TYPE_PCI_DEVICE)) {
        PCIDevice *pcidev = PCI_DEVICE(dev);
        PCIBus *root = pci_device_root_bus(pcidev);
        SpaprPhbState *phb =
            (SpaprPhbState *)object_dynamic_cast(OBJECT(BUS(root)->parent),
                                                 TYPE_SPAPR_PCI_HOST_BRIDGE);

        if (phb) {
            return HOTPLUG_HANDLER(phb);
        }
    }
    return NULL;
}

static CpuInstanceProperties
spapr_cpu_index_to_props(MachineState *machine, unsigned cpu_index)
{
    CPUArchId *core_slot;
    MachineClass *mc = MACHINE_GET_CLASS(machine);

    /* make sure possible_cpu are initialized */
    mc->possible_cpu_arch_ids(machine);
    /* get CPU core slot containing thread that matches cpu_index */
    core_slot = spapr_find_cpu_slot(machine, cpu_index, NULL);
    assert(core_slot);
    return core_slot->props;
}

static int64_t spapr_get_default_cpu_node_id(const MachineState *ms, int idx)
{
    return idx / ms->smp.cores % ms->numa_state->num_nodes;
}

static const CPUArchIdList *spapr_possible_cpu_arch_ids(MachineState *machine)
{
    int i;
    unsigned int smp_threads = machine->smp.threads;
    unsigned int smp_cpus = machine->smp.cpus;
    const char *core_type;
    int spapr_max_cores = machine->smp.max_cpus / smp_threads;
    MachineClass *mc = MACHINE_GET_CLASS(machine);

    if (!mc->has_hotpluggable_cpus) {
        spapr_max_cores = QEMU_ALIGN_UP(smp_cpus, smp_threads) / smp_threads;
    }
    if (machine->possible_cpus) {
        assert(machine->possible_cpus->len == spapr_max_cores);
        return machine->possible_cpus;
    }

    core_type = spapr_get_cpu_core_type(machine->cpu_type);
    if (!core_type) {
        error_report("Unable to find sPAPR CPU Core definition");
        exit(1);
    }

    machine->possible_cpus = g_malloc0(sizeof(CPUArchIdList) +
                             sizeof(CPUArchId) * spapr_max_cores);
    machine->possible_cpus->len = spapr_max_cores;
    for (i = 0; i < machine->possible_cpus->len; i++) {
        int core_id = i * smp_threads;

        machine->possible_cpus->cpus[i].type = core_type;
        machine->possible_cpus->cpus[i].vcpus_count = smp_threads;
        machine->possible_cpus->cpus[i].arch_id = core_id;
        machine->possible_cpus->cpus[i].props.has_core_id = true;
        machine->possible_cpus->cpus[i].props.core_id = core_id;
    }
    return machine->possible_cpus;
}

static bool spapr_phb_placement(SpaprMachineState *spapr, uint32_t index,
                                uint64_t *buid, hwaddr *pio,
                                hwaddr *mmio32, hwaddr *mmio64,
                                unsigned n_dma, uint32_t *liobns, Error **errp)
{
    /*
     * New-style PHB window placement.
     *
     * Goals: Gives large (1TiB), naturally aligned 64-bit MMIO window
     * for each PHB, in addition to 2GiB 32-bit MMIO and 64kiB PIO
     * windows.
     *
     * Some guest kernels can't work with MMIO windows above 1<<46
     * (64TiB), so we place up to 31 PHBs in the area 32TiB..64TiB
     *
     * 32TiB..(33TiB+1984kiB) contains the 64kiB PIO windows for each
     * PHB stacked together.  (32TiB+2GiB)..(32TiB+64GiB) contains the
     * 2GiB 32-bit MMIO windows for each PHB.  Then 33..64TiB has the
     * 1TiB 64-bit MMIO windows for each PHB.
     */
    const uint64_t base_buid = 0x800000020000000ULL;
    int i;

    /* Sanity check natural alignments */
    QEMU_BUILD_BUG_ON((SPAPR_PCI_BASE % SPAPR_PCI_MEM64_WIN_SIZE) != 0);
    QEMU_BUILD_BUG_ON((SPAPR_PCI_LIMIT % SPAPR_PCI_MEM64_WIN_SIZE) != 0);
    QEMU_BUILD_BUG_ON((SPAPR_PCI_MEM64_WIN_SIZE % SPAPR_PCI_MEM32_WIN_SIZE) != 0);
    QEMU_BUILD_BUG_ON((SPAPR_PCI_MEM32_WIN_SIZE % SPAPR_PCI_IO_WIN_SIZE) != 0);
    /* Sanity check bounds */
    QEMU_BUILD_BUG_ON((SPAPR_MAX_PHBS * SPAPR_PCI_IO_WIN_SIZE) >
                      SPAPR_PCI_MEM32_WIN_SIZE);
    QEMU_BUILD_BUG_ON((SPAPR_MAX_PHBS * SPAPR_PCI_MEM32_WIN_SIZE) >
                      SPAPR_PCI_MEM64_WIN_SIZE);

    if (index >= SPAPR_MAX_PHBS) {
        error_setg(errp, "\"index\" for PAPR PHB is too large (max %llu)",
                   SPAPR_MAX_PHBS - 1);
        return false;
    }

    *buid = base_buid + index;
    for (i = 0; i < n_dma; ++i) {
        liobns[i] = SPAPR_PCI_LIOBN(index, i);
    }

    *pio = SPAPR_PCI_BASE + index * SPAPR_PCI_IO_WIN_SIZE;
    *mmio32 = SPAPR_PCI_BASE + (index + 1) * SPAPR_PCI_MEM32_WIN_SIZE;
    *mmio64 = SPAPR_PCI_BASE + (index + 1) * SPAPR_PCI_MEM64_WIN_SIZE;
    return true;
}

static ICSState *spapr_ics_get(XICSFabric *dev, int irq)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(dev);

    return ics_valid_irq(spapr->ics, irq) ? spapr->ics : NULL;
}

static void spapr_ics_resend(XICSFabric *dev)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(dev);

    ics_resend(spapr->ics);
}

static ICPState *spapr_icp_get(XICSFabric *xi, int vcpu_id)
{
    PowerPCCPU *cpu = spapr_find_cpu(vcpu_id);

    return cpu ? spapr_cpu_state(cpu)->icp : NULL;
}

static void spapr_pic_print_info(InterruptStatsProvider *obj,
                                 Monitor *mon)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(obj);

    spapr_irq_print_info(spapr, mon);
    monitor_printf(mon, "irqchip: %s\n",
                   kvm_irqchip_in_kernel() ? "in-kernel" : "emulated");
}

/*
 * This is a XIVE only operation
 */
static int spapr_match_nvt(XiveFabric *xfb, uint8_t format,
                           uint8_t nvt_blk, uint32_t nvt_idx,
                           bool cam_ignore, uint8_t priority,
                           uint32_t logic_serv, XiveTCTXMatch *match)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(xfb);
    XivePresenter *xptr = XIVE_PRESENTER(spapr->active_intc);
    XivePresenterClass *xpc = XIVE_PRESENTER_GET_CLASS(xptr);
    int count;

    count = xpc->match_nvt(xptr, format, nvt_blk, nvt_idx, cam_ignore,
                           priority, logic_serv, match);
    if (count < 0) {
        return count;
    }

    /*
     * When we implement the save and restore of the thread interrupt
     * contexts in the enter/exit CPU handlers of the machine and the
     * escalations in QEMU, we should be able to handle non dispatched
     * vCPUs.
     *
     * Until this is done, the sPAPR machine should find at least one
     * matching context always.
     */
    if (count == 0) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: NVT %x/%x is not dispatched\n",
                      nvt_blk, nvt_idx);
    }

    return count;
}

int spapr_get_vcpu_id(PowerPCCPU *cpu)
{
    return cpu->vcpu_id;
}

bool spapr_set_vcpu_id(PowerPCCPU *cpu, int cpu_index, Error **errp)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(qdev_get_machine());
    MachineState *ms = MACHINE(spapr);
    int vcpu_id;

    vcpu_id = spapr_vcpu_id(spapr, cpu_index);

    if (kvm_enabled() && !kvm_vcpu_id_is_valid(vcpu_id)) {
        error_setg(errp, "Can't create CPU with id %d in KVM", vcpu_id);
        error_append_hint(errp, "Adjust the number of cpus to %d "
                          "or try to raise the number of threads per core\n",
                          vcpu_id * ms->smp.threads / spapr->vsmt);
        return false;
    }

    cpu->vcpu_id = vcpu_id;
    return true;
}

PowerPCCPU *spapr_find_cpu(int vcpu_id)
{
    CPUState *cs;

    CPU_FOREACH(cs) {
        PowerPCCPU *cpu = POWERPC_CPU(cs);

        if (spapr_get_vcpu_id(cpu) == vcpu_id) {
            return cpu;
        }
    }

    return NULL;
}

static bool spapr_cpu_in_nested(PowerPCCPU *cpu)
{
    SpaprCpuState *spapr_cpu = spapr_cpu_state(cpu);

    return spapr_cpu->in_nested;
}

static void spapr_cpu_exec_enter(PPCVirtualHypervisor *vhyp, PowerPCCPU *cpu)
{
    SpaprCpuState *spapr_cpu = spapr_cpu_state(cpu);

    /* These are only called by TCG, KVM maintains dispatch state */

    spapr_cpu->prod = false;
    if (spapr_cpu->vpa_addr) {
        CPUState *cs = CPU(cpu);
        uint32_t dispatch;

        dispatch = ldl_be_phys(cs->as,
                               spapr_cpu->vpa_addr + VPA_DISPATCH_COUNTER);
        dispatch++;
        if ((dispatch & 1) != 0) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "VPA: incorrect dispatch counter value for "
                          "dispatched partition %u, correcting.\n", dispatch);
            dispatch++;
        }
        stl_be_phys(cs->as,
                    spapr_cpu->vpa_addr + VPA_DISPATCH_COUNTER, dispatch);
    }
}

static void spapr_cpu_exec_exit(PPCVirtualHypervisor *vhyp, PowerPCCPU *cpu)
{
    SpaprCpuState *spapr_cpu = spapr_cpu_state(cpu);

    if (spapr_cpu->vpa_addr) {
        CPUState *cs = CPU(cpu);
        uint32_t dispatch;

        dispatch = ldl_be_phys(cs->as,
                               spapr_cpu->vpa_addr + VPA_DISPATCH_COUNTER);
        dispatch++;
        if ((dispatch & 1) != 1) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "VPA: incorrect dispatch counter value for "
                          "preempted partition %u, correcting.\n", dispatch);
            dispatch++;
        }
        stl_be_phys(cs->as,
                    spapr_cpu->vpa_addr + VPA_DISPATCH_COUNTER, dispatch);
    }
}

static void spapr_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    SpaprMachineClass *smc = SPAPR_MACHINE_CLASS(oc);
    FWPathProviderClass *fwc = FW_PATH_PROVIDER_CLASS(oc);
    NMIClass *nc = NMI_CLASS(oc);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(oc);
    PPCVirtualHypervisorClass *vhc = PPC_VIRTUAL_HYPERVISOR_CLASS(oc);
    XICSFabricClass *xic = XICS_FABRIC_CLASS(oc);
    InterruptStatsProviderClass *ispc = INTERRUPT_STATS_PROVIDER_CLASS(oc);
    XiveFabricClass *xfc = XIVE_FABRIC_CLASS(oc);
    VofMachineIfClass *vmc = VOF_MACHINE_CLASS(oc);

    mc->desc = "pSeries Logical Partition (PAPR compliant)";
    mc->ignore_boot_device_suffixes = true;

    /*
     * We set up the default / latest behaviour here.  The class_init
     * functions for the specific versioned machine types can override
     * these details for backwards compatibility
     */
    mc->init = spapr_machine_init;
    mc->reset = spapr_machine_reset;
    mc->block_default_type = IF_SCSI;

    /*
     * Setting max_cpus to INT32_MAX. Both KVM and TCG max_cpus values
     * should be limited by the host capability instead of hardcoded.
     * max_cpus for KVM guests will be checked in kvm_init(), and TCG
     * guests are welcome to have as many CPUs as the host are capable
     * of emulate.
     */
    mc->max_cpus = INT32_MAX;

    mc->no_parallel = 1;
    mc->default_boot_order = "";
    mc->default_ram_size = 512 * MiB;
    mc->default_ram_id = "ppc_spapr.ram";
    mc->default_display = "std";
    mc->kvm_type = spapr_kvm_type;
    machine_class_allow_dynamic_sysbus_dev(mc, TYPE_SPAPR_PCI_HOST_BRIDGE);
    mc->pci_allow_0_address = true;
    assert(!mc->get_hotplug_handler);
    mc->get_hotplug_handler = spapr_get_hotplug_handler;
    hc->pre_plug = spapr_machine_device_pre_plug;
    hc->plug = spapr_machine_device_plug;
    mc->cpu_index_to_instance_props = spapr_cpu_index_to_props;
    mc->get_default_cpu_node_id = spapr_get_default_cpu_node_id;
    mc->possible_cpu_arch_ids = spapr_possible_cpu_arch_ids;
    hc->unplug_request = spapr_machine_device_unplug_request;
    hc->unplug = spapr_machine_device_unplug;

    smc->dr_lmb_enabled = true;
    smc->update_dt_enabled = true;
    mc->default_cpu_type = POWERPC_CPU_TYPE_NAME("power9_v2.2");
    mc->has_hotpluggable_cpus = true;
    mc->nvdimm_supported = true;
    smc->resize_hpt_default = SPAPR_RESIZE_HPT_ENABLED;
    fwc->get_dev_path = spapr_get_fw_dev_path;
    nc->nmi_monitor_handler = spapr_nmi;
    smc->phb_placement = spapr_phb_placement;
    vhc->cpu_in_nested = spapr_cpu_in_nested;
    vhc->deliver_hv_excp = spapr_exit_nested;
    vhc->hypercall = emulate_spapr_hypercall;
    vhc->hpt_mask = spapr_hpt_mask;
    vhc->map_hptes = spapr_map_hptes;
    vhc->unmap_hptes = spapr_unmap_hptes;
    vhc->hpte_set_c = spapr_hpte_set_c;
    vhc->hpte_set_r = spapr_hpte_set_r;
    vhc->get_pate = spapr_get_pate;
    vhc->encode_hpt_for_kvm_pr = spapr_encode_hpt_for_kvm_pr;
    vhc->cpu_exec_enter = spapr_cpu_exec_enter;
    vhc->cpu_exec_exit = spapr_cpu_exec_exit;
    xic->ics_get = spapr_ics_get;
    xic->ics_resend = spapr_ics_resend;
    xic->icp_get = spapr_icp_get;
    ispc->print_info = spapr_pic_print_info;
    /* Force NUMA node memory size to be a multiple of
     * SPAPR_MEMORY_BLOCK_SIZE (256M) since that's the granularity
     * in which LMBs are represented and hot-added
     */
    mc->numa_mem_align_shift = 28;
    mc->auto_enable_numa = true;

    smc->default_caps.caps[SPAPR_CAP_HTM] = SPAPR_CAP_OFF;
    smc->default_caps.caps[SPAPR_CAP_VSX] = SPAPR_CAP_ON;
    smc->default_caps.caps[SPAPR_CAP_DFP] = SPAPR_CAP_ON;
    smc->default_caps.caps[SPAPR_CAP_CFPC] = SPAPR_CAP_WORKAROUND;
    smc->default_caps.caps[SPAPR_CAP_SBBC] = SPAPR_CAP_WORKAROUND;
    smc->default_caps.caps[SPAPR_CAP_IBS] = SPAPR_CAP_WORKAROUND;
    smc->default_caps.caps[SPAPR_CAP_HPT_MAXPAGESIZE] = 16; /* 64kiB */
    smc->default_caps.caps[SPAPR_CAP_NESTED_KVM_HV] = SPAPR_CAP_OFF;
    smc->default_caps.caps[SPAPR_CAP_LARGE_DECREMENTER] = SPAPR_CAP_ON;
    smc->default_caps.caps[SPAPR_CAP_CCF_ASSIST] = SPAPR_CAP_ON;
    smc->default_caps.caps[SPAPR_CAP_FWNMI] = SPAPR_CAP_ON;
    smc->default_caps.caps[SPAPR_CAP_RPT_INVALIDATE] = SPAPR_CAP_OFF;

    /*
     * This cap specifies whether the AIL 3 mode for
     * H_SET_RESOURCE is supported. The default is modified
     * by default_caps_with_cpu().
     */
    smc->default_caps.caps[SPAPR_CAP_AIL_MODE_3] = SPAPR_CAP_ON;
    spapr_caps_add_properties(smc);
    smc->irq = &spapr_irq_dual;
    smc->dr_phb_enabled = true;
    smc->linux_pci_probe = true;
    smc->smp_threads_vsmt = true;
    smc->nr_xirqs = SPAPR_NR_XIRQS;
    xfc->match_nvt = spapr_match_nvt;
    vmc->client_architecture_support = spapr_vof_client_architecture_support;
    vmc->quiesce = spapr_vof_quiesce;
    vmc->setprop = spapr_vof_setprop;
}

static const TypeInfo spapr_machine_info = {
    .name          = TYPE_SPAPR_MACHINE,
    .parent        = TYPE_MACHINE,
    .abstract      = true,
    .instance_size = sizeof(SpaprMachineState),
    .instance_init = spapr_instance_init,
    .instance_finalize = spapr_machine_finalizefn,
    .class_size    = sizeof(SpaprMachineClass),
    .class_init    = spapr_machine_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_FW_PATH_PROVIDER },
        { TYPE_NMI },
        { TYPE_HOTPLUG_HANDLER },
        { TYPE_PPC_VIRTUAL_HYPERVISOR },
        { TYPE_XICS_FABRIC },
        { TYPE_INTERRUPT_STATS_PROVIDER },
        { TYPE_XIVE_FABRIC },
        { TYPE_VOF_MACHINE_IF },
        { }
    },
};

static void spapr_machine_latest_class_options(MachineClass *mc)
{
    mc->alias = "pseries";
    mc->is_default = true;
}

#define DEFINE_SPAPR_MACHINE(suffix, verstr, latest)                 \
    static void spapr_machine_##suffix##_class_init(ObjectClass *oc, \
                                                    void *data)      \
    {                                                                \
        MachineClass *mc = MACHINE_CLASS(oc);                        \
        spapr_machine_##suffix##_class_options(mc);                  \
        if (latest) {                                                \
            spapr_machine_latest_class_options(mc);                  \
        }                                                            \
    }                                                                \
    static const TypeInfo spapr_machine_##suffix##_info = {          \
        .name = MACHINE_TYPE_NAME("pseries-" verstr),                \
        .parent = TYPE_SPAPR_MACHINE,                                \
        .class_init = spapr_machine_##suffix##_class_init,           \
    };                                                               \
    static void spapr_machine_register_##suffix(void)                \
    {                                                                \
        type_register(&spapr_machine_##suffix##_info);               \
    }                                                                \
    type_init(spapr_machine_register_##suffix)

/*
 * pseries-8.2
 */
static void spapr_machine_8_2_class_options(MachineClass *mc)
{
    /* Defaults for the latest behaviour inherited from the base class */
}

DEFINE_SPAPR_MACHINE(8_2, "8.2", true);

/*
 * pseries-8.1
 */
static void spapr_machine_8_1_class_options(MachineClass *mc)
{
    spapr_machine_8_2_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_8_1, hw_compat_8_1_len);
}

DEFINE_SPAPR_MACHINE(8_1, "8.1", false);

/*
 * pseries-8.0
 */
static void spapr_machine_8_0_class_options(MachineClass *mc)
{
    spapr_machine_8_1_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_8_0, hw_compat_8_0_len);
}

DEFINE_SPAPR_MACHINE(8_0, "8.0", false);

/*
 * pseries-7.2
 */
static void spapr_machine_7_2_class_options(MachineClass *mc)
{
    spapr_machine_8_0_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_7_2, hw_compat_7_2_len);
}

DEFINE_SPAPR_MACHINE(7_2, "7.2", false);

/*
 * pseries-7.1
 */
static void spapr_machine_7_1_class_options(MachineClass *mc)
{
    spapr_machine_7_2_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_7_1, hw_compat_7_1_len);
}

DEFINE_SPAPR_MACHINE(7_1, "7.1", false);

/*
 * pseries-7.0
 */
static void spapr_machine_7_0_class_options(MachineClass *mc)
{
    spapr_machine_7_1_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_7_0, hw_compat_7_0_len);
}

DEFINE_SPAPR_MACHINE(7_0, "7.0", false);

/*
 * pseries-6.2
 */
static void spapr_machine_6_2_class_options(MachineClass *mc)
{
    spapr_machine_7_0_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_6_2, hw_compat_6_2_len);
}

DEFINE_SPAPR_MACHINE(6_2, "6.2", false);

/*
 * pseries-6.1
 */
static void spapr_machine_6_1_class_options(MachineClass *mc)
{
    SpaprMachineClass *smc = SPAPR_MACHINE_CLASS(mc);

    spapr_machine_6_2_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_6_1, hw_compat_6_1_len);
    smc->pre_6_2_numa_affinity = true;
    mc->smp_props.prefer_sockets = true;
}

DEFINE_SPAPR_MACHINE(6_1, "6.1", false);

/*
 * pseries-6.0
 */
static void spapr_machine_6_0_class_options(MachineClass *mc)
{
    spapr_machine_6_1_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_6_0, hw_compat_6_0_len);
}

DEFINE_SPAPR_MACHINE(6_0, "6.0", false);

/*
 * pseries-5.2
 */
static void spapr_machine_5_2_class_options(MachineClass *mc)
{
    spapr_machine_6_0_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_5_2, hw_compat_5_2_len);
}

DEFINE_SPAPR_MACHINE(5_2, "5.2", false);

/*
 * pseries-5.1
 */
static void spapr_machine_5_1_class_options(MachineClass *mc)
{
    SpaprMachineClass *smc = SPAPR_MACHINE_CLASS(mc);

    spapr_machine_5_2_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_5_1, hw_compat_5_1_len);
    smc->pre_5_2_numa_associativity = true;
}

DEFINE_SPAPR_MACHINE(5_1, "5.1", false);

/*
 * pseries-5.0
 */
static void spapr_machine_5_0_class_options(MachineClass *mc)
{
    SpaprMachineClass *smc = SPAPR_MACHINE_CLASS(mc);
    static GlobalProperty compat[] = {
        { TYPE_SPAPR_PCI_HOST_BRIDGE, "pre-5.1-associativity", "on" },
    };

    spapr_machine_5_1_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_5_0, hw_compat_5_0_len);
    compat_props_add(mc->compat_props, compat, G_N_ELEMENTS(compat));
    mc->numa_mem_supported = true;
    smc->pre_5_1_assoc_refpoints = true;
}

DEFINE_SPAPR_MACHINE(5_0, "5.0", false);

/*
 * pseries-4.2
 */
static void spapr_machine_4_2_class_options(MachineClass *mc)
{
    SpaprMachineClass *smc = SPAPR_MACHINE_CLASS(mc);

    spapr_machine_5_0_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_4_2, hw_compat_4_2_len);
    smc->default_caps.caps[SPAPR_CAP_CCF_ASSIST] = SPAPR_CAP_OFF;
    smc->default_caps.caps[SPAPR_CAP_FWNMI] = SPAPR_CAP_OFF;
    smc->rma_limit = 16 * GiB;
    mc->nvdimm_supported = false;
}

DEFINE_SPAPR_MACHINE(4_2, "4.2", false);

/*
 * pseries-4.1
 */
static void spapr_machine_4_1_class_options(MachineClass *mc)
{
    SpaprMachineClass *smc = SPAPR_MACHINE_CLASS(mc);
    static GlobalProperty compat[] = {
        /* Only allow 4kiB and 64kiB IOMMU pagesizes */
        { TYPE_SPAPR_PCI_HOST_BRIDGE, "pgsz", "0x11000" },
    };

    spapr_machine_4_2_class_options(mc);
    smc->linux_pci_probe = false;
    smc->smp_threads_vsmt = false;
    compat_props_add(mc->compat_props, hw_compat_4_1, hw_compat_4_1_len);
    compat_props_add(mc->compat_props, compat, G_N_ELEMENTS(compat));
}

DEFINE_SPAPR_MACHINE(4_1, "4.1", false);

/*
 * pseries-4.0
 */
static bool phb_placement_4_0(SpaprMachineState *spapr, uint32_t index,
                              uint64_t *buid, hwaddr *pio,
                              hwaddr *mmio32, hwaddr *mmio64,
                              unsigned n_dma, uint32_t *liobns, Error **errp)
{
    if (!spapr_phb_placement(spapr, index, buid, pio, mmio32, mmio64, n_dma,
                             liobns, errp)) {
        return false;
    }
    return true;
}
static void spapr_machine_4_0_class_options(MachineClass *mc)
{
    SpaprMachineClass *smc = SPAPR_MACHINE_CLASS(mc);

    spapr_machine_4_1_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_4_0, hw_compat_4_0_len);
    smc->phb_placement = phb_placement_4_0;
    smc->irq = &spapr_irq_xics;
    smc->pre_4_1_migration = true;
}

DEFINE_SPAPR_MACHINE(4_0, "4.0", false);

/*
 * pseries-3.1
 */
static void spapr_machine_3_1_class_options(MachineClass *mc)
{
    SpaprMachineClass *smc = SPAPR_MACHINE_CLASS(mc);

    spapr_machine_4_0_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_3_1, hw_compat_3_1_len);

    mc->default_cpu_type = POWERPC_CPU_TYPE_NAME("power8_v2.0");
    smc->update_dt_enabled = false;
    smc->dr_phb_enabled = false;
    smc->broken_host_serial_model = true;
    smc->default_caps.caps[SPAPR_CAP_CFPC] = SPAPR_CAP_BROKEN;
    smc->default_caps.caps[SPAPR_CAP_SBBC] = SPAPR_CAP_BROKEN;
    smc->default_caps.caps[SPAPR_CAP_IBS] = SPAPR_CAP_BROKEN;
    smc->default_caps.caps[SPAPR_CAP_LARGE_DECREMENTER] = SPAPR_CAP_OFF;
}

DEFINE_SPAPR_MACHINE(3_1, "3.1", false);

/*
 * pseries-3.0
 */

static void spapr_machine_3_0_class_options(MachineClass *mc)
{
    SpaprMachineClass *smc = SPAPR_MACHINE_CLASS(mc);

    spapr_machine_3_1_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_3_0, hw_compat_3_0_len);

    smc->legacy_irq_allocation = true;
    smc->nr_xirqs = 0x400;
    smc->irq = &spapr_irq_xics_legacy;
}

DEFINE_SPAPR_MACHINE(3_0, "3.0", false);

/*
 * pseries-2.12
 */
static void spapr_machine_2_12_class_options(MachineClass *mc)
{
    SpaprMachineClass *smc = SPAPR_MACHINE_CLASS(mc);
    static GlobalProperty compat[] = {
        { TYPE_POWERPC_CPU, "pre-3.0-migration", "on" },
        { TYPE_SPAPR_CPU_CORE, "pre-3.0-migration", "on" },
    };

    spapr_machine_3_0_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_2_12, hw_compat_2_12_len);
    compat_props_add(mc->compat_props, compat, G_N_ELEMENTS(compat));

    /* We depend on kvm_enabled() to choose a default value for the
     * hpt-max-page-size capability. Of course we can't do it here
     * because this is too early and the HW accelerator isn't initialized
     * yet. Postpone this to machine init (see default_caps_with_cpu()).
     */
    smc->default_caps.caps[SPAPR_CAP_HPT_MAXPAGESIZE] = 0;
}

DEFINE_SPAPR_MACHINE(2_12, "2.12", false);

static void spapr_machine_2_12_sxxm_class_options(MachineClass *mc)
{
    SpaprMachineClass *smc = SPAPR_MACHINE_CLASS(mc);

    spapr_machine_2_12_class_options(mc);
    smc->default_caps.caps[SPAPR_CAP_CFPC] = SPAPR_CAP_WORKAROUND;
    smc->default_caps.caps[SPAPR_CAP_SBBC] = SPAPR_CAP_WORKAROUND;
    smc->default_caps.caps[SPAPR_CAP_IBS] = SPAPR_CAP_FIXED_CCD;
}

DEFINE_SPAPR_MACHINE(2_12_sxxm, "2.12-sxxm", false);

/*
 * pseries-2.11
 */

static void spapr_machine_2_11_class_options(MachineClass *mc)
{
    SpaprMachineClass *smc = SPAPR_MACHINE_CLASS(mc);

    spapr_machine_2_12_class_options(mc);
    smc->default_caps.caps[SPAPR_CAP_HTM] = SPAPR_CAP_ON;
    compat_props_add(mc->compat_props, hw_compat_2_11, hw_compat_2_11_len);
}

DEFINE_SPAPR_MACHINE(2_11, "2.11", false);

/*
 * pseries-2.10
 */

static void spapr_machine_2_10_class_options(MachineClass *mc)
{
    spapr_machine_2_11_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_2_10, hw_compat_2_10_len);
}

DEFINE_SPAPR_MACHINE(2_10, "2.10", false);

/*
 * pseries-2.9
 */

static void spapr_machine_2_9_class_options(MachineClass *mc)
{
    SpaprMachineClass *smc = SPAPR_MACHINE_CLASS(mc);
    static GlobalProperty compat[] = {
        { TYPE_POWERPC_CPU, "pre-2.10-migration", "on" },
    };

    spapr_machine_2_10_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_2_9, hw_compat_2_9_len);
    compat_props_add(mc->compat_props, compat, G_N_ELEMENTS(compat));
    smc->pre_2_10_has_unused_icps = true;
    smc->resize_hpt_default = SPAPR_RESIZE_HPT_DISABLED;
}

DEFINE_SPAPR_MACHINE(2_9, "2.9", false);

/*
 * pseries-2.8
 */

static void spapr_machine_2_8_class_options(MachineClass *mc)
{
    static GlobalProperty compat[] = {
        { TYPE_SPAPR_PCI_HOST_BRIDGE, "pcie-extended-configuration-space", "off" },
    };

    spapr_machine_2_9_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_2_8, hw_compat_2_8_len);
    compat_props_add(mc->compat_props, compat, G_N_ELEMENTS(compat));
    mc->numa_mem_align_shift = 23;
}

DEFINE_SPAPR_MACHINE(2_8, "2.8", false);

/*
 * pseries-2.7
 */

static bool phb_placement_2_7(SpaprMachineState *spapr, uint32_t index,
                              uint64_t *buid, hwaddr *pio,
                              hwaddr *mmio32, hwaddr *mmio64,
                              unsigned n_dma, uint32_t *liobns, Error **errp)
{
    /* Legacy PHB placement for pseries-2.7 and earlier machine types */
    const uint64_t base_buid = 0x800000020000000ULL;
    const hwaddr phb_spacing = 0x1000000000ULL; /* 64 GiB */
    const hwaddr mmio_offset = 0xa0000000; /* 2 GiB + 512 MiB */
    const hwaddr pio_offset = 0x80000000; /* 2 GiB */
    const uint32_t max_index = 255;
    const hwaddr phb0_alignment = 0x10000000000ULL; /* 1 TiB */

    uint64_t ram_top = MACHINE(spapr)->ram_size;
    hwaddr phb0_base, phb_base;
    int i;

    /* Do we have device memory? */
    if (MACHINE(spapr)->device_memory) {
        /* Can't just use maxram_size, because there may be an
         * alignment gap between normal and device memory regions
         */
        ram_top = MACHINE(spapr)->device_memory->base +
            memory_region_size(&MACHINE(spapr)->device_memory->mr);
    }

    phb0_base = QEMU_ALIGN_UP(ram_top, phb0_alignment);

    if (index > max_index) {
        error_setg(errp, "\"index\" for PAPR PHB is too large (max %u)",
                   max_index);
        return false;
    }

    *buid = base_buid + index;
    for (i = 0; i < n_dma; ++i) {
        liobns[i] = SPAPR_PCI_LIOBN(index, i);
    }

    phb_base = phb0_base + index * phb_spacing;
    *pio = phb_base + pio_offset;
    *mmio32 = phb_base + mmio_offset;
    /*
     * We don't set the 64-bit MMIO window, relying on the PHB's
     * fallback behaviour of automatically splitting a large "32-bit"
     * window into contiguous 32-bit and 64-bit windows
     */

    return true;
}

static void spapr_machine_2_7_class_options(MachineClass *mc)
{
    SpaprMachineClass *smc = SPAPR_MACHINE_CLASS(mc);
    static GlobalProperty compat[] = {
        { TYPE_SPAPR_PCI_HOST_BRIDGE, "mem_win_size", "0xf80000000", },
        { TYPE_SPAPR_PCI_HOST_BRIDGE, "mem64_win_size", "0", },
        { TYPE_POWERPC_CPU, "pre-2.8-migration", "on", },
        { TYPE_SPAPR_PCI_HOST_BRIDGE, "pre-2.8-migration", "on", },
    };

    spapr_machine_2_8_class_options(mc);
    mc->default_cpu_type = POWERPC_CPU_TYPE_NAME("power7_v2.3");
    mc->default_machine_opts = "modern-hotplug-events=off";
    compat_props_add(mc->compat_props, hw_compat_2_7, hw_compat_2_7_len);
    compat_props_add(mc->compat_props, compat, G_N_ELEMENTS(compat));
    smc->phb_placement = phb_placement_2_7;
}

DEFINE_SPAPR_MACHINE(2_7, "2.7", false);

/*
 * pseries-2.6
 */

static void spapr_machine_2_6_class_options(MachineClass *mc)
{
    static GlobalProperty compat[] = {
        { TYPE_SPAPR_PCI_HOST_BRIDGE, "ddw", "off" },
    };

    spapr_machine_2_7_class_options(mc);
    mc->has_hotpluggable_cpus = false;
    compat_props_add(mc->compat_props, hw_compat_2_6, hw_compat_2_6_len);
    compat_props_add(mc->compat_props, compat, G_N_ELEMENTS(compat));
}

DEFINE_SPAPR_MACHINE(2_6, "2.6", false);

/*
 * pseries-2.5
 */

static void spapr_machine_2_5_class_options(MachineClass *mc)
{
    SpaprMachineClass *smc = SPAPR_MACHINE_CLASS(mc);
    static GlobalProperty compat[] = {
        { "spapr-vlan", "use-rx-buffer-pools", "off" },
    };

    spapr_machine_2_6_class_options(mc);
    smc->use_ohci_by_default = true;
    compat_props_add(mc->compat_props, hw_compat_2_5, hw_compat_2_5_len);
    compat_props_add(mc->compat_props, compat, G_N_ELEMENTS(compat));
}

DEFINE_SPAPR_MACHINE(2_5, "2.5", false);

/*
 * pseries-2.4
 */

static void spapr_machine_2_4_class_options(MachineClass *mc)
{
    SpaprMachineClass *smc = SPAPR_MACHINE_CLASS(mc);

    spapr_machine_2_5_class_options(mc);
    smc->dr_lmb_enabled = false;
    compat_props_add(mc->compat_props, hw_compat_2_4, hw_compat_2_4_len);
}

DEFINE_SPAPR_MACHINE(2_4, "2.4", false);

/*
 * pseries-2.3
 */

static void spapr_machine_2_3_class_options(MachineClass *mc)
{
    static GlobalProperty compat[] = {
        { "spapr-pci-host-bridge", "dynamic-reconfiguration", "off" },
    };
    spapr_machine_2_4_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_2_3, hw_compat_2_3_len);
    compat_props_add(mc->compat_props, compat, G_N_ELEMENTS(compat));
}
DEFINE_SPAPR_MACHINE(2_3, "2.3", false);

/*
 * pseries-2.2
 */

static void spapr_machine_2_2_class_options(MachineClass *mc)
{
    static GlobalProperty compat[] = {
        { TYPE_SPAPR_PCI_HOST_BRIDGE, "mem_win_size", "0x20000000" },
    };

    spapr_machine_2_3_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_2_2, hw_compat_2_2_len);
    compat_props_add(mc->compat_props, compat, G_N_ELEMENTS(compat));
    mc->default_machine_opts = "modern-hotplug-events=off,suppress-vmdesc=on";
}
DEFINE_SPAPR_MACHINE(2_2, "2.2", false);

/*
 * pseries-2.1
 */

static void spapr_machine_2_1_class_options(MachineClass *mc)
{
    spapr_machine_2_2_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_2_1, hw_compat_2_1_len);
}
DEFINE_SPAPR_MACHINE(2_1, "2.1", false);

static void spapr_machine_register_types(void)
{
    type_register_static(&spapr_machine_info);
}

type_init(spapr_machine_register_types)
