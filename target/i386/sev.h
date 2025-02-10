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

#if !defined(CONFIG_SEV) || defined(CONFIG_USER_ONLY)
#define sev_enabled() 0
#define sev_es_enabled() 0
#define sev_snp_enabled() 0
#else
bool sev_enabled(void);
bool sev_es_enabled(void);
bool sev_snp_enabled(void);
#endif

#if !defined(CONFIG_USER_ONLY)

#define TYPE_SEV_COMMON "sev-common"
#define TYPE_SEV_GUEST "sev-guest"
#define TYPE_SEV_SNP_GUEST "sev-snp-guest"

#define SEV_POLICY_NODBG        0x1
#define SEV_POLICY_NOKS         0x2
#define SEV_POLICY_ES           0x4
#define SEV_POLICY_NOSEND       0x8
#define SEV_POLICY_DOMAIN       0x10
#define SEV_POLICY_SEV          0x20

#define SEV_SNP_POLICY_SMT      0x10000
#define SEV_SNP_POLICY_DBG      0x80000

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

bool sev_add_kernel_loader_hashes(SevKernelLoaderContext *ctx, Error **errp);

int sev_encrypt_flash(hwaddr gpa, uint8_t *ptr, uint64_t len, Error **errp);
int sev_inject_launch_secret(const char *hdr, const char *secret,
                             uint64_t gpa, Error **errp);

int sev_es_save_reset_vector(void *flash_ptr, uint64_t flash_size);
void sev_es_set_reset_vector(CPUState *cpu);

void pc_system_parse_sev_metadata(uint8_t *flash_ptr, size_t flash_size);

#endif /* !CONFIG_USER_ONLY */

uint32_t sev_get_cbit_position(void);
uint32_t sev_get_reduced_phys_bits(void);

#endif
