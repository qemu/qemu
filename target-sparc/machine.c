#include "hw/hw.h"
#include "hw/boards.h"
#include "qemu-timer.h"

#include "cpu.h"

void cpu_save(QEMUFile *f, void *opaque)
{
    CPUSPARCState *env = opaque;
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
    for (i = 0; i < TARGET_DPREGS; i++) {
        qemu_put_be32(f, env->fpr[i].l.upper);
        qemu_put_be32(f, env->fpr[i].l.lower);
    }

    qemu_put_betls(f, &env->pc);
    qemu_put_betls(f, &env->npc);
    qemu_put_betls(f, &env->y);
    tmp = cpu_get_psr(env);
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
    for (i = 0; i < 4; i++) {
        qemu_put_be64s(f, &env->mxccdata[i]);
    }
    for (i = 0; i < 8; i++) {
        qemu_put_be64s(f, &env->mxccregs[i]);
    }
    qemu_put_be32s(f, &env->mmubpctrv);
    qemu_put_be32s(f, &env->mmubpctrc);
    qemu_put_be32s(f, &env->mmubpctrs);
    qemu_put_be64s(f, &env->mmubpaction);
    for (i = 0; i < 4; i++) {
        qemu_put_be64s(f, &env->mmubpregs[i]);
    }
#else
    qemu_put_be64s(f, &env->lsu);
    for (i = 0; i < 16; i++) {
        qemu_put_be64s(f, &env->immuregs[i]);
        qemu_put_be64s(f, &env->dmmuregs[i]);
    }
    for (i = 0; i < 64; i++) {
        qemu_put_be64s(f, &env->itlb[i].tag);
        qemu_put_be64s(f, &env->itlb[i].tte);
        qemu_put_be64s(f, &env->dtlb[i].tag);
        qemu_put_be64s(f, &env->dtlb[i].tte);
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
    cpu_put_timer(f, env->tick);
    cpu_put_timer(f, env->stick);
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
    cpu_put_timer(f, env->hstick);
#endif
}

int cpu_load(QEMUFile *f, void *opaque, int version_id)
{
    CPUSPARCState *env = opaque;
    int i;
    uint32_t tmp;

    if (version_id < 6)
        return -EINVAL;
    for(i = 0; i < 8; i++)
        qemu_get_betls(f, &env->gregs[i]);
    qemu_get_be32s(f, &env->nwindows);
    for(i = 0; i < env->nwindows * 16; i++)
        qemu_get_betls(f, &env->regbase[i]);

    /* FPU */
    for (i = 0; i < TARGET_DPREGS; i++) {
        env->fpr[i].l.upper = qemu_get_be32(f);
        env->fpr[i].l.lower = qemu_get_be32(f);
    }

    qemu_get_betls(f, &env->pc);
    qemu_get_betls(f, &env->npc);
    qemu_get_betls(f, &env->y);
    tmp = qemu_get_be32(f);
    env->cwp = 0; /* needed to ensure that the wrapping registers are
                     correctly updated */
    cpu_put_psr(env, tmp);
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
    for (i = 0; i < 4; i++) {
        qemu_get_be64s(f, &env->mxccdata[i]);
    }
    for (i = 0; i < 8; i++) {
        qemu_get_be64s(f, &env->mxccregs[i]);
    }
    qemu_get_be32s(f, &env->mmubpctrv);
    qemu_get_be32s(f, &env->mmubpctrc);
    qemu_get_be32s(f, &env->mmubpctrs);
    qemu_get_be64s(f, &env->mmubpaction);
    for (i = 0; i < 4; i++) {
        qemu_get_be64s(f, &env->mmubpregs[i]);
    }
#else
    qemu_get_be64s(f, &env->lsu);
    for (i = 0; i < 16; i++) {
        qemu_get_be64s(f, &env->immuregs[i]);
        qemu_get_be64s(f, &env->dmmuregs[i]);
    }
    for (i = 0; i < 64; i++) {
        qemu_get_be64s(f, &env->itlb[i].tag);
        qemu_get_be64s(f, &env->itlb[i].tte);
        qemu_get_be64s(f, &env->dtlb[i].tag);
        qemu_get_be64s(f, &env->dtlb[i].tte);
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
    cpu_get_timer(f, env->tick);
    cpu_get_timer(f, env->stick);
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
    cpu_get_timer(f, env->hstick);
#endif
    tlb_flush(env, 1);
    return 0;
}
