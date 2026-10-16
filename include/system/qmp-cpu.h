/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Per-architecture implementations of CPU-related QMP commands.
 */

#ifndef SYSTEM_QMP_CPU_H
#define SYSTEM_QMP_CPU_H

#include "qapi/error.h"
#include "qapi/qapi-types-machine.h"

CpuDefinitionInfoList *qmp_query_arm_cpu_definitions(Error **errp);
CpuDefinitionInfoList *qmp_query_i386_cpu_definitions(Error **errp);
CpuDefinitionInfoList *qmp_query_loongarch_cpu_definitions(Error **errp);
CpuDefinitionInfoList *qmp_query_mips_cpu_definitions(Error **errp);
CpuDefinitionInfoList *qmp_query_ppc_cpu_definitions(Error **errp);
CpuDefinitionInfoList *qmp_query_riscv_cpu_definitions(Error **errp);
CpuDefinitionInfoList *qmp_query_s390x_cpu_definitions(Error **errp);

#endif
