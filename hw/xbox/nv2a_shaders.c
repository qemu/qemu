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

#include "qemu-common.h"
#include "hw/xbox/nv2a_debug.h"
#include "hw/xbox/nv2a_shaders_common.h"
#include "hw/xbox/nv2a_shaders.h"

static void generate_geometry_shader_pass_vertex(QString* s, const char* v)
{
    qstring_append_fmt(s, "        gl_Position = gl_in[%s].gl_Position;\n", v);
    qstring_append_fmt(s, "        gl_PointSize = gl_in[%s].gl_PointSize;\n", v);
    qstring_append_fmt(s, "        g_vtx = v_vtx[%s];\n", v);
    qstring_append(s,     "        EmitVertex();\n");
}

static QString* generate_geometry_shader(enum ShaderPrimitiveMode primitive_mode)
{
    /* generate a geometry shader to support deprecated primitive types */
    QString* s = qstring_new();
    qstring_append(s, "#version 330\n");
    qstring_append(s, "\n");
    switch (primitive_mode) {
    case PRIM_TYPE_QUADS:
    case PRIM_TYPE_QUAD_STRIP:
        qstring_append(s, "layout(lines_adjacency) in;\n");
        qstring_append(s, "layout(triangle_strip, max_vertices = 4) out;\n");
        break;
    default:
        assert(false);
        break;
    }
    qstring_append(s, "\n");
    qstring_append(s, STRUCT_VERTEX_DATA);
    qstring_append(s,
        "noperspective in VertexData v_vtx[];\n");
    qstring_append(s,
        "noperspective out VertexData g_vtx;\n");
    qstring_append(s, "\n");

    qstring_append(s, "void main() {\n");
    switch (primitive_mode) {
    case PRIM_TYPE_QUADS:
        generate_geometry_shader_pass_vertex(s, "0");
        generate_geometry_shader_pass_vertex(s, "1");
        generate_geometry_shader_pass_vertex(s, "3");
        generate_geometry_shader_pass_vertex(s, "2");
        qstring_append(s, "EndPrimitive();\n");
        break;
    case PRIM_TYPE_QUAD_STRIP:
        qstring_append(s, "if ((gl_PrimitiveIDIn & 1) == 0) {\n");
        generate_geometry_shader_pass_vertex(s, "0");
        generate_geometry_shader_pass_vertex(s, "1");
        generate_geometry_shader_pass_vertex(s, "2");
        generate_geometry_shader_pass_vertex(s, "3");
        qstring_append(s, "  EndPrimitive();\n"
                          "}");
        break;
    default:
        assert(false);
        break;
    }
    qstring_append(s, "}\n");

    return s;
}

static void append_skinning_code(QString* str, bool mix,
                                 unsigned int count, const char* type,
                                 const char* output, const char* input,
                                 const char* matrix, const char* swizzle)
{

    if (count == 0) {
        qstring_append_fmt(str, "%s %s = (%s * %s0).%s;\n",
                           type, output, input, matrix, swizzle);
    } else {
        qstring_append_fmt(str, "%s %s = %s(0.0);\n", type, output, type);
        if (mix) {
            /* Tweening */
            if (count == 2) {
                qstring_append_fmt(str,
                                   "%s += mix((%s * %s1).%s,\n"
                                   "          (%s * %s0).%s, weight.x);\n",
                                   output,
                                   input, matrix, swizzle,
                                   input, matrix, swizzle);
            } else {
                /* FIXME: Not sure how blend weights are calculated */
                assert(false);
            }
        } else {
            /* Individual matrices */
            int i;
            for (i = 0; i < count; i++) {
                char c = "xyzw"[i];
                qstring_append_fmt(str, "%s += (%s * %s%d * weight.%c).%s;\n",
                                   output, input, matrix, i, c,
                                   swizzle);
            }
            assert(false); /* FIXME: Untested */
        }
    }
}

static QString* generate_fixed_function(const ShaderState state,
                                        char out_prefix)
{
    int i, j;

    /* generate vertex shader mimicking fixed function */
    QString* h = qstring_new();
    QString* s = qstring_new();
    qstring_append(h,
"#version 330\n"
"\n"
"#define position      v0\n"
"#define weight        v1\n"
"#define normal        v2.xyz\n"
"#define diffuse       v3\n"
"#define specular      v4\n"
"#define fogCoord      v5.x\n"
"#define pointSize     v6\n"
"#define backDiffuse   v7\n"
"#define backSpecular  v8\n"
"#define texture0      v9\n"
"#define texture1      v10\n"
"#define texture2      v11\n"
"#define texture3      v12\n"
"#define reserved1     v13\n"
"#define reserved2     v14\n"
"#define reserved3     v15\n"
"\n");

    for(i = 0; i < 16; i++) {
        qstring_append_fmt(h, "in vec4 v%d;\n", i);
    }

    qstring_append(h, "\n"
                      STRUCT_VERTEX_DATA);
    qstring_append_fmt(h, "noperspective out VertexData %c_vtx;\n", out_prefix);
    qstring_append_fmt(h, "#define vtx %c_vtx", out_prefix);


    qstring_append(h,
"\n"
/* FIXME: Add these uniforms using code when they are used */
"uniform vec4 fogColor;\n"
"uniform vec4 fogPlane;\n"
"uniform float fogParam[2];\n"
"uniform vec4 texPlaneS0;\n"
"uniform vec4 texPlaneT0;\n"
"uniform vec4 texPlaneQ0;\n"
"uniform vec4 texPlaneR0;\n"
"uniform vec4 texPlaneS1;\n"
"uniform vec4 texPlaneT1;\n"
"uniform vec4 texPlaneQ1;\n"
"uniform vec4 texPlaneR1;\n"
"uniform vec4 texPlaneS2;\n"
"uniform vec4 texPlaneT2;\n"
"uniform vec4 texPlaneQ2;\n"
"uniform vec4 texPlaneR2;\n"
"uniform vec4 texPlaneS3;\n"
"uniform vec4 texPlaneT3;\n"
"uniform vec4 texPlaneQ3;\n"
"uniform vec4 texPlaneR3;\n"
"uniform mat4 texMat0;\n"
"uniform mat4 texMat1;\n"
"uniform mat4 texMat2;\n"
"uniform mat4 texMat3;\n"
"uniform mat4 modelViewMat0;\n"
"uniform mat4 modelViewMat1;\n"
"uniform mat4 modelViewMat2;\n"
"uniform mat4 modelViewMat3;\n"
"uniform mat4 invModelViewMat0;\n"
"uniform mat4 invModelViewMat1;\n"
"uniform mat4 invModelViewMat2;\n"
"uniform mat4 invModelViewMat3;\n"
"uniform mat4 projectionMat; /* FIXME: when is this used? */\n"
"uniform mat4 compositeMat;\n"
"uniform mat4 invViewport;\n"
"\n");

    /* Skinning */
    unsigned int count;
    bool mix;
    switch (state.skinning) {
    case SKINNING_OFF:
        mix = false; count = 0; break;
    case SKINNING_1WEIGHTS:
        mix = true; count = 2; break;
    case SKINNING_2WEIGHTS:
        mix = true; count = 3; break;
    case SKINNING_3WEIGHTS:
        mix = true; count = 4; break;
    case SKINNING_2WEIGHTS2MATRICES:
        mix = false; count = 2; break;
    case SKINNING_3WEIGHTS3MATRICES:
        mix = false; count = 3; break;
    case SKINNING_4WEIGHTS4MATRICES:
        mix = false; count = 4; break;
    default:
        assert(false);
        break;
    }
    qstring_append_fmt(s, "/* Skinning mode %d */\n",
                       state.skinning);

    append_skinning_code(s, mix, count, "vec4",
                         "tPosition", "position",
                         "modelViewMat", "xyzw");
    append_skinning_code(s, mix, count, "vec3",
                         "tNormal", "vec4(normal, 0.0)",
                         "invModelViewMat", "xyz");

    /* Normalization */
    if (state.normalization) {
        qstring_append(s, "tNormal = normalize(tNormal);\n");
    }

    /* Texgen */
    for (i = 0; i < 4 /* FIXME: NV2A_MAX_TEXTURES */; i++) {
        qstring_append_fmt(s, "/* Texgen for stage %d */\n",
                           i);
        qstring_append_fmt(s, "vec4 tTexture%d;\n",
                           i);
        /* Set each component individually */
        /* FIXME: could be nicer if some channels share the same texgen */
        for (j = 0; j < 4; j++) {
            /* TODO: TexGen View Model missing! */
            char c = "xyzw"[j];
            char cSuffix = "STRQ"[j];
            switch (state.texgen[i][j]) {
            case TEXGEN_DISABLE:
                qstring_append_fmt(s, "tTexture%d.%c = texture%d.%c;\n",
                                   i, c, i, c);
                break;
            case TEXGEN_EYE_LINEAR:
                qstring_append_fmt(s, "tTexture%d.%c = dot(texPlane%c%d, tPosition);\n",
                                   i, c, cSuffix, i);
                break;
            case TEXGEN_OBJECT_LINEAR:
                qstring_append_fmt(s, "tTexture%d.%c = dot(texPlane%c%d, position);\n",
                                   i, c, cSuffix, i);
                assert(false); /* Untested */
                break;
            case TEXGEN_SPHERE_MAP:
                assert(i < 2);  /* Channels S,T only! */
                qstring_append(s, "{\n");
                /* FIXME: u, r and m only have to be calculated once */
                qstring_append(s, "  vec3 u = normalize(tPosition.xyz);\n");
                //FIXME: tNormal before or after normalization? Always normalize?
                qstring_append(s, "  vec3 r = reflect(u, tNormal);\n");

                /* FIXME: This would consume 1 division fewer and *might* be
                 *        faster than length:
                 *   // [z=1/(2*x) => z=1/x*0.5]
                 *   vec3 ro = r + vec3(0.0, 0.0, 1.0);
                 *   float m = inversesqrt(dot(ro,ro))*0.5;
                 */

                qstring_append(s, "  float invM = 1.0 / (2.0 * length(r + vec3(0.0, 0.0, 1.0)));\n");
                qstring_append_fmt(s, "  tTexture%d.%c = r.%c * invM + 0.5;\n",
                                   i, c, c);
                qstring_append(s, "}\n");
                assert(false); /* Untested */
                break;
            case TEXGEN_REFLECTION_MAP:
                assert(i < 3); /* Channels S,T,R only! */
                qstring_append(s, "{\n");
                /* FIXME: u and r only have to be calculated once, can share the one from SPHERE_MAP */
                qstring_append(s, "  vec3 u = normalize(tPosition.xyz);\n");
                qstring_append(s, "  vec3 r = reflect(u, tNormal);\n");
                qstring_append_fmt(s, "  tTexture%d.%c = r.%c;\n",
                                   i, c, c);
                qstring_append(s, "}\n");
                break;
            case TEXGEN_NORMAL_MAP:
                assert(i < 3); /* Channels S,T,R only! */
                qstring_append_fmt(s, "tTexture%d.%c = tNormal.%c;\n",
                                   i, c, c);
                break;
            default:
                assert(false);
                break;
            }
        }
    }

    /* Apply texture matrices */
    for (i = 0; i < 4; i++) {
        if (state.texture_matrix_enable[i]) {
            qstring_append_fmt(s,
                               "tTexture%d = tTexture%d * texMat%d;\n",
                               i, i, i);
        }
    }

    /* Lighting */
    if (state.lighting) {

        //FIXME: Do 2 passes if we want 2 sided-lighting?
        qstring_append_fmt(h, "uniform vec3 sceneAmbientColor;\n");
        qstring_append(s, "vec4 tD0 = vec4(sceneAmbientColor, diffuse.a);\n");
        qstring_append(s, "vec4 tD1 = vec4(0.0, 0.0, 0.0, specular.a);\n");

        /* FIXME: Only add if necessary */
        qstring_append(h,
            "uniform vec4 eyePosition;\n");

        for (i = 0; i < NV2A_MAX_LIGHTS; i++) {
            if (state.light[i] == LIGHT_OFF) {
                continue;
            }

            qstring_append_fmt(h,
                "uniform vec3 lightAmbientColor%d;\n"
                "uniform vec3 lightDiffuseColor%d;\n"
                "uniform vec3 lightSpecularColor%d;\n",
                i, i, i);

            /* FIXME: It seems that we only have to handle the surface colors if
             *        they are not part of the material [= vertex colors].
             *        If they are material the cpu will premultiply light
             *        colors
             */

            qstring_append_fmt(s, "/* Light %d */ {\n", i);

            qstring_append_fmt(h,
                "uniform float lightLocalRange%d;\n", i);

            if (state.light[i] == LIGHT_LOCAL
                    || state.light[i] == LIGHT_SPOT) {

                qstring_append_fmt(h,
                    "uniform vec3 lightLocalPosition%d;\n"
                    "uniform vec3 lightLocalAttenuation%d;\n",
                    i, i);
                qstring_append_fmt(s,
                    "  vec3 VP = lightLocalPosition%d - tPosition.xyz/tPosition.w;\n"
                    "  float d = length(VP);\n"
//FIXME: if (d > lightLocalRange) { .. don't process this light .. } /* inclusive?! */ - what about directional lights?
                    "  VP = normalize(VP);\n"
                    "  float attenuation = 1.0 / (lightLocalAttenuation%d.x\n"
                    "                               + lightLocalAttenuation%d.y * d\n"
                    "                               + lightLocalAttenuation%d.z * d * d);\n"
                    "  vec3 halfVector = normalize(VP + eyePosition.xyz / eyePosition.w);\n" /* FIXME: Not sure if eyePosition is correct */
                    "  float nDotVP = max(0.0, dot(tNormal, VP));\n"
                    "  float nDotHV = max(0.0, dot(tNormal, halfVector));\n",
                    i, i, i, i);

            }

            switch(state.light[i]) {
            case LIGHT_INFINITE:

                /* lightLocalRange will be 1e+30 here */

                qstring_append_fmt(h,
                    "uniform vec3 lightInfiniteHalfVector%d;\n"
                    "uniform vec3 lightInfiniteDirection%d;\n",
                    i, i);
                qstring_append_fmt(s,
                    "  float attenuation = 1.0;\n"
                    "  float nDotVP = max(0.0, dot(tNormal, normalize(vec3(lightInfiniteDirection%d))));\n"
                    "  float nDotHV = max(0.0, dot(tNormal, vec3(lightInfiniteHalfVector%d)));\n",
                    i, i);

                /* FIXME: Do specular */

                /* FIXME: tBackDiffuse */

                break;
            case LIGHT_LOCAL:
                /* Everything done already */
                break;
            case LIGHT_SPOT:
                qstring_append_fmt(h,
                    "uniform vec3 lightSpotFalloff%d;\n"
                    "uniform vec4 lightSpotDirection%d;\n",
                    i, i);
                assert(false);
                /*FIXME: calculate falloff */
                break;
            default:
                assert(false);
                break;
            }

            qstring_append_fmt(s,
                "  float pf;\n"
                "  if (nDotVP == 0.0) {\n"
                "    pf = 0.0;\n"
                "  } else {\n"
                "    pf = pow(nDotHV, /* specular(l, m, n, l1, m1, n1) */ 0.001);\n"
                "  }\n"
                "  vec3 lightAmbient = lightAmbientColor%d * attenuation;\n"
                "  vec3 lightDiffuse = lightDiffuseColor%d * attenuation * nDotVP;\n"
                "  vec3 lightSpecular = lightSpecularColor%d * pf;\n",
                i, i, i);

            qstring_append(s,
                "  tD0.xyz += lightAmbient;\n");

            qstring_append(s,
                "  tD0.xyz += diffuse.xyz * lightDiffuse;\n");

            qstring_append(s,
                "  tD1.xyz += specular.xyz * lightSpecular;\n"
                "}\n");
        }
    } else {
        qstring_append(s, "vec4 tD0 = diffuse;\n");
        qstring_append(s, "vec4 tD1 = specular;\n");
    }
    qstring_append(s, "vec4 tB0 = backDiffuse;\n");
    qstring_append(s, "vec4 tB1 = backSpecular;\n");

    /* Fog */
    if (state.fog_enable) {

        /* From: https://www.opengl.org/registry/specs/NV/fog_distance.txt */
        switch(state.foggen) {
        case FOGGEN_SPEC_ALPHA:
            assert(false); /* FIXME: Do this before or after calculations in VSH? */
            if (state.fixed_function) {
                /* FIXME: Do we have to clamp here? */
                qstring_append(s, "float fogDistance = clamp(specular.a, 0.0, 1.0);\n");
            } else if (state.vertex_program) {
                qstring_append(s, "float fogDistance = oD1.a;\n");
            } else {
                assert(false);
            }
            break;
        case FOGGEN_RADIAL:
            qstring_append(s, "float fogDistance = length(tPosition.xyz)");
            break;
        case FOGGEN_PLANAR:
        case FOGGEN_ABS_PLANAR:
            qstring_append(s, "float fogDistance = dot(fogPlane.xyz, tPosition.xyz) + fogPlane.w;\n");
            if (state.foggen == FOGGEN_ABS_PLANAR) {
                qstring_append(s, "fogDistance = abs(fogDistance);\n");
            }
            break;
        case FOGGEN_FOG_X:
            if (state.fixed_function) {
                qstring_append(s, "float fogDistance = fogCoord;\n");
            } else if (state.vertex_program) {
                qstring_append(s, "float fogDistance = oFog.x;\n");
            } else {
                assert(false);
            }
            break;
        default:
            assert(false);
            break;
        }

        //FIXME: Do this per pixel?
        switch (state.fog_mode) {
        case FOG_MODE_LINEAR:
        case FOG_MODE_LINEAR_ABS:

            /* f = (end - d) / (end - start)
             *    fogParam[1] = 1 / (end - start)
             *    fogParam[0] = 1 + end * fogParam[1];
             */

            qstring_append(s, "float fogFactor = fogParam[0] + fogDistance * fogParam[1];\n");
            qstring_append(s, "fogFactor -= 1.0;\n"); /* FIXME: WHHYYY?!! */
            break;
        case FOG_MODE_EXP:
        case FOG_MODE_EXP_ABS:

            /* f = 1 / (e^(d * density))
             *    fogParam[1] = -density / (2 * ln(256))
             *    fogParam[0] = 1.5
             */

            qstring_append(s, "float fogFactor = fogParam[0] + exp2(fogDistance * fogParam[1] * 16.0);\n");
            qstring_append(s, "fogFactor -= 1.5;\n"); /* FIXME: WHHYYY?!! */
            break;
        case FOG_MODE_EXP2:
        case FOG_MODE_EXP2_ABS:

            /* f = 1 / (e^((d * density)^2))
             *    fogParam[1] = -density / (2 * sqrt(ln(256)))
             *    fogParam[0] = 1.5
             */

            qstring_append(s, "float fogFactor = fogParam[0] + exp2(-fogDistance * fogDistance * fogParam[1] * fogParam[1] * 32.0);\n");
            qstring_append(s, "fogFactor -= 1.5;\n"); /* FIXME: WHHYYY?!! */
            break;
        default:
            assert(false);
            break;
        }
        /* Calculate absolute for the modes which need it */
        switch (state.fog_mode) {
        case FOG_MODE_LINEAR_ABS:
        case FOG_MODE_EXP_ABS:
        case FOG_MODE_EXP2_ABS:
            qstring_append(s, "fogFactor = abs(fogFactor);\n");
            break;
        default:
            break;
        }
        /* FIXME: What about fog alpha?! */
        qstring_append(s, "float tFog = fogFactor;\n");
    } else {
        /* FIXME: Is the fog still calculated / passed somehow?!
         */
        qstring_append(s, "float tFog = 0.0;\n");
    }

    /* If skinning is off the composite matrix already includes the MV matrix */
    if (state.skinning == SKINNING_OFF) {
        qstring_append(s, "tPosition = position;\n");
    }

    qstring_append(s,
    "   gl_Position = invViewport * (tPosition * compositeMat);\n"
/* temp hack: the composite matrix includes the view transform... */
//"   gl_Position = position * compositeMat;\n"
//"   gl_Position.x = (gl_Position.x - 320.0) / 320.0;\n"
//"   gl_Position.y = -(gl_Position.y - 240.0) / 240.0;\n"
    "   gl_Position.z = gl_Position.z * 2.0 - gl_Position.w;\n");

    qstring_append(s, "vtx.inv_w = 1.0/gl_Position.w;\n");
    qstring_append(s, "vtx.D0 = clamp(tD0, 0.0, 1.0) * vtx.inv_w;\n");
    qstring_append(s, "vtx.D1 = clamp(tD1, 0.0, 1.0) * vtx.inv_w;\n");
    qstring_append(s, "vtx.B0 = clamp(tB0, 0.0, 1.0) * vtx.inv_w;\n");
    qstring_append(s, "vtx.B1 = clamp(tB1, 0.0, 1.0) * vtx.inv_w;\n");
    qstring_append(s, "vtx.Fog = tFog * vtx.inv_w;\n");
    qstring_append(s, "vtx.T0 = tTexture0 *  vtx.inv_w;\n");
    qstring_append(s, "vtx.T1 = tTexture1 * vtx.inv_w;\n");
    qstring_append(s, "vtx.T2 = tTexture2 * vtx.inv_w;\n");
    qstring_append(s, "vtx.T3 = tTexture3 * vtx.inv_w;\n");

    qstring_append(h,"void main() {\n");
    qstring_append(h, qstring_get_str(s));
    qstring_append(h, "}\n");

    QDECREF(s);

    return h;
}

static GLuint create_gl_shader(GLenum gl_shader_type,
                               const char *code,
                               const char *name)
{
    GLint compiled = 0;

    NV2A_GL_DGROUP_BEGIN("Creating new %s", name);

    NV2A_DPRINTF("compile new %s, code:\n%s\n", name, code);

    GLuint shader = glCreateShader(gl_shader_type);
    glShaderSource(shader, 1, &code, 0);
    glCompileShader(shader);

    /* Check it compiled */
    compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLchar* log;
        GLint log_length;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
        log = g_malloc(log_length * sizeof(GLchar));
        glGetShaderInfoLog(shader, log_length, NULL, log);
        fprintf(stderr, "nv2a: %s compilation failed: %s\n", name, log);
        g_free(log);

        NV2A_GL_DGROUP_END();
        abort();
    }

    NV2A_GL_DGROUP_END();

    return shader;
}

ShaderBinding* generate_shaders(const ShaderState state)
{
    int i, j;

    bool with_geom = state.primitive_mode == PRIM_TYPE_QUADS ||
                     state.primitive_mode == PRIM_TYPE_QUAD_STRIP;
    char vtx_prefix = with_geom ? 'v' : 'g';

    GLuint program = glCreateProgram();

    /* create the vertex shader */

    QString *s = NULL;
    if (state.fixed_function) {
        s = generate_fixed_function(state, vtx_prefix);

    } else if (state.vertex_program) {
        s = vsh_translate(VSH_VERSION_XVS,
                                           (uint32_t*)state.program_data,
                                           state.program_length,
                                           vtx_prefix);
    } else {
        assert(false);
    }

    if (s) {
        const char* s_str = qstring_get_str(s);

        GLuint vertex_shader = create_gl_shader(GL_VERTEX_SHADER,
                                                s_str,
                                                "vertex shader");
        glAttachShader(program, vertex_shader);

        QDECREF(s);
    }


    /* Bind attributes for vertices */
    char tmp[8];
    for(i = 0; i < 16; i++) {
        snprintf(tmp, sizeof(tmp), "v%d", i);
        glBindAttribLocation(program, i, tmp);
    }


    /* generate a fragment shader from register combiners */

    QString *fragment_shader_code = psh_translate(state.combiner_control,
                   state.shader_stage_program,
                   state.other_stage_input,
                   state.rgb_inputs, state.rgb_outputs,
                   state.alpha_inputs, state.alpha_outputs,
                   /* constant_0, constant_1, */
                   state.final_inputs_0, state.final_inputs_1,
                   /* final_constant_0, final_constant_1, */
                   state.rect_tex,
                   state.compare_mode,
                   state.alphakill,
                   state.alpha_test, state.alpha_func);

    const char *fragment_shader_code_str = qstring_get_str(fragment_shader_code);

    GLuint fragment_shader = create_gl_shader(GL_FRAGMENT_SHADER,
                                              fragment_shader_code_str,
                                              "fragment shader");
    glAttachShader(program, fragment_shader);

    QDECREF(fragment_shader_code);


    if (with_geom) {
        QString* geometry_shader_code =
            generate_geometry_shader(state.primitive_mode);
        const char* geometry_shader_code_str =
             qstring_get_str(geometry_shader_code);

        GLuint geometry_shader = create_gl_shader(GL_GEOMETRY_SHADER,
                                                  geometry_shader_code_str,
                                                  "geometry shader");
        glAttachShader(program, geometry_shader);

        QDECREF(geometry_shader_code);
    }


    /* link the program */
    glLinkProgram(program);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if(!linked) {
        GLchar log[2048];
        glGetProgramInfoLog(program, 2048, NULL, log);
        fprintf(stderr, "nv2a: shader linking failed: %s\n", log);
        abort();
    }

    glUseProgram(program);

    /* set texture samplers */
    for (i = 0; i < 4; i++) {
        char samplerName[16];
        snprintf(samplerName, sizeof(samplerName), "texSamp%d", i);
        GLint texSampLoc = glGetUniformLocation(program, samplerName);
        if (texSampLoc >= 0) {
            glUniform1i(texSampLoc, i);
        }
    }

    /* validate the program */
    glValidateProgram(program);
    GLint valid = 0;
    glGetProgramiv(program, GL_VALIDATE_STATUS, &valid);
    if (!valid) {
        GLchar log[1024];
        glGetProgramInfoLog(program, 1024, NULL, log);
        fprintf(stderr, "nv2a: shader validation failed: %s\n", log);
        abort();
    }

    ShaderBinding* ret = g_malloc0(sizeof(ShaderBinding));
    ret->gl_program = program;

    /* lookup fragment shader locations */
    for (i=0; i<=8; i++) {
        for (j=0; j<2; j++) {
            char tmp[8];
            snprintf(tmp, sizeof(tmp), "c_%d_%d", i, j);
            ret->psh_constant_loc[i][j] = glGetUniformLocation(program, tmp);
        }
    }

    ret->gl_constants_loc = glGetUniformBlockIndex(program, "VertexConstants");

    return ret;
}
