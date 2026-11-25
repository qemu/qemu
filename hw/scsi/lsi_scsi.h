/*
 * QEMU LSI53C895A SCSI Host Bus Adapter emulation
 * Definitions shared with other NCR/LSI SCRIPTS based controllers
 *
 * Copyright (c) 2006 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the LGPL.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef HW_SCSI_LSI_SCSI_H
#define HW_SCSI_LSI_SCSI_H

#include "qemu/queue.h"
#include "hw/scsi/scsi.h"

#define LSI_SCNTL0_TRG    0x01
#define LSI_SCNTL0_AAP    0x02
#define LSI_SCNTL0_EPC    0x08
#define LSI_SCNTL0_WATN   0x10
#define LSI_SCNTL0_START  0x20

#define LSI_SCNTL1_AESP   0x04
#define LSI_SCNTL1_RST    0x08
#define LSI_SCNTL1_CON    0x10
#define LSI_SCNTL1_ADB    0x40
#define LSI_SCNTL1_EXC    0x80

#define LSI_ISTAT0_DIP    0x01
#define LSI_ISTAT0_SIP    0x02
#define LSI_ISTAT0_CON    0x08
#define LSI_ISTAT0_SIGP   0x20
#define LSI_ISTAT0_SRST   0x40
#define LSI_ISTAT0_ABRT   0x80

#define LSI_SOCL_ATN      0x08

#define LSI_DSTAT_IID     0x01
#define LSI_DSTAT_SIR     0x04
#define LSI_DSTAT_SSI     0x08
#define LSI_DSTAT_ABRT    0x10
#define LSI_DSTAT_BF      0x20
#define LSI_DSTAT_DFE     0x80

#define LSI_DCNTL_COM     0x01
#define LSI_DCNTL_STD     0x04
#define LSI_DCNTL_SSM     0x10

#define LSI_DMODE_MAN     0x01

#define LSI_CTEST2_DACK   0x01
#define LSI_CTEST2_SIGP   0x40

#define LSI_SBCL_ATN         0x08
#define LSI_SBCL_BSY         0x20
#define LSI_SBCL_ACK         0x40
#define LSI_SBCL_REQ         0x80

#define PHASE_DO          0
#define PHASE_DI          1
#define PHASE_CMD         2
#define PHASE_ST          3
#define PHASE_MO          6
#define PHASE_MI          7
#define PHASE_MASK        7

/* Maximum length of MSG IN data.  */
#define LSI_MAX_MSGIN_LEN 8

/* Flag set if this is a tagged command.  */
#define LSI_TAG_VALID     (1 << 16)

/* Maximum instructions to process. */
#define LSI_MAX_INSN    500

typedef struct lsi_request {
    SCSIRequest *req;
    uint32_t tag;
    uint32_t dma_len;
    uint8_t *dma_buf;
    uint32_t pending;
    int out;
    bool orphan;
    QTAILQ_ENTRY(lsi_request) next;
} lsi_request;

enum {
    LSI_NOWAIT, /* SCRIPTS are running or stopped */
    LSI_WAIT_RESELECT, /* Wait Reselect instruction has been issued */
    LSI_DMA_SCRIPTS, /* processing DMA from lsi_execute_script */
    LSI_DMA_IN_PROGRESS, /* DMA operation is in progress */
    LSI_WAIT_SCRIPTS, /* SCRIPTS stopped because of instruction count limit */
};

enum {
    LSI_MSG_ACTION_COMMAND = 0,
    LSI_MSG_ACTION_DISCONNECT = 1,
    LSI_MSG_ACTION_DOUT = 2,
    LSI_MSG_ACTION_DIN = 3,
};

#endif
