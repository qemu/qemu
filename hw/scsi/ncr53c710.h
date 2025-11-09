/*
 * QEMU NCR710 SCSI Controller
 *
 * Copyright (c) 2025 Soumyajyotii Ssarkar <soumyajyotisarkar23@gmail.com>
 *
 * NCR710 SCSI Controller implementation
 * Based on the NCR53C710 Technical Manual Version 3.2, December 2000
 *
 * Developed from the hackish implementation of NCR53C710 by Helge Deller
 * which was interim based on the hackish implementation by Toni Wilen for UAE
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_NCR53C710_H
#define HW_NCR53C710_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/scsi/scsi.h"
#include "qemu/fifo8.h"
#include "qom/object.h"
#include "system/memory.h"
#include "hw/irq.h"
#include "qemu/timer.h"

#define TYPE_NCR710_SCSI "ncr710-scsi"
#define TYPE_SYSBUS_NCR710_SCSI "sysbus-ncr710-scsi"

#define SYSBUS_NCR710_SCSI(obj) \
    OBJECT_CHECK(SysBusNCR710State, (obj), TYPE_SYSBUS_NCR710_SCSI)

#define ENABLE_DEBUG 0
#if ENABLE_DEBUG
#define DBG(x)          x
#define NCR710_DPRINTF(fmt, ...) \
    fprintf(stderr, "QEMU: " fmt, ## __VA_ARGS__)
#define BADF(fmt, ...) \
    fprintf(stderr, "QEMU: error: " fmt, ## __VA_ARGS__)
#else
#define DBG(x)         do { } while (0)
#define NCR710_DPRINTF(fmt, ...) do { } while (0)
#define BADF(fmt, ...) do { } while (0)
#endif

/* NCR710 - Little Endian register Ordering */
#define NCR710_SCNTL0_REG       0x00    /* SCSI Control Zero */
#define NCR710_SCNTL1_REG       0x01    /* SCSI Control One */
#define NCR710_SDID_REG         0x02    /* SCSI Destination ID */
#define NCR710_SIEN_REG         0x03    /* SCSI Interrupt Enable */
#define NCR710_SCID_REG         0x04    /* SCSI Chip ID */
#define NCR710_SXFER_REG        0x05    /* SCSI Transfer */
#define NCR710_SODL_REG         0x06    /* SCSI Output Data Latch */
#define NCR710_SOCL_REG         0x07    /* SCSI Output Control Latch */
#define NCR710_SFBR_REG         0x08    /* SCSI First Byte Received */
#define NCR710_SIDL_REG         0x09    /* SCSI Input Data Latch */
#define NCR710_SBDL_REG         0x0A    /* SCSI Bus Data Lines */
#define NCR710_SBCL_REG         0x0B    /* SCSI Bus Control Lines */
#define NCR710_DSTAT_REG        0x0C    /* DMA Status */
#define NCR710_SSTAT0_REG       0x0D    /* SCSI Status Zero */
#define NCR710_SSTAT1_REG       0x0E    /* SCSI Status One */
#define NCR710_SSTAT2_REG       0x0F    /* SCSI Status Two */
#define NCR710_DSA_REG          0x10    /* Data Structure Address */
#define NCR710_CTEST0_REG       0x14    /* Chip Test Zero */
#define NCR710_CTEST1_REG       0x15    /* Chip Test One */
#define NCR710_CTEST2_REG       0x16    /* Chip Test Two */
#define NCR710_CTEST3_REG       0x17    /* Chip Test Three */
#define NCR710_CTEST4_REG       0x18    /* Chip Test Four */
#define NCR710_CTEST5_REG       0x19    /* Chip Test Five */
#define NCR710_CTEST6_REG       0x1A    /* Chip Test Six */
#define NCR710_CTEST7_REG       0x1B    /* Chip Test Seven */
#define NCR710_TEMP_REG         0x1C    /* Temporary Stack */
#define NCR710_DFIFO_REG        0x20    /* DMA FIFO */
#define NCR710_ISTAT_REG        0x21    /* Interrupt Status */
#define NCR710_CTEST8_REG       0x22    /* Chip Test Eight */
#define NCR710_LCRC_REG         0x23    /* Longitudinal Parity */
#define NCR710_DBC_REG          0x24    /* DMA Byte Counter (24-bit, LE) */
#define NCR710_DCMD_REG         0x27    /* DMA Command */
#define NCR710_DNAD_REG         0x28    /* DMA Next Data Address (32-bit, LE) */
#define NCR710_DSP_REG          0x2C    /* DMA SCRIPTS Pointer (32-bit, LE) */
#define NCR710_DSPS_REG         0x30    /* DMA SCRIPTS Pointer Save */
#define NCR710_SCRATCH_REG      0x34    /* Scratch (32-bit, LE) */
#define NCR710_DMODE_REG        0x38    /* DMA Mode */
#define NCR710_DIEN_REG         0x39    /* DMA Interrupt Enable */
#define NCR710_DWT_REG          0x3A    /* DMA Watchdog Timer */
#define NCR710_DCNTL_REG        0x3B    /* DMA Control */
#define NCR710_ADDER_REG        0x3C    /* Adder Sum Output (32-bit, LE) */

#define NCR710_REG_SIZE         0x100

#define NCR710_BUF_SIZE         4096
#define NCR710_HOST_ID          7
#define NCR710_MAX_MSGIN_LEN    8
#define NCR710_SCSI_FIFO_SIZE   8

typedef enum {
    NCR710_WAIT_NONE = 0,
    NCR710_WAIT_RESELECT = 1,
    NCR710_WAIT_DMA = 2,
    NCR710_WAIT_RESERVED = 3
} NCR710WaitState;

typedef enum {
    NCR710_CMD_PENDING = 0,
    NCR710_CMD_DATA_READY = 1,
    NCR710_CMD_COMPLETE = 2
} NCR710CommandState;

typedef enum {
    NCR710_MSG_ACTION_NONE = 0,
    NCR710_MSG_ACTION_DISCONNECT = 1,
    NCR710_MSG_ACTION_DATA_OUT = 2,
    NCR710_MSG_ACTION_DATA_IN = 3
} NCR710MessageAction;

typedef struct NCR710State NCR710State;
typedef struct NCR710Request NCR710Request;

/*
 * SCSI FIFO structure - 8 transfers deep, 1 byte per transfer
 * (9-bit wide with parity)
 */
typedef struct {
    uint8_t data[NCR710_SCSI_FIFO_SIZE];
    uint8_t parity[NCR710_SCSI_FIFO_SIZE];
    int head;
    int count;
} NCR710_SCSI_FIFO;

struct NCR710Request {
    SCSIRequest *req;
    uint32_t tag;
    uint32_t dma_len;
    uint32_t pending;
    uint8_t status;
    bool active;
    uint8_t *dma_buf;
    bool out;
    uint32_t resume_offset;
    uint32_t saved_dnad;
};

struct NCR710State {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    qemu_irq irq;

    SCSIBus bus;
    AddressSpace *as;

    /* Registers */
    uint8_t scntl0;
    uint8_t scntl1;
    uint8_t sdid;
    uint8_t sien0;
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
    uint8_t ctest1;
    uint8_t ctest2;
    uint8_t ctest3;
    uint8_t ctest4;
    uint8_t ctest5;
    uint8_t ctest6;
    uint8_t ctest7;
    uint8_t ctest8;
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

    NCR710_SCSI_FIFO scsi_fifo;

    NCR710Request *current;
    uint8_t status;
    uint8_t msg[NCR710_MAX_MSGIN_LEN];
    uint8_t msg_len;
    uint8_t msg_action;         /* NCR710MessageAction values */
    int carry;
    bool script_active;
    int32_t waiting;            /* NCR710WaitState values */
    uint8_t command_complete;   /* NCR710CommandState values */

    QEMUTimer *reselection_retry_timer;
    uint32_t saved_dsps;

    uint32_t select_tag;
    uint8_t current_lun;
    uint8_t reselection_id;
    bool wait_reselect;
};

typedef struct SysBusNCR710State {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    MemoryRegion iomem;
    qemu_irq irq;
    NCR710State ncr710;
} SysBusNCR710State;

static inline NCR710State *ncr710_from_scsi_bus(SCSIBus *bus)
{
    return container_of(bus, NCR710State, bus);
}

static inline SysBusNCR710State *sysbus_from_ncr710(NCR710State *s)
{
    return container_of(s, SysBusNCR710State, ncr710);
}

DeviceState *ncr53c710_init(MemoryRegion *address_space, hwaddr addr,
                             qemu_irq irq);
DeviceState *ncr710_device_create_sysbus(hwaddr addr, qemu_irq irq);
void ncr710_reg_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
uint64_t ncr710_reg_read(void *opaque, hwaddr addr, unsigned size);
void ncr710_soft_reset(NCR710State *s);
void ncr710_request_cancelled(SCSIRequest *req);
void ncr710_command_complete(SCSIRequest *req, size_t resid);
void ncr710_transfer_data(SCSIRequest *req, uint32_t len);
void ncr710_execute_script(NCR710State *s);
void ncr710_set_phase(NCR710State *s, int phase);
void ncr710_reselection_retry_callback(void *opaque);
extern const VMStateDescription vmstate_ncr710;

#endif /* HW_NCR53C710_H */
