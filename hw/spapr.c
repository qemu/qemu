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
#include "sysemu.h"
#include "hw.h"
#include "elf.h"
#include "net.h"
#include "blockdev.h"
#include "cpus.h"
#include "kvm.h"
#include "kvm_ppc.h"

#include "hw/boards.h"
#include "hw/ppc.h"
#include "hw/loader.h"

#include "hw/spapr.h"
#include "hw/spapr_vio.h"
#include "hw/spapr_pci.h"
#include "hw/xics.h"

#include "kvm.h"
#include "kvm_ppc.h"
#include "pci.h"

#include "exec-memory.h"

#include <libfdt.h>

#define KERNEL_LOAD_ADDR        0x00000000
#define INITRD_LOAD_ADDR        0x02800000
#define FDT_MAX_SIZE            0x10000
#define RTAS_MAX_SIZE           0x10000
#define FW_MAX_SIZE             0x400000
#define FW_FILE_NAME            "slof.bin"

#define MIN_RMA_SLOF		128UL

#define TIMEBASE_FREQ           512000000ULL

#define MAX_CPUS                256
#define XICS_IRQS		1024

#define SPAPR_PCI_BUID          0x800000020000001ULL
#define SPAPR_PCI_MEM_WIN_ADDR  (0x10000000000ULL + 0xA0000000)
#define SPAPR_PCI_MEM_WIN_SIZE  0x20000000
#define SPAPR_PCI_IO_WIN_ADDR   (0x10000000000ULL + 0x80000000)

#define PHANDLE_XICP            0x00001111

sPAPREnvironment *spapr;

qemu_irq spapr_allocate_irq(uint32_t hint, uint32_t *irq_num)
{
    uint32_t irq;
    qemu_irq qirq;

    if (hint) {
        irq = hint;
        /* FIXME: we should probably check for collisions somehow */
    } else {
        irq = spapr->next_irq++;
    }

    qirq = xics_find_qirq(spapr->icp, irq);
    if (!qirq) {
        return NULL;
    }

    if (irq_num) {
        *irq_num = irq;
    }

    return qirq;
}

static void *spapr_create_fdt_skel(const char *cpu_model,
                                   target_phys_addr_t rma_size,
                                   target_phys_addr_t initrd_base,
                                   target_phys_addr_t initrd_size,
                                   const char *boot_device,
                                   const char *kernel_cmdline,
                                   long hash_shift)
{
    void *fdt;
    CPUState *env;
    uint64_t mem_reg_property_rma[] = { 0, cpu_to_be64(rma_size) };
    uint64_t mem_reg_property_nonrma[] = { cpu_to_be64(rma_size),
                                           cpu_to_be64(ram_size - rma_size) };
    uint32_t start_prop = cpu_to_be32(initrd_base);
    uint32_t end_prop = cpu_to_be32(initrd_base + initrd_size);
    uint32_t pft_size_prop[] = {0, cpu_to_be32(hash_shift)};
    char hypertas_prop[] = "hcall-pft\0hcall-term\0hcall-dabr\0hcall-interrupt"
        "\0hcall-tce\0hcall-vio\0hcall-splpar\0hcall-bulk";
    uint32_t interrupt_server_ranges_prop[] = {0, cpu_to_be32(smp_cpus)};
    int i;
    char *modelname;
    int smt = kvmppc_smt_threads();

#define _FDT(exp) \
    do { \
        int ret = (exp);                                           \
        if (ret < 0) {                                             \
            fprintf(stderr, "qemu: error creating device tree: %s: %s\n", \
                    #exp, fdt_strerror(ret));                      \
            exit(1);                                               \
        }                                                          \
    } while (0)

    fdt = g_malloc0(FDT_MAX_SIZE);
    _FDT((fdt_create(fdt, FDT_MAX_SIZE)));

    _FDT((fdt_finish_reservemap(fdt)));

    /* Root node */
    _FDT((fdt_begin_node(fdt, "")));
    _FDT((fdt_property_string(fdt, "device_type", "chrp")));
    _FDT((fdt_property_string(fdt, "model", "IBM pSeries (emulated by qemu)")));

    _FDT((fdt_property_cell(fdt, "#address-cells", 0x2)));
    _FDT((fdt_property_cell(fdt, "#size-cells", 0x2)));

    /* /chosen */
    _FDT((fdt_begin_node(fdt, "chosen")));

    _FDT((fdt_property_string(fdt, "bootargs", kernel_cmdline)));
    _FDT((fdt_property(fdt, "linux,initrd-start",
                       &start_prop, sizeof(start_prop))));
    _FDT((fdt_property(fdt, "linux,initrd-end",
                       &end_prop, sizeof(end_prop))));
    _FDT((fdt_property_string(fdt, "qemu,boot-device", boot_device)));

    /*
     * Because we don't always invoke any firmware, we can't rely on
     * that to do BAR allocation.  Long term, we should probably do
     * that ourselves, but for now, this setting (plus advertising the
     * current BARs as 0) causes sufficiently recent kernels to to the
     * BAR assignment themselves */
    _FDT((fdt_property_cell(fdt, "linux,pci-probe-only", 0)));

    _FDT((fdt_end_node(fdt)));

    /* memory node(s) */
    _FDT((fdt_begin_node(fdt, "memory@0")));

    _FDT((fdt_property_string(fdt, "device_type", "memory")));
    _FDT((fdt_property(fdt, "reg", mem_reg_property_rma,
                       sizeof(mem_reg_property_rma))));
    _FDT((fdt_end_node(fdt)));

    if (ram_size > rma_size) {
        char mem_name[32];

        sprintf(mem_name, "memory@%" PRIx64, (uint64_t)rma_size);
        _FDT((fdt_begin_node(fdt, mem_name)));
        _FDT((fdt_property_string(fdt, "device_type", "memory")));
        _FDT((fdt_property(fdt, "reg", mem_reg_property_nonrma,
                           sizeof(mem_reg_property_nonrma))));
        _FDT((fdt_end_node(fdt)));
    }

    /* cpus */
    _FDT((fdt_begin_node(fdt, "cpus")));

    _FDT((fdt_property_cell(fdt, "#address-cells", 0x1)));
    _FDT((fdt_property_cell(fdt, "#size-cells", 0x0)));

    modelname = g_strdup(cpu_model);

    for (i = 0; i < strlen(modelname); i++) {
        modelname[i] = toupper(modelname[i]);
    }

    for (env = first_cpu; env != NULL; env = env->next_cpu) {
        int index = env->cpu_index;
        uint32_t servers_prop[smp_threads];
        uint32_t gservers_prop[smp_threads * 2];
        char *nodename;
        uint32_t segs[] = {cpu_to_be32(28), cpu_to_be32(40),
                           0xffffffff, 0xffffffff};
        uint32_t tbfreq = kvm_enabled() ? kvmppc_get_tbfreq() : TIMEBASE_FREQ;
        uint32_t cpufreq = kvm_enabled() ? kvmppc_get_clockfreq() : 1000000000;

        if ((index % smt) != 0) {
            continue;
        }

        if (asprintf(&nodename, "%s@%x", modelname, index) < 0) {
            fprintf(stderr, "Allocation failure\n");
            exit(1);
        }

        _FDT((fdt_begin_node(fdt, nodename)));

        free(nodename);

        _FDT((fdt_property_cell(fdt, "reg", index)));
        _FDT((fdt_property_string(fdt, "device_type", "cpu")));

        _FDT((fdt_property_cell(fdt, "cpu-version", env->spr[SPR_PVR])));
        _FDT((fdt_property_cell(fdt, "dcache-block-size",
                                env->dcache_line_size)));
        _FDT((fdt_property_cell(fdt, "icache-block-size",
                                env->icache_line_size)));
        _FDT((fdt_property_cell(fdt, "timebase-frequency", tbfreq)));
        _FDT((fdt_property_cell(fdt, "clock-frequency", cpufreq)));
        _FDT((fdt_property_cell(fdt, "ibm,slb-size", env->slb_nr)));
        _FDT((fdt_property(fdt, "ibm,pft-size",
                           pft_size_prop, sizeof(pft_size_prop))));
        _FDT((fdt_property_string(fdt, "status", "okay")));
        _FDT((fdt_property(fdt, "64-bit", NULL, 0)));

        /* Build interrupt servers and gservers properties */
        for (i = 0; i < smp_threads; i++) {
            servers_prop[i] = cpu_to_be32(index + i);
            /* Hack, direct the group queues back to cpu 0 */
            gservers_prop[i*2] = cpu_to_be32(index + i);
            gservers_prop[i*2 + 1] = 0;
        }
        _FDT((fdt_property(fdt, "ibm,ppc-interrupt-server#s",
                           servers_prop, sizeof(servers_prop))));
        _FDT((fdt_property(fdt, "ibm,ppc-interrupt-gserver#s",
                           gservers_prop, sizeof(gservers_prop))));

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

        _FDT((fdt_end_node(fdt)));
    }

    g_free(modelname);

    _FDT((fdt_end_node(fdt)));

    /* RTAS */
    _FDT((fdt_begin_node(fdt, "rtas")));

    _FDT((fdt_property(fdt, "ibm,hypertas-functions", hypertas_prop,
                       sizeof(hypertas_prop))));

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

    _FDT((fdt_end_node(fdt))); /* close root node */
    _FDT((fdt_finish(fdt)));

    return fdt;
}

static void spapr_finalize_fdt(sPAPREnvironment *spapr,
                               target_phys_addr_t fdt_addr,
                               target_phys_addr_t rtas_addr,
                               target_phys_addr_t rtas_size)
{
    int ret;
    void *fdt;
    sPAPRPHBState *phb;

    fdt = g_malloc(FDT_MAX_SIZE);

    /* open out the base tree into a temp buffer for the final tweaks */
    _FDT((fdt_open_into(spapr->fdt_skel, fdt, FDT_MAX_SIZE)));

    ret = spapr_populate_vdevice(spapr->vio_bus, fdt);
    if (ret < 0) {
        fprintf(stderr, "couldn't setup vio devices in fdt\n");
        exit(1);
    }

    QLIST_FOREACH(phb, &spapr->phbs, list) {
        ret = spapr_populate_pci_devices(phb, PHANDLE_XICP, fdt);
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

    _FDT((fdt_pack(fdt)));

    cpu_physical_memory_write(fdt_addr, fdt, fdt_totalsize(fdt));

    g_free(fdt);
}

static uint64_t translate_kernel_address(void *opaque, uint64_t addr)
{
    return (addr & 0x0fffffff) + KERNEL_LOAD_ADDR;
}

static void emulate_spapr_hypercall(CPUState *env)
{
    env->gpr[3] = spapr_hypercall(env, env->gpr[3], &env->gpr[4]);
}

static void spapr_reset(void *opaque)
{
    sPAPREnvironment *spapr = (sPAPREnvironment *)opaque;

    fprintf(stderr, "sPAPR reset\n");

    /* flush out the hash table */
    memset(spapr->htab, 0, spapr->htab_size);

    /* Load the fdt */
    spapr_finalize_fdt(spapr, spapr->fdt_addr, spapr->rtas_addr,
                       spapr->rtas_size);

    /* Set up the entry state */
    first_cpu->gpr[3] = spapr->fdt_addr;
    first_cpu->gpr[5] = 0;
    first_cpu->halted = 0;
    first_cpu->nip = spapr->entry_point;

}

/* pSeries LPAR / sPAPR hardware init */
static void ppc_spapr_init(ram_addr_t ram_size,
                           const char *boot_device,
                           const char *kernel_filename,
                           const char *kernel_cmdline,
                           const char *initrd_filename,
                           const char *cpu_model)
{
    CPUState *env;
    int i;
    MemoryRegion *sysmem = get_system_memory();
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    target_phys_addr_t rma_alloc_size, rma_size;
    uint32_t initrd_base;
    long kernel_size, initrd_size, fw_size;
    long pteg_shift = 17;
    char *filename;

    spapr = g_malloc0(sizeof(*spapr));
    QLIST_INIT(&spapr->phbs);

    cpu_ppc_hypercall = emulate_spapr_hypercall;

    /* Allocate RMA if necessary */
    rma_alloc_size = kvmppc_alloc_rma("ppc_spapr.rma", sysmem);

    if (rma_alloc_size == -1) {
        hw_error("qemu: Unable to create RMA\n");
        exit(1);
    }
    if (rma_alloc_size && (rma_alloc_size < ram_size)) {
        rma_size = rma_alloc_size;
    } else {
        rma_size = ram_size;
    }

    /* We place the device tree just below either the top of the RMA,
     * or just below 2GB, whichever is lowere, so that it can be
     * processed with 32-bit real mode code if necessary */
    spapr->fdt_addr = MIN(rma_size, 0x80000000) - FDT_MAX_SIZE;
    spapr->rtas_addr = spapr->fdt_addr - RTAS_MAX_SIZE;

    /* init CPUs */
    if (cpu_model == NULL) {
        cpu_model = kvm_enabled() ? "host" : "POWER7";
    }
    for (i = 0; i < smp_cpus; i++) {
        env = cpu_init(cpu_model);

        if (!env) {
            fprintf(stderr, "Unable to find PowerPC CPU definition\n");
            exit(1);
        }
        /* Set time-base frequency to 512 MHz */
        cpu_ppc_tb_init(env, TIMEBASE_FREQ);
        qemu_register_reset((QEMUResetHandler *)&cpu_reset, env);

        env->hreset_vector = 0x60;
        env->hreset_excp_prefix = 0;
        env->gpr[3] = env->cpu_index;
    }

    /* allocate RAM */
    spapr->ram_limit = ram_size;
    if (spapr->ram_limit > rma_alloc_size) {
        ram_addr_t nonrma_base = rma_alloc_size;
        ram_addr_t nonrma_size = spapr->ram_limit - rma_alloc_size;

        memory_region_init_ram(ram, NULL, "ppc_spapr.ram", nonrma_size);
        memory_region_add_subregion(sysmem, nonrma_base, ram);
    }

    /* allocate hash page table.  For now we always make this 16mb,
     * later we should probably make it scale to the size of guest
     * RAM */
    spapr->htab_size = 1ULL << (pteg_shift + 7);
    spapr->htab = qemu_memalign(spapr->htab_size, spapr->htab_size);

    for (env = first_cpu; env != NULL; env = env->next_cpu) {
        env->external_htab = spapr->htab;
        env->htab_base = -1;
        env->htab_mask = spapr->htab_size - 1;

        /* Tell KVM that we're in PAPR mode */
        env->spr[SPR_SDR1] = (unsigned long)spapr->htab |
                             ((pteg_shift + 7) - 18);
        env->spr[SPR_HIOR] = 0;

        if (kvm_enabled()) {
            kvmppc_set_papr(env);
        }
    }

    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, "spapr-rtas.bin");
    spapr->rtas_size = load_image_targphys(filename, spapr->rtas_addr,
                                           ram_size - spapr->rtas_addr);
    if (spapr->rtas_size < 0) {
        hw_error("qemu: could not load LPAR rtas '%s'\n", filename);
        exit(1);
    }
    g_free(filename);

    /* Set up Interrupt Controller */
    spapr->icp = xics_system_init(XICS_IRQS);
    spapr->next_irq = 16;

    /* Set up VIO bus */
    spapr->vio_bus = spapr_vio_bus_init();

    for (i = 0; i < MAX_SERIAL_PORTS; i++) {
        if (serial_hds[i]) {
            spapr_vty_create(spapr->vio_bus, SPAPR_VTY_BASE_ADDRESS + i,
                             serial_hds[i]);
        }
    }

    /* Set up PCI */
    spapr_create_phb(spapr, "pci", SPAPR_PCI_BUID,
                     SPAPR_PCI_MEM_WIN_ADDR,
                     SPAPR_PCI_MEM_WIN_SIZE,
                     SPAPR_PCI_IO_WIN_ADDR);

    for (i = 0; i < nb_nics; i++) {
        NICInfo *nd = &nd_table[i];

        if (!nd->model) {
            nd->model = g_strdup("ibmveth");
        }

        if (strcmp(nd->model, "ibmveth") == 0) {
            spapr_vlan_create(spapr->vio_bus, 0x1000 + i, nd);
        } else {
            pci_nic_init_nofail(&nd_table[i], nd->model, NULL);
        }
    }

    for (i = 0; i <= drive_get_max_bus(IF_SCSI); i++) {
        spapr_vscsi_create(spapr->vio_bus, 0x2000 + i);
    }

    if (kernel_filename) {
        uint64_t lowaddr = 0;

        kernel_size = load_elf(kernel_filename, translate_kernel_address, NULL,
                               NULL, &lowaddr, NULL, 1, ELF_MACHINE, 0);
        if (kernel_size < 0) {
            kernel_size = load_image_targphys(kernel_filename,
                                              KERNEL_LOAD_ADDR,
                                              ram_size - KERNEL_LOAD_ADDR);
        }
        if (kernel_size < 0) {
            fprintf(stderr, "qemu: could not load kernel '%s'\n",
                    kernel_filename);
            exit(1);
        }

        /* load initrd */
        if (initrd_filename) {
            initrd_base = INITRD_LOAD_ADDR;
            initrd_size = load_image_targphys(initrd_filename, initrd_base,
                                              ram_size - initrd_base);
            if (initrd_size < 0) {
                fprintf(stderr, "qemu: could not load initial ram disk '%s'\n",
                        initrd_filename);
                exit(1);
            }
        } else {
            initrd_base = 0;
            initrd_size = 0;
        }

        spapr->entry_point = KERNEL_LOAD_ADDR;
    } else {
        if (rma_size < (MIN_RMA_SLOF << 20)) {
            fprintf(stderr, "qemu: pSeries SLOF firmware requires >= "
                    "%ldM guest RMA (Real Mode Area memory)\n", MIN_RMA_SLOF);
            exit(1);
        }
        filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, FW_FILE_NAME);
        fw_size = load_image_targphys(filename, 0, FW_MAX_SIZE);
        if (fw_size < 0) {
            hw_error("qemu: could not load LPAR rtas '%s'\n", filename);
            exit(1);
        }
        g_free(filename);
        spapr->entry_point = 0x100;
        initrd_base = 0;
        initrd_size = 0;

        /* SLOF will startup the secondary CPUs using RTAS,
           rather than expecting a kexec() style entry */
        for (env = first_cpu; env != NULL; env = env->next_cpu) {
            env->halted = 1;
        }
    }

    /* Prepare the device tree */
    spapr->fdt_skel = spapr_create_fdt_skel(cpu_model, rma_size,
                                            initrd_base, initrd_size,
                                            boot_device, kernel_cmdline,
                                            pteg_shift + 7);
    assert(spapr->fdt_skel != NULL);

    qemu_register_reset(spapr_reset, spapr);
}

static QEMUMachine spapr_machine = {
    .name = "pseries",
    .desc = "pSeries Logical Partition (PAPR compliant)",
    .init = ppc_spapr_init,
    .max_cpus = MAX_CPUS,
    .no_vga = 1,
    .no_parallel = 1,
    .use_scsi = 1,
};

static void spapr_machine_init(void)
{
    qemu_register_machine(&spapr_machine);
}

machine_init(spapr_machine_init);
