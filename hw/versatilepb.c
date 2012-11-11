/*
 * ARM Versatile Platform/Application Baseboard System emulation.
 *
 * Copyright (c) 2005-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "sysbus.h"
#include "arm-misc.h"
#include "devices.h"
#include "net.h"
#include "sysemu.h"
#include "pci.h"
#include "i2c.h"
#include "boards.h"
#include "blockdev.h"
#include "exec-memory.h"
#include "flash.h"

#define VERSATILE_FLASH_ADDR 0x34000000
#define VERSATILE_FLASH_SIZE (64 * 1024 * 1024)
#define VERSATILE_FLASH_SECT_SIZE (256 * 1024)

/* Primary interrupt controller.  */

typedef struct vpb_sic_state
{
  SysBusDevice busdev;
  MemoryRegion iomem;
  uint32_t level;
  uint32_t mask;
  uint32_t pic_enable;
  qemu_irq parent[32];
  int irq;
} vpb_sic_state;

static const VMStateDescription vmstate_vpb_sic = {
    .name = "versatilepb_sic",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(level, vpb_sic_state),
        VMSTATE_UINT32(mask, vpb_sic_state),
        VMSTATE_UINT32(pic_enable, vpb_sic_state),
        VMSTATE_END_OF_LIST()
    }
};

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

static uint64_t vpb_sic_read(void *opaque, hwaddr offset,
                             unsigned size)
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

static void vpb_sic_write(void *opaque, hwaddr offset,
                          uint64_t value, unsigned size)
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

static const MemoryRegionOps vpb_sic_ops = {
    .read = vpb_sic_read,
    .write = vpb_sic_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int vpb_sic_init(SysBusDevice *dev)
{
    vpb_sic_state *s = FROM_SYSBUS(vpb_sic_state, dev);
    int i;

    qdev_init_gpio_in(&dev->qdev, vpb_sic_set_irq, 32);
    for (i = 0; i < 32; i++) {
        sysbus_init_irq(dev, &s->parent[i]);
    }
    s->irq = 31;
    memory_region_init_io(&s->iomem, &vpb_sic_ops, s, "vpb-sic", 0x1000);
    sysbus_init_mmio(dev, &s->iomem);
    return 0;
}

/* Board init.  */

/* The AB and PB boards both use the same core, just with different
   peripherals and expansion busses.  For now we emulate a subset of the
   PB peripherals and just change the board ID.  */

static struct arm_boot_info versatile_binfo;

static void versatile_init(QEMUMachineInitArgs *args, int board_id)
{
    ARMCPU *cpu;
    MemoryRegion *sysmem = get_system_memory();
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    qemu_irq *cpu_pic;
    qemu_irq pic[32];
    qemu_irq sic[32];
    DeviceState *dev, *sysctl;
    SysBusDevice *busdev;
    DeviceState *pl041;
    PCIBus *pci_bus;
    NICInfo *nd;
    i2c_bus *i2c;
    int n;
    int done_smc = 0;
    DriveInfo *dinfo;

    if (!args->cpu_model) {
        args->cpu_model = "arm926";
    }
    cpu = cpu_arm_init(args->cpu_model);
    if (!cpu) {
        fprintf(stderr, "Unable to find CPU definition\n");
        exit(1);
    }
    memory_region_init_ram(ram, "versatile.ram", args->ram_size);
    vmstate_register_ram_global(ram);
    /* ??? RAM should repeat to fill physical memory space.  */
    /* SDRAM at address zero.  */
    memory_region_add_subregion(sysmem, 0, ram);

    sysctl = qdev_create(NULL, "realview_sysctl");
    qdev_prop_set_uint32(sysctl, "sys_id", 0x41007004);
    qdev_prop_set_uint32(sysctl, "proc_id", 0x02000000);
    qdev_init_nofail(sysctl);
    sysbus_mmio_map(sysbus_from_qdev(sysctl), 0, 0x10000000);

    cpu_pic = arm_pic_init_cpu(cpu);
    dev = sysbus_create_varargs("pl190", 0x10140000,
                                cpu_pic[ARM_PIC_CPU_IRQ],
                                cpu_pic[ARM_PIC_CPU_FIQ], NULL);
    for (n = 0; n < 32; n++) {
        pic[n] = qdev_get_gpio_in(dev, n);
    }
    dev = sysbus_create_simple("versatilepb_sic", 0x10003000, NULL);
    for (n = 0; n < 32; n++) {
        sysbus_connect_irq(sysbus_from_qdev(dev), n, pic[n]);
        sic[n] = qdev_get_gpio_in(dev, n);
    }

    sysbus_create_simple("pl050_keyboard", 0x10006000, sic[3]);
    sysbus_create_simple("pl050_mouse", 0x10007000, sic[4]);

    dev = qdev_create(NULL, "versatile_pci");
    busdev = sysbus_from_qdev(dev);
    qdev_init_nofail(dev);
    sysbus_mmio_map(busdev, 0, 0x41000000); /* PCI self-config */
    sysbus_mmio_map(busdev, 1, 0x42000000); /* PCI config */
    sysbus_connect_irq(busdev, 0, sic[27]);
    sysbus_connect_irq(busdev, 1, sic[28]);
    sysbus_connect_irq(busdev, 2, sic[29]);
    sysbus_connect_irq(busdev, 3, sic[30]);
    pci_bus = (PCIBus *)qdev_get_child_bus(dev, "pci");

    /* The Versatile PCI bridge does not provide access to PCI IO space,
       so many of the qemu PCI devices are not useable.  */
    for(n = 0; n < nb_nics; n++) {
        nd = &nd_table[n];

        if (!done_smc && (!nd->model || strcmp(nd->model, "smc91c111") == 0)) {
            smc91c111_init(nd, 0x10010000, sic[25]);
            done_smc = 1;
        } else {
            pci_nic_init_nofail(nd, "rtl8139", NULL);
        }
    }
    if (usb_enabled(false)) {
        pci_create_simple(pci_bus, -1, "pci-ohci");
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

    sysbus_create_simple("pl080", 0x10130000, pic[17]);
    sysbus_create_simple("sp804", 0x101e2000, pic[4]);
    sysbus_create_simple("sp804", 0x101e3000, pic[5]);

    sysbus_create_simple("pl061", 0x101e4000, pic[6]);
    sysbus_create_simple("pl061", 0x101e5000, pic[7]);
    sysbus_create_simple("pl061", 0x101e6000, pic[8]);
    sysbus_create_simple("pl061", 0x101e7000, pic[9]);

    /* The versatile/PB actually has a modified Color LCD controller
       that includes hardware cursor support from the PL111.  */
    dev = sysbus_create_simple("pl110_versatile", 0x10120000, pic[16]);
    /* Wire up the mux control signals from the SYS_CLCD register */
    qdev_connect_gpio_out(sysctl, 0, qdev_get_gpio_in(dev, 0));

    sysbus_create_varargs("pl181", 0x10005000, sic[22], sic[1], NULL);
    sysbus_create_varargs("pl181", 0x1000b000, sic[23], sic[2], NULL);

    /* Add PL031 Real Time Clock. */
    sysbus_create_simple("pl031", 0x101e8000, pic[10]);

    dev = sysbus_create_simple("versatile_i2c", 0x10002000, NULL);
    i2c = (i2c_bus *)qdev_get_child_bus(dev, "i2c");
    i2c_create_slave(i2c, "ds1338", 0x68);

    /* Add PL041 AACI Interface to the LM4549 codec */
    pl041 = qdev_create(NULL, "pl041");
    qdev_prop_set_uint32(pl041, "nc_fifo_depth", 512);
    qdev_init_nofail(pl041);
    sysbus_mmio_map(sysbus_from_qdev(pl041), 0, 0x10004000);
    sysbus_connect_irq(sysbus_from_qdev(pl041), 0, sic[24]);

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
    /* 0x34000000 NOR Flash */

    dinfo = drive_get(IF_PFLASH, 0, 0);
    if (!pflash_cfi01_register(VERSATILE_FLASH_ADDR, NULL, "versatile.flash",
                          VERSATILE_FLASH_SIZE, dinfo ? dinfo->bdrv : NULL,
                          VERSATILE_FLASH_SECT_SIZE,
                          VERSATILE_FLASH_SIZE / VERSATILE_FLASH_SECT_SIZE,
                          4, 0x0089, 0x0018, 0x0000, 0x0, 0)) {
        fprintf(stderr, "qemu: Error registering flash memory.\n");
    }

    versatile_binfo.ram_size = args->ram_size;
    versatile_binfo.kernel_filename = args->kernel_filename;
    versatile_binfo.kernel_cmdline = args->kernel_cmdline;
    versatile_binfo.initrd_filename = args->initrd_filename;
    versatile_binfo.board_id = board_id;
    arm_load_kernel(cpu, &versatile_binfo);
}

static void vpb_init(QEMUMachineInitArgs *args)
{
    versatile_init(args, 0x183);
}

static void vab_init(QEMUMachineInitArgs *args)
{
    versatile_init(args, 0x25e);
}

static QEMUMachine versatilepb_machine = {
    .name = "versatilepb",
    .desc = "ARM Versatile/PB (ARM926EJ-S)",
    .init = vpb_init,
    .use_scsi = 1,
};

static QEMUMachine versatileab_machine = {
    .name = "versatileab",
    .desc = "ARM Versatile/AB (ARM926EJ-S)",
    .init = vab_init,
    .use_scsi = 1,
};

static void versatile_machine_init(void)
{
    qemu_register_machine(&versatilepb_machine);
    qemu_register_machine(&versatileab_machine);
}

machine_init(versatile_machine_init);

static void vpb_sic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = vpb_sic_init;
    dc->no_user = 1;
    dc->vmsd = &vmstate_vpb_sic;
}

static TypeInfo vpb_sic_info = {
    .name          = "versatilepb_sic",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(vpb_sic_state),
    .class_init    = vpb_sic_class_init,
};

static void versatilepb_register_types(void)
{
    type_register_static(&vpb_sic_info);
}

type_init(versatilepb_register_types)
