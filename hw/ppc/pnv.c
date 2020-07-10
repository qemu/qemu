/*
 * QEMU PowerPC PowerNV machine model
 *
 * Copyright (c) 2016, IBM Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "sysemu/qtest.h"
#include "sysemu/sysemu.h"
#include "sysemu/numa.h"
#include "sysemu/reset.h"
#include "sysemu/runstate.h"
#include "sysemu/cpus.h"
#include "sysemu/device_tree.h"
#include "sysemu/hw_accel.h"
#include "target/ppc/cpu.h"
#include "qemu/log.h"
#include "hw/ppc/fdt.h"
#include "hw/ppc/ppc.h"
#include "hw/ppc/pnv.h"
#include "hw/ppc/pnv_core.h"
#include "hw/loader.h"
#include "hw/nmi.h"
#include "exec/address-spaces.h"
#include "qapi/visitor.h"
#include "monitor/monitor.h"
#include "hw/intc/intc.h"
#include "hw/ipmi/ipmi.h"
#include "target/ppc/mmu-hash64.h"
#include "hw/pci/msi.h"

#include "hw/ppc/xics.h"
#include "hw/qdev-properties.h"
#include "hw/ppc/pnv_xscom.h"
#include "hw/ppc/pnv_pnor.h"

#include "hw/isa/isa.h"
#include "hw/boards.h"
#include "hw/char/serial.h"
#include "hw/rtc/mc146818rtc.h"

#include <libfdt.h>

#define FDT_MAX_SIZE            (1 * MiB)

#define FW_FILE_NAME            "skiboot.lid"
#define FW_LOAD_ADDR            0x0
#define FW_MAX_SIZE             (4 * MiB)

#define KERNEL_LOAD_ADDR        0x20000000
#define KERNEL_MAX_SIZE         (256 * MiB)
#define INITRD_LOAD_ADDR        0x60000000
#define INITRD_MAX_SIZE         (256 * MiB)

static const char *pnv_chip_core_typename(const PnvChip *o)
{
    const char *chip_type = object_class_get_name(object_get_class(OBJECT(o)));
    int len = strlen(chip_type) - strlen(PNV_CHIP_TYPE_SUFFIX);
    char *s = g_strdup_printf(PNV_CORE_TYPE_NAME("%.*s"), len, chip_type);
    const char *core_type = object_class_get_name(object_class_by_name(s));
    g_free(s);
    return core_type;
}

/*
 * On Power Systems E880 (POWER8), the max cpus (threads) should be :
 *     4 * 4 sockets * 12 cores * 8 threads = 1536
 * Let's make it 2^11
 */
#define MAX_CPUS                2048

/*
 * Memory nodes are created by hostboot, one for each range of memory
 * that has a different "affinity". In practice, it means one range
 * per chip.
 */
static void pnv_dt_memory(void *fdt, int chip_id, hwaddr start, hwaddr size)
{
    char *mem_name;
    uint64_t mem_reg_property[2];
    int off;

    mem_reg_property[0] = cpu_to_be64(start);
    mem_reg_property[1] = cpu_to_be64(size);

    mem_name = g_strdup_printf("memory@%"HWADDR_PRIx, start);
    off = fdt_add_subnode(fdt, 0, mem_name);
    g_free(mem_name);

    _FDT((fdt_setprop_string(fdt, off, "device_type", "memory")));
    _FDT((fdt_setprop(fdt, off, "reg", mem_reg_property,
                       sizeof(mem_reg_property))));
    _FDT((fdt_setprop_cell(fdt, off, "ibm,chip-id", chip_id)));
}

static int get_cpus_node(void *fdt)
{
    int cpus_offset = fdt_path_offset(fdt, "/cpus");

    if (cpus_offset < 0) {
        cpus_offset = fdt_add_subnode(fdt, 0, "cpus");
        if (cpus_offset) {
            _FDT((fdt_setprop_cell(fdt, cpus_offset, "#address-cells", 0x1)));
            _FDT((fdt_setprop_cell(fdt, cpus_offset, "#size-cells", 0x0)));
        }
    }
    _FDT(cpus_offset);
    return cpus_offset;
}

/*
 * The PowerNV cores (and threads) need to use real HW ids and not an
 * incremental index like it has been done on other platforms. This HW
 * id is stored in the CPU PIR, it is used to create cpu nodes in the
 * device tree, used in XSCOM to address cores and in interrupt
 * servers.
 */
static void pnv_dt_core(PnvChip *chip, PnvCore *pc, void *fdt)
{
    PowerPCCPU *cpu = pc->threads[0];
    CPUState *cs = CPU(cpu);
    DeviceClass *dc = DEVICE_GET_CLASS(cs);
    int smt_threads = CPU_CORE(pc)->nr_threads;
    CPUPPCState *env = &cpu->env;
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cs);
    uint32_t servers_prop[smt_threads];
    int i;
    uint32_t segs[] = {cpu_to_be32(28), cpu_to_be32(40),
                       0xffffffff, 0xffffffff};
    uint32_t tbfreq = PNV_TIMEBASE_FREQ;
    uint32_t cpufreq = 1000000000;
    uint32_t page_sizes_prop[64];
    size_t page_sizes_prop_size;
    const uint8_t pa_features[] = { 24, 0,
                                    0xf6, 0x3f, 0xc7, 0xc0, 0x80, 0xf0,
                                    0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
                                    0x00, 0x00, 0x00, 0x00, 0x80, 0x00,
                                    0x80, 0x00, 0x80, 0x00, 0x80, 0x00 };
    int offset;
    char *nodename;
    int cpus_offset = get_cpus_node(fdt);

    nodename = g_strdup_printf("%s@%x", dc->fw_name, pc->pir);
    offset = fdt_add_subnode(fdt, cpus_offset, nodename);
    _FDT(offset);
    g_free(nodename);

    _FDT((fdt_setprop_cell(fdt, offset, "ibm,chip-id", chip->chip_id)));

    _FDT((fdt_setprop_cell(fdt, offset, "reg", pc->pir)));
    _FDT((fdt_setprop_cell(fdt, offset, "ibm,pir", pc->pir)));
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
    _FDT((fdt_setprop_cell(fdt, offset, "ibm,slb-size",
                           cpu->hash64_opts->slb_size)));
    _FDT((fdt_setprop_string(fdt, offset, "status", "okay")));
    _FDT((fdt_setprop(fdt, offset, "64-bit", NULL, 0)));

    if (env->spr_cb[SPR_PURR].oea_read) {
        _FDT((fdt_setprop(fdt, offset, "ibm,purr", NULL, 0)));
    }

    if (ppc_hash64_has(cpu, PPC_HASH64_1TSEG)) {
        _FDT((fdt_setprop(fdt, offset, "ibm,processor-segment-sizes",
                           segs, sizeof(segs))));
    }

    /*
     * Advertise VMX/VSX (vector extensions) if available
     *   0 / no property == no vector extensions
     *   1               == VMX / Altivec available
     *   2               == VSX available
     */
    if (env->insns_flags & PPC_ALTIVEC) {
        uint32_t vmx = (env->insns_flags2 & PPC2_VSX) ? 2 : 1;

        _FDT((fdt_setprop_cell(fdt, offset, "ibm,vmx", vmx)));
    }

    /*
     * Advertise DFP (Decimal Floating Point) if available
     *   0 / no property == no DFP
     *   1               == DFP available
     */
    if (env->insns_flags2 & PPC2_DFP) {
        _FDT((fdt_setprop_cell(fdt, offset, "ibm,dfp", 1)));
    }

    page_sizes_prop_size = ppc_create_page_sizes_prop(cpu, page_sizes_prop,
                                                      sizeof(page_sizes_prop));
    if (page_sizes_prop_size) {
        _FDT((fdt_setprop(fdt, offset, "ibm,segment-page-sizes",
                           page_sizes_prop, page_sizes_prop_size)));
    }

    _FDT((fdt_setprop(fdt, offset, "ibm,pa-features",
                       pa_features, sizeof(pa_features))));

    /* Build interrupt servers properties */
    for (i = 0; i < smt_threads; i++) {
        servers_prop[i] = cpu_to_be32(pc->pir + i);
    }
    _FDT((fdt_setprop(fdt, offset, "ibm,ppc-interrupt-server#s",
                       servers_prop, sizeof(servers_prop))));
}

static void pnv_dt_icp(PnvChip *chip, void *fdt, uint32_t pir,
                       uint32_t nr_threads)
{
    uint64_t addr = PNV_ICP_BASE(chip) | (pir << 12);
    char *name;
    const char compat[] = "IBM,power8-icp\0IBM,ppc-xicp";
    uint32_t irange[2], i, rsize;
    uint64_t *reg;
    int offset;

    irange[0] = cpu_to_be32(pir);
    irange[1] = cpu_to_be32(nr_threads);

    rsize = sizeof(uint64_t) * 2 * nr_threads;
    reg = g_malloc(rsize);
    for (i = 0; i < nr_threads; i++) {
        reg[i * 2] = cpu_to_be64(addr | ((pir + i) * 0x1000));
        reg[i * 2 + 1] = cpu_to_be64(0x1000);
    }

    name = g_strdup_printf("interrupt-controller@%"PRIX64, addr);
    offset = fdt_add_subnode(fdt, 0, name);
    _FDT(offset);
    g_free(name);

    _FDT((fdt_setprop(fdt, offset, "compatible", compat, sizeof(compat))));
    _FDT((fdt_setprop(fdt, offset, "reg", reg, rsize)));
    _FDT((fdt_setprop_string(fdt, offset, "device_type",
                              "PowerPC-External-Interrupt-Presentation")));
    _FDT((fdt_setprop(fdt, offset, "interrupt-controller", NULL, 0)));
    _FDT((fdt_setprop(fdt, offset, "ibm,interrupt-server-ranges",
                       irange, sizeof(irange))));
    _FDT((fdt_setprop_cell(fdt, offset, "#interrupt-cells", 1)));
    _FDT((fdt_setprop_cell(fdt, offset, "#address-cells", 0)));
    g_free(reg);
}

static void pnv_chip_power8_dt_populate(PnvChip *chip, void *fdt)
{
    static const char compat[] = "ibm,power8-xscom\0ibm,xscom";
    int i;

    pnv_dt_xscom(chip, fdt, 0,
                 cpu_to_be64(PNV_XSCOM_BASE(chip)),
                 cpu_to_be64(PNV_XSCOM_SIZE),
                 compat, sizeof(compat));

    for (i = 0; i < chip->nr_cores; i++) {
        PnvCore *pnv_core = chip->cores[i];

        pnv_dt_core(chip, pnv_core, fdt);

        /* Interrupt Control Presenters (ICP). One per core. */
        pnv_dt_icp(chip, fdt, pnv_core->pir, CPU_CORE(pnv_core)->nr_threads);
    }

    if (chip->ram_size) {
        pnv_dt_memory(fdt, chip->chip_id, chip->ram_start, chip->ram_size);
    }
}

static void pnv_chip_power9_dt_populate(PnvChip *chip, void *fdt)
{
    static const char compat[] = "ibm,power9-xscom\0ibm,xscom";
    int i;

    pnv_dt_xscom(chip, fdt, 0,
                 cpu_to_be64(PNV9_XSCOM_BASE(chip)),
                 cpu_to_be64(PNV9_XSCOM_SIZE),
                 compat, sizeof(compat));

    for (i = 0; i < chip->nr_cores; i++) {
        PnvCore *pnv_core = chip->cores[i];

        pnv_dt_core(chip, pnv_core, fdt);
    }

    if (chip->ram_size) {
        pnv_dt_memory(fdt, chip->chip_id, chip->ram_start, chip->ram_size);
    }

    pnv_dt_lpc(chip, fdt, 0, PNV9_LPCM_BASE(chip), PNV9_LPCM_SIZE);
}

static void pnv_chip_power10_dt_populate(PnvChip *chip, void *fdt)
{
    static const char compat[] = "ibm,power10-xscom\0ibm,xscom";
    int i;

    pnv_dt_xscom(chip, fdt, 0,
                 cpu_to_be64(PNV10_XSCOM_BASE(chip)),
                 cpu_to_be64(PNV10_XSCOM_SIZE),
                 compat, sizeof(compat));

    for (i = 0; i < chip->nr_cores; i++) {
        PnvCore *pnv_core = chip->cores[i];

        pnv_dt_core(chip, pnv_core, fdt);
    }

    if (chip->ram_size) {
        pnv_dt_memory(fdt, chip->chip_id, chip->ram_start, chip->ram_size);
    }

    pnv_dt_lpc(chip, fdt, 0, PNV10_LPCM_BASE(chip), PNV10_LPCM_SIZE);
}

static void pnv_dt_rtc(ISADevice *d, void *fdt, int lpc_off)
{
    uint32_t io_base = d->ioport_id;
    uint32_t io_regs[] = {
        cpu_to_be32(1),
        cpu_to_be32(io_base),
        cpu_to_be32(2)
    };
    char *name;
    int node;

    name = g_strdup_printf("%s@i%x", qdev_fw_name(DEVICE(d)), io_base);
    node = fdt_add_subnode(fdt, lpc_off, name);
    _FDT(node);
    g_free(name);

    _FDT((fdt_setprop(fdt, node, "reg", io_regs, sizeof(io_regs))));
    _FDT((fdt_setprop_string(fdt, node, "compatible", "pnpPNP,b00")));
}

static void pnv_dt_serial(ISADevice *d, void *fdt, int lpc_off)
{
    const char compatible[] = "ns16550\0pnpPNP,501";
    uint32_t io_base = d->ioport_id;
    uint32_t io_regs[] = {
        cpu_to_be32(1),
        cpu_to_be32(io_base),
        cpu_to_be32(8)
    };
    char *name;
    int node;

    name = g_strdup_printf("%s@i%x", qdev_fw_name(DEVICE(d)), io_base);
    node = fdt_add_subnode(fdt, lpc_off, name);
    _FDT(node);
    g_free(name);

    _FDT((fdt_setprop(fdt, node, "reg", io_regs, sizeof(io_regs))));
    _FDT((fdt_setprop(fdt, node, "compatible", compatible,
                      sizeof(compatible))));

    _FDT((fdt_setprop_cell(fdt, node, "clock-frequency", 1843200)));
    _FDT((fdt_setprop_cell(fdt, node, "current-speed", 115200)));
    _FDT((fdt_setprop_cell(fdt, node, "interrupts", d->isairq[0])));
    _FDT((fdt_setprop_cell(fdt, node, "interrupt-parent",
                           fdt_get_phandle(fdt, lpc_off))));

    /* This is needed by Linux */
    _FDT((fdt_setprop_string(fdt, node, "device_type", "serial")));
}

static void pnv_dt_ipmi_bt(ISADevice *d, void *fdt, int lpc_off)
{
    const char compatible[] = "bt\0ipmi-bt";
    uint32_t io_base;
    uint32_t io_regs[] = {
        cpu_to_be32(1),
        0, /* 'io_base' retrieved from the 'ioport' property of 'isa-ipmi-bt' */
        cpu_to_be32(3)
    };
    uint32_t irq;
    char *name;
    int node;

    io_base = object_property_get_int(OBJECT(d), "ioport", &error_fatal);
    io_regs[1] = cpu_to_be32(io_base);

    irq = object_property_get_int(OBJECT(d), "irq", &error_fatal);

    name = g_strdup_printf("%s@i%x", qdev_fw_name(DEVICE(d)), io_base);
    node = fdt_add_subnode(fdt, lpc_off, name);
    _FDT(node);
    g_free(name);

    _FDT((fdt_setprop(fdt, node, "reg", io_regs, sizeof(io_regs))));
    _FDT((fdt_setprop(fdt, node, "compatible", compatible,
                      sizeof(compatible))));

    /* Mark it as reserved to avoid Linux trying to claim it */
    _FDT((fdt_setprop_string(fdt, node, "status", "reserved")));
    _FDT((fdt_setprop_cell(fdt, node, "interrupts", irq)));
    _FDT((fdt_setprop_cell(fdt, node, "interrupt-parent",
                           fdt_get_phandle(fdt, lpc_off))));
}

typedef struct ForeachPopulateArgs {
    void *fdt;
    int offset;
} ForeachPopulateArgs;

static int pnv_dt_isa_device(DeviceState *dev, void *opaque)
{
    ForeachPopulateArgs *args = opaque;
    ISADevice *d = ISA_DEVICE(dev);

    if (object_dynamic_cast(OBJECT(dev), TYPE_MC146818_RTC)) {
        pnv_dt_rtc(d, args->fdt, args->offset);
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_ISA_SERIAL)) {
        pnv_dt_serial(d, args->fdt, args->offset);
    } else if (object_dynamic_cast(OBJECT(dev), "isa-ipmi-bt")) {
        pnv_dt_ipmi_bt(d, args->fdt, args->offset);
    } else {
        error_report("unknown isa device %s@i%x", qdev_fw_name(dev),
                     d->ioport_id);
    }

    return 0;
}

/*
 * The default LPC bus of a multichip system is on chip 0. It's
 * recognized by the firmware (skiboot) using a "primary" property.
 */
static void pnv_dt_isa(PnvMachineState *pnv, void *fdt)
{
    int isa_offset = fdt_path_offset(fdt, pnv->chips[0]->dt_isa_nodename);
    ForeachPopulateArgs args = {
        .fdt = fdt,
        .offset = isa_offset,
    };
    uint32_t phandle;

    _FDT((fdt_setprop(fdt, isa_offset, "primary", NULL, 0)));

    phandle = qemu_fdt_alloc_phandle(fdt);
    assert(phandle > 0);
    _FDT((fdt_setprop_cell(fdt, isa_offset, "phandle", phandle)));

    /*
     * ISA devices are not necessarily parented to the ISA bus so we
     * can not use object_child_foreach()
     */
    qbus_walk_children(BUS(pnv->isa_bus), pnv_dt_isa_device, NULL, NULL, NULL,
                       &args);
}

static void pnv_dt_power_mgt(PnvMachineState *pnv, void *fdt)
{
    int off;

    off = fdt_add_subnode(fdt, 0, "ibm,opal");
    off = fdt_add_subnode(fdt, off, "power-mgt");

    _FDT(fdt_setprop_cell(fdt, off, "ibm,enabled-stop-levels", 0xc0000000));
}

static void *pnv_dt_create(MachineState *machine)
{
    PnvMachineClass *pmc = PNV_MACHINE_GET_CLASS(machine);
    PnvMachineState *pnv = PNV_MACHINE(machine);
    void *fdt;
    char *buf;
    int off;
    int i;

    fdt = g_malloc0(FDT_MAX_SIZE);
    _FDT((fdt_create_empty_tree(fdt, FDT_MAX_SIZE)));

    /* /qemu node */
    _FDT((fdt_add_subnode(fdt, 0, "qemu")));

    /* Root node */
    _FDT((fdt_setprop_cell(fdt, 0, "#address-cells", 0x2)));
    _FDT((fdt_setprop_cell(fdt, 0, "#size-cells", 0x2)));
    _FDT((fdt_setprop_string(fdt, 0, "model",
                             "IBM PowerNV (emulated by qemu)")));
    _FDT((fdt_setprop(fdt, 0, "compatible", pmc->compat, pmc->compat_size)));

    buf =  qemu_uuid_unparse_strdup(&qemu_uuid);
    _FDT((fdt_setprop_string(fdt, 0, "vm,uuid", buf)));
    if (qemu_uuid_set) {
        _FDT((fdt_property_string(fdt, "system-id", buf)));
    }
    g_free(buf);

    off = fdt_add_subnode(fdt, 0, "chosen");
    if (machine->kernel_cmdline) {
        _FDT((fdt_setprop_string(fdt, off, "bootargs",
                                 machine->kernel_cmdline)));
    }

    if (pnv->initrd_size) {
        uint32_t start_prop = cpu_to_be32(pnv->initrd_base);
        uint32_t end_prop = cpu_to_be32(pnv->initrd_base + pnv->initrd_size);

        _FDT((fdt_setprop(fdt, off, "linux,initrd-start",
                               &start_prop, sizeof(start_prop))));
        _FDT((fdt_setprop(fdt, off, "linux,initrd-end",
                               &end_prop, sizeof(end_prop))));
    }

    /* Populate device tree for each chip */
    for (i = 0; i < pnv->num_chips; i++) {
        PNV_CHIP_GET_CLASS(pnv->chips[i])->dt_populate(pnv->chips[i], fdt);
    }

    /* Populate ISA devices on chip 0 */
    pnv_dt_isa(pnv, fdt);

    if (pnv->bmc) {
        pnv_dt_bmc_sensors(pnv->bmc, fdt);
    }

    /* Create an extra node for power management on machines that support it */
    if (pmc->dt_power_mgt) {
        pmc->dt_power_mgt(pnv, fdt);
    }

    return fdt;
}

static void pnv_powerdown_notify(Notifier *n, void *opaque)
{
    PnvMachineState *pnv = container_of(n, PnvMachineState, powerdown_notifier);

    if (pnv->bmc) {
        pnv_bmc_powerdown(pnv->bmc);
    }
}

static void pnv_reset(MachineState *machine)
{
    PnvMachineState *pnv = PNV_MACHINE(machine);
    IPMIBmc *bmc;
    void *fdt;

    qemu_devices_reset();

    /*
     * The machine should provide by default an internal BMC simulator.
     * If not, try to use the BMC device that was provided on the command
     * line.
     */
    bmc = pnv_bmc_find(&error_fatal);
    if (!pnv->bmc) {
        if (!bmc) {
            if (!qtest_enabled()) {
                warn_report("machine has no BMC device. Use '-device "
                            "ipmi-bmc-sim,id=bmc0 -device isa-ipmi-bt,bmc=bmc0,irq=10' "
                            "to define one");
            }
        } else {
            pnv_bmc_set_pnor(bmc, pnv->pnor);
            pnv->bmc = bmc;
        }
    }

    fdt = pnv_dt_create(machine);

    /* Pack resulting tree */
    _FDT((fdt_pack(fdt)));

    qemu_fdt_dumpdtb(fdt, fdt_totalsize(fdt));
    cpu_physical_memory_write(PNV_FDT_ADDR, fdt, fdt_totalsize(fdt));

    g_free(fdt);
}

static ISABus *pnv_chip_power8_isa_create(PnvChip *chip, Error **errp)
{
    Pnv8Chip *chip8 = PNV8_CHIP(chip);
    return pnv_lpc_isa_create(&chip8->lpc, true, errp);
}

static ISABus *pnv_chip_power8nvl_isa_create(PnvChip *chip, Error **errp)
{
    Pnv8Chip *chip8 = PNV8_CHIP(chip);
    return pnv_lpc_isa_create(&chip8->lpc, false, errp);
}

static ISABus *pnv_chip_power9_isa_create(PnvChip *chip, Error **errp)
{
    Pnv9Chip *chip9 = PNV9_CHIP(chip);
    return pnv_lpc_isa_create(&chip9->lpc, false, errp);
}

static ISABus *pnv_chip_power10_isa_create(PnvChip *chip, Error **errp)
{
    Pnv10Chip *chip10 = PNV10_CHIP(chip);
    return pnv_lpc_isa_create(&chip10->lpc, false, errp);
}

static ISABus *pnv_isa_create(PnvChip *chip, Error **errp)
{
    return PNV_CHIP_GET_CLASS(chip)->isa_create(chip, errp);
}

static void pnv_chip_power8_pic_print_info(PnvChip *chip, Monitor *mon)
{
    Pnv8Chip *chip8 = PNV8_CHIP(chip);
    int i;

    ics_pic_print_info(&chip8->psi.ics, mon);
    for (i = 0; i < chip->num_phbs; i++) {
        pnv_phb3_msi_pic_print_info(&chip8->phbs[i].msis, mon);
        ics_pic_print_info(&chip8->phbs[i].lsis, mon);
    }
}

static void pnv_chip_power9_pic_print_info(PnvChip *chip, Monitor *mon)
{
    Pnv9Chip *chip9 = PNV9_CHIP(chip);
    int i, j;

    pnv_xive_pic_print_info(&chip9->xive, mon);
    pnv_psi_pic_print_info(&chip9->psi, mon);

    for (i = 0; i < PNV9_CHIP_MAX_PEC; i++) {
        PnvPhb4PecState *pec = &chip9->pecs[i];
        for (j = 0; j < pec->num_stacks; j++) {
            pnv_phb4_pic_print_info(&pec->stacks[j].phb, mon);
        }
    }
}

static uint64_t pnv_chip_power8_xscom_core_base(PnvChip *chip,
                                                uint32_t core_id)
{
    return PNV_XSCOM_EX_BASE(core_id);
}

static uint64_t pnv_chip_power9_xscom_core_base(PnvChip *chip,
                                                uint32_t core_id)
{
    return PNV9_XSCOM_EC_BASE(core_id);
}

static uint64_t pnv_chip_power10_xscom_core_base(PnvChip *chip,
                                                 uint32_t core_id)
{
    return PNV10_XSCOM_EC_BASE(core_id);
}

static bool pnv_match_cpu(const char *default_type, const char *cpu_type)
{
    PowerPCCPUClass *ppc_default =
        POWERPC_CPU_CLASS(object_class_by_name(default_type));
    PowerPCCPUClass *ppc =
        POWERPC_CPU_CLASS(object_class_by_name(cpu_type));

    return ppc_default->pvr_match(ppc_default, ppc->pvr);
}

static void pnv_ipmi_bt_init(ISABus *bus, IPMIBmc *bmc, uint32_t irq)
{
    ISADevice *dev = isa_new("isa-ipmi-bt");

    object_property_set_link(OBJECT(dev), "bmc", OBJECT(bmc), &error_fatal);
    object_property_set_int(OBJECT(dev), "irq", irq, &error_fatal);
    isa_realize_and_unref(dev, bus, &error_fatal);
}

static void pnv_chip_power10_pic_print_info(PnvChip *chip, Monitor *mon)
{
    Pnv10Chip *chip10 = PNV10_CHIP(chip);

    pnv_psi_pic_print_info(&chip10->psi, mon);
}

static void pnv_init(MachineState *machine)
{
    PnvMachineState *pnv = PNV_MACHINE(machine);
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    char *fw_filename;
    long fw_size;
    int i;
    char *chip_typename;
    DriveInfo *pnor = drive_get(IF_MTD, 0, 0);
    DeviceState *dev;

    /* allocate RAM */
    if (machine->ram_size < (1 * GiB)) {
        warn_report("skiboot may not work with < 1GB of RAM");
    }
    memory_region_add_subregion(get_system_memory(), 0, machine->ram);

    /*
     * Create our simple PNOR device
     */
    dev = qdev_new(TYPE_PNV_PNOR);
    if (pnor) {
        qdev_prop_set_drive(dev, "drive", blk_by_legacy_dinfo(pnor));
    }
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    pnv->pnor = PNV_PNOR(dev);

    /* load skiboot firmware  */
    if (bios_name == NULL) {
        bios_name = FW_FILE_NAME;
    }

    fw_filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
    if (!fw_filename) {
        error_report("Could not find OPAL firmware '%s'", bios_name);
        exit(1);
    }

    fw_size = load_image_targphys(fw_filename, pnv->fw_load_addr, FW_MAX_SIZE);
    if (fw_size < 0) {
        error_report("Could not load OPAL firmware '%s'", fw_filename);
        exit(1);
    }
    g_free(fw_filename);

    /* load kernel */
    if (machine->kernel_filename) {
        long kernel_size;

        kernel_size = load_image_targphys(machine->kernel_filename,
                                          KERNEL_LOAD_ADDR, KERNEL_MAX_SIZE);
        if (kernel_size < 0) {
            error_report("Could not load kernel '%s'",
                         machine->kernel_filename);
            exit(1);
        }
    }

    /* load initrd */
    if (machine->initrd_filename) {
        pnv->initrd_base = INITRD_LOAD_ADDR;
        pnv->initrd_size = load_image_targphys(machine->initrd_filename,
                                  pnv->initrd_base, INITRD_MAX_SIZE);
        if (pnv->initrd_size < 0) {
            error_report("Could not load initial ram disk '%s'",
                         machine->initrd_filename);
            exit(1);
        }
    }

    /* MSIs are supported on this platform */
    msi_nonbroken = true;

    /*
     * Check compatibility of the specified CPU with the machine
     * default.
     */
    if (!pnv_match_cpu(mc->default_cpu_type, machine->cpu_type)) {
        error_report("invalid CPU model '%s' for %s machine",
                     machine->cpu_type, mc->name);
        exit(1);
    }

    /* Create the processor chips */
    i = strlen(machine->cpu_type) - strlen(POWERPC_CPU_TYPE_SUFFIX);
    chip_typename = g_strdup_printf(PNV_CHIP_TYPE_NAME("%.*s"),
                                    i, machine->cpu_type);
    if (!object_class_by_name(chip_typename)) {
        error_report("invalid chip model '%.*s' for %s machine",
                     i, machine->cpu_type, mc->name);
        exit(1);
    }

    pnv->num_chips =
        machine->smp.max_cpus / (machine->smp.cores * machine->smp.threads);
    /*
     * TODO: should we decide on how many chips we can create based
     * on #cores and Venice vs. Murano vs. Naples chip type etc...,
     */
    if (!is_power_of_2(pnv->num_chips) || pnv->num_chips > 4) {
        error_report("invalid number of chips: '%d'", pnv->num_chips);
        error_printf("Try '-smp sockets=N'. Valid values are : 1, 2 or 4.\n");
        exit(1);
    }

    pnv->chips = g_new0(PnvChip *, pnv->num_chips);
    for (i = 0; i < pnv->num_chips; i++) {
        char chip_name[32];
        Object *chip = OBJECT(qdev_new(chip_typename));

        pnv->chips[i] = PNV_CHIP(chip);

        /*
         * TODO: put all the memory in one node on chip 0 until we find a
         * way to specify different ranges for each chip
         */
        if (i == 0) {
            object_property_set_int(chip, "ram-size", machine->ram_size,
                                    &error_fatal);
        }

        snprintf(chip_name, sizeof(chip_name), "chip[%d]", PNV_CHIP_HWID(i));
        object_property_add_child(OBJECT(pnv), chip_name, chip);
        object_property_set_int(chip, "chip-id", PNV_CHIP_HWID(i),
                                &error_fatal);
        object_property_set_int(chip, "nr-cores", machine->smp.cores,
                                &error_fatal);
        object_property_set_int(chip, "nr-threads", machine->smp.threads,
                                &error_fatal);
        /*
         * The POWER8 machine use the XICS interrupt interface.
         * Propagate the XICS fabric to the chip and its controllers.
         */
        if (object_dynamic_cast(OBJECT(pnv), TYPE_XICS_FABRIC)) {
            object_property_set_link(chip, "xics", OBJECT(pnv), &error_abort);
        }
        if (object_dynamic_cast(OBJECT(pnv), TYPE_XIVE_FABRIC)) {
            object_property_set_link(chip, "xive-fabric", OBJECT(pnv),
                                     &error_abort);
        }
        sysbus_realize_and_unref(SYS_BUS_DEVICE(chip), &error_fatal);
    }
    g_free(chip_typename);

    /* Instantiate ISA bus on chip 0 */
    pnv->isa_bus = pnv_isa_create(pnv->chips[0], &error_fatal);

    /* Create serial port */
    serial_hds_isa_init(pnv->isa_bus, 0, MAX_ISA_SERIAL_PORTS);

    /* Create an RTC ISA device too */
    mc146818_rtc_init(pnv->isa_bus, 2000, NULL);

    /*
     * Create the machine BMC simulator and the IPMI BT device for
     * communication with the BMC
     */
    if (defaults_enabled()) {
        pnv->bmc = pnv_bmc_create(pnv->pnor);
        pnv_ipmi_bt_init(pnv->isa_bus, pnv->bmc, 10);
    }

    /*
     * OpenPOWER systems use a IPMI SEL Event message to notify the
     * host to powerdown
     */
    pnv->powerdown_notifier.notify = pnv_powerdown_notify;
    qemu_register_powerdown_notifier(&pnv->powerdown_notifier);
}

/*
 *    0:21  Reserved - Read as zeros
 *   22:24  Chip ID
 *   25:28  Core number
 *   29:31  Thread ID
 */
static uint32_t pnv_chip_core_pir_p8(PnvChip *chip, uint32_t core_id)
{
    return (chip->chip_id << 7) | (core_id << 3);
}

static void pnv_chip_power8_intc_create(PnvChip *chip, PowerPCCPU *cpu,
                                        Error **errp)
{
    Pnv8Chip *chip8 = PNV8_CHIP(chip);
    Error *local_err = NULL;
    Object *obj;
    PnvCPUState *pnv_cpu = pnv_cpu_state(cpu);

    obj = icp_create(OBJECT(cpu), TYPE_PNV_ICP, chip8->xics, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    pnv_cpu->intc = obj;
}


static void pnv_chip_power8_intc_reset(PnvChip *chip, PowerPCCPU *cpu)
{
    PnvCPUState *pnv_cpu = pnv_cpu_state(cpu);

    icp_reset(ICP(pnv_cpu->intc));
}

static void pnv_chip_power8_intc_destroy(PnvChip *chip, PowerPCCPU *cpu)
{
    PnvCPUState *pnv_cpu = pnv_cpu_state(cpu);

    icp_destroy(ICP(pnv_cpu->intc));
    pnv_cpu->intc = NULL;
}

static void pnv_chip_power8_intc_print_info(PnvChip *chip, PowerPCCPU *cpu,
                                            Monitor *mon)
{
    icp_pic_print_info(ICP(pnv_cpu_state(cpu)->intc), mon);
}

/*
 *    0:48  Reserved - Read as zeroes
 *   49:52  Node ID
 *   53:55  Chip ID
 *   56     Reserved - Read as zero
 *   57:61  Core number
 *   62:63  Thread ID
 *
 * We only care about the lower bits. uint32_t is fine for the moment.
 */
static uint32_t pnv_chip_core_pir_p9(PnvChip *chip, uint32_t core_id)
{
    return (chip->chip_id << 8) | (core_id << 2);
}

static uint32_t pnv_chip_core_pir_p10(PnvChip *chip, uint32_t core_id)
{
    return (chip->chip_id << 8) | (core_id << 2);
}

static void pnv_chip_power9_intc_create(PnvChip *chip, PowerPCCPU *cpu,
                                        Error **errp)
{
    Pnv9Chip *chip9 = PNV9_CHIP(chip);
    Error *local_err = NULL;
    Object *obj;
    PnvCPUState *pnv_cpu = pnv_cpu_state(cpu);

    /*
     * The core creates its interrupt presenter but the XIVE interrupt
     * controller object is initialized afterwards. Hopefully, it's
     * only used at runtime.
     */
    obj = xive_tctx_create(OBJECT(cpu), XIVE_PRESENTER(&chip9->xive),
                           &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    pnv_cpu->intc = obj;
}

static void pnv_chip_power9_intc_reset(PnvChip *chip, PowerPCCPU *cpu)
{
    PnvCPUState *pnv_cpu = pnv_cpu_state(cpu);

    xive_tctx_reset(XIVE_TCTX(pnv_cpu->intc));
}

static void pnv_chip_power9_intc_destroy(PnvChip *chip, PowerPCCPU *cpu)
{
    PnvCPUState *pnv_cpu = pnv_cpu_state(cpu);

    xive_tctx_destroy(XIVE_TCTX(pnv_cpu->intc));
    pnv_cpu->intc = NULL;
}

static void pnv_chip_power9_intc_print_info(PnvChip *chip, PowerPCCPU *cpu,
                                            Monitor *mon)
{
    xive_tctx_pic_print_info(XIVE_TCTX(pnv_cpu_state(cpu)->intc), mon);
}

static void pnv_chip_power10_intc_create(PnvChip *chip, PowerPCCPU *cpu,
                                        Error **errp)
{
    PnvCPUState *pnv_cpu = pnv_cpu_state(cpu);

    /* Will be defined when the interrupt controller is */
    pnv_cpu->intc = NULL;
}

static void pnv_chip_power10_intc_reset(PnvChip *chip, PowerPCCPU *cpu)
{
    ;
}

static void pnv_chip_power10_intc_destroy(PnvChip *chip, PowerPCCPU *cpu)
{
    PnvCPUState *pnv_cpu = pnv_cpu_state(cpu);

    pnv_cpu->intc = NULL;
}

static void pnv_chip_power10_intc_print_info(PnvChip *chip, PowerPCCPU *cpu,
                                             Monitor *mon)
{
}

/*
 * Allowed core identifiers on a POWER8 Processor Chip :
 *
 * <EX0 reserved>
 *  EX1  - Venice only
 *  EX2  - Venice only
 *  EX3  - Venice only
 *  EX4
 *  EX5
 *  EX6
 * <EX7,8 reserved> <reserved>
 *  EX9  - Venice only
 *  EX10 - Venice only
 *  EX11 - Venice only
 *  EX12
 *  EX13
 *  EX14
 * <EX15 reserved>
 */
#define POWER8E_CORE_MASK  (0x7070ull)
#define POWER8_CORE_MASK   (0x7e7eull)

/*
 * POWER9 has 24 cores, ids starting at 0x0
 */
#define POWER9_CORE_MASK   (0xffffffffffffffull)


#define POWER10_CORE_MASK  (0xffffffffffffffull)

static void pnv_chip_power8_instance_init(Object *obj)
{
    PnvChip *chip = PNV_CHIP(obj);
    Pnv8Chip *chip8 = PNV8_CHIP(obj);
    PnvChipClass *pcc = PNV_CHIP_GET_CLASS(obj);
    int i;

    object_property_add_link(obj, "xics", TYPE_XICS_FABRIC,
                             (Object **)&chip8->xics,
                             object_property_allow_set_link,
                             OBJ_PROP_LINK_STRONG);

    object_initialize_child(obj, "psi", &chip8->psi, TYPE_PNV8_PSI);

    object_initialize_child(obj, "lpc", &chip8->lpc, TYPE_PNV8_LPC);

    object_initialize_child(obj, "occ", &chip8->occ, TYPE_PNV8_OCC);

    object_initialize_child(obj, "homer", &chip8->homer, TYPE_PNV8_HOMER);

    for (i = 0; i < pcc->num_phbs; i++) {
        object_initialize_child(obj, "phb[*]", &chip8->phbs[i], TYPE_PNV_PHB3);
    }

    /*
     * Number of PHBs is the chip default
     */
    chip->num_phbs = pcc->num_phbs;
}

static void pnv_chip_icp_realize(Pnv8Chip *chip8, Error **errp)
 {
    PnvChip *chip = PNV_CHIP(chip8);
    PnvChipClass *pcc = PNV_CHIP_GET_CLASS(chip);
    int i, j;
    char *name;

    name = g_strdup_printf("icp-%x", chip->chip_id);
    memory_region_init(&chip8->icp_mmio, OBJECT(chip), name, PNV_ICP_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(chip), &chip8->icp_mmio);
    g_free(name);

    sysbus_mmio_map(SYS_BUS_DEVICE(chip), 1, PNV_ICP_BASE(chip));

    /* Map the ICP registers for each thread */
    for (i = 0; i < chip->nr_cores; i++) {
        PnvCore *pnv_core = chip->cores[i];
        int core_hwid = CPU_CORE(pnv_core)->core_id;

        for (j = 0; j < CPU_CORE(pnv_core)->nr_threads; j++) {
            uint32_t pir = pcc->core_pir(chip, core_hwid) + j;
            PnvICPState *icp = PNV_ICP(xics_icp_get(chip8->xics, pir));

            memory_region_add_subregion(&chip8->icp_mmio, pir << 12,
                                        &icp->mmio);
        }
    }
}

static void pnv_chip_power8_realize(DeviceState *dev, Error **errp)
{
    PnvChipClass *pcc = PNV_CHIP_GET_CLASS(dev);
    PnvChip *chip = PNV_CHIP(dev);
    Pnv8Chip *chip8 = PNV8_CHIP(dev);
    Pnv8Psi *psi8 = &chip8->psi;
    Error *local_err = NULL;
    int i;

    assert(chip8->xics);

    /* XSCOM bridge is first */
    pnv_xscom_realize(chip, PNV_XSCOM_SIZE, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(chip), 0, PNV_XSCOM_BASE(chip));

    pcc->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    /* Processor Service Interface (PSI) Host Bridge */
    object_property_set_int(OBJECT(&chip8->psi), "bar", PNV_PSIHB_BASE(chip),
                            &error_fatal);
    object_property_set_link(OBJECT(&chip8->psi), ICS_PROP_XICS,
                             OBJECT(chip8->xics), &error_abort);
    if (!qdev_realize(DEVICE(&chip8->psi), NULL, errp)) {
        return;
    }
    pnv_xscom_add_subregion(chip, PNV_XSCOM_PSIHB_BASE,
                            &PNV_PSI(psi8)->xscom_regs);

    /* Create LPC controller */
    object_property_set_link(OBJECT(&chip8->lpc), "psi", OBJECT(&chip8->psi),
                             &error_abort);
    qdev_realize(DEVICE(&chip8->lpc), NULL, &error_fatal);
    pnv_xscom_add_subregion(chip, PNV_XSCOM_LPC_BASE, &chip8->lpc.xscom_regs);

    chip->dt_isa_nodename = g_strdup_printf("/xscom@%" PRIx64 "/isa@%x",
                                            (uint64_t) PNV_XSCOM_BASE(chip),
                                            PNV_XSCOM_LPC_BASE);

    /*
     * Interrupt Management Area. This is the memory region holding
     * all the Interrupt Control Presenter (ICP) registers
     */
    pnv_chip_icp_realize(chip8, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    /* Create the simplified OCC model */
    object_property_set_link(OBJECT(&chip8->occ), "psi", OBJECT(&chip8->psi),
                             &error_abort);
    if (!qdev_realize(DEVICE(&chip8->occ), NULL, errp)) {
        return;
    }
    pnv_xscom_add_subregion(chip, PNV_XSCOM_OCC_BASE, &chip8->occ.xscom_regs);

    /* OCC SRAM model */
    memory_region_add_subregion(get_system_memory(), PNV_OCC_SENSOR_BASE(chip),
                                &chip8->occ.sram_regs);

    /* HOMER */
    object_property_set_link(OBJECT(&chip8->homer), "chip", OBJECT(chip),
                             &error_abort);
    if (!qdev_realize(DEVICE(&chip8->homer), NULL, errp)) {
        return;
    }
    /* Homer Xscom region */
    pnv_xscom_add_subregion(chip, PNV_XSCOM_PBA_BASE, &chip8->homer.pba_regs);

    /* Homer mmio region */
    memory_region_add_subregion(get_system_memory(), PNV_HOMER_BASE(chip),
                                &chip8->homer.regs);

    /* PHB3 controllers */
    for (i = 0; i < chip->num_phbs; i++) {
        PnvPHB3 *phb = &chip8->phbs[i];
        PnvPBCQState *pbcq = &phb->pbcq;

        object_property_set_int(OBJECT(phb), "index", i, &error_fatal);
        object_property_set_int(OBJECT(phb), "chip-id", chip->chip_id,
                                &error_fatal);
        if (!sysbus_realize(SYS_BUS_DEVICE(phb), errp)) {
            return;
        }

        /* Populate the XSCOM address space. */
        pnv_xscom_add_subregion(chip,
                                PNV_XSCOM_PBCQ_NEST_BASE + 0x400 * phb->phb_id,
                                &pbcq->xscom_nest_regs);
        pnv_xscom_add_subregion(chip,
                                PNV_XSCOM_PBCQ_PCI_BASE + 0x400 * phb->phb_id,
                                &pbcq->xscom_pci_regs);
        pnv_xscom_add_subregion(chip,
                                PNV_XSCOM_PBCQ_SPCI_BASE + 0x040 * phb->phb_id,
                                &pbcq->xscom_spci_regs);
    }
}

static uint32_t pnv_chip_power8_xscom_pcba(PnvChip *chip, uint64_t addr)
{
    addr &= (PNV_XSCOM_SIZE - 1);
    return ((addr >> 4) & ~0xfull) | ((addr >> 3) & 0xf);
}

static void pnv_chip_power8e_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvChipClass *k = PNV_CHIP_CLASS(klass);

    k->chip_cfam_id = 0x221ef04980000000ull;  /* P8 Murano DD2.1 */
    k->cores_mask = POWER8E_CORE_MASK;
    k->num_phbs = 3;
    k->core_pir = pnv_chip_core_pir_p8;
    k->intc_create = pnv_chip_power8_intc_create;
    k->intc_reset = pnv_chip_power8_intc_reset;
    k->intc_destroy = pnv_chip_power8_intc_destroy;
    k->intc_print_info = pnv_chip_power8_intc_print_info;
    k->isa_create = pnv_chip_power8_isa_create;
    k->dt_populate = pnv_chip_power8_dt_populate;
    k->pic_print_info = pnv_chip_power8_pic_print_info;
    k->xscom_core_base = pnv_chip_power8_xscom_core_base;
    k->xscom_pcba = pnv_chip_power8_xscom_pcba;
    dc->desc = "PowerNV Chip POWER8E";

    device_class_set_parent_realize(dc, pnv_chip_power8_realize,
                                    &k->parent_realize);
}

static void pnv_chip_power8_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvChipClass *k = PNV_CHIP_CLASS(klass);

    k->chip_cfam_id = 0x220ea04980000000ull; /* P8 Venice DD2.0 */
    k->cores_mask = POWER8_CORE_MASK;
    k->num_phbs = 3;
    k->core_pir = pnv_chip_core_pir_p8;
    k->intc_create = pnv_chip_power8_intc_create;
    k->intc_reset = pnv_chip_power8_intc_reset;
    k->intc_destroy = pnv_chip_power8_intc_destroy;
    k->intc_print_info = pnv_chip_power8_intc_print_info;
    k->isa_create = pnv_chip_power8_isa_create;
    k->dt_populate = pnv_chip_power8_dt_populate;
    k->pic_print_info = pnv_chip_power8_pic_print_info;
    k->xscom_core_base = pnv_chip_power8_xscom_core_base;
    k->xscom_pcba = pnv_chip_power8_xscom_pcba;
    dc->desc = "PowerNV Chip POWER8";

    device_class_set_parent_realize(dc, pnv_chip_power8_realize,
                                    &k->parent_realize);
}

static void pnv_chip_power8nvl_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvChipClass *k = PNV_CHIP_CLASS(klass);

    k->chip_cfam_id = 0x120d304980000000ull;  /* P8 Naples DD1.0 */
    k->cores_mask = POWER8_CORE_MASK;
    k->num_phbs = 3;
    k->core_pir = pnv_chip_core_pir_p8;
    k->intc_create = pnv_chip_power8_intc_create;
    k->intc_reset = pnv_chip_power8_intc_reset;
    k->intc_destroy = pnv_chip_power8_intc_destroy;
    k->intc_print_info = pnv_chip_power8_intc_print_info;
    k->isa_create = pnv_chip_power8nvl_isa_create;
    k->dt_populate = pnv_chip_power8_dt_populate;
    k->pic_print_info = pnv_chip_power8_pic_print_info;
    k->xscom_core_base = pnv_chip_power8_xscom_core_base;
    k->xscom_pcba = pnv_chip_power8_xscom_pcba;
    dc->desc = "PowerNV Chip POWER8NVL";

    device_class_set_parent_realize(dc, pnv_chip_power8_realize,
                                    &k->parent_realize);
}

static void pnv_chip_power9_instance_init(Object *obj)
{
    PnvChip *chip = PNV_CHIP(obj);
    Pnv9Chip *chip9 = PNV9_CHIP(obj);
    PnvChipClass *pcc = PNV_CHIP_GET_CLASS(obj);
    int i;

    object_initialize_child(obj, "xive", &chip9->xive, TYPE_PNV_XIVE);
    object_property_add_alias(obj, "xive-fabric", OBJECT(&chip9->xive),
                              "xive-fabric");

    object_initialize_child(obj, "psi", &chip9->psi, TYPE_PNV9_PSI);

    object_initialize_child(obj, "lpc", &chip9->lpc, TYPE_PNV9_LPC);

    object_initialize_child(obj, "occ", &chip9->occ, TYPE_PNV9_OCC);

    object_initialize_child(obj, "homer", &chip9->homer, TYPE_PNV9_HOMER);

    for (i = 0; i < PNV9_CHIP_MAX_PEC; i++) {
        object_initialize_child(obj, "pec[*]", &chip9->pecs[i],
                                TYPE_PNV_PHB4_PEC);
    }

    /*
     * Number of PHBs is the chip default
     */
    chip->num_phbs = pcc->num_phbs;
}

static void pnv_chip_quad_realize(Pnv9Chip *chip9, Error **errp)
{
    PnvChip *chip = PNV_CHIP(chip9);
    int i;

    chip9->nr_quads = DIV_ROUND_UP(chip->nr_cores, 4);
    chip9->quads = g_new0(PnvQuad, chip9->nr_quads);

    for (i = 0; i < chip9->nr_quads; i++) {
        char eq_name[32];
        PnvQuad *eq = &chip9->quads[i];
        PnvCore *pnv_core = chip->cores[i * 4];
        int core_id = CPU_CORE(pnv_core)->core_id;

        snprintf(eq_name, sizeof(eq_name), "eq[%d]", core_id);
        object_initialize_child_with_props(OBJECT(chip), eq_name, eq,
                                           sizeof(*eq), TYPE_PNV_QUAD,
                                           &error_fatal, NULL);

        object_property_set_int(OBJECT(eq), "id", core_id, &error_fatal);
        qdev_realize(DEVICE(eq), NULL, &error_fatal);

        pnv_xscom_add_subregion(chip, PNV9_XSCOM_EQ_BASE(eq->id),
                                &eq->xscom_regs);
    }
}

static void pnv_chip_power9_phb_realize(PnvChip *chip, Error **errp)
{
    Pnv9Chip *chip9 = PNV9_CHIP(chip);
    int i, j;
    int phb_id = 0;

    for (i = 0; i < PNV9_CHIP_MAX_PEC; i++) {
        PnvPhb4PecState *pec = &chip9->pecs[i];
        PnvPhb4PecClass *pecc = PNV_PHB4_PEC_GET_CLASS(pec);
        uint32_t pec_nest_base;
        uint32_t pec_pci_base;

        object_property_set_int(OBJECT(pec), "index", i, &error_fatal);
        /*
         * PEC0 -> 1 stack
         * PEC1 -> 2 stacks
         * PEC2 -> 3 stacks
         */
        object_property_set_int(OBJECT(pec), "num-stacks", i + 1,
                                &error_fatal);
        object_property_set_int(OBJECT(pec), "chip-id", chip->chip_id,
                                &error_fatal);
        object_property_set_link(OBJECT(pec), "system-memory",
                                 OBJECT(get_system_memory()), &error_abort);
        if (!qdev_realize(DEVICE(pec), NULL, errp)) {
            return;
        }

        pec_nest_base = pecc->xscom_nest_base(pec);
        pec_pci_base = pecc->xscom_pci_base(pec);

        pnv_xscom_add_subregion(chip, pec_nest_base, &pec->nest_regs_mr);
        pnv_xscom_add_subregion(chip, pec_pci_base, &pec->pci_regs_mr);

        for (j = 0; j < pec->num_stacks && phb_id < chip->num_phbs;
             j++, phb_id++) {
            PnvPhb4PecStack *stack = &pec->stacks[j];
            Object *obj = OBJECT(&stack->phb);

            object_property_set_int(obj, "index", phb_id, &error_fatal);
            object_property_set_int(obj, "chip-id", chip->chip_id,
                                    &error_fatal);
            object_property_set_int(obj, "version", PNV_PHB4_VERSION,
                                    &error_fatal);
            object_property_set_int(obj, "device-id", PNV_PHB4_DEVICE_ID,
                                    &error_fatal);
            object_property_set_link(obj, "stack", OBJECT(stack),
                                     &error_abort);
            if (!sysbus_realize(SYS_BUS_DEVICE(obj), errp)) {
                return;
            }

            /* Populate the XSCOM address space. */
            pnv_xscom_add_subregion(chip,
                                   pec_nest_base + 0x40 * (stack->stack_no + 1),
                                   &stack->nest_regs_mr);
            pnv_xscom_add_subregion(chip,
                                    pec_pci_base + 0x40 * (stack->stack_no + 1),
                                    &stack->pci_regs_mr);
            pnv_xscom_add_subregion(chip,
                                    pec_pci_base + PNV9_XSCOM_PEC_PCI_STK0 +
                                    0x40 * stack->stack_no,
                                    &stack->phb_regs_mr);
        }
    }
}

static void pnv_chip_power9_realize(DeviceState *dev, Error **errp)
{
    PnvChipClass *pcc = PNV_CHIP_GET_CLASS(dev);
    Pnv9Chip *chip9 = PNV9_CHIP(dev);
    PnvChip *chip = PNV_CHIP(dev);
    Pnv9Psi *psi9 = &chip9->psi;
    Error *local_err = NULL;

    /* XSCOM bridge is first */
    pnv_xscom_realize(chip, PNV9_XSCOM_SIZE, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(chip), 0, PNV9_XSCOM_BASE(chip));

    pcc->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    pnv_chip_quad_realize(chip9, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    /* XIVE interrupt controller (POWER9) */
    object_property_set_int(OBJECT(&chip9->xive), "ic-bar",
                            PNV9_XIVE_IC_BASE(chip), &error_fatal);
    object_property_set_int(OBJECT(&chip9->xive), "vc-bar",
                            PNV9_XIVE_VC_BASE(chip), &error_fatal);
    object_property_set_int(OBJECT(&chip9->xive), "pc-bar",
                            PNV9_XIVE_PC_BASE(chip), &error_fatal);
    object_property_set_int(OBJECT(&chip9->xive), "tm-bar",
                            PNV9_XIVE_TM_BASE(chip), &error_fatal);
    object_property_set_link(OBJECT(&chip9->xive), "chip", OBJECT(chip),
                             &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&chip9->xive), errp)) {
        return;
    }
    pnv_xscom_add_subregion(chip, PNV9_XSCOM_XIVE_BASE,
                            &chip9->xive.xscom_regs);

    /* Processor Service Interface (PSI) Host Bridge */
    object_property_set_int(OBJECT(&chip9->psi), "bar", PNV9_PSIHB_BASE(chip),
                            &error_fatal);
    if (!qdev_realize(DEVICE(&chip9->psi), NULL, errp)) {
        return;
    }
    pnv_xscom_add_subregion(chip, PNV9_XSCOM_PSIHB_BASE,
                            &PNV_PSI(psi9)->xscom_regs);

    /* LPC */
    object_property_set_link(OBJECT(&chip9->lpc), "psi", OBJECT(&chip9->psi),
                             &error_abort);
    if (!qdev_realize(DEVICE(&chip9->lpc), NULL, errp)) {
        return;
    }
    memory_region_add_subregion(get_system_memory(), PNV9_LPCM_BASE(chip),
                                &chip9->lpc.xscom_regs);

    chip->dt_isa_nodename = g_strdup_printf("/lpcm-opb@%" PRIx64 "/lpc@0",
                                            (uint64_t) PNV9_LPCM_BASE(chip));

    /* Create the simplified OCC model */
    object_property_set_link(OBJECT(&chip9->occ), "psi", OBJECT(&chip9->psi),
                             &error_abort);
    if (!qdev_realize(DEVICE(&chip9->occ), NULL, errp)) {
        return;
    }
    pnv_xscom_add_subregion(chip, PNV9_XSCOM_OCC_BASE, &chip9->occ.xscom_regs);

    /* OCC SRAM model */
    memory_region_add_subregion(get_system_memory(), PNV9_OCC_SENSOR_BASE(chip),
                                &chip9->occ.sram_regs);

    /* HOMER */
    object_property_set_link(OBJECT(&chip9->homer), "chip", OBJECT(chip),
                             &error_abort);
    if (!qdev_realize(DEVICE(&chip9->homer), NULL, errp)) {
        return;
    }
    /* Homer Xscom region */
    pnv_xscom_add_subregion(chip, PNV9_XSCOM_PBA_BASE, &chip9->homer.pba_regs);

    /* Homer mmio region */
    memory_region_add_subregion(get_system_memory(), PNV9_HOMER_BASE(chip),
                                &chip9->homer.regs);

    /* PHBs */
    pnv_chip_power9_phb_realize(chip, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
}

static uint32_t pnv_chip_power9_xscom_pcba(PnvChip *chip, uint64_t addr)
{
    addr &= (PNV9_XSCOM_SIZE - 1);
    return addr >> 3;
}

static void pnv_chip_power9_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvChipClass *k = PNV_CHIP_CLASS(klass);

    k->chip_cfam_id = 0x220d104900008000ull; /* P9 Nimbus DD2.0 */
    k->cores_mask = POWER9_CORE_MASK;
    k->core_pir = pnv_chip_core_pir_p9;
    k->intc_create = pnv_chip_power9_intc_create;
    k->intc_reset = pnv_chip_power9_intc_reset;
    k->intc_destroy = pnv_chip_power9_intc_destroy;
    k->intc_print_info = pnv_chip_power9_intc_print_info;
    k->isa_create = pnv_chip_power9_isa_create;
    k->dt_populate = pnv_chip_power9_dt_populate;
    k->pic_print_info = pnv_chip_power9_pic_print_info;
    k->xscom_core_base = pnv_chip_power9_xscom_core_base;
    k->xscom_pcba = pnv_chip_power9_xscom_pcba;
    dc->desc = "PowerNV Chip POWER9";
    k->num_phbs = 6;

    device_class_set_parent_realize(dc, pnv_chip_power9_realize,
                                    &k->parent_realize);
}

static void pnv_chip_power10_instance_init(Object *obj)
{
    Pnv10Chip *chip10 = PNV10_CHIP(obj);

    object_initialize_child(obj, "psi", &chip10->psi, TYPE_PNV10_PSI);
    object_initialize_child(obj, "lpc", &chip10->lpc, TYPE_PNV10_LPC);
}

static void pnv_chip_power10_realize(DeviceState *dev, Error **errp)
{
    PnvChipClass *pcc = PNV_CHIP_GET_CLASS(dev);
    PnvChip *chip = PNV_CHIP(dev);
    Pnv10Chip *chip10 = PNV10_CHIP(dev);
    Error *local_err = NULL;

    /* XSCOM bridge is first */
    pnv_xscom_realize(chip, PNV10_XSCOM_SIZE, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(chip), 0, PNV10_XSCOM_BASE(chip));

    pcc->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    /* Processor Service Interface (PSI) Host Bridge */
    object_property_set_int(OBJECT(&chip10->psi), "bar",
                            PNV10_PSIHB_BASE(chip), &error_fatal);
    if (!qdev_realize(DEVICE(&chip10->psi), NULL, errp)) {
        return;
    }
    pnv_xscom_add_subregion(chip, PNV10_XSCOM_PSIHB_BASE,
                            &PNV_PSI(&chip10->psi)->xscom_regs);

    /* LPC */
    object_property_set_link(OBJECT(&chip10->lpc), "psi",
                             OBJECT(&chip10->psi), &error_abort);
    if (!qdev_realize(DEVICE(&chip10->lpc), NULL, errp)) {
        return;
    }
    memory_region_add_subregion(get_system_memory(), PNV10_LPCM_BASE(chip),
                                &chip10->lpc.xscom_regs);

    chip->dt_isa_nodename = g_strdup_printf("/lpcm-opb@%" PRIx64 "/lpc@0",
                                            (uint64_t) PNV10_LPCM_BASE(chip));
}

static uint32_t pnv_chip_power10_xscom_pcba(PnvChip *chip, uint64_t addr)
{
    addr &= (PNV10_XSCOM_SIZE - 1);
    return addr >> 3;
}

static void pnv_chip_power10_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvChipClass *k = PNV_CHIP_CLASS(klass);

    k->chip_cfam_id = 0x120da04900008000ull; /* P10 DD1.0 (with NX) */
    k->cores_mask = POWER10_CORE_MASK;
    k->core_pir = pnv_chip_core_pir_p10;
    k->intc_create = pnv_chip_power10_intc_create;
    k->intc_reset = pnv_chip_power10_intc_reset;
    k->intc_destroy = pnv_chip_power10_intc_destroy;
    k->intc_print_info = pnv_chip_power10_intc_print_info;
    k->isa_create = pnv_chip_power10_isa_create;
    k->dt_populate = pnv_chip_power10_dt_populate;
    k->pic_print_info = pnv_chip_power10_pic_print_info;
    k->xscom_core_base = pnv_chip_power10_xscom_core_base;
    k->xscom_pcba = pnv_chip_power10_xscom_pcba;
    dc->desc = "PowerNV Chip POWER10";

    device_class_set_parent_realize(dc, pnv_chip_power10_realize,
                                    &k->parent_realize);
}

static void pnv_chip_core_sanitize(PnvChip *chip, Error **errp)
{
    PnvChipClass *pcc = PNV_CHIP_GET_CLASS(chip);
    int cores_max;

    /*
     * No custom mask for this chip, let's use the default one from *
     * the chip class
     */
    if (!chip->cores_mask) {
        chip->cores_mask = pcc->cores_mask;
    }

    /* filter alien core ids ! some are reserved */
    if ((chip->cores_mask & pcc->cores_mask) != chip->cores_mask) {
        error_setg(errp, "warning: invalid core mask for chip Ox%"PRIx64" !",
                   chip->cores_mask);
        return;
    }
    chip->cores_mask &= pcc->cores_mask;

    /* now that we have a sane layout, let check the number of cores */
    cores_max = ctpop64(chip->cores_mask);
    if (chip->nr_cores > cores_max) {
        error_setg(errp, "warning: too many cores for chip ! Limit is %d",
                   cores_max);
        return;
    }
}

static void pnv_chip_core_realize(PnvChip *chip, Error **errp)
{
    Error *error = NULL;
    PnvChipClass *pcc = PNV_CHIP_GET_CLASS(chip);
    const char *typename = pnv_chip_core_typename(chip);
    int i, core_hwid;
    PnvMachineState *pnv = PNV_MACHINE(qdev_get_machine());

    if (!object_class_by_name(typename)) {
        error_setg(errp, "Unable to find PowerNV CPU Core '%s'", typename);
        return;
    }

    /* Cores */
    pnv_chip_core_sanitize(chip, &error);
    if (error) {
        error_propagate(errp, error);
        return;
    }

    chip->cores = g_new0(PnvCore *, chip->nr_cores);

    for (i = 0, core_hwid = 0; (core_hwid < sizeof(chip->cores_mask) * 8)
             && (i < chip->nr_cores); core_hwid++) {
        char core_name[32];
        PnvCore *pnv_core;
        uint64_t xscom_core_base;

        if (!(chip->cores_mask & (1ull << core_hwid))) {
            continue;
        }

        pnv_core = PNV_CORE(object_new(typename));

        snprintf(core_name, sizeof(core_name), "core[%d]", core_hwid);
        object_property_add_child(OBJECT(chip), core_name, OBJECT(pnv_core));
        chip->cores[i] = pnv_core;
        object_property_set_int(OBJECT(pnv_core), "nr-threads",
                                chip->nr_threads, &error_fatal);
        object_property_set_int(OBJECT(pnv_core), CPU_CORE_PROP_CORE_ID,
                                core_hwid, &error_fatal);
        object_property_set_int(OBJECT(pnv_core), "pir",
                                pcc->core_pir(chip, core_hwid), &error_fatal);
        object_property_set_int(OBJECT(pnv_core), "hrmor", pnv->fw_load_addr,
                                &error_fatal);
        object_property_set_link(OBJECT(pnv_core), "chip", OBJECT(chip),
                                 &error_abort);
        qdev_realize(DEVICE(pnv_core), NULL, &error_fatal);

        /* Each core has an XSCOM MMIO region */
        xscom_core_base = pcc->xscom_core_base(chip, core_hwid);

        pnv_xscom_add_subregion(chip, xscom_core_base,
                                &pnv_core->xscom_regs);
        i++;
    }
}

static void pnv_chip_realize(DeviceState *dev, Error **errp)
{
    PnvChip *chip = PNV_CHIP(dev);
    Error *error = NULL;

    /* Cores */
    pnv_chip_core_realize(chip, &error);
    if (error) {
        error_propagate(errp, error);
        return;
    }
}

static Property pnv_chip_properties[] = {
    DEFINE_PROP_UINT32("chip-id", PnvChip, chip_id, 0),
    DEFINE_PROP_UINT64("ram-start", PnvChip, ram_start, 0),
    DEFINE_PROP_UINT64("ram-size", PnvChip, ram_size, 0),
    DEFINE_PROP_UINT32("nr-cores", PnvChip, nr_cores, 1),
    DEFINE_PROP_UINT64("cores-mask", PnvChip, cores_mask, 0x0),
    DEFINE_PROP_UINT32("nr-threads", PnvChip, nr_threads, 1),
    DEFINE_PROP_UINT32("num-phbs", PnvChip, num_phbs, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void pnv_chip_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_CPU, dc->categories);
    dc->realize = pnv_chip_realize;
    device_class_set_props(dc, pnv_chip_properties);
    dc->desc = "PowerNV Chip";
}

PowerPCCPU *pnv_chip_find_cpu(PnvChip *chip, uint32_t pir)
{
    int i, j;

    for (i = 0; i < chip->nr_cores; i++) {
        PnvCore *pc = chip->cores[i];
        CPUCore *cc = CPU_CORE(pc);

        for (j = 0; j < cc->nr_threads; j++) {
            if (ppc_cpu_pir(pc->threads[j]) == pir) {
                return pc->threads[j];
            }
        }
    }
    return NULL;
}

static ICSState *pnv_ics_get(XICSFabric *xi, int irq)
{
    PnvMachineState *pnv = PNV_MACHINE(xi);
    int i, j;

    for (i = 0; i < pnv->num_chips; i++) {
        PnvChip *chip = pnv->chips[i];
        Pnv8Chip *chip8 = PNV8_CHIP(pnv->chips[i]);

        if (ics_valid_irq(&chip8->psi.ics, irq)) {
            return &chip8->psi.ics;
        }
        for (j = 0; j < chip->num_phbs; j++) {
            if (ics_valid_irq(&chip8->phbs[j].lsis, irq)) {
                return &chip8->phbs[j].lsis;
            }
            if (ics_valid_irq(ICS(&chip8->phbs[j].msis), irq)) {
                return ICS(&chip8->phbs[j].msis);
            }
        }
    }
    return NULL;
}

static void pnv_ics_resend(XICSFabric *xi)
{
    PnvMachineState *pnv = PNV_MACHINE(xi);
    int i, j;

    for (i = 0; i < pnv->num_chips; i++) {
        PnvChip *chip = pnv->chips[i];
        Pnv8Chip *chip8 = PNV8_CHIP(pnv->chips[i]);

        ics_resend(&chip8->psi.ics);
        for (j = 0; j < chip->num_phbs; j++) {
            ics_resend(&chip8->phbs[j].lsis);
            ics_resend(ICS(&chip8->phbs[j].msis));
        }
    }
}

static ICPState *pnv_icp_get(XICSFabric *xi, int pir)
{
    PowerPCCPU *cpu = ppc_get_vcpu_by_pir(pir);

    return cpu ? ICP(pnv_cpu_state(cpu)->intc) : NULL;
}

static void pnv_pic_print_info(InterruptStatsProvider *obj,
                               Monitor *mon)
{
    PnvMachineState *pnv = PNV_MACHINE(obj);
    int i;
    CPUState *cs;

    CPU_FOREACH(cs) {
        PowerPCCPU *cpu = POWERPC_CPU(cs);

        /* XXX: loop on each chip/core/thread instead of CPU_FOREACH() */
        PNV_CHIP_GET_CLASS(pnv->chips[0])->intc_print_info(pnv->chips[0], cpu,
                                                           mon);
    }

    for (i = 0; i < pnv->num_chips; i++) {
        PNV_CHIP_GET_CLASS(pnv->chips[i])->pic_print_info(pnv->chips[i], mon);
    }
}

static int pnv_match_nvt(XiveFabric *xfb, uint8_t format,
                         uint8_t nvt_blk, uint32_t nvt_idx,
                         bool cam_ignore, uint8_t priority,
                         uint32_t logic_serv,
                         XiveTCTXMatch *match)
{
    PnvMachineState *pnv = PNV_MACHINE(xfb);
    int total_count = 0;
    int i;

    for (i = 0; i < pnv->num_chips; i++) {
        Pnv9Chip *chip9 = PNV9_CHIP(pnv->chips[i]);
        XivePresenter *xptr = XIVE_PRESENTER(&chip9->xive);
        XivePresenterClass *xpc = XIVE_PRESENTER_GET_CLASS(xptr);
        int count;

        count = xpc->match_nvt(xptr, format, nvt_blk, nvt_idx, cam_ignore,
                               priority, logic_serv, match);

        if (count < 0) {
            return count;
        }

        total_count += count;
    }

    return total_count;
}

static void pnv_machine_power8_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    XICSFabricClass *xic = XICS_FABRIC_CLASS(oc);
    PnvMachineClass *pmc = PNV_MACHINE_CLASS(oc);
    static const char compat[] = "qemu,powernv8\0qemu,powernv\0ibm,powernv";

    mc->desc = "IBM PowerNV (Non-Virtualized) POWER8";
    mc->default_cpu_type = POWERPC_CPU_TYPE_NAME("power8_v2.0");

    xic->icp_get = pnv_icp_get;
    xic->ics_get = pnv_ics_get;
    xic->ics_resend = pnv_ics_resend;

    pmc->compat = compat;
    pmc->compat_size = sizeof(compat);
}

static void pnv_machine_power9_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    XiveFabricClass *xfc = XIVE_FABRIC_CLASS(oc);
    PnvMachineClass *pmc = PNV_MACHINE_CLASS(oc);
    static const char compat[] = "qemu,powernv9\0ibm,powernv";

    mc->desc = "IBM PowerNV (Non-Virtualized) POWER9";
    mc->default_cpu_type = POWERPC_CPU_TYPE_NAME("power9_v2.0");
    xfc->match_nvt = pnv_match_nvt;

    mc->alias = "powernv";

    pmc->compat = compat;
    pmc->compat_size = sizeof(compat);
    pmc->dt_power_mgt = pnv_dt_power_mgt;
}

static void pnv_machine_power10_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    PnvMachineClass *pmc = PNV_MACHINE_CLASS(oc);
    static const char compat[] = "qemu,powernv10\0ibm,powernv";

    mc->desc = "IBM PowerNV (Non-Virtualized) POWER10";
    mc->default_cpu_type = POWERPC_CPU_TYPE_NAME("power10_v1.0");

    pmc->compat = compat;
    pmc->compat_size = sizeof(compat);
    pmc->dt_power_mgt = pnv_dt_power_mgt;
}

static bool pnv_machine_get_hb(Object *obj, Error **errp)
{
    PnvMachineState *pnv = PNV_MACHINE(obj);

    return !!pnv->fw_load_addr;
}

static void pnv_machine_set_hb(Object *obj, bool value, Error **errp)
{
    PnvMachineState *pnv = PNV_MACHINE(obj);

    if (value) {
        pnv->fw_load_addr = 0x8000000;
    }
}

static void pnv_cpu_do_nmi_on_cpu(CPUState *cs, run_on_cpu_data arg)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;

    cpu_synchronize_state(cs);
    ppc_cpu_do_system_reset(cs);
    if (env->spr[SPR_SRR1] & SRR1_WAKESTATE) {
        /*
         * Power-save wakeups, as indicated by non-zero SRR1[46:47] put the
         * wakeup reason in SRR1[42:45], system reset is indicated with 0b0100
         * (PPC_BIT(43)).
         */
        if (!(env->spr[SPR_SRR1] & SRR1_WAKERESET)) {
            warn_report("ppc_cpu_do_system_reset does not set system reset wakeup reason");
            env->spr[SPR_SRR1] |= SRR1_WAKERESET;
        }
    } else {
        /*
         * For non-powersave system resets, SRR1[42:45] are defined to be
         * implementation-dependent. The POWER9 User Manual specifies that
         * an external (SCOM driven, which may come from a BMC nmi command or
         * another CPU requesting a NMI IPI) system reset exception should be
         * 0b0010 (PPC_BIT(44)).
         */
        env->spr[SPR_SRR1] |= SRR1_WAKESCOM;
    }
}

static void pnv_nmi(NMIState *n, int cpu_index, Error **errp)
{
    CPUState *cs;

    CPU_FOREACH(cs) {
        async_run_on_cpu(cs, pnv_cpu_do_nmi_on_cpu, RUN_ON_CPU_NULL);
    }
}

static void pnv_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    InterruptStatsProviderClass *ispc = INTERRUPT_STATS_PROVIDER_CLASS(oc);
    NMIClass *nc = NMI_CLASS(oc);

    mc->desc = "IBM PowerNV (Non-Virtualized)";
    mc->init = pnv_init;
    mc->reset = pnv_reset;
    mc->max_cpus = MAX_CPUS;
    /* Pnv provides a AHCI device for storage */
    mc->block_default_type = IF_IDE;
    mc->no_parallel = 1;
    mc->default_boot_order = NULL;
    /*
     * RAM defaults to less than 2048 for 32-bit hosts, and large
     * enough to fit the maximum initrd size at it's load address
     */
    mc->default_ram_size = INITRD_LOAD_ADDR + INITRD_MAX_SIZE;
    mc->default_ram_id = "pnv.ram";
    ispc->print_info = pnv_pic_print_info;
    nc->nmi_monitor_handler = pnv_nmi;

    object_class_property_add_bool(oc, "hb-mode",
                                   pnv_machine_get_hb, pnv_machine_set_hb);
    object_class_property_set_description(oc, "hb-mode",
                              "Use a hostboot like boot loader");
}

#define DEFINE_PNV8_CHIP_TYPE(type, class_initfn) \
    {                                             \
        .name          = type,                    \
        .class_init    = class_initfn,            \
        .parent        = TYPE_PNV8_CHIP,          \
    }

#define DEFINE_PNV9_CHIP_TYPE(type, class_initfn) \
    {                                             \
        .name          = type,                    \
        .class_init    = class_initfn,            \
        .parent        = TYPE_PNV9_CHIP,          \
    }

#define DEFINE_PNV10_CHIP_TYPE(type, class_initfn) \
    {                                              \
        .name          = type,                     \
        .class_init    = class_initfn,             \
        .parent        = TYPE_PNV10_CHIP,          \
    }

static const TypeInfo types[] = {
    {
        .name          = MACHINE_TYPE_NAME("powernv10"),
        .parent        = TYPE_PNV_MACHINE,
        .class_init    = pnv_machine_power10_class_init,
    },
    {
        .name          = MACHINE_TYPE_NAME("powernv9"),
        .parent        = TYPE_PNV_MACHINE,
        .class_init    = pnv_machine_power9_class_init,
        .interfaces = (InterfaceInfo[]) {
            { TYPE_XIVE_FABRIC },
            { },
        },
    },
    {
        .name          = MACHINE_TYPE_NAME("powernv8"),
        .parent        = TYPE_PNV_MACHINE,
        .class_init    = pnv_machine_power8_class_init,
        .interfaces = (InterfaceInfo[]) {
            { TYPE_XICS_FABRIC },
            { },
        },
    },
    {
        .name          = TYPE_PNV_MACHINE,
        .parent        = TYPE_MACHINE,
        .abstract       = true,
        .instance_size = sizeof(PnvMachineState),
        .class_init    = pnv_machine_class_init,
        .class_size    = sizeof(PnvMachineClass),
        .interfaces = (InterfaceInfo[]) {
            { TYPE_INTERRUPT_STATS_PROVIDER },
            { TYPE_NMI },
            { },
        },
    },
    {
        .name          = TYPE_PNV_CHIP,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .class_init    = pnv_chip_class_init,
        .instance_size = sizeof(PnvChip),
        .class_size    = sizeof(PnvChipClass),
        .abstract      = true,
    },

    /*
     * P10 chip and variants
     */
    {
        .name          = TYPE_PNV10_CHIP,
        .parent        = TYPE_PNV_CHIP,
        .instance_init = pnv_chip_power10_instance_init,
        .instance_size = sizeof(Pnv10Chip),
    },
    DEFINE_PNV10_CHIP_TYPE(TYPE_PNV_CHIP_POWER10, pnv_chip_power10_class_init),

    /*
     * P9 chip and variants
     */
    {
        .name          = TYPE_PNV9_CHIP,
        .parent        = TYPE_PNV_CHIP,
        .instance_init = pnv_chip_power9_instance_init,
        .instance_size = sizeof(Pnv9Chip),
    },
    DEFINE_PNV9_CHIP_TYPE(TYPE_PNV_CHIP_POWER9, pnv_chip_power9_class_init),

    /*
     * P8 chip and variants
     */
    {
        .name          = TYPE_PNV8_CHIP,
        .parent        = TYPE_PNV_CHIP,
        .instance_init = pnv_chip_power8_instance_init,
        .instance_size = sizeof(Pnv8Chip),
    },
    DEFINE_PNV8_CHIP_TYPE(TYPE_PNV_CHIP_POWER8, pnv_chip_power8_class_init),
    DEFINE_PNV8_CHIP_TYPE(TYPE_PNV_CHIP_POWER8E, pnv_chip_power8e_class_init),
    DEFINE_PNV8_CHIP_TYPE(TYPE_PNV_CHIP_POWER8NVL,
                          pnv_chip_power8nvl_class_init),
};

DEFINE_TYPES(types)
