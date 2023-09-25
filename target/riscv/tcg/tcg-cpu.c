/*
 * riscv TCG cpu class initialization
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2017-2018 SiFive, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "exec/exec-all.h"
#include "cpu.h"
#include "pmu.h"
#include "time_helper.h"
#include "qapi/error.h"
#include "qemu/accel.h"
#include "hw/core/accel-cpu.h"


static void riscv_cpu_validate_misa_priv(CPURISCVState *env, Error **errp)
{
    if (riscv_has_ext(env, RVH) && env->priv_ver < PRIV_VERSION_1_12_0) {
        error_setg(errp, "H extension requires priv spec 1.12.0");
        return;
    }
}

static void riscv_cpu_validate_misa_mxl(RISCVCPU *cpu, Error **errp)
{
    RISCVCPUClass *mcc = RISCV_CPU_GET_CLASS(cpu);
    CPUClass *cc = CPU_CLASS(mcc);
    CPURISCVState *env = &cpu->env;

    /* Validate that MISA_MXL is set properly. */
    switch (env->misa_mxl_max) {
#ifdef TARGET_RISCV64
    case MXL_RV64:
    case MXL_RV128:
        cc->gdb_core_xml_file = "riscv-64bit-cpu.xml";
        break;
#endif
    case MXL_RV32:
        cc->gdb_core_xml_file = "riscv-32bit-cpu.xml";
        break;
    default:
        g_assert_not_reached();
    }

    if (env->misa_mxl_max != env->misa_mxl) {
        error_setg(errp, "misa_mxl_max must be equal to misa_mxl");
        return;
    }
}

static void riscv_cpu_validate_priv_spec(RISCVCPU *cpu, Error **errp)
{
    CPURISCVState *env = &cpu->env;
    int priv_version = -1;

    if (cpu->cfg.priv_spec) {
        if (!g_strcmp0(cpu->cfg.priv_spec, "v1.12.0")) {
            priv_version = PRIV_VERSION_1_12_0;
        } else if (!g_strcmp0(cpu->cfg.priv_spec, "v1.11.0")) {
            priv_version = PRIV_VERSION_1_11_0;
        } else if (!g_strcmp0(cpu->cfg.priv_spec, "v1.10.0")) {
            priv_version = PRIV_VERSION_1_10_0;
        } else {
            error_setg(errp,
                       "Unsupported privilege spec version '%s'",
                       cpu->cfg.priv_spec);
            return;
        }

        env->priv_ver = priv_version;
    }
}

/*
 * We'll get here via the following path:
 *
 * riscv_cpu_realize()
 *   -> cpu_exec_realizefn()
 *      -> tcg_cpu_realize() (via accel_cpu_common_realize())
 */
static bool tcg_cpu_realize(CPUState *cs, Error **errp)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    Error *local_err = NULL;

    if (object_dynamic_cast(OBJECT(cpu), TYPE_RISCV_CPU_HOST)) {
        error_setg(errp, "'host' CPU is not compatible with TCG acceleration");
        return false;
    }

    riscv_cpu_validate_misa_mxl(cpu, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return false;
    }

    riscv_cpu_validate_priv_spec(cpu, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return false;
    }

    riscv_cpu_validate_misa_priv(env, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return false;
    }

    if (cpu->cfg.epmp && !cpu->cfg.pmp) {
        /*
         * Enhanced PMP should only be available
         * on harts with PMP support
         */
        error_setg(errp, "Invalid configuration: EPMP requires PMP support");
        return false;
    }

    riscv_cpu_validate_set_extensions(cpu, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return false;
    }

#ifndef CONFIG_USER_ONLY
    CPU(cs)->tcg_cflags |= CF_PCREL;

    if (cpu->cfg.ext_sstc) {
        riscv_timer_init(cpu);
    }

    if (cpu->cfg.pmu_num) {
        if (!riscv_pmu_init(cpu, cpu->cfg.pmu_num) && cpu->cfg.ext_sscofpmf) {
            cpu->pmu_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                          riscv_pmu_timer_cb, cpu);
        }
     }
#endif

    return true;
}

static void tcg_cpu_init_ops(AccelCPUClass *accel_cpu, CPUClass *cc)
{
    /*
     * All cpus use the same set of operations.
     * riscv_tcg_ops is being imported from cpu.c for now.
     */
    cc->tcg_ops = &riscv_tcg_ops;
}

static void tcg_cpu_class_init(CPUClass *cc)
{
    cc->init_accel_cpu = tcg_cpu_init_ops;
}

static void tcg_cpu_accel_class_init(ObjectClass *oc, void *data)
{
    AccelCPUClass *acc = ACCEL_CPU_CLASS(oc);

    acc->cpu_class_init = tcg_cpu_class_init;
    acc->cpu_target_realize = tcg_cpu_realize;
}

static const TypeInfo tcg_cpu_accel_type_info = {
    .name = ACCEL_CPU_NAME("tcg"),

    .parent = TYPE_ACCEL_CPU,
    .class_init = tcg_cpu_accel_class_init,
    .abstract = true,
};

static void tcg_cpu_accel_register_types(void)
{
    type_register_static(&tcg_cpu_accel_type_info);
}
type_init(tcg_cpu_accel_register_types);
