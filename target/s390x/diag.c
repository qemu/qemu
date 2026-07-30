/*
 * S390x DIAG instruction helper functions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "s390x-internal.h"
#include "hw/watchdog/wdt_diag288.h"
#include "system/cpus.h"
#include "hw/s390x/cert-store.h"
#include "hw/s390x/ipl.h"
#include "hw/s390x/ipl/diag320.h"
#include "hw/s390x/ipl/diag508.h"
#include "hw/s390x/s390-virtio-ccw.h"
#include "system/kvm.h"
#include "kvm/kvm_s390x.h"
#include "target/s390x/kvm/pv.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "crypto/x509-utils.h"


static inline bool diag_parm_addr_valid(uint64_t addr, size_t size, bool write)
{
    return address_space_access_valid(&address_space_memory, addr,
                                      size, write, MEMTXATTRS_UNSPECIFIED);
}

int handle_diag_288(CPUS390XState *env, uint64_t r1, uint64_t r3)
{
    uint64_t func = env->regs[r1];
    uint64_t timeout = env->regs[r1 + 1];
    uint64_t action = env->regs[r3];
    Object *obj;
    DIAG288State *diag288;
    DIAG288Class *diag288_class;

    if (r1 % 2 || action != 0) {
        return -1;
    }

    /* Timeout must be more than 15 seconds except for timer deletion */
    if (func != WDT_DIAG288_CANCEL && timeout < 15) {
        return -1;
    }

    obj = object_resolve_path_type("", TYPE_WDT_DIAG288, NULL);
    if (!obj) {
        return -1;
    }

    diag288 = DIAG288(obj);
    diag288_class = DIAG288_GET_CLASS(diag288);
    return diag288_class->handle_timer(diag288, func, timeout);
}

static int diag308_parm_check(CPUS390XState *env, uint64_t r1, uint64_t addr,
                              uintptr_t ra, bool write)
{
    /* Handled by the Ultravisor */
    if (s390_is_pv()) {
        return 0;
    }
    if ((r1 & 1) || (addr & ~TARGET_PAGE_MASK)) {
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return -1;
    }
    if (!diag_parm_addr_valid(addr, sizeof(IplParameterBlock), write)) {
        s390_program_interrupt(env, PGM_ADDRESSING, ra);
        return -1;
    }
    return 0;
}

static void s390_ipl_read(CPUS390XState *env, uint64_t addr,
                          IplParameterBlock *iplb, size_t size)
{
    if (s390_is_pv()) {
        s390_cpu_pv_mem_read(env_archcpu(env), 0, iplb, size);
    } else {
        address_space_read(cpu_get_address_space(env_cpu(env), 0), addr,
                           MEMTXATTRS_UNSPECIFIED, iplb, size);
    }
}

static void s390_ipl_write(CPUS390XState *env, uint64_t addr,
                           IplParameterBlock *iplb, size_t size)
{
    if (s390_is_pv()) {
        s390_cpu_pv_mem_write(env_archcpu(env), 0, iplb, size);
    } else {
        address_space_write(cpu_get_address_space(env_cpu(env), 0), addr,
                            MEMTXATTRS_UNSPECIFIED, iplb, size);
    }
}

bool handle_diag_308(CPUS390XState *env, uint64_t r1, uint64_t r3, uintptr_t ra)
{
    bool valid;
    CPUState *cs = env_cpu(env);
    uint64_t addr =  env->regs[r1];
    uint64_t subcode = env->regs[r3];
    IplParameterBlock *iplb;

    if (env->psw.mask & PSW_MASK_PSTATE) {
        s390_program_interrupt(env, PGM_PRIVILEGED, ra);
        return false;
    }

    if (subcode & ~0x0ffffULL) {
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return false;
    }

    if (subcode >= DIAG308_PV_SET && !s390_has_feat(S390_FEAT_UNPACK)) {
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return false;
    }

    switch (subcode) {
    case DIAG308_RESET_MOD_CLR:
        s390_ipl_reset_request(cs, S390_RESET_MODIFIED_CLEAR);
        return true;
    case DIAG308_RESET_LOAD_NORM:
        s390_ipl_reset_request(cs, S390_RESET_LOAD_NORMAL);
        return true;
    case DIAG308_LOAD_CLEAR:
        /* Well we still lack the clearing bit... */
        s390_ipl_reset_request(cs, S390_RESET_REIPL);
        return true;
    case DIAG308_SET:
    case DIAG308_PV_SET:
        if (diag308_parm_check(env, r1, addr, ra, false)) {
            return false;
        }
        iplb = g_new0(IplParameterBlock, 1);
        s390_ipl_read(env, addr, iplb, sizeof(iplb->len));
        if (!iplb_valid_len(iplb)) {
            env->regs[r1 + 1] = DIAG_308_RC_INVALID;
            goto out;
        }
        s390_ipl_read(env, addr, iplb, be32_to_cpu(iplb->len));

        valid = subcode == DIAG308_PV_SET ? iplb_valid_pv(iplb) : iplb_valid(iplb);
        if (!valid) {
            if (subcode == DIAG308_SET && iplb->pbt == S390_IPL_TYPE_QEMU_SCSI) {
                s390_rebuild_iplb(iplb->devno, iplb);
                s390_ipl_update_diag308(iplb);
                env->regs[r1 + 1] = DIAG_308_RC_OK;
            } else {
                env->regs[r1 + 1] = DIAG_308_RC_INVALID;
            }

            goto out;
        }

        s390_ipl_update_diag308(iplb);
        env->regs[r1 + 1] = DIAG_308_RC_OK;
out:
        g_free(iplb);
        return false;
    case DIAG308_STORE:
    case DIAG308_PV_STORE:
        if (diag308_parm_check(env, r1, addr, ra, true)) {
            return false;
        }
        if (subcode == DIAG308_PV_STORE) {
            iplb = s390_ipl_get_iplb_pv();
        } else {
            iplb = s390_ipl_get_iplb();
        }
        if (!iplb) {
            env->regs[r1 + 1] = DIAG_308_RC_NO_CONF;
            return false;
        }

        s390_ipl_write(env, addr, iplb, be32_to_cpu(iplb->len));
        env->regs[r1 + 1] = DIAG_308_RC_OK;
        return false;
    case DIAG308_PV_START:
        iplb = s390_ipl_get_iplb_pv();
        if (!iplb) {
            env->regs[r1 + 1] = DIAG_308_RC_NO_PV_CONF;
            return false;
        }

        if (kvm_enabled() && kvm_s390_get_hpage()) {
            error_report("Protected VMs can currently not be backed with "
                         "huge pages");
            env->regs[r1 + 1] = DIAG_308_RC_INVAL_FOR_PV;
            return false;
        }

        s390_ipl_reset_request(cs, S390_RESET_PV);
        return true;
    default:
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return false;
    }
}

static int handle_diag320_query_vcsi(S390CPU *cpu, uint64_t addr, uint64_t r1,
                                     uintptr_t ra, S390IPLCertificateStore *cs)
{
    g_autofree VCStorageSizeBlock *vcssb = NULL;

    vcssb = g_new0(VCStorageSizeBlock, 1);
    if (s390_cpu_virt_mem_read(cpu, addr, r1, vcssb, sizeof(*vcssb))) {
        s390_cpu_virt_mem_handle_exc(cpu, ra);
        return -1;
    }

    if (be32_to_cpu(vcssb->length) != VCSSB_LEN_VALID) {
        return DIAG_320_RC_INVAL_VCSSB_LEN;
    }

    if (!cs->count) {
        vcssb->length = cpu_to_be32(VCSSB_NO_VC);
    } else {
        vcssb->version = 0;
        vcssb->total_vc_ct = cpu_to_be16(cs->count);
        vcssb->max_vc_ct = cpu_to_be16(MAX_CERTIFICATES);
        vcssb->max_single_vcb_len = cpu_to_be32(sizeof(VCBlockHeader) +
                                                sizeof(VCEntryHeader) +
                                                cs->largest_cert_size);
        vcssb->total_vcb_len = cpu_to_be32(sizeof(VCBlockHeader) +
                                           cs->count * sizeof(VCEntryHeader) +
                                           cs->total_bytes);
    }

    if (s390_cpu_virt_mem_write(cpu, addr, r1, vcssb, be32_to_cpu(vcssb->length))) {
        s390_cpu_virt_mem_handle_exc(cpu, ra);
        return -1;
    }
    return DIAG_320_RC_OK;
}

static bool is_cert_valid(const S390IPLCertificate *cert)
{
    int rc;
    Error *err = NULL;

    rc = qcrypto_x509_check_cert_times(cert->raw, cert->size, &err);
    if (rc != 0) {
        error_report_err(err);
        return false;
    }

    return true;
}

static int handle_key_id(VCEntry *vce, const S390IPLCertificate *cert)
{
    int rc;
    g_autofree unsigned char *key_id_data = NULL;
    size_t key_id_len;
    Error *err = NULL;

    rc = qcrypto_x509_get_cert_key_id(cert->raw, cert->size,
                                      QCRYPTO_HASH_ALGO_SHA256,
                                      &key_id_data, &key_id_len, &err);
    if (rc < 0) {
        error_report_err(err);
        return 0;
    }

    if (sizeof(VCEntryHeader) + key_id_len > be32_to_cpu(vce->vce_hdr.len)) {
        error_report("Unable to write key ID: exceeds buffer bounds");
        return 0;
    }

    vce->vce_hdr.keyid_len = cpu_to_be16(key_id_len);

    memcpy(vce->cert_buf, key_id_data, key_id_len);

    return ROUND_UP(key_id_len, 4);
}

static int handle_hash(VCEntry *vce, const S390IPLCertificate *cert,
                       uint16_t keyid_field_len)
{
    int rc;
    uint16_t hash_offset;
    g_autofree void *hash_data = NULL;
    size_t hash_len;
    Error *err = NULL;

    hash_len = CERT_HASH_LEN;
    hash_data = g_malloc0(hash_len);
    rc = qcrypto_get_x509_cert_fingerprint(cert->raw, cert->size,
                                           QCRYPTO_HASH_ALGO_SHA256,
                                           hash_data, &hash_len, &err);
    if (rc < 0) {
        error_report_err(err);
        return 0;
    }

    hash_offset = sizeof(VCEntryHeader) + keyid_field_len;
    if (hash_offset + hash_len > be32_to_cpu(vce->vce_hdr.len)) {
        error_report("Unable to write hash: exceeds buffer bounds");
        return 0;
    }

    vce->vce_hdr.hash_len = cpu_to_be16(hash_len);
    vce->vce_hdr.hash_type = DIAG_320_VCE_HASHTYPE_SHA2_256;
    vce->vce_hdr.hash_offset = cpu_to_be16(hash_offset);

    memcpy((uint8_t *)vce + hash_offset, hash_data, hash_len);

    return ROUND_UP(hash_len, 4);
}

static int handle_cert(VCEntry *vce, const S390IPLCertificate *cert,
                       uint16_t hash_field_len)
{
    int rc;
    uint16_t cert_offset;
    g_autofree uint8_t *cert_der = NULL;
    size_t der_size;
    Error *err = NULL;

    rc = qcrypto_x509_convert_cert_der(cert->raw, cert->size,
                                       &cert_der, &der_size, &err);
    if (rc < 0) {
        error_report_err(err);
        return 0;
    }

    cert_offset = be16_to_cpu(vce->vce_hdr.hash_offset) + hash_field_len;
    if (cert_offset + der_size > be32_to_cpu(vce->vce_hdr.len)) {
        error_report("Unable to write certificate: exceeds buffer bounds");
        return 0;
    }

    vce->vce_hdr.format = DIAG_320_VCE_FORMAT_X509_DER;
    vce->vce_hdr.cert_len = cpu_to_be32(der_size);
    vce->vce_hdr.cert_offset = cpu_to_be16(cert_offset);

    memcpy((uint8_t *)vce + cert_offset, cert_der, der_size);

    return ROUND_UP(der_size, 4);
}

static int get_key_type(const S390IPLCertificate *cert)
{
    int rc;
    Error *err = NULL;

    rc = qcrypto_x509_check_ecc_curve_p521(cert->raw, cert->size, &err);
    if (rc == -1) {
        error_report_err(err);
        return -1;
    }

    return (rc == 1) ? DIAG_320_VCE_KEYTYPE_ECDSA_P521 :
                       DIAG_320_VCE_KEYTYPE_SELF_DESCRIBING;
}

static int build_vce_header(VCEntry *vce, const S390IPLCertificate *cert, int idx)
{
    int key_type;

    vce->vce_hdr.len = cpu_to_be32(sizeof(VCEntryHeader));
    vce->vce_hdr.cert_idx = cpu_to_be16(idx + 1);
    memcpy(vce->vce_hdr.name, cert->name, CERT_NAME_MAX_LEN);

    if (!is_cert_valid(cert)) {
        return -1;
    }

    key_type = get_key_type(cert);
    if (key_type == -1) {
        return -1;
    }
    vce->vce_hdr.key_type = key_type;

    return 0;
}

static int build_vce_data(VCEntry *vce, const S390IPLCertificate *cert,
                          uint32_t vce_max_len)
{
    uint16_t keyid_field_len;
    uint16_t hash_field_len;
    uint32_t cert_field_len;
    uint32_t vce_len;

    vce->vce_hdr.len = cpu_to_be32(vce_max_len);

    keyid_field_len = handle_key_id(vce, cert);
    if (!keyid_field_len) {
        return -1;
    }

    hash_field_len = handle_hash(vce, cert, keyid_field_len);
    if (!hash_field_len) {
        return -1;
    }

    cert_field_len = handle_cert(vce, cert, hash_field_len);
    if (!cert_field_len) {
        return -1;
    }

    vce_len = sizeof(VCEntryHeader) + keyid_field_len + hash_field_len + cert_field_len;
    if (vce_len > vce_max_len) {
        return -1;
    }

    vce->vce_hdr.flags |= DIAG_320_VCE_FLAGS_VALID;

    /* Update vce length to reflect the actual size used by vce */
    vce->vce_hdr.len = cpu_to_be32(vce_len);

    return 0;
}

static int handle_diag320_store_vc(S390CPU *cpu, uint64_t addr, uint64_t r1, uintptr_t ra,
                                   S390IPLCertificateStore *cs)
{
    g_autofree VCBlockHeader *vcb_hdr = NULL;
    size_t remaining_space;
    uint16_t first_vc_index;
    uint16_t last_vc_index;
    int cs_start_index;
    int cs_end_index;
    uint32_t vce_max_len;
    uint32_t vce_len;
    uint32_t in_len;

    vcb_hdr = g_new0(VCBlockHeader, 1);
    if (s390_cpu_virt_mem_read(cpu, addr, r1, vcb_hdr, sizeof(*vcb_hdr))) {
        s390_cpu_virt_mem_handle_exc(cpu, ra);
        return -1;
    }

    in_len = be32_to_cpu(vcb_hdr->in_len);
    first_vc_index = be16_to_cpu(vcb_hdr->first_vc_index);
    last_vc_index = be16_to_cpu(vcb_hdr->last_vc_index);

    if (in_len % TARGET_PAGE_SIZE != 0) {
        return DIAG_320_RC_INVAL_VCB_LEN;
    }

    if (first_vc_index > last_vc_index) {
        return DIAG_320_RC_BAD_RANGE;
    }

    vcb_hdr->out_len = sizeof(VCBlockHeader);

    /*
     * DIAG 320 subcode 2 expects to query a certificate store that
     * maintains an index origin of 1. However, the S390IPLCertificateStore
     * maintains an index origin of 0. Thus, the indices must be adjusted
     * for correct access into the cert store. A couple of special cases
     * must also be accounted for.
     */

    /* Both indices are 0; return header with no certs */
    if (first_vc_index == 0 && last_vc_index == 0) {
        goto out;
    }

    /* Normalize indices */
    cs_start_index = (first_vc_index == 0) ? 0 : first_vc_index - 1;
    cs_end_index = last_vc_index - 1;

    /* Requested range is outside the cert store; return header with no certs */
    if (cs_start_index >= cs->count || cs_end_index >= cs->count) {
        goto out;
    }

    remaining_space = in_len - sizeof(VCBlockHeader);

    for (int i = cs_start_index; i <= cs_end_index; i++) {
        const S390IPLCertificate *cert = &cs->certs[i];
        /*
         * Each field of the VCE is word-aligned.
         * Allocate enough space for the largest possible size for this VCE.
         * As the certificate fields (key-id, hash, data) are parsed, the
         * VCE's length field will be updated accordingly.
         */
        vce_max_len = sizeof(VCEntryHeader) + ROUND_UP(CERT_KEY_ID_LEN, 4) +
                      ROUND_UP(CERT_HASH_LEN, 4) + ROUND_UP(cert->der_size, 4);
        g_autofree VCEntry *vce = g_malloc0(vce_max_len);

        /*
         * Bit 0 of the VCE flags indicates whether the certificate is valid.
         * The caller of DIAG320 subcode 2 is responsible for verifying that
         * the VCE contains a valid certificate.
         */
        if (build_vce_header(vce, cert, i) || build_vce_data(vce, cert, vce_max_len)) {
            /*
             * Error occurs - VCE does not contain a valid certificate.
             * Bit 0 of the VCE flags is 0 and the VCE length is set.
             */
            vce->vce_hdr.len = cpu_to_be32(VCE_INVALID_LEN);
        }
        vce_len = be32_to_cpu(vce->vce_hdr.len);

        /*
         * If there is no more space to store the cert,
         * set the remaining verification cert count and
         * break early.
         */
        if (remaining_space < vce_len) {
            vcb_hdr->remain_ct = cpu_to_be16(last_vc_index - i);
            break;
        }

        /* Write VCE */
        if (s390_cpu_virt_mem_write(cpu, addr + vcb_hdr->out_len, r1, vce, vce_len)) {
            s390_cpu_virt_mem_handle_exc(cpu, ra);
            return -1;
        }

        vcb_hdr->out_len += vce_len;
        remaining_space -= vce_len;
        vcb_hdr->stored_ct++;
    }
    vcb_hdr->stored_ct = cpu_to_be16(vcb_hdr->stored_ct);

out:
    vcb_hdr->out_len = cpu_to_be32(vcb_hdr->out_len);

    if (s390_cpu_virt_mem_write(cpu, addr, r1, vcb_hdr, sizeof(VCBlockHeader))) {
        s390_cpu_virt_mem_handle_exc(cpu, ra);
        return -1;
    }

    return DIAG_320_RC_OK;
}

QEMU_BUILD_BUG_MSG(sizeof(VCStorageSizeBlock) != VCSSB_LEN_VALID,
                   "size of VCStorageSizeBlock is wrong");
QEMU_BUILD_BUG_MSG(sizeof(VCBlock) != 64, "size of VCBlock is wrong");
QEMU_BUILD_BUG_MSG(sizeof(VCEntry) != 128, "size of VCEntry is wrong");

void handle_diag_320(CPUS390XState *env, uint64_t r1, uint64_t r3, uintptr_t ra)
{
    S390CPU *cpu = env_archcpu(env);
    S390IPLCertificateStore *cs = s390_ipl_get_certificate_store();
    uint64_t subcode = env->regs[r3];
    uint64_t addr = env->regs[r1];
    uint32_t ism_word0;
    int rc;

    if (env->psw.mask & PSW_MASK_PSTATE) {
        s390_program_interrupt(env, PGM_PRIVILEGED, ra);
        return;
    }

    if (!s390_has_feat(S390_FEAT_CERT_STORE) ||
        (subcode & ~0x000ffULL) ||
        (r1 & 1)) {
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return;
    }

    switch (subcode) {
    case DIAG_320_SUBC_QUERY_ISM:
        /*
         * The Installed Subcode Block (ISB) can be up 8 words in size,
         * but the current set of subcodes can fit within a single word
         * for now.
         */
        ism_word0 = cpu_to_be32(DIAG_320_ISM_QUERY_SUBCODES |
                                         DIAG_320_ISM_QUERY_VCSI |
                                         DIAG_320_ISM_STORE_VC);

        if (s390_cpu_virt_mem_write(cpu, addr, r1, &ism_word0, sizeof(ism_word0))) {
            s390_cpu_virt_mem_handle_exc(cpu, ra);
            return;
        }

        env->regs[r1 + 1] = DIAG_320_RC_OK;
        break;
    case DIAG_320_SUBC_QUERY_VCSI:
        if (addr & 0x7) {
            s390_program_interrupt(env, PGM_SPECIFICATION, ra);
            return;
        }

        if (!diag_parm_addr_valid(addr, sizeof(VCStorageSizeBlock), true)) {
            s390_program_interrupt(env, PGM_ADDRESSING, ra);
            return;
        }

        rc = handle_diag320_query_vcsi(cpu, addr, r1, ra, cs);
        if (rc == -1) {
            return;
        }
        env->regs[r1 + 1] = rc;
        break;
    case DIAG_320_SUBC_STORE_VC:
        if (addr & ~TARGET_PAGE_MASK) {
            s390_program_interrupt(env, PGM_SPECIFICATION, ra);
            return;
        }

        rc = handle_diag320_store_vc(cpu, addr, r1, ra, cs);
        if (rc == -1) {
            return;
        }
        env->regs[r1 + 1] = rc;
        break;
    default:
        env->regs[r1 + 1] = DIAG_320_RC_NOT_SUPPORTED;
        break;
    }
}

void handle_diag_508(CPUS390XState *env, uint64_t r1, uint64_t r3, uintptr_t ra)
{
    uint64_t subcode = env->regs[r3];
    int rc;

    if (env->psw.mask & PSW_MASK_PSTATE) {
        s390_program_interrupt(env, PGM_PRIVILEGED, ra);
        return;
    }

    if ((subcode & ~0x0ffffULL) || (r1 & 1)) {
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return;
    }

    switch (subcode) {
    case DIAG_508_SUBC_QUERY_SUBC:
        rc = 0;
        break;
    default:
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return;
    }
    env->regs[r1 + 1] = rc;
}
