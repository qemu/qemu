/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * uefi vars device - AuthVariableLib
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "system/dma.h"

#include "hw/uefi/var-service.h"

static const uint16_t name_pk[]           = u"PK";
static const uint16_t name_kek[]          = u"KEK";
static const uint16_t name_db[]           = u"db";
static const uint16_t name_dbx[]          = u"dbx";
static const uint16_t name_setup_mode[]   = u"SetupMode";
static const uint16_t name_sigs_support[] = u"SignatureSupport";
static const uint16_t name_sb[]           = u"SecureBoot";
static const uint16_t name_sb_enable[]    = u"SecureBootEnable";
static const uint16_t name_custom_mode[]  = u"CustomMode";
static const uint16_t name_vk[]           = u"VendorKeys";
static const uint16_t name_vk_nv[]        = u"VendorKeysNv";

static const uint32_t sigdb_attrs =
    EFI_VARIABLE_NON_VOLATILE |
    EFI_VARIABLE_BOOTSERVICE_ACCESS |
    EFI_VARIABLE_RUNTIME_ACCESS |
    EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS;

static void set_secure_boot(uefi_vars_state *uv, uint8_t sb)
{
    uefi_vars_set_variable(uv, EfiGlobalVariable,
                           name_sb, sizeof(name_sb),
                           EFI_VARIABLE_BOOTSERVICE_ACCESS |
                           EFI_VARIABLE_RUNTIME_ACCESS,
                           &sb, sizeof(sb));
}

static void set_secure_boot_enable(uefi_vars_state *uv, uint8_t sbe)
{
    uefi_vars_set_variable(uv, EfiSecureBootEnableDisable,
                           name_sb_enable, sizeof(name_sb_enable),
                           EFI_VARIABLE_NON_VOLATILE |
                           EFI_VARIABLE_BOOTSERVICE_ACCESS,
                           &sbe, sizeof(sbe));
}

static void set_setup_mode(uefi_vars_state *uv, uint8_t sm)
{
    uefi_vars_set_variable(uv, EfiGlobalVariable,
                           name_setup_mode, sizeof(name_setup_mode),
                           EFI_VARIABLE_BOOTSERVICE_ACCESS |
                           EFI_VARIABLE_RUNTIME_ACCESS,
                           &sm, sizeof(sm));
}

static void set_custom_mode(uefi_vars_state *uv, uint8_t cm)
{
    uefi_vars_set_variable(uv, EfiCustomModeEnable,
                           name_custom_mode, sizeof(name_custom_mode),
                           EFI_VARIABLE_NON_VOLATILE |
                           EFI_VARIABLE_BOOTSERVICE_ACCESS,
                           &cm, sizeof(cm));
}

static void set_signature_support(uefi_vars_state *uv)
{
    QemuUUID sigs_support[5];

    sigs_support[0] = EfiCertSha256Guid;
    sigs_support[1] = EfiCertSha384Guid;
    sigs_support[2] = EfiCertSha512Guid;
    sigs_support[3] = EfiCertRsa2048Guid;
    sigs_support[4] = EfiCertX509Guid;

    uefi_vars_set_variable(uv, EfiGlobalVariable,
                           name_sigs_support, sizeof(name_sigs_support),
                           EFI_VARIABLE_BOOTSERVICE_ACCESS |
                           EFI_VARIABLE_RUNTIME_ACCESS,
                           sigs_support, sizeof(sigs_support));
}

static bool setup_mode_is_active(uefi_vars_state *uv)
{
    uefi_variable *var;
    uint8_t *value;

    var = uefi_vars_find_variable(uv, EfiGlobalVariable,
                                  name_setup_mode, sizeof(name_setup_mode));
    if (var) {
        value = var->data;
        if (value[0] == SETUP_MODE) {
            return true;
        }
    }
    return false;
}

static bool custom_mode_is_active(uefi_vars_state *uv)
{
    uefi_variable *var;
    uint8_t *value;

    var = uefi_vars_find_variable(uv, EfiCustomModeEnable,
                                  name_custom_mode, sizeof(name_custom_mode));
    if (var) {
        value = var->data;
        if (value[0] == CUSTOM_SECURE_BOOT_MODE) {
            return true;
        }
    }
    return false;
}

bool uefi_vars_is_sb_pk(uefi_variable *var)
{
    if (qemu_uuid_is_equal(&var->guid, &EfiGlobalVariable) &&
        uefi_str_equal(var->name, var->name_size, name_pk, sizeof(name_pk))) {
        return true;
    }
    return false;
}

static bool uefi_vars_is_sb_kek(uefi_variable *var)
{
    if (qemu_uuid_is_equal(&var->guid, &EfiGlobalVariable) &&
        uefi_str_equal(var->name, var->name_size, name_kek, sizeof(name_kek))) {
        return true;
    }
    return false;
}

static bool uefi_vars_is_sb_db(uefi_variable *var)
{
    if (!qemu_uuid_is_equal(&var->guid, &EfiImageSecurityDatabase)) {
        return false;
    }
    if (uefi_str_equal(var->name, var->name_size, name_db, sizeof(name_db))) {
        return true;
    }
    if (uefi_str_equal(var->name, var->name_size, name_dbx, sizeof(name_dbx))) {
        return true;
    }
    return false;
}

bool uefi_vars_is_sb_any(uefi_variable *var)
{
    if (uefi_vars_is_sb_pk(var) ||
        uefi_vars_is_sb_kek(var) ||
        uefi_vars_is_sb_db(var)) {
        return true;
    }
    return false;
}

static uefi_variable *uefi_vars_find_siglist(uefi_vars_state *uv,
                                             uefi_variable *var)
{
    if (uefi_vars_is_sb_pk(var)) {
        return uefi_vars_find_variable(uv, EfiGlobalVariable,
                                       name_pk, sizeof(name_pk));
    }
    if (uefi_vars_is_sb_kek(var)) {
        return uefi_vars_find_variable(uv, EfiGlobalVariable,
                                       name_pk, sizeof(name_pk));
    }
    if (uefi_vars_is_sb_db(var)) {
        return uefi_vars_find_variable(uv, EfiGlobalVariable,
                                       name_kek, sizeof(name_kek));
    }

    return NULL;
}

static efi_status uefi_vars_check_auth_2_sb(uefi_vars_state *uv,
                                            uefi_variable *var,
                                            mm_variable_access *va,
                                            void *data,
                                            uint64_t data_offset)
{
    variable_auth_2 *auth = data;
    uefi_variable *siglist;

    if (custom_mode_is_active(uv)) {
        /* no authentication in custom mode */
        return EFI_SUCCESS;
    }

    if (setup_mode_is_active(uv) && !uefi_vars_is_sb_pk(var)) {
        /* no authentication in setup mode (except PK) */
        return EFI_SUCCESS;
    }

    if (auth->hdr_length == 24) {
        /* no signature (auth->cert_data is empty) */
        return EFI_SECURITY_VIOLATION;
    }

    siglist = uefi_vars_find_siglist(uv, var);
    if (!siglist && setup_mode_is_active(uv) && uefi_vars_is_sb_pk(var)) {
        /* check PK is self-signed */
        uefi_variable tmp = {
            .guid       = EfiGlobalVariable,
            .name       = (uint16_t *)name_pk,
            .name_size  = sizeof(name_pk),
            .attributes = sigdb_attrs,
            .data       = data + data_offset,
            .data_size  = va->data_size - data_offset,
        };
        return uefi_vars_check_pkcs7_2(&tmp, NULL, NULL, va, data);
    }

    return uefi_vars_check_pkcs7_2(siglist, NULL, NULL, va, data);
}

efi_status uefi_vars_check_auth_2(uefi_vars_state *uv, uefi_variable *var,
                                  mm_variable_access *va, void *data)
{
    variable_auth_2 *auth = data;
    uint64_t data_offset;
    efi_status status;

    if (va->data_size < sizeof(*auth)) {
        return EFI_SECURITY_VIOLATION;
    }
    if (uadd64_overflow(sizeof(efi_time), auth->hdr_length, &data_offset)) {
        return EFI_SECURITY_VIOLATION;
    }
    if (va->data_size < data_offset) {
        return EFI_SECURITY_VIOLATION;
    }

    if (auth->hdr_revision != 0x0200 ||
        auth->hdr_cert_type != WIN_CERT_TYPE_EFI_GUID ||
        !qemu_uuid_is_equal(&auth->guid_cert_type, &EfiCertTypePkcs7Guid)) {
        return EFI_UNSUPPORTED;
    }

    if (uefi_vars_is_sb_any(var)) {
        /* secure boot variables */
        status = uefi_vars_check_auth_2_sb(uv, var, va, data, data_offset);
        if (status != EFI_SUCCESS) {
            return status;
        }
    } else {
        /* other authenticated variables */
        status = uefi_vars_check_pkcs7_2(NULL,
                                         &var->digest, &var->digest_size,
                                         va, data);
        if (status != EFI_SUCCESS) {
            return status;
        }
    }

    /* checks passed, set variable data */
    var->time = auth->timestamp;
    if (va->data_size - data_offset > 0) {
        var->data = g_malloc(va->data_size - data_offset);
        memcpy(var->data, data + data_offset, va->data_size - data_offset);
        var->data_size = va->data_size - data_offset;
    }

    return EFI_SUCCESS;
}

efi_status uefi_vars_check_secure_boot(uefi_vars_state *uv, uefi_variable *var)
{
    uint8_t *value = var->data;

    if (uefi_vars_is_sb_any(var)) {
        if (var->attributes != sigdb_attrs) {
            return EFI_INVALID_PARAMETER;
        }
    }

    /* reject SecureBootEnable updates if force_secure_boot is set */
    if (qemu_uuid_is_equal(&var->guid, &EfiSecureBootEnableDisable) &&
        uefi_str_equal(var->name, var->name_size,
                       name_sb_enable, sizeof(name_sb_enable)) &&
        uv->force_secure_boot &&
        value[0] != SECURE_BOOT_ENABLE) {
        return EFI_WRITE_PROTECTED;
    }

    /* reject CustomMode updates if disable_custom_mode is set */
    if (qemu_uuid_is_equal(&var->guid, &EfiCustomModeEnable) &&
        uefi_str_equal(var->name, var->name_size,
                       name_custom_mode, sizeof(name_custom_mode)) &&
        uv->disable_custom_mode) {
        return EFI_WRITE_PROTECTED;
    }

    return EFI_SUCCESS;
}

/* AuthVariableLibInitialize */
void uefi_vars_auth_init(uefi_vars_state *uv)
{
    uefi_variable *pk_var, *sbe_var;
    uint8_t platform_mode, sb, sbe, vk;

    /* SetupMode */
    pk_var = uefi_vars_find_variable(uv, EfiGlobalVariable,
                                     name_pk, sizeof(name_pk));
    if (!pk_var) {
        platform_mode = SETUP_MODE;
    } else {
        platform_mode = USER_MODE;
    }
    set_setup_mode(uv, platform_mode);

    /* SignatureSupport */
    set_signature_support(uv);

    /* SecureBootEnable */
    sbe = SECURE_BOOT_DISABLE;
    sbe_var = uefi_vars_find_variable(uv, EfiSecureBootEnableDisable,
                                      name_sb_enable, sizeof(name_sb_enable));
    if (sbe_var) {
        if (platform_mode == USER_MODE) {
            sbe = ((uint8_t *)sbe_var->data)[0];
        }
    } else if (platform_mode == USER_MODE) {
        sbe = SECURE_BOOT_ENABLE;
        set_secure_boot_enable(uv, sbe);
    }

    if (uv->force_secure_boot && sbe != SECURE_BOOT_ENABLE) {
        sbe = SECURE_BOOT_ENABLE;
        set_secure_boot_enable(uv, sbe);
    }

    /* SecureBoot */
    if ((sbe == SECURE_BOOT_ENABLE) && (platform_mode == USER_MODE)) {
        sb = SECURE_BOOT_MODE_ENABLE;
    } else {
        sb = SECURE_BOOT_MODE_DISABLE;
    }
    set_secure_boot(uv, sb);

    /* CustomMode */
    set_custom_mode(uv, STANDARD_SECURE_BOOT_MODE);

    vk = 0;
    uefi_vars_set_variable(uv, EfiGlobalVariable,
                           name_vk_nv, sizeof(name_vk_nv),
                           EFI_VARIABLE_NON_VOLATILE |
                           EFI_VARIABLE_BOOTSERVICE_ACCESS |
                           EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS,
                           &vk, sizeof(vk));
    uefi_vars_set_variable(uv, EfiGlobalVariable,
                           name_vk, sizeof(name_vk),
                           EFI_VARIABLE_BOOTSERVICE_ACCESS |
                           EFI_VARIABLE_RUNTIME_ACCESS,
                           &vk, sizeof(vk));

    /* flush to disk */
    uefi_vars_json_save(uv);
}
