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
 *
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "sysemu/sysemu.h"
#include "sysemu/numa.h"
#include "hw/hw.h"
#include "qemu/log.h"
#include "hw/fw-path-provider.h"
#include "elf.h"
#include "net/net.h"
#include "sysemu/device_tree.h"
#include "sysemu/block-backend.h"
#include "sysemu/cpus.h"
#include "sysemu/kvm.h"
#include "sysemu/device_tree.h"
#include "kvm_ppc.h"
#include "migration/migration.h"
#include "mmu-hash64.h"
#include "qom/cpu.h"

#include "hw/boards.h"
#include "hw/ppc/ppc.h"
#include "hw/loader.h"

#include "hw/ppc/spapr.h"
#include "hw/ppc/spapr_vio.h"
#include "hw/pci-host/spapr.h"
#include "hw/ppc/xics.h"
#include "hw/pci/msi.h"

#include "hw/pci/pci.h"
#include "hw/scsi/scsi.h"
#include "hw/virtio/virtio-scsi.h"

#include "exec/address-spaces.h"
#include "hw/usb.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "trace.h"
#include "hw/nmi.h"

#include "hw/compat.h"
#include "qemu/cutils.h"

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
#define FDT_MAX_SIZE            0x100000
#define RTAS_MAX_SIZE           0x10000
#define RTAS_MAX_ADDR           0x80000000 /* RTAS must stay below that */
#define FW_MAX_SIZE             0x400000
#define FW_FILE_NAME            "slof.bin"
#define FW_OVERHEAD             0x2800000
#define KERNEL_LOAD_ADDR        FW_MAX_SIZE

#define MIN_RMA_SLOF            128UL

#define TIMEBASE_FREQ           512000000ULL

#define PHANDLE_XICP            0x00001111

#define HTAB_SIZE(spapr)        (1ULL << ((spapr)->htab_shift))

static XICSState *try_create_xics(const char *type, int nr_servers,
                                  int nr_irqs, Error **errp)
{
    Error *err = NULL;
    DeviceState *dev;

    dev = qdev_create(NULL, type);
    qdev_prop_set_uint32(dev, "nr_servers", nr_servers);
    qdev_prop_set_uint32(dev, "nr_irqs", nr_irqs);
    object_property_set_bool(OBJECT(dev), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        object_unparent(OBJECT(dev));
        return NULL;
    }
    return XICS_COMMON(dev);
}

static XICSState *xics_system_init(MachineState *machine,
                                   int nr_servers, int nr_irqs, Error **errp)
{
    XICSState *icp = NULL;

    if (kvm_enabled()) {
        Error *err = NULL;

        if (machine_kernel_irqchip_allowed(machine)) {
            icp = try_create_xics(TYPE_KVM_XICS, nr_servers, nr_irqs, &err);
        }
        if (machine_kernel_irqchip_required(machine) && !icp) {
            error_reportf_err(err,
                              "kernel_irqchip requested but unavailable: ");
        } else {
            error_free(err);
        }
    }

    if (!icp) {
        icp = try_create_xics(TYPE_XICS, nr_servers, nr_irqs, errp);
    }

    return icp;
}

static int spapr_fixup_cpu_smt_dt(void *fdt, int offset, PowerPCCPU *cpu,
                                  int smt_threads)
{
    int i, ret = 0;
    uint32_t servers_prop[smt_threads];
    uint32_t gservers_prop[smt_threads * 2];
    int index = ppc_get_vcpu_dt_id(cpu);

    if (cpu->cpu_version) {
        ret = fdt_setprop_cell(fdt, offset, "cpu-version", cpu->cpu_version);
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
                      servers_prop, sizeof(servers_prop));
    if (ret < 0) {
        return ret;
    }
    ret = fdt_setprop(fdt, offset, "ibm,ppc-interrupt-gserver#s",
                      gservers_prop, sizeof(gservers_prop));

    return ret;
}

static int spapr_fixup_cpu_numa_dt(void *fdt, int offset, CPUState *cs)
{
    int ret = 0;
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    int index = ppc_get_vcpu_dt_id(cpu);
    uint32_t associativity[] = {cpu_to_be32(0x5),
                                cpu_to_be32(0x0),
                                cpu_to_be32(0x0),
                                cpu_to_be32(0x0),
                                cpu_to_be32(cs->numa_node),
                                cpu_to_be32(index)};

    /* Advertise NUMA via ibm,associativity */
    if (nb_numa_nodes > 1) {
        ret = fdt_setprop(fdt, offset, "ibm,associativity", associativity,
                          sizeof(associativity));
    }

    return ret;
}

static int spapr_fixup_cpu_dt(void *fdt, sPAPRMachineState *spapr)
{
    int ret = 0, offset, cpus_offset;
    CPUState *cs;
    char cpu_model[32];
    int smt = kvmppc_smt_threads();
    uint32_t pft_size_prop[] = {0, cpu_to_be32(spapr->htab_shift)};

    CPU_FOREACH(cs) {
        PowerPCCPU *cpu = POWERPC_CPU(cs);
        DeviceClass *dc = DEVICE_GET_CLASS(cs);
        int index = ppc_get_vcpu_dt_id(cpu);

        if ((index % smt) != 0) {
            continue;
        }

        snprintf(cpu_model, 32, "%s@%x", dc->fw_name, index);

        cpus_offset = fdt_path_offset(fdt, "/cpus");
        if (cpus_offset < 0) {
            cpus_offset = fdt_add_subnode(fdt, fdt_path_offset(fdt, "/"),
                                          "cpus");
            if (cpus_offset < 0) {
                return cpus_offset;
            }
        }
        offset = fdt_subnode_offset(fdt, cpus_offset, cpu_model);
        if (offset < 0) {
            offset = fdt_add_subnode(fdt, cpus_offset, cpu_model);
            if (offset < 0) {
                return offset;
            }
        }

        ret = fdt_setprop(fdt, offset, "ibm,pft-size",
                          pft_size_prop, sizeof(pft_size_prop));
        if (ret < 0) {
            return ret;
        }

        ret = spapr_fixup_cpu_numa_dt(fdt, offset, cs);
        if (ret < 0) {
            return ret;
        }

        ret = spapr_fixup_cpu_smt_dt(fdt, offset, cpu,
                                     ppc_get_compat_smt_threads(cpu));
        if (ret < 0) {
            return ret;
        }
    }
    return ret;
}


static size_t create_page_sizes_prop(CPUPPCState *env, uint32_t *prop,
                                     size_t maxsize)
{
    size_t maxcells = maxsize / sizeof(uint32_t);
    int i, j, count;
    uint32_t *p = prop;

    for (i = 0; i < PPC_PAGE_SIZES_MAX_SZ; i++) {
        struct ppc_one_seg_page_size *sps = &env->sps.sps[i];

        if (!sps->page_shift) {
            break;
        }
        for (count = 0; count < PPC_PAGE_SIZES_MAX_SZ; count++) {
            if (sps->enc[count].page_shift == 0) {
                break;
            }
        }
        if ((p - prop) >= (maxcells - 3 - count * 2)) {
            break;
        }
        *(p++) = cpu_to_be32(sps->page_shift);
        *(p++) = cpu_to_be32(sps->slb_enc);
        *(p++) = cpu_to_be32(count);
        for (j = 0; j < count; j++) {
            *(p++) = cpu_to_be32(sps->enc[j].page_shift);
            *(p++) = cpu_to_be32(sps->enc[j].pte_enc);
        }
    }

    return (p - prop) * sizeof(uint32_t);
}

static hwaddr spapr_node0_size(void)
{
    MachineState *machine = MACHINE(qdev_get_machine());

    if (nb_numa_nodes) {
        int i;
        for (i = 0; i < nb_numa_nodes; ++i) {
            if (numa_info[i].node_mem) {
                return MIN(pow2floor(numa_info[i].node_mem),
                           machine->ram_size);
            }
        }
    }
    return machine->ram_size;
}

#define _FDT(exp) \
    do { \
        int ret = (exp);                                           \
        if (ret < 0) {                                             \
            fprintf(stderr, "qemu: error creating device tree: %s: %s\n", \
                    #exp, fdt_strerror(ret));                      \
            exit(1);                                               \
        }                                                          \
    } while (0)

static void add_str(GString *s, const gchar *s1)
{
    g_string_append_len(s, s1, strlen(s1) + 1);
}

static void *spapr_create_fdt_skel(hwaddr initrd_base,
                                   hwaddr initrd_size,
                                   hwaddr kernel_size,
                                   bool little_endian,
                                   const char *kernel_cmdline,
                                   uint32_t epow_irq)
{
    void *fdt;
    uint32_t start_prop = cpu_to_be32(initrd_base);
    uint32_t end_prop = cpu_to_be32(initrd_base + initrd_size);
    GString *hypertas = g_string_sized_new(256);
    GString *qemu_hypertas = g_string_sized_new(256);
    uint32_t refpoints[] = {cpu_to_be32(0x4), cpu_to_be32(0x4)};
    uint32_t interrupt_server_ranges_prop[] = {0, cpu_to_be32(max_cpus)};
    unsigned char vec5[] = {0x0, 0x0, 0x0, 0x0, 0x0, 0x80};
    char *buf;

    add_str(hypertas, "hcall-pft");
    add_str(hypertas, "hcall-term");
    add_str(hypertas, "hcall-dabr");
    add_str(hypertas, "hcall-interrupt");
    add_str(hypertas, "hcall-tce");
    add_str(hypertas, "hcall-vio");
    add_str(hypertas, "hcall-splpar");
    add_str(hypertas, "hcall-bulk");
    add_str(hypertas, "hcall-set-mode");
    add_str(qemu_hypertas, "hcall-memop1");

    fdt = g_malloc0(FDT_MAX_SIZE);
    _FDT((fdt_create(fdt, FDT_MAX_SIZE)));

    if (kernel_size) {
        _FDT((fdt_add_reservemap_entry(fdt, KERNEL_LOAD_ADDR, kernel_size)));
    }
    if (initrd_size) {
        _FDT((fdt_add_reservemap_entry(fdt, initrd_base, initrd_size)));
    }
    _FDT((fdt_finish_reservemap(fdt)));

    /* Root node */
    _FDT((fdt_begin_node(fdt, "")));
    _FDT((fdt_property_string(fdt, "device_type", "chrp")));
    _FDT((fdt_property_string(fdt, "model", "IBM pSeries (emulated by qemu)")));
    _FDT((fdt_property_string(fdt, "compatible", "qemu,pseries")));

    /*
     * Add info to guest to indentify which host is it being run on
     * and what is the uuid of the guest
     */
    if (kvmppc_get_host_model(&buf)) {
        _FDT((fdt_property_string(fdt, "host-model", buf)));
        g_free(buf);
    }
    if (kvmppc_get_host_serial(&buf)) {
        _FDT((fdt_property_string(fdt, "host-serial", buf)));
        g_free(buf);
    }

    buf = g_strdup_printf(UUID_FMT, qemu_uuid[0], qemu_uuid[1],
                          qemu_uuid[2], qemu_uuid[3], qemu_uuid[4],
                          qemu_uuid[5], qemu_uuid[6], qemu_uuid[7],
                          qemu_uuid[8], qemu_uuid[9], qemu_uuid[10],
                          qemu_uuid[11], qemu_uuid[12], qemu_uuid[13],
                          qemu_uuid[14], qemu_uuid[15]);

    _FDT((fdt_property_string(fdt, "vm,uuid", buf)));
    if (qemu_uuid_set) {
        _FDT((fdt_property_string(fdt, "system-id", buf)));
    }
    g_free(buf);

    if (qemu_get_vm_name()) {
        _FDT((fdt_property_string(fdt, "ibm,partition-name",
                                  qemu_get_vm_name())));
    }

    _FDT((fdt_property_cell(fdt, "#address-cells", 0x2)));
    _FDT((fdt_property_cell(fdt, "#size-cells", 0x2)));

    /* /chosen */
    _FDT((fdt_begin_node(fdt, "chosen")));

    /* Set Form1_affinity */
    _FDT((fdt_property(fdt, "ibm,architecture-vec-5", vec5, sizeof(vec5))));

    _FDT((fdt_property_string(fdt, "bootargs", kernel_cmdline)));
    _FDT((fdt_property(fdt, "linux,initrd-start",
                       &start_prop, sizeof(start_prop))));
    _FDT((fdt_property(fdt, "linux,initrd-end",
                       &end_prop, sizeof(end_prop))));
    if (kernel_size) {
        uint64_t kprop[2] = { cpu_to_be64(KERNEL_LOAD_ADDR),
                              cpu_to_be64(kernel_size) };

        _FDT((fdt_property(fdt, "qemu,boot-kernel", &kprop, sizeof(kprop))));
        if (little_endian) {
            _FDT((fdt_property(fdt, "qemu,boot-kernel-le", NULL, 0)));
        }
    }
    if (boot_menu) {
        _FDT((fdt_property_cell(fdt, "qemu,boot-menu", boot_menu)));
    }
    _FDT((fdt_property_cell(fdt, "qemu,graphic-width", graphic_width)));
    _FDT((fdt_property_cell(fdt, "qemu,graphic-height", graphic_height)));
    _FDT((fdt_property_cell(fdt, "qemu,graphic-depth", graphic_depth)));

    _FDT((fdt_end_node(fdt)));

    /* RTAS */
    _FDT((fdt_begin_node(fdt, "rtas")));

    if (!kvm_enabled() || kvmppc_spapr_use_multitce()) {
        add_str(hypertas, "hcall-multi-tce");
    }
    _FDT((fdt_property(fdt, "ibm,hypertas-functions", hypertas->str,
                       hypertas->len)));
    g_string_free(hypertas, TRUE);
    _FDT((fdt_property(fdt, "qemu,hypertas-functions", qemu_hypertas->str,
                       qemu_hypertas->len)));
    g_string_free(qemu_hypertas, TRUE);

    _FDT((fdt_property(fdt, "ibm,associativity-reference-points",
        refpoints, sizeof(refpoints))));

    _FDT((fdt_property_cell(fdt, "rtas-error-log-max", RTAS_ERROR_LOG_MAX)));
    _FDT((fdt_property_cell(fdt, "rtas-event-scan-rate",
                            RTAS_EVENT_SCAN_RATE)));

    if (msi_nonbroken) {
        _FDT((fdt_property(fdt, "ibm,change-msix-capable", NULL, 0)));
    }

    /*
     * According to PAPR, rtas ibm,os-term does not guarantee a return
     * back to the guest cpu.
     *
     * While an additional ibm,extended-os-term property indicates that
     * rtas call return will always occur. Set this property.
     */
    _FDT((fdt_property(fdt, "ibm,extended-os-term", NULL, 0)));

    _FDT((fdt_end_node(fdt)));

    /* interrupt controller */
    _FDT((fdt_begin_node(fdt, "interrupt-controller")));

    _FDT((fdt_property_string(fdt, "device_type",
                              "PowerPC-External-Interrupt-Presentation")));
    _FDT((fdt_property_string(fdt, "compatible", "IBM,ppc-xicp")));
    _FDT((fdt_property(fdt, "interrupt-controller", NULL, 0)));
    _FDT((fdt_property(fdt, "ibm,interrupt-server-ranges",
                       interrupt_server_ranges_prop,
                       sizeof(interrupt_server_ranges_prop))));
    _FDT((fdt_property_cell(fdt, "#interrupt-cells", 2)));
    _FDT((fdt_property_cell(fdt, "linux,phandle", PHANDLE_XICP)));
    _FDT((fdt_property_cell(fdt, "phandle", PHANDLE_XICP)));

    _FDT((fdt_end_node(fdt)));

    /* vdevice */
    _FDT((fdt_begin_node(fdt, "vdevice")));

    _FDT((fdt_property_string(fdt, "device_type", "vdevice")));
    _FDT((fdt_property_string(fdt, "compatible", "IBM,vdevice")));
    _FDT((fdt_property_cell(fdt, "#address-cells", 0x1)));
    _FDT((fdt_property_cell(fdt, "#size-cells", 0x0)));
    _FDT((fdt_property_cell(fdt, "#interrupt-cells", 0x2)));
    _FDT((fdt_property(fdt, "interrupt-controller", NULL, 0)));

    _FDT((fdt_end_node(fdt)));

    /* event-sources */
    spapr_events_fdt_skel(fdt, epow_irq);

    /* /hypervisor node */
    if (kvm_enabled()) {
        uint8_t hypercall[16];

        /* indicate KVM hypercall interface */
        _FDT((fdt_begin_node(fdt, "hypervisor")));
        _FDT((fdt_property_string(fdt, "compatible", "linux,kvm")));
        if (kvmppc_has_cap_fixup_hcalls()) {
            /*
             * Older KVM versions with older guest kernels were broken with the
             * magic page, don't allow the guest to map it.
             */
            if (!kvmppc_get_hypercall(first_cpu->env_ptr, hypercall,
                                      sizeof(hypercall))) {
                _FDT((fdt_property(fdt, "hcall-instructions", hypercall,
                                   sizeof(hypercall))));
            }
        }
        _FDT((fdt_end_node(fdt)));
    }

    _FDT((fdt_end_node(fdt))); /* close root node */
    _FDT((fdt_finish(fdt)));

    return fdt;
}

static int spapr_populate_memory_node(void *fdt, int nodeid, hwaddr start,
                                       hwaddr size)
{
    uint32_t associativity[] = {
        cpu_to_be32(0x4), /* length */
        cpu_to_be32(0x0), cpu_to_be32(0x0),
        cpu_to_be32(0x0), cpu_to_be32(nodeid)
    };
    char mem_name[32];
    uint64_t mem_reg_property[2];
    int off;

    mem_reg_property[0] = cpu_to_be64(start);
    mem_reg_property[1] = cpu_to_be64(size);

    sprintf(mem_name, "memory@" TARGET_FMT_lx, start);
    off = fdt_add_subnode(fdt, 0, mem_name);
    _FDT(off);
    _FDT((fdt_setprop_string(fdt, off, "device_type", "memory")));
    _FDT((fdt_setprop(fdt, off, "reg", mem_reg_property,
                      sizeof(mem_reg_property))));
    _FDT((fdt_setprop(fdt, off, "ibm,associativity", associativity,
                      sizeof(associativity))));
    return off;
}

static int spapr_populate_memory(sPAPRMachineState *spapr, void *fdt)
{
    MachineState *machine = MACHINE(spapr);
    hwaddr mem_start, node_size;
    int i, nb_nodes = nb_numa_nodes;
    NodeInfo *nodes = numa_info;
    NodeInfo ramnode;

    /* No NUMA nodes, assume there is just one node with whole RAM */
    if (!nb_numa_nodes) {
        nb_nodes = 1;
        ramnode.node_mem = machine->ram_size;
        nodes = &ramnode;
    }

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
            /* ppc_spapr_init() checks for rma_size <= node0_size already */
            spapr_populate_memory_node(fdt, i, 0, spapr->rma_size);
            mem_start += spapr->rma_size;
            node_size -= spapr->rma_size;
        }
        for ( ; node_size; ) {
            hwaddr sizetmp = pow2floor(node_size);

            /* mem_start != 0 here */
            if (ctzl(mem_start) < ctzl(sizetmp)) {
                sizetmp = 1ULL << ctzl(mem_start);
            }

            spapr_populate_memory_node(fdt, i, mem_start, sizetmp);
            node_size -= sizetmp;
            mem_start += sizetmp;
        }
    }

    return 0;
}

static void spapr_populate_cpu_dt(CPUState *cs, void *fdt, int offset,
                                  sPAPRMachineState *spapr)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cs);
    int index = ppc_get_vcpu_dt_id(cpu);
    uint32_t segs[] = {cpu_to_be32(28), cpu_to_be32(40),
                       0xffffffff, 0xffffffff};
    uint32_t tbfreq = kvm_enabled() ? kvmppc_get_tbfreq() : TIMEBASE_FREQ;
    uint32_t cpufreq = kvm_enabled() ? kvmppc_get_clockfreq() : 1000000000;
    uint32_t page_sizes_prop[64];
    size_t page_sizes_prop_size;
    uint32_t vcpus_per_socket = smp_threads * smp_cores;
    uint32_t pft_size_prop[] = {0, cpu_to_be32(spapr->htab_shift)};

    /* Note: we keep CI large pages off for now because a 64K capable guest
     * provisioned with large pages might otherwise try to map a qemu
     * framebuffer (or other kind of memory mapped PCI BAR) using 64K pages
     * even if that qemu runs on a 4k host.
     *
     * We can later add this bit back when we are confident this is not
     * an issue (!HV KVM or 64K host)
     */
    uint8_t pa_features_206[] = { 6, 0,
        0xf6, 0x1f, 0xc7, 0x00, 0x80, 0xc0 };
    uint8_t pa_features_207[] = { 24, 0,
        0xf6, 0x1f, 0xc7, 0xc0, 0x80, 0xf0,
        0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x80, 0x00,
        0x80, 0x00, 0x80, 0x00, 0x80, 0x00 };
    uint8_t *pa_features;
    size_t pa_size;

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
        fprintf(stderr, "Warning: Unknown L1 dcache size for cpu\n");
    }
    if (pcc->l1_icache_size) {
        _FDT((fdt_setprop_cell(fdt, offset, "i-cache-size",
                               pcc->l1_icache_size)));
    } else {
        fprintf(stderr, "Warning: Unknown L1 icache size for cpu\n");
    }

    _FDT((fdt_setprop_cell(fdt, offset, "timebase-frequency", tbfreq)));
    _FDT((fdt_setprop_cell(fdt, offset, "clock-frequency", cpufreq)));
    _FDT((fdt_setprop_cell(fdt, offset, "slb-size", env->slb_nr)));
    _FDT((fdt_setprop_cell(fdt, offset, "ibm,slb-size", env->slb_nr)));
    _FDT((fdt_setprop_string(fdt, offset, "status", "okay")));
    _FDT((fdt_setprop(fdt, offset, "64-bit", NULL, 0)));

    if (env->spr_cb[SPR_PURR].oea_read) {
        _FDT((fdt_setprop(fdt, offset, "ibm,purr", NULL, 0)));
    }

    if (env->mmu_model & POWERPC_MMU_1TSEG) {
        _FDT((fdt_setprop(fdt, offset, "ibm,processor-segment-sizes",
                          segs, sizeof(segs))));
    }

    /* Advertise VMX/VSX (vector extensions) if available
     *   0 / no property == no vector extensions
     *   1               == VMX / Altivec available
     *   2               == VSX available */
    if (env->insns_flags & PPC_ALTIVEC) {
        uint32_t vmx = (env->insns_flags2 & PPC2_VSX) ? 2 : 1;

        _FDT((fdt_setprop_cell(fdt, offset, "ibm,vmx", vmx)));
    }

    /* Advertise DFP (Decimal Floating Point) if available
     *   0 / no property == no DFP
     *   1               == DFP available */
    if (env->insns_flags2 & PPC2_DFP) {
        _FDT((fdt_setprop_cell(fdt, offset, "ibm,dfp", 1)));
    }

    page_sizes_prop_size = create_page_sizes_prop(env, page_sizes_prop,
                                                  sizeof(page_sizes_prop));
    if (page_sizes_prop_size) {
        _FDT((fdt_setprop(fdt, offset, "ibm,segment-page-sizes",
                          page_sizes_prop, page_sizes_prop_size)));
    }

    /* Do the ibm,pa-features property, adjust it for ci-large-pages */
    if (env->mmu_model == POWERPC_MMU_2_06) {
        pa_features = pa_features_206;
        pa_size = sizeof(pa_features_206);
    } else /* env->mmu_model == POWERPC_MMU_2_07 */ {
        pa_features = pa_features_207;
        pa_size = sizeof(pa_features_207);
    }
    if (env->ci_large_pages) {
        pa_features[3] |= 0x20;
    }
    _FDT((fdt_setprop(fdt, offset, "ibm,pa-features", pa_features, pa_size)));

    _FDT((fdt_setprop_cell(fdt, offset, "ibm,chip-id",
                           cs->cpu_index / vcpus_per_socket)));

    _FDT((fdt_setprop(fdt, offset, "ibm,pft-size",
                      pft_size_prop, sizeof(pft_size_prop))));

    _FDT(spapr_fixup_cpu_numa_dt(fdt, offset, cs));

    _FDT(spapr_fixup_cpu_smt_dt(fdt, offset, cpu,
                                ppc_get_compat_smt_threads(cpu)));
}

static void spapr_populate_cpus_dt_node(void *fdt, sPAPRMachineState *spapr)
{
    CPUState *cs;
    int cpus_offset;
    char *nodename;
    int smt = kvmppc_smt_threads();

    cpus_offset = fdt_add_subnode(fdt, 0, "cpus");
    _FDT(cpus_offset);
    _FDT((fdt_setprop_cell(fdt, cpus_offset, "#address-cells", 0x1)));
    _FDT((fdt_setprop_cell(fdt, cpus_offset, "#size-cells", 0x0)));

    /*
     * We walk the CPUs in reverse order to ensure that CPU DT nodes
     * created by fdt_add_subnode() end up in the right order in FDT
     * for the guest kernel the enumerate the CPUs correctly.
     */
    CPU_FOREACH_REVERSE(cs) {
        PowerPCCPU *cpu = POWERPC_CPU(cs);
        int index = ppc_get_vcpu_dt_id(cpu);
        DeviceClass *dc = DEVICE_GET_CLASS(cs);
        int offset;

        if ((index % smt) != 0) {
            continue;
        }

        nodename = g_strdup_printf("%s@%x", dc->fw_name, index);
        offset = fdt_add_subnode(fdt, cpus_offset, nodename);
        g_free(nodename);
        _FDT(offset);
        spapr_populate_cpu_dt(cs, fdt, offset, spapr);
    }

}

/*
 * Adds ibm,dynamic-reconfiguration-memory node.
 * Refer to docs/specs/ppc-spapr-hotplug.txt for the documentation
 * of this device tree node.
 */
static int spapr_populate_drconf_memory(sPAPRMachineState *spapr, void *fdt)
{
    MachineState *machine = MACHINE(spapr);
    int ret, i, offset;
    uint64_t lmb_size = SPAPR_MEMORY_BLOCK_SIZE;
    uint32_t prop_lmb_size[] = {0, cpu_to_be32(lmb_size)};
    uint32_t nr_lmbs = (machine->maxram_size - machine->ram_size)/lmb_size;
    uint32_t *int_buf, *cur_index, buf_len;
    int nr_nodes = nb_numa_nodes ? nb_numa_nodes : 1;

    /*
     * Don't create the node if there are no DR LMBs.
     */
    if (!nr_lmbs) {
        return 0;
    }

    /*
     * Allocate enough buffer size to fit in ibm,dynamic-memory
     * or ibm,associativity-lookup-arrays
     */
    buf_len = MAX(nr_lmbs * SPAPR_DR_LMB_LIST_ENTRY_SIZE + 1, nr_nodes * 4 + 2)
              * sizeof(uint32_t);
    cur_index = int_buf = g_malloc0(buf_len);

    offset = fdt_add_subnode(fdt, 0, "ibm,dynamic-reconfiguration-memory");

    ret = fdt_setprop(fdt, offset, "ibm,lmb-size", prop_lmb_size,
                    sizeof(prop_lmb_size));
    if (ret < 0) {
        goto out;
    }

    ret = fdt_setprop_cell(fdt, offset, "ibm,memory-flags-mask", 0xff);
    if (ret < 0) {
        goto out;
    }

    ret = fdt_setprop_cell(fdt, offset, "ibm,memory-preservation-time", 0x0);
    if (ret < 0) {
        goto out;
    }

    /* ibm,dynamic-memory */
    int_buf[0] = cpu_to_be32(nr_lmbs);
    cur_index++;
    for (i = 0; i < nr_lmbs; i++) {
        sPAPRDRConnector *drc;
        sPAPRDRConnectorClass *drck;
        uint64_t addr = i * lmb_size + spapr->hotplug_memory.base;;
        uint32_t *dynamic_memory = cur_index;

        drc = spapr_dr_connector_by_id(SPAPR_DR_CONNECTOR_TYPE_LMB,
                                       addr/lmb_size);
        g_assert(drc);
        drck = SPAPR_DR_CONNECTOR_GET_CLASS(drc);

        dynamic_memory[0] = cpu_to_be32(addr >> 32);
        dynamic_memory[1] = cpu_to_be32(addr & 0xffffffff);
        dynamic_memory[2] = cpu_to_be32(drck->get_index(drc));
        dynamic_memory[3] = cpu_to_be32(0); /* reserved */
        dynamic_memory[4] = cpu_to_be32(numa_get_node(addr, NULL));
        if (addr < machine->ram_size ||
                    memory_region_present(get_system_memory(), addr)) {
            dynamic_memory[5] = cpu_to_be32(SPAPR_LMB_FLAGS_ASSIGNED);
        } else {
            dynamic_memory[5] = cpu_to_be32(0);
        }

        cur_index += SPAPR_DR_LMB_LIST_ENTRY_SIZE;
    }
    ret = fdt_setprop(fdt, offset, "ibm,dynamic-memory", int_buf, buf_len);
    if (ret < 0) {
        goto out;
    }

    /* ibm,associativity-lookup-arrays */
    cur_index = int_buf;
    int_buf[0] = cpu_to_be32(nr_nodes);
    int_buf[1] = cpu_to_be32(4); /* Number of entries per associativity list */
    cur_index += 2;
    for (i = 0; i < nr_nodes; i++) {
        uint32_t associativity[] = {
            cpu_to_be32(0x0),
            cpu_to_be32(0x0),
            cpu_to_be32(0x0),
            cpu_to_be32(i)
        };
        memcpy(cur_index, associativity, sizeof(associativity));
        cur_index += 4;
    }
    ret = fdt_setprop(fdt, offset, "ibm,associativity-lookup-arrays", int_buf,
            (cur_index - int_buf) * sizeof(uint32_t));
out:
    g_free(int_buf);
    return ret;
}

int spapr_h_cas_compose_response(sPAPRMachineState *spapr,
                                 target_ulong addr, target_ulong size,
                                 bool cpu_update, bool memory_update)
{
    void *fdt, *fdt_skel;
    sPAPRDeviceTreeUpdateHeader hdr = { .version_id = 1 };
    sPAPRMachineClass *smc = SPAPR_MACHINE_GET_CLASS(qdev_get_machine());

    size -= sizeof(hdr);

    /* Create sceleton */
    fdt_skel = g_malloc0(size);
    _FDT((fdt_create(fdt_skel, size)));
    _FDT((fdt_begin_node(fdt_skel, "")));
    _FDT((fdt_end_node(fdt_skel)));
    _FDT((fdt_finish(fdt_skel)));
    fdt = g_malloc0(size);
    _FDT((fdt_open_into(fdt_skel, fdt, size)));
    g_free(fdt_skel);

    /* Fixup cpu nodes */
    if (cpu_update) {
        _FDT((spapr_fixup_cpu_dt(fdt, spapr)));
    }

    /* Generate ibm,dynamic-reconfiguration-memory node if required */
    if (memory_update && smc->dr_lmb_enabled) {
        _FDT((spapr_populate_drconf_memory(spapr, fdt)));
    }

    /* Pack resulting tree */
    _FDT((fdt_pack(fdt)));

    if (fdt_totalsize(fdt) + sizeof(hdr) > size) {
        trace_spapr_cas_failed(size);
        return -1;
    }

    cpu_physical_memory_write(addr, &hdr, sizeof(hdr));
    cpu_physical_memory_write(addr + sizeof(hdr), fdt, fdt_totalsize(fdt));
    trace_spapr_cas_continue(fdt_totalsize(fdt) + sizeof(hdr));
    g_free(fdt);

    return 0;
}

static void spapr_finalize_fdt(sPAPRMachineState *spapr,
                               hwaddr fdt_addr,
                               hwaddr rtas_addr,
                               hwaddr rtas_size)
{
    MachineState *machine = MACHINE(qdev_get_machine());
    sPAPRMachineClass *smc = SPAPR_MACHINE_GET_CLASS(machine);
    const char *boot_device = machine->boot_order;
    int ret, i;
    size_t cb = 0;
    char *bootlist;
    void *fdt;
    sPAPRPHBState *phb;

    fdt = g_malloc(FDT_MAX_SIZE);

    /* open out the base tree into a temp buffer for the final tweaks */
    _FDT((fdt_open_into(spapr->fdt_skel, fdt, FDT_MAX_SIZE)));

    ret = spapr_populate_memory(spapr, fdt);
    if (ret < 0) {
        fprintf(stderr, "couldn't setup memory nodes in fdt\n");
        exit(1);
    }

    ret = spapr_populate_vdevice(spapr->vio_bus, fdt);
    if (ret < 0) {
        fprintf(stderr, "couldn't setup vio devices in fdt\n");
        exit(1);
    }

    if (object_resolve_path_type("", TYPE_SPAPR_RNG, NULL)) {
        ret = spapr_rng_populate_dt(fdt);
        if (ret < 0) {
            fprintf(stderr, "could not set up rng device in the fdt\n");
            exit(1);
        }
    }

    QLIST_FOREACH(phb, &spapr->phbs, list) {
        ret = spapr_populate_pci_dt(phb, PHANDLE_XICP, fdt);
        if (ret < 0) {
            error_report("couldn't setup PCI devices in fdt");
            exit(1);
        }
    }

    /* RTAS */
    ret = spapr_rtas_device_tree_setup(fdt, rtas_addr, rtas_size);
    if (ret < 0) {
        fprintf(stderr, "Couldn't set up RTAS device tree properties\n");
    }

    /* cpus */
    spapr_populate_cpus_dt_node(fdt, spapr);

    bootlist = get_boot_devices_list(&cb, true);
    if (cb && bootlist) {
        int offset = fdt_path_offset(fdt, "/chosen");
        if (offset < 0) {
            exit(1);
        }
        for (i = 0; i < cb; i++) {
            if (bootlist[i] == '\n') {
                bootlist[i] = ' ';
            }

        }
        ret = fdt_setprop_string(fdt, offset, "qemu,boot-list", bootlist);
    }

    if (boot_device && strlen(boot_device)) {
        int offset = fdt_path_offset(fdt, "/chosen");

        if (offset < 0) {
            exit(1);
        }
        fdt_setprop_string(fdt, offset, "qemu,boot-device", boot_device);
    }

    if (!spapr->has_graphics) {
        spapr_populate_chosen_stdout(fdt, spapr->vio_bus);
    }

    if (smc->dr_lmb_enabled) {
        _FDT(spapr_drc_populate_dt(fdt, 0, NULL, SPAPR_DR_CONNECTOR_TYPE_LMB));
    }

    _FDT((fdt_pack(fdt)));

    if (fdt_totalsize(fdt) > FDT_MAX_SIZE) {
        error_report("FDT too big ! 0x%x bytes (max is 0x%x)",
                     fdt_totalsize(fdt), FDT_MAX_SIZE);
        exit(1);
    }

    qemu_fdt_dumpdtb(fdt, fdt_totalsize(fdt));
    cpu_physical_memory_write(fdt_addr, fdt, fdt_totalsize(fdt));

    g_free(bootlist);
    g_free(fdt);
}

static uint64_t translate_kernel_address(void *opaque, uint64_t addr)
{
    return (addr & 0x0fffffff) + KERNEL_LOAD_ADDR;
}

static void emulate_spapr_hypercall(PowerPCCPU *cpu)
{
    CPUPPCState *env = &cpu->env;

    if (msr_pr) {
        hcall_dprintf("Hypercall made with MSR[PR]=1\n");
        env->gpr[3] = H_PRIVILEGE;
    } else {
        env->gpr[3] = spapr_hypercall(cpu, env->gpr[3], &env->gpr[4]);
    }
}

#define HPTE(_table, _i)   (void *)(((uint64_t *)(_table)) + ((_i) * 2))
#define HPTE_VALID(_hpte)  (tswap64(*((uint64_t *)(_hpte))) & HPTE64_V_VALID)
#define HPTE_DIRTY(_hpte)  (tswap64(*((uint64_t *)(_hpte))) & HPTE64_V_HPTE_DIRTY)
#define CLEAN_HPTE(_hpte)  ((*(uint64_t *)(_hpte)) &= tswap64(~HPTE64_V_HPTE_DIRTY))
#define DIRTY_HPTE(_hpte)  ((*(uint64_t *)(_hpte)) |= tswap64(HPTE64_V_HPTE_DIRTY))

/*
 * Get the fd to access the kernel htab, re-opening it if necessary
 */
static int get_htab_fd(sPAPRMachineState *spapr)
{
    if (spapr->htab_fd >= 0) {
        return spapr->htab_fd;
    }

    spapr->htab_fd = kvmppc_get_htab_fd(false);
    if (spapr->htab_fd < 0) {
        error_report("Unable to open fd for reading hash table from KVM: %s",
                     strerror(errno));
    }

    return spapr->htab_fd;
}

static void close_htab_fd(sPAPRMachineState *spapr)
{
    if (spapr->htab_fd >= 0) {
        close(spapr->htab_fd);
    }
    spapr->htab_fd = -1;
}

static int spapr_hpt_shift_for_ramsize(uint64_t ramsize)
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

static void spapr_reallocate_hpt(sPAPRMachineState *spapr, int shift,
                                 Error **errp)
{
    long rc;

    /* Clean up any HPT info from a previous boot */
    g_free(spapr->htab);
    spapr->htab = NULL;
    spapr->htab_shift = 0;
    close_htab_fd(spapr);

    rc = kvmppc_reset_htab(shift);
    if (rc < 0) {
        /* kernel-side HPT needed, but couldn't allocate one */
        error_setg_errno(errp, errno,
                         "Failed to allocate KVM HPT of order %d (try smaller maxmem?)",
                         shift);
        /* This is almost certainly fatal, but if the caller really
         * wants to carry on with shift == 0, it's welcome to try */
    } else if (rc > 0) {
        /* kernel-side HPT allocated */
        if (rc != shift) {
            error_setg(errp,
                       "Requested order %d HPT, but kernel allocated order %ld (try smaller maxmem?)",
                       shift, rc);
        }

        spapr->htab_shift = shift;
        spapr->htab = NULL;
    } else {
        /* kernel-side HPT not needed, allocate in userspace instead */
        size_t size = 1ULL << shift;
        int i;

        spapr->htab = qemu_memalign(size, size);
        if (!spapr->htab) {
            error_setg_errno(errp, errno,
                             "Could not allocate HPT of order %d", shift);
            return;
        }

        memset(spapr->htab, 0, size);
        spapr->htab_shift = shift;

        for (i = 0; i < size / HASH_PTE_SIZE_64; i++) {
            DIRTY_HPTE(HPTE(spapr->htab, i));
        }
    }
}

static int find_unknown_sysbus_device(SysBusDevice *sbdev, void *opaque)
{
    bool matched = false;

    if (object_dynamic_cast(OBJECT(sbdev), TYPE_SPAPR_PCI_HOST_BRIDGE)) {
        matched = true;
    }

    if (!matched) {
        error_report("Device %s is not supported by this machine yet.",
                     qdev_fw_name(DEVICE(sbdev)));
        exit(1);
    }

    return 0;
}

static void ppc_spapr_reset(void)
{
    MachineState *machine = MACHINE(qdev_get_machine());
    sPAPRMachineState *spapr = SPAPR_MACHINE(machine);
    PowerPCCPU *first_ppc_cpu;
    uint32_t rtas_limit;

    /* Check for unknown sysbus devices */
    foreach_dynamic_sysbus_device(find_unknown_sysbus_device, NULL);

    /* Allocate and/or reset the hash page table */
    spapr_reallocate_hpt(spapr,
                         spapr_hpt_shift_for_ramsize(machine->maxram_size),
                         &error_fatal);

    /* Update the RMA size if necessary */
    if (spapr->vrma_adjust) {
        spapr->rma_size = kvmppc_rma_size(spapr_node0_size(),
                                          spapr->htab_shift);
    }

    qemu_devices_reset();

    /*
     * We place the device tree and RTAS just below either the top of the RMA,
     * or just below 2GB, whichever is lowere, so that it can be
     * processed with 32-bit real mode code if necessary
     */
    rtas_limit = MIN(spapr->rma_size, RTAS_MAX_ADDR);
    spapr->rtas_addr = rtas_limit - RTAS_MAX_SIZE;
    spapr->fdt_addr = spapr->rtas_addr - FDT_MAX_SIZE;

    /* Load the fdt */
    spapr_finalize_fdt(spapr, spapr->fdt_addr, spapr->rtas_addr,
                       spapr->rtas_size);

    /* Copy RTAS over */
    cpu_physical_memory_write(spapr->rtas_addr, spapr->rtas_blob,
                              spapr->rtas_size);

    /* Set up the entry state */
    first_ppc_cpu = POWERPC_CPU(first_cpu);
    first_ppc_cpu->env.gpr[3] = spapr->fdt_addr;
    first_ppc_cpu->env.gpr[5] = 0;
    first_cpu->halted = 0;
    first_ppc_cpu->env.nip = SPAPR_ENTRY_POINT;

}

static void spapr_cpu_reset(void *opaque)
{
    sPAPRMachineState *spapr = SPAPR_MACHINE(qdev_get_machine());
    PowerPCCPU *cpu = opaque;
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;

    cpu_reset(cs);

    /* All CPUs start halted.  CPU0 is unhalted from the machine level
     * reset code and the rest are explicitly started up by the guest
     * using an RTAS call */
    cs->halted = 1;

    env->spr[SPR_HIOR] = 0;

    ppc_hash64_set_external_hpt(cpu, spapr->htab, spapr->htab_shift,
                                &error_fatal);
}

static void spapr_create_nvram(sPAPRMachineState *spapr)
{
    DeviceState *dev = qdev_create(&spapr->vio_bus->bus, "spapr-nvram");
    DriveInfo *dinfo = drive_get(IF_PFLASH, 0, 0);

    if (dinfo) {
        qdev_prop_set_drive(dev, "drive", blk_by_legacy_dinfo(dinfo),
                            &error_fatal);
    }

    qdev_init_nofail(dev);

    spapr->nvram = (struct sPAPRNVRAM *)dev;
}

static void spapr_rtc_create(sPAPRMachineState *spapr)
{
    DeviceState *dev = qdev_create(NULL, TYPE_SPAPR_RTC);

    qdev_init_nofail(dev);
    spapr->rtc = dev;

    object_property_add_alias(qdev_get_machine(), "rtc-time",
                              OBJECT(spapr->rtc), "date", NULL);
}

/* Returns whether we want to use VGA or not */
static bool spapr_vga_init(PCIBus *pci_bus, Error **errp)
{
    switch (vga_interface_type) {
    case VGA_NONE:
        return false;
    case VGA_DEVICE:
        return true;
    case VGA_STD:
    case VGA_VIRTIO:
        return pci_vga_init(pci_bus) != NULL;
    default:
        error_setg(errp,
                   "Unsupported VGA mode, only -vga std or -vga virtio is supported");
        return false;
    }
}

static int spapr_post_load(void *opaque, int version_id)
{
    sPAPRMachineState *spapr = (sPAPRMachineState *)opaque;
    int err = 0;

    /* In earlier versions, there was no separate qdev for the PAPR
     * RTC, so the RTC offset was stored directly in sPAPREnvironment.
     * So when migrating from those versions, poke the incoming offset
     * value into the RTC device */
    if (version_id < 3) {
        err = spapr_rtc_import_offset(spapr->rtc, spapr->rtc_offset);
    }

    return err;
}

static bool version_before_3(void *opaque, int version_id)
{
    return version_id < 3;
}

static const VMStateDescription vmstate_spapr = {
    .name = "spapr",
    .version_id = 3,
    .minimum_version_id = 1,
    .post_load = spapr_post_load,
    .fields = (VMStateField[]) {
        /* used to be @next_irq */
        VMSTATE_UNUSED_BUFFER(version_before_3, 0, 4),

        /* RTC offset */
        VMSTATE_UINT64_TEST(rtc_offset, sPAPRMachineState, version_before_3),

        VMSTATE_PPC_TIMEBASE_V(tb, sPAPRMachineState, 2),
        VMSTATE_END_OF_LIST()
    },
};

static int htab_save_setup(QEMUFile *f, void *opaque)
{
    sPAPRMachineState *spapr = opaque;

    /* "Iteration" header */
    qemu_put_be32(f, spapr->htab_shift);

    if (spapr->htab) {
        spapr->htab_save_index = 0;
        spapr->htab_first_pass = true;
    } else {
        assert(kvm_enabled());
    }


    return 0;
}

static void htab_save_first_pass(QEMUFile *f, sPAPRMachineState *spapr,
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
            index++;
            CLEAN_HPTE(HPTE(spapr->htab, index));
        }

        /* Consume valid HPTEs */
        chunkstart = index;
        while ((index < htabslots) && (index - chunkstart < USHRT_MAX)
               && HPTE_VALID(HPTE(spapr->htab, index))) {
            index++;
            CLEAN_HPTE(HPTE(spapr->htab, index));
        }

        if (index > chunkstart) {
            int n_valid = index - chunkstart;

            qemu_put_be32(f, chunkstart);
            qemu_put_be16(f, n_valid);
            qemu_put_be16(f, 0);
            qemu_put_buffer(f, HPTE(spapr->htab, chunkstart),
                            HASH_PTE_SIZE_64 * n_valid);

            if (has_timeout &&
                (qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - starttime) > max_ns) {
                break;
            }
        }
    } while ((index < htabslots) && !qemu_file_rate_limit(f));

    if (index >= htabslots) {
        assert(index == htabslots);
        index = 0;
        spapr->htab_first_pass = false;
    }
    spapr->htab_save_index = index;
}

static int htab_save_later_pass(QEMUFile *f, sPAPRMachineState *spapr,
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

            qemu_put_be32(f, chunkstart);
            qemu_put_be16(f, n_valid);
            qemu_put_be16(f, n_invalid);
            qemu_put_buffer(f, HPTE(spapr->htab, chunkstart),
                            HASH_PTE_SIZE_64 * n_valid);
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
    } while ((examined < htabslots) && (!qemu_file_rate_limit(f) || final));

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
    sPAPRMachineState *spapr = opaque;
    int fd;
    int rc = 0;

    /* Iteration header */
    qemu_put_be32(f, 0);

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

    /* End marker */
    qemu_put_be32(f, 0);
    qemu_put_be16(f, 0);
    qemu_put_be16(f, 0);

    return rc;
}

static int htab_save_complete(QEMUFile *f, void *opaque)
{
    sPAPRMachineState *spapr = opaque;
    int fd;

    /* Iteration header */
    qemu_put_be32(f, 0);

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
        close_htab_fd(spapr);
    } else {
        if (spapr->htab_first_pass) {
            htab_save_first_pass(f, spapr, -1);
        }
        htab_save_later_pass(f, spapr, -1);
    }

    /* End marker */
    qemu_put_be32(f, 0);
    qemu_put_be16(f, 0);
    qemu_put_be16(f, 0);

    return 0;
}

static int htab_load(QEMUFile *f, void *opaque, int version_id)
{
    sPAPRMachineState *spapr = opaque;
    uint32_t section_hdr;
    int fd = -1;

    if (version_id < 1 || version_id > 1) {
        error_report("htab_load() bad version");
        return -EINVAL;
    }

    section_hdr = qemu_get_be32(f);

    if (section_hdr) {
        Error *local_err = NULL;

        /* First section gives the htab size */
        spapr_reallocate_hpt(spapr, section_hdr, &local_err);
        if (local_err) {
            error_report_err(local_err);
            return -EINVAL;
        }
        return 0;
    }

    if (!spapr->htab) {
        assert(kvm_enabled());

        fd = kvmppc_get_htab_fd(true);
        if (fd < 0) {
            error_report("Unable to open fd to restore KVM hash table: %s",
                         strerror(errno));
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

            rc = kvmppc_load_htab_chunk(f, fd, index, n_valid, n_invalid);
            if (rc < 0) {
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

static SaveVMHandlers savevm_htab_handlers = {
    .save_live_setup = htab_save_setup,
    .save_live_iterate = htab_save_iterate,
    .save_live_complete_precopy = htab_save_complete,
    .load_state = htab_load,
};

static void spapr_boot_set(void *opaque, const char *boot_device,
                           Error **errp)
{
    MachineState *machine = MACHINE(qdev_get_machine());
    machine->boot_order = g_strdup(boot_device);
}

static void spapr_cpu_init(sPAPRMachineState *spapr, PowerPCCPU *cpu,
                           Error **errp)
{
    CPUPPCState *env = &cpu->env;

    /* Set time-base frequency to 512 MHz */
    cpu_ppc_tb_init(env, TIMEBASE_FREQ);

    /* Enable PAPR mode in TCG or KVM */
    cpu_ppc_set_papr(cpu);

    if (cpu->max_compat) {
        Error *local_err = NULL;

        ppc_set_compat(cpu, cpu->max_compat, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    }

    xics_cpu_setup(spapr->icp, cpu);

    qemu_register_reset(spapr_cpu_reset, cpu);
}

/*
 * Reset routine for LMB DR devices.
 *
 * Unlike PCI DR devices, LMB DR devices explicitly register this reset
 * routine. Reset for PCI DR devices will be handled by PHB reset routine
 * when it walks all its children devices. LMB devices reset occurs
 * as part of spapr_ppc_reset().
 */
static void spapr_drc_reset(void *opaque)
{
    sPAPRDRConnector *drc = opaque;
    DeviceState *d = DEVICE(drc);

    if (d) {
        device_reset(d);
    }
}

static void spapr_create_lmb_dr_connectors(sPAPRMachineState *spapr)
{
    MachineState *machine = MACHINE(spapr);
    uint64_t lmb_size = SPAPR_MEMORY_BLOCK_SIZE;
    uint32_t nr_lmbs = (machine->maxram_size - machine->ram_size)/lmb_size;
    int i;

    for (i = 0; i < nr_lmbs; i++) {
        sPAPRDRConnector *drc;
        uint64_t addr;

        addr = i * lmb_size + spapr->hotplug_memory.base;
        drc = spapr_dr_connector_new(OBJECT(spapr), SPAPR_DR_CONNECTOR_TYPE_LMB,
                                     addr/lmb_size);
        qemu_register_reset(spapr_drc_reset, drc);
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
                   " is not aligned to %llu MiB",
                   machine->ram_size,
                   SPAPR_MEMORY_BLOCK_SIZE / M_BYTE);
        return;
    }

    if (machine->maxram_size % SPAPR_MEMORY_BLOCK_SIZE) {
        error_setg(errp, "Maximum memory size 0x" RAM_ADDR_FMT
                   " is not aligned to %llu MiB",
                   machine->ram_size,
                   SPAPR_MEMORY_BLOCK_SIZE / M_BYTE);
        return;
    }

    for (i = 0; i < nb_numa_nodes; i++) {
        if (numa_info[i].node_mem % SPAPR_MEMORY_BLOCK_SIZE) {
            error_setg(errp,
                       "Node %d memory size 0x%" PRIx64
                       " is not aligned to %llu MiB",
                       i, numa_info[i].node_mem,
                       SPAPR_MEMORY_BLOCK_SIZE / M_BYTE);
            return;
        }
    }
}

/* pSeries LPAR / sPAPR hardware init */
static void ppc_spapr_init(MachineState *machine)
{
    sPAPRMachineState *spapr = SPAPR_MACHINE(machine);
    sPAPRMachineClass *smc = SPAPR_MACHINE_GET_CLASS(machine);
    const char *kernel_filename = machine->kernel_filename;
    const char *kernel_cmdline = machine->kernel_cmdline;
    const char *initrd_filename = machine->initrd_filename;
    PowerPCCPU *cpu;
    PCIHostState *phb;
    int i;
    MemoryRegion *sysmem = get_system_memory();
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    MemoryRegion *rma_region;
    void *rma = NULL;
    hwaddr rma_alloc_size;
    hwaddr node0_size = spapr_node0_size();
    uint32_t initrd_base = 0;
    long kernel_size = 0, initrd_size = 0;
    long load_limit, fw_size;
    bool kernel_le = false;
    char *filename;

    msi_nonbroken = true;

    QLIST_INIT(&spapr->phbs);

    cpu_ppc_hypercall = emulate_spapr_hypercall;

    /* Allocate RMA if necessary */
    rma_alloc_size = kvmppc_alloc_rma(&rma);

    if (rma_alloc_size == -1) {
        error_report("Unable to create RMA");
        exit(1);
    }

    if (rma_alloc_size && (rma_alloc_size < node0_size)) {
        spapr->rma_size = rma_alloc_size;
    } else {
        spapr->rma_size = node0_size;

        /* With KVM, we don't actually know whether KVM supports an
         * unbounded RMA (PR KVM) or is limited by the hash table size
         * (HV KVM using VRMA), so we always assume the latter
         *
         * In that case, we also limit the initial allocations for RTAS
         * etc... to 256M since we have no way to know what the VRMA size
         * is going to be as it depends on the size of the hash table
         * isn't determined yet.
         */
        if (kvm_enabled()) {
            spapr->vrma_adjust = 1;
            spapr->rma_size = MIN(spapr->rma_size, 0x10000000);
        }
    }

    if (spapr->rma_size > node0_size) {
        error_report("Numa node 0 has to span the RMA (%#08"HWADDR_PRIx")",
                     spapr->rma_size);
        exit(1);
    }

    /* Setup a load limit for the ramdisk leaving room for SLOF and FDT */
    load_limit = MIN(spapr->rma_size, RTAS_MAX_ADDR) - FW_OVERHEAD;

    /* Set up Interrupt Controller before we create the VCPUs */
    spapr->icp = xics_system_init(machine,
                                  DIV_ROUND_UP(max_cpus * kvmppc_smt_threads(),
                                               smp_threads),
                                  XICS_IRQS, &error_fatal);

    if (smc->dr_lmb_enabled) {
        spapr_validate_node_memory(machine, &error_fatal);
    }

    /* init CPUs */
    if (machine->cpu_model == NULL) {
        machine->cpu_model = kvm_enabled() ? "host" : "POWER7";
    }
    for (i = 0; i < smp_cpus; i++) {
        cpu = cpu_ppc_init(machine->cpu_model);
        if (cpu == NULL) {
            error_report("Unable to find PowerPC CPU definition");
            exit(1);
        }
        spapr_cpu_init(spapr, cpu, &error_fatal);
    }

    if (kvm_enabled()) {
        /* Enable H_LOGICAL_CI_* so SLOF can talk to in-kernel devices */
        kvmppc_enable_logical_ci_hcalls();
        kvmppc_enable_set_mode_hcall();
    }

    /* allocate RAM */
    memory_region_allocate_system_memory(ram, NULL, "ppc_spapr.ram",
                                         machine->ram_size);
    memory_region_add_subregion(sysmem, 0, ram);

    if (rma_alloc_size && rma) {
        rma_region = g_new(MemoryRegion, 1);
        memory_region_init_ram_ptr(rma_region, NULL, "ppc_spapr.rma",
                                   rma_alloc_size, rma);
        vmstate_register_ram_global(rma_region);
        memory_region_add_subregion(sysmem, 0, rma_region);
    }

    /* initialize hotplug memory address space */
    if (machine->ram_size < machine->maxram_size) {
        ram_addr_t hotplug_mem_size = machine->maxram_size - machine->ram_size;

        if (machine->ram_slots > SPAPR_MAX_RAM_SLOTS) {
            error_report("Specified number of memory slots %"
                         PRIu64" exceeds max supported %d",
                         machine->ram_slots, SPAPR_MAX_RAM_SLOTS);
            exit(1);
        }

        spapr->hotplug_memory.base = ROUND_UP(machine->ram_size,
                                              SPAPR_HOTPLUG_MEM_ALIGN);
        memory_region_init(&spapr->hotplug_memory.mr, OBJECT(spapr),
                           "hotplug-memory", hotplug_mem_size);
        memory_region_add_subregion(sysmem, spapr->hotplug_memory.base,
                                    &spapr->hotplug_memory.mr);
    }

    if (smc->dr_lmb_enabled) {
        spapr_create_lmb_dr_connectors(spapr);
    }

    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, "spapr-rtas.bin");
    if (!filename) {
        error_report("Could not find LPAR rtas '%s'", "spapr-rtas.bin");
        exit(1);
    }
    spapr->rtas_size = get_image_size(filename);
    spapr->rtas_blob = g_malloc(spapr->rtas_size);
    if (load_image_size(filename, spapr->rtas_blob, spapr->rtas_size) < 0) {
        error_report("Could not load LPAR rtas '%s'", filename);
        exit(1);
    }
    if (spapr->rtas_size > RTAS_MAX_SIZE) {
        error_report("RTAS too big ! 0x%zx bytes (max is 0x%x)",
                     (size_t)spapr->rtas_size, RTAS_MAX_SIZE);
        exit(1);
    }
    g_free(filename);

    /* Set up EPOW events infrastructure */
    spapr_events_init(spapr);

    /* Set up the RTC RTAS interfaces */
    spapr_rtc_create(spapr);

    /* Set up VIO bus */
    spapr->vio_bus = spapr_vio_bus_init();

    for (i = 0; i < MAX_SERIAL_PORTS; i++) {
        if (serial_hds[i]) {
            spapr_vty_create(spapr->vio_bus, serial_hds[i]);
        }
    }

    /* We always have at least the nvram device on VIO */
    spapr_create_nvram(spapr);

    /* Set up PCI */
    spapr_pci_rtas_init();

    phb = spapr_create_phb(spapr, 0);

    for (i = 0; i < nb_nics; i++) {
        NICInfo *nd = &nd_table[i];

        if (!nd->model) {
            nd->model = g_strdup("ibmveth");
        }

        if (strcmp(nd->model, "ibmveth") == 0) {
            spapr_vlan_create(spapr->vio_bus, nd);
        } else {
            pci_nic_init_nofail(&nd_table[i], phb->bus, nd->model, NULL);
        }
    }

    for (i = 0; i <= drive_get_max_bus(IF_SCSI); i++) {
        spapr_vscsi_create(spapr->vio_bus);
    }

    /* Graphics */
    if (spapr_vga_init(phb->bus, &error_fatal)) {
        spapr->has_graphics = true;
        machine->usb |= defaults_enabled() && !machine->usb_disabled;
    }

    if (machine->usb) {
        if (smc->use_ohci_by_default) {
            pci_create_simple(phb->bus, -1, "pci-ohci");
        } else {
            pci_create_simple(phb->bus, -1, "nec-usb-xhci");
        }

        if (spapr->has_graphics) {
            USBBus *usb_bus = usb_bus_find(-1);

            usb_create_simple(usb_bus, "usb-kbd");
            usb_create_simple(usb_bus, "usb-mouse");
        }
    }

    if (spapr->rma_size < (MIN_RMA_SLOF << 20)) {
        error_report(
            "pSeries SLOF firmware requires >= %ldM guest RMA (Real Mode Area memory)",
            MIN_RMA_SLOF);
        exit(1);
    }

    if (kernel_filename) {
        uint64_t lowaddr = 0;

        kernel_size = load_elf(kernel_filename, translate_kernel_address, NULL,
                               NULL, &lowaddr, NULL, 1, PPC_ELF_MACHINE,
                               0, 0);
        if (kernel_size == ELF_LOAD_WRONG_ENDIAN) {
            kernel_size = load_elf(kernel_filename,
                                   translate_kernel_address, NULL,
                                   NULL, &lowaddr, NULL, 0, PPC_ELF_MACHINE,
                                   0, 0);
            kernel_le = kernel_size > 0;
        }
        if (kernel_size < 0) {
            error_report("error loading %s: %s",
                         kernel_filename, load_elf_strerror(kernel_size));
            exit(1);
        }

        /* load initrd */
        if (initrd_filename) {
            /* Try to locate the initrd in the gap between the kernel
             * and the firmware. Add a bit of space just in case
             */
            initrd_base = (KERNEL_LOAD_ADDR + kernel_size + 0x1ffff) & ~0xffff;
            initrd_size = load_image_targphys(initrd_filename, initrd_base,
                                              load_limit - initrd_base);
            if (initrd_size < 0) {
                error_report("could not load initial ram disk '%s'",
                             initrd_filename);
                exit(1);
            }
        } else {
            initrd_base = 0;
            initrd_size = 0;
        }
    }

    if (bios_name == NULL) {
        bios_name = FW_FILE_NAME;
    }
    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
    if (!filename) {
        error_report("Could not find LPAR firmware '%s'", bios_name);
        exit(1);
    }
    fw_size = load_image_targphys(filename, 0, FW_MAX_SIZE);
    if (fw_size <= 0) {
        error_report("Could not load LPAR firmware '%s'", filename);
        exit(1);
    }
    g_free(filename);

    /* FIXME: Should register things through the MachineState's qdev
     * interface, this is a legacy from the sPAPREnvironment structure
     * which predated MachineState but had a similar function */
    vmstate_register(NULL, 0, &vmstate_spapr, spapr);
    register_savevm_live(NULL, "spapr/htab", -1, 1,
                         &savevm_htab_handlers, spapr);

    /* Prepare the device tree */
    spapr->fdt_skel = spapr_create_fdt_skel(initrd_base, initrd_size,
                                            kernel_size, kernel_le,
                                            kernel_cmdline,
                                            spapr->check_exception_irq);
    assert(spapr->fdt_skel != NULL);

    /* used by RTAS */
    QTAILQ_INIT(&spapr->ccs_list);
    qemu_register_reset(spapr_ccs_reset_hook, spapr);

    qemu_register_boot_set(spapr_boot_set, spapr);
}

static int spapr_kvm_type(const char *vm_type)
{
    if (!vm_type) {
        return 0;
    }

    if (!strcmp(vm_type, "HV")) {
        return 1;
    }

    if (!strcmp(vm_type, "PR")) {
        return 2;
    }

    error_report("Unknown kvm-type specified '%s'", vm_type);
    exit(1);
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
    sPAPRPHBState *phb = CAST(sPAPRPHBState, dev, TYPE_SPAPR_PCI_HOST_BRIDGE);

    if (d) {
        void *spapr = CAST(void, bus->parent, "spapr-vscsi");
        VirtIOSCSI *virtio = CAST(VirtIOSCSI, bus->parent, TYPE_VIRTIO_SCSI);
        USBDevice *usb = CAST(USBDevice, bus->parent, TYPE_USB_DEVICE);

        if (spapr) {
            /*
             * Replace "channel@0/disk@0,0" with "disk@8000000000000000":
             * We use SRP luns of the form 8000 | (bus << 8) | (id << 5) | lun
             * in the top 16 bits of the 64-bit LUN
             */
            unsigned id = 0x8000 | (d->id << 8) | d->lun;
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

    if (phb) {
        /* Replace "pci" with "pci@800000020000000" */
        return g_strdup_printf("pci@%"PRIX64, phb->buid);
    }

    return NULL;
}

static char *spapr_get_kvm_type(Object *obj, Error **errp)
{
    sPAPRMachineState *spapr = SPAPR_MACHINE(obj);

    return g_strdup(spapr->kvm_type);
}

static void spapr_set_kvm_type(Object *obj, const char *value, Error **errp)
{
    sPAPRMachineState *spapr = SPAPR_MACHINE(obj);

    g_free(spapr->kvm_type);
    spapr->kvm_type = g_strdup(value);
}

static void spapr_machine_initfn(Object *obj)
{
    sPAPRMachineState *spapr = SPAPR_MACHINE(obj);

    spapr->htab_fd = -1;
    object_property_add_str(obj, "kvm-type",
                            spapr_get_kvm_type, spapr_set_kvm_type, NULL);
    object_property_set_description(obj, "kvm-type",
                                    "Specifies the KVM virtualization mode (HV, PR)",
                                    NULL);
}

static void spapr_machine_finalizefn(Object *obj)
{
    sPAPRMachineState *spapr = SPAPR_MACHINE(obj);

    g_free(spapr->kvm_type);
}

static void ppc_cpu_do_nmi_on_cpu(void *arg)
{
    CPUState *cs = arg;

    cpu_synchronize_state(cs);
    ppc_cpu_do_system_reset(cs);
}

static void spapr_nmi(NMIState *n, int cpu_index, Error **errp)
{
    CPUState *cs;

    CPU_FOREACH(cs) {
        async_run_on_cpu(cs, ppc_cpu_do_nmi_on_cpu, cs);
    }
}

static void spapr_add_lmbs(DeviceState *dev, uint64_t addr, uint64_t size,
                           uint32_t node, Error **errp)
{
    sPAPRDRConnector *drc;
    sPAPRDRConnectorClass *drck;
    uint32_t nr_lmbs = size/SPAPR_MEMORY_BLOCK_SIZE;
    int i, fdt_offset, fdt_size;
    void *fdt;

    /*
     * Check for DRC connectors and send hotplug notification to the
     * guest only in case of hotplugged memory. This allows cold plugged
     * memory to be specified at boot time.
     */
    if (!dev->hotplugged) {
        return;
    }

    for (i = 0; i < nr_lmbs; i++) {
        drc = spapr_dr_connector_by_id(SPAPR_DR_CONNECTOR_TYPE_LMB,
                addr/SPAPR_MEMORY_BLOCK_SIZE);
        g_assert(drc);

        fdt = create_device_tree(&fdt_size);
        fdt_offset = spapr_populate_memory_node(fdt, node, addr,
                                                SPAPR_MEMORY_BLOCK_SIZE);

        drck = SPAPR_DR_CONNECTOR_GET_CLASS(drc);
        drck->attach(drc, dev, fdt, fdt_offset, !dev->hotplugged, errp);
        addr += SPAPR_MEMORY_BLOCK_SIZE;
    }
    spapr_hotplug_req_add_by_count(SPAPR_DR_CONNECTOR_TYPE_LMB, nr_lmbs);
}

static void spapr_memory_plug(HotplugHandler *hotplug_dev, DeviceState *dev,
                              uint32_t node, Error **errp)
{
    Error *local_err = NULL;
    sPAPRMachineState *ms = SPAPR_MACHINE(hotplug_dev);
    PCDIMMDevice *dimm = PC_DIMM(dev);
    PCDIMMDeviceClass *ddc = PC_DIMM_GET_CLASS(dimm);
    MemoryRegion *mr = ddc->get_memory_region(dimm);
    uint64_t align = memory_region_get_alignment(mr);
    uint64_t size = memory_region_size(mr);
    uint64_t addr;

    if (size % SPAPR_MEMORY_BLOCK_SIZE) {
        error_setg(&local_err, "Hotplugged memory size must be a multiple of "
                      "%lld MB", SPAPR_MEMORY_BLOCK_SIZE/M_BYTE);
        goto out;
    }

    pc_dimm_memory_plug(dev, &ms->hotplug_memory, mr, align, &local_err);
    if (local_err) {
        goto out;
    }

    addr = object_property_get_int(OBJECT(dimm), PC_DIMM_ADDR_PROP, &local_err);
    if (local_err) {
        pc_dimm_memory_unplug(dev, &ms->hotplug_memory, mr);
        goto out;
    }

    spapr_add_lmbs(dev, addr, size, node, &error_abort);

out:
    error_propagate(errp, local_err);
}

static void spapr_machine_device_plug(HotplugHandler *hotplug_dev,
                                      DeviceState *dev, Error **errp)
{
    sPAPRMachineClass *smc = SPAPR_MACHINE_GET_CLASS(qdev_get_machine());

    if (object_dynamic_cast(OBJECT(dev), TYPE_PC_DIMM)) {
        int node;

        if (!smc->dr_lmb_enabled) {
            error_setg(errp, "Memory hotplug not supported for this machine");
            return;
        }
        node = object_property_get_int(OBJECT(dev), PC_DIMM_NODE_PROP, errp);
        if (*errp) {
            return;
        }
        if (node < 0 || node >= MAX_NODES) {
            error_setg(errp, "Invaild node %d", node);
            return;
        }

        /*
         * Currently PowerPC kernel doesn't allow hot-adding memory to
         * memory-less node, but instead will silently add the memory
         * to the first node that has some memory. This causes two
         * unexpected behaviours for the user.
         *
         * - Memory gets hotplugged to a different node than what the user
         *   specified.
         * - Since pc-dimm subsystem in QEMU still thinks that memory belongs
         *   to memory-less node, a reboot will set things accordingly
         *   and the previously hotplugged memory now ends in the right node.
         *   This appears as if some memory moved from one node to another.
         *
         * So until kernel starts supporting memory hotplug to memory-less
         * nodes, just prevent such attempts upfront in QEMU.
         */
        if (nb_numa_nodes && !numa_info[node].node_mem) {
            error_setg(errp, "Can't hotplug memory to memory-less node %d",
                       node);
            return;
        }

        spapr_memory_plug(hotplug_dev, dev, node, errp);
    }
}

static void spapr_machine_device_unplug(HotplugHandler *hotplug_dev,
                                      DeviceState *dev, Error **errp)
{
    if (object_dynamic_cast(OBJECT(dev), TYPE_PC_DIMM)) {
        error_setg(errp, "Memory hot unplug not supported by sPAPR");
    }
}

static HotplugHandler *spapr_get_hotpug_handler(MachineState *machine,
                                             DeviceState *dev)
{
    if (object_dynamic_cast(OBJECT(dev), TYPE_PC_DIMM)) {
        return HOTPLUG_HANDLER(machine);
    }
    return NULL;
}

static unsigned spapr_cpu_index_to_socket_id(unsigned cpu_index)
{
    /* Allocate to NUMA nodes on a "socket" basis (not that concept of
     * socket means much for the paravirtualized PAPR platform) */
    return cpu_index / smp_threads / smp_cores;
}

static void spapr_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    sPAPRMachineClass *smc = SPAPR_MACHINE_CLASS(oc);
    FWPathProviderClass *fwc = FW_PATH_PROVIDER_CLASS(oc);
    NMIClass *nc = NMI_CLASS(oc);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(oc);

    mc->desc = "pSeries Logical Partition (PAPR compliant)";

    /*
     * We set up the default / latest behaviour here.  The class_init
     * functions for the specific versioned machine types can override
     * these details for backwards compatibility
     */
    mc->init = ppc_spapr_init;
    mc->reset = ppc_spapr_reset;
    mc->block_default_type = IF_SCSI;
    mc->max_cpus = MAX_CPUMASK_BITS;
    mc->no_parallel = 1;
    mc->default_boot_order = "";
    mc->default_ram_size = 512 * M_BYTE;
    mc->kvm_type = spapr_kvm_type;
    mc->has_dynamic_sysbus = true;
    mc->pci_allow_0_address = true;
    mc->get_hotplug_handler = spapr_get_hotpug_handler;
    hc->plug = spapr_machine_device_plug;
    hc->unplug = spapr_machine_device_unplug;
    mc->cpu_index_to_socket_id = spapr_cpu_index_to_socket_id;

    smc->dr_lmb_enabled = true;
    fwc->get_dev_path = spapr_get_fw_dev_path;
    nc->nmi_monitor_handler = spapr_nmi;
}

static const TypeInfo spapr_machine_info = {
    .name          = TYPE_SPAPR_MACHINE,
    .parent        = TYPE_MACHINE,
    .abstract      = true,
    .instance_size = sizeof(sPAPRMachineState),
    .instance_init = spapr_machine_initfn,
    .instance_finalize = spapr_machine_finalizefn,
    .class_size    = sizeof(sPAPRMachineClass),
    .class_init    = spapr_machine_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_FW_PATH_PROVIDER },
        { TYPE_NMI },
        { TYPE_HOTPLUG_HANDLER },
        { }
    },
};

#define DEFINE_SPAPR_MACHINE(suffix, verstr, latest)                 \
    static void spapr_machine_##suffix##_class_init(ObjectClass *oc, \
                                                    void *data)      \
    {                                                                \
        MachineClass *mc = MACHINE_CLASS(oc);                        \
        spapr_machine_##suffix##_class_options(mc);                  \
        if (latest) {                                                \
            mc->alias = "pseries";                                   \
            mc->is_default = 1;                                      \
        }                                                            \
    }                                                                \
    static void spapr_machine_##suffix##_instance_init(Object *obj)  \
    {                                                                \
        MachineState *machine = MACHINE(obj);                        \
        spapr_machine_##suffix##_instance_options(machine);          \
    }                                                                \
    static const TypeInfo spapr_machine_##suffix##_info = {          \
        .name = MACHINE_TYPE_NAME("pseries-" verstr),                \
        .parent = TYPE_SPAPR_MACHINE,                                \
        .class_init = spapr_machine_##suffix##_class_init,           \
        .instance_init = spapr_machine_##suffix##_instance_init,     \
    };                                                               \
    static void spapr_machine_register_##suffix(void)                \
    {                                                                \
        type_register(&spapr_machine_##suffix##_info);               \
    }                                                                \
    type_init(spapr_machine_register_##suffix)

/*
 * pseries-2.6
 */
static void spapr_machine_2_6_instance_options(MachineState *machine)
{
}

static void spapr_machine_2_6_class_options(MachineClass *mc)
{
    /* Defaults for the latest behaviour inherited from the base class */
}

DEFINE_SPAPR_MACHINE(2_6, "2.6", true);

/*
 * pseries-2.5
 */
#define SPAPR_COMPAT_2_5 \
    HW_COMPAT_2_5 \
    { \
        .driver   = "spapr-vlan", \
        .property = "use-rx-buffer-pools", \
        .value    = "off", \
    },

static void spapr_machine_2_5_instance_options(MachineState *machine)
{
}

static void spapr_machine_2_5_class_options(MachineClass *mc)
{
    sPAPRMachineClass *smc = SPAPR_MACHINE_CLASS(mc);

    spapr_machine_2_6_class_options(mc);
    smc->use_ohci_by_default = true;
    SET_MACHINE_COMPAT(mc, SPAPR_COMPAT_2_5);
}

DEFINE_SPAPR_MACHINE(2_5, "2.5", false);

/*
 * pseries-2.4
 */
#define SPAPR_COMPAT_2_4 \
        SPAPR_COMPAT_2_5 \
        HW_COMPAT_2_4

static void spapr_machine_2_4_instance_options(MachineState *machine)
{
    spapr_machine_2_5_instance_options(machine);
}

static void spapr_machine_2_4_class_options(MachineClass *mc)
{
    sPAPRMachineClass *smc = SPAPR_MACHINE_CLASS(mc);

    spapr_machine_2_5_class_options(mc);
    smc->dr_lmb_enabled = false;
    SET_MACHINE_COMPAT(mc, SPAPR_COMPAT_2_4);
}

DEFINE_SPAPR_MACHINE(2_4, "2.4", false);

/*
 * pseries-2.3
 */
#define SPAPR_COMPAT_2_3 \
        SPAPR_COMPAT_2_4 \
        HW_COMPAT_2_3 \
        {\
            .driver   = "spapr-pci-host-bridge",\
            .property = "dynamic-reconfiguration",\
            .value    = "off",\
        },

static void spapr_machine_2_3_instance_options(MachineState *machine)
{
    spapr_machine_2_4_instance_options(machine);
    savevm_skip_section_footers();
    global_state_set_optional();
    savevm_skip_configuration();
}

static void spapr_machine_2_3_class_options(MachineClass *mc)
{
    spapr_machine_2_4_class_options(mc);
    SET_MACHINE_COMPAT(mc, SPAPR_COMPAT_2_3);
}
DEFINE_SPAPR_MACHINE(2_3, "2.3", false);

/*
 * pseries-2.2
 */

#define SPAPR_COMPAT_2_2 \
        SPAPR_COMPAT_2_3 \
        HW_COMPAT_2_2 \
        {\
            .driver   = TYPE_SPAPR_PCI_HOST_BRIDGE,\
            .property = "mem_win_size",\
            .value    = "0x20000000",\
        },

static void spapr_machine_2_2_instance_options(MachineState *machine)
{
    spapr_machine_2_3_instance_options(machine);
    machine->suppress_vmdesc = true;
}

static void spapr_machine_2_2_class_options(MachineClass *mc)
{
    spapr_machine_2_3_class_options(mc);
    SET_MACHINE_COMPAT(mc, SPAPR_COMPAT_2_2);
}
DEFINE_SPAPR_MACHINE(2_2, "2.2", false);

/*
 * pseries-2.1
 */
#define SPAPR_COMPAT_2_1 \
        SPAPR_COMPAT_2_2 \
        HW_COMPAT_2_1

static void spapr_machine_2_1_instance_options(MachineState *machine)
{
    spapr_machine_2_2_instance_options(machine);
}

static void spapr_machine_2_1_class_options(MachineClass *mc)
{
    spapr_machine_2_2_class_options(mc);
    SET_MACHINE_COMPAT(mc, SPAPR_COMPAT_2_1);
}
DEFINE_SPAPR_MACHINE(2_1, "2.1", false);

static void spapr_machine_register_types(void)
{
    type_register_static(&spapr_machine_info);
}

type_init(spapr_machine_register_types)
