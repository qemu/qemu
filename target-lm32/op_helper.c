#include <assert.h>
#include "cpu.h"
#include "dyngen-exec.h"
#include "helper.h"
#include "host-utils.h"

#include "hw/lm32_pic.h"
#include "hw/lm32_juart.h"

#if !defined(CONFIG_USER_ONLY)
#define MMUSUFFIX _mmu
#define SHIFT 0
#include "softmmu_template.h"
#define SHIFT 1
#include "softmmu_template.h"
#define SHIFT 2
#include "softmmu_template.h"
#define SHIFT 3
#include "softmmu_template.h"

void helper_raise_exception(uint32_t index)
{
    env->exception_index = index;
    cpu_loop_exit(env);
}

void helper_hlt(void)
{
    env->halted = 1;
    env->exception_index = EXCP_HLT;
    cpu_loop_exit(env);
}

void helper_wcsr_im(uint32_t im)
{
    lm32_pic_set_im(env->pic_state, im);
}

void helper_wcsr_ip(uint32_t im)
{
    lm32_pic_set_ip(env->pic_state, im);
}

void helper_wcsr_jtx(uint32_t jtx)
{
    lm32_juart_set_jtx(env->juart_state, jtx);
}

void helper_wcsr_jrx(uint32_t jrx)
{
    lm32_juart_set_jrx(env->juart_state, jrx);
}

uint32_t helper_rcsr_im(void)
{
    return lm32_pic_get_im(env->pic_state);
}

uint32_t helper_rcsr_ip(void)
{
    return lm32_pic_get_ip(env->pic_state);
}

uint32_t helper_rcsr_jtx(void)
{
    return lm32_juart_get_jtx(env->juart_state);
}

uint32_t helper_rcsr_jrx(void)
{
    return lm32_juart_get_jrx(env->juart_state);
}

/* Try to fill the TLB and return an exception if error. If retaddr is
   NULL, it means that the function was called in C code (i.e. not
   from generated code or from helper.c) */
/* XXX: fix it to restore all registers */
void tlb_fill(target_ulong addr, int is_write, int mmu_idx, void *retaddr)
{
    TranslationBlock *tb;
    CPUState *saved_env;
    unsigned long pc;
    int ret;

    /* XXX: hack to restore env in all cases, even if not called from
       generated code */
    saved_env = env;
    env = cpu_single_env;

    ret = cpu_lm32_handle_mmu_fault(env, addr, is_write, mmu_idx);
    if (unlikely(ret)) {
        if (retaddr) {
            /* now we have a real cpu fault */
            pc = (unsigned long)retaddr;
            tb = tb_find_pc(pc);
            if (tb) {
                /* the PC is inside the translated code. It means that we have
                   a virtual CPU fault */
                cpu_restore_state(tb, env, pc);
            }
        }
        cpu_loop_exit(env);
    }
    env = saved_env;
}
#endif

