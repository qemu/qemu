/*
 * QEMU HPPA hardware system emulator.
 * (C) Copyright 2018-2023 Helge Deller <deller@gmx.de>
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 */

#include "qemu/osdep.h"
#include "qemu/datadir.h"
#include "cpu.h"
#include "elf.h"
#include "hw/loader.h"
#include "qemu/error-report.h"
#include "exec/target_page.h"
#include "system/reset.h"
#include "system/system.h"
#include "system/qtest.h"
#include "system/runstate.h"
#include "hw/rtc/mc146818rtc.h"
#include "hw/timer/i8254.h"
#include "hw/char/serial-mm.h"
#include "hw/char/parallel.h"
#include "hw/intc/i8259.h"
#include "hw/input/lasips2.h"
#include "hw/net/lasi_82596.h"
#include "hw/nmi.h"
#include "hw/usb.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "hw/pci-host/astro.h"
#include "hw/pci-host/dino.h"
#include "hw/misc/lasi.h"
#include "hppa_hardware.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "net/net.h"
#include "qemu/log.h"

#define MIN_SEABIOS_HPPA_VERSION 12 /* require at least this fw version */

#define HPA_POWER_BUTTON        (FIRMWARE_END - 0x10)
static hwaddr soft_power_reg;

#define enable_lasi_lan()       0

static DeviceState *lasi_dev;

static void hppa_powerdown_req(Notifier *n, void *opaque)
{
    uint32_t val;

    val = ldl_be_phys(&address_space_memory, soft_power_reg);
    if ((val >> 8) == 0) {
        /* immediately shut down when under hardware control */
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
        return;
    }

    /* clear bit 31 to indicate that the power switch was pressed. */
    val &= ~1;
    stl_be_phys(&address_space_memory, soft_power_reg, val);
}

static Notifier hppa_system_powerdown_notifier = {
    .notify = hppa_powerdown_req
};

/* Fallback for unassigned PCI I/O operations.  Avoids MCHK.  */
static uint64_t ignore_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void ignore_write(void *opaque, hwaddr addr, uint64_t v, unsigned size)
{
}

static const MemoryRegionOps hppa_pci_ignore_ops = {
    .read = ignore_read,
    .write = ignore_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

static ISABus *hppa_isa_bus(hwaddr addr)
{
    ISABus *isa_bus;
    qemu_irq *isa_irqs;
    MemoryRegion *isa_region;

    isa_region = g_new(MemoryRegion, 1);
    memory_region_init_io(isa_region, NULL, &hppa_pci_ignore_ops,
                          NULL, "isa-io", 0x800);
    memory_region_add_subregion(get_system_memory(), addr, isa_region);

    isa_bus = isa_bus_new(NULL, get_system_memory(), isa_region,
                          &error_abort);
    isa_irqs = i8259_init(isa_bus, NULL);
    isa_bus_register_input_irqs(isa_bus, isa_irqs);

    return isa_bus;
}

/*
 * Helper functions to emulate RTC clock and DebugOutputPort
 */
static time_t rtc_ref;

static uint64_t io_cpu_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;

    switch (addr) {
    case 0:             /* RTC clock */
        val = time(NULL);
        val += rtc_ref;
        break;
    case 8:             /* DebugOutputPort */
        return 0xe9;    /* readback */
    }
    return val;
}

static void io_cpu_write(void *opaque, hwaddr addr,
                         uint64_t val, unsigned size)
{
    unsigned char ch;
    Chardev *debugout;

    switch (addr) {
    case 0:             /* RTC clock */
        rtc_ref = val - time(NULL);
        break;
    case 8:             /* DebugOutputPort */
        ch = val;
        debugout = serial_hd(0);
        if (debugout) {
            qemu_chr_fe_write_all(debugout->be, &ch, 1);
        } else {
            fprintf(stderr, "%c", ch);
        }
        break;
    }
}

static const MemoryRegionOps hppa_io_helper_ops = {
    .read = io_cpu_read,
    .write = io_cpu_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

typedef uint64_t TranslateFn(void *opaque, uint64_t addr);

static uint64_t linux_kernel_virt_to_phys(void *opaque, uint64_t addr)
{
    addr &= (0x10000000 - 1);
    return addr;
}

static uint64_t translate_pa10(void *dummy, uint64_t addr)
{
    return (uint32_t)addr;
}

static uint64_t translate_pa20(void *dummy, uint64_t addr)
{
    return hppa_abs_to_phys_pa2_w0(addr);
}

static HPPACPU *cpu[HPPA_MAX_CPUS];
static uint64_t firmware_entry;

static void fw_cfg_boot_set(void *opaque, const char *boot_device,
                            Error **errp)
{
    fw_cfg_modify_i16(opaque, FW_CFG_BOOT_DEVICE, boot_device[0]);
}

static FWCfgState *create_fw_cfg(MachineState *ms, PCIBus *pci_bus,
                                 hwaddr addr)
{
    FWCfgState *fw_cfg;
    uint64_t val;
    const char qemu_version[] = QEMU_VERSION;
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    int btlb_entries = HPPA_BTLB_ENTRIES(&cpu[0]->env);
    int len;

    fw_cfg = fw_cfg_init_mem(addr, addr + 4);
    fw_cfg_add_i16(fw_cfg, FW_CFG_NB_CPUS, ms->smp.cpus);
    fw_cfg_add_i16(fw_cfg, FW_CFG_MAX_CPUS, HPPA_MAX_CPUS);
    fw_cfg_add_i64(fw_cfg, FW_CFG_RAM_SIZE, ms->ram_size);

    val = cpu_to_le64(MIN_SEABIOS_HPPA_VERSION);
    fw_cfg_add_file(fw_cfg, "/etc/firmware-min-version",
                    g_memdup2(&val, sizeof(val)), sizeof(val));

    val = cpu_to_le64(HPPA_TLB_ENTRIES - btlb_entries);
    fw_cfg_add_file(fw_cfg, "/etc/cpu/tlb_entries",
                    g_memdup2(&val, sizeof(val)), sizeof(val));

    val = cpu_to_le64(btlb_entries);
    fw_cfg_add_file(fw_cfg, "/etc/cpu/btlb_entries",
                    g_memdup2(&val, sizeof(val)), sizeof(val));

    len = strlen(mc->name) + 1;
    fw_cfg_add_file(fw_cfg, "/etc/hppa/machine",
                    g_memdup2(mc->name, len), len);

    val = cpu_to_le64(soft_power_reg);
    fw_cfg_add_file(fw_cfg, "/etc/hppa/power-button-addr",
                    g_memdup2(&val, sizeof(val)), sizeof(val));

    val = cpu_to_le64(CPU_HPA + 16);
    fw_cfg_add_file(fw_cfg, "/etc/hppa/rtc-addr",
                    g_memdup2(&val, sizeof(val)), sizeof(val));

    val = cpu_to_le64(CPU_HPA + 24);
    fw_cfg_add_file(fw_cfg, "/etc/hppa/DebugOutputPort",
                    g_memdup2(&val, sizeof(val)), sizeof(val));

    fw_cfg_add_i16(fw_cfg, FW_CFG_BOOT_DEVICE, ms->boot_config.order[0]);
    qemu_register_boot_set(fw_cfg_boot_set, fw_cfg);

    fw_cfg_add_file(fw_cfg, "/etc/qemu-version",
                    g_memdup2(qemu_version, sizeof(qemu_version)),
                    sizeof(qemu_version));

    pci_bus_add_fw_cfg_extra_pci_roots(fw_cfg, pci_bus, &error_abort);

    return fw_cfg;
}

static LasiState *lasi_init(void)
{
    DeviceState *dev;

    dev = qdev_new(TYPE_LASI_CHIP);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    return LASI_CHIP(dev);
}

static DinoState *dino_init(MemoryRegion *addr_space)
{
    DeviceState *dev;

    dev = qdev_new(TYPE_DINO_PCI_HOST_BRIDGE);
    object_property_set_link(OBJECT(dev), "memory-as", OBJECT(addr_space),
                             &error_fatal);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    return DINO_PCI_HOST_BRIDGE(dev);
}

/*
 * Step 1: Create CPUs and Memory
 */
static TranslateFn *machine_HP_common_init_cpus(MachineState *machine)
{
    MemoryRegion *addr_space = get_system_memory();
    unsigned int smp_cpus = machine->smp.cpus;
    TranslateFn *translate;
    MemoryRegion *cpu_region;
    uint64_t ram_max;

    /* Create CPUs.  */
    for (unsigned int i = 0; i < smp_cpus; i++) {
        cpu[i] = HPPA_CPU(cpu_create(machine->cpu_type));
    }

    /* Initialize memory */
    if (hppa_is_pa20(&cpu[0]->env)) {
        translate = translate_pa20;
        ram_max = 256 * GiB;       /* like HP rp8440 */
    } else {
        translate = translate_pa10;
        ram_max = FIRMWARE_START;  /* 3.75 GB (32-bit CPU) */
    }

    soft_power_reg = translate(NULL, HPA_POWER_BUTTON);

    for (unsigned int i = 0; i < smp_cpus; i++) {
        g_autofree char *name = g_strdup_printf("cpu%u-io-eir", i);

        cpu_region = g_new(MemoryRegion, 1);
        memory_region_init_io(cpu_region, OBJECT(cpu[i]), &hppa_io_eir_ops,
                              cpu[i], name, 4);
        memory_region_add_subregion(addr_space,
                                    translate(NULL, CPU_HPA + i * 0x1000),
                                    cpu_region);
    }

    /* RTC and DebugOutputPort on CPU #0 */
    cpu_region = g_new(MemoryRegion, 1);
    memory_region_init_io(cpu_region, OBJECT(cpu[0]), &hppa_io_helper_ops,
                          cpu[0], "cpu0-io-rtc", 2 * sizeof(uint64_t));
    memory_region_add_subregion(addr_space, translate(NULL, CPU_HPA + 16),
                                cpu_region);

    /* Main memory region. */
    if (machine->ram_size > ram_max) {
        info_report("Max RAM size limited to %" PRIu64 " MB", ram_max / MiB);
        machine->ram_size = ram_max;
    }
    if (machine->ram_size <= FIRMWARE_START) {
        /* contiguous memory up to 3.75 GB RAM */
        memory_region_add_subregion_overlap(addr_space, 0, machine->ram, -1);
    } else {
        /* non-contiguous: Memory above 3.75 GB is mapped at RAM_MAP_HIGH */
        MemoryRegion *mem_region;
        mem_region = g_new(MemoryRegion, 2);
        memory_region_init_alias(&mem_region[0], &addr_space->parent_obj,
                              "LowMem", machine->ram, 0, FIRMWARE_START);
        memory_region_init_alias(&mem_region[1], &addr_space->parent_obj,
                              "HighMem", machine->ram, FIRMWARE_START,
                              machine->ram_size - FIRMWARE_START);
        memory_region_add_subregion_overlap(addr_space, 0, &mem_region[0], -1);
        memory_region_add_subregion_overlap(addr_space, RAM_MAP_HIGH,
                                            &mem_region[1], -1);
    }

    return translate;
}

/*
 * Last creation step: Add SCSI discs, NICs, graphics & load firmware
 */
static void machine_HP_common_init_tail(MachineState *machine, PCIBus *pci_bus,
                                        TranslateFn *translate)
{
    const char *kernel_filename = machine->kernel_filename;
    const char *kernel_cmdline = machine->kernel_cmdline;
    const char *initrd_filename = machine->initrd_filename;
    const char *firmware = machine->firmware;
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    DeviceState *dev;
    PCIDevice *pci_dev;
    char *firmware_filename;
    uint64_t firmware_low, firmware_high;
    long size;
    uint64_t kernel_entry = 0, kernel_low, kernel_high;
    MemoryRegion *addr_space = get_system_memory();
    MemoryRegion *rom_region;
    SysBusDevice *s;

    /* SCSI disk setup. */
    if (drive_get_max_bus(IF_SCSI) >= 0) {
        dev = DEVICE(pci_create_simple(pci_bus, -1, "lsi53c895a"));
        lsi53c8xx_handle_legacy_cmdline(dev);
    }

    /* Graphics setup. */
    if (machine->enable_graphics && vga_interface_type != VGA_NONE) {
        dev = qdev_new("artist");
        s = SYS_BUS_DEVICE(dev);
        bool disabled = object_property_get_bool(OBJECT(dev), "disable", NULL);
        if (!disabled) {
            sysbus_realize_and_unref(s, &error_fatal);
            vga_interface_created = true;
            sysbus_mmio_map(s, 0, translate(NULL, LASI_GFX_HPA));
            sysbus_mmio_map(s, 1, translate(NULL, ARTIST_FB_ADDR));
        }
    }

    /* Network setup. */
    if (lasi_dev) {
        lasi_82596_init(addr_space, translate(NULL, LASI_LAN_HPA),
                        qdev_get_gpio_in(lasi_dev, LASI_IRQ_LAN_HPA),
                        enable_lasi_lan());
    }

    pci_init_nic_devices(pci_bus, mc->default_nic);

    /* BMC board: HP Diva GSP */
    dev = qdev_new("diva-gsp");
    if (!object_property_get_bool(OBJECT(dev), "disable", NULL)) {
        pci_dev = pci_new_multifunction(PCI_DEVFN(2, 0), "diva-gsp");
        if (!lasi_dev) {
            /* bind default keyboard/serial to Diva card */
            qdev_prop_set_chr(DEVICE(pci_dev), "chardev1", serial_hd(0));
            qdev_prop_set_chr(DEVICE(pci_dev), "chardev2", serial_hd(1));
            qdev_prop_set_chr(DEVICE(pci_dev), "chardev3", serial_hd(2));
            qdev_prop_set_chr(DEVICE(pci_dev), "chardev4", serial_hd(3));
        }
        pci_realize_and_unref(pci_dev, pci_bus, &error_fatal);
    }

    /* create USB OHCI controller for USB keyboard & mouse on Astro machines */
    if (!lasi_dev && machine->enable_graphics && defaults_enabled()) {
        USBBus *usb_bus;

        pci_create_simple(pci_bus, -1, "pci-ohci");
        usb_bus = USB_BUS(object_resolve_type_unambiguous(TYPE_USB_BUS,
                                                          &error_abort));
        usb_create_simple(usb_bus, "usb-kbd");
        usb_create_simple(usb_bus, "usb-mouse");
    }

    /* register power switch emulation */
    qemu_register_powerdown_notifier(&hppa_system_powerdown_notifier);

    /* fw_cfg configuration interface */
    create_fw_cfg(machine, pci_bus, translate(NULL, FW_CFG_IO_BASE));

    /* Load firmware.  Given that this is not "real" firmware,
       but one explicitly written for the emulation, we might as
       well load it directly from an ELF image. Load the 64-bit
       firmware on 64-bit machines by default if not specified
       on command line. */
    if (!qtest_enabled()) {
        if (!firmware) {
            firmware = lasi_dev ? "hppa-firmware.img" : "hppa-firmware64.img";
        }
        firmware_filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, firmware);
        if (firmware_filename == NULL) {
            error_report("no firmware provided");
            exit(1);
        }

        size = load_elf(firmware_filename, NULL, translate, NULL,
                        &firmware_entry, &firmware_low, &firmware_high, NULL,
                        ELFDATA2MSB, EM_PARISC, 0, 0);

        if (size < 0) {
            error_report("could not load firmware '%s'", firmware_filename);
            exit(1);
        }
        qemu_log_mask(CPU_LOG_PAGE, "Firmware loaded at 0x%08" PRIx64
                      "-0x%08" PRIx64 ", entry at 0x%08" PRIx64 ".\n",
                      firmware_low, firmware_high, firmware_entry);
        if (firmware_low < translate(NULL, FIRMWARE_START) ||
            firmware_high >= translate(NULL, FIRMWARE_END)) {
            error_report("Firmware overlaps with memory or IO space");
            exit(1);
        }
        g_free(firmware_filename);
    }

    rom_region = g_new(MemoryRegion, 1);
    memory_region_init_ram(rom_region, NULL, "firmware",
                           (FIRMWARE_END - FIRMWARE_START), &error_fatal);
    memory_region_add_subregion(addr_space,
                                translate(NULL, FIRMWARE_START), rom_region);

    /* Load kernel */
    if (kernel_filename) {
        size = load_elf(kernel_filename, NULL, linux_kernel_virt_to_phys,
                        NULL, &kernel_entry, &kernel_low, &kernel_high, NULL,
                        ELFDATA2MSB, EM_PARISC, 0, 0);

        kernel_entry = linux_kernel_virt_to_phys(NULL, kernel_entry);

        if (size < 0) {
            error_report("could not load kernel '%s'", kernel_filename);
            exit(1);
        }
        qemu_log_mask(CPU_LOG_PAGE, "Kernel loaded at 0x%08" PRIx64
                      "-0x%08" PRIx64 ", entry at 0x%08" PRIx64
                      ", size %" PRIu64 " kB\n",
                      kernel_low, kernel_high, kernel_entry, size / KiB);

        if (kernel_cmdline) {
            cpu[0]->env.cmdline_or_bootorder = 0x4000;
            pstrcpy_targphys("cmdline", cpu[0]->env.cmdline_or_bootorder,
                             TARGET_PAGE_SIZE, kernel_cmdline);
        }

        if (initrd_filename) {
            ram_addr_t initrd_base;
            int64_t initrd_size;

            initrd_size = get_image_size(initrd_filename);
            if (initrd_size < 0) {
                error_report("could not load initial ram disk '%s'",
                             initrd_filename);
                exit(1);
            }

            /* Load the initrd image high in memory.
               Mirror the algorithm used by palo:
               (1) Due to sign-extension problems and PDC,
               put the initrd no higher than 1G.
               (2) Reserve 64k for stack.  */
            initrd_base = MIN(machine->ram_size, 1 * GiB);
            initrd_base = initrd_base - 64 * KiB;
            initrd_base = (initrd_base - initrd_size) & TARGET_PAGE_MASK;

            if (initrd_base < kernel_high) {
                error_report("kernel and initial ram disk too large!");
                exit(1);
            }

            load_image_targphys(initrd_filename, initrd_base, initrd_size);
            cpu[0]->env.initrd_base = initrd_base;
            cpu[0]->env.initrd_end  = initrd_base + initrd_size;
        }
    }

    if (!kernel_entry) {
        /* When booting via firmware, tell firmware if we want interactive
         * mode (kernel_entry=1), and to boot from CD (cmdline_or_bootorder='d')
         * or hard disc (cmdline_or_bootorder='c').
         */
        kernel_entry = machine->boot_config.has_menu ? machine->boot_config.menu : 0;
        cpu[0]->env.cmdline_or_bootorder = machine->boot_config.order[0];
    }

    /* Keep initial kernel_entry for first boot */
    cpu[0]->env.kernel_entry = kernel_entry;
}

/*
 * Create HP B160L workstation
 */
static void machine_HP_B160L_init(MachineState *machine)
{
    DeviceState *dev, *dino_dev;
    MemoryRegion *addr_space = get_system_memory();
    TranslateFn *translate;
    ISABus *isa_bus;
    PCIBus *pci_bus;

    /* Create CPUs and RAM.  */
    translate = machine_HP_common_init_cpus(machine);

    if (hppa_is_pa20(&cpu[0]->env)) {
        error_report("The HP B160L workstation requires a 32-bit "
                     "CPU. Use '-machine C3700' instead.");
        exit(1);
    }

    /* Init Lasi chip */
    lasi_dev = DEVICE(lasi_init());
    memory_region_add_subregion(addr_space, translate(NULL, LASI_HPA),
                                sysbus_mmio_get_region(
                                    SYS_BUS_DEVICE(lasi_dev), 0));

    /* Init Dino (PCI host bus chip).  */
    dino_dev = DEVICE(dino_init(addr_space));
    memory_region_add_subregion(addr_space, translate(NULL, DINO_HPA),
                                sysbus_mmio_get_region(
                                    SYS_BUS_DEVICE(dino_dev), 0));
    pci_bus = PCI_BUS(qdev_get_child_bus(dino_dev, "pci"));
    assert(pci_bus);

    /* Create ISA bus, needed for PS/2 kbd/mouse port emulation */
    isa_bus = hppa_isa_bus(translate(NULL, IDE_HPA));
    assert(isa_bus);

    /* Serial ports: Lasi and Dino use a 7.272727 MHz clock. */
    serial_mm_init(addr_space, translate(NULL, LASI_UART_HPA + 0x800), 0,
        qdev_get_gpio_in(lasi_dev, LASI_IRQ_UART_HPA), 7272727 / 16,
        serial_hd(0), DEVICE_BIG_ENDIAN);

    serial_mm_init(addr_space, translate(NULL, DINO_UART_HPA + 0x800), 0,
        qdev_get_gpio_in(dino_dev, DINO_IRQ_RS232INT), 7272727 / 16,
        serial_hd(1), DEVICE_BIG_ENDIAN);

    /* Parallel port */
    parallel_mm_init(addr_space, translate(NULL, LASI_LPT_HPA + 0x800), 0,
                     qdev_get_gpio_in(lasi_dev, LASI_IRQ_LAN_HPA),
                     parallel_hds[0]);

    /* PS/2 Keyboard/Mouse */
    dev = qdev_new(TYPE_LASIPS2);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0,
                       qdev_get_gpio_in(lasi_dev, LASI_IRQ_PS2KBD_HPA));
    memory_region_add_subregion(addr_space,
                                translate(NULL, LASI_PS2KBD_HPA),
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(dev),
                                                       0));
    memory_region_add_subregion(addr_space,
                                translate(NULL, LASI_PS2KBD_HPA + 0x100),
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(dev),
                                                       1));

    /* Add SCSI discs, NICs, graphics & load firmware */
    machine_HP_common_init_tail(machine, pci_bus, translate);
}

static AstroState *astro_init(void)
{
    DeviceState *dev;

    dev = qdev_new(TYPE_ASTRO_CHIP);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    return ASTRO_CHIP(dev);
}

/*
 * Create HP C3700 workstation
 */
static void machine_HP_C3700_init(MachineState *machine)
{
    PCIBus *pci_bus;
    AstroState *astro;
    DeviceState *astro_dev;
    MemoryRegion *addr_space = get_system_memory();
    TranslateFn *translate;

    /* Create CPUs and RAM.  */
    translate = machine_HP_common_init_cpus(machine);

    if (!hppa_is_pa20(&cpu[0]->env)) {
        error_report("The HP C3000 workstation requires a 64-bit CPU. "
                     "Use '-machine B160L' instead.");
        exit(1);
    }

    /* Init Astro and the Elroys (PCI host bus chips).  */
    astro = astro_init();
    astro_dev = DEVICE(astro);
    memory_region_add_subregion(addr_space, translate(NULL, ASTRO_HPA),
                                sysbus_mmio_get_region(
                                    SYS_BUS_DEVICE(astro_dev), 0));
    pci_bus = PCI_BUS(qdev_get_child_bus(DEVICE(astro->elroy[0]), "pci"));
    assert(pci_bus);

    /* Add SCSI discs, NICs, graphics & load firmware */
    machine_HP_common_init_tail(machine, pci_bus, translate);
}

static void hppa_machine_reset(MachineState *ms, ResetType type)
{
    unsigned int smp_cpus = ms->smp.cpus;
    int i;

    qemu_devices_reset(type);

    /* Start all CPUs at the firmware entry point.
     *  Monarch CPU will initialize firmware, secondary CPUs
     *  will enter a small idle loop and wait for rendevouz. */
    for (i = 0; i < smp_cpus; i++) {
        CPUState *cs = CPU(cpu[i]);

        /* reset CPU */
        resettable_reset(OBJECT(cs), RESET_TYPE_COLD);

        cpu_set_pc(cs, firmware_entry);
        cpu[i]->env.psw = PSW_Q;
        cpu[i]->env.gr[5] = CPU_HPA + i * 0x1000;
    }

    cpu[0]->env.gr[26] = ms->ram_size;
    cpu[0]->env.gr[25] = cpu[0]->env.kernel_entry;
    cpu[0]->env.gr[24] = cpu[0]->env.cmdline_or_bootorder;
    cpu[0]->env.gr[23] = cpu[0]->env.initrd_base;
    cpu[0]->env.gr[22] = cpu[0]->env.initrd_end;
    cpu[0]->env.gr[21] = smp_cpus;
    cpu[0]->env.gr[19] = FW_CFG_IO_BASE;

    /* reset static fields to avoid starting Linux kernel & initrd on reboot */
    cpu[0]->env.kernel_entry = 0;
    cpu[0]->env.initrd_base = 0;
    cpu[0]->env.initrd_end = 0;
    cpu[0]->env.cmdline_or_bootorder = 'c';
}

static void hppa_nmi(NMIState *n, int cpu_index, Error **errp)
{
    CPUState *cs;

    CPU_FOREACH(cs) {
        cpu_interrupt(cs, CPU_INTERRUPT_NMI);
    }
}

static void HP_B160L_machine_init_class_init(ObjectClass *oc, void *data)
{
    static const char * const valid_cpu_types[] = {
        TYPE_HPPA_CPU,
        NULL
    };
    MachineClass *mc = MACHINE_CLASS(oc);
    NMIClass *nc = NMI_CLASS(oc);

    mc->desc = "HP B160L workstation";
    mc->default_cpu_type = TYPE_HPPA_CPU;
    mc->valid_cpu_types = valid_cpu_types;
    mc->init = machine_HP_B160L_init;
    mc->reset = hppa_machine_reset;
    mc->block_default_type = IF_SCSI;
    mc->max_cpus = HPPA_MAX_CPUS;
    mc->default_cpus = 1;
    mc->is_default = true;
    mc->default_ram_size = 512 * MiB;
    mc->default_boot_order = "cd";
    mc->default_ram_id = "ram";
    mc->default_nic = "tulip";

    nc->nmi_monitor_handler = hppa_nmi;
}

static const TypeInfo HP_B160L_machine_init_typeinfo = {
    .name = MACHINE_TYPE_NAME("B160L"),
    .parent = TYPE_MACHINE,
    .class_init = HP_B160L_machine_init_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_NMI },
        { }
    },
};

static void HP_C3700_machine_init_class_init(ObjectClass *oc, void *data)
{
    static const char * const valid_cpu_types[] = {
        TYPE_HPPA64_CPU,
        NULL
    };
    MachineClass *mc = MACHINE_CLASS(oc);
    NMIClass *nc = NMI_CLASS(oc);

    mc->desc = "HP C3700 workstation";
    mc->default_cpu_type = TYPE_HPPA64_CPU;
    mc->valid_cpu_types = valid_cpu_types;
    mc->init = machine_HP_C3700_init;
    mc->reset = hppa_machine_reset;
    mc->block_default_type = IF_SCSI;
    mc->max_cpus = HPPA_MAX_CPUS;
    mc->default_cpus = 1;
    mc->is_default = false;
    mc->default_ram_size = 1024 * MiB;
    mc->default_boot_order = "cd";
    mc->default_ram_id = "ram";
    mc->default_nic = "tulip";

    nc->nmi_monitor_handler = hppa_nmi;
}

static const TypeInfo HP_C3700_machine_init_typeinfo = {
    .name = MACHINE_TYPE_NAME("C3700"),
    .parent = TYPE_MACHINE,
    .class_init = HP_C3700_machine_init_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_NMI },
        { }
    },
};

static void hppa_machine_init_register_types(void)
{
    type_register_static(&HP_B160L_machine_init_typeinfo);
    type_register_static(&HP_C3700_machine_init_typeinfo);
}

type_init(hppa_machine_init_register_types)
