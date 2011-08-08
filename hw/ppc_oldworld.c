
/*
 * QEMU OldWorld PowerMac (currently ~G3 Beige) hardware System Emulator
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
 */
#include "hw.h"
#include "ppc.h"
#include "ppc_mac.h"
#include "mac_dbdma.h"
#include "nvram.h"
#include "pc.h"
#include "sysemu.h"
#include "net.h"
#include "isa.h"
#include "pci.h"
#include "usb-ohci.h"
#include "boards.h"
#include "fw_cfg.h"
#include "escc.h"
#include "ide.h"
#include "loader.h"
#include "elf.h"
#include "kvm.h"
#include "kvm_ppc.h"
#include "blockdev.h"
#include "exec-memory.h"

#define MAX_IDE_BUS 2
#define CFG_ADDR 0xf0000510

static int fw_cfg_boot_set(void *opaque, const char *boot_device)
{
    fw_cfg_add_i16(opaque, FW_CFG_BOOT_DEVICE, boot_device[0]);
    return 0;
}


static uint64_t translate_kernel_address(void *opaque, uint64_t addr)
{
    return (addr & 0x0fffffff) + KERNEL_LOAD_ADDR;
}

static target_phys_addr_t round_page(target_phys_addr_t addr)
{
    return (addr + TARGET_PAGE_SIZE - 1) & TARGET_PAGE_MASK;
}

static void ppc_heathrow_init (ram_addr_t ram_size,
                               const char *boot_device,
                               const char *kernel_filename,
                               const char *kernel_cmdline,
                               const char *initrd_filename,
                               const char *cpu_model)
{
    CPUState *env = NULL;
    char *filename;
    qemu_irq *pic, **heathrow_irqs;
    int linux_boot, i;
    ram_addr_t ram_offset, bios_offset;
    uint32_t kernel_base, initrd_base, cmdline_base = 0;
    int32_t kernel_size, initrd_size;
    PCIBus *pci_bus;
    MacIONVRAMState *nvr;
    int bios_size;
    int pic_mem_index, nvram_mem_index, dbdma_mem_index, cuda_mem_index;
    int escc_mem_index, ide_mem_index[2];
    uint16_t ppc_boot_device;
    DriveInfo *hd[MAX_IDE_BUS * MAX_IDE_DEVS];
    void *fw_cfg;
    void *dbdma;

    linux_boot = (kernel_filename != NULL);

    /* init CPUs */
    if (cpu_model == NULL)
        cpu_model = "G3";
    for (i = 0; i < smp_cpus; i++) {
        env = cpu_init(cpu_model);
        if (!env) {
            fprintf(stderr, "Unable to find PowerPC CPU definition\n");
            exit(1);
        }
        /* Set time-base frequency to 16.6 Mhz */
        cpu_ppc_tb_init(env,  16600000UL);
        qemu_register_reset((QEMUResetHandler*)&cpu_reset, env);
    }

    /* allocate RAM */
    if (ram_size > (2047 << 20)) {
        fprintf(stderr,
                "qemu: Too much memory for this machine: %d MB, maximum 2047 MB\n",
                ((unsigned int)ram_size / (1 << 20)));
        exit(1);
    }

    ram_offset = qemu_ram_alloc(NULL, "ppc_heathrow.ram", ram_size);
    cpu_register_physical_memory(0, ram_size, ram_offset);

    /* allocate and load BIOS */
    bios_offset = qemu_ram_alloc(NULL, "ppc_heathrow.bios", BIOS_SIZE);
    if (bios_name == NULL)
        bios_name = PROM_FILENAME;
    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
    cpu_register_physical_memory(PROM_ADDR, BIOS_SIZE, bios_offset | IO_MEM_ROM);

    /* Load OpenBIOS (ELF) */
    if (filename) {
        bios_size = load_elf(filename, 0, NULL, NULL, NULL, NULL,
                             1, ELF_MACHINE, 0);
        qemu_free(filename);
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
            hw_error("qemu: could not load kernel '%s'\n",
                      kernel_filename);
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
        for (i = 0; boot_device[i] != '\0'; i++) {
            /* TOFIX: for now, the second IDE channel is not properly
             *        used by OHW. The Mac floppy disk are not emulated.
             *        For now, OHW cannot boot from the network.
             */
#if 0
            if (boot_device[i] >= 'a' && boot_device[i] <= 'f') {
                ppc_boot_device = boot_device[i];
                break;
            }
#else
            if (boot_device[i] >= 'c' && boot_device[i] <= 'd') {
                ppc_boot_device = boot_device[i];
                break;
            }
#endif
        }
        if (ppc_boot_device == '\0') {
            fprintf(stderr, "No valid boot device for G3 Beige machine\n");
            exit(1);
        }
    }

    isa_mem_base = 0x80000000;

    /* Register 2 MB of ISA IO space */
    isa_mmio_init(0xfe000000, 0x00200000);

    /* XXX: we register only 1 output pin for heathrow PIC */
    heathrow_irqs = qemu_mallocz(smp_cpus * sizeof(qemu_irq *));
    heathrow_irqs[0] =
        qemu_mallocz(smp_cpus * sizeof(qemu_irq) * 1);
    /* Connect the heathrow PIC outputs to the 6xx bus */
    for (i = 0; i < smp_cpus; i++) {
        switch (PPC_INPUT(env)) {
        case PPC_FLAGS_INPUT_6xx:
            heathrow_irqs[i] = heathrow_irqs[0] + (i * 1);
            heathrow_irqs[i][0] =
                ((qemu_irq *)env->irq_inputs)[PPC6xx_INPUT_INT];
            break;
        default:
            hw_error("Bus model not supported on OldWorld Mac machine\n");
        }
    }

    /* init basic PC hardware */
    if (PPC_INPUT(env) != PPC_FLAGS_INPUT_6xx) {
        hw_error("Only 6xx bus is supported on heathrow machine\n");
    }
    pic = heathrow_pic_init(&pic_mem_index, 1, heathrow_irqs);
    pci_bus = pci_grackle_init(0xfec00000, pic,
                               get_system_memory(),
                               get_system_io());
    pci_vga_init(pci_bus);

    escc_mem_index = escc_init(0x80013000, pic[0x0f], pic[0x10], serial_hds[0],
                               serial_hds[1], ESCC_CLOCK, 4);

    for(i = 0; i < nb_nics; i++)
        pci_nic_init_nofail(&nd_table[i], "ne2k_pci", NULL);


    ide_drive_get(hd, MAX_IDE_BUS);

    /* First IDE channel is a MAC IDE on the MacIO bus */
    dbdma = DBDMA_init(&dbdma_mem_index);
    ide_mem_index[0] = -1;
    ide_mem_index[1] = pmac_ide_init(hd, pic[0x0D], dbdma, 0x16, pic[0x02]);

    /* Second IDE channel is a CMD646 on the PCI bus */
    hd[0] = hd[MAX_IDE_DEVS];
    hd[1] = hd[MAX_IDE_DEVS + 1];
    hd[3] = hd[2] = NULL;
    pci_cmd646_ide_init(pci_bus, hd, 0);

    /* cuda also initialize ADB */
    cuda_init(&cuda_mem_index, pic[0x12]);

    adb_kbd_init(&adb_bus);
    adb_mouse_init(&adb_bus);

    nvr = macio_nvram_init(&nvram_mem_index, 0x2000, 4);
    pmac_format_nvram_partition(nvr, 0x2000);

    macio_init(pci_bus, PCI_DEVICE_ID_APPLE_343S1201, 1, pic_mem_index,
               dbdma_mem_index, cuda_mem_index, nvr, 2, ide_mem_index,
               escc_mem_index);

    if (usb_enabled) {
        usb_ohci_init_pci(pci_bus, -1);
    }

    if (graphic_depth != 15 && graphic_depth != 32 && graphic_depth != 8)
        graphic_depth = 15;

    /* No PCI init: the BIOS will do it */

    fw_cfg = fw_cfg_init(0, 0, CFG_ADDR, CFG_ADDR + 2);
    fw_cfg_add_i32(fw_cfg, FW_CFG_ID, 1);
    fw_cfg_add_i64(fw_cfg, FW_CFG_RAM_SIZE, (uint64_t)ram_size);
    fw_cfg_add_i16(fw_cfg, FW_CFG_MACHINE_ID, ARCH_HEATHROW);
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
        hypercall = qemu_malloc(16);
        kvmppc_get_hypercall(env, hypercall, 16);
        fw_cfg_add_bytes(fw_cfg, FW_CFG_PPC_KVM_HC, hypercall, 16);
        fw_cfg_add_i32(fw_cfg, FW_CFG_PPC_KVM_PID, getpid());
#endif
    } else {
        fw_cfg_add_i32(fw_cfg, FW_CFG_PPC_TBFREQ, get_ticks_per_sec());
    }

    qemu_register_boot_set(fw_cfg_boot_set, fw_cfg);
}

static QEMUMachine heathrow_machine = {
    .name = "g3beige",
    .desc = "Heathrow based PowerMAC",
    .init = ppc_heathrow_init,
    .max_cpus = MAX_CPUS,
#ifndef TARGET_PPC64
    .is_default = 1,
#endif
};

static void heathrow_machine_init(void)
{
    qemu_register_machine(&heathrow_machine);
}

machine_init(heathrow_machine_init);
