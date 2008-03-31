/* ARM memory operations.  */

/* Swap T0 with memory at address T1.  */
/* ??? Is this exception safe?  */
#define MEM_SWP_OP(name, lname) \
void OPPROTO glue(op_swp##name,MEMSUFFIX)(void) \
{ \
    uint32_t tmp; \
    cpu_lock(); \
    tmp = glue(ld##lname,MEMSUFFIX)(T1); \
    glue(st##name,MEMSUFFIX)(T1, T0); \
    T0 = tmp; \
    cpu_unlock(); \
    FORCE_RET(); \
}

MEM_SWP_OP(b, ub)
MEM_SWP_OP(l, l)

#undef MEM_SWP_OP

/* Load-locked, store exclusive.  */
#define EXCLUSIVE_OP(suffix, ldsuffix) \
void OPPROTO glue(op_ld##suffix##ex,MEMSUFFIX)(void) \
{ \
    cpu_lock(); \
    helper_mark_exclusive(env, T1); \
    T0 = glue(ld##ldsuffix,MEMSUFFIX)(T1); \
    cpu_unlock(); \
    FORCE_RET(); \
} \
 \
void OPPROTO glue(op_st##suffix##ex,MEMSUFFIX)(void) \
{ \
    int failed; \
    cpu_lock(); \
    failed = helper_test_exclusive(env, T1); \
    /* ??? Is it safe to hold the cpu lock over a store?  */ \
    if (!failed) { \
        glue(st##suffix,MEMSUFFIX)(T1, T0); \
    } \
    T0 = failed; \
    cpu_unlock(); \
    FORCE_RET(); \
}

EXCLUSIVE_OP(b, ub)
EXCLUSIVE_OP(w, uw)
EXCLUSIVE_OP(l, l)

#undef EXCLUSIVE_OP

/* Load exclusive T0:T1 from address T1.  */
void OPPROTO glue(op_ldqex,MEMSUFFIX)(void)
{
    cpu_lock();
    helper_mark_exclusive(env, T1);
    T0 = glue(ldl,MEMSUFFIX)(T1);
    T1 = glue(ldl,MEMSUFFIX)((T1 + 4));
    cpu_unlock();
    FORCE_RET();
}

/* Store exclusive T0:T2 to address T1.  */
void OPPROTO glue(op_stqex,MEMSUFFIX)(void)
{
    int failed;
    cpu_lock();
    failed = helper_test_exclusive(env, T1);
    /* ??? Is it safe to hold the cpu lock over a store?  */
    if (!failed) {
        glue(stl,MEMSUFFIX)(T1, T0);
        glue(stl,MEMSUFFIX)((T1 + 4), T2);
    }
    T0 = failed;
    cpu_unlock();
    FORCE_RET();
}

/* iwMMXt load/store.  Address is in T1 */
#define MMX_MEM_OP(name, ldname) \
void OPPROTO glue(op_iwmmxt_ld##name,MEMSUFFIX)(void) \
{ \
    M0 = glue(ld##ldname,MEMSUFFIX)(T1); \
    FORCE_RET(); \
} \
void OPPROTO glue(op_iwmmxt_st##name,MEMSUFFIX)(void) \
{ \
    glue(st##name,MEMSUFFIX)(T1, M0); \
    FORCE_RET(); \
}

MMX_MEM_OP(b, ub)
MMX_MEM_OP(w, uw)
MMX_MEM_OP(l, l)
MMX_MEM_OP(q, q)

#undef MMX_MEM_OP

#undef MEMSUFFIX
