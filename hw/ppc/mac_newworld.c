/*
 * QEMU PowerPC CHRP (currently NewWorld PowerMac) hardware System Emulator
 *
 * Copyright (c) 2004-2007 Fabrice Bellard
 * Copyright (c) 2007 Jocelyn Mayer
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
 * PCI bus layout on a real G5 (U3 based):
 *
 * 0000:f0:0b.0 Host bridge [0600]: Apple Computer Inc. U3 AGP [106b:004b]
 * 0000:f0:10.0 VGA compatible controller [0300]: ATI Technologies Inc RV350 AP [Radeon 9600] [1002:4150]
 * 0001:00:00.0 Host bridge [0600]: Apple Computer Inc. CPC945 HT Bridge [106b:004a]
 * 0001:00:01.0 PCI bridge [0604]: Advanced Micro Devices [AMD] AMD-8131 PCI-X Bridge [1022:7450] (rev 12)
 * 0001:00:02.0 PCI bridge [0604]: Advanced Micro Devices [AMD] AMD-8131 PCI-X Bridge [1022:7450] (rev 12)
 * 0001:00:03.0 PCI bridge [0604]: Apple Computer Inc. K2 HT-PCI Bridge [106b:0045]
 * 0001:00:04.0 PCI bridge [0604]: Apple Computer Inc. K2 HT-PCI Bridge [106b:0046]
 * 0001:00:05.0 PCI bridge [0604]: Apple Computer Inc. K2 HT-PCI Bridge [106b:0047]
 * 0001:00:06.0 PCI bridge [0604]: Apple Computer Inc. K2 HT-PCI Bridge [106b:0048]
 * 0001:00:07.0 PCI bridge [0604]: Apple Computer Inc. K2 HT-PCI Bridge [106b:0049]
 * 0001:01:07.0 Class [ff00]: Apple Computer Inc. K2 KeyLargo Mac/IO [106b:0041] (rev 20)
 * 0001:01:08.0 USB Controller [0c03]: Apple Computer Inc. K2 KeyLargo USB [106b:0040]
 * 0001:01:09.0 USB Controller [0c03]: Apple Computer Inc. K2 KeyLargo USB [106b:0040]
 * 0001:02:0b.0 USB Controller [0c03]: NEC Corporation USB [1033:0035] (rev 43)
 * 0001:02:0b.1 USB Controller [0c03]: NEC Corporation USB [1033:0035] (rev 43)
 * 0001:02:0b.2 USB Controller [0c03]: NEC Corporation USB 2.0 [1033:00e0] (rev 04)
 * 0001:03:0d.0 Class [ff00]: Apple Computer Inc. K2 ATA/100 [106b:0043]
 * 0001:03:0e.0 FireWire (IEEE 1394) [0c00]: Apple Computer Inc. K2 FireWire [106b:0042]
 * 0001:04:0f.0 Ethernet controller [0200]: Apple Computer Inc. K2 GMAC (Sun GEM) [106b:004c]
 * 0001:05:0c.0 IDE interface [0101]: Broadcom K2 SATA [1166:0240]
 *
 */
#include "hw/hw.h"
#include "hw/ppc/ppc.h"
#include "hw/ppc/mac.h"
#include "hw/input/adb.h"
#include "hw/ppc/mac_dbdma.h"
#include "hw/timer/m48t59.h"
#include "hw/pci/pci.h"
#include "net/net.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "hw/nvram/fw_cfg.h"
#include "hw/char/escc.h"
#include "hw/ppc/openpic.h"
#include "hw/ide.h"
#include "hw/loader.h"
#include "elf.h"
#include "sysemu/kvm.h"
#include "kvm_ppc.h"
#include "hw/usb.h"
#include "sysemu/blockdev.h"
#include "exec/address-spaces.h"
#include "hw/sysbus.h"

#define MAX_IDE_BUS 2
#define CFG_ADDR 0xf0000510
#define TBFREQ (100UL * 1000UL * 1000UL)
#define CLOCKFREQ (266UL * 1000UL * 1000UL)
#define BUSFREQ (100UL * 1000UL * 1000UL)

/* debug UniNorth */
//#define DEBUG_UNIN

#ifdef DEBUG_UNIN
#define UNIN_DPRINTF(fmt, ...)                                  \
    do { printf("UNIN: " fmt , ## __VA_ARGS__); } while (0)
#else
#define UNIN_DPRINTF(fmt, ...)
#endif

/* UniN device */
static void unin_write(void *opaque, hwaddr addr, uint64_t value,
                       unsigned size)
{
    UNIN_DPRINTF("write addr " TARGET_FMT_plx " val %"PRIx64"\n", addr, value);
    if (addr == 0x0) {
        *(int*)opaque = value;
    }
}

static uint64_t unin_read(void *opaque, hwaddr addr, unsigned size)
{
    uint32_t value;

    value = 0;
    switch (addr) {
    case 0:
        value = *(int*)opaque;
    }

    UNIN_DPRINTF("readl addr " TARGET_FMT_plx " val %x\n", addr, value);

    return value;
}

static const MemoryRegionOps unin_ops = {
    .read = unin_read,
    .write = unin_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int fw_cfg_boot_set(void *opaque, const char *boot_device)
{
    fw_cfg_add_i16(opaque, FW_CFG_BOOT_DEVICE, boot_device[0]);
    return 0;
}

static uint64_t translate_kernel_address(void *opaque, uint64_t addr)
{
    return (addr & 0x0fffffff) + KERNEL_LOAD_ADDR;
}

static hwaddr round_page(hwaddr addr)
{
    return (addr + TARGET_PAGE_SIZE - 1) & TARGET_PAGE_MASK;
}

static void ppc_core99_reset(void *opaque)
{
    PowerPCCPU *cpu = opaque;

    cpu_reset(CPU(cpu));
    /* 970 CPUs want to get their initial IP as part of their boot protocol */
    cpu->env.nip = PROM_ADDR + 0x100;
}

/* PowerPC Mac99 hardware initialisation */
static void ppc_core99_init(MachineState *machine)
{
    ram_addr_t ram_size = machine->ram_size;
    const char *cpu_model = machine->cpu_model;
    const char *kernel_filename = machine->kernel_filename;
    const char *kernel_cmdline = machine->kernel_cmdline;
    const char *initrd_filename = machine->initrd_filename;
    const char *boot_device = machine->boot_order;
    PowerPCCPU *cpu = NULL;
    CPUPPCState *env = NULL;
    char *filename;
    qemu_irq *pic, **openpic_irqs;
    MemoryRegion *isa = g_new(MemoryRegion, 1);
    MemoryRegion *unin_memory = g_new(MemoryRegion, 1);
    MemoryRegion *unin2_memory = g_new(MemoryRegion, 1);
    int linux_boot, i, j, k;
    MemoryRegion *ram = g_new(MemoryRegion, 1), *bios = g_new(MemoryRegion, 1);
    hwaddr kernel_base, initrd_base, cmdline_base = 0;
    long kernel_size, initrd_size;
    PCIBus *pci_bus;
    PCIDevice *macio;
    MACIOIDEState *macio_ide;
    BusState *adb_bus;
    MacIONVRAMState *nvr;
    int bios_size;
    MemoryRegion *pic_mem, *escc_mem;
    MemoryRegion *escc_bar = g_new(MemoryRegion, 1);
    int ppc_boot_device;
    DriveInfo *hd[MAX_IDE_BUS * MAX_IDE_DEVS];
    void *fw_cfg;
    int machine_arch;
    SysBusDevice *s;
    DeviceState *dev;
    int *token = g_new(int, 1);
    hwaddr nvram_addr = 0xFFF04000;

    linux_boot = (kernel_filename != NULL);

    /* init CPUs */
    if (cpu_model == NULL)
#ifdef TARGET_PPC64
        cpu_model = "970fx";
#else
        cpu_model = "G4";
#endif
    for (i = 0; i < smp_cpus; i++) {
        cpu = cpu_ppc_init(cpu_model);
        if (cpu == NULL) {
            fprintf(stderr, "Unable to find PowerPC CPU definition\n");
            exit(1);
        }
        env = &cpu->env;

        /* Set time-base frequency to 100 Mhz */
        cpu_ppc_tb_init(env, TBFREQ);
        qemu_register_reset(ppc_core99_reset, cpu);
    }

    /* allocate RAM */
    memory_region_allocate_system_memory(ram, NULL, "ppc_core99.ram", ram_size);
    memory_region_add_subregion(get_system_memory(), 0, ram);

    /* allocate and load BIOS */
    memory_region_init_ram(bios, NULL, "ppc_core99.bios", BIOS_SIZE);
    vmstate_register_ram_global(bios);

    if (bios_name == NULL)
        bios_name = PROM_FILENAME;
    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
    memory_region_set_readonly(bios, true);
    memory_region_add_subregion(get_system_memory(), PROM_ADDR, bios);

    /* Load OpenBIOS (ELF) */
    if (filename) {
        bios_size = load_elf(filename, NULL, NULL, NULL,
                             NULL, NULL, 1, ELF_MACHINE, 0);

        g_free(filename);
    } else {
        bios_size = -1;
    }
    if (bios_size < 0 || bios_size > BIOS_SIZE) {
        hw_error("qemu: could not load PowerPC bios '%s'\n", bios_name);
        exit(1);
    }

    if (linux_boot) {
        uint64_t lowaddr = 0;
        int bswap_needed;

#ifdef BSWAP_NEEDED
        bswap_needed = 1;
#else
        bswap_needed = 0;
#endif
        kernel_base = KERNEL_LOAD_ADDR;

        kernel_size = load_elf(kernel_filename, translate_kernel_address, NULL,
                               NULL, &lowaddr, NULL, 1, ELF_MACHINE, 0);
        if (kernel_size < 0)
            kernel_size = load_aout(kernel_filename, kernel_base,
                                    ram_size - kernel_base, bswap_needed,
                                    TARGET_PAGE_SIZE);
        if (kernel_size < 0)
            kernel_size = load_image_targphys(kernel_filename,
                                              kernel_base,
                                              ram_size - kernel_base);
        if (kernel_size < 0) {
            hw_error("qemu: could not load kernel '%s'\n", kernel_filename);
            exit(1);
        }
        /* load initrd */
        if (initrd_filename) {
            initrd_base = round_page(kernel_base + kernel_size + KERNEL_GAP);
            initrd_size = load_image_targphys(initrd_filename, initrd_base,
                                              ram_size - initrd_base);
            if (initrd_size < 0) {
                hw_error("qemu: could not load initial ram disk '%s'\n",
                         initrd_filename);
                exit(1);
            }
            cmdline_base = round_page(initrd_base + initrd_size);
        } else {
            initrd_base = 0;
            initrd_size = 0;
            cmdline_base = round_page(kernel_base + kernel_size + KERNEL_GAP);
        }
        ppc_boot_device = 'm';
    } else {
        kernel_base = 0;
        kernel_size = 0;
        initrd_base = 0;
        initrd_size = 0;
        ppc_boot_device = '\0';
        /* We consider that NewWorld PowerMac never have any floppy drive
         * For now, OHW cannot boot from the network.
         */
        for (i = 0; boot_device[i] != '\0'; i++) {
            if (boot_device[i] >= 'c' && boot_device[i] <= 'f') {
                ppc_boot_device = boot_device[i];
                break;
            }
        }
        if (ppc_boot_device == '\0') {
            fprintf(stderr, "No valid boot device for Mac99 machine\n");
            exit(1);
        }
    }

    /* Register 8 MB of ISA IO space */
    memory_region_init_alias(isa, NULL, "isa_mmio",
                             get_system_io(), 0, 0x00800000);
    memory_region_add_subregion(get_system_memory(), 0xf2000000, isa);

    /* UniN init: XXX should be a real device */
    memory_region_init_io(unin_memory, NULL, &unin_ops, token, "unin", 0x1000);
    memory_region_add_subregion(get_system_memory(), 0xf8000000, unin_memory);

    memory_region_init_io(unin2_memory, NULL, &unin_ops, token, "unin", 0x1000);
    memory_region_add_subregion(get_system_memory(), 0xf3000000, unin2_memory);

    openpic_irqs = g_malloc0(smp_cpus * sizeof(qemu_irq *));
    openpic_irqs[0] =
        g_malloc0(smp_cpus * sizeof(qemu_irq) * OPENPIC_OUTPUT_NB);
    for (i = 0; i < smp_cpus; i++) {
        /* Mac99 IRQ connection between OpenPIC outputs pins
         * and PowerPC input pins
         */
        switch (PPC_INPUT(env)) {
        case PPC_FLAGS_INPUT_6xx:
            openpic_irqs[i] = openpic_irqs[0] + (i * OPENPIC_OUTPUT_NB);
            openpic_irqs[i][OPENPIC_OUTPUT_INT] =
                ((qemu_irq *)env->irq_inputs)[PPC6xx_INPUT_INT];
            openpic_irqs[i][OPENPIC_OUTPUT_CINT] =
                ((qemu_irq *)env->irq_inputs)[PPC6xx_INPUT_INT];
            openpic_irqs[i][OPENPIC_OUTPUT_MCK] =
                ((qemu_irq *)env->irq_inputs)[PPC6xx_INPUT_MCP];
            /* Not connected ? */
            openpic_irqs[i][OPENPIC_OUTPUT_DEBUG] = NULL;
            /* Check this */
            openpic_irqs[i][OPENPIC_OUTPUT_RESET] =
                ((qemu_irq *)env->irq_inputs)[PPC6xx_INPUT_HRESET];
            break;
#if defined(TARGET_PPC64)
        case PPC_FLAGS_INPUT_970:
            openpic_irqs[i] = openpic_irqs[0] + (i * OPENPIC_OUTPUT_NB);
            openpic_irqs[i][OPENPIC_OUTPUT_INT] =
                ((qemu_irq *)env->irq_inputs)[PPC970_INPUT_INT];
            openpic_irqs[i][OPENPIC_OUTPUT_CINT] =
                ((qemu_irq *)env->irq_inputs)[PPC970_INPUT_INT];
            openpic_irqs[i][OPENPIC_OUTPUT_MCK] =
                ((qemu_irq *)env->irq_inputs)[PPC970_INPUT_MCP];
            /* Not connected ? */
            openpic_irqs[i][OPENPIC_OUTPUT_DEBUG] = NULL;
            /* Check this */
            openpic_irqs[i][OPENPIC_OUTPUT_RESET] =
                ((qemu_irq *)env->irq_inputs)[PPC970_INPUT_HRESET];
            break;
#endif /* defined(TARGET_PPC64) */
        default:
            hw_error("Bus model not supported on mac99 machine\n");
            exit(1);
        }
    }

    pic = g_new0(qemu_irq, 64);

    dev = qdev_create(NULL, TYPE_OPENPIC);
    qdev_prop_set_uint32(dev, "model", OPENPIC_MODEL_RAVEN);
    qdev_init_nofail(dev);
    s = SYS_BUS_DEVICE(dev);
    pic_mem = s->mmio[0].memory;
    k = 0;
    for (i = 0; i < smp_cpus; i++) {
        for (j = 0; j < OPENPIC_OUTPUT_NB; j++) {
            sysbus_connect_irq(s, k++, openpic_irqs[i][j]);
        }
    }

    for (i = 0; i < 64; i++) {
        pic[i] = qdev_get_gpio_in(dev, i);
    }

    if (PPC_INPUT(env) == PPC_FLAGS_INPUT_970) {
        /* 970 gets a U3 bus */
        pci_bus = pci_pmac_u3_init(pic, get_system_memory(), get_system_io());
        machine_arch = ARCH_MAC99_U3;
    } else {
        pci_bus = pci_pmac_init(pic, get_system_memory(), get_system_io());
        machine_arch = ARCH_MAC99;
    }
    /* init basic PC hardware */
    escc_mem = escc_init(0, pic[0x25], pic[0x24],
                         serial_hds[0], serial_hds[1], ESCC_CLOCK, 4);
    memory_region_init_alias(escc_bar, NULL, "escc-bar",
                             escc_mem, 0, memory_region_size(escc_mem));

    macio = pci_create(pci_bus, -1, TYPE_NEWWORLD_MACIO);
    dev = DEVICE(macio);
    qdev_connect_gpio_out(dev, 0, pic[0x19]); /* CUDA */
    qdev_connect_gpio_out(dev, 1, pic[0x0d]); /* IDE */
    qdev_connect_gpio_out(dev, 2, pic[0x02]); /* IDE DMA */
    qdev_connect_gpio_out(dev, 3, pic[0x0e]); /* IDE */
    qdev_connect_gpio_out(dev, 4, pic[0x03]); /* IDE DMA */
    macio_init(macio, pic_mem, escc_bar);

    /* We only emulate 2 out of 3 IDE controllers for now */
    ide_drive_get(hd, MAX_IDE_BUS);

    macio_ide = MACIO_IDE(object_resolve_path_component(OBJECT(macio),
                                                        "ide[0]"));
    macio_ide_init_drives(macio_ide, hd);

    macio_ide = MACIO_IDE(object_resolve_path_component(OBJECT(macio),
                                                        "ide[1]"));
    macio_ide_init_drives(macio_ide, &hd[MAX_IDE_DEVS]);

    dev = DEVICE(object_resolve_path_component(OBJECT(macio), "cuda"));
    adb_bus = qdev_get_child_bus(dev, "adb.0");
    dev = qdev_create(adb_bus, TYPE_ADB_KEYBOARD);
    qdev_init_nofail(dev);
    dev = qdev_create(adb_bus, TYPE_ADB_MOUSE);
    qdev_init_nofail(dev);

    if (usb_enabled(machine_arch == ARCH_MAC99_U3)) {
        pci_create_simple(pci_bus, -1, "pci-ohci");
        /* U3 needs to use USB for input because Linux doesn't support via-cuda
        on PPC64 */
        if (machine_arch == ARCH_MAC99_U3) {
            usbdevice_create("keyboard");
            usbdevice_create("mouse");
        }
    }

    pci_vga_init(pci_bus);

    if (graphic_depth != 15 && graphic_depth != 32 && graphic_depth != 8) {
        graphic_depth = 15;
    }

    for (i = 0; i < nb_nics; i++) {
        pci_nic_init_nofail(&nd_table[i], pci_bus, "ne2k_pci", NULL);
    }

    /* The NewWorld NVRAM is not located in the MacIO device */
#ifdef CONFIG_KVM
    if (kvm_enabled() && getpagesize() > 4096) {
        /* We can't combine read-write and read-only in a single page, so
           move the NVRAM out of ROM again for KVM */
        nvram_addr = 0xFFE00000;
    }
#endif
    dev = qdev_create(NULL, TYPE_MACIO_NVRAM);
    qdev_prop_set_uint32(dev, "size", 0x2000);
    qdev_prop_set_uint32(dev, "it_shift", 1);
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, nvram_addr);
    nvr = MACIO_NVRAM(dev);
    pmac_format_nvram_partition(nvr, 0x2000);
    /* No PCI init: the BIOS will do it */

    fw_cfg = fw_cfg_init(0, 0, CFG_ADDR, CFG_ADDR + 2);
    fw_cfg_add_i16(fw_cfg, FW_CFG_MAX_CPUS, (uint16_t)max_cpus);
    fw_cfg_add_i32(fw_cfg, FW_CFG_ID, 1);
    fw_cfg_add_i64(fw_cfg, FW_CFG_RAM_SIZE, (uint64_t)ram_size);
    fw_cfg_add_i16(fw_cfg, FW_CFG_MACHINE_ID, machine_arch);
    fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_ADDR, kernel_base);
    fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_SIZE, kernel_size);
    if (kernel_cmdline) {
        fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_CMDLINE, cmdline_base);
        pstrcpy_targphys("cmdline", cmdline_base, TARGET_PAGE_SIZE, kernel_cmdline);
    } else {
        fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_CMDLINE, 0);
    }
    fw_cfg_add_i32(fw_cfg, FW_CFG_INITRD_ADDR, initrd_base);
    fw_cfg_add_i32(fw_cfg, FW_CFG_INITRD_SIZE, initrd_size);
    fw_cfg_add_i16(fw_cfg, FW_CFG_BOOT_DEVICE, ppc_boot_device);

    fw_cfg_add_i16(fw_cfg, FW_CFG_PPC_WIDTH, graphic_width);
    fw_cfg_add_i16(fw_cfg, FW_CFG_PPC_HEIGHT, graphic_height);
    fw_cfg_add_i16(fw_cfg, FW_CFG_PPC_DEPTH, graphic_depth);

    fw_cfg_add_i32(fw_cfg, FW_CFG_PPC_IS_KVM, kvm_enabled());
    if (kvm_enabled()) {
#ifdef CONFIG_KVM
        uint8_t *hypercall;

        fw_cfg_add_i32(fw_cfg, FW_CFG_PPC_TBFREQ, kvmppc_get_tbfreq());
        hypercall = g_malloc(16);
        kvmppc_get_hypercall(env, hypercall, 16);
        fw_cfg_add_bytes(fw_cfg, FW_CFG_PPC_KVM_HC, hypercall, 16);
        fw_cfg_add_i32(fw_cfg, FW_CFG_PPC_KVM_PID, getpid());
#endif
    } else {
        fw_cfg_add_i32(fw_cfg, FW_CFG_PPC_TBFREQ, TBFREQ);
    }
    /* Mac OS X requires a "known good" clock-frequency value; pass it one. */
    fw_cfg_add_i32(fw_cfg, FW_CFG_PPC_CLOCKFREQ, CLOCKFREQ);
    fw_cfg_add_i32(fw_cfg, FW_CFG_PPC_BUSFREQ, BUSFREQ);
    fw_cfg_add_i32(fw_cfg, FW_CFG_PPC_NVRAM_ADDR, nvram_addr);

    qemu_register_boot_set(fw_cfg_boot_set, fw_cfg);
}

static int core99_kvm_type(const char *arg)
{
    /* Always force PR KVM */
    return 2;
}

static QEMUMachine core99_machine = {
    .name = "mac99",
    .desc = "Mac99 based PowerMAC",
    .init = ppc_core99_init,
    .max_cpus = MAX_CPUS,
    .default_boot_order = "cd",
    .kvm_type = core99_kvm_type,
};

static void core99_machine_init(void)
{
    qemu_register_machine(&core99_machine);
}

machine_init(core99_machine_init);
