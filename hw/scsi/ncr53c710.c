/*
 * QEMU NCR710 SCSI Controller
 *
 * Copyright (c) 2025 Soumyajyotii Ssarkar <soumyajyotisarkar23@gmail.com>
 * This driver was developed during the Google Summer of Code 2025 program.
 *
 * NCR710 SCSI Controller implementation
 * Based on the NCR53C710 Technical Manual Version 3.2, December 2000
 *
 * Developed from an implementation of NCR53C710 by Helge Deller
 * which was interim based on the implementation by Toni Wilen for UAE.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Contents:
 * 1. Register Definitions
 * 2. Register name functions
 * 3. Parity functions
 * 4. SCSI FIFO Structures
 * 5. Scripts Misc functions
 * 6. DMA functions
 * 7. Scripts functions
 * 8. Read and Write functions
 * 9. QEMU Device model functions
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/timer.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "hw/scsi/scsi.h"
#include "hw/scsi/ncr53c710.h"
#include "migration/vmstate.h"
#include "system/dma.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "trace.h"
#include "qom/object.h"

#define NCR710_MAX_DEVS 7

/* SCNTL0 (0x00) - SCSI Control Register 0 */
#define NCR710_SCNTL0_TRG    0x01
#define NCR710_SCNTL0_AAP    0x02
#define NCR710_SCNTL0_EPG    0x04
#define NCR710_SCNTL0_EPC    0x08
#define NCR710_SCNTL0_WATN   0x10
#define NCR710_SCNTL0_START  0x20
#define NCR710_SCNTL0_ARB0   0x40
#define NCR710_SCNTL0_ARB1   0x80

/* SCNTL1 (0x01) - SCSI Control Register 1 */
#define NCR710_SCNTL1_RES0   0x01
#define NCR710_SCNTL1_RES1   0x02
#define NCR710_SCNTL1_AESP   0x04
#define NCR710_SCNTL1_RST    0x08
#define NCR710_SCNTL1_CON    0x10
#define NCR710_SCNTL1_ESR    0x20
#define NCR710_SCNTL1_ADB    0x40
#define NCR710_SCNTL1_EXC    0x80

/* ISTAT (0x21) - Interrupt Status Register */
#define NCR710_ISTAT_DIP    0x01
#define NCR710_ISTAT_SIP    0x02
#define NCR710_ISTAT_CON    0x08
#define NCR710_ISTAT_SIGP   0x20
#define NCR710_ISTAT_RST    0x40
#define NCR710_ISTAT_ABRT   0x80

/* SSTAT0 (0x0D) - SCSI Status Register 0 */
#define NCR710_SSTAT0_PAR    0x01
#define NCR710_SSTAT0_RST    0x02
#define NCR710_SSTAT0_UDC    0x04
#define NCR710_SSTAT0_SGE    0x08
#define NCR710_SSTAT0_SEL    0x10
#define NCR710_SSTAT0_STO    0x20
#define NCR710_SSTAT0_FCMP   0x40
#define NCR710_SSTAT0_MA     0x80

/* SSTAT1 (0x0E) - SCSI Status Register 1 */
#define NCR710_SSTAT1_ORF    0x02
#define NCR710_SSTAT1_ILF    0x04

/* SSTAT2 (0x0F) - SCSI Status Register 2 */
#define NCR710_SSTAT2_FF0    0x01
#define NCR710_SSTAT2_FF1    0x02
#define NCR710_SSTAT2_FF2    0x04
#define NCR710_SSTAT2_FF3    0x08

/* SOCL (0x07) / SBCL (0x0B) - SCSI Output/Bus Control Lines */
#define NCR710_SOCL_IO       0x01
#define NCR710_SOCL_CD       0x02
#define NCR710_SOCL_MSG      0x04
#define NCR710_SOCL_ATN      0x08
#define NCR710_SOCL_SEL      0x10
#define NCR710_SOCL_BSY      0x20
#define NCR710_SOCL_ACK      0x40
#define NCR710_SOCL_REQ      0x80

/* SBCL bits same as SOCL */
#define NCR710_SBCL_IO       0x01
#define NCR710_SBCL_CD       0x02
#define NCR710_SBCL_MSG      0x04
#define NCR710_SBCL_ATN      0x08
#define NCR710_SBCL_SEL      0x10
#define NCR710_SBCL_BSY      0x20
#define NCR710_SBCL_ACK      0x40
#define NCR710_SBCL_REQ      0x80

/* DSTAT (0x0C) - DMA Status Register */
#define NCR710_DSTAT_IID     0x01
#define NCR710_DSTAT_SIR     0x04
#define NCR710_DSTAT_SSI     0x08
#define NCR710_DSTAT_ABRT    0x10
#define NCR710_DSTAT_BF      0x20
#define NCR710_DSTAT_MDPE    0x40
#define NCR710_DSTAT_DFE     0x80

/* DCNTL (0x3B) - DMA Control Register */
#define NCR710_DCNTL_COM     0x01
#define NCR710_DCNTL_IRQD    0x02
#define NCR710_DCNTL_STD     0x04
#define NCR710_DCNTL_IRQM    0x08
#define NCR710_DCNTL_SSM     0x10
#define NCR710_DCNTL_PFEN    0x20
#define NCR710_DCNTL_PFF     0x40

/* DMODE (0x38) - DMA Mode Register */
#define NCR710_DMODE_MAN     0x01
#define NCR710_DMODE_BOF     0x02
#define NCR710_DMODE_ERMP    0x04
#define NCR710_DMODE_ERL     0x08
#define NCR710_DMODE_DIOM    0x10
#define NCR710_DMODE_SIOM    0x20
#define NCR710_DMODE_BL_MASK 0xC0
#define NCR710_DMODE_BL_1    0x00
#define NCR710_DMODE_BL_2    0x40
#define NCR710_DMODE_BL_4    0x80
#define NCR710_DMODE_BL_8    0xC0

/* CTEST2 (0x16) - Chip Test Register 2 */
#define NCR710_CTEST2_DACK   0x01
#define NCR710_CTEST2_DREQ   0x02
#define NCR710_CTEST2_TEOP   0x04
#define NCR710_CTEST2_PCICIE 0x08
#define NCR710_CTEST2_CM     0x10
#define NCR710_CTEST2_CIO    0x20
#define NCR710_CTEST2_SIGP   0x40
#define NCR710_CTEST2_DDIR   0x80

/* CTEST5 (0x19) - Chip Test Register 5 */
#define NCR710_CTEST5_BL2    0x04
#define NCR710_CTEST5_DDIR   0x08
#define NCR710_CTEST5_MASR   0x10
#define NCR710_CTEST5_DFSN   0x20
#define NCR710_CTEST5_BBCK   0x40
#define NCR710_CTEST5_ADCK   0x80

/* SCID (0x04) - SCSI Chip ID Register */
#define NCR710_SCID_RRE      0x60
#define NCR710_SCID_ID_MASK  0x07

#define NCR710_HOST_ID       7

/* NCR53C710 has 8-byte SCSI FIFO */
#define NCR710_MAX_MSGIN_LEN 8
#define NCR710_BUF_SIZE         4096

/* Standard SCSI Message Byte Constants */
#define SCSI_MSG_ABORT                      0x06
#define SCSI_MSG_BUS_DEVICE_RESET           0x0c
#define SCSI_MSG_COMMAND_COMPLETE           0x00
#define SCSI_MSG_DISCONNECT                 0x04
#define SCSI_MSG_EXTENDED_MESSAGE           0x01
#define SCSI_MSG_IDENTIFY                   0x80
#define SCSI_MSG_IGNORE_WIDE_RESIDUE        0x23
#define SCSI_MSG_MESSAGE_PARITY_ERROR       0x09
#define SCSI_MSG_MESSAGE_REJECT             0x07
#define SCSI_MSG_NO_OPERATION               0x08
#define SCSI_MSG_RELEASE_RECOVERY           0x10
#define SCSI_MSG_RESTORE_POINTERS           0x03
#define SCSI_MSG_SAVE_DATA_POINTER          0x02
#define SCSI_MSG_SYNCHRONOUS_DATA_TRANSFER  0x01
#define SCSI_MSG_WIDE_DATA_TRANSFER         0x03

/* Script interrupt codes */
#define A_GOOD_STATUS_AFTER_STATUS          0x401
#define A_DISCONNECT_AFTER_CMD              0x380
#define A_DISCONNECT_AFTER_DATA             0x580
#define A_DISCONNECT_DURING_DATA            0x780
#define A_RESELECTION_IDENTIFIED            0x1003
#define A_UNEXPECTED_PHASE                  0x20
#define A_FATAL                             0x2000
#define A_DEBUG_INTERRUPT                   0x3000

/* SCSI Script execution states */
#define SCRIPT_STATE_IDLE                  0
#define SCRIPT_STATE_SELECTING             1
#define SCRIPT_STATE_COMMAND               2
#define SCRIPT_STATE_DATA                  3
#define SCRIPT_STATE_STATUS                4
#define SCRIPT_STATE_MESSAGE               5
#define SCRIPT_STATE_DISCONNECTED          6

#define AFTER_SELECTION         0x100
#define BEFORE_CMD              0x200
#define AFTER_CMD               0x300
#define AFTER_STATUS            0x400
#define AFTER_DATA_IN           0x500
#define AFTER_DATA_OUT          0x600
#define DURING_DATA_IN          0x700

#define NOT_MSG_OUT             0x10
#define UNEXPECTED_PHASE        0x20
#define NOT_MSG_IN              0x30
#define UNEXPECTED_MSG          0x40
#define MSG_IN                  0x50
#define SDTR_MSG_R              0x60
#define REJECT_MSG_R            0x70
#define DISCONNECT              0x80
#define MSG_OUT                 0x90
#define WDTR_MSG_R              0xA0

#define GOOD_STATUS             0x1

#define NOT_MSG_OUT_AFTER_SELECTION         0x110
#define UNEXPECTED_PHASE_BEFORE_CMD         0x220
#define UNEXPECTED_PHASE_AFTER_CMD          0x320
#define NOT_MSG_IN_AFTER_STATUS             0x430
#define GOOD_STATUS_AFTER_STATUS            0x401
#define UNEXPECTED_PHASE_AFTER_DATA_IN      0x520
#define UNEXPECTED_PHASE_AFTER_DATA_OUT     0x620
#define UNEXPECTED_MSG_BEFORE_CMD           0x240
#define MSG_IN_BEFORE_CMD                   0x250
#define MSG_IN_AFTER_CMD                    0x350
#define SDTR_MSG_BEFORE_CMD                 0x260
#define REJECT_MSG_BEFORE_CMD               0x270
#define DISCONNECT_AFTER_CMD                0x380
#define SDTR_MSG_AFTER_CMD                  0x360
#define WDTR_MSG_AFTER_CMD                  0x3A0
#define MSG_IN_AFTER_STATUS                 0x440
#define DISCONNECT_AFTER_DATA               0x580
#define MSG_IN_AFTER_DATA_IN                0x550
#define MSG_IN_AFTER_DATA_OUT               0x650
#define MSG_OUT_AFTER_DATA_IN               0x590
#define DATA_IN_AFTER_DATA_IN               0x5a0
#define MSG_IN_DURING_DATA_IN               0x750
#define DISCONNECT_DURING_DATA              0x780

#define RESELECTED_DURING_SELECTION      0x1000
#define COMPLETED_SELECTION_AS_TARGET    0x1001
#define RESELECTION_IDENTIFIED           0x1003

#define FATAL                            0x2000
#define FATAL_UNEXPECTED_RESELECTION_MSG 0x2000
#define FATAL_SEND_MSG                   0x2001
#define FATAL_NOT_MSG_IN_AFTER_SELECTION 0x2002
#define FATAL_ILLEGAL_MSG_LENGTH         0x2003

#define DEBUG_INTERRUPT     0x3000
#define DEBUG_INTERRUPT1    0x3001
#define DEBUG_INTERRUPT2    0x3002
#define DEBUG_INTERRUPT3    0x3003
#define DEBUG_INTERRUPT4    0x3004
#define DEBUG_INTERRUPT5    0x3005
#define DEBUG_INTERRUPT6    0x3006

#define COMMAND_COMPLETE_MSG      0x00
#define EXTENDED_MSG              0x01
#define SDTR_MSG                  0x01
#define SAVE_DATA_PTRS_MSG        0x02
#define RESTORE_DATA_PTRS_MSG     0x03
#define WDTR_MSG                  0x03
#define DISCONNECT_MSG            0x04
#define REJECT_MSG                0x07
#define PARITY_ERROR_MSG          0x09
#define SIMPLE_TAG_MSG            0x20
#define IDENTIFY_MSG              0x80
#define IDENTIFY_MSG_MASK         0x7F
#define TWO_BYTE_MSG              0x20
#define TWO_BYTE_MSG_MASK         0x0F

/* SCSI phases */
#define PHASE_DO   0  /* Data out phase */
#define PHASE_DI   1  /* Data in phase */
#define PHASE_CO   2  /* Command phase */
#define PHASE_SI   3  /* Status phase */
#define PHASE_ST   3  /* Status phase (alias) */
#define PHASE_MO   6  /* Message out phase */
#define PHASE_MI   7  /* Message in phase */
#define PHASE_MASK 7  /* Mask for phase bits */

#define NCR710_TAG_VALID     (1 << 16)

static void ncr710_scsi_fifo_init(NCR710_SCSI_FIFO *fifo);
static const char *ncr710_reg_name(int offset);
static void ncr710_script_scsi_interrupt(NCR710State *s, int stat0);
static void ncr710_update_irq(NCR710State *s);
static void ncr710_script_dma_interrupt(NCR710State *s, int stat);
static void ncr710_request_free(NCR710State *s, NCR710Request *p);
static inline void ncr710_dma_read(NCR710State *s, uint32_t addr,
                                   void *buf, uint32_t len);
static inline void ncr710_dma_write(NCR710State *s, uint32_t addr,
                                    const void *buf, uint32_t len);
static uint8_t ncr710_reg_readb(NCR710State *s, int offset);
static void ncr710_reg_writeb(NCR710State *s, int offset, uint8_t val);


static inline int ncr710_irq_on_rsl(NCR710State *s)
{
    return (s->sien0 & NCR710_SSTAT0_SEL) != 0;
}

static void ncr710_clear_pending_irq(NCR710State *s)
{
    if (s->current) {
        if (s->current->req) {
            s->current->req->hba_private = NULL;
        }
        ncr710_request_free(s, s->current);
        s->current = NULL;
    }
}

void ncr710_soft_reset(NCR710State *s)
{
    trace_ncr710_reset();
    s->carry = 0;
    s->msg_action = NCR710_MSG_ACTION_NONE;
    s->msg_len = 0;
    s->waiting = NCR710_WAIT_NONE;
    s->wait_reselect = false;
    s->reselection_id = 0;
    s->dsa = 0;
    s->dnad = 0;
    s->dbc = 0;
    s->temp = 0;
    s->scratch = 0;
    s->istat &= 0x40;
    s->dcmd = 0x40;
    s->dstat = NCR710_DSTAT_DFE;
    s->dien = 0x04;
    s->sien0 = 0;
    s->ctest2 = NCR710_CTEST2_DACK;
    s->ctest3 = 0;
    s->ctest4 = 0;
    s->ctest5 = 0;
    s->dsp = 0;
    s->dsps = 0;
    s->dmode = 0;
    s->dcntl = 0;
    s->scntl0 = 0xc0;
    s->scntl1 = 0;
    s->sstat0 = 0;
    s->sstat1 = 0;
    s->sstat2 = 0;
    s->scid = 0x80;
    s->sxfer = 0;
    s->socl = 0;
    s->sdid = 0;
    s->sbcl = 0;
    s->sidl = 0;
    s->sfbr = 0;
    qemu_set_irq(s->irq, 0);
    ncr710_clear_pending_irq(s);
    ncr710_scsi_fifo_init(&s->scsi_fifo);
}

static const char *ncr710_reg_name(int offset)
{
    switch (offset) {
    case NCR710_SCNTL0_REG:      return "SCNTL0";
    case NCR710_SCNTL1_REG:      return "SCNTL1";
    case NCR710_SDID_REG:        return "SDID";
    case NCR710_SIEN_REG:        return "SIEN";
    case NCR710_SCID_REG:        return "SCID";
    case NCR710_SXFER_REG:       return "SXFER";
    case NCR710_SODL_REG:        return "SODL";
    case NCR710_SOCL_REG:        return "SOCL";
    case NCR710_SFBR_REG:        return "SFBR";
    case NCR710_SIDL_REG:        return "SIDL";
    case NCR710_SBDL_REG:        return "SBDL";
    case NCR710_SBCL_REG:        return "SBCL";
    case NCR710_DSTAT_REG:       return "DSTAT";
    case NCR710_SSTAT0_REG:      return "SSTAT0";
    case NCR710_SSTAT1_REG:      return "SSTAT1";
    case NCR710_SSTAT2_REG:      return "SSTAT2";
    case NCR710_DSA_REG:         return "DSA";
    case NCR710_DSA_REG + 1:     return "DSA+1";
    case NCR710_DSA_REG + 2:     return "DSA+2";
    case NCR710_DSA_REG + 3:     return "DSA+3";
    case NCR710_CTEST0_REG:      return "CTEST0";
    case NCR710_CTEST1_REG:      return "CTEST1";
    case NCR710_CTEST2_REG:      return "CTEST2";
    case NCR710_CTEST3_REG:      return "CTEST3";
    case NCR710_CTEST4_REG:      return "CTEST4";
    case NCR710_CTEST5_REG:      return "CTEST5";
    case NCR710_CTEST6_REG:      return "CTEST6";
    case NCR710_CTEST7_REG:      return "CTEST7";
    case NCR710_TEMP_REG:        return "TEMP";
    case NCR710_TEMP_REG + 1:    return "TEMP+1";
    case NCR710_TEMP_REG + 2:    return "TEMP+2";
    case NCR710_TEMP_REG + 3:    return "TEMP+3";
    case NCR710_DFIFO_REG:       return "DFIFO";
    case NCR710_ISTAT_REG:       return "ISTAT";
    case NCR710_CTEST8_REG:      return "CTEST8";
    case NCR710_LCRC_REG:        return "LCRC";
    case NCR710_DBC_REG:         return "DBC";
    case NCR710_DBC_REG + 1:     return "DBC+1";
    case NCR710_DBC_REG + 2:     return "DBC+2";
    case NCR710_DCMD_REG:        return "DCMD";
    case NCR710_DNAD_REG:        return "DNAD";
    case NCR710_DNAD_REG + 1:    return "DNAD+1";
    case NCR710_DNAD_REG + 2:    return "DNAD+2";
    case NCR710_DNAD_REG + 3:    return "DNAD+3";
    case NCR710_DSP_REG:         return "DSP";
    case NCR710_DSP_REG + 1:     return "DSP+1";
    case NCR710_DSP_REG + 2:     return "DSP+2";
    case NCR710_DSP_REG + 3:     return "DSP+3";
    case NCR710_DSPS_REG:        return "DSPS";
    case NCR710_DSPS_REG + 1:    return "DSPS+1";
    case NCR710_DSPS_REG + 2:    return "DSPS+2";
    case NCR710_DSPS_REG + 3:    return "DSPS+3";
    case NCR710_SCRATCH_REG:     return "SCRATCH";
    case NCR710_SCRATCH_REG + 1: return "SCRATCH+1";
    case NCR710_SCRATCH_REG + 2: return "SCRATCH+2";
    case NCR710_SCRATCH_REG + 3: return "SCRATCH+3";
    case NCR710_DMODE_REG:       return "DMODE";
    case NCR710_DIEN_REG:        return "DIEN";
    case NCR710_DWT_REG:         return "DWT";
    case NCR710_DCNTL_REG:       return "DCNTL";
    case NCR710_ADDER_REG:       return "ADDER";
    case NCR710_ADDER_REG + 1:   return "ADDER+1";
    case NCR710_ADDER_REG + 2:   return "ADDER+2";
    case NCR710_ADDER_REG + 3:   return "ADDER+3";
    default:                     return "UNKNOWN";
    }
}

static uint8_t ncr710_generate_scsi_parity(NCR710State *s, uint8_t data)
{
    uint8_t parity = parity8(data);

    if (s->scntl1 & NCR710_SCNTL1_AESP) {
        parity = !parity;
    }

    return parity;
}

static bool ncr710_check_scsi_parity(NCR710State *s, uint8_t data,
                                     uint8_t parity)
{
    if (!(s->scntl0 & NCR710_SCNTL0_EPC)) {
        return true;
    }

    uint8_t expected_parity = ncr710_generate_scsi_parity(s, data);
    return parity == expected_parity;
}

static void ncr710_handle_parity_error(NCR710State *s)
{
    s->sstat0 |= NCR710_SSTAT0_PAR;

    /* If parity error ATN is enabled, assert ATN */
    if (s->scntl0 & NCR710_SCNTL0_AAP) {
        s->socl |= NCR710_SOCL_ATN;
    }

    ncr710_script_scsi_interrupt(s, NCR710_SSTAT0_PAR);
}

/*
 * NCR710 SCSI FIFO IMPLEMENTATION
 *
 * Hardware Specifications (NCR53C710 datasheet):
 * - Width: 9 bits (8 data bits + 1 parity bit)
 * - Depth: 8 bytes
 * - Type: Circular buffer
 *
 * Implementation:
 * - Enqueue: Add byte at tail position ((head + count) % 8)
 * - Dequeue: Remove byte from head position, advance head
 * - Status: Empty (count=0), Full (count=8)
 *
 * FIFO Operations:
 * - ncr710_scsi_fifo_init()    - Reset FIFO to empty state
 * - ncr710_scsi_fifo_enqueue() - Add byte with parity to tail
 * - ncr710_scsi_fifo_dequeue() - Remove byte with parity from head
 * - ncr710_scsi_fifo_empty()   - Check if FIFO is empty
 * - ncr710_scsi_fifo_full()    - Check if FIFO is full
 */

static void ncr710_scsi_fifo_init(NCR710_SCSI_FIFO *fifo)
{
    memset(fifo->data, 0, NCR710_SCSI_FIFO_SIZE);
    memset(fifo->parity, 0, NCR710_SCSI_FIFO_SIZE);
    fifo->head = 0;
    fifo->count = 0;
}

static inline bool ncr710_scsi_fifo_empty(NCR710_SCSI_FIFO *fifo)
{
    return fifo->count == 0;
}

static inline bool ncr710_scsi_fifo_full(NCR710_SCSI_FIFO *fifo)
{
    return fifo->count == NCR710_SCSI_FIFO_SIZE;
}

static inline int ncr710_scsi_fifo_enqueue(NCR710_SCSI_FIFO *fifo,
                                           uint8_t data, uint8_t parity)
{
    if (ncr710_scsi_fifo_full(fifo)) {
        return -1; /* FIFO full - 8 transfers deep */
    }

    /* Add data at the tail (head + count) */
    int tail_pos = (fifo->head + fifo->count) % NCR710_SCSI_FIFO_SIZE;
    fifo->data[tail_pos] = data;
    fifo->parity[tail_pos] = parity;
    fifo->count++;

    return 0;
}

static inline uint8_t ncr710_scsi_fifo_dequeue(NCR710_SCSI_FIFO *fifo,
                                                uint8_t *parity)
{
    uint8_t data;

    if (ncr710_scsi_fifo_empty(fifo)) {
        *parity = 0;
        return 0; /* FIFO empty */
    }

    /* Taking data from the head position */
    data = fifo->data[fifo->head];
    *parity = fifo->parity[fifo->head];
    fifo->head = (fifo->head + 1) % NCR710_SCSI_FIFO_SIZE;
    fifo->count--;

    return data;
}

static inline uint32_t ncr710_read_dword(NCR710State *s, uint32_t addr)
{
    uint32_t buf;
    address_space_read(s->as, addr, MEMTXATTRS_UNSPECIFIED,
                       (uint8_t *)&buf, 4);
    /*
     * The NCR710 datasheet saying "operates internally in LE mode"
     * refers to its internal register organization,
     * not how it reads SCRIPTS from host memory.
     */
    buf = be32_to_cpu(buf);
    NCR710_DPRINTF("Read dword %08x from %08x\n", buf, addr);
    return buf;
}

static inline void ncr710_dma_read(NCR710State *s, uint32_t addr,
                                   void *buf, uint32_t len)
{
    address_space_read(s->as, addr, MEMTXATTRS_UNSPECIFIED,
                       buf, len);
    NCR710_DPRINTF("Read %d bytes from %08x: ", len, addr);
    for (int i = 0; i < len && i < 16; i++) {
        NCR710_DPRINTF("%02x ", ((uint8_t *)buf)[i]);
    }
    NCR710_DPRINTF("\n");
}

static inline void ncr710_dma_write(NCR710State *s, uint32_t addr,
                                    const void *buf, uint32_t len)
{
    address_space_write(s->as, addr, MEMTXATTRS_UNSPECIFIED,
                        buf, len);
    NCR710_DPRINTF("Wrote %d bytes to %08x\n", len, addr);
}

static void ncr710_stop_script(NCR710State *s)
{
    s->script_active = 0;
    s->scntl1 &= ~NCR710_SCNTL1_CON;
    s->istat &= ~NCR710_ISTAT_CON;
}

static void ncr710_update_irq(NCR710State *s)
{
    int level = 0;

    if (s->dstat) {
        if (s->dstat & s->dien) {
            level = 1;
        }
        s->istat |= NCR710_ISTAT_DIP;
    } else {
        s->istat &= ~NCR710_ISTAT_DIP;
    }

    if (s->sstat0) {
        if ((s->sstat0 & s->sien0)) {
            level = 1;
        }
        s->istat |= NCR710_ISTAT_SIP;
    } else {
        s->istat &= ~NCR710_ISTAT_SIP;
    }

    qemu_set_irq(s->irq, level);
}

static void ncr710_script_scsi_interrupt(NCR710State *s, int stat0)
{
    uint32_t mask0;

    trace_ncr710_script_scsi_interrupt(stat0, s->sstat0);
    s->sstat0 |= stat0;
    mask0 = stat0 & s->sien0;
    if (mask0) {
        ncr710_stop_script(s);
        s->istat |= NCR710_ISTAT_SIP;
        ncr710_update_irq(s);
    }
}

static void ncr710_script_dma_interrupt(NCR710State *s, int stat)
{
    trace_ncr710_script_dma_interrupt(stat, s->dstat);
    if (stat == NCR710_DSTAT_SIR && (s->dstat & NCR710_DSTAT_DFE)) {
        s->dstat &= ~NCR710_DSTAT_DFE;
    }

    s->dstat |= stat;
    s->istat |= NCR710_ISTAT_DIP;
    ncr710_update_irq(s);
    ncr710_stop_script(s);
}

inline void ncr710_set_phase(NCR710State *s, int phase)
{
    s->sstat2 = (s->sstat2 & ~PHASE_MASK) | phase;
    s->ctest0 &= ~1;
    if (phase == PHASE_DI) {
        s->ctest0 |= 1;
    }
    s->sbcl &= ~NCR710_SBCL_REQ;
}

static void ncr710_disconnect(NCR710State *s)
{
    trace_ncr710_disconnect(s->waiting);
    if (s->waiting == NCR710_WAIT_NONE) {
        s->scntl1 &= ~NCR710_SCNTL1_CON;
        s->istat &= ~NCR710_ISTAT_CON;
    }
    s->sstat2 &= ~PHASE_MASK;
}

static void ncr710_bad_selection(NCR710State *s, uint32_t id)
{
    trace_ncr710_bad_selection(id);
    s->dstat = 0;
    s->dsps = 0;
    ncr710_script_scsi_interrupt(s, NCR710_SSTAT0_STO);
    ncr710_disconnect(s);
}

static void ncr710_clear_selection_timeout(NCR710State *s)
{
    if (s->sstat0 & NCR710_SSTAT0_STO) {
        s->sstat0 &= ~NCR710_SSTAT0_STO;
        ncr710_clear_pending_irq(s);
        if (s->sstat0 == 0) {
            s->istat &= ~NCR710_ISTAT_SIP;
        }
        ncr710_update_irq(s);
    }
}

static void ncr710_do_dma(NCR710State *s, int out)
{
    uint32_t count;
    uint32_t addr;
    SCSIDevice *dev;
    assert(s->current);
    if (!s->current->dma_len) {
        /* We wait until data is available.  */
        return;
    }

    dev = s->current->req->dev;
    assert(dev);

    count = s->dbc;
    if (count > s->current->dma_len) {
        count = s->current->dma_len;
    }

    addr = s->dnad;

    s->dnad += count;
    s->dbc -= count;
    if (s->current->dma_buf == NULL) {
        s->current->dma_buf = scsi_req_get_buf(s->current->req);
    }
    /* ??? Set SFBR to first data byte.  */
    if (out) {
        ncr710_dma_read(s, addr, s->current->dma_buf, count);
    } else {
        ncr710_dma_write(s, addr, s->current->dma_buf, count);
    }
    s->current->dma_len -= count;
    if (s->current->dma_len == 0) {
        s->current->dma_buf = NULL;
        s->current->pending = 0;
        scsi_req_continue(s->current->req);
    } else {
        s->current->dma_buf += count;
        s->waiting = NCR710_WAIT_NONE;
        ncr710_execute_script(s);
    }
}

static void ncr710_add_msg_byte(NCR710State *s, uint8_t data)
{
    if (s->msg_len >= NCR710_MAX_MSGIN_LEN) {
        BADF("MSG IN data too long\n");
    } else {
        s->msg[s->msg_len++] = data;
    }
}

static void ncr710_request_free(NCR710State *s, NCR710Request *p)
{
    if (p == s->current) {
        s->current = NULL;
    }
    g_free(p);
}

void ncr710_request_cancelled(SCSIRequest *req)
{
    NCR710State *s = ncr710_from_scsi_bus(req->bus);
    NCR710Request *p = (NCR710Request *)req->hba_private;
    req->hba_private = NULL;
    ncr710_request_free(s, p);
    scsi_req_unref(req);
}

static int ncr710_queue_req(NCR710State *s, SCSIRequest *req, uint32_t len)
{
    NCR710Request *p = (NCR710Request *)req->hba_private;

    if (!p) {
        return -1;
    }
    p->pending = len;
    if ((s->waiting == NCR710_WAIT_RESELECT &&
        !(s->istat & (NCR710_ISTAT_SIP | NCR710_ISTAT_DIP))) ||
        (ncr710_irq_on_rsl(s) && !(s->scntl1 & NCR710_SCNTL1_CON) &&
        !(s->istat & (NCR710_ISTAT_SIP | NCR710_ISTAT_DIP)))) {
        s->current = p;
        return 0;
    } else {
        s->current = p;
        return 1;
    }
}

void ncr710_command_complete(SCSIRequest *req, size_t resid)
{
    NCR710State *s = ncr710_from_scsi_bus(req->bus);
    NCR710Request *p = (NCR710Request *)req->hba_private;

    trace_ncr710_command_complete(req->tag, req->status);

    s->lcrc = 0;
    s->status = req->status;
    s->command_complete = NCR710_CMD_COMPLETE;

    if (p) {
        p->pending = 0;
    }

    ncr710_set_phase(s, PHASE_ST);

    if (req->hba_private == s->current) {
        scsi_req_unref(req);
    }

    if (s->waiting == NCR710_WAIT_RESELECT || s->waiting == NCR710_WAIT_DMA) {
        s->waiting = NCR710_WAIT_NONE;
        ncr710_execute_script(s);
    }
}

void ncr710_transfer_data(SCSIRequest *req, uint32_t len)
{
    NCR710State *s = ncr710_from_scsi_bus(req->bus);

    assert(req->hba_private);

    if (s->waiting == NCR710_WAIT_DMA) {
        NCR710Request *p = (NCR710Request *)req->hba_private;
        if (p) {
            p->dma_len = len;
        }
        s->dsp -= 8;
        s->waiting = NCR710_WAIT_NONE;
        ncr710_execute_script(s);
        return;
    }

    if (s->wait_reselect) {
        s->current = (NCR710Request *)req->hba_private;
        s->current->dma_len = len;
        s->waiting = NCR710_WAIT_RESELECT;
    }

    if (req->hba_private != s->current ||
        (ncr710_irq_on_rsl(s) && !(s->scntl1 & NCR710_SCNTL1_CON)) ||
        s->waiting == NCR710_WAIT_RESELECT) {
        int queue_result = ncr710_queue_req(s, req, len);
        if (queue_result != 0) {
            return;
        }
    }

    /* Host adapter (re)connected */
    s->command_complete = NCR710_CMD_DATA_READY;
    if (!s->current) {
        return;
    }
    s->current->dma_len = len;

    if (s->waiting) {
        s->scntl1 |= NCR710_SCNTL1_CON;
        s->istat |= NCR710_ISTAT_CON;
        s->sbcl = NCR710_SBCL_IO | NCR710_SBCL_CD | NCR710_SBCL_MSG |
                  NCR710_SBCL_BSY | NCR710_SBCL_SEL | NCR710_SBCL_REQ;
        uint8_t host_id = (s->scid & 0x07);

        /* Special case: both target and host are ID 0 */
        if (req->dev->id == 0 && host_id == 0) {
            s->sfbr = 0x00;
        } else {
            s->sfbr = (req->dev->id == 0 ? 0 : (1 << req->dev->id)) |
                      (host_id == 0 ? 0 : (1 << host_id));
        }

        ncr710_set_phase(s, PHASE_MI);

        if (s->current) {
            uint8_t identify_msg = 0x80 | (req->lun & 0x07);
            ncr710_add_msg_byte(s, identify_msg);

            if (s->current->tag) {
                ncr710_add_msg_byte(s, 0x20);  /* SIMPLE_TAG_MSG */
                ncr710_add_msg_byte(s, s->current->tag & 0xff);
            }
        }

        s->sstat0 |= NCR710_SSTAT0_SEL;
        s->istat |= NCR710_ISTAT_SIP;
        s->dsps = RESELECTED_DURING_SELECTION;
        s->waiting = NCR710_WAIT_NONE;
        ncr710_update_irq(s);
        return;
    }
    if (!s->script_active && !s->waiting) {
        ncr710_execute_script(s);
    }
}

static int idbitstonum(uint8_t id)
{
    return 7 - clz8(id);
}

static void ncr710_do_command(NCR710State *s)
{
    SCSIDevice *dev;
    uint8_t buf[16];
    uint32_t id;
    int n;
    int bytes_read;
    if (s->dbc > 16) {
        s->dbc = 16;
    }

    /*
     * Reading command data directly from memory
     * NOTE: SCSI commands can be up to 16 bytes
     * (e.g., READ_CAPACITY_10 is 10 bytes) but the NCR710 SCSI FIFO is
     * only 8 bytes deep. For command phase, we bypass the FIFO and read
     * directly from memory since commands don't need FIFO buffering.
     */
    bytes_read = MIN(s->dbc, sizeof(buf));
    ncr710_dma_read(s, s->dnad, buf, bytes_read);

    s->dnad += bytes_read;
    s->dbc -= bytes_read;
    s->sfbr = buf[0];

    s->command_complete = NCR710_CMD_PENDING;
    id = (s->select_tag >> 8) & 0xff;
    s->lcrc = id;

    dev = scsi_device_find(&s->bus, 0, idbitstonum(id), s->current_lun);

    if (!dev) {
        ncr710_bad_selection(s, id);
        return;
    }

    if (s->current) {
        ncr710_request_free(s, s->current);
        s->current = NULL;
    }

    s->current = g_new0(NCR710Request, 1);
    s->current->tag = s->select_tag;
    s->current->resume_offset = 0;

    s->current->req = scsi_req_new(dev, s->current->tag, s->current_lun, buf,
                                   bytes_read, s->current);
    n = scsi_req_enqueue(s->current->req);
    if (n) {
        if (n > 0) {
            ncr710_set_phase(s, PHASE_DI);
        } else if (n < 0) {
            ncr710_set_phase(s, PHASE_DO);
        }
        scsi_req_continue(s->current->req);
    }

    if (!s->command_complete) {
        if (!n) {
            ncr710_set_phase(s, PHASE_SI);
        } else {
            NCR710_DPRINTF("Data transfer phase\n");
        }
    }
}

static void ncr710_do_status(NCR710State *s)
{
    uint8_t status = s->status;
    uint8_t parity = 0;

    if (s->dbc != 1) {
        BADF("Bad Status move\n");
    }
    s->dbc = 1;
    s->sfbr = status;

    /* Generate parity if enabled and enqueue status byte */
    if (s->scntl0 & NCR710_SCNTL0_EPG) {
        parity = ncr710_generate_scsi_parity(s, status);
    }
    ncr710_scsi_fifo_enqueue(&s->scsi_fifo, status, parity);

    /* Dequeue status byte and write to memory */
    status = ncr710_scsi_fifo_dequeue(&s->scsi_fifo, &parity);
    if (s->scntl0 & NCR710_SCNTL0_EPC) {
        if (!ncr710_check_scsi_parity(s, status, parity)) {
            ncr710_handle_parity_error(s);
        }
    }
    ncr710_dma_write(s, s->dnad, &status, 1);

    s->dnad += 1;
    s->dbc  -= 1;

    ncr710_set_phase(s, PHASE_MI);
    s->msg_action = NCR710_MSG_ACTION_DISCONNECT;
    ncr710_add_msg_byte(s, 0); /* COMMAND COMPLETE */
}

static void ncr710_do_msgin(NCR710State *s)
{
    int len;
    len = s->msg_len;
    if (len > s->dbc) {
        len = s->dbc;
    }
    s->sfbr = s->msg[0];

    for (int i = 0; i < len; i++) {
        uint8_t parity = 0;
        if (s->scntl0 & NCR710_SCNTL0_EPG) {
            parity = ncr710_generate_scsi_parity(s, s->msg[i]);
        }
        ncr710_scsi_fifo_enqueue(&s->scsi_fifo, s->msg[i], parity);
    }

    uint8_t buf[NCR710_MAX_MSGIN_LEN];
    for (int i = 0; i < len; i++) {
        uint8_t parity;
        buf[i] = ncr710_scsi_fifo_dequeue(&s->scsi_fifo, &parity);
        if (s->scntl0 & NCR710_SCNTL0_EPC) {
            if (!ncr710_check_scsi_parity(s, buf[i], parity)) {
                ncr710_handle_parity_error(s);
            }
        }
    }
    ncr710_dma_write(s, s->dnad, buf, len);

    s->dnad += len;
    s->dbc  -= len;
    s->sidl = s->msg[len - 1];
    s->msg_len -= len;
    if (s->msg_len) {
        memmove(s->msg, s->msg + len, s->msg_len);
        return;
    }
    switch (s->msg_action) {
    case NCR710_MSG_ACTION_NONE:
        ncr710_set_phase(s, PHASE_CO);
        break;
    case NCR710_MSG_ACTION_DISCONNECT:
        ncr710_disconnect(s);
        break;
    case NCR710_MSG_ACTION_DATA_OUT:
        ncr710_set_phase(s, PHASE_DO);
        break;
    case NCR710_MSG_ACTION_DATA_IN:
        ncr710_set_phase(s, PHASE_DI);
        break;
    default:
        abort();
    }
}

static void ncr710_do_msgout(NCR710State *s)
{
    NCR710Request *current_req = s->current;

    while (s->dbc > 0) {
        int to_move = MIN((int)s->dbc, NCR710_SCSI_FIFO_SIZE);
        uint8_t temp_buf[NCR710_SCSI_FIFO_SIZE];
        ncr710_dma_read(s, s->dnad, temp_buf, to_move);
        int filled = 0;
        for (int j = 0; j < to_move &&
                 !ncr710_scsi_fifo_full(&s->scsi_fifo); j++) {
            uint8_t parity = 0;
            if (s->scntl0 & NCR710_SCNTL0_EPG) {
                parity = ncr710_generate_scsi_parity(s, temp_buf[j]);
            }
            if (ncr710_scsi_fifo_enqueue(&s->scsi_fifo, temp_buf[j],
                                         parity) == 0) {
                filled++;
            } else {
                break;
            }
        }

        if (filled <= 0) {
            break;
        }
        uint8_t buf[NCR710_SCSI_FIFO_SIZE];
        int bytes = 0;
        for (int j = 0; j < filled &&
                 !ncr710_scsi_fifo_empty(&s->scsi_fifo); j++) {
            uint8_t parity;
            buf[bytes] = ncr710_scsi_fifo_dequeue(&s->scsi_fifo, &parity);
            if (s->scntl0 & NCR710_SCNTL0_EPC) {
                if (!ncr710_check_scsi_parity(s, buf[bytes], parity)) {
                    ncr710_handle_parity_error(s);
                }
            }
            bytes++;
        }

        s->dnad += bytes;
        s->dbc  -= bytes;
        int i = 0;
        while (i < bytes) {
            uint8_t msg = buf[i++];
            s->sfbr = msg;

            switch (msg) {
            case SCSI_MSG_COMMAND_COMPLETE:
                /* 0x00 - NOP / padding byte / Command Complete */
                /* Just gonna ignore padding bytes, continue processing */
                break;

            case SCSI_MSG_DISCONNECT: /* 0x04 - Disconnect */
                ncr710_disconnect(s);
                break;

            case SCSI_MSG_MESSAGE_REJECT: /* 0x07 - Message Reject */
                /* Target is rejecting our last message */
                ncr710_set_phase(s, PHASE_CO);
                break;

            case SCSI_MSG_NO_OPERATION: /* 0x08 - NOP */
                ncr710_set_phase(s, PHASE_CO);
                break;

            case SCSI_MSG_SAVE_DATA_POINTER: /* 0x02 - Save Data Pointer */
                /* Save current data pointer for later restore */
                break;

            case SCSI_MSG_RESTORE_POINTERS: /* 0x03 - Restore Pointers */
                /* Restore previously saved data pointer */
                break;

            case SCSI_MSG_EXTENDED_MESSAGE: { /* 0x01 - Extended message */
                if (i >= bytes) {
                    /* Not enough data; let next chunk continue parsing */
                    i--; /* rewind one to reparse later */
                    goto out_chunk;
                }
                i++; /* skip ext_len */

                if (i >= bytes) {
                    i -= 2; /* rewind msg + ext_len for next chunk */
                    goto out_chunk;
                }
                uint8_t ext_code = buf[i++];

                switch (ext_code) {
                case 1: /* SDTR (ignore body) */
                    /* Body has 2 bytes, may span chunks: skip what we have */
                    {
                        int skip = MIN(2, bytes - i);
                        i += skip;
                        /*
                         * If not all skipped this chunk, rest will arrive
                         * in next loop
                         */
                    }
                    break;
                case 3: /* WDTR (ignore body) */
                    if (i < bytes) {
                        i++; /* skip one param byte if present this chunk */
                    }
                    break;
                default:
                    goto bad;
                }
                break;
            }

            case 0x20: /* SIMPLE queue tag */
            case 0x21: /* HEAD of queue tag */
            case 0x22: /* ORDERED queue tag */
                if (i < bytes) {
                    uint8_t tag = buf[i++];
                    s->select_tag = (s->select_tag & 0xFF00) | tag |
                                    NCR710_TAG_VALID;
                    NCR710_DPRINTF("Tagged command: tag=0x%02x, "
                                   "type=0x%02x\n", tag, msg);
                } else {
                    /*
                     * Tag byte not in this chunk; rewind and reparse
                     * next loop
                     */
                    i--;
                    goto out_chunk;
                }
                break;

            case 0x0d: /* ABORT TAG */
                if (current_req) {
                    scsi_req_cancel(current_req->req);
                }
                ncr710_disconnect(s);
                break;

            case SCSI_MSG_ABORT: /* 0x06 - ABORT */
            case 0x0e: /* CLEAR QUEUE */
            case SCSI_MSG_BUS_DEVICE_RESET: /* 0x0c - BUS DEVICE RESET */
                if (s->current) {
                    scsi_req_cancel(s->current->req);
                }
                ncr710_disconnect(s);
                break;

            default:
                if (msg & SCSI_MSG_IDENTIFY) {
                    uint8_t lun = msg & 0x07;
                    s->current_lun = lun;
                    ncr710_set_phase(s, PHASE_CO);
                    break;
                }

                /* Unknown message - reject it */
                goto bad;
            }
        }

out_chunk:
        continue;
    }

    return;

bad:
    BADF("Unimplemented/Invalid message 0x%02x\n", s->sfbr);
    ncr710_set_phase(s, PHASE_MI);
    ncr710_add_msg_byte(s, 7);
    s->msg_action = NCR710_MSG_ACTION_NONE;
}

static void ncr710_memcpy(NCR710State *s, uint32_t dest, uint32_t src,
                          int count)
{
    uint8_t buf[NCR710_BUF_SIZE];

    while (count) {
        int chunk = MIN(count, NCR710_BUF_SIZE);
        /* Read from source */
        ncr710_dma_read(s, src, buf, chunk);

        /* Write to destination */
        ncr710_dma_write(s, dest, buf, chunk);

        src += chunk;
        dest += chunk;
        count -= chunk;
    }
}

static void ncr710_wait_reselect(NCR710State *s)
{
    s->wait_reselect = true;
    s->waiting = NCR710_WAIT_RESELECT;
    s->script_active = false;

    s->scntl1 &= ~NCR710_SCNTL1_CON;
    s->istat &= ~NCR710_ISTAT_CON;
}

void ncr710_reselection_retry_callback(void *opaque)
{
    NCR710State *s = opaque;

    if (!s->current || s->current->pending == 0) {
        return;
    }

    if (s->waiting != NCR710_WAIT_RESELECT) {
        return;
    }

    if (s->istat & (NCR710_ISTAT_SIP | NCR710_ISTAT_DIP)) {
        timer_mod(s->reselection_retry_timer,
                  qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000);
        return;
    }

    NCR710Request *p = s->current;
    uint32_t len = p->pending;
    p->pending = 0;

    SCSIRequest *req = p->req;
    s->command_complete = NCR710_CMD_PENDING;
    p->dma_len = len;

    s->scntl1 |= NCR710_SCNTL1_CON;
    s->istat |= NCR710_ISTAT_CON;
    s->sbcl = NCR710_SBCL_IO | NCR710_SBCL_CD | NCR710_SBCL_MSG |
              NCR710_SBCL_BSY | NCR710_SBCL_SEL | NCR710_SBCL_REQ;

    uint8_t host_id = (s->scid & 0x07);
    if (req->dev->id == 0 && host_id == 0) {
        s->sfbr = 0x00;
    } else {
        s->sfbr = (req->dev->id == 0 ? 0 : (1 << req->dev->id)) |
                  (host_id == 0 ? 0 : (1 << host_id));
    }

    ncr710_set_phase(s, PHASE_MI);

    uint8_t identify_msg = 0x80 | (req->lun & 0x07);
    ncr710_add_msg_byte(s, identify_msg);

    if (p->tag) {
        ncr710_add_msg_byte(s, 0x20);  /* SIMPLE_TAG_MSG */
        ncr710_add_msg_byte(s, p->tag & 0xff);
    }

    s->dsp = p->resume_offset - 8;

    s->dsps = RESELECTED_DURING_SELECTION;
    s->sstat0 |= NCR710_SSTAT0_SEL;
    s->istat |= NCR710_ISTAT_SIP;
    ncr710_update_irq(s);
    s->waiting = NCR710_WAIT_NONE;
}

void ncr710_execute_script(NCR710State *s)
{
    uint32_t insn;
    uint32_t addr;
    int opcode;
    s->script_active = 1;

again:
    insn = ncr710_read_dword(s, s->dsp);
    if (!insn) {
        /*
         * If we receive an empty opcode increment the DSP by 4 bytes
         * and execute the next opcode at that location
         */
        s->dsp += 4;
        goto again;
    }
    addr = ncr710_read_dword(s, s->dsp + 4);
    s->dsps = addr;
    s->dcmd = insn >> 24;
    s->dsp += 8;
    switch (insn >> 30) {
    case 0: /* Block move.  */
        if (s->sstat0 & NCR710_SSTAT0_STO) {
            NCR710_DPRINTF("Delayed select timeout\n");
            ncr710_stop_script(s);
            ncr710_update_irq(s);
            break;
        }
        s->dbc = insn & 0xffffff;
        if (insn & (1 << 29)) {
            /* Indirect addressing.  */
            addr = ncr710_read_dword(s, addr);
        } else if (insn & (1 << 28)) {
            uint32_t buf[2];
            int32_t offset;
            /* Table indirect addressing.  */

            /* 32-bit Table indirect */
            offset = sextract32(addr, 0, 24);
            ncr710_dma_read(s, s->dsa + offset, buf, 8);
            /* byte count is stored in bits 0:23 only */
            s->dbc = cpu_to_le32(buf[0]) & 0xffffff;
            addr = cpu_to_le32(buf[1]);
        }
        /* Check phase match for block move instructions */
        if ((s->sstat2 & PHASE_MASK) != ((insn >> 24) & 7)) {
            uint8_t current_phase = s->sstat2 & PHASE_MASK;

            ncr710_set_phase(s, current_phase);
            s->sbcl |= NCR710_SBCL_REQ;
            ncr710_script_scsi_interrupt(s, NCR710_SSTAT0_MA);
            ncr710_stop_script(s);
            break;
        }

        s->dnad = addr;
        switch (s->sstat2 & 0x7) {
        case PHASE_DO:
            s->waiting = NCR710_WAIT_DMA;
            ncr710_do_dma(s, 1);
            break;
        case PHASE_DI:
            s->waiting = NCR710_WAIT_DMA;
            ncr710_do_dma(s, 0);
            break;
        case PHASE_CO:
            ncr710_do_command(s);
            break;
        case PHASE_SI:
            ncr710_do_status(s);
            break;
        case PHASE_MO:
            ncr710_do_msgout(s);
            break;
        case PHASE_MI:
            ncr710_do_msgin(s);
            break;
        default:
            BADF("Unimplemented phase %d\n", s->sstat2 & PHASE_MASK);
        }
        s->ctest5 = (s->ctest5 & 0xfc) | ((s->dbc >> 8) & 3);
        s->sbcl = s->dbc;
        break;

    case 1: /* IO or Read/Write instruction.  */
        opcode = (insn >> 27) & 7;
        if (opcode < 5) {
            uint32_t id;

            if (insn & (1 << 25)) {
                id = ncr710_read_dword(s, s->dsa + sextract32(insn, 0, 24));
            } else {
                id = insn;
            }
            id = (id >> 16) & 0xff;
            if (insn & (1 << 26)) {
                addr = s->dsp + sextract32(addr, 0, 24);
            }
            s->dnad = addr;
            switch (opcode) {
            case 0: /* Select */
                s->sdid = id;
                if (s->scntl1 & NCR710_SCNTL1_CON) {
                    if (!(insn & (1 << 24))) {
                        s->dsp = s->dnad;
                        break;
                    }
                } else if (!scsi_device_find(&s->bus, 0, idbitstonum(id), 0)) {
                    ncr710_bad_selection(s, id);
                    break;
                } else {
                    /*
                     * ??? Linux drivers compain when this is set. Maybe
                     * it only applies in low-level mode (unimplemented).
                     * ncr710_script_scsi_interrupt(s, NCR710_SIST0_CMP, 0);
                     */
                    s->select_tag = id << 8;
                    s->scntl1 |= NCR710_SCNTL1_CON;

                    if (insn & (1 << 24)) {
                        s->socl |= NCR710_SOCL_ATN;
                        ncr710_set_phase(s, PHASE_MO);
                    } else {
                        ncr710_set_phase(s, PHASE_CO);
                    }
                }
                break;
            case 1: /* Disconnect */

                if (s->command_complete != NCR710_CMD_PENDING) {
                    s->scntl1 &= ~NCR710_SCNTL1_CON;
                    s->istat &= ~NCR710_ISTAT_CON;
                    if (s->waiting == NCR710_WAIT_RESELECT) {
                        s->waiting = NCR710_WAIT_NONE;
                    }
                } else {
                    if (s->current) {
                        s->current->resume_offset = s->dsp;
                    }

                    s->waiting = NCR710_WAIT_RESELECT;
                    ncr710_stop_script(s);
                    NCR710_DPRINTF("SCRIPTS paused at WAIT DISCONNECT\n");
                }
                break;
            case 2: /* Wait Reselect */
                if (!ncr710_irq_on_rsl(s)) {
                    ncr710_wait_reselect(s);
                }
                break;
            case 3: /* Set */
                if (insn & (1 << 3)) {
                    s->socl |= NCR710_SOCL_ATN;
                    ncr710_set_phase(s, PHASE_MO);
                }
                if (insn & (1 << 10)) {
                    s->carry = 1;
                }
                break;
            case 4: /* Clear */
                if (insn & (1 << 3)) {
                    s->socl &= ~NCR710_SOCL_ATN;
                }
                if (insn & (1 << 10)) {
                    s->carry = 0;
                }
                break;
            }
        } else {
            uint8_t op0;
            uint8_t op1;
            uint8_t data8;
            int reg;
            int xoperator;

            reg = ((insn >> 16) & 0x7f) | (insn & 0x80);
            data8 = (insn >> 8) & 0xff;
            opcode = (insn >> 27) & 7;
            xoperator = (insn >> 24) & 7;
            op0 = op1 = 0;
            switch (opcode) {
            case 5: /* From SFBR */
                op0 = s->sfbr;
                op1 = data8;
                break;
            case 6: /* To SFBR */
                if (xoperator) {
                    op0 = ncr710_reg_readb(s, reg);
                }
                op1 = data8;
                break;
            case 7: /* Read-modify-write */
                if (xoperator) {
                    op0 = ncr710_reg_readb(s, reg);
                }
                if (insn & (1 << 23)) {
                    op1 = s->sfbr;
                } else {
                    op1 = data8;
                }
                break;
            }

            switch (xoperator) {
            case 0: /* move */
                op0 = op1;
                break;
            case 1: /* Shift left */
                op1 = op0 >> 7;
                op0 = (op0 << 1) | s->carry;
                s->carry = op1;
                break;
            case 2: /* OR */
                op0 |= op1;
                break;
            case 3: /* XOR */
                op0 ^= op1;
                break;
            case 4: /* AND */
                op0 &= op1;
                break;
            case 5: /* SHR */
                op1 = op0 & 1;
                op0 = (op0 >> 1) | (s->carry << 7);
                s->carry = op1;
                break;
            case 6: /* ADD */
                op0 += op1;
                s->carry = op0 < op1;
                break;
            case 7: /* ADC */
                op0 += op1 + s->carry;
                if (s->carry) {
                    s->carry = op0 <= op1;
                } else {
                    s->carry = op0 < op1;
                }
                break;
            }

            switch (opcode) {
            case 5: /* From SFBR */
            case 7: /* Read-modify-write */
                ncr710_reg_writeb(s, reg, op0);
                break;
            case 6: /* To SFBR */
                s->sfbr = op0;
                break;
            }
        }
        break;

    case 2: /* Transfer Control.  */
        {
            int cond;
            int jmp;


            if (s->sstat0 & NCR710_SSTAT0_STO) {
                break;
            }
            cond = jmp = (insn & (1 << 19)) != 0;
            if (cond == jmp && (insn & (1 << 21))) {
                cond = s->carry != 0;
            }
            if (cond == jmp && (insn & (1 << 17))) {
                cond = (s->sstat2 & PHASE_MASK) == ((insn >> 24) & 7);
            }
            if (cond == jmp && (insn & (1 << 18))) {
                uint8_t mask;

                mask = (~insn >> 8) & 0xff;
                cond = (s->sfbr & mask) == (insn & mask);
            }
            if (cond == jmp) {
                if (insn & (1 << 23)) {
                    /* Relative address.  */
                    addr = s->dsp + sextract32(addr, 0, 24);
                }
                switch ((insn >> 27) & 7) {
                case 0: /* Jump */
                    s->dsp = addr;
                    break;
                case 1: /* Call */
                    s->temp = s->dsp;
                    s->dsp = addr;
                    break;
                case 2: /* Return */
                    if (s->temp == 0) {
                        ncr710_script_dma_interrupt(s, NCR710_DSTAT_IID);
                        break;
                    }
                    s->dsp = s->temp;
                    break;
                case 3: /* Interrupt */
                    if ((insn & (1 << 20)) != 0) {
                        ncr710_update_irq(s);
                    } else {
                        if (s->dsps == GOOD_STATUS_AFTER_STATUS) {
                            NCR710_DPRINTF("Script completion: Processing "
                                           "GOOD_STATUS_AFTER_STATUS\n");
                            NCR710_DPRINTF("Script completion: Command state "
                                           "preserved for driver processing\n");
                            ncr710_script_dma_interrupt(s,
                                                        NCR710_DSTAT_SIR);
                            s->command_complete = NCR710_CMD_PENDING;
                        } else {
                            ncr710_script_dma_interrupt(s, NCR710_DSTAT_SIR);
                        }
                    }
                    break;
                default:
                    ncr710_script_dma_interrupt(s, NCR710_DSTAT_IID);
                    break;
                }
            }
        }
        break;

    case 3:
        if ((insn & (1 << 29)) == 0) {
            /* Memory move.  */
            uint32_t dest;
            /*
             * ??? The docs imply the destination address is loaded into
             * the TEMP register.  However the Linux drivers rely on
             * the value being preserved.
             */
            dest = ncr710_read_dword(s, s->dsp);
            s->dsp += 4;
            ncr710_memcpy(s, dest, addr, insn & 0xffffff);
        } else {
            uint8_t data[8];
            int reg;
            int n;
            int i;
            bool dsa_relative = (insn & (1 << 28)) != 0;
            bool is_load = (insn & (1 << 24)) != 0;

            if (dsa_relative) {
                addr = s->dsa + sextract32(addr, 0, 24);
            }

            n = (insn & 7);
            if (n == 0) {
                n = 8;  /* 0 means 8 bytes */
            }

            reg = (insn >> 16) & 0xff;

            if (is_load) {
                ncr710_dma_read(s, addr, data, n);
                for (i = 0; i < n; i++) {
                    ncr710_reg_writeb(s, reg + i, data[i]);
                }
            } else {
                for (i = 0; i < n; i++) {
                    data[i] = ncr710_reg_readb(s, reg + i);
                }
                ncr710_dma_write(s, addr, data, n);
            }
        }
    }

    if (s->script_active && s->waiting == NCR710_WAIT_NONE) {
        if (s->dcntl & NCR710_DCNTL_SSM) {
            ncr710_script_dma_interrupt(s, NCR710_DSTAT_SSI);
            return;
        } else {
            goto again;
        }
    } else if (s->waiting == NCR710_WAIT_RESELECT) {
        return;
    } else if (s->waiting == NCR710_WAIT_DMA ||
               s->waiting == NCR710_WAIT_RESERVED) {
        if (s->command_complete == NCR710_CMD_COMPLETE) {
            s->waiting = NCR710_WAIT_NONE;
            goto again;
        }
        return;
    }
}

static uint8_t ncr710_reg_readb(NCR710State *s, int offset)
{
    uint8_t ret = 0;

#define CASE_GET_REG24(name, addr) \
    case addr: \
        ret = s->name & 0xff; \
        break; \
    case addr + 1: \
        ret = (s->name >> 8) & 0xff; \
        break; \
    case addr + 2: \
        ret = (s->name >> 16) & 0xff; \
        break;

#define CASE_GET_REG32(name, addr) \
    case addr: \
        ret = s->name & 0xff; \
        break; \
    case addr + 1: \
        ret = (s->name >> 8) & 0xff; \
        break; \
    case addr + 2: \
        ret = (s->name >> 16) & 0xff; \
        break; \
    case addr + 3: \
        ret = (s->name >> 24) & 0xff; \
        break;

    switch (offset) {
    case NCR710_SCNTL0_REG: /* SCNTL0 */
        ret = s->scntl0;
        break;
    case NCR710_SCNTL1_REG: /* SCNTL1 */
        ret = s->scntl1;
        break;
    case NCR710_SDID_REG: /* SDID */
        ret = s->sdid;
        break;
    case NCR710_SIEN_REG: /* SIEN */
        ret = s->sien0;
        break;
    case NCR710_SCID_REG:
        ret = s->scid;
        if ((ret & 0x7F) == 0) {
            ret = 0x80 | NCR710_HOST_ID;
        } else {
            ret |= 0x80;
        }
        break;
    case NCR710_SXFER_REG: /* SXFER */
        ret = s->sxfer;
        break;
    case NCR710_SODL_REG: /* SODL */
        ret = s->sodl;
        break;
    case NCR710_SOCL_REG: /* SOCL */
        ret = s->socl;
        break;
    case NCR710_SFBR_REG: /* SFBR */
        ret = s->sfbr;
        break;
    case NCR710_SIDL_REG: /* SIDL */
        ret = s->sidl;
        break;
    case NCR710_SBDL_REG: /* SBDL */
        ret = s->sbdl;
        break;
    case NCR710_SBCL_REG: /* SBCL */
        ret = 0;
        if (s->scntl1 & NCR710_SCNTL1_CON) {
            ret = s->sstat2 & PHASE_MASK;
            ret |= s->sbcl;
            if (s->socl & NCR710_SOCL_ATN) {
                ret |= NCR710_SBCL_ATN;
            }
        }
        break;
    case NCR710_DSTAT_REG: /* DSTAT */
        ret = s->dstat;

        /*
         * Not freeing s->current here:: driver needs it for completion
         * processing. It will be freed when the next command starts.
         */
        if (s->dstat & NCR710_DSTAT_SIR) {
            /* SIR bit set */
        }
        s->dstat = 0;  /* Clear all DMA interrupt status bits */
        s->dstat |= NCR710_DSTAT_DFE;
        s->istat &= ~NCR710_ISTAT_DIP;
        ncr710_update_irq(s);

        if (s->waiting == NCR710_WAIT_RESELECT && s->current &&
            s->current->pending > 0) {
            timer_mod(s->reselection_retry_timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
        }

        if (!s->script_active && s->current && s->current->pending > 0 &&
            s->command_complete == NCR710_CMD_COMPLETE) {
            s->current->pending = 0;
            s->waiting = NCR710_WAIT_NONE;
            ncr710_execute_script(s);
        }

        if (s->waiting && s->current && s->current->pending > 0 &&
            s->command_complete == NCR710_CMD_COMPLETE) {
            s->current->pending = 0;
            s->waiting = NCR710_WAIT_NONE;
            ncr710_execute_script(s);
        }

        return ret;
    case NCR710_SSTAT0_REG: /* SSTAT0 */
        ret = s->sstat0;
        if (s->sstat0 != 0 && !(s->sstat0 & NCR710_SSTAT0_STO)) {
            s->sstat0 = 0;
            s->istat &= ~NCR710_ISTAT_SIP;
            ncr710_update_irq(s);
            if (s->sbcl != 0) {
                s->sbcl = 0;
            }
        }
        break;
    case NCR710_SSTAT1_REG: /* SSTAT1 */
        ret = s->sstat0;
        break;
    case NCR710_SSTAT2_REG: /* SSTAT2 */
        ret = s->dstat;

        if (s->dstat & NCR710_DSTAT_SIR) {
            /* SIR bit processing */
        }
        s->dstat = 0;
        s->istat &= ~NCR710_ISTAT_DIP;
        ncr710_update_irq(s);
        break;
        CASE_GET_REG32(dsa, NCR710_DSA_REG)
        break;
    case NCR710_CTEST0_REG: /* CTEST0 */
        ret = s->ctest0;
        break;
    case NCR710_CTEST1_REG: /* CTEST1 */
        ret = s->ctest1;
        break;
    case NCR710_CTEST2_REG: /* CTEST2 */
        ret = s->ctest2;
        s->ctest2 |= 0x04;
        break;
    case NCR710_CTEST3_REG: /* CTEST3 */
        ret = s->ctest3;
        if (!ncr710_scsi_fifo_empty(&s->scsi_fifo)) {
            uint8_t parity;
            ret = ncr710_scsi_fifo_dequeue(&s->scsi_fifo, &parity);
            if (parity) {
                s->ctest2 |= 0x10;
            } else {
                s->ctest2 &= ~0x10;
            }
        }
        break;
    case NCR710_CTEST4_REG: /* CTEST4 */
        ret = s->ctest4;
        break;
    case NCR710_CTEST5_REG: /* CTEST5 */
        ret = s->ctest5;
        break;
    case NCR710_CTEST6_REG: /* CTEST6 */
        ret = s->ctest6;
        break;
    case NCR710_CTEST7_REG: /* CTEST7 */
        ret = s->ctest7;
        break;
        CASE_GET_REG32(temp, NCR710_TEMP_REG)
    case NCR710_DFIFO_REG: /* DFIFO */
        ret = s->dfifo;
        s->dfifo = 0;  /* DMA FIFO count is always 0 */
        break;
    case NCR710_ISTAT_REG: /* ISTAT */
        ret = s->istat;
        break;
    case NCR710_CTEST8_REG: /* CTEST8 */
        ret = s->istat;
        break;
    case NCR710_LCRC_REG: /* LCRC */
        ret = s->lcrc;
        break;
        CASE_GET_REG24(dbc, NCR710_DBC_REG)
    case NCR710_DCMD_REG: /* DCMD */
        ret = s->dcmd;
        break;
        CASE_GET_REG32(dnad, NCR710_DNAD_REG)
    case NCR710_DSP_REG:
        ret = s->dsp & 0xff;
        break;
    case NCR710_DSP_REG + 1:
        ret = (s->dsp >> 8) & 0xff;
        break;
    case NCR710_DSP_REG + 2:
        ret = (s->dsp >> 16) & 0xff;
        break;
    case NCR710_DSP_REG + 3:
        ret = (s->dsp >> 24) & 0xff;
        if (s->dsps == GOOD_STATUS_AFTER_STATUS &&
            (s->dstat & NCR710_DSTAT_SIR)) {
            s->dstat &= ~NCR710_DSTAT_SIR;
            s->istat &= ~NCR710_ISTAT_DIP;
            ncr710_update_irq(s);
        }
        break;
    case NCR710_DSPS_REG:
        ret = s->dsps & 0xff;
        break;
    case NCR710_DSPS_REG + 1:
        ret = (s->dsps >> 8) & 0xff;
        break;
    case NCR710_DSPS_REG + 2:
        ret = (s->dsps >> 16) & 0xff;
        break;
    case NCR710_DSPS_REG + 3:
        ret = (s->dsps >> 24) & 0xff;
        if (!(s->dstat & NCR710_DSTAT_SIR) && s->dsps != 0) {
            s->dsps = 0;
        }
        break;
        CASE_GET_REG32(scratch, NCR710_SCRATCH_REG)
        break;
    case NCR710_DMODE_REG: /* DMODE */
        ret = s->dmode;
        break;
    case NCR710_DIEN_REG: /* DIEN */
        ret = s->dien;
        break;
    case NCR710_DWT_REG: /* DWT */
        ret = s->dwt;
        break;
    case NCR710_DCNTL_REG: /* DCNTL */
        ret = s->dcntl;
        return ret;
        CASE_GET_REG32(adder, NCR710_ADDER_REG)
        break;
    default:
        ret = 0;
        break;
    }

#undef CASE_GET_REG24
#undef CASE_GET_REG32
    return ret;
}

static void ncr710_reg_writeb(NCR710State *s, int offset, uint8_t val)
{
    uint8_t old_val;

#define CASE_SET_REG24(name, addr) \
    case addr: \
        s->name &= 0xffffff00; \
        s->name |= val; \
        break; \
    case addr + 1: \
        s->name &= 0xffff00ff; \
        s->name |= val << 8; \
        break; \
    case addr + 2: \
        s->name &= 0xff00ffff; \
        s->name |= val << 16; \
        break;

#define CASE_SET_REG32(name, addr) \
    case addr: \
        s->name &= 0xffffff00; \
        s->name |= val; \
        break; \
    case addr + 1: \
        s->name &= 0xffff00ff; \
        s->name |= val << 8; \
        break; \
    case addr + 2: \
        s->name &= 0xff00ffff; \
        s->name |= val << 16; \
        break; \
    case addr + 3: \
        s->name &= 0x00ffffff; \
        s->name |= val << 24; \
        break;

    trace_ncr710_reg_write(ncr710_reg_name(offset), offset, val);

    switch (offset) {
    case NCR710_SCNTL0_REG: /* SCNTL0 */
        old_val = s->scntl0;
        s->scntl0 = val;
        break;

    case NCR710_SCNTL1_REG: /* SCNTL1 */
        old_val = s->scntl1;
        s->scntl1 = val;


        /* Handle Assert Even SCSI Parity (AESP) bit changes */
        if ((val & NCR710_SCNTL1_AESP) != (old_val & NCR710_SCNTL1_AESP)) {
            trace_ncr710_parity_sense_changed((val & NCR710_SCNTL1_AESP)
                                               != 0 ? "even" : "odd");
        }

        if (val & NCR710_SCNTL1_RST) {
            if (!(s->sstat0 & NCR710_SSTAT0_RST)) {
                s->sstat0 |= NCR710_SSTAT0_RST;
                ncr710_script_scsi_interrupt(s, NCR710_SSTAT0_RST);
            }
            if (!(old_val & NCR710_SCNTL1_RST)) {
                NCR710_DPRINTF("NCR710: SCNTL1: SCSI bus reset "
                               "initiated\n");
                ncr710_soft_reset(s);
            }
        } else {
            s->sstat0 &= ~NCR710_SSTAT0_RST;
        }
        break;

    case NCR710_SDID_REG: /* SDID */
        s->sdid = val & 0x0F; /* Only lower 4 bits are valid */
        break;

    case NCR710_SIEN_REG: /* SIEN */
        s->sien0 = val;
        NCR710_DPRINTF("SIEN: interrupt mask=0x%02x\n", val);
        ncr710_update_irq(s);
        break;

    case NCR710_SCID_REG: /* SCID */
        s->scid = val;
        break;

    case NCR710_SXFER_REG: /* SXFER */
        s->sxfer = val;
        break;

    case NCR710_SODL_REG: /* SODL */
        s->sodl = val;
        s->sstat1 |= NCR710_SSTAT1_ORF; /* SCSI Output Register Full */
        break;

    case NCR710_SOCL_REG: /* SOCL */
        s->socl = val;
        break;

    case NCR710_SFBR_REG: /* SFBR */
        s->sfbr = val;
        break;

    case NCR710_SIDL_REG: /* SIDL */
    case NCR710_SBDL_REG: /* SBDL */
        break;

    case NCR710_SBCL_REG: /* SBCL */
        s->sbcl = val;
        ncr710_set_phase(s, val & PHASE_MASK);
        break;

    case NCR710_DSTAT_REG:
    case NCR710_SSTAT0_REG:
    case NCR710_SSTAT1_REG:
    case NCR710_SSTAT2_REG:
        /* Linux writes to these readonly registers on startup */
        return;

    CASE_SET_REG32(dsa, NCR710_DSA_REG)
        break;

    case NCR710_CTEST0_REG: /* CTEST0 */
        s->ctest0 = val;
        break;

    case NCR710_CTEST1_REG: /* CTEST1, read-only */
        s->ctest1 = val;
        break;

    case NCR710_CTEST2_REG: /* CTEST2, read-only */
        s->ctest2 = val;
        break;

    case NCR710_CTEST3_REG: /* CTEST3 */
        s->ctest3 = val;
        break;

    case NCR710_CTEST4_REG: /* CTEST4 */
        s->ctest4 = val;
        break;

    case NCR710_CTEST5_REG: /* CTEST5 */
        s->ctest5 = val;
        break;

    case NCR710_CTEST6_REG: /* CTEST6 */
        s->ctest6 = val;
        break;

    case NCR710_CTEST7_REG: /* CTEST7 */
        s->ctest7 = val;
        break;

    CASE_SET_REG32(temp, NCR710_TEMP_REG)

    case NCR710_DFIFO_REG: /* DFIFO, read-only */
        break;

    case NCR710_ISTAT_REG: /* ISTAT */
        old_val = s->istat;

        if ((old_val & NCR710_ISTAT_DIP) && !(val & NCR710_ISTAT_DIP)) {
            /* Clear script interrupt data after Linux processes it */
            s->dstat = 0;
            s->dsps = 0;
        }

        if ((old_val & NCR710_ISTAT_SIP) && !(val & NCR710_ISTAT_SIP)) {
            s->sstat0 = 0;
        }

        s->istat = (val & ~(NCR710_ISTAT_DIP | NCR710_ISTAT_SIP)) |
                  (s->istat & (NCR710_ISTAT_DIP | NCR710_ISTAT_SIP));
        ncr710_update_irq(s);

        if (val & NCR710_ISTAT_ABRT) {
            ncr710_script_dma_interrupt(s, NCR710_DSTAT_ABRT);
        }
        break;

    case NCR710_CTEST8_REG: /* CTEST8 */
        if (val & 0x08) {
            s->dstat |= NCR710_DSTAT_DFE;
        }
        if (val & 0x04) {
            ncr710_scsi_fifo_init(&s->scsi_fifo);
            s->dstat |= NCR710_DSTAT_DFE;
        }
        break;
    case NCR710_LCRC_REG: /* LCRC */
        s->lcrc = val;
        break;

    CASE_SET_REG24(dbc, NCR710_DBC_REG)

    case NCR710_DCMD_REG: /* DCMD */
        s->dcmd = val;
        break;

    CASE_SET_REG32(dnad, NCR710_DNAD_REG)
    case 0x2c: /* DSP[0:7] */
        s->dsp &= 0xffffff00;
        s->dsp |= val;
        break;
    case 0x2d: /* DSP[8:15] */
        s->dsp &= 0xffff00ff;
        s->dsp |= val << 8;
        break;
    case 0x2e: /* DSP[16:23] */
        s->dsp &= 0xff00ffff;
        s->dsp |= val << 16;
        break;
    case 0x2f: /* DSP[24:31] */
        s->dsp &= 0x00ffffff;
        s->dsp |= val << 24;
        s->waiting = NCR710_WAIT_NONE;
        s->script_active = 1;
        s->istat |= NCR710_ISTAT_CON;
        ncr710_clear_selection_timeout(s);
        ncr710_execute_script(s);
        break;
    CASE_SET_REG32(dsps, NCR710_DSPS_REG)
    CASE_SET_REG32(scratch, NCR710_SCRATCH_REG)
        break;

    case NCR710_DMODE_REG: /* DMODE */
        s->dmode = val;
        break;

    case NCR710_DIEN_REG: /* DIEN */
        s->dien = val;
        NCR710_DPRINTF("DIEN: interrupt enable=0x%02x\n", val);
        ncr710_update_irq(s);
        break;

    case NCR710_DWT_REG: /* DWT */
        s->dwt = val;
        break;

    case NCR710_DCNTL_REG: /* DCNTL */
        s->dcntl = val & ~(NCR710_DCNTL_PFF);
        if (val & NCR710_DCNTL_STD) {
            s->waiting = NCR710_WAIT_NONE;
            ncr710_execute_script(s);
            s->dcntl &= ~NCR710_DCNTL_STD;
        }
        break;

    CASE_SET_REG32(adder, NCR710_ADDER_REG)
        break;

    default:
        break;
    }

#undef CASE_SET_REG24
#undef CASE_SET_REG32
}

/* Memory region wrapper for NCR710 registers */
uint64_t ncr710_reg_read(void *opaque, hwaddr addr, unsigned size)
{
    NCR710State *s = opaque;
    uint8_t offset = addr & 0xff;
    uint8_t val = ncr710_reg_readb(s, offset);
    trace_ncr710_reg_read(ncr710_reg_name(offset), offset, val);
    return val;
}

void ncr710_reg_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    NCR710State *s = opaque;
    uint8_t offset = addr & 0xff;
    uint8_t val8 = val & 0xff;
    trace_ncr710_reg_write(ncr710_reg_name(offset), offset, val8);
    ncr710_reg_writeb(s, offset, val8);
}

/* Device reset */
static void ncr710_device_reset(DeviceState *dev)
{
    SysBusNCR710State *sysbus_dev = SYSBUS_NCR710_SCSI(dev);
    NCR710State *s = &sysbus_dev->ncr710;

    ncr710_soft_reset(s);
}

static const struct SCSIBusInfo ncr710_scsi_info = {
    .tcq = true,
    .max_target = 8,
    .max_lun = 8,  /* Full LUN support */

    .transfer_data = ncr710_transfer_data,
    .complete = ncr710_command_complete,
    .cancel = ncr710_request_cancelled,
};

static const MemoryRegionOps ncr710_mmio_ops = {
    .read = ncr710_reg_read,
    .write = ncr710_reg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static const VMStateDescription vmstate_ncr710_scsi_fifo = {
    .name = "ncr710_scsi_fifo",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8_ARRAY(data, NCR710_SCSI_FIFO, NCR710_SCSI_FIFO_SIZE),
        VMSTATE_UINT8_ARRAY(parity, NCR710_SCSI_FIFO, NCR710_SCSI_FIFO_SIZE),
        VMSTATE_INT32(count, NCR710_SCSI_FIFO),
        VMSTATE_END_OF_LIST()
    }
};

const VMStateDescription vmstate_ncr710 = {
    .name = "ncr710",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(scntl0, NCR710State),
        VMSTATE_UINT8(scntl1, NCR710State),
        VMSTATE_UINT8(sdid, NCR710State),
        VMSTATE_UINT8(sien0, NCR710State),
        VMSTATE_UINT8(scid, NCR710State),
        VMSTATE_UINT8(sxfer, NCR710State),
        VMSTATE_UINT8(sodl, NCR710State),
        VMSTATE_UINT8(socl, NCR710State),
        VMSTATE_UINT8(sfbr, NCR710State),
        VMSTATE_UINT8(sidl, NCR710State),
        VMSTATE_UINT8(sbdl, NCR710State),
        VMSTATE_UINT8(sbcl, NCR710State),
        VMSTATE_UINT8(dstat, NCR710State),
        VMSTATE_UINT8(sstat0, NCR710State),
        VMSTATE_UINT8(sstat1, NCR710State),
        VMSTATE_UINT8(sstat2, NCR710State),
        VMSTATE_UINT8(ctest0, NCR710State),
        VMSTATE_UINT8(ctest1, NCR710State),
        VMSTATE_UINT8(ctest2, NCR710State),
        VMSTATE_UINT8(ctest3, NCR710State),
        VMSTATE_UINT8(ctest4, NCR710State),
        VMSTATE_UINT8(ctest5, NCR710State),
        VMSTATE_UINT8(ctest6, NCR710State),
        VMSTATE_UINT8(ctest7, NCR710State),
        VMSTATE_UINT8(ctest8, NCR710State),
        VMSTATE_UINT32(temp, NCR710State),
        VMSTATE_UINT8(dfifo, NCR710State),
        VMSTATE_UINT8(istat, NCR710State),
        VMSTATE_UINT8(lcrc, NCR710State),
        VMSTATE_UINT8(dcmd, NCR710State),
        VMSTATE_UINT8(dmode, NCR710State),
        VMSTATE_UINT8(dien, NCR710State),
        VMSTATE_UINT8(dwt, NCR710State),
        VMSTATE_UINT8(dcntl, NCR710State),
        VMSTATE_UINT32(dsa, NCR710State),
        VMSTATE_UINT32(dbc, NCR710State),
        VMSTATE_UINT32(dnad, NCR710State),
        VMSTATE_UINT32(dsp, NCR710State),
        VMSTATE_UINT32(dsps, NCR710State),
        VMSTATE_UINT32(scratch, NCR710State),
        VMSTATE_UINT32(adder, NCR710State),
        VMSTATE_STRUCT(scsi_fifo, NCR710State, 1,
            vmstate_ncr710_scsi_fifo, NCR710_SCSI_FIFO),
        VMSTATE_UINT8(status, NCR710State),
        VMSTATE_UINT8_ARRAY(msg, NCR710State,
            NCR710_MAX_MSGIN_LEN),
        VMSTATE_UINT8(msg_len, NCR710State),
        VMSTATE_UINT8(msg_action, NCR710State),
        VMSTATE_INT32(carry, NCR710State),
        VMSTATE_BOOL(script_active, NCR710State),
        VMSTATE_INT32(waiting, NCR710State),
        VMSTATE_UINT8(command_complete, NCR710State),
        VMSTATE_UINT32(select_tag, NCR710State),
        VMSTATE_UINT8(current_lun, NCR710State),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_sysbus_ncr710 = {
    .name = "sysbus_ncr710",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT(ncr710, SysBusNCR710State, 1, vmstate_ncr710,
                       NCR710State),
        VMSTATE_END_OF_LIST()
    }
};

DeviceState *ncr710_device_create_sysbus(hwaddr addr, qemu_irq irq)
{
    DeviceState *dev;
    SysBusDevice *sysbus;

    dev = qdev_new(TYPE_SYSBUS_NCR710_SCSI);
    sysbus = SYS_BUS_DEVICE(dev);

    qdev_realize_and_unref(dev, NULL, &error_abort);
    sysbus_mmio_map(sysbus, 0, addr);
    sysbus_connect_irq(sysbus, 0, irq);
    return dev;
}

DeviceState *ncr53c710_init(MemoryRegion *address_space, hwaddr addr,
                             qemu_irq irq)
{
    DeviceState *dev;
    SysBusDevice *sysbus;
    SysBusNCR710State *s;

    /* trace_ncr710_device_init(addr); */

    dev = qdev_new(TYPE_SYSBUS_NCR710_SCSI);
    sysbus = SYS_BUS_DEVICE(dev);

    qdev_realize_and_unref(dev, NULL, &error_abort);
    sysbus_mmio_map(sysbus, 0, addr);
    sysbus_connect_irq(sysbus, 0, irq);

    s = SYSBUS_NCR710_SCSI(dev);
    if (!s->ncr710.as) {
        s->ncr710.as = &address_space_memory;
    }

    return dev;
}

static void sysbus_ncr710_realize(DeviceState *dev, Error **errp)
{
    SysBusNCR710State *s = SYSBUS_NCR710_SCSI(dev);

    trace_ncr710_device_realize();
    scsi_bus_init(&s->ncr710.bus, sizeof(s->ncr710.bus), dev,
                  &ncr710_scsi_info);
    s->ncr710.as = &address_space_memory;

    ncr710_scsi_fifo_init(&s->ncr710.scsi_fifo);
    s->ncr710.dcntl &= ~NCR710_DCNTL_COM;
    s->ncr710.scid = 0x80 | NCR710_HOST_ID;

    s->ncr710.reselection_retry_timer =
        timer_new_ns(QEMU_CLOCK_VIRTUAL,
                     ncr710_reselection_retry_callback,
                     &s->ncr710);

    memset(s->ncr710.msg, 0, sizeof(s->ncr710.msg));

    memory_region_init_io(&s->iomem, OBJECT(s), &ncr710_mmio_ops, &s->ncr710,
                          "ncr710", 0x100);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(s), &s->ncr710.irq);

}

static void sysbus_ncr710_init(Object *obj)
{
    SysBusNCR710State *s = SYSBUS_NCR710_SCSI(obj);
    memset(&s->ncr710, 0, sizeof(NCR710State));
    s->ncr710.ctest0 = 0x01;
    s->ncr710.scid = 0x80 | NCR710_HOST_ID;
    s->ncr710.dstat = NCR710_DSTAT_DFE;
}

static void sysbus_ncr710_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = sysbus_ncr710_realize;
    device_class_set_legacy_reset(dc, ncr710_device_reset);
    dc->bus_type = NULL;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    dc->desc = "NCR53C710 SCSI I/O Processor (SysBus)";
    dc->vmsd = &vmstate_sysbus_ncr710;
}

static const TypeInfo sysbus_ncr710_info = {
    .name = TYPE_SYSBUS_NCR710_SCSI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SysBusNCR710State),
    .instance_init = sysbus_ncr710_init,
    .class_init = sysbus_ncr710_class_init,
};

static void ncr710_register_types(void)
{
    type_register_static(&sysbus_ncr710_info);
}

type_init(ncr710_register_types)
