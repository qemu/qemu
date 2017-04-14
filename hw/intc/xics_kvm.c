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
#include "qemu-common.h"
#include "cpu.h"
#include "hw/hw.h"
#include "trace.h"
#include "sysemu/kvm.h"
#include "hw/ppc/spapr.h"
#include "hw/ppc/xics.h"
#include "kvm_ppc.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"

#include <sys/ioctl.h>

static int kernel_xics_fd = -1;

/*
 * ICP-KVM
 */
static void icp_get_kvm_state(ICPState *icp)
{
    uint64_t state;
    struct kvm_one_reg reg = {
        .id = KVM_REG_PPC_ICP_STATE,
        .addr = (uintptr_t)&state,
    };
    int ret;

    /* ICP for this CPU thread is not in use, exiting */
    if (!icp->cs) {
        return;
    }

    ret = kvm_vcpu_ioctl(icp->cs, KVM_GET_ONE_REG, &reg);
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

static int icp_set_kvm_state(ICPState *icp, int version_id)
{
    uint64_t state;
    struct kvm_one_reg reg = {
        .id = KVM_REG_PPC_ICP_STATE,
        .addr = (uintptr_t)&state,
    };
    int ret;

    /* ICP for this CPU thread is not in use, exiting */
    if (!icp->cs) {
        return 0;
    }

    state = ((uint64_t)icp->xirr << KVM_REG_PPC_ICP_XISR_SHIFT)
        | ((uint64_t)icp->mfrr << KVM_REG_PPC_ICP_MFRR_SHIFT)
        | ((uint64_t)icp->pending_priority << KVM_REG_PPC_ICP_PPRI_SHIFT);

    ret = kvm_vcpu_ioctl(icp->cs, KVM_SET_ONE_REG, &reg);
    if (ret != 0) {
        error_report("Unable to restore KVM interrupt controller state (0x%"
                PRIx64 ") for CPU %ld: %s", state, kvm_arch_vcpu_id(icp->cs),
                strerror(errno));
        return ret;
    }

    return 0;
}

static void icp_kvm_reset(void *dev)
{
    ICPState *icp = ICP(dev);

    icp->xirr = 0;
    icp->pending_priority = 0xff;
    icp->mfrr = 0xff;

    /* Make all outputs as deasserted only if the CPU thread is in use */
    if (icp->output) {
        qemu_set_irq(icp->output, 0);
    }

    icp_set_kvm_state(icp, 1);
}

static void icp_kvm_cpu_setup(ICPState *icp, PowerPCCPU *cpu)
{
    CPUState *cs = CPU(cpu);
    int ret;

    if (kernel_xics_fd == -1) {
        abort();
    }

    /*
     * If we are reusing a parked vCPU fd corresponding to the CPU
     * which was hot-removed earlier we don't have to renable
     * KVM_CAP_IRQ_XICS capability again.
     */
    if (icp->cap_irq_xics_enabled) {
        return;
    }

    ret = kvm_vcpu_enable_cap(cs, KVM_CAP_IRQ_XICS, 0, kernel_xics_fd,
                              kvm_arch_vcpu_id(cs));
    if (ret < 0) {
        error_report("Unable to connect CPU%ld to kernel XICS: %s",
                     kvm_arch_vcpu_id(cs), strerror(errno));
        exit(1);
    }
    icp->cap_irq_xics_enabled = true;
}

static void icp_kvm_realize(DeviceState *dev, Error **errp)
{
    qemu_register_reset(icp_kvm_reset, dev);
}

static void icp_kvm_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ICPStateClass *icpc = ICP_CLASS(klass);

    dc->realize = icp_kvm_realize;
    icpc->pre_save = icp_get_kvm_state;
    icpc->post_load = icp_set_kvm_state;
    icpc->cpu_setup = icp_kvm_cpu_setup;
}

static const TypeInfo icp_kvm_info = {
    .name = TYPE_KVM_ICP,
    .parent = TYPE_ICP,
    .instance_size = sizeof(ICPState),
    .class_init = icp_kvm_class_init,
    .class_size = sizeof(ICPStateClass),
};

/*
 * ICS-KVM
 */
static void ics_get_kvm_state(ICSState *ics)
{
    uint64_t state;
    struct kvm_device_attr attr = {
        .flags = 0,
        .group = KVM_DEV_XICS_GRP_SOURCES,
        .addr = (uint64_t)(uintptr_t)&state,
    };
    int i;

    for (i = 0; i < ics->nr_irqs; i++) {
        ICSIRQState *irq = &ics->irqs[i];
        int ret;

        attr.attr = i + ics->offset;

        ret = ioctl(kernel_xics_fd, KVM_GET_DEVICE_ATTR, &attr);
        if (ret != 0) {
            error_report("Unable to retrieve KVM interrupt controller state"
                    " for IRQ %d: %s", i + ics->offset, strerror(errno));
            exit(1);
        }

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
    }
}

static int ics_set_kvm_state(ICSState *ics, int version_id)
{
    uint64_t state;
    struct kvm_device_attr attr = {
        .flags = 0,
        .group = KVM_DEV_XICS_GRP_SOURCES,
        .addr = (uint64_t)(uintptr_t)&state,
    };
    int i;

    for (i = 0; i < ics->nr_irqs; i++) {
        ICSIRQState *irq = &ics->irqs[i];
        int ret;

        attr.attr = i + ics->offset;

        state = irq->server;
        state |= (uint64_t)(irq->saved_priority & KVM_XICS_PRIORITY_MASK)
            << KVM_XICS_PRIORITY_SHIFT;
        if (irq->priority != irq->saved_priority) {
            assert(irq->priority == 0xff);
            state |= KVM_XICS_MASKED;
        }

        if (ics->irqs[i].flags & XICS_FLAGS_IRQ_LSI) {
            state |= KVM_XICS_LEVEL_SENSITIVE;
            if (irq->status & XICS_STATUS_ASSERTED) {
                state |= KVM_XICS_PENDING;
            }
        } else {
            if (irq->status & XICS_STATUS_MASKED_PENDING) {
                state |= KVM_XICS_PENDING;
            }
        }

        ret = ioctl(kernel_xics_fd, KVM_SET_DEVICE_ATTR, &attr);
        if (ret != 0) {
            error_report("Unable to restore KVM interrupt controller state"
                    " for IRQs %d: %s", i + ics->offset, strerror(errno));
            return ret;
        }
    }

    return 0;
}

static void ics_kvm_set_irq(void *opaque, int srcno, int val)
{
    ICSState *ics = opaque;
    struct kvm_irq_level args;
    int rc;

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

static void ics_kvm_reset(void *dev)
{
    ICSState *ics = ICS_SIMPLE(dev);
    int i;
    uint8_t flags[ics->nr_irqs];

    for (i = 0; i < ics->nr_irqs; i++) {
        flags[i] = ics->irqs[i].flags;
    }

    memset(ics->irqs, 0, sizeof(ICSIRQState) * ics->nr_irqs);

    for (i = 0; i < ics->nr_irqs; i++) {
        ics->irqs[i].priority = 0xff;
        ics->irqs[i].saved_priority = 0xff;
        ics->irqs[i].flags = flags[i];
    }

    ics_set_kvm_state(ics, 1);
}

static void ics_kvm_realize(DeviceState *dev, Error **errp)
{
    ICSState *ics = ICS_SIMPLE(dev);

    if (!ics->nr_irqs) {
        error_setg(errp, "Number of interrupts needs to be greater 0");
        return;
    }
    ics->irqs = g_malloc0(ics->nr_irqs * sizeof(ICSIRQState));
    ics->qirqs = qemu_allocate_irqs(ics_kvm_set_irq, ics, ics->nr_irqs);

    qemu_register_reset(ics_kvm_reset, dev);
}

static void ics_kvm_class_init(ObjectClass *klass, void *data)
{
    ICSStateClass *icsc = ICS_BASE_CLASS(klass);

    icsc->realize = ics_kvm_realize;
    icsc->pre_save = ics_get_kvm_state;
    icsc->post_load = ics_set_kvm_state;
}

static const TypeInfo ics_kvm_info = {
    .name = TYPE_ICS_KVM,
    .parent = TYPE_ICS_SIMPLE,
    .instance_size = sizeof(ICSState),
    .class_init = ics_kvm_class_init,
};

/*
 * XICS-KVM
 */

static void rtas_dummy(PowerPCCPU *cpu, sPAPRMachineState *spapr,
                       uint32_t token,
                       uint32_t nargs, target_ulong args,
                       uint32_t nret, target_ulong rets)
{
    error_report("pseries: %s must never be called for in-kernel XICS",
                 __func__);
}

int xics_kvm_init(sPAPRMachineState *spapr, Error **errp)
{
    int rc;
    struct kvm_create_device xics_create_device = {
        .type = KVM_DEV_TYPE_XICS,
        .flags = 0,
    };

    if (!kvm_enabled() || !kvm_check_extension(kvm_state, KVM_CAP_IRQ_XICS)) {
        error_setg(errp,
                   "KVM and IRQ_XICS capability must be present for in-kernel XICS");
        goto fail;
    }

    spapr_rtas_register(RTAS_IBM_SET_XIVE, "ibm,set-xive", rtas_dummy);
    spapr_rtas_register(RTAS_IBM_GET_XIVE, "ibm,get-xive", rtas_dummy);
    spapr_rtas_register(RTAS_IBM_INT_OFF, "ibm,int-off", rtas_dummy);
    spapr_rtas_register(RTAS_IBM_INT_ON, "ibm,int-on", rtas_dummy);

    rc = kvmppc_define_rtas_kernel_token(RTAS_IBM_SET_XIVE, "ibm,set-xive");
    if (rc < 0) {
        error_setg(errp, "kvmppc_define_rtas_kernel_token: ibm,set-xive");
        goto fail;
    }

    rc = kvmppc_define_rtas_kernel_token(RTAS_IBM_GET_XIVE, "ibm,get-xive");
    if (rc < 0) {
        error_setg(errp, "kvmppc_define_rtas_kernel_token: ibm,get-xive");
        goto fail;
    }

    rc = kvmppc_define_rtas_kernel_token(RTAS_IBM_INT_ON, "ibm,int-on");
    if (rc < 0) {
        error_setg(errp, "kvmppc_define_rtas_kernel_token: ibm,int-on");
        goto fail;
    }

    rc = kvmppc_define_rtas_kernel_token(RTAS_IBM_INT_OFF, "ibm,int-off");
    if (rc < 0) {
        error_setg(errp, "kvmppc_define_rtas_kernel_token: ibm,int-off");
        goto fail;
    }

    /* Create the kernel ICP */
    rc = kvm_vm_ioctl(kvm_state, KVM_CREATE_DEVICE, &xics_create_device);
    if (rc < 0) {
        error_setg_errno(errp, -rc, "Error on KVM_CREATE_DEVICE for XICS");
        goto fail;
    }

    kernel_xics_fd = xics_create_device.fd;

    kvm_kernel_irqchip = true;
    kvm_msi_via_irqfd_allowed = true;
    kvm_gsi_direct_mapping = true;

    return rc;

fail:
    kvmppc_define_rtas_kernel_token(0, "ibm,set-xive");
    kvmppc_define_rtas_kernel_token(0, "ibm,get-xive");
    kvmppc_define_rtas_kernel_token(0, "ibm,int-on");
    kvmppc_define_rtas_kernel_token(0, "ibm,int-off");
    return -1;
}

static void xics_kvm_register_types(void)
{
    type_register_static(&ics_kvm_info);
    type_register_static(&icp_kvm_info);
}

type_init(xics_kvm_register_types)
