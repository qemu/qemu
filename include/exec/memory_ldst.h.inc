/*
 *  Physical memory access templates
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *  Copyright (c) 2015 Linaro, Inc.
 *  Copyright (c) 2016 Red Hat, Inc.
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

#ifdef TARGET_ENDIANNESS
extern uint32_t glue(address_space_lduw, SUFFIX)(ARG1_DECL,
    hwaddr addr, MemTxAttrs attrs, MemTxResult *result);
extern uint32_t glue(address_space_ldl, SUFFIX)(ARG1_DECL,
    hwaddr addr, MemTxAttrs attrs, MemTxResult *result);
extern uint64_t glue(address_space_ldq, SUFFIX)(ARG1_DECL,
    hwaddr addr, MemTxAttrs attrs, MemTxResult *result);
extern void glue(address_space_stl_notdirty, SUFFIX)(ARG1_DECL,
    hwaddr addr, uint32_t val, MemTxAttrs attrs, MemTxResult *result);
extern void glue(address_space_stw, SUFFIX)(ARG1_DECL,
    hwaddr addr, uint32_t val, MemTxAttrs attrs, MemTxResult *result);
extern void glue(address_space_stl, SUFFIX)(ARG1_DECL,
    hwaddr addr, uint32_t val, MemTxAttrs attrs, MemTxResult *result);
extern void glue(address_space_stq, SUFFIX)(ARG1_DECL,
    hwaddr addr, uint64_t val, MemTxAttrs attrs, MemTxResult *result);
#else
extern uint32_t glue(address_space_ldub, SUFFIX)(ARG1_DECL,
    hwaddr addr, MemTxAttrs attrs, MemTxResult *result);
extern uint32_t glue(address_space_lduw_le, SUFFIX)(ARG1_DECL,
    hwaddr addr, MemTxAttrs attrs, MemTxResult *result);
extern uint32_t glue(address_space_lduw_be, SUFFIX)(ARG1_DECL,
    hwaddr addr, MemTxAttrs attrs, MemTxResult *result);
extern uint32_t glue(address_space_ldl_le, SUFFIX)(ARG1_DECL,
    hwaddr addr, MemTxAttrs attrs, MemTxResult *result);
extern uint32_t glue(address_space_ldl_be, SUFFIX)(ARG1_DECL,
    hwaddr addr, MemTxAttrs attrs, MemTxResult *result);
extern uint64_t glue(address_space_ldq_le, SUFFIX)(ARG1_DECL,
    hwaddr addr, MemTxAttrs attrs, MemTxResult *result);
extern uint64_t glue(address_space_ldq_be, SUFFIX)(ARG1_DECL,
    hwaddr addr, MemTxAttrs attrs, MemTxResult *result);
extern void glue(address_space_stb, SUFFIX)(ARG1_DECL,
    hwaddr addr, uint32_t val, MemTxAttrs attrs, MemTxResult *result);
extern void glue(address_space_stw_le, SUFFIX)(ARG1_DECL,
    hwaddr addr, uint32_t val, MemTxAttrs attrs, MemTxResult *result);
extern void glue(address_space_stw_be, SUFFIX)(ARG1_DECL,
    hwaddr addr, uint32_t val, MemTxAttrs attrs, MemTxResult *result);
extern void glue(address_space_stl_le, SUFFIX)(ARG1_DECL,
    hwaddr addr, uint32_t val, MemTxAttrs attrs, MemTxResult *result);
extern void glue(address_space_stl_be, SUFFIX)(ARG1_DECL,
    hwaddr addr, uint32_t val, MemTxAttrs attrs, MemTxResult *result);
extern void glue(address_space_stq_le, SUFFIX)(ARG1_DECL,
    hwaddr addr, uint64_t val, MemTxAttrs attrs, MemTxResult *result);
extern void glue(address_space_stq_be, SUFFIX)(ARG1_DECL,
    hwaddr addr, uint64_t val, MemTxAttrs attrs, MemTxResult *result);
#endif

#undef ARG1_DECL
#undef ARG1
#undef SUFFIX
#undef TARGET_ENDIANNESS
