// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef HW_ISA_PC87560_H
#define HW_ISA_PC87560_H

#define TYPE_PC87560_Superio  "pc87560-superio"
OBJECT_DECLARE_SIMPLE_TYPE(PC87560SuperioState, PC87560_Superio)

#define REG_FXBAR0      0x40
#define REG_FXBAR1      0x44
#define REG_FXBAR2      0x48
#define REG_DDMA_BAR    0x4C
#define REG_FIR_BAR     0x50
#define REG_SYSCFG      0x58
#define REG_FUNCEN      0x5A
#define REG_SIOCFG      0x5C
#define REG_SPECFEN     0x5D
#define REG_PPDID       0x5E
#define REG_PPMODE      0x5F
#define REG_DMA_ROUTE4  0x61
#define REG_ROMCFG      0x62
#define REG_DMA_ROUTE1  0x63
#define REG_DMA_ROUTE2  0x64
#define REG_DMA_ROUTE3  0x65
#define REG_DMA_CHAN    0x66
#define REG_IRQ_TRIG    0x67
#define REG_IRQ_ROUTE1  0x69
#define REG_ARBCTL      0x72
#define REG_FX_MASK2    0x74
#define REG_FX_TIMING   0x76
#define REG_FXCTL       0x78
#define REG_GPIO_CFG    0x79
#define REG_GPIO_DIR    0x7A
#define REG_SIRQ_CTL    0x7B
#define REG_SIRQ_EN     0x7C
#define REG_RSVD_CFG    0x7E
#define REG_AUDIOCS_EN  0x7F
#define REG_FXMEM_CTL1  0x80
#define REG_KBCBAR      0x84
#define REG_ACPIBAR     0x88
#define REG_PMBAR       0x8C
#define REG_FDCBAR      0x90
#define REG_SP1BAR      0x94
#define REG_SP2BAR      0x98
#define REG_PPBAR       0x9C
#define REG_TEST_CTL    0xFF

#define FUNCEN_FDC  (1 << 0)
#define FUNCEN_SP1  (1 << 1)
#define FUNCEN_SP2  (1 << 3)
#define FUNCEN_PP   (1 << 5)
#define FUNCEN_KBC  (1 << 13)

#define FUNCEN_DEFAULT  0xFFBF

#define IC_PIC1  0x20

typedef struct {
    uint8_t imr;       /* interrupt mask register (OCW1) */
    uint8_t irr;       /* interrupt request register    */
    uint8_t isr;       /* in-service register           */
    bool    poll_mode; /* OCW3 POLL bit set             */
    bool    read_isr;  /* OCW3: true=ISR, false=IRR     */
    int     init_phase;/* 0=normal, 1-3=ICW2/3/4 expected */
    int     priority_base;
    int     irq_level;
} Pic8259;

struct PC87560SuperioState {
    PCIDevice parent_obj;

    qemu_irq parent_irq;

    SerialState   serial[2];
    ParallelState pp;
    FDCtrl        fdc;

    MemoryRegion fdc_io;
    MemoryRegion sp1_io;
    MemoryRegion sp2_io;
    MemoryRegion pp_io;
    MemoryRegion kbc_io;
    MemoryRegion acpi_io;
    MemoryRegion pm_io;
    MemoryRegion pic1_io;

    bool fdc_mapped;
    bool sp1_mapped;
    bool sp2_mapped;
    bool pp_mapped;
    bool kbc_mapped;
    bool acpi_mapped;
    bool pm_mapped;
    Pic8259 pic;
};

#endif
