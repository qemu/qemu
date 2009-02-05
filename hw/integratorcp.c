/*
 * ARM Integrator CP System emulation.
 *
 * Copyright (c) 2005-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licenced under the GPL
 */

#include "hw.h"
#include "primecell.h"
#include "devices.h"
#include "sysemu.h"
#include "boards.h"
#include "arm-misc.h"
#include "net.h"

typedef struct {
    uint32_t flash_offset;
    uint32_t cm_osc;
    uint32_t cm_ctrl;
    uint32_t cm_lock;
    uint32_t cm_auxosc;
    uint32_t cm_sdram;
    uint32_t cm_init;
    uint32_t cm_flags;
    uint32_t cm_nvflags;
    uint32_t int_level;
    uint32_t irq_enabled;
    uint32_t fiq_enabled;
} integratorcm_state;

static uint8_t integrator_spd[128] = {
   128, 8, 4, 11, 9, 1, 64, 0,  2, 0xa0, 0xa0, 0, 0, 8, 0, 1,
   0xe, 4, 0x1c, 1, 2, 0x20, 0xc0, 0, 0, 0, 0, 0x30, 0x28, 0x30, 0x28, 0x40
};

static uint32_t integratorcm_read(void *opaque, target_phys_addr_t offset)
{
    integratorcm_state *s = (integratorcm_state *)opaque;
    if (offset >= 0x100 && offset < 0x200) {
        /* CM_SPD */
        if (offset >= 0x180)
            return 0;
        return integrator_spd[offset >> 2];
    }
    switch (offset >> 2) {
    case 0: /* CM_ID */
        return 0x411a3001;
    case 1: /* CM_PROC */
        return 0;
    case 2: /* CM_OSC */
        return s->cm_osc;
    case 3: /* CM_CTRL */
        return s->cm_ctrl;
    case 4: /* CM_STAT */
        return 0x00100000;
    case 5: /* CM_LOCK */
        if (s->cm_lock == 0xa05f) {
            return 0x1a05f;
        } else {
            return s->cm_lock;
        }
    case 6: /* CM_LMBUSCNT */
        /* ??? High frequency timer.  */
        cpu_abort(cpu_single_env, "integratorcm_read: CM_LMBUSCNT");
    case 7: /* CM_AUXOSC */
        return s->cm_auxosc;
    case 8: /* CM_SDRAM */
        return s->cm_sdram;
    case 9: /* CM_INIT */
        return s->cm_init;
    case 10: /* CM_REFCT */
        /* ??? High frequency timer.  */
        cpu_abort(cpu_single_env, "integratorcm_read: CM_REFCT");
    case 12: /* CM_FLAGS */
        return s->cm_flags;
    case 14: /* CM_NVFLAGS */
        return s->cm_nvflags;
    case 16: /* CM_IRQ_STAT */
        return s->int_level & s->irq_enabled;
    case 17: /* CM_IRQ_RSTAT */
        return s->int_level;
    case 18: /* CM_IRQ_ENSET */
        return s->irq_enabled;
    case 20: /* CM_SOFT_INTSET */
        return s->int_level & 1;
    case 24: /* CM_FIQ_STAT */
        return s->int_level & s->fiq_enabled;
    case 25: /* CM_FIQ_RSTAT */
        return s->int_level;
    case 26: /* CM_FIQ_ENSET */
        return s->fiq_enabled;
    case 32: /* CM_VOLTAGE_CTL0 */
    case 33: /* CM_VOLTAGE_CTL1 */
    case 34: /* CM_VOLTAGE_CTL2 */
    case 35: /* CM_VOLTAGE_CTL3 */
        /* ??? Voltage control unimplemented.  */
        return 0;
    default:
        cpu_abort (cpu_single_env,
            "integratorcm_read: Unimplemented offset 0x%x\n", (int)offset);
        return 0;
    }
}

static void integratorcm_do_remap(integratorcm_state *s, int flash)
{
    if (flash) {
        cpu_register_physical_memory(0, 0x100000, IO_MEM_RAM);
    } else {
        cpu_register_physical_memory(0, 0x100000, s->flash_offset | IO_MEM_RAM);
    }
    //??? tlb_flush (cpu_single_env, 1);
}

static void integratorcm_set_ctrl(integratorcm_state *s, uint32_t value)
{
    if (value & 8) {
        cpu_abort(cpu_single_env, "Board reset\n");
    }
    if ((s->cm_init ^ value) & 4) {
        integratorcm_do_remap(s, (value & 4) == 0);
    }
    if ((s->cm_init ^ value) & 1) {
        printf("Green LED %s\n", (value & 1) ? "on" : "off");
    }
    s->cm_init = (s->cm_init & ~ 5) | (value ^ 5);
}

static void integratorcm_update(integratorcm_state *s)
{
    /* ??? The CPU irq/fiq is raised when either the core module or base PIC
       are active.  */
    if (s->int_level & (s->irq_enabled | s->fiq_enabled))
        cpu_abort(cpu_single_env, "Core module interrupt\n");
}

static void integratorcm_write(void *opaque, target_phys_addr_t offset,
                               uint32_t value)
{
    integratorcm_state *s = (integratorcm_state *)opaque;
    switch (offset >> 2) {
    case 2: /* CM_OSC */
        if (s->cm_lock == 0xa05f)
            s->cm_osc = value;
        break;
    case 3: /* CM_CTRL */
        integratorcm_set_ctrl(s, value);
        break;
    case 5: /* CM_LOCK */
        s->cm_lock = value & 0xffff;
        break;
    case 7: /* CM_AUXOSC */
        if (s->cm_lock == 0xa05f)
            s->cm_auxosc = value;
        break;
    case 8: /* CM_SDRAM */
        s->cm_sdram = value;
        break;
    case 9: /* CM_INIT */
        /* ??? This can change the memory bus frequency.  */
        s->cm_init = value;
        break;
    case 12: /* CM_FLAGSS */
        s->cm_flags |= value;
        break;
    case 13: /* CM_FLAGSC */
        s->cm_flags &= ~value;
        break;
    case 14: /* CM_NVFLAGSS */
        s->cm_nvflags |= value;
        break;
    case 15: /* CM_NVFLAGSS */
        s->cm_nvflags &= ~value;
        break;
    case 18: /* CM_IRQ_ENSET */
        s->irq_enabled |= value;
        integratorcm_update(s);
        break;
    case 19: /* CM_IRQ_ENCLR */
        s->irq_enabled &= ~value;
        integratorcm_update(s);
        break;
    case 20: /* CM_SOFT_INTSET */
        s->int_level |= (value & 1);
        integratorcm_update(s);
        break;
    case 21: /* CM_SOFT_INTCLR */
        s->int_level &= ~(value & 1);
        integratorcm_update(s);
        break;
    case 26: /* CM_FIQ_ENSET */
        s->fiq_enabled |= value;
        integratorcm_update(s);
        break;
    case 27: /* CM_FIQ_ENCLR */
        s->fiq_enabled &= ~value;
        integratorcm_update(s);
        break;
    case 32: /* CM_VOLTAGE_CTL0 */
    case 33: /* CM_VOLTAGE_CTL1 */
    case 34: /* CM_VOLTAGE_CTL2 */
    case 35: /* CM_VOLTAGE_CTL3 */
        /* ??? Voltage control unimplemented.  */
        break;
    default:
        cpu_abort (cpu_single_env,
            "integratorcm_write: Unimplemented offset 0x%x\n", (int)offset);
        break;
    }
}

/* Integrator/CM control registers.  */

static CPUReadMemoryFunc *integratorcm_readfn[] = {
   integratorcm_read,
   integratorcm_read,
   integratorcm_read
};

static CPUWriteMemoryFunc *integratorcm_writefn[] = {
   integratorcm_write,
   integratorcm_write,
   integratorcm_write
};

static void integratorcm_init(int memsz)
{
    int iomemtype;
    integratorcm_state *s;

    s = (integratorcm_state *)qemu_mallocz(sizeof(integratorcm_state));
    s->cm_osc = 0x01000048;
    /* ??? What should the high bits of this value be?  */
    s->cm_auxosc = 0x0007feff;
    s->cm_sdram = 0x00011122;
    if (memsz >= 256) {
        integrator_spd[31] = 64;
        s->cm_sdram |= 0x10;
    } else if (memsz >= 128) {
        integrator_spd[31] = 32;
        s->cm_sdram |= 0x0c;
    } else if (memsz >= 64) {
        integrator_spd[31] = 16;
        s->cm_sdram |= 0x08;
    } else if (memsz >= 32) {
        integrator_spd[31] = 4;
        s->cm_sdram |= 0x04;
    } else {
        integrator_spd[31] = 2;
    }
    memcpy(integrator_spd + 73, "QEMU-MEMORY", 11);
    s->cm_init = 0x00000112;
    s->flash_offset = qemu_ram_alloc(0x100000);

    iomemtype = cpu_register_io_memory(0, integratorcm_readfn,
                                       integratorcm_writefn, s);
    cpu_register_physical_memory(0x10000000, 0x00800000, iomemtype);
    integratorcm_do_remap(s, 1);
    /* ??? Save/restore.  */
}

/* Integrator/CP hardware emulation.  */
/* Primary interrupt controller.  */

typedef struct icp_pic_state
{
  uint32_t level;
  uint32_t irq_enabled;
  uint32_t fiq_enabled;
  qemu_irq parent_irq;
  qemu_irq parent_fiq;
} icp_pic_state;

static void icp_pic_update(icp_pic_state *s)
{
    uint32_t flags;

    flags = (s->level & s->irq_enabled);
    qemu_set_irq(s->parent_irq, flags != 0);
    flags = (s->level & s->fiq_enabled);
    qemu_set_irq(s->parent_fiq, flags != 0);
}

static void icp_pic_set_irq(void *opaque, int irq, int level)
{
    icp_pic_state *s = (icp_pic_state *)opaque;
    if (level)
        s->level |= 1 << irq;
    else
        s->level &= ~(1 << irq);
    icp_pic_update(s);
}

static uint32_t icp_pic_read(void *opaque, target_phys_addr_t offset)
{
    icp_pic_state *s = (icp_pic_state *)opaque;

    switch (offset >> 2) {
    case 0: /* IRQ_STATUS */
        return s->level & s->irq_enabled;
    case 1: /* IRQ_RAWSTAT */
        return s->level;
    case 2: /* IRQ_ENABLESET */
        return s->irq_enabled;
    case 4: /* INT_SOFTSET */
        return s->level & 1;
    case 8: /* FRQ_STATUS */
        return s->level & s->fiq_enabled;
    case 9: /* FRQ_RAWSTAT */
        return s->level;
    case 10: /* FRQ_ENABLESET */
        return s->fiq_enabled;
    case 3: /* IRQ_ENABLECLR */
    case 5: /* INT_SOFTCLR */
    case 11: /* FRQ_ENABLECLR */
    default:
        printf ("icp_pic_read: Bad register offset 0x%x\n", (int)offset);
        return 0;
    }
}

static void icp_pic_write(void *opaque, target_phys_addr_t offset,
                          uint32_t value)
{
    icp_pic_state *s = (icp_pic_state *)opaque;

    switch (offset >> 2) {
    case 2: /* IRQ_ENABLESET */
        s->irq_enabled |= value;
        break;
    case 3: /* IRQ_ENABLECLR */
        s->irq_enabled &= ~value;
        break;
    case 4: /* INT_SOFTSET */
        if (value & 1)
            icp_pic_set_irq(s, 0, 1);
        break;
    case 5: /* INT_SOFTCLR */
        if (value & 1)
            icp_pic_set_irq(s, 0, 0);
        break;
    case 10: /* FRQ_ENABLESET */
        s->fiq_enabled |= value;
        break;
    case 11: /* FRQ_ENABLECLR */
        s->fiq_enabled &= ~value;
        break;
    case 0: /* IRQ_STATUS */
    case 1: /* IRQ_RAWSTAT */
    case 8: /* FRQ_STATUS */
    case 9: /* FRQ_RAWSTAT */
    default:
        printf ("icp_pic_write: Bad register offset 0x%x\n", (int)offset);
        return;
    }
    icp_pic_update(s);
}

static CPUReadMemoryFunc *icp_pic_readfn[] = {
   icp_pic_read,
   icp_pic_read,
   icp_pic_read
};

static CPUWriteMemoryFunc *icp_pic_writefn[] = {
   icp_pic_write,
   icp_pic_write,
   icp_pic_write
};

static qemu_irq *icp_pic_init(uint32_t base,
                              qemu_irq parent_irq, qemu_irq parent_fiq)
{
    icp_pic_state *s;
    int iomemtype;
    qemu_irq *qi;

    s = (icp_pic_state *)qemu_mallocz(sizeof(icp_pic_state));
    qi = qemu_allocate_irqs(icp_pic_set_irq, s, 32);
    s->parent_irq = parent_irq;
    s->parent_fiq = parent_fiq;
    iomemtype = cpu_register_io_memory(0, icp_pic_readfn,
                                       icp_pic_writefn, s);
    cpu_register_physical_memory(base, 0x00800000, iomemtype);
    /* ??? Save/restore.  */
    return qi;
}

/* CP control registers.  */
static uint32_t icp_control_read(void *opaque, target_phys_addr_t offset)
{
    switch (offset >> 2) {
    case 0: /* CP_IDFIELD */
        return 0x41034003;
    case 1: /* CP_FLASHPROG */
        return 0;
    case 2: /* CP_INTREG */
        return 0;
    case 3: /* CP_DECODE */
        return 0x11;
    default:
        cpu_abort (cpu_single_env, "icp_control_read: Bad offset %x\n",
                   (int)offset);
        return 0;
    }
}

static void icp_control_write(void *opaque, target_phys_addr_t offset,
                          uint32_t value)
{
    switch (offset >> 2) {
    case 1: /* CP_FLASHPROG */
    case 2: /* CP_INTREG */
    case 3: /* CP_DECODE */
        /* Nothing interesting implemented yet.  */
        break;
    default:
        cpu_abort (cpu_single_env, "icp_control_write: Bad offset %x\n",
                   (int)offset);
    }
}
static CPUReadMemoryFunc *icp_control_readfn[] = {
   icp_control_read,
   icp_control_read,
   icp_control_read
};

static CPUWriteMemoryFunc *icp_control_writefn[] = {
   icp_control_write,
   icp_control_write,
   icp_control_write
};

static void icp_control_init(uint32_t base)
{
    int iomemtype;

    iomemtype = cpu_register_io_memory(0, icp_control_readfn,
                                       icp_control_writefn, NULL);
    cpu_register_physical_memory(base, 0x00800000, iomemtype);
    /* ??? Save/restore.  */
}


/* Board init.  */

static struct arm_boot_info integrator_binfo = {
    .loader_start = 0x0,
    .board_id = 0x113,
};

static void integratorcp_init(ram_addr_t ram_size, int vga_ram_size,
                     const char *boot_device,
                     const char *kernel_filename, const char *kernel_cmdline,
                     const char *initrd_filename, const char *cpu_model)
{
    CPUState *env;
    uint32_t ram_offset;
    qemu_irq *pic;
    qemu_irq *cpu_pic;
    int sd;

    if (!cpu_model)
        cpu_model = "arm926";
    env = cpu_init(cpu_model);
    if (!env) {
        fprintf(stderr, "Unable to find CPU definition\n");
        exit(1);
    }
    ram_offset = qemu_ram_alloc(ram_size);
    /* ??? On a real system the first 1Mb is mapped as SSRAM or boot flash.  */
    /* ??? RAM should repeat to fill physical memory space.  */
    /* SDRAM at address zero*/
    cpu_register_physical_memory(0, ram_size, ram_offset | IO_MEM_RAM);
    /* And again at address 0x80000000 */
    cpu_register_physical_memory(0x80000000, ram_size, ram_offset | IO_MEM_RAM);

    integratorcm_init(ram_size >> 20);
    cpu_pic = arm_pic_init_cpu(env);
    pic = icp_pic_init(0x14000000, cpu_pic[ARM_PIC_CPU_IRQ],
                       cpu_pic[ARM_PIC_CPU_FIQ]);
    icp_pic_init(0xca000000, pic[26], NULL);
    icp_pit_init(0x13000000, pic, 5);
    pl031_init(0x15000000, pic[8]);
    pl011_init(0x16000000, pic[1], serial_hds[0], PL011_ARM);
    pl011_init(0x17000000, pic[2], serial_hds[1], PL011_ARM);
    icp_control_init(0xcb000000);
    pl050_init(0x18000000, pic[3], 0);
    pl050_init(0x19000000, pic[4], 1);
    sd = drive_get_index(IF_SD, 0, 0);
    if (sd == -1) {
        fprintf(stderr, "qemu: missing SecureDigital card\n");
        exit(1);
    }
    pl181_init(0x1c000000, drives_table[sd].bdrv, pic[23], pic[24]);
    if (nd_table[0].vlan)
        smc91c111_init(&nd_table[0], 0xc8000000, pic[27]);
    pl110_init(0xc0000000, pic[22], 0);

    integrator_binfo.ram_size = ram_size;
    integrator_binfo.kernel_filename = kernel_filename;
    integrator_binfo.kernel_cmdline = kernel_cmdline;
    integrator_binfo.initrd_filename = initrd_filename;
    arm_load_kernel(env, &integrator_binfo);
}

QEMUMachine integratorcp_machine = {
    .name = "integratorcp",
    .desc = "ARM Integrator/CP (ARM926EJ-S)",
    .init = integratorcp_init,
    .ram_require = 0x100000,
};
