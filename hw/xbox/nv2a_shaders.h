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

#define NV2A_LTCTXA_COUNT  26
#define NV2A_LTCTXB_COUNT  52
#define NV2A_LTC1_COUNT    20

/* vertex processing (cheops) context layout */
#define NV_IGRAPH_XF_XFCTX_CMAT0                     0x00
#define NV_IGRAPH_XF_XFCTX_PMAT0                     0x04
#define NV_IGRAPH_XF_XFCTX_MMAT0                     0x08
#define NV_IGRAPH_XF_XFCTX_IMMAT0                    0x0c
#define NV_IGRAPH_XF_XFCTX_MMAT1                     0x10
#define NV_IGRAPH_XF_XFCTX_IMMAT1                    0x14
#define NV_IGRAPH_XF_XFCTX_MMAT2                     0x18
#define NV_IGRAPH_XF_XFCTX_IMMAT2                    0x1c
#define NV_IGRAPH_XF_XFCTX_MMAT3                     0x20
#define NV_IGRAPH_XF_XFCTX_IMMAT3                    0x24
#define NV_IGRAPH_XF_XFCTX_LIT0                      0x28
#define NV_IGRAPH_XF_XFCTX_LIT1                      0x29
#define NV_IGRAPH_XF_XFCTX_LIT2                      0x2a
#define NV_IGRAPH_XF_XFCTX_LIT3                      0x2b
#define NV_IGRAPH_XF_XFCTX_LIT4                      0x2c
#define NV_IGRAPH_XF_XFCTX_LIT5                      0x2d
#define NV_IGRAPH_XF_XFCTX_LIT6                      0x2e
#define NV_IGRAPH_XF_XFCTX_LIT7                      0x2f
#define NV_IGRAPH_XF_XFCTX_SPOT0                     0x30
#define NV_IGRAPH_XF_XFCTX_SPOT1                     0x31
#define NV_IGRAPH_XF_XFCTX_SPOT2                     0x32
#define NV_IGRAPH_XF_XFCTX_SPOT3                     0x33
#define NV_IGRAPH_XF_XFCTX_SPOT4                     0x34
#define NV_IGRAPH_XF_XFCTX_SPOT5                     0x35
#define NV_IGRAPH_XF_XFCTX_SPOT6                     0x36
#define NV_IGRAPH_XF_XFCTX_SPOT7                     0x37
#define NV_IGRAPH_XF_XFCTX_EYEP                      0x38
#define NV_IGRAPH_XF_XFCTX_FOG                       0x39
#define NV_IGRAPH_XF_XFCTX_VPSCL                     0x3a
#define NV_IGRAPH_XF_XFCTX_VPOFF                     0x3b
#define NV_IGRAPH_XF_XFCTX_CONS0                     0x3c
#define NV_IGRAPH_XF_XFCTX_CONS1                     0x3d
#define NV_IGRAPH_XF_XFCTX_CONS2                     0x3e
#define NV_IGRAPH_XF_XFCTX_CONS3                     0x3f
#define NV_IGRAPH_XF_XFCTX_TG0MAT                    0x40
#define NV_IGRAPH_XF_XFCTX_T0MAT                     0x44
#define NV_IGRAPH_XF_XFCTX_TG1MAT                    0x48
#define NV_IGRAPH_XF_XFCTX_T1MAT                     0x4c
#define NV_IGRAPH_XF_XFCTX_TG2MAT                    0x50
#define NV_IGRAPH_XF_XFCTX_T2MAT                     0x54
#define NV_IGRAPH_XF_XFCTX_TG3MAT                    0x58
#define NV_IGRAPH_XF_XFCTX_T3MAT                     0x5c
#define NV_IGRAPH_XF_XFCTX_PRSPACE                   0x60

/* lighting (zoser) context layout */
#define NV_IGRAPH_XF_LTCTXA_L0_K                     0x00
#define NV_IGRAPH_XF_LTCTXA_L0_SPT                   0x01
#define NV_IGRAPH_XF_LTCTXA_L1_K                     0x02
#define NV_IGRAPH_XF_LTCTXA_L1_SPT                   0x03
#define NV_IGRAPH_XF_LTCTXA_L2_K                     0x04
#define NV_IGRAPH_XF_LTCTXA_L2_SPT                   0x05
#define NV_IGRAPH_XF_LTCTXA_L3_K                     0x06
#define NV_IGRAPH_XF_LTCTXA_L3_SPT                   0x07
#define NV_IGRAPH_XF_LTCTXA_L4_K                     0x08
#define NV_IGRAPH_XF_LTCTXA_L4_SPT                   0x09
#define NV_IGRAPH_XF_LTCTXA_L5_K                     0x0a
#define NV_IGRAPH_XF_LTCTXA_L5_SPT                   0x0b
#define NV_IGRAPH_XF_LTCTXA_L6_K                     0x0c
#define NV_IGRAPH_XF_LTCTXA_L6_SPT                   0x0d
#define NV_IGRAPH_XF_LTCTXA_L7_K                     0x0e
#define NV_IGRAPH_XF_LTCTXA_L7_SPT                   0x0f
#define NV_IGRAPH_XF_LTCTXA_EYED                     0x10
#define NV_IGRAPH_XF_LTCTXA_FR_AMB                   0x11
#define NV_IGRAPH_XF_LTCTXA_BR_AMB                   0x12
#define NV_IGRAPH_XF_LTCTXA_CM_COL                   0x13
#define NV_IGRAPH_XF_LTCTXA_BCM_COL                  0x14
#define NV_IGRAPH_XF_LTCTXA_FOG_K                    0x15
#define NV_IGRAPH_XF_LTCTXA_ZERO                     0x16
#define NV_IGRAPH_XF_LTCTXA_PT0                      0x17
#define NV_IGRAPH_XF_LTCTXA_FOGLIN                   0x18

#define NV_IGRAPH_XF_LTCTXB_L0_AMB                   0x00
#define NV_IGRAPH_XF_LTCTXB_L0_DIF                   0x01
#define NV_IGRAPH_XF_LTCTXB_L0_SPC                   0x02
#define NV_IGRAPH_XF_LTCTXB_L0_BAMB                  0x03
#define NV_IGRAPH_XF_LTCTXB_L0_BDIF                  0x04
#define NV_IGRAPH_XF_LTCTXB_L0_BSPC                  0x05
#define NV_IGRAPH_XF_LTCTXB_L1_AMB                   0x06
#define NV_IGRAPH_XF_LTCTXB_L1_DIF                   0x07
#define NV_IGRAPH_XF_LTCTXB_L1_SPC                   0x08
#define NV_IGRAPH_XF_LTCTXB_L1_BAMB                  0x09
#define NV_IGRAPH_XF_LTCTXB_L1_BDIF                  0x0a
#define NV_IGRAPH_XF_LTCTXB_L1_BSPC                  0x0b
#define NV_IGRAPH_XF_LTCTXB_L2_AMB                   0x0c
#define NV_IGRAPH_XF_LTCTXB_L2_DIF                   0x0d
#define NV_IGRAPH_XF_LTCTXB_L2_SPC                   0x0e
#define NV_IGRAPH_XF_LTCTXB_L2_BAMB                  0x0f
#define NV_IGRAPH_XF_LTCTXB_L2_BDIF                  0x10
#define NV_IGRAPH_XF_LTCTXB_L2_BSPC                  0x11
#define NV_IGRAPH_XF_LTCTXB_L3_AMB                   0x12
#define NV_IGRAPH_XF_LTCTXB_L3_DIF                   0x13
#define NV_IGRAPH_XF_LTCTXB_L3_SPC                   0x14
#define NV_IGRAPH_XF_LTCTXB_L3_BAMB                  0x15
#define NV_IGRAPH_XF_LTCTXB_L3_BDIF                  0x16
#define NV_IGRAPH_XF_LTCTXB_L3_BSPC                  0x17
#define NV_IGRAPH_XF_LTCTXB_L4_AMB                   0x18
#define NV_IGRAPH_XF_LTCTXB_L4_DIF                   0x19
#define NV_IGRAPH_XF_LTCTXB_L4_SPC                   0x1a
#define NV_IGRAPH_XF_LTCTXB_L4_BAMB                  0x1b
#define NV_IGRAPH_XF_LTCTXB_L4_BDIF                  0x1c
#define NV_IGRAPH_XF_LTCTXB_L4_BSPC                  0x1d
#define NV_IGRAPH_XF_LTCTXB_L5_AMB                   0x1e
#define NV_IGRAPH_XF_LTCTXB_L5_DIF                   0x1f
#define NV_IGRAPH_XF_LTCTXB_L5_SPC                   0x20
#define NV_IGRAPH_XF_LTCTXB_L5_BAMB                  0x21
#define NV_IGRAPH_XF_LTCTXB_L5_BDIF                  0x22
#define NV_IGRAPH_XF_LTCTXB_L5_BSPC                  0x23
#define NV_IGRAPH_XF_LTCTXB_L6_AMB                   0x24
#define NV_IGRAPH_XF_LTCTXB_L6_DIF                   0x25
#define NV_IGRAPH_XF_LTCTXB_L6_SPC                   0x26
#define NV_IGRAPH_XF_LTCTXB_L6_BAMB                  0x27
#define NV_IGRAPH_XF_LTCTXB_L6_BDIF                  0x28
#define NV_IGRAPH_XF_LTCTXB_L6_BSPC                  0x29
#define NV_IGRAPH_XF_LTCTXB_L7_AMB                   0x2a
#define NV_IGRAPH_XF_LTCTXB_L7_DIF                   0x2b
#define NV_IGRAPH_XF_LTCTXB_L7_SPC                   0x2c
#define NV_IGRAPH_XF_LTCTXB_L7_BAMB                  0x2d
#define NV_IGRAPH_XF_LTCTXB_L7_BDIF                  0x2e
#define NV_IGRAPH_XF_LTCTXB_L7_BSPC                  0x2f
#define NV_IGRAPH_XF_LTCTXB_PT1                      0x30
#define NV_IGRAPH_XF_LTCTXB_ONE                      0x31
#define NV_IGRAPH_XF_LTCTXB_VPOFFSET                 0x32

#define NV_IGRAPH_XF_LTC1_ZERO1                      0x00
#define NV_IGRAPH_XF_LTC1_l0                         0x01
#define NV_IGRAPH_XF_LTC1_Bl0                        0x02
#define NV_IGRAPH_XF_LTC1_PP                         0x03
#define NV_IGRAPH_XF_LTC1_r0                         0x04
#define NV_IGRAPH_XF_LTC1_r1                         0x05
#define NV_IGRAPH_XF_LTC1_r2                         0x06
#define NV_IGRAPH_XF_LTC1_r3                         0x07
#define NV_IGRAPH_XF_LTC1_r4                         0x08
#define NV_IGRAPH_XF_LTC1_r5                         0x09
#define NV_IGRAPH_XF_LTC1_r6                         0x0a
#define NV_IGRAPH_XF_LTC1_r7                         0x0b
#define NV_IGRAPH_XF_LTC1_L0                         0x0c
#define NV_IGRAPH_XF_LTC1_L1                         0x0d
#define NV_IGRAPH_XF_LTC1_L2                         0x0e
#define NV_IGRAPH_XF_LTC1_L3                         0x0f
#define NV_IGRAPH_XF_LTC1_L4                         0x10
#define NV_IGRAPH_XF_LTC1_L5                         0x11
#define NV_IGRAPH_XF_LTC1_L6                         0x12
#define NV_IGRAPH_XF_LTC1_L7                         0x13


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

enum ShaderPolygonMode {
    POLY_MODE_FILL,
    POLY_MODE_POINT,
    POLY_MODE_LINE,
};

typedef struct ShaderState {
    PshState psh;

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
    bool z_perspective;

    /* primitive format for geometry shader */
    enum ShaderPolygonMode polygon_front_mode;
    enum ShaderPolygonMode polygon_back_mode;
    enum ShaderPrimitiveMode primitive_mode;
} ShaderState;

typedef struct ShaderBinding {
    GLuint gl_program;
    GLenum gl_primitive_mode;

    GLint psh_constant_loc[9][2];
    GLint alpha_ref_loc;

    GLint bump_mat_loc[4];
    GLint bump_scale_loc[4];
    GLint bump_offset_loc[4];

    GLint surface_size_loc;
    GLint clip_range_loc;

    GLint vsh_constant_loc[NV2A_VERTEXSHADER_CONSTANTS];

    GLint inv_viewport_loc;
    GLint ltctxa_loc[NV2A_LTCTXA_COUNT];
    GLint ltctxb_loc[NV2A_LTCTXB_COUNT];
    GLint ltc1_loc[NV2A_LTC1_COUNT];

    GLint fog_color_loc;
    GLint fog_param_loc[2];
    GLint light_infinite_half_vector_loc;
    GLint light_infinite_direction_loc;
    GLint light_local_position_loc;
    GLint light_local_attenuation_loc;

} ShaderBinding;

ShaderBinding* generate_shaders(const ShaderState state);

#endif
