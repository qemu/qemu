/* Load/store ops.  */
#define MEM_LD_OP(name,suffix) \
OP(glue(glue(ld,name),MEMSUFFIX)) \
{ \
    uint32_t addr = get_op(PARAM2); \
    set_op(PARAM1, glue(glue(ld,suffix),MEMSUFFIX)(addr)); \
    FORCE_RET(); \
}

MEM_LD_OP(8u32,ub)
MEM_LD_OP(8s32,sb)
MEM_LD_OP(16u32,uw)
MEM_LD_OP(16s32,sw)
MEM_LD_OP(32,l)

#undef MEM_LD_OP

#define MEM_ST_OP(name,suffix) \
OP(glue(glue(st,name),MEMSUFFIX)) \
{ \
    uint32_t addr = get_op(PARAM1); \
    glue(glue(st,suffix),MEMSUFFIX)(addr, get_op(PARAM2)); \
    FORCE_RET(); \
}

MEM_ST_OP(8,b)
MEM_ST_OP(16,w)
MEM_ST_OP(32,l)

#undef MEM_ST_OP

OP(glue(ldf64,MEMSUFFIX))
{
    uint32_t addr = get_op(PARAM2);
    set_opf64(PARAM1, glue(ldfq,MEMSUFFIX)(addr));
    FORCE_RET();
}

OP(glue(stf64,MEMSUFFIX))
{
    uint32_t addr = get_op(PARAM1);
    glue(stfq,MEMSUFFIX)(addr, get_opf64(PARAM2));
    FORCE_RET();
}

#undef MEMSUFFIX
