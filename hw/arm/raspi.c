/*
 * Raspberry Pi emulation (c) 2012 Gregory Estrade
 * Upstreaming code cleanup [including bcm2835_*] (c) 2013 Jan Petrous
 * This code is licensed under the GNU GPLv2 and later.
 */

/* Based on versatilepb.c, copyright terms below. */

/*
 * ARM Versatile Platform/Application Baseboard System emulation.
 *
 * Copyright (c) 2005-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "hw/boards.h"
#include "hw/devices.h"
#include "hw/loader.h"
#include "hw/sysbus.h"
#include "hw/arm/arm.h"
#include "sysemu/sysemu.h"
#include "exec/address-spaces.h"
#include "hw/arm/bcm2835_common.h"

#define BUS_ADDR(x) (((x) - BCM2708_PERI_BASE) + 0x7e000000)

/* Globals */
hwaddr bcm2835_vcram_base;

const uint32_t bootloader_0[] = {
    0xea000006,
    0xe1a00000,
    0xe1a00000,
    0xe1a00000,
    0xe1a00000,
    0xe1a00000,
    0xe1a00000,
    0xe1a00000,

    0xe3a00000,
    0xe3a01042,
    0xe3811c0c,
    0xe59f2000,
    0xe59ff000,
    0x00000100,
    0x00008000
};

uint32_t bootloader_100[] = {
    0x00000005,
    0x54410001,
    0x00000001,
    0x00001000,
    0x00000000,
    0x00000004,
    0x54410002,
    /* It will be overwritten by dynamically calculated memory size */
    0x08000000,
    0x00000000,
    0x00000000,
    0x00000000
};


static struct arm_boot_info raspi_binfo;

static void raspi_init(QEMUMachineInitArgs *args)
{
    ARMCPU *cpu;
    MemoryRegion *sysmem = get_system_memory();

    MemoryRegion *bcm2835_ram = g_new(MemoryRegion, 1);
    MemoryRegion *bcm2835_vcram = g_new(MemoryRegion, 1);

    MemoryRegion *ram_alias = g_new(MemoryRegion, 4);
    MemoryRegion *vcram_alias = g_new(MemoryRegion, 4);

    MemoryRegion *per_todo_bus = g_new(MemoryRegion, 1);
    MemoryRegion *per_ic_bus = g_new(MemoryRegion, 1);
    MemoryRegion *per_uart_bus = g_new(MemoryRegion, 1);
    MemoryRegion *per_st_bus = g_new(MemoryRegion, 1);
    MemoryRegion *per_sbm_bus = g_new(MemoryRegion, 1);
    MemoryRegion *per_power_bus = g_new(MemoryRegion, 1);
    MemoryRegion *per_fb_bus = g_new(MemoryRegion, 1);
    MemoryRegion *per_prop_bus = g_new(MemoryRegion, 1);
    MemoryRegion *per_vchiq_bus = g_new(MemoryRegion, 1);
    MemoryRegion *per_emmc_bus = g_new(MemoryRegion, 1);
    MemoryRegion *per_dma1_bus = g_new(MemoryRegion, 1);
    MemoryRegion *per_dma2_bus = g_new(MemoryRegion, 1);
    MemoryRegion *per_timer_bus = g_new(MemoryRegion, 1);
    MemoryRegion *per_usb_bus = g_new(MemoryRegion, 1);
    MemoryRegion *per_mphi_bus = g_new(MemoryRegion, 1);

    MemoryRegion *mr;

    qemu_irq pic[72];
    qemu_irq mbox_irq[MBOX_CHAN_COUNT];

    DeviceState *dev;
    SysBusDevice *s;

    int n;

    cpu = cpu_arm_init("arm1176");
    if (!cpu) {
        fprintf(stderr, "Unable to find CPU definition\n");
        exit(1);
    }

    bcm2835_vcram_base = args->ram_size - VCRAM_SIZE;

    /* Write real RAM size in ATAG structure */
    bootloader_100[7] = bcm2835_vcram_base;

    memory_region_init_ram(bcm2835_ram, NULL, "raspi.ram", bcm2835_vcram_base);
    vmstate_register_ram_global(bcm2835_ram);

    memory_region_init_ram(bcm2835_vcram, NULL, "vcram.ram", VCRAM_SIZE);
    vmstate_register_ram_global(bcm2835_vcram);

    memory_region_add_subregion(sysmem, (0 << 30), bcm2835_ram);
    memory_region_add_subregion(sysmem, (0 << 30) + bcm2835_vcram_base,
        bcm2835_vcram);
    for (n = 1; n < 4; n++) {
        memory_region_init_alias(&ram_alias[n], NULL, NULL, bcm2835_ram,
            0, bcm2835_vcram_base);
        memory_region_init_alias(&vcram_alias[n], NULL, NULL, bcm2835_vcram,
            0, VCRAM_SIZE);
        memory_region_add_subregion(sysmem, (n << 30), &ram_alias[n]);
        memory_region_add_subregion(sysmem, (n << 30) + bcm2835_vcram_base,
            &vcram_alias[n]);
    }

    /* (Yet) unmapped I/O registers */
    dev = sysbus_create_simple("bcm2835_todo", BCM2708_PERI_BASE, NULL);
    s = SYS_BUS_DEVICE(dev);
    mr = sysbus_mmio_get_region(s, 0);
    memory_region_init_alias(per_todo_bus, NULL, NULL, mr,
        0, memory_region_size(mr));
    memory_region_add_subregion(sysmem, BUS_ADDR(BCM2708_PERI_BASE),
        per_todo_bus);

    /* Interrupt Controller */
    dev = sysbus_create_varargs("bcm2835_ic", ARMCTRL_IC_BASE,
        qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_IRQ),
        qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_FIQ), NULL);

    s = SYS_BUS_DEVICE(dev);
    mr = sysbus_mmio_get_region(s, 0);
    memory_region_init_alias(per_ic_bus, NULL, NULL, mr,
        0, memory_region_size(mr));
    memory_region_add_subregion(sysmem, BUS_ADDR(ARMCTRL_IC_BASE),
        per_ic_bus);
    for (n = 0; n < 72; n++) {
        pic[n] = qdev_get_gpio_in(dev, n);
    }

    /* UART */
    dev = sysbus_create_simple("pl011", UART0_BASE, pic[INTERRUPT_VC_UART]);
    s = SYS_BUS_DEVICE(dev);
    mr = sysbus_mmio_get_region(s, 0);
    memory_region_init_alias(per_uart_bus, NULL, NULL, mr,
        0, memory_region_size(mr));
    memory_region_add_subregion(sysmem, BUS_ADDR(UART0_BASE),
        per_uart_bus);


    /* System timer */
    dev = sysbus_create_varargs("bcm2835_st", ST_BASE,
            pic[INTERRUPT_TIMER0], pic[INTERRUPT_TIMER1],
            pic[INTERRUPT_TIMER2], pic[INTERRUPT_TIMER3],
            NULL);
    s = SYS_BUS_DEVICE(dev);
    mr = sysbus_mmio_get_region(s, 0);
    memory_region_init_alias(per_st_bus, NULL, NULL, mr,
        0, memory_region_size(mr));
    memory_region_add_subregion(sysmem, BUS_ADDR(ST_BASE),
        per_st_bus);

    /* ARM timer */
    dev = sysbus_create_simple("bcm2835_timer", ARMCTRL_TIMER0_1_BASE,
        pic[INTERRUPT_ARM_TIMER]);
    s = SYS_BUS_DEVICE(dev);
    mr = sysbus_mmio_get_region(s, 0);
    memory_region_init_alias(per_timer_bus, NULL, NULL, mr,
        0, memory_region_size(mr));
    memory_region_add_subregion(sysmem, BUS_ADDR(ARMCTRL_TIMER0_1_BASE),
        per_timer_bus);

    /* USB controller */
    dev = sysbus_create_simple("bcm2835_usb", USB_BASE,
        pic[INTERRUPT_VC_USB]);
    s = SYS_BUS_DEVICE(dev);
    mr = sysbus_mmio_get_region(s, 0);
    memory_region_init_alias(per_usb_bus, NULL, NULL, mr,
        0, memory_region_size(mr));
    memory_region_add_subregion(sysmem, BUS_ADDR(USB_BASE),
        per_usb_bus);

    /* MPHI - Message-based Parallel Host Interface */
    dev = sysbus_create_simple("bcm2835_mphi", MPHI_BASE,
        pic[INTERRUPT_HOSTPORT]);
    s = SYS_BUS_DEVICE(dev);
    mr = sysbus_mmio_get_region(s, 0);
    memory_region_init_alias(per_mphi_bus, NULL, NULL, mr,
        0, memory_region_size(mr));
    memory_region_add_subregion(sysmem, BUS_ADDR(MPHI_BASE),
        per_mphi_bus);


    /* Semaphores / Doorbells / Mailboxes */
    dev = sysbus_create_simple("bcm2835_sbm", ARMCTRL_0_SBM_BASE,
        pic[INTERRUPT_ARM_MAILBOX]);
    s = SYS_BUS_DEVICE(dev);
    mr = sysbus_mmio_get_region(s, 0);
    memory_region_init_alias(per_sbm_bus, NULL, NULL, mr,
        0, memory_region_size(mr));
    memory_region_add_subregion(sysmem, BUS_ADDR(ARMCTRL_0_SBM_BASE),
        per_sbm_bus);

    for (n = 0; n < MBOX_CHAN_COUNT; n++) {
        mbox_irq[n] = qdev_get_gpio_in(dev, n);
    }

    /* Mailbox-addressable peripherals using (hopefully) free address space */
    /* locations and pseudo-irqs to dispatch mailbox requests and responses */
    /* between them. */

    /* Power management */
    dev = sysbus_create_simple("bcm2835_power",
        ARMCTRL_0_SBM_BASE + 0x400 + (MBOX_CHAN_POWER<<4),
        mbox_irq[MBOX_CHAN_POWER]);
    s = SYS_BUS_DEVICE(dev);
    mr = sysbus_mmio_get_region(s, 0);
    memory_region_init_alias(per_power_bus, NULL, NULL, mr,
        0, memory_region_size(mr));
    memory_region_add_subregion(sysmem,
        BUS_ADDR(ARMCTRL_0_SBM_BASE + 0x400 + (MBOX_CHAN_POWER<<4)),
        per_power_bus);

    /* Framebuffer */
    dev = sysbus_create_simple("bcm2835_fb",
        ARMCTRL_0_SBM_BASE + 0x400 + (MBOX_CHAN_FB<<4),
        mbox_irq[MBOX_CHAN_FB]);
    s = SYS_BUS_DEVICE(dev);
    mr = sysbus_mmio_get_region(s, 0);
    memory_region_init_alias(per_fb_bus, NULL, NULL, mr,
        0, memory_region_size(mr));
    memory_region_add_subregion(sysmem,
        BUS_ADDR(ARMCTRL_0_SBM_BASE + 0x400 + (MBOX_CHAN_FB<<4)),
        per_fb_bus);

    /* Property channel */
    dev = sysbus_create_simple("bcm2835_property",
        ARMCTRL_0_SBM_BASE + 0x400 + (MBOX_CHAN_PROPERTY<<4),
        mbox_irq[MBOX_CHAN_PROPERTY]);
    s = SYS_BUS_DEVICE(dev);
    mr = sysbus_mmio_get_region(s, 0);
    memory_region_init_alias(per_prop_bus, NULL, NULL, mr,
        0, memory_region_size(mr));
    memory_region_add_subregion(sysmem,
        BUS_ADDR(ARMCTRL_0_SBM_BASE + 0x400 + (MBOX_CHAN_PROPERTY<<4)),
        per_prop_bus);

    /* VCHIQ */
    dev = sysbus_create_simple("bcm2835_vchiq",
        ARMCTRL_0_SBM_BASE + 0x400 + (MBOX_CHAN_VCHIQ<<4),
        mbox_irq[MBOX_CHAN_VCHIQ]);
    s = SYS_BUS_DEVICE(dev);
    mr = sysbus_mmio_get_region(s, 0);
    memory_region_init_alias(per_vchiq_bus, NULL, NULL, mr,
        0, memory_region_size(mr));
    memory_region_add_subregion(sysmem,
        BUS_ADDR(ARMCTRL_0_SBM_BASE + 0x400 + (MBOX_CHAN_VCHIQ<<4)),
        per_vchiq_bus);

    /* Extended Mass Media Controller */
    dev = sysbus_create_simple("bcm2835_emmc", EMMC_BASE,
        pic[INTERRUPT_VC_ARASANSDIO]);
    s = SYS_BUS_DEVICE(dev);
    mr = sysbus_mmio_get_region(s, 0);
    memory_region_init_alias(per_emmc_bus, NULL, NULL, mr,
        0, memory_region_size(mr));
    memory_region_add_subregion(sysmem, BUS_ADDR(EMMC_BASE),
        per_emmc_bus);

    /* DMA Channels */
    dev = qdev_create(NULL, "bcm2835_dma");
    s = SYS_BUS_DEVICE(dev);
    qdev_init_nofail(dev);
    sysbus_mmio_map(s, 0, DMA_BASE);
    sysbus_mmio_map(s, 1, (BCM2708_PERI_BASE + 0xe05000));
    s = SYS_BUS_DEVICE(dev);
    mr = sysbus_mmio_get_region(s, 0);
    memory_region_init_alias(per_dma1_bus, NULL, NULL, mr,
        0, memory_region_size(mr));
    memory_region_add_subregion(sysmem, BUS_ADDR(DMA_BASE),
        per_dma1_bus);
    mr = sysbus_mmio_get_region(s, 1);
    memory_region_init_alias(per_dma2_bus, NULL, NULL, mr,
        0, memory_region_size(mr));
    memory_region_add_subregion(sysmem, BUS_ADDR(BCM2708_PERI_BASE + 0xe05000),
        per_dma2_bus);
    sysbus_connect_irq(s, 0, pic[INTERRUPT_DMA0]);
    sysbus_connect_irq(s, 1, pic[INTERRUPT_DMA1]);
    sysbus_connect_irq(s, 2, pic[INTERRUPT_VC_DMA2]);
    sysbus_connect_irq(s, 3, pic[INTERRUPT_VC_DMA3]);
    sysbus_connect_irq(s, 4, pic[INTERRUPT_DMA4]);
    sysbus_connect_irq(s, 5, pic[INTERRUPT_DMA5]);
    sysbus_connect_irq(s, 6, pic[INTERRUPT_DMA6]);
    sysbus_connect_irq(s, 7, pic[INTERRUPT_DMA7]);
    sysbus_connect_irq(s, 8, pic[INTERRUPT_DMA8]);
    sysbus_connect_irq(s, 9, pic[INTERRUPT_DMA9]);
    sysbus_connect_irq(s, 10, pic[INTERRUPT_DMA10]);
    sysbus_connect_irq(s, 11, pic[INTERRUPT_DMA11]);
    sysbus_connect_irq(s, 12, pic[INTERRUPT_DMA12]);

    /* Finally, the board itself */
    raspi_binfo.ram_size = bcm2835_vcram_base;
    raspi_binfo.kernel_filename = args->kernel_filename;
    raspi_binfo.kernel_cmdline = args->kernel_cmdline;
    raspi_binfo.initrd_filename = args->initrd_filename;
    raspi_binfo.board_id = 0xc42;

    /* Quick and dirty "selector" */
    if (args->initrd_filename
        && !strcmp(args->kernel_filename, args->initrd_filename)) {

        for (n = 0; n < ARRAY_SIZE(bootloader_0); n++) {
            stl_phys(&address_space_memory, (n << 2), bootloader_0[n]);
        }
        for (n = 0; n < ARRAY_SIZE(bootloader_100); n++) {
            stl_phys(&address_space_memory, 0x100 + (n << 2), bootloader_100[n]);
        }
        load_image_targphys(args->initrd_filename,
                            0x8000,
                            bcm2835_vcram_base - 0x8000);
        cpu_reset(CPU(cpu));
    } else {
        arm_load_kernel(cpu, &raspi_binfo);
    }
}

static QEMUMachine raspi_machine = {
    .name = "raspi",
    .desc = "Raspberry Pi",
    .init = raspi_init
};

static void raspi_machine_init(void)
{
    qemu_register_machine(&raspi_machine);
}

machine_init(raspi_machine_init);
