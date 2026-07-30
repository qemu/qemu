/*
 * S/390 Secure IPL
 *
 * Functions to support IPL in secure boot mode (DIAG 320, DIAG 508,
 * signature verification, and certificate handling).
 *
 * For secure IPL overview: docs/system/s390x/secure-ipl.rst
 * For secure IPL technical: docs/specs/s390x-secure-ipl.rst
 *
 * Copyright 2025 IBM Corp.
 * Author(s): Zhuoying Cai <zycai@linux.ibm.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "s390-ccw.h"
#include "sclp.h"
#include "secure-ipl.h"

static VCStorageSizeBlock vcssb __attribute__((__aligned__(8)));

#define for_each_rb_entry(entry, list) \
    for (entry = (void *)(list) + sizeof((list)->ipl_info_header); \
         (void *)(entry) + sizeof(*(entry)) <= \
         (void *)(list) + (list)->ipl_info_header.len; \
         entry++)

int zipl_secure_get_vcssb(void)
{
    /* avoid retrieving vcssb multiple times */
    if (vcssb.length == VCSSB_LEN_VALID) {
        goto out;
    }

    vcssb.length = VCSSB_LEN_VALID;
    if (_diag320(&vcssb, DIAG_320_SUBC_QUERY_VCSI) != DIAG_320_RC_OK) {
        vcssb.length = 0;
    }

out:
    return vcssb.length;
}

static uint32_t request_certificate(uint8_t *cert_buf, uint8_t index)
{
    VCEntryHeader *vce_hdr;
    struct vcb {
        VCBlockHeader vcb_hdr;
        struct vce {
            VCEntryHeader vce_hdr;
            uint8_t cert_buf[CERT_BUF_MAX_LEN];
        } vce;
    } __attribute__((__aligned__(PAGE_SIZE))) vcb = { 0 };

    /*
     * Request single entry
     * Fill input fields of single-entry VCB
     *
     * First and last index must be equal because only one
     * VCE per VCB is currently supported
     */
    vcb.vcb_hdr.in_len = ROUND_UP(vcssb.max_single_vcb_len, PAGE_SIZE);
    vcb.vcb_hdr.first_vc_index = index;
    vcb.vcb_hdr.last_vc_index = index;

    if (_diag320(&vcb, DIAG_320_SUBC_STORE_VC) != DIAG_320_RC_OK) {
        puts("Could not get certificate");
        return 0;
    }

    if (vcb.vcb_hdr.out_len == sizeof(VCBlockHeader)) {
        puts("No certificate entry");
        return 0;
    }

    if (vcb.vcb_hdr.remain_ct != 0) {
        panic("Not enough memory to store requested certificate");
    }

    vce_hdr = &vcb.vce.vce_hdr;
    if (!(vce_hdr->flags & DIAG_320_VCE_FLAGS_VALID)) {
        puts("Invalid certificate");
        return 0;
    }

    memcpy(cert_buf, (uint8_t *)&vcb.vce + vce_hdr->cert_offset, vce_hdr->cert_len);

    return vce_hdr->cert_len;
}

static int cert_list_add(IplSignatureCertificateList *cert_list,
                         IplSignatureCertificateEntry cert_entry)
{
    int cert_entry_idx;

    cert_entry_idx = (cert_list->ipl_info_header.len - sizeof(IplInfoBlockHeader)) /
                     sizeof(IplSignatureCertificateEntry);

    cert_list->cert_entries[cert_entry_idx] = cert_entry;
    cert_list->ipl_info_header.len += sizeof(IplSignatureCertificateEntry);

    return cert_entry_idx;
}

static void comp_list_add(IplDeviceComponentList *comp_list,
                          IplDeviceComponentEntry comp_entry)
{
    int comp_entry_idx;

    comp_entry_idx = (comp_list->ipl_info_header.len - sizeof(IplInfoBlockHeader)) /
                     sizeof(IplDeviceComponentEntry);
    if (comp_entry_idx > MAX_COMP_ENTRIES - 1) {
        printf("Warning: only %d component entries are supported\n",
                MAX_COMP_ENTRIES);
        panic("The device component list has reached its maximum capacity");
    }

    comp_list->device_entries[comp_entry_idx] = comp_entry;
    comp_list->ipl_info_header.len += sizeof(IplDeviceComponentEntry);
}

void update_iirb(IplDeviceComponentList *comp_list,
                 IplSignatureCertificateList *cert_list)
{
    IplInfoReportBlock *iirb;
    IplDeviceComponentList *iirb_comps;
    IplSignatureCertificateList *iirb_certs;
    uint32_t iirb_hdr_len;
    uint32_t comps_len;
    uint32_t certs_len;

    if (iplb->len % 8 != 0) {
        panic("IPL parameter block length field value is not multiple of 8 bytes");
    }

    iirb_hdr_len = sizeof(IplInfoReportBlockHeader);
    comps_len = comp_list->ipl_info_header.len;
    certs_len = cert_list->ipl_info_header.len;
    if ((comps_len + certs_len + iirb_hdr_len) > sizeof(IplInfoReportBlock)) {
        panic("Not enough space to hold all components and certificates in IIRB");
    }

    /* IIRB immediately follows IPLB */
    iirb = &ipl_blocks.iirb;
    iirb->hdr.len = iirb_hdr_len;

    /* Copy IPL device component list after IIRB Header */
    iirb_comps = (IplDeviceComponentList *) iirb->info_blks;
    memcpy(iirb_comps, comp_list, comps_len);

    /* Update IIRB length */
    iirb->hdr.len += comps_len;

    /* Copy IPL sig cert list after IPL device component list */
    iirb_certs = (IplSignatureCertificateList *) (iirb->info_blks +
                                                  iirb_comps->ipl_info_header.len);
    memcpy(iirb_certs, cert_list, certs_len);

    /* Update IIRB length */
    iirb->hdr.len += certs_len;
}

bool secure_ipl_supported(void)
{
    if (!sclp_is_fac_ipl_flag_on(SCCB_FAC_IPL_SIPL_BIT)) {
        puts("Secure IPL Facility is not supported by the hypervisor!");
        return false;
    }

    if (!is_signature_verif_supported()) {
        puts("Secure IPL extensions are not supported by the hypervisor!");
        return false;
    }

    if (!is_cert_store_facility_supported()) {
        puts("Certificate Store Facility is not supported by the hypervisor!");
        return false;
    }

    return true;
}

static void init_lists(IplDeviceComponentList *comp_list,
                       IplSignatureCertificateList *cert_list)
{
    comp_list->ipl_info_header.type = IPL_INFO_BLOCK_TYPE_COMPONENTS;
    comp_list->ipl_info_header.len = sizeof(IplInfoBlockHeader);

    cert_list->ipl_info_header.type = IPL_INFO_BLOCK_TYPE_CERTIFICATES;
    cert_list->ipl_info_header.len = sizeof(IplInfoBlockHeader);
}

static int zipl_load_signature(ComponentEntry *entry, uint64_t sig)
{
    if (entry->compdat.sig_info.format != DER_SIGNATURE_FORMAT) {
        puts("Signature is not in DER format");
        return -1;
    }

    if (zipl_load_segment(entry->data.blockno, sig) < 0) {
        return -1;
    }

    return entry->compdat.sig_info.sig_len;
}

void update_cert_list(IplSignatureCertificateList *cert_list)
{
    IplSignatureCertificateEntry *cert_entry;
    uint8_t *cert_buf;

    /*
     * Recover the original base address of ipl_data for cert storage.
     *
     * The IplParameterBlocks stored in ipl_data will no longer be needed
     * after this point. Reuse this region to store certificates from the
     * BIOS heap into stable memory.
     */
    cert_buf = (uint8_t *)qipl.ipl_data - qipl.index * sizeof(IplParameterBlock);

    for_each_rb_entry(cert_entry, cert_list) {
        memcpy(cert_buf, (uint8_t *)cert_entry->addr, cert_entry->len);
        cert_entry->addr = (uint64_t)cert_buf;
        cert_buf += cert_entry->len;
    }
}

int zipl_run_secure(ComponentEntry **entry_ptr, const uint8_t *tmp_sec,
                    IplDeviceComponentList *comp_list,
                    IplSignatureCertificateList *cert_list,
                    uint8_t **tmp_cert_buf)
{
    /*
     * Keep track of which certificate store indices correspond to the
     * certificate data entries within the IplSignatureCertificateList to
     * prevent allocating space for the same certificate multiple times.
     *
     * The array index corresponds to the certificate's cert-store index.
     *
     * The array value corresponds to the certificate's entry within the
     * IplSignatureCertificateList (with a value of -1 denoting no entry
     * exists for the certificate).
     */
    int cert_list_table[vcssb.total_vc_ct + 1];
    IplSignatureCertificateEntry sig_entry = { 0 };
    IplSignatureCertificateEntry cert_entry;
    IplDeviceComponentEntry comp_entry;
    ComponentEntry *entry = *entry_ptr;
    int rc = -1;
    int sig_len = 0;
    int comp_len;
    int cert_entry_idx;
    uint64_t comp_addr;
    uint8_t cert_table_idx;
    uint8_t *tmp_buf;
    bool verified;
    bool signed_found = false;

    if ((MAX_SIGNED_COMP * CERT_BUF_MAX_LEN) > CERT_BUF_SIZE) {
        panic("Not enough memory to store certificates");
    }
    *tmp_cert_buf = malloc(CERT_BUF_SIZE);
    tmp_buf = *tmp_cert_buf;

    init_lists(comp_list, cert_list);
    sig_entry.addr = (uint64_t)malloc(MAX_SECTOR_SIZE);
    memset(cert_list_table, -1, sizeof(cert_list_table));

    while (entry->component_type != ZIPL_COMP_ENTRY_EXEC) {
        switch (entry->component_type) {
        case ZIPL_COMP_ENTRY_SIGNATURE:
            if (sig_entry.len) {
                goto error;
            }

            sig_len = zipl_load_signature(entry, sig_entry.addr);
            if (sig_len < 0) {
                goto error;
            }

            sig_entry.len = sig_len;
            break;
        case ZIPL_COMP_ENTRY_LOAD:
            comp_addr = entry->compdat.load_addr;
            comp_len = zipl_load_segment(entry->data.blockno, comp_addr);
            if (comp_len < 0) {
                goto error;
            }

            comp_entry = (IplDeviceComponentEntry){ 0 };
            comp_entry.addr = comp_addr;
            comp_entry.len = (uint64_t)comp_len;

            /* no signature present (unsigned component) */
            if (!sig_entry.len) {
                comp_list_add(comp_list, comp_entry);
                break;
            }

            /*
             * Initialize with SC flag (signed component)
             * CSV flag set upon successful verification
             */
            comp_entry.flags = S390_IPL_DEV_COMP_FLAG_SC;
            signed_found = true;

            cert_entry = (IplSignatureCertificateEntry) { 0 };
            verified = verify_signature(comp_entry, sig_entry,
                                        &cert_entry.len, &cert_table_idx);

            if (verified) {
                if (cert_list_table[cert_table_idx] == -1) {
                    if (!request_certificate(tmp_buf, cert_table_idx)) {
                        puts("Could not get certificate");
                        goto error;
                    }

                    cert_entry.addr = (uint64_t)tmp_buf;
                    cert_entry_idx = cert_list_add(cert_list, cert_entry);
                    /* map cert-store index to cert-list entry index */
                    cert_list_table[cert_table_idx] = cert_entry_idx;
                    /* increment for the next certificate */
                    tmp_buf += cert_entry.len;
                }

                comp_entry.cert_index = cert_list_table[cert_table_idx];
                comp_entry.flags |= S390_IPL_DEV_COMP_FLAG_CSV;
                puts("Verified component");
            } else {
                zipl_secure_error("Could not verify component");
            }

            comp_list_add(comp_list, comp_entry);

            /* After a signature is used another new one can be accepted */
            sig_entry.len = 0;
            break;
        default:
            puts("Unknown component entry type");
            goto error;
        }

        entry++;

        if ((uint8_t *)(&entry[1]) > tmp_sec + MAX_SECTOR_SIZE) {
            puts("Wrong entry value");
            rc = -EINVAL;
            goto error;
        }
    }

    if (!signed_found) {
        zipl_secure_error("Secure boot is on, but components are not signed");
    }

    *entry_ptr = entry;
    free((void *)sig_entry.addr);

    return 0;
error:
    free(*tmp_cert_buf);
    *tmp_cert_buf = NULL;
    free((void *)sig_entry.addr);

    return rc;
}
