/*
 * QEMU Ingenic T41 XBurst2 SoC Board Support
 *
 * Copyright (c) 2024 OpenSensor Project
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * T41 Memory Map (from device tree):
 *   0x10000000 - Clock controller
 *   0x10002000 - TCU (Timer/Counter Unit)
 *   0x10010000 - Pin controller / GPIO
 *   0x12000000 - Core OST (System Timer)
 *   0x12100000 - Core OST (per-CPU)
 *   0x12300000 - Core interrupt controller
 *   0x12502000 - NNA DMA
 *   0x12600000 - NNA ORAM (896KB)
 *   0x12b00000 - AIP (AI Processor)
 *   0x13010000 - UART0-5
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/datadir.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/mips/mips.h"
#include "hw/mips/bootloader.h"
#include "hw/char/serial-mm.h"
#include "hw/loader.h"
#include "hw/irq.h"
#include "hw/qdev-clock.h"
#include "hw/misc/unimp.h"
#include "hw/sysbus.h"
#include "elf.h"
#include "system/reset.h"
#include "system/system.h"
#include "system/device_tree.h"
#include "system/blockdev.h"
#include "qom/object.h"
#include "cpu.h"

#include <libfdt.h>

/* T41 SoC Memory Map */
#define T41_LOWMEM_BASE      0x00000000
#define T41_LOWMEM_SIZE      (256 * MiB)
#define T41_KERNEL_LOAD_ADDR 0x00010000  /* Kernel linked at 0x80010000 */

#define T41_CLOCK_BASE       0x10000000
#define T41_TCU_BASE         0x10002000
#define T41_PINCTRL_BASE     0x10010000

#define T41_OST_BASE         0x12000000
#define T41_CORE_OST_BASE    0x12100000
#define T41_CCU_BASE         0x12200000  /* CPU Cluster Unit (SMP) */
#define T41_INTC_BASE        0x12300000

#define T41_NNA_DMA_BASE     0x12502000
#define T41_NNA_ORAM_BASE    0x12600000
#define T41_NNA_ORAM_SIZE    (896 * KiB)
#define T41_AIP_BASE         0x12b00000

/* UART base addresses (8250-compatible, reg-shift=2) */
#define T41_UART0_BASE       0x10030000
#define T41_UART1_BASE       0x10031000
#define T41_UART2_BASE       0x10032000
#define T41_UART3_BASE       0x10033000
#define T41_UART4_BASE       0x10034000
#define T41_UART5_BASE       0x10035000
#define T41_UART_SIZE        0x1000
#define T41_UART_REG_SHIFT   2

/* MMC/SD Controller base addresses */
#define T41_MSC0_BASE        0x13060000
#define T41_MSC1_BASE        0x13070000
#define T41_MSC_SIZE         0x1000

/* Reset vector and bootloader */
#define T41_RESET_ADDRESS    0x1fc00000
#define T41_BROM_SIZE        0x10000  /* 64KB boot ROM */

/* DTB location - place after kernel and initrd (max ~9MB), before 16MB flash limit */
#define T41_DTB_BASE         0x00F00000  /* 15MB - after kernel+initrd, within 16MB */

#define TYPE_INGENIC_T41 "ingenic-t41"
OBJECT_DECLARE_SIMPLE_TYPE(IngenicT41State, INGENIC_T41)

struct IngenicT41State {
    SysBusDevice parent_obj;

    MIPSCPU *cpu;
    Clock *cpuclk;
    MemoryRegion oram;  /* NNA On-chip SRAM */
    MemoryRegion brom;  /* Boot ROM at reset vector */
    SerialMM *uart0;
    SerialMM *uart1;
    DeviceState *intc;  /* Interrupt controller */
    hwaddr kernel_entry;
    hwaddr dtb_addr;
};

/* Reset handler to initialize UART LCR after device reset */
static void t41_machine_reset(void *opaque)
{
    IngenicT41State *s = opaque;

    /* Set LCR to 0x03 (8 data bits, 1 stop bit, no parity)
     * This allows the kernel's UART probe to detect the UARTs.
     * We do this in the reset handler because the serial device's
     * reset handler runs before this and sets LCR to 0.
     */
    if (s->uart0) {
        s->uart0->serial.lcr = 0x03;
    }
    if (s->uart1) {
        s->uart1->serial.lcr = 0x03;
    }
}

static void t41_init(MachineState *machine)
{
    IngenicT41State *s;
    MemoryRegion *system_memory = get_system_memory();
    const char *kernel_filename = machine->kernel_filename;
    const char *kernel_cmdline = machine->kernel_cmdline;

    /* Create the SoC state */
    s = g_new0(IngenicT41State, 1);

    /* Initialize CPU clock */
    s->cpuclk = clock_new(OBJECT(machine), "cpu-refclk");
    clock_set_hz(s->cpuclk, 1200000000); /* 1.2 GHz */

    /* Create CPU - XBurst2 core */
    s->cpu = MIPS_CPU(cpu_create(machine->cpu_type));
    cpu_mips_irq_init_cpu(s->cpu);
    cpu_mips_clock_init(s->cpu);

    /* Create main RAM - use machine->ram which is already created by the machine */
    memory_region_add_subregion(system_memory, T41_LOWMEM_BASE, machine->ram);

    /* Create NNA ORAM - on-chip SRAM at 0x12600000 */
    memory_region_init_ram(&s->oram, NULL, "t41.oram", T41_NNA_ORAM_SIZE,
                           &error_fatal);
    memory_region_add_subregion(system_memory, T41_NNA_ORAM_BASE, &s->oram);

    /* Create boot ROM at reset vector area (0x1fc00000)
     * This provides:
     * 1. A bootloader that sets up registers and jumps to the kernel
     * 2. Exception handlers for the kernel
     */
    memory_region_init_ram(&s->brom, NULL, "t41.brom", T41_BROM_SIZE, &error_fatal);
    memory_region_add_subregion(system_memory, T41_RESET_ADDRESS, &s->brom);

    /* Create the interrupt controller FIRST so we can connect UART IRQs */
    {
        DeviceState *intc_dev;
        SysBusDevice *intc_sbd;

        intc_dev = qdev_new("ingenic-intc");
        intc_sbd = SYS_BUS_DEVICE(intc_dev);
        sysbus_realize_and_unref(intc_sbd, &error_fatal);
        sysbus_mmio_map(intc_sbd, 0, T41_INTC_BASE);
        /* Connect INTC output to CPU IRQ 2 */
        sysbus_connect_irq(intc_sbd, 0, s->cpu->env.irq[2]);

        /* Store for connecting peripheral IRQs */
        s->intc = intc_dev;
    }

    /* Create UARTs for console (memory-mapped, reg-shift=2)
     * Note: Wyze camera uses UART1 for console, so we connect serial_hd(0) to both
     * UART0 and UART1 to catch early boot messages from either.
     *
     * Standard 8250 uses registers 0-7 (0x00-0x1C with reg-shift=2).
     * Ingenic UART has extra registers:
     *   ISR (8), UMR (9), UACR (10), RCR (16), TCR (17)
     * We create stub regions for these extra registers after the 8250 region.
     *
     * UART IRQs (from t41-irq.h):
     *   IRQ_UART0 = 32 + 19 = 51 (INTC bank 1, bit 19)
     *   IRQ_UART1 = 32 + 18 = 50 (INTC bank 1, bit 18)
     */
    s->uart0 = serial_mm_init(system_memory, T41_UART0_BASE, T41_UART_REG_SHIFT,
                              qdev_get_gpio_in(s->intc, 51), 115200,
                              serial_hd(0), DEVICE_LITTLE_ENDIAN);
    s->uart1 = serial_mm_init(system_memory, T41_UART1_BASE, T41_UART_REG_SHIFT,
                              qdev_get_gpio_in(s->intc, 50), 115200,
                              serial_hd(0), DEVICE_LITTLE_ENDIAN);

    /* Create stub regions for Ingenic-specific UART registers (offset 0x20-0x4C)
     * These are registers 8-19 with reg-shift=2, starting after the standard 8250 regs
     */
    create_unimplemented_device("t41-uart0-ext", T41_UART0_BASE + 0x20, 0xE0);
    create_unimplemented_device("t41-uart1-ext", T41_UART1_BASE + 0x20, 0xE0);

    /* Register our reset handler to run AFTER the serial device reset handlers.
     * This allows us to set the LCR to a non-zero value so the kernel can detect the UARTs.
     */
    qemu_register_reset(t41_machine_reset, s);

    /* Create stub devices for unimplemented UARTs (kernel probes UART0-5) */
    create_unimplemented_device("t41-uart2", T41_UART2_BASE, T41_UART_SIZE);
    create_unimplemented_device("t41-uart3", T41_UART3_BASE, T41_UART_SIZE);
    create_unimplemented_device("t41-uart4", T41_UART4_BASE, T41_UART_SIZE);
    create_unimplemented_device("t41-uart5", T41_UART5_BASE, T41_UART_SIZE);

    /* Create CPM (Clock Power Management) device */
    {
        DeviceState *cpm_dev = qdev_new("ingenic-cpm");
        sysbus_realize_and_unref(SYS_BUS_DEVICE(cpm_dev), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(cpm_dev), 0, T41_CLOCK_BASE);
    }

    create_unimplemented_device("t41-tcu", T41_TCU_BASE, 0x200);
    create_unimplemented_device("t41-rtc", 0x10003000, 0x2000);    /* RTC */
    create_unimplemented_device("t41-pinctrl", T41_PINCTRL_BASE, 0x4000);  /* 4 GPIO ports */
    create_unimplemented_device("t41-aic", 0x10020000, 0x2000);    /* Audio */
    create_unimplemented_device("t41-mipi", 0x10022000, 0x2000);   /* MIPI PHY/CSI */
    create_unimplemented_device("t41-ssi", 0x10040000, 0x5000);    /* SSI/SPI */
    create_unimplemented_device("t41-usb-phy", 0x10060000, 0x2000); /* USB PHY */
    create_unimplemented_device("t41-i2c0", 0x10050000, 0x1000);   /* I2C 0 */
    create_unimplemented_device("t41-i2c1", 0x10051000, 0x1000);   /* I2C 1 */
    create_unimplemented_device("t41-i2c2", 0x10052000, 0x1000);   /* I2C 2 */
    create_unimplemented_device("t41-ccu", T41_CCU_BASE, 0x1000);  /* CPU Cluster Unit */
    create_unimplemented_device("t41-nna-dma", T41_NNA_DMA_BASE, 0x1000);
    create_unimplemented_device("t41-aip", T41_AIP_BASE, 0x10000);

    /* AHB0 bus devices */
    create_unimplemented_device("t41-ldc", 0x13040000, 0x10000);   /* LDC */
    create_unimplemented_device("t41-lcdc", 0x13050000, 0x10000);  /* LCDC */
    create_unimplemented_device("t41-msc0", 0x13060000, 0x10000);  /* MMC/SD 0 */
    create_unimplemented_device("t41-msc1", 0x13070000, 0x10000);  /* MMC/SD 1 */
    create_unimplemented_device("t41-ipu", 0x13080000, 0x10000);   /* IPU */
    create_unimplemented_device("t41-i2d", 0x130b0000, 0x10000);   /* I2D */
    create_unimplemented_device("t41-vo", 0x130c0000, 0x10000);    /* Video Output */
    create_unimplemented_device("t41-dbox", 0x130d0000, 0x10000);  /* Draw Box */
    create_unimplemented_device("t41-isp", 0x13300000, 0x80000);   /* ISP */

    /* AHB2 bus devices */
    create_unimplemented_device("t41-pdma", 0x13420000, 0x10000);  /* PDMA controller */

    /* Create HARB0 (AHB0 Bus Controller with CPU ID) device */
    {
        DeviceState *harb0_dev = qdev_new("ingenic-harb0");
        sysbus_realize_and_unref(SYS_BUS_DEVICE(harb0_dev), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(harb0_dev), 0, 0x13000000);
    }

    /* Create EFUSE (OTP/EFUSE) device for chip identification */
    {
        DeviceState *efuse_dev = qdev_new("ingenic-efuse");
        sysbus_realize_and_unref(SYS_BUS_DEVICE(efuse_dev), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(efuse_dev), 0, 0x13540000);
    }

    /* Create SFC (SPI Flash Controller) device */
    {
        DeviceState *sfc_dev;
        SysBusDevice *sfc_sbd;
        DriveInfo *dinfo;

        sfc_dev = qdev_new("ingenic-sfc");

        /* Check for MTD flash drive from command line: -mtdblock flash.bin */
        dinfo = drive_get(IF_MTD, 0, 0);
        if (dinfo) {
            qdev_prop_set_drive(sfc_dev, "drive", blk_by_legacy_dinfo(dinfo));
        }

        sfc_sbd = SYS_BUS_DEVICE(sfc_dev);
        sysbus_realize_and_unref(sfc_sbd, &error_fatal);
        sysbus_mmio_map(sfc_sbd, 0, 0x13440000);
        /* Connect SFC IRQ to INTC input 7 (IRQ_SFC0) */
        sysbus_connect_irq(sfc_sbd, 0, qdev_get_gpio_in(s->intc, 7));
    }

    create_unimplemented_device("t41-sfc1", 0x13450000, 0x10000);  /* SFC1 controller */
    create_unimplemented_device("t41-gmac", 0x134b0000, 0x10000);  /* Ethernet MAC */
    create_unimplemented_device("t41-otg", 0x13500000, 0x40000);   /* USB OTG */
    create_unimplemented_device("t41-efuse", 0x13540000, 0x10000); /* eFuse */

    /* Create MSC (MMC/SD Controller) stub devices to prevent boot hangs
     * MSC0 IRQ = 37 (32+5), MSC1 IRQ = 36 (32+4) - INTC inputs 5 and 4
     */
    {
        DeviceState *msc0_dev, *msc1_dev;
        SysBusDevice *msc0_sbd, *msc1_sbd;

        msc0_dev = qdev_new("ingenic-msc");
        msc0_sbd = SYS_BUS_DEVICE(msc0_dev);
        sysbus_realize_and_unref(msc0_sbd, &error_fatal);
        sysbus_mmio_map(msc0_sbd, 0, T41_MSC0_BASE);
        sysbus_connect_irq(msc0_sbd, 0, qdev_get_gpio_in(s->intc, 5));

        msc1_dev = qdev_new("ingenic-msc");
        msc1_sbd = SYS_BUS_DEVICE(msc1_dev);
        sysbus_realize_and_unref(msc1_sbd, &error_fatal);
        sysbus_mmio_map(msc1_sbd, 0, T41_MSC1_BASE);
        sysbus_connect_irq(msc1_sbd, 0, qdev_get_gpio_in(s->intc, 4));
    }

    /* Create the OST (Operating System Timer) device
     * The OST provides:
     *  - Global OST at 0x12000000: 64-bit clocksource
     *  - Core OST at 0x12100000: Per-CPU clock events
     * Timer interrupt is connected to MIPS CPU IRQ 4 (CORE_SYS_OST_IRQ)
     */
    {
        DeviceState *ost_dev;
        SysBusDevice *ost_sbd;

        ost_dev = qdev_new("ingenic-ost");
        ost_sbd = SYS_BUS_DEVICE(ost_dev);
        sysbus_realize_and_unref(ost_sbd, &error_fatal);

        /* Map the two OST regions */
        sysbus_mmio_map(ost_sbd, 0, T41_OST_BASE);      /* Global OST */
        sysbus_mmio_map(ost_sbd, 1, T41_CORE_OST_BASE); /* Core OST */

        /* Connect timer IRQ to CPU IRQ 4 (CORE_SYS_OST_IRQ)
         * MIPS CPU IRQs are accessed via env->irq[] after cpu_mips_irq_init_cpu()
         */
        sysbus_connect_irq(ost_sbd, 0, s->cpu->env.irq[4]);
    }

    /* Load kernel if specified */
    if (kernel_filename) {
        uint64_t kernel_entry, kernel_high;
        int kernel_size;
        void *fdt = NULL;
        int fdt_size = 0;
        ram_addr_t initrd_offset = 0;
        ssize_t initrd_size = 0;

        kernel_size = load_elf(kernel_filename, NULL,
                               cpu_mips_kseg0_to_phys, NULL,
                               &kernel_entry, NULL, &kernel_high, NULL,
                               ELFDATA2LSB, EM_MIPS, 1, 0);

        if (kernel_size < 0) {
            /* Try loading as uImage */
            kernel_size = load_uimage(kernel_filename, &kernel_entry,
                                      NULL, NULL, NULL, NULL);
        }

        if (kernel_size < 0) {
            /* Try loading as raw binary at T41_KERNEL_LOAD_ADDR
             * Kernel is linked at 0x80010000 (physical 0x00010000)
             */
            kernel_size = load_image_targphys(kernel_filename,
                                              T41_KERNEL_LOAD_ADDR,
                                              machine->ram_size - T41_KERNEL_LOAD_ADDR,
                                              NULL);
            if (kernel_size > 0) {
                /* For raw binaries, entry is at 0x80010400 (skip 1KB header)
                 * The kernel has 1KB of zeros before actual code starts
                 */
                kernel_entry = cpu_mips_phys_to_kseg0(NULL, T41_KERNEL_LOAD_ADDR + 0x400);
                kernel_high = T41_KERNEL_LOAD_ADDR + kernel_size;
            }
        }

        if (kernel_size < 0) {
            error_report("Could not load kernel '%s'", kernel_filename);
            exit(1);
        }

        s->kernel_entry = kernel_entry;

        /* Load initrd if specified */
        if (machine->initrd_filename) {
            initrd_size = get_image_size(machine->initrd_filename, NULL);
            if (initrd_size > 0) {
                /* Place initrd after kernel, page-aligned */
                initrd_offset = ROUND_UP(kernel_high + 64 * KiB, 4 * KiB);
                if (initrd_offset + initrd_size > machine->ram_size) {
                    error_report("Memory too small for initrd '%s'",
                                 machine->initrd_filename);
                    exit(1);
                }
                initrd_size = load_image_targphys(machine->initrd_filename,
                                                  initrd_offset,
                                                  machine->ram_size - initrd_offset,
                                                  NULL);
                if (initrd_size <= 0) {
                    error_report("Could not load initrd '%s'",
                                 machine->initrd_filename);
                    exit(1);
                }
            }
        }

        /* Load DTB if specified */
        if (machine->dtb) {
            int mem_offset;

            fdt = load_device_tree(machine->dtb, &fdt_size);
            if (!fdt) {
                error_report("Could not load DTB '%s'", machine->dtb);
                exit(1);
            }

            /* Create or update memory node in DTB
             * Stock Ingenic DTBs don't have a memory node - the bootloader
             * passes memory info via bootargs. QEMU needs the node.
             */
            mem_offset = fdt_path_offset(fdt, "/memory@0");
            if (mem_offset < 0) {
                mem_offset = fdt_path_offset(fdt, "/memory");
            }
            if (mem_offset < 0) {
                /* Memory node doesn't exist, create it */
                mem_offset = fdt_add_subnode(fdt, 0, "memory@0");
                if (mem_offset < 0) {
                    error_report("Could not create /memory@0 node: %s",
                                 fdt_strerror(mem_offset));
                    exit(1);
                }
                qemu_fdt_setprop_string(fdt, "/memory@0", "device_type", "memory");
            }
            /* Set the memory size */
            qemu_fdt_setprop_sized_cells(fdt, "/memory@0", "reg",
                                          1, T41_LOWMEM_BASE,
                                          1, machine->ram_size);

            /* Create /chosen node if it doesn't exist */
            if (fdt_path_offset(fdt, "/chosen") < 0) {
                int chosen_offset = fdt_add_subnode(fdt, 0, "chosen");
                if (chosen_offset < 0) {
                    error_report("Could not create /chosen node: %s",
                                 fdt_strerror(chosen_offset));
                    exit(1);
                }
            }

            /* Set bootargs - append rd_start/rd_size if initrd is loaded */
            if (initrd_size > 0) {
                char bootargs[1024];
                /* Use 32-bit address for 32-bit MIPS kernel */
                uint32_t rd_addr = (uint32_t)cpu_mips_phys_to_kseg0(NULL,
                                                                    initrd_offset);
                snprintf(bootargs, sizeof(bootargs),
                         "%s rd_start=0x%08x rd_size=%zd",
                         kernel_cmdline ? kernel_cmdline : "",
                         rd_addr, initrd_size);
                qemu_fdt_setprop_string(fdt, "/chosen", "bootargs", bootargs);
            } else if (kernel_cmdline && kernel_cmdline[0]) {
                qemu_fdt_setprop_string(fdt, "/chosen", "bootargs", kernel_cmdline);
            }

            /* Load DTB into memory using CPU's address space */
            s->dtb_addr = cpu_mips_phys_to_kseg0(NULL, T41_DTB_BASE);
            fdt_size = fdt_totalsize(fdt);
            rom_add_blob_fixed_as("dtb", fdt, fdt_size, T41_DTB_BASE,
                                  CPU(s->cpu)->as);
            g_free(fdt);
        } else {
            s->dtb_addr = 0;
        }

        /* Generate bootloader in boot ROM
         * The bootloader sets up registers per UHI boot protocol:
         *   a0 = -2 (indicates DTB is passed)
         *   a1 = DTB address (kseg0)
         *   a2 = 0
         *   a3 = 0
         * Then jumps to kernel entry point.
         *
         * We put the bootloader at offset 0x1000 to avoid conflict with
         * exception vectors. The reset vector at 0x000 jumps to the bootloader.
         */
        {
            uint32_t *brom = memory_region_get_ram_ptr(&s->brom);
            void *p;

            /* Fill with NOPs first */
            memset(brom, 0, T41_BROM_SIZE);

            /* Generate bootloader at offset 0x1000 */
            p = (char *)brom + 0x1000;
            bl_gen_jump_kernel(&p,
                               false, 0,                    /* don't set SP */
                               true, (target_ulong)-2,      /* a0 = -2 */
                               true, s->dtb_addr,           /* a1 = DTB addr */
                               true, 0,                     /* a2 = 0 */
                               true, 0,                     /* a3 = 0 */
                               kernel_entry);

            /* Reset vector at 0x000 - jump to bootloader at 0x1000
             * The CPU starts at 0xbfc00000 (kseg1 mapping of 0x1fc00000)
             * We want to jump to 0xbfc01000 (bootloader at offset 0x1000)
             * j instruction: opcode=000010, target = (addr >> 2) & 0x03ffffff
             * j 0xbfc01000: target = 0xbfc01000 >> 2 = 0x2ff00400
             *               j = 0x08000000 | (0x2ff00400 & 0x03ffffff)
             *                 = 0x08000000 | 0x03f00400 = 0x0bf00400
             */
            brom[0] = 0x0bf00400;  /* j 0xbfc01000 */
            brom[1] = 0x00000000;  /* nop (delay slot) */

            /* Exception handlers that loop forever
             * With BEV=1 (default after reset), exception base is 0xbfc00000
             */
            uint32_t *exc;

            /* Cache error at 0x200 */
            exc = (uint32_t *)((char *)brom + 0x200);
            exc[0] = 0x1000ffff;  /* b . */
            exc[1] = 0x00000000;  /* nop */

            /* General exception at 0x380 */
            exc = (uint32_t *)((char *)brom + 0x380);
            exc[0] = 0x1000ffff;  /* b . */
            exc[1] = 0x00000000;  /* nop */
        }

        /* Set CPU to start at reset vector */
        s->cpu->env.active_tc.PC = cpu_mips_phys_to_kseg1(NULL, T41_RESET_ADDRESS);
    }
}

static void t41_machine_init(MachineClass *mc)
{
    mc->desc = "Ingenic T41 XBurst2 SoC";
    mc->init = t41_init;
    mc->default_cpu_type = MIPS_CPU_TYPE_NAME("XBurstR2");
    mc->default_ram_size = 256 * MiB;
    mc->default_ram_id = "t41.ram";
    mc->max_cpus = 2;
}

DEFINE_MACHINE("ingenic-t41", t41_machine_init)

