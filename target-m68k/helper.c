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

#include "cpu.h"
#include "exec/gdbstub.h"

#include "helper.h"

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

static int fpu_gdb_get_reg(CPUM68KState *env, uint8_t *mem_buf, int n)
{
    if (n < 8) {
        stfq_p(mem_buf, env->fregs[n]);
        return 8;
    }
    if (n < 11) {
        /* FP control registers (not implemented)  */
        memset(mem_buf, 0, 4);
        return 4;
    }
    return 0;
}

static int fpu_gdb_set_reg(CPUM68KState *env, uint8_t *mem_buf, int n)
{
    if (n < 8) {
        env->fregs[n] = ldfq_p(mem_buf);
        return 8;
    }
    if (n < 11) {
        /* FP control registers (not implemented)  */
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
        gdb_register_coprocessor(cs, fpu_gdb_get_reg, fpu_gdb_set_reg,
                                 11, "cf-fp.xml", 18);
    }
    /* TODO: Add [E]MAC registers.  */
}

void cpu_m68k_flush_flags(CPUM68KState *env, int cc_op)
{
    M68kCPU *cpu = m68k_env_get_cpu(env);
    int flags;
    uint32_t src;
    uint32_t dest;
    uint32_t tmp;

#define HIGHBIT 0x80000000u

#define SET_NZ(x) do { \
    if ((x) == 0) \
        flags |= CCF_Z; \
    else if ((int32_t)(x) < 0) \
        flags |= CCF_N; \
    } while (0)

#define SET_FLAGS_SUB(type, utype) do { \
    SET_NZ((type)dest); \
    tmp = dest + src; \
    if ((utype) tmp < (utype) src) \
        flags |= CCF_C; \
    if ((1u << (sizeof(type) * 8 - 1)) & (tmp ^ dest) & (tmp ^ src)) \
        flags |= CCF_V; \
    } while (0)

    flags = 0;
    src = env->cc_src;
    dest = env->cc_dest;
    switch (cc_op) {
    case CC_OP_FLAGS:
        flags = dest;
        break;
    case CC_OP_LOGIC:
        SET_NZ(dest);
        break;
    case CC_OP_ADD:
        SET_NZ(dest);
        if (dest < src)
            flags |= CCF_C;
        tmp = dest - src;
        if (HIGHBIT & (src ^ dest) & ~(tmp ^ src))
            flags |= CCF_V;
        break;
    case CC_OP_SUB:
        SET_FLAGS_SUB(int32_t, uint32_t);
        break;
    case CC_OP_CMPB:
        SET_FLAGS_SUB(int8_t, uint8_t);
        break;
    case CC_OP_CMPW:
        SET_FLAGS_SUB(int16_t, uint16_t);
        break;
    case CC_OP_ADDX:
        SET_NZ(dest);
        if (dest <= src)
            flags |= CCF_C;
        tmp = dest - src - 1;
        if (HIGHBIT & (src ^ dest) & ~(tmp ^ src))
            flags |= CCF_V;
        break;
    case CC_OP_SUBX:
        SET_NZ(dest);
        tmp = dest + src + 1;
        if (tmp <= src)
            flags |= CCF_C;
        if (HIGHBIT & (tmp ^ dest) & (tmp ^ src))
            flags |= CCF_V;
        break;
    case CC_OP_SHIFT:
        SET_NZ(dest);
        if (src)
            flags |= CCF_C;
        break;
    default:
        cpu_abort(CPU(cpu), "Bad CC_OP %d", cc_op);
    }
    env->cc_op = CC_OP_FLAGS;
    env->cc_dest = flags;
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

uint32_t HELPER(sats)(uint32_t val, uint32_t ccr)
{
    /* The result has the opposite sign to the original value.  */
    if (ccr & CCF_V)
        val = (((int32_t)val) >> 31) ^ SIGNBIT;
    return val;
}

uint32_t HELPER(subx_cc)(CPUM68KState *env, uint32_t op1, uint32_t op2)
{
    uint32_t res;
    uint32_t old_flags;

    old_flags = env->cc_dest;
    if (env->cc_x) {
        env->cc_x = (op1 <= op2);
        env->cc_op = CC_OP_SUBX;
        res = op1 - (op2 + 1);
    } else {
        env->cc_x = (op1 < op2);
        env->cc_op = CC_OP_SUB;
        res = op1 - op2;
    }
    env->cc_dest = res;
    env->cc_src = op2;
    cpu_m68k_flush_flags(env, env->cc_op);
    /* !Z is sticky.  */
    env->cc_dest &= (old_flags | ~CCF_Z);
    return res;
}

uint32_t HELPER(addx_cc)(CPUM68KState *env, uint32_t op1, uint32_t op2)
{
    uint32_t res;
    uint32_t old_flags;

    old_flags = env->cc_dest;
    if (env->cc_x) {
        res = op1 + op2 + 1;
        env->cc_x = (res <= op2);
        env->cc_op = CC_OP_ADDX;
    } else {
        res = op1 + op2;
        env->cc_x = (res < op2);
        env->cc_op = CC_OP_ADD;
    }
    env->cc_dest = res;
    env->cc_src = op2;
    cpu_m68k_flush_flags(env, env->cc_op);
    /* !Z is sticky.  */
    env->cc_dest &= (old_flags | ~CCF_Z);
    return res;
}

uint32_t HELPER(xflag_lt)(uint32_t a, uint32_t b)
{
    return a < b;
}

void HELPER(set_sr)(CPUM68KState *env, uint32_t val)
{
    env->sr = val & 0xffff;
    m68k_switch_sp(env);
}

uint32_t HELPER(shl_cc)(CPUM68KState *env, uint32_t val, uint32_t shift)
{
    uint32_t result;
    uint32_t cf;

    shift &= 63;
    if (shift == 0) {
        result = val;
        cf = env->cc_src & CCF_C;
    } else if (shift < 32) {
        result = val << shift;
        cf = (val >> (32 - shift)) & 1;
    } else if (shift == 32) {
        result = 0;
        cf = val & 1;
    } else /* shift > 32 */ {
        result = 0;
        cf = 0;
    }
    env->cc_src = cf;
    env->cc_x = (cf != 0);
    env->cc_dest = result;
    return result;
}

uint32_t HELPER(shr_cc)(CPUM68KState *env, uint32_t val, uint32_t shift)
{
    uint32_t result;
    uint32_t cf;

    shift &= 63;
    if (shift == 0) {
        result = val;
        cf = env->cc_src & CCF_C;
    } else if (shift < 32) {
        result = val >> shift;
        cf = (val >> (shift - 1)) & 1;
    } else if (shift == 32) {
        result = 0;
        cf = val >> 31;
    } else /* shift > 32 */ {
        result = 0;
        cf = 0;
    }
    env->cc_src = cf;
    env->cc_x = (cf != 0);
    env->cc_dest = result;
    return result;
}

uint32_t HELPER(sar_cc)(CPUM68KState *env, uint32_t val, uint32_t shift)
{
    uint32_t result;
    uint32_t cf;

    shift &= 63;
    if (shift == 0) {
        result = val;
        cf = (env->cc_src & CCF_C) != 0;
    } else if (shift < 32) {
        result = (int32_t)val >> shift;
        cf = (val >> (shift - 1)) & 1;
    } else /* shift >= 32 */ {
        result = (int32_t)val >> 31;
        cf = val >> 31;
    }
    env->cc_src = cf;
    env->cc_x = cf;
    env->cc_dest = result;
    return result;
}

/* FPU helpers.  */
uint32_t HELPER(f64_to_i32)(CPUM68KState *env, float64 val)
{
    return float64_to_int32(val, &env->fp_status);
}

float32 HELPER(f64_to_f32)(CPUM68KState *env, float64 val)
{
    return float64_to_float32(val, &env->fp_status);
}

float64 HELPER(i32_to_f64)(CPUM68KState *env, uint32_t val)
{
    return int32_to_float64(val, &env->fp_status);
}

float64 HELPER(f32_to_f64)(CPUM68KState *env, float32 val)
{
    return float32_to_float64(val, &env->fp_status);
}

float64 HELPER(iround_f64)(CPUM68KState *env, float64 val)
{
    return float64_round_to_int(val, &env->fp_status);
}

float64 HELPER(itrunc_f64)(CPUM68KState *env, float64 val)
{
    return float64_trunc_to_int(val, &env->fp_status);
}

float64 HELPER(sqrt_f64)(CPUM68KState *env, float64 val)
{
    return float64_sqrt(val, &env->fp_status);
}

float64 HELPER(abs_f64)(float64 val)
{
    return float64_abs(val);
}

float64 HELPER(chs_f64)(float64 val)
{
    return float64_chs(val);
}

float64 HELPER(add_f64)(CPUM68KState *env, float64 a, float64 b)
{
    return float64_add(a, b, &env->fp_status);
}

float64 HELPER(sub_f64)(CPUM68KState *env, float64 a, float64 b)
{
    return float64_sub(a, b, &env->fp_status);
}

float64 HELPER(mul_f64)(CPUM68KState *env, float64 a, float64 b)
{
    return float64_mul(a, b, &env->fp_status);
}

float64 HELPER(div_f64)(CPUM68KState *env, float64 a, float64 b)
{
    return float64_div(a, b, &env->fp_status);
}

float64 HELPER(sub_cmp_f64)(CPUM68KState *env, float64 a, float64 b)
{
    /* ??? This may incorrectly raise exceptions.  */
    /* ??? Should flush denormals to zero.  */
    float64 res;
    res = float64_sub(a, b, &env->fp_status);
    if (float64_is_quiet_nan(res)) {
        /* +/-inf compares equal against itself, but sub returns nan.  */
        if (!float64_is_quiet_nan(a)
            && !float64_is_quiet_nan(b)) {
            res = float64_zero;
            if (float64_lt_quiet(a, res, &env->fp_status))
                res = float64_chs(res);
        }
    }
    return res;
}

uint32_t HELPER(compare_f64)(CPUM68KState *env, float64 val)
{
    return float64_compare_quiet(val, float64_zero, &env->fp_status);
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

void HELPER(flush_flags)(CPUM68KState *env, uint32_t cc_op)
{
    cpu_m68k_flush_flags(env, cc_op);
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
    val = (env->macc[acc] >> 32) & 0xff00;
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
