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
#include <linux/kvm_para.h>
#include <linux/psp-sev.h>

#include <sys/ioctl.h>

#include "qapi/error.h"
#include "qom/object_interfaces.h"
#include "qemu/base64.h"
#include "qemu/module.h"
#include "qemu/uuid.h"
#include "qemu/error-report.h"
#include "crypto/hash.h"
#include "exec/target_page.h"
#include "system/kvm.h"
#include "kvm/kvm_i386.h"
#include "sev.h"
#include "system/system.h"
#include "system/runstate.h"
#include "trace.h"
#include "migration/blocker.h"
#include "qom/object.h"
#include "monitor/monitor.h"
#include "monitor/hmp-target.h"
#include "qapi/qapi-commands-misc-target.h"
#include "confidential-guest.h"
#include "hw/i386/pc.h"
#include "system/address-spaces.h"
#include "qemu/queue.h"

OBJECT_DECLARE_TYPE(SevCommonState, SevCommonStateClass, SEV_COMMON)
OBJECT_DECLARE_TYPE(SevGuestState, SevCommonStateClass, SEV_GUEST)
OBJECT_DECLARE_TYPE(SevSnpGuestState, SevCommonStateClass, SEV_SNP_GUEST)

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

struct SevCommonState {
    X86ConfidentialGuest parent_obj;

    int kvm_type;

    /* configuration parameters */
    char *sev_device;
    uint32_t cbitpos;
    uint32_t reduced_phys_bits;
    bool kernel_hashes;

    /* runtime state */
    uint8_t api_major;
    uint8_t api_minor;
    uint8_t build_id;
    int sev_fd;
    SevState state;

    uint32_t reset_cs;
    uint32_t reset_ip;
    bool reset_data_valid;
};

struct SevCommonStateClass {
    X86ConfidentialGuestClass parent_class;

    /* public */
    bool (*build_kernel_loader_hashes)(SevCommonState *sev_common,
                                       SevHashTableDescriptor *area,
                                       SevKernelLoaderContext *ctx,
                                       Error **errp);
    int (*launch_start)(SevCommonState *sev_common);
    void (*launch_finish)(SevCommonState *sev_common);
    int (*launch_update_data)(SevCommonState *sev_common, hwaddr gpa, uint8_t *ptr, size_t len);
    int (*kvm_init)(ConfidentialGuestSupport *cgs, Error **errp);
};

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
    SevCommonState parent_obj;
    gchar *measurement;

    /* configuration parameters */
    uint32_t handle;
    uint32_t policy;
    char *dh_cert_file;
    char *session_file;
    OnOffAuto legacy_vm_type;
};

struct SevSnpGuestState {
    SevCommonState parent_obj;

    /* configuration parameters */
    char *guest_visible_workarounds;
    char *id_block_base64;
    uint8_t *id_block;
    char *id_auth_base64;
    uint8_t *id_auth;
    char *host_data;

    struct kvm_sev_snp_launch_start kvm_start_conf;
    struct kvm_sev_snp_launch_finish kvm_finish_conf;

    uint32_t kernel_hashes_offset;
    PaddedSevHashTable *kernel_hashes_data;
};

#define DEFAULT_GUEST_POLICY    0x1 /* disable debug */
#define DEFAULT_SEV_DEVICE      "/dev/sev"
#define DEFAULT_SEV_SNP_POLICY  0x30000

typedef struct SevLaunchUpdateData {
    QTAILQ_ENTRY(SevLaunchUpdateData) next;
    hwaddr gpa;
    void *hva;
    size_t len;
    int type;
} SevLaunchUpdateData;

static QTAILQ_HEAD(, SevLaunchUpdateData) launch_update;

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

/* <linux/kvm.h> doesn't expose this, so re-use the max from kvm.c */
#define KVM_MAX_CPUID_ENTRIES 100

typedef struct KvmCpuidInfo {
    struct kvm_cpuid2 cpuid;
    struct kvm_cpuid_entry2 entries[KVM_MAX_CPUID_ENTRIES];
} KvmCpuidInfo;

#define SNP_CPUID_FUNCTION_MAXCOUNT 64
#define SNP_CPUID_FUNCTION_UNKNOWN 0xFFFFFFFF

typedef struct {
    uint32_t eax_in;
    uint32_t ecx_in;
    uint64_t xcr0_in;
    uint64_t xss_in;
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint64_t reserved;
} __attribute__((packed)) SnpCpuidFunc;

typedef struct {
    uint32_t count;
    uint32_t reserved1;
    uint64_t reserved2;
    SnpCpuidFunc entries[SNP_CPUID_FUNCTION_MAXCOUNT];
} __attribute__((packed)) SnpCpuidInfo;

static int
sev_ioctl(int fd, int cmd, void *data, int *error)
{
    int r;
    struct kvm_sev_cmd input;

    memset(&input, 0x0, sizeof(input));

    input.id = cmd;
    input.sev_fd = fd;
    input.data = (uintptr_t)data;

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
sev_check_state(const SevCommonState *sev_common, SevState state)
{
    assert(sev_common);
    return sev_common->state == state ? true : false;
}

static void
sev_set_guest_state(SevCommonState *sev_common, SevState new_state)
{
    assert(new_state < SEV_STATE__MAX);
    assert(sev_common);

    trace_kvm_sev_change_state(SevState_str(sev_common->state),
                               SevState_str(new_state));
    sev_common->state = new_state;
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

    range.addr = (uintptr_t)host;
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

    range.addr = (uintptr_t)host;
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

bool
sev_enabled(void)
{
    ConfidentialGuestSupport *cgs = MACHINE(qdev_get_machine())->cgs;

    return !!object_dynamic_cast(OBJECT(cgs), TYPE_SEV_COMMON);
}

bool
sev_snp_enabled(void)
{
    ConfidentialGuestSupport *cgs = MACHINE(qdev_get_machine())->cgs;

    return !!object_dynamic_cast(OBJECT(cgs), TYPE_SEV_SNP_GUEST);
}

bool
sev_es_enabled(void)
{
    ConfidentialGuestSupport *cgs = MACHINE(qdev_get_machine())->cgs;

    return sev_snp_enabled() ||
            (sev_enabled() && SEV_GUEST(cgs)->policy & SEV_POLICY_ES);
}

uint32_t
sev_get_cbit_position(void)
{
    SevCommonState *sev_common = SEV_COMMON(MACHINE(qdev_get_machine())->cgs);

    return sev_common ? sev_common->cbitpos : 0;
}

uint32_t
sev_get_reduced_phys_bits(void)
{
    SevCommonState *sev_common = SEV_COMMON(MACHINE(qdev_get_machine())->cgs);

    return sev_common ? sev_common->reduced_phys_bits : 0;
}

static SevInfo *sev_get_info(void)
{
    SevInfo *info;
    SevCommonState *sev_common = SEV_COMMON(MACHINE(qdev_get_machine())->cgs);

    info = g_new0(SevInfo, 1);
    info->enabled = sev_enabled();

    if (info->enabled) {
        info->api_major = sev_common->api_major;
        info->api_minor = sev_common->api_minor;
        info->build_id = sev_common->build_id;
        info->state = sev_common->state;

        if (sev_snp_enabled()) {
            info->sev_type = SEV_GUEST_TYPE_SEV_SNP;
            info->u.sev_snp.snp_policy =
                object_property_get_uint(OBJECT(sev_common), "policy", NULL);
        } else {
            info->sev_type = SEV_GUEST_TYPE_SEV;
            info->u.sev.handle = SEV_GUEST(sev_common)->handle;
            info->u.sev.policy =
                (uint32_t)object_property_get_uint(OBJECT(sev_common),
                                                   "policy", NULL);
        }
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

    if (!info || !info->enabled) {
        monitor_printf(mon, "SEV is not enabled\n");
        goto out;
    }

    monitor_printf(mon, "SEV type: %s\n", SevGuestType_str(info->sev_type));
    monitor_printf(mon, "state: %s\n", SevState_str(info->state));
    monitor_printf(mon, "build: %d\n", info->build_id);
    monitor_printf(mon, "api version: %d.%d\n", info->api_major,
                   info->api_minor);

    if (sev_snp_enabled()) {
        monitor_printf(mon, "debug: %s\n",
                       info->u.sev_snp.snp_policy & SEV_SNP_POLICY_DBG ? "on"
                                                                       : "off");
        monitor_printf(mon, "SMT allowed: %s\n",
                       info->u.sev_snp.snp_policy & SEV_SNP_POLICY_SMT ? "on"
                                                                       : "off");
    } else {
        monitor_printf(mon, "handle: %d\n", info->u.sev.handle);
        monitor_printf(mon, "debug: %s\n",
                       info->u.sev.policy & SEV_POLICY_NODBG ? "off" : "on");
        monitor_printf(mon, "key-sharing: %s\n",
                       info->u.sev.policy & SEV_POLICY_NOKS ? "off" : "on");
    }

out:
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

static int sev_get_cpu0_id(int fd, guchar **id, size_t *id_len, Error **errp)
{
    guchar *id_data;
    struct sev_user_data_get_id2 get_id2 = {};
    int err, r;

    /* query the ID length */
    r = sev_platform_ioctl(fd, SEV_GET_ID2, &get_id2, &err);
    if (r < 0 && err != SEV_RET_INVALID_LEN) {
        error_setg(errp, "SEV: Failed to get ID ret=%d fw_err=%d (%s)",
                   r, err, fw_error_to_str(err));
        return 1;
    }

    id_data = g_new(guchar, get_id2.length);
    get_id2.address = (unsigned long)id_data;

    r = sev_platform_ioctl(fd, SEV_GET_ID2, &get_id2, &err);
    if (r < 0) {
        error_setg(errp, "SEV: Failed to get ID ret=%d fw_err=%d (%s)",
                   r, err, fw_error_to_str(err));
        goto err;
    }

    *id = id_data;
    *id_len = get_id2.length;
    return 0;

err:
    g_free(id_data);
    return 1;
}

static SevCapability *sev_get_capabilities(Error **errp)
{
    SevCapability *cap = NULL;
    guchar *pdh_data = NULL;
    guchar *cert_chain_data = NULL;
    guchar *cpu0_id_data = NULL;
    size_t pdh_len = 0, cert_chain_len = 0, cpu0_id_len = 0;
    uint32_t ebx;
    int fd;
    SevCommonState *sev_common;
    char *sev_device;

    if (!kvm_enabled()) {
        error_setg(errp, "KVM not enabled");
        return NULL;
    }
    if (kvm_vm_ioctl(kvm_state, KVM_MEMORY_ENCRYPT_OP, NULL) < 0) {
        error_setg(errp, "SEV is not enabled in KVM");
        return NULL;
    }

    sev_common = SEV_COMMON(MACHINE(qdev_get_machine())->cgs);
    if (sev_common) {
        sev_device = object_property_get_str(OBJECT(sev_common), "sev-device",
                                             &error_abort);
    } else {
        sev_device = g_strdup(DEFAULT_SEV_DEVICE);
    }

    fd = open(sev_device, O_RDWR);
    if (fd < 0) {
        error_setg_errno(errp, errno, "SEV: Failed to open %s",
                         sev_device);
        g_free(sev_device);
        return NULL;
    }
    g_free(sev_device);

    if (sev_get_pdh_info(fd, &pdh_data, &pdh_len,
                         &cert_chain_data, &cert_chain_len, errp)) {
        goto out;
    }

    if (sev_get_cpu0_id(fd, &cpu0_id_data, &cpu0_id_len, errp)) {
        goto out;
    }

    cap = g_new0(SevCapability, 1);
    cap->pdh = g_base64_encode(pdh_data, pdh_len);
    cap->cert_chain = g_base64_encode(cert_chain_data, cert_chain_len);
    cap->cpu0_id = g_base64_encode(cpu0_id_data, cpu0_id_len);

    host_cpuid(0x8000001F, 0, NULL, &ebx, NULL, NULL);
    cap->cbitpos = ebx & 0x3f;

    /*
     * When SEV feature is enabled, we loose one bit in guest physical
     * addressing.
     */
    cap->reduced_phys_bits = 1;

out:
    g_free(cpu0_id_data);
    g_free(pdh_data);
    g_free(cert_chain_data);
    close(fd);
    return cap;
}

SevCapability *qmp_query_sev_capabilities(Error **errp)
{
    return sev_get_capabilities(errp);
}

static OvmfSevMetadata *ovmf_sev_metadata_table;

#define OVMF_SEV_META_DATA_GUID "dc886566-984a-4798-A75e-5585a7bf67cc"
typedef struct __attribute__((__packed__)) OvmfSevMetadataOffset {
    uint32_t offset;
} OvmfSevMetadataOffset;

OvmfSevMetadata *pc_system_get_ovmf_sev_metadata_ptr(void)
{
    return ovmf_sev_metadata_table;
}

void pc_system_parse_sev_metadata(uint8_t *flash_ptr, size_t flash_size)
{
    OvmfSevMetadata     *metadata;
    OvmfSevMetadataOffset  *data;

    if (!pc_system_ovmf_table_find(OVMF_SEV_META_DATA_GUID, (uint8_t **)&data,
                                   NULL)) {
        return;
    }

    metadata = (OvmfSevMetadata *)(flash_ptr + flash_size - data->offset);
    if (memcmp(metadata->signature, "ASEV", 4) != 0 ||
        metadata->len < sizeof(OvmfSevMetadata) ||
        metadata->len > flash_size - data->offset) {
        return;
    }

    ovmf_sev_metadata_table = g_memdup2(metadata, metadata->len);
}

static SevAttestationReport *sev_get_attestation_report(const char *mnonce,
                                                        Error **errp)
{
    struct kvm_sev_attestation_report input = {};
    SevAttestationReport *report = NULL;
    SevCommonState *sev_common;
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

    sev_common = SEV_COMMON(MACHINE(qdev_get_machine())->cgs);

    /* Query the report length */
    ret = sev_ioctl(sev_common->sev_fd, KVM_SEV_GET_ATTESTATION_REPORT,
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
    ret = sev_ioctl(sev_common->sev_fd, KVM_SEV_GET_ATTESTATION_REPORT,
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
sev_snp_launch_start(SevCommonState *sev_common)
{
    int fw_error, rc;
    SevSnpGuestState *sev_snp_guest = SEV_SNP_GUEST(sev_common);
    struct kvm_sev_snp_launch_start *start = &sev_snp_guest->kvm_start_conf;

    trace_kvm_sev_snp_launch_start(start->policy,
                                   sev_snp_guest->guest_visible_workarounds);

    if (!kvm_enable_hypercall(BIT_ULL(KVM_HC_MAP_GPA_RANGE))) {
            return 1;
    }

    rc = sev_ioctl(sev_common->sev_fd, KVM_SEV_SNP_LAUNCH_START,
                   start, &fw_error);
    if (rc < 0) {
        error_report("%s: SNP_LAUNCH_START ret=%d fw_error=%d '%s'",
                __func__, rc, fw_error, fw_error_to_str(fw_error));
        return 1;
    }

    QTAILQ_INIT(&launch_update);

    sev_set_guest_state(sev_common, SEV_STATE_LAUNCH_UPDATE);

    return 0;
}

static int
sev_launch_start(SevCommonState *sev_common)
{
    gsize sz;
    int ret = 1;
    int fw_error, rc;
    SevGuestState *sev_guest = SEV_GUEST(sev_common);
    struct kvm_sev_launch_start start = {
        .handle = sev_guest->handle, .policy = sev_guest->policy
    };
    guchar *session = NULL, *dh_cert = NULL;

    if (sev_guest->session_file) {
        if (sev_read_file_base64(sev_guest->session_file, &session, &sz) < 0) {
            goto out;
        }
        start.session_uaddr = (unsigned long)session;
        start.session_len = sz;
    }

    if (sev_guest->dh_cert_file) {
        if (sev_read_file_base64(sev_guest->dh_cert_file, &dh_cert, &sz) < 0) {
            goto out;
        }
        start.dh_uaddr = (unsigned long)dh_cert;
        start.dh_len = sz;
    }

    trace_kvm_sev_launch_start(start.policy, session, dh_cert);
    rc = sev_ioctl(sev_common->sev_fd, KVM_SEV_LAUNCH_START, &start, &fw_error);
    if (rc < 0) {
        error_report("%s: LAUNCH_START ret=%d fw_error=%d '%s'",
                __func__, ret, fw_error, fw_error_to_str(fw_error));
        goto out;
    }

    sev_set_guest_state(sev_common, SEV_STATE_LAUNCH_UPDATE);
    sev_guest->handle = start.handle;
    ret = 0;

out:
    g_free(session);
    g_free(dh_cert);
    return ret;
}

static void
sev_snp_cpuid_report_mismatches(SnpCpuidInfo *old,
                                SnpCpuidInfo *new)
{
    size_t i;

    if (old->count != new->count) {
        error_report("SEV-SNP: CPUID validation failed due to count mismatch, "
                     "provided: %d, expected: %d", old->count, new->count);
        return;
    }

    for (i = 0; i < old->count; i++) {
        SnpCpuidFunc *old_func, *new_func;

        old_func = &old->entries[i];
        new_func = &new->entries[i];

        if (memcmp(old_func, new_func, sizeof(SnpCpuidFunc))) {
            error_report("SEV-SNP: CPUID validation failed for function 0x%x, index: 0x%x, "
                         "provided: eax:0x%08x, ebx: 0x%08x, ecx: 0x%08x, edx: 0x%08x, "
                         "expected: eax:0x%08x, ebx: 0x%08x, ecx: 0x%08x, edx: 0x%08x",
                         old_func->eax_in, old_func->ecx_in,
                         old_func->eax, old_func->ebx, old_func->ecx, old_func->edx,
                         new_func->eax, new_func->ebx, new_func->ecx, new_func->edx);
        }
    }
}

static const char *
snp_page_type_to_str(int type)
{
    switch (type) {
    case KVM_SEV_SNP_PAGE_TYPE_NORMAL: return "Normal";
    case KVM_SEV_SNP_PAGE_TYPE_ZERO: return "Zero";
    case KVM_SEV_SNP_PAGE_TYPE_UNMEASURED: return "Unmeasured";
    case KVM_SEV_SNP_PAGE_TYPE_SECRETS: return "Secrets";
    case KVM_SEV_SNP_PAGE_TYPE_CPUID: return "Cpuid";
    default: return "unknown";
    }
}

static int
sev_snp_launch_update(SevSnpGuestState *sev_snp_guest,
                      SevLaunchUpdateData *data)
{
    int ret, fw_error;
    SnpCpuidInfo snp_cpuid_info;
    struct kvm_sev_snp_launch_update update = {0};

    if (!data->hva || !data->len) {
        error_report("SNP_LAUNCH_UPDATE called with invalid address"
                     "/ length: %p / %zx",
                     data->hva, data->len);
        return 1;
    }

    if (data->type == KVM_SEV_SNP_PAGE_TYPE_CPUID) {
        /* Save a copy for comparison in case the LAUNCH_UPDATE fails */
        memcpy(&snp_cpuid_info, data->hva, sizeof(snp_cpuid_info));
    }

    update.uaddr = (__u64)(unsigned long)data->hva;
    update.gfn_start = data->gpa >> TARGET_PAGE_BITS;
    update.len = data->len;
    update.type = data->type;

    /*
     * KVM_SEV_SNP_LAUNCH_UPDATE requires that GPA ranges have the private
     * memory attribute set in advance.
     */
    ret = kvm_set_memory_attributes_private(data->gpa, data->len);
    if (ret) {
        error_report("SEV-SNP: failed to configure initial"
                     "private guest memory");
        goto out;
    }

    while (update.len || ret == -EAGAIN) {
        trace_kvm_sev_snp_launch_update(update.uaddr, update.gfn_start <<
                                        TARGET_PAGE_BITS, update.len,
                                        snp_page_type_to_str(update.type));

        ret = sev_ioctl(SEV_COMMON(sev_snp_guest)->sev_fd,
                        KVM_SEV_SNP_LAUNCH_UPDATE,
                        &update, &fw_error);
        if (ret && ret != -EAGAIN) {
            error_report("SNP_LAUNCH_UPDATE ret=%d fw_error=%d '%s'",
                         ret, fw_error, fw_error_to_str(fw_error));

            if (data->type == KVM_SEV_SNP_PAGE_TYPE_CPUID) {
                sev_snp_cpuid_report_mismatches(&snp_cpuid_info, data->hva);
                error_report("SEV-SNP: failed update CPUID page");
            }
            break;
        }
    }

out:
    if (!ret && update.gfn_start << TARGET_PAGE_BITS != data->gpa + data->len) {
        error_report("SEV-SNP: expected update of GPA range %"
                     HWADDR_PRIx "-%" HWADDR_PRIx ","
                     "got GPA range %" HWADDR_PRIx "-%llx",
                     data->gpa, data->gpa + data->len, data->gpa,
                     update.gfn_start << TARGET_PAGE_BITS);
        ret = -EIO;
    }

    return ret;
}

static uint32_t
sev_snp_mask_cpuid_features(X86ConfidentialGuest *cg, uint32_t feature, uint32_t index,
                            int reg, uint32_t value)
{
    switch (feature) {
    case 1:
        if (reg == R_ECX) {
            return value & ~CPUID_EXT_TSC_DEADLINE_TIMER;
        }
        break;
    case 7:
        if (index == 0 && reg == R_EBX) {
            return value & ~CPUID_7_0_EBX_TSC_ADJUST;
        }
        if (index == 0 && reg == R_EDX) {
            return value & ~(CPUID_7_0_EDX_SPEC_CTRL |
                             CPUID_7_0_EDX_STIBP |
                             CPUID_7_0_EDX_FLUSH_L1D |
                             CPUID_7_0_EDX_ARCH_CAPABILITIES |
                             CPUID_7_0_EDX_CORE_CAPABILITY |
                             CPUID_7_0_EDX_SPEC_CTRL_SSBD);
        }
        break;
    case 0x80000008:
        if (reg == R_EBX) {
            return value & ~CPUID_8000_0008_EBX_VIRT_SSBD;
        }
        break;
    }
    return value;
}

static int
sev_launch_update_data(SevCommonState *sev_common, hwaddr gpa,
                       uint8_t *addr, size_t len)
{
    int ret, fw_error;
    struct kvm_sev_launch_update_data update;

    if (!addr || !len) {
        return 1;
    }

    update.uaddr = (uintptr_t)addr;
    update.len = len;
    trace_kvm_sev_launch_update_data(addr, len);
    ret = sev_ioctl(sev_common->sev_fd, KVM_SEV_LAUNCH_UPDATE_DATA,
                    &update, &fw_error);
    if (ret) {
        error_report("%s: LAUNCH_UPDATE ret=%d fw_error=%d '%s'",
                __func__, ret, fw_error, fw_error_to_str(fw_error));
    }

    return ret;
}

static int
sev_launch_update_vmsa(SevGuestState *sev_guest)
{
    int ret, fw_error;

    ret = sev_ioctl(SEV_COMMON(sev_guest)->sev_fd, KVM_SEV_LAUNCH_UPDATE_VMSA,
                    NULL, &fw_error);
    if (ret) {
        error_report("%s: LAUNCH_UPDATE_VMSA ret=%d fw_error=%d '%s'",
                __func__, ret, fw_error, fw_error_to_str(fw_error));
    }

    return ret;
}

static void
sev_launch_get_measure(Notifier *notifier, void *unused)
{
    SevCommonState *sev_common = SEV_COMMON(MACHINE(qdev_get_machine())->cgs);
    SevGuestState *sev_guest = SEV_GUEST(sev_common);
    int ret, error;
    g_autofree guchar *data = NULL;
    struct kvm_sev_launch_measure measurement = {};

    if (!sev_check_state(sev_common, SEV_STATE_LAUNCH_UPDATE)) {
        return;
    }

    if (sev_es_enabled()) {
        /* measure all the VM save areas before getting launch_measure */
        ret = sev_launch_update_vmsa(sev_guest);
        if (ret) {
            exit(1);
        }
        kvm_mark_guest_state_protected();
    }

    /* query the measurement blob length */
    ret = sev_ioctl(sev_common->sev_fd, KVM_SEV_LAUNCH_MEASURE,
                    &measurement, &error);
    if (!measurement.len) {
        error_report("%s: LAUNCH_MEASURE ret=%d fw_error=%d '%s'",
                     __func__, ret, error, fw_error_to_str(errno));
        return;
    }

    data = g_new0(guchar, measurement.len);
    measurement.uaddr = (unsigned long)data;

    /* get the measurement blob */
    ret = sev_ioctl(sev_common->sev_fd, KVM_SEV_LAUNCH_MEASURE,
                    &measurement, &error);
    if (ret) {
        error_report("%s: LAUNCH_MEASURE ret=%d fw_error=%d '%s'",
                     __func__, ret, error, fw_error_to_str(errno));
        return;
    }

    sev_set_guest_state(sev_common, SEV_STATE_LAUNCH_SECRET);

    /* encode the measurement value and emit the event */
    sev_guest->measurement = g_base64_encode(data, measurement.len);
    trace_kvm_sev_launch_measurement(sev_guest->measurement);
}

static char *sev_get_launch_measurement(void)
{
    ConfidentialGuestSupport *cgs = MACHINE(qdev_get_machine())->cgs;
    SevGuestState *sev_guest =
        (SevGuestState *)object_dynamic_cast(OBJECT(cgs), TYPE_SEV_GUEST);

    if (sev_guest &&
        SEV_COMMON(sev_guest)->state >= SEV_STATE_LAUNCH_SECRET) {
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
sev_launch_finish(SevCommonState *sev_common)
{
    int ret, error;

    trace_kvm_sev_launch_finish();
    ret = sev_ioctl(sev_common->sev_fd, KVM_SEV_LAUNCH_FINISH, 0,
                    &error);
    if (ret) {
        error_report("%s: LAUNCH_FINISH ret=%d fw_error=%d '%s'",
                     __func__, ret, error, fw_error_to_str(error));
        exit(1);
    }

    sev_set_guest_state(sev_common, SEV_STATE_RUNNING);

    /* add migration blocker */
    error_setg(&sev_mig_blocker,
               "SEV: Migration is not implemented");
    migrate_add_blocker(&sev_mig_blocker, &error_fatal);
}

static int
snp_launch_update_data(uint64_t gpa, void *hva, size_t len, int type)
{
    SevLaunchUpdateData *data;

    data = g_new0(SevLaunchUpdateData, 1);
    data->gpa = gpa;
    data->hva = hva;
    data->len = len;
    data->type = type;

    QTAILQ_INSERT_TAIL(&launch_update, data, next);

    return 0;
}

static int
sev_snp_launch_update_data(SevCommonState *sev_common, hwaddr gpa,
                           uint8_t *ptr, size_t len)
{
       int ret = snp_launch_update_data(gpa, ptr, len,
                                         KVM_SEV_SNP_PAGE_TYPE_NORMAL);
       return ret;
}

static int
sev_snp_cpuid_info_fill(SnpCpuidInfo *snp_cpuid_info,
                        const KvmCpuidInfo *kvm_cpuid_info)
{
    size_t i;

    if (kvm_cpuid_info->cpuid.nent > SNP_CPUID_FUNCTION_MAXCOUNT) {
        error_report("SEV-SNP: CPUID entry count (%d) exceeds max (%d)",
                     kvm_cpuid_info->cpuid.nent, SNP_CPUID_FUNCTION_MAXCOUNT);
        return -1;
    }

    memset(snp_cpuid_info, 0, sizeof(*snp_cpuid_info));

    for (i = 0; i < kvm_cpuid_info->cpuid.nent; i++) {
        const struct kvm_cpuid_entry2 *kvm_cpuid_entry;
        SnpCpuidFunc *snp_cpuid_entry;

        kvm_cpuid_entry = &kvm_cpuid_info->entries[i];
        snp_cpuid_entry = &snp_cpuid_info->entries[i];

        snp_cpuid_entry->eax_in = kvm_cpuid_entry->function;
        if (kvm_cpuid_entry->flags == KVM_CPUID_FLAG_SIGNIFCANT_INDEX) {
            snp_cpuid_entry->ecx_in = kvm_cpuid_entry->index;
        }
        snp_cpuid_entry->eax = kvm_cpuid_entry->eax;
        snp_cpuid_entry->ebx = kvm_cpuid_entry->ebx;
        snp_cpuid_entry->ecx = kvm_cpuid_entry->ecx;
        snp_cpuid_entry->edx = kvm_cpuid_entry->edx;

        /*
         * Guest kernels will calculate EBX themselves using the 0xD
         * subfunctions corresponding to the individual XSAVE areas, so only
         * encode the base XSAVE size in the initial leaves, corresponding
         * to the initial XCR0=1 state.
         */
        if (snp_cpuid_entry->eax_in == 0xD &&
            (snp_cpuid_entry->ecx_in == 0x0 || snp_cpuid_entry->ecx_in == 0x1)) {
            snp_cpuid_entry->ebx = 0x240;
            snp_cpuid_entry->xcr0_in = 1;
            snp_cpuid_entry->xss_in = 0;
        }
    }

    snp_cpuid_info->count = i;

    return 0;
}

static int
snp_launch_update_cpuid(uint32_t cpuid_addr, void *hva, size_t cpuid_len)
{
    KvmCpuidInfo kvm_cpuid_info = {0};
    SnpCpuidInfo snp_cpuid_info;
    CPUState *cs = first_cpu;
    int ret;
    uint32_t i = 0;

    assert(sizeof(snp_cpuid_info) <= cpuid_len);

    /* get the cpuid list from KVM */
    do {
        kvm_cpuid_info.cpuid.nent = ++i;
        ret = kvm_vcpu_ioctl(cs, KVM_GET_CPUID2, &kvm_cpuid_info);
    } while (ret == -E2BIG);

    if (ret) {
        error_report("SEV-SNP: unable to query CPUID values for CPU: '%s'",
                     strerror(-ret));
        return 1;
    }

    ret = sev_snp_cpuid_info_fill(&snp_cpuid_info, &kvm_cpuid_info);
    if (ret) {
        error_report("SEV-SNP: failed to generate CPUID table information");
        return 1;
    }

    memcpy(hva, &snp_cpuid_info, sizeof(snp_cpuid_info));

    return snp_launch_update_data(cpuid_addr, hva, cpuid_len,
                                  KVM_SEV_SNP_PAGE_TYPE_CPUID);
}

static int
snp_launch_update_kernel_hashes(SevSnpGuestState *sev_snp, uint32_t addr,
                                void *hva, uint32_t len)
{
    int type = KVM_SEV_SNP_PAGE_TYPE_ZERO;
    if (sev_snp->parent_obj.kernel_hashes) {
        assert(sev_snp->kernel_hashes_data);
        assert((sev_snp->kernel_hashes_offset +
                sizeof(*sev_snp->kernel_hashes_data)) <= len);
        memset(hva, 0, len);
        memcpy(hva + sev_snp->kernel_hashes_offset, sev_snp->kernel_hashes_data,
               sizeof(*sev_snp->kernel_hashes_data));
        type = KVM_SEV_SNP_PAGE_TYPE_NORMAL;
    }
    return snp_launch_update_data(addr, hva, len, type);
}

static int
snp_metadata_desc_to_page_type(int desc_type)
{
    switch (desc_type) {
    /* Add the umeasured prevalidated pages as a zero page */
    case SEV_DESC_TYPE_SNP_SEC_MEM: return KVM_SEV_SNP_PAGE_TYPE_ZERO;
    case SEV_DESC_TYPE_SNP_SECRETS: return KVM_SEV_SNP_PAGE_TYPE_SECRETS;
    case SEV_DESC_TYPE_CPUID: return KVM_SEV_SNP_PAGE_TYPE_CPUID;
    default:
         return KVM_SEV_SNP_PAGE_TYPE_ZERO;
    }
}

static void
snp_populate_metadata_pages(SevSnpGuestState *sev_snp,
                            OvmfSevMetadata *metadata)
{
    OvmfSevMetadataDesc *desc;
    int type, ret, i;
    void *hva;
    MemoryRegion *mr = NULL;

    for (i = 0; i < metadata->num_desc; i++) {
        desc = &metadata->descs[i];

        type = snp_metadata_desc_to_page_type(desc->type);

        hva = gpa2hva(&mr, desc->base, desc->len, NULL);
        if (!hva) {
            error_report("%s: Failed to get HVA for GPA 0x%x sz 0x%x",
                         __func__, desc->base, desc->len);
            exit(1);
        }

        if (type == KVM_SEV_SNP_PAGE_TYPE_CPUID) {
            ret = snp_launch_update_cpuid(desc->base, hva, desc->len);
        } else if (desc->type == SEV_DESC_TYPE_SNP_KERNEL_HASHES) {
            ret = snp_launch_update_kernel_hashes(sev_snp, desc->base, hva,
                                                  desc->len);
        } else {
            ret = snp_launch_update_data(desc->base, hva, desc->len, type);
        }

        if (ret) {
            error_report("%s: Failed to add metadata page gpa 0x%x+%x type %d",
                         __func__, desc->base, desc->len, desc->type);
            exit(1);
        }
    }
}

static void
sev_snp_launch_finish(SevCommonState *sev_common)
{
    int ret, error;
    Error *local_err = NULL;
    OvmfSevMetadata *metadata;
    SevLaunchUpdateData *data;
    SevSnpGuestState *sev_snp = SEV_SNP_GUEST(sev_common);
    struct kvm_sev_snp_launch_finish *finish = &sev_snp->kvm_finish_conf;

    /*
     * To boot the SNP guest, the hypervisor is required to populate the CPUID
     * and Secrets page before finalizing the launch flow. The location of
     * the secrets and CPUID page is available through the OVMF metadata GUID.
     */
    metadata = pc_system_get_ovmf_sev_metadata_ptr();
    if (metadata == NULL) {
        error_report("%s: Failed to locate SEV metadata header", __func__);
        exit(1);
    }

    /* Populate all the metadata pages */
    snp_populate_metadata_pages(sev_snp, metadata);

    QTAILQ_FOREACH(data, &launch_update, next) {
        ret = sev_snp_launch_update(sev_snp, data);
        if (ret) {
            exit(1);
        }
    }

    trace_kvm_sev_snp_launch_finish(sev_snp->id_block_base64, sev_snp->id_auth_base64,
                                    sev_snp->host_data);
    ret = sev_ioctl(sev_common->sev_fd, KVM_SEV_SNP_LAUNCH_FINISH,
                    finish, &error);
    if (ret) {
        error_report("SNP_LAUNCH_FINISH ret=%d fw_error=%d '%s'",
                     ret, error, fw_error_to_str(error));
        exit(1);
    }

    kvm_mark_guest_state_protected();
    sev_set_guest_state(sev_common, SEV_STATE_RUNNING);

    /* add migration blocker */
    error_setg(&sev_mig_blocker,
               "SEV-SNP: Migration is not implemented");
    ret = migrate_add_blocker(&sev_mig_blocker, &local_err);
    if (local_err) {
        error_report_err(local_err);
        error_free(sev_mig_blocker);
        exit(1);
    }
}


static void
sev_vm_state_change(void *opaque, bool running, RunState state)
{
    SevCommonState *sev_common = opaque;
    SevCommonStateClass *klass = SEV_COMMON_GET_CLASS(opaque);

    if (running) {
        if (!sev_check_state(sev_common, SEV_STATE_RUNNING)) {
            klass->launch_finish(sev_common);
        }
    }
}

/*
 * This helper is to examine sev-guest properties and determine if any options
 * have been set which rely on the newer KVM_SEV_INIT2 interface and associated
 * KVM VM types.
 */
static bool sev_init2_required(SevGuestState *sev_guest)
{
    /* Currently no KVM_SEV_INIT2-specific options are exposed via QEMU */
    return false;
}

static int sev_kvm_type(X86ConfidentialGuest *cg)
{
    SevCommonState *sev_common = SEV_COMMON(cg);
    SevGuestState *sev_guest = SEV_GUEST(sev_common);
    int kvm_type;

    if (sev_common->kvm_type != -1) {
        goto out;
    }

    /* These are the only cases where legacy VM types can be used. */
    if (sev_guest->legacy_vm_type == ON_OFF_AUTO_ON ||
        (sev_guest->legacy_vm_type == ON_OFF_AUTO_AUTO &&
         !sev_init2_required(sev_guest))) {
        sev_common->kvm_type = KVM_X86_DEFAULT_VM;
        goto out;
    }

    /*
     * Newer VM types are required, either explicitly via legacy-vm-type=on, or
     * implicitly via legacy-vm-type=auto along with additional sev-guest
     * properties that require the newer VM types.
     */
    kvm_type = (sev_guest->policy & SEV_POLICY_ES) ?
                KVM_X86_SEV_ES_VM : KVM_X86_SEV_VM;
    if (!kvm_is_vm_type_supported(kvm_type)) {
        if (sev_guest->legacy_vm_type == ON_OFF_AUTO_AUTO) {
            error_report("SEV: host kernel does not support requested %s VM type, which is required "
                         "for the set of options specified. To allow use of the legacy "
                         "KVM_X86_DEFAULT_VM VM type, please disable any options that are not "
                         "compatible with the legacy VM type, or upgrade your kernel.",
                         kvm_type == KVM_X86_SEV_VM ? "KVM_X86_SEV_VM" : "KVM_X86_SEV_ES_VM");
        } else {
            error_report("SEV: host kernel does not support requested %s VM type. To allow use of "
                         "the legacy KVM_X86_DEFAULT_VM VM type, the 'legacy-vm-type' argument "
                         "must be set to 'on' or 'auto' for the sev-guest object.",
                         kvm_type == KVM_X86_SEV_VM ? "KVM_X86_SEV_VM" : "KVM_X86_SEV_ES_VM");
        }

        return -1;
    }

    sev_common->kvm_type = kvm_type;
out:
    return sev_common->kvm_type;
}

static int sev_snp_kvm_type(X86ConfidentialGuest *cg)
{
    return KVM_X86_SNP_VM;
}

static int sev_common_kvm_init(ConfidentialGuestSupport *cgs, Error **errp)
{
    char *devname;
    int ret, fw_error, cmd;
    uint32_t ebx;
    uint32_t host_cbitpos;
    struct sev_user_data_status status = {};
    SevCommonState *sev_common = SEV_COMMON(cgs);
    SevCommonStateClass *klass = SEV_COMMON_GET_CLASS(cgs);
    X86ConfidentialGuestClass *x86_klass =
                               X86_CONFIDENTIAL_GUEST_GET_CLASS(cgs);

    sev_common->state = SEV_STATE_UNINIT;

    host_cpuid(0x8000001F, 0, NULL, &ebx, NULL, NULL);
    host_cbitpos = ebx & 0x3f;

    /*
     * The cbitpos value will be placed in bit positions 5:0 of the EBX
     * register of CPUID 0x8000001F. No need to verify the range as the
     * comparison against the host value accomplishes that.
     */
    if (host_cbitpos != sev_common->cbitpos) {
        error_setg(errp, "%s: cbitpos check failed, host '%d' requested '%d'",
                   __func__, host_cbitpos, sev_common->cbitpos);
        return -1;
    }

    /*
     * The reduced-phys-bits value will be placed in bit positions 11:6 of
     * the EBX register of CPUID 0x8000001F, so verify the supplied value
     * is in the range of 1 to 63.
     */
    if (sev_common->reduced_phys_bits < 1 ||
        sev_common->reduced_phys_bits > 63) {
        error_setg(errp, "%s: reduced_phys_bits check failed,"
                   " it should be in the range of 1 to 63, requested '%d'",
                   __func__, sev_common->reduced_phys_bits);
        return -1;
    }

    devname = object_property_get_str(OBJECT(sev_common), "sev-device", NULL);
    sev_common->sev_fd = open(devname, O_RDWR);
    if (sev_common->sev_fd < 0) {
        error_setg(errp, "%s: Failed to open %s '%s'", __func__,
                   devname, strerror(errno));
        g_free(devname);
        return -1;
    }
    g_free(devname);

    ret = sev_platform_ioctl(sev_common->sev_fd, SEV_PLATFORM_STATUS, &status,
                             &fw_error);
    if (ret) {
        error_setg(errp, "%s: failed to get platform status ret=%d "
                   "fw_error='%d: %s'", __func__, ret, fw_error,
                   fw_error_to_str(fw_error));
        return -1;
    }
    sev_common->build_id = status.build;
    sev_common->api_major = status.api_major;
    sev_common->api_minor = status.api_minor;

    if (sev_es_enabled()) {
        if (!kvm_kernel_irqchip_allowed()) {
            error_setg(errp, "%s: SEV-ES guests require in-kernel irqchip"
                       "support", __func__);
            return -1;
        }
    }

    if (sev_es_enabled() && !sev_snp_enabled()) {
        if (!(status.flags & SEV_STATUS_FLAGS_CONFIG_ES)) {
            error_setg(errp, "%s: guest policy requires SEV-ES, but "
                         "host SEV-ES support unavailable",
                         __func__);
            return -1;
        }
    }

    trace_kvm_sev_init();
    switch (x86_klass->kvm_type(X86_CONFIDENTIAL_GUEST(sev_common))) {
    case KVM_X86_DEFAULT_VM:
        cmd = sev_es_enabled() ? KVM_SEV_ES_INIT : KVM_SEV_INIT;

        ret = sev_ioctl(sev_common->sev_fd, cmd, NULL, &fw_error);
        break;
    case KVM_X86_SEV_VM:
    case KVM_X86_SEV_ES_VM:
    case KVM_X86_SNP_VM: {
        struct kvm_sev_init args = { 0 };

        ret = sev_ioctl(sev_common->sev_fd, KVM_SEV_INIT2, &args, &fw_error);
        break;
    }
    default:
        error_setg(errp, "%s: host kernel does not support the requested SEV configuration.",
                   __func__);
        return -1;
    }

    if (ret) {
        error_setg(errp, "%s: failed to initialize ret=%d fw_error=%d '%s'",
                   __func__, ret, fw_error, fw_error_to_str(fw_error));
        return -1;
    }

    ret = klass->launch_start(sev_common);

    if (ret) {
        error_setg(errp, "%s: failed to create encryption context", __func__);
        return -1;
    }

    if (klass->kvm_init && klass->kvm_init(cgs, errp)) {
        return -1;
    }

    qemu_add_vm_change_state_handler(sev_vm_state_change, sev_common);

    cgs->ready = true;

    return 0;
}

static int sev_kvm_init(ConfidentialGuestSupport *cgs, Error **errp)
{
     int ret;

    /*
     * SEV/SEV-ES rely on pinned memory to back guest RAM so discarding
     * isn't actually possible. With SNP, only guest_memfd pages are used
     * for private guest memory, so discarding of shared memory is still
     * possible..
     */
    ret = ram_block_discard_disable(true);
    if (ret) {
        error_setg(errp, "%s: cannot disable RAM discard", __func__);
        return -1;
    }

    /*
     * SEV uses these notifiers to register/pin pages prior to guest use,
     * but SNP relies on guest_memfd for private pages, which has its
     * own internal mechanisms for registering/pinning private memory.
     */
    ram_block_notifier_add(&sev_ram_notifier);

    /*
     * The machine done notify event is used for SEV guests to get the
     * measurement of the encrypted images. When SEV-SNP is enabled, the
     * measurement is part of the guest attestation process where it can
     * be collected without any reliance on the VMM. So skip registering
     * the notifier for SNP in favor of using guest attestation instead.
     */
    qemu_add_machine_init_done_notifier(&sev_machine_done_notify);

    return 0;
}

static int sev_snp_kvm_init(ConfidentialGuestSupport *cgs, Error **errp)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    X86MachineState *x86ms = X86_MACHINE(ms);

    if (x86ms->smm == ON_OFF_AUTO_AUTO) {
        x86ms->smm = ON_OFF_AUTO_OFF;
    } else if (x86ms->smm == ON_OFF_AUTO_ON) {
        error_setg(errp, "SEV-SNP does not support SMM.");
        return -1;
    }

    return 0;
}

int
sev_encrypt_flash(hwaddr gpa, uint8_t *ptr, uint64_t len, Error **errp)
{
    SevCommonState *sev_common = SEV_COMMON(MACHINE(qdev_get_machine())->cgs);
    SevCommonStateClass *klass;

    if (!sev_common) {
        return 0;
    }
    klass = SEV_COMMON_GET_CLASS(sev_common);

    /* if SEV is in update state then encrypt the data else do nothing */
    if (sev_check_state(sev_common, SEV_STATE_LAUNCH_UPDATE)) {
        int ret;

        ret = klass->launch_update_data(sev_common, gpa, ptr, len);
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
    ERRP_GUARD();
    struct kvm_sev_launch_secret input;
    g_autofree guchar *data = NULL, *hdr = NULL;
    int error, ret = 1;
    void *hva;
    gsize hdr_sz = 0, data_sz = 0;
    MemoryRegion *mr = NULL;
    SevCommonState *sev_common = SEV_COMMON(MACHINE(qdev_get_machine())->cgs);

    if (!sev_common) {
        error_setg(errp, "SEV not enabled for guest");
        return 1;
    }

    /* secret can be injected only in this state */
    if (!sev_check_state(sev_common, SEV_STATE_LAUNCH_SECRET)) {
        error_setg(errp, "SEV: Not in correct state. (LSECRET) %x",
                   sev_common->state);
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

    ret = sev_ioctl(sev_common->sev_fd, KVM_SEV_LAUNCH_SECRET,
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
    ConfidentialGuestSupport *cgs = MACHINE(qdev_get_machine())->cgs;
    SevCommonState *sev_common = SEV_COMMON(
        object_dynamic_cast(OBJECT(cgs), TYPE_SEV_COMMON));

    /* Only update if we have valid reset information */
    if (!sev_common || !sev_common->reset_data_valid) {
        return;
    }

    /* Do not update the BSP reset state */
    if (cpu->cpu_index == 0) {
        return;
    }

    x86 = X86_CPU(cpu);
    env = &x86->env;

    cpu_x86_load_seg_cache(env, R_CS, 0xf000, sev_common->reset_cs, 0xffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_CS_MASK |
                           DESC_R_MASK | DESC_A_MASK);

    env->eip = sev_common->reset_ip;
}

int sev_es_save_reset_vector(void *flash_ptr, uint64_t flash_size)
{
    CPUState *cpu;
    uint32_t addr;
    int ret;
    SevCommonState *sev_common = SEV_COMMON(MACHINE(qdev_get_machine())->cgs);

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
        sev_common->reset_cs = addr & 0xffff0000;
        sev_common->reset_ip = addr & 0x0000ffff;
        sev_common->reset_data_valid = true;

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

static bool build_kernel_loader_hashes(PaddedSevHashTable *padded_ht,
                                       SevKernelLoaderContext *ctx,
                                       Error **errp)
{
    SevHashTable *ht;
    uint8_t cmdline_hash[HASH_SIZE];
    uint8_t initrd_hash[HASH_SIZE];
    uint8_t kernel_hash[HASH_SIZE];
    uint8_t *hashp;
    size_t hash_len = HASH_SIZE;

    /*
     * Calculate hash of kernel command-line with the terminating null byte. If
     * the user doesn't supply a command-line via -append, the 1-byte "\0" will
     * be used.
     */
    hashp = cmdline_hash;
    if (qcrypto_hash_bytes(QCRYPTO_HASH_ALGO_SHA256, ctx->cmdline_data,
                           ctx->cmdline_size, &hashp, &hash_len, errp) < 0) {
        return false;
    }
    assert(hash_len == HASH_SIZE);

    /*
     * Calculate hash of initrd. If the user doesn't supply an initrd via
     * -initrd, an empty buffer will be used (ctx->initrd_size == 0).
     */
    hashp = initrd_hash;
    if (qcrypto_hash_bytes(QCRYPTO_HASH_ALGO_SHA256, ctx->initrd_data,
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
    if (qcrypto_hash_bytesv(QCRYPTO_HASH_ALGO_SHA256, iov, ARRAY_SIZE(iov),
                            &hashp, &hash_len, errp) < 0) {
        return false;
    }
    assert(hash_len == HASH_SIZE);

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

    return true;
}

static bool sev_snp_build_kernel_loader_hashes(SevCommonState *sev_common,
                                               SevHashTableDescriptor *area,
                                               SevKernelLoaderContext *ctx,
                                               Error **errp)
{
    /*
     * SNP: Populate the hashes table in an area that later in
     * snp_launch_update_kernel_hashes() will be copied to the guest memory
     * and encrypted.
     */
    SevSnpGuestState *sev_snp_guest = SEV_SNP_GUEST(sev_common);
    sev_snp_guest->kernel_hashes_offset = area->base & ~TARGET_PAGE_MASK;
    sev_snp_guest->kernel_hashes_data = g_new0(PaddedSevHashTable, 1);
    return build_kernel_loader_hashes(sev_snp_guest->kernel_hashes_data, ctx, errp);
}

static bool sev_build_kernel_loader_hashes(SevCommonState *sev_common,
                                           SevHashTableDescriptor *area,
                                           SevKernelLoaderContext *ctx,
                                           Error **errp)
{
    PaddedSevHashTable *padded_ht;
    hwaddr mapped_len = sizeof(*padded_ht);
    MemTxAttrs attrs = { 0 };
    bool ret = true;

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

    if (build_kernel_loader_hashes(padded_ht, ctx, errp)) {
        if (sev_encrypt_flash(area->base, (uint8_t *)padded_ht,
                              sizeof(*padded_ht), errp) < 0) {
            ret = false;
        }
    } else {
        ret = false;
    }

    address_space_unmap(&address_space_memory, padded_ht,
                        mapped_len, true, mapped_len);

    return ret;
}

/*
 * Add the hashes of the linux kernel/initrd/cmdline to an encrypted guest page
 * which is included in SEV's initial memory measurement.
 */
bool sev_add_kernel_loader_hashes(SevKernelLoaderContext *ctx, Error **errp)
{
    uint8_t *data;
    SevHashTableDescriptor *area;
    SevCommonState *sev_common = SEV_COMMON(MACHINE(qdev_get_machine())->cgs);
    SevCommonStateClass *klass = SEV_COMMON_GET_CLASS(sev_common);

    /*
     * Only add the kernel hashes if the sev-guest configuration explicitly
     * stated kernel-hashes=on.
     */
    if (!sev_common->kernel_hashes) {
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

    return klass->build_kernel_loader_hashes(sev_common, area, ctx, errp);
}

static char *
sev_common_get_sev_device(Object *obj, Error **errp)
{
    return g_strdup(SEV_COMMON(obj)->sev_device);
}

static void
sev_common_set_sev_device(Object *obj, const char *value, Error **errp)
{
    SEV_COMMON(obj)->sev_device = g_strdup(value);
}

static bool sev_common_get_kernel_hashes(Object *obj, Error **errp)
{
    return SEV_COMMON(obj)->kernel_hashes;
}

static void sev_common_set_kernel_hashes(Object *obj, bool value, Error **errp)
{
    SEV_COMMON(obj)->kernel_hashes = value;
}

static void
sev_common_class_init(ObjectClass *oc, void *data)
{
    ConfidentialGuestSupportClass *klass = CONFIDENTIAL_GUEST_SUPPORT_CLASS(oc);

    klass->kvm_init = sev_common_kvm_init;

    object_class_property_add_str(oc, "sev-device",
                                  sev_common_get_sev_device,
                                  sev_common_set_sev_device);
    object_class_property_set_description(oc, "sev-device",
            "SEV device to use");
    object_class_property_add_bool(oc, "kernel-hashes",
                                   sev_common_get_kernel_hashes,
                                   sev_common_set_kernel_hashes);
    object_class_property_set_description(oc, "kernel-hashes",
            "add kernel hashes to guest firmware for measured Linux boot");
}

static void
sev_common_instance_init(Object *obj)
{
    SevCommonState *sev_common = SEV_COMMON(obj);

    sev_common->kvm_type = -1;

    sev_common->sev_device = g_strdup(DEFAULT_SEV_DEVICE);

    object_property_add_uint32_ptr(obj, "cbitpos", &sev_common->cbitpos,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_add_uint32_ptr(obj, "reduced-phys-bits",
                                   &sev_common->reduced_phys_bits,
                                   OBJ_PROP_FLAG_READWRITE);
}

/* sev guest info common to sev/sev-es/sev-snp */
static const TypeInfo sev_common_info = {
    .parent = TYPE_X86_CONFIDENTIAL_GUEST,
    .name = TYPE_SEV_COMMON,
    .instance_size = sizeof(SevCommonState),
    .instance_init = sev_common_instance_init,
    .class_size = sizeof(SevCommonStateClass),
    .class_init = sev_common_class_init,
    .abstract = true,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static char *
sev_guest_get_dh_cert_file(Object *obj, Error **errp)
{
    return g_strdup(SEV_GUEST(obj)->dh_cert_file);
}

static void
sev_guest_set_dh_cert_file(Object *obj, const char *value, Error **errp)
{
    SEV_GUEST(obj)->dh_cert_file = g_strdup(value);
}

static char *
sev_guest_get_session_file(Object *obj, Error **errp)
{
    SevGuestState *sev_guest = SEV_GUEST(obj);

    return sev_guest->session_file ? g_strdup(sev_guest->session_file) : NULL;
}

static void
sev_guest_set_session_file(Object *obj, const char *value, Error **errp)
{
    SEV_GUEST(obj)->session_file = g_strdup(value);
}

static void sev_guest_get_legacy_vm_type(Object *obj, Visitor *v,
                                         const char *name, void *opaque,
                                         Error **errp)
{
    SevGuestState *sev_guest = SEV_GUEST(obj);
    OnOffAuto legacy_vm_type = sev_guest->legacy_vm_type;

    visit_type_OnOffAuto(v, name, &legacy_vm_type, errp);
}

static void sev_guest_set_legacy_vm_type(Object *obj, Visitor *v,
                                         const char *name, void *opaque,
                                         Error **errp)
{
    SevGuestState *sev_guest = SEV_GUEST(obj);

    visit_type_OnOffAuto(v, name, &sev_guest->legacy_vm_type, errp);
}

static void
sev_guest_class_init(ObjectClass *oc, void *data)
{
    SevCommonStateClass *klass = SEV_COMMON_CLASS(oc);
    X86ConfidentialGuestClass *x86_klass = X86_CONFIDENTIAL_GUEST_CLASS(oc);

    klass->build_kernel_loader_hashes = sev_build_kernel_loader_hashes;
    klass->launch_start = sev_launch_start;
    klass->launch_finish = sev_launch_finish;
    klass->launch_update_data = sev_launch_update_data;
    klass->kvm_init = sev_kvm_init;
    x86_klass->kvm_type = sev_kvm_type;

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
    object_class_property_add(oc, "legacy-vm-type", "OnOffAuto",
                              sev_guest_get_legacy_vm_type,
                              sev_guest_set_legacy_vm_type, NULL, NULL);
    object_class_property_set_description(oc, "legacy-vm-type",
            "use legacy VM type to maintain measurement compatibility with older QEMU or kernel versions.");
}

static void
sev_guest_instance_init(Object *obj)
{
    SevGuestState *sev_guest = SEV_GUEST(obj);

    sev_guest->policy = DEFAULT_GUEST_POLICY;
    object_property_add_uint32_ptr(obj, "handle", &sev_guest->handle,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_add_uint32_ptr(obj, "policy", &sev_guest->policy,
                                   OBJ_PROP_FLAG_READWRITE);
    object_apply_compat_props(obj);

    sev_guest->legacy_vm_type = ON_OFF_AUTO_AUTO;
}

/* guest info specific sev/sev-es */
static const TypeInfo sev_guest_info = {
    .parent = TYPE_SEV_COMMON,
    .name = TYPE_SEV_GUEST,
    .instance_size = sizeof(SevGuestState),
    .instance_init = sev_guest_instance_init,
    .class_init = sev_guest_class_init,
};

static void
sev_snp_guest_get_policy(Object *obj, Visitor *v, const char *name,
                         void *opaque, Error **errp)
{
    visit_type_uint64(v, name,
                      (uint64_t *)&SEV_SNP_GUEST(obj)->kvm_start_conf.policy,
                      errp);
}

static void
sev_snp_guest_set_policy(Object *obj, Visitor *v, const char *name,
                         void *opaque, Error **errp)
{
    visit_type_uint64(v, name,
                      (uint64_t *)&SEV_SNP_GUEST(obj)->kvm_start_conf.policy,
                      errp);
}

static char *
sev_snp_guest_get_guest_visible_workarounds(Object *obj, Error **errp)
{
    return g_strdup(SEV_SNP_GUEST(obj)->guest_visible_workarounds);
}

static void
sev_snp_guest_set_guest_visible_workarounds(Object *obj, const char *value,
                                            Error **errp)
{
    SevSnpGuestState *sev_snp_guest = SEV_SNP_GUEST(obj);
    struct kvm_sev_snp_launch_start *start = &sev_snp_guest->kvm_start_conf;
    g_autofree guchar *blob;
    gsize len;

    g_free(sev_snp_guest->guest_visible_workarounds);

    /* store the base64 str so we don't need to re-encode in getter */
    sev_snp_guest->guest_visible_workarounds = g_strdup(value);

    blob = qbase64_decode(sev_snp_guest->guest_visible_workarounds,
                          -1, &len, errp);
    if (!blob) {
        return;
    }

    if (len != sizeof(start->gosvw)) {
        error_setg(errp, "parameter length of %" G_GSIZE_FORMAT
                   " exceeds max of %zu",
                   len, sizeof(start->gosvw));
        return;
    }

    memcpy(start->gosvw, blob, len);
}

static char *
sev_snp_guest_get_id_block(Object *obj, Error **errp)
{
    SevSnpGuestState *sev_snp_guest = SEV_SNP_GUEST(obj);

    return g_strdup(sev_snp_guest->id_block_base64);
}

static void
sev_snp_guest_set_id_block(Object *obj, const char *value, Error **errp)
{
    SevSnpGuestState *sev_snp_guest = SEV_SNP_GUEST(obj);
    struct kvm_sev_snp_launch_finish *finish = &sev_snp_guest->kvm_finish_conf;
    gsize len;

    finish->id_block_en = 0;
    g_free(sev_snp_guest->id_block);
    g_free(sev_snp_guest->id_block_base64);

    /* store the base64 str so we don't need to re-encode in getter */
    sev_snp_guest->id_block_base64 = g_strdup(value);
    sev_snp_guest->id_block =
        qbase64_decode(sev_snp_guest->id_block_base64, -1, &len, errp);

    if (!sev_snp_guest->id_block) {
        return;
    }

    if (len != KVM_SEV_SNP_ID_BLOCK_SIZE) {
        error_setg(errp, "parameter length of %" G_GSIZE_FORMAT
                   " not equal to %u",
                   len, KVM_SEV_SNP_ID_BLOCK_SIZE);
        return;
    }

    finish->id_block_en = 1;
    finish->id_block_uaddr = (uintptr_t)sev_snp_guest->id_block;
}

static char *
sev_snp_guest_get_id_auth(Object *obj, Error **errp)
{
    SevSnpGuestState *sev_snp_guest = SEV_SNP_GUEST(obj);

    return g_strdup(sev_snp_guest->id_auth_base64);
}

static void
sev_snp_guest_set_id_auth(Object *obj, const char *value, Error **errp)
{
    SevSnpGuestState *sev_snp_guest = SEV_SNP_GUEST(obj);
    struct kvm_sev_snp_launch_finish *finish = &sev_snp_guest->kvm_finish_conf;
    gsize len;

    finish->id_auth_uaddr = 0;
    g_free(sev_snp_guest->id_auth);
    g_free(sev_snp_guest->id_auth_base64);

    /* store the base64 str so we don't need to re-encode in getter */
    sev_snp_guest->id_auth_base64 = g_strdup(value);
    sev_snp_guest->id_auth =
        qbase64_decode(sev_snp_guest->id_auth_base64, -1, &len, errp);

    if (!sev_snp_guest->id_auth) {
        return;
    }

    if (len > KVM_SEV_SNP_ID_AUTH_SIZE) {
        error_setg(errp, "parameter length:ID_AUTH %" G_GSIZE_FORMAT
                   " exceeds max of %u",
                   len, KVM_SEV_SNP_ID_AUTH_SIZE);
        return;
    }

    finish->id_auth_uaddr = (uintptr_t)sev_snp_guest->id_auth;
}

static bool
sev_snp_guest_get_author_key_enabled(Object *obj, Error **errp)
{
    SevSnpGuestState *sev_snp_guest = SEV_SNP_GUEST(obj);

    return !!sev_snp_guest->kvm_finish_conf.auth_key_en;
}

static void
sev_snp_guest_set_author_key_enabled(Object *obj, bool value, Error **errp)
{
    SevSnpGuestState *sev_snp_guest = SEV_SNP_GUEST(obj);

    sev_snp_guest->kvm_finish_conf.auth_key_en = value;
}

static bool
sev_snp_guest_get_vcek_disabled(Object *obj, Error **errp)
{
    SevSnpGuestState *sev_snp_guest = SEV_SNP_GUEST(obj);

    return !!sev_snp_guest->kvm_finish_conf.vcek_disabled;
}

static void
sev_snp_guest_set_vcek_disabled(Object *obj, bool value, Error **errp)
{
    SevSnpGuestState *sev_snp_guest = SEV_SNP_GUEST(obj);

    sev_snp_guest->kvm_finish_conf.vcek_disabled = value;
}

static char *
sev_snp_guest_get_host_data(Object *obj, Error **errp)
{
    SevSnpGuestState *sev_snp_guest = SEV_SNP_GUEST(obj);

    return g_strdup(sev_snp_guest->host_data);
}

static void
sev_snp_guest_set_host_data(Object *obj, const char *value, Error **errp)
{
    SevSnpGuestState *sev_snp_guest = SEV_SNP_GUEST(obj);
    struct kvm_sev_snp_launch_finish *finish = &sev_snp_guest->kvm_finish_conf;
    g_autofree guchar *blob;
    gsize len;

    g_free(sev_snp_guest->host_data);

    /* store the base64 str so we don't need to re-encode in getter */
    sev_snp_guest->host_data = g_strdup(value);

    blob = qbase64_decode(sev_snp_guest->host_data, -1, &len, errp);

    if (!blob) {
        return;
    }

    if (len != sizeof(finish->host_data)) {
        error_setg(errp, "parameter length of %" G_GSIZE_FORMAT
                   " not equal to %zu",
                   len, sizeof(finish->host_data));
        return;
    }

    memcpy(finish->host_data, blob, len);
}

static void
sev_snp_guest_class_init(ObjectClass *oc, void *data)
{
    SevCommonStateClass *klass = SEV_COMMON_CLASS(oc);
    X86ConfidentialGuestClass *x86_klass = X86_CONFIDENTIAL_GUEST_CLASS(oc);

    klass->build_kernel_loader_hashes = sev_snp_build_kernel_loader_hashes;
    klass->launch_start = sev_snp_launch_start;
    klass->launch_finish = sev_snp_launch_finish;
    klass->launch_update_data = sev_snp_launch_update_data;
    klass->kvm_init = sev_snp_kvm_init;
    x86_klass->mask_cpuid_features = sev_snp_mask_cpuid_features;
    x86_klass->kvm_type = sev_snp_kvm_type;

    object_class_property_add(oc, "policy", "uint64",
                              sev_snp_guest_get_policy,
                              sev_snp_guest_set_policy, NULL, NULL);
    object_class_property_add_str(oc, "guest-visible-workarounds",
                                  sev_snp_guest_get_guest_visible_workarounds,
                                  sev_snp_guest_set_guest_visible_workarounds);
    object_class_property_add_str(oc, "id-block",
                                  sev_snp_guest_get_id_block,
                                  sev_snp_guest_set_id_block);
    object_class_property_add_str(oc, "id-auth",
                                  sev_snp_guest_get_id_auth,
                                  sev_snp_guest_set_id_auth);
    object_class_property_add_bool(oc, "author-key-enabled",
                                   sev_snp_guest_get_author_key_enabled,
                                   sev_snp_guest_set_author_key_enabled);
    object_class_property_add_bool(oc, "vcek-disabled",
                                   sev_snp_guest_get_vcek_disabled,
                                   sev_snp_guest_set_vcek_disabled);
    object_class_property_add_str(oc, "host-data",
                                  sev_snp_guest_get_host_data,
                                  sev_snp_guest_set_host_data);
}

static void
sev_snp_guest_instance_init(Object *obj)
{
    ConfidentialGuestSupport *cgs = CONFIDENTIAL_GUEST_SUPPORT(obj);
    SevSnpGuestState *sev_snp_guest = SEV_SNP_GUEST(obj);

    cgs->require_guest_memfd = true;

    /* default init/start/finish params for kvm */
    sev_snp_guest->kvm_start_conf.policy = DEFAULT_SEV_SNP_POLICY;
}

/* guest info specific to sev-snp */
static const TypeInfo sev_snp_guest_info = {
    .parent = TYPE_SEV_COMMON,
    .name = TYPE_SEV_SNP_GUEST,
    .instance_size = sizeof(SevSnpGuestState),
    .class_init = sev_snp_guest_class_init,
    .instance_init = sev_snp_guest_instance_init,
};

static void
sev_register_types(void)
{
    type_register_static(&sev_common_info);
    type_register_static(&sev_guest_info);
    type_register_static(&sev_snp_guest_info);
}

type_init(sev_register_types);
