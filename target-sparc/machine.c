#include "hw/hw.h"
#include "hw/boards.h"

#include "exec-all.h"

void register_machines(void)
{
#ifdef TARGET_SPARC64
    qemu_register_machine(&sun4u_machine);
    qemu_register_machine(&sun4v_machine);
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
#ifndef TARGET_SPARC64
    qemu_put_be32s(f, &env->wim);
    /* MMU */
    for(i = 0; i < 16; i++)
        qemu_put_be32s(f, &env->mmuregs[i]);
#endif
}

int cpu_load(QEMUFile *f, void *opaque, int version_id)
{
    CPUState *env = opaque;
    int i;
    uint32_t tmp;

    if (version_id != 4)
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
#ifndef TARGET_SPARC64
    qemu_get_be32s(f, &env->wim);
    /* MMU */
    for(i = 0; i < 16; i++)
        qemu_get_be32s(f, &env->mmuregs[i]);
#endif
    tlb_flush(env, 1);
    return 0;
}


