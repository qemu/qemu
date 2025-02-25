/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * uefi-vars device - state struct and function prototypes
 */
#ifndef QEMU_UEFI_VAR_SERVICE_H
#define QEMU_UEFI_VAR_SERVICE_H

#include "qemu/uuid.h"
#include "qemu/queue.h"

#include "hw/uefi/var-service-edk2.h"

#define MAX_BUFFER_SIZE (64 * 1024)

typedef struct uefi_variable uefi_variable;
typedef struct uefi_var_policy uefi_var_policy;
typedef struct uefi_vars_state uefi_vars_state;

typedef struct uefi_vars_cert uefi_vars_cert;
typedef struct uefi_vars_hash uefi_vars_hash;
typedef struct uefi_vars_siglist uefi_vars_siglist;

struct uefi_variable {
    QemuUUID                          guid;
    uint16_t                          *name;
    uint32_t                          name_size;
    uint32_t                          attributes;
    void                              *data;
    uint32_t                          data_size;
    efi_time                          time;
    void                              *digest;
    uint32_t                          digest_size;
    QTAILQ_ENTRY(uefi_variable)       next;
};

struct uefi_var_policy {
    variable_policy_entry             *entry;
    uint32_t                          entry_size;
    uint16_t                          *name;
    uint32_t                          name_size;

    /* number of hashmarks (wildcard character) in name */
    uint32_t                          hashmarks;

    QTAILQ_ENTRY(uefi_var_policy)     next;
};

struct uefi_vars_state {
    MemoryRegion                      mr;
    uint16_t                          sts;
    uint32_t                          buf_size;
    uint32_t                          buf_addr_lo;
    uint32_t                          buf_addr_hi;
    uint8_t                           *buffer;
    QTAILQ_HEAD(, uefi_variable)      variables;
    QTAILQ_HEAD(, uefi_var_policy)    var_policies;

    /* pio transfer buffer */
    uint32_t                          pio_xfer_offset;
    uint8_t                           *pio_xfer_buffer;

    /* boot phases */
    bool                              end_of_dxe;
    bool                              ready_to_boot;
    bool                              exit_boot_service;
    bool                              policy_locked;

    /* storage accounting */
    uint64_t                          max_storage;
    uint64_t                          used_storage;

    /* config options */
    char                              *jsonfile;
    int                               jsonfd;
    bool                              force_secure_boot;
    bool                              disable_custom_mode;
    bool                              use_pio;
};

struct uefi_vars_cert {
    QTAILQ_ENTRY(uefi_vars_cert)  next;
    QemuUUID                      owner;
    uint64_t                      size;
    uint8_t                       data[];
};

struct uefi_vars_hash {
    QTAILQ_ENTRY(uefi_vars_hash)  next;
    QemuUUID                      owner;
    uint8_t                       data[];
};

struct uefi_vars_siglist {
    QTAILQ_HEAD(, uefi_vars_cert)  x509;
    QTAILQ_HEAD(, uefi_vars_hash)  sha256;
};

/* vars-service-guid.c */
extern const QemuUUID EfiGlobalVariable;
extern const QemuUUID EfiImageSecurityDatabase;
extern const QemuUUID EfiCustomModeEnable;
extern const QemuUUID EfiSecureBootEnableDisable;

extern const QemuUUID EfiCertSha256Guid;
extern const QemuUUID EfiCertSha384Guid;
extern const QemuUUID EfiCertSha512Guid;
extern const QemuUUID EfiCertRsa2048Guid;
extern const QemuUUID EfiCertX509Guid;
extern const QemuUUID EfiCertTypePkcs7Guid;

extern const QemuUUID EfiSmmVariableProtocolGuid;
extern const QemuUUID VarCheckPolicyLibMmiHandlerGuid;

extern const QemuUUID EfiEndOfDxeEventGroupGuid;
extern const QemuUUID EfiEventReadyToBootGuid;
extern const QemuUUID EfiEventExitBootServicesGuid;

/* vars-service-utils.c */
gboolean uefi_str_is_valid(const uint16_t *str, size_t len,
                           gboolean must_be_null_terminated);
size_t uefi_strlen(const uint16_t *str, size_t len);
gboolean uefi_str_equal_ex(const uint16_t *a, size_t alen,
                           const uint16_t *b, size_t blen,
                           gboolean wildcards_in_a);
gboolean uefi_str_equal(const uint16_t *a, size_t alen,
                        const uint16_t *b, size_t blen);
char *uefi_ucs2_to_ascii(const uint16_t *ucs2, uint64_t ucs2_size);
int uefi_time_compare(efi_time *a, efi_time *b);
void uefi_trace_variable(const char *action, QemuUUID guid,
                         const uint16_t *name, uint64_t name_size);
void uefi_trace_status(const char *action, efi_status status);

/* vars-service-core.c */
extern const VMStateDescription vmstate_uefi_vars;
void uefi_vars_init(Object *obj, uefi_vars_state *uv);
void uefi_vars_realize(uefi_vars_state *uv, Error **errp);
void uefi_vars_hard_reset(uefi_vars_state *uv);

/* vars-service-json.c */
void uefi_vars_json_init(uefi_vars_state *uv, Error **errp);
void uefi_vars_json_save(uefi_vars_state *uv);
void uefi_vars_json_load(uefi_vars_state *uv, Error **errp);

/* vars-service-vars.c */
extern const VMStateDescription vmstate_uefi_variable;
uefi_variable *uefi_vars_find_variable(uefi_vars_state *uv, QemuUUID guid,
                                       const uint16_t *name,
                                       uint64_t name_size);
void uefi_vars_set_variable(uefi_vars_state *uv, QemuUUID guid,
                            const uint16_t *name, uint64_t name_size,
                            uint32_t attributes,
                            void *data, uint64_t data_size);
void uefi_vars_clear_volatile(uefi_vars_state *uv);
void uefi_vars_clear_all(uefi_vars_state *uv);
void uefi_vars_update_storage(uefi_vars_state *uv);
uint32_t uefi_vars_mm_vars_proto(uefi_vars_state *uv);

/* vars-service-auth.c */
bool uefi_vars_is_sb_pk(uefi_variable *var);
bool uefi_vars_is_sb_any(uefi_variable *var);
efi_status uefi_vars_check_auth_2(uefi_vars_state *uv, uefi_variable *var,
                                  mm_variable_access *va, void *data);
efi_status uefi_vars_check_secure_boot(uefi_vars_state *uv, uefi_variable *var);
void uefi_vars_auth_init(uefi_vars_state *uv);

/* vars-service-pkcs7.c */
efi_status uefi_vars_check_pkcs7_2(uefi_variable *siglist,
                                   void **digest, uint32_t *digest_size,
                                   mm_variable_access *va, void *data);

/* vars-service-siglist.c */
void uefi_vars_siglist_init(uefi_vars_siglist *siglist);
void uefi_vars_siglist_free(uefi_vars_siglist *siglist);
void uefi_vars_siglist_parse(uefi_vars_siglist *siglist,
                             void *data, uint64_t size);
uint64_t uefi_vars_siglist_blob_size(uefi_vars_siglist *siglist);
void uefi_vars_siglist_blob_generate(uefi_vars_siglist *siglist,
                                     void *data, uint64_t size);

/* vars-service-policy.c */
extern const VMStateDescription vmstate_uefi_var_policy;
efi_status uefi_vars_policy_check(uefi_vars_state *uv,
                                  uefi_variable *var,
                                  gboolean is_newvar);
void uefi_vars_policies_clear(uefi_vars_state *uv);
uefi_var_policy *uefi_vars_add_policy(uefi_vars_state *uv,
                                      variable_policy_entry *pe);
uint32_t uefi_vars_mm_check_policy_proto(uefi_vars_state *uv);

#endif /* QEMU_UEFI_VAR_SERVICE_H */
