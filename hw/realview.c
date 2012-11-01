/*
 * ARM RealView Baseboard System emulation.
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "sysbus.h"
#include "arm-misc.h"
#include "primecell.h"
#include "devices.h"
#include "pci.h"
#include "net.h"
#include "sysemu.h"
#include "boards.h"
#include "i2c.h"
#include "blockdev.h"
#include "exec-memory.h"

#define SMP_BOOT_ADDR 0xe0000000
#define SMP_BOOTREG_ADDR 0x10000030

/* Board init.  */

static struct arm_boot_info realview_binfo = {
    .smp_loader_start = SMP_BOOT_ADDR,
    .smp_bootreg_addr = SMP_BOOTREG_ADDR,
};

/* The following two lists must be consistent.  */
enum realview_board_type {
    BOARD_EB,
    BOARD_EB_MPCORE,
    BOARD_PB_A8,
    BOARD_PBX_A9,
};

static const int realview_board_id[] = {
    0x33b,
    0x33b,
    0x769,
    0x76d
};

static void realview_init(QEMUMachineInitArgs *args,
                          enum realview_board_type board_type)
{
    ARMCPU *cpu = NULL;
    CPUARMState *env;
    MemoryRegion *sysmem = get_system_memory();
    MemoryRegion *ram_lo = g_new(MemoryRegion, 1);
    MemoryRegion *ram_hi = g_new(MemoryRegion, 1);
    MemoryRegion *ram_alias = g_new(MemoryRegion, 1);
    MemoryRegion *ram_hack = g_new(MemoryRegion, 1);
    DeviceState *dev, *sysctl, *gpio2, *pl041;
    SysBusDevice *busdev;
    qemu_irq *irqp;
    qemu_irq pic[64];
    qemu_irq mmc_irq[2];
    PCIBus *pci_bus;
    NICInfo *nd;
    i2c_bus *i2c;
    int n;
    int done_nic = 0;
    qemu_irq cpu_irq[4];
    int is_mpcore = 0;
    int is_pb = 0;
    uint32_t proc_id = 0;
    uint32_t sys_id;
    ram_addr_t low_ram_size;
    ram_addr_t ram_size = args->ram_size;

    switch (board_type) {
    case BOARD_EB:
        break;
    case BOARD_EB_MPCORE:
        is_mpcore = 1;
        break;
    case BOARD_PB_A8:
        is_pb = 1;
        break;
    case BOARD_PBX_A9:
        is_mpcore = 1;
        is_pb = 1;
        break;
    }
    for (n = 0; n < smp_cpus; n++) {
        cpu = cpu_arm_init(args->cpu_model);
        if (!cpu) {
            fprintf(stderr, "Unable to find CPU definition\n");
            exit(1);
        }
        irqp = arm_pic_init_cpu(cpu);
        cpu_irq[n] = irqp[ARM_PIC_CPU_IRQ];
    }
    env = &cpu->env;
    if (arm_feature(env, ARM_FEATURE_V7)) {
        if (is_mpcore) {
            proc_id = 0x0c000000;
        } else {
            proc_id = 0x0e000000;
        }
    } else if (arm_feature(env, ARM_FEATURE_V6K)) {
        proc_id = 0x06000000;
    } else if (arm_feature(env, ARM_FEATURE_V6)) {
        proc_id = 0x04000000;
    } else {
        proc_id = 0x02000000;
    }

    if (is_pb && ram_size > 0x20000000) {
        /* Core tile RAM.  */
        low_ram_size = ram_size - 0x20000000;
        ram_size = 0x20000000;
        memory_region_init_ram(ram_lo, "realview.lowmem", low_ram_size);
        vmstate_register_ram_global(ram_lo);
        memory_region_add_subregion(sysmem, 0x20000000, ram_lo);
    }

    memory_region_init_ram(ram_hi, "realview.highmem", ram_size);
    vmstate_register_ram_global(ram_hi);
    low_ram_size = ram_size;
    if (low_ram_size > 0x10000000)
      low_ram_size = 0x10000000;
    /* SDRAM at address zero.  */
    memory_region_init_alias(ram_alias, "realview.alias",
                             ram_hi, 0, low_ram_size);
    memory_region_add_subregion(sysmem, 0, ram_alias);
    if (is_pb) {
        /* And again at a high address.  */
        memory_region_add_subregion(sysmem, 0x70000000, ram_hi);
    } else {
        ram_size = low_ram_size;
    }

    sys_id = is_pb ? 0x01780500 : 0xc1400400;
    sysctl = qdev_create(NULL, "realview_sysctl");
    qdev_prop_set_uint32(sysctl, "sys_id", sys_id);
    qdev_prop_set_uint32(sysctl, "proc_id", proc_id);
    qdev_init_nofail(sysctl);
    sysbus_mmio_map(sysbus_from_qdev(sysctl), 0, 0x10000000);

    if (is_mpcore) {
        hwaddr periphbase;
        dev = qdev_create(NULL, is_pb ? "a9mpcore_priv": "realview_mpcore");
        qdev_prop_set_uint32(dev, "num-cpu", smp_cpus);
        qdev_init_nofail(dev);
        busdev = sysbus_from_qdev(dev);
        if (is_pb) {
            periphbase = 0x1f000000;
        } else {
            periphbase = 0x10100000;
        }
        sysbus_mmio_map(busdev, 0, periphbase);
        for (n = 0; n < smp_cpus; n++) {
            sysbus_connect_irq(busdev, n, cpu_irq[n]);
        }
        sysbus_create_varargs("l2x0", periphbase + 0x2000, NULL);
        /* Both A9 and 11MPCore put the GIC CPU i/f at base + 0x100 */
        realview_binfo.gic_cpu_if_addr = periphbase + 0x100;
    } else {
        uint32_t gic_addr = is_pb ? 0x1e000000 : 0x10040000;
        /* For now just create the nIRQ GIC, and ignore the others.  */
        dev = sysbus_create_simple("realview_gic", gic_addr, cpu_irq[0]);
    }
    for (n = 0; n < 64; n++) {
        pic[n] = qdev_get_gpio_in(dev, n);
    }

    pl041 = qdev_create(NULL, "pl041");
    qdev_prop_set_uint32(pl041, "nc_fifo_depth", 512);
    qdev_init_nofail(pl041);
    sysbus_mmio_map(sysbus_from_qdev(pl041), 0, 0x10004000);
    sysbus_connect_irq(sysbus_from_qdev(pl041), 0, pic[19]);

    sysbus_create_simple("pl050_keyboard", 0x10006000, pic[20]);
    sysbus_create_simple("pl050_mouse", 0x10007000, pic[21]);

    sysbus_create_simple("pl011", 0x10009000, pic[12]);
    sysbus_create_simple("pl011", 0x1000a000, pic[13]);
    sysbus_create_simple("pl011", 0x1000b000, pic[14]);
    sysbus_create_simple("pl011", 0x1000c000, pic[15]);

    /* DMA controller is optional, apparently.  */
    sysbus_create_simple("pl081", 0x10030000, pic[24]);

    sysbus_create_simple("sp804", 0x10011000, pic[4]);
    sysbus_create_simple("sp804", 0x10012000, pic[5]);

    sysbus_create_simple("pl061", 0x10013000, pic[6]);
    sysbus_create_simple("pl061", 0x10014000, pic[7]);
    gpio2 = sysbus_create_simple("pl061", 0x10015000, pic[8]);

    sysbus_create_simple("pl111", 0x10020000, pic[23]);

    dev = sysbus_create_varargs("pl181", 0x10005000, pic[17], pic[18], NULL);
    /* Wire up MMC card detect and read-only signals. These have
     * to go to both the PL061 GPIO and the sysctl register.
     * Note that the PL181 orders these lines (readonly,inserted)
     * and the PL061 has them the other way about. Also the card
     * detect line is inverted.
     */
    mmc_irq[0] = qemu_irq_split(
        qdev_get_gpio_in(sysctl, ARM_SYSCTL_GPIO_MMC_WPROT),
        qdev_get_gpio_in(gpio2, 1));
    mmc_irq[1] = qemu_irq_split(
        qdev_get_gpio_in(sysctl, ARM_SYSCTL_GPIO_MMC_CARDIN),
        qemu_irq_invert(qdev_get_gpio_in(gpio2, 0)));
    qdev_connect_gpio_out(dev, 0, mmc_irq[0]);
    qdev_connect_gpio_out(dev, 1, mmc_irq[1]);

    sysbus_create_simple("pl031", 0x10017000, pic[10]);

    if (!is_pb) {
        dev = qdev_create(NULL, "realview_pci");
        busdev = sysbus_from_qdev(dev);
        qdev_init_nofail(dev);
        sysbus_mmio_map(busdev, 0, 0x61000000); /* PCI self-config */
        sysbus_mmio_map(busdev, 1, 0x62000000); /* PCI config */
        sysbus_mmio_map(busdev, 2, 0x63000000); /* PCI I/O */
        sysbus_connect_irq(busdev, 0, pic[48]);
        sysbus_connect_irq(busdev, 1, pic[49]);
        sysbus_connect_irq(busdev, 2, pic[50]);
        sysbus_connect_irq(busdev, 3, pic[51]);
        pci_bus = (PCIBus *)qdev_get_child_bus(dev, "pci");
        if (usb_enabled(false)) {
            pci_create_simple(pci_bus, -1, "pci-ohci");
        }
        n = drive_get_max_bus(IF_SCSI);
        while (n >= 0) {
            pci_create_simple(pci_bus, -1, "lsi53c895a");
            n--;
        }
    }
    for(n = 0; n < nb_nics; n++) {
        nd = &nd_table[n];

        if (!done_nic && (!nd->model ||
                    strcmp(nd->model, is_pb ? "lan9118" : "smc91c111") == 0)) {
            if (is_pb) {
                lan9118_init(nd, 0x4e000000, pic[28]);
            } else {
                smc91c111_init(nd, 0x4e000000, pic[28]);
            }
            done_nic = 1;
        } else {
            pci_nic_init_nofail(nd, "rtl8139", NULL);
        }
    }

    dev = sysbus_create_simple("versatile_i2c", 0x10002000, NULL);
    i2c = (i2c_bus *)qdev_get_child_bus(dev, "i2c");
    i2c_create_slave(i2c, "ds1338", 0x68);

    /* Memory map for RealView Emulation Baseboard:  */
    /* 0x10000000 System registers.  */
    /*  0x10001000 System controller.  */
    /* 0x10002000 Two-Wire Serial Bus.  */
    /* 0x10003000 Reserved.  */
    /*  0x10004000 AACI.  */
    /*  0x10005000 MCI.  */
    /* 0x10006000 KMI0.  */
    /* 0x10007000 KMI1.  */
    /*  0x10008000 Character LCD. (EB) */
    /* 0x10009000 UART0.  */
    /* 0x1000a000 UART1.  */
    /* 0x1000b000 UART2.  */
    /* 0x1000c000 UART3.  */
    /*  0x1000d000 SSPI.  */
    /*  0x1000e000 SCI.  */
    /* 0x1000f000 Reserved.  */
    /*  0x10010000 Watchdog.  */
    /* 0x10011000 Timer 0+1.  */
    /* 0x10012000 Timer 2+3.  */
    /*  0x10013000 GPIO 0.  */
    /*  0x10014000 GPIO 1.  */
    /*  0x10015000 GPIO 2.  */
    /*  0x10002000 Two-Wire Serial Bus - DVI. (PB) */
    /* 0x10017000 RTC.  */
    /*  0x10018000 DMC.  */
    /*  0x10019000 PCI controller config.  */
    /*  0x10020000 CLCD.  */
    /* 0x10030000 DMA Controller.  */
    /* 0x10040000 GIC1. (EB) */
    /*  0x10050000 GIC2. (EB) */
    /*  0x10060000 GIC3. (EB) */
    /*  0x10070000 GIC4. (EB) */
    /*  0x10080000 SMC.  */
    /* 0x1e000000 GIC1. (PB) */
    /*  0x1e001000 GIC2. (PB) */
    /*  0x1e002000 GIC3. (PB) */
    /*  0x1e003000 GIC4. (PB) */
    /*  0x40000000 NOR flash.  */
    /*  0x44000000 DoC flash.  */
    /*  0x48000000 SRAM.  */
    /*  0x4c000000 Configuration flash.  */
    /* 0x4e000000 Ethernet.  */
    /*  0x4f000000 USB.  */
    /*  0x50000000 PISMO.  */
    /*  0x54000000 PISMO.  */
    /*  0x58000000 PISMO.  */
    /*  0x5c000000 PISMO.  */
    /* 0x60000000 PCI.  */
    /* 0x61000000 PCI Self Config.  */
    /* 0x62000000 PCI Config.  */
    /* 0x63000000 PCI IO.  */
    /* 0x64000000 PCI mem 0.  */
    /* 0x68000000 PCI mem 1.  */
    /* 0x6c000000 PCI mem 2.  */

    /* ??? Hack to map an additional page of ram for the secondary CPU
       startup code.  I guess this works on real hardware because the
       BootROM happens to be in ROM/flash or in memory that isn't clobbered
       until after Linux boots the secondary CPUs.  */
    memory_region_init_ram(ram_hack, "realview.hack", 0x1000);
    vmstate_register_ram_global(ram_hack);
    memory_region_add_subregion(sysmem, SMP_BOOT_ADDR, ram_hack);

    realview_binfo.ram_size = ram_size;
    realview_binfo.kernel_filename = args->kernel_filename;
    realview_binfo.kernel_cmdline = args->kernel_cmdline;
    realview_binfo.initrd_filename = args->initrd_filename;
    realview_binfo.nb_cpus = smp_cpus;
    realview_binfo.board_id = realview_board_id[board_type];
    realview_binfo.loader_start = (board_type == BOARD_PB_A8 ? 0x70000000 : 0);
    arm_load_kernel(arm_env_get_cpu(first_cpu), &realview_binfo);
}

static void realview_eb_init(QEMUMachineInitArgs *args)
{
    if (!args->cpu_model) {
        args->cpu_model = "arm926";
    }
    realview_init(args, BOARD_EB);
}

static void realview_eb_mpcore_init(QEMUMachineInitArgs *args)
{
    if (!args->cpu_model) {
        args->cpu_model = "arm11mpcore";
    }
    realview_init(args, BOARD_EB_MPCORE);
}

static void realview_pb_a8_init(QEMUMachineInitArgs *args)
{
    if (!args->cpu_model) {
        args->cpu_model = "cortex-a8";
    }
    realview_init(args, BOARD_PB_A8);
}

static void realview_pbx_a9_init(QEMUMachineInitArgs *args)
{
    if (!args->cpu_model) {
        args->cpu_model = "cortex-a9";
    }
    realview_init(args, BOARD_PBX_A9);
}

static QEMUMachine realview_eb_machine = {
    .name = "realview-eb",
    .desc = "ARM RealView Emulation Baseboard (ARM926EJ-S)",
    .init = realview_eb_init,
    .use_scsi = 1,
};

static QEMUMachine realview_eb_mpcore_machine = {
    .name = "realview-eb-mpcore",
    .desc = "ARM RealView Emulation Baseboard (ARM11MPCore)",
    .init = realview_eb_mpcore_init,
    .use_scsi = 1,
    .max_cpus = 4,
};

static QEMUMachine realview_pb_a8_machine = {
    .name = "realview-pb-a8",
    .desc = "ARM RealView Platform Baseboard for Cortex-A8",
    .init = realview_pb_a8_init,
};

static QEMUMachine realview_pbx_a9_machine = {
    .name = "realview-pbx-a9",
    .desc = "ARM RealView Platform Baseboard Explore for Cortex-A9",
    .init = realview_pbx_a9_init,
    .use_scsi = 1,
    .max_cpus = 4,
};

static void realview_machine_init(void)
{
    qemu_register_machine(&realview_eb_machine);
    qemu_register_machine(&realview_eb_mpcore_machine);
    qemu_register_machine(&realview_pb_a8_machine);
    qemu_register_machine(&realview_pbx_a9_machine);
}

machine_init(realview_machine_init);
