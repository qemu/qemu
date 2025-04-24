/*
 * QEMU PowerPC CHRP (Genesi/bPlan Pegasos II) hardware System Emulator
 *
 * Copyright (c) 2018-2021 BALATON Zoltan
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 *
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/ppc/ppc.h"
#include "hw/sysbus.h"
#include "hw/pci/pci_host.h"
#include "hw/irq.h"
#include "hw/or-irq.h"
#include "hw/pci-host/mv64361.h"
#include "hw/isa/vt82c686.h"
#include "hw/ide/pci.h"
#include "hw/i2c/smbus_eeprom.h"
#include "hw/qdev-properties.h"
#include "system/reset.h"
#include "system/runstate.h"
#include "system/qtest.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/fw-path-provider.h"
#include "elf.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "system/kvm.h"
#include "kvm_ppc.h"
#include "system/address-spaces.h"
#include "qom/qom-qobject.h"
#include "qobject/qdict.h"
#include "trace.h"
#include "qemu/datadir.h"
#include "system/device_tree.h"
#include "hw/ppc/vof.h"

#include <libfdt.h>

#define PROM_FILENAME "vof.bin"
#define PROM_ADDR     0xfff00000
#define PROM_SIZE     0x80000

#define INITRD_MIN_ADDR 0x600000

#define KVMPPC_HCALL_BASE    0xf000
#define KVMPPC_H_RTAS        (KVMPPC_HCALL_BASE + 0x0)
#define KVMPPC_H_VOF_CLIENT  (KVMPPC_HCALL_BASE + 0x5)

#define H_SUCCESS     0
#define H_PRIVILEGE  -3  /* Caller not privileged */
#define H_PARAMETER  -4  /* Parameter invalid, out-of-range or conflicting */

#define BUS_FREQ_HZ 133333333

#define PCI0_CFG_ADDR 0xcf8
#define PCI0_MEM_BASE 0xc0000000
#define PCI0_MEM_SIZE 0x20000000
#define PCI0_IO_BASE  0xf8000000
#define PCI0_IO_SIZE  0x10000

#define PCI1_CFG_ADDR 0xc78
#define PCI1_MEM_BASE 0x80000000
#define PCI1_MEM_SIZE 0x40000000
#define PCI1_IO_BASE  0xfe000000
#define PCI1_IO_SIZE  0x10000

#define TYPE_PEGASOS2_MACHINE  MACHINE_TYPE_NAME("pegasos2")
OBJECT_DECLARE_TYPE(Pegasos2MachineState, MachineClass, PEGASOS2_MACHINE)

struct Pegasos2MachineState {
    MachineState parent_obj;

    PowerPCCPU *cpu;
    DeviceState *mv;
    IRQState pci_irqs[PCI_NUM_PINS];
    OrIRQState orirq[PCI_NUM_PINS];
    qemu_irq mv_pirq[PCI_NUM_PINS];
    qemu_irq via_pirq[PCI_NUM_PINS];
    Vof *vof;
    void *fdt_blob;
    uint64_t kernel_addr;
    uint64_t kernel_entry;
    uint64_t kernel_size;
    uint64_t initrd_addr;
    uint64_t initrd_size;
};

static void *build_fdt(MachineState *machine, int *fdt_size);

static void pegasos2_cpu_reset(void *opaque)
{
    PowerPCCPU *cpu = opaque;
    Pegasos2MachineState *pm = PEGASOS2_MACHINE(current_machine);

    cpu_reset(CPU(cpu));
    cpu->env.spr[SPR_HID1] = 7ULL << 28;
    if (pm->vof) {
        cpu->env.gpr[1] = 2 * VOF_STACK_SIZE - 0x20;
        cpu->env.nip = 0x100;
    }
    cpu_ppc_tb_reset(&cpu->env);
}

static void pegasos2_pci_irq(void *opaque, int n, int level)
{
    Pegasos2MachineState *pm = opaque;

    /* PCI interrupt lines are connected to both MV64361 and VT8231 */
    qemu_set_irq(pm->mv_pirq[n], level);
    qemu_set_irq(pm->via_pirq[n], level);
}

static void pegasos2_init(MachineState *machine)
{
    Pegasos2MachineState *pm = PEGASOS2_MACHINE(machine);
    CPUPPCState *env;
    MemoryRegion *rom = g_new(MemoryRegion, 1);
    PCIBus *pci_bus;
    Object *via;
    PCIDevice *dev;
    I2CBus *i2c_bus;
    const char *fwname = machine->firmware ?: PROM_FILENAME;
    char *filename;
    int i;
    ssize_t sz;
    uint8_t *spd_data;

    /* init CPU */
    pm->cpu = POWERPC_CPU(cpu_create(machine->cpu_type));
    env = &pm->cpu->env;
    if (PPC_INPUT(env) != PPC_FLAGS_INPUT_6xx) {
        error_report("Incompatible CPU, only 6xx bus supported");
        exit(1);
    }

    /* Set time-base frequency */
    cpu_ppc_tb_init(env, BUS_FREQ_HZ / 4);
    qemu_register_reset(pegasos2_cpu_reset, pm->cpu);

    /* RAM */
    if (machine->ram_size > 2 * GiB) {
        error_report("RAM size more than 2 GiB is not supported");
        exit(1);
    }
    memory_region_add_subregion(get_system_memory(), 0, machine->ram);

    /* allocate and load firmware */
    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, fwname);
    if (!filename) {
        error_report("Could not find firmware '%s'", fwname);
        exit(1);
    }
    if (!machine->firmware && !pm->vof) {
        pm->vof = g_malloc0(sizeof(*pm->vof));
    }
    memory_region_init_rom(rom, NULL, "pegasos2.rom", PROM_SIZE, &error_fatal);
    memory_region_add_subregion(get_system_memory(), PROM_ADDR, rom);
    sz = load_elf(filename, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                  ELFDATA2MSB, PPC_ELF_MACHINE, 0, 0);
    if (sz <= 0) {
        sz = load_image_targphys(filename, pm->vof ? 0 : PROM_ADDR, PROM_SIZE);
    }
    if (sz <= 0 || sz > PROM_SIZE) {
        error_report("Could not load firmware '%s'", filename);
        exit(1);
    }
    g_free(filename);
    if (pm->vof) {
        pm->vof->fw_size = sz;
    }

    /* Marvell Discovery II system controller */
    pm->mv = DEVICE(sysbus_create_simple(TYPE_MV64361, -1,
                          qdev_get_gpio_in(DEVICE(pm->cpu), PPC6xx_INPUT_INT)));
    for (i = 0; i < PCI_NUM_PINS; i++) {
        pm->mv_pirq[i] = qdev_get_gpio_in_named(pm->mv, "gpp", 12 + i);
    }
    pci_bus = mv64361_get_pci_bus(pm->mv, 1);

    /* VIA VT8231 South Bridge (multifunction PCI device) */
    via = OBJECT(pci_new_multifunction(PCI_DEVFN(12, 0), TYPE_VT8231_ISA));

    /* Set properties on individual devices before realizing the south bridge */
    if (machine->audiodev) {
        dev = PCI_DEVICE(object_resolve_path_component(via, "ac97"));
        qdev_prop_set_string(DEVICE(dev), "audiodev", machine->audiodev);
    }

    pci_realize_and_unref(PCI_DEVICE(via), pci_bus, &error_abort);
    for (i = 0; i < PCI_NUM_PINS; i++) {
        pm->via_pirq[i] = qdev_get_gpio_in_named(DEVICE(via), "pirq", i);
    }
    object_property_add_alias(OBJECT(machine), "rtc-time",
                              object_resolve_path_component(via, "rtc"),
                              "date");
    qdev_connect_gpio_out_named(DEVICE(via), "intr", 0,
                                qdev_get_gpio_in_named(pm->mv, "gpp", 31));

    dev = PCI_DEVICE(object_resolve_path_component(via, "ide"));
    pci_ide_create_devs(dev);

    dev = PCI_DEVICE(object_resolve_path_component(via, "pm"));
    i2c_bus = I2C_BUS(qdev_get_child_bus(DEVICE(dev), "i2c"));
    spd_data = spd_data_generate(DDR, machine->ram_size);
    smbus_eeprom_init_one(i2c_bus, 0x57, spd_data);

    /* other PC hardware */
    pci_vga_init(pci_bus);

    /* PCI interrupt routing: lines from pci.0 and pci.1 are ORed */
    for (int h = 0; h < 2; h++) {
        DeviceState *pd;
        g_autofree const char *pn = g_strdup_printf("pcihost%d", h);

        pd = DEVICE(object_resolve_path_component(OBJECT(pm->mv), pn));
        assert(pd);
        for (i = 0; i < PCI_NUM_PINS; i++) {
            OrIRQState *ori = &pm->orirq[i];

            if (h == 0) {
                g_autofree const char *n = g_strdup_printf("pci-orirq[%d]", i);

                object_initialize_child_with_props(OBJECT(pm), n,
                                                   ori, sizeof(*ori),
                                                   TYPE_OR_IRQ, &error_fatal,
                                                   "num-lines", "2", NULL);
                qdev_realize(DEVICE(ori), NULL, &error_fatal);
                qemu_init_irq(&pm->pci_irqs[i], pegasos2_pci_irq, pm, i);
                qdev_connect_gpio_out(DEVICE(ori), 0, &pm->pci_irqs[i]);
            }
            qdev_connect_gpio_out(pd, i, qdev_get_gpio_in(DEVICE(ori), h));
        }
    }

    if (machine->kernel_filename) {
        sz = load_elf(machine->kernel_filename, NULL, NULL, NULL,
                      &pm->kernel_entry, &pm->kernel_addr, NULL, NULL,
                      ELFDATA2MSB, PPC_ELF_MACHINE, 0, 0);
        if (sz <= 0) {
            error_report("Could not load kernel '%s'",
                         machine->kernel_filename);
            exit(1);
        }
        pm->kernel_size = sz;
        if (!pm->vof) {
            warn_report("Option -kernel may be ineffective with -bios.");
        }
    } else if (pm->vof && !qtest_enabled()) {
        warn_report("Using Virtual OpenFirmware but no -kernel option.");
    }

    if (machine->initrd_filename) {
        pm->initrd_addr = pm->kernel_addr + pm->kernel_size + 64 * KiB;
        pm->initrd_addr = ROUND_UP(pm->initrd_addr, 4);
        pm->initrd_addr = MAX(pm->initrd_addr, INITRD_MIN_ADDR);
        sz = load_image_targphys(machine->initrd_filename, pm->initrd_addr,
                                 machine->ram_size - pm->initrd_addr);
        if (sz <= 0) {
            error_report("Could not load initrd '%s'",
                         machine->initrd_filename);
            exit(1);
        }
        pm->initrd_size = sz;
    }

    if (!pm->vof && machine->kernel_cmdline && machine->kernel_cmdline[0]) {
        warn_report("Option -append may be ineffective with -bios.");
    }
}

static uint32_t pegasos2_mv_reg_read(Pegasos2MachineState *pm,
                                     uint32_t addr, uint32_t len)
{
    MemoryRegion *r = sysbus_mmio_get_region(SYS_BUS_DEVICE(pm->mv), 0);
    uint64_t val = 0xffffffffULL;
    memory_region_dispatch_read(r, addr, &val, size_memop(len) | MO_LE,
                                MEMTXATTRS_UNSPECIFIED);
    return val;
}

static void pegasos2_mv_reg_write(Pegasos2MachineState *pm, uint32_t addr,
                                  uint32_t len, uint32_t val)
{
    MemoryRegion *r = sysbus_mmio_get_region(SYS_BUS_DEVICE(pm->mv), 0);
    memory_region_dispatch_write(r, addr, val, size_memop(len) | MO_LE,
                                 MEMTXATTRS_UNSPECIFIED);
}

static uint32_t pegasos2_pci_config_read(Pegasos2MachineState *pm, int bus,
                                         uint32_t addr, uint32_t len)
{
    hwaddr pcicfg = bus ? PCI1_CFG_ADDR : PCI0_CFG_ADDR;
    uint64_t val = 0xffffffffULL;

    if (len <= 4) {
        pegasos2_mv_reg_write(pm, pcicfg, 4, addr | BIT(31));
        val = pegasos2_mv_reg_read(pm, pcicfg + 4, len);
    }
    return val;
}

static void pegasos2_pci_config_write(Pegasos2MachineState *pm, int bus,
                                      uint32_t addr, uint32_t len, uint32_t val)
{
    hwaddr pcicfg = bus ? PCI1_CFG_ADDR : PCI0_CFG_ADDR;

    pegasos2_mv_reg_write(pm, pcicfg, 4, addr | BIT(31));
    pegasos2_mv_reg_write(pm, pcicfg + 4, len, val);
}

static void pegasos2_superio_write(uint8_t addr, uint8_t val)
{
    cpu_physical_memory_write(PCI1_IO_BASE + 0x3f0, &addr, 1);
    cpu_physical_memory_write(PCI1_IO_BASE + 0x3f1, &val, 1);
}

static void pegasos2_machine_reset(MachineState *machine, ResetType type)
{
    Pegasos2MachineState *pm = PEGASOS2_MACHINE(machine);
    void *fdt;
    uint64_t d[2];
    int sz;

    qemu_devices_reset(type);
    if (!pm->vof) {
        return; /* Firmware should set up machine so nothing to do */
    }

    /* Otherwise, set up devices that board firmware would normally do */
    pegasos2_mv_reg_write(pm, 0, 4, 0x28020ff);
    pegasos2_mv_reg_write(pm, 0x278, 4, 0xa31fc);
    pegasos2_mv_reg_write(pm, 0xf300, 4, 0x11ff0400);
    pegasos2_mv_reg_write(pm, 0xf10c, 4, 0x80000000);
    pegasos2_mv_reg_write(pm, 0x1c, 4, 0x8000000);
    pegasos2_pci_config_write(pm, 0, PCI_COMMAND, 2, PCI_COMMAND_IO |
                              PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
    pegasos2_pci_config_write(pm, 1, PCI_COMMAND, 2, PCI_COMMAND_IO |
                              PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);

    pegasos2_pci_config_write(pm, 1, (PCI_DEVFN(12, 0) << 8) |
                              PCI_INTERRUPT_LINE, 2, 0x9);
    pegasos2_pci_config_write(pm, 1, (PCI_DEVFN(12, 0) << 8) |
                              0x50, 1, 0x6);
    pegasos2_superio_write(0xf4, 0xbe);
    pegasos2_superio_write(0xf6, 0xef);
    pegasos2_superio_write(0xf7, 0xfc);
    pegasos2_superio_write(0xf2, 0x14);
    pegasos2_pci_config_write(pm, 1, (PCI_DEVFN(12, 0) << 8) |
                              0x50, 1, 0x2);
    pegasos2_pci_config_write(pm, 1, (PCI_DEVFN(12, 0) << 8) |
                              0x55, 1, 0x90);
    pegasos2_pci_config_write(pm, 1, (PCI_DEVFN(12, 0) << 8) |
                              0x56, 1, 0x99);
    pegasos2_pci_config_write(pm, 1, (PCI_DEVFN(12, 0) << 8) |
                              0x57, 1, 0x90);

    pegasos2_pci_config_write(pm, 1, (PCI_DEVFN(12, 1) << 8) |
                              PCI_INTERRUPT_LINE, 2, 0x109);
    pegasos2_pci_config_write(pm, 1, (PCI_DEVFN(12, 1) << 8) |
                              PCI_CLASS_PROG, 1, 0xf);
    pegasos2_pci_config_write(pm, 1, (PCI_DEVFN(12, 1) << 8) |
                              0x40, 1, 0xb);
    pegasos2_pci_config_write(pm, 1, (PCI_DEVFN(12, 1) << 8) |
                              0x50, 4, 0x17171717);
    pegasos2_pci_config_write(pm, 1, (PCI_DEVFN(12, 1) << 8) |
                              PCI_COMMAND, 2, 0x87);

    pegasos2_pci_config_write(pm, 1, (PCI_DEVFN(12, 2) << 8) |
                              PCI_INTERRUPT_LINE, 2, 0x409);
    pegasos2_pci_config_write(pm, 1, (PCI_DEVFN(12, 2) << 8) |
                              PCI_COMMAND, 2, 0x7);

    pegasos2_pci_config_write(pm, 1, (PCI_DEVFN(12, 3) << 8) |
                              PCI_INTERRUPT_LINE, 2, 0x409);
    pegasos2_pci_config_write(pm, 1, (PCI_DEVFN(12, 3) << 8) |
                              PCI_COMMAND, 2, 0x7);

    pegasos2_pci_config_write(pm, 1, (PCI_DEVFN(12, 4) << 8) |
                              PCI_INTERRUPT_LINE, 2, 0x9);
    pegasos2_pci_config_write(pm, 1, (PCI_DEVFN(12, 4) << 8) |
                              0x48, 4, 0xf00);
    pegasos2_pci_config_write(pm, 1, (PCI_DEVFN(12, 4) << 8) |
                              0x40, 4, 0x558020);
    pegasos2_pci_config_write(pm, 1, (PCI_DEVFN(12, 4) << 8) |
                              0x90, 4, 0xd00);

    pegasos2_pci_config_write(pm, 1, (PCI_DEVFN(12, 5) << 8) |
                              PCI_INTERRUPT_LINE, 2, 0x309);

    pegasos2_pci_config_write(pm, 1, (PCI_DEVFN(12, 6) << 8) |
                              PCI_INTERRUPT_LINE, 2, 0x309);

    /* Device tree and VOF set up */
    vof_init(pm->vof, machine->ram_size, &error_fatal);
    if (vof_claim(pm->vof, 0, VOF_STACK_SIZE, VOF_STACK_SIZE) == -1) {
        error_report("Memory allocation for stack failed");
        exit(1);
    }
    if (pm->kernel_size &&
        vof_claim(pm->vof, pm->kernel_addr, pm->kernel_size, 0) == -1) {
        error_report("Memory for kernel is in use");
        exit(1);
    }
    if (pm->initrd_size &&
        vof_claim(pm->vof, pm->initrd_addr, pm->initrd_size, 0) == -1) {
        error_report("Memory for initrd is in use");
        exit(1);
    }
    fdt = build_fdt(machine, &sz);
    /* FIXME: VOF assumes entry is same as load address */
    d[0] = cpu_to_be64(pm->kernel_entry);
    d[1] = cpu_to_be64(pm->kernel_size - (pm->kernel_entry - pm->kernel_addr));
    qemu_fdt_setprop(fdt, "/chosen", "qemu,boot-kernel", d, sizeof(d));

    g_free(pm->fdt_blob);
    pm->fdt_blob = fdt;

    vof_build_dt(fdt, pm->vof);
    vof_client_open_store(fdt, pm->vof, "/chosen", "stdout", "/failsafe");

    /* Set machine->fdt for 'dumpdtb' QMP/HMP command */
    machine->fdt = fdt;

    pm->cpu->vhyp = PPC_VIRTUAL_HYPERVISOR(machine);
    pm->cpu->vhyp_class = PPC_VIRTUAL_HYPERVISOR_GET_CLASS(pm->cpu->vhyp);
}

enum pegasos2_rtas_tokens {
    RTAS_RESTART_RTAS = 0,
    RTAS_NVRAM_FETCH = 1,
    RTAS_NVRAM_STORE = 2,
    RTAS_GET_TIME_OF_DAY = 3,
    RTAS_SET_TIME_OF_DAY = 4,
    RTAS_EVENT_SCAN = 6,
    RTAS_CHECK_EXCEPTION = 7,
    RTAS_READ_PCI_CONFIG = 8,
    RTAS_WRITE_PCI_CONFIG = 9,
    RTAS_DISPLAY_CHARACTER = 10,
    RTAS_SET_INDICATOR = 11,
    RTAS_POWER_OFF = 17,
    RTAS_SUSPEND = 18,
    RTAS_HIBERNATE = 19,
    RTAS_SYSTEM_REBOOT = 20,
};

static target_ulong pegasos2_rtas(PowerPCCPU *cpu, Pegasos2MachineState *pm,
                                  target_ulong args_real)
{
    AddressSpace *as = CPU(cpu)->as;
    uint32_t token = ldl_be_phys(as, args_real);
    uint32_t nargs = ldl_be_phys(as, args_real + 4);
    uint32_t nrets = ldl_be_phys(as, args_real + 8);
    uint32_t args = args_real + 12;
    uint32_t rets = args_real + 12 + nargs * 4;

    if (nrets < 1) {
        qemu_log_mask(LOG_GUEST_ERROR, "Too few return values in RTAS call\n");
        return H_PARAMETER;
    }
    switch (token) {
    case RTAS_GET_TIME_OF_DAY:
    {
        QObject *qo = object_property_get_qobject(qdev_get_machine(),
                                                  "rtc-time", &error_fatal);
        QDict *qd = qobject_to(QDict, qo);

        if (nargs != 0 || nrets != 8 || !qd) {
            stl_be_phys(as, rets, -1);
            qobject_unref(qo);
            return H_PARAMETER;
        }

        stl_be_phys(as, rets, 0);
        stl_be_phys(as, rets + 4, qdict_get_int(qd, "tm_year") + 1900);
        stl_be_phys(as, rets + 8, qdict_get_int(qd, "tm_mon") + 1);
        stl_be_phys(as, rets + 12, qdict_get_int(qd, "tm_mday"));
        stl_be_phys(as, rets + 16, qdict_get_int(qd, "tm_hour"));
        stl_be_phys(as, rets + 20, qdict_get_int(qd, "tm_min"));
        stl_be_phys(as, rets + 24, qdict_get_int(qd, "tm_sec"));
        stl_be_phys(as, rets + 28, 0);
        qobject_unref(qo);
        return H_SUCCESS;
    }
    case RTAS_READ_PCI_CONFIG:
    {
        uint32_t addr, len, val;

        if (nargs != 2 || nrets != 2) {
            stl_be_phys(as, rets, -1);
            return H_PARAMETER;
        }
        addr = ldl_be_phys(as, args);
        len = ldl_be_phys(as, args + 4);
        val = pegasos2_pci_config_read(pm, !(addr >> 24),
                                       addr & 0x0fffffff, len);
        stl_be_phys(as, rets, 0);
        stl_be_phys(as, rets + 4, val);
        return H_SUCCESS;
    }
    case RTAS_WRITE_PCI_CONFIG:
    {
        uint32_t addr, len, val;

        if (nargs != 3 || nrets != 1) {
            stl_be_phys(as, rets, -1);
            return H_PARAMETER;
        }
        addr = ldl_be_phys(as, args);
        len = ldl_be_phys(as, args + 4);
        val = ldl_be_phys(as, args + 8);
        pegasos2_pci_config_write(pm, !(addr >> 24),
                                  addr & 0x0fffffff, len, val);
        stl_be_phys(as, rets, 0);
        return H_SUCCESS;
    }
    case RTAS_DISPLAY_CHARACTER:
        if (nargs != 1 || nrets != 1) {
            stl_be_phys(as, rets, -1);
            return H_PARAMETER;
        }
        qemu_log_mask(LOG_UNIMP, "%c", ldl_be_phys(as, args));
        stl_be_phys(as, rets, 0);
        return H_SUCCESS;
    case RTAS_POWER_OFF:
    {
        if (nargs != 2 || nrets != 1) {
            stl_be_phys(as, rets, -1);
            return H_PARAMETER;
        }
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
        stl_be_phys(as, rets, 0);
        return H_SUCCESS;
    }
    default:
        qemu_log_mask(LOG_UNIMP, "Unknown RTAS token %u (args=%u, rets=%u)\n",
                      token, nargs, nrets);
        stl_be_phys(as, rets, 0);
        return H_SUCCESS;
    }
}

static bool pegasos2_cpu_in_nested(PowerPCCPU *cpu)
{
    return false;
}

static void pegasos2_hypercall(PPCVirtualHypervisor *vhyp, PowerPCCPU *cpu)
{
    Pegasos2MachineState *pm = PEGASOS2_MACHINE(vhyp);
    CPUPPCState *env = &cpu->env;

    /* The TCG path should also be holding the BQL at this point */
    g_assert(bql_locked());

    if (FIELD_EX64(env->msr, MSR, PR)) {
        qemu_log_mask(LOG_GUEST_ERROR, "Hypercall made with MSR[PR]=1\n");
        env->gpr[3] = H_PRIVILEGE;
    } else if (env->gpr[3] == KVMPPC_H_RTAS) {
        env->gpr[3] = pegasos2_rtas(cpu, pm, env->gpr[4]);
    } else if (env->gpr[3] == KVMPPC_H_VOF_CLIENT) {
        int ret = vof_client_call(MACHINE(pm), pm->vof, pm->fdt_blob,
                                  env->gpr[4]);
        env->gpr[3] = (ret ? H_PARAMETER : H_SUCCESS);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "Unsupported hypercall " TARGET_FMT_lx
                      "\n", env->gpr[3]);
        env->gpr[3] = -1;
    }
}

static void vhyp_nop(PPCVirtualHypervisor *vhyp, PowerPCCPU *cpu)
{
}

static target_ulong vhyp_encode_hpt_for_kvm_pr(PPCVirtualHypervisor *vhyp)
{
    return POWERPC_CPU(current_cpu)->env.spr[SPR_SDR1];
}

static bool pegasos2_setprop(MachineState *ms, const char *path,
                             const char *propname, void *val, int vallen)
{
    return true;
}

static void pegasos2_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    PPCVirtualHypervisorClass *vhc = PPC_VIRTUAL_HYPERVISOR_CLASS(oc);
    VofMachineIfClass *vmc = VOF_MACHINE_CLASS(oc);

    mc->desc = "Genesi/bPlan Pegasos II";
    mc->init = pegasos2_init;
    mc->reset = pegasos2_machine_reset;
    mc->block_default_type = IF_IDE;
    mc->default_boot_order = "cd";
    mc->default_display = "std";
    mc->default_cpu_type = POWERPC_CPU_TYPE_NAME("7457_v1.2");
    mc->default_ram_id = "pegasos2.ram";
    mc->default_ram_size = 512 * MiB;
    machine_add_audiodev_property(mc);

    vhc->cpu_in_nested = pegasos2_cpu_in_nested;
    vhc->hypercall = pegasos2_hypercall;
    vhc->cpu_exec_enter = vhyp_nop;
    vhc->cpu_exec_exit = vhyp_nop;
    vhc->encode_hpt_for_kvm_pr = vhyp_encode_hpt_for_kvm_pr;

    vmc->setprop = pegasos2_setprop;
}

static const TypeInfo pegasos2_machine_info = {
    .name          = TYPE_PEGASOS2_MACHINE,
    .parent        = TYPE_MACHINE,
    .class_init    = pegasos2_machine_class_init,
    .instance_size = sizeof(Pegasos2MachineState),
    .interfaces = (InterfaceInfo[]) {
        { TYPE_PPC_VIRTUAL_HYPERVISOR },
        { TYPE_VOF_MACHINE_IF },
        { }
    },
};

static void pegasos2_machine_register_types(void)
{
    type_register_static(&pegasos2_machine_info);
}

type_init(pegasos2_machine_register_types)

/* FDT creation for passing to firmware */

typedef struct {
    void *fdt;
    const char *path;
} FDTInfo;

/* We do everything in reverse order so it comes out right in the tree */

static void dt_ide(PCIBus *bus, PCIDevice *d, FDTInfo *fi)
{
    qemu_fdt_setprop_string(fi->fdt, fi->path, "device_type", "spi");
}

static void dt_usb(PCIBus *bus, PCIDevice *d, FDTInfo *fi)
{
    qemu_fdt_setprop_cell(fi->fdt, fi->path, "#size-cells", 0);
    qemu_fdt_setprop_cell(fi->fdt, fi->path, "#address-cells", 1);
    qemu_fdt_setprop_string(fi->fdt, fi->path, "device_type", "usb");
}

static void dt_isa(PCIBus *bus, PCIDevice *d, FDTInfo *fi)
{
    GString *name = g_string_sized_new(64);
    uint32_t cells[3];

    qemu_fdt_setprop_cell(fi->fdt, fi->path, "#size-cells", 1);
    qemu_fdt_setprop_cell(fi->fdt, fi->path, "#address-cells", 2);
    qemu_fdt_setprop_string(fi->fdt, fi->path, "device_type", "isa");
    qemu_fdt_setprop_string(fi->fdt, fi->path, "name", "isa");

    /* additional devices */
    g_string_printf(name, "%s/lpt@i3bc", fi->path);
    qemu_fdt_add_subnode(fi->fdt, name->str);
    qemu_fdt_setprop_cell(fi->fdt, name->str, "clock-frequency", 0);
    cells[0] = cpu_to_be32(7);
    cells[1] = 0;
    qemu_fdt_setprop(fi->fdt, name->str, "interrupts",
                     cells, 2 * sizeof(cells[0]));
    cells[0] = cpu_to_be32(1);
    cells[1] = cpu_to_be32(0x3bc);
    cells[2] = cpu_to_be32(8);
    qemu_fdt_setprop(fi->fdt, name->str, "reg", cells, 3 * sizeof(cells[0]));
    qemu_fdt_setprop_string(fi->fdt, name->str, "device_type", "lpt");
    qemu_fdt_setprop_string(fi->fdt, name->str, "name", "lpt");

    g_string_printf(name, "%s/fdc@i3f0", fi->path);
    qemu_fdt_add_subnode(fi->fdt, name->str);
    qemu_fdt_setprop_cell(fi->fdt, name->str, "clock-frequency", 0);
    cells[0] = cpu_to_be32(6);
    cells[1] = 0;
    qemu_fdt_setprop(fi->fdt, name->str, "interrupts",
                     cells, 2 * sizeof(cells[0]));
    cells[0] = cpu_to_be32(1);
    cells[1] = cpu_to_be32(0x3f0);
    cells[2] = cpu_to_be32(8);
    qemu_fdt_setprop(fi->fdt, name->str, "reg", cells, 3 * sizeof(cells[0]));
    qemu_fdt_setprop_string(fi->fdt, name->str, "device_type", "fdc");
    qemu_fdt_setprop_string(fi->fdt, name->str, "name", "fdc");

    g_string_printf(name, "%s/timer@i40", fi->path);
    qemu_fdt_add_subnode(fi->fdt, name->str);
    qemu_fdt_setprop_cell(fi->fdt, name->str, "clock-frequency", 0);
    cells[0] = cpu_to_be32(1);
    cells[1] = cpu_to_be32(0x40);
    cells[2] = cpu_to_be32(8);
    qemu_fdt_setprop(fi->fdt, name->str, "reg", cells, 3 * sizeof(cells[0]));
    qemu_fdt_setprop_string(fi->fdt, name->str, "device_type", "timer");
    qemu_fdt_setprop_string(fi->fdt, name->str, "name", "timer");

    g_string_printf(name, "%s/rtc@i70", fi->path);
    qemu_fdt_add_subnode(fi->fdt, name->str);
    qemu_fdt_setprop_string(fi->fdt, name->str, "compatible", "ds1385-rtc");
    qemu_fdt_setprop_cell(fi->fdt, name->str, "clock-frequency", 0);
    cells[0] = cpu_to_be32(8);
    cells[1] = 0;
    qemu_fdt_setprop(fi->fdt, name->str, "interrupts",
                     cells, 2 * sizeof(cells[0]));
    cells[0] = cpu_to_be32(1);
    cells[1] = cpu_to_be32(0x70);
    cells[2] = cpu_to_be32(2);
    qemu_fdt_setprop(fi->fdt, name->str, "reg", cells, 3 * sizeof(cells[0]));
    qemu_fdt_setprop_string(fi->fdt, name->str, "device_type", "rtc");
    qemu_fdt_setprop_string(fi->fdt, name->str, "name", "rtc");

    g_string_printf(name, "%s/keyboard@i60", fi->path);
    qemu_fdt_add_subnode(fi->fdt, name->str);
    cells[0] = cpu_to_be32(1);
    cells[1] = 0;
    qemu_fdt_setprop(fi->fdt, name->str, "interrupts",
                     cells, 2 * sizeof(cells[0]));
    cells[0] = cpu_to_be32(1);
    cells[1] = cpu_to_be32(0x60);
    cells[2] = cpu_to_be32(5);
    qemu_fdt_setprop(fi->fdt, name->str, "reg", cells, 3 * sizeof(cells[0]));
    qemu_fdt_setprop_string(fi->fdt, name->str, "device_type", "keyboard");
    qemu_fdt_setprop_string(fi->fdt, name->str, "name", "keyboard");

    g_string_printf(name, "%s/8042@i60", fi->path);
    qemu_fdt_add_subnode(fi->fdt, name->str);
    qemu_fdt_setprop_cell(fi->fdt, name->str, "#interrupt-cells", 2);
    qemu_fdt_setprop_cell(fi->fdt, name->str, "#size-cells", 0);
    qemu_fdt_setprop_cell(fi->fdt, name->str, "#address-cells", 1);
    qemu_fdt_setprop_string(fi->fdt, name->str, "interrupt-controller", "");
    qemu_fdt_setprop_cell(fi->fdt, name->str, "clock-frequency", 0);
    cells[0] = cpu_to_be32(1);
    cells[1] = cpu_to_be32(0x60);
    cells[2] = cpu_to_be32(5);
    qemu_fdt_setprop(fi->fdt, name->str, "reg", cells, 3 * sizeof(cells[0]));
    qemu_fdt_setprop_string(fi->fdt, name->str, "device_type", "");
    qemu_fdt_setprop_string(fi->fdt, name->str, "name", "8042");

    g_string_printf(name, "%s/serial@i2f8", fi->path);
    qemu_fdt_add_subnode(fi->fdt, name->str);
    qemu_fdt_setprop_cell(fi->fdt, name->str, "clock-frequency", 0);
    cells[0] = cpu_to_be32(3);
    cells[1] = 0;
    qemu_fdt_setprop(fi->fdt, name->str, "interrupts",
                     cells, 2 * sizeof(cells[0]));
    cells[0] = cpu_to_be32(1);
    cells[1] = cpu_to_be32(0x2f8);
    cells[2] = cpu_to_be32(8);
    qemu_fdt_setprop(fi->fdt, name->str, "reg", cells, 3 * sizeof(cells[0]));
    qemu_fdt_setprop_string(fi->fdt, name->str, "device_type", "serial");
    qemu_fdt_setprop_string(fi->fdt, name->str, "name", "serial");

    g_string_free(name, TRUE);
}

static struct {
    const char *id;
    const char *name;
    void (*dtf)(PCIBus *bus, PCIDevice *d, FDTInfo *fi);
} device_map[] = {
    { "pci11ab,6460", "host", NULL },
    { "pci1106,8231", "isa", dt_isa },
    { "pci1106,571", "ide", dt_ide },
    { "pci1106,3044", "firewire", NULL },
    { "pci1106,3038", "usb", dt_usb },
    { "pci1106,8235", "other", NULL },
    { "pci1106,3058", "sound", NULL },
    { NULL, NULL }
};

static void add_pci_device(PCIBus *bus, PCIDevice *d, void *opaque)
{
    FDTInfo *fi = opaque;
    GString *node = g_string_new(NULL);
    uint32_t cells[(PCI_NUM_REGIONS + 1) * 5];
    int i, j;
    const char *name = NULL;
    g_autofree const gchar *pn = g_strdup_printf("pci%x,%x",
                                     pci_get_word(&d->config[PCI_VENDOR_ID]),
                                     pci_get_word(&d->config[PCI_DEVICE_ID]));

    if (pci_get_word(&d->config[PCI_CLASS_DEVICE])  ==
        PCI_CLASS_NETWORK_ETHERNET) {
        name = "ethernet";
    } else if (pci_get_word(&d->config[PCI_CLASS_DEVICE]) >> 8 ==
        PCI_BASE_CLASS_DISPLAY) {
        name = "display";
    }
    for (i = 0; device_map[i].id; i++) {
        if (!strcmp(pn, device_map[i].id)) {
            name = device_map[i].name;
            break;
        }
    }
    g_string_printf(node, "%s/%s@%x", fi->path, (name ?: pn),
                    PCI_SLOT(d->devfn));
    if (PCI_FUNC(d->devfn)) {
        g_string_append_printf(node, ",%x", PCI_FUNC(d->devfn));
    }

    qemu_fdt_add_subnode(fi->fdt, node->str);
    if (device_map[i].dtf) {
        FDTInfo cfi = { fi->fdt, node->str };
        device_map[i].dtf(bus, d, &cfi);
    }
    cells[0] = cpu_to_be32(d->devfn << 8);
    cells[1] = 0;
    cells[2] = 0;
    cells[3] = 0;
    cells[4] = 0;
    j = 5;
    for (i = 0; i < PCI_NUM_REGIONS; i++) {
        if (!d->io_regions[i].size) {
            continue;
        }
        cells[j] = PCI_BASE_ADDRESS_0 + i * 4;
        if (cells[j] == 0x28) {
            cells[j] = 0x30;
        }
        cells[j] = cpu_to_be32(d->devfn << 8 | cells[j]);
        if (d->io_regions[i].type & PCI_BASE_ADDRESS_SPACE_IO) {
            cells[j] |= cpu_to_be32(1 << 24);
        } else {
            if (d->io_regions[i].type & PCI_BASE_ADDRESS_MEM_TYPE_64) {
                cells[j] |= cpu_to_be32(3 << 24);
            } else {
                cells[j] |= cpu_to_be32(2 << 24);
            }
            if (d->io_regions[i].type & PCI_BASE_ADDRESS_MEM_PREFETCH) {
                cells[j] |= cpu_to_be32(4 << 28);
            }
        }
        cells[j + 1] = 0;
        cells[j + 2] = 0;
        cells[j + 3] = cpu_to_be32(d->io_regions[i].size >> 32);
        cells[j + 4] = cpu_to_be32(d->io_regions[i].size);
        j += 5;
    }
    qemu_fdt_setprop(fi->fdt, node->str, "reg", cells, j * sizeof(cells[0]));
    qemu_fdt_setprop_string(fi->fdt, node->str, "name", name ?: pn);
    if (pci_get_byte(&d->config[PCI_INTERRUPT_PIN])) {
        qemu_fdt_setprop_cell(fi->fdt, node->str, "interrupts",
                              pci_get_byte(&d->config[PCI_INTERRUPT_PIN]));
    }
    /* Pegasos2 firmware has subsystem-id amd subsystem-vendor-id swapped */
    qemu_fdt_setprop_cell(fi->fdt, node->str, "subsystem-vendor-id",
                          pci_get_word(&d->config[PCI_SUBSYSTEM_ID]));
    qemu_fdt_setprop_cell(fi->fdt, node->str, "subsystem-id",
                          pci_get_word(&d->config[PCI_SUBSYSTEM_VENDOR_ID]));
    cells[0] = pci_get_long(&d->config[PCI_CLASS_REVISION]);
    qemu_fdt_setprop_cell(fi->fdt, node->str, "class-code", cells[0] >> 8);
    qemu_fdt_setprop_cell(fi->fdt, node->str, "revision-id", cells[0] & 0xff);
    qemu_fdt_setprop_cell(fi->fdt, node->str, "device-id",
                          pci_get_word(&d->config[PCI_DEVICE_ID]));
    qemu_fdt_setprop_cell(fi->fdt, node->str, "vendor-id",
                          pci_get_word(&d->config[PCI_VENDOR_ID]));

    g_string_free(node, TRUE);
}

static void *build_fdt(MachineState *machine, int *fdt_size)
{
    Pegasos2MachineState *pm = PEGASOS2_MACHINE(machine);
    PowerPCCPU *cpu = pm->cpu;
    PCIBus *pci_bus;
    FDTInfo fi;
    uint32_t cells[16];
    void *fdt = create_device_tree(fdt_size);

    fi.fdt = fdt;

    /* root node */
    qemu_fdt_setprop_string(fdt, "/", "CODEGEN,description",
                            "Pegasos CHRP PowerPC System");
    qemu_fdt_setprop_string(fdt, "/", "CODEGEN,board", "Pegasos2");
    qemu_fdt_setprop_string(fdt, "/", "CODEGEN,vendor", "bplan GmbH");
    qemu_fdt_setprop_string(fdt, "/", "revision", "2B");
    qemu_fdt_setprop_string(fdt, "/", "model", "Pegasos2");
    qemu_fdt_setprop_string(fdt, "/", "device_type", "chrp");
    qemu_fdt_setprop_cell(fdt, "/", "#address-cells", 1);
    qemu_fdt_setprop_string(fdt, "/", "name", "bplan,Pegasos2");

    /* pci@c0000000 */
    qemu_fdt_add_subnode(fdt, "/pci@c0000000");
    cells[0] = 0;
    cells[1] = 0;
    qemu_fdt_setprop(fdt, "/pci@c0000000", "bus-range",
                     cells, 2 * sizeof(cells[0]));
    qemu_fdt_setprop_cell(fdt, "/pci@c0000000", "pci-bridge-number", 1);
    cells[0] = cpu_to_be32(PCI0_MEM_BASE);
    cells[1] = cpu_to_be32(PCI0_MEM_SIZE);
    qemu_fdt_setprop(fdt, "/pci@c0000000", "reg", cells, 2 * sizeof(cells[0]));
    cells[0] = cpu_to_be32(0x01000000);
    cells[1] = 0;
    cells[2] = 0;
    cells[3] = cpu_to_be32(PCI0_IO_BASE);
    cells[4] = 0;
    cells[5] = cpu_to_be32(PCI0_IO_SIZE);
    cells[6] = cpu_to_be32(0x02000000);
    cells[7] = 0;
    cells[8] = cpu_to_be32(PCI0_MEM_BASE);
    cells[9] = cpu_to_be32(PCI0_MEM_BASE);
    cells[10] = 0;
    cells[11] = cpu_to_be32(PCI0_MEM_SIZE);
    qemu_fdt_setprop(fdt, "/pci@c0000000", "ranges",
                     cells, 12 * sizeof(cells[0]));
    qemu_fdt_setprop_cell(fdt, "/pci@c0000000", "#size-cells", 2);
    qemu_fdt_setprop_cell(fdt, "/pci@c0000000", "#address-cells", 3);
    qemu_fdt_setprop_string(fdt, "/pci@c0000000", "device_type", "pci");
    qemu_fdt_setprop_string(fdt, "/pci@c0000000", "name", "pci");

    fi.path = "/pci@c0000000";
    pci_bus = mv64361_get_pci_bus(pm->mv, 0);
    pci_for_each_device_reverse(pci_bus, 0, add_pci_device, &fi);

    /* pci@80000000 */
    qemu_fdt_add_subnode(fdt, "/pci@80000000");
    cells[0] = 0;
    cells[1] = 0;
    qemu_fdt_setprop(fdt, "/pci@80000000", "bus-range",
                     cells, 2 * sizeof(cells[0]));
    qemu_fdt_setprop_cell(fdt, "/pci@80000000", "pci-bridge-number", 0);
    cells[0] = cpu_to_be32(PCI1_MEM_BASE);
    cells[1] = cpu_to_be32(PCI1_MEM_SIZE);
    qemu_fdt_setprop(fdt, "/pci@80000000", "reg", cells, 2 * sizeof(cells[0]));
    qemu_fdt_setprop_cell(fdt, "/pci@80000000", "8259-interrupt-acknowledge",
                          0xf1000cb4);
    cells[0] = cpu_to_be32(0x01000000);
    cells[1] = 0;
    cells[2] = 0;
    cells[3] = cpu_to_be32(PCI1_IO_BASE);
    cells[4] = 0;
    cells[5] = cpu_to_be32(PCI1_IO_SIZE);
    cells[6] = cpu_to_be32(0x02000000);
    cells[7] = 0;
    cells[8] = cpu_to_be32(PCI1_MEM_BASE);
    cells[9] = cpu_to_be32(PCI1_MEM_BASE);
    cells[10] = 0;
    cells[11] = cpu_to_be32(PCI1_MEM_SIZE);
    qemu_fdt_setprop(fdt, "/pci@80000000", "ranges",
                     cells, 12 * sizeof(cells[0]));
    qemu_fdt_setprop_cell(fdt, "/pci@80000000", "#size-cells", 2);
    qemu_fdt_setprop_cell(fdt, "/pci@80000000", "#address-cells", 3);
    qemu_fdt_setprop_string(fdt, "/pci@80000000", "device_type", "pci");
    qemu_fdt_setprop_string(fdt, "/pci@80000000", "name", "pci");

    fi.path = "/pci@80000000";
    pci_bus = mv64361_get_pci_bus(pm->mv, 1);
    pci_for_each_device_reverse(pci_bus, 0, add_pci_device, &fi);

    qemu_fdt_add_subnode(fdt, "/failsafe");
    qemu_fdt_setprop_string(fdt, "/failsafe", "device_type", "serial");
    qemu_fdt_setprop_string(fdt, "/failsafe", "name", "failsafe");

    qemu_fdt_add_subnode(fdt, "/rtas");
    qemu_fdt_setprop_cell(fdt, "/rtas", "system-reboot", RTAS_SYSTEM_REBOOT);
    qemu_fdt_setprop_cell(fdt, "/rtas", "hibernate", RTAS_HIBERNATE);
    qemu_fdt_setprop_cell(fdt, "/rtas", "suspend", RTAS_SUSPEND);
    qemu_fdt_setprop_cell(fdt, "/rtas", "power-off", RTAS_POWER_OFF);
    qemu_fdt_setprop_cell(fdt, "/rtas", "set-indicator", RTAS_SET_INDICATOR);
    qemu_fdt_setprop_cell(fdt, "/rtas", "display-character",
                          RTAS_DISPLAY_CHARACTER);
    qemu_fdt_setprop_cell(fdt, "/rtas", "write-pci-config",
                          RTAS_WRITE_PCI_CONFIG);
    qemu_fdt_setprop_cell(fdt, "/rtas", "read-pci-config",
                          RTAS_READ_PCI_CONFIG);
    /* Pegasos2 firmware misspells check-exception and guests use that */
    qemu_fdt_setprop_cell(fdt, "/rtas", "check-execption",
                          RTAS_CHECK_EXCEPTION);
    qemu_fdt_setprop_cell(fdt, "/rtas", "event-scan", RTAS_EVENT_SCAN);
    qemu_fdt_setprop_cell(fdt, "/rtas", "set-time-of-day",
                          RTAS_SET_TIME_OF_DAY);
    qemu_fdt_setprop_cell(fdt, "/rtas", "get-time-of-day",
                          RTAS_GET_TIME_OF_DAY);
    qemu_fdt_setprop_cell(fdt, "/rtas", "nvram-store", RTAS_NVRAM_STORE);
    qemu_fdt_setprop_cell(fdt, "/rtas", "nvram-fetch", RTAS_NVRAM_FETCH);
    qemu_fdt_setprop_cell(fdt, "/rtas", "restart-rtas", RTAS_RESTART_RTAS);
    qemu_fdt_setprop_cell(fdt, "/rtas", "rtas-error-log-max", 0);
    qemu_fdt_setprop_cell(fdt, "/rtas", "rtas-event-scan-rate", 0);
    qemu_fdt_setprop_cell(fdt, "/rtas", "rtas-display-device", 0);
    qemu_fdt_setprop_cell(fdt, "/rtas", "rtas-size", 20);
    qemu_fdt_setprop_cell(fdt, "/rtas", "rtas-version", 1);
    qemu_fdt_setprop_string(fdt, "/rtas", "name", "rtas");

    /* cpus */
    qemu_fdt_add_subnode(fdt, "/cpus");
    qemu_fdt_setprop_cell(fdt, "/cpus", "#cpus", 1);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#address-cells", 1);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#size-cells", 0);
    qemu_fdt_setprop_string(fdt, "/cpus", "name", "cpus");

    /* FIXME Get CPU name from CPU object */
    const char *cp = "/cpus/PowerPC,G4";
    qemu_fdt_add_subnode(fdt, cp);
    qemu_fdt_setprop_cell(fdt, cp, "l2cr", 0);
    qemu_fdt_setprop_cell(fdt, cp, "d-cache-size", 0x8000);
    qemu_fdt_setprop_cell(fdt, cp, "d-cache-block-size",
                          cpu->env.dcache_line_size);
    qemu_fdt_setprop_cell(fdt, cp, "d-cache-line-size",
                          cpu->env.dcache_line_size);
    qemu_fdt_setprop_cell(fdt, cp, "i-cache-size", 0x8000);
    qemu_fdt_setprop_cell(fdt, cp, "i-cache-block-size",
                          cpu->env.icache_line_size);
    qemu_fdt_setprop_cell(fdt, cp, "i-cache-line-size",
                          cpu->env.icache_line_size);
    if (ppc_is_split_tlb(cpu)) {
        qemu_fdt_setprop_cell(fdt, cp, "i-tlb-sets", cpu->env.nb_ways);
        qemu_fdt_setprop_cell(fdt, cp, "i-tlb-size", cpu->env.tlb_per_way);
        qemu_fdt_setprop_cell(fdt, cp, "d-tlb-sets", cpu->env.nb_ways);
        qemu_fdt_setprop_cell(fdt, cp, "d-tlb-size", cpu->env.tlb_per_way);
        qemu_fdt_setprop_string(fdt, cp, "tlb-split", "");
    }
    qemu_fdt_setprop_cell(fdt, cp, "tlb-sets", cpu->env.nb_ways);
    qemu_fdt_setprop_cell(fdt, cp, "tlb-size", cpu->env.nb_tlb);
    qemu_fdt_setprop_string(fdt, cp, "state", "running");
    if (cpu->env.insns_flags & PPC_ALTIVEC) {
        qemu_fdt_setprop_string(fdt, cp, "altivec", "");
        qemu_fdt_setprop_string(fdt, cp, "data-streams", "");
    }
    /*
     * FIXME What flags do data-streams, external-control and
     * performance-monitor depend on?
     */
    qemu_fdt_setprop_string(fdt, cp, "external-control", "");
    if (cpu->env.insns_flags & PPC_FLOAT_FSQRT) {
        qemu_fdt_setprop_string(fdt, cp, "general-purpose", "");
    }
    qemu_fdt_setprop_string(fdt, cp, "performance-monitor", "");
    if (cpu->env.insns_flags & PPC_FLOAT_FRES) {
        qemu_fdt_setprop_string(fdt, cp, "graphics", "");
    }
    qemu_fdt_setprop_cell(fdt, cp, "reservation-granule-size", 4);
    qemu_fdt_setprop_cell(fdt, cp, "timebase-frequency",
                          cpu->env.tb_env->tb_freq);
    qemu_fdt_setprop_cell(fdt, cp, "bus-frequency", BUS_FREQ_HZ);
    qemu_fdt_setprop_cell(fdt, cp, "clock-frequency", BUS_FREQ_HZ * 7.5);
    qemu_fdt_setprop_cell(fdt, cp, "cpu-version", cpu->env.spr[SPR_PVR]);
    cells[0] = 0;
    cells[1] = 0;
    qemu_fdt_setprop(fdt, cp, "reg", cells, 2 * sizeof(cells[0]));
    qemu_fdt_setprop_string(fdt, cp, "device_type", "cpu");
    qemu_fdt_setprop_string(fdt, cp, "name", strrchr(cp, '/') + 1);

    /* memory */
    qemu_fdt_add_subnode(fdt, "/memory@0");
    cells[0] = 0;
    cells[1] = cpu_to_be32(machine->ram_size);
    qemu_fdt_setprop(fdt, "/memory@0", "reg", cells, 2 * sizeof(cells[0]));
    qemu_fdt_setprop_string(fdt, "/memory@0", "device_type", "memory");
    qemu_fdt_setprop_string(fdt, "/memory@0", "name", "memory");

    qemu_fdt_add_subnode(fdt, "/chosen");
    if (pm->initrd_addr && pm->initrd_size) {
        qemu_fdt_setprop_cell(fdt, "/chosen", "linux,initrd-end",
                              pm->initrd_addr + pm->initrd_size);
        qemu_fdt_setprop_cell(fdt, "/chosen", "linux,initrd-start",
                              pm->initrd_addr);
    }
    qemu_fdt_setprop_string(fdt, "/chosen", "bootargs",
                            machine->kernel_cmdline ?: "");
    qemu_fdt_setprop_string(fdt, "/chosen", "name", "chosen");

    qemu_fdt_add_subnode(fdt, "/openprom");
    qemu_fdt_setprop_string(fdt, "/openprom", "model", "Pegasos2,1.1");

    return fdt;
}
