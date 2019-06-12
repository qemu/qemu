/*
 * QEMU ICH9 TCO emulation
 *
 * Copyright (c) 2015 Paulo Alcantara <pcacjr@zytor.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef HW_ACPI_TCO_H
#define HW_ACPI_TCO_H


/* As per ICH9 spec, the internal timer has an error of ~0.6s on every tick */
#define TCO_TICK_NSEC 600000000LL

/* TCO I/O register offsets */
enum {
    TCO_RLD           = 0x00,
    TCO_DAT_IN        = 0x02,
    TCO_DAT_OUT       = 0x03,
    TCO1_STS          = 0x04,
    TCO2_STS          = 0x06,
    TCO1_CNT          = 0x08,
    TCO2_CNT          = 0x0a,
    TCO_MESSAGE1      = 0x0c,
    TCO_MESSAGE2      = 0x0d,
    TCO_WDCNT         = 0x0e,
    SW_IRQ_GEN        = 0x10,
    TCO_TMR           = 0x12,
};

/* TCO I/O register control/status bits */
enum {
    SW_TCO_SMI           = 1 << 1,
    TCO_INT_STS          = 1 << 2,
    TCO_LOCK             = 1 << 12,
    TCO_TMR_HLT          = 1 << 11,
    TCO_TIMEOUT          = 1 << 3,
    TCO_SECOND_TO_STS    = 1 << 1,
    TCO_BOOT_STS         = 1 << 2,
};

/* TCO I/O registers mask bits */
enum {
    TCO_RLD_MASK     = 0x3ff,
    TCO1_STS_MASK    = 0xe870,
    TCO2_STS_MASK    = 0xfff8,
    TCO1_CNT_MASK    = 0xfeff,
    TCO_TMR_MASK     = 0x3ff,
};

typedef struct TCOIORegs {
    struct {
        uint16_t rld;
        uint8_t din;
        uint8_t dout;
        uint16_t sts1;
        uint16_t sts2;
        uint16_t cnt1;
        uint16_t cnt2;
        uint8_t msg1;
        uint8_t msg2;
        uint8_t wdcnt;
        uint16_t tmr;
    } tco;
    uint8_t sw_irq_gen;

    QEMUTimer *tco_timer;
    int64_t expire_time;
    uint8_t timeouts_no;

    MemoryRegion io;
} TCOIORegs;

/* tco.c */
void acpi_pm_tco_init(TCOIORegs *tr, MemoryRegion *parent);

extern const VMStateDescription vmstate_tco_io_sts;

#endif /* HW_ACPI_TCO_H */
