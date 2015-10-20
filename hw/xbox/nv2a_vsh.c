/*
 * QEMU Geforce NV2A vertex shader translation
 *
 * Copyright (c) 2014 Jannik Vogel
 * Copyright (c) 2012 espes
 *
 * Based on:
 * Cxbx, VertexShader.cpp
 * Copyright (c) 2004 Aaron Robinson <caustik@caustik.com>
 *                    Kingofc <kingofc@freenet.de>
 * Dxbx, uPushBuffer.pas
 * Copyright (c) 2007 Shadow_tj, PatrickvL
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

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

#include "hw/xbox/nv2a_shaders_common.h"
#include "hw/xbox/nv2a_vsh.h"

#define VSH_D3DSCM_CORRECTION 96


typedef enum {
    PARAM_UNKNOWN = 0,
    PARAM_R,
    PARAM_V,
    PARAM_C
} VshParameterType;

typedef enum {
    OUTPUT_C = 0,
    OUTPUT_O
} VshOutputType;

typedef enum {
    OMUX_MAC = 0,
    OMUX_ILU
} VshOutputMux;

typedef enum {
    ILU_NOP = 0,
    ILU_MOV,
    ILU_RCP,
    ILU_RCC,
    ILU_RSQ,
    ILU_EXP,
    ILU_LOG,
    ILU_LIT
} VshILU;

typedef enum {
    MAC_NOP,
    MAC_MOV,
    MAC_MUL,
    MAC_ADD,
    MAC_MAD,
    MAC_DP3,
    MAC_DPH,
    MAC_DP4,
    MAC_DST,
    MAC_MIN,
    MAC_MAX,
    MAC_SLT,
    MAC_SGE,
    MAC_ARL
} VshMAC;

typedef enum {
    SWIZZLE_X = 0,
    SWIZZLE_Y,
    SWIZZLE_Z,
    SWIZZLE_W
} VshSwizzle;


typedef struct VshFieldMapping {
    VshFieldName field_name;
    uint8_t subtoken;
    uint8_t start_bit;
    uint8_t bit_length;
} VshFieldMapping;

static const VshFieldMapping field_mapping[] = {
    // Field Name         DWORD BitPos BitSize
    {  FLD_ILU,              1,   25,     3 },
    {  FLD_MAC,              1,   21,     4 },
    {  FLD_CONST,            1,   13,     8 },
    {  FLD_V,                1,    9,     4 },
    // INPUT A
    {  FLD_A_NEG,            1,    8,     1 },
    {  FLD_A_SWZ_X,          1,    6,     2 },
    {  FLD_A_SWZ_Y,          1,    4,     2 },
    {  FLD_A_SWZ_Z,          1,    2,     2 },
    {  FLD_A_SWZ_W,          1,    0,     2 },
    {  FLD_A_R,              2,   28,     4 },
    {  FLD_A_MUX,            2,   26,     2 },
    // INPUT B
    {  FLD_B_NEG,            2,   25,     1 },
    {  FLD_B_SWZ_X,          2,   23,     2 },
    {  FLD_B_SWZ_Y,          2,   21,     2 },
    {  FLD_B_SWZ_Z,          2,   19,     2 },
    {  FLD_B_SWZ_W,          2,   17,     2 },
    {  FLD_B_R,              2,   13,     4 },
    {  FLD_B_MUX,            2,   11,     2 },
    // INPUT C
    {  FLD_C_NEG,            2,   10,     1 },
    {  FLD_C_SWZ_X,          2,    8,     2 },
    {  FLD_C_SWZ_Y,          2,    6,     2 },
    {  FLD_C_SWZ_Z,          2,    4,     2 },
    {  FLD_C_SWZ_W,          2,    2,     2 },
    {  FLD_C_R_HIGH,         2,    0,     2 },
    {  FLD_C_R_LOW,          3,   30,     2 },
    {  FLD_C_MUX,            3,   28,     2 },
    // Output
    {  FLD_OUT_MAC_MASK,     3,   24,     4 },
    {  FLD_OUT_R,            3,   20,     4 },
    {  FLD_OUT_ILU_MASK,     3,   16,     4 },
    {  FLD_OUT_O_MASK,       3,   12,     4 },
    {  FLD_OUT_ORB,          3,   11,     1 },
    {  FLD_OUT_ADDRESS,      3,    3,     8 },
    {  FLD_OUT_MUX,          3,    2,     1 },
    // Other
    {  FLD_A0X,              3,    1,     1 },
    {  FLD_FINAL,            3,    0,     1 }
};


typedef struct VshOpcodeParams {
    bool A;
    bool B;
    bool C;
} VshOpcodeParams;

static const VshOpcodeParams ilu_opcode_params[] = {
    /* ILU OP       ParamA ParamB ParamC */
    /* ILU_NOP */ { false, false, false }, // Dxbx note : Unused
    /* ILU_MOV */ { false, false, true  },
    /* ILU_RCP */ { false, false, true  },
    /* ILU_RCC */ { false, false, true  },
    /* ILU_RSQ */ { false, false, true  },
    /* ILU_EXP */ { false, false, true  },
    /* ILU_LOG */ { false, false, true  },
    /* ILU_LIT */ { false, false, true  },
};

static const VshOpcodeParams mac_opcode_params[] = {
    /* MAC OP      ParamA  ParamB ParamC */
    /* MAC_NOP */ { false, false, false }, // Dxbx note : Unused
    /* MAC_MOV */ { true,  false, false },
    /* MAC_MUL */ { true,  true,  false },
    /* MAC_ADD */ { true,  false, true  },
    /* MAC_MAD */ { true,  true,  true  },
    /* MAC_DP3 */ { true,  true,  false },
    /* MAC_DPH */ { true,  true,  false },
    /* MAC_DP4 */ { true,  true,  false },
    /* MAC_DST */ { true,  true,  false },
    /* MAC_MIN */ { true,  true,  false },
    /* MAC_MAX */ { true,  true,  false },
    /* MAC_SLT */ { true,  true,  false },
    /* MAC_SGE */ { true,  true,  false },
    /* MAC_ARL */ { true,  false, false },
};


static const char* mask_str[] = {
            // xyzw xyzw
    ",",     // 0000 ____
    ",w",   // 0001 ___w
    ",z",   // 0010 __z_
    ",zw",  // 0011 __zw
    ",y",   // 0100 _y__
    ",yw",  // 0101 _y_w
    ",yz",  // 0110 _yz_
    ",yzw", // 0111 _yzw
    ",x",   // 1000 x___
    ",xw",  // 1001 x__w
    ",xz",  // 1010 x_z_
    ",xzw", // 1011 x_zw
    ",xy",  // 1100 xy__
    ",xyw", // 1101 xy_w
    ",xyz", // 1110 xyz_
    ",xyzw" // 1111 xyzw
};

/* Note: OpenGL seems to be case-sensitive, and requires upper-case opcodes! */
static const char* mac_opcode[] = {
    "NOP",
    "MOV",
    "MUL",
    "ADD",
    "MAD",
    "DP3",
    "DPH",
    "DP4",
    "DST",
    "MIN",
    "MAX",
    "SLT",
    "SGE",
    "ARL A0.x", // Dxbx note : Alias for "mov a0.x"
};

static const char* ilu_opcode[] = {
    "NOP",
    "MOV",
    "RCP",
    "RCC",
    "RSQ",
    "EXP",
    "LOG",
    "LIT",
};

static bool ilu_force_scalar[] = {
    false,
    false,
    true,
    true,
    true,
    true,
    true,
    false,
};

static const char* out_reg_name[] = {
    "oPos",
    "???",
    "???",
    "oD0",
    "oD1",
    "oFog",
    "oPts",
    "oB0",
    "oB1",
    "oT0",
    "oT1",
    "oT2",
    "oT3",
    "???",
    "???",
    "A0.x",
};



// Retrieves a number of bits in the instruction token
static int vsh_get_from_token(const uint32_t *shader_token,
                              uint8_t subtoken,
                              uint8_t start_bit,
                              uint8_t bit_length)
{
    return (shader_token[subtoken] >> start_bit) & ~(0xFFFFFFFF << bit_length);
}

uint8_t vsh_get_field(const uint32_t *shader_token, VshFieldName field_name)
{

    return (uint8_t)(vsh_get_from_token(shader_token,
                                        field_mapping[field_name].subtoken,
                                        field_mapping[field_name].start_bit,
                                        field_mapping[field_name].bit_length));
}


// Converts the C register address to disassembly format
static int16_t convert_c_register(const int16_t c_reg)
{
    int16_t r = ((((c_reg >> 5) & 7) - 3) * 32) + (c_reg & 31);
    r += VSH_D3DSCM_CORRECTION; /* to map -96..95 to 0..191 */
    return r; //FIXME: = c_reg?!
}



static QString* decode_swizzle(const uint32_t *shader_token,
                               VshFieldName swizzle_field)
{
    const char* swizzle_str = "xyzw";
    VshSwizzle x, y, z, w;

    /* some microcode instructions force a scalar value */
    if (swizzle_field == FLD_C_SWZ_X
        && ilu_force_scalar[vsh_get_field(shader_token, FLD_ILU)]) {
        x = y = z = w = vsh_get_field(shader_token, swizzle_field);
    } else {
        x = vsh_get_field(shader_token, swizzle_field++);
        y = vsh_get_field(shader_token, swizzle_field++);
        z = vsh_get_field(shader_token, swizzle_field++);
        w = vsh_get_field(shader_token, swizzle_field);
    }

    if (x == SWIZZLE_X && y == SWIZZLE_Y
        && z == SWIZZLE_Z && w == SWIZZLE_W) {
        /* Don't print the swizzle if it's .xyzw */
        return qstring_from_str(""); // Will turn ".xyzw" into "."
    /* Don't print duplicates */
    } else if (x == y && y == z && z == w) {
        return qstring_from_str((char[]){'.', swizzle_str[x], '\0'});
    } else if (y == z && z == w) {
        return qstring_from_str((char[]){'.',
            swizzle_str[x], swizzle_str[y], '\0'});
    } else if (z == w) {
        return qstring_from_str((char[]){'.',
            swizzle_str[x], swizzle_str[y], swizzle_str[z], '\0'});
    } else {
        return qstring_from_str((char[]){'.',
                                       swizzle_str[x], swizzle_str[y],
                                       swizzle_str[z], swizzle_str[w],
                                       '\0'}); // Normal swizzle mask
    }
}

static QString* decode_opcode_input(const uint32_t *shader_token,
                                    VshParameterType param,
                                    VshFieldName neg_field,
                                    int reg_num)
{
    /* This function decodes a vertex shader opcode parameter into a string.
     * Input A, B or C is controlled via the Param and NEG fieldnames,
     * the R-register address for each input is already given by caller. */

    QString *ret_str = qstring_new();


    if (vsh_get_field(shader_token, neg_field) > 0) {
        qstring_append_chr(ret_str, '-');
    }

    /* PARAM_R uses the supplied reg_num, but the other two need to be
     * determined */
    char tmp[40];
    switch (param) {
    case PARAM_R:
        snprintf(tmp, sizeof(tmp), "R%d", reg_num);
        break;
    case PARAM_V:
        reg_num = vsh_get_field(shader_token, FLD_V);
        snprintf(tmp, sizeof(tmp), "v%d", reg_num);
        break;
    case PARAM_C:
        reg_num = convert_c_register(vsh_get_field(shader_token, FLD_CONST));
        if (vsh_get_field(shader_token, FLD_A0X) > 0) {
            //FIXME: does this really require the "correction" doe in convert_c_register?!
            snprintf(tmp, sizeof(tmp), "c[A0+%d]", reg_num);
        } else {
            snprintf(tmp, sizeof(tmp), "c[%d]", reg_num);
        }
        break;
    default:
        printf("Param: 0x%x\n", param);
        assert(false);
    }
    qstring_append(ret_str, tmp);

    {
        /* swizzle bits are next to the neg bit */
        QString *swizzle_str = decode_swizzle(shader_token, neg_field+1);
        qstring_append(ret_str, qstring_get_str(swizzle_str));
        QDECREF(swizzle_str);
    }

    return ret_str;
}


static QString* decode_opcode(const uint32_t *shader_token,
                              VshOutputMux out_mux,
                              uint32_t mask,
                              const char *opcode,
                              const char *inputs)
{
    QString *ret = qstring_new();
    int reg_num = vsh_get_field(shader_token, FLD_OUT_R);

    /* Test for paired opcodes (in other words : Are both <> NOP?) */
    if (out_mux == OMUX_MAC
          &&  vsh_get_field(shader_token, FLD_ILU) != ILU_NOP
          && reg_num == 1) {
        /* Ignore paired MAC opcodes that write to R1 */
        mask = 0;
    } else if (out_mux == OMUX_ILU
               && vsh_get_field(shader_token, FLD_MAC) != MAC_NOP) {
        /* Paired ILU opcodes can only write to R1 */
        reg_num = 1;
    }

    if (strcmp(opcode, mac_opcode[MAC_ARL]) == 0) {
        qstring_append_fmt(ret, "  ARL(A0%s);\n", inputs);
    } else if (mask > 0) {
        qstring_append_fmt(ret, "  %s(R%d%s%s);\n",
                           opcode, reg_num, mask_str[mask], inputs);
    }

    /* See if we must add a muxed opcode too: */
    if (vsh_get_field(shader_token, FLD_OUT_MUX) == out_mux
        /* Only if it's not masked away: */
        && vsh_get_field(shader_token, FLD_OUT_O_MASK) != 0) {

        qstring_append(ret, "  ");
        qstring_append(ret, opcode);
        qstring_append(ret, "(");

        if (vsh_get_field(shader_token, FLD_OUT_ORB) == OUTPUT_C) {
            /* TODO : Emulate writeable const registers */
            qstring_append(ret, "c");
            qstring_append_int(ret,
                convert_c_register(
                    vsh_get_field(shader_token, FLD_OUT_ADDRESS)));
        } else {
            qstring_append(ret,
                out_reg_name[
                    vsh_get_field(shader_token, FLD_OUT_ADDRESS) & 0xF]);
        }
        qstring_append(ret,
            mask_str[
                vsh_get_field(shader_token, FLD_OUT_O_MASK)]);
        qstring_append(ret, inputs);
        qstring_append(ret, ");\n");
    }

    return ret;
}


static QString* decode_token(const uint32_t *shader_token)
{
    QString *ret;

    /* Since it's potentially used twice, decode input C once: */
    QString *input_c =
        decode_opcode_input(shader_token,
                            vsh_get_field(shader_token, FLD_C_MUX),
                            FLD_C_NEG,
                            (vsh_get_field(shader_token, FLD_C_R_HIGH) << 2)
                                | vsh_get_field(shader_token, FLD_C_R_LOW));

    /* See what MAC opcode is written to (if not masked away): */
    VshMAC mac = vsh_get_field(shader_token, FLD_MAC);
    if (mac != MAC_NOP) {
        QString *inputs_mac = qstring_new();
        if (mac_opcode_params[mac].A) {
            QString *input_a =
                decode_opcode_input(shader_token,
                                    vsh_get_field(shader_token, FLD_A_MUX),
                                    FLD_A_NEG,
                                    vsh_get_field(shader_token, FLD_A_R));
            qstring_append(inputs_mac, ", ");
            qstring_append(inputs_mac, qstring_get_str(input_a));
            QDECREF(input_a);
        }
        if (mac_opcode_params[mac].B) {
            QString *input_b =
                decode_opcode_input(shader_token,
                                    vsh_get_field(shader_token, FLD_B_MUX),
                                    FLD_B_NEG,
                                    vsh_get_field(shader_token, FLD_B_R));
            qstring_append(inputs_mac, ", ");
            qstring_append(inputs_mac, qstring_get_str(input_b));
            QDECREF(input_b);
        }
        if (mac_opcode_params[mac].C) {
            qstring_append(inputs_mac, ", ");
            qstring_append(inputs_mac, qstring_get_str(input_c));
        }

        /* Then prepend these inputs with the actual opcode, mask, and input : */
        ret = decode_opcode(shader_token,
                            OMUX_MAC,
                            vsh_get_field(shader_token, FLD_OUT_MAC_MASK),
                            mac_opcode[mac],
                            qstring_get_str(inputs_mac));
        QDECREF(inputs_mac);
    } else {
        ret = qstring_new();
    }

    /* See if a ILU opcode is present too: */
    VshILU ilu = vsh_get_field(shader_token, FLD_ILU);
    if (ilu != ILU_NOP) {
        QString *inputs_c = qstring_from_str(", ");
        qstring_append(inputs_c, qstring_get_str(input_c));

        /* Append the ILU opcode, mask and (the already determined) input C: */
        QString *ilu_op =
            decode_opcode(shader_token,
                          OMUX_ILU,
                          vsh_get_field(shader_token, FLD_OUT_ILU_MASK),
                          ilu_opcode[ilu],
                          qstring_get_str(inputs_c));

        qstring_append(ret, qstring_get_str(ilu_op));

        QDECREF(inputs_c);
        QDECREF(ilu_op);
    }

    QDECREF(input_c);

    return ret;
}

static const char* vsh_header =
    "\n"
    "int A0 = 0;\n"
    "\n"
    "vec4 R0 = vec4(0.0,0.0,0.0,0.0);\n"
    "vec4 R1 = vec4(0.0,0.0,0.0,0.0);\n"
    "vec4 R2 = vec4(0.0,0.0,0.0,0.0);\n"
    "vec4 R3 = vec4(0.0,0.0,0.0,0.0);\n"
    "vec4 R4 = vec4(0.0,0.0,0.0,0.0);\n"
    "vec4 R5 = vec4(0.0,0.0,0.0,0.0);\n"
    "vec4 R6 = vec4(0.0,0.0,0.0,0.0);\n"
    "vec4 R7 = vec4(0.0,0.0,0.0,0.0);\n"
    "vec4 R8 = vec4(0.0,0.0,0.0,0.0);\n"
    "vec4 R9 = vec4(0.0,0.0,0.0,0.0);\n"
    "vec4 R10 = vec4(0.0,0.0,0.0,0.0);\n"
    "vec4 R11 = vec4(0.0,0.0,0.0,0.0);\n"
    "#define R12 oPos\n" /* R12 is a mirror of oPos */
    "\n"

    /* See:
     * http://msdn.microsoft.com/en-us/library/windows/desktop/bb174703%28v=vs.85%29.aspx
     * https://www.opengl.org/registry/specs/NV/vertex_program1_1.txt
     */
    "\n"
//QQQ #ifdef NICE_CODE
    "/* Converts the input to vec4, pads with last component */\n"
    "vec4 _in(float v) { return vec4(v); }\n"
    "vec4 _in(vec2 v) { return v.xyyy; }\n"
    "vec4 _in(vec3 v) { return v.xyzz; }\n"
    "vec4 _in(vec4 v) { return v.xyzw; }\n"
//#else
//    "/* Make sure input is always a vec4 */\n"
//   "#define _in(v) vec4(v)\n"
//#endif
    "\n"
    "#define INFINITY (1.0 / 0.0)\n"
    "\n"
    "#define MOV(dest, mask, src) dest.mask = _MOV(_in(src)).mask\n"
    "vec4 _MOV(vec4 src)\n"
    "{\n"
    "  return src;\n"
    "}\n"
    "\n"
    "#define MUL(dest, mask, src0, src1) dest.mask = _MUL(_in(src0), _in(src1)).mask\n"
    "vec4 _MUL(vec4 src0, vec4 src1)\n" 
    "{\n"
    "  return src0 * src1;\n"
    "}\n"
    "\n"
    "#define ADD(dest, mask, src0, src1) dest.mask = _ADD(_in(src0), _in(src1)).mask\n"
    "vec4 _ADD(vec4 src0, vec4 src1)\n" 
    "{\n"
    "  return src0 + src1;\n"
    "}\n"
    "\n"
    "#define MAD(dest, mask, src0, src1, src2) dest.mask = _MAD(_in(src0), _in(src1), _in(src2)).mask\n"
    "vec4 _MAD(vec4 src0, vec4 src1, vec4 src2)\n" 
    "{\n"
    "  return src0 * src1 + src2;\n"
    "}\n"
    "\n"
    "#define DP3(dest, mask, src0, src1) dest.mask = _DP3(_in(src0), _in(src1)).mask\n"
    "vec4 _DP3(vec4 src0, vec4 src1)\n"
    "{\n"
    "  return vec4(dot(src0.xyz, src1.xyz));\n"
    "}\n"
    "\n"
    "#define DPH(dest, mask, src0, src1) dest.mask = _DPH(_in(src0), _in(src1)).mask\n"
    "vec4 _DPH(vec4 src0, vec4 src1)\n"
    "{\n"
    "  return vec4(dot(vec4(src0.xyz, 1.0), src1));\n"
    "}\n"
    "\n"
    "#define DP4(dest, mask, src0, src1) dest.mask = _DP4(_in(src0), _in(src1)).mask\n"
    "vec4 _DP4(vec4 src0, vec4 src1)\n"
    "{\n"
    "  return vec4(dot(src0, src1));\n"
    "}\n"
    "\n"
    "#define DST(dest, mask, src0, src1) dest.mask = _DST(_in(src0), _in(src1)).mask\n"
    "vec4 _DST(vec4 src0, vec4 src1)\n"
    "{\n"
    "  return vec4(1.0,\n"
    "              src0.y * src1.y,\n"
    "              src0.z,\n"
    "              src1.w);\n"
    "}\n"
    "\n"
    "#define MIN(dest, mask, src0, src1) dest.mask = _MIN(_in(src0), _in(src1)).mask\n"
    "vec4 _MIN(vec4 src0, vec4 src1)\n"
    "{\n"
    "  return min(src0, src1);\n"
    "}\n"
    "\n"
    "#define MAX(dest, mask, src0, src1) dest.mask = _MAX(_in(src0), _in(src1)).mask\n"
    "vec4 _MAX(vec4 src0, vec4 src1)\n"
    "{\n"
    "  return max(src0, src1);\n"
    "}\n"
    "\n"
    "#define SLT(dest, mask, src0, src1) dest.mask = _SLT(_in(src0), _in(src1)).mask\n"
    "vec4 _SLT(vec4 src0, vec4 src1)\n"
    "{\n"
    "  return vec4(lessThan(src0, src1));\n"
    "}\n"
    "\n"
    "#define ARL(dest, src) dest = _ARL(_in(src).x)\n"
    "int _ARL(float src)\n"
    "{\n"
    "  return int(floor(src));\n"
    "}\n"
    "\n"
    "#define SGE(dest, mask, src0, src1) dest.mask = _SGE(_in(src0), _in(src1)).mask\n"
    "vec4 _SGE(vec4 src0, vec4 src1)\n"
    "{\n"
    "  return vec4(greaterThanEqual(src0, src1));\n"
    "}\n"
    "\n"
    "#define RCP(dest, mask, src) dest.mask = _RCP(_in(src).x).mask\n"
    "vec4 _RCP(float src)\n"
    "{\n"
    "  return vec4(1.0 / src);\n"
    "}\n"
    "\n"
    "#define RCC(dest, mask, src) dest.mask = _RCC(_in(src).x).mask\n"
    "vec4 _RCC(float src)\n"
    "{\n"
    "  float t = 1.0 / src;\n"
    "  if (t > 0.0) {\n"
    "    t = clamp(t, 5.42101e-020, 1.884467e+019);\n"
    "  } else {\n"
    "    t = clamp(t, -1.884467e+019, -5.42101e-020);\n"
    "  }\n"
    "  return vec4(t);\n"
    "}\n"
    "\n"
    "#define RSQ(dest, mask, src) dest.mask = _RSQ(_in(src).x).mask\n"
    "vec4 _RSQ(float src)\n"
    "{\n"
    "  if (src == 0.0) { return vec4(INFINITY); }\n"
    "  if (isinf(src)) { return vec4(0.0); }\n"
    "  return vec4(inversesqrt(abs(src)));\n"
    "}\n"
    "\n"
    "#define EXP(dest, mask, src) dest.mask = _EXP(_in(src).x).mask\n"
    "vec4 _EXP(float src)\n"
    "{\n"
    "  return vec4(exp2(src));\n"
    "}\n"
    "\n"
    "#define LOG(dest, mask, src) dest.mask = _LOG(_in(src).x).mask\n"
    "vec4 _LOG(float src)\n"
    "{\n"
    "  return vec4(log2(src));\n"
    "}\n"
    "\n"
    "#define LIT(dest, mask, src) dest.mask = _LIT(_in(src)).mask\n"
    "vec4 _LIT(vec4 src)\n"
    "{\n"
    "  vec4 s = src;\n"
    "  float epsilon = 1.0 / 256.0;\n"
    "  s.w = clamp(s.w, -(128.0 - epsilon), 128.0 - epsilon);\n"
    "  s.x = max(s.x, 0.0);\n"
    "  s.y = max(s.y, 0.0);\n"
    "  vec4 t = vec4(1.0, 0.0, 0.0, 1.0);\n"
    "  t.y = s.x;\n"
#if 1
    "  t.z = (s.x > 0.0) ? exp2(s.w * log2(s.y)) : 0.0;\n"
#else
    "  t.z = (s.x > 0.0) ? pow(s.y, s.w) : 0.0;\n"
#endif
    "  return t;\n"
    "}\n";

void vsh_translate(uint16_t version,
                   const uint32_t *tokens,
                   unsigned int length,
                   bool z_perspective,
                   QString *header, QString *body)
{


    qstring_append(header, "\n"
                      "uniform mat4 texMat0;\n"
                      "uniform mat4 texMat1;\n"
                      "uniform mat4 texMat2;\n"
                      "uniform mat4 texMat3;\n");
    qstring_append(header, vsh_header);

    bool has_final = false;
    int slot;
    for (slot=0; slot < length; slot++) {
        const uint32_t* cur_token = &tokens[slot * VSH_TOKEN_SIZE];
        QString *token_str = decode_token(cur_token);
        qstring_append_fmt(body,
                           "  /* Slot %d: 0x%08X 0x%08X 0x%08X 0x%08X */",
                           slot,
                           cur_token[0],cur_token[1],cur_token[2],cur_token[3]);
        qstring_append(body, "\n");
        qstring_append(body, qstring_get_str(token_str));
        qstring_append(body, "\n");
        QDECREF(token_str);

        if (vsh_get_field(cur_token, FLD_FINAL)) {
            has_final = true;
            break;
        }
    }
    assert(has_final);

    /* pre-divide and output the generated W so we can do persepctive correct
     * interpolation manually. OpenGL can't, since we give it a W of 1 to work
     * around the perspective divide */
    qstring_append(body,
        "  if (oPos.w == 0.0 || isinf(oPos.w)) {\n"
        "    vtx.inv_w = 1.0;\n"
        "  } else {\n"
        "    vtx.inv_w = 1.0 / oPos.w;\n"
        "  }\n"
    );

    qstring_append(body,
        /* the shaders leave the result in screen space, while
         * opengl expects it in clip space.
         * TODO: the pixel-center co-ordinate differences should handled
         */
        "  oPos.x = 2.0 * (oPos.x - surfaceSize.x * 0.5) / surfaceSize.x;\n"
        "  oPos.y = -2.0 * (oPos.y - surfaceSize.y * 0.5) / surfaceSize.y;\n"
    );
    if (z_perspective) {
        qstring_append(body, "  oPos.z = oPos.w;\n");
    }
    qstring_append(body,
        /* Map the clip range into clip space so z is clipped correctly.
         * Note this makes the values in the depth buffer wrong. This should be
         * handled with gl_ClipDistance instead, but that has performance issues
         * on OS X.
         */
        "  if (clipRange.y != clipRange.x) {\n"
        "    oPos.z = (oPos.z - 0.5 * (clipRange.x + clipRange.y)) / (0.5 * (clipRange.y - clipRange.x));\n"
        "  }\n"

        /* Correct for the perspective divide */
        "  if (oPos.w < 0.0) {\n"
            /* undo the perspective divide in the case where the point would be
             * clipped so opengl can clip it correctly */
        "    oPos.xyz *= oPos.w;\n"
        "  } else {\n"
            /* we don't want the OpenGL perspective divide to happen, but we
             * can't multiply by W because it could be meaningless here */
        "    oPos.w = 1.0;\n"
        "  }\n"
    );

}

