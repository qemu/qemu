/*
 * QEMU PowerPC pSeries Logical Partition (aka sPAPR) hardware System Emulator
 *
 * PAPR Virtualized Interrupt System, aka ICS/ICP aka xics, in-kernel emulation
 *
 * Copyright (c) 2013 David Gibson, IBM Corporation.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "trace.h"
#include "system/kvm.h"
#include "hw/ppc/spapr.h"
#include "hw/ppc/spapr_cpu_core.h"
#include "hw/ppc/xics.h"
#include "hw/ppc/xics_spapr.h"
#include "kvm_ppc.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"

#include <sys/ioctl.h>

static int kernel_xics_fd = -1;

typedef struct KVMEnabledICP {
    unsigned long vcpu_id;
    QLIST_ENTRY(KVMEnabledICP) node;
} KVMEnabledICP;

static QLIST_HEAD(, KVMEnabledICP)
    kvm_enabled_icps = QLIST_HEAD_INITIALIZER(&kvm_enabled_icps);

static void kvm_disable_icps(void)
{
    KVMEnabledICP *enabled_icp, *next;

    QLIST_FOREACH_SAFE(enabled_icp, &kvm_enabled_icps, node, next) {
        QLIST_REMOVE(enabled_icp, node);
        g_free(enabled_icp);
    }
}

/*
 * ICP-KVM
 */
void icp_get_kvm_state(ICPState *icp)
{
    uint64_t state;
    int ret;

    /* The KVM XICS device is not in use */
    if (kernel_xics_fd == -1) {
        return;
    }

    /* ICP for this CPU thread is not in use, exiting */
    if (!icp->cs) {
        return;
    }

    ret = kvm_get_one_reg(icp->cs, KVM_REG_PPC_ICP_STATE, &state);
    if (ret != 0) {
        error_report("Unable to retrieve KVM interrupt controller state"
                " for CPU %ld: %s", kvm_arch_vcpu_id(icp->cs), strerror(errno));
        exit(1);
    }

    icp->xirr = state >> KVM_REG_PPC_ICP_XISR_SHIFT;
    icp->mfrr = (state >> KVM_REG_PPC_ICP_MFRR_SHIFT)
        & KVM_REG_PPC_ICP_MFRR_MASK;
    icp->pending_priority = (state >> KVM_REG_PPC_ICP_PPRI_SHIFT)
        & KVM_REG_PPC_ICP_PPRI_MASK;
}

static void do_icp_synchronize_state(CPUState *cpu, run_on_cpu_data arg)
{
    icp_get_kvm_state(arg.host_ptr);
}

void icp_synchronize_state(ICPState *icp)
{
    if (icp->cs) {
        run_on_cpu(icp->cs, do_icp_synchronize_state, RUN_ON_CPU_HOST_PTR(icp));
    }
}

int icp_set_kvm_state(ICPState *icp, Error **errp)
{
    uint64_t state;
    int ret;

    /* The KVM XICS device is not in use */
    if (kernel_xics_fd == -1) {
        return 0;
    }

    /* ICP for this CPU thread is not in use, exiting */
    if (!icp->cs) {
        return 0;
    }

    state = ((uint64_t)icp->xirr << KVM_REG_PPC_ICP_XISR_SHIFT)
        | ((uint64_t)icp->mfrr << KVM_REG_PPC_ICP_MFRR_SHIFT)
        | ((uint64_t)icp->pending_priority << KVM_REG_PPC_ICP_PPRI_SHIFT);

    ret = kvm_set_one_reg(icp->cs, KVM_REG_PPC_ICP_STATE, &state);
    if (ret < 0) {
        error_setg_errno(errp, -ret,
                         "Unable to restore KVM interrupt controller state (0x%"
                         PRIx64 ") for CPU %ld", state,
                         kvm_arch_vcpu_id(icp->cs));
        return ret;
    }

    return 0;
}

void icp_kvm_realize(DeviceState *dev, Error **errp)
{
    ICPState *icp = ICP(dev);
    CPUState *cs;
    KVMEnabledICP *enabled_icp;
    unsigned long vcpu_id;
    int ret;

    /* The KVM XICS device is not in use */
    if (kernel_xics_fd == -1) {
        return;
    }

    cs = icp->cs;
    vcpu_id = kvm_arch_vcpu_id(cs);

    /*
     * If we are reusing a parked vCPU fd corresponding to the CPU
     * which was hot-removed earlier we don't have to renable
     * KVM_CAP_IRQ_XICS capability again.
     */
    QLIST_FOREACH(enabled_icp, &kvm_enabled_icps, node) {
        if (enabled_icp->vcpu_id == vcpu_id) {
            return;
        }
    }

    ret = kvm_vcpu_enable_cap(cs, KVM_CAP_IRQ_XICS, 0, kernel_xics_fd, vcpu_id);
    if (ret < 0) {
        Error *local_err = NULL;

        error_setg(&local_err, "Unable to connect CPU%ld to kernel XICS: %s",
                   vcpu_id, strerror(errno));
        if (errno == ENOSPC) {
            error_append_hint(&local_err, "Try -smp maxcpus=N with N < %u\n",
                              MACHINE(qdev_get_machine())->smp.max_cpus);
        }
        error_propagate(errp, local_err);
        return;
    }
    enabled_icp = g_malloc(sizeof(*enabled_icp));
    enabled_icp->vcpu_id = vcpu_id;
    QLIST_INSERT_HEAD(&kvm_enabled_icps, enabled_icp, node);
}

/*
 * ICS-KVM
 */
void ics_get_kvm_state(ICSState *ics)
{
    uint64_t state;
    int i;

    /* The KVM XICS device is not in use */
    if (kernel_xics_fd == -1) {
        return;
    }

    for (i = 0; i < ics->nr_irqs; i++) {
        ICSIRQState *irq = &ics->irqs[i];

        if (ics_irq_free(ics, i)) {
            continue;
        }

        kvm_device_access(kernel_xics_fd, KVM_DEV_XICS_GRP_SOURCES,
                          i + ics->offset, &state, false, &error_fatal);

        irq->server = state & KVM_XICS_DESTINATION_MASK;
        irq->saved_priority = (state >> KVM_XICS_PRIORITY_SHIFT)
            & KVM_XICS_PRIORITY_MASK;
        /*
         * To be consistent with the software emulation in xics.c, we
         * split out the masked state + priority that we get from the
         * kernel into 'current priority' (0xff if masked) and
         * 'saved priority' (if masked, this is the priority the
         * interrupt had before it was masked).  Masking and unmasking
         * are done with the ibm,int-off and ibm,int-on RTAS calls.
         */
        if (state & KVM_XICS_MASKED) {
            irq->priority = 0xff;
        } else {
            irq->priority = irq->saved_priority;
        }

        irq->status = 0;
        if (state & KVM_XICS_PENDING) {
            if (state & KVM_XICS_LEVEL_SENSITIVE) {
                irq->status |= XICS_STATUS_ASSERTED;
            } else {
                /*
                 * A pending edge-triggered interrupt (or MSI)
                 * must have been rejected previously when we
                 * first detected it and tried to deliver it,
                 * so mark it as pending and previously rejected
                 * for consistency with how xics.c works.
                 */
                irq->status |= XICS_STATUS_MASKED_PENDING
                    | XICS_STATUS_REJECTED;
            }
        }
        if (state & KVM_XICS_PRESENTED) {
                irq->status |= XICS_STATUS_PRESENTED;
        }
        if (state & KVM_XICS_QUEUED) {
                irq->status |= XICS_STATUS_QUEUED;
        }
    }
}

void ics_synchronize_state(ICSState *ics)
{
    ics_get_kvm_state(ics);
}

int ics_set_kvm_state_one(ICSState *ics, int srcno, Error **errp)
{
    uint64_t state;
    ICSIRQState *irq = &ics->irqs[srcno];
    int ret;

    /* The KVM XICS device is not in use */
    if (kernel_xics_fd == -1) {
        return 0;
    }

    state = irq->server;
    state |= (uint64_t)(irq->saved_priority & KVM_XICS_PRIORITY_MASK)
        << KVM_XICS_PRIORITY_SHIFT;
    if (irq->priority != irq->saved_priority) {
        assert(irq->priority == 0xff);
    }

    if (irq->priority == 0xff) {
        state |= KVM_XICS_MASKED;
    }

    if (irq->flags & XICS_FLAGS_IRQ_LSI) {
        state |= KVM_XICS_LEVEL_SENSITIVE;
        if (irq->status & XICS_STATUS_ASSERTED) {
            state |= KVM_XICS_PENDING;
        }
    } else {
        if (irq->status & XICS_STATUS_MASKED_PENDING) {
            state |= KVM_XICS_PENDING;
        }
    }
    if (irq->status & XICS_STATUS_PRESENTED) {
        state |= KVM_XICS_PRESENTED;
    }
    if (irq->status & XICS_STATUS_QUEUED) {
        state |= KVM_XICS_QUEUED;
    }

    ret = kvm_device_access(kernel_xics_fd, KVM_DEV_XICS_GRP_SOURCES,
                            srcno + ics->offset, &state, true, errp);
    if (ret < 0) {
        return ret;
    }

    return 0;
}

int ics_set_kvm_state(ICSState *ics, Error **errp)
{
    int i;

    /* The KVM XICS device is not in use */
    if (kernel_xics_fd == -1) {
        return 0;
    }

    for (i = 0; i < ics->nr_irqs; i++) {
        int ret;

        if (ics_irq_free(ics, i)) {
            continue;
        }

        ret = ics_set_kvm_state_one(ics, i, errp);
        if (ret < 0) {
            return ret;
        }
    }

    return 0;
}

void ics_kvm_set_irq(ICSState *ics, int srcno, int val)
{
    struct kvm_irq_level args;
    int rc;

    /* The KVM XICS device should be in use */
    assert(kernel_xics_fd != -1);

    args.irq = srcno + ics->offset;
    if (ics->irqs[srcno].flags & XICS_FLAGS_IRQ_MSI) {
        if (!val) {
            return;
        }
        args.level = KVM_INTERRUPT_SET;
    } else {
        args.level = val ? KVM_INTERRUPT_SET_LEVEL : KVM_INTERRUPT_UNSET;
    }
    rc = kvm_vm_ioctl(kvm_state, KVM_IRQ_LINE, &args);
    if (rc < 0) {
        perror("kvm_irq_line");
    }
}

int xics_kvm_connect(SpaprInterruptController *intc, uint32_t nr_servers,
                     Error **errp)
{
    ICSState *ics = ICS_SPAPR(intc);
    int rc;
    CPUState *cs;
    Error *local_err = NULL;

    /*
     * The KVM XICS device already in use. This is the case when
     * rebooting under the XICS-only interrupt mode.
     */
    if (kernel_xics_fd != -1) {
        return 0;
    }

    if (!kvm_enabled() || !kvm_check_extension(kvm_state, KVM_CAP_IRQ_XICS)) {
        error_setg(errp,
                   "KVM and IRQ_XICS capability must be present for in-kernel XICS");
        return -1;
    }

    rc = kvmppc_define_rtas_kernel_token(RTAS_IBM_SET_XIVE, "ibm,set-xive");
    if (rc < 0) {
        error_setg_errno(&local_err, -rc,
                         "kvmppc_define_rtas_kernel_token: ibm,set-xive");
        goto fail;
    }

    rc = kvmppc_define_rtas_kernel_token(RTAS_IBM_GET_XIVE, "ibm,get-xive");
    if (rc < 0) {
        error_setg_errno(&local_err, -rc,
                         "kvmppc_define_rtas_kernel_token: ibm,get-xive");
        goto fail;
    }

    rc = kvmppc_define_rtas_kernel_token(RTAS_IBM_INT_ON, "ibm,int-on");
    if (rc < 0) {
        error_setg_errno(&local_err, -rc,
                         "kvmppc_define_rtas_kernel_token: ibm,int-on");
        goto fail;
    }

    rc = kvmppc_define_rtas_kernel_token(RTAS_IBM_INT_OFF, "ibm,int-off");
    if (rc < 0) {
        error_setg_errno(&local_err, -rc,
                         "kvmppc_define_rtas_kernel_token: ibm,int-off");
        goto fail;
    }

    /* Create the KVM XICS device */
    rc = kvm_create_device(kvm_state, KVM_DEV_TYPE_XICS, false);
    if (rc < 0) {
        error_setg_errno(&local_err, -rc, "Error on KVM_CREATE_DEVICE for XICS");
        goto fail;
    }

    /* Tell KVM about the # of VCPUs we may have (POWER9 and newer only) */
    if (kvm_device_check_attr(rc, KVM_DEV_XICS_GRP_CTRL,
                              KVM_DEV_XICS_NR_SERVERS)) {
        if (kvm_device_access(rc, KVM_DEV_XICS_GRP_CTRL,
                              KVM_DEV_XICS_NR_SERVERS, &nr_servers, true,
                              &local_err)) {
            goto fail;
        }
    }

    kernel_xics_fd = rc;
    kvm_kernel_irqchip = true;
    kvm_msi_via_irqfd_allowed = true;
    kvm_gsi_direct_mapping = true;

    /* Create the presenters */
    CPU_FOREACH(cs) {
        PowerPCCPU *cpu = POWERPC_CPU(cs);

        icp_kvm_realize(DEVICE(spapr_cpu_state(cpu)->icp), &local_err);
        if (local_err) {
            goto fail;
        }
    }

    /* Update the KVM sources */
    ics_set_kvm_state(ics, &local_err);
    if (local_err) {
        goto fail;
    }

    /* Connect the presenters to the initial VCPUs of the machine */
    CPU_FOREACH(cs) {
        PowerPCCPU *cpu = POWERPC_CPU(cs);
        icp_set_kvm_state(spapr_cpu_state(cpu)->icp, &local_err);
        if (local_err) {
            goto fail;
        }
    }

    return 0;

fail:
    error_propagate(errp, local_err);
    xics_kvm_disconnect(intc);
    return -1;
}

void xics_kvm_disconnect(SpaprInterruptController *intc)
{
    /*
     * Only on P9 using the XICS-on XIVE KVM device:
     *
     * When the KVM device fd is closed, the device is destroyed and
     * removed from the list of devices of the VM. The VCPU presenters
     * are also detached from the device.
     */
    if (kernel_xics_fd != -1) {
        close(kernel_xics_fd);
        kernel_xics_fd = -1;
    }

    kvmppc_define_rtas_kernel_token(0, "ibm,set-xive");
    kvmppc_define_rtas_kernel_token(0, "ibm,get-xive");
    kvmppc_define_rtas_kernel_token(0, "ibm,int-on");
    kvmppc_define_rtas_kernel_token(0, "ibm,int-off");

    kvm_kernel_irqchip = false;
    kvm_msi_via_irqfd_allowed = false;
    kvm_gsi_direct_mapping = false;

    /* Clear the presenter from the VCPUs */
    kvm_disable_icps();
}

/*
 * This is a heuristic to detect older KVMs on POWER9 hosts that don't
 * support destruction of a KVM XICS device while the VM is running.
 * Required to start a spapr machine with ic-mode=dual,kernel-irqchip=on.
 */
bool xics_kvm_has_broken_disconnect(void)
{
    int rc;

    rc = kvm_create_device(kvm_state, KVM_DEV_TYPE_XICS, false);
    if (rc < 0) {
        /*
         * The error is ignored on purpose. The KVM XICS setup code
         * will catch it again anyway. The goal here is to see if
         * close() actually destroys the device or not.
         */
        return false;
    }

    close(rc);

    rc = kvm_create_device(kvm_state, KVM_DEV_TYPE_XICS, false);
    if (rc >= 0) {
        close(rc);
        return false;
    }

    return errno == EEXIST;
}
