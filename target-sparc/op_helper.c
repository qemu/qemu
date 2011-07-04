#include "cpu.h"
#include "dyngen-exec.h"
#include "helper.h"

#if !defined(CONFIG_USER_ONLY)
#include "softmmu_exec.h"
static void do_unaligned_access(target_ulong addr, int is_write, int is_user,
                                void *retaddr);

#define MMUSUFFIX _mmu
#define ALIGNED_ONLY

#define SHIFT 0
#include "softmmu_template.h"

#define SHIFT 1
#include "softmmu_template.h"

#define SHIFT 2
#include "softmmu_template.h"

#define SHIFT 3
#include "softmmu_template.h"

/* XXX: make it generic ? */
static void cpu_restore_state2(void *retaddr)
{
    TranslationBlock *tb;
    unsigned long pc;

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
}

static void do_unaligned_access(target_ulong addr, int is_write, int is_user,
                                void *retaddr)
{
#ifdef DEBUG_UNALIGNED
    printf("Unaligned access to 0x" TARGET_FMT_lx " from 0x" TARGET_FMT_lx
           "\n", addr, env->pc);
#endif
    cpu_restore_state2(retaddr);
    helper_raise_exception(env, TT_UNALIGNED);
}

/* try to fill the TLB and return an exception if error. If retaddr is
   NULL, it means that the function was called in C code (i.e. not
   from generated code or from helper.c) */
/* XXX: fix it to restore all registers */
void tlb_fill(CPUSPARCState *env1, target_ulong addr, int is_write, int mmu_idx,
              void *retaddr)
{
    int ret;
    CPUSPARCState *saved_env;

    saved_env = env;
    env = env1;

    ret = cpu_sparc_handle_mmu_fault(env, addr, is_write, mmu_idx);
    if (ret) {
        cpu_restore_state2(retaddr);
        cpu_loop_exit(env);
    }
    env = saved_env;
}

#define WRAP_LD(rettype, fn)                                    \
    rettype cpu_ ## fn (CPUSPARCState *env1, target_ulong addr) \
    {                                                           \
        CPUSPARCState *saved_env;                               \
        rettype ret;                                            \
                                                                \
        saved_env = env;                                        \
        env = env1;                                             \
        ret = fn(addr);                                         \
        env = saved_env;                                        \
        return ret;                                             \
    }

WRAP_LD(uint32_t, ldub_kernel)
WRAP_LD(uint32_t, lduw_kernel)
WRAP_LD(uint32_t, ldl_kernel)
WRAP_LD(uint64_t, ldq_kernel)

WRAP_LD(uint32_t, ldub_user)
WRAP_LD(uint32_t, lduw_user)
WRAP_LD(uint32_t, ldl_user)
WRAP_LD(uint64_t, ldq_user)

WRAP_LD(uint64_t, ldfq_kernel)
WRAP_LD(uint64_t, ldfq_user)
#ifdef TARGET_SPARC64
WRAP_LD(uint32_t, ldub_hypv)
WRAP_LD(uint32_t, lduw_hypv)
WRAP_LD(uint32_t, ldl_hypv)
WRAP_LD(uint64_t, ldq_hypv)

WRAP_LD(uint64_t, ldfq_hypv)

WRAP_LD(uint32_t, ldub_nucleus)
WRAP_LD(uint32_t, lduw_nucleus)
WRAP_LD(uint32_t, ldl_nucleus)
WRAP_LD(uint64_t, ldq_nucleus)

WRAP_LD(uint32_t, ldub_kernel_secondary)
WRAP_LD(uint32_t, lduw_kernel_secondary)
WRAP_LD(uint32_t, ldl_kernel_secondary)
WRAP_LD(uint64_t, ldq_kernel_secondary)

WRAP_LD(uint32_t, ldub_user_secondary)
WRAP_LD(uint32_t, lduw_user_secondary)
WRAP_LD(uint32_t, ldl_user_secondary)
WRAP_LD(uint64_t, ldq_user_secondary)
#endif
#undef WRAP_LD

#define WRAP_ST(datatype, fn)                                           \
    void cpu_ ## fn (CPUSPARCState *env1, target_ulong addr, datatype val)   \
    {                                                                   \
        CPUSPARCState *saved_env;                                       \
                                                                        \
        saved_env = env;                                                \
        env = env1;                                                     \
        fn(addr, val);                                                  \
        env = saved_env;                                                \
    }

WRAP_ST(uint32_t, stb_kernel)
WRAP_ST(uint32_t, stw_kernel)
WRAP_ST(uint32_t, stl_kernel)
WRAP_ST(uint64_t, stq_kernel)

WRAP_ST(uint32_t, stb_user)
WRAP_ST(uint32_t, stw_user)
WRAP_ST(uint32_t, stl_user)
WRAP_ST(uint64_t, stq_user)

WRAP_ST(uint64_t, stfq_kernel)
WRAP_ST(uint64_t, stfq_user)

#ifdef TARGET_SPARC64
WRAP_ST(uint32_t, stb_hypv)
WRAP_ST(uint32_t, stw_hypv)
WRAP_ST(uint32_t, stl_hypv)
WRAP_ST(uint64_t, stq_hypv)

WRAP_ST(uint64_t, stfq_hypv)

WRAP_ST(uint32_t, stb_nucleus)
WRAP_ST(uint32_t, stw_nucleus)
WRAP_ST(uint32_t, stl_nucleus)
WRAP_ST(uint64_t, stq_nucleus)

WRAP_ST(uint32_t, stb_kernel_secondary)
WRAP_ST(uint32_t, stw_kernel_secondary)
WRAP_ST(uint32_t, stl_kernel_secondary)
WRAP_ST(uint64_t, stq_kernel_secondary)

WRAP_ST(uint32_t, stb_user_secondary)
WRAP_ST(uint32_t, stw_user_secondary)
WRAP_ST(uint32_t, stl_user_secondary)
WRAP_ST(uint64_t, stq_user_secondary)
#endif

#undef WRAP_ST
#endif
