/*
 * ARM gdb server stub: AArch64 specific functions.
 *
 * Copyright (c) 2013 SUSE LINUX Products GmbH
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "internals.h"
#include "gdbstub/helpers.h"
#include "gdbstub/commands.h"
#include "tcg/mte_helper.h"
#if defined(CONFIG_USER_ONLY) && defined(CONFIG_LINUX)
#include <sys/prctl.h>
#include "mte_user_helper.h"
#endif
#ifdef CONFIG_TCG
#include "accel/tcg/cpu-mmu-index.h"
#include "exec/target_page.h"
#endif

int aarch64_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    if (n < 31) {
        /* Core integer register.  */
        return gdb_get_reg64(mem_buf, env->xregs[n]);
    }
    switch (n) {
    case 31:
        return gdb_get_reg64(mem_buf, env->xregs[31]);
    case 32:
        return gdb_get_reg64(mem_buf, env->pc);
    case 33:
        return gdb_get_reg32(mem_buf, pstate_read(env));
    }
    /* Unknown register.  */
    return 0;
}

int aarch64_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    uint64_t tmp;

    tmp = ldq_p(mem_buf);

    if (n < 31) {
        /* Core integer register.  */
        env->xregs[n] = tmp;
        return 8;
    }
    switch (n) {
    case 31:
        env->xregs[31] = tmp;
        return 8;
    case 32:
        env->pc = tmp;
        return 8;
    case 33:
        /* CPSR */
        pstate_write(env, tmp);
        return 4;
    }
    /* Unknown register.  */
    return 0;
}

int aarch64_gdb_get_fpu_reg(CPUState *cs, GByteArray *buf, int reg)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    switch (reg) {
    case 0 ... 31:
    {
        /* 128 bit FP register - quads are in LE order */
        uint64_t *q = aa64_vfp_qreg(env, reg);
        return gdb_get_reg128(buf, q[1], q[0]);
    }
    case 32:
        /* FPSR */
        return gdb_get_reg32(buf, vfp_get_fpsr(env));
    case 33:
        /* FPCR */
        return gdb_get_reg32(buf, vfp_get_fpcr(env));
    default:
        return 0;
    }
}

int aarch64_gdb_set_fpu_reg(CPUState *cs, uint8_t *buf, int reg)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    switch (reg) {
    case 0 ... 31:
        /* 128 bit FP register */
        {
            uint64_t *q = aa64_vfp_qreg(env, reg);
            q[0] = ldq_le_p(buf);
            q[1] = ldq_le_p(buf + 8);
            return 16;
        }
    case 32:
        /* FPSR */
        vfp_set_fpsr(env, ldl_p(buf));
        return 4;
    case 33:
        /* FPCR */
        vfp_set_fpcr(env, ldl_p(buf));
        return 4;
    default:
        return 0;
    }
}

int aarch64_gdb_get_sve_reg(CPUState *cs, GByteArray *buf, int reg)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    switch (reg) {
    /* The first 32 registers are the zregs */
    case 0 ... 31:
    {
        int vq, len = 0;
        for (vq = 0; vq < cpu->sve_max_vq; vq++) {
            len += gdb_get_reg128(buf,
                                  env->vfp.zregs[reg].d[vq * 2 + 1],
                                  env->vfp.zregs[reg].d[vq * 2]);
        }
        return len;
    }
    case 32:
        return gdb_get_reg32(buf, vfp_get_fpsr(env));
    case 33:
        return gdb_get_reg32(buf, vfp_get_fpcr(env));
    /* then 16 predicates and the ffr */
    case 34 ... 50:
    {
        int preg = reg - 34;
        int vq, len = 0;
        for (vq = 0; vq < cpu->sve_max_vq; vq = vq + 4) {
            len += gdb_get_reg64(buf, env->vfp.pregs[preg].p[vq / 4]);
        }
        return len;
    }
    case 51:
    {
        /*
         * We report in Vector Granules (VG) which is 64bit in a Z reg
         * while the ZCR works in Vector Quads (VQ) which is 128bit chunks.
         */
        int vq = sve_vqm1_for_el(env, arm_current_el(env)) + 1;
        return gdb_get_reg64(buf, vq * 2);
    }
    default:
        /* gdbstub asked for something out our range */
        qemu_log_mask(LOG_UNIMP, "%s: out of range register %d", __func__, reg);
        break;
    }

    return 0;
}

int aarch64_gdb_set_sve_reg(CPUState *cs, uint8_t *buf, int reg)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    /* The first 32 registers are the zregs */
    switch (reg) {
    /* The first 32 registers are the zregs */
    case 0 ... 31:
    {
        int vq, len = 0;
        uint64_t *p = (uint64_t *) buf;
        for (vq = 0; vq < cpu->sve_max_vq; vq++) {
            env->vfp.zregs[reg].d[vq * 2 + 1] = *p++;
            env->vfp.zregs[reg].d[vq * 2] = *p++;
            len += 16;
        }
        return len;
    }
    case 32:
        vfp_set_fpsr(env, *(uint32_t *)buf);
        return 4;
    case 33:
        vfp_set_fpcr(env, *(uint32_t *)buf);
        return 4;
    case 34 ... 50:
    {
        int preg = reg - 34;
        int vq, len = 0;
        uint64_t *p = (uint64_t *) buf;
        for (vq = 0; vq < cpu->sve_max_vq; vq = vq + 4) {
            env->vfp.pregs[preg].p[vq / 4] = *p++;
            len += 8;
        }
        return len;
    }
    case 51:
        /* cannot set vg via gdbstub */
        return 0;
    default:
        /* gdbstub asked for something out our range */
        break;
    }

    return 0;
}

int aarch64_gdb_get_pauth_reg(CPUState *cs, GByteArray *buf, int reg)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    switch (reg) {
    case 0: /* pauth_dmask */
    case 1: /* pauth_cmask */
    case 2: /* pauth_dmask_high */
    case 3: /* pauth_cmask_high */
        /*
         * Note that older versions of this feature only contained
         * pauth_{d,c}mask, for use with Linux user processes, and
         * thus exclusively in the low half of the address space.
         *
         * To support system mode, and to debug kernels, two new regs
         * were added to cover the high half of the address space.
         * For the purpose of pauth_ptr_mask, we can use any well-formed
         * address within the address space half -- here, 0 and -1.
         */
        {
            bool is_data = !(reg & 1);
            bool is_high = reg & 2;
            ARMMMUIdx mmu_idx = arm_stage1_mmu_idx(env);
            ARMVAParameters param;

            param = aa64_va_parameters(env, -is_high, mmu_idx, is_data, false);
            return gdb_get_reg64(buf, pauth_ptr_mask(param));
        }
    default:
        return 0;
    }
}

int aarch64_gdb_set_pauth_reg(CPUState *cs, uint8_t *buf, int reg)
{
    /* All pseudo registers are read-only. */
    return 0;
}

static void output_vector_union_type(GDBFeatureBuilder *builder, int reg_width,
                                     const char *name)
{
    struct TypeSize {
        const char *gdb_type;
        short size;
        char sz, suffix;
    };

    static const struct TypeSize vec_lanes[] = {
        /* quads */
        { "uint128", 128, 'q', 'u' },
        { "int128", 128, 'q', 's' },
        /* 64 bit */
        { "ieee_double", 64, 'd', 'f' },
        { "uint64", 64, 'd', 'u' },
        { "int64", 64, 'd', 's' },
        /* 32 bit */
        { "ieee_single", 32, 's', 'f' },
        { "uint32", 32, 's', 'u' },
        { "int32", 32, 's', 's' },
        /* 16 bit */
        { "ieee_half", 16, 'h', 'f' },
        { "uint16", 16, 'h', 'u' },
        { "int16", 16, 'h', 's' },
        /* bytes */
        { "uint8", 8, 'b', 'u' },
        { "int8", 8, 'b', 's' },
    };

    static const char suf[] = { 'b', 'h', 's', 'd', 'q' };
    int i, j;

    /* First define types and totals in a whole VL */
    for (i = 0; i < ARRAY_SIZE(vec_lanes); i++) {
        gdb_feature_builder_append_tag(
            builder, "<vector id=\"%s%c%c\" type=\"%s\" count=\"%d\"/>",
            name, vec_lanes[i].sz, vec_lanes[i].suffix,
            vec_lanes[i].gdb_type, reg_width / vec_lanes[i].size);
    }

    /*
     * Now define a union for each size group containing unsigned and
     * signed and potentially float versions of each size from 128 to
     * 8 bits.
     */
    for (i = 0; i < ARRAY_SIZE(suf); i++) {
        int bits = 8 << i;

        gdb_feature_builder_append_tag(builder, "<union id=\"%sn%c\">",
                                       name, suf[i]);
        for (j = 0; j < ARRAY_SIZE(vec_lanes); j++) {
            if (vec_lanes[j].size == bits) {
                gdb_feature_builder_append_tag(
                    builder, "<field name=\"%c\" type=\"%s%c%c\"/>",
                    vec_lanes[j].suffix, name,
                    vec_lanes[j].sz, vec_lanes[j].suffix);
            }
        }
        gdb_feature_builder_append_tag(builder, "</union>");
    }

    /* And now the final union of unions */
    gdb_feature_builder_append_tag(builder, "<union id=\"%s\">", name);
    for (i = ARRAY_SIZE(suf) - 1; i >= 0; i--) {
        gdb_feature_builder_append_tag(builder,
                                       "<field name=\"%c\" type=\"%sn%c\"/>",
                                       suf[i], name, suf[i]);
    }
    gdb_feature_builder_append_tag(builder, "</union>");
}

GDBFeature *arm_gen_dynamic_svereg_feature(CPUState *cs, int base_reg)
{
    ARMCPU *cpu = ARM_CPU(cs);
    int reg_width = cpu->sve_max_vq * 128;
    int pred_width = cpu->sve_max_vq * 16;
    GDBFeatureBuilder builder;
    char *name;
    int reg = 0;
    int i;

    gdb_feature_builder_init(&builder, &cpu->dyn_svereg_feature.desc,
                             "org.gnu.gdb.aarch64.sve", "sve-registers.xml",
                             base_reg);

    /* Create the vector union type. */
    output_vector_union_type(&builder, reg_width, "svev");

    /* Create the predicate vector type. */
    gdb_feature_builder_append_tag(
        &builder, "<vector id=\"svep\" type=\"uint8\" count=\"%d\"/>",
        pred_width / 8);

    /* Define the vector registers. */
    for (i = 0; i < 32; i++) {
        name = g_strdup_printf("z%d", i);
        gdb_feature_builder_append_reg(&builder, name, reg_width, reg++,
                                       "svev", NULL);
    }

    /* fpscr & status registers */
    gdb_feature_builder_append_reg(&builder, "fpsr", 32, reg++,
                                   "int", "float");
    gdb_feature_builder_append_reg(&builder, "fpcr", 32, reg++,
                                   "int", "float");

    /* Define the predicate registers. */
    for (i = 0; i < 16; i++) {
        name = g_strdup_printf("p%d", i);
        gdb_feature_builder_append_reg(&builder, name, pred_width, reg++,
                                       "svep", NULL);
    }
    gdb_feature_builder_append_reg(&builder, "ffr", pred_width, reg++,
                                   "svep", "vector");

    /* Define the vector length pseudo-register. */
    gdb_feature_builder_append_reg(&builder, "vg", 64, reg++, "int", NULL);

    gdb_feature_builder_end(&builder);

    return &cpu->dyn_svereg_feature.desc;
}

#ifdef CONFIG_USER_ONLY
int aarch64_gdb_get_tag_ctl_reg(CPUState *cs, GByteArray *buf, int reg)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    uint64_t tcf0;

    assert(reg == 0);

    tcf0 = extract64(env->cp15.sctlr_el[1], 38, 2);

    return gdb_get_reg64(buf, tcf0);
}

int aarch64_gdb_set_tag_ctl_reg(CPUState *cs, uint8_t *buf, int reg)
{
#if defined(CONFIG_LINUX)
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    uint8_t tcf;

    assert(reg == 0);

    tcf = *buf << PR_MTE_TCF_SHIFT;

    if (!tcf) {
        return 0;
    }

    /*
     * 'tag_ctl' register is actually a "pseudo-register" provided by GDB to
     * expose options regarding the type of MTE fault that can be controlled at
     * runtime.
     */
    arm_set_mte_tcf0(env, tcf);

    return 1;
#else
    return 0;
#endif
}
#endif /* CONFIG_USER_ONLY */

#ifdef CONFIG_TCG
static void handle_q_memtag(GArray *params, void *user_ctx)
{
    ARMCPU *cpu = ARM_CPU(user_ctx);
    CPUARMState *env = &cpu->env;
    uint32_t mmu_index;

    uint64_t addr = gdb_get_cmd_param(params, 0)->val_ull;
    uint64_t len = gdb_get_cmd_param(params, 1)->val_ul;
    int type = gdb_get_cmd_param(params, 2)->val_ul;

    uint8_t *tags;
    uint8_t addr_tag;

    g_autoptr(GString) str_buf = g_string_new(NULL);

    /*
     * GDB does not query multiple tags for a memory range on remote targets, so
     * that's not supported either by gdbstub.
     */
    if (len != 1) {
        gdb_put_packet("E02");
    }

    /* GDB never queries a tag different from an allocation tag (type 1). */
    if (type != 1) {
        gdb_put_packet("E03");
    }

    /* Find out the current translation regime for probe. */
    mmu_index = cpu_mmu_index(env_cpu(env), false);
    /* Note that tags are packed here (2 tags packed in one byte). */
    tags = allocation_tag_mem_probe(env, mmu_index, addr, MMU_DATA_LOAD, 1,
                                    MMU_DATA_LOAD, true, 0);
    if (!tags) {
        /* Address is not in a tagged region. */
        gdb_put_packet("E04");
        return;
    }

    /* Unpack tag from byte. */
    addr_tag = load_tag1(addr, tags);
    g_string_printf(str_buf, "m%.2x", addr_tag);

    gdb_put_packet(str_buf->str);
}

static void handle_q_isaddresstagged(GArray *params, void *user_ctx)
{
    ARMCPU *cpu = ARM_CPU(user_ctx);
    CPUARMState *env = &cpu->env;
    uint32_t mmu_index;

    uint64_t addr = gdb_get_cmd_param(params, 0)->val_ull;

    uint8_t *tags;
    const char *reply;

    /* Find out the current translation regime for probe. */
    mmu_index = cpu_mmu_index(env_cpu(env), false);
    tags = allocation_tag_mem_probe(env, mmu_index, addr, MMU_DATA_LOAD, 1,
                                    MMU_DATA_LOAD, true, 0);
    reply = tags ? "01" : "00";

    gdb_put_packet(reply);
}

static void handle_Q_memtag(GArray *params, void *user_ctx)
{
    ARMCPU *cpu = ARM_CPU(user_ctx);
    CPUARMState *env = &cpu->env;
    uint32_t mmu_index;

    uint64_t start_addr = gdb_get_cmd_param(params, 0)->val_ull;
    uint64_t len = gdb_get_cmd_param(params, 1)->val_ul;
    int type = gdb_get_cmd_param(params, 2)->val_ul;
    char const *new_tags_str = gdb_get_cmd_param(params, 3)->data;

    uint64_t end_addr;

    int num_new_tags;
    uint8_t *tags;

    g_autoptr(GByteArray) new_tags = g_byte_array_new();

    /*
     * Only the allocation tag (i.e. type 1) can be set at the stub side.
     */
    if (type != 1) {
        gdb_put_packet("E02");
        return;
    }

    end_addr = start_addr + (len - 1); /* 'len' is always >= 1 */
    /* Check if request's memory range does not cross page boundaries. */
    if ((start_addr ^ end_addr) & TARGET_PAGE_MASK) {
        gdb_put_packet("E03");
        return;
    }

    /*
     * Get all tags in the page starting from the tag of the start address.
     * Note that there are two tags packed into a single byte here.
     */
    /* Find out the current translation regime for probe. */
    mmu_index = cpu_mmu_index(env_cpu(env), false);
    tags = allocation_tag_mem_probe(env, mmu_index, start_addr, MMU_DATA_STORE,
                                    1, MMU_DATA_STORE, true, 0);
    if (!tags) {
        /* Address is not in a tagged region. */
        gdb_put_packet("E04");
        return;
    }

    /* Convert tags provided by GDB, 2 hex digits per tag. */
    num_new_tags = strlen(new_tags_str) / 2;
    gdb_hextomem(new_tags, new_tags_str, num_new_tags);

    uint64_t address = start_addr;
    int new_tag_index = 0;
    while (address <= end_addr) {
        uint8_t new_tag;
        int packed_index;

        /*
         * Find packed tag index from unpacked tag index. There are two tags
         * in one packed index (one tag per nibble).
         */
        packed_index = new_tag_index / 2;

        new_tag = new_tags->data[new_tag_index % num_new_tags];
        store_tag1(address, tags + packed_index, new_tag);

        address += TAG_GRANULE;
        new_tag_index++;
    }

    gdb_put_packet("OK");
}

enum Command {
    qMemTags,
    qIsAddressTagged,
    QMemTags,
    NUM_CMDS
};

static const GdbCmdParseEntry cmd_handler_table[NUM_CMDS] = {
    [qMemTags] = {
        .handler = handle_q_memtag,
        .cmd_startswith = true,
        .cmd = "MemTags:",
        .schema = "L,l:l0",
        .need_cpu_context = true
    },
    [qIsAddressTagged] = {
        .handler = handle_q_isaddresstagged,
        .cmd_startswith = true,
        .cmd = "IsAddressTagged:",
        .schema = "L0",
        .need_cpu_context = true
    },
    [QMemTags] = {
        .handler = handle_Q_memtag,
        .cmd_startswith = true,
        .cmd = "MemTags:",
        .schema = "L,l:l:s0",
        .need_cpu_context = true
    },
};
#endif /* CONFIG_TCG */

void aarch64_cpu_register_gdb_commands(ARMCPU *cpu, GString *qsupported,
                                       GPtrArray *qtable, GPtrArray *stable)
{
    /* MTE */
#ifdef CONFIG_TCG
    if (cpu_isar_feature(aa64_mte, cpu)) {
        g_string_append(qsupported, ";memory-tagging+");

        g_ptr_array_add(qtable, (gpointer) &cmd_handler_table[qMemTags]);
        g_ptr_array_add(qtable, (gpointer) &cmd_handler_table[qIsAddressTagged]);
        g_ptr_array_add(stable, (gpointer) &cmd_handler_table[QMemTags]);
    }
#endif
}
