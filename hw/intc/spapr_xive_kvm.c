/*
 * QEMU PowerPC sPAPR XIVE interrupt controller model
 *
 * Copyright (c) 2017-2019, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "target/ppc/cpu.h"
#include "sysemu/cpus.h"
#include "sysemu/kvm.h"
#include "sysemu/runstate.h"
#include "hw/ppc/spapr.h"
#include "hw/ppc/spapr_cpu_core.h"
#include "hw/ppc/spapr_xive.h"
#include "hw/ppc/xive.h"
#include "kvm_ppc.h"

#include <sys/ioctl.h>

/*
 * Helpers for CPU hotplug
 *
 * TODO: make a common KVMEnabledCPU layer for XICS and XIVE
 */
typedef struct KVMEnabledCPU {
    unsigned long vcpu_id;
    QLIST_ENTRY(KVMEnabledCPU) node;
} KVMEnabledCPU;

static QLIST_HEAD(, KVMEnabledCPU)
    kvm_enabled_cpus = QLIST_HEAD_INITIALIZER(&kvm_enabled_cpus);

static bool kvm_cpu_is_enabled(CPUState *cs)
{
    KVMEnabledCPU *enabled_cpu;
    unsigned long vcpu_id = kvm_arch_vcpu_id(cs);

    QLIST_FOREACH(enabled_cpu, &kvm_enabled_cpus, node) {
        if (enabled_cpu->vcpu_id == vcpu_id) {
            return true;
        }
    }
    return false;
}

static void kvm_cpu_enable(CPUState *cs)
{
    KVMEnabledCPU *enabled_cpu;
    unsigned long vcpu_id = kvm_arch_vcpu_id(cs);

    enabled_cpu = g_malloc(sizeof(*enabled_cpu));
    enabled_cpu->vcpu_id = vcpu_id;
    QLIST_INSERT_HEAD(&kvm_enabled_cpus, enabled_cpu, node);
}

static void kvm_cpu_disable_all(void)
{
    KVMEnabledCPU *enabled_cpu, *next;

    QLIST_FOREACH_SAFE(enabled_cpu, &kvm_enabled_cpus, node, next) {
        QLIST_REMOVE(enabled_cpu, node);
        g_free(enabled_cpu);
    }
}

/*
 * XIVE Thread Interrupt Management context (KVM)
 */

void kvmppc_xive_cpu_set_state(XiveTCTX *tctx, Error **errp)
{
    SpaprXive *xive = SPAPR_MACHINE(qdev_get_machine())->xive;
    uint64_t state[2];
    int ret;

    /* The KVM XIVE device is not in use yet */
    if (xive->fd == -1) {
        return;
    }

    /* word0 and word1 of the OS ring. */
    state[0] = *((uint64_t *) &tctx->regs[TM_QW1_OS]);

    ret = kvm_set_one_reg(tctx->cs, KVM_REG_PPC_VP_STATE, state);
    if (ret != 0) {
        error_setg_errno(errp, errno,
                         "XIVE: could not restore KVM state of CPU %ld",
                         kvm_arch_vcpu_id(tctx->cs));
    }
}

void kvmppc_xive_cpu_get_state(XiveTCTX *tctx, Error **errp)
{
    SpaprXive *xive = SPAPR_MACHINE(qdev_get_machine())->xive;
    uint64_t state[2] = { 0 };
    int ret;

    /* The KVM XIVE device is not in use */
    if (xive->fd == -1) {
        return;
    }

    ret = kvm_get_one_reg(tctx->cs, KVM_REG_PPC_VP_STATE, state);
    if (ret != 0) {
        error_setg_errno(errp, errno,
                         "XIVE: could not capture KVM state of CPU %ld",
                         kvm_arch_vcpu_id(tctx->cs));
        return;
    }

    /* word0 and word1 of the OS ring. */
    *((uint64_t *) &tctx->regs[TM_QW1_OS]) = state[0];
}

typedef struct {
    XiveTCTX *tctx;
    Error *err;
} XiveCpuGetState;

static void kvmppc_xive_cpu_do_synchronize_state(CPUState *cpu,
                                                 run_on_cpu_data arg)
{
    XiveCpuGetState *s = arg.host_ptr;

    kvmppc_xive_cpu_get_state(s->tctx, &s->err);
}

void kvmppc_xive_cpu_synchronize_state(XiveTCTX *tctx, Error **errp)
{
    XiveCpuGetState s = {
        .tctx = tctx,
        .err = NULL,
    };

    /*
     * Kick the vCPU to make sure they are available for the KVM ioctl.
     */
    run_on_cpu(tctx->cs, kvmppc_xive_cpu_do_synchronize_state,
               RUN_ON_CPU_HOST_PTR(&s));

    if (s.err) {
        error_propagate(errp, s.err);
        return;
    }
}

void kvmppc_xive_cpu_connect(XiveTCTX *tctx, Error **errp)
{
    SpaprXive *xive = SPAPR_MACHINE(qdev_get_machine())->xive;
    unsigned long vcpu_id;
    int ret;

    /* The KVM XIVE device is not in use */
    if (xive->fd == -1) {
        return;
    }

    /* Check if CPU was hot unplugged and replugged. */
    if (kvm_cpu_is_enabled(tctx->cs)) {
        return;
    }

    vcpu_id = kvm_arch_vcpu_id(tctx->cs);

    ret = kvm_vcpu_enable_cap(tctx->cs, KVM_CAP_PPC_IRQ_XIVE, 0, xive->fd,
                              vcpu_id, 0);
    if (ret < 0) {
        error_setg(errp, "XIVE: unable to connect CPU%ld to KVM device: %s",
                   vcpu_id, strerror(errno));
        return;
    }

    kvm_cpu_enable(tctx->cs);
}

/*
 * XIVE Interrupt Source (KVM)
 */

void kvmppc_xive_set_source_config(SpaprXive *xive, uint32_t lisn, XiveEAS *eas,
                                   Error **errp)
{
    uint32_t end_idx;
    uint32_t end_blk;
    uint8_t priority;
    uint32_t server;
    bool masked;
    uint32_t eisn;
    uint64_t kvm_src;
    Error *local_err = NULL;

    assert(xive_eas_is_valid(eas));

    end_idx = xive_get_field64(EAS_END_INDEX, eas->w);
    end_blk = xive_get_field64(EAS_END_BLOCK, eas->w);
    eisn = xive_get_field64(EAS_END_DATA, eas->w);
    masked = xive_eas_is_masked(eas);

    spapr_xive_end_to_target(end_blk, end_idx, &server, &priority);

    kvm_src = priority << KVM_XIVE_SOURCE_PRIORITY_SHIFT &
        KVM_XIVE_SOURCE_PRIORITY_MASK;
    kvm_src |= server << KVM_XIVE_SOURCE_SERVER_SHIFT &
        KVM_XIVE_SOURCE_SERVER_MASK;
    kvm_src |= ((uint64_t) masked << KVM_XIVE_SOURCE_MASKED_SHIFT) &
        KVM_XIVE_SOURCE_MASKED_MASK;
    kvm_src |= ((uint64_t)eisn << KVM_XIVE_SOURCE_EISN_SHIFT) &
        KVM_XIVE_SOURCE_EISN_MASK;

    kvm_device_access(xive->fd, KVM_DEV_XIVE_GRP_SOURCE_CONFIG, lisn,
                      &kvm_src, true, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
}

void kvmppc_xive_sync_source(SpaprXive *xive, uint32_t lisn, Error **errp)
{
    kvm_device_access(xive->fd, KVM_DEV_XIVE_GRP_SOURCE_SYNC, lisn,
                      NULL, true, errp);
}

/*
 * At reset, the interrupt sources are simply created and MASKED. We
 * only need to inform the KVM XIVE device about their type: LSI or
 * MSI.
 */
int kvmppc_xive_source_reset_one(XiveSource *xsrc, int srcno, Error **errp)
{
    SpaprXive *xive = SPAPR_XIVE(xsrc->xive);
    uint64_t state = 0;

    /* The KVM XIVE device is not in use */
    if (xive->fd == -1) {
        return -ENODEV;
    }

    if (xive_source_irq_is_lsi(xsrc, srcno)) {
        state |= KVM_XIVE_LEVEL_SENSITIVE;
        if (xsrc->status[srcno] & XIVE_STATUS_ASSERTED) {
            state |= KVM_XIVE_LEVEL_ASSERTED;
        }
    }

    return kvm_device_access(xive->fd, KVM_DEV_XIVE_GRP_SOURCE, srcno, &state,
                             true, errp);
}

static void kvmppc_xive_source_reset(XiveSource *xsrc, Error **errp)
{
    SpaprXive *xive = SPAPR_XIVE(xsrc->xive);
    int i;

    for (i = 0; i < xsrc->nr_irqs; i++) {
        Error *local_err = NULL;

        if (!xive_eas_is_valid(&xive->eat[i])) {
            continue;
        }

        kvmppc_xive_source_reset_one(xsrc, i, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    }
}

/*
 * This is used to perform the magic loads on the ESB pages, described
 * in xive.h.
 *
 * Memory barriers should not be needed for loads (no store for now).
 */
static uint64_t xive_esb_rw(XiveSource *xsrc, int srcno, uint32_t offset,
                            uint64_t data, bool write)
{
    uint64_t *addr = xsrc->esb_mmap + xive_source_esb_mgmt(xsrc, srcno) +
        offset;

    if (write) {
        *addr = cpu_to_be64(data);
        return -1;
    } else {
        /* Prevent the compiler from optimizing away the load */
        volatile uint64_t value = be64_to_cpu(*addr);
        return value;
    }
}

static uint8_t xive_esb_read(XiveSource *xsrc, int srcno, uint32_t offset)
{
    return xive_esb_rw(xsrc, srcno, offset, 0, 0) & 0x3;
}

static void xive_esb_trigger(XiveSource *xsrc, int srcno)
{
    uint64_t *addr = xsrc->esb_mmap + xive_source_esb_page(xsrc, srcno);

    *addr = 0x0;
}

uint64_t kvmppc_xive_esb_rw(XiveSource *xsrc, int srcno, uint32_t offset,
                            uint64_t data, bool write)
{
    if (write) {
        return xive_esb_rw(xsrc, srcno, offset, data, 1);
    }

    /*
     * Special Load EOI handling for LSI sources. Q bit is never set
     * and the interrupt should be re-triggered if the level is still
     * asserted.
     */
    if (xive_source_irq_is_lsi(xsrc, srcno) &&
        offset == XIVE_ESB_LOAD_EOI) {
        xive_esb_read(xsrc, srcno, XIVE_ESB_SET_PQ_00);
        if (xsrc->status[srcno] & XIVE_STATUS_ASSERTED) {
            xive_esb_trigger(xsrc, srcno);
        }
        return 0;
    } else {
        return xive_esb_rw(xsrc, srcno, offset, 0, 0);
    }
}

static void kvmppc_xive_source_get_state(XiveSource *xsrc)
{
    SpaprXive *xive = SPAPR_XIVE(xsrc->xive);
    int i;

    for (i = 0; i < xsrc->nr_irqs; i++) {
        uint8_t pq;

        if (!xive_eas_is_valid(&xive->eat[i])) {
            continue;
        }

        /* Perform a load without side effect to retrieve the PQ bits */
        pq = xive_esb_read(xsrc, i, XIVE_ESB_GET);

        /* and save PQ locally */
        xive_source_esb_set(xsrc, i, pq);
    }
}

void kvmppc_xive_source_set_irq(void *opaque, int srcno, int val)
{
    XiveSource *xsrc = opaque;
    SpaprXive *xive = SPAPR_XIVE(xsrc->xive);
    struct kvm_irq_level args;
    int rc;

    /* The KVM XIVE device should be in use */
    assert(xive->fd != -1);

    args.irq = srcno;
    if (!xive_source_irq_is_lsi(xsrc, srcno)) {
        if (!val) {
            return;
        }
        args.level = KVM_INTERRUPT_SET;
    } else {
        if (val) {
            xsrc->status[srcno] |= XIVE_STATUS_ASSERTED;
            args.level = KVM_INTERRUPT_SET_LEVEL;
        } else {
            xsrc->status[srcno] &= ~XIVE_STATUS_ASSERTED;
            args.level = KVM_INTERRUPT_UNSET;
        }
    }
    rc = kvm_vm_ioctl(kvm_state, KVM_IRQ_LINE, &args);
    if (rc < 0) {
        error_report("XIVE: kvm_irq_line() failed : %s", strerror(errno));
    }
}

/*
 * sPAPR XIVE interrupt controller (KVM)
 */
void kvmppc_xive_get_queue_config(SpaprXive *xive, uint8_t end_blk,
                                  uint32_t end_idx, XiveEND *end,
                                  Error **errp)
{
    struct kvm_ppc_xive_eq kvm_eq = { 0 };
    uint64_t kvm_eq_idx;
    uint8_t priority;
    uint32_t server;
    Error *local_err = NULL;

    assert(xive_end_is_valid(end));

    /* Encode the tuple (server, prio) as a KVM EQ index */
    spapr_xive_end_to_target(end_blk, end_idx, &server, &priority);

    kvm_eq_idx = priority << KVM_XIVE_EQ_PRIORITY_SHIFT &
            KVM_XIVE_EQ_PRIORITY_MASK;
    kvm_eq_idx |= server << KVM_XIVE_EQ_SERVER_SHIFT &
        KVM_XIVE_EQ_SERVER_MASK;

    kvm_device_access(xive->fd, KVM_DEV_XIVE_GRP_EQ_CONFIG, kvm_eq_idx,
                      &kvm_eq, false, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    /*
     * The EQ index and toggle bit are updated by HW. These are the
     * only fields from KVM we want to update QEMU with. The other END
     * fields should already be in the QEMU END table.
     */
    end->w1 = xive_set_field32(END_W1_GENERATION, 0ul, kvm_eq.qtoggle) |
        xive_set_field32(END_W1_PAGE_OFF, 0ul, kvm_eq.qindex);
}

void kvmppc_xive_set_queue_config(SpaprXive *xive, uint8_t end_blk,
                                  uint32_t end_idx, XiveEND *end,
                                  Error **errp)
{
    struct kvm_ppc_xive_eq kvm_eq = { 0 };
    uint64_t kvm_eq_idx;
    uint8_t priority;
    uint32_t server;
    Error *local_err = NULL;

    /*
     * Build the KVM state from the local END structure.
     */

    kvm_eq.flags = 0;
    if (xive_get_field32(END_W0_UCOND_NOTIFY, end->w0)) {
        kvm_eq.flags |= KVM_XIVE_EQ_ALWAYS_NOTIFY;
    }

    /*
     * If the hcall is disabling the EQ, set the size and page address
     * to zero. When migrating, only valid ENDs are taken into
     * account.
     */
    if (xive_end_is_valid(end)) {
        kvm_eq.qshift = xive_get_field32(END_W0_QSIZE, end->w0) + 12;
        kvm_eq.qaddr  = xive_end_qaddr(end);
        /*
         * The EQ toggle bit and index should only be relevant when
         * restoring the EQ state
         */
        kvm_eq.qtoggle = xive_get_field32(END_W1_GENERATION, end->w1);
        kvm_eq.qindex  = xive_get_field32(END_W1_PAGE_OFF, end->w1);
    } else {
        kvm_eq.qshift = 0;
        kvm_eq.qaddr  = 0;
    }

    /* Encode the tuple (server, prio) as a KVM EQ index */
    spapr_xive_end_to_target(end_blk, end_idx, &server, &priority);

    kvm_eq_idx = priority << KVM_XIVE_EQ_PRIORITY_SHIFT &
            KVM_XIVE_EQ_PRIORITY_MASK;
    kvm_eq_idx |= server << KVM_XIVE_EQ_SERVER_SHIFT &
        KVM_XIVE_EQ_SERVER_MASK;

    kvm_device_access(xive->fd, KVM_DEV_XIVE_GRP_EQ_CONFIG, kvm_eq_idx,
                      &kvm_eq, true, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
}

void kvmppc_xive_reset(SpaprXive *xive, Error **errp)
{
    kvm_device_access(xive->fd, KVM_DEV_XIVE_GRP_CTRL, KVM_DEV_XIVE_RESET,
                      NULL, true, errp);
}

static void kvmppc_xive_get_queues(SpaprXive *xive, Error **errp)
{
    Error *local_err = NULL;
    int i;

    for (i = 0; i < xive->nr_ends; i++) {
        if (!xive_end_is_valid(&xive->endt[i])) {
            continue;
        }

        kvmppc_xive_get_queue_config(xive, SPAPR_XIVE_BLOCK_ID, i,
                                     &xive->endt[i], &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    }
}

/*
 * The primary goal of the XIVE VM change handler is to mark the EQ
 * pages dirty when all XIVE event notifications have stopped.
 *
 * Whenever the VM is stopped, the VM change handler sets the source
 * PQs to PENDING to stop the flow of events and to possibly catch a
 * triggered interrupt occuring while the VM is stopped. The previous
 * state is saved in anticipation of a migration. The XIVE controller
 * is then synced through KVM to flush any in-flight event
 * notification and stabilize the EQs.
 *
 * At this stage, we can mark the EQ page dirty and let a migration
 * sequence transfer the EQ pages to the destination, which is done
 * just after the stop state.
 *
 * The previous configuration of the sources is restored when the VM
 * runs again. If an interrupt was queued while the VM was stopped,
 * simply generate a trigger.
 */
static void kvmppc_xive_change_state_handler(void *opaque, int running,
                                             RunState state)
{
    SpaprXive *xive = opaque;
    XiveSource *xsrc = &xive->source;
    Error *local_err = NULL;
    int i;

    /*
     * Restore the sources to their initial state. This is called when
     * the VM resumes after a stop or a migration.
     */
    if (running) {
        for (i = 0; i < xsrc->nr_irqs; i++) {
            uint8_t pq;
            uint8_t old_pq;

            if (!xive_eas_is_valid(&xive->eat[i])) {
                continue;
            }

            pq = xive_source_esb_get(xsrc, i);
            old_pq = xive_esb_read(xsrc, i, XIVE_ESB_SET_PQ_00 + (pq << 8));

            /*
             * An interrupt was queued while the VM was stopped,
             * generate a trigger.
             */
            if (pq == XIVE_ESB_RESET && old_pq == XIVE_ESB_QUEUED) {
                xive_esb_trigger(xsrc, i);
            }
        }

        return;
    }

    /*
     * Mask the sources, to stop the flow of event notifications, and
     * save the PQs locally in the XiveSource object. The XiveSource
     * state will be collected later on by its vmstate handler if a
     * migration is in progress.
     */
    for (i = 0; i < xsrc->nr_irqs; i++) {
        uint8_t pq;

        if (!xive_eas_is_valid(&xive->eat[i])) {
            continue;
        }

        pq = xive_esb_read(xsrc, i, XIVE_ESB_GET);

        /*
         * PQ is set to PENDING to possibly catch a triggered
         * interrupt occuring while the VM is stopped (hotplug event
         * for instance) .
         */
        if (pq != XIVE_ESB_OFF) {
            pq = xive_esb_read(xsrc, i, XIVE_ESB_SET_PQ_10);
        }
        xive_source_esb_set(xsrc, i, pq);
    }

    /*
     * Sync the XIVE controller in KVM, to flush in-flight event
     * notification that should be enqueued in the EQs and mark the
     * XIVE EQ pages dirty to collect all updates.
     */
    kvm_device_access(xive->fd, KVM_DEV_XIVE_GRP_CTRL,
                      KVM_DEV_XIVE_EQ_SYNC, NULL, true, &local_err);
    if (local_err) {
        error_report_err(local_err);
        return;
    }
}

void kvmppc_xive_synchronize_state(SpaprXive *xive, Error **errp)
{
    /* The KVM XIVE device is not in use */
    if (xive->fd == -1) {
        return;
    }

    /*
     * When the VM is stopped, the sources are masked and the previous
     * state is saved in anticipation of a migration. We should not
     * synchronize the source state in that case else we will override
     * the saved state.
     */
    if (runstate_is_running()) {
        kvmppc_xive_source_get_state(&xive->source);
    }

    /* EAT: there is no extra state to query from KVM */

    /* ENDT */
    kvmppc_xive_get_queues(xive, errp);
}

/*
 * The SpaprXive 'pre_save' method is called by the vmstate handler of
 * the SpaprXive model, after the XIVE controller is synced in the VM
 * change handler.
 */
int kvmppc_xive_pre_save(SpaprXive *xive)
{
    Error *local_err = NULL;

    /* The KVM XIVE device is not in use */
    if (xive->fd == -1) {
        return 0;
    }

    /* EAT: there is no extra state to query from KVM */

    /* ENDT */
    kvmppc_xive_get_queues(xive, &local_err);
    if (local_err) {
        error_report_err(local_err);
        return -1;
    }

    return 0;
}

/*
 * The SpaprXive 'post_load' method is not called by a vmstate
 * handler. It is called at the sPAPR machine level at the end of the
 * migration sequence by the sPAPR IRQ backend 'post_load' method,
 * when all XIVE states have been transferred and loaded.
 */
int kvmppc_xive_post_load(SpaprXive *xive, int version_id)
{
    Error *local_err = NULL;
    CPUState *cs;
    int i;

    /* The KVM XIVE device should be in use */
    assert(xive->fd != -1);

    /* Restore the ENDT first. The targetting depends on it. */
    for (i = 0; i < xive->nr_ends; i++) {
        if (!xive_end_is_valid(&xive->endt[i])) {
            continue;
        }

        kvmppc_xive_set_queue_config(xive, SPAPR_XIVE_BLOCK_ID, i,
                                     &xive->endt[i], &local_err);
        if (local_err) {
            error_report_err(local_err);
            return -1;
        }
    }

    /* Restore the EAT */
    for (i = 0; i < xive->nr_irqs; i++) {
        if (!xive_eas_is_valid(&xive->eat[i])) {
            continue;
        }

        /*
         * We can only restore the source config if the source has been
         * previously set in KVM. Since we don't do that for all interrupts
         * at reset time anymore, let's do it now.
         */
        kvmppc_xive_source_reset_one(&xive->source, i, &local_err);
        if (local_err) {
            error_report_err(local_err);
            return -1;
        }

        kvmppc_xive_set_source_config(xive, i, &xive->eat[i], &local_err);
        if (local_err) {
            error_report_err(local_err);
            return -1;
        }
    }

    /*
     * Restore the thread interrupt contexts of initial CPUs.
     *
     * The context of hotplugged CPUs is restored later, by the
     * 'post_load' handler of the XiveTCTX model because they are not
     * available at the time the SpaprXive 'post_load' method is
     * called. We can not restore the context of all CPUs in the
     * 'post_load' handler of XiveTCTX because the machine is not
     * necessarily connected to the KVM device at that time.
     */
    CPU_FOREACH(cs) {
        PowerPCCPU *cpu = POWERPC_CPU(cs);

        kvmppc_xive_cpu_set_state(spapr_cpu_state(cpu)->tctx, &local_err);
        if (local_err) {
            error_report_err(local_err);
            return -1;
        }
    }

    /* The source states will be restored when the machine starts running */
    return 0;
}

static void *kvmppc_xive_mmap(SpaprXive *xive, int pgoff, size_t len,
                              Error **errp)
{
    void *addr;
    uint32_t page_shift = 16; /* TODO: fix page_shift */

    addr = mmap(NULL, len, PROT_WRITE | PROT_READ, MAP_SHARED, xive->fd,
                pgoff << page_shift);
    if (addr == MAP_FAILED) {
        error_setg_errno(errp, errno, "XIVE: unable to set memory mapping");
        return NULL;
    }

    return addr;
}

/*
 * All the XIVE memory regions are now backed by mappings from the KVM
 * XIVE device.
 */
int kvmppc_xive_connect(SpaprInterruptController *intc, Error **errp)
{
    SpaprXive *xive = SPAPR_XIVE(intc);
    XiveSource *xsrc = &xive->source;
    Error *local_err = NULL;
    size_t esb_len = (1ull << xsrc->esb_shift) * xsrc->nr_irqs;
    size_t tima_len = 4ull << TM_SHIFT;
    CPUState *cs;

    /*
     * The KVM XIVE device already in use. This is the case when
     * rebooting under the XIVE-only interrupt mode.
     */
    if (xive->fd != -1) {
        return 0;
    }

    if (!kvmppc_has_cap_xive()) {
        error_setg(errp, "IRQ_XIVE capability must be present for KVM");
        return -1;
    }

    /* First, create the KVM XIVE device */
    xive->fd = kvm_create_device(kvm_state, KVM_DEV_TYPE_XIVE, false);
    if (xive->fd < 0) {
        error_setg_errno(errp, -xive->fd, "XIVE: error creating KVM device");
        return -1;
    }

    /*
     * 1. Source ESB pages - KVM mapping
     */
    xsrc->esb_mmap = kvmppc_xive_mmap(xive, KVM_XIVE_ESB_PAGE_OFFSET, esb_len,
                                      &local_err);
    if (local_err) {
        goto fail;
    }

    memory_region_init_ram_device_ptr(&xsrc->esb_mmio_kvm, OBJECT(xsrc),
                                      "xive.esb", esb_len, xsrc->esb_mmap);
    memory_region_add_subregion_overlap(&xsrc->esb_mmio, 0,
                                        &xsrc->esb_mmio_kvm, 1);

    /*
     * 2. END ESB pages (No KVM support yet)
     */

    /*
     * 3. TIMA pages - KVM mapping
     */
    xive->tm_mmap = kvmppc_xive_mmap(xive, KVM_XIVE_TIMA_PAGE_OFFSET, tima_len,
                                     &local_err);
    if (local_err) {
        goto fail;
    }
    memory_region_init_ram_device_ptr(&xive->tm_mmio_kvm, OBJECT(xive),
                                      "xive.tima", tima_len, xive->tm_mmap);
    memory_region_add_subregion_overlap(&xive->tm_mmio, 0,
                                        &xive->tm_mmio_kvm, 1);

    xive->change = qemu_add_vm_change_state_handler(
        kvmppc_xive_change_state_handler, xive);

    /* Connect the presenters to the initial VCPUs of the machine */
    CPU_FOREACH(cs) {
        PowerPCCPU *cpu = POWERPC_CPU(cs);

        kvmppc_xive_cpu_connect(spapr_cpu_state(cpu)->tctx, &local_err);
        if (local_err) {
            goto fail;
        }
    }

    /* Update the KVM sources */
    kvmppc_xive_source_reset(xsrc, &local_err);
    if (local_err) {
        goto fail;
    }

    kvm_kernel_irqchip = true;
    kvm_msi_via_irqfd_allowed = true;
    kvm_gsi_direct_mapping = true;
    return 0;

fail:
    error_propagate(errp, local_err);
    kvmppc_xive_disconnect(intc);
    return -1;
}

void kvmppc_xive_disconnect(SpaprInterruptController *intc)
{
    SpaprXive *xive = SPAPR_XIVE(intc);
    XiveSource *xsrc;
    size_t esb_len;

    /* The KVM XIVE device is not in use */
    if (!xive || xive->fd == -1) {
        return;
    }

    /* Clear the KVM mapping */
    xsrc = &xive->source;
    esb_len = (1ull << xsrc->esb_shift) * xsrc->nr_irqs;

    if (xsrc->esb_mmap) {
        memory_region_del_subregion(&xsrc->esb_mmio, &xsrc->esb_mmio_kvm);
        object_unparent(OBJECT(&xsrc->esb_mmio_kvm));
        munmap(xsrc->esb_mmap, esb_len);
        xsrc->esb_mmap = NULL;
    }

    if (xive->tm_mmap) {
        memory_region_del_subregion(&xive->tm_mmio, &xive->tm_mmio_kvm);
        object_unparent(OBJECT(&xive->tm_mmio_kvm));
        munmap(xive->tm_mmap, 4ull << TM_SHIFT);
        xive->tm_mmap = NULL;
    }

    /*
     * When the KVM device fd is closed, the KVM device is destroyed
     * and removed from the list of devices of the VM. The VCPU
     * presenters are also detached from the device.
     */
    if (xive->fd != -1) {
        close(xive->fd);
        xive->fd = -1;
    }

    kvm_kernel_irqchip = false;
    kvm_msi_via_irqfd_allowed = false;
    kvm_gsi_direct_mapping = false;

    /* Clear the local list of presenter (hotplug) */
    kvm_cpu_disable_all();

    /* VM Change state handler is not needed anymore */
    if (xive->change) {
        qemu_del_vm_change_state_handler(xive->change);
        xive->change = NULL;
    }
}
