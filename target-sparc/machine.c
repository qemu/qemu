#include "hw/hw.h"
#include "hw/boards.h"
#include "qemu-timer.h"

#include "exec-all.h"

void register_machines(void)
{
#ifdef TARGET_SPARC64
    qemu_register_machine(&sun4u_machine);
    qemu_register_machine(&sun4v_machine);
    qemu_register_machine(&niagara_machine);
#else
    qemu_register_machine(&ss5_machine);
    qemu_register_machine(&ss10_machine);
    qemu_register_machine(&ss600mp_machine);
    qemu_register_machine(&ss20_machine);
    qemu_register_machine(&ss2_machine);
    qemu_register_machine(&voyager_machine);
    qemu_register_machine(&ss_lx_machine);
    qemu_register_machine(&ss4_machine);
    qemu_register_machine(&scls_machine);
    qemu_register_machine(&sbook_machine);
    qemu_register_machine(&ss1000_machine);
    qemu_register_machine(&ss2000_machine);
#endif
}

void cpu_save(QEMUFile *f, void *opaque)
{
    CPUState *env = opaque;
    int i;
    uint32_t tmp;

    // if env->cwp == env->nwindows - 1, this will set the ins of the last
    // window as the outs of the first window
    cpu_set_cwp(env, env->cwp);

    for(i = 0; i < 8; i++)
        qemu_put_betls(f, &env->gregs[i]);
    qemu_put_be32s(f, &env->nwindows);
    for(i = 0; i < env->nwindows * 16; i++)
        qemu_put_betls(f, &env->regbase[i]);

    /* FPU */
    for(i = 0; i < TARGET_FPREGS; i++) {
        union {
            float32 f;
            uint32_t i;
        } u;
        u.f = env->fpr[i];
        qemu_put_be32(f, u.i);
    }

    qemu_put_betls(f, &env->pc);
    qemu_put_betls(f, &env->npc);
    qemu_put_betls(f, &env->y);
    tmp = GET_PSR(env);
    qemu_put_be32(f, tmp);
    qemu_put_betls(f, &env->fsr);
    qemu_put_betls(f, &env->tbr);
    tmp = env->interrupt_index;
    qemu_put_be32(f, tmp);
    qemu_put_be32s(f, &env->pil_in);
#ifndef TARGET_SPARC64
    qemu_put_be32s(f, &env->wim);
    /* MMU */
    for (i = 0; i < 32; i++)
        qemu_put_be32s(f, &env->mmuregs[i]);
#else
    qemu_put_be64s(f, &env->lsu);
    for (i = 0; i < 16; i++) {
        qemu_put_be64s(f, &env->immuregs[i]);
        qemu_put_be64s(f, &env->dmmuregs[i]);
    }
    for (i = 0; i < 64; i++) {
        qemu_put_be64s(f, &env->itlb_tag[i]);
        qemu_put_be64s(f, &env->itlb_tte[i]);
        qemu_put_be64s(f, &env->dtlb_tag[i]);
        qemu_put_be64s(f, &env->dtlb_tte[i]);
    }
    qemu_put_be32s(f, &env->mmu_version);
    for (i = 0; i < MAXTL_MAX; i++) {
        qemu_put_be64s(f, &env->ts[i].tpc);
        qemu_put_be64s(f, &env->ts[i].tnpc);
        qemu_put_be64s(f, &env->ts[i].tstate);
        qemu_put_be32s(f, &env->ts[i].tt);
    }
    qemu_put_be32s(f, &env->xcc);
    qemu_put_be32s(f, &env->asi);
    qemu_put_be32s(f, &env->pstate);
    qemu_put_be32s(f, &env->tl);
    qemu_put_be32s(f, &env->cansave);
    qemu_put_be32s(f, &env->canrestore);
    qemu_put_be32s(f, &env->otherwin);
    qemu_put_be32s(f, &env->wstate);
    qemu_put_be32s(f, &env->cleanwin);
    for (i = 0; i < 8; i++)
        qemu_put_be64s(f, &env->agregs[i]);
    for (i = 0; i < 8; i++)
        qemu_put_be64s(f, &env->bgregs[i]);
    for (i = 0; i < 8; i++)
        qemu_put_be64s(f, &env->igregs[i]);
    for (i = 0; i < 8; i++)
        qemu_put_be64s(f, &env->mgregs[i]);
    qemu_put_be64s(f, &env->fprs);
    qemu_put_be64s(f, &env->tick_cmpr);
    qemu_put_be64s(f, &env->stick_cmpr);
    qemu_put_ptimer(f, env->tick);
    qemu_put_ptimer(f, env->stick);
    qemu_put_be64s(f, &env->gsr);
    qemu_put_be32s(f, &env->gl);
    qemu_put_be64s(f, &env->hpstate);
    for (i = 0; i < MAXTL_MAX; i++)
        qemu_put_be64s(f, &env->htstate[i]);
    qemu_put_be64s(f, &env->hintp);
    qemu_put_be64s(f, &env->htba);
    qemu_put_be64s(f, &env->hver);
    qemu_put_be64s(f, &env->hstick_cmpr);
    qemu_put_be64s(f, &env->ssr);
    qemu_put_ptimer(f, env->hstick);
#endif
}

int cpu_load(QEMUFile *f, void *opaque, int version_id)
{
    CPUState *env = opaque;
    int i;
    uint32_t tmp;

    if (version_id != 5)
        return -EINVAL;
    for(i = 0; i < 8; i++)
        qemu_get_betls(f, &env->gregs[i]);
    qemu_get_be32s(f, &env->nwindows);
    for(i = 0; i < env->nwindows * 16; i++)
        qemu_get_betls(f, &env->regbase[i]);

    /* FPU */
    for(i = 0; i < TARGET_FPREGS; i++) {
        union {
            float32 f;
            uint32_t i;
        } u;
        u.i = qemu_get_be32(f);
        env->fpr[i] = u.f;
    }

    qemu_get_betls(f, &env->pc);
    qemu_get_betls(f, &env->npc);
    qemu_get_betls(f, &env->y);
    tmp = qemu_get_be32(f);
    env->cwp = 0; /* needed to ensure that the wrapping registers are
                     correctly updated */
    PUT_PSR(env, tmp);
    qemu_get_betls(f, &env->fsr);
    qemu_get_betls(f, &env->tbr);
    tmp = qemu_get_be32(f);
    env->interrupt_index = tmp;
    qemu_get_be32s(f, &env->pil_in);
#ifndef TARGET_SPARC64
    qemu_get_be32s(f, &env->wim);
    /* MMU */
    for (i = 0; i < 32; i++)
        qemu_get_be32s(f, &env->mmuregs[i]);
#else
    qemu_get_be64s(f, &env->lsu);
    for (i = 0; i < 16; i++) {
        qemu_get_be64s(f, &env->immuregs[i]);
        qemu_get_be64s(f, &env->dmmuregs[i]);
    }
    for (i = 0; i < 64; i++) {
        qemu_get_be64s(f, &env->itlb_tag[i]);
        qemu_get_be64s(f, &env->itlb_tte[i]);
        qemu_get_be64s(f, &env->dtlb_tag[i]);
        qemu_get_be64s(f, &env->dtlb_tte[i]);
    }
    qemu_get_be32s(f, &env->mmu_version);
    for (i = 0; i < MAXTL_MAX; i++) {
        qemu_get_be64s(f, &env->ts[i].tpc);
        qemu_get_be64s(f, &env->ts[i].tnpc);
        qemu_get_be64s(f, &env->ts[i].tstate);
        qemu_get_be32s(f, &env->ts[i].tt);
    }
    qemu_get_be32s(f, &env->xcc);
    qemu_get_be32s(f, &env->asi);
    qemu_get_be32s(f, &env->pstate);
    qemu_get_be32s(f, &env->tl);
    env->tsptr = &env->ts[env->tl & MAXTL_MASK];
    qemu_get_be32s(f, &env->cansave);
    qemu_get_be32s(f, &env->canrestore);
    qemu_get_be32s(f, &env->otherwin);
    qemu_get_be32s(f, &env->wstate);
    qemu_get_be32s(f, &env->cleanwin);
    for (i = 0; i < 8; i++)
        qemu_get_be64s(f, &env->agregs[i]);
    for (i = 0; i < 8; i++)
        qemu_get_be64s(f, &env->bgregs[i]);
    for (i = 0; i < 8; i++)
        qemu_get_be64s(f, &env->igregs[i]);
    for (i = 0; i < 8; i++)
        qemu_get_be64s(f, &env->mgregs[i]);
    qemu_get_be64s(f, &env->fprs);
    qemu_get_be64s(f, &env->tick_cmpr);
    qemu_get_be64s(f, &env->stick_cmpr);
    qemu_get_ptimer(f, env->tick);
    qemu_get_ptimer(f, env->stick);
    qemu_get_be64s(f, &env->gsr);
    qemu_get_be32s(f, &env->gl);
    qemu_get_be64s(f, &env->hpstate);
    for (i = 0; i < MAXTL_MAX; i++)
        qemu_get_be64s(f, &env->htstate[i]);
    qemu_get_be64s(f, &env->hintp);
    qemu_get_be64s(f, &env->htba);
    qemu_get_be64s(f, &env->hver);
    qemu_get_be64s(f, &env->hstick_cmpr);
    qemu_get_be64s(f, &env->ssr);
    qemu_get_ptimer(f, env->hstick);
#endif
    tlb_flush(env, 1);
    return 0;
}


