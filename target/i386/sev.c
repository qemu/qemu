/*
 * QEMU SEV support
 *
 * Copyright Advanced Micro Devices 2016-2018
 *
 * Author:
 *      Brijesh Singh <brijesh.singh@amd.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"

#include <linux/kvm.h>
#include <linux/psp-sev.h>

#include <sys/ioctl.h>

#include "qapi/error.h"
#include "qom/object_interfaces.h"
#include "qemu/base64.h"
#include "qemu/module.h"
#include "qemu/uuid.h"
#include "crypto/hash.h"
#include "sysemu/kvm.h"
#include "sev.h"
#include "sysemu/sysemu.h"
#include "sysemu/runstate.h"
#include "trace.h"
#include "migration/blocker.h"
#include "qom/object.h"
#include "monitor/monitor.h"
#include "monitor/hmp-target.h"
#include "qapi/qapi-commands-misc-target.h"
#include "qapi/qmp/qerror.h"
#include "exec/confidential-guest-support.h"
#include "hw/i386/pc.h"
#include "exec/address-spaces.h"

#define TYPE_SEV_GUEST "sev-guest"
OBJECT_DECLARE_SIMPLE_TYPE(SevGuestState, SEV_GUEST)


/**
 * SevGuestState:
 *
 * The SevGuestState object is used for creating and managing a SEV
 * guest.
 *
 * # $QEMU \
 *         -object sev-guest,id=sev0 \
 *         -machine ...,memory-encryption=sev0
 */
struct SevGuestState {
    ConfidentialGuestSupport parent_obj;

    /* configuration parameters */
    char *sev_device;
    uint32_t policy;
    char *dh_cert_file;
    char *session_file;
    uint32_t cbitpos;
    uint32_t reduced_phys_bits;
    bool kernel_hashes;

    /* runtime state */
    uint32_t handle;
    uint8_t api_major;
    uint8_t api_minor;
    uint8_t build_id;
    int sev_fd;
    SevState state;
    gchar *measurement;

    uint32_t reset_cs;
    uint32_t reset_ip;
    bool reset_data_valid;
};

#define DEFAULT_GUEST_POLICY    0x1 /* disable debug */
#define DEFAULT_SEV_DEVICE      "/dev/sev"

#define SEV_INFO_BLOCK_GUID     "00f771de-1a7e-4fcb-890e-68c77e2fb44e"
typedef struct __attribute__((__packed__)) SevInfoBlock {
    /* SEV-ES Reset Vector Address */
    uint32_t reset_addr;
} SevInfoBlock;

#define SEV_HASH_TABLE_RV_GUID  "7255371f-3a3b-4b04-927b-1da6efa8d454"
typedef struct QEMU_PACKED SevHashTableDescriptor {
    /* SEV hash table area guest address */
    uint32_t base;
    /* SEV hash table area size (in bytes) */
    uint32_t size;
} SevHashTableDescriptor;

/* hard code sha256 digest size */
#define HASH_SIZE 32

typedef struct QEMU_PACKED SevHashTableEntry {
    QemuUUID guid;
    uint16_t len;
    uint8_t hash[HASH_SIZE];
} SevHashTableEntry;

typedef struct QEMU_PACKED SevHashTable {
    QemuUUID guid;
    uint16_t len;
    SevHashTableEntry cmdline;
    SevHashTableEntry initrd;
    SevHashTableEntry kernel;
} SevHashTable;

/*
 * Data encrypted by sev_encrypt_flash() must be padded to a multiple of
 * 16 bytes.
 */
typedef struct QEMU_PACKED PaddedSevHashTable {
    SevHashTable ht;
    uint8_t padding[ROUND_UP(sizeof(SevHashTable), 16) - sizeof(SevHashTable)];
} PaddedSevHashTable;

QEMU_BUILD_BUG_ON(sizeof(PaddedSevHashTable) % 16 != 0);

static SevGuestState *sev_guest;
static Error *sev_mig_blocker;

static const char *const sev_fw_errlist[] = {
    [SEV_RET_SUCCESS]                = "",
    [SEV_RET_INVALID_PLATFORM_STATE] = "Platform state is invalid",
    [SEV_RET_INVALID_GUEST_STATE]    = "Guest state is invalid",
    [SEV_RET_INAVLID_CONFIG]         = "Platform configuration is invalid",
    [SEV_RET_INVALID_LEN]            = "Buffer too small",
    [SEV_RET_ALREADY_OWNED]          = "Platform is already owned",
    [SEV_RET_INVALID_CERTIFICATE]    = "Certificate is invalid",
    [SEV_RET_POLICY_FAILURE]         = "Policy is not allowed",
    [SEV_RET_INACTIVE]               = "Guest is not active",
    [SEV_RET_INVALID_ADDRESS]        = "Invalid address",
    [SEV_RET_BAD_SIGNATURE]          = "Bad signature",
    [SEV_RET_BAD_MEASUREMENT]        = "Bad measurement",
    [SEV_RET_ASID_OWNED]             = "ASID is already owned",
    [SEV_RET_INVALID_ASID]           = "Invalid ASID",
    [SEV_RET_WBINVD_REQUIRED]        = "WBINVD is required",
    [SEV_RET_DFFLUSH_REQUIRED]       = "DF_FLUSH is required",
    [SEV_RET_INVALID_GUEST]          = "Guest handle is invalid",
    [SEV_RET_INVALID_COMMAND]        = "Invalid command",
    [SEV_RET_ACTIVE]                 = "Guest is active",
    [SEV_RET_HWSEV_RET_PLATFORM]     = "Hardware error",
    [SEV_RET_HWSEV_RET_UNSAFE]       = "Hardware unsafe",
    [SEV_RET_UNSUPPORTED]            = "Feature not supported",
    [SEV_RET_INVALID_PARAM]          = "Invalid parameter",
    [SEV_RET_RESOURCE_LIMIT]         = "Required firmware resource depleted",
    [SEV_RET_SECURE_DATA_INVALID]    = "Part-specific integrity check failure",
};

#define SEV_FW_MAX_ERROR      ARRAY_SIZE(sev_fw_errlist)

static int
sev_ioctl(int fd, int cmd, void *data, int *error)
{
    int r;
    struct kvm_sev_cmd input;

    memset(&input, 0x0, sizeof(input));

    input.id = cmd;
    input.sev_fd = fd;
    input.data = (__u64)(unsigned long)data;

    r = kvm_vm_ioctl(kvm_state, KVM_MEMORY_ENCRYPT_OP, &input);

    if (error) {
        *error = input.error;
    }

    return r;
}

static int
sev_platform_ioctl(int fd, int cmd, void *data, int *error)
{
    int r;
    struct sev_issue_cmd arg;

    arg.cmd = cmd;
    arg.data = (unsigned long)data;
    r = ioctl(fd, SEV_ISSUE_CMD, &arg);
    if (error) {
        *error = arg.error;
    }

    return r;
}

static const char *
fw_error_to_str(int code)
{
    if (code < 0 || code >= SEV_FW_MAX_ERROR) {
        return "unknown error";
    }

    return sev_fw_errlist[code];
}

static bool
sev_check_state(const SevGuestState *sev, SevState state)
{
    assert(sev);
    return sev->state == state ? true : false;
}

static void
sev_set_guest_state(SevGuestState *sev, SevState new_state)
{
    assert(new_state < SEV_STATE__MAX);
    assert(sev);

    trace_kvm_sev_change_state(SevState_str(sev->state),
                               SevState_str(new_state));
    sev->state = new_state;
}

static void
sev_ram_block_added(RAMBlockNotifier *n, void *host, size_t size,
                    size_t max_size)
{
    int r;
    struct kvm_enc_region range;
    ram_addr_t offset;
    MemoryRegion *mr;

    /*
     * The RAM device presents a memory region that should be treated
     * as IO region and should not be pinned.
     */
    mr = memory_region_from_host(host, &offset);
    if (mr && memory_region_is_ram_device(mr)) {
        return;
    }

    range.addr = (__u64)(unsigned long)host;
    range.size = max_size;

    trace_kvm_memcrypt_register_region(host, max_size);
    r = kvm_vm_ioctl(kvm_state, KVM_MEMORY_ENCRYPT_REG_REGION, &range);
    if (r) {
        error_report("%s: failed to register region (%p+%#zx) error '%s'",
                     __func__, host, max_size, strerror(errno));
        exit(1);
    }
}

static void
sev_ram_block_removed(RAMBlockNotifier *n, void *host, size_t size,
                      size_t max_size)
{
    int r;
    struct kvm_enc_region range;
    ram_addr_t offset;
    MemoryRegion *mr;

    /*
     * The RAM device presents a memory region that should be treated
     * as IO region and should not have been pinned.
     */
    mr = memory_region_from_host(host, &offset);
    if (mr && memory_region_is_ram_device(mr)) {
        return;
    }

    range.addr = (__u64)(unsigned long)host;
    range.size = max_size;

    trace_kvm_memcrypt_unregister_region(host, max_size);
    r = kvm_vm_ioctl(kvm_state, KVM_MEMORY_ENCRYPT_UNREG_REGION, &range);
    if (r) {
        error_report("%s: failed to unregister region (%p+%#zx)",
                     __func__, host, max_size);
    }
}

static struct RAMBlockNotifier sev_ram_notifier = {
    .ram_block_added = sev_ram_block_added,
    .ram_block_removed = sev_ram_block_removed,
};

static void
sev_guest_finalize(Object *obj)
{
}

static char *
sev_guest_get_session_file(Object *obj, Error **errp)
{
    SevGuestState *s = SEV_GUEST(obj);

    return s->session_file ? g_strdup(s->session_file) : NULL;
}

static void
sev_guest_set_session_file(Object *obj, const char *value, Error **errp)
{
    SevGuestState *s = SEV_GUEST(obj);

    s->session_file = g_strdup(value);
}

static char *
sev_guest_get_dh_cert_file(Object *obj, Error **errp)
{
    SevGuestState *s = SEV_GUEST(obj);

    return g_strdup(s->dh_cert_file);
}

static void
sev_guest_set_dh_cert_file(Object *obj, const char *value, Error **errp)
{
    SevGuestState *s = SEV_GUEST(obj);

    s->dh_cert_file = g_strdup(value);
}

static char *
sev_guest_get_sev_device(Object *obj, Error **errp)
{
    SevGuestState *sev = SEV_GUEST(obj);

    return g_strdup(sev->sev_device);
}

static void
sev_guest_set_sev_device(Object *obj, const char *value, Error **errp)
{
    SevGuestState *sev = SEV_GUEST(obj);

    sev->sev_device = g_strdup(value);
}

static bool sev_guest_get_kernel_hashes(Object *obj, Error **errp)
{
    SevGuestState *sev = SEV_GUEST(obj);

    return sev->kernel_hashes;
}

static void sev_guest_set_kernel_hashes(Object *obj, bool value, Error **errp)
{
    SevGuestState *sev = SEV_GUEST(obj);

    sev->kernel_hashes = value;
}

static void
sev_guest_class_init(ObjectClass *oc, void *data)
{
    object_class_property_add_str(oc, "sev-device",
                                  sev_guest_get_sev_device,
                                  sev_guest_set_sev_device);
    object_class_property_set_description(oc, "sev-device",
            "SEV device to use");
    object_class_property_add_str(oc, "dh-cert-file",
                                  sev_guest_get_dh_cert_file,
                                  sev_guest_set_dh_cert_file);
    object_class_property_set_description(oc, "dh-cert-file",
            "guest owners DH certificate (encoded with base64)");
    object_class_property_add_str(oc, "session-file",
                                  sev_guest_get_session_file,
                                  sev_guest_set_session_file);
    object_class_property_set_description(oc, "session-file",
            "guest owners session parameters (encoded with base64)");
    object_class_property_add_bool(oc, "kernel-hashes",
                                   sev_guest_get_kernel_hashes,
                                   sev_guest_set_kernel_hashes);
    object_class_property_set_description(oc, "kernel-hashes",
            "add kernel hashes to guest firmware for measured Linux boot");
}

static void
sev_guest_instance_init(Object *obj)
{
    SevGuestState *sev = SEV_GUEST(obj);

    sev->sev_device = g_strdup(DEFAULT_SEV_DEVICE);
    sev->policy = DEFAULT_GUEST_POLICY;
    object_property_add_uint32_ptr(obj, "policy", &sev->policy,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_add_uint32_ptr(obj, "handle", &sev->handle,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_add_uint32_ptr(obj, "cbitpos", &sev->cbitpos,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_add_uint32_ptr(obj, "reduced-phys-bits",
                                   &sev->reduced_phys_bits,
                                   OBJ_PROP_FLAG_READWRITE);
}

/* sev guest info */
static const TypeInfo sev_guest_info = {
    .parent = TYPE_CONFIDENTIAL_GUEST_SUPPORT,
    .name = TYPE_SEV_GUEST,
    .instance_size = sizeof(SevGuestState),
    .instance_finalize = sev_guest_finalize,
    .class_init = sev_guest_class_init,
    .instance_init = sev_guest_instance_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

bool
sev_enabled(void)
{
    return !!sev_guest;
}

bool
sev_es_enabled(void)
{
    return sev_enabled() && (sev_guest->policy & SEV_POLICY_ES);
}

uint32_t
sev_get_cbit_position(void)
{
    return sev_guest ? sev_guest->cbitpos : 0;
}

uint32_t
sev_get_reduced_phys_bits(void)
{
    return sev_guest ? sev_guest->reduced_phys_bits : 0;
}

static SevInfo *sev_get_info(void)
{
    SevInfo *info;

    info = g_new0(SevInfo, 1);
    info->enabled = sev_enabled();

    if (info->enabled) {
        info->api_major = sev_guest->api_major;
        info->api_minor = sev_guest->api_minor;
        info->build_id = sev_guest->build_id;
        info->policy = sev_guest->policy;
        info->state = sev_guest->state;
        info->handle = sev_guest->handle;
    }

    return info;
}

SevInfo *qmp_query_sev(Error **errp)
{
    SevInfo *info;

    info = sev_get_info();
    if (!info) {
        error_setg(errp, "SEV feature is not available");
        return NULL;
    }

    return info;
}

void hmp_info_sev(Monitor *mon, const QDict *qdict)
{
    SevInfo *info = sev_get_info();

    if (info && info->enabled) {
        monitor_printf(mon, "handle: %d\n", info->handle);
        monitor_printf(mon, "state: %s\n", SevState_str(info->state));
        monitor_printf(mon, "build: %d\n", info->build_id);
        monitor_printf(mon, "api version: %d.%d\n",
                       info->api_major, info->api_minor);
        monitor_printf(mon, "debug: %s\n",
                       info->policy & SEV_POLICY_NODBG ? "off" : "on");
        monitor_printf(mon, "key-sharing: %s\n",
                       info->policy & SEV_POLICY_NOKS ? "off" : "on");
    } else {
        monitor_printf(mon, "SEV is not enabled\n");
    }

    qapi_free_SevInfo(info);
}

static int
sev_get_pdh_info(int fd, guchar **pdh, size_t *pdh_len, guchar **cert_chain,
                 size_t *cert_chain_len, Error **errp)
{
    guchar *pdh_data = NULL;
    guchar *cert_chain_data = NULL;
    struct sev_user_data_pdh_cert_export export = {};
    int err, r;

    /* query the certificate length */
    r = sev_platform_ioctl(fd, SEV_PDH_CERT_EXPORT, &export, &err);
    if (r < 0) {
        if (err != SEV_RET_INVALID_LEN) {
            error_setg(errp, "SEV: Failed to export PDH cert"
                             " ret=%d fw_err=%d (%s)",
                       r, err, fw_error_to_str(err));
            return 1;
        }
    }

    pdh_data = g_new(guchar, export.pdh_cert_len);
    cert_chain_data = g_new(guchar, export.cert_chain_len);
    export.pdh_cert_address = (unsigned long)pdh_data;
    export.cert_chain_address = (unsigned long)cert_chain_data;

    r = sev_platform_ioctl(fd, SEV_PDH_CERT_EXPORT, &export, &err);
    if (r < 0) {
        error_setg(errp, "SEV: Failed to export PDH cert ret=%d fw_err=%d (%s)",
                   r, err, fw_error_to_str(err));
        goto e_free;
    }

    *pdh = pdh_data;
    *pdh_len = export.pdh_cert_len;
    *cert_chain = cert_chain_data;
    *cert_chain_len = export.cert_chain_len;
    return 0;

e_free:
    g_free(pdh_data);
    g_free(cert_chain_data);
    return 1;
}

static SevCapability *sev_get_capabilities(Error **errp)
{
    SevCapability *cap = NULL;
    guchar *pdh_data = NULL;
    guchar *cert_chain_data = NULL;
    size_t pdh_len = 0, cert_chain_len = 0;
    uint32_t ebx;
    int fd;

    if (!kvm_enabled()) {
        error_setg(errp, "KVM not enabled");
        return NULL;
    }
    if (kvm_vm_ioctl(kvm_state, KVM_MEMORY_ENCRYPT_OP, NULL) < 0) {
        error_setg(errp, "SEV is not enabled in KVM");
        return NULL;
    }

    fd = open(DEFAULT_SEV_DEVICE, O_RDWR);
    if (fd < 0) {
        error_setg_errno(errp, errno, "SEV: Failed to open %s",
                         DEFAULT_SEV_DEVICE);
        return NULL;
    }

    if (sev_get_pdh_info(fd, &pdh_data, &pdh_len,
                         &cert_chain_data, &cert_chain_len, errp)) {
        goto out;
    }

    cap = g_new0(SevCapability, 1);
    cap->pdh = g_base64_encode(pdh_data, pdh_len);
    cap->cert_chain = g_base64_encode(cert_chain_data, cert_chain_len);

    host_cpuid(0x8000001F, 0, NULL, &ebx, NULL, NULL);
    cap->cbitpos = ebx & 0x3f;

    /*
     * When SEV feature is enabled, we loose one bit in guest physical
     * addressing.
     */
    cap->reduced_phys_bits = 1;

out:
    g_free(pdh_data);
    g_free(cert_chain_data);
    close(fd);
    return cap;
}

SevCapability *qmp_query_sev_capabilities(Error **errp)
{
    return sev_get_capabilities(errp);
}

static SevAttestationReport *sev_get_attestation_report(const char *mnonce,
                                                        Error **errp)
{
    struct kvm_sev_attestation_report input = {};
    SevAttestationReport *report = NULL;
    SevGuestState *sev = sev_guest;
    g_autofree guchar *data = NULL;
    g_autofree guchar *buf = NULL;
    gsize len;
    int err = 0, ret;

    if (!sev_enabled()) {
        error_setg(errp, "SEV is not enabled");
        return NULL;
    }

    /* lets decode the mnonce string */
    buf = g_base64_decode(mnonce, &len);
    if (!buf) {
        error_setg(errp, "SEV: failed to decode mnonce input");
        return NULL;
    }

    /* verify the input mnonce length */
    if (len != sizeof(input.mnonce)) {
        error_setg(errp, "SEV: mnonce must be %zu bytes (got %" G_GSIZE_FORMAT ")",
                sizeof(input.mnonce), len);
        return NULL;
    }

    /* Query the report length */
    ret = sev_ioctl(sev->sev_fd, KVM_SEV_GET_ATTESTATION_REPORT,
            &input, &err);
    if (ret < 0) {
        if (err != SEV_RET_INVALID_LEN) {
            error_setg(errp, "SEV: Failed to query the attestation report"
                             " length ret=%d fw_err=%d (%s)",
                       ret, err, fw_error_to_str(err));
            return NULL;
        }
    }

    data = g_malloc(input.len);
    input.uaddr = (unsigned long)data;
    memcpy(input.mnonce, buf, sizeof(input.mnonce));

    /* Query the report */
    ret = sev_ioctl(sev->sev_fd, KVM_SEV_GET_ATTESTATION_REPORT,
            &input, &err);
    if (ret) {
        error_setg_errno(errp, errno, "SEV: Failed to get attestation report"
                " ret=%d fw_err=%d (%s)", ret, err, fw_error_to_str(err));
        return NULL;
    }

    report = g_new0(SevAttestationReport, 1);
    report->data = g_base64_encode(data, input.len);

    trace_kvm_sev_attestation_report(mnonce, report->data);

    return report;
}

SevAttestationReport *qmp_query_sev_attestation_report(const char *mnonce,
                                                       Error **errp)
{
    return sev_get_attestation_report(mnonce, errp);
}

static int
sev_read_file_base64(const char *filename, guchar **data, gsize *len)
{
    gsize sz;
    g_autofree gchar *base64 = NULL;
    GError *error = NULL;

    if (!g_file_get_contents(filename, &base64, &sz, &error)) {
        error_report("SEV: Failed to read '%s' (%s)", filename, error->message);
        g_error_free(error);
        return -1;
    }

    *data = g_base64_decode(base64, len);
    return 0;
}

static int
sev_launch_start(SevGuestState *sev)
{
    gsize sz;
    int ret = 1;
    int fw_error, rc;
    struct kvm_sev_launch_start start = {
        .handle = sev->handle, .policy = sev->policy
    };
    guchar *session = NULL, *dh_cert = NULL;

    if (sev->session_file) {
        if (sev_read_file_base64(sev->session_file, &session, &sz) < 0) {
            goto out;
        }
        start.session_uaddr = (unsigned long)session;
        start.session_len = sz;
    }

    if (sev->dh_cert_file) {
        if (sev_read_file_base64(sev->dh_cert_file, &dh_cert, &sz) < 0) {
            goto out;
        }
        start.dh_uaddr = (unsigned long)dh_cert;
        start.dh_len = sz;
    }

    trace_kvm_sev_launch_start(start.policy, session, dh_cert);
    rc = sev_ioctl(sev->sev_fd, KVM_SEV_LAUNCH_START, &start, &fw_error);
    if (rc < 0) {
        error_report("%s: LAUNCH_START ret=%d fw_error=%d '%s'",
                __func__, ret, fw_error, fw_error_to_str(fw_error));
        goto out;
    }

    sev_set_guest_state(sev, SEV_STATE_LAUNCH_UPDATE);
    sev->handle = start.handle;
    ret = 0;

out:
    g_free(session);
    g_free(dh_cert);
    return ret;
}

static int
sev_launch_update_data(SevGuestState *sev, uint8_t *addr, uint64_t len)
{
    int ret, fw_error;
    struct kvm_sev_launch_update_data update;

    if (!addr || !len) {
        return 1;
    }

    update.uaddr = (__u64)(unsigned long)addr;
    update.len = len;
    trace_kvm_sev_launch_update_data(addr, len);
    ret = sev_ioctl(sev->sev_fd, KVM_SEV_LAUNCH_UPDATE_DATA,
                    &update, &fw_error);
    if (ret) {
        error_report("%s: LAUNCH_UPDATE ret=%d fw_error=%d '%s'",
                __func__, ret, fw_error, fw_error_to_str(fw_error));
    }

    return ret;
}

static int
sev_launch_update_vmsa(SevGuestState *sev)
{
    int ret, fw_error;

    ret = sev_ioctl(sev->sev_fd, KVM_SEV_LAUNCH_UPDATE_VMSA, NULL, &fw_error);
    if (ret) {
        error_report("%s: LAUNCH_UPDATE_VMSA ret=%d fw_error=%d '%s'",
                __func__, ret, fw_error, fw_error_to_str(fw_error));
    }

    return ret;
}

static void
sev_launch_get_measure(Notifier *notifier, void *unused)
{
    SevGuestState *sev = sev_guest;
    int ret, error;
    g_autofree guchar *data = NULL;
    struct kvm_sev_launch_measure measurement = {};

    if (!sev_check_state(sev, SEV_STATE_LAUNCH_UPDATE)) {
        return;
    }

    if (sev_es_enabled()) {
        /* measure all the VM save areas before getting launch_measure */
        ret = sev_launch_update_vmsa(sev);
        if (ret) {
            exit(1);
        }
    }

    /* query the measurement blob length */
    ret = sev_ioctl(sev->sev_fd, KVM_SEV_LAUNCH_MEASURE,
                    &measurement, &error);
    if (!measurement.len) {
        error_report("%s: LAUNCH_MEASURE ret=%d fw_error=%d '%s'",
                     __func__, ret, error, fw_error_to_str(errno));
        return;
    }

    data = g_new0(guchar, measurement.len);
    measurement.uaddr = (unsigned long)data;

    /* get the measurement blob */
    ret = sev_ioctl(sev->sev_fd, KVM_SEV_LAUNCH_MEASURE,
                    &measurement, &error);
    if (ret) {
        error_report("%s: LAUNCH_MEASURE ret=%d fw_error=%d '%s'",
                     __func__, ret, error, fw_error_to_str(errno));
        return;
    }

    sev_set_guest_state(sev, SEV_STATE_LAUNCH_SECRET);

    /* encode the measurement value and emit the event */
    sev->measurement = g_base64_encode(data, measurement.len);
    trace_kvm_sev_launch_measurement(sev->measurement);
}

static char *sev_get_launch_measurement(void)
{
    if (sev_guest &&
        sev_guest->state >= SEV_STATE_LAUNCH_SECRET) {
        return g_strdup(sev_guest->measurement);
    }

    return NULL;
}

SevLaunchMeasureInfo *qmp_query_sev_launch_measure(Error **errp)
{
    char *data;
    SevLaunchMeasureInfo *info;

    data = sev_get_launch_measurement();
    if (!data) {
        error_setg(errp, "SEV launch measurement is not available");
        return NULL;
    }

    info = g_malloc0(sizeof(*info));
    info->data = data;

    return info;
}

static Notifier sev_machine_done_notify = {
    .notify = sev_launch_get_measure,
};

static void
sev_launch_finish(SevGuestState *sev)
{
    int ret, error;

    trace_kvm_sev_launch_finish();
    ret = sev_ioctl(sev->sev_fd, KVM_SEV_LAUNCH_FINISH, 0, &error);
    if (ret) {
        error_report("%s: LAUNCH_FINISH ret=%d fw_error=%d '%s'",
                     __func__, ret, error, fw_error_to_str(error));
        exit(1);
    }

    sev_set_guest_state(sev, SEV_STATE_RUNNING);

    /* add migration blocker */
    error_setg(&sev_mig_blocker,
               "SEV: Migration is not implemented");
    migrate_add_blocker(sev_mig_blocker, &error_fatal);
}

static void
sev_vm_state_change(void *opaque, bool running, RunState state)
{
    SevGuestState *sev = opaque;

    if (running) {
        if (!sev_check_state(sev, SEV_STATE_RUNNING)) {
            sev_launch_finish(sev);
        }
    }
}

int sev_kvm_init(ConfidentialGuestSupport *cgs, Error **errp)
{
    SevGuestState *sev
        = (SevGuestState *)object_dynamic_cast(OBJECT(cgs), TYPE_SEV_GUEST);
    char *devname;
    int ret, fw_error, cmd;
    uint32_t ebx;
    uint32_t host_cbitpos;
    struct sev_user_data_status status = {};

    if (!sev) {
        return 0;
    }

    ret = ram_block_discard_disable(true);
    if (ret) {
        error_report("%s: cannot disable RAM discard", __func__);
        return -1;
    }

    sev_guest = sev;
    sev->state = SEV_STATE_UNINIT;

    host_cpuid(0x8000001F, 0, NULL, &ebx, NULL, NULL);
    host_cbitpos = ebx & 0x3f;

    if (host_cbitpos != sev->cbitpos) {
        error_setg(errp, "%s: cbitpos check failed, host '%d' requested '%d'",
                   __func__, host_cbitpos, sev->cbitpos);
        goto err;
    }

    if (sev->reduced_phys_bits < 1) {
        error_setg(errp, "%s: reduced_phys_bits check failed, it should be >=1,"
                   " requested '%d'", __func__, sev->reduced_phys_bits);
        goto err;
    }

    devname = object_property_get_str(OBJECT(sev), "sev-device", NULL);
    sev->sev_fd = open(devname, O_RDWR);
    if (sev->sev_fd < 0) {
        error_setg(errp, "%s: Failed to open %s '%s'", __func__,
                   devname, strerror(errno));
        g_free(devname);
        goto err;
    }
    g_free(devname);

    ret = sev_platform_ioctl(sev->sev_fd, SEV_PLATFORM_STATUS, &status,
                             &fw_error);
    if (ret) {
        error_setg(errp, "%s: failed to get platform status ret=%d "
                   "fw_error='%d: %s'", __func__, ret, fw_error,
                   fw_error_to_str(fw_error));
        goto err;
    }
    sev->build_id = status.build;
    sev->api_major = status.api_major;
    sev->api_minor = status.api_minor;

    if (sev_es_enabled()) {
        if (!kvm_kernel_irqchip_allowed()) {
            error_report("%s: SEV-ES guests require in-kernel irqchip support",
                         __func__);
            goto err;
        }

        if (!(status.flags & SEV_STATUS_FLAGS_CONFIG_ES)) {
            error_report("%s: guest policy requires SEV-ES, but "
                         "host SEV-ES support unavailable",
                         __func__);
            goto err;
        }
        cmd = KVM_SEV_ES_INIT;
    } else {
        cmd = KVM_SEV_INIT;
    }

    trace_kvm_sev_init();
    ret = sev_ioctl(sev->sev_fd, cmd, NULL, &fw_error);
    if (ret) {
        error_setg(errp, "%s: failed to initialize ret=%d fw_error=%d '%s'",
                   __func__, ret, fw_error, fw_error_to_str(fw_error));
        goto err;
    }

    ret = sev_launch_start(sev);
    if (ret) {
        error_setg(errp, "%s: failed to create encryption context", __func__);
        goto err;
    }

    ram_block_notifier_add(&sev_ram_notifier);
    qemu_add_machine_init_done_notifier(&sev_machine_done_notify);
    qemu_add_vm_change_state_handler(sev_vm_state_change, sev);

    cgs->ready = true;

    return 0;
err:
    sev_guest = NULL;
    ram_block_discard_disable(false);
    return -1;
}

int
sev_encrypt_flash(uint8_t *ptr, uint64_t len, Error **errp)
{
    if (!sev_guest) {
        return 0;
    }

    /* if SEV is in update state then encrypt the data else do nothing */
    if (sev_check_state(sev_guest, SEV_STATE_LAUNCH_UPDATE)) {
        int ret = sev_launch_update_data(sev_guest, ptr, len);
        if (ret < 0) {
            error_setg(errp, "SEV: Failed to encrypt pflash rom");
            return ret;
        }
    }

    return 0;
}

int sev_inject_launch_secret(const char *packet_hdr, const char *secret,
                             uint64_t gpa, Error **errp)
{
    struct kvm_sev_launch_secret input;
    g_autofree guchar *data = NULL, *hdr = NULL;
    int error, ret = 1;
    void *hva;
    gsize hdr_sz = 0, data_sz = 0;
    MemoryRegion *mr = NULL;

    if (!sev_guest) {
        error_setg(errp, "SEV not enabled for guest");
        return 1;
    }

    /* secret can be injected only in this state */
    if (!sev_check_state(sev_guest, SEV_STATE_LAUNCH_SECRET)) {
        error_setg(errp, "SEV: Not in correct state. (LSECRET) %x",
                     sev_guest->state);
        return 1;
    }

    hdr = g_base64_decode(packet_hdr, &hdr_sz);
    if (!hdr || !hdr_sz) {
        error_setg(errp, "SEV: Failed to decode sequence header");
        return 1;
    }

    data = g_base64_decode(secret, &data_sz);
    if (!data || !data_sz) {
        error_setg(errp, "SEV: Failed to decode data");
        return 1;
    }

    hva = gpa2hva(&mr, gpa, data_sz, errp);
    if (!hva) {
        error_prepend(errp, "SEV: Failed to calculate guest address: ");
        return 1;
    }

    input.hdr_uaddr = (uint64_t)(unsigned long)hdr;
    input.hdr_len = hdr_sz;

    input.trans_uaddr = (uint64_t)(unsigned long)data;
    input.trans_len = data_sz;

    input.guest_uaddr = (uint64_t)(unsigned long)hva;
    input.guest_len = data_sz;

    trace_kvm_sev_launch_secret(gpa, input.guest_uaddr,
                                input.trans_uaddr, input.trans_len);

    ret = sev_ioctl(sev_guest->sev_fd, KVM_SEV_LAUNCH_SECRET,
                    &input, &error);
    if (ret) {
        error_setg(errp, "SEV: failed to inject secret ret=%d fw_error=%d '%s'",
                     ret, error, fw_error_to_str(error));
        return ret;
    }

    return 0;
}

#define SEV_SECRET_GUID "4c2eb361-7d9b-4cc3-8081-127c90d3d294"
struct sev_secret_area {
    uint32_t base;
    uint32_t size;
};

void qmp_sev_inject_launch_secret(const char *packet_hdr,
                                  const char *secret,
                                  bool has_gpa, uint64_t gpa,
                                  Error **errp)
{
    if (!sev_enabled()) {
        error_setg(errp, "SEV not enabled for guest");
        return;
    }
    if (!has_gpa) {
        uint8_t *data;
        struct sev_secret_area *area;

        if (!pc_system_ovmf_table_find(SEV_SECRET_GUID, &data, NULL)) {
            error_setg(errp, "SEV: no secret area found in OVMF,"
                       " gpa must be specified.");
            return;
        }
        area = (struct sev_secret_area *)data;
        gpa = area->base;
    }

    sev_inject_launch_secret(packet_hdr, secret, gpa, errp);
}

static int
sev_es_parse_reset_block(SevInfoBlock *info, uint32_t *addr)
{
    if (!info->reset_addr) {
        error_report("SEV-ES reset address is zero");
        return 1;
    }

    *addr = info->reset_addr;

    return 0;
}

static int
sev_es_find_reset_vector(void *flash_ptr, uint64_t flash_size,
                         uint32_t *addr)
{
    QemuUUID info_guid, *guid;
    SevInfoBlock *info;
    uint8_t *data;
    uint16_t *len;

    /*
     * Initialize the address to zero. An address of zero with a successful
     * return code indicates that SEV-ES is not active.
     */
    *addr = 0;

    /*
     * Extract the AP reset vector for SEV-ES guests by locating the SEV GUID.
     * The SEV GUID is located on its own (original implementation) or within
     * the Firmware GUID Table (new implementation), either of which are
     * located 32 bytes from the end of the flash.
     *
     * Check the Firmware GUID Table first.
     */
    if (pc_system_ovmf_table_find(SEV_INFO_BLOCK_GUID, &data, NULL)) {
        return sev_es_parse_reset_block((SevInfoBlock *)data, addr);
    }

    /*
     * SEV info block not found in the Firmware GUID Table (or there isn't
     * a Firmware GUID Table), fall back to the original implementation.
     */
    data = flash_ptr + flash_size - 0x20;

    qemu_uuid_parse(SEV_INFO_BLOCK_GUID, &info_guid);
    info_guid = qemu_uuid_bswap(info_guid); /* GUIDs are LE */

    guid = (QemuUUID *)(data - sizeof(info_guid));
    if (!qemu_uuid_is_equal(guid, &info_guid)) {
        error_report("SEV information block/Firmware GUID Table block not found in pflash rom");
        return 1;
    }

    len = (uint16_t *)((uint8_t *)guid - sizeof(*len));
    info = (SevInfoBlock *)(data - le16_to_cpu(*len));

    return sev_es_parse_reset_block(info, addr);
}

void sev_es_set_reset_vector(CPUState *cpu)
{
    X86CPU *x86;
    CPUX86State *env;

    /* Only update if we have valid reset information */
    if (!sev_guest || !sev_guest->reset_data_valid) {
        return;
    }

    /* Do not update the BSP reset state */
    if (cpu->cpu_index == 0) {
        return;
    }

    x86 = X86_CPU(cpu);
    env = &x86->env;

    cpu_x86_load_seg_cache(env, R_CS, 0xf000, sev_guest->reset_cs, 0xffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_CS_MASK |
                           DESC_R_MASK | DESC_A_MASK);

    env->eip = sev_guest->reset_ip;
}

int sev_es_save_reset_vector(void *flash_ptr, uint64_t flash_size)
{
    CPUState *cpu;
    uint32_t addr;
    int ret;

    if (!sev_es_enabled()) {
        return 0;
    }

    addr = 0;
    ret = sev_es_find_reset_vector(flash_ptr, flash_size,
                                   &addr);
    if (ret) {
        return ret;
    }

    if (addr) {
        sev_guest->reset_cs = addr & 0xffff0000;
        sev_guest->reset_ip = addr & 0x0000ffff;
        sev_guest->reset_data_valid = true;

        CPU_FOREACH(cpu) {
            sev_es_set_reset_vector(cpu);
        }
    }

    return 0;
}

static const QemuUUID sev_hash_table_header_guid = {
    .data = UUID_LE(0x9438d606, 0x4f22, 0x4cc9, 0xb4, 0x79, 0xa7, 0x93,
                    0xd4, 0x11, 0xfd, 0x21)
};

static const QemuUUID sev_kernel_entry_guid = {
    .data = UUID_LE(0x4de79437, 0xabd2, 0x427f, 0xb8, 0x35, 0xd5, 0xb1,
                    0x72, 0xd2, 0x04, 0x5b)
};
static const QemuUUID sev_initrd_entry_guid = {
    .data = UUID_LE(0x44baf731, 0x3a2f, 0x4bd7, 0x9a, 0xf1, 0x41, 0xe2,
                    0x91, 0x69, 0x78, 0x1d)
};
static const QemuUUID sev_cmdline_entry_guid = {
    .data = UUID_LE(0x97d02dd8, 0xbd20, 0x4c94, 0xaa, 0x78, 0xe7, 0x71,
                    0x4d, 0x36, 0xab, 0x2a)
};

/*
 * Add the hashes of the linux kernel/initrd/cmdline to an encrypted guest page
 * which is included in SEV's initial memory measurement.
 */
bool sev_add_kernel_loader_hashes(SevKernelLoaderContext *ctx, Error **errp)
{
    uint8_t *data;
    SevHashTableDescriptor *area;
    SevHashTable *ht;
    PaddedSevHashTable *padded_ht;
    uint8_t cmdline_hash[HASH_SIZE];
    uint8_t initrd_hash[HASH_SIZE];
    uint8_t kernel_hash[HASH_SIZE];
    uint8_t *hashp;
    size_t hash_len = HASH_SIZE;
    hwaddr mapped_len = sizeof(*padded_ht);
    MemTxAttrs attrs = { 0 };
    bool ret = true;

    /*
     * Only add the kernel hashes if the sev-guest configuration explicitly
     * stated kernel-hashes=on.
     */
    if (!sev_guest->kernel_hashes) {
        return false;
    }

    if (!pc_system_ovmf_table_find(SEV_HASH_TABLE_RV_GUID, &data, NULL)) {
        error_setg(errp, "SEV: kernel specified but guest firmware "
                         "has no hashes table GUID");
        return false;
    }
    area = (SevHashTableDescriptor *)data;
    if (!area->base || area->size < sizeof(PaddedSevHashTable)) {
        error_setg(errp, "SEV: guest firmware hashes table area is invalid "
                         "(base=0x%x size=0x%x)", area->base, area->size);
        return false;
    }

    /*
     * Calculate hash of kernel command-line with the terminating null byte. If
     * the user doesn't supply a command-line via -append, the 1-byte "\0" will
     * be used.
     */
    hashp = cmdline_hash;
    if (qcrypto_hash_bytes(QCRYPTO_HASH_ALG_SHA256, ctx->cmdline_data,
                           ctx->cmdline_size, &hashp, &hash_len, errp) < 0) {
        return false;
    }
    assert(hash_len == HASH_SIZE);

    /*
     * Calculate hash of initrd. If the user doesn't supply an initrd via
     * -initrd, an empty buffer will be used (ctx->initrd_size == 0).
     */
    hashp = initrd_hash;
    if (qcrypto_hash_bytes(QCRYPTO_HASH_ALG_SHA256, ctx->initrd_data,
                           ctx->initrd_size, &hashp, &hash_len, errp) < 0) {
        return false;
    }
    assert(hash_len == HASH_SIZE);

    /* Calculate hash of the kernel */
    hashp = kernel_hash;
    struct iovec iov[2] = {
        { .iov_base = ctx->setup_data, .iov_len = ctx->setup_size },
        { .iov_base = ctx->kernel_data, .iov_len = ctx->kernel_size }
    };
    if (qcrypto_hash_bytesv(QCRYPTO_HASH_ALG_SHA256, iov, ARRAY_SIZE(iov),
                            &hashp, &hash_len, errp) < 0) {
        return false;
    }
    assert(hash_len == HASH_SIZE);

    /*
     * Populate the hashes table in the guest's memory at the OVMF-designated
     * area for the SEV hashes table
     */
    padded_ht = address_space_map(&address_space_memory, area->base,
                                  &mapped_len, true, attrs);
    if (!padded_ht || mapped_len != sizeof(*padded_ht)) {
        error_setg(errp, "SEV: cannot map hashes table guest memory area");
        return false;
    }
    ht = &padded_ht->ht;

    ht->guid = sev_hash_table_header_guid;
    ht->len = sizeof(*ht);

    ht->cmdline.guid = sev_cmdline_entry_guid;
    ht->cmdline.len = sizeof(ht->cmdline);
    memcpy(ht->cmdline.hash, cmdline_hash, sizeof(ht->cmdline.hash));

    ht->initrd.guid = sev_initrd_entry_guid;
    ht->initrd.len = sizeof(ht->initrd);
    memcpy(ht->initrd.hash, initrd_hash, sizeof(ht->initrd.hash));

    ht->kernel.guid = sev_kernel_entry_guid;
    ht->kernel.len = sizeof(ht->kernel);
    memcpy(ht->kernel.hash, kernel_hash, sizeof(ht->kernel.hash));

    /* zero the excess data so the measurement can be reliably calculated */
    memset(padded_ht->padding, 0, sizeof(padded_ht->padding));

    if (sev_encrypt_flash((uint8_t *)padded_ht, sizeof(*padded_ht), errp) < 0) {
        ret = false;
    }

    address_space_unmap(&address_space_memory, padded_ht,
                        mapped_len, true, mapped_len);

    return ret;
}

static void
sev_register_types(void)
{
    type_register_static(&sev_guest_info);
}

type_init(sev_register_types);
