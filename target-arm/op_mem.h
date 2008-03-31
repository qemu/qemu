/* ARM memory operations.  */

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
