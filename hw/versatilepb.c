/*
 * ARM Versatile Platform/Application Baseboard System emulation.
 *
 * Copyright (c) 2005-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licenced under the GPL.
 */

#include "sysbus.h"
#include "arm-misc.h"
#include "primecell.h"
#include "devices.h"
#include "net.h"
#include "sysemu.h"
#include "pci.h"
#include "boards.h"

/* Primary interrupt controller.  */

typedef struct vpb_sic_state
{
  uint32_t level;
  uint32_t mask;
  uint32_t pic_enable;
  qemu_irq *parent;
  int irq;
} vpb_sic_state;

static void vpb_sic_update(vpb_sic_state *s)
{
    uint32_t flags;

    flags = s->level & s->mask;
    qemu_set_irq(s->parent[s->irq], flags != 0);
}

static void vpb_sic_update_pic(vpb_sic_state *s)
{
    int i;
    uint32_t mask;

    for (i = 21; i <= 30; i++) {
        mask = 1u << i;
        if (!(s->pic_enable & mask))
            continue;
        qemu_set_irq(s->parent[i], (s->level & mask) != 0);
    }
}

static void vpb_sic_set_irq(void *opaque, int irq, int level)
{
    vpb_sic_state *s = (vpb_sic_state *)opaque;
    if (level)
        s->level |= 1u << irq;
    else
        s->level &= ~(1u << irq);
    if (s->pic_enable & (1u << irq))
        qemu_set_irq(s->parent[irq], level);
    vpb_sic_update(s);
}

static uint32_t vpb_sic_read(void *opaque, target_phys_addr_t offset)
{
    vpb_sic_state *s = (vpb_sic_state *)opaque;

    switch (offset >> 2) {
    case 0: /* STATUS */
        return s->level & s->mask;
    case 1: /* RAWSTAT */
        return s->level;
    case 2: /* ENABLE */
        return s->mask;
    case 4: /* SOFTINT */
        return s->level & 1;
    case 8: /* PICENABLE */
        return s->pic_enable;
    default:
        printf ("vpb_sic_read: Bad register offset 0x%x\n", (int)offset);
        return 0;
    }
}

static void vpb_sic_write(void *opaque, target_phys_addr_t offset,
                          uint32_t value)
{
    vpb_sic_state *s = (vpb_sic_state *)opaque;

    switch (offset >> 2) {
    case 2: /* ENSET */
        s->mask |= value;
        break;
    case 3: /* ENCLR */
        s->mask &= ~value;
        break;
    case 4: /* SOFTINTSET */
        if (value)
            s->mask |= 1;
        break;
    case 5: /* SOFTINTCLR */
        if (value)
            s->mask &= ~1u;
        break;
    case 8: /* PICENSET */
        s->pic_enable |= (value & 0x7fe00000);
        vpb_sic_update_pic(s);
        break;
    case 9: /* PICENCLR */
        s->pic_enable &= ~value;
        vpb_sic_update_pic(s);
        break;
    default:
        printf ("vpb_sic_write: Bad register offset 0x%x\n", (int)offset);
        return;
    }
    vpb_sic_update(s);
}

static CPUReadMemoryFunc *vpb_sic_readfn[] = {
   vpb_sic_read,
   vpb_sic_read,
   vpb_sic_read
};

static CPUWriteMemoryFunc *vpb_sic_writefn[] = {
   vpb_sic_write,
   vpb_sic_write,
   vpb_sic_write
};

static qemu_irq *vpb_sic_init(uint32_t base, qemu_irq *parent, int irq)
{
    vpb_sic_state *s;
    qemu_irq *qi;
    int iomemtype;

    s = (vpb_sic_state *)qemu_mallocz(sizeof(vpb_sic_state));
    qi = qemu_allocate_irqs(vpb_sic_set_irq, s, 32);
    s->parent = parent;
    s->irq = irq;
    iomemtype = cpu_register_io_memory(0, vpb_sic_readfn,
                                       vpb_sic_writefn, s);
    cpu_register_physical_memory(base, 0x00001000, iomemtype);
    /* ??? Save/restore.  */
    return qi;
}

/* Board init.  */

/* The AB and PB boards both use the same core, just with different
   peripherans and expansion busses.  For now we emulate a subset of the
   PB peripherals and just change the board ID.  */

static struct arm_boot_info versatile_binfo;

static void versatile_init(ram_addr_t ram_size,
                     const char *boot_device,
                     const char *kernel_filename, const char *kernel_cmdline,
                     const char *initrd_filename, const char *cpu_model,
                     int board_id)
{
    CPUState *env;
    ram_addr_t ram_offset;
    qemu_irq *pic;
    qemu_irq *sic;
    PCIBus *pci_bus;
    NICInfo *nd;
    int n;
    int done_smc = 0;

    if (!cpu_model)
        cpu_model = "arm926";
    env = cpu_init(cpu_model);
    if (!env) {
        fprintf(stderr, "Unable to find CPU definition\n");
        exit(1);
    }
    ram_offset = qemu_ram_alloc(ram_size);
    /* ??? RAM should repeat to fill physical memory space.  */
    /* SDRAM at address zero.  */
    cpu_register_physical_memory(0, ram_size, ram_offset | IO_MEM_RAM);

    arm_sysctl_init(0x10000000, 0x41007004);
    pic = arm_pic_init_cpu(env);
    pic = pl190_init(0x10140000, pic[0], pic[1]);
    sic = vpb_sic_init(0x10003000, pic, 31);

    sysbus_create_simple("pl050_keyboard", 0x10006000, sic[3]);
    sysbus_create_simple("pl050_mouse", 0x10007000, sic[4]);

    pci_bus = pci_vpb_init(sic, 27, 0);
    /* The Versatile PCI bridge does not provide access to PCI IO space,
       so many of the qemu PCI devices are not useable.  */
    for(n = 0; n < nb_nics; n++) {
        nd = &nd_table[n];

        if ((!nd->model && !done_smc) || strcmp(nd->model, "smc91c111") == 0) {
            smc91c111_init(nd, 0x10010000, sic[25]);
            done_smc = 1;
        } else {
            pci_nic_init(pci_bus, nd, -1, "rtl8139");
        }
    }
    if (usb_enabled) {
        usb_ohci_init_pci(pci_bus, 3, -1);
    }
    n = drive_get_max_bus(IF_SCSI);
    while (n >= 0) {
        pci_create_simple(pci_bus, -1, "lsi53c895a");
        n--;
    }

    sysbus_create_simple("pl011", 0x101f1000, pic[12]);
    sysbus_create_simple("pl011", 0x101f2000, pic[13]);
    sysbus_create_simple("pl011", 0x101f3000, pic[14]);
    sysbus_create_simple("pl011", 0x10009000, sic[6]);

    pl080_init(0x10130000, pic[17], 8);
    sysbus_create_simple("sp804", 0x101e2000, pic[4]);
    sysbus_create_simple("sp804", 0x101e3000, pic[5]);

    /* The versatile/PB actually has a modified Color LCD controller
       that includes hardware cursor support from the PL111.  */
    sysbus_create_simple("pl110_versatile", 0x10120000, pic[16]);

    sysbus_create_varargs("pl181", 0x10005000, sic[22], sic[1], NULL);
    sysbus_create_varargs("pl181", 0x1000b000, sic[23], sic[2], NULL);

    /* Add PL031 Real Time Clock. */
    sysbus_create_simple("pl031", 0x101e8000, pic[10]);

    /* Memory map for Versatile/PB:  */
    /* 0x10000000 System registers.  */
    /* 0x10001000 PCI controller config registers.  */
    /* 0x10002000 Serial bus interface.  */
    /*  0x10003000 Secondary interrupt controller.  */
    /* 0x10004000 AACI (audio).  */
    /*  0x10005000 MMCI0.  */
    /*  0x10006000 KMI0 (keyboard).  */
    /*  0x10007000 KMI1 (mouse).  */
    /* 0x10008000 Character LCD Interface.  */
    /*  0x10009000 UART3.  */
    /* 0x1000a000 Smart card 1.  */
    /*  0x1000b000 MMCI1.  */
    /*  0x10010000 Ethernet.  */
    /* 0x10020000 USB.  */
    /* 0x10100000 SSMC.  */
    /* 0x10110000 MPMC.  */
    /*  0x10120000 CLCD Controller.  */
    /*  0x10130000 DMA Controller.  */
    /*  0x10140000 Vectored interrupt controller.  */
    /* 0x101d0000 AHB Monitor Interface.  */
    /* 0x101e0000 System Controller.  */
    /* 0x101e1000 Watchdog Interface.  */
    /* 0x101e2000 Timer 0/1.  */
    /* 0x101e3000 Timer 2/3.  */
    /* 0x101e4000 GPIO port 0.  */
    /* 0x101e5000 GPIO port 1.  */
    /* 0x101e6000 GPIO port 2.  */
    /* 0x101e7000 GPIO port 3.  */
    /* 0x101e8000 RTC.  */
    /* 0x101f0000 Smart card 0.  */
    /*  0x101f1000 UART0.  */
    /*  0x101f2000 UART1.  */
    /*  0x101f3000 UART2.  */
    /* 0x101f4000 SSPI.  */

    versatile_binfo.ram_size = ram_size;
    versatile_binfo.kernel_filename = kernel_filename;
    versatile_binfo.kernel_cmdline = kernel_cmdline;
    versatile_binfo.initrd_filename = initrd_filename;
    versatile_binfo.board_id = board_id;
    arm_load_kernel(env, &versatile_binfo);
}

static void vpb_init(ram_addr_t ram_size,
                     const char *boot_device,
                     const char *kernel_filename, const char *kernel_cmdline,
                     const char *initrd_filename, const char *cpu_model)
{
    versatile_init(ram_size,
                   boot_device,
                   kernel_filename, kernel_cmdline,
                   initrd_filename, cpu_model, 0x183);
}

static void vab_init(ram_addr_t ram_size,
                     const char *boot_device,
                     const char *kernel_filename, const char *kernel_cmdline,
                     const char *initrd_filename, const char *cpu_model)
{
    versatile_init(ram_size,
                   boot_device,
                   kernel_filename, kernel_cmdline,
                   initrd_filename, cpu_model, 0x25e);
}

QEMUMachine versatilepb_machine = {
    .name = "versatilepb",
    .desc = "ARM Versatile/PB (ARM926EJ-S)",
    .init = vpb_init,
    .use_scsi = 1,
};

QEMUMachine versatileab_machine = {
    .name = "versatileab",
    .desc = "ARM Versatile/AB (ARM926EJ-S)",
    .init = vab_init,
    .use_scsi = 1,
};
