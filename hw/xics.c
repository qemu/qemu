/*
 * QEMU PowerPC pSeries Logical Partition (aka sPAPR) hardware System Emulator
 *
 * PAPR Virtualized Interrupt System, aka ICS/ICP aka xics
 *
 * Copyright (c) 2010,2011 David Gibson, IBM Corporation.
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

#include "hw.h"
#include "hw/spapr.h"
#include "hw/xics.h"

#include <pthread.h>

/*
 * ICP: Presentation layer
 */

struct icp_server_state {
    uint32_t xirr;
    uint8_t pending_priority;
    uint8_t mfrr;
    qemu_irq output;
};

#define XISR_MASK  0x00ffffff
#define CPPR_MASK  0xff000000

#define XISR(ss)   (((ss)->xirr) & XISR_MASK)
#define CPPR(ss)   (((ss)->xirr) >> 24)

struct ics_state;

struct icp_state {
    long nr_servers;
    struct icp_server_state *ss;
    struct ics_state *ics;
};

static void ics_reject(struct ics_state *ics, int nr);
static void ics_resend(struct ics_state *ics);
static void ics_eoi(struct ics_state *ics, int nr);

static void icp_check_ipi(struct icp_state *icp, int server)
{
    struct icp_server_state *ss = icp->ss + server;

    if (XISR(ss) && (ss->pending_priority <= ss->mfrr)) {
        return;
    }

    if (XISR(ss)) {
        ics_reject(icp->ics, XISR(ss));
    }

    ss->xirr = (ss->xirr & ~XISR_MASK) | XICS_IPI;
    ss->pending_priority = ss->mfrr;
    qemu_irq_raise(ss->output);
}

static void icp_resend(struct icp_state *icp, int server)
{
    struct icp_server_state *ss = icp->ss + server;

    if (ss->mfrr < CPPR(ss)) {
        icp_check_ipi(icp, server);
    }
    ics_resend(icp->ics);
}

static void icp_set_cppr(struct icp_state *icp, int server, uint8_t cppr)
{
    struct icp_server_state *ss = icp->ss + server;
    uint8_t old_cppr;
    uint32_t old_xisr;

    old_cppr = CPPR(ss);
    ss->xirr = (ss->xirr & ~CPPR_MASK) | (cppr << 24);

    if (cppr < old_cppr) {
        if (XISR(ss) && (cppr <= ss->pending_priority)) {
            old_xisr = XISR(ss);
            ss->xirr &= ~XISR_MASK; /* Clear XISR */
            qemu_irq_lower(ss->output);
            ics_reject(icp->ics, old_xisr);
        }
    } else {
        if (!XISR(ss)) {
            icp_resend(icp, server);
        }
    }
}

static void icp_set_mfrr(struct icp_state *icp, int nr, uint8_t mfrr)
{
    struct icp_server_state *ss = icp->ss + nr;

    ss->mfrr = mfrr;
    if (mfrr < CPPR(ss)) {
        icp_check_ipi(icp, nr);
    }
}

static uint32_t icp_accept(struct icp_server_state *ss)
{
    uint32_t xirr;

    qemu_irq_lower(ss->output);
    xirr = ss->xirr;
    ss->xirr = ss->pending_priority << 24;
    return xirr;
}

static void icp_eoi(struct icp_state *icp, int server, uint32_t xirr)
{
    struct icp_server_state *ss = icp->ss + server;

    ics_eoi(icp->ics, xirr & XISR_MASK);
    /* Send EOI -> ICS */
    ss->xirr = (ss->xirr & ~CPPR_MASK) | (xirr & CPPR_MASK);
    if (!XISR(ss)) {
        icp_resend(icp, server);
    }
}

static void icp_irq(struct icp_state *icp, int server, int nr, uint8_t priority)
{
    struct icp_server_state *ss = icp->ss + server;

    if ((priority >= CPPR(ss))
        || (XISR(ss) && (ss->pending_priority <= priority))) {
        ics_reject(icp->ics, nr);
    } else {
        if (XISR(ss)) {
            ics_reject(icp->ics, XISR(ss));
        }
        ss->xirr = (ss->xirr & ~XISR_MASK) | (nr & XISR_MASK);
        ss->pending_priority = priority;
        qemu_irq_raise(ss->output);
    }
}

/*
 * ICS: Source layer
 */

struct ics_irq_state {
    int server;
    uint8_t priority;
    uint8_t saved_priority;
    /* int pending:1; */
    /* int presented:1; */
    int rejected:1;
    int masked_pending:1;
};

struct ics_state {
    int nr_irqs;
    int offset;
    qemu_irq *qirqs;
    struct ics_irq_state *irqs;
    struct icp_state *icp;
};

static int ics_valid_irq(struct ics_state *ics, uint32_t nr)
{
    return (nr >= ics->offset)
        && (nr < (ics->offset + ics->nr_irqs));
}

static void ics_set_irq_msi(void *opaque, int nr, int val)
{
    struct ics_state *ics = (struct ics_state *)opaque;
    struct ics_irq_state *irq = ics->irqs + nr;

    if (val) {
        if (irq->priority == 0xff) {
            irq->masked_pending = 1;
            /* masked pending */ ;
        } else  {
            icp_irq(ics->icp, irq->server, nr + ics->offset, irq->priority);
        }
    }
}

static void ics_reject_msi(struct ics_state *ics, int nr)
{
    struct ics_irq_state *irq = ics->irqs + nr - ics->offset;

    irq->rejected = 1;
}

static void ics_resend_msi(struct ics_state *ics)
{
    int i;

    for (i = 0; i < ics->nr_irqs; i++) {
        struct ics_irq_state *irq = ics->irqs + i;

        /* FIXME: filter by server#? */
        if (irq->rejected) {
            irq->rejected = 0;
            if (irq->priority != 0xff) {
                icp_irq(ics->icp, irq->server, i + ics->offset, irq->priority);
            }
        }
    }
}

static void ics_write_xive_msi(struct ics_state *ics, int nr, int server,
                               uint8_t priority)
{
    struct ics_irq_state *irq = ics->irqs + nr;

    irq->server = server;
    irq->priority = priority;

    if (!irq->masked_pending || (priority == 0xff)) {
        return;
    }

    irq->masked_pending = 0;
    icp_irq(ics->icp, server, nr + ics->offset, priority);
}

static void ics_reject(struct ics_state *ics, int nr)
{
    ics_reject_msi(ics, nr);
}

static void ics_resend(struct ics_state *ics)
{
    ics_resend_msi(ics);
}

static void ics_eoi(struct ics_state *ics, int nr)
{
}

/*
 * Exported functions
 */

qemu_irq xics_find_qirq(struct icp_state *icp, int irq)
{
    if ((irq < icp->ics->offset)
        || (irq >= (icp->ics->offset + icp->ics->nr_irqs))) {
        return NULL;
    }

    return icp->ics->qirqs[irq - icp->ics->offset];
}

static target_ulong h_cppr(CPUState *env, sPAPREnvironment *spapr,
                           target_ulong opcode, target_ulong *args)
{
    target_ulong cppr = args[0];

    icp_set_cppr(spapr->icp, env->cpu_index, cppr);
    return H_SUCCESS;
}

static target_ulong h_ipi(CPUState *env, sPAPREnvironment *spapr,
                          target_ulong opcode, target_ulong *args)
{
    target_ulong server = args[0];
    target_ulong mfrr = args[1];

    if (server >= spapr->icp->nr_servers) {
        return H_PARAMETER;
    }

    icp_set_mfrr(spapr->icp, server, mfrr);
    return H_SUCCESS;

}

static target_ulong h_xirr(CPUState *env, sPAPREnvironment *spapr,
                           target_ulong opcode, target_ulong *args)
{
    uint32_t xirr = icp_accept(spapr->icp->ss + env->cpu_index);

    args[0] = xirr;
    return H_SUCCESS;
}

static target_ulong h_eoi(CPUState *env, sPAPREnvironment *spapr,
                          target_ulong opcode, target_ulong *args)
{
    target_ulong xirr = args[0];

    icp_eoi(spapr->icp, env->cpu_index, xirr);
    return H_SUCCESS;
}

static void rtas_set_xive(sPAPREnvironment *spapr, uint32_t token,
                          uint32_t nargs, target_ulong args,
                          uint32_t nret, target_ulong rets)
{
    struct ics_state *ics = spapr->icp->ics;
    uint32_t nr, server, priority;

    if ((nargs != 3) || (nret != 1)) {
        rtas_st(rets, 0, -3);
        return;
    }

    nr = rtas_ld(args, 0);
    server = rtas_ld(args, 1);
    priority = rtas_ld(args, 2);

    if (!ics_valid_irq(ics, nr) || (server >= ics->icp->nr_servers)
        || (priority > 0xff)) {
        rtas_st(rets, 0, -3);
        return;
    }

    ics_write_xive_msi(ics, nr - ics->offset, server, priority);

    rtas_st(rets, 0, 0); /* Success */
}

static void rtas_get_xive(sPAPREnvironment *spapr, uint32_t token,
                          uint32_t nargs, target_ulong args,
                          uint32_t nret, target_ulong rets)
{
    struct ics_state *ics = spapr->icp->ics;
    uint32_t nr;

    if ((nargs != 1) || (nret != 3)) {
        rtas_st(rets, 0, -3);
        return;
    }

    nr = rtas_ld(args, 0);

    if (!ics_valid_irq(ics, nr)) {
        rtas_st(rets, 0, -3);
        return;
    }

    rtas_st(rets, 0, 0); /* Success */
    rtas_st(rets, 1, ics->irqs[nr - ics->offset].server);
    rtas_st(rets, 2, ics->irqs[nr - ics->offset].priority);
}

static void rtas_int_off(sPAPREnvironment *spapr, uint32_t token,
                         uint32_t nargs, target_ulong args,
                         uint32_t nret, target_ulong rets)
{
    struct ics_state *ics = spapr->icp->ics;
    uint32_t nr;

    if ((nargs != 1) || (nret != 1)) {
        rtas_st(rets, 0, -3);
        return;
    }

    nr = rtas_ld(args, 0);

    if (!ics_valid_irq(ics, nr)) {
        rtas_st(rets, 0, -3);
        return;
    }

    /* This is a NOP for now, since the described PAPR semantics don't
     * seem to gel with what Linux does */
#if 0
    struct ics_irq_state *irq = xics->irqs + (nr - xics->offset);

    irq->saved_priority = irq->priority;
    ics_write_xive_msi(xics, nr - xics->offset, irq->server, 0xff);
#endif

    rtas_st(rets, 0, 0); /* Success */
}

static void rtas_int_on(sPAPREnvironment *spapr, uint32_t token,
                        uint32_t nargs, target_ulong args,
                        uint32_t nret, target_ulong rets)
{
    struct ics_state *ics = spapr->icp->ics;
    uint32_t nr;

    if ((nargs != 1) || (nret != 1)) {
        rtas_st(rets, 0, -3);
        return;
    }

    nr = rtas_ld(args, 0);

    if (!ics_valid_irq(ics, nr)) {
        rtas_st(rets, 0, -3);
        return;
    }

    /* This is a NOP for now, since the described PAPR semantics don't
     * seem to gel with what Linux does */
#if 0
    struct ics_irq_state *irq = xics->irqs + (nr - xics->offset);

    ics_write_xive_msi(xics, nr - xics->offset,
                       irq->server, irq->saved_priority);
#endif

    rtas_st(rets, 0, 0); /* Success */
}

struct icp_state *xics_system_init(int nr_irqs)
{
    CPUState *env;
    int max_server_num;
    int i;
    struct icp_state *icp;
    struct ics_state *ics;

    max_server_num = -1;
    for (env = first_cpu; env != NULL; env = env->next_cpu) {
        if (env->cpu_index > max_server_num) {
            max_server_num = env->cpu_index;
        }
    }

    icp = g_malloc0(sizeof(*icp));
    icp->nr_servers = max_server_num + 1;
    icp->ss = g_malloc0(icp->nr_servers*sizeof(struct icp_server_state));

    for (i = 0; i < icp->nr_servers; i++) {
        icp->ss[i].mfrr = 0xff;
    }

    for (env = first_cpu; env != NULL; env = env->next_cpu) {
        struct icp_server_state *ss = &icp->ss[env->cpu_index];

        switch (PPC_INPUT(env)) {
        case PPC_FLAGS_INPUT_POWER7:
            ss->output = env->irq_inputs[POWER7_INPUT_INT];
            break;

        case PPC_FLAGS_INPUT_970:
            ss->output = env->irq_inputs[PPC970_INPUT_INT];
            break;

        default:
            hw_error("XICS interrupt model does not support this CPU bus "
                     "model\n");
            exit(1);
        }
    }

    ics = g_malloc0(sizeof(*ics));
    ics->nr_irqs = nr_irqs;
    ics->offset = 16;
    ics->irqs = g_malloc0(nr_irqs * sizeof(struct ics_irq_state));

    icp->ics = ics;
    ics->icp = icp;

    for (i = 0; i < nr_irqs; i++) {
        ics->irqs[i].priority = 0xff;
        ics->irqs[i].saved_priority = 0xff;
    }

    ics->qirqs = qemu_allocate_irqs(ics_set_irq_msi, ics, nr_irqs);

    spapr_register_hypercall(H_CPPR, h_cppr);
    spapr_register_hypercall(H_IPI, h_ipi);
    spapr_register_hypercall(H_XIRR, h_xirr);
    spapr_register_hypercall(H_EOI, h_eoi);

    spapr_rtas_register("ibm,set-xive", rtas_set_xive);
    spapr_rtas_register("ibm,get-xive", rtas_get_xive);
    spapr_rtas_register("ibm,int-off", rtas_int_off);
    spapr_rtas_register("ibm,int-on", rtas_int_on);

    return icp;
}
