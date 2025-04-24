/*
 * QEMU aCube Sam460ex board emulation
 *
 * Copyright (c) 2012 François Revol
 * Copyright (c) 2016-2019 BALATON Zoltan
 *
 * This file is derived from hw/ppc440_bamboo.c,
 * the copyright for that material belongs to the original owners.
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 *
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/datadir.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "system/kvm.h"
#include "kvm_ppc.h"
#include "system/device_tree.h"
#include "system/block-backend.h"
#include "exec/page-protection.h"
#include "hw/loader.h"
#include "elf.h"
#include "system/memory.h"
#include "ppc440.h"
#include "hw/pci-host/ppc4xx.h"
#include "hw/block/flash.h"
#include "system/system.h"
#include "system/reset.h"
#include "hw/sysbus.h"
#include "hw/char/serial-mm.h"
#include "hw/i2c/ppc4xx_i2c.h"
#include "hw/i2c/smbus_eeprom.h"
#include "hw/ide/pci.h"
#include "hw/usb/hcd-ehci.h"
#include "hw/ppc/fdt.h"
#include "hw/qdev-properties.h"
#include "hw/intc/ppc-uic.h"

#include <libfdt.h>

#define BINARY_DEVICE_TREE_FILE "canyonlands.dtb"
#define UBOOT_FILENAME "u-boot-sam460-20100605.bin"
/* to extract the official U-Boot bin from the updater: */
/* dd bs=1 skip=$(($(stat -c '%s' updater/updater-460) - 0x80000)) \
     if=updater/updater-460 of=u-boot-sam460-20100605.bin */

#define PCIE0_DCRN_BASE 0x100
#define PCIE1_DCRN_BASE 0x120

/* from Sam460 U-Boot include/configs/Sam460ex.h */
#define FLASH_BASE             0xfff00000
#define FLASH_BASE_H           0x4
#define FLASH_SIZE             (1 * MiB)
#define UBOOT_LOAD_BASE        0xfff80000
#define UBOOT_SIZE             0x00080000
#define UBOOT_ENTRY            0xfffffffc

/* from U-Boot */
#define EPAPR_MAGIC           (0x45504150)
#define KERNEL_ADDR           0x1000000
#define FDT_ADDR              0x1800000
#define RAMDISK_ADDR          0x1900000

/* Sam460ex IRQ MAP:
   IRQ0  = ETH_INT
   IRQ1  = FPGA_INT
   IRQ2  = PCI_INT (PCIA, PCIB, PCIC, PCIB)
   IRQ3  = FPGA_INT2
   IRQ11 = RTC_INT
   IRQ12 = SM502_INT
*/

#define CPU_FREQ 1150000000
#define PLB_FREQ 230000000
#define OPB_FREQ 115000000
#define EBC_FREQ 115000000
#define UART_FREQ 11059200

struct boot_info {
    uint32_t dt_base;
    uint32_t dt_size;
    uint32_t entry;
};

static int sam460ex_load_uboot(void)
{
    /*
     * This first creates 1MiB of flash memory mapped at the end of
     * the 32-bit address space (0xFFF00000..0xFFFFFFFF).
     *
     * If_PFLASH unit 0 is defined, the flash memory is initialized
     * from that block backend.
     *
     * Else, it's initialized to zero.  And then 512KiB of ROM get
     * mapped on top of its second half (0xFFF80000..0xFFFFFFFF),
     * initialized from u-boot-sam460-20100605.bin.
     *
     * This doesn't smell right.
     *
     * The physical hardware appears to have 512KiB flash memory.
     *
     * TODO Figure out what we really need here, and clean this up.
     */

    DriveInfo *dinfo;

    dinfo = drive_get(IF_PFLASH, 0, 0);
    if (!pflash_cfi01_register(FLASH_BASE | ((hwaddr)FLASH_BASE_H << 32),
                               "sam460ex.flash", FLASH_SIZE,
                               dinfo ? blk_by_legacy_dinfo(dinfo) : NULL,
                               64 * KiB, 1, 0x89, 0x18, 0x0000, 0x0, 1)) {
        error_report("Error registering flash memory");
        /* XXX: return an error instead? */
        exit(1);
    }

    if (!dinfo) {
        /*error_report("No flash image given with the 'pflash' parameter,"
                " using default u-boot image");*/
        rom_add_file_fixed(UBOOT_FILENAME,
                           UBOOT_LOAD_BASE | ((hwaddr)FLASH_BASE_H << 32),
                           -1);
    }

    return 0;
}

static int sam460ex_load_device_tree(MachineState *machine,
                                     hwaddr addr,
                                     hwaddr initrd_base,
                                     hwaddr initrd_size)
{
    uint32_t mem_reg_property[] = { 0, 0, cpu_to_be32(machine->ram_size) };
    char *filename;
    int fdt_size;
    void *fdt;
    uint32_t tb_freq = CPU_FREQ;
    uint32_t clock_freq = CPU_FREQ;
    int offset;

    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, BINARY_DEVICE_TREE_FILE);
    if (!filename) {
        error_report("Couldn't find dtb file `%s'", BINARY_DEVICE_TREE_FILE);
        exit(1);
    }
    fdt = load_device_tree(filename, &fdt_size);
    if (!fdt) {
        error_report("Couldn't load dtb file `%s'", filename);
        g_free(filename);
        exit(1);
    }
    g_free(filename);

    /* Manipulate device tree in memory. */

    qemu_fdt_setprop(fdt, "/memory", "reg", mem_reg_property,
                     sizeof(mem_reg_property));

    /* default FDT doesn't have a /chosen node... */
    qemu_fdt_add_subnode(fdt, "/chosen");

    qemu_fdt_setprop_cell(fdt, "/chosen", "linux,initrd-start", initrd_base);

    qemu_fdt_setprop_cell(fdt, "/chosen", "linux,initrd-end",
                          (initrd_base + initrd_size));

    qemu_fdt_setprop_string(fdt, "/chosen", "bootargs",
                            machine->kernel_cmdline);

    /* Copy data from the host device tree into the guest. Since the guest can
     * directly access the timebase without host involvement, we must expose
     * the correct frequencies. */
    if (kvm_enabled()) {
        tb_freq = kvmppc_get_tbfreq();
        clock_freq = kvmppc_get_clockfreq();
    }

    qemu_fdt_setprop_cell(fdt, "/cpus/cpu@0", "clock-frequency",
                              clock_freq);
    qemu_fdt_setprop_cell(fdt, "/cpus/cpu@0", "timebase-frequency",
                              tb_freq);

    /* Remove cpm node if it exists (it is not emulated) */
    offset = fdt_path_offset(fdt, "/cpm");
    if (offset >= 0) {
        _FDT(fdt_nop_node(fdt, offset));
    }

    /* set serial port clocks */
    offset = fdt_node_offset_by_compatible(fdt, -1, "ns16550");
    while (offset >= 0) {
        _FDT(fdt_setprop_cell(fdt, offset, "clock-frequency", UART_FREQ));
        offset = fdt_node_offset_by_compatible(fdt, offset, "ns16550");
    }

    /* some more clocks */
    qemu_fdt_setprop_cell(fdt, "/plb", "clock-frequency",
                              PLB_FREQ);
    qemu_fdt_setprop_cell(fdt, "/plb/opb", "clock-frequency",
                              OPB_FREQ);
    qemu_fdt_setprop_cell(fdt, "/plb/opb/ebc", "clock-frequency",
                              EBC_FREQ);

    rom_add_blob_fixed(BINARY_DEVICE_TREE_FILE, fdt, fdt_size, addr);

    /* Set machine->fdt for 'dumpdtb' QMP/HMP command */
    machine->fdt = fdt;

    return fdt_size;
}

static void main_cpu_reset(void *opaque)
{
    PowerPCCPU *cpu = opaque;
    CPUPPCState *env = &cpu->env;
    struct boot_info *bi = env->load_info;

    cpu_reset(CPU(cpu));

    /*
     * On reset the flash is mapped by a shadow TLB, but since we
     * don't implement them we need to use the same values U-Boot
     * will use to avoid a fault.
     * either we have a kernel to boot or we jump to U-Boot
     */
    if (bi->entry != UBOOT_ENTRY) {
        env->gpr[1] = (16 * MiB) - 8;
        env->gpr[3] = FDT_ADDR;
        env->nip = bi->entry;

        /* Create a mapping for the kernel.  */
        booke_set_tlb(&env->tlb.tlbe[0], 0, 0, 1 << 31);
        env->gpr[6] = EPAPR_MAGIC;
        env->gpr[7] = (16 * MiB) - 8; /* bi->ima_size; */

    } else {
        env->nip = UBOOT_ENTRY;
        /* Create a mapping for U-Boot. */
        booke_set_tlb(&env->tlb.tlbe[0], 0xf0000000, 0xf0000000, 0x10000000);
        env->tlb.tlbe[0].RPN |= 4;
    }
}

static void sam460ex_init(MachineState *machine)
{
    MemoryRegion *l2cache_ram = g_new(MemoryRegion, 1);
    DeviceState *uic[4];
    int i;
    PCIBus *pci_bus;
    USBBus *usb_bus;
    PowerPCCPU *cpu;
    CPUPPCState *env;
    I2CBus *i2c;
    hwaddr entry = UBOOT_ENTRY;
    target_long initrd_size = 0;
    DeviceState *dev;
    SysBusDevice *sbdev;
    struct boot_info *boot_info;
    uint8_t *spd_data;
    int success;

    cpu = POWERPC_CPU(cpu_create(machine->cpu_type));
    env = &cpu->env;
    if (env->mmu_model != POWERPC_MMU_BOOKE) {
        error_report("Only MMU model BookE is supported by this machine.");
        exit(1);
    }

    qemu_register_reset(main_cpu_reset, cpu);
    boot_info = g_malloc0(sizeof(*boot_info));
    env->load_info = boot_info;

    ppc_booke_timers_init(cpu, CPU_FREQ, 0);
    ppc_dcr_init(env, NULL, NULL);

    /* PLB arbitrer */
    dev = qdev_new(TYPE_PPC4xx_PLB);
    ppc4xx_dcr_realize(PPC4xx_DCR_DEVICE(dev), cpu, &error_fatal);
    object_unref(OBJECT(dev));

    /* interrupt controllers */
    for (i = 0; i < ARRAY_SIZE(uic); i++) {
        /*
         * UICs 1, 2 and 3 are cascaded through UIC 0.
         * input_ints[n] is the interrupt number on UIC 0 which
         * the INT output of UIC n is connected to. The CINT output
         * of UIC n connects to input_ints[n] + 1.
         * The entry in input_ints[] for UIC 0 is ignored, because UIC 0's
         * INT and CINT outputs are connected to the CPU.
         */
        const int input_ints[] = { -1, 30, 10, 16 };

        uic[i] = qdev_new(TYPE_PPC_UIC);
        qdev_prop_set_uint32(uic[i], "dcr-base", 0xc0 + i * 0x10);
        ppc4xx_dcr_realize(PPC4xx_DCR_DEVICE(uic[i]), cpu, &error_fatal);
        object_unref(OBJECT(uic[i]));

        sbdev = SYS_BUS_DEVICE(uic[i]);
        if (i == 0) {
            sysbus_connect_irq(sbdev, PPCUIC_OUTPUT_INT,
                             qdev_get_gpio_in(DEVICE(cpu), PPC40x_INPUT_INT));
            sysbus_connect_irq(sbdev, PPCUIC_OUTPUT_CINT,
                             qdev_get_gpio_in(DEVICE(cpu), PPC40x_INPUT_CINT));
        } else {
            sysbus_connect_irq(sbdev, PPCUIC_OUTPUT_INT,
                               qdev_get_gpio_in(uic[0], input_ints[i]));
            sysbus_connect_irq(sbdev, PPCUIC_OUTPUT_CINT,
                               qdev_get_gpio_in(uic[0], input_ints[i] + 1));
        }
    }

    /* SDRAM controller */
    /* The SoC could also handle 4 GiB but firmware does not work with that. */
    if (machine->ram_size > 2 * GiB) {
        error_report("Memory over 2 GiB is not supported");
        exit(1);
    }
    /* Firmware needs at least 64 MiB */
    if (machine->ram_size < 64 * MiB) {
        error_report("Memory below 64 MiB is not supported");
        exit(1);
    }
    dev = qdev_new(TYPE_PPC4xx_SDRAM_DDR2);
    object_property_set_link(OBJECT(dev), "dram", OBJECT(machine->ram),
                             &error_abort);
    /*
     * Put all RAM on first bank because board has one slot
     * and firmware only checks that
     */
    object_property_set_int(OBJECT(dev), "nbanks", 1, &error_abort);
    ppc4xx_dcr_realize(PPC4xx_DCR_DEVICE(dev), cpu, &error_fatal);
    object_unref(OBJECT(dev));
    /* FIXME: does 460EX have ECC interrupts? */
    /* Enable SDRAM memory regions as we may boot without firmware */
    ppc4xx_sdram_ddr2_enable(PPC4xx_SDRAM_DDR2(dev));

    /* IIC controllers and devices */
    dev = sysbus_create_simple(TYPE_PPC4xx_I2C, 0x4ef600700,
                               qdev_get_gpio_in(uic[0], 2));
    i2c = PPC4xx_I2C(dev)->bus;
    /* SPD EEPROM on RAM module */
    spd_data = spd_data_generate(machine->ram_size < 128 * MiB ? DDR : DDR2,
                                 machine->ram_size);
    spd_data[20] = 4; /* SO-DIMM module */
    smbus_eeprom_init_one(i2c, 0x50, spd_data);
    /* RTC */
    i2c_slave_create_simple(i2c, "m41t80", 0x68);

    dev = sysbus_create_simple(TYPE_PPC4xx_I2C, 0x4ef600800,
                               qdev_get_gpio_in(uic[0], 3));

    /* External bus controller */
    dev = qdev_new(TYPE_PPC4xx_EBC);
    ppc4xx_dcr_realize(PPC4xx_DCR_DEVICE(dev), cpu, &error_fatal);
    object_unref(OBJECT(dev));

    /* CPR */
    ppc4xx_cpr_init(env);

    /* PLB to AHB bridge */
    ppc4xx_ahb_init(env);

    /* System DCRs */
    ppc4xx_sdr_init(env);

    /* MAL */
    dev = qdev_new(TYPE_PPC4xx_MAL);
    qdev_prop_set_uint8(dev, "txc-num", 4);
    qdev_prop_set_uint8(dev, "rxc-num", 16);
    ppc4xx_dcr_realize(PPC4xx_DCR_DEVICE(dev), cpu, &error_fatal);
    object_unref(OBJECT(dev));
    sbdev = SYS_BUS_DEVICE(dev);
    for (i = 0; i < ARRAY_SIZE(PPC4xx_MAL(dev)->irqs); i++) {
        sysbus_connect_irq(sbdev, i, qdev_get_gpio_in(uic[2], 3 + i));
    }

    /* DMA */
    ppc4xx_dma_init(env, 0x200);

    /* 256K of L2 cache as memory */
    ppc4xx_l2sram_init(env);
    /* FIXME: remove this after fixing l2sram mapping in ppc440_uc.c? */
    memory_region_init_ram(l2cache_ram, NULL, "ppc440.l2cache_ram", 256 * KiB,
                           &error_abort);
    memory_region_add_subregion(get_system_memory(), 0x400000000LL,
                                l2cache_ram);

    /* USB */
    sysbus_create_simple(TYPE_PPC4xx_EHCI, 0x4bffd0400,
                         qdev_get_gpio_in(uic[2], 29));
    dev = qdev_new("sysbus-ohci");
    qdev_prop_set_string(dev, "masterbus", "usb-bus.0");
    qdev_prop_set_uint32(dev, "num-ports", 6);
    sbdev = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(sbdev, &error_fatal);
    sysbus_mmio_map(sbdev, 0, 0x4bffd0000);
    sysbus_connect_irq(sbdev, 0, qdev_get_gpio_in(uic[2], 30));
    usb_bus = USB_BUS(object_resolve_type_unambiguous(TYPE_USB_BUS,
                                                      &error_abort));
    usb_create_simple(usb_bus, "usb-kbd");
    usb_create_simple(usb_bus, "usb-mouse");

    /* PCIe buses */
    dev = qdev_new(TYPE_PPC460EX_PCIE_HOST);
    qdev_prop_set_int32(dev, "busnum", 0);
    qdev_prop_set_int32(dev, "dcrn-base", PCIE0_DCRN_BASE);
    object_property_set_link(OBJECT(dev), "cpu", OBJECT(cpu), &error_abort);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    dev = qdev_new(TYPE_PPC460EX_PCIE_HOST);
    qdev_prop_set_int32(dev, "busnum", 1);
    qdev_prop_set_int32(dev, "dcrn-base", PCIE1_DCRN_BASE);
    object_property_set_link(OBJECT(dev), "cpu", OBJECT(cpu), &error_abort);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    /* PCI bus */
    /* All PCI irqs are connected to the same UIC pin (cf. UBoot source) */
    dev = sysbus_create_simple(TYPE_PPC440_PCIX_HOST, 0xc0ec00000,
                               qdev_get_gpio_in(uic[1], 0));
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 1, 0xc08000000);
    pci_bus = PCI_BUS(qdev_get_child_bus(dev, "pci.0"));

    /* PCI devices */
    pci_create_simple(pci_bus, PCI_DEVFN(6, 0), "sm501");
    /*
     * SoC has a single SATA port but we don't emulate that
     * However, firmware and usual clients have driver for SiI311x
     * PCI SATA card so add one for convenience by default
     */
    if (defaults_enabled()) {
        PCIIDEState *s = PCI_IDE(pci_create_simple(pci_bus, -1, "sii3112"));
        DriveInfo *di;

        di = drive_get_by_index(IF_IDE, 0);
        if (di) {
            ide_bus_create_drive(&s->bus[0], 0, di);
        }
        /* Use index 2 only if 1 does not exist, this allows -cdrom */
        di = drive_get_by_index(IF_IDE, 1) ?: drive_get_by_index(IF_IDE, 2);
        if (di) {
            ide_bus_create_drive(&s->bus[1], 0, di);
        }
    }

    /* SoC has 4 UARTs but board has only one wired and two described in fdt */
    if (serial_hd(0) != NULL) {
        serial_mm_init(get_system_memory(), 0x4ef600300, 0,
                       qdev_get_gpio_in(uic[1], 1),
                       PPC_SERIAL_MM_BAUDBASE, serial_hd(0),
                       DEVICE_BIG_ENDIAN);
    }
    if (serial_hd(1) != NULL) {
        serial_mm_init(get_system_memory(), 0x4ef600400, 0,
                       qdev_get_gpio_in(uic[0], 1),
                       PPC_SERIAL_MM_BAUDBASE, serial_hd(1),
                       DEVICE_BIG_ENDIAN);
    }

    /* Load U-Boot image. */
    if (!machine->kernel_filename) {
        success = sam460ex_load_uboot();
        if (success < 0) {
            error_report("could not load firmware");
            exit(1);
        }
    }

    /* Load kernel. */
    if (machine->kernel_filename) {
        hwaddr loadaddr = LOAD_UIMAGE_LOADADDR_INVALID;
        success = load_uimage(machine->kernel_filename, &entry, &loadaddr,
                              NULL, NULL, NULL);
        if (success < 0) {
            uint64_t elf_entry;

            success = load_elf(machine->kernel_filename, NULL, NULL, NULL,
                               &elf_entry, NULL, NULL, NULL,
                               ELFDATA2MSB, PPC_ELF_MACHINE, 0, 0);
            entry = elf_entry;
        }
        /* XXX try again as binary */
        if (success < 0) {
            error_report("could not load kernel '%s'",
                    machine->kernel_filename);
            exit(1);
        }
    }

    /* Load initrd. */
    if (machine->initrd_filename) {
        initrd_size = load_image_targphys(machine->initrd_filename,
                                          RAMDISK_ADDR,
                                          machine->ram_size - RAMDISK_ADDR);
        if (initrd_size < 0) {
            error_report("could not load ram disk '%s' at %x",
                    machine->initrd_filename, RAMDISK_ADDR);
            exit(1);
        }
    }

    /* If we're loading a kernel directly, we must load the device tree too. */
    if (machine->kernel_filename) {
        int dt_size;

        dt_size = sam460ex_load_device_tree(machine, FDT_ADDR,
                                            RAMDISK_ADDR, initrd_size);

        boot_info->dt_base = FDT_ADDR;
        boot_info->dt_size = dt_size;
    }

    boot_info->entry = entry;
}

static void sam460ex_machine_init(MachineClass *mc)
{
    mc->desc = "aCube Sam460ex";
    mc->init = sam460ex_init;
    mc->block_default_type = IF_IDE;
    mc->default_cpu_type = POWERPC_CPU_TYPE_NAME("460exb");
    mc->default_ram_size = 512 * MiB;
    mc->default_ram_id = "ppc4xx.sdram";
}

DEFINE_MACHINE("sam460ex", sam460ex_machine_init)
