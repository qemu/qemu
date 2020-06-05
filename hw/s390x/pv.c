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

#include "cpu.h"
#include "qemu/error-report.h"
#include "sysemu/kvm.h"
#include "hw/s390x/ipl.h"
#include "hw/s390x/pv.h"

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
#define s390_pv_cmd(cmd, data) __s390_pv_cmd(cmd, #cmd, data);
#define s390_pv_cmd_exit(cmd, data)    \
{                                      \
    int rc;                            \
                                       \
    rc = __s390_pv_cmd(cmd, #cmd, data);\
    if (rc) {                          \
        exit(1);                       \
    }                                  \
}

int s390_pv_vm_enable(void)
{
    return s390_pv_cmd(KVM_PV_ENABLE, NULL);
}

void s390_pv_vm_disable(void)
{
     s390_pv_cmd_exit(KVM_PV_DISABLE, NULL);
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
