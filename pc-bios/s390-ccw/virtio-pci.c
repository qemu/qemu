/*
 * Functionality for virtio-pci
 *
 * Copyright 2025 IBM Corp.
 * Author(s): Jared Rossi <jrossi@linux.ibm.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "clp.h"
#include "pci.h"
#include "helper.h"
#include "virtio.h"
#include "bswap.h"
#include "virtio-pci.h"
#include "s390-time.h"
#include <stdio.h>

/* Variable offsets used for reads/writes to modern memory regions */
VirtioPciCap c_cap; /* Common capabilities  */
VirtioPciCap d_cap; /* Device capabilities  */
VirtioPciCap n_cap; /* Notify capabilities  */
uint32_t notify_mult;
uint16_t q_notify_offset;

static int virtio_pci_set_status(uint8_t status)
{
    int rc = vpci_write_byte(c_cap.off + VPCI_C_OFFSET_STATUS, c_cap.bar, status);
    if (rc) {
        puts("Failed to write virtio-pci status");
        return -EIO;
    }

    return 0;
}

static int virtio_pci_get_status(uint8_t *status)
{
    int rc = vpci_read_byte(c_cap.off + VPCI_C_OFFSET_STATUS, c_cap.bar, status);
    if (rc) {
        puts("Failed to read virtio-pci status");
        return -EIO;
    }

    return 0;
}

/* virtio spec v1.3 section 4.1.2.1 */
void virtio_pci_id2type(VDev *vdev, uint16_t device_id)
{
    switch (device_id) {
    case 0x1042:
    case 0x1001:
        vdev->dev_type = VIRTIO_ID_BLOCK;
        break;
    default:
        vdev->dev_type = 0;
    }
}

int virtio_pci_reset(VDev *vdev)
{
    int rc;
    uint8_t status = 0;

    rc = virtio_pci_set_status(status);
    rc |= virtio_pci_get_status(&status);

    if (rc || status) {
        puts("Failed to reset virtio-pci device");
        return 1;
    }

    return 0;
}

long virtio_pci_notify(int vq_id)
{
    uint32_t offset = n_cap.off + notify_mult * q_notify_offset;
    return vpci_bswap16_write(offset, n_cap.bar, (uint16_t) vq_id);
}

/*
 * Wrappers to byte swap common data sizes then write
 */
int vpci_write_byte(uint64_t offset, uint8_t pcias, uint8_t data)
{
    return pci_write(virtio_get_device()->pci_fh, offset, pcias, (uint64_t) data, 1);
}

int vpci_bswap16_write(uint64_t offset, uint8_t pcias, uint16_t data)
{
    uint64_t le_data = bswap16(data);
    return pci_write(virtio_get_device()->pci_fh, offset, pcias, le_data, 2);
}

int vpci_bswap32_write(uint64_t offset, uint8_t pcias, uint32_t data)
{
    uint64_t le_data = bswap32(data);
    return pci_write(virtio_get_device()->pci_fh, offset, pcias, le_data, 4);
}

int vpci_bswap64_write(uint64_t offset, uint8_t pcias, uint64_t data)
{
    uint64_t le_data = bswap64(data);
    return pci_write(virtio_get_device()->pci_fh, offset, pcias, le_data, 8);
}

/*
 * Wrappers to read common data sizes then byte swap
 */
int vpci_read_byte(uint64_t offset, uint8_t pcias, uint8_t *buf)
{
    return pci_read(virtio_get_device()->pci_fh, offset, pcias, buf, 1);
}

int vpci_read_bswap16(uint64_t offset, uint8_t pcias, uint16_t *buf)
{
    int rc = pci_read(virtio_get_device()->pci_fh, offset, pcias, buf, 2);
    *buf = bswap16(*buf);
    return rc;
}

int vpci_read_bswap32(uint64_t offset, uint8_t pcias, uint32_t *buf)
{
    int rc = pci_read(virtio_get_device()->pci_fh, offset, pcias, buf, 4);
    *buf = bswap32(*buf);
    return rc;
}

int vpci_read_bswap64(uint64_t offset, uint8_t pcias, uint64_t *buf)
{
    int rc = pci_read(virtio_get_device()->pci_fh, offset, pcias, buf, 8);
    *buf = bswap64(*buf);
    return rc;
}

/*
 * Read to an arbitrary length buffer without byte swapping
 */
int vpci_read_flex(uint64_t offset, uint8_t pcias, void *buf, int len)
{
    uint8_t readlen;
    int rc;
    int remaining = len;

    /* Read bytes in powers of 2, up to a maximum of 8 bytes per read */
    while (remaining) {
        for (int i = 3; i >= 0; i--) {
            readlen = 1 << i;
            if (remaining >= readlen) {
                break;
            }
        }

        rc = pci_read(virtio_get_device()->pci_fh, offset, pcias, buf, readlen);
        if (rc) {
            return -1;
        }

        remaining -= readlen;
        buf += readlen;
        offset += readlen;
    }

    return 0;
}
