/*
 * Protected Virtualization functions
 *
 * Copyright IBM Corp. 2020
 * Author(s):
 *  Janosch Frank <frankja@linux.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */
#include "qemu/osdep.h"

#include <linux/kvm.h>

#include "qemu/units.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "sysemu/kvm.h"
#include "sysemu/cpus.h"
#include "qom/object_interfaces.h"
#include "exec/confidential-guest-support.h"
#include "hw/s390x/ipl.h"
#include "hw/s390x/sclp.h"
#include "target/s390x/kvm/kvm_s390x.h"
#include "target/s390x/kvm/pv.h"

static bool info_valid;
static struct kvm_s390_pv_info_vm info_vm;
static struct kvm_s390_pv_info_dump info_dump;

static int __s390_pv_cmd(uint32_t cmd, const char *cmdname, void *data)
{
    struct kvm_pv_cmd pv_cmd = {
        .cmd = cmd,
        .data = (uint64_t)data,
    };
    int rc;

    do {
        rc = kvm_vm_ioctl(kvm_state, KVM_S390_PV_COMMAND, &pv_cmd);
    } while (rc == -EINTR);

    if (rc) {
        error_report("KVM PV command %d (%s) failed: header rc %x rrc %x "
                     "IOCTL rc: %d", cmd, cmdname, pv_cmd.rc, pv_cmd.rrc,
                     rc);
    }
    return rc;
}

/*
 * This macro lets us pass the command as a string to the function so
 * we can print it on an error.
 */
#define s390_pv_cmd(cmd, data) __s390_pv_cmd(cmd, #cmd, data)
#define s390_pv_cmd_exit(cmd, data)    \
{                                      \
    int rc;                            \
                                       \
    rc = __s390_pv_cmd(cmd, #cmd, data);\
    if (rc) {                          \
        exit(1);                       \
    }                                  \
}

int s390_pv_query_info(void)
{
    struct kvm_s390_pv_info info = {
        .header.id = KVM_PV_INFO_VM,
        .header.len_max = sizeof(info.header) + sizeof(info.vm),
    };
    int rc;

    /* Info API's first user is dump so they are bundled */
    if (!kvm_s390_get_protected_dump()) {
        return 0;
    }

    rc = s390_pv_cmd(KVM_PV_INFO, &info);
    if (rc) {
        error_report("KVM PV INFO cmd %x failed: %s",
                     info.header.id, strerror(-rc));
        return rc;
    }
    memcpy(&info_vm, &info.vm, sizeof(info.vm));

    info.header.id = KVM_PV_INFO_DUMP;
    info.header.len_max = sizeof(info.header) + sizeof(info.dump);
    rc = s390_pv_cmd(KVM_PV_INFO, &info);
    if (rc) {
        error_report("KVM PV INFO cmd %x failed: %s",
                     info.header.id, strerror(-rc));
        return rc;
    }

    memcpy(&info_dump, &info.dump, sizeof(info.dump));
    info_valid = true;

    return rc;
}

int s390_pv_vm_enable(void)
{
    return s390_pv_cmd(KVM_PV_ENABLE, NULL);
}

void s390_pv_vm_disable(void)
{
     s390_pv_cmd_exit(KVM_PV_DISABLE, NULL);
}

static void *s390_pv_do_unprot_async_fn(void *p)
{
     s390_pv_cmd_exit(KVM_PV_ASYNC_CLEANUP_PERFORM, NULL);
     return NULL;
}

bool s390_pv_vm_try_disable_async(S390CcwMachineState *ms)
{
    /*
     * t is only needed to create the thread; once qemu_thread_create
     * returns, it can safely be discarded.
     */
    QemuThread t;

    /*
     * If the feature is not present or if the VM is not larger than 2 GiB,
     * KVM_PV_ASYNC_CLEANUP_PREPARE fill fail; no point in attempting it.
     */
    if ((MACHINE(ms)->maxram_size <= 2 * GiB) ||
        !kvm_check_extension(kvm_state, KVM_CAP_S390_PROTECTED_ASYNC_DISABLE)) {
        return false;
    }
    if (s390_pv_cmd(KVM_PV_ASYNC_CLEANUP_PREPARE, NULL) != 0) {
        return false;
    }

    qemu_thread_create(&t, "async_cleanup", s390_pv_do_unprot_async_fn, NULL,
                       QEMU_THREAD_DETACHED);

    return true;
}

int s390_pv_set_sec_parms(uint64_t origin, uint64_t length)
{
    struct kvm_s390_pv_sec_parm args = {
        .origin = origin,
        .length = length,
    };

    return s390_pv_cmd(KVM_PV_SET_SEC_PARMS, &args);
}

/*
 * Called for each component in the SE type IPL parameter block 0.
 */
int s390_pv_unpack(uint64_t addr, uint64_t size, uint64_t tweak)
{
    struct kvm_s390_pv_unp args = {
        .addr = addr,
        .size = size,
        .tweak = tweak,
    };

    return s390_pv_cmd(KVM_PV_UNPACK, &args);
}

void s390_pv_prep_reset(void)
{
    s390_pv_cmd_exit(KVM_PV_PREP_RESET, NULL);
}

int s390_pv_verify(void)
{
    return s390_pv_cmd(KVM_PV_VERIFY, NULL);
}

void s390_pv_unshare(void)
{
    s390_pv_cmd_exit(KVM_PV_UNSHARE_ALL, NULL);
}

void s390_pv_inject_reset_error(CPUState *cs)
{
    int r1 = (cs->kvm_run->s390_sieic.ipa & 0x00f0) >> 4;
    CPUS390XState *env = &S390_CPU(cs)->env;

    /* Report that we are unable to enter protected mode */
    env->regs[r1 + 1] = DIAG_308_RC_INVAL_FOR_PV;
}

uint64_t kvm_s390_pv_dmp_get_size_cpu(void)
{
    return info_dump.dump_cpu_buffer_len;
}

uint64_t kvm_s390_pv_dmp_get_size_completion_data(void)
{
    return info_dump.dump_config_finalize_len;
}

uint64_t kvm_s390_pv_dmp_get_size_mem_state(void)
{
    return info_dump.dump_config_mem_buffer_per_1m;
}

bool kvm_s390_pv_info_basic_valid(void)
{
    return info_valid;
}

static int s390_pv_dump_cmd(uint64_t subcmd, uint64_t uaddr, uint64_t gaddr,
                            uint64_t len)
{
    struct kvm_s390_pv_dmp dmp = {
        .subcmd = subcmd,
        .buff_addr = uaddr,
        .buff_len = len,
        .gaddr = gaddr,
    };
    int ret;

    ret = s390_pv_cmd(KVM_PV_DUMP, (void *)&dmp);
    if (ret) {
        error_report("KVM DUMP command %ld failed", subcmd);
    }
    return ret;
}

int kvm_s390_dump_cpu(S390CPU *cpu, void *buff)
{
    struct kvm_s390_pv_dmp dmp = {
        .subcmd = KVM_PV_DUMP_CPU,
        .buff_addr = (uint64_t)buff,
        .gaddr = 0,
        .buff_len = info_dump.dump_cpu_buffer_len,
    };
    struct kvm_pv_cmd pv = {
        .cmd = KVM_PV_DUMP,
        .data = (uint64_t)&dmp,
    };

    return kvm_vcpu_ioctl(CPU(cpu), KVM_S390_PV_CPU_COMMAND, &pv);
}

int kvm_s390_dump_init(void)
{
    return s390_pv_dump_cmd(KVM_PV_DUMP_INIT, 0, 0, 0);
}

int kvm_s390_dump_mem_state(uint64_t gaddr, size_t len, void *dest)
{
    return s390_pv_dump_cmd(KVM_PV_DUMP_CONFIG_STOR_STATE, (uint64_t)dest,
                            gaddr, len);
}

int kvm_s390_dump_completion_data(void *buff)
{
    return s390_pv_dump_cmd(KVM_PV_DUMP_COMPLETE, (uint64_t)buff, 0,
                            info_dump.dump_config_finalize_len);
}

#define TYPE_S390_PV_GUEST "s390-pv-guest"
OBJECT_DECLARE_SIMPLE_TYPE(S390PVGuest, S390_PV_GUEST)

/**
 * S390PVGuest:
 *
 * The S390PVGuest object is basically a dummy used to tell the
 * confidential guest support system to use s390's PV mechanism.
 *
 * # $QEMU \
 *         -object s390-pv-guest,id=pv0 \
 *         -machine ...,confidential-guest-support=pv0
 */
struct S390PVGuest {
    ConfidentialGuestSupport parent_obj;
};

typedef struct S390PVGuestClass S390PVGuestClass;

struct S390PVGuestClass {
    ConfidentialGuestSupportClass parent_class;
};

/*
 * If protected virtualization is enabled, the amount of data that the
 * Read SCP Info Service Call can use is limited to one page. The
 * available space also depends on the Extended-Length SCCB (ELS)
 * feature which can take more buffer space to store feature
 * information. This impacts the maximum number of CPUs supported in
 * the machine.
 */
static uint32_t s390_pv_get_max_cpus(void)
{
    int offset_cpu = s390_has_feat(S390_FEAT_EXTENDED_LENGTH_SCCB) ?
        offsetof(ReadInfo, entries) : SCLP_READ_SCP_INFO_FIXED_CPU_OFFSET;

    return (TARGET_PAGE_SIZE - offset_cpu) / sizeof(CPUEntry);
}

static bool s390_pv_check_cpus(Error **errp)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    uint32_t pv_max_cpus = s390_pv_get_max_cpus();

    if (ms->smp.max_cpus > pv_max_cpus) {
        error_setg(errp, "Protected VMs support a maximum of %d CPUs",
                   pv_max_cpus);
        return false;
    }

    return true;
}

static bool s390_pv_guest_check(ConfidentialGuestSupport *cgs, Error **errp)
{
    return s390_pv_check_cpus(errp);
}

int s390_pv_kvm_init(ConfidentialGuestSupport *cgs, Error **errp)
{
    if (!object_dynamic_cast(OBJECT(cgs), TYPE_S390_PV_GUEST)) {
        return 0;
    }

    if (!s390_has_feat(S390_FEAT_UNPACK)) {
        error_setg(errp,
                   "CPU model does not support Protected Virtualization");
        return -1;
    }

    if (!s390_pv_guest_check(cgs, errp)) {
        return -1;
    }

    cgs->ready = true;

    return 0;
}

OBJECT_DEFINE_TYPE_WITH_INTERFACES(S390PVGuest,
                                   s390_pv_guest,
                                   S390_PV_GUEST,
                                   CONFIDENTIAL_GUEST_SUPPORT,
                                   { TYPE_USER_CREATABLE },
                                   { NULL })

static void s390_pv_guest_class_init(ObjectClass *oc, void *data)
{
}

static void s390_pv_guest_init(Object *obj)
{
}

static void s390_pv_guest_finalize(Object *obj)
{
}
