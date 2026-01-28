/*
 * Q1 Shared Memory Interface
 *
 * Provides shared memory and signaling between the Q1 PCIe device model
 * and the RISC-V firmware running in a separate QEMU instance.
 *
 * Copyright (c) 2026 Qernel AI
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef Q1_SHMEM_H
#define Q1_SHMEM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*============================================================================
 * Shared Memory Layout
 *============================================================================*/

/* 
 * Shared memory layout (must match firmware's shmem_interface.h):
 *   0x00000000 - 0x0EFFFFFF: 240MB - DDR region
 *   0x0F000000 - 0x0F0000FF: 256B  - Control region (SHMEM_CTRL)
 *   Total: 256MB file (matches RISC-V virt machine RAM at 0x80000000)
 */
#define Q1_SHMEM_DDR_SIZE           (0x0F000000)          /* 240MB */
#define Q1_SHMEM_CTRL_SIZE          256                   /* 256 bytes */
#define Q1_SHMEM_TOTAL_SIZE         (256 * 1024 * 1024)   /* 256MB total file */

/* Control region offsets (relative to control region base) */
#define Q1_SHMEM_CTRL_DOORBELL      0x00    /* uint32_t: doorbell value */
#define Q1_SHMEM_CTRL_FW_STATUS     0x04    /* uint32_t: firmware status */
#define Q1_SHMEM_CTRL_IRQ_STATUS    0x08    /* uint32_t: IRQ status flags */
#define Q1_SHMEM_CTRL_IRQ_MASK      0x0C    /* uint32_t: IRQ mask */
#define Q1_SHMEM_CTRL_CMD_ADDR_LO   0x10    /* uint32_t: command buffer addr low */
#define Q1_SHMEM_CTRL_CMD_ADDR_HI   0x14    /* uint32_t: command buffer addr high */
#define Q1_SHMEM_CTRL_CMD_SIZE      0x18    /* uint32_t: command buffer size */
#define Q1_SHMEM_CTRL_RESP_STATUS   0x1C    /* uint32_t: response status */
#define Q1_SHMEM_CTRL_MAGIC         0xFC    /* uint32_t: magic value for validation */

/* Magic value to verify shared memory is initialized */
#define Q1_SHMEM_MAGIC              0x51534D45  /* "QSME" */

/* Firmware status values */
#define Q1_SHMEM_FW_STATUS_RESET    0x00
#define Q1_SHMEM_FW_STATUS_INIT     0x01
#define Q1_SHMEM_FW_STATUS_READY    0x02
#define Q1_SHMEM_FW_STATUS_BUSY     0x03
#define Q1_SHMEM_FW_STATUS_DONE     0x04
#define Q1_SHMEM_FW_STATUS_ERROR    0xFF

/* IRQ status bits */
#define Q1_SHMEM_IRQ_DOORBELL       (1 << 0)
#define Q1_SHMEM_IRQ_COMPLETE       (1 << 1)
#define Q1_SHMEM_IRQ_ERROR          (1 << 2)

/*============================================================================
 * Signaling Protocol
 *============================================================================*/

/* Signal types sent over the Unix socket */
#define Q1_SIGNAL_DOORBELL          0x01    /* Host -> Firmware: doorbell rung */
#define Q1_SIGNAL_COMPLETE          0x02    /* Firmware -> Host: command complete */
#define Q1_SIGNAL_ERROR             0x03    /* Firmware -> Host: error occurred */
#define Q1_SIGNAL_PING              0x04    /* Bidirectional: keepalive/test */
#define Q1_SIGNAL_PONG              0x05    /* Response to PING */

/* Signal message structure (8 bytes) */
typedef struct Q1Signal {
    uint32_t type;      /* Q1_SIGNAL_* */
    uint32_t value;     /* Associated value (e.g., doorbell value) */
} Q1Signal;

/*============================================================================
 * Shared Memory Context
 *============================================================================*/

typedef struct Q1ShmemContext {
    /* File descriptor for shared memory file */
    int shmem_fd;
    
    /* Mapped memory regions */
    void *shmem_base;       /* Base of entire shared memory */
    void *ddr_base;         /* DDR region (same as shmem_base) */
    void *ctrl_base;        /* Control region (shmem_base + DDR_SIZE) */
    
    /* Socket for signaling */
    int signal_sock;        /* Unix domain socket FD */
    bool is_server;         /* True if we created the socket server */
    
    /* State */
    bool initialized;
} Q1ShmemContext;

/*============================================================================
 * API Functions
 *============================================================================*/

/**
 * Initialize shared memory context
 * 
 * @param ctx       Context to initialize
 * @param shmem_path Path to shared memory file (e.g., "/tmp/q1_ddr.bin")
 * @param create    If true, create the file if it doesn't exist
 * @return 0 on success, negative errno on failure
 */
int q1_shmem_init(Q1ShmemContext *ctx, const char *shmem_path, bool create);

/**
 * Cleanup shared memory context
 * 
 * @param ctx   Context to cleanup
 */
void q1_shmem_cleanup(Q1ShmemContext *ctx);

/**
 * Connect to signaling socket (client mode)
 * 
 * @param ctx           Context
 * @param socket_path   Path to Unix domain socket
 * @return 0 on success, negative errno on failure
 */
int q1_shmem_connect_signal(Q1ShmemContext *ctx, const char *socket_path);

/**
 * Create signaling socket server
 * 
 * @param ctx           Context
 * @param socket_path   Path to create Unix domain socket
 * @return 0 on success, negative errno on failure
 */
int q1_shmem_create_signal_server(Q1ShmemContext *ctx, const char *socket_path);

/**
 * Accept a connection on the signal server socket
 * 
 * @param ctx   Context with server socket
 * @return Client socket FD on success, negative errno on failure
 */
int q1_shmem_accept_signal(Q1ShmemContext *ctx);

/**
 * Send a signal to the other side
 * 
 * @param ctx       Context
 * @param type      Signal type (Q1_SIGNAL_*)
 * @param value     Associated value
 * @return 0 on success, negative errno on failure
 */
int q1_shmem_send_signal(Q1ShmemContext *ctx, uint32_t type, uint32_t value);

/**
 * Receive a signal (blocking)
 * 
 * @param ctx       Context
 * @param signal    Output: received signal
 * @return 0 on success, negative errno on failure
 */
int q1_shmem_recv_signal(Q1ShmemContext *ctx, Q1Signal *signal);

/**
 * Check if a signal is available (non-blocking)
 * 
 * @param ctx       Context
 * @return 1 if signal available, 0 if not, negative errno on error
 */
int q1_shmem_signal_available(Q1ShmemContext *ctx);

/*============================================================================
 * Control Region Access Helpers
 *============================================================================*/

/**
 * Read a 32-bit value from the control region
 */
static inline uint32_t q1_shmem_ctrl_read32(Q1ShmemContext *ctx, uint32_t offset)
{
    volatile uint32_t *ptr = (volatile uint32_t *)((uint8_t *)ctx->ctrl_base + offset);
    return *ptr;
}

/**
 * Write a 32-bit value to the control region
 */
static inline void q1_shmem_ctrl_write32(Q1ShmemContext *ctx, uint32_t offset, uint32_t value)
{
    volatile uint32_t *ptr = (volatile uint32_t *)((uint8_t *)ctx->ctrl_base + offset);
    *ptr = value;
}

/**
 * Get pointer to DDR at given offset
 */
static inline void *q1_shmem_ddr_ptr(Q1ShmemContext *ctx, uint64_t offset)
{
    if (offset >= Q1_SHMEM_DDR_SIZE) {
        return NULL;
    }
    return (uint8_t *)ctx->ddr_base + offset;
}

/**
 * Validate shared memory is properly initialized
 */
static inline bool q1_shmem_is_valid(Q1ShmemContext *ctx)
{
    if (!ctx->initialized) {
        return false;
    }
    return q1_shmem_ctrl_read32(ctx, Q1_SHMEM_CTRL_MAGIC) == Q1_SHMEM_MAGIC;
}

/**
 * Initialize the control region with default values
 */
void q1_shmem_ctrl_init(Q1ShmemContext *ctx);

#endif /* Q1_SHMEM_H */
