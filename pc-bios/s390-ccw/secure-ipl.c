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

    if (!sclp_is_fac_ipl_flag_on(SCCB_FAC_IPL_SCLAF_BIT)) {
        puts("Secure IPL Code Loading Attributes Facility is not supported by"
             " the hypervisor!");
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

static void check_comp_overlap(IplDeviceComponentList *comp_list,
                               IplDeviceComponentEntry comp_entry)
{
    IplDeviceComponentEntry *comp;

    /*
     * Check component's address range does not overlap with any
     * signed component's address range.
     */
    for_each_rb_entry(comp, comp_list) {
        if (comp->flags & S390_IPL_DEV_COMP_FLAG_SC &&
            intersects(comp->addr, comp->len, comp_entry.addr, comp_entry.len)) {
            zipl_secure_error("Component addresses overlap");
        }
    }
}

static bool is_psw_valid(uint64_t psw, IplDeviceComponentEntry *comp)
{
    uint32_t addr = psw & 0x7fffffff;

    /*
     * PSW points within a signed binary code component
     *
     * Check addr falls within [comp->addr, comp->addr + comp->len - 2],
     * ensuring at least 2 bytes (minimum instruction length) remain.
     */
    return intersects(addr, 1, comp->addr, comp->len - 1);
}

void check_global_sclab(const SclaBlock *global_sclab,
                        IplDeviceComponentEntry *comp_entry,
                        IplDeviceComponentList *comp_list)
{
    bool psw_valid = false;
    bool global_psw_valid = false;
    int signed_count = 0;
    int unsigned_count = 0;
    IplDeviceComponentEntry *comp;

    if (!global_sclab) {
        comp_list->ipl_info_header.iiei |= S390_IIEI_NO_GLOBAL_SCLAB;
        zipl_secure_error("Global SCLAB does not exist");
        return;
    }

    for_each_rb_entry(comp, comp_list) {
        if (comp->flags & S390_IPL_DEV_COMP_FLAG_SC) {
            psw_valid |= is_psw_valid(comp_entry->addr, comp);
            global_psw_valid |= is_psw_valid(global_sclab->load_psw, comp);
            signed_count += 1;
        } else {
            unsigned_count += 1;
        }
    }

    /* validate load PSW with PSW specified in the final entry */
    zipl_secure_validate(psw_valid && global_psw_valid, &comp_entry->cei,
                         S390_CEI_INVALID_LOAD_PSW, "Invalid PSW");

    /* compare load PSW with the PSW specified in component */
    zipl_secure_validate(global_sclab->load_psw == comp_entry->addr,
                         &comp_entry->cei, S390_CEI_UNMATCHED_SCLAB_LOAD_PSW,
                         "Load PSW does not match with PSW in component");

    /* Unsigned components are not allowed if NUC flag is set in the global SCLAB */
    if ((global_sclab->flags & S390_SCLAB_NUC) && unsigned_count > 0) {
        comp_list->ipl_info_header.iiei |= S390_IIEI_FOUND_UNSIGNED_COMP;
        zipl_secure_error("Unsigned components are not allowed");
    }

    /*
     * Only one signed component is allowed if SC flag is set in the global SCLAB
     * More than one component in the component table is not allowed
     */
    if ((global_sclab->flags & S390_SCLAB_SC) &&
        (signed_count != 1 || unsigned_count != 0)) {
        comp_list->ipl_info_header.iiei |= S390_IIEI_MORE_SIGNED_COMP;
        zipl_secure_error("Only one signed component is allowed");
    }
}

static void check_sclab(SclaBlock **global_sclab,
                        IplDeviceComponentEntry *comp_entry,
                        IplInfoBlockHeader *comp_list_hdr)
{
    SclabOriginLocator *sclab_locator;
    SclaBlock *sclab;

    /* must be large enough to locate the sclab locator, else implies invalid SCLAB */
    zipl_secure_validate(comp_entry->len >= 8, &comp_entry->cei,
                         S390_CEI_INVALID_SCLAB,
                         "Signed component too short to contain SCLAB locator");

    if (comp_entry->cei & S390_CEI_INVALID_SCLAB) {
        return;
    }

    /* sclab locator is located at the last 8 bytes of the signed comp */
    sclab_locator = (SclabOriginLocator *)(comp_entry->addr +
                                           comp_entry->len - 8);

    /* return early if sclab does not exist */
    zipl_secure_validate(magic_match(sclab_locator->magic, ZIPL_MAGIC),
                         &comp_entry->cei, S390_CEI_INVALID_SCLAB,
                         "Magic does not match. SCLAB does not exist");

    if (comp_entry->cei & S390_CEI_INVALID_SCLAB) {
        return;
    }

    zipl_secure_validate(sclab_locator->len >= S390_SCLAB_MIN_LEN, &comp_entry->cei,
                         S390_CEI_INVALID_SCLAB_LEN | S390_CEI_INVALID_SCLAB,
                         "Invalid SCLAB length");

    /* return early if sclab is invalid */
    if (comp_entry->cei & S390_CEI_INVALID_SCLAB) {
        return;
    }

    sclab = (SclaBlock *)(comp_entry->addr + comp_entry->len -
                          sclab_locator->len);

    zipl_secure_validate(sclab->format == 0, &comp_entry->cei,
                         S390_CEI_INVALID_SCLAB_FORMAT,
                         "Format-0 SCLAB is not being used");

    if (!(sclab->flags & S390_SCLAB_OPSW)) {
        /* OPSW = 0 - Load PSW field in SCLAB must contain zeros */
        zipl_secure_validate(sclab->load_psw == 0, &comp_entry->cei,
                             S390_CEI_SCLAB_LOAD_PSW_NOT_ZERO,
                             "Load PSW is not zero when Override PSW bit is zero");
    } else {
        /* OPSW = 1 indicating global SCLAB */
        if (*global_sclab) {
            comp_list_hdr->iiei |= S390_IIEI_MORE_GLOBAL_SCLAB;
            zipl_secure_error("More than one global SCLAB");
        }
        *global_sclab = sclab;

        /* override load address flag must set to one */
        zipl_secure_validate(sclab->flags & S390_SCLAB_OLA, &comp_entry->cei,
                             S390_CEI_SCLAB_OLA_NOT_ONE,
                             "OLA flag is not set to one in the global SCLAB");
    }

    if (!(sclab->flags & S390_SCLAB_OLA)) {
        /* OLA = 0 - Load address field in SCLAB must contain zeros */
        zipl_secure_validate(sclab->load_addr == 0, &comp_entry->cei,
                             S390_CEI_SCLAB_LOAD_ADDR_NOT_ZERO,
                             "Load Address is not zero when OLA flag is zero");
    } else {
        /* OLA = 1 - Load address field must match storage address of the component */
        zipl_secure_validate(sclab->load_addr == comp_entry->addr, &comp_entry->cei,
                             S390_CEI_UNMATCHED_SCLAB_LOAD_ADDR,
                             "Load Address does not match with component load address");
    }

    zipl_secure_validate(~sclab->flags & S390_SCLAB_NUC || sclab->flags & S390_SCLAB_OPSW,
                         &comp_entry->cei, S390_CEI_NUC_NOT_IN_GLOBAL_SCLAB,
                         "NUC bit is set, but not in the global SCLAB");

    zipl_secure_validate(~sclab->flags & S390_SCLAB_SC || sclab->flags & S390_SCLAB_OPSW,
                         &comp_entry->cei, S390_CEI_SC_NOT_IN_GLOBAL_SCLAB,
                         "SC bit is set, but not in the global SCLAB");
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
    bool sclab_found = false;
    SclaBlock *global_sclab = NULL;

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

            check_comp_overlap(comp_list, comp_entry);

            /* no signature present (unsigned component) */
            if (!sig_entry.len) {
                zipl_secure_validate(comp_entry.addr >= S390_UNSIGNED_MIN_ADDR,
                            &comp_entry.cei, S390_CEI_INVALID_UNSIGNED_ADDR,
                            "Load address for unsigned component is less than 0x2000");

                comp_list_add(comp_list, comp_entry);
                break;
            }

            /*
             * Initialize with SC flag (signed component)
             * CSV flag set upon successful verification
             */
            comp_entry.flags = S390_IPL_DEV_COMP_FLAG_SC;
            signed_found = true;

            check_sclab(&global_sclab, &comp_entry, &comp_list->ipl_info_header);
            sclab_found |= !(comp_entry.cei & S390_CEI_INVALID_SCLAB);

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

    zipl_secure_validate(signed_found, &comp_list->ipl_info_header.iiei,
                         S390_IIEI_NO_SIGNED_COMP,
                         "Secure boot is on, but components are not signed");

    zipl_secure_validate(sclab_found, &comp_list->ipl_info_header.iiei,
                         S390_IIEI_NO_SCLAB, "No recognizable SCLAB");

    comp_entry = (IplDeviceComponentEntry){ 0 };
    comp_entry.addr = entry->compdat.load_psw;
    check_global_sclab(global_sclab, &comp_entry, comp_list);
    comp_list_add(comp_list, comp_entry);

    *entry_ptr = entry;
    free((void *)sig_entry.addr);

    return 0;
error:
    free(*tmp_cert_buf);
    *tmp_cert_buf = NULL;
    free((void *)sig_entry.addr);

    return rc;
}
