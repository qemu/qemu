/*
 * Q1 PCIe Device - Full BAR Layout Implementation
 *
 * Q1 SoC with embedded RISC-V and 4x Q32 CIM accelerators.
 * Exposed to host via PCIe with:
 *   - BAR0: Control Block + Accelerator Registers (64KB)
 *   - BAR2: DDR Memory (512MB, 64-bit prefetchable)
 *
 * Vendor ID: 0x1234 (QEMU educational)
 * Device ID: 0x0001 (Q1)
 *
 * Copyright (c) 2026 Qernel AI
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/units.h"
#include "qemu/main-loop.h"
#include "qapi/error.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/msi.h"
#include "hw/core/qdev-properties.h"
#include "qom/object.h"
#include "qemu/module.h"
#include "q1_shmem.h"

#define TYPE_Q1_PCIE "q1-pcie"
typedef struct Q1PCIEState Q1PCIEState;
DECLARE_INSTANCE_CHECKER(Q1PCIEState, Q1_PCIE, TYPE_Q1_PCIE)

/*============================================================================
 * PCIe Configuration
 *============================================================================*/

#define Q1_VENDOR_ID            0x1234
#define Q1_DEVICE_ID            0x0001

/*============================================================================
 * BAR0: Control & Accelerator Registers (64KB)
 *============================================================================*/

#define Q1_BAR0_SIZE            (64 * KiB)

/* Region offsets within BAR0 */
#define Q1_BAR0_CTRL_OFFSET     0x0000
#define Q1_BAR0_CTRL_SIZE       0x1000      /* 4KB */

#define Q1_BAR0_Q32_OFFSET      0x1000
#define Q1_BAR0_Q32_SIZE        0x1000      /* 4KB per Q32 */
#define Q1_BAR0_Q32_COUNT       4

#define Q1_BAR0_SFU_OFFSET      0x5000
#define Q1_BAR0_SFU_SIZE        0x1000      /* 4KB */

#define Q1_BAR0_FA_OFFSET       0x6000
#define Q1_BAR0_FA_SIZE         0x1000      /* 4KB */

#define Q1_BAR0_DMA_OFFSET      0x7000
#define Q1_BAR0_DMA_SIZE        0x1000      /* 4KB */

/*============================================================================
 * Control Block Registers (BAR0 + 0x0000)
 *============================================================================*/

#define Q1_CTRL_DOORBELL        0x000
#define Q1_CTRL_STATUS          0x004
#define Q1_CTRL_IRQ_STATUS      0x008
#define Q1_CTRL_IRQ_MASK        0x00C
#define Q1_CTRL_CMD_BUF_ADDR_LO 0x010
#define Q1_CTRL_CMD_BUF_ADDR_HI 0x014
#define Q1_CTRL_CMD_BUF_SIZE    0x018
#define Q1_CTRL_FW_STATUS       0x01C
#define Q1_CTRL_VERSION         0x020
#define Q1_CTRL_CAPS            0x024

/* IRQ bits */
#define Q1_IRQ_DOORBELL         (1 << 0)
#define Q1_IRQ_COMPLETE         (1 << 1)
#define Q1_IRQ_ERROR            (1 << 2)

/* Firmware status values */
#define Q1_FW_STATUS_RESET      0x00
#define Q1_FW_STATUS_INIT       0x01
#define Q1_FW_STATUS_READY      0x02
#define Q1_FW_STATUS_BUSY       0x03
#define Q1_FW_STATUS_ERROR      0xFF

/* Version: major.minor.patch encoded as 0xMMmmpp */
#define Q1_VERSION              0x010000    /* v1.0.0 */

/* Capabilities */
#define Q1_CAPS_NUM_Q32_SHIFT   0
#define Q1_CAPS_NUM_Q32_MASK    0xF
#define Q1_CAPS_HAS_SFU         (1 << 4)
#define Q1_CAPS_HAS_FA          (1 << 5)
#define Q1_CAPS_HAS_DMA         (1 << 6)

/*============================================================================
 * Q32 Registers (BAR0 + 0x1000 + core_id * 0x1000)
 *============================================================================*/

#define Q32_REG_CONTROL         0x00
#define Q32_REG_STATUS          0x04
#define Q32_REG_SRC_ADDR_LO     0x08
#define Q32_REG_SRC_ADDR_HI     0x0C
#define Q32_REG_DST_ADDR_LO     0x10
#define Q32_REG_DST_ADDR_HI     0x14
#define Q32_REG_SCALE_ADDR_HI   0x18
#define Q32_REG_SCALE_ADDR_LO   0x1C
#define Q32_REG_CMD_FIFO_CTRL   0x24
#define Q32_REG_CMD_FIFO_STATUS 0x2C
#define Q32_REG_CIM_STATUS      0x30
#define Q32_REG_DEBUG           0x34

/* Q32 Status bits */
#define Q32_STATUS_BUSY         (1 << 0)
#define Q32_STATUS_DONE         (1 << 1)
#define Q32_STATUS_ERROR        (1 << 2)
#define Q32_STATUS_FIFO_EMPTY   (1 << 4)
#define Q32_STATUS_FIFO_FULL    (1 << 5)

/* Q32 FIFO configuration */
#define Q32_CMD_FIFO_SIZE       32
#define Q32_FIFO_DEPTH_MASK     0x3F

/*============================================================================
 * SFU Registers (BAR0 + 0x5000) - Special Function Unit
 *============================================================================*/

#define SFU_REG_CONTROL         0x00
#define SFU_REG_STATUS          0x04
#define SFU_REG_SRC_ADDR_LO     0x08
#define SFU_REG_SRC_ADDR_HI     0x0C
#define SFU_REG_DST_ADDR_LO     0x10
#define SFU_REG_DST_ADDR_HI     0x14
#define SFU_REG_LENGTH          0x18
#define SFU_REG_OPCODE          0x1C

/*============================================================================
 * FA Registers (BAR0 + 0x6000) - Fused Attention
 *============================================================================*/

#define FA_REG_CONTROL          0x00
#define FA_REG_STATUS           0x04
#define FA_REG_Q_ADDR_LO        0x08
#define FA_REG_Q_ADDR_HI        0x0C
#define FA_REG_K_ADDR_LO        0x10
#define FA_REG_K_ADDR_HI        0x14
#define FA_REG_V_ADDR_LO        0x18
#define FA_REG_V_ADDR_HI        0x1C
#define FA_REG_OUT_ADDR_LO      0x20
#define FA_REG_OUT_ADDR_HI      0x24
#define FA_REG_SEQ_LEN          0x28
#define FA_REG_HEAD_DIM         0x2C

/*============================================================================
 * DMA Registers (BAR0 + 0x7000)
 *============================================================================*/

#define DMA_REG_CONTROL         0x00
#define DMA_REG_STATUS          0x04
#define DMA_REG_SRC_ADDR_LO     0x08
#define DMA_REG_SRC_ADDR_HI     0x0C
#define DMA_REG_DST_ADDR_LO     0x10
#define DMA_REG_DST_ADDR_HI     0x14
#define DMA_REG_LENGTH          0x18
#define DMA_REG_STRIDE_SRC      0x1C
#define DMA_REG_STRIDE_DST      0x20

/*============================================================================
 * BAR2: DDR Memory (512MB)
 *============================================================================*/

#define Q1_BAR2_SIZE            (512 * MiB)

/* DDR layout: 120MB per Q32 + 32MB shared */
#define Q1_DDR_Q32_SIZE         (120 * MiB)
#define Q1_DDR_Q32_0_OFFSET     (0 * Q1_DDR_Q32_SIZE)
#define Q1_DDR_Q32_1_OFFSET     (1 * Q1_DDR_Q32_SIZE)
#define Q1_DDR_Q32_2_OFFSET     (2 * Q1_DDR_Q32_SIZE)
#define Q1_DDR_Q32_3_OFFSET     (3 * Q1_DDR_Q32_SIZE)
#define Q1_DDR_SHARED_OFFSET    (4 * Q1_DDR_Q32_SIZE)   /* 480MB */
#define Q1_DDR_SHARED_SIZE      (32 * MiB)

/*============================================================================
 * Device State
 *============================================================================*/

/* Q32 core state */
typedef struct Q32State {
    uint32_t control;
    uint32_t status;
    uint32_t src_addr_lo;
    uint32_t src_addr_hi;
    uint32_t dst_addr_lo;
    uint32_t dst_addr_hi;
    uint32_t scale_addr_hi;
    uint32_t scale_addr_lo;
    uint32_t cmd_fifo_ctrl;
    uint32_t cmd_fifo_status;
    uint32_t cim_status;
    uint32_t debug;
    
    /* Internal state */
    uint32_t fifo_depth;
    uint32_t commands_executed;
    bool cim_filled;
} Q32State;

/* SFU state */
typedef struct SFUState {
    uint32_t control;
    uint32_t status;
    uint32_t src_addr_lo;
    uint32_t src_addr_hi;
    uint32_t dst_addr_lo;
    uint32_t dst_addr_hi;
    uint32_t length;
    uint32_t opcode;
} SFUState;

/* FA state */
typedef struct FAState {
    uint32_t control;
    uint32_t status;
    uint32_t q_addr_lo;
    uint32_t q_addr_hi;
    uint32_t k_addr_lo;
    uint32_t k_addr_hi;
    uint32_t v_addr_lo;
    uint32_t v_addr_hi;
    uint32_t out_addr_lo;
    uint32_t out_addr_hi;
    uint32_t seq_len;
    uint32_t head_dim;
} FAState;

/* DMA state */
typedef struct DMAState {
    uint32_t control;
    uint32_t status;
    uint32_t src_addr_lo;
    uint32_t src_addr_hi;
    uint32_t dst_addr_lo;
    uint32_t dst_addr_hi;
    uint32_t length;
    uint32_t stride_src;
    uint32_t stride_dst;
} DMAState;

/* Control block state */
typedef struct CtrlState {
    uint32_t doorbell;
    uint32_t status;
    uint32_t irq_status;
    uint32_t irq_mask;
    uint32_t cmd_buf_addr_lo;
    uint32_t cmd_buf_addr_hi;
    uint32_t cmd_buf_size;
    uint32_t fw_status;
} CtrlState;

/* Main device state */
struct Q1PCIEState {
    PCIDevice pdev;
    
    /* Memory regions */
    MemoryRegion bar0;          /* Registers */
    MemoryRegion bar2;          /* DDR */
    
    /* DDR backing storage */
    uint8_t *ddr;
    
    /* Shared memory for firmware communication */
    Q1ShmemContext shmem;
    char *shmem_path;           /* Device property: path to shared memory file */
    bool use_shmem;             /* Whether shared memory is active */
    
    /* Register state */
    CtrlState ctrl;
    Q32State q32[Q1_BAR0_Q32_COUNT];
    SFUState sfu;
    FAState fa;
    DMAState dma;
};

/*============================================================================
 * Control Block Read/Write
 *============================================================================*/

static uint64_t q1_ctrl_read(Q1PCIEState *s, hwaddr offset)
{
    switch (offset) {
    case Q1_CTRL_DOORBELL:
        return 0;  /* Write-only */
    case Q1_CTRL_STATUS:
        return s->ctrl.status;
    case Q1_CTRL_IRQ_STATUS:
        /* If using shmem, also check for IRQ status from shared region */
        if (s->use_shmem && s->shmem.initialized) {
            uint32_t shmem_irq = q1_shmem_ctrl_read32(&s->shmem, Q1_SHMEM_CTRL_IRQ_STATUS);
            s->ctrl.irq_status |= shmem_irq;
        }
        return s->ctrl.irq_status;
    case Q1_CTRL_IRQ_MASK:
        return s->ctrl.irq_mask;
    case Q1_CTRL_CMD_BUF_ADDR_LO:
        return s->ctrl.cmd_buf_addr_lo;
    case Q1_CTRL_CMD_BUF_ADDR_HI:
        return s->ctrl.cmd_buf_addr_hi;
    case Q1_CTRL_CMD_BUF_SIZE:
        return s->ctrl.cmd_buf_size;
    case Q1_CTRL_FW_STATUS:
        /* Read firmware status from shared memory if available */
        if (s->use_shmem && s->shmem.initialized) {
            s->ctrl.fw_status = q1_shmem_ctrl_read32(&s->shmem, Q1_SHMEM_CTRL_FW_STATUS);
        }
        return s->ctrl.fw_status;
    case Q1_CTRL_VERSION:
        return Q1_VERSION;
    case Q1_CTRL_CAPS:
        return (Q1_BAR0_Q32_COUNT << Q1_CAPS_NUM_Q32_SHIFT) |
               Q1_CAPS_HAS_SFU | Q1_CAPS_HAS_FA | Q1_CAPS_HAS_DMA;
    default:
        return 0;
    }
}

static void q1_ctrl_write(Q1PCIEState *s, hwaddr offset, uint64_t val)
{
    switch (offset) {
    case Q1_CTRL_DOORBELL:
        s->ctrl.doorbell = val;
        s->ctrl.irq_status |= Q1_IRQ_DOORBELL;
        qemu_log_mask(LOG_UNIMP, "q1-pcie: doorbell rung (val=0x%lx)\n",
                      (unsigned long)val);
        
        /* If using shared memory, write doorbell value for firmware to poll */
        if (s->use_shmem && s->shmem.initialized) {
            q1_shmem_ctrl_write32(&s->shmem, Q1_SHMEM_CTRL_DOORBELL, val);
        }
        break;
    case Q1_CTRL_STATUS:
        s->ctrl.status = val;
        break;
    case Q1_CTRL_IRQ_STATUS:
        /* Write 1 to clear */
        s->ctrl.irq_status &= ~val;
        break;
    case Q1_CTRL_IRQ_MASK:
        s->ctrl.irq_mask = val;
        break;
    case Q1_CTRL_CMD_BUF_ADDR_LO:
        s->ctrl.cmd_buf_addr_lo = val;
        break;
    case Q1_CTRL_CMD_BUF_ADDR_HI:
        s->ctrl.cmd_buf_addr_hi = val;
        break;
    case Q1_CTRL_CMD_BUF_SIZE:
        s->ctrl.cmd_buf_size = val;
        break;
    case Q1_CTRL_FW_STATUS:
        s->ctrl.fw_status = val;
        break;
    default:
        break;
    }
}

/*============================================================================
 * Q32 Read/Write
 *============================================================================*/

static uint64_t q1_q32_read(Q1PCIEState *s, int core, hwaddr offset)
{
    Q32State *q = &s->q32[core];
    
    switch (offset) {
    case Q32_REG_CONTROL:
        return q->control;
    case Q32_REG_STATUS:
        return q->status;
    case Q32_REG_SRC_ADDR_LO:
        return q->src_addr_lo;
    case Q32_REG_SRC_ADDR_HI:
        return q->src_addr_hi;
    case Q32_REG_DST_ADDR_LO:
        return q->dst_addr_lo;
    case Q32_REG_DST_ADDR_HI:
        return q->dst_addr_hi;
    case Q32_REG_SCALE_ADDR_HI:
        return q->scale_addr_hi;
    case Q32_REG_SCALE_ADDR_LO:
        return q->scale_addr_lo;
    case Q32_REG_CMD_FIFO_CTRL:
        return q->cmd_fifo_ctrl;
    case Q32_REG_CMD_FIFO_STATUS:
        return q->fifo_depth & Q32_FIFO_DEPTH_MASK;
    case Q32_REG_CIM_STATUS:
        return q->cim_status;
    case Q32_REG_DEBUG:
        return q->debug;
    default:
        return 0;
    }
}

static void q1_q32_write(Q1PCIEState *s, int core, hwaddr offset, uint64_t val)
{
    Q32State *q = &s->q32[core];
    
    switch (offset) {
    case Q32_REG_CONTROL:
        q->control = val;
        break;
    case Q32_REG_STATUS:
        /* Write 1 to clear DONE/ERROR bits */
        q->status &= ~(val & (Q32_STATUS_DONE | Q32_STATUS_ERROR));
        break;
    case Q32_REG_SRC_ADDR_LO:
        q->src_addr_lo = val;
        break;
    case Q32_REG_SRC_ADDR_HI:
        q->src_addr_hi = val;
        break;
    case Q32_REG_DST_ADDR_LO:
        q->dst_addr_lo = val;
        break;
    case Q32_REG_DST_ADDR_HI:
        q->dst_addr_hi = val;
        break;
    case Q32_REG_SCALE_ADDR_HI:
        q->scale_addr_hi = val;
        break;
    case Q32_REG_SCALE_ADDR_LO:
        q->scale_addr_lo = val;
        break;
    case Q32_REG_CMD_FIFO_CTRL:
        /* Writing to CMD_FIFO_CTRL triggers command execution */
        q->cmd_fifo_ctrl = val;
        q->commands_executed++;
        q->fifo_depth++;
        if (q->fifo_depth >= Q32_CMD_FIFO_SIZE) {
            q->status |= Q32_STATUS_FIFO_FULL;
        }
        q->status &= ~Q32_STATUS_FIFO_EMPTY;
        qemu_log_mask(LOG_UNIMP, "q1-pcie: Q32[%d] cmd=0x%08lx (depth=%d)\n",
                      core, (unsigned long)val, q->fifo_depth);
        break;
    case Q32_REG_DEBUG:
        q->debug = val;
        break;
    default:
        break;
    }
}

/*============================================================================
 * SFU Read/Write
 *============================================================================*/

static uint64_t q1_sfu_read(Q1PCIEState *s, hwaddr offset)
{
    SFUState *sfu = &s->sfu;
    
    switch (offset) {
    case SFU_REG_CONTROL:    return sfu->control;
    case SFU_REG_STATUS:     return sfu->status;
    case SFU_REG_SRC_ADDR_LO: return sfu->src_addr_lo;
    case SFU_REG_SRC_ADDR_HI: return sfu->src_addr_hi;
    case SFU_REG_DST_ADDR_LO: return sfu->dst_addr_lo;
    case SFU_REG_DST_ADDR_HI: return sfu->dst_addr_hi;
    case SFU_REG_LENGTH:     return sfu->length;
    case SFU_REG_OPCODE:     return sfu->opcode;
    default:                 return 0;
    }
}

static void q1_sfu_write(Q1PCIEState *s, hwaddr offset, uint64_t val)
{
    SFUState *sfu = &s->sfu;
    
    switch (offset) {
    case SFU_REG_CONTROL:    sfu->control = val; break;
    case SFU_REG_STATUS:     sfu->status &= ~val; break;  /* W1C */
    case SFU_REG_SRC_ADDR_LO: sfu->src_addr_lo = val; break;
    case SFU_REG_SRC_ADDR_HI: sfu->src_addr_hi = val; break;
    case SFU_REG_DST_ADDR_LO: sfu->dst_addr_lo = val; break;
    case SFU_REG_DST_ADDR_HI: sfu->dst_addr_hi = val; break;
    case SFU_REG_LENGTH:     sfu->length = val; break;
    case SFU_REG_OPCODE:     sfu->opcode = val; break;
    default:                 break;
    }
}

/*============================================================================
 * FA Read/Write
 *============================================================================*/

static uint64_t q1_fa_read(Q1PCIEState *s, hwaddr offset)
{
    FAState *fa = &s->fa;
    
    switch (offset) {
    case FA_REG_CONTROL:     return fa->control;
    case FA_REG_STATUS:      return fa->status;
    case FA_REG_Q_ADDR_LO:   return fa->q_addr_lo;
    case FA_REG_Q_ADDR_HI:   return fa->q_addr_hi;
    case FA_REG_K_ADDR_LO:   return fa->k_addr_lo;
    case FA_REG_K_ADDR_HI:   return fa->k_addr_hi;
    case FA_REG_V_ADDR_LO:   return fa->v_addr_lo;
    case FA_REG_V_ADDR_HI:   return fa->v_addr_hi;
    case FA_REG_OUT_ADDR_LO: return fa->out_addr_lo;
    case FA_REG_OUT_ADDR_HI: return fa->out_addr_hi;
    case FA_REG_SEQ_LEN:     return fa->seq_len;
    case FA_REG_HEAD_DIM:    return fa->head_dim;
    default:                 return 0;
    }
}

static void q1_fa_write(Q1PCIEState *s, hwaddr offset, uint64_t val)
{
    FAState *fa = &s->fa;
    
    switch (offset) {
    case FA_REG_CONTROL:     fa->control = val; break;
    case FA_REG_STATUS:      fa->status &= ~val; break;  /* W1C */
    case FA_REG_Q_ADDR_LO:   fa->q_addr_lo = val; break;
    case FA_REG_Q_ADDR_HI:   fa->q_addr_hi = val; break;
    case FA_REG_K_ADDR_LO:   fa->k_addr_lo = val; break;
    case FA_REG_K_ADDR_HI:   fa->k_addr_hi = val; break;
    case FA_REG_V_ADDR_LO:   fa->v_addr_lo = val; break;
    case FA_REG_V_ADDR_HI:   fa->v_addr_hi = val; break;
    case FA_REG_OUT_ADDR_LO: fa->out_addr_lo = val; break;
    case FA_REG_OUT_ADDR_HI: fa->out_addr_hi = val; break;
    case FA_REG_SEQ_LEN:     fa->seq_len = val; break;
    case FA_REG_HEAD_DIM:    fa->head_dim = val; break;
    default:                 break;
    }
}

/*============================================================================
 * DMA Read/Write
 *============================================================================*/

static uint64_t q1_dma_read(Q1PCIEState *s, hwaddr offset)
{
    DMAState *dma = &s->dma;
    
    switch (offset) {
    case DMA_REG_CONTROL:    return dma->control;
    case DMA_REG_STATUS:     return dma->status;
    case DMA_REG_SRC_ADDR_LO: return dma->src_addr_lo;
    case DMA_REG_SRC_ADDR_HI: return dma->src_addr_hi;
    case DMA_REG_DST_ADDR_LO: return dma->dst_addr_lo;
    case DMA_REG_DST_ADDR_HI: return dma->dst_addr_hi;
    case DMA_REG_LENGTH:     return dma->length;
    case DMA_REG_STRIDE_SRC: return dma->stride_src;
    case DMA_REG_STRIDE_DST: return dma->stride_dst;
    default:                 return 0;
    }
}

static void q1_dma_write(Q1PCIEState *s, hwaddr offset, uint64_t val)
{
    DMAState *dma = &s->dma;
    
    switch (offset) {
    case DMA_REG_CONTROL:    dma->control = val; break;
    case DMA_REG_STATUS:     dma->status &= ~val; break;  /* W1C */
    case DMA_REG_SRC_ADDR_LO: dma->src_addr_lo = val; break;
    case DMA_REG_SRC_ADDR_HI: dma->src_addr_hi = val; break;
    case DMA_REG_DST_ADDR_LO: dma->dst_addr_lo = val; break;
    case DMA_REG_DST_ADDR_HI: dma->dst_addr_hi = val; break;
    case DMA_REG_LENGTH:     dma->length = val; break;
    case DMA_REG_STRIDE_SRC: dma->stride_src = val; break;
    case DMA_REG_STRIDE_DST: dma->stride_dst = val; break;
    default:                 break;
    }
}

/*============================================================================
 * BAR0 MMIO Dispatch
 *============================================================================*/

static uint64_t q1_bar0_read(void *opaque, hwaddr addr, unsigned size)
{
    Q1PCIEState *s = opaque;
    
    if (addr < Q1_BAR0_CTRL_OFFSET + Q1_BAR0_CTRL_SIZE) {
        /* Control Block */
        return q1_ctrl_read(s, addr - Q1_BAR0_CTRL_OFFSET);
    } else if (addr >= Q1_BAR0_Q32_OFFSET &&
               addr < Q1_BAR0_Q32_OFFSET + Q1_BAR0_Q32_COUNT * Q1_BAR0_Q32_SIZE) {
        /* Q32 registers */
        int core = (addr - Q1_BAR0_Q32_OFFSET) / Q1_BAR0_Q32_SIZE;
        hwaddr offset = (addr - Q1_BAR0_Q32_OFFSET) % Q1_BAR0_Q32_SIZE;
        return q1_q32_read(s, core, offset);
    } else if (addr >= Q1_BAR0_SFU_OFFSET &&
               addr < Q1_BAR0_SFU_OFFSET + Q1_BAR0_SFU_SIZE) {
        /* SFU registers */
        return q1_sfu_read(s, addr - Q1_BAR0_SFU_OFFSET);
    } else if (addr >= Q1_BAR0_FA_OFFSET &&
               addr < Q1_BAR0_FA_OFFSET + Q1_BAR0_FA_SIZE) {
        /* FA registers */
        return q1_fa_read(s, addr - Q1_BAR0_FA_OFFSET);
    } else if (addr >= Q1_BAR0_DMA_OFFSET &&
               addr < Q1_BAR0_DMA_OFFSET + Q1_BAR0_DMA_SIZE) {
        /* DMA registers */
        return q1_dma_read(s, addr - Q1_BAR0_DMA_OFFSET);
    }
    
    qemu_log_mask(LOG_GUEST_ERROR,
                  "q1-pcie: BAR0 read from unknown offset 0x%lx\n",
                  (unsigned long)addr);
    return 0;
}

static void q1_bar0_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    Q1PCIEState *s = opaque;
    
    if (addr < Q1_BAR0_CTRL_OFFSET + Q1_BAR0_CTRL_SIZE) {
        /* Control Block */
        q1_ctrl_write(s, addr - Q1_BAR0_CTRL_OFFSET, val);
    } else if (addr >= Q1_BAR0_Q32_OFFSET &&
               addr < Q1_BAR0_Q32_OFFSET + Q1_BAR0_Q32_COUNT * Q1_BAR0_Q32_SIZE) {
        /* Q32 registers */
        int core = (addr - Q1_BAR0_Q32_OFFSET) / Q1_BAR0_Q32_SIZE;
        hwaddr offset = (addr - Q1_BAR0_Q32_OFFSET) % Q1_BAR0_Q32_SIZE;
        q1_q32_write(s, core, offset, val);
    } else if (addr >= Q1_BAR0_SFU_OFFSET &&
               addr < Q1_BAR0_SFU_OFFSET + Q1_BAR0_SFU_SIZE) {
        /* SFU registers */
        q1_sfu_write(s, addr - Q1_BAR0_SFU_OFFSET, val);
    } else if (addr >= Q1_BAR0_FA_OFFSET &&
               addr < Q1_BAR0_FA_OFFSET + Q1_BAR0_FA_SIZE) {
        /* FA registers */
        q1_fa_write(s, addr - Q1_BAR0_FA_OFFSET, val);
    } else if (addr >= Q1_BAR0_DMA_OFFSET &&
               addr < Q1_BAR0_DMA_OFFSET + Q1_BAR0_DMA_SIZE) {
        /* DMA registers */
        q1_dma_write(s, addr - Q1_BAR0_DMA_OFFSET, val);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "q1-pcie: BAR0 write to unknown offset 0x%lx\n",
                      (unsigned long)addr);
    }
}

static const MemoryRegionOps q1_bar0_ops = {
    .read = q1_bar0_read,
    .write = q1_bar0_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

/*============================================================================
 * BAR2 DDR Access
 *============================================================================*/

static uint64_t q1_bar2_read(void *opaque, hwaddr addr, unsigned size)
{
    Q1PCIEState *s = opaque;
    uint64_t val = 0;
    
    if (addr + size <= Q1_BAR2_SIZE && s->ddr) {
        memcpy(&val, s->ddr + addr, size);
    }
    
    return val;
}

static void q1_bar2_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    Q1PCIEState *s = opaque;
    
    if (addr + size <= Q1_BAR2_SIZE && s->ddr) {
        memcpy(s->ddr + addr, &val, size);
    }
}

static const MemoryRegionOps q1_bar2_ops = {
    .read = q1_bar2_read,
    .write = q1_bar2_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

/*============================================================================
 * Device Lifecycle
 *============================================================================*/

static void q1_pcie_realize(PCIDevice *pdev, Error **errp)
{
    Q1PCIEState *s = Q1_PCIE(pdev);
    int i;
    int ret;
    
    s->use_shmem = false;
    
    /* Try to use shared memory if path is configured */
    if (s->shmem_path && strlen(s->shmem_path) > 0) {
        ret = q1_shmem_init(&s->shmem, s->shmem_path, true);
        if (ret < 0) {
            qemu_log_mask(LOG_UNIMP,
                          "q1-pcie: failed to init shared memory at %s: %d, using local allocation\n",
                          s->shmem_path, ret);
        } else {
            s->ddr = s->shmem.ddr_base;
            s->use_shmem = true;
            qemu_log_mask(LOG_UNIMP,
                          "q1-pcie: using shared memory at %s\n", s->shmem_path);
        }
    }
    
    /* Fall back to local allocation if shared memory not available */
    if (!s->use_shmem) {
        s->ddr = g_malloc0(Q1_BAR2_SIZE);
        if (!s->ddr) {
            error_setg(errp, "Failed to allocate Q1 DDR memory");
            return;
        }
    }
    
    /* Initialize BAR0 - Registers */
    memory_region_init_io(&s->bar0, OBJECT(s), &q1_bar0_ops, s,
                          "q1-pcie-bar0", Q1_BAR0_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar0);
    
    /* Initialize BAR2 - DDR (64-bit prefetchable) */
    memory_region_init_io(&s->bar2, OBJECT(s), &q1_bar2_ops, s,
                          "q1-pcie-bar2", Q1_BAR2_SIZE);
    pci_register_bar(pdev, 2,
                     PCI_BASE_ADDRESS_SPACE_MEMORY |
                     PCI_BASE_ADDRESS_MEM_PREFETCH |
                     PCI_BASE_ADDRESS_MEM_TYPE_64,
                     &s->bar2);
    
    /* Initialize Control Block */
    s->ctrl.fw_status = Q1_FW_STATUS_RESET;
    s->ctrl.status = 0;
    s->ctrl.irq_status = 0;
    s->ctrl.irq_mask = 0;
    
    /* Initialize Q32 cores */
    for (i = 0; i < Q1_BAR0_Q32_COUNT; i++) {
        s->q32[i].status = Q32_STATUS_DONE | Q32_STATUS_FIFO_EMPTY;
        s->q32[i].fifo_depth = 0;
        s->q32[i].commands_executed = 0;
        s->q32[i].cim_filled = false;
    }
    
    qemu_log_mask(LOG_UNIMP, 
                  "q1-pcie: initialized (BAR0=%luKB, BAR2=%luMB, shmem=%s)\n",
                  (unsigned long)(Q1_BAR0_SIZE / KiB),
                  (unsigned long)(Q1_BAR2_SIZE / MiB),
                  s->use_shmem ? "yes" : "no");
}

static void q1_pcie_exit(PCIDevice *pdev)
{
    Q1PCIEState *s = Q1_PCIE(pdev);
    
    if (s->use_shmem) {
        q1_shmem_cleanup(&s->shmem);
        s->ddr = NULL;
    } else {
        g_free(s->ddr);
        s->ddr = NULL;
    }
}

/*============================================================================
 * Device Properties
 *============================================================================*/

static const Property q1_pcie_properties[] = {
    DEFINE_PROP_STRING("shmem", Q1PCIEState, shmem_path),
};

/*============================================================================
 * Class Initialization
 *============================================================================*/

static void q1_pcie_class_init(ObjectClass *class, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);
    
    k->realize = q1_pcie_realize;
    k->exit = q1_pcie_exit;
    k->vendor_id = Q1_VENDOR_ID;
    k->device_id = Q1_DEVICE_ID;
    k->revision = 0x01;
    k->class_id = PCI_CLASS_PROCESSOR_CO;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->desc = "Q1 AI Accelerator (4x Q32 CIM + SFU + FA + DMA)";
    device_class_set_props(dc, q1_pcie_properties);
}

static const TypeInfo q1_pcie_types[] = {
    {
        .name          = TYPE_Q1_PCIE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(Q1PCIEState),
        .class_init    = q1_pcie_class_init,
        .interfaces    = (const InterfaceInfo[]) {
            { INTERFACE_CONVENTIONAL_PCI_DEVICE },
            { },
        },
    }
};

DEFINE_TYPES(q1_pcie_types)
