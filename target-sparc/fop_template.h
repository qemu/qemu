/*
 *  SPARC micro operations (templates for various register related
 *  operations)
 *
 *  Copyright (c) 2003 Fabrice Bellard
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/* floating point registers moves */
void OPPROTO glue(op_load_fpr_FT0_fpr, REGNAME)(void)
{
    FT0 = REG;
}

void OPPROTO glue(op_store_FT0_fpr_fpr, REGNAME)(void)
{
    REG = FT0;
}

void OPPROTO glue(op_load_fpr_FT1_fpr, REGNAME)(void)
{
    FT1 = REG;
}

void OPPROTO glue(op_store_FT1_fpr_fpr, REGNAME)(void)
{
    REG = FT1;
}

/* double floating point registers moves */
void OPPROTO glue(op_load_fpr_DT0_fpr, REGNAME)(void)
{
    CPU_DoubleU u;
    uint32_t *p = (uint32_t *)&REG;
    u.l.lower = *(p +1);
    u.l.upper = *p;
    DT0 = u.d;
}

void OPPROTO glue(op_store_DT0_fpr_fpr, REGNAME)(void)
{
    CPU_DoubleU u;
    uint32_t *p = (uint32_t *)&REG;
    u.d = DT0;
    *(p +1) = u.l.lower;
    *p = u.l.upper;
}

void OPPROTO glue(op_load_fpr_DT1_fpr, REGNAME)(void)
{
    CPU_DoubleU u;
    uint32_t *p = (uint32_t *)&REG;
    u.l.lower = *(p +1);
    u.l.upper = *p;
    DT1 = u.d;
}

void OPPROTO glue(op_store_DT1_fpr_fpr, REGNAME)(void)
{
    CPU_DoubleU u;
    uint32_t *p = (uint32_t *)&REG;
    u.d = DT1;
    *(p +1) = u.l.lower;
    *p = u.l.upper;
}

#if defined(CONFIG_USER_ONLY)
/* quad floating point registers moves */
void OPPROTO glue(op_load_fpr_QT0_fpr, REGNAME)(void)
{
    CPU_QuadU u;
    uint32_t *p = (uint32_t *)&REG;
    u.l.lowest = *(p + 3);
    u.l.lower = *(p + 2);
    u.l.upper = *(p + 1);
    u.l.upmost = *p;
    QT0 = u.q;
}

void OPPROTO glue(op_store_QT0_fpr_fpr, REGNAME)(void)
{
    CPU_QuadU u;
    uint32_t *p = (uint32_t *)&REG;
    u.q = QT0;
    *(p + 3) = u.l.lowest;
    *(p + 2) = u.l.lower;
    *(p + 1) = u.l.upper;
    *p = u.l.upmost;
}

void OPPROTO glue(op_load_fpr_QT1_fpr, REGNAME)(void)
{
    CPU_QuadU u;
    uint32_t *p = (uint32_t *)&REG;
    u.l.lowest = *(p + 3);
    u.l.lower = *(p + 2);
    u.l.upper = *(p + 1);
    u.l.upmost = *p;
    QT1 = u.q;
}

void OPPROTO glue(op_store_QT1_fpr_fpr, REGNAME)(void)
{
    CPU_QuadU u;
    uint32_t *p = (uint32_t *)&REG;
    u.q = QT1;
    *(p + 3) = u.l.lowest;
    *(p + 2) = u.l.lower;
    *(p + 1) = u.l.upper;
    *p = u.l.upmost;
}
#endif

#undef REG
#undef REGNAME
