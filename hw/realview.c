/*
 * ARM RealView Baseboard System emulation.
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licenced under the GPL.
 */

#include "hw.h"
#include "arm-misc.h"
#include "primecell.h"
#include "devices.h"
#include "pci.h"
#include "net.h"
#include "sysemu.h"
#include "boards.h"

/* Board init.  */

static struct arm_boot_info realview_binfo = {
    .loader_start = 0x0,
    .board_id = 0x33b,
};

static void realview_init(ram_addr_t ram_size, int vga_ram_size,
                     const char *boot_device, DisplayState *ds,
                     const char *kernel_filename, const char *kernel_cmdline,
                     const char *initrd_filename, const char *cpu_model)
{
    CPUState *env;
    qemu_irq *pic;
    void *scsi_hba;
    PCIBus *pci_bus;
    NICInfo *nd;
    int n;
    int done_smc = 0;
    qemu_irq cpu_irq[4];
    int ncpu;
    int index;

    if (!cpu_model)
        cpu_model = "arm926";
    /* FIXME: obey smp_cpus.  */
    if (strcmp(cpu_model, "arm11mpcore") == 0) {
        ncpu = 4;
    } else {
        ncpu = 1;
    }

    for (n = 0; n < ncpu; n++) {
        env = cpu_init(cpu_model);
        if (!env) {
            fprintf(stderr, "Unable to find CPU definition\n");
            exit(1);
        }
        pic = arm_pic_init_cpu(env);
        cpu_irq[n] = pic[ARM_PIC_CPU_IRQ];
        if (n > 0) {
            /* Set entry point for secondary CPUs.  This assumes we're using
               the init code from arm_boot.c.  Real hardware resets all CPUs
               the same.  */
            env->regs[15] = 0x80000000;
        }
    }

    /* ??? RAM should repeat to fill physical memory space.  */
    /* SDRAM at address zero.  */
    cpu_register_physical_memory(0, ram_size, IO_MEM_RAM);

    arm_sysctl_init(0x10000000, 0xc1400400);

    if (ncpu == 1) {
        /* ??? The documentation says GIC1 is nFIQ and either GIC2 or GIC3
           is nIRQ (there are inconsistencies).  However Linux 2.6.17 expects
           GIC1 to be nIRQ and ignores all the others, so do that for now.  */
        pic = realview_gic_init(0x10040000, cpu_irq[0]);
    } else {
        pic = mpcore_irq_init(cpu_irq);
    }

    pl050_init(0x10006000, pic[20], 0);
    pl050_init(0x10007000, pic[21], 1);

    pl011_init(0x10009000, pic[12], serial_hds[0], PL011_ARM);
    pl011_init(0x1000a000, pic[13], serial_hds[1], PL011_ARM);
    pl011_init(0x1000b000, pic[14], serial_hds[2], PL011_ARM);
    pl011_init(0x1000c000, pic[15], serial_hds[3], PL011_ARM);

    /* DMA controller is optional, apparently.  */
    pl080_init(0x10030000, pic[24], 2);

    sp804_init(0x10011000, pic[4]);
    sp804_init(0x10012000, pic[5]);

    pl110_init(ds, 0x10020000, pic[23], 1);

    index = drive_get_index(IF_SD, 0, 0);
    if (index == -1) {
        fprintf(stderr, "qemu: missing SecureDigital card\n");
        exit(1);
    }
    pl181_init(0x10005000, drives_table[index].bdrv, pic[17], pic[18]);

    pl031_init(0x10017000, pic[10]);

    pci_bus = pci_vpb_init(pic, 48, 1);
    if (usb_enabled) {
        usb_ohci_init_pci(pci_bus, 3, -1);
    }
    if (drive_get_max_bus(IF_SCSI) > 0) {
        fprintf(stderr, "qemu: too many SCSI bus\n");
        exit(1);
    }
    scsi_hba = lsi_scsi_init(pci_bus, -1);
    for (n = 0; n < LSI_MAX_DEVS; n++) {
        index = drive_get_index(IF_SCSI, 0, n);
        if (index == -1)
            continue;
        lsi_scsi_attach(scsi_hba, drives_table[index].bdrv, n);
    }
    for(n = 0; n < nb_nics; n++) {
        nd = &nd_table[n];

        if ((!nd->model && !done_smc) || strcmp(nd->model, "smc91c111") == 0) {
            smc91c111_init(nd, 0x4e000000, pic[28]);
            done_smc = 1;
        } else {
            if (!nd->model)
                nd->model = "rtl8139";
            pci_nic_init(pci_bus, nd, -1);
        }
    }

    /* Memory map for RealView Emulation Baseboard:  */
    /* 0x10000000 System registers.  */
    /*  0x10001000 System controller.  */
    /*  0x10002000 Two-Wire Serial Bus.  */
    /* 0x10003000 Reserved.  */
    /*  0x10004000 AACI.  */
    /*  0x10005000 MCI.  */
    /* 0x10006000 KMI0.  */
    /* 0x10007000 KMI1.  */
    /*  0x10008000 Character LCD.  */
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
    /* 0x10016000 Reserved.  */
    /* 0x10017000 RTC.  */
    /*  0x10018000 DMC.  */
    /*  0x10019000 PCI controller config.  */
    /*  0x10020000 CLCD.  */
    /* 0x10030000 DMA Controller.  */
    /* 0x10040000 GIC1.  */
    /* 0x10050000 GIC2.  */
    /* 0x10060000 GIC3.  */
    /* 0x10070000 GIC4.  */
    /*  0x10080000 SMC.  */
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

    realview_binfo.ram_size = ram_size;
    realview_binfo.kernel_filename = kernel_filename;
    realview_binfo.kernel_cmdline = kernel_cmdline;
    realview_binfo.initrd_filename = initrd_filename;
    realview_binfo.nb_cpus = ncpu;
    arm_load_kernel(first_cpu, &realview_binfo);

    /* ??? Hack to map an additional page of ram for the secondary CPU
       startup code.  I guess this works on real hardware because the
       BootROM happens to be in ROM/flash or in memory that isn't clobbered
       until after Linux boots the secondary CPUs.  */
    cpu_register_physical_memory(0x80000000, 0x1000, IO_MEM_RAM + ram_size);
}

QEMUMachine realview_machine = {
    .name = "realview",
    .desc = "ARM RealView Emulation Baseboard (ARM926EJ-S)",
    .init = realview_init,
    .ram_require = 0x1000,
    .use_scsi = 1,
};
