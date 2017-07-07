/*
 * Motorola ColdFire MCF5208 SoC emulation.
 *
 * Copyright (c) 2007 CodeSourcery.
 *
 * This code is licensed under the GPL
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "cpu.h"
#include "hw/hw.h"
#include "hw/m68k/mcf.h"
#include "hw/m68k/mcf_fec.h"
#include "qemu/timer.h"
#include "hw/ptimer.h"
#include "sysemu/sysemu.h"
#include "sysemu/qtest.h"
#include "net/net.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/sysbus.h"
#include "elf.h"
#include "exec/address-spaces.h"

#define SYS_FREQ 166666666

#define PCSR_EN         0x0001
#define PCSR_RLD        0x0002
#define PCSR_PIF        0x0004
#define PCSR_PIE        0x0008
#define PCSR_OVW        0x0010
#define PCSR_DBG        0x0020
#define PCSR_DOZE       0x0040
#define PCSR_PRE_SHIFT  8
#define PCSR_PRE_MASK   0x0f00

typedef struct {
    MemoryRegion iomem;
    qemu_irq irq;
    ptimer_state *timer;
    uint16_t pcsr;
    uint16_t pmr;
    uint16_t pcntr;
} m5208_timer_state;

static void m5208_timer_update(m5208_timer_state *s)
{
    if ((s->pcsr & (PCSR_PIE | PCSR_PIF)) == (PCSR_PIE | PCSR_PIF))
        qemu_irq_raise(s->irq);
    else
        qemu_irq_lower(s->irq);
}

static void m5208_timer_write(void *opaque, hwaddr offset,
                              uint64_t value, unsigned size)
{
    m5208_timer_state *s = (m5208_timer_state *)opaque;
    int prescale;
    int limit;
    switch (offset) {
    case 0:
        /* The PIF bit is set-to-clear.  */
        if (value & PCSR_PIF) {
            s->pcsr &= ~PCSR_PIF;
            value &= ~PCSR_PIF;
        }
        /* Avoid frobbing the timer if we're just twiddling IRQ bits. */
        if (((s->pcsr ^ value) & ~PCSR_PIE) == 0) {
            s->pcsr = value;
            m5208_timer_update(s);
            return;
        }

        if (s->pcsr & PCSR_EN)
            ptimer_stop(s->timer);

        s->pcsr = value;

        prescale = 1 << ((s->pcsr & PCSR_PRE_MASK) >> PCSR_PRE_SHIFT);
        ptimer_set_freq(s->timer, (SYS_FREQ / 2) / prescale);
        if (s->pcsr & PCSR_RLD)
            limit = s->pmr;
        else
            limit = 0xffff;
        ptimer_set_limit(s->timer, limit, 0);

        if (s->pcsr & PCSR_EN)
            ptimer_run(s->timer, 0);
        break;
    case 2:
        s->pmr = value;
        s->pcsr &= ~PCSR_PIF;
        if ((s->pcsr & PCSR_RLD) == 0) {
            if (s->pcsr & PCSR_OVW)
                ptimer_set_count(s->timer, value);
        } else {
            ptimer_set_limit(s->timer, value, s->pcsr & PCSR_OVW);
        }
        break;
    case 4:
        break;
    default:
        hw_error("m5208_timer_write: Bad offset 0x%x\n", (int)offset);
        break;
    }
    m5208_timer_update(s);
}

static void m5208_timer_trigger(void *opaque)
{
    m5208_timer_state *s = (m5208_timer_state *)opaque;
    s->pcsr |= PCSR_PIF;
    m5208_timer_update(s);
}

static uint64_t m5208_timer_read(void *opaque, hwaddr addr,
                                 unsigned size)
{
    m5208_timer_state *s = (m5208_timer_state *)opaque;
    switch (addr) {
    case 0:
        return s->pcsr;
    case 2:
        return s->pmr;
    case 4:
        return ptimer_get_count(s->timer);
    default:
        hw_error("m5208_timer_read: Bad offset 0x%x\n", (int)addr);
        return 0;
    }
}

static const MemoryRegionOps m5208_timer_ops = {
    .read = m5208_timer_read,
    .write = m5208_timer_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static uint64_t m5208_sys_read(void *opaque, hwaddr addr,
                               unsigned size)
{
    switch (addr) {
    case 0x110: /* SDCS0 */
        {
            int n;
            for (n = 0; n < 32; n++) {
                if (ram_size < (2u << n))
                    break;
            }
            return (n - 1)  | 0x40000000;
        }
    case 0x114: /* SDCS1 */
        return 0;

    default:
        hw_error("m5208_sys_read: Bad offset 0x%x\n", (int)addr);
        return 0;
    }
}

static void m5208_sys_write(void *opaque, hwaddr addr,
                            uint64_t value, unsigned size)
{
    hw_error("m5208_sys_write: Bad offset 0x%x\n", (int)addr);
}

static const MemoryRegionOps m5208_sys_ops = {
    .read = m5208_sys_read,
    .write = m5208_sys_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void mcf5208_sys_init(MemoryRegion *address_space, qemu_irq *pic)
{
    MemoryRegion *iomem = g_new(MemoryRegion, 1);
    m5208_timer_state *s;
    QEMUBH *bh;
    int i;

    /* SDRAMC.  */
    memory_region_init_io(iomem, NULL, &m5208_sys_ops, NULL, "m5208-sys", 0x00004000);
    memory_region_add_subregion(address_space, 0xfc0a8000, iomem);
    /* Timers.  */
    for (i = 0; i < 2; i++) {
        s = (m5208_timer_state *)g_malloc0(sizeof(m5208_timer_state));
        bh = qemu_bh_new(m5208_timer_trigger, s);
        s->timer = ptimer_init(bh, PTIMER_POLICY_DEFAULT);
        memory_region_init_io(&s->iomem, NULL, &m5208_timer_ops, s,
                              "m5208-timer", 0x00004000);
        memory_region_add_subregion(address_space, 0xfc080000 + 0x4000 * i,
                                    &s->iomem);
        s->irq = pic[4 + i];
    }
}

static void mcf_fec_init(MemoryRegion *sysmem, NICInfo *nd, hwaddr base,
                         qemu_irq *irqs)
{
    DeviceState *dev;
    SysBusDevice *s;
    int i;

    qemu_check_nic_model(nd, TYPE_MCF_FEC_NET);
    dev = qdev_create(NULL, TYPE_MCF_FEC_NET);
    qdev_set_nic_properties(dev, nd);
    qdev_init_nofail(dev);

    s = SYS_BUS_DEVICE(dev);
    for (i = 0; i < FEC_NUM_IRQ; i++) {
        sysbus_connect_irq(s, i, irqs[i]);
    }

    memory_region_add_subregion(sysmem, base, sysbus_mmio_get_region(s, 0));
}

static void mcf5208evb_init(MachineState *machine)
{
    ram_addr_t ram_size = machine->ram_size;
    const char *cpu_model = machine->cpu_model;
    const char *kernel_filename = machine->kernel_filename;
    M68kCPU *cpu;
    CPUM68KState *env;
    int kernel_size;
    uint64_t elf_entry;
    hwaddr entry;
    qemu_irq *pic;
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    MemoryRegion *sram = g_new(MemoryRegion, 1);

    if (!cpu_model) {
        cpu_model = "m5208";
    }
    cpu = cpu_m68k_init(cpu_model);
    if (!cpu) {
        fprintf(stderr, "Unable to find m68k CPU definition\n");
        exit(1);
    }
    env = &cpu->env;

    /* Initialize CPU registers.  */
    env->vbr = 0;
    /* TODO: Configure BARs.  */

    /* DRAM at 0x40000000 */
    memory_region_allocate_system_memory(ram, NULL, "mcf5208.ram", ram_size);
    memory_region_add_subregion(address_space_mem, 0x40000000, ram);

    /* Internal SRAM.  */
    memory_region_init_ram(sram, NULL, "mcf5208.sram", 16384, &error_fatal);
    memory_region_add_subregion(address_space_mem, 0x80000000, sram);

    /* Internal peripherals.  */
    pic = mcf_intc_init(address_space_mem, 0xfc048000, cpu);

    mcf_uart_mm_init(0xfc060000, pic[26], serial_hds[0]);
    mcf_uart_mm_init(0xfc064000, pic[27], serial_hds[1]);
    mcf_uart_mm_init(0xfc068000, pic[28], serial_hds[2]);

    mcf5208_sys_init(address_space_mem, pic);

    if (nb_nics > 1) {
        fprintf(stderr, "Too many NICs\n");
        exit(1);
    }
    if (nd_table[0].used) {
        mcf_fec_init(address_space_mem, &nd_table[0],
                     0xfc030000, pic + 36);
    }

    /*  0xfc000000 SCM.  */
    /*  0xfc004000 XBS.  */
    /*  0xfc008000 FlexBus CS.  */
    /* 0xfc030000 FEC.  */
    /*  0xfc040000 SCM + Power management.  */
    /*  0xfc044000 eDMA.  */
    /* 0xfc048000 INTC.  */
    /*  0xfc058000 I2C.  */
    /*  0xfc05c000 QSPI.  */
    /* 0xfc060000 UART0.  */
    /* 0xfc064000 UART0.  */
    /* 0xfc068000 UART0.  */
    /*  0xfc070000 DMA timers.  */
    /* 0xfc080000 PIT0.  */
    /* 0xfc084000 PIT1.  */
    /*  0xfc088000 EPORT.  */
    /*  0xfc08c000 Watchdog.  */
    /*  0xfc090000 clock module.  */
    /*  0xfc0a0000 CCM + reset.  */
    /*  0xfc0a4000 GPIO.  */
    /* 0xfc0a8000 SDRAM controller.  */

    /* Load kernel.  */
    if (!kernel_filename) {
        if (qtest_enabled()) {
            return;
        }
        fprintf(stderr, "Kernel image must be specified\n");
        exit(1);
    }

    kernel_size = load_elf(kernel_filename, NULL, NULL, &elf_entry,
                           NULL, NULL, 1, EM_68K, 0, 0);
    entry = elf_entry;
    if (kernel_size < 0) {
        kernel_size = load_uimage(kernel_filename, &entry, NULL, NULL,
                                  NULL, NULL);
    }
    if (kernel_size < 0) {
        kernel_size = load_image_targphys(kernel_filename, 0x40000000,
                                          ram_size);
        entry = 0x40000000;
    }
    if (kernel_size < 0) {
        fprintf(stderr, "qemu: could not load kernel '%s'\n", kernel_filename);
        exit(1);
    }

    env->pc = entry;
}

static void mcf5208evb_machine_init(MachineClass *mc)
{
    mc->desc = "MCF5206EVB";
    mc->init = mcf5208evb_init;
    mc->is_default = 1;
}

DEFINE_MACHINE("mcf5208evb", mcf5208evb_machine_init)
