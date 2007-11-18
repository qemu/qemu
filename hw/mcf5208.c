/*
 * Motorola ColdFire MCF5208 SoC emulation.
 *
 * Copyright (c) 2007 CodeSourcery.
 *
 * This code is licenced under the GPL
 */
#include "hw.h"
#include "mcf.h"
#include "qemu-timer.h"
#include "sysemu.h"
#include "net.h"
#include "boards.h"

#define SYS_FREQ 66000000

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

static void m5208_timer_write(m5208_timer_state *s, int offset,
                              uint32_t value)
{
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
        /* Should never happen.  */
        abort();
    }
    m5208_timer_update(s);
}

static void m5208_timer_trigger(void *opaque)
{
    m5208_timer_state *s = (m5208_timer_state *)opaque;
    s->pcsr |= PCSR_PIF;
    m5208_timer_update(s);
}

typedef struct {
    m5208_timer_state timer[2];
} m5208_sys_state;

static uint32_t m5208_sys_read(void *opaque, target_phys_addr_t addr)
{
    m5208_sys_state *s = (m5208_sys_state *)opaque;
    switch (addr) {
    /* PIT0 */
    case 0xfc080000:
        return s->timer[0].pcsr;
    case 0xfc080002:
        return s->timer[0].pmr;
    case 0xfc080004:
        return ptimer_get_count(s->timer[0].timer);
    /* PIT1 */
    case 0xfc084000:
        return s->timer[1].pcsr;
    case 0xfc084002:
        return s->timer[1].pmr;
    case 0xfc084004:
        return ptimer_get_count(s->timer[1].timer);

    /* SDRAM Controller.  */
    case 0xfc0a8110: /* SDCS0 */
        {
            int n;
            for (n = 0; n < 32; n++) {
                if (ram_size < (2u << n))
                    break;
            }
            return (n - 1)  | 0x40000000;
        }
    case 0xfc0a8114: /* SDCS1 */
        return 0;

    default:
        cpu_abort(cpu_single_env, "m5208_sys_read: Bad offset 0x%x\n",
                  (int)addr);
        return 0;
    }
}

static void m5208_sys_write(void *opaque, target_phys_addr_t addr,
                            uint32_t value)
{
    m5208_sys_state *s = (m5208_sys_state *)opaque;
    switch (addr) {
    /* PIT0 */
    case 0xfc080000:
    case 0xfc080002:
    case 0xfc080004:
        m5208_timer_write(&s->timer[0], addr & 0xf, value);
        return;
    /* PIT1 */
    case 0xfc084000:
    case 0xfc084002:
    case 0xfc084004:
        m5208_timer_write(&s->timer[1], addr & 0xf, value);
        return;
    default:
        cpu_abort(cpu_single_env, "m5208_sys_write: Bad offset 0x%x\n",
                  (int)addr);
        break;
    }
}

static CPUReadMemoryFunc *m5208_sys_readfn[] = {
   m5208_sys_read,
   m5208_sys_read,
   m5208_sys_read
};

static CPUWriteMemoryFunc *m5208_sys_writefn[] = {
   m5208_sys_write,
   m5208_sys_write,
   m5208_sys_write
};

static void mcf5208_sys_init(qemu_irq *pic)
{
    int iomemtype;
    m5208_sys_state *s;
    QEMUBH *bh;
    int i;

    s = (m5208_sys_state *)qemu_mallocz(sizeof(m5208_sys_state));
    iomemtype = cpu_register_io_memory(0, m5208_sys_readfn,
                                       m5208_sys_writefn, s);
    /* SDRAMC.  */
    cpu_register_physical_memory(0xfc0a8000, 0x00004000, iomemtype);
    /* Timers.  */
    for (i = 0; i < 2; i++) {
        bh = qemu_bh_new(m5208_timer_trigger, &s->timer[i]);
        s->timer[i].timer = ptimer_init(bh);
        cpu_register_physical_memory(0xfc080000 + 0x4000 * i, 0x00004000,
                                     iomemtype);
        s->timer[i].irq = pic[4 + i];
    }
}

static void mcf5208evb_init(int ram_size, int vga_ram_size,
                     const char *boot_device, DisplayState *ds,
                     const char *kernel_filename, const char *kernel_cmdline,
                     const char *initrd_filename, const char *cpu_model)
{
    CPUState *env;
    int kernel_size;
    uint64_t elf_entry;
    target_ulong entry;
    qemu_irq *pic;

    if (!cpu_model)
        cpu_model = "m5208";
    env = cpu_init(cpu_model);
    if (!env) {
        fprintf(stderr, "Unable to find m68k CPU definition\n");
        exit(1);
    }

    /* Initialize CPU registers.  */
    env->vbr = 0;
    /* TODO: Configure BARs.  */

    /* DRAM at 0x20000000 */
    cpu_register_physical_memory(0x40000000, ram_size,
        qemu_ram_alloc(ram_size) | IO_MEM_RAM);

    /* Internal SRAM.  */
    cpu_register_physical_memory(0x80000000, 16384,
        qemu_ram_alloc(16384) | IO_MEM_RAM);

    /* Internal peripherals.  */
    pic = mcf_intc_init(0xfc048000, env);

    mcf_uart_mm_init(0xfc060000, pic[26], serial_hds[0]);
    mcf_uart_mm_init(0xfc064000, pic[27], serial_hds[1]);
    mcf_uart_mm_init(0xfc068000, pic[28], serial_hds[2]);

    mcf5208_sys_init(pic);

    if (nb_nics > 1) {
        fprintf(stderr, "Too many NICs\n");
        exit(1);
    }
    if (nd_table[0].vlan) {
        if (nd_table[0].model == NULL
            || strcmp(nd_table[0].model, "mcf_fec") == 0) {
            mcf_fec_init(&nd_table[0], 0xfc030000, pic + 36);
        } else if (strcmp(nd_table[0].model, "?") == 0) {
            fprintf(stderr, "qemu: Supported NICs: mcf_fec\n");
            exit (1);
        } else {
            fprintf(stderr, "qemu: Unsupported NIC: %s\n", nd_table[0].model);
            exit (1);
        }
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
        fprintf(stderr, "Kernel image must be specified\n");
        exit(1);
    }

    kernel_size = load_elf(kernel_filename, 0, &elf_entry, NULL, NULL);
    entry = elf_entry;
    if (kernel_size < 0) {
        kernel_size = load_uboot(kernel_filename, &entry, NULL);
    }
    if (kernel_size < 0) {
        kernel_size = load_image(kernel_filename, phys_ram_base);
        entry = 0x20000000;
    }
    if (kernel_size < 0) {
        fprintf(stderr, "qemu: could not load kernel '%s'\n", kernel_filename);
        exit(1);
    }

    env->pc = entry;
}

QEMUMachine mcf5208evb_machine = {
    "mcf5208evb",
    "MCF5206EVB",
    mcf5208evb_init,
};
