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

#ifndef QEMU_SEV_I386_H
#define QEMU_SEV_I386_H

#include "qom/object.h"
#include "qapi/error.h"
#include "sysemu/kvm.h"
#include "sysemu/sev.h"
#include "qemu/error-report.h"
#include "qapi/qapi-types-misc-target.h"

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

extern bool sev_es_enabled(void);
extern uint64_t sev_get_me_mask(void);
extern SevInfo *sev_get_info(void);
extern uint32_t sev_get_cbit_position(void);
extern uint32_t sev_get_reduced_phys_bits(void);
extern char *sev_get_launch_measurement(void);
extern SevCapability *sev_get_capabilities(Error **errp);
extern SevAttestationReport *
sev_get_attestation_report(const char *mnonce, Error **errp);
extern bool sev_add_kernel_loader_hashes(SevKernelLoaderContext *ctx, Error **errp);

#endif
