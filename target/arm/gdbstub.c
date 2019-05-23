/*
 * ARM gdb server stub
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 * Copyright (c) 2013 SUSE LINUX Products GmbH
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/gdbstub.h"

typedef struct RegisterSysregXmlParam {
    CPUState *cs;
    GString *s;
} RegisterSysregXmlParam;

/* Old gdb always expect FPA registers.  Newer (xml-aware) gdb only expect
   whatever the target description contains.  Due to a historical mishap
   the FPA registers appear in between core integer regs and the CPSR.
   We hack round this by giving the FPA regs zero size when talking to a
   newer gdb.  */

int arm_cpu_gdb_read_register(CPUState *cs, uint8_t *mem_buf, int n)
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
        memset(mem_buf, 0, 12);
        return 12;
    }
    switch (n) {
    case 24:
        /* FPA status register.  */
        if (gdb_has_xml) {
            return 0;
        }
        return gdb_get_reg32(mem_buf, 0);
    case 25:
        /* CPSR */
        return gdb_get_reg32(mem_buf, cpsr_read(env));
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
        /* CPSR */
        cpsr_write(env, tmp, 0xffffffff, CPSRWriteByGDBStub);
        return 4;
    }
    /* Unknown register.  */
    return 0;
}

static void arm_gen_one_xml_reg_tag(GString *s, DynamicGDBXMLInfo *dyn_xml,
                                    ARMCPRegInfo *ri, uint32_t ri_key,
                                    int bitsize)
{
    g_string_append_printf(s, "<reg name=\"%s\"", ri->name);
    g_string_append_printf(s, " bitsize=\"%d\"", bitsize);
    g_string_append_printf(s, " group=\"cp_regs\"/>");
    dyn_xml->num_cpregs++;
    dyn_xml->cpregs_keys[dyn_xml->num_cpregs - 1] = ri_key;
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
    DynamicGDBXMLInfo *dyn_xml = &cpu->dyn_xml;

    if (!(ri->type & (ARM_CP_NO_RAW | ARM_CP_NO_GDB))) {
        if (arm_feature(env, ARM_FEATURE_AARCH64)) {
            if (ri->state == ARM_CP_STATE_AA64) {
                arm_gen_one_xml_reg_tag(s , dyn_xml, ri, ri_key, 64);
            }
        } else {
            if (ri->state == ARM_CP_STATE_AA32) {
                if (!arm_feature(env, ARM_FEATURE_EL3) &&
                    (ri->secure & ARM_CP_SECSTATE_S)) {
                    return;
                }
                if (ri->type & ARM_CP_64BIT) {
                    arm_gen_one_xml_reg_tag(s , dyn_xml, ri, ri_key, 64);
                } else {
                    arm_gen_one_xml_reg_tag(s , dyn_xml, ri, ri_key, 32);
                }
            }
        }
    }
}

int arm_gen_dynamic_xml(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);
    GString *s = g_string_new(NULL);
    RegisterSysregXmlParam param = {cs, s};

    cpu->dyn_xml.num_cpregs = 0;
    cpu->dyn_xml.cpregs_keys = g_new(uint32_t, g_hash_table_size(cpu->cp_regs));
    g_string_printf(s, "<?xml version=\"1.0\"?>");
    g_string_append_printf(s, "<!DOCTYPE target SYSTEM \"gdb-target.dtd\">");
    g_string_append_printf(s, "<feature name=\"org.qemu.gdb.arm.sys.regs\">");
    g_hash_table_foreach(cpu->cp_regs, arm_register_sysreg_for_xml, &param);
    g_string_append_printf(s, "</feature>");
    cpu->dyn_xml.desc = g_string_free(s, false);
    return cpu->dyn_xml.num_cpregs;
}

const char *arm_gdb_get_dynamic_xml(CPUState *cs, const char *xmlname)
{
    ARMCPU *cpu = ARM_CPU(cs);

    if (strcmp(xmlname, "system-registers.xml") == 0) {
        return cpu->dyn_xml.desc;
    }
    return NULL;
}
