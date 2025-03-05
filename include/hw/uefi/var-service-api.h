/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * uefi-vars device - API of the virtual device for guest/host communication.
 */
#ifndef QEMU_UEFI_VAR_SERVICE_API_H
#define QEMU_UEFI_VAR_SERVICE_API_H

/* qom: device names */
#define TYPE_UEFI_VARS_X64       "uefi-vars-x64"
#define TYPE_UEFI_VARS_SYSBUS    "uefi-vars-sysbus"

/* sysbus: fdt node path */
#define UEFI_VARS_FDT_NODE       "qemu-uefi-vars"
#define UEFI_VARS_FDT_COMPAT     "qemu,uefi-vars"

/* registers */
#define UEFI_VARS_REG_MAGIC                  0x00  /* 16 bit */
#define UEFI_VARS_REG_CMD_STS                0x02  /* 16 bit */
#define UEFI_VARS_REG_BUFFER_SIZE            0x04  /* 32 bit */
#define UEFI_VARS_REG_DMA_BUFFER_ADDR_LO     0x08  /* 32 bit */
#define UEFI_VARS_REG_DMA_BUFFER_ADDR_HI     0x0c  /* 32 bit */
#define UEFI_VARS_REG_PIO_BUFFER_TRANSFER    0x10  /* 8-64 bit */
#define UEFI_VARS_REG_PIO_BUFFER_CRC32C      0x18  /* 32 bit (read-only) */
#define UEFI_VARS_REG_FLAGS                  0x1c  /* 32 bit */
#define UEFI_VARS_REGS_SIZE                  0x20

/* flags register */
#define UEFI_VARS_FLAG_USE_PIO           (1 << 0)

/* magic value */
#define UEFI_VARS_MAGIC_VALUE               0xef1

/* command values */
#define UEFI_VARS_CMD_RESET                  0x01
#define UEFI_VARS_CMD_DMA_MM                 0x02
#define UEFI_VARS_CMD_PIO_MM                 0x03
#define UEFI_VARS_CMD_PIO_ZERO_OFFSET        0x04

/* status values */
#define UEFI_VARS_STS_SUCCESS                0x00
#define UEFI_VARS_STS_BUSY                   0x01
#define UEFI_VARS_STS_ERR_UNKNOWN            0x10
#define UEFI_VARS_STS_ERR_NOT_SUPPORTED      0x11
#define UEFI_VARS_STS_ERR_BAD_BUFFER_SIZE    0x12


#endif /* QEMU_UEFI_VAR_SERVICE_API_H */
