/*
 * DEC 21272 (TSUNAMI/TYPHOON) chipset emulation.
 *
 * Written by Richard Henderson.
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 */

#include "cpu.h"
#include "exec-all.h"
#include "hw.h"
#include "devices.h"
#include "sysemu.h"
#include "alpha_sys.h"
#include "exec-memory.h"


typedef struct TyphoonCchip {
    MemoryRegion region;
    uint64_t misc;
    uint64_t drir;
    uint64_t dim[4];
    uint32_t iic[4];
    CPUState *cpu[4];
} TyphoonCchip;

typedef struct TyphoonWindow {
    uint32_t base_addr;
    uint32_t mask;
    uint32_t translated_base_pfn;
} TyphoonWindow;
 
typedef struct TyphoonPchip {
    MemoryRegion region;
    MemoryRegion reg_iack;
    MemoryRegion reg_mem;
    MemoryRegion reg_io;
    MemoryRegion reg_conf;
    uint64_t ctl;
    TyphoonWindow win[4];
} TyphoonPchip;

typedef struct TyphoonState {
    PCIHostState host;
    TyphoonCchip cchip;
    TyphoonPchip pchip;
    MemoryRegion dchip_region;
    MemoryRegion ram_region;

    /* QEMU emulation state.  */
    uint32_t latch_tmp;
} TyphoonState;

/* Called when one of DRIR or DIM changes.  */
static void cpu_irq_change(CPUState *env, uint64_t req)
{
    /* If there are any non-masked interrupts, tell the cpu.  */
    if (env) {
        if (req) {
            cpu_interrupt(env, CPU_INTERRUPT_HARD);
        } else {
            cpu_reset_interrupt(env, CPU_INTERRUPT_HARD);
        }
    }
}

static uint64_t cchip_read(void *opaque, target_phys_addr_t addr, unsigned size)
{
    CPUState *env = cpu_single_env;
    TyphoonState *s = opaque;
    uint64_t ret = 0;

    if (addr & 4) {
        return s->latch_tmp;
    }

    switch (addr) {
    case 0x0000:
        /* CSC: Cchip System Configuration Register.  */
        /* All sorts of data here; probably the only thing relevant is
           PIP<14> Pchip 1 Present = 0.  */
        break;

    case 0x0040:
        /* MTR: Memory Timing Register.  */
        /* All sorts of stuff related to real DRAM.  */
        break;

    case 0x0080:
        /* MISC: Miscellaneous Register.  */
        ret = s->cchip.misc | (env->cpu_index & 3);
        break;

    case 0x00c0:
        /* MPD: Memory Presence Detect Register.  */
        break;

    case 0x0100: /* AAR0 */
    case 0x0140: /* AAR1 */
    case 0x0180: /* AAR2 */
    case 0x01c0: /* AAR3 */
        /* AAR: Array Address Register.  */
        /* All sorts of information about DRAM.  */
        break;

    case 0x0200:
        /* DIM0: Device Interrupt Mask Register, CPU0.  */
        ret = s->cchip.dim[0];
        break;
    case 0x0240:
        /* DIM1: Device Interrupt Mask Register, CPU1.  */
        ret = s->cchip.dim[1];
        break;
    case 0x0280:
        /* DIR0: Device Interrupt Request Register, CPU0.  */
        ret = s->cchip.dim[0] & s->cchip.drir;
        break;
    case 0x02c0:
        /* DIR1: Device Interrupt Request Register, CPU1.  */
        ret = s->cchip.dim[1] & s->cchip.drir;
        break;
    case 0x0300:
        /* DRIR: Device Raw Interrupt Request Register.  */
        ret = s->cchip.drir;
        break;

    case 0x0340:
        /* PRBEN: Probe Enable Register.  */
        break;

    case 0x0380:
        /* IIC0: Interval Ignore Count Register, CPU0.  */
        ret = s->cchip.iic[0];
        break;
    case 0x03c0:
        /* IIC1: Interval Ignore Count Register, CPU1.  */
        ret = s->cchip.iic[1];
        break;

    case 0x0400: /* MPR0 */
    case 0x0440: /* MPR1 */
    case 0x0480: /* MPR2 */
    case 0x04c0: /* MPR3 */
        /* MPR: Memory Programming Register.  */
        break;

    case 0x0580:
        /* TTR: TIGbus Timing Register.  */
        /* All sorts of stuff related to interrupt delivery timings.  */
        break;
    case 0x05c0:
        /* TDR: TIGbug Device Timing Register.  */
        break;

    case 0x0600:
        /* DIM2: Device Interrupt Mask Register, CPU2.  */
        ret = s->cchip.dim[2];
        break;
    case 0x0640:
        /* DIM3: Device Interrupt Mask Register, CPU3.  */
        ret = s->cchip.dim[3];
        break;
    case 0x0680:
        /* DIR2: Device Interrupt Request Register, CPU2.  */
        ret = s->cchip.dim[2] & s->cchip.drir;
        break;
    case 0x06c0:
        /* DIR3: Device Interrupt Request Register, CPU3.  */
        ret = s->cchip.dim[3] & s->cchip.drir;
        break;

    case 0x0700:
        /* IIC2: Interval Ignore Count Register, CPU2.  */
        ret = s->cchip.iic[2];
        break;
    case 0x0740:
        /* IIC3: Interval Ignore Count Register, CPU3.  */
        ret = s->cchip.iic[3];
        break;

    case 0x0780:
        /* PWR: Power Management Control.   */
        break;
    
    case 0x0c00: /* CMONCTLA */
    case 0x0c40: /* CMONCTLB */
    case 0x0c80: /* CMONCNT01 */
    case 0x0cc0: /* CMONCNT23 */
        break;

    default:
        cpu_unassigned_access(cpu_single_env, addr, 0, 0, 0, size);
        return -1;
    }

    s->latch_tmp = ret >> 32;
    return ret;
}

static uint64_t dchip_read(void *opaque, target_phys_addr_t addr, unsigned size)
{
    /* Skip this.  It's all related to DRAM timing and setup.  */
    return 0;
}

static uint64_t pchip_read(void *opaque, target_phys_addr_t addr, unsigned size)
{
    TyphoonState *s = opaque;
    uint64_t ret = 0;

    if (addr & 4) {
        return s->latch_tmp;
    }

    switch (addr) {
    case 0x0000:
        /* WSBA0: Window Space Base Address Register.  */
        ret = s->pchip.win[0].base_addr;
        break;
    case 0x0040:
        /* WSBA1 */
        ret = s->pchip.win[1].base_addr;
        break;
    case 0x0080:
        /* WSBA2 */
        ret = s->pchip.win[2].base_addr;
        break;
    case 0x00c0:
        /* WSBA3 */
        ret = s->pchip.win[3].base_addr;
        break;

    case 0x0100:
        /* WSM0: Window Space Mask Register.  */
        ret = s->pchip.win[0].mask;
        break;
    case 0x0140:
        /* WSM1 */
        ret = s->pchip.win[1].mask;
        break;
    case 0x0180:
        /* WSM2 */
        ret = s->pchip.win[2].mask;
        break;
    case 0x01c0:
        /* WSM3 */
        ret = s->pchip.win[3].mask;
        break;

    case 0x0200:
        /* TBA0: Translated Base Address Register.  */
        ret = (uint64_t)s->pchip.win[0].translated_base_pfn << 10;
        break;
    case 0x0240:
        /* TBA1 */
        ret = (uint64_t)s->pchip.win[1].translated_base_pfn << 10;
        break;
    case 0x0280:
        /* TBA2 */
        ret = (uint64_t)s->pchip.win[2].translated_base_pfn << 10;
        break;
    case 0x02c0:
        /* TBA3 */
        ret = (uint64_t)s->pchip.win[3].translated_base_pfn << 10;
        break;

    case 0x0300:
        /* PCTL: Pchip Control Register.  */
        ret = s->pchip.ctl;
        break;
    case 0x0340:
        /* PLAT: Pchip Master Latency Register.  */
        break;
    case 0x03c0:
        /* PERROR: Pchip Error Register.  */
        break;
    case 0x0400:
        /* PERRMASK: Pchip Error Mask Register.  */
        break;
    case 0x0440:
        /* PERRSET: Pchip Error Set Register.  */
        break;
    case 0x0480:
        /* TLBIV: Translation Buffer Invalidate Virtual Register (WO).  */
        break;
    case 0x04c0:
        /* TLBIA: Translation Buffer Invalidate All Register (WO).  */
        break;
    case 0x0500: /* PMONCTL */
    case 0x0540: /* PMONCNT */
    case 0x0800: /* SPRST */
        break;

    default:
        cpu_unassigned_access(cpu_single_env, addr, 0, 0, 0, size);
        return -1;
    }

    s->latch_tmp = ret >> 32;
    return ret;
}

static void cchip_write(void *opaque, target_phys_addr_t addr,
                        uint64_t v32, unsigned size)
{
    TyphoonState *s = opaque;
    uint64_t val, oldval, newval;

    if (addr & 4) {
        val = v32 << 32 | s->latch_tmp;
        addr ^= 4;
    } else {
        s->latch_tmp = v32;
        return;
    }

    switch (addr) {
    case 0x0000:
        /* CSC: Cchip System Configuration Register.  */
        /* All sorts of data here; nothing relevant RW.  */
        break;

    case 0x0040:
        /* MTR: Memory Timing Register.  */
        /* All sorts of stuff related to real DRAM.  */
        break;

    case 0x0080:
        /* MISC: Miscellaneous Register.  */
        newval = oldval = s->cchip.misc;
        newval &= ~(val & 0x10000ff0);     /* W1C fields */
        if (val & 0x100000) {
            newval &= ~0xff0000ull;        /* ACL clears ABT and ABW */
        } else {
            newval |= val & 0x00f00000;    /* ABT field is W1S */
            if ((newval & 0xf0000) == 0) {
                newval |= val & 0xf0000;   /* ABW field is W1S iff zero */
            }
        }
        newval |= (val & 0xf000) >> 4;     /* IPREQ field sets IPINTR.  */

        newval &= ~0xf0000000000ull;       /* WO and RW fields */
        newval |= val & 0xf0000000000ull;
        s->cchip.misc = newval;

        /* Pass on changes to IPI and ITI state.  */
        if ((newval ^ oldval) & 0xff0) {
            int i;
            for (i = 0; i < 4; ++i) {
                CPUState *env = s->cchip.cpu[i];
                if (env) {
                    /* IPI can be either cleared or set by the write.  */
                    if (newval & (1 << (i + 8))) {
                        cpu_interrupt(env, CPU_INTERRUPT_SMP);
                    } else {
                        cpu_reset_interrupt(env, CPU_INTERRUPT_SMP);
                    }

                    /* ITI can only be cleared by the write.  */
                    if ((newval & (1 << (i + 4))) == 0) {
                        cpu_reset_interrupt(env, CPU_INTERRUPT_TIMER);
                    }
                }
            }
        }
        break;

    case 0x00c0:
        /* MPD: Memory Presence Detect Register.  */
        break;

    case 0x0100: /* AAR0 */
    case 0x0140: /* AAR1 */
    case 0x0180: /* AAR2 */
    case 0x01c0: /* AAR3 */
        /* AAR: Array Address Register.  */
        /* All sorts of information about DRAM.  */
        break;

    case 0x0200: /* DIM0 */
        /* DIM: Device Interrupt Mask Register, CPU0.  */
        s->cchip.dim[0] = val;
        cpu_irq_change(s->cchip.cpu[0], val & s->cchip.drir);
        break;
    case 0x0240: /* DIM1 */
        /* DIM: Device Interrupt Mask Register, CPU1.  */
        s->cchip.dim[0] = val;
        cpu_irq_change(s->cchip.cpu[1], val & s->cchip.drir);
        break;

    case 0x0280: /* DIR0 (RO) */
    case 0x02c0: /* DIR1 (RO) */
    case 0x0300: /* DRIR (RO) */
        break;

    case 0x0340:
        /* PRBEN: Probe Enable Register.  */
        break;

    case 0x0380: /* IIC0 */
        s->cchip.iic[0] = val & 0xffffff;
        break;
    case 0x03c0: /* IIC1 */
        s->cchip.iic[1] = val & 0xffffff;
        break;

    case 0x0400: /* MPR0 */
    case 0x0440: /* MPR1 */
    case 0x0480: /* MPR2 */
    case 0x04c0: /* MPR3 */
        /* MPR: Memory Programming Register.  */
        break;

    case 0x0580:
        /* TTR: TIGbus Timing Register.  */
        /* All sorts of stuff related to interrupt delivery timings.  */
        break;
    case 0x05c0:
        /* TDR: TIGbug Device Timing Register.  */
        break;

    case 0x0600:
        /* DIM2: Device Interrupt Mask Register, CPU2.  */
        s->cchip.dim[2] = val;
        cpu_irq_change(s->cchip.cpu[2], val & s->cchip.drir);
        break;
    case 0x0640:
        /* DIM3: Device Interrupt Mask Register, CPU3.  */
        s->cchip.dim[3] = val;
        cpu_irq_change(s->cchip.cpu[3], val & s->cchip.drir);
        break;

    case 0x0680: /* DIR2 (RO) */
    case 0x06c0: /* DIR3 (RO) */
        break;

    case 0x0700: /* IIC2 */
        s->cchip.iic[2] = val & 0xffffff;
        break;
    case 0x0740: /* IIC3 */
        s->cchip.iic[3] = val & 0xffffff;
        break;

    case 0x0780:
        /* PWR: Power Management Control.   */
        break;
    
    case 0x0c00: /* CMONCTLA */
    case 0x0c40: /* CMONCTLB */
    case 0x0c80: /* CMONCNT01 */
    case 0x0cc0: /* CMONCNT23 */
        break;

    default:
        cpu_unassigned_access(cpu_single_env, addr, 1, 0, 0, size);
        return;
    }
}

static void dchip_write(void *opaque, target_phys_addr_t addr,
                        uint64_t val, unsigned size)
{
    /* Skip this.  It's all related to DRAM timing and setup.  */
}

static void pchip_write(void *opaque, target_phys_addr_t addr,
                        uint64_t v32, unsigned size)
{
    TyphoonState *s = opaque;
    uint64_t val, oldval;

    if (addr & 4) {
        val = v32 << 32 | s->latch_tmp;
        addr ^= 4;
    } else {
        s->latch_tmp = v32;
        return;
    }

    switch (addr) {
    case 0x0000:
        /* WSBA0: Window Space Base Address Register.  */
        s->pchip.win[0].base_addr = val;
        break;
    case 0x0040:
        /* WSBA1 */
        s->pchip.win[1].base_addr = val;
        break;
    case 0x0080:
        /* WSBA2 */
        s->pchip.win[2].base_addr = val;
        break;
    case 0x00c0:
        /* WSBA3 */
        s->pchip.win[3].base_addr = val;
        break;

    case 0x0100:
        /* WSM0: Window Space Mask Register.  */
        s->pchip.win[0].mask = val;
        break;
    case 0x0140:
        /* WSM1 */
        s->pchip.win[1].mask = val;
        break;
    case 0x0180:
        /* WSM2 */
        s->pchip.win[2].mask = val;
        break;
    case 0x01c0:
        /* WSM3 */
        s->pchip.win[3].mask = val;
        break;

    case 0x0200:
        /* TBA0: Translated Base Address Register.  */
        s->pchip.win[0].translated_base_pfn = val >> 10;
        break;
    case 0x0240:
        /* TBA1 */
        s->pchip.win[1].translated_base_pfn = val >> 10;
        break;
    case 0x0280:
        /* TBA2 */
        s->pchip.win[2].translated_base_pfn = val >> 10;
        break;
    case 0x02c0:
        /* TBA3 */
        s->pchip.win[3].translated_base_pfn = val >> 10;
        break;

    case 0x0300:
        /* PCTL: Pchip Control Register.  */
        oldval = s->pchip.ctl;
        oldval &= ~0x00001cff0fc7ffull;       /* RW fields */
        oldval |= val & 0x00001cff0fc7ffull;

        s->pchip.ctl = oldval;
        break;

    case 0x0340:
        /* PLAT: Pchip Master Latency Register.  */
        break;
    case 0x03c0:
        /* PERROR: Pchip Error Register.  */
        break;
    case 0x0400:
        /* PERRMASK: Pchip Error Mask Register.  */
        break;
    case 0x0440:
        /* PERRSET: Pchip Error Set Register.  */
        break;

    case 0x0480:
        /* TLBIV: Translation Buffer Invalidate Virtual Register.  */
        break;

    case 0x04c0:
        /* TLBIA: Translation Buffer Invalidate All Register (WO).  */
        break;

    case 0x0500:
        /* PMONCTL */
    case 0x0540:
        /* PMONCNT */
    case 0x0800:
        /* SPRST */
        break;

    default:
        cpu_unassigned_access(cpu_single_env, addr, 1, 0, 0, size);
        return;
    }
}

static const MemoryRegionOps cchip_ops = {
    .read = cchip_read,
    .write = cchip_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,  /* ??? Should be 8.  */
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const MemoryRegionOps dchip_ops = {
    .read = dchip_read,
    .write = dchip_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,  /* ??? Should be 8.  */
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

static const MemoryRegionOps pchip_ops = {
    .read = pchip_read,
    .write = pchip_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,  /* ??? Should be 8.  */
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void typhoon_set_irq(void *opaque, int irq, int level)
{
    TyphoonState *s = opaque;
    uint64_t drir;
    int i;

    /* Set/Reset the bit in CCHIP.DRIR based on IRQ+LEVEL.  */
    drir = s->cchip.drir;
    if (level) {
        drir |= 1ull << irq;
    } else {
        drir &= ~(1ull << irq);
    }
    s->cchip.drir = drir;

    for (i = 0; i < 4; ++i) {
        cpu_irq_change(s->cchip.cpu[i], s->cchip.dim[i] & drir);
    }
}

static void typhoon_set_isa_irq(void *opaque, int irq, int level)
{
    typhoon_set_irq(opaque, 55, level);
}

static void typhoon_set_timer_irq(void *opaque, int irq, int level)
{
    TyphoonState *s = opaque;
    int i;

    /* Thankfully, the mc146818rtc code doesn't track the IRQ state,
       and so we don't have to worry about missing interrupts just
       because we never actually ACK the interrupt.  Just ignore any
       case of the interrupt level going low.  */
    if (level == 0) {
        return;
    }

    /* Deliver the interrupt to each CPU, considering each CPU's IIC.  */
    for (i = 0; i < 4; ++i) {
        CPUState *env = s->cchip.cpu[i];
        if (env) {
            uint32_t iic = s->cchip.iic[i];

            /* ??? The verbage in Section 10.2.2.10 isn't 100% clear.
               Bit 24 is the OverFlow bit, RO, and set when the count
               decrements past 0.  When is OF cleared?  My guess is that
               OF is actually cleared when the IIC is written, and that
               the ICNT field always decrements.  At least, that's an
               interpretation that makes sense, and "allows the CPU to
               determine exactly how mant interval timer ticks were
               skipped".  At least within the next 4M ticks...  */

            iic = ((iic - 1) & 0x1ffffff) | (iic & 0x1000000);
            s->cchip.iic[i] = iic;

            if (iic & 0x1000000) {
                /* Set the ITI bit for this cpu.  */
                s->cchip.misc |= 1 << (i + 4);
                /* And signal the interrupt.  */
                cpu_interrupt(env, CPU_INTERRUPT_TIMER);
            }
        }
    }
}

static void typhoon_alarm_timer(void *opaque)
{
    TyphoonState *s = (TyphoonState *)((uintptr_t)opaque & ~3);
    int cpu = (uintptr_t)opaque & 3;

    /* Set the ITI bit for this cpu.  */
    s->cchip.misc |= 1 << (cpu + 4);
    cpu_interrupt(s->cchip.cpu[cpu], CPU_INTERRUPT_TIMER);
}

PCIBus *typhoon_init(ram_addr_t ram_size, qemu_irq *p_rtc_irq,
                     CPUState *cpus[4], pci_map_irq_fn sys_map_irq)
{
    const uint64_t MB = 1024 * 1024;
    const uint64_t GB = 1024 * MB;
    MemoryRegion *addr_space = get_system_memory();
    MemoryRegion *addr_space_io = get_system_io();
    DeviceState *dev;
    PCIHostState *p;
    TyphoonState *s;
    PCIBus *b;
    int i;

    dev = qdev_create(NULL, "typhoon-pcihost");
    qdev_init_nofail(dev);

    p = FROM_SYSBUS(PCIHostState, sysbus_from_qdev(dev));
    s = container_of(p, TyphoonState, host);

    /* Remember the CPUs so that we can deliver interrupts to them.  */
    for (i = 0; i < 4; i++) {
        CPUState *env = cpus[i];
        s->cchip.cpu[i] = env;
        if (env) {
            env->alarm_timer = qemu_new_timer_ns(rtc_clock,
                                                 typhoon_alarm_timer,
                                                 (void *)((uintptr_t)s + i));
        }
    }

    *p_rtc_irq = *qemu_allocate_irqs(typhoon_set_timer_irq, s, 1);

    /* Main memory region, 0x00.0000.0000.  Real hardware supports 32GB,
       but the address space hole reserved at this point is 8TB.  */
    memory_region_init_ram(&s->ram_region, NULL, "ram", ram_size);
    memory_region_add_subregion(addr_space, 0, &s->ram_region);

    /* TIGbus, 0x801.0000.0000, 1GB.  */
    /* ??? The TIGbus is used for delivering interrupts, and access to
       the flash ROM.  I'm not sure that we need to implement it at all.  */

    /* Pchip0 CSRs, 0x801.8000.0000, 256MB.  */
    memory_region_init_io(&s->pchip.region, &pchip_ops, s, "pchip0", 256*MB);
    memory_region_add_subregion(addr_space, 0x80180000000ULL,
                                &s->pchip.region);

    /* Cchip CSRs, 0x801.A000.0000, 256MB.  */
    memory_region_init_io(&s->cchip.region, &cchip_ops, s, "cchip0", 256*MB);
    memory_region_add_subregion(addr_space, 0x801a0000000ULL,
                                &s->cchip.region);

    /* Dchip CSRs, 0x801.B000.0000, 256MB.  */
    memory_region_init_io(&s->dchip_region, &dchip_ops, s, "dchip0", 256*MB);
    memory_region_add_subregion(addr_space, 0x801b0000000ULL,
                                &s->dchip_region);

    /* Pchip0 PCI memory, 0x800.0000.0000, 4GB.  */
    memory_region_init(&s->pchip.reg_mem, "pci0-mem", 4*GB);
    memory_region_add_subregion(addr_space, 0x80000000000ULL,
                                &s->pchip.reg_mem);

    /* Pchip0 PCI I/O, 0x801.FC00.0000, 32MB.  */
    /* ??? Ideally we drop the "system" i/o space on the floor and give the
       PCI subsystem the full address space reserved by the chipset.
       We can't do that until the MEM and IO paths in memory.c are unified.  */
    memory_region_init_io(&s->pchip.reg_io, &alpha_pci_bw_io_ops, NULL,
                          "pci0-io", 32*MB);
    memory_region_add_subregion(addr_space, 0x801fc000000ULL,
                                &s->pchip.reg_io);

    b = pci_register_bus(&s->host.busdev.qdev, "pci",
                         typhoon_set_irq, sys_map_irq, s,
                         &s->pchip.reg_mem, addr_space_io, 0, 64);
    s->host.bus = b;

    /* Pchip0 PCI special/interrupt acknowledge, 0x801.F800.0000, 64MB.  */
    memory_region_init_io(&s->pchip.reg_iack, &alpha_pci_iack_ops, b,
                          "pci0-iack", 64*MB);
    memory_region_add_subregion(addr_space, 0x801f8000000ULL,
                                &s->pchip.reg_iack);

    /* Pchip0 PCI configuration, 0x801.FE00.0000, 16MB.  */
    memory_region_init_io(&s->pchip.reg_conf, &alpha_pci_conf1_ops, b,
                          "pci0-conf", 16*MB);
    memory_region_add_subregion(addr_space, 0x801fe000000ULL,
                                &s->pchip.reg_conf);

    /* For the record, these are the mappings for the second PCI bus.
       We can get away with not implementing them because we indicate
       via the Cchip.CSC<PIP> bit that Pchip1 is not present.  */
    /* Pchip1 PCI memory, 0x802.0000.0000, 4GB.  */
    /* Pchip1 CSRs, 0x802.8000.0000, 256MB.  */
    /* Pchip1 PCI special/interrupt acknowledge, 0x802.F800.0000, 64MB.  */
    /* Pchip1 PCI I/O, 0x802.FC00.0000, 32MB.  */
    /* Pchip1 PCI configuration, 0x802.FE00.0000, 16MB.  */

    /* Init the ISA bus.  */
    /* ??? Technically there should be a cy82c693ub pci-isa bridge.  */
    {
        qemu_irq isa_pci_irq, *isa_irqs;

        isa_bus_new(NULL, addr_space_io);
        isa_pci_irq = *qemu_allocate_irqs(typhoon_set_isa_irq, s, 1);
        isa_irqs = i8259_init(isa_pci_irq);
        isa_bus_irqs(isa_irqs);
    }

    return b;
}

static int typhoon_pcihost_init(SysBusDevice *dev)
{
    return 0;
}

static SysBusDeviceInfo typhoon_pcihost_info = {
    .init = typhoon_pcihost_init,
    .qdev.name = "typhoon-pcihost",
    .qdev.size = sizeof(TyphoonState),
    .qdev.no_user = 1
};

static void typhoon_register(void)
{
    sysbus_register_withprop(&typhoon_pcihost_info);
}
device_init(typhoon_register);
