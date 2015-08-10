/*
 * QEMU Geforce NV2A shader generator
 *
 * Copyright (c) 2015 espes
 * Copyright (c) 2015 JayFoxRox
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
#include "hw/xbox/nv2a_shaders_common.h"
#include "hw/xbox/nv2a_shaders.h"

// #define NV2A_DEBUG
#ifdef NV2A_DEBUG
# define NV2A_DPRINTF(format, ...)       printf("nv2a: " format, ## __VA_ARGS__)
#else
# define NV2A_DPRINTF(format, ...)       do { } while (0)
#endif

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
    default:
        assert(false);
        break;
    }
    qstring_append(s, "}\n");

    return s;
}



static QString* generate_fixed_function(const ShaderState state,
                                        char out_prefix)
{
    int i, j;

    /* generate vertex shader mimicking fixed function */
    QString* s = qstring_new();
    qstring_append(s,
"#version 330\n"
"\n"
"in vec4 position;\n"
"in vec4 weight;\n"
"in vec3 normal;\n"
"in vec4 diffuse;\n"
"in vec4 specular;\n"
"in float fogCoord;\n"
"in vec4 backDiffuse;\n"
"in vec4 backSpecular;\n"
"in vec4 texture0;\n"
"in vec4 texture1;\n"
"in vec4 texture2;\n"
"in vec4 texture3;\n"
"\n");

    qstring_append(s, STRUCT_VERTEX_DATA);
    qstring_append_fmt(s, "noperspective out VertexData %c_vtx;\n", out_prefix);
    qstring_append_fmt(s, "#define vtx %c_vtx", out_prefix);

    qstring_append(s,
"\n"
/* FIXME: Add these uniforms using code when they are used */
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
"\n"
"void main() {\n");

    /* Skinning */
    unsigned int count;
    bool mix;
    switch (state.skinning) {
    case SKINNING_OFF:
        count = 0; break;
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
    if (count == 0) {
        qstring_append(s, "vec4 tPosition = position * modelViewMat0;\n");
        /* FIXME: Is the normal still transformed? */
        qstring_append(s, "vec3 tNormal = (vec4(normal, 0.0) * invModelViewMat0).xyz;\n");
    } else {
        qstring_append(s, "vec4 tPosition = vec4(0.0);\n");
        qstring_append(s, "vec3 tNormal = vec3(0.0);\n");
        if (mix) {
            /* Tweening */
            if (count == 2) {
                qstring_append(s,
                    "tPosition += mix(position * modelViewMat1,\n"
                    "                 position * modelViewMat0, weight.x);\n"
                    "tNormal += mix((vec4(normal, 0.0) * invModelViewMat1).xyz,\n"
                    "               (vec4(normal, 0.0) * invModelViewMat0).xyz, weight.x);\n");
            } else {
                /* FIXME: Not sure how blend weights are calculated */
                assert(false);
            }
        } else {
            /* Individual matrices */
            for (i = 0; i < count; i++) {
                char c = "xyzw"[i];
                qstring_append_fmt(s,
                    "tPosition += position * modelViewMat%d * weight.%c;\n",
                    i, c);
                qstring_append_fmt(s,
                    "tNormal += (vec4(normal, 0.0) * invModelViewMat%d * weight.%c).xyz;\n",
                    i, c);
            }
            assert(false); /* FIXME: Untested */
        }
    }

    /* Normalization */
    if (state.normalization) {
        qstring_append(s, "tNormal = normalize(tNormal);\n");
    }

    /* Texgen */
    for (i = 0; i < 4; i++) {
        qstring_append_fmt(s, "/* Texgen for stage %d */\n",
                           i);
        qstring_append_fmt(s, "vec4 tTexture%d;\n",
                           i);
        /* Set each component individually */
        /* FIXME: could be nicer if some channels share the same texgen */
        for (j = 0; j < 4; j++) {
            char c = "xyzw"[j];
            switch (state.texgen[i][j]) {
            case TEXGEN_DISABLE:
                qstring_append_fmt(s,
                                   "tTexture%d.%c = texture%d.%c;\n",
                                   i, c, i, c);
                break;
            case TEXGEN_EYE_LINEAR:
                qstring_append_fmt(s,
                                   "tTexture%d.%c = tPosition.%c;\n",
                                   i, c, c);
                break;
            case TEXGEN_OBJECT_LINEAR:
                qstring_append_fmt(s,
                                   "tTexture%d.%c = position.%c;\n",
                                   i, c, c);
                break;
            case TEXGEN_SPHERE_MAP:
                assert(i < 2);  /* Channels S,T only! */
                assert(false);
                break;
            case TEXGEN_REFLECTION_MAP:
                assert(i < 3); /* Channels S,T,R only! */
                qstring_append_fmt(s,
                                   "tTexture%d.%c = reflect(???, tNormal).%c;\n",
                                   i, c, c);
                assert(false); /* FIXME: Code not complete yet! */
                break;
            case TEXGEN_NORMAL_MAP:
                assert(i < 3); /* Channels S,T,R only! */
                qstring_append_fmt(s,
                                   "tTexture%d.%c = tNormal.%c;\n",
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
    qstring_append(s, "vtx.D0 = diffuse * vtx.inv_w;\n");
    qstring_append(s, "vtx.D1 = specular * vtx.inv_w;\n");
    qstring_append(s, "vtx.B0 = backDiffuse * vtx.inv_w;\n");
    qstring_append(s, "vtx.B1 = backSpecular * vtx.inv_w;\n");
    qstring_append(s, "vtx.Fog = vec4(0.0,0.0,0.0,1.0) * vtx.inv_w;\n");
    qstring_append(s, "vtx.T0 = tTexture0 *  vtx.inv_w;\n");
    qstring_append(s, "vtx.T1 = tTexture1 * vtx.inv_w;\n");
    qstring_append(s, "vtx.T2 = tTexture2 * vtx.inv_w;\n");
    qstring_append(s, "vtx.T3 = tTexture3 * vtx.inv_w;\n");

    qstring_append(s, "}\n");

    return s;
}

ShaderBinding* generate_shaders(const ShaderState state)
{
    int i, j;
    GLint compiled = 0;

    bool with_geom = state.primitive_mode == PRIM_TYPE_QUADS;
    char vtx_prefix = with_geom ? 'v' : 'g';

    GLuint program = glCreateProgram();

    /* create the vertex shader */

    QString *vertex_shader_code = NULL;
    if (state.fixed_function) {
        vertex_shader_code = generate_fixed_function(state, vtx_prefix);

    } else if (state.vertex_program) {
        vertex_shader_code = vsh_translate(VSH_VERSION_XVS,
                                           (uint32_t*)state.program_data,
                                           state.program_length,
                                           vtx_prefix);
    } else {
        assert(false);
    }

    if (vertex_shader_code) {
        const char* vertex_shader_code_str = qstring_get_str(vertex_shader_code);

        GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
        glAttachShader(program, vertex_shader);

        glShaderSource(vertex_shader, 1, &vertex_shader_code_str, 0);
        glCompileShader(vertex_shader);

        NV2A_DPRINTF("bind new vertex shader, code:\n%s\n", vertex_shader_code_str);

        /* Check it compiled */
        compiled = 0;
        glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            GLchar log[1024];
            glGetShaderInfoLog(vertex_shader, 1024, NULL, log);
            fprintf(stderr, "nv2a: vertex shader compilation failed: %s\n", log);
            abort();
        }

        QDECREF(vertex_shader_code);
    }


    if (state.fixed_function) {
        /* bind fixed function vertex attributes */
        glBindAttribLocation(program, NV2A_VERTEX_ATTR_POSITION, "position");
        glBindAttribLocation(program, NV2A_VERTEX_ATTR_WEIGHT, "weight");
        glBindAttribLocation(program, NV2A_VERTEX_ATTR_DIFFUSE, "diffuse");
        glBindAttribLocation(program, NV2A_VERTEX_ATTR_SPECULAR, "specular");
        glBindAttribLocation(program, NV2A_VERTEX_ATTR_FOG, "fog");
        glBindAttribLocation(program, NV2A_VERTEX_ATTR_BACK_DIFFUSE, "backDiffuse");
        glBindAttribLocation(program, NV2A_VERTEX_ATTR_BACK_SPECULAR, "backSpecular");
        glBindAttribLocation(program, NV2A_VERTEX_ATTR_TEXTURE0, "texture0");
        glBindAttribLocation(program, NV2A_VERTEX_ATTR_TEXTURE1, "texture1");
        glBindAttribLocation(program, NV2A_VERTEX_ATTR_TEXTURE2, "texture2");
        glBindAttribLocation(program, NV2A_VERTEX_ATTR_TEXTURE3, "texture3");
    } else if (state.vertex_program) {
        /* Bind attributes for transform program*/
        char tmp[8];
        for(i = 0; i < 16; i++) {
            snprintf(tmp, sizeof(tmp), "v%d", i);
            glBindAttribLocation(program, i, tmp);
        }
    } else {
        assert(false);
    }


    /* generate a fragment shader from register combiners */
    GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glAttachShader(program, fragment_shader);

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

    NV2A_DPRINTF("bind new fragment shader, code:\n%s\n", fragment_shader_code_str);

    glShaderSource(fragment_shader, 1, &fragment_shader_code_str, 0);
    glCompileShader(fragment_shader);

    /* Check it compiled */
    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLchar log[1024];
        glGetShaderInfoLog(fragment_shader, 1024, NULL, log);
        fprintf(stderr, "nv2a: fragment shader compilation failed: %s\n", log);
        abort();
    }

    QDECREF(fragment_shader_code);


    if (with_geom) {
        GLuint geometry_shader = glCreateShader(GL_GEOMETRY_SHADER);
        glAttachShader(program, geometry_shader);

        QString* geometry_shader_code =
            generate_geometry_shader(state.primitive_mode);
        const char* geometry_shader_code_str =
             qstring_get_str(geometry_shader_code);

        NV2A_DPRINTF("bind geometry shader, code:\n%s\n", geometry_shader_code_str);

        glShaderSource(geometry_shader, 1, &geometry_shader_code_str, 0);
        glCompileShader(geometry_shader);

        /* Check it compiled */
        compiled = 0;
        glGetShaderiv(geometry_shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            GLchar log[2048];
            glGetShaderInfoLog(geometry_shader, 2048, NULL, log);
            fprintf(stderr, "nv2a: geometry shader compilation failed: %s\n", log);
            abort();
        }

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
    if (state.vertex_program) {
        /* lookup vertex shader bindings */
        for(i = 0; i < NV2A_VERTEXSHADER_CONSTANTS; i++) {
            char tmp[8];
            snprintf(tmp, sizeof(tmp), "c[%d]", i);
            ret->vsh_constant_loc[i] = glGetUniformLocation(program, tmp);
        }
    }

    return ret;
}
