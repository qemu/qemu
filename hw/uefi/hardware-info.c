/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * pass hardware information to uefi
 *
 * see OvmfPkg/Library/HardwareInfoLib/ in edk2
 */

#include "qemu/osdep.h"

#include "hw/nvram/fw_cfg.h"
#include "hw/uefi/hardware-info.h"

static void      *blob;
static uint64_t  blobsize;

void hardware_info_register(HARDWARE_INFO_TYPE type, void *info, uint64_t infosize)
{
    HARDWARE_INFO_HEADER hdr = {
        .type.value = cpu_to_le64(type),
        .size       = cpu_to_le64(infosize),
    };

    blob = g_realloc(blob, blobsize + sizeof(hdr) + infosize);
    memcpy(blob + blobsize, &hdr, sizeof(hdr));
    blobsize += sizeof(hdr);
    memcpy(blob + blobsize, info, infosize);
    blobsize += infosize;

    fw_cfg_modify_file(fw_cfg_find(), "etc/hardware-info", blob, blobsize);
}
