/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * uefi vars device - pkcs7 stubs
 */
#include "qemu/osdep.h"
#include "system/dma.h"

#include "hw/uefi/var-service.h"

efi_status uefi_vars_check_pkcs7_2(uefi_variable *siglist,
                                   void **digest, uint32_t *digest_size,
                                   mm_variable_access *va, void *data)
{
    return EFI_WRITE_PROTECTED;
}
