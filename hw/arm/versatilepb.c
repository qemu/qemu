/*
 * ARM Versatile Platform/Application Baseboard System emulation.
 *
 * Copyright (c) 2005-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/arm/boot.h"
#include "hw/net/smc91c111.h"
#include "net/net.h"
#include "sysemu/sysemu.h"
#include "hw/pci/pci.h"
#include "hw/i2c/i2c.h"
#include "hw/i2c/arm_sbcon_i2c.h"
#include "hw/irq.h"
#include "hw/boards.h"
#include "exec/address-spaces.h"
#include "hw/block/flash.h"
#include "qemu/error-report.h"
#include "hw/char/pl011.h"
#include "hw/sd/sd.h"
#include "qom/object.h"

#define VERSATILE_FLASH_ADDR 0x34000000
#define VERSATILE_FLASH_SIZE (64 * 1024 * 1024)
#define VERSATILE_FLASH_SECT_SIZE (256 * 1024)

/* Primary interrupt controller.  */

#define TYPE_VERSATILE_PB_SIC "versatilepb_sic"
OBJECT_DECLARE_SIMPLE_TYPE(vpb_sic_state, VERSATILE_PB_SIC)

struct vpb_sic_state {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t level;
    uint32_t mask;
    uint32_t pic_enable;
    qemu_irq parent[32];
    int irq;
};

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

static void vpb_sic_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    vpb_sic_state *s = VERSATILE_PB_SIC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    int i;

    qdev_init_gpio_in(dev, vpb_sic_set_irq, 32);
    for (i = 0; i < 32; i++) {
        sysbus_init_irq(sbd, &s->parent[i]);
    }
    s->irq = 31;
    memory_region_init_io(&s->iomem, obj, &vpb_sic_ops, s,
                          "vpb-sic", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
}

/* Board init.  */

/* The AB and PB boards both use the same core, just with different
   peripherals and expansion busses.  For now we emulate a subset of the
   PB peripherals and just change the board ID.  */

static struct arm_boot_info versatile_binfo;

static void versatile_init(MachineState *machine, int board_id)
{
    Object *cpuobj;
    ARMCPU *cpu;
    MemoryRegion *sysmem = get_system_memory();
    qemu_irq pic[32];
    qemu_irq sic[32];
    DeviceState *dev, *sysctl;
    SysBusDevice *busdev;
    DeviceState *pl041;
    PCIBus *pci_bus;
    NICInfo *nd;
    I2CBus *i2c;
    int n;
    int done_smc = 0;
    DriveInfo *dinfo;

    if (machine->ram_size > 0x10000000) {
        /* Device starting at address 0x10000000,
         * and memory cannot overlap with devices.
         * Refuse to run rather than behaving very confusingly.
         */
        error_report("versatilepb: memory size must not exceed 256MB");
        exit(1);
    }

    cpuobj = object_new(machine->cpu_type);

    /* By default ARM1176 CPUs have EL3 enabled.  This board does not
     * currently support EL3 so the CPU EL3 property is disabled before
     * realization.
     */
    if (object_property_find(cpuobj, "has_el3")) {
        object_property_set_bool(cpuobj, "has_el3", false, &error_fatal);
    }

    qdev_realize(DEVICE(cpuobj), NULL, &error_fatal);

    cpu = ARM_CPU(cpuobj);

    /* ??? RAM should repeat to fill physical memory space.  */
    /* SDRAM at address zero.  */
    memory_region_add_subregion(sysmem, 0, machine->ram);

    sysctl = qdev_new("realview_sysctl");
    qdev_prop_set_uint32(sysctl, "sys_id", 0x41007004);
    qdev_prop_set_uint32(sysctl, "proc_id", 0x02000000);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(sysctl), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(sysctl), 0, 0x10000000);

    dev = sysbus_create_varargs("pl190", 0x10140000,
                                qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_IRQ),
                                qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_FIQ),
                                NULL);
    for (n = 0; n < 32; n++) {
        pic[n] = qdev_get_gpio_in(dev, n);
    }
    dev = sysbus_create_simple(TYPE_VERSATILE_PB_SIC, 0x10003000, NULL);
    for (n = 0; n < 32; n++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), n, pic[n]);
        sic[n] = qdev_get_gpio_in(dev, n);
    }

    sysbus_create_simple("pl050_keyboard", 0x10006000, sic[3]);
    sysbus_create_simple("pl050_mouse", 0x10007000, sic[4]);

    dev = qdev_new("versatile_pci");
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(busdev, &error_fatal);
    sysbus_mmio_map(busdev, 0, 0x10001000); /* PCI controller regs */
    sysbus_mmio_map(busdev, 1, 0x41000000); /* PCI self-config */
    sysbus_mmio_map(busdev, 2, 0x42000000); /* PCI config */
    sysbus_mmio_map(busdev, 3, 0x43000000); /* PCI I/O */
    sysbus_mmio_map(busdev, 4, 0x44000000); /* PCI memory window 1 */
    sysbus_mmio_map(busdev, 5, 0x50000000); /* PCI memory window 2 */
    sysbus_mmio_map(busdev, 6, 0x60000000); /* PCI memory window 3 */
    sysbus_connect_irq(busdev, 0, sic[27]);
    sysbus_connect_irq(busdev, 1, sic[28]);
    sysbus_connect_irq(busdev, 2, sic[29]);
    sysbus_connect_irq(busdev, 3, sic[30]);
    pci_bus = (PCIBus *)qdev_get_child_bus(dev, "pci");

    for(n = 0; n < nb_nics; n++) {
        nd = &nd_table[n];

        if (!done_smc && (!nd->model || strcmp(nd->model, "smc91c111") == 0)) {
            smc91c111_init(nd, 0x10010000, sic[25]);
            done_smc = 1;
        } else {
            pci_nic_init_nofail(nd, pci_bus, "rtl8139", NULL);
        }
    }
    if (machine_usb(machine)) {
        pci_create_simple(pci_bus, -1, "pci-ohci");
    }
    n = drive_get_max_bus(IF_SCSI);
    while (n >= 0) {
        dev = DEVICE(pci_create_simple(pci_bus, -1, "lsi53c895a"));
        lsi53c8xx_handle_legacy_cmdline(dev);
        n--;
    }

    pl011_create(0x101f1000, pic[12], serial_hd(0));
    pl011_create(0x101f2000, pic[13], serial_hd(1));
    pl011_create(0x101f3000, pic[14], serial_hd(2));
    pl011_create(0x10009000, sic[6], serial_hd(3));

    dev = qdev_new("pl080");
    object_property_set_link(OBJECT(dev), "downstream", OBJECT(sysmem),
                             &error_fatal);
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(busdev, &error_fatal);
    sysbus_mmio_map(busdev, 0, 0x10130000);
    sysbus_connect_irq(busdev, 0, pic[17]);

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

    dev = sysbus_create_varargs("pl181", 0x10005000, sic[22], sic[1], NULL);
    dinfo = drive_get_next(IF_SD);
    if (dinfo) {
        DeviceState *card;

        card = qdev_new(TYPE_SD_CARD);
        qdev_prop_set_drive_err(card, "drive", blk_by_legacy_dinfo(dinfo),
                                &error_fatal);
        qdev_realize_and_unref(card, qdev_get_child_bus(dev, "sd-bus"),
                               &error_fatal);
    }

    dev = sysbus_create_varargs("pl181", 0x1000b000, sic[23], sic[2], NULL);
    dinfo = drive_get_next(IF_SD);
    if (dinfo) {
        DeviceState *card;

        card = qdev_new(TYPE_SD_CARD);
        qdev_prop_set_drive_err(card, "drive", blk_by_legacy_dinfo(dinfo),
                                &error_fatal);
        qdev_realize_and_unref(card, qdev_get_child_bus(dev, "sd-bus"),
                               &error_fatal);
    }

    /* Add PL031 Real Time Clock. */
    sysbus_create_simple("pl031", 0x101e8000, pic[10]);

    dev = sysbus_create_simple(TYPE_VERSATILE_I2C, 0x10002000, NULL);
    i2c = (I2CBus *)qdev_get_child_bus(dev, "i2c");
    i2c_slave_create_simple(i2c, "ds1338", 0x68);

    /* Add PL041 AACI Interface to the LM4549 codec */
    pl041 = qdev_new("pl041");
    qdev_prop_set_uint32(pl041, "nc_fifo_depth", 512);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(pl041), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(pl041), 0, 0x10004000);
    sysbus_connect_irq(SYS_BUS_DEVICE(pl041), 0, sic[24]);

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
    if (!pflash_cfi01_register(VERSATILE_FLASH_ADDR, "versatile.flash",
                          VERSATILE_FLASH_SIZE,
                          dinfo ? blk_by_legacy_dinfo(dinfo) : NULL,
                          VERSATILE_FLASH_SECT_SIZE,
                          4, 0x0089, 0x0018, 0x0000, 0x0, 0)) {
        fprintf(stderr, "qemu: Error registering flash memory.\n");
    }

    versatile_binfo.ram_size = machine->ram_size;
    versatile_binfo.board_id = board_id;
    arm_load_kernel(cpu, machine, &versatile_binfo);
}

static void vpb_init(MachineState *machine)
{
    versatile_init(machine, 0x183);
}

static void vab_init(MachineState *machine)
{
    versatile_init(machine, 0x25e);
}

static void versatilepb_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "ARM Versatile/PB (ARM926EJ-S)";
    mc->init = vpb_init;
    mc->block_default_type = IF_SCSI;
    mc->ignore_memory_transaction_failures = true;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("arm926");
    mc->default_ram_id = "versatile.ram";
}

static const TypeInfo versatilepb_type = {
    .name = MACHINE_TYPE_NAME("versatilepb"),
    .parent = TYPE_MACHINE,
    .class_init = versatilepb_class_init,
};

static void versatileab_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "ARM Versatile/AB (ARM926EJ-S)";
    mc->init = vab_init;
    mc->block_default_type = IF_SCSI;
    mc->ignore_memory_transaction_failures = true;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("arm926");
    mc->default_ram_id = "versatile.ram";
}

static const TypeInfo versatileab_type = {
    .name = MACHINE_TYPE_NAME("versatileab"),
    .parent = TYPE_MACHINE,
    .class_init = versatileab_class_init,
};

static void versatile_machine_init(void)
{
    type_register_static(&versatilepb_type);
    type_register_static(&versatileab_type);
}

type_init(versatile_machine_init)

static void vpb_sic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_vpb_sic;
}

static const TypeInfo vpb_sic_info = {
    .name          = TYPE_VERSATILE_PB_SIC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(vpb_sic_state),
    .instance_init = vpb_sic_init,
    .class_init    = vpb_sic_class_init,
};

static void versatilepb_register_types(void)
{
    type_register_static(&vpb_sic_info);
}

type_init(versatilepb_register_types)
