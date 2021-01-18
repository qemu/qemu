/*
 * ARM gdb server stub
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
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
#include "cpu.h"
#include "exec/gdbstub.h"

typedef struct RegisterSysregXmlParam {
    CPUState *cs;
    GString *s;
    int n;
} RegisterSysregXmlParam;

/* Old gdb always expect FPA registers.  Newer (xml-aware) gdb only expect
   whatever the target description contains.  Due to a historical mishap
   the FPA registers appear in between core integer regs and the CPSR.
   We hack round this by giving the FPA regs zero size when talking to a
   newer gdb.  */

int arm_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    if (n < 16) {
        /* Core integer register.  */
        return gdb_get_reg32(mem_buf, env->regs[n]);
    }
    if (n < 24) {
        /* FPA registers.  */
        if (gdb_has_xml) {
            return 0;
        }
        return gdb_get_zeroes(mem_buf, 12);
    }
    switch (n) {
    case 24:
        /* FPA status register.  */
        if (gdb_has_xml) {
            return 0;
        }
        return gdb_get_reg32(mem_buf, 0);
    case 25:
        /* CPSR, or XPSR for M-profile */
        if (arm_feature(env, ARM_FEATURE_M)) {
            return gdb_get_reg32(mem_buf, xpsr_read(env));
        } else {
            return gdb_get_reg32(mem_buf, cpsr_read(env));
        }
    }
    /* Unknown register.  */
    return 0;
}

int arm_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    uint32_t tmp;

    tmp = ldl_p(mem_buf);

    /* Mask out low bit of PC to workaround gdb bugs.  This will probably
       cause problems if we ever implement the Jazelle DBX extensions.  */
    if (n == 15) {
        tmp &= ~1;
    }

    if (n < 16) {
        /* Core integer register.  */
        env->regs[n] = tmp;
        return 4;
    }
    if (n < 24) { /* 16-23 */
        /* FPA registers (ignored).  */
        if (gdb_has_xml) {
            return 0;
        }
        return 12;
    }
    switch (n) {
    case 24:
        /* FPA status register (ignored).  */
        if (gdb_has_xml) {
            return 0;
        }
        return 4;
    case 25:
        /* CPSR, or XPSR for M-profile */
        if (arm_feature(env, ARM_FEATURE_M)) {
            /*
             * Don't allow writing to XPSR.Exception as it can cause
             * a transition into or out of handler mode (it's not
             * writeable via the MSR insn so this is a reasonable
             * restriction). Other fields are safe to update.
             */
            xpsr_write(env, tmp, ~XPSR_EXCP);
        } else {
            cpsr_write(env, tmp, 0xffffffff, CPSRWriteByGDBStub);
        }
        return 4;
    }
    /* Unknown register.  */
    return 0;
}

static void arm_gen_one_xml_sysreg_tag(GString *s, DynamicGDBXMLInfo *dyn_xml,
                                       ARMCPRegInfo *ri, uint32_t ri_key,
                                       int bitsize, int regnum)
{
    g_string_append_printf(s, "<reg name=\"%s\"", ri->name);
    g_string_append_printf(s, " bitsize=\"%d\"", bitsize);
    g_string_append_printf(s, " regnum=\"%d\"", regnum);
    g_string_append_printf(s, " group=\"cp_regs\"/>");
    dyn_xml->data.cpregs.keys[dyn_xml->num] = ri_key;
    dyn_xml->num++;
}

static void arm_register_sysreg_for_xml(gpointer key, gpointer value,
                                        gpointer p)
{
    uint32_t ri_key = *(uint32_t *)key;
    ARMCPRegInfo *ri = value;
    RegisterSysregXmlParam *param = (RegisterSysregXmlParam *)p;
    GString *s = param->s;
    ARMCPU *cpu = ARM_CPU(param->cs);
    CPUARMState *env = &cpu->env;
    DynamicGDBXMLInfo *dyn_xml = &cpu->dyn_sysreg_xml;

    if (!(ri->type & (ARM_CP_NO_RAW | ARM_CP_NO_GDB))) {
        if (arm_feature(env, ARM_FEATURE_AARCH64)) {
            if (ri->state == ARM_CP_STATE_AA64) {
                arm_gen_one_xml_sysreg_tag(s , dyn_xml, ri, ri_key, 64,
                                           param->n++);
            }
        } else {
            if (ri->state == ARM_CP_STATE_AA32) {
                if (!arm_feature(env, ARM_FEATURE_EL3) &&
                    (ri->secure & ARM_CP_SECSTATE_S)) {
                    return;
                }
                if (ri->type & ARM_CP_64BIT) {
                    arm_gen_one_xml_sysreg_tag(s , dyn_xml, ri, ri_key, 64,
                                               param->n++);
                } else {
                    arm_gen_one_xml_sysreg_tag(s , dyn_xml, ri, ri_key, 32,
                                               param->n++);
                }
            }
        }
    }
}

int arm_gen_dynamic_sysreg_xml(CPUState *cs, int base_reg)
{
    ARMCPU *cpu = ARM_CPU(cs);
    GString *s = g_string_new(NULL);
    RegisterSysregXmlParam param = {cs, s, base_reg};

    cpu->dyn_sysreg_xml.num = 0;
    cpu->dyn_sysreg_xml.data.cpregs.keys = g_new(uint32_t, g_hash_table_size(cpu->cp_regs));
    g_string_printf(s, "<?xml version=\"1.0\"?>");
    g_string_append_printf(s, "<!DOCTYPE target SYSTEM \"gdb-target.dtd\">");
    g_string_append_printf(s, "<feature name=\"org.qemu.gdb.arm.sys.regs\">");
    g_hash_table_foreach(cpu->cp_regs, arm_register_sysreg_for_xml, &param);
    g_string_append_printf(s, "</feature>");
    cpu->dyn_sysreg_xml.desc = g_string_free(s, false);
    return cpu->dyn_sysreg_xml.num;
}

struct TypeSize {
    const char *gdb_type;
    int  size;
    const char sz, suffix;
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


int arm_gen_dynamic_svereg_xml(CPUState *cs, int base_reg)
{
    ARMCPU *cpu = ARM_CPU(cs);
    GString *s = g_string_new(NULL);
    DynamicGDBXMLInfo *info = &cpu->dyn_svereg_xml;
    g_autoptr(GString) ts = g_string_new("");
    int i, j, bits, reg_width = (cpu->sve_max_vq * 128);
    info->num = 0;
    g_string_printf(s, "<?xml version=\"1.0\"?>");
    g_string_append_printf(s, "<!DOCTYPE target SYSTEM \"gdb-target.dtd\">");
    g_string_append_printf(s, "<feature name=\"org.gnu.gdb.aarch64.sve\">");

    /* First define types and totals in a whole VL */
    for (i = 0; i < ARRAY_SIZE(vec_lanes); i++) {
        int count = reg_width / vec_lanes[i].size;
        g_string_printf(ts, "svev%c%c", vec_lanes[i].sz, vec_lanes[i].suffix);
        g_string_append_printf(s,
                               "<vector id=\"%s\" type=\"%s\" count=\"%d\"/>",
                               ts->str, vec_lanes[i].gdb_type, count);
    }
    /*
     * Now define a union for each size group containing unsigned and
     * signed and potentially float versions of each size from 128 to
     * 8 bits.
     */
    for (bits = 128, i = 0; bits >= 8; bits /= 2, i++) {
        const char suf[] = { 'q', 'd', 's', 'h', 'b' };
        g_string_append_printf(s, "<union id=\"svevn%c\">", suf[i]);
        for (j = 0; j < ARRAY_SIZE(vec_lanes); j++) {
            if (vec_lanes[j].size == bits) {
                g_string_append_printf(s, "<field name=\"%c\" type=\"svev%c%c\"/>",
                                       vec_lanes[j].suffix,
                                       vec_lanes[j].sz, vec_lanes[j].suffix);
            }
        }
        g_string_append(s, "</union>");
    }
    /* And now the final union of unions */
    g_string_append(s, "<union id=\"svev\">");
    for (bits = 128, i = 0; bits >= 8; bits /= 2, i++) {
        const char suf[] = { 'q', 'd', 's', 'h', 'b' };
        g_string_append_printf(s, "<field name=\"%c\" type=\"svevn%c\"/>",
                               suf[i], suf[i]);
    }
    g_string_append(s, "</union>");

    /* Finally the sve prefix type */
    g_string_append_printf(s,
                           "<vector id=\"svep\" type=\"uint8\" count=\"%d\"/>",
                           reg_width / 8);

    /* Then define each register in parts for each vq */
    for (i = 0; i < 32; i++) {
        g_string_append_printf(s,
                               "<reg name=\"z%d\" bitsize=\"%d\""
                               " regnum=\"%d\" type=\"svev\"/>",
                               i, reg_width, base_reg++);
        info->num++;
    }
    /* fpscr & status registers */
    g_string_append_printf(s, "<reg name=\"fpsr\" bitsize=\"32\""
                           " regnum=\"%d\" group=\"float\""
                           " type=\"int\"/>", base_reg++);
    g_string_append_printf(s, "<reg name=\"fpcr\" bitsize=\"32\""
                           " regnum=\"%d\" group=\"float\""
                           " type=\"int\"/>", base_reg++);
    info->num += 2;

    for (i = 0; i < 16; i++) {
        g_string_append_printf(s,
                               "<reg name=\"p%d\" bitsize=\"%d\""
                               " regnum=\"%d\" type=\"svep\"/>",
                               i, cpu->sve_max_vq * 16, base_reg++);
        info->num++;
    }
    g_string_append_printf(s,
                           "<reg name=\"ffr\" bitsize=\"%d\""
                           " regnum=\"%d\" group=\"vector\""
                           " type=\"svep\"/>",
                           cpu->sve_max_vq * 16, base_reg++);
    g_string_append_printf(s,
                           "<reg name=\"vg\" bitsize=\"64\""
                           " regnum=\"%d\" type=\"int\"/>",
                           base_reg++);
    info->num += 2;
    g_string_append_printf(s, "</feature>");
    cpu->dyn_svereg_xml.desc = g_string_free(s, false);

    return cpu->dyn_svereg_xml.num;
}


const char *arm_gdb_get_dynamic_xml(CPUState *cs, const char *xmlname)
{
    ARMCPU *cpu = ARM_CPU(cs);

    if (strcmp(xmlname, "system-registers.xml") == 0) {
        return cpu->dyn_sysreg_xml.desc;
    } else if (strcmp(xmlname, "sve-registers.xml") == 0) {
        return cpu->dyn_svereg_xml.desc;
    }
    return NULL;
}
