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
#include "sysemu/sysemu.h"
#include "sysemu/numa.h"
#include "sysemu/cpus.h"
#include "sysemu/device_tree.h"
#include "hw/hw.h"
#include "target/ppc/cpu.h"
#include "qemu/log.h"
#include "hw/ppc/fdt.h"
#include "hw/ppc/ppc.h"
#include "hw/ppc/pnv.h"
#include "hw/ppc/pnv_core.h"
#include "hw/loader.h"
#include "exec/address-spaces.h"
#include "qapi/visitor.h"
#include "monitor/monitor.h"
#include "hw/intc/intc.h"
#include "hw/ipmi/ipmi.h"
#include "target/ppc/mmu-hash64.h"

#include "hw/ppc/xics.h"
#include "hw/ppc/pnv_xscom.h"

#include "hw/isa/isa.h"
#include "hw/char/serial.h"
#include "hw/timer/mc146818rtc.h"

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
    _FDT((fdt_setprop_cell(fdt, offset, "ibm,slb-size", cpu->hash64_opts->slb_size)));
    _FDT((fdt_setprop_string(fdt, offset, "status", "okay")));
    _FDT((fdt_setprop(fdt, offset, "64-bit", NULL, 0)));

    if (env->spr_cb[SPR_PURR].oea_read) {
        _FDT((fdt_setprop(fdt, offset, "ibm,purr", NULL, 0)));
    }

    if (ppc_hash64_has(cpu, PPC_HASH64_1TSEG)) {
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
    const char *typename = pnv_chip_core_typename(chip);
    size_t typesize = object_type_get_instance_size(typename);
    int i;

    pnv_dt_xscom(chip, fdt, 0);

    for (i = 0; i < chip->nr_cores; i++) {
        PnvCore *pnv_core = PNV_CORE(chip->cores + i * typesize);

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
    const char *typename = pnv_chip_core_typename(chip);
    size_t typesize = object_type_get_instance_size(typename);
    int i;

    pnv_dt_xscom(chip, fdt, 0);

    for (i = 0; i < chip->nr_cores; i++) {
        PnvCore *pnv_core = PNV_CORE(chip->cores + i * typesize);

        pnv_dt_core(chip, pnv_core, fdt);
    }

    if (chip->ram_size) {
        pnv_dt_memory(fdt, chip->chip_id, chip->ram_start, chip->ram_size);
    }

    pnv_dt_lpc(chip, fdt, 0);
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

/* The default LPC bus of a multichip system is on chip 0. It's
 * recognized by the firmware (skiboot) using a "primary" property.
 */
static void pnv_dt_isa(PnvMachineState *pnv, void *fdt)
{
    int isa_offset = fdt_path_offset(fdt, pnv->chips[0]->dt_isa_nodename);
    ForeachPopulateArgs args = {
        .fdt = fdt,
        .offset = isa_offset,
    };

    _FDT((fdt_setprop(fdt, isa_offset, "primary", NULL, 0)));

    /* ISA devices are not necessarily parented to the ISA bus so we
     * can not use object_child_foreach() */
    qbus_walk_children(BUS(pnv->isa_bus), pnv_dt_isa_device, NULL, NULL, NULL,
                       &args);
}

static void pnv_dt_power_mgt(void *fdt)
{
    int off;

    off = fdt_add_subnode(fdt, 0, "ibm,opal");
    off = fdt_add_subnode(fdt, off, "power-mgt");

    _FDT(fdt_setprop_cell(fdt, off, "ibm,enabled-stop-levels", 0xc0000000));
}

static void *pnv_dt_create(MachineState *machine)
{
    const char plat_compat8[] = "qemu,powernv8\0qemu,powernv\0ibm,powernv";
    const char plat_compat9[] = "qemu,powernv9\0ibm,powernv";
    PnvMachineState *pnv = PNV_MACHINE(machine);
    void *fdt;
    char *buf;
    int off;
    int i;

    fdt = g_malloc0(FDT_MAX_SIZE);
    _FDT((fdt_create_empty_tree(fdt, FDT_MAX_SIZE)));

    /* Root node */
    _FDT((fdt_setprop_cell(fdt, 0, "#address-cells", 0x2)));
    _FDT((fdt_setprop_cell(fdt, 0, "#size-cells", 0x2)));
    _FDT((fdt_setprop_string(fdt, 0, "model",
                             "IBM PowerNV (emulated by qemu)")));
    if (pnv_is_power9(pnv)) {
        _FDT((fdt_setprop(fdt, 0, "compatible", plat_compat9,
                          sizeof(plat_compat9))));
    } else {
        _FDT((fdt_setprop(fdt, 0, "compatible", plat_compat8,
                          sizeof(plat_compat8))));
    }


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

    /* Create an extra node for power management on Power9 */
    if (pnv_is_power9(pnv)) {
        pnv_dt_power_mgt(fdt);
    }

    return fdt;
}

static void pnv_powerdown_notify(Notifier *n, void *opaque)
{
    PnvMachineState *pnv = PNV_MACHINE(qdev_get_machine());

    if (pnv->bmc) {
        pnv_bmc_powerdown(pnv->bmc);
    }
}

static void pnv_reset(void)
{
    MachineState *machine = MACHINE(qdev_get_machine());
    PnvMachineState *pnv = PNV_MACHINE(machine);
    void *fdt;
    Object *obj;

    qemu_devices_reset();

    /* OpenPOWER systems have a BMC, which can be defined on the
     * command line with:
     *
     *   -device ipmi-bmc-sim,id=bmc0
     *
     * This is the internal simulator but it could also be an external
     * BMC.
     */
    obj = object_resolve_path_type("", "ipmi-bmc-sim", NULL);
    if (obj) {
        pnv->bmc = IPMI_BMC(obj);
    }

    fdt = pnv_dt_create(machine);

    /* Pack resulting tree */
    _FDT((fdt_pack(fdt)));

    qemu_fdt_dumpdtb(fdt, fdt_totalsize(fdt));
    cpu_physical_memory_write(PNV_FDT_ADDR, fdt, fdt_totalsize(fdt));
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

static ISABus *pnv_isa_create(PnvChip *chip, Error **errp)
{
    return PNV_CHIP_GET_CLASS(chip)->isa_create(chip, errp);
}

static void pnv_chip_power8_pic_print_info(PnvChip *chip, Monitor *mon)
{
    Pnv8Chip *chip8 = PNV8_CHIP(chip);

    ics_pic_print_info(&chip8->psi.ics, mon);
}

static void pnv_chip_power9_pic_print_info(PnvChip *chip, Monitor *mon)
{
    Pnv9Chip *chip9 = PNV9_CHIP(chip);

    pnv_xive_pic_print_info(&chip9->xive, mon);
    pnv_psi_pic_print_info(&chip9->psi, mon);
}

static void pnv_init(MachineState *machine)
{
    PnvMachineState *pnv = PNV_MACHINE(machine);
    MemoryRegion *ram;
    char *fw_filename;
    long fw_size;
    int i;
    char *chip_typename;

    /* allocate RAM */
    if (machine->ram_size < (1 * GiB)) {
        warn_report("skiboot may not work with < 1GB of RAM");
    }

    ram = g_new(MemoryRegion, 1);
    memory_region_allocate_system_memory(ram, NULL, "pnv.ram",
                                         machine->ram_size);
    memory_region_add_subregion(get_system_memory(), 0, ram);

    /* load skiboot firmware  */
    if (bios_name == NULL) {
        bios_name = FW_FILE_NAME;
    }

    fw_filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
    if (!fw_filename) {
        error_report("Could not find OPAL firmware '%s'", bios_name);
        exit(1);
    }

    fw_size = load_image_targphys(fw_filename, FW_LOAD_ADDR, FW_MAX_SIZE);
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

    /* Create the processor chips */
    i = strlen(machine->cpu_type) - strlen(POWERPC_CPU_TYPE_SUFFIX);
    chip_typename = g_strdup_printf(PNV_CHIP_TYPE_NAME("%.*s"),
                                    i, machine->cpu_type);
    if (!object_class_by_name(chip_typename)) {
        error_report("invalid CPU model '%.*s' for %s machine",
                     i, machine->cpu_type, MACHINE_GET_CLASS(machine)->name);
        exit(1);
    }

    pnv->chips = g_new0(PnvChip *, pnv->num_chips);
    for (i = 0; i < pnv->num_chips; i++) {
        char chip_name[32];
        Object *chip = object_new(chip_typename);

        pnv->chips[i] = PNV_CHIP(chip);

        /* TODO: put all the memory in one node on chip 0 until we find a
         * way to specify different ranges for each chip
         */
        if (i == 0) {
            object_property_set_int(chip, machine->ram_size, "ram-size",
                                    &error_fatal);
        }

        snprintf(chip_name, sizeof(chip_name), "chip[%d]", PNV_CHIP_HWID(i));
        object_property_add_child(OBJECT(pnv), chip_name, chip, &error_fatal);
        object_property_set_int(chip, PNV_CHIP_HWID(i), "chip-id",
                                &error_fatal);
        object_property_set_int(chip, smp_cores, "nr-cores", &error_fatal);
        object_property_set_bool(chip, true, "realized", &error_fatal);
    }
    g_free(chip_typename);

    /* Instantiate ISA bus on chip 0 */
    pnv->isa_bus = pnv_isa_create(pnv->chips[0], &error_fatal);

    /* Create serial port */
    serial_hds_isa_init(pnv->isa_bus, 0, MAX_ISA_SERIAL_PORTS);

    /* Create an RTC ISA device too */
    mc146818_rtc_init(pnv->isa_bus, 2000, NULL);

    /* OpenPOWER systems use a IPMI SEL Event message to notify the
     * host to powerdown */
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
    Error *local_err = NULL;
    Object *obj;
    PnvCPUState *pnv_cpu = pnv_cpu_state(cpu);

    obj = icp_create(OBJECT(cpu), TYPE_PNV_ICP, XICS_FABRIC(qdev_get_machine()),
                     &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    pnv_cpu->intc = obj;
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
    obj = xive_tctx_create(OBJECT(cpu), XIVE_ROUTER(&chip9->xive), &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    pnv_cpu->intc = obj;
}

/* Allowed core identifiers on a POWER8 Processor Chip :
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

static void pnv_chip_power8_instance_init(Object *obj)
{
    Pnv8Chip *chip8 = PNV8_CHIP(obj);

    object_initialize_child(obj, "psi",  &chip8->psi, sizeof(chip8->psi),
                            TYPE_PNV8_PSI, &error_abort, NULL);
    object_property_add_const_link(OBJECT(&chip8->psi), "xics",
                                   OBJECT(qdev_get_machine()), &error_abort);

    object_initialize_child(obj, "lpc",  &chip8->lpc, sizeof(chip8->lpc),
                            TYPE_PNV8_LPC, &error_abort, NULL);
    object_property_add_const_link(OBJECT(&chip8->lpc), "psi",
                                   OBJECT(&chip8->psi), &error_abort);

    object_initialize_child(obj, "occ",  &chip8->occ, sizeof(chip8->occ),
                            TYPE_PNV8_OCC, &error_abort, NULL);
    object_property_add_const_link(OBJECT(&chip8->occ), "psi",
                                   OBJECT(&chip8->psi), &error_abort);
}

static void pnv_chip_icp_realize(Pnv8Chip *chip8, Error **errp)
 {
    PnvChip *chip = PNV_CHIP(chip8);
    PnvChipClass *pcc = PNV_CHIP_GET_CLASS(chip);
    const char *typename = pnv_chip_core_typename(chip);
    size_t typesize = object_type_get_instance_size(typename);
    int i, j;
    char *name;
    XICSFabric *xi = XICS_FABRIC(qdev_get_machine());

    name = g_strdup_printf("icp-%x", chip->chip_id);
    memory_region_init(&chip8->icp_mmio, OBJECT(chip), name, PNV_ICP_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(chip), &chip8->icp_mmio);
    g_free(name);

    sysbus_mmio_map(SYS_BUS_DEVICE(chip), 1, PNV_ICP_BASE(chip));

    /* Map the ICP registers for each thread */
    for (i = 0; i < chip->nr_cores; i++) {
        PnvCore *pnv_core = PNV_CORE(chip->cores + i * typesize);
        int core_hwid = CPU_CORE(pnv_core)->core_id;

        for (j = 0; j < CPU_CORE(pnv_core)->nr_threads; j++) {
            uint32_t pir = pcc->core_pir(chip, core_hwid) + j;
            PnvICPState *icp = PNV_ICP(xics_icp_get(xi, pir));

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
    object_property_set_int(OBJECT(&chip8->psi), PNV_PSIHB_BASE(chip),
                            "bar", &error_fatal);
    object_property_set_bool(OBJECT(&chip8->psi), true, "realized", &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    pnv_xscom_add_subregion(chip, PNV_XSCOM_PSIHB_BASE,
                            &PNV_PSI(psi8)->xscom_regs);

    /* Create LPC controller */
    object_property_set_bool(OBJECT(&chip8->lpc), true, "realized",
                             &error_fatal);
    pnv_xscom_add_subregion(chip, PNV_XSCOM_LPC_BASE, &chip8->lpc.xscom_regs);

    chip->dt_isa_nodename = g_strdup_printf("/xscom@%" PRIx64 "/isa@%x",
                                            (uint64_t) PNV_XSCOM_BASE(chip),
                                            PNV_XSCOM_LPC_BASE);

    /* Interrupt Management Area. This is the memory region holding
     * all the Interrupt Control Presenter (ICP) registers */
    pnv_chip_icp_realize(chip8, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    /* Create the simplified OCC model */
    object_property_set_bool(OBJECT(&chip8->occ), true, "realized", &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    pnv_xscom_add_subregion(chip, PNV_XSCOM_OCC_BASE, &chip8->occ.xscom_regs);
}

static void pnv_chip_power8e_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvChipClass *k = PNV_CHIP_CLASS(klass);

    k->chip_type = PNV_CHIP_POWER8E;
    k->chip_cfam_id = 0x221ef04980000000ull;  /* P8 Murano DD2.1 */
    k->cores_mask = POWER8E_CORE_MASK;
    k->core_pir = pnv_chip_core_pir_p8;
    k->intc_create = pnv_chip_power8_intc_create;
    k->isa_create = pnv_chip_power8_isa_create;
    k->dt_populate = pnv_chip_power8_dt_populate;
    k->pic_print_info = pnv_chip_power8_pic_print_info;
    dc->desc = "PowerNV Chip POWER8E";

    device_class_set_parent_realize(dc, pnv_chip_power8_realize,
                                    &k->parent_realize);
}

static void pnv_chip_power8_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvChipClass *k = PNV_CHIP_CLASS(klass);

    k->chip_type = PNV_CHIP_POWER8;
    k->chip_cfam_id = 0x220ea04980000000ull; /* P8 Venice DD2.0 */
    k->cores_mask = POWER8_CORE_MASK;
    k->core_pir = pnv_chip_core_pir_p8;
    k->intc_create = pnv_chip_power8_intc_create;
    k->isa_create = pnv_chip_power8_isa_create;
    k->dt_populate = pnv_chip_power8_dt_populate;
    k->pic_print_info = pnv_chip_power8_pic_print_info;
    dc->desc = "PowerNV Chip POWER8";

    device_class_set_parent_realize(dc, pnv_chip_power8_realize,
                                    &k->parent_realize);
}

static void pnv_chip_power8nvl_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvChipClass *k = PNV_CHIP_CLASS(klass);

    k->chip_type = PNV_CHIP_POWER8NVL;
    k->chip_cfam_id = 0x120d304980000000ull;  /* P8 Naples DD1.0 */
    k->cores_mask = POWER8_CORE_MASK;
    k->core_pir = pnv_chip_core_pir_p8;
    k->intc_create = pnv_chip_power8_intc_create;
    k->isa_create = pnv_chip_power8nvl_isa_create;
    k->dt_populate = pnv_chip_power8_dt_populate;
    k->pic_print_info = pnv_chip_power8_pic_print_info;
    dc->desc = "PowerNV Chip POWER8NVL";

    device_class_set_parent_realize(dc, pnv_chip_power8_realize,
                                    &k->parent_realize);
}

static void pnv_chip_power9_instance_init(Object *obj)
{
    Pnv9Chip *chip9 = PNV9_CHIP(obj);

    object_initialize_child(obj, "xive", &chip9->xive, sizeof(chip9->xive),
                            TYPE_PNV_XIVE, &error_abort, NULL);
    object_property_add_const_link(OBJECT(&chip9->xive), "chip", obj,
                                   &error_abort);

    object_initialize_child(obj, "psi",  &chip9->psi, sizeof(chip9->psi),
                            TYPE_PNV9_PSI, &error_abort, NULL);
    object_property_add_const_link(OBJECT(&chip9->psi), "chip", obj,
                                   &error_abort);

    object_initialize_child(obj, "lpc",  &chip9->lpc, sizeof(chip9->lpc),
                            TYPE_PNV9_LPC, &error_abort, NULL);
    object_property_add_const_link(OBJECT(&chip9->lpc), "psi",
                                   OBJECT(&chip9->psi), &error_abort);

    object_initialize_child(obj, "occ",  &chip9->occ, sizeof(chip9->occ),
                            TYPE_PNV9_OCC, &error_abort, NULL);
    object_property_add_const_link(OBJECT(&chip9->occ), "psi",
                                   OBJECT(&chip9->psi), &error_abort);
}

static void pnv_chip_quad_realize(Pnv9Chip *chip9, Error **errp)
{
    PnvChip *chip = PNV_CHIP(chip9);
    const char *typename = pnv_chip_core_typename(chip);
    size_t typesize = object_type_get_instance_size(typename);
    int i;

    chip9->nr_quads = DIV_ROUND_UP(chip->nr_cores, 4);
    chip9->quads = g_new0(PnvQuad, chip9->nr_quads);

    for (i = 0; i < chip9->nr_quads; i++) {
        char eq_name[32];
        PnvQuad *eq = &chip9->quads[i];
        PnvCore *pnv_core = PNV_CORE(chip->cores + (i * 4) * typesize);
        int core_id = CPU_CORE(pnv_core)->core_id;

        snprintf(eq_name, sizeof(eq_name), "eq[%d]", core_id);
        object_initialize_child(OBJECT(chip), eq_name, eq, sizeof(*eq),
                                TYPE_PNV_QUAD, &error_fatal, NULL);

        object_property_set_int(OBJECT(eq), core_id, "id", &error_fatal);
        object_property_set_bool(OBJECT(eq), true, "realized", &error_fatal);

        pnv_xscom_add_subregion(chip, PNV9_XSCOM_EQ_BASE(eq->id),
                                &eq->xscom_regs);
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
    object_property_set_int(OBJECT(&chip9->xive), PNV9_XIVE_IC_BASE(chip),
                            "ic-bar", &error_fatal);
    object_property_set_int(OBJECT(&chip9->xive), PNV9_XIVE_VC_BASE(chip),
                            "vc-bar", &error_fatal);
    object_property_set_int(OBJECT(&chip9->xive), PNV9_XIVE_PC_BASE(chip),
                            "pc-bar", &error_fatal);
    object_property_set_int(OBJECT(&chip9->xive), PNV9_XIVE_TM_BASE(chip),
                            "tm-bar", &error_fatal);
    object_property_set_bool(OBJECT(&chip9->xive), true, "realized",
                             &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    pnv_xscom_add_subregion(chip, PNV9_XSCOM_XIVE_BASE,
                            &chip9->xive.xscom_regs);

    /* Processor Service Interface (PSI) Host Bridge */
    object_property_set_int(OBJECT(&chip9->psi), PNV9_PSIHB_BASE(chip),
                            "bar", &error_fatal);
    object_property_set_bool(OBJECT(&chip9->psi), true, "realized", &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    pnv_xscom_add_subregion(chip, PNV9_XSCOM_PSIHB_BASE,
                            &PNV_PSI(psi9)->xscom_regs);

    /* LPC */
    object_property_set_bool(OBJECT(&chip9->lpc), true, "realized", &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    memory_region_add_subregion(get_system_memory(), PNV9_LPCM_BASE(chip),
                                &chip9->lpc.xscom_regs);

    chip->dt_isa_nodename = g_strdup_printf("/lpcm-opb@%" PRIx64 "/lpc@0",
                                            (uint64_t) PNV9_LPCM_BASE(chip));

    /* Create the simplified OCC model */
    object_property_set_bool(OBJECT(&chip9->occ), true, "realized", &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    pnv_xscom_add_subregion(chip, PNV9_XSCOM_OCC_BASE, &chip9->occ.xscom_regs);
}

static void pnv_chip_power9_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvChipClass *k = PNV_CHIP_CLASS(klass);

    k->chip_type = PNV_CHIP_POWER9;
    k->chip_cfam_id = 0x220d104900008000ull; /* P9 Nimbus DD2.0 */
    k->cores_mask = POWER9_CORE_MASK;
    k->core_pir = pnv_chip_core_pir_p9;
    k->intc_create = pnv_chip_power9_intc_create;
    k->isa_create = pnv_chip_power9_isa_create;
    k->dt_populate = pnv_chip_power9_dt_populate;
    k->pic_print_info = pnv_chip_power9_pic_print_info;
    dc->desc = "PowerNV Chip POWER9";

    device_class_set_parent_realize(dc, pnv_chip_power9_realize,
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
    size_t typesize = object_type_get_instance_size(typename);
    int i, core_hwid;

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

    chip->cores = g_malloc0(typesize * chip->nr_cores);

    for (i = 0, core_hwid = 0; (core_hwid < sizeof(chip->cores_mask) * 8)
             && (i < chip->nr_cores); core_hwid++) {
        char core_name[32];
        void *pnv_core = chip->cores + i * typesize;
        uint64_t xscom_core_base;

        if (!(chip->cores_mask & (1ull << core_hwid))) {
            continue;
        }

        snprintf(core_name, sizeof(core_name), "core[%d]", core_hwid);
        object_initialize_child(OBJECT(chip), core_name, pnv_core, typesize,
                                typename, &error_fatal, NULL);
        object_property_set_int(OBJECT(pnv_core), smp_threads, "nr-threads",
                                &error_fatal);
        object_property_set_int(OBJECT(pnv_core), core_hwid,
                                CPU_CORE_PROP_CORE_ID, &error_fatal);
        object_property_set_int(OBJECT(pnv_core),
                                pcc->core_pir(chip, core_hwid),
                                "pir", &error_fatal);
        object_property_add_const_link(OBJECT(pnv_core), "chip",
                                       OBJECT(chip), &error_fatal);
        object_property_set_bool(OBJECT(pnv_core), true, "realized",
                                 &error_fatal);

        /* Each core has an XSCOM MMIO region */
        if (!pnv_chip_is_power9(chip)) {
            xscom_core_base = PNV_XSCOM_EX_BASE(core_hwid);
        } else {
            xscom_core_base = PNV9_XSCOM_EC_BASE(core_hwid);
        }

        pnv_xscom_add_subregion(chip, xscom_core_base,
                                &PNV_CORE(pnv_core)->xscom_regs);
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
    DEFINE_PROP_END_OF_LIST(),
};

static void pnv_chip_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_CPU, dc->categories);
    dc->realize = pnv_chip_realize;
    dc->props = pnv_chip_properties;
    dc->desc = "PowerNV Chip";
}

static ICSState *pnv_ics_get(XICSFabric *xi, int irq)
{
    PnvMachineState *pnv = PNV_MACHINE(xi);
    int i;

    for (i = 0; i < pnv->num_chips; i++) {
        Pnv8Chip *chip8 = PNV8_CHIP(pnv->chips[i]);

        if (ics_valid_irq(&chip8->psi.ics, irq)) {
            return &chip8->psi.ics;
        }
    }
    return NULL;
}

static void pnv_ics_resend(XICSFabric *xi)
{
    PnvMachineState *pnv = PNV_MACHINE(xi);
    int i;

    for (i = 0; i < pnv->num_chips; i++) {
        Pnv8Chip *chip8 = PNV8_CHIP(pnv->chips[i]);
        ics_resend(&chip8->psi.ics);
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

        if (pnv_chip_is_power9(pnv->chips[0])) {
            xive_tctx_pic_print_info(XIVE_TCTX(pnv_cpu_state(cpu)->intc), mon);
        } else {
            icp_pic_print_info(ICP(pnv_cpu_state(cpu)->intc), mon);
        }
    }

    for (i = 0; i < pnv->num_chips; i++) {
        PNV_CHIP_GET_CLASS(pnv->chips[i])->pic_print_info(pnv->chips[i], mon);
    }
}

static void pnv_get_num_chips(Object *obj, Visitor *v, const char *name,
                              void *opaque, Error **errp)
{
    visit_type_uint32(v, name, &PNV_MACHINE(obj)->num_chips, errp);
}

static void pnv_set_num_chips(Object *obj, Visitor *v, const char *name,
                              void *opaque, Error **errp)
{
    PnvMachineState *pnv = PNV_MACHINE(obj);
    uint32_t num_chips;
    Error *local_err = NULL;

    visit_type_uint32(v, name, &num_chips, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    /*
     * TODO: should we decide on how many chips we can create based
     * on #cores and Venice vs. Murano vs. Naples chip type etc...,
     */
    if (!is_power_of_2(num_chips) || num_chips > 4) {
        error_setg(errp, "invalid number of chips: '%d'", num_chips);
        return;
    }

    pnv->num_chips = num_chips;
}

static void pnv_machine_instance_init(Object *obj)
{
    PnvMachineState *pnv = PNV_MACHINE(obj);
    pnv->num_chips = 1;
}

static void pnv_machine_class_props_init(ObjectClass *oc)
{
    object_class_property_add(oc, "num-chips", "uint32",
                              pnv_get_num_chips, pnv_set_num_chips,
                              NULL, NULL, NULL);
    object_class_property_set_description(oc, "num-chips",
                              "Specifies the number of processor chips",
                              NULL);
}

static void pnv_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    XICSFabricClass *xic = XICS_FABRIC_CLASS(oc);
    InterruptStatsProviderClass *ispc = INTERRUPT_STATS_PROVIDER_CLASS(oc);

    mc->desc = "IBM PowerNV (Non-Virtualized)";
    mc->init = pnv_init;
    mc->reset = pnv_reset;
    mc->max_cpus = MAX_CPUS;
    mc->default_cpu_type = POWERPC_CPU_TYPE_NAME("power8_v2.0");
    mc->block_default_type = IF_IDE; /* Pnv provides a AHCI device for
                                      * storage */
    mc->no_parallel = 1;
    mc->default_boot_order = NULL;
    mc->default_ram_size = 1 * GiB;
    xic->icp_get = pnv_icp_get;
    xic->ics_get = pnv_ics_get;
    xic->ics_resend = pnv_ics_resend;
    ispc->print_info = pnv_pic_print_info;

    pnv_machine_class_props_init(oc);
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

static const TypeInfo types[] = {
    {
        .name          = TYPE_PNV_MACHINE,
        .parent        = TYPE_MACHINE,
        .instance_size = sizeof(PnvMachineState),
        .instance_init = pnv_machine_instance_init,
        .class_init    = pnv_machine_class_init,
        .interfaces = (InterfaceInfo[]) {
            { TYPE_XICS_FABRIC },
            { TYPE_INTERRUPT_STATS_PROVIDER },
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
