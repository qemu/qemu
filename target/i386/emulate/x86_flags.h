/////////////////////////////////////////////////////////////////////////
//
//  Copyright (C) 2001-2012  The Bochs Project
//  Copyright (C) 2017 Google Inc.
//
//  This library is free software; you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public
//  License as published by the Free Software Foundation; either
//  version 2.1 of the License, or (at your option) any later version.
//
//  This library is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//  Lesser General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public
//  License along with this library; if not, see
//  <https://www.gnu.org/licenses/>.
/////////////////////////////////////////////////////////////////////////
/*
 * x86 eflags functions
 */

#ifndef X86_EMU_FLAGS_H
#define X86_EMU_FLAGS_H

#include "cpu.h"
void lflags_to_rflags(CPUX86State *env);
void rflags_to_lflags(CPUX86State *env);

bool get_CF(CPUX86State *env);
void set_CF(CPUX86State *env, bool val);

void SET_FLAGS_OxxxxC(CPUX86State *env, bool new_of, bool new_cf);

void SET_FLAGS_OSZAPC_SUB32(CPUX86State *env, uint32_t v1, uint32_t v2,
                            uint32_t diff);
void SET_FLAGS_OSZAPC_SUB16(CPUX86State *env, uint16_t v1, uint16_t v2,
                            uint16_t diff);
void SET_FLAGS_OSZAPC_SUB8(CPUX86State *env, uint8_t v1, uint8_t v2,
                           uint8_t diff);

void SET_FLAGS_OSZAPC_ADD32(CPUX86State *env, uint32_t v1, uint32_t v2,
                            uint32_t diff);
void SET_FLAGS_OSZAPC_ADD16(CPUX86State *env, uint16_t v1, uint16_t v2,
                            uint16_t diff);
void SET_FLAGS_OSZAPC_ADD8(CPUX86State *env, uint8_t v1, uint8_t v2,
                           uint8_t diff);

void SET_FLAGS_OSZAP_SUB32(CPUX86State *env, uint32_t v1, uint32_t v2,
                           uint32_t diff);
void SET_FLAGS_OSZAP_SUB16(CPUX86State *env, uint16_t v1, uint16_t v2,
                           uint16_t diff);
void SET_FLAGS_OSZAP_SUB8(CPUX86State *env, uint8_t v1, uint8_t v2,
                          uint8_t diff);

void SET_FLAGS_OSZAP_ADD32(CPUX86State *env, uint32_t v1, uint32_t v2,
                           uint32_t diff);
void SET_FLAGS_OSZAP_ADD16(CPUX86State *env, uint16_t v1, uint16_t v2,
                           uint16_t diff);
void SET_FLAGS_OSZAP_ADD8(CPUX86State *env, uint8_t v1, uint8_t v2,
                          uint8_t diff);

void SET_FLAGS_OSZAPC_LOGIC32(CPUX86State *env, uint32_t v1, uint32_t v2,
                              uint32_t diff);
void SET_FLAGS_OSZAPC_LOGIC16(CPUX86State *env, uint16_t v1, uint16_t v2,
                              uint16_t diff);
void SET_FLAGS_OSZAPC_LOGIC8(CPUX86State *env, uint8_t v1, uint8_t v2,
                             uint8_t diff);

#endif /* X86_EMU_FLAGS_H */
