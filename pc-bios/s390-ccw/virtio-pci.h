/*
 * Definitions for virtio-pci
 *
 * Copyright 2025 IBM Corp.
 * Author(s): Jared Rossi <jrossi@linux.ibm.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef VIRTIO_PCI_H
#define VIRTIO_PCI_H

/* Common configuration */
#define VPCI_CAP_COMMON_CFG          1
/* Notifications */
#define VPCI_CAP_NOTIFY_CFG          2
/* ISR access */
#define VPCI_CAP_ISR_CFG             3
/* Device specific configuration */
#define VPCI_CAP_DEVICE_CFG          4
/* PCI configuration access */
#define VPCI_CAP_PCI_CFG             5
/* Additional shared memory capability */
#define VPCI_CAP_SHARED_MEMORY_CFG   8
/* PCI vendor data configuration */
#define VPCI_CAP_VENDOR_CFG          9

/* Offsets within capability header */
#define VPCI_CAP_VNDR        0
#define VPCI_CAP_NEXT        1
#define VPCI_CAP_LEN         2
#define VPCI_CAP_CFG_TYPE    3
#define VPCI_CAP_BAR         4
#define VPCI_CAP_OFFSET      8
#define VPCI_CAP_LENGTH      12

#define VPCI_N_CAP_MULT 16 /* Notify multiplier, VPCI_CAP_NOTIFY_CFG only */

/* Common Area Offsets for virtio-pci queue */
#define VPCI_C_OFFSET_DFSELECT      0
#define VPCI_C_OFFSET_DF            4
#define VPCI_C_OFFSET_GFSELECT      8
#define VPCI_C_OFFSET_GF            12
#define VPCI_C_COMMON_NUMQ          18
#define VPCI_C_OFFSET_STATUS        20
#define VPCI_C_OFFSET_Q_SELECT      22
#define VPCI_C_OFFSET_Q_SIZE        24
#define VPCI_C_OFFSET_Q_ENABLE      28
#define VPCI_C_OFFSET_Q_NOFF        30
#define VPCI_C_OFFSET_Q_DESCLO      32
#define VPCI_C_OFFSET_Q_DESCHI      36
#define VPCI_C_OFFSET_Q_AVAILLO     40
#define VPCI_C_OFFSET_Q_AVAILHI     44
#define VPCI_C_OFFSET_Q_USEDLO      48
#define VPCI_C_OFFSET_Q_USEDHI      52

#define VIRTIO_F_VERSION_1          1   /* Feature bit 32 */

struct VirtioPciCap {
    uint8_t bar;     /* Which PCIAS it's in */
    uint32_t off;    /* Offset within bar */
};
typedef struct VirtioPciCap  VirtioPciCap;

void virtio_pci_id2type(VDev *vdev, uint16_t device_id);
int virtio_pci_reset(VDev *vdev);
long virtio_pci_notify(int vq_id);
int virtio_pci_setup(VDev *vdev);
int virtio_pci_setup_device(void);

int vpci_read_flex(uint64_t offset, uint8_t pcias, void *buf, int len);
int vpci_read_bswap64(uint64_t offset, uint8_t pcias, uint64_t *buf);
int vpci_read_bswap32(uint64_t offset, uint8_t pcias, uint32_t *buf);
int vpci_read_bswap16(uint64_t offset, uint8_t pcias, uint16_t *buf);
int vpci_read_byte(uint64_t offset, uint8_t pcias, uint8_t *buf);

int vpci_bswap64_write(uint64_t offset, uint8_t pcias, uint64_t data);
int vpci_bswap32_write(uint64_t offset, uint8_t pcias, uint32_t data);
int vpci_bswap16_write(uint64_t offset, uint8_t pcias, uint16_t data);
int vpci_write_byte(uint64_t offset, uint8_t pcias, uint8_t data);

#endif
