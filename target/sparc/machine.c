#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "qemu/timer.h"

#include "migration/cpu.h"

#ifdef TARGET_SPARC64
static const VMStateDescription vmstate_cpu_timer = {
    .name = "cpu_timer",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(frequency, CPUTimer),
        VMSTATE_UINT32(disabled, CPUTimer),
        VMSTATE_UINT64(disabled_mask, CPUTimer),
        VMSTATE_UINT32(npt, CPUTimer),
        VMSTATE_UINT64(npt_mask, CPUTimer),
        VMSTATE_INT64(clock_offset, CPUTimer),
        VMSTATE_TIMER_PTR(qtimer, CPUTimer),
        VMSTATE_END_OF_LIST()
    }
};

#define VMSTATE_CPU_TIMER(_f, _s)                             \
    VMSTATE_STRUCT_POINTER(_f, _s, vmstate_cpu_timer, CPUTimer)

static const VMStateDescription vmstate_trap_state = {
    .name = "trap_state",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(tpc, trap_state),
        VMSTATE_UINT64(tnpc, trap_state),
        VMSTATE_UINT64(tstate, trap_state),
        VMSTATE_UINT32(tt, trap_state),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_tlb_entry = {
    .name = "tlb_entry",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(tag, SparcTLBEntry),
        VMSTATE_UINT64(tte, SparcTLBEntry),
        VMSTATE_END_OF_LIST()
    }
};
#endif

static int get_psr(QEMUFile *f, void *opaque, size_t size,
                   const VMStateField *field)
{
    SPARCCPU *cpu = opaque;
    CPUSPARCState *env = &cpu->env;
    uint32_t val = qemu_get_be32(f);

    /* needed to ensure that the wrapping registers are correctly updated */
    env->cwp = 0;
    cpu_put_psr_raw(env, val);

    return 0;
}

static int put_psr(QEMUFile *f, void *opaque, size_t size,
                   const VMStateField *field, JSONWriter *vmdesc)
{
    SPARCCPU *cpu = opaque;
    CPUSPARCState *env = &cpu->env;
    uint32_t val;

    val = cpu_get_psr(env);

    qemu_put_be32(f, val);
    return 0;
}

static const VMStateInfo vmstate_psr = {
    .name = "psr",
    .get = get_psr,
    .put = put_psr,
};

static int cpu_pre_save(void *opaque)
{
    SPARCCPU *cpu = opaque;
    CPUSPARCState *env = &cpu->env;

    /* if env->cwp == env->nwindows - 1, this will set the ins of the last
     * window as the outs of the first window
     */
    cpu_set_cwp(env, env->cwp);

    return 0;
}

/* 32-bit SPARC retains migration compatibility with older versions
 * of QEMU; 64-bit SPARC has had a migration break since then, so the
 * versions are different.
 */
#ifndef TARGET_SPARC64
#define SPARC_VMSTATE_VER 7
#else
#define SPARC_VMSTATE_VER 9
#endif

const VMStateDescription vmstate_sparc_cpu = {
    .name = "cpu",
    .version_id = SPARC_VMSTATE_VER,
    .minimum_version_id = SPARC_VMSTATE_VER,
    .minimum_version_id_old = SPARC_VMSTATE_VER,
    .pre_save = cpu_pre_save,
    .fields = (VMStateField[]) {
        VMSTATE_UINTTL_ARRAY(env.gregs, SPARCCPU, 8),
        VMSTATE_UINT32(env.nwindows, SPARCCPU),
        VMSTATE_VARRAY_MULTIPLY(env.regbase, SPARCCPU, env.nwindows, 16,
                                vmstate_info_uinttl, target_ulong),
        VMSTATE_CPUDOUBLE_ARRAY(env.fpr, SPARCCPU, TARGET_DPREGS),
        VMSTATE_UINTTL(env.pc, SPARCCPU),
        VMSTATE_UINTTL(env.npc, SPARCCPU),
        VMSTATE_UINTTL(env.y, SPARCCPU),
        {

            .name = "psr",
            .version_id = 0,
            .size = sizeof(uint32_t),
            .info = &vmstate_psr,
            .flags = VMS_SINGLE,
            .offset = 0,
        },
        VMSTATE_UINTTL(env.fsr, SPARCCPU),
        VMSTATE_UINTTL(env.tbr, SPARCCPU),
        VMSTATE_INT32(env.interrupt_index, SPARCCPU),
        VMSTATE_UINT32(env.pil_in, SPARCCPU),
#ifndef TARGET_SPARC64
        /* MMU */
        VMSTATE_UINT32(env.wim, SPARCCPU),
        VMSTATE_UINT32_ARRAY(env.mmuregs, SPARCCPU, 32),
        VMSTATE_UINT64_ARRAY(env.mxccdata, SPARCCPU, 4),
        VMSTATE_UINT64_ARRAY(env.mxccregs, SPARCCPU, 8),
        VMSTATE_UINT32(env.mmubpctrv, SPARCCPU),
        VMSTATE_UINT32(env.mmubpctrc, SPARCCPU),
        VMSTATE_UINT32(env.mmubpctrs, SPARCCPU),
        VMSTATE_UINT64(env.mmubpaction, SPARCCPU),
        VMSTATE_UINT64_ARRAY(env.mmubpregs, SPARCCPU, 4),
#else
        VMSTATE_UINT64(env.lsu, SPARCCPU),
        VMSTATE_UINT64_ARRAY(env.immu.mmuregs, SPARCCPU, 16),
        VMSTATE_UINT64_ARRAY(env.dmmu.mmuregs, SPARCCPU, 16),
        VMSTATE_STRUCT_ARRAY(env.itlb, SPARCCPU, 64, 0,
                             vmstate_tlb_entry, SparcTLBEntry),
        VMSTATE_STRUCT_ARRAY(env.dtlb, SPARCCPU, 64, 0,
                             vmstate_tlb_entry, SparcTLBEntry),
        VMSTATE_UINT32(env.mmu_version, SPARCCPU),
        VMSTATE_STRUCT_ARRAY(env.ts, SPARCCPU, MAXTL_MAX, 0,
                             vmstate_trap_state, trap_state),
        VMSTATE_UINT32(env.xcc, SPARCCPU),
        VMSTATE_UINT32(env.asi, SPARCCPU),
        VMSTATE_UINT32(env.pstate, SPARCCPU),
        VMSTATE_UINT32(env.tl, SPARCCPU),
        VMSTATE_UINT32(env.cansave, SPARCCPU),
        VMSTATE_UINT32(env.canrestore, SPARCCPU),
        VMSTATE_UINT32(env.otherwin, SPARCCPU),
        VMSTATE_UINT32(env.wstate, SPARCCPU),
        VMSTATE_UINT32(env.cleanwin, SPARCCPU),
        VMSTATE_UINT64_ARRAY(env.agregs, SPARCCPU, 8),
        VMSTATE_UINT64_ARRAY(env.bgregs, SPARCCPU, 8),
        VMSTATE_UINT64_ARRAY(env.igregs, SPARCCPU, 8),
        VMSTATE_UINT64_ARRAY(env.mgregs, SPARCCPU, 8),
        VMSTATE_UINT64(env.fprs, SPARCCPU),
        VMSTATE_UINT64(env.tick_cmpr, SPARCCPU),
        VMSTATE_UINT64(env.stick_cmpr, SPARCCPU),
        VMSTATE_CPU_TIMER(env.tick, SPARCCPU),
        VMSTATE_CPU_TIMER(env.stick, SPARCCPU),
        VMSTATE_UINT64(env.gsr, SPARCCPU),
        VMSTATE_UINT32(env.gl, SPARCCPU),
        VMSTATE_UINT64(env.hpstate, SPARCCPU),
        VMSTATE_UINT64_ARRAY(env.htstate, SPARCCPU, MAXTL_MAX),
        VMSTATE_UINT64(env.hintp, SPARCCPU),
        VMSTATE_UINT64(env.htba, SPARCCPU),
        VMSTATE_UINT64(env.hver, SPARCCPU),
        VMSTATE_UINT64(env.hstick_cmpr, SPARCCPU),
        VMSTATE_UINT64(env.ssr, SPARCCPU),
        VMSTATE_CPU_TIMER(env.hstick, SPARCCPU),
        /* On SPARC32 env.psrpil and env.cwp are migrated as part of the PSR */
        VMSTATE_UINT32(env.psrpil, SPARCCPU),
        VMSTATE_UINT32(env.cwp, SPARCCPU),
#endif
        VMSTATE_END_OF_LIST()
    },
};
