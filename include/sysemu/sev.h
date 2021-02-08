/*
 * QEMU Secure Encrypted Virutualization (SEV) support
 *
 * Copyright: Advanced Micro Devices, 2016-2018
 *
 * Authors:
 *  Brijesh Singh <brijesh.singh@amd.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_SEV_H
#define QEMU_SEV_H

#include "sysemu/kvm.h"

bool sev_enabled(void);
int sev_kvm_init(ConfidentialGuestSupport *cgs, Error **errp);
int sev_encrypt_flash(uint8_t *ptr, uint64_t len, Error **errp);
int sev_inject_launch_secret(const char *hdr, const char *secret,
                             uint64_t gpa, Error **errp);

int sev_es_save_reset_vector(void *flash_ptr, uint64_t flash_size);
void sev_es_set_reset_vector(CPUState *cpu);

#endif
