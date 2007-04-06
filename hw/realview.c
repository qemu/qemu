/* 
 * ARM RealView Baseboard System emulation.
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licenced under the GPL.
 */

#include "vl.h"
#include "arm_pic.h"

/* Board init.  */

static void realview_init(int ram_size, int vga_ram_size, int boot_device,
                     DisplayState *ds, const char **fd_filename, int snapshot,
                     const char *kernel_filename, const char *kernel_cmdline,
                     const char *initrd_filename, const char *cpu_model)
{
    CPUState *env;
    void *pic;
    void *scsi_hba;
    PCIBus *pci_bus;
    NICInfo *nd;
    int n;
    int done_smc = 0;

    env = cpu_init();
    if (!cpu_model)
        cpu_model = "arm926";
    cpu_arm_set_model(env, cpu_model);
    /* ??? RAM shoud repeat to fill physical memory space.  */
    /* SDRAM at address zero.  */
    cpu_register_physical_memory(0, ram_size, IO_MEM_RAM);

    arm_sysctl_init(0x10000000, 0xc1400400);
    pic = arm_pic_init_cpu(env);
    /* ??? The documentation says GIC1 is nFIQ and either GIC2 or GIC3
       is nIRQ (there are inconsistencies).  However Linux 2.6.17 expects
       GIC1 to be nIRQ and ignores all the others, so do that for now.  */
    pic = arm_gic_init(0x10040000, pic, ARM_PIC_CPU_IRQ);
    pl050_init(0x10006000, pic, 20, 0);
    pl050_init(0x10007000, pic, 21, 1);

    pl011_init(0x10009000, pic, 12, serial_hds[0]);
    pl011_init(0x1000a000, pic, 13, serial_hds[1]);
    pl011_init(0x1000b000, pic, 14, serial_hds[2]);
    pl011_init(0x1000c000, pic, 15, serial_hds[3]);

    /* DMA controller is optional, apparently.  */
    pl080_init(0x10030000, pic, 24, 2);

    sp804_init(0x10011000, pic, 4);
    sp804_init(0x10012000, pic, 5);

    pl110_init(ds, 0x10020000, pic, 23, 1);

    pl181_init(0x10005000, sd_bdrv, pic, 17, 18);

    pci_bus = pci_vpb_init(pic, 48, 1);
    if (usb_enabled) {
        usb_ohci_init_pci(pci_bus, 3, -1);
    }
    scsi_hba = lsi_scsi_init(pci_bus, -1);
    for (n = 0; n < MAX_DISKS; n++) {
        if (bs_table[n]) {
            lsi_scsi_attach(scsi_hba, bs_table[n], n);
        }
    }
    for(n = 0; n < nb_nics; n++) {
        nd = &nd_table[n];
        if (!nd->model)
            nd->model = done_smc ? "rtl8139" : "smc91c111";
        if (strcmp(nd->model, "smc91c111") == 0) {
            smc91c111_init(nd, 0x4e000000, pic, 28);
        } else {
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
    /*  0x10017000 RTC.  */
    /*  0x10018000 DMC.  */
    /*  0x10019000 PCI controller config.  */
    /*  0x10020000 CLCD.  */
    /* 0x10030000 DMA Controller.  */
    /* 0x10040000 GIC1 (FIQ1).  */
    /* 0x10050000 GIC2 (IRQ1).  */
    /*  0x10060000 GIC3 (FIQ2).  */
    /*  0x10070000 GIC4 (IRQ2).  */
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

    arm_load_kernel(env, ram_size, kernel_filename, kernel_cmdline,
                    initrd_filename, 0x33b);
}

QEMUMachine realview_machine = {
    "realview",
    "ARM RealView Emulation Baseboard (ARM926EJ-S)",
    realview_init
};
