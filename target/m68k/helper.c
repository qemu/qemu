/*
 *  m68k op helpers
 *
 *  Copyright (c) 2006-2007 CodeSourcery
 *  Written by Paul Brook
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/gdbstub.h"

#include "exec/helper-proto.h"

#define SIGNBIT (1u << 31)

/* Sort alphabetically, except for "any". */
static gint m68k_cpu_list_compare(gconstpointer a, gconstpointer b)
{
    ObjectClass *class_a = (ObjectClass *)a;
    ObjectClass *class_b = (ObjectClass *)b;
    const char *name_a, *name_b;

    name_a = object_class_get_name(class_a);
    name_b = object_class_get_name(class_b);
    if (strcmp(name_a, "any-" TYPE_M68K_CPU) == 0) {
        return 1;
    } else if (strcmp(name_b, "any-" TYPE_M68K_CPU) == 0) {
        return -1;
    } else {
        return strcasecmp(name_a, name_b);
    }
}

static void m68k_cpu_list_entry(gpointer data, gpointer user_data)
{
    ObjectClass *c = data;
    CPUListState *s = user_data;
    const char *typename;
    char *name;

    typename = object_class_get_name(c);
    name = g_strndup(typename, strlen(typename) - strlen("-" TYPE_M68K_CPU));
    (*s->cpu_fprintf)(s->file, "%s\n",
                      name);
    g_free(name);
}

void m68k_cpu_list(FILE *f, fprintf_function cpu_fprintf)
{
    CPUListState s = {
        .file = f,
        .cpu_fprintf = cpu_fprintf,
    };
    GSList *list;

    list = object_class_get_list(TYPE_M68K_CPU, false);
    list = g_slist_sort(list, m68k_cpu_list_compare);
    g_slist_foreach(list, m68k_cpu_list_entry, &s);
    g_slist_free(list);
}

static int cf_fpu_gdb_get_reg(CPUM68KState *env, uint8_t *mem_buf, int n)
{
    if (n < 8) {
        float_status s;
        stfq_p(mem_buf, floatx80_to_float64(env->fregs[n].d, &s));
        return 8;
    }
    switch (n) {
    case 8: /* fpcontrol */
        stl_be_p(mem_buf, env->fpcr);
        return 4;
    case 9: /* fpstatus */
        stl_be_p(mem_buf, env->fpsr);
        return 4;
    case 10: /* fpiar, not implemented */
        memset(mem_buf, 0, 4);
        return 4;
    }
    return 0;
}

static int cf_fpu_gdb_set_reg(CPUM68KState *env, uint8_t *mem_buf, int n)
{
    if (n < 8) {
        float_status s;
        env->fregs[n].d = float64_to_floatx80(ldfq_p(mem_buf), &s);
        return 8;
    }
    switch (n) {
    case 8: /* fpcontrol */
        cpu_m68k_set_fpcr(env, ldl_p(mem_buf));
        return 4;
    case 9: /* fpstatus */
        env->fpsr = ldl_p(mem_buf);
        return 4;
    case 10: /* fpiar, not implemented */
        return 4;
    }
    return 0;
}

static int m68k_fpu_gdb_get_reg(CPUM68KState *env, uint8_t *mem_buf, int n)
{
    if (n < 8) {
        stw_be_p(mem_buf, env->fregs[n].l.upper);
        memset(mem_buf + 2, 0, 2);
        stq_be_p(mem_buf + 4, env->fregs[n].l.lower);
        return 12;
    }
    switch (n) {
    case 8: /* fpcontrol */
        stl_be_p(mem_buf, env->fpcr);
        return 4;
    case 9: /* fpstatus */
        stl_be_p(mem_buf, env->fpsr);
        return 4;
    case 10: /* fpiar, not implemented */
        memset(mem_buf, 0, 4);
        return 4;
    }
    return 0;
}

static int m68k_fpu_gdb_set_reg(CPUM68KState *env, uint8_t *mem_buf, int n)
{
    if (n < 8) {
        env->fregs[n].l.upper = lduw_be_p(mem_buf);
        env->fregs[n].l.lower = ldq_be_p(mem_buf + 4);
        return 12;
    }
    switch (n) {
    case 8: /* fpcontrol */
        cpu_m68k_set_fpcr(env, ldl_p(mem_buf));
        return 4;
    case 9: /* fpstatus */
        env->fpsr = ldl_p(mem_buf);
        return 4;
    case 10: /* fpiar, not implemented */
        return 4;
    }
    return 0;
}

M68kCPU *cpu_m68k_init(const char *cpu_model)
{
    M68kCPU *cpu;
    CPUM68KState *env;
    ObjectClass *oc;

    oc = cpu_class_by_name(TYPE_M68K_CPU, cpu_model);
    if (oc == NULL) {
        return NULL;
    }
    cpu = M68K_CPU(object_new(object_class_get_name(oc)));
    env = &cpu->env;

    register_m68k_insns(env);

    object_property_set_bool(OBJECT(cpu), true, "realized", NULL);

    return cpu;
}

void m68k_cpu_init_gdb(M68kCPU *cpu)
{
    CPUState *cs = CPU(cpu);
    CPUM68KState *env = &cpu->env;

    if (m68k_feature(env, M68K_FEATURE_CF_FPU)) {
        gdb_register_coprocessor(cs, cf_fpu_gdb_get_reg, cf_fpu_gdb_set_reg,
                                 11, "cf-fp.xml", 18);
    } else if (m68k_feature(env, M68K_FEATURE_FPU)) {
        gdb_register_coprocessor(cs, m68k_fpu_gdb_get_reg,
                                 m68k_fpu_gdb_set_reg, 11, "m68k-fp.xml", 18);
    }
    /* TODO: Add [E]MAC registers.  */
}

void HELPER(movec)(CPUM68KState *env, uint32_t reg, uint32_t val)
{
    M68kCPU *cpu = m68k_env_get_cpu(env);

    switch (reg) {
    case 0x02: /* CACR */
        env->cacr = val;
        m68k_switch_sp(env);
        break;
    case 0x04: case 0x05: case 0x06: case 0x07: /* ACR[0-3] */
        /* TODO: Implement Access Control Registers.  */
        break;
    case 0x801: /* VBR */
        env->vbr = val;
        break;
    /* TODO: Implement control registers.  */
    default:
        cpu_abort(CPU(cpu), "Unimplemented control register write 0x%x = 0x%x\n",
                  reg, val);
    }
}

void HELPER(set_macsr)(CPUM68KState *env, uint32_t val)
{
    uint32_t acc;
    int8_t exthigh;
    uint8_t extlow;
    uint64_t regval;
    int i;
    if ((env->macsr ^ val) & (MACSR_FI | MACSR_SU)) {
        for (i = 0; i < 4; i++) {
            regval = env->macc[i];
            exthigh = regval >> 40;
            if (env->macsr & MACSR_FI) {
                acc = regval >> 8;
                extlow = regval;
            } else {
                acc = regval;
                extlow = regval >> 32;
            }
            if (env->macsr & MACSR_FI) {
                regval = (((uint64_t)acc) << 8) | extlow;
                regval |= ((int64_t)exthigh) << 40;
            } else if (env->macsr & MACSR_SU) {
                regval = acc | (((int64_t)extlow) << 32);
                regval |= ((int64_t)exthigh) << 40;
            } else {
                regval = acc | (((uint64_t)extlow) << 32);
                regval |= ((uint64_t)(uint8_t)exthigh) << 40;
            }
            env->macc[i] = regval;
        }
    }
    env->macsr = val;
}

void m68k_switch_sp(CPUM68KState *env)
{
    int new_sp;

    env->sp[env->current_sp] = env->aregs[7];
    new_sp = (env->sr & SR_S && env->cacr & M68K_CACR_EUSP)
             ? M68K_SSP : M68K_USP;
    env->aregs[7] = env->sp[new_sp];
    env->current_sp = new_sp;
}

#if defined(CONFIG_USER_ONLY)

int m68k_cpu_handle_mmu_fault(CPUState *cs, vaddr address, int rw,
                              int mmu_idx)
{
    M68kCPU *cpu = M68K_CPU(cs);

    cs->exception_index = EXCP_ACCESS;
    cpu->env.mmu.ar = address;
    return 1;
}

#else

/* MMU */

/* TODO: This will need fixing once the MMU is implemented.  */
hwaddr m68k_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    return addr;
}

int m68k_cpu_handle_mmu_fault(CPUState *cs, vaddr address, int rw,
                              int mmu_idx)
{
    int prot;

    address &= TARGET_PAGE_MASK;
    prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
    tlb_set_page(cs, address, address, prot, mmu_idx, TARGET_PAGE_SIZE);
    return 0;
}

/* Notify CPU of a pending interrupt.  Prioritization and vectoring should
   be handled by the interrupt controller.  Real hardware only requests
   the vector when the interrupt is acknowledged by the CPU.  For
   simplicitly we calculate it when the interrupt is signalled.  */
void m68k_set_irq_level(M68kCPU *cpu, int level, uint8_t vector)
{
    CPUState *cs = CPU(cpu);
    CPUM68KState *env = &cpu->env;

    env->pending_level = level;
    env->pending_vector = vector;
    if (level) {
        cpu_interrupt(cs, CPU_INTERRUPT_HARD);
    } else {
        cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
    }
}

#endif

uint32_t HELPER(bitrev)(uint32_t x)
{
    x = ((x >> 1) & 0x55555555u) | ((x << 1) & 0xaaaaaaaau);
    x = ((x >> 2) & 0x33333333u) | ((x << 2) & 0xccccccccu);
    x = ((x >> 4) & 0x0f0f0f0fu) | ((x << 4) & 0xf0f0f0f0u);
    return bswap32(x);
}

uint32_t HELPER(ff1)(uint32_t x)
{
    int n;
    for (n = 32; x; n--)
        x >>= 1;
    return n;
}

uint32_t HELPER(sats)(uint32_t val, uint32_t v)
{
    /* The result has the opposite sign to the original value.  */
    if ((int32_t)v < 0) {
        val = (((int32_t)val) >> 31) ^ SIGNBIT;
    }
    return val;
}

void HELPER(set_sr)(CPUM68KState *env, uint32_t val)
{
    env->sr = val & 0xffe0;
    cpu_m68k_set_ccr(env, val);
    m68k_switch_sp(env);
}


/* MAC unit.  */
/* FIXME: The MAC unit implementation is a bit of a mess.  Some helpers
   take values,  others take register numbers and manipulate the contents
   in-place.  */
void HELPER(mac_move)(CPUM68KState *env, uint32_t dest, uint32_t src)
{
    uint32_t mask;
    env->macc[dest] = env->macc[src];
    mask = MACSR_PAV0 << dest;
    if (env->macsr & (MACSR_PAV0 << src))
        env->macsr |= mask;
    else
        env->macsr &= ~mask;
}

uint64_t HELPER(macmuls)(CPUM68KState *env, uint32_t op1, uint32_t op2)
{
    int64_t product;
    int64_t res;

    product = (uint64_t)op1 * op2;
    res = (product << 24) >> 24;
    if (res != product) {
        env->macsr |= MACSR_V;
        if (env->macsr & MACSR_OMC) {
            /* Make sure the accumulate operation overflows.  */
            if (product < 0)
                res = ~(1ll << 50);
            else
                res = 1ll << 50;
        }
    }
    return res;
}

uint64_t HELPER(macmulu)(CPUM68KState *env, uint32_t op1, uint32_t op2)
{
    uint64_t product;

    product = (uint64_t)op1 * op2;
    if (product & (0xffffffull << 40)) {
        env->macsr |= MACSR_V;
        if (env->macsr & MACSR_OMC) {
            /* Make sure the accumulate operation overflows.  */
            product = 1ll << 50;
        } else {
            product &= ((1ull << 40) - 1);
        }
    }
    return product;
}

uint64_t HELPER(macmulf)(CPUM68KState *env, uint32_t op1, uint32_t op2)
{
    uint64_t product;
    uint32_t remainder;

    product = (uint64_t)op1 * op2;
    if (env->macsr & MACSR_RT) {
        remainder = product & 0xffffff;
        product >>= 24;
        if (remainder > 0x800000)
            product++;
        else if (remainder == 0x800000)
            product += (product & 1);
    } else {
        product >>= 24;
    }
    return product;
}

void HELPER(macsats)(CPUM68KState *env, uint32_t acc)
{
    int64_t tmp;
    int64_t result;
    tmp = env->macc[acc];
    result = ((tmp << 16) >> 16);
    if (result != tmp) {
        env->macsr |= MACSR_V;
    }
    if (env->macsr & MACSR_V) {
        env->macsr |= MACSR_PAV0 << acc;
        if (env->macsr & MACSR_OMC) {
            /* The result is saturated to 32 bits, despite overflow occurring
               at 48 bits.  Seems weird, but that's what the hardware docs
               say.  */
            result = (result >> 63) ^ 0x7fffffff;
        }
    }
    env->macc[acc] = result;
}

void HELPER(macsatu)(CPUM68KState *env, uint32_t acc)
{
    uint64_t val;

    val = env->macc[acc];
    if (val & (0xffffull << 48)) {
        env->macsr |= MACSR_V;
    }
    if (env->macsr & MACSR_V) {
        env->macsr |= MACSR_PAV0 << acc;
        if (env->macsr & MACSR_OMC) {
            if (val > (1ull << 53))
                val = 0;
            else
                val = (1ull << 48) - 1;
        } else {
            val &= ((1ull << 48) - 1);
        }
    }
    env->macc[acc] = val;
}

void HELPER(macsatf)(CPUM68KState *env, uint32_t acc)
{
    int64_t sum;
    int64_t result;

    sum = env->macc[acc];
    result = (sum << 16) >> 16;
    if (result != sum) {
        env->macsr |= MACSR_V;
    }
    if (env->macsr & MACSR_V) {
        env->macsr |= MACSR_PAV0 << acc;
        if (env->macsr & MACSR_OMC) {
            result = (result >> 63) ^ 0x7fffffffffffll;
        }
    }
    env->macc[acc] = result;
}

void HELPER(mac_set_flags)(CPUM68KState *env, uint32_t acc)
{
    uint64_t val;
    val = env->macc[acc];
    if (val == 0) {
        env->macsr |= MACSR_Z;
    } else if (val & (1ull << 47)) {
        env->macsr |= MACSR_N;
    }
    if (env->macsr & (MACSR_PAV0 << acc)) {
        env->macsr |= MACSR_V;
    }
    if (env->macsr & MACSR_FI) {
        val = ((int64_t)val) >> 40;
        if (val != 0 && val != -1)
            env->macsr |= MACSR_EV;
    } else if (env->macsr & MACSR_SU) {
        val = ((int64_t)val) >> 32;
        if (val != 0 && val != -1)
            env->macsr |= MACSR_EV;
    } else {
        if ((val >> 32) != 0)
            env->macsr |= MACSR_EV;
    }
}

#define EXTSIGN(val, index) (     \
    (index == 0) ? (int8_t)(val) : ((index == 1) ? (int16_t)(val) : (val)) \
)

#define COMPUTE_CCR(op, x, n, z, v, c) {                                   \
    switch (op) {                                                          \
    case CC_OP_FLAGS:                                                      \
        /* Everything in place.  */                                        \
        break;                                                             \
    case CC_OP_ADDB:                                                       \
    case CC_OP_ADDW:                                                       \
    case CC_OP_ADDL:                                                       \
        res = n;                                                           \
        src2 = v;                                                          \
        src1 = EXTSIGN(res - src2, op - CC_OP_ADDB);                       \
        c = x;                                                             \
        z = n;                                                             \
        v = (res ^ src1) & ~(src1 ^ src2);                                 \
        break;                                                             \
    case CC_OP_SUBB:                                                       \
    case CC_OP_SUBW:                                                       \
    case CC_OP_SUBL:                                                       \
        res = n;                                                           \
        src2 = v;                                                          \
        src1 = EXTSIGN(res + src2, op - CC_OP_SUBB);                       \
        c = x;                                                             \
        z = n;                                                             \
        v = (res ^ src1) & (src1 ^ src2);                                  \
        break;                                                             \
    case CC_OP_CMPB:                                                       \
    case CC_OP_CMPW:                                                       \
    case CC_OP_CMPL:                                                       \
        src1 = n;                                                          \
        src2 = v;                                                          \
        res = EXTSIGN(src1 - src2, op - CC_OP_CMPB);                       \
        n = res;                                                           \
        z = res;                                                           \
        c = src1 < src2;                                                   \
        v = (res ^ src1) & (src1 ^ src2);                                  \
        break;                                                             \
    case CC_OP_LOGIC:                                                      \
        c = v = 0;                                                         \
        z = n;                                                             \
        break;                                                             \
    default:                                                               \
        cpu_abort(CPU(m68k_env_get_cpu(env)), "Bad CC_OP %d", op);         \
    }                                                                      \
} while (0)

uint32_t cpu_m68k_get_ccr(CPUM68KState *env)
{
    uint32_t x, c, n, z, v;
    uint32_t res, src1, src2;

    x = env->cc_x;
    n = env->cc_n;
    z = env->cc_z;
    v = env->cc_v;
    c = env->cc_c;

    COMPUTE_CCR(env->cc_op, x, n, z, v, c);

    n = n >> 31;
    z = (z == 0);
    v = v >> 31;

    return x * CCF_X + n * CCF_N + z * CCF_Z + v * CCF_V + c * CCF_C;
}

uint32_t HELPER(get_ccr)(CPUM68KState *env)
{
    return cpu_m68k_get_ccr(env);
}

void cpu_m68k_set_ccr(CPUM68KState *env, uint32_t ccr)
{
    env->cc_x = (ccr & CCF_X ? 1 : 0);
    env->cc_n = (ccr & CCF_N ? -1 : 0);
    env->cc_z = (ccr & CCF_Z ? 0 : 1);
    env->cc_v = (ccr & CCF_V ? -1 : 0);
    env->cc_c = (ccr & CCF_C ? 1 : 0);
    env->cc_op = CC_OP_FLAGS;
}

void HELPER(set_ccr)(CPUM68KState *env, uint32_t ccr)
{
    cpu_m68k_set_ccr(env, ccr);
}

void HELPER(flush_flags)(CPUM68KState *env, uint32_t cc_op)
{
    uint32_t res, src1, src2;

    COMPUTE_CCR(cc_op, env->cc_x, env->cc_n, env->cc_z, env->cc_v, env->cc_c);
    env->cc_op = CC_OP_FLAGS;
}

uint32_t HELPER(get_macf)(CPUM68KState *env, uint64_t val)
{
    int rem;
    uint32_t result;

    if (env->macsr & MACSR_SU) {
        /* 16-bit rounding.  */
        rem = val & 0xffffff;
        val = (val >> 24) & 0xffffu;
        if (rem > 0x800000)
            val++;
        else if (rem == 0x800000)
            val += (val & 1);
    } else if (env->macsr & MACSR_RT) {
        /* 32-bit rounding.  */
        rem = val & 0xff;
        val >>= 8;
        if (rem > 0x80)
            val++;
        else if (rem == 0x80)
            val += (val & 1);
    } else {
        /* No rounding.  */
        val >>= 8;
    }
    if (env->macsr & MACSR_OMC) {
        /* Saturate.  */
        if (env->macsr & MACSR_SU) {
            if (val != (uint16_t) val) {
                result = ((val >> 63) ^ 0x7fff) & 0xffff;
            } else {
                result = val & 0xffff;
            }
        } else {
            if (val != (uint32_t)val) {
                result = ((uint32_t)(val >> 63) & 0x7fffffff);
            } else {
                result = (uint32_t)val;
            }
        }
    } else {
        /* No saturation.  */
        if (env->macsr & MACSR_SU) {
            result = val & 0xffff;
        } else {
            result = (uint32_t)val;
        }
    }
    return result;
}

uint32_t HELPER(get_macs)(uint64_t val)
{
    if (val == (int32_t)val) {
        return (int32_t)val;
    } else {
        return (val >> 61) ^ ~SIGNBIT;
    }
}

uint32_t HELPER(get_macu)(uint64_t val)
{
    if ((val >> 32) == 0) {
        return (uint32_t)val;
    } else {
        return 0xffffffffu;
    }
}

uint32_t HELPER(get_mac_extf)(CPUM68KState *env, uint32_t acc)
{
    uint32_t val;
    val = env->macc[acc] & 0x00ff;
    val |= (env->macc[acc] >> 32) & 0xff00;
    val |= (env->macc[acc + 1] << 16) & 0x00ff0000;
    val |= (env->macc[acc + 1] >> 16) & 0xff000000;
    return val;
}

uint32_t HELPER(get_mac_exti)(CPUM68KState *env, uint32_t acc)
{
    uint32_t val;
    val = (env->macc[acc] >> 32) & 0xffff;
    val |= (env->macc[acc + 1] >> 16) & 0xffff0000;
    return val;
}

void HELPER(set_mac_extf)(CPUM68KState *env, uint32_t val, uint32_t acc)
{
    int64_t res;
    int32_t tmp;
    res = env->macc[acc] & 0xffffffff00ull;
    tmp = (int16_t)(val & 0xff00);
    res |= ((int64_t)tmp) << 32;
    res |= val & 0xff;
    env->macc[acc] = res;
    res = env->macc[acc + 1] & 0xffffffff00ull;
    tmp = (val & 0xff000000);
    res |= ((int64_t)tmp) << 16;
    res |= (val >> 16) & 0xff;
    env->macc[acc + 1] = res;
}

void HELPER(set_mac_exts)(CPUM68KState *env, uint32_t val, uint32_t acc)
{
    int64_t res;
    int32_t tmp;
    res = (uint32_t)env->macc[acc];
    tmp = (int16_t)val;
    res |= ((int64_t)tmp) << 32;
    env->macc[acc] = res;
    res = (uint32_t)env->macc[acc + 1];
    tmp = val & 0xffff0000;
    res |= (int64_t)tmp << 16;
    env->macc[acc + 1] = res;
}

void HELPER(set_mac_extu)(CPUM68KState *env, uint32_t val, uint32_t acc)
{
    uint64_t res;
    res = (uint32_t)env->macc[acc];
    res |= ((uint64_t)(val & 0xffff)) << 32;
    env->macc[acc] = res;
    res = (uint32_t)env->macc[acc + 1];
    res |= (uint64_t)(val & 0xffff0000) << 16;
    env->macc[acc + 1] = res;
}
