/*
 * QEMU NCR 53C710 SCSI I/O Processor emulation
 *
 * Copyright (c) 2026 Keith Monahan <keith@techtravels.org>
 *
 * Interface to the 53C710 model (ncr53c710.c).  The only instantiated device is
 * the LASI wrapper in lasi_ncr710.c, which embeds one NCR710State by value and
 * drives it through the functions declared here.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_NCR53C710_H
#define HW_NCR53C710_H

#include "qemu/queue.h"
#include "hw/scsi/scsi.h"
#include "system/memory.h"
#include "qemu/timer.h"
#include "migration/vmstate.h"
#include "lsi_scsi.h"

/* Register offsets (little endian addressing; the chip is internally LE). */
#define NCR710_SCNTL0   0x00
#define NCR710_SCNTL1   0x01
#define NCR710_SDID     0x02
#define NCR710_SIEN     0x03
#define NCR710_SCID     0x04
#define NCR710_SXFER    0x05
#define NCR710_SODL     0x06
#define NCR710_SOCL     0x07
#define NCR710_SFBR     0x08
#define NCR710_SIDL     0x09
#define NCR710_SBDL     0x0a
#define NCR710_SBCL     0x0b
#define NCR710_DSTAT    0x0c
#define NCR710_SSTAT0   0x0d
#define NCR710_SSTAT1   0x0e
#define NCR710_SSTAT2   0x0f
#define NCR710_DSA      0x10
#define NCR710_CTEST0   0x14
#define NCR710_CTEST1   0x15
#define NCR710_CTEST2   0x16
#define NCR710_CTEST3   0x17
#define NCR710_CTEST4   0x18
#define NCR710_CTEST5   0x19
#define NCR710_CTEST6   0x1a
#define NCR710_CTEST7   0x1b
#define NCR710_TEMP     0x1c
#define NCR710_DFIFO    0x20
#define NCR710_ISTAT    0x21
#define NCR710_CTEST8   0x22
#define NCR710_LCRC     0x23
#define NCR710_DBC      0x24
#define NCR710_DCMD     0x27
#define NCR710_DNAD     0x28
#define NCR710_DSP      0x2c
#define NCR710_DSPS     0x30
#define NCR710_SCRATCH  0x34
#define NCR710_DMODE    0x38
#define NCR710_DIEN     0x39
#define NCR710_DWT      0x3a
#define NCR710_DCNTL    0x3b
#define NCR710_ADDER    0x3c

/* DMODE bit 4: Function Code 1 (FC1), kept by an ISTAT.RST soft reset. */
#define NCR710_DMODE_FC1  0x10
/* DCNTL bit 5: Enable Acknowledge (EA), kept by an ISTAT.RST soft reset. */
#define NCR710_DCNTL_EA   0x20

/* The chip revision reported in CTEST8[7:4]. */
#define NCR710_CHIP_REVISION 0x2

typedef struct NCR710State {
    /* Wiring filled in by the LASI wrapper before ncr710_soft_reset(). */
    SCSIBus bus;
    AddressSpace *as;
    qemu_irq irq;
    /*
     * SCRIPTS yield/resume timer, mirroring lsi53c895a's scripts_timer.  The
     * wrapper creates it.
     */
    QEMUTimer *scripts_timer;

    /* SCRIPTS engine working state (not directly register mapped). */
    int carry;
    int status;
    int msg_action;
    int msg_len;
    uint8_t msg[LSI_MAX_MSGIN_LEN];
    int waiting;
    int current_lun;
    uint32_t select_tag;
    int command_complete;
    bool script_running;
    /*
     * A SIGP arrived while the engine was halted instead of parked at Wait
     * Reselect; the Wait Reselect handler consumes it when the engine next
     * parks.
     */
    bool sigp_pending_resume;
    /* A SCSI continue was deferred to lsi_issue_bh. */
    bool issue_pending;
    lsi_request *current;
    /* Disconnected tagged commands awaiting reselection. */
    QTAILQ_HEAD(, lsi_request) queue;

    /*
     * Registers (53C710 layout).  Several (e.g. sodl/sidl/sbdl/lcrc/ctest*) are
     * readback/scratch only, with no modelled side effects.
     */
    uint8_t scntl0;
    uint8_t scntl1;
    uint8_t sdid;
    uint8_t sien;
    uint8_t scid;
    uint8_t sxfer;
    uint8_t sodl;
    uint8_t socl;
    uint8_t sfbr;
    uint8_t sidl;
    uint8_t sbdl;
    uint8_t sbcl;
    uint8_t dstat;
    uint8_t sstat0;
    uint8_t sstat1;
    uint8_t sstat2;
    uint32_t dsa;
    uint8_t ctest0;
    uint8_t ctest3;
    uint8_t ctest4;
    uint8_t ctest5;
    uint8_t ctest7;
    uint32_t temp;
    uint8_t dfifo;
    uint8_t istat;
    uint8_t lcrc;
    uint32_t dbc;
    uint8_t dcmd;
    uint32_t dnad;
    uint32_t dsp;
    uint32_t dsps;
    uint32_t scratch;
    uint8_t dmode;
    uint8_t dien;
    uint8_t dwt;
    uint8_t dcntl;
    uint32_t adder;
} NCR710State;

static inline NCR710State *ncr710_from_scsi_bus(SCSIBus *bus)
{
    return container_of(bus, NCR710State, bus);
}

/* Interface used by the LASI wrapper (lasi_ncr710.c). */
uint64_t ncr710_reg_read(NCR710State *s, hwaddr addr, unsigned size);
void ncr710_reg_write(NCR710State *s, hwaddr addr, uint64_t val, unsigned size);
void ncr710_soft_reset(NCR710State *s);
void ncr710_request_cancelled(SCSIRequest *req);
void ncr710_command_complete(SCSIRequest *req, size_t resid);
void ncr710_transfer_data(SCSIRequest *req, uint32_t len);
void ncr710_scripts_timer_callback(void *opaque);
extern const VMStateDescription vmstate_ncr710;

#endif /* HW_NCR53C710_H */
