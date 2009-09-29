#include "hw/hw.h"
#include "hw/boards.h"
#include "hw/pc.h"
#include "hw/isa.h"
#include "host-utils.h"

#include "exec-all.h"
#include "kvm.h"

static const VMStateDescription vmstate_segment = {
    .name = "segment",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField []) {
        VMSTATE_UINT32(selector, SegmentCache),
        VMSTATE_UINTTL(base, SegmentCache),
        VMSTATE_UINT32(limit, SegmentCache),
        VMSTATE_UINT32(flags, SegmentCache),
        VMSTATE_END_OF_LIST()
    }
};

static void cpu_put_seg(QEMUFile *f, SegmentCache *dt)
{
    vmstate_save_state(f, &vmstate_segment, dt);
}

static void cpu_get_seg(QEMUFile *f, SegmentCache *dt)
{
    vmstate_load_state(f, &vmstate_segment, dt, vmstate_segment.version_id);
}

static const VMStateDescription vmstate_xmm_reg = {
    .name = "xmm_reg",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField []) {
        VMSTATE_UINT64(XMM_Q(0), XMMReg),
        VMSTATE_UINT64(XMM_Q(1), XMMReg),
        VMSTATE_END_OF_LIST()
    }
};

static void cpu_put_xmm_reg(QEMUFile *f, XMMReg *xmm_reg)
{
    vmstate_save_state(f, &vmstate_xmm_reg, xmm_reg);
}

static void cpu_get_xmm_reg(QEMUFile *f, XMMReg *xmm_reg)
{
    vmstate_load_state(f, &vmstate_xmm_reg, xmm_reg, vmstate_xmm_reg.version_id);
}

static const VMStateDescription vmstate_mtrr_var = {
    .name = "mtrr_var",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField []) {
        VMSTATE_UINT64(base, MTRRVar),
        VMSTATE_UINT64(mask, MTRRVar),
        VMSTATE_END_OF_LIST()
    }
};

static void cpu_put_mtrr_var(QEMUFile *f, MTRRVar *mtrr_var)
{
    vmstate_save_state(f, &vmstate_mtrr_var, mtrr_var);
}

static void cpu_get_mtrr_var(QEMUFile *f, MTRRVar *mtrr_var)
{
    vmstate_load_state(f, &vmstate_mtrr_var, mtrr_var, vmstate_mtrr_var.version_id);
}

static void cpu_pre_save(void *opaque)
{
    CPUState *env = opaque;
    int i, bit;

    cpu_synchronize_state(env);

    /* FPU */
    env->fpus_vmstate = (env->fpus & ~0x3800) | (env->fpstt & 0x7) << 11;
    env->fptag_vmstate = 0;
    for(i = 0; i < 8; i++) {
        env->fptag_vmstate |= ((!env->fptags[i]) << i);
    }

#ifdef USE_X86LDOUBLE
    env->fpregs_format_vmstate = 0;
#else
    env->fpregs_format_vmstate = 1;
#endif

    /* There can only be one pending IRQ set in the bitmap at a time, so try
       to find it and save its number instead (-1 for none). */
    env->pending_irq_vmstate = -1;
    for (i = 0; i < ARRAY_SIZE(env->interrupt_bitmap); i++) {
        if (env->interrupt_bitmap[i]) {
            bit = ctz64(env->interrupt_bitmap[i]);
            env->pending_irq_vmstate = i * 64 + bit;
            break;
        }
    }
}

void cpu_save(QEMUFile *f, void *opaque)
{
    CPUState *env = opaque;
    int i;

    cpu_pre_save(opaque);

    for(i = 0; i < CPU_NB_REGS; i++)
        qemu_put_betls(f, &env->regs[i]);
    qemu_put_betls(f, &env->eip);
    qemu_put_betls(f, &env->eflags);
    qemu_put_be32s(f, &env->hflags);

    /* FPU */
    qemu_put_be16s(f, &env->fpuc);
    qemu_put_be16s(f, &env->fpus_vmstate);
    qemu_put_be16s(f, &env->fptag_vmstate);

    qemu_put_be16s(f, &env->fpregs_format_vmstate);

    for(i = 0; i < 8; i++) {
#ifdef USE_X86LDOUBLE
        {
            uint64_t mant;
            uint16_t exp;
            /* we save the real CPU data (in case of MMX usage only 'mant'
               contains the MMX register */
            cpu_get_fp80(&mant, &exp, env->fpregs[i].d);
            qemu_put_be64(f, mant);
            qemu_put_be16(f, exp);
        }
#else
        /* if we use doubles for float emulation, we save the doubles to
           avoid losing information in case of MMX usage. It can give
           problems if the image is restored on a CPU where long
           doubles are used instead. */
        qemu_put_be64(f, env->fpregs[i].mmx.MMX_Q(0));
#endif
    }

    for(i = 0; i < 6; i++)
        cpu_put_seg(f, &env->segs[i]);
    cpu_put_seg(f, &env->ldt);
    cpu_put_seg(f, &env->tr);
    cpu_put_seg(f, &env->gdt);
    cpu_put_seg(f, &env->idt);

    qemu_put_be32s(f, &env->sysenter_cs);
    qemu_put_betls(f, &env->sysenter_esp);
    qemu_put_betls(f, &env->sysenter_eip);

    qemu_put_betls(f, &env->cr[0]);
    qemu_put_betls(f, &env->cr[2]);
    qemu_put_betls(f, &env->cr[3]);
    qemu_put_betls(f, &env->cr[4]);

    for(i = 0; i < 8; i++)
        qemu_put_betls(f, &env->dr[i]);

    /* MMU */
    qemu_put_sbe32s(f, &env->a20_mask);

    /* XMM */
    qemu_put_be32s(f, &env->mxcsr);
    for(i = 0; i < CPU_NB_REGS; i++) {
        cpu_put_xmm_reg(f, &env->xmm_regs[i]);
    }

#ifdef TARGET_X86_64
    qemu_put_be64s(f, &env->efer);
    qemu_put_be64s(f, &env->star);
    qemu_put_be64s(f, &env->lstar);
    qemu_put_be64s(f, &env->cstar);
    qemu_put_be64s(f, &env->fmask);
    qemu_put_be64s(f, &env->kernelgsbase);
#endif
    qemu_put_be32s(f, &env->smbase);

    qemu_put_be64s(f, &env->pat);
    qemu_put_be32s(f, &env->hflags2);
    
    qemu_put_be64s(f, &env->vm_hsave);
    qemu_put_be64s(f, &env->vm_vmcb);
    qemu_put_be64s(f, &env->tsc_offset);
    qemu_put_be64s(f, &env->intercept);
    qemu_put_be16s(f, &env->intercept_cr_read);
    qemu_put_be16s(f, &env->intercept_cr_write);
    qemu_put_be16s(f, &env->intercept_dr_read);
    qemu_put_be16s(f, &env->intercept_dr_write);
    qemu_put_be32s(f, &env->intercept_exceptions);
    qemu_put_8s(f, &env->v_tpr);

    /* MTRRs */
    for(i = 0; i < 11; i++)
        qemu_put_be64s(f, &env->mtrr_fixed[i]);
    qemu_put_be64s(f, &env->mtrr_deftype);
    for(i = 0; i < 8; i++) {
        cpu_put_mtrr_var(f, &env->mtrr_var[i]);
    }

    /* KVM-related states */

    qemu_put_sbe32s(f, &env->pending_irq_vmstate);
    qemu_put_be32s(f, &env->mp_state);
    qemu_put_be64s(f, &env->tsc);

    /* MCE */
    qemu_put_be64s(f, &env->mcg_cap);
    qemu_put_be64s(f, &env->mcg_status);
    qemu_put_be64s(f, &env->mcg_ctl);
    for (i = 0; i < MCE_BANKS_DEF * 4; i++) {
        qemu_put_be64s(f, &env->mce_banks[i]);
    }
    qemu_put_be64s(f, &env->tsc_aux);
 }

#ifdef USE_X86LDOUBLE
/* XXX: add that in a FPU generic layer */
union x86_longdouble {
    uint64_t mant;
    uint16_t exp;
};

#define MANTD1(fp)	(fp & ((1LL << 52) - 1))
#define EXPBIAS1 1023
#define EXPD1(fp)	((fp >> 52) & 0x7FF)
#define SIGND1(fp)	((fp >> 32) & 0x80000000)

static void fp64_to_fp80(union x86_longdouble *p, uint64_t temp)
{
    int e;
    /* mantissa */
    p->mant = (MANTD1(temp) << 11) | (1LL << 63);
    /* exponent + sign */
    e = EXPD1(temp) - EXPBIAS1 + 16383;
    e |= SIGND1(temp) >> 16;
    p->exp = e;
}
#endif

static int cpu_pre_load(void *opaque)
{
    CPUState *env = opaque;

    cpu_synchronize_state(env);
    return 0;
}

static int cpu_post_load(void *opaque, int version_id)
{
    CPUState *env = opaque;
    int i;

    /* XXX: restore FPU round state */
    env->fpstt = (env->fpus_vmstate >> 11) & 7;
    env->fpus = env->fpus_vmstate & ~0x3800;
    env->fptag_vmstate ^= 0xff;
    for(i = 0; i < 8; i++) {
        env->fptags[i] = (env->fptag_vmstate >> i) & 1;
    }

    cpu_breakpoint_remove_all(env, BP_CPU);
    cpu_watchpoint_remove_all(env, BP_CPU);
    for (i = 0; i < 4; i++)
        hw_breakpoint_insert(env, i);

    if (version_id >= 9) {
        memset(&env->interrupt_bitmap, 0, sizeof(env->interrupt_bitmap));
        if (env->pending_irq_vmstate >= 0) {
            env->interrupt_bitmap[env->pending_irq_vmstate / 64] |=
                (uint64_t)1 << (env->pending_irq_vmstate % 64);
        }
    }

    return cpu_post_load(env, version_id);
}

int cpu_load(QEMUFile *f, void *opaque, int version_id)
{
    CPUState *env = opaque;
    int i, guess_mmx;

    cpu_pre_load(env);

    if (version_id < 3 || version_id > CPU_SAVE_VERSION)
        return -EINVAL;
    for(i = 0; i < CPU_NB_REGS; i++)
        qemu_get_betls(f, &env->regs[i]);
    qemu_get_betls(f, &env->eip);
    qemu_get_betls(f, &env->eflags);
    qemu_get_be32s(f, &env->hflags);

    qemu_get_be16s(f, &env->fpuc);
    qemu_get_be16s(f, &env->fpus_vmstate);
    qemu_get_be16s(f, &env->fptag_vmstate);
    qemu_get_be16s(f, &env->fpregs_format_vmstate);

    /* NOTE: we cannot always restore the FPU state if the image come
       from a host with a different 'USE_X86LDOUBLE' define. We guess
       if we are in an MMX state to restore correctly in that case. */
    guess_mmx = ((env->fptag_vmstate == 0xff) && (env->fpus_vmstate & 0x3800) == 0);
    for(i = 0; i < 8; i++) {
        uint64_t mant;
        uint16_t exp;

        switch(env->fpregs_format_vmstate) {
        case 0:
            mant = qemu_get_be64(f);
            exp = qemu_get_be16(f);
#ifdef USE_X86LDOUBLE
            env->fpregs[i].d = cpu_set_fp80(mant, exp);
#else
            /* difficult case */
            if (guess_mmx)
                env->fpregs[i].mmx.MMX_Q(0) = mant;
            else
                env->fpregs[i].d = cpu_set_fp80(mant, exp);
#endif
            break;
        case 1:
            mant = qemu_get_be64(f);
#ifdef USE_X86LDOUBLE
            {
                union x86_longdouble *p;
                /* difficult case */
                p = (void *)&env->fpregs[i];
                if (guess_mmx) {
                    p->mant = mant;
                    p->exp = 0xffff;
                } else {
                    fp64_to_fp80(p, mant);
                }
            }
#else
            env->fpregs[i].mmx.MMX_Q(0) = mant;
#endif
            break;
        default:
            return -EINVAL;
        }
    }

    for(i = 0; i < 6; i++)
        cpu_get_seg(f, &env->segs[i]);
    cpu_get_seg(f, &env->ldt);
    cpu_get_seg(f, &env->tr);
    cpu_get_seg(f, &env->gdt);
    cpu_get_seg(f, &env->idt);

    qemu_get_be32s(f, &env->sysenter_cs);
    if (version_id >= 7) {
        qemu_get_betls(f, &env->sysenter_esp);
        qemu_get_betls(f, &env->sysenter_eip);
    } else {
        env->sysenter_esp = qemu_get_be32(f);
        env->sysenter_eip = qemu_get_be32(f);
    }

    qemu_get_betls(f, &env->cr[0]);
    qemu_get_betls(f, &env->cr[2]);
    qemu_get_betls(f, &env->cr[3]);
    qemu_get_betls(f, &env->cr[4]);

    for(i = 0; i < 8; i++)
        qemu_get_betls(f, &env->dr[i]);

    qemu_get_sbe32s(f, &env->a20_mask);

    qemu_get_be32s(f, &env->mxcsr);
    for(i = 0; i < CPU_NB_REGS; i++) {
        cpu_get_xmm_reg(f, &env->xmm_regs[i]);
    }

#ifdef TARGET_X86_64
    qemu_get_be64s(f, &env->efer);
    qemu_get_be64s(f, &env->star);
    qemu_get_be64s(f, &env->lstar);
    qemu_get_be64s(f, &env->cstar);
    qemu_get_be64s(f, &env->fmask);
    qemu_get_be64s(f, &env->kernelgsbase);
#endif
    if (version_id >= 4) {
        qemu_get_be32s(f, &env->smbase);
    }
    if (version_id >= 5) {
        qemu_get_be64s(f, &env->pat);
        qemu_get_be32s(f, &env->hflags2);
        if (version_id < 6)
            qemu_get_be32s(f, &env->halted);

        qemu_get_be64s(f, &env->vm_hsave);
        qemu_get_be64s(f, &env->vm_vmcb);
        qemu_get_be64s(f, &env->tsc_offset);
        qemu_get_be64s(f, &env->intercept);
        qemu_get_be16s(f, &env->intercept_cr_read);
        qemu_get_be16s(f, &env->intercept_cr_write);
        qemu_get_be16s(f, &env->intercept_dr_read);
        qemu_get_be16s(f, &env->intercept_dr_write);
        qemu_get_be32s(f, &env->intercept_exceptions);
        qemu_get_8s(f, &env->v_tpr);
    }

    if (version_id >= 8) {
        /* MTRRs */
        for(i = 0; i < 11; i++)
            qemu_get_be64s(f, &env->mtrr_fixed[i]);
        qemu_get_be64s(f, &env->mtrr_deftype);
        for(i = 0; i < 8; i++) {
            cpu_get_mtrr_var(f, &env->mtrr_var[i]);
        }
    }

    if (version_id >= 9) {
        qemu_get_sbe32s(f, &env->pending_irq_vmstate);
        qemu_get_be32s(f, &env->mp_state);
        qemu_get_be64s(f, &env->tsc);
    }

    if (version_id >= 10) {
        qemu_get_be64s(f, &env->mcg_cap);
        qemu_get_be64s(f, &env->mcg_status);
        qemu_get_be64s(f, &env->mcg_ctl);
        for (i = 0; i < MCE_BANKS_DEF * 4; i++) {
            qemu_get_be64s(f, &env->mce_banks[i]);
        }
    }

    if (version_id >= 11) {
        qemu_get_be64s(f, &env->tsc_aux);
    }

    tlb_flush(env, 1);
    return 0;
}
