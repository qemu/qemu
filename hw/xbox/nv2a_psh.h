/*
 * QEMU Geforce NV2A pixel shader translation
 *
 * Copyright (c) 2013 espes
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#ifndef HW_NV2A_PSH_H
#define HW_NV2A_PSH_H

#include "qapi/qmp/qstring.h"

QString *psh_translate(uint32_t combiner_control, uint32_t shader_stage_program,
                       uint32_t other_stage_input,
                       uint32_t rgb_inputs[8], uint32_t rgb_outputs[8],
                       uint32_t alpha_inputs[8], uint32_t alpha_outputs[8],
                       /*uint32_t constant_0[8], uint32_t constant_1[8],*/
                       uint32_t final_inputs_0, uint32_t final_inputs_1,
                       /*uint32_t final_constant_0, uint32_t final_constant_1,*/
                       bool rect_tex[4]);

#endif