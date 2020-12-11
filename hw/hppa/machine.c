/*
 * QEMU HPPA hardware system emulator.
 * Copyright 2018 Helge Deller <deller@gmx.de>
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/datadir.h"
#include "cpu.h"
#include "elf.h"
#include "hw/loader.h"
#include "hw/boards.h"
#include "qemu/error-report.h"
#include "sysemu/reset.h"
#include "sysemu/sysemu.h"
#include "sysemu/runstate.h"
#include "hw/rtc/mc146818rtc.h"
#include "hw/timer/i8254.h"
#include "hw/char/serial.h"
#include "hw/net/lasi_82596.h"
#include "hppa_sys.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "net/net.h"
#include "qemu/log.h"
#include "net/net.h"

#define MAX_IDE_BUS 2

#define MIN_SEABIOS_HPPA_VERSION 1 /* require at least this fw version */

#define HPA_POWER_BUTTON (FIRMWARE_END - 0x10)

static void hppa_powerdown_req(Notifier *n, void *opaque)
{
    hwaddr soft_power_reg = HPA_POWER_BUTTON;
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


static ISABus *hppa_isa_bus(void)
{
    ISABus *isa_bus;
    qemu_irq *isa_irqs;
    MemoryRegion *isa_region;

    isa_region = g_new(MemoryRegion, 1);
    memory_region_init_io(isa_region, NULL, &hppa_pci_ignore_ops,
                          NULL, "isa-io", 0x800);
    memory_region_add_subregion(get_system_memory(), IDE_HPA,
                                isa_region);

    isa_bus = isa_bus_new(NULL, get_system_memory(), isa_region,
                          &error_abort);
    isa_irqs = i8259_init(isa_bus,
                          /* qemu_allocate_irq(dino_set_isa_irq, s, 0)); */
                          NULL);
    isa_bus_irqs(isa_bus, isa_irqs);

    return isa_bus;
}

static uint64_t cpu_hppa_to_phys(void *opaque, uint64_t addr)
{
    addr &= (0x10000000 - 1);
    return addr;
}

static HPPACPU *cpu[HPPA_MAX_CPUS];
static uint64_t firmware_entry;

static void fw_cfg_boot_set(void *opaque, const char *boot_device,
                            Error **errp)
{
    fw_cfg_modify_i16(opaque, FW_CFG_BOOT_DEVICE, boot_device[0]);
}

static FWCfgState *create_fw_cfg(MachineState *ms)
{
    FWCfgState *fw_cfg;
    uint64_t val;

    fw_cfg = fw_cfg_init_mem(FW_CFG_IO_BASE, FW_CFG_IO_BASE + 4);
    fw_cfg_add_i16(fw_cfg, FW_CFG_NB_CPUS, ms->smp.cpus);
    fw_cfg_add_i16(fw_cfg, FW_CFG_MAX_CPUS, HPPA_MAX_CPUS);
    fw_cfg_add_i64(fw_cfg, FW_CFG_RAM_SIZE, ms->ram_size);

    val = cpu_to_le64(MIN_SEABIOS_HPPA_VERSION);
    fw_cfg_add_file(fw_cfg, "/etc/firmware-min-version",
                    g_memdup(&val, sizeof(val)), sizeof(val));

    val = cpu_to_le64(HPPA_TLB_ENTRIES);
    fw_cfg_add_file(fw_cfg, "/etc/cpu/tlb_entries",
                    g_memdup(&val, sizeof(val)), sizeof(val));

    val = cpu_to_le64(HPPA_BTLB_ENTRIES);
    fw_cfg_add_file(fw_cfg, "/etc/cpu/btlb_entries",
                    g_memdup(&val, sizeof(val)), sizeof(val));

    val = cpu_to_le64(HPA_POWER_BUTTON);
    fw_cfg_add_file(fw_cfg, "/etc/power-button-addr",
                    g_memdup(&val, sizeof(val)), sizeof(val));

    fw_cfg_add_i16(fw_cfg, FW_CFG_BOOT_DEVICE, ms->boot_order[0]);
    qemu_register_boot_set(fw_cfg_boot_set, fw_cfg);

    return fw_cfg;
}

static void machine_hppa_init(MachineState *machine)
{
    const char *kernel_filename = machine->kernel_filename;
    const char *kernel_cmdline = machine->kernel_cmdline;
    const char *initrd_filename = machine->initrd_filename;
    DeviceState *dev;
    PCIBus *pci_bus;
    ISABus *isa_bus;
    qemu_irq rtc_irq, serial_irq;
    char *firmware_filename;
    uint64_t firmware_low, firmware_high;
    long size;
    uint64_t kernel_entry = 0, kernel_low, kernel_high;
    MemoryRegion *addr_space = get_system_memory();
    MemoryRegion *rom_region;
    MemoryRegion *cpu_region;
    long i;
    unsigned int smp_cpus = machine->smp.cpus;
    SysBusDevice *s;

    /* Create CPUs.  */
    for (i = 0; i < smp_cpus; i++) {
        char *name = g_strdup_printf("cpu%ld-io-eir", i);
        cpu[i] = HPPA_CPU(cpu_create(machine->cpu_type));

        cpu_region = g_new(MemoryRegion, 1);
        memory_region_init_io(cpu_region, OBJECT(cpu[i]), &hppa_io_eir_ops,
                              cpu[i], name, 4);
        memory_region_add_subregion(addr_space, CPU_HPA + i * 0x1000,
                                    cpu_region);
        g_free(name);
    }

    /* Main memory region. */
    if (machine->ram_size > 3 * GiB) {
        error_report("RAM size is currently restricted to 3GB");
        exit(EXIT_FAILURE);
    }
    memory_region_add_subregion_overlap(addr_space, 0, machine->ram, -1);


    /* Init Lasi chip */
    lasi_init(addr_space);

    /* Init Dino (PCI host bus chip).  */
    pci_bus = dino_init(addr_space, &rtc_irq, &serial_irq);
    assert(pci_bus);

    /* Create ISA bus. */
    isa_bus = hppa_isa_bus();
    assert(isa_bus);

    /* Realtime clock, used by firmware for PDC_TOD call. */
    mc146818_rtc_init(isa_bus, 2000, rtc_irq);

    /* Serial code setup.  */
    if (serial_hd(0)) {
        uint32_t addr = DINO_UART_HPA + 0x800;
        serial_mm_init(addr_space, addr, 0, serial_irq,
                       115200, serial_hd(0), DEVICE_BIG_ENDIAN);
    }

    /* fw_cfg configuration interface */
    create_fw_cfg(machine);

    /* SCSI disk setup. */
    dev = DEVICE(pci_create_simple(pci_bus, -1, "lsi53c895a"));
    lsi53c8xx_handle_legacy_cmdline(dev);

    /* Graphics setup. */
    if (machine->enable_graphics && vga_interface_type != VGA_NONE) {
        dev = qdev_new("artist");
        s = SYS_BUS_DEVICE(dev);
        sysbus_realize_and_unref(s, &error_fatal);
        sysbus_mmio_map(s, 0, LASI_GFX_HPA);
        sysbus_mmio_map(s, 1, ARTIST_FB_ADDR);
    }

    /* Network setup. */
    for (i = 0; i < nb_nics; i++) {
        if (!enable_lasi_lan()) {
            pci_nic_init_nofail(&nd_table[i], pci_bus, "tulip", NULL);
        }
    }

    /* register power switch emulation */
    qemu_register_powerdown_notifier(&hppa_system_powerdown_notifier);

    /* Load firmware.  Given that this is not "real" firmware,
       but one explicitly written for the emulation, we might as
       well load it directly from an ELF image.  */
    firmware_filename = qemu_find_file(QEMU_FILE_TYPE_BIOS,
                                       machine->firmware ?: "hppa-firmware.img");
    if (firmware_filename == NULL) {
        error_report("no firmware provided");
        exit(1);
    }

    size = load_elf(firmware_filename, NULL, NULL, NULL,
                    &firmware_entry, &firmware_low, &firmware_high, NULL,
                    true, EM_PARISC, 0, 0);

    /* Unfortunately, load_elf sign-extends reading elf32.  */
    firmware_entry = (target_ureg)firmware_entry;
    firmware_low = (target_ureg)firmware_low;
    firmware_high = (target_ureg)firmware_high;

    if (size < 0) {
        error_report("could not load firmware '%s'", firmware_filename);
        exit(1);
    }
    qemu_log_mask(CPU_LOG_PAGE, "Firmware loaded at 0x%08" PRIx64
                  "-0x%08" PRIx64 ", entry at 0x%08" PRIx64 ".\n",
                  firmware_low, firmware_high, firmware_entry);
    if (firmware_low < FIRMWARE_START || firmware_high >= FIRMWARE_END) {
        error_report("Firmware overlaps with memory or IO space");
        exit(1);
    }
    g_free(firmware_filename);

    rom_region = g_new(MemoryRegion, 1);
    memory_region_init_ram(rom_region, NULL, "firmware",
                           (FIRMWARE_END - FIRMWARE_START), &error_fatal);
    memory_region_add_subregion(addr_space, FIRMWARE_START, rom_region);

    /* Load kernel */
    if (kernel_filename) {
        size = load_elf(kernel_filename, NULL, &cpu_hppa_to_phys,
                        NULL, &kernel_entry, &kernel_low, &kernel_high, NULL,
                        true, EM_PARISC, 0, 0);

        /* Unfortunately, load_elf sign-extends reading elf32.  */
        kernel_entry = (target_ureg) cpu_hppa_to_phys(NULL, kernel_entry);
        kernel_low = (target_ureg)kernel_low;
        kernel_high = (target_ureg)kernel_high;

        if (size < 0) {
            error_report("could not load kernel '%s'", kernel_filename);
            exit(1);
        }
        qemu_log_mask(CPU_LOG_PAGE, "Kernel loaded at 0x%08" PRIx64
                      "-0x%08" PRIx64 ", entry at 0x%08" PRIx64
                      ", size %" PRIu64 " kB\n",
                      kernel_low, kernel_high, kernel_entry, size / KiB);

        if (kernel_cmdline) {
            cpu[0]->env.gr[24] = 0x4000;
            pstrcpy_targphys("cmdline", cpu[0]->env.gr[24],
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
            cpu[0]->env.gr[23] = initrd_base;
            cpu[0]->env.gr[22] = initrd_base + initrd_size;
        }
    }

    if (!kernel_entry) {
        /* When booting via firmware, tell firmware if we want interactive
         * mode (kernel_entry=1), and to boot from CD (gr[24]='d')
         * or hard disc * (gr[24]='c').
         */
        kernel_entry = boot_menu ? 1 : 0;
        cpu[0]->env.gr[24] = machine->boot_order[0];
    }

    /* We jump to the firmware entry routine and pass the
     * various parameters in registers. After firmware initialization,
     * firmware will start the Linux kernel with ramdisk and cmdline.
     */
    cpu[0]->env.gr[26] = machine->ram_size;
    cpu[0]->env.gr[25] = kernel_entry;

    /* tell firmware how many SMP CPUs to present in inventory table */
    cpu[0]->env.gr[21] = smp_cpus;

    /* tell firmware fw_cfg port */
    cpu[0]->env.gr[19] = FW_CFG_IO_BASE;
}

static void hppa_machine_reset(MachineState *ms)
{
    unsigned int smp_cpus = ms->smp.cpus;
    int i;

    qemu_devices_reset();

    /* Start all CPUs at the firmware entry point.
     *  Monarch CPU will initialize firmware, secondary CPUs
     *  will enter a small idle look and wait for rendevouz. */
    for (i = 0; i < smp_cpus; i++) {
        cpu_set_pc(CPU(cpu[i]), firmware_entry);
        cpu[i]->env.gr[5] = CPU_HPA + i * 0x1000;
    }

    /* already initialized by machine_hppa_init()? */
    if (cpu[0]->env.gr[26] == ms->ram_size) {
        return;
    }

    cpu[0]->env.gr[26] = ms->ram_size;
    cpu[0]->env.gr[25] = 0; /* no firmware boot menu */
    cpu[0]->env.gr[24] = 'c';
    /* gr22/gr23 unused, no initrd while reboot. */
    cpu[0]->env.gr[21] = smp_cpus;
    /* tell firmware fw_cfg port */
    cpu[0]->env.gr[19] = FW_CFG_IO_BASE;
}


static void machine_hppa_machine_init(MachineClass *mc)
{
    mc->desc = "HPPA generic machine";
    mc->default_cpu_type = TYPE_HPPA_CPU;
    mc->init = machine_hppa_init;
    mc->reset = hppa_machine_reset;
    mc->block_default_type = IF_SCSI;
    mc->max_cpus = HPPA_MAX_CPUS;
    mc->default_cpus = 1;
    mc->is_default = true;
    mc->default_ram_size = 512 * MiB;
    mc->default_boot_order = "cd";
    mc->default_ram_id = "ram";
}

DEFINE_MACHINE("hppa", machine_hppa_machine_init)
