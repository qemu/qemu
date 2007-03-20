/*
 *  MIPS emulation for qemu: CPU initialisation routines.
 *
 *  Copyright (c) 2004-2005 Jocelyn Mayer
 *  Copyright (c) 2007 Herve Poussineau
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

struct mips_def_t {
    const unsigned char *name;
    int32_t CP0_PRid;
    int32_t CP0_Config0;
    int32_t CP0_Config1;
};

/*****************************************************************************/
/* MIPS CPU definitions */
static mips_def_t mips_defs[] =
{
#ifndef MIPS_HAS_MIPS64
    {
        .name = "4Kc",
        .CP0_PRid = 0x00018000,
        .CP0_Config0 = MIPS_CONFIG0,
        .CP0_Config1 = MIPS_CONFIG1,
    },
    {
        .name = "4KEc",
        .CP0_PRid = 0x00018400,
        .CP0_Config0 = MIPS_CONFIG0 | (0x1 << CP0C0_AR),
        .CP0_Config1 = MIPS_CONFIG1,
    },
    {
        .name = "4KEcR1",
        .CP0_PRid = 0x00018448,
        .CP0_Config0 = MIPS_CONFIG0,
        .CP0_Config1 = MIPS_CONFIG1,
    },
    {
        .name = "24Kf",
        .CP0_PRid = 0x00019300,
        .CP0_Config0 = MIPS_CONFIG0 | (0x1 << CP0C0_AR),
        .CP0_Config1 = MIPS_CONFIG1 | (1 << CP0C1_FP),
    },
#else
    {
        .name = "R4000",
        .CP0_PRid = 0x00000400,
        .CP0_Config0 = MIPS_CONFIG0 | (0x2 << CP0C0_AT),
        .CP0_Config1 = MIPS_CONFIG1 | (1 << CP0C1_FP),
    },
#endif
};

int mips_find_by_name (const unsigned char *name, mips_def_t **def)
{
    int i, ret;

    ret = -1;
    *def = NULL;
    for (i = 0; i < sizeof(mips_defs) / sizeof(mips_defs[0]); i++) {
        if (strcasecmp(name, mips_defs[i].name) == 0) {
            *def = &mips_defs[i];
            ret = 0;
            break;
        }
    }

    return ret;
}

void mips_cpu_list (FILE *f, int (*cpu_fprintf)(FILE *f, const char *fmt, ...))
{
    int i;

    for (i = 0; i < sizeof(mips_defs) / sizeof(mips_defs[0]); i++) {
        (*cpu_fprintf)(f, "MIPS '%s'\n",
                       mips_defs[i].name);
    }
}

int cpu_mips_register (CPUMIPSState *env, mips_def_t *def)
{
    if (!def)
        cpu_abort(env, "Unable to find MIPS CPU definition\n");
    env->CP0_PRid = def->CP0_PRid;
    env->CP0_Config0 = def->CP0_Config0;
    env->CP0_Config1 = def->CP0_Config1;
    return 0;
}
