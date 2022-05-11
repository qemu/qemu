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

#ifndef I386_SEV_H
#define I386_SEV_H

#ifndef CONFIG_USER_ONLY
#include CONFIG_DEVICES /* CONFIG_SEV */
#endif

#include "exec/confidential-guest-support.h"

#define SEV_POLICY_NODBG        0x1
#define SEV_POLICY_NOKS         0x2
#define SEV_POLICY_ES           0x4
#define SEV_POLICY_NOSEND       0x8
#define SEV_POLICY_DOMAIN       0x10
#define SEV_POLICY_SEV          0x20

typedef struct SevKernelLoaderContext {
    char *setup_data;
    size_t setup_size;
    char *kernel_data;
    size_t kernel_size;
    char *initrd_data;
    size_t initrd_size;
    char *cmdline_data;
    size_t cmdline_size;
} SevKernelLoaderContext;

#ifdef CONFIG_SEV
bool sev_enabled(void);
bool sev_es_enabled(void);
#else
#define sev_enabled() 0
#define sev_es_enabled() 0
#endif

extern uint32_t sev_get_cbit_position(void);
extern uint32_t sev_get_reduced_phys_bits(void);
extern bool sev_add_kernel_loader_hashes(SevKernelLoaderContext *ctx, Error **errp);

int sev_encrypt_flash(uint8_t *ptr, uint64_t len, Error **errp);
int sev_inject_launch_secret(const char *hdr, const char *secret,
                             uint64_t gpa, Error **errp);

int sev_es_save_reset_vector(void *flash_ptr, uint64_t flash_size);
void sev_es_set_reset_vector(CPUState *cpu);

int sev_kvm_init(ConfidentialGuestSupport *cgs, Error **errp);

#endif
