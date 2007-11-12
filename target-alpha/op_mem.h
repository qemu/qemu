/*
 *  Alpha emulation cpu micro-operations for memory accesses for qemu.
 *
 *  Copyright (c) 2007 Jocelyn Mayer
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

//#define DEBUG_MEM_ACCESSES
#if defined (DEBUG_MEM_ACCESSES)
void helper_print_mem_EA (target_ulong EA);
#define print_mem_EA(EA) do { helper_print_mem_EA(EA); } while (0)
#else
#define print_mem_EA(EA) do { } while (0)
#endif

static always_inline uint32_t glue(ldl_l, MEMSUFFIX) (target_ulong EA)
{
    env->lock = EA;

    return glue(ldl, MEMSUFFIX)(EA);
}

static always_inline uint32_t glue(ldq_l, MEMSUFFIX) (target_ulong EA)
{
    env->lock = EA;

    return glue(ldq, MEMSUFFIX)(EA);
}

static always_inline void glue(stl_c, MEMSUFFIX) (target_ulong EA,
                                                  uint32_t data)
{
    if (EA == env->lock) {
        glue(stl, MEMSUFFIX)(EA, data);
        T0 = 0;
    } else {
        T0 = 1;
    }
    env->lock = -1;
}

static always_inline void glue(stq_c, MEMSUFFIX) (target_ulong EA,
                                                  uint64_t data)
{
    if (EA == env->lock) {
        glue(stq, MEMSUFFIX)(EA, data);
        T0 = 0;
    } else {
        T0 = 1;
    }
    env->lock = -1;
}

#define ALPHA_LD_OP(name, op)                                                 \
void OPPROTO glue(glue(op_ld, name), MEMSUFFIX) (void)                        \
{                                                                             \
    print_mem_EA(T0);                                                         \
    T1 = glue(op, MEMSUFFIX)(T0);                                             \
    RETURN();                                                                 \
}

#define ALPHA_ST_OP(name, op)                                                 \
void OPPROTO glue(glue(op_st, name), MEMSUFFIX) (void)                        \
{                                                                             \
    print_mem_EA(T0);                                                         \
    glue(op, MEMSUFFIX)(T0, T1);                                              \
    RETURN();                                                                 \
}

ALPHA_LD_OP(bu, ldub);
ALPHA_ST_OP(b, stb);
ALPHA_LD_OP(wu, lduw);
ALPHA_ST_OP(w, stw);
ALPHA_LD_OP(l, ldl);
ALPHA_ST_OP(l, stl);
ALPHA_LD_OP(q, ldq);
ALPHA_ST_OP(q, stq);

ALPHA_LD_OP(q_u, ldq);
ALPHA_ST_OP(q_u, stq);

ALPHA_LD_OP(l_l, ldl_l);
ALPHA_LD_OP(q_l, ldq_l);
ALPHA_ST_OP(l_c, stl_c);
ALPHA_ST_OP(q_c, stq_c);

#define ALPHA_LDF_OP(name, op)                                                \
void OPPROTO glue(glue(op_ld, name), MEMSUFFIX) (void)                        \
{                                                                             \
    print_mem_EA(T0);                                                         \
    FT1 = glue(op, MEMSUFFIX)(T0);                                            \
    RETURN();                                                                 \
}

#define ALPHA_STF_OP(name, op)                                                \
void OPPROTO glue(glue(op_st, name), MEMSUFFIX) (void)                        \
{                                                                             \
    print_mem_EA(T0);                                                         \
    glue(op, MEMSUFFIX)(T0, FT1);                                             \
    RETURN();                                                                 \
}

ALPHA_LDF_OP(t, ldfq);
ALPHA_STF_OP(t, stfq);
ALPHA_LDF_OP(s, ldfl);
ALPHA_STF_OP(s, stfl);

/* VAX floating point */
ALPHA_LDF_OP(f, helper_ldff);
ALPHA_STF_OP(f, helper_stff);
ALPHA_LDF_OP(g, helper_ldfg);
ALPHA_STF_OP(g, helper_stfg);

#undef MEMSUFFIX
