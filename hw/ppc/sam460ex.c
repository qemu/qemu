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
#include "qemu-common.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "sysemu/kvm.h"
#include "kvm_ppc.h"
#include "sysemu/device_tree.h"
#include "sysemu/block-backend.h"
#include "hw/loader.h"
#include "elf.h"
#include "exec/address-spaces.h"
#include "exec/memory.h"
#include "ppc440.h"
#include "ppc405.h"
#include "hw/block/flash.h"
#include "sysemu/sysemu.h"
#include "sysemu/qtest.h"
#include "sysemu/reset.h"
#include "hw/sysbus.h"
#include "hw/char/serial.h"
#include "hw/i2c/ppc4xx_i2c.h"
#include "hw/i2c/smbus_eeprom.h"
#include "hw/usb/hcd-ehci.h"
#include "hw/ppc/fdt.h"
#include "hw/qdev-properties.h"

#include <libfdt.h>

#define BINARY_DEVICE_TREE_FILE "canyonlands.dtb"
#define UBOOT_FILENAME "u-boot-sam460-20100605.bin"
/* to extract the official U-Boot bin from the updater: */
/* dd bs=1 skip=$(($(stat -c '%s' updater/updater-460) - 0x80000)) \
     if=updater/updater-460 of=u-boot-sam460-20100605.bin */

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
#define SDRAM_NR_BANKS 4

/* The SoC could also handle 4 GiB but firmware does not work with that. */
/* Maybe it overflows a signed 32 bit number somewhere? */
static const ram_addr_t ppc460ex_sdram_bank_sizes[] = {
    2 * GiB, 1 * GiB, 512 * MiB, 256 * MiB, 128 * MiB, 64 * MiB,
    32 * MiB, 0
};

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

static int sam460ex_load_device_tree(hwaddr addr,
                                     uint32_t ramsize,
                                     hwaddr initrd_base,
                                     hwaddr initrd_size,
                                     const char *kernel_cmdline)
{
    uint32_t mem_reg_property[] = { 0, 0, cpu_to_be32(ramsize) };
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

    qemu_fdt_setprop_string(fdt, "/chosen", "bootargs", kernel_cmdline);

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
    g_free(fdt);

    return fdt_size;
}

/* Create reset TLB entries for BookE, mapping only the flash memory.  */
static void mmubooke_create_initial_mapping_uboot(CPUPPCState *env)
{
    ppcemb_tlb_t *tlb = &env->tlb.tlbe[0];

    /* on reset the flash is mapped by a shadow TLB,
     * but since we don't implement them we need to use
     * the same values U-Boot will use to avoid a fault.
     */
    tlb->attr = 0;
    tlb->prot = PAGE_VALID | ((PAGE_READ | PAGE_WRITE | PAGE_EXEC) << 4);
    tlb->size = 0x10000000; /* up to 0xffffffff  */
    tlb->EPN = 0xf0000000 & TARGET_PAGE_MASK;
    tlb->RPN = (0xf0000000 & TARGET_PAGE_MASK) | 0x4;
    tlb->PID = 0;
}

/* Create reset TLB entries for BookE, spanning the 32bit addr space.  */
static void mmubooke_create_initial_mapping(CPUPPCState *env,
                                     target_ulong va,
                                     hwaddr pa)
{
    ppcemb_tlb_t *tlb = &env->tlb.tlbe[0];

    tlb->attr = 0;
    tlb->prot = PAGE_VALID | ((PAGE_READ | PAGE_WRITE | PAGE_EXEC) << 4);
    tlb->size = 1 << 31; /* up to 0x80000000  */
    tlb->EPN = va & TARGET_PAGE_MASK;
    tlb->RPN = pa & TARGET_PAGE_MASK;
    tlb->PID = 0;
}

static void main_cpu_reset(void *opaque)
{
    PowerPCCPU *cpu = opaque;
    CPUPPCState *env = &cpu->env;
    struct boot_info *bi = env->load_info;

    cpu_reset(CPU(cpu));

    /* either we have a kernel to boot or we jump to U-Boot */
    if (bi->entry != UBOOT_ENTRY) {
        env->gpr[1] = (16 * MiB) - 8;
        env->gpr[3] = FDT_ADDR;
        env->nip = bi->entry;

        /* Create a mapping for the kernel.  */
        mmubooke_create_initial_mapping(env, 0, 0);
        env->gpr[6] = tswap32(EPAPR_MAGIC);
        env->gpr[7] = (16 * MiB) - 8; /* bi->ima_size; */

    } else {
        env->nip = UBOOT_ENTRY;
        mmubooke_create_initial_mapping_uboot(env);
    }
}

static void sam460ex_init(MachineState *machine)
{
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *isa = g_new(MemoryRegion, 1);
    MemoryRegion *ram_memories = g_new(MemoryRegion, SDRAM_NR_BANKS);
    hwaddr ram_bases[SDRAM_NR_BANKS] = {0};
    hwaddr ram_sizes[SDRAM_NR_BANKS] = {0};
    MemoryRegion *l2cache_ram = g_new(MemoryRegion, 1);
    qemu_irq *irqs, *uic[4];
    PCIBus *pci_bus;
    PowerPCCPU *cpu;
    CPUPPCState *env;
    I2CBus *i2c;
    hwaddr entry = UBOOT_ENTRY;
    hwaddr loadaddr = LOAD_UIMAGE_LOADADDR_INVALID;
    target_long initrd_size = 0;
    DeviceState *dev;
    SysBusDevice *sbdev;
    struct boot_info *boot_info;
    uint8_t *spd_data;
    Error *err = NULL;
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
    ppc4xx_plb_init(env);

    /* interrupt controllers */
    irqs = g_new0(qemu_irq, PPCUIC_OUTPUT_NB);
    irqs[PPCUIC_OUTPUT_INT] = ((qemu_irq *)env->irq_inputs)[PPC40x_INPUT_INT];
    irqs[PPCUIC_OUTPUT_CINT] = ((qemu_irq *)env->irq_inputs)[PPC40x_INPUT_CINT];
    uic[0] = ppcuic_init(env, irqs, 0xc0, 0, 1);
    uic[1] = ppcuic_init(env, &uic[0][30], 0xd0, 0, 1);
    uic[2] = ppcuic_init(env, &uic[0][10], 0xe0, 0, 1);
    uic[3] = ppcuic_init(env, &uic[0][16], 0xf0, 0, 1);

    /* SDRAM controller */
    /* put all RAM on first bank because board has one slot
     * and firmware only checks that */
    machine->ram_size = ppc4xx_sdram_adjust(machine->ram_size, 1,
                                   ram_memories, ram_bases, ram_sizes,
                                   ppc460ex_sdram_bank_sizes);

    /* FIXME: does 460EX have ECC interrupts? */
    ppc440_sdram_init(env, SDRAM_NR_BANKS, ram_memories,
                      ram_bases, ram_sizes, 1);

    /* IIC controllers and devices */
    dev = sysbus_create_simple(TYPE_PPC4xx_I2C, 0x4ef600700, uic[0][2]);
    i2c = PPC4xx_I2C(dev)->bus;
    /* SPD EEPROM on RAM module */
    spd_data = spd_data_generate(DDR2, ram_sizes[0], &err);
    if (err) {
        warn_report_err(err);
    }
    if (spd_data) {
        spd_data[20] = 4; /* SO-DIMM module */
        smbus_eeprom_init_one(i2c, 0x50, spd_data);
    }
    /* RTC */
    i2c_create_slave(i2c, "m41t80", 0x68);

    dev = sysbus_create_simple(TYPE_PPC4xx_I2C, 0x4ef600800, uic[0][3]);

    /* External bus controller */
    ppc405_ebc_init(env);

    /* CPR */
    ppc4xx_cpr_init(env);

    /* PLB to AHB bridge */
    ppc4xx_ahb_init(env);

    /* System DCRs */
    ppc4xx_sdr_init(env);

    /* MAL */
    ppc4xx_mal_init(env, 4, 16, &uic[2][3]);

    /* DMA */
    ppc4xx_dma_init(env, 0x200);

    /* 256K of L2 cache as memory */
    ppc4xx_l2sram_init(env);
    /* FIXME: remove this after fixing l2sram mapping in ppc440_uc.c? */
    memory_region_init_ram(l2cache_ram, NULL, "ppc440.l2cache_ram", 256 * KiB,
                           &error_abort);
    memory_region_add_subregion(address_space_mem, 0x400000000LL, l2cache_ram);

    /* USB */
    sysbus_create_simple(TYPE_PPC4xx_EHCI, 0x4bffd0400, uic[2][29]);
    dev = qdev_create(NULL, "sysbus-ohci");
    qdev_prop_set_string(dev, "masterbus", "usb-bus.0");
    qdev_prop_set_uint32(dev, "num-ports", 6);
    qdev_init_nofail(dev);
    sbdev = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(sbdev, 0, 0x4bffd0000);
    sysbus_connect_irq(sbdev, 0, uic[2][30]);
    usb_create_simple(usb_bus_find(-1), "usb-kbd");
    usb_create_simple(usb_bus_find(-1), "usb-mouse");

    /* PCI bus */
    ppc460ex_pcie_init(env);
    /* All PCI irqs are connected to the same UIC pin (cf. UBoot source) */
    dev = sysbus_create_simple("ppc440-pcix-host", 0xc0ec00000, uic[1][0]);
    pci_bus = (PCIBus *)qdev_get_child_bus(dev, "pci.0");
    if (!pci_bus) {
        error_report("couldn't create PCI controller!");
        exit(1);
    }
    memory_region_init_alias(isa, NULL, "isa_mmio", get_system_io(),
                             0, 0x10000);
    memory_region_add_subregion(get_system_memory(), 0xc08000000, isa);

    /* PCI devices */
    pci_create_simple(pci_bus, PCI_DEVFN(6, 0), "sm501");
    /* SoC has a single SATA port but we don't emulate that yet
     * However, firmware and usual clients have driver for SiI311x
     * so add one for convenience by default */
    if (defaults_enabled()) {
        pci_create_simple(pci_bus, -1, "sii3112");
    }

    /* SoC has 4 UARTs
     * but board has only one wired and two are present in fdt */
    if (serial_hd(0) != NULL) {
        serial_mm_init(address_space_mem, 0x4ef600300, 0, uic[1][1],
                       PPC_SERIAL_MM_BAUDBASE, serial_hd(0),
                       DEVICE_BIG_ENDIAN);
    }
    if (serial_hd(1) != NULL) {
        serial_mm_init(address_space_mem, 0x4ef600400, 0, uic[0][1],
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
        success = load_uimage(machine->kernel_filename, &entry, &loadaddr,
                              NULL, NULL, NULL);
        if (success < 0) {
            uint64_t elf_entry, elf_lowaddr;

            success = load_elf(machine->kernel_filename, NULL,
                               NULL, NULL, &elf_entry,
                               &elf_lowaddr, NULL, 1, PPC_ELF_MACHINE, 0, 0);
            entry = elf_entry;
            loadaddr = elf_lowaddr;
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

        dt_size = sam460ex_load_device_tree(FDT_ADDR, machine->ram_size,
                                    RAMDISK_ADDR, initrd_size,
                                    machine->kernel_cmdline);

        boot_info->dt_base = FDT_ADDR;
        boot_info->dt_size = dt_size;
    }

    boot_info->entry = entry;
}

static void sam460ex_machine_init(MachineClass *mc)
{
    mc->desc = "aCube Sam460ex";
    mc->init = sam460ex_init;
    mc->default_cpu_type = POWERPC_CPU_TYPE_NAME("460exb");
    mc->default_ram_size = 512 * MiB;
}

DEFINE_MACHINE("sam460ex", sam460ex_machine_init)
