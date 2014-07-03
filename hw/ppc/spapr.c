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
#include "sysemu/sysemu.h"
#include "hw/hw.h"
#include "hw/fw-path-provider.h"
#include "elf.h"
#include "net/net.h"
#include "sysemu/blockdev.h"
#include "sysemu/cpus.h"
#include "sysemu/kvm.h"
#include "kvm_ppc.h"
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
#define FDT_MAX_SIZE            0x40000
#define RTAS_MAX_SIZE           0x10000
#define FW_MAX_SIZE             0x400000
#define FW_FILE_NAME            "slof.bin"
#define FW_OVERHEAD             0x2800000
#define KERNEL_LOAD_ADDR        FW_MAX_SIZE

#define MIN_RMA_SLOF            128UL

#define TIMEBASE_FREQ           512000000ULL

#define MAX_CPUS                256

#define PHANDLE_XICP            0x00001111

#define HTAB_SIZE(spapr)        (1ULL << ((spapr)->htab_shift))

typedef struct sPAPRMachineState sPAPRMachineState;

#define TYPE_SPAPR_MACHINE      "spapr-machine"
#define SPAPR_MACHINE(obj) \
    OBJECT_CHECK(sPAPRMachineState, (obj), TYPE_SPAPR_MACHINE)

/**
 * sPAPRMachineState:
 */
struct sPAPRMachineState {
    /*< private >*/
    MachineState parent_obj;

    /*< public >*/
    char *kvm_type;
};

sPAPREnvironment *spapr;

static XICSState *try_create_xics(const char *type, int nr_servers,
                                  int nr_irqs)
{
    DeviceState *dev;

    dev = qdev_create(NULL, type);
    qdev_prop_set_uint32(dev, "nr_servers", nr_servers);
    qdev_prop_set_uint32(dev, "nr_irqs", nr_irqs);
    if (qdev_init(dev) < 0) {
        return NULL;
    }

    return XICS_COMMON(dev);
}

static XICSState *xics_system_init(int nr_servers, int nr_irqs)
{
    XICSState *icp = NULL;

    if (kvm_enabled()) {
        QemuOpts *machine_opts = qemu_get_machine_opts();
        bool irqchip_allowed = qemu_opt_get_bool(machine_opts,
                                                "kernel_irqchip", true);
        bool irqchip_required = qemu_opt_get_bool(machine_opts,
                                                  "kernel_irqchip", false);
        if (irqchip_allowed) {
            icp = try_create_xics(TYPE_KVM_XICS, nr_servers, nr_irqs);
        }

        if (irqchip_required && !icp) {
            perror("Failed to create in-kernel XICS\n");
            abort();
        }
    }

    if (!icp) {
        icp = try_create_xics(TYPE_XICS, nr_servers, nr_irqs);
    }

    if (!icp) {
        perror("Failed to create XICS\n");
        abort();
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

static int spapr_fixup_cpu_dt(void *fdt, sPAPREnvironment *spapr)
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
        uint32_t associativity[] = {cpu_to_be32(0x5),
                                    cpu_to_be32(0x0),
                                    cpu_to_be32(0x0),
                                    cpu_to_be32(0x0),
                                    cpu_to_be32(cs->numa_node),
                                    cpu_to_be32(index)};

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

        if (nb_numa_nodes > 1) {
            ret = fdt_setprop(fdt, offset, "ibm,associativity", associativity,
                              sizeof(associativity));
            if (ret < 0) {
                return ret;
            }
        }

        ret = fdt_setprop(fdt, offset, "ibm,pft-size",
                          pft_size_prop, sizeof(pft_size_prop));
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
                                   const char *boot_device,
                                   const char *kernel_cmdline,
                                   uint32_t epow_irq)
{
    void *fdt;
    CPUState *cs;
    uint32_t start_prop = cpu_to_be32(initrd_base);
    uint32_t end_prop = cpu_to_be32(initrd_base + initrd_size);
    GString *hypertas = g_string_sized_new(256);
    GString *qemu_hypertas = g_string_sized_new(256);
    uint32_t refpoints[] = {cpu_to_be32(0x4), cpu_to_be32(0x4)};
    uint32_t interrupt_server_ranges_prop[] = {0, cpu_to_be32(smp_cpus)};
    int smt = kvmppc_smt_threads();
    unsigned char vec5[] = {0x0, 0x0, 0x0, 0x0, 0x0, 0x80};
    QemuOpts *opts = qemu_opts_find(qemu_find_opts("smp-opts"), NULL);
    unsigned sockets = opts ? qemu_opt_get_number(opts, "sockets", 0) : 0;
    uint32_t cpus_per_socket = sockets ? (smp_cpus / sockets) : 1;
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

    if (kvm_enabled()) {
        _FDT((fdt_property_string(fdt, "hypervisor", "kvm")));
    }

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
    g_free(buf);

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
    if (boot_device) {
        _FDT((fdt_property_string(fdt, "qemu,boot-device", boot_device)));
    }
    if (boot_menu) {
        _FDT((fdt_property_cell(fdt, "qemu,boot-menu", boot_menu)));
    }
    _FDT((fdt_property_cell(fdt, "qemu,graphic-width", graphic_width)));
    _FDT((fdt_property_cell(fdt, "qemu,graphic-height", graphic_height)));
    _FDT((fdt_property_cell(fdt, "qemu,graphic-depth", graphic_depth)));

    _FDT((fdt_end_node(fdt)));

    /* cpus */
    _FDT((fdt_begin_node(fdt, "cpus")));

    _FDT((fdt_property_cell(fdt, "#address-cells", 0x1)));
    _FDT((fdt_property_cell(fdt, "#size-cells", 0x0)));

    CPU_FOREACH(cs) {
        PowerPCCPU *cpu = POWERPC_CPU(cs);
        CPUPPCState *env = &cpu->env;
        DeviceClass *dc = DEVICE_GET_CLASS(cs);
        PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cs);
        int index = ppc_get_vcpu_dt_id(cpu);
        char *nodename;
        uint32_t segs[] = {cpu_to_be32(28), cpu_to_be32(40),
                           0xffffffff, 0xffffffff};
        uint32_t tbfreq = kvm_enabled() ? kvmppc_get_tbfreq() : TIMEBASE_FREQ;
        uint32_t cpufreq = kvm_enabled() ? kvmppc_get_clockfreq() : 1000000000;
        uint32_t page_sizes_prop[64];
        size_t page_sizes_prop_size;

        if ((index % smt) != 0) {
            continue;
        }

        nodename = g_strdup_printf("%s@%x", dc->fw_name, index);

        _FDT((fdt_begin_node(fdt, nodename)));

        g_free(nodename);

        _FDT((fdt_property_cell(fdt, "reg", index)));
        _FDT((fdt_property_string(fdt, "device_type", "cpu")));

        _FDT((fdt_property_cell(fdt, "cpu-version", env->spr[SPR_PVR])));
        _FDT((fdt_property_cell(fdt, "d-cache-block-size",
                                env->dcache_line_size)));
        _FDT((fdt_property_cell(fdt, "d-cache-line-size",
                                env->dcache_line_size)));
        _FDT((fdt_property_cell(fdt, "i-cache-block-size",
                                env->icache_line_size)));
        _FDT((fdt_property_cell(fdt, "i-cache-line-size",
                                env->icache_line_size)));

        if (pcc->l1_dcache_size) {
            _FDT((fdt_property_cell(fdt, "d-cache-size", pcc->l1_dcache_size)));
        } else {
            fprintf(stderr, "Warning: Unknown L1 dcache size for cpu\n");
        }
        if (pcc->l1_icache_size) {
            _FDT((fdt_property_cell(fdt, "i-cache-size", pcc->l1_icache_size)));
        } else {
            fprintf(stderr, "Warning: Unknown L1 icache size for cpu\n");
        }

        _FDT((fdt_property_cell(fdt, "timebase-frequency", tbfreq)));
        _FDT((fdt_property_cell(fdt, "clock-frequency", cpufreq)));
        _FDT((fdt_property_cell(fdt, "ibm,slb-size", env->slb_nr)));
        _FDT((fdt_property_string(fdt, "status", "okay")));
        _FDT((fdt_property(fdt, "64-bit", NULL, 0)));

        if (env->spr_cb[SPR_PURR].oea_read) {
            _FDT((fdt_property(fdt, "ibm,purr", NULL, 0)));
        }

        if (env->mmu_model & POWERPC_MMU_1TSEG) {
            _FDT((fdt_property(fdt, "ibm,processor-segment-sizes",
                               segs, sizeof(segs))));
        }

        /* Advertise VMX/VSX (vector extensions) if available
         *   0 / no property == no vector extensions
         *   1               == VMX / Altivec available
         *   2               == VSX available */
        if (env->insns_flags & PPC_ALTIVEC) {
            uint32_t vmx = (env->insns_flags2 & PPC2_VSX) ? 2 : 1;

            _FDT((fdt_property_cell(fdt, "ibm,vmx", vmx)));
        }

        /* Advertise DFP (Decimal Floating Point) if available
         *   0 / no property == no DFP
         *   1               == DFP available */
        if (env->insns_flags2 & PPC2_DFP) {
            _FDT((fdt_property_cell(fdt, "ibm,dfp", 1)));
        }

        page_sizes_prop_size = create_page_sizes_prop(env, page_sizes_prop,
                                                      sizeof(page_sizes_prop));
        if (page_sizes_prop_size) {
            _FDT((fdt_property(fdt, "ibm,segment-page-sizes",
                               page_sizes_prop, page_sizes_prop_size)));
        }

        _FDT((fdt_property_cell(fdt, "ibm,chip-id",
                                cs->cpu_index / cpus_per_socket)));

        _FDT((fdt_end_node(fdt)));
    }

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

    /*
     * According to PAPR, rtas ibm,os-term, does not gaurantee a return
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
            kvmppc_get_hypercall(first_cpu->env_ptr, hypercall,
                                 sizeof(hypercall));
            _FDT((fdt_property(fdt, "hcall-instructions", hypercall,
                              sizeof(hypercall))));
        }
        _FDT((fdt_end_node(fdt)));
    }

    _FDT((fdt_end_node(fdt))); /* close root node */
    _FDT((fdt_finish(fdt)));

    return fdt;
}

int spapr_h_cas_compose_response(target_ulong addr, target_ulong size)
{
    void *fdt, *fdt_skel;
    sPAPRDeviceTreeUpdateHeader hdr = { .version_id = 1 };

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

    /* Fix skeleton up */
    _FDT((spapr_fixup_cpu_dt(fdt, spapr)));

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

static void spapr_populate_memory_node(void *fdt, int nodeid, hwaddr start,
                                       hwaddr size)
{
    uint32_t associativity[] = {
        cpu_to_be32(0x4), /* length */
        cpu_to_be32(0x0), cpu_to_be32(0x0),
        cpu_to_be32(nodeid), cpu_to_be32(nodeid)
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
}

static int spapr_populate_memory(sPAPREnvironment *spapr, void *fdt)
{
    uint32_t associativity[] = {cpu_to_be32(0x4), cpu_to_be32(0x0),
                                cpu_to_be32(0x0), cpu_to_be32(0x0),
                                cpu_to_be32(0x0)};
    char mem_name[32];
    hwaddr node0_size, mem_start, node_size;
    uint64_t mem_reg_property[2];
    int i, off;

    /* memory node(s) */
    if (nb_numa_nodes > 1 && numa_info[0].node_mem < ram_size) {
        node0_size = numa_info[0].node_mem;
    } else {
        node0_size = ram_size;
    }

    /* RMA */
    spapr_populate_memory_node(fdt, 0, 0, spapr->rma_size);

    /* RAM: Node 0 */
    if (node0_size > spapr->rma_size) {
        spapr_populate_memory_node(fdt, 0, spapr->rma_size,
                                   node0_size - spapr->rma_size);
    }

    /* RAM: Node 1 and beyond */
    mem_start = node0_size;
    for (i = 1; i < nb_numa_nodes; i++) {
        mem_reg_property[0] = cpu_to_be64(mem_start);
        if (mem_start >= ram_size) {
            node_size = 0;
        } else {
            node_size = numa_info[i].node_mem;
            if (node_size > ram_size - mem_start) {
                node_size = ram_size - mem_start;
            }
        }
        mem_reg_property[1] = cpu_to_be64(node_size);
        associativity[3] = associativity[4] = cpu_to_be32(i);
        sprintf(mem_name, "memory@" TARGET_FMT_lx, mem_start);
        off = fdt_add_subnode(fdt, 0, mem_name);
        _FDT(off);
        _FDT((fdt_setprop_string(fdt, off, "device_type", "memory")));
        _FDT((fdt_setprop(fdt, off, "reg", mem_reg_property,
                          sizeof(mem_reg_property))));
        _FDT((fdt_setprop(fdt, off, "ibm,associativity", associativity,
                          sizeof(associativity))));
        mem_start += node_size;
    }

    return 0;
}

static void spapr_finalize_fdt(sPAPREnvironment *spapr,
                               hwaddr fdt_addr,
                               hwaddr rtas_addr,
                               hwaddr rtas_size)
{
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

    QLIST_FOREACH(phb, &spapr->phbs, list) {
        ret = spapr_populate_pci_dt(phb, PHANDLE_XICP, fdt);
    }

    if (ret < 0) {
        fprintf(stderr, "couldn't setup PCI devices in fdt\n");
        exit(1);
    }

    /* RTAS */
    ret = spapr_rtas_device_tree_setup(fdt, rtas_addr, rtas_size);
    if (ret < 0) {
        fprintf(stderr, "Couldn't set up RTAS device tree properties\n");
    }

    /* Advertise NUMA via ibm,associativity */
    ret = spapr_fixup_cpu_dt(fdt, spapr);
    if (ret < 0) {
        fprintf(stderr, "Couldn't finalize CPU device tree properties\n");
    }

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

    if (!spapr->has_graphics) {
        spapr_populate_chosen_stdout(fdt, spapr->vio_bus);
    }

    _FDT((fdt_pack(fdt)));

    if (fdt_totalsize(fdt) > FDT_MAX_SIZE) {
        hw_error("FDT too big ! 0x%x bytes (max is 0x%x)\n",
                 fdt_totalsize(fdt), FDT_MAX_SIZE);
        exit(1);
    }

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

static void spapr_reset_htab(sPAPREnvironment *spapr)
{
    long shift;

    /* allocate hash page table.  For now we always make this 16mb,
     * later we should probably make it scale to the size of guest
     * RAM */

    shift = kvmppc_reset_htab(spapr->htab_shift);

    if (shift > 0) {
        /* Kernel handles htab, we don't need to allocate one */
        spapr->htab_shift = shift;
        kvmppc_kern_htab = true;
    } else {
        if (!spapr->htab) {
            /* Allocate an htab if we don't yet have one */
            spapr->htab = qemu_memalign(HTAB_SIZE(spapr), HTAB_SIZE(spapr));
        }

        /* And clear it */
        memset(spapr->htab, 0, HTAB_SIZE(spapr));
    }

    /* Update the RMA size if necessary */
    if (spapr->vrma_adjust) {
        hwaddr node0_size = (nb_numa_nodes > 1) ?
            numa_info[0].node_mem : ram_size;
        spapr->rma_size = kvmppc_rma_size(node0_size, spapr->htab_shift);
    }
}

static void ppc_spapr_reset(void)
{
    PowerPCCPU *first_ppc_cpu;

    /* Reset the hash table & recalc the RMA */
    spapr_reset_htab(spapr);

    qemu_devices_reset();

    /* Load the fdt */
    spapr_finalize_fdt(spapr, spapr->fdt_addr, spapr->rtas_addr,
                       spapr->rtas_size);

    /* Set up the entry state */
    first_ppc_cpu = POWERPC_CPU(first_cpu);
    first_ppc_cpu->env.gpr[3] = spapr->fdt_addr;
    first_ppc_cpu->env.gpr[5] = 0;
    first_cpu->halted = 0;
    first_ppc_cpu->env.nip = spapr->entry_point;

}

static void spapr_cpu_reset(void *opaque)
{
    PowerPCCPU *cpu = opaque;
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;

    cpu_reset(cs);

    /* All CPUs start halted.  CPU0 is unhalted from the machine level
     * reset code and the rest are explicitly started up by the guest
     * using an RTAS call */
    cs->halted = 1;

    env->spr[SPR_HIOR] = 0;

    env->external_htab = (uint8_t *)spapr->htab;
    if (kvm_enabled() && !env->external_htab) {
        /*
         * HV KVM, set external_htab to 1 so our ppc_hash64_load_hpte*
         * functions do the right thing.
         */
        env->external_htab = (void *)1;
    }
    env->htab_base = -1;
    /*
     * htab_mask is the mask used to normalize hash value to PTEG index.
     * htab_shift is log2 of hash table size.
     * We have 8 hpte per group, and each hpte is 16 bytes.
     * ie have 128 bytes per hpte entry.
     */
    env->htab_mask = (1ULL << ((spapr)->htab_shift - 7)) - 1;
    env->spr[SPR_SDR1] = (target_ulong)(uintptr_t)spapr->htab |
        (spapr->htab_shift - 18);
}

static void spapr_create_nvram(sPAPREnvironment *spapr)
{
    DeviceState *dev = qdev_create(&spapr->vio_bus->bus, "spapr-nvram");
    DriveInfo *dinfo = drive_get(IF_PFLASH, 0, 0);

    if (dinfo) {
        qdev_prop_set_drive_nofail(dev, "drive", dinfo->bdrv);
    }

    qdev_init_nofail(dev);

    spapr->nvram = (struct sPAPRNVRAM *)dev;
}

/* Returns whether we want to use VGA or not */
static int spapr_vga_init(PCIBus *pci_bus)
{
    switch (vga_interface_type) {
    case VGA_NONE:
        return false;
    case VGA_DEVICE:
        return true;
    case VGA_STD:
        return pci_vga_init(pci_bus) != NULL;
    default:
        fprintf(stderr, "This vga model is not supported,"
                "currently it only supports -vga std\n");
        exit(0);
    }
}

static const VMStateDescription vmstate_spapr = {
    .name = "spapr",
    .version_id = 2,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UNUSED(4), /* used to be @next_irq */

        /* RTC offset */
        VMSTATE_UINT64(rtc_offset, sPAPREnvironment),
        VMSTATE_PPC_TIMEBASE_V(tb, sPAPREnvironment, 2),
        VMSTATE_END_OF_LIST()
    },
};

#define HPTE(_table, _i)   (void *)(((uint64_t *)(_table)) + ((_i) * 2))
#define HPTE_VALID(_hpte)  (tswap64(*((uint64_t *)(_hpte))) & HPTE64_V_VALID)
#define HPTE_DIRTY(_hpte)  (tswap64(*((uint64_t *)(_hpte))) & HPTE64_V_HPTE_DIRTY)
#define CLEAN_HPTE(_hpte)  ((*(uint64_t *)(_hpte)) &= tswap64(~HPTE64_V_HPTE_DIRTY))

static int htab_save_setup(QEMUFile *f, void *opaque)
{
    sPAPREnvironment *spapr = opaque;

    /* "Iteration" header */
    qemu_put_be32(f, spapr->htab_shift);

    if (spapr->htab) {
        spapr->htab_save_index = 0;
        spapr->htab_first_pass = true;
    } else {
        assert(kvm_enabled());

        spapr->htab_fd = kvmppc_get_htab_fd(false);
        if (spapr->htab_fd < 0) {
            fprintf(stderr, "Unable to open fd for reading hash table from KVM: %s\n",
                    strerror(errno));
            return -1;
        }
    }


    return 0;
}

static void htab_save_first_pass(QEMUFile *f, sPAPREnvironment *spapr,
                                 int64_t max_ns)
{
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
        while ((index < htabslots)
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

            if ((qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - starttime) > max_ns) {
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

static int htab_save_later_pass(QEMUFile *f, sPAPREnvironment *spapr,
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
        while ((index < htabslots)
               && HPTE_DIRTY(HPTE(spapr->htab, index))
               && HPTE_VALID(HPTE(spapr->htab, index))) {
            CLEAN_HPTE(HPTE(spapr->htab, index));
            index++;
            examined++;
        }

        invalidstart = index;
        /* Consume invalid dirty HPTEs */
        while ((index < htabslots)
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
    sPAPREnvironment *spapr = opaque;
    int rc = 0;

    /* Iteration header */
    qemu_put_be32(f, 0);

    if (!spapr->htab) {
        assert(kvm_enabled());

        rc = kvmppc_save_htab(f, spapr->htab_fd,
                              MAX_KVM_BUF_SIZE, MAX_ITERATION_NS);
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
    sPAPREnvironment *spapr = opaque;

    /* Iteration header */
    qemu_put_be32(f, 0);

    if (!spapr->htab) {
        int rc;

        assert(kvm_enabled());

        rc = kvmppc_save_htab(f, spapr->htab_fd, MAX_KVM_BUF_SIZE, -1);
        if (rc < 0) {
            return rc;
        }
        close(spapr->htab_fd);
        spapr->htab_fd = -1;
    } else {
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
    sPAPREnvironment *spapr = opaque;
    uint32_t section_hdr;
    int fd = -1;

    if (version_id < 1 || version_id > 1) {
        fprintf(stderr, "htab_load() bad version\n");
        return -EINVAL;
    }

    section_hdr = qemu_get_be32(f);

    if (section_hdr) {
        /* First section, just the hash shift */
        if (spapr->htab_shift != section_hdr) {
            return -EINVAL;
        }
        return 0;
    }

    if (!spapr->htab) {
        assert(kvm_enabled());

        fd = kvmppc_get_htab_fd(true);
        if (fd < 0) {
            fprintf(stderr, "Unable to open fd to restore KVM hash table: %s\n",
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
            fprintf(stderr, "htab_load() bad index %d (%hd+%hd entries) "
                    "in htab stream (htab_shift=%d)\n", index, n_valid, n_invalid,
                    spapr->htab_shift);
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
    .save_live_complete = htab_save_complete,
    .load_state = htab_load,
};

/* pSeries LPAR / sPAPR hardware init */
static void ppc_spapr_init(MachineState *machine)
{
    ram_addr_t ram_size = machine->ram_size;
    const char *cpu_model = machine->cpu_model;
    const char *kernel_filename = machine->kernel_filename;
    const char *kernel_cmdline = machine->kernel_cmdline;
    const char *initrd_filename = machine->initrd_filename;
    const char *boot_device = machine->boot_order;
    PowerPCCPU *cpu;
    CPUPPCState *env;
    PCIHostState *phb;
    int i;
    MemoryRegion *sysmem = get_system_memory();
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    MemoryRegion *rma_region;
    void *rma = NULL;
    hwaddr rma_alloc_size;
    hwaddr node0_size = (nb_numa_nodes > 1) ? numa_info[0].node_mem : ram_size;
    uint32_t initrd_base = 0;
    long kernel_size = 0, initrd_size = 0;
    long load_limit, rtas_limit, fw_size;
    bool kernel_le = false;
    char *filename;

    msi_supported = true;

    spapr = g_malloc0(sizeof(*spapr));
    QLIST_INIT(&spapr->phbs);

    cpu_ppc_hypercall = emulate_spapr_hypercall;

    /* Allocate RMA if necessary */
    rma_alloc_size = kvmppc_alloc_rma(&rma);

    if (rma_alloc_size == -1) {
        hw_error("qemu: Unable to create RMA\n");
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
        fprintf(stderr, "Error: Numa node 0 has to span the RMA (%#08"HWADDR_PRIx")\n",
                spapr->rma_size);
        exit(1);
    }

    /* We place the device tree and RTAS just below either the top of the RMA,
     * or just below 2GB, whichever is lowere, so that it can be
     * processed with 32-bit real mode code if necessary */
    rtas_limit = MIN(spapr->rma_size, 0x80000000);
    spapr->rtas_addr = rtas_limit - RTAS_MAX_SIZE;
    spapr->fdt_addr = spapr->rtas_addr - FDT_MAX_SIZE;
    load_limit = spapr->fdt_addr - FW_OVERHEAD;

    /* We aim for a hash table of size 1/128 the size of RAM.  The
     * normal rule of thumb is 1/64 the size of RAM, but that's much
     * more than needed for the Linux guests we support. */
    spapr->htab_shift = 18; /* Minimum architected size */
    while (spapr->htab_shift <= 46) {
        if ((1ULL << (spapr->htab_shift + 7)) >= ram_size) {
            break;
        }
        spapr->htab_shift++;
    }

    /* Set up Interrupt Controller before we create the VCPUs */
    spapr->icp = xics_system_init(smp_cpus * kvmppc_smt_threads() / smp_threads,
                                  XICS_IRQS);

    /* init CPUs */
    if (cpu_model == NULL) {
        cpu_model = kvm_enabled() ? "host" : "POWER7";
    }
    for (i = 0; i < smp_cpus; i++) {
        cpu = cpu_ppc_init(cpu_model);
        if (cpu == NULL) {
            fprintf(stderr, "Unable to find PowerPC CPU definition\n");
            exit(1);
        }
        env = &cpu->env;

        /* Set time-base frequency to 512 MHz */
        cpu_ppc_tb_init(env, TIMEBASE_FREQ);

        /* PAPR always has exception vectors in RAM not ROM. To ensure this,
         * MSR[IP] should never be set.
         */
        env->msr_mask &= ~(1 << 6);

        /* Tell KVM that we're in PAPR mode */
        if (kvm_enabled()) {
            kvmppc_set_papr(cpu);
        }

        if (cpu->max_compat) {
            if (ppc_set_compat(cpu, cpu->max_compat) < 0) {
                exit(1);
            }
        }

        xics_cpu_setup(spapr->icp, cpu);

        qemu_register_reset(spapr_cpu_reset, cpu);
    }

    /* allocate RAM */
    spapr->ram_limit = ram_size;
    memory_region_allocate_system_memory(ram, NULL, "ppc_spapr.ram",
                                         spapr->ram_limit);
    memory_region_add_subregion(sysmem, 0, ram);

    if (rma_alloc_size && rma) {
        rma_region = g_new(MemoryRegion, 1);
        memory_region_init_ram_ptr(rma_region, NULL, "ppc_spapr.rma",
                                   rma_alloc_size, rma);
        vmstate_register_ram_global(rma_region);
        memory_region_add_subregion(sysmem, 0, rma_region);
    }

    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, "spapr-rtas.bin");
    spapr->rtas_size = load_image_targphys(filename, spapr->rtas_addr,
                                           rtas_limit - spapr->rtas_addr);
    if (spapr->rtas_size < 0) {
        hw_error("qemu: could not load LPAR rtas '%s'\n", filename);
        exit(1);
    }
    if (spapr->rtas_size > RTAS_MAX_SIZE) {
        hw_error("RTAS too big ! 0x%lx bytes (max is 0x%x)\n",
                 spapr->rtas_size, RTAS_MAX_SIZE);
        exit(1);
    }
    g_free(filename);

    /* Set up EPOW events infrastructure */
    spapr_events_init(spapr);

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
    spapr_pci_msi_init(spapr, SPAPR_PCI_MSI_WINDOW);
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
    if (spapr_vga_init(phb->bus)) {
        spapr->has_graphics = true;
    }

    if (usb_enabled(spapr->has_graphics)) {
        pci_create_simple(phb->bus, -1, "pci-ohci");
        if (spapr->has_graphics) {
            usbdevice_create("keyboard");
            usbdevice_create("mouse");
        }
    }

    if (spapr->rma_size < (MIN_RMA_SLOF << 20)) {
        fprintf(stderr, "qemu: pSeries SLOF firmware requires >= "
                "%ldM guest RMA (Real Mode Area memory)\n", MIN_RMA_SLOF);
        exit(1);
    }

    if (kernel_filename) {
        uint64_t lowaddr = 0;

        kernel_size = load_elf(kernel_filename, translate_kernel_address, NULL,
                               NULL, &lowaddr, NULL, 1, ELF_MACHINE, 0);
        if (kernel_size == ELF_LOAD_WRONG_ENDIAN) {
            kernel_size = load_elf(kernel_filename,
                                   translate_kernel_address, NULL,
                                   NULL, &lowaddr, NULL, 0, ELF_MACHINE, 0);
            kernel_le = kernel_size > 0;
        }
        if (kernel_size < 0) {
            fprintf(stderr, "qemu: error loading %s: %s\n",
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
                fprintf(stderr, "qemu: could not load initial ram disk '%s'\n",
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
    fw_size = load_image_targphys(filename, 0, FW_MAX_SIZE);
    if (fw_size < 0) {
        hw_error("qemu: could not load LPAR rtas '%s'\n", filename);
        exit(1);
    }
    g_free(filename);

    spapr->entry_point = 0x100;

    vmstate_register(NULL, 0, &vmstate_spapr, spapr);
    register_savevm_live(NULL, "spapr/htab", -1, 1,
                         &savevm_htab_handlers, spapr);

    /* Prepare the device tree */
    spapr->fdt_skel = spapr_create_fdt_skel(initrd_base, initrd_size,
                                            kernel_size, kernel_le,
                                            boot_device, kernel_cmdline,
                                            spapr->epow_irq);
    assert(spapr->fdt_skel != NULL);
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
 * Implementation of an interface to adjust firmware patch
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
    sPAPRMachineState *sm = SPAPR_MACHINE(obj);

    return g_strdup(sm->kvm_type);
}

static void spapr_set_kvm_type(Object *obj, const char *value, Error **errp)
{
    sPAPRMachineState *sm = SPAPR_MACHINE(obj);

    g_free(sm->kvm_type);
    sm->kvm_type = g_strdup(value);
}

static void spapr_machine_initfn(Object *obj)
{
    object_property_add_str(obj, "kvm-type",
                            spapr_get_kvm_type, spapr_set_kvm_type, NULL);
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

static void spapr_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    FWPathProviderClass *fwc = FW_PATH_PROVIDER_CLASS(oc);
    NMIClass *nc = NMI_CLASS(oc);

    mc->name = "pseries";
    mc->desc = "pSeries Logical Partition (PAPR compliant)";
    mc->is_default = 1;
    mc->init = ppc_spapr_init;
    mc->reset = ppc_spapr_reset;
    mc->block_default_type = IF_SCSI;
    mc->max_cpus = MAX_CPUS;
    mc->no_parallel = 1;
    mc->default_boot_order = NULL;
    mc->kvm_type = spapr_kvm_type;

    fwc->get_dev_path = spapr_get_fw_dev_path;
    nc->nmi_monitor_handler = spapr_nmi;
}

static const TypeInfo spapr_machine_info = {
    .name          = TYPE_SPAPR_MACHINE,
    .parent        = TYPE_MACHINE,
    .instance_size = sizeof(sPAPRMachineState),
    .instance_init = spapr_machine_initfn,
    .class_init    = spapr_machine_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_FW_PATH_PROVIDER },
        { TYPE_NMI },
        { }
    },
};

static void spapr_machine_2_1_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->name = "pseries-2.1";
    mc->desc = "pSeries Logical Partition (PAPR compliant) v2.1";
    mc->is_default = 0;
}

static const TypeInfo spapr_machine_2_1_info = {
    .name          = TYPE_SPAPR_MACHINE "2.1",
    .parent        = TYPE_SPAPR_MACHINE,
    .class_init    = spapr_machine_2_1_class_init,
};

static void spapr_machine_register_types(void)
{
    type_register_static(&spapr_machine_info);
    type_register_static(&spapr_machine_2_1_info);
}

type_init(spapr_machine_register_types)
