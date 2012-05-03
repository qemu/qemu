/*
 * QEMU Alpha CPU
 *
 * Copyright (c) 2007 Jocelyn Mayer
 * Copyright (c) 2012 SUSE LINUX Products GmbH
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
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#include "cpu.h"
#include "qemu-common.h"


static void alpha_cpu_initfn(Object *obj)
{
    AlphaCPU *cpu = ALPHA_CPU(obj);
    CPUAlphaState *env = &cpu->env;

    cpu_exec_init(env);
    tlb_flush(env, 1);

#if defined(CONFIG_USER_ONLY)
    env->ps = PS_USER_MODE;
    cpu_alpha_store_fpcr(env, (FPCR_INVD | FPCR_DZED | FPCR_OVFD
                               | FPCR_UNFD | FPCR_INED | FPCR_DNOD
                               | FPCR_DYN_NORMAL));
#endif
    env->lock_addr = -1;
    env->fen = 1;
}

static const TypeInfo alpha_cpu_type_info = {
    .name = TYPE_ALPHA_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(AlphaCPU),
    .instance_init = alpha_cpu_initfn,
    .abstract = false,
    .class_size = sizeof(AlphaCPUClass),
};

static void alpha_cpu_register_types(void)
{
    type_register_static(&alpha_cpu_type_info);
}

type_init(alpha_cpu_register_types)
