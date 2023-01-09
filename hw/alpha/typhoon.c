/*
 * DEC 21272 (TSUNAMI/TYPHOON) chipset emulation.
 *
 * Written by Richard Henderson.
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/pci/pci_host.h"
#include "cpu.h"
#include "hw/irq.h"
#include "alpha_sys.h"


#define TYPE_TYPHOON_PCI_HOST_BRIDGE "typhoon-pcihost"
#define TYPE_TYPHOON_IOMMU_MEMORY_REGION "typhoon-iommu-memory-region"

typedef struct TyphoonCchip {
    MemoryRegion region;
    uint64_t misc;
    uint64_t drir;
    uint64_t dim[4];
    uint32_t iic[4];
    AlphaCPU *cpu[4];
} TyphoonCchip;

typedef struct TyphoonWindow {
    uint64_t wba;
    uint64_t wsm;
    uint64_t tba;
} TyphoonWindow;
 
typedef struct TyphoonPchip {
    MemoryRegion region;
    MemoryRegion reg_iack;
    MemoryRegion reg_mem;
    MemoryRegion reg_io;
    MemoryRegion reg_conf;

    AddressSpace iommu_as;
    IOMMUMemoryRegion iommu;

    uint64_t ctl;
    TyphoonWindow win[4];
} TyphoonPchip;

OBJECT_DECLARE_SIMPLE_TYPE(TyphoonState, TYPHOON_PCI_HOST_BRIDGE)

struct TyphoonState {
    PCIHostState parent_obj;

    TyphoonCchip cchip;
    TyphoonPchip pchip;
    MemoryRegion dchip_region;
};

/* Called when one of DRIR or DIM changes.  */
static void cpu_irq_change(AlphaCPU *cpu, uint64_t req)
{
    /* If there are any non-masked interrupts, tell the cpu.  */
    if (cpu != NULL) {
        CPUState *cs = CPU(cpu);
        if (req) {
            cpu_interrupt(cs, CPU_INTERRUPT_HARD);
        } else {
            cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
        }
    }
}

static MemTxResult cchip_read(void *opaque, hwaddr addr,
                              uint64_t *data, unsigned size,
                              MemTxAttrs attrs)
{
    CPUState *cpu = current_cpu;
    TyphoonState *s = opaque;
    uint64_t ret = 0;

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
        ret = s->cchip.misc | (cpu->cpu_index & 3);
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
        return MEMTX_ERROR;
    }

    *data = ret;
    return MEMTX_OK;
}

static uint64_t dchip_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Skip this.  It's all related to DRAM timing and setup.  */
    return 0;
}

static MemTxResult pchip_read(void *opaque, hwaddr addr, uint64_t *data,
                              unsigned size, MemTxAttrs attrs)
{
    TyphoonState *s = opaque;
    uint64_t ret = 0;

    switch (addr) {
    case 0x0000:
        /* WSBA0: Window Space Base Address Register.  */
        ret = s->pchip.win[0].wba;
        break;
    case 0x0040:
        /* WSBA1 */
        ret = s->pchip.win[1].wba;
        break;
    case 0x0080:
        /* WSBA2 */
        ret = s->pchip.win[2].wba;
        break;
    case 0x00c0:
        /* WSBA3 */
        ret = s->pchip.win[3].wba;
        break;

    case 0x0100:
        /* WSM0: Window Space Mask Register.  */
        ret = s->pchip.win[0].wsm;
        break;
    case 0x0140:
        /* WSM1 */
        ret = s->pchip.win[1].wsm;
        break;
    case 0x0180:
        /* WSM2 */
        ret = s->pchip.win[2].wsm;
        break;
    case 0x01c0:
        /* WSM3 */
        ret = s->pchip.win[3].wsm;
        break;

    case 0x0200:
        /* TBA0: Translated Base Address Register.  */
        ret = s->pchip.win[0].tba;
        break;
    case 0x0240:
        /* TBA1 */
        ret = s->pchip.win[1].tba;
        break;
    case 0x0280:
        /* TBA2 */
        ret = s->pchip.win[2].tba;
        break;
    case 0x02c0:
        /* TBA3 */
        ret = s->pchip.win[3].tba;
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
        return MEMTX_ERROR;
    }

    *data = ret;
    return MEMTX_OK;
}

static MemTxResult cchip_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned size,
                               MemTxAttrs attrs)
{
    TyphoonState *s = opaque;
    uint64_t oldval, newval;

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
                AlphaCPU *cpu = s->cchip.cpu[i];
                if (cpu != NULL) {
                    CPUState *cs = CPU(cpu);
                    /* IPI can be either cleared or set by the write.  */
                    if (newval & (1 << (i + 8))) {
                        cpu_interrupt(cs, CPU_INTERRUPT_SMP);
                    } else {
                        cpu_reset_interrupt(cs, CPU_INTERRUPT_SMP);
                    }

                    /* ITI can only be cleared by the write.  */
                    if ((newval & (1 << (i + 4))) == 0) {
                        cpu_reset_interrupt(cs, CPU_INTERRUPT_TIMER);
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
        s->cchip.dim[1] = val;
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
        return MEMTX_ERROR;
    }

    return MEMTX_OK;
}

static void dchip_write(void *opaque, hwaddr addr,
                        uint64_t val, unsigned size)
{
    /* Skip this.  It's all related to DRAM timing and setup.  */
}

static MemTxResult pchip_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned size,
                               MemTxAttrs attrs)
{
    TyphoonState *s = opaque;
    uint64_t oldval;

    switch (addr) {
    case 0x0000:
        /* WSBA0: Window Space Base Address Register.  */
        s->pchip.win[0].wba = val & 0xfff00003u;
        break;
    case 0x0040:
        /* WSBA1 */
        s->pchip.win[1].wba = val & 0xfff00003u;
        break;
    case 0x0080:
        /* WSBA2 */
        s->pchip.win[2].wba = val & 0xfff00003u;
        break;
    case 0x00c0:
        /* WSBA3 */
        s->pchip.win[3].wba = (val & 0x80fff00001ull) | 2;
        break;

    case 0x0100:
        /* WSM0: Window Space Mask Register.  */
        s->pchip.win[0].wsm = val & 0xfff00000u;
        break;
    case 0x0140:
        /* WSM1 */
        s->pchip.win[1].wsm = val & 0xfff00000u;
        break;
    case 0x0180:
        /* WSM2 */
        s->pchip.win[2].wsm = val & 0xfff00000u;
        break;
    case 0x01c0:
        /* WSM3 */
        s->pchip.win[3].wsm = val & 0xfff00000u;
        break;

    case 0x0200:
        /* TBA0: Translated Base Address Register.  */
        s->pchip.win[0].tba = val & 0x7fffffc00ull;
        break;
    case 0x0240:
        /* TBA1 */
        s->pchip.win[1].tba = val & 0x7fffffc00ull;
        break;
    case 0x0280:
        /* TBA2 */
        s->pchip.win[2].tba = val & 0x7fffffc00ull;
        break;
    case 0x02c0:
        /* TBA3 */
        s->pchip.win[3].tba = val & 0x7fffffc00ull;
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
        return MEMTX_ERROR;
    }

    return MEMTX_OK;
}

static const MemoryRegionOps cchip_ops = {
    .read_with_attrs = cchip_read,
    .write_with_attrs = cchip_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
};

static const MemoryRegionOps dchip_ops = {
    .read = dchip_read,
    .write = dchip_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
};

static const MemoryRegionOps pchip_ops = {
    .read_with_attrs = pchip_read,
    .write_with_attrs = pchip_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
};

/* A subroutine of typhoon_translate_iommu that builds an IOMMUTLBEntry
   using the given translated address and mask.  */
static bool make_iommu_tlbe(hwaddr taddr, hwaddr mask, IOMMUTLBEntry *ret)
{
    *ret = (IOMMUTLBEntry) {
        .target_as = &address_space_memory,
        .translated_addr = taddr,
        .addr_mask = mask,
        .perm = IOMMU_RW,
    };
    return true;
}

/* A subroutine of typhoon_translate_iommu that handles scatter-gather
   translation, given the address of the PTE.  */
static bool pte_translate(hwaddr pte_addr, IOMMUTLBEntry *ret)
{
    uint64_t pte = address_space_ldq(&address_space_memory, pte_addr,
                                     MEMTXATTRS_UNSPECIFIED, NULL);

    /* Check valid bit.  */
    if ((pte & 1) == 0) {
        return false;
    }

    return make_iommu_tlbe((pte & 0x3ffffe) << 12, 0x1fff, ret);
}

/* A subroutine of typhoon_translate_iommu that handles one of the
   four single-address-cycle translation windows.  */
static bool window_translate(TyphoonWindow *win, hwaddr addr,
                             IOMMUTLBEntry *ret)
{
    uint32_t wba = win->wba;
    uint64_t wsm = win->wsm;
    uint64_t tba = win->tba;
    uint64_t wsm_ext = wsm | 0xfffff;

    /* Check for window disabled.  */
    if ((wba & 1) == 0) {
        return false;
    }

    /* Check for window hit.  */
    if ((addr & ~wsm_ext) != (wba & 0xfff00000u)) {
        return false;
    }

    if (wba & 2) {
        /* Scatter-gather translation.  */
        hwaddr pte_addr;

        /* See table 10-6, Generating PTE address for PCI DMA Address.  */
        pte_addr  = tba & ~(wsm >> 10);
        pte_addr |= (addr & (wsm | 0xfe000)) >> 10;
        return pte_translate(pte_addr, ret);
    } else {
        /* Direct-mapped translation.  */
        return make_iommu_tlbe(tba & ~wsm_ext, wsm_ext, ret);
    }
}

/* Handle PCI-to-system address translation.  */
/* TODO: A translation failure here ought to set PCI error codes on the
   Pchip and generate a machine check interrupt.  */
static IOMMUTLBEntry typhoon_translate_iommu(IOMMUMemoryRegion *iommu,
                                             hwaddr addr,
                                             IOMMUAccessFlags flag,
                                             int iommu_idx)
{
    TyphoonPchip *pchip = container_of(iommu, TyphoonPchip, iommu);
    IOMMUTLBEntry ret;
    int i;

    if (addr <= 0xffffffffu) {
        /* Single-address cycle.  */

        /* Check for the Window Hole, inhibiting matching.  */
        if ((pchip->ctl & 0x20)
            && addr >= 0x80000
            && addr <= 0xfffff) {
            goto failure;
        }

        /* Check the first three windows.  */
        for (i = 0; i < 3; ++i) {
            if (window_translate(&pchip->win[i], addr, &ret)) {
                goto success;
            }
        }

        /* Check the fourth window for DAC disable.  */
        if ((pchip->win[3].wba & 0x80000000000ull) == 0
            && window_translate(&pchip->win[3], addr, &ret)) {
            goto success;
        }
    } else {
        /* Double-address cycle.  */

        if (addr >= 0x10000000000ull && addr < 0x20000000000ull) {
            /* Check for the DMA monster window.  */
            if (pchip->ctl & 0x40) {
                /* See 10.1.4.4; in particular <39:35> is ignored.  */
                make_iommu_tlbe(0, 0x007ffffffffull, &ret);
                goto success;
            }
        }

        if (addr >= 0x80000000000ull && addr <= 0xfffffffffffull) {
            /* Check the fourth window for DAC enable and window enable.  */
            if ((pchip->win[3].wba & 0x80000000001ull) == 0x80000000001ull) {
                uint64_t pte_addr;

                pte_addr  = pchip->win[3].tba & 0x7ffc00000ull;
                pte_addr |= (addr & 0xffffe000u) >> 10;
                if (pte_translate(pte_addr, &ret)) {
                        goto success;
                }
            }
        }
    }

 failure:
    ret = (IOMMUTLBEntry) { .perm = IOMMU_NONE };
 success:
    return ret;
}

static AddressSpace *typhoon_pci_dma_iommu(PCIBus *bus, void *opaque, int devfn)
{
    TyphoonState *s = opaque;
    return &s->pchip.iommu_as;
}

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
        AlphaCPU *cpu = s->cchip.cpu[i];
        if (cpu != NULL) {
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
                cpu_interrupt(CPU(cpu), CPU_INTERRUPT_TIMER);
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
    cpu_interrupt(CPU(s->cchip.cpu[cpu]), CPU_INTERRUPT_TIMER);
}

PCIBus *typhoon_init(MemoryRegion *ram, qemu_irq *p_isa_irq,
                     qemu_irq *p_rtc_irq, AlphaCPU *cpus[4],
                     pci_map_irq_fn sys_map_irq, uint8_t devfn_min)
{
    MemoryRegion *addr_space = get_system_memory();
    DeviceState *dev;
    TyphoonState *s;
    PCIHostState *phb;
    PCIBus *b;
    int i;

    dev = qdev_new(TYPE_TYPHOON_PCI_HOST_BRIDGE);

    s = TYPHOON_PCI_HOST_BRIDGE(dev);
    phb = PCI_HOST_BRIDGE(dev);

    s->cchip.misc = 0x800000000ull; /* Revision: Typhoon.  */
    s->pchip.win[3].wba = 2;        /* Window 3 SG always enabled. */

    /* Remember the CPUs so that we can deliver interrupts to them.  */
    for (i = 0; i < 4; i++) {
        AlphaCPU *cpu = cpus[i];
        s->cchip.cpu[i] = cpu;
        if (cpu != NULL) {
            cpu->alarm_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                                 typhoon_alarm_timer,
                                                 (void *)((uintptr_t)s + i));
        }
    }

    *p_isa_irq = qemu_allocate_irq(typhoon_set_isa_irq, s, 0);
    *p_rtc_irq = qemu_allocate_irq(typhoon_set_timer_irq, s, 0);

    /* Main memory region, 0x00.0000.0000.  Real hardware supports 32GB,
       but the address space hole reserved at this point is 8TB.  */
    memory_region_add_subregion(addr_space, 0, ram);

    /* TIGbus, 0x801.0000.0000, 1GB.  */
    /* ??? The TIGbus is used for delivering interrupts, and access to
       the flash ROM.  I'm not sure that we need to implement it at all.  */

    /* Pchip0 CSRs, 0x801.8000.0000, 256MB.  */
    memory_region_init_io(&s->pchip.region, OBJECT(s), &pchip_ops, s, "pchip0",
                          256 * MiB);
    memory_region_add_subregion(addr_space, 0x80180000000ULL,
                                &s->pchip.region);

    /* Cchip CSRs, 0x801.A000.0000, 256MB.  */
    memory_region_init_io(&s->cchip.region, OBJECT(s), &cchip_ops, s, "cchip0",
                          256 * MiB);
    memory_region_add_subregion(addr_space, 0x801a0000000ULL,
                                &s->cchip.region);

    /* Dchip CSRs, 0x801.B000.0000, 256MB.  */
    memory_region_init_io(&s->dchip_region, OBJECT(s), &dchip_ops, s, "dchip0",
                          256 * MiB);
    memory_region_add_subregion(addr_space, 0x801b0000000ULL,
                                &s->dchip_region);

    /* Pchip0 PCI memory, 0x800.0000.0000, 4GB.  */
    memory_region_init(&s->pchip.reg_mem, OBJECT(s), "pci0-mem", 4 * GiB);
    memory_region_add_subregion(addr_space, 0x80000000000ULL,
                                &s->pchip.reg_mem);

    /* Pchip0 PCI I/O, 0x801.FC00.0000, 32MB.  */
    memory_region_init_io(&s->pchip.reg_io, OBJECT(s), &alpha_pci_ignore_ops,
                          NULL, "pci0-io", 32 * MiB);
    memory_region_add_subregion(addr_space, 0x801fc000000ULL,
                                &s->pchip.reg_io);

    b = pci_register_root_bus(dev, "pci",
                              typhoon_set_irq, sys_map_irq, s,
                              &s->pchip.reg_mem, &s->pchip.reg_io,
                              devfn_min, 64, TYPE_PCI_BUS);
    phb->bus = b;
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    /* Host memory as seen from the PCI side, via the IOMMU.  */
    memory_region_init_iommu(&s->pchip.iommu, sizeof(s->pchip.iommu),
                             TYPE_TYPHOON_IOMMU_MEMORY_REGION, OBJECT(s),
                             "iommu-typhoon", UINT64_MAX);
    address_space_init(&s->pchip.iommu_as, MEMORY_REGION(&s->pchip.iommu),
                       "pchip0-pci");
    pci_setup_iommu(b, typhoon_pci_dma_iommu, s);

    /* Pchip0 PCI special/interrupt acknowledge, 0x801.F800.0000, 64MB.  */
    memory_region_init_io(&s->pchip.reg_iack, OBJECT(s), &alpha_pci_iack_ops,
                          b, "pci0-iack", 64 * MiB);
    memory_region_add_subregion(addr_space, 0x801f8000000ULL,
                                &s->pchip.reg_iack);

    /* Pchip0 PCI configuration, 0x801.FE00.0000, 16MB.  */
    memory_region_init_io(&s->pchip.reg_conf, OBJECT(s), &alpha_pci_conf1_ops,
                          b, "pci0-conf", 16 * MiB);
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

    return b;
}

static const TypeInfo typhoon_pcihost_info = {
    .name          = TYPE_TYPHOON_PCI_HOST_BRIDGE,
    .parent        = TYPE_PCI_HOST_BRIDGE,
    .instance_size = sizeof(TyphoonState),
};

static void typhoon_iommu_memory_region_class_init(ObjectClass *klass,
                                                   void *data)
{
    IOMMUMemoryRegionClass *imrc = IOMMU_MEMORY_REGION_CLASS(klass);

    imrc->translate = typhoon_translate_iommu;
}

static const TypeInfo typhoon_iommu_memory_region_info = {
    .parent = TYPE_IOMMU_MEMORY_REGION,
    .name = TYPE_TYPHOON_IOMMU_MEMORY_REGION,
    .class_init = typhoon_iommu_memory_region_class_init,
};

static void typhoon_register_types(void)
{
    type_register_static(&typhoon_pcihost_info);
    type_register_static(&typhoon_iommu_memory_region_info);
}

type_init(typhoon_register_types)
