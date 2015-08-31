/*
 * QEMU Geforce NV2A shader generator
 *
 * Copyright (c) 2015 espes
 * Copyright (c) 2015 Jannik Vogel
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_NV2A_SHADERS_H
#define HW_NV2A_SHADERS_H

#include "qapi/qmp/qstring.h"
#include "gl/gloffscreen.h"

#include "nv2a_vsh.h"
#include "nv2a_psh.h"

#define NV2A_MAX_TRANSFORM_PROGRAM_LENGTH 136
#define NV2A_VERTEXSHADER_CONSTANTS 192
#define NV2A_MAX_LIGHTS 8

enum ShaderPrimitiveMode {
    PRIM_TYPE_NONE,
    PRIM_TYPE_POINTS,
    PRIM_TYPE_LINES,
    PRIM_TYPE_LINE_LOOP,
    PRIM_TYPE_LINE_STRIP,
    PRIM_TYPE_TRIANGLES,
    PRIM_TYPE_TRIANGLE_STRIP,
    PRIM_TYPE_TRIANGLE_FAN,
    PRIM_TYPE_QUADS,
    PRIM_TYPE_QUAD_STRIP,
    PRIM_TYPE_POLYGON,
};

typedef struct ShaderState {
    /* fragment shader - register combiner stuff */
    uint32_t combiner_control;
    uint32_t shader_stage_program;
    uint32_t other_stage_input;
    uint32_t final_inputs_0;
    uint32_t final_inputs_1;

    uint32_t rgb_inputs[8], rgb_outputs[8];
    uint32_t alpha_inputs[8], alpha_outputs[8];

    bool rect_tex[4];
    bool compare_mode[4][4];
    bool alphakill[4];

    bool alpha_test;
    enum PshAlphaFunc alpha_func;

    bool texture_matrix_enable[4];
    enum VshTexgen texgen[4][4];

    bool fog_enable;
    enum VshFoggen foggen;
    enum VshFogMode fog_mode;

    enum VshSkinning skinning;

    bool normalization;

    bool lighting;
    enum VshLight light[NV2A_MAX_LIGHTS];

    bool fixed_function;

    /* vertex program */
    bool vertex_program;
    uint32_t program_data[NV2A_MAX_TRANSFORM_PROGRAM_LENGTH][VSH_TOKEN_SIZE];
    int program_length;

    /* primitive format for geometry shader */
    enum ShaderPrimitiveMode primitive_mode;
} ShaderState;

typedef struct ShaderBinding {
    GLuint gl_program;
    GLint psh_constant_loc[9][2];
    GLint gl_constants_loc;
} ShaderBinding;

ShaderBinding* generate_shaders(const ShaderState state);

#endif
