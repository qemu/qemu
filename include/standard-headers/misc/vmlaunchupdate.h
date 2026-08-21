/*
 * Guest driven VM launch state update device via IGVM.
 * The definitions in this header defines the API for the hypervisor interface.
 * For details and specification, please look at docs/specs/vmlaunchupdate.rst.
 *
 * Copyright (C) 2026 Red Hat, Inc.
 *
 * Authors: Ani Sinha <anisinha@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */
#ifndef VMLAUNCHUPDATE_API_H
#define VMLAUNCHUPDATE_API_H

/* fw-cfg file definition */
#define FILE_VMLAUNCHUPDATE "etc/vmlaunchupdate"

/* version */
#define VM_LAUNCHUPDATE_VERSION 0x01

/* format bits, used by both 'capabilities' and 'control'  */

/* igvm */
#define VM_LAUNCHUPDATE_FORMAT_IGVM           (1ULL << 32)

/* 'control' field bits  */

/* disable vmlaunchupdate interface */
#define VM_LAUNCHUPDATE_CTL_DISABLE            (1 << 0)
/* revert to the original host provided igvm */
#define VM_LAUNCHUPDATE_CTL_HOST_IGVM          (1 << 1)

/* The combination of the above two ctl interfaces work as
 * follows:
 *
 * A) CTL_HOST_IGVM=off CTL_DISABLE=off
 *
 * Supplied IGVM file replaces the firmware permanently.  Updating the
 * firmware again is possible.
 *
 * B) CTL_HOST_IGVM=off CTL_DISABLE=on
 *
 * Supplied IGVM file replaces the firmware permanently.  Updating the
 * firmware again is not possible.
 *
 * C) CTL_HOST_IGVM=on CTL_DISABLE=off
 *
 * Supplied IGVM file replaces the firmware for one reset.  Resetting
 * again will switch back to the original firmware.  Updating the
 * firmware again is possible.
 *
 * D) CTL_HOST_IGVM=on CTL_DISABLE=on
 *
 * Supplied IGVM file replaces the firmware for one reset.  Resetting
 * again will switch back to the original firmware.  Updating the
 * firmware again is NOT possible.
 *
 */

/* status code */
enum VMLaunchUpdateStatus {
    VM_LAUNCHUPDATE_SUCCESS = 0,
    VM_LAUNCHUPDATE_LOAD_FAIL = 1,
    VM_LAUNCHUPDATE_NOT_IGVM_INIT = 2,
};

typedef struct QEMU_PACKED {
    /* api version */
    uint16_t version;

    /*
     * The guest can read this in order to determine if loading new IGVM
     * succeeded.
     */
    uint16_t status;

    uint32_t _padding;

    /* VMM capabilities, read-only. */
    uint64_t capabilities;
    /* control bits, see VMFWUPDATE_CTL_* */
    uint64_t control;

    /*
     * address and size of the IGVM image.  Will be cleared when
     * the write completes successfully and IGVM file is correctly parsed.
     */
    uint64_t fw_image_addr;
    uint64_t fw_image_size;

    /*
     * address + size of opaque blob.  The guest can use this to pass on
     * information, for example which memory region the linux kernel has been
     * loaded to.  writable, will be kept intact on firmware update.
     */
    uint64_t opaque_addr;
    uint64_t opaque_size;

} VMLaunchUpdate;

#endif
