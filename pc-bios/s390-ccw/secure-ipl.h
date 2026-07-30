/*
 * S/390 Secure IPL
 *
 * Copyright 2025 IBM Corp.
 * Author(s): Zhuoying Cai <zycai@linux.ibm.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef _PC_BIOS_S390_CCW_SECURE_IPL_H
#define _PC_BIOS_S390_CCW_SECURE_IPL_H

#include "bootmap.h"
#include <diag320.h>
#include <diag508.h>

#define MAX_SIGNED_COMP     3

int zipl_secure_get_vcssb(void);
bool secure_ipl_supported(void);
void update_iirb(IplDeviceComponentList *comp_list,
                 IplSignatureCertificateList *cert_list);
void update_cert_list(IplSignatureCertificateList *cert_list);
int zipl_run_secure(ComponentEntry **entry_ptr, const uint8_t *tmp_sec,
                    IplDeviceComponentList *comp_list,
                    IplSignatureCertificateList *cert_list,
                    uint8_t **tmp_cert_buf);

static inline void zipl_secure_error(const char *message)
{
    switch (boot_mode) {
    case ZIPL_BOOT_MODE_SECURE_AUDIT:
        printf("AUDIT MODE WARNING: %s\n", message);
        break;
    default:
        /*
         * Errors are intentionally ignored in non-secure boot modes.
         * This function should only be reached in SECURE modes.
         */
        break;
    }
}

static inline uint64_t _diag320(void *data, unsigned long subcode)
{
    register unsigned long addr asm("0") = (unsigned long)data;
    register unsigned long rc asm("1") = 0;

    asm volatile ("diag %0,%2,0x320\n"
                  : "+d" (addr), "+d" (rc)
                  : "d" (subcode)
                  : "memory", "cc");
    return rc;
}

static inline bool is_cert_store_facility_supported(void)
{
    uint32_t d320_ism;

    if (!sclp_is_diag320_on()) {
        return false;
    }

    if (_diag320(&d320_ism, DIAG_320_SUBC_QUERY_ISM) != DIAG_320_RC_OK) {
        return false;
    }

    return d320_ism & (DIAG_320_ISM_QUERY_VCSI | DIAG_320_ISM_STORE_VC);
}

static inline uint64_t _diag508(void *data, unsigned long subcode)
{
    register unsigned long addr asm("0") = (unsigned long)data;
    register unsigned long rc asm("1") = 0;

    asm volatile ("diag %0,%2,0x508\n"
                  : "+d" (addr), "+d" (rc)
                  : "d" (subcode)
                  : "memory", "cc");
    return rc;
}

static inline bool is_signature_verif_supported(void)
{
    uint64_t d508_subcodes;

    d508_subcodes = _diag508(NULL, DIAG_508_SUBC_QUERY_SUBC);
    return d508_subcodes & DIAG_508_SUBC_SIG_VERIF;
}

static inline bool verify_signature(IplDeviceComponentEntry comp_entry,
                                    IplSignatureCertificateEntry sig_entry,
                                    uint64_t *cert_len, uint8_t *cert_idx)
{
    Diag508SigVerifBlock svb;

    svb.length = sizeof(Diag508SigVerifBlock);
    svb.version = 0;
    svb.comp_len = comp_entry.len;
    svb.comp_addr = comp_entry.addr;
    svb.sig_len = sig_entry.len;
    svb.sig_addr = sig_entry.addr;

    if (_diag508(&svb, DIAG_508_SUBC_SIG_VERIF) == DIAG_508_RC_OK) {
        *cert_len = svb.cert_len;
        /*
         * DIAG 508 utilizes an index origin of 0 when indexing the cert store.
         * The cert_idx will be used for DIAG 320 data structures, which expects
         * an index origin of 1. Account for the offset here so it's easier to
         * manage later.
         */
        *cert_idx = svb.cert_store_index + 1;
        return true;
    }

    return false;
}

#endif /* _PC_BIOS_S390_CCW_SECURE_IPL_H */
