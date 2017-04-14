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
#include "qapi/error.h"
#include "sysemu/sysemu.h"
#include "sysemu/numa.h"
#include "sysemu/cpus.h"
#include "hw/hw.h"
#include "target/ppc/cpu.h"
#include "qemu/log.h"
#include "hw/ppc/fdt.h"
#include "hw/ppc/ppc.h"
#include "hw/ppc/pnv.h"
#include "hw/ppc/pnv_core.h"
#include "hw/loader.h"
#include "exec/address-spaces.h"
#include "qemu/cutils.h"
#include "qapi/visitor.h"

#include "hw/ppc/pnv_xscom.h"

#include "hw/isa/isa.h"
#include "hw/char/serial.h"
#include "hw/timer/mc146818rtc.h"

#include <libfdt.h>

#define FDT_MAX_SIZE            0x00100000

#define FW_FILE_NAME            "skiboot.lid"
#define FW_LOAD_ADDR            0x0
#define FW_MAX_SIZE             0x00400000

#define KERNEL_LOAD_ADDR        0x20000000
#define INITRD_LOAD_ADDR        0x40000000

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
static void powernv_populate_memory_node(void *fdt, int chip_id, hwaddr start,
                                         hwaddr size)
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
        cpus_offset = fdt_add_subnode(fdt, fdt_path_offset(fdt, "/"),
                                      "cpus");
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
static void powernv_create_core_node(PnvChip *chip, PnvCore *pc, void *fdt)
{
    CPUState *cs = CPU(DEVICE(pc->threads));
    DeviceClass *dc = DEVICE_GET_CLASS(cs);
    PowerPCCPU *cpu = POWERPC_CPU(cs);
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
        error_report("Warning: Unknown L1 dcache size for cpu");
    }
    if (pcc->l1_icache_size) {
        _FDT((fdt_setprop_cell(fdt, offset, "i-cache-size",
                               pcc->l1_icache_size)));
    } else {
        error_report("Warning: Unknown L1 icache size for cpu");
    }

    _FDT((fdt_setprop_cell(fdt, offset, "timebase-frequency", tbfreq)));
    _FDT((fdt_setprop_cell(fdt, offset, "clock-frequency", cpufreq)));
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

    page_sizes_prop_size = ppc_create_page_sizes_prop(env, page_sizes_prop,
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

static void powernv_populate_chip(PnvChip *chip, void *fdt)
{
    PnvChipClass *pcc = PNV_CHIP_GET_CLASS(chip);
    char *typename = pnv_core_typename(pcc->cpu_model);
    size_t typesize = object_type_get_instance_size(typename);
    int i;

    pnv_xscom_populate(chip, fdt, 0);

    for (i = 0; i < chip->nr_cores; i++) {
        PnvCore *pnv_core = PNV_CORE(chip->cores + i * typesize);

        powernv_create_core_node(chip, pnv_core, fdt);
    }

    if (chip->ram_size) {
        powernv_populate_memory_node(fdt, chip->chip_id, chip->ram_start,
                                     chip->ram_size);
    }
    g_free(typename);
}

static void *powernv_create_fdt(MachineState *machine)
{
    const char plat_compat[] = "qemu,powernv\0ibm,powernv";
    PnvMachineState *pnv = POWERNV_MACHINE(machine);
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
    _FDT((fdt_setprop(fdt, 0, "compatible", plat_compat,
                      sizeof(plat_compat))));

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
        powernv_populate_chip(pnv->chips[i], fdt);
    }
    return fdt;
}

static void ppc_powernv_reset(void)
{
    MachineState *machine = MACHINE(qdev_get_machine());
    void *fdt;

    qemu_devices_reset();

    fdt = powernv_create_fdt(machine);

    /* Pack resulting tree */
    _FDT((fdt_pack(fdt)));

    cpu_physical_memory_write(PNV_FDT_ADDR, fdt, fdt_totalsize(fdt));
}

/* If we don't use the built-in LPC interrupt deserializer, we need
 * to provide a set of qirqs for the ISA bus or things will go bad.
 *
 * Most machines using pre-Naples chips (without said deserializer)
 * have a CPLD that will collect the SerIRQ and shoot them as a
 * single level interrupt to the P8 chip. So let's setup a hook
 * for doing just that.
 *
 * Note: The actual interrupt input isn't emulated yet, this will
 * come with the PSI bridge model.
 */
static void pnv_lpc_isa_irq_handler_cpld(void *opaque, int n, int level)
{
    /* We don't yet emulate the PSI bridge which provides the external
     * interrupt, so just drop interrupts on the floor
     */
}

static void pnv_lpc_isa_irq_handler(void *opaque, int n, int level)
{
     /* XXX TODO */
}

static ISABus *pnv_isa_create(PnvChip *chip)
{
    PnvLpcController *lpc = &chip->lpc;
    ISABus *isa_bus;
    qemu_irq *irqs;
    PnvChipClass *pcc = PNV_CHIP_GET_CLASS(chip);

    /* let isa_bus_new() create its own bridge on SysBus otherwise
     * devices speficied on the command line won't find the bus and
     * will fail to create.
     */
    isa_bus = isa_bus_new(NULL, &lpc->isa_mem, &lpc->isa_io,
                          &error_fatal);

    /* Not all variants have a working serial irq decoder. If not,
     * handling of LPC interrupts becomes a platform issue (some
     * platforms have a CPLD to do it).
     */
    if (pcc->chip_type == PNV_CHIP_POWER8NVL) {
        irqs = qemu_allocate_irqs(pnv_lpc_isa_irq_handler, chip, ISA_NUM_IRQS);
    } else {
        irqs = qemu_allocate_irqs(pnv_lpc_isa_irq_handler_cpld, chip,
                                  ISA_NUM_IRQS);
    }

    isa_bus_irqs(isa_bus, irqs);
    return isa_bus;
}

static void ppc_powernv_init(MachineState *machine)
{
    PnvMachineState *pnv = POWERNV_MACHINE(machine);
    MemoryRegion *ram;
    char *fw_filename;
    long fw_size;
    int i;
    char *chip_typename;

    /* allocate RAM */
    if (machine->ram_size < (1 * G_BYTE)) {
        error_report("Warning: skiboot may not work with < 1GB of RAM");
    }

    ram = g_new(MemoryRegion, 1);
    memory_region_allocate_system_memory(ram, NULL, "ppc_powernv.ram",
                                         machine->ram_size);
    memory_region_add_subregion(get_system_memory(), 0, ram);

    /* load skiboot firmware  */
    if (bios_name == NULL) {
        bios_name = FW_FILE_NAME;
    }

    fw_filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);

    fw_size = load_image_targphys(fw_filename, FW_LOAD_ADDR, FW_MAX_SIZE);
    if (fw_size < 0) {
        error_report("Could not load OPAL '%s'", fw_filename);
        exit(1);
    }
    g_free(fw_filename);

    /* load kernel */
    if (machine->kernel_filename) {
        long kernel_size;

        kernel_size = load_image_targphys(machine->kernel_filename,
                                          KERNEL_LOAD_ADDR, 0x2000000);
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
                                  pnv->initrd_base, 0x10000000); /* 128MB max */
        if (pnv->initrd_size < 0) {
            error_report("Could not load initial ram disk '%s'",
                         machine->initrd_filename);
            exit(1);
        }
    }

    /* We need some cpu model to instantiate the PnvChip class */
    if (machine->cpu_model == NULL) {
        machine->cpu_model = "POWER8";
    }

    /* Create the processor chips */
    chip_typename = g_strdup_printf(TYPE_PNV_CHIP "-%s", machine->cpu_model);
    if (!object_class_by_name(chip_typename)) {
        error_report("qemu: invalid CPU model '%s' for %s machine",
                     machine->cpu_model, MACHINE_GET_CLASS(machine)->name);
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
    pnv->isa_bus = pnv_isa_create(pnv->chips[0]);

    /* Create serial port */
    serial_hds_isa_init(pnv->isa_bus, 0, MAX_SERIAL_PORTS);

    /* Create an RTC ISA device too */
    rtc_init(pnv->isa_bus, 2000, NULL);
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
 * POWER9 has 24 cores, ids starting at 0x20
 */
#define POWER9_CORE_MASK   (0xffffff00000000ull)

static void pnv_chip_power8e_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvChipClass *k = PNV_CHIP_CLASS(klass);

    k->cpu_model = "POWER8E";
    k->chip_type = PNV_CHIP_POWER8E;
    k->chip_cfam_id = 0x221ef04980000000ull;  /* P8 Murano DD2.1 */
    k->cores_mask = POWER8E_CORE_MASK;
    k->core_pir = pnv_chip_core_pir_p8;
    k->xscom_base = 0x003fc0000000000ull;
    k->xscom_core_base = 0x10000000ull;
    dc->desc = "PowerNV Chip POWER8E";
}

static const TypeInfo pnv_chip_power8e_info = {
    .name          = TYPE_PNV_CHIP_POWER8E,
    .parent        = TYPE_PNV_CHIP,
    .instance_size = sizeof(PnvChip),
    .class_init    = pnv_chip_power8e_class_init,
};

static void pnv_chip_power8_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvChipClass *k = PNV_CHIP_CLASS(klass);

    k->cpu_model = "POWER8";
    k->chip_type = PNV_CHIP_POWER8;
    k->chip_cfam_id = 0x220ea04980000000ull; /* P8 Venice DD2.0 */
    k->cores_mask = POWER8_CORE_MASK;
    k->core_pir = pnv_chip_core_pir_p8;
    k->xscom_base = 0x003fc0000000000ull;
    k->xscom_core_base = 0x10000000ull;
    dc->desc = "PowerNV Chip POWER8";
}

static const TypeInfo pnv_chip_power8_info = {
    .name          = TYPE_PNV_CHIP_POWER8,
    .parent        = TYPE_PNV_CHIP,
    .instance_size = sizeof(PnvChip),
    .class_init    = pnv_chip_power8_class_init,
};

static void pnv_chip_power8nvl_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvChipClass *k = PNV_CHIP_CLASS(klass);

    k->cpu_model = "POWER8NVL";
    k->chip_type = PNV_CHIP_POWER8NVL;
    k->chip_cfam_id = 0x120d304980000000ull;  /* P8 Naples DD1.0 */
    k->cores_mask = POWER8_CORE_MASK;
    k->core_pir = pnv_chip_core_pir_p8;
    k->xscom_base = 0x003fc0000000000ull;
    k->xscom_core_base = 0x10000000ull;
    dc->desc = "PowerNV Chip POWER8NVL";
}

static const TypeInfo pnv_chip_power8nvl_info = {
    .name          = TYPE_PNV_CHIP_POWER8NVL,
    .parent        = TYPE_PNV_CHIP,
    .instance_size = sizeof(PnvChip),
    .class_init    = pnv_chip_power8nvl_class_init,
};

static void pnv_chip_power9_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvChipClass *k = PNV_CHIP_CLASS(klass);

    k->cpu_model = "POWER9";
    k->chip_type = PNV_CHIP_POWER9;
    k->chip_cfam_id = 0x100d104980000000ull; /* P9 Nimbus DD1.0 */
    k->cores_mask = POWER9_CORE_MASK;
    k->core_pir = pnv_chip_core_pir_p9;
    k->xscom_base = 0x00603fc00000000ull;
    k->xscom_core_base = 0x0ull;
    dc->desc = "PowerNV Chip POWER9";
}

static const TypeInfo pnv_chip_power9_info = {
    .name          = TYPE_PNV_CHIP_POWER9,
    .parent        = TYPE_PNV_CHIP,
    .instance_size = sizeof(PnvChip),
    .class_init    = pnv_chip_power9_class_init,
};

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

static void pnv_chip_init(Object *obj)
{
    PnvChip *chip = PNV_CHIP(obj);
    PnvChipClass *pcc = PNV_CHIP_GET_CLASS(chip);

    chip->xscom_base = pcc->xscom_base;

    object_initialize(&chip->lpc, sizeof(chip->lpc), TYPE_PNV_LPC);
    object_property_add_child(obj, "lpc", OBJECT(&chip->lpc), NULL);
}

static void pnv_chip_realize(DeviceState *dev, Error **errp)
{
    PnvChip *chip = PNV_CHIP(dev);
    Error *error = NULL;
    PnvChipClass *pcc = PNV_CHIP_GET_CLASS(chip);
    char *typename = pnv_core_typename(pcc->cpu_model);
    size_t typesize = object_type_get_instance_size(typename);
    int i, core_hwid;

    if (!object_class_by_name(typename)) {
        error_setg(errp, "Unable to find PowerNV CPU Core '%s'", typename);
        return;
    }

    /* XSCOM bridge */
    pnv_xscom_realize(chip, &error);
    if (error) {
        error_propagate(errp, error);
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(chip), 0, PNV_XSCOM_BASE(chip));

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

        if (!(chip->cores_mask & (1ull << core_hwid))) {
            continue;
        }

        object_initialize(pnv_core, typesize, typename);
        snprintf(core_name, sizeof(core_name), "core[%d]", core_hwid);
        object_property_add_child(OBJECT(chip), core_name, OBJECT(pnv_core),
                                  &error_fatal);
        object_property_set_int(OBJECT(pnv_core), smp_threads, "nr-threads",
                                &error_fatal);
        object_property_set_int(OBJECT(pnv_core), core_hwid,
                                CPU_CORE_PROP_CORE_ID, &error_fatal);
        object_property_set_int(OBJECT(pnv_core),
                                pcc->core_pir(chip, core_hwid),
                                "pir", &error_fatal);
        object_property_set_bool(OBJECT(pnv_core), true, "realized",
                                 &error_fatal);
        object_unref(OBJECT(pnv_core));

        /* Each core has an XSCOM MMIO region */
        pnv_xscom_add_subregion(chip,
                                PNV_XSCOM_EX_CORE_BASE(pcc->xscom_core_base,
                                                       core_hwid),
                                &PNV_CORE(pnv_core)->xscom_regs);
        i++;
    }
    g_free(typename);

    /* Create LPC controller */
    object_property_set_bool(OBJECT(&chip->lpc), true, "realized",
                             &error_fatal);
    pnv_xscom_add_subregion(chip, PNV_XSCOM_LPC_BASE, &chip->lpc.xscom_regs);
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

    dc->realize = pnv_chip_realize;
    dc->props = pnv_chip_properties;
    dc->desc = "PowerNV Chip";
}

static const TypeInfo pnv_chip_info = {
    .name          = TYPE_PNV_CHIP,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .class_init    = pnv_chip_class_init,
    .instance_init = pnv_chip_init,
    .class_size    = sizeof(PnvChipClass),
    .abstract      = true,
};

static void pnv_get_num_chips(Object *obj, Visitor *v, const char *name,
                              void *opaque, Error **errp)
{
    visit_type_uint32(v, name, &POWERNV_MACHINE(obj)->num_chips, errp);
}

static void pnv_set_num_chips(Object *obj, Visitor *v, const char *name,
                              void *opaque, Error **errp)
{
    PnvMachineState *pnv = POWERNV_MACHINE(obj);
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

static void powernv_machine_initfn(Object *obj)
{
    PnvMachineState *pnv = POWERNV_MACHINE(obj);
    pnv->num_chips = 1;
}

static void powernv_machine_class_props_init(ObjectClass *oc)
{
    object_class_property_add(oc, "num-chips", "uint32_t",
                              pnv_get_num_chips, pnv_set_num_chips,
                              NULL, NULL, NULL);
    object_class_property_set_description(oc, "num-chips",
                              "Specifies the number of processor chips",
                              NULL);
}

static void powernv_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "IBM PowerNV (Non-Virtualized)";
    mc->init = ppc_powernv_init;
    mc->reset = ppc_powernv_reset;
    mc->max_cpus = MAX_CPUS;
    mc->block_default_type = IF_IDE; /* Pnv provides a AHCI device for
                                      * storage */
    mc->no_parallel = 1;
    mc->default_boot_order = NULL;
    mc->default_ram_size = 1 * G_BYTE;

    powernv_machine_class_props_init(oc);
}

static const TypeInfo powernv_machine_info = {
    .name          = TYPE_POWERNV_MACHINE,
    .parent        = TYPE_MACHINE,
    .instance_size = sizeof(PnvMachineState),
    .instance_init = powernv_machine_initfn,
    .class_init    = powernv_machine_class_init,
};

static void powernv_machine_register_types(void)
{
    type_register_static(&powernv_machine_info);
    type_register_static(&pnv_chip_info);
    type_register_static(&pnv_chip_power8e_info);
    type_register_static(&pnv_chip_power8_info);
    type_register_static(&pnv_chip_power8nvl_info);
    type_register_static(&pnv_chip_power9_info);
}

type_init(powernv_machine_register_types)
