/* 
 * ARM Versatile Platform Baseboard System emulation.
 *
 * Copyright (c) 2005-2006 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licenced under the GPL.
 */

#include "vl.h"
#include "arm_pic.h"

#define KERNEL_ARGS_ADDR 0x100
#define KERNEL_LOAD_ADDR 0x00010000
#define INITRD_LOAD_ADDR 0x00800000

/* Primary interrupt controller.  */

typedef struct vpb_sic_state
{
  arm_pic_handler handler;
  uint32_t base;
  uint32_t level;
  uint32_t mask;
  uint32_t pic_enable;
  void *parent;
  int irq;
} vpb_sic_state;

static void vpb_sic_update(vpb_sic_state *s)
{
    uint32_t flags;

    flags = s->level & s->mask;
    pic_set_irq_new(s->parent, s->irq, flags != 0);
}

static void vpb_sic_update_pic(vpb_sic_state *s)
{
    int i;
    uint32_t mask;

    for (i = 21; i <= 30; i++) {
        mask = 1u << i;
        if (!(s->pic_enable & mask))
            continue;
        pic_set_irq_new(s->parent, i, (s->level & mask) != 0);
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
        pic_set_irq_new(s->parent, irq, level);
    vpb_sic_update(s);
}

static uint32_t vpb_sic_read(void *opaque, target_phys_addr_t offset)
{
    vpb_sic_state *s = (vpb_sic_state *)opaque;

    offset -= s->base;
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
        printf ("vpb_sic_read: Bad register offset 0x%x\n", offset);
        return 0;
    }
}

static void vpb_sic_write(void *opaque, target_phys_addr_t offset,
                          uint32_t value)
{
    vpb_sic_state *s = (vpb_sic_state *)opaque;
    offset -= s->base;

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
        printf ("vpb_sic_write: Bad register offset 0x%x\n", offset);
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

static vpb_sic_state *vpb_sic_init(uint32_t base, void *parent, int irq)
{
    vpb_sic_state *s;
    int iomemtype;

    s = (vpb_sic_state *)qemu_mallocz(sizeof(vpb_sic_state));
    if (!s)
        return NULL;
    s->handler = vpb_sic_set_irq;
    s->base = base;
    s->parent = parent;
    s->irq = irq;
    iomemtype = cpu_register_io_memory(0, vpb_sic_readfn,
                                       vpb_sic_writefn, s);
    cpu_register_physical_memory(base, 0x00000fff, iomemtype);
    /* ??? Save/restore.  */
    return s;
}

/* Board init.  */

/* The worlds second smallest bootloader.  Set r0-r2, then jump to kernel.  */
static uint32_t bootloader[] = {
  0xe3a00000, /* mov     r0, #0 */
  0xe3a01083, /* mov     r1, #0x83 */
  0xe3811c01, /* orr     r1, r1, #0x100 */
  0xe59f2000, /* ldr     r2, [pc, #0] */
  0xe59ff000, /* ldr     pc, [pc, #0] */
  0, /* Address of kernel args.  Set by integratorcp_init.  */
  0  /* Kernel entry point.  Set by integratorcp_init.  */
};

static void set_kernel_args(uint32_t ram_size, int initrd_size,
                            const char *kernel_cmdline)
{
    uint32_t *p;

    p = (uint32_t *)(phys_ram_base + KERNEL_ARGS_ADDR);
    /* ATAG_CORE */
    stl_raw(p++, 5);
    stl_raw(p++, 0x54410001);
    stl_raw(p++, 1);
    stl_raw(p++, 0x1000);
    stl_raw(p++, 0);
    /* ATAG_MEM */
    stl_raw(p++, 4);
    stl_raw(p++, 0x54410002);
    stl_raw(p++, ram_size);
    stl_raw(p++, 0);
    if (initrd_size) {
        /* ATAG_INITRD2 */
        stl_raw(p++, 4);
        stl_raw(p++, 0x54420005);
        stl_raw(p++, INITRD_LOAD_ADDR);
        stl_raw(p++, initrd_size);
    }
    if (kernel_cmdline && *kernel_cmdline) {
        /* ATAG_CMDLINE */
        int cmdline_size;

        cmdline_size = strlen(kernel_cmdline);
        memcpy (p + 2, kernel_cmdline, cmdline_size + 1);
        cmdline_size = (cmdline_size >> 2) + 1;
        stl_raw(p++, cmdline_size + 2);
        stl_raw(p++, 0x54410009);
        p += cmdline_size;
    }
    /* ATAG_END */
    stl_raw(p++, 0);
    stl_raw(p++, 0);
}

static void vpb_init(int ram_size, int vga_ram_size, int boot_device,
                     DisplayState *ds, const char **fd_filename, int snapshot,
                     const char *kernel_filename, const char *kernel_cmdline,
                     const char *initrd_filename)
{
    CPUState *env;
    int kernel_size;
    int initrd_size;
    int n;
    void *pic;
    void *sic;

    env = cpu_init();
    cpu_arm_set_model(env, ARM_CPUID_ARM926);
    /* ??? RAM shoud repeat to fill physical memory space.  */
    /* SDRAM at address zero.  */
    cpu_register_physical_memory(0, ram_size, IO_MEM_RAM);

    pic = arm_pic_init_cpu(env);
    pic = pl190_init(0x10140000, pic, ARM_PIC_CPU_IRQ, ARM_PIC_CPU_FIQ);
    sic = vpb_sic_init(0x10003000, pic, 31);
    pl050_init(0x10006000, sic, 3, 0);
    pl050_init(0x10007000, sic, 4, 1);

    /* TODO: Init PCI NICs.  */
    if (nd_table[0].vlan) {
        if (nd_table[0].model == NULL
            || strcmp(nd_table[0].model, "smc91c111") == 0) {
            smc91c111_init(&nd_table[0], 0x10010000, sic, 25);
        } else {
            fprintf(stderr, "qemu: Unsupported NIC: %s\n", nd_table[0].model);
            exit (1);
        }
    }

    pl011_init(0x101f1000, pic, 12, serial_hds[0]);
    pl011_init(0x101f2000, pic, 13, serial_hds[1]);
    pl011_init(0x101f3000, pic, 14, serial_hds[2]);
    pl011_init(0x10009000, sic, 6, serial_hds[3]);

    pl080_init(0x10130000, pic, 17);
    sp804_init(0x101e2000, pic, 4);
    sp804_init(0x101e3000, pic, 5);

    /* The versatile/PB actually has a modified Color LCD controller
       that includes hardware cursor support from the PL111.  */
    pl110_init(ds, 0x10120000, pic, 16, 1);

    /* 0x10000000 System registers.  */
    /* 0x10001000 PCI controller config registers.  */
    /* 0x10002000 Serial bus interface.  */
    /*  0x10003000 Secondary interrupt controller.  */
    /* 0x10004000 AACI (audio).  */
    /* 0x10005000 MMCI0.  */
    /*  0x10006000 KMI0 (keyboard).  */
    /*  0x10007000 KMI1 (mouse).  */
    /* 0x10008000 Character LCD Interface.  */
    /*  0x10009000 UART3.  */
    /* 0x1000a000 Smart card 1.  */
    /* 0x1000b000 MMCI1.  */
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

    /* Load the kernel.  */
    if (!kernel_filename) {
        fprintf(stderr, "Kernel image must be specified\n");
        exit(1);
    }
    kernel_size = load_image(kernel_filename,
                             phys_ram_base + KERNEL_LOAD_ADDR);
    if (kernel_size < 0) {
        fprintf(stderr, "qemu: could not load kernel '%s'\n", kernel_filename);
        exit(1);
    }
    if (initrd_filename) {
        initrd_size = load_image(initrd_filename,
                                 phys_ram_base + INITRD_LOAD_ADDR);
        if (initrd_size < 0) {
            fprintf(stderr, "qemu: could not load initrd '%s'\n",
                    initrd_filename);
            exit(1);
        }
    } else {
        initrd_size = 0;
    }
    bootloader[5] = KERNEL_ARGS_ADDR;
    bootloader[6] = KERNEL_LOAD_ADDR;
    for (n = 0; n < sizeof(bootloader) / 4; n++)
        stl_raw(phys_ram_base + (n * 4), bootloader[n]);
    set_kernel_args(ram_size, initrd_size, kernel_cmdline);
}

QEMUMachine versatilepb_machine = {
    "versatilepb",
    "ARM Versatile/PB (ARM926EJ-S)",
    vpb_init,
};
