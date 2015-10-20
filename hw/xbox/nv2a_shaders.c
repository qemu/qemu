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

static QString* generate_geometry_shader(
                                      enum ShaderPolygonMode polygon_front_mode,
                                      enum ShaderPolygonMode polygon_back_mode,
                                      enum ShaderPrimitiveMode primitive_mode,
                                      GLenum *gl_primitive_mode)
{

    /* FIXME: Missing support for 2-sided-poly mode */
    assert(polygon_front_mode == polygon_back_mode);
    enum ShaderPolygonMode polygon_mode = polygon_front_mode;

    /* POINT mode shouldn't require any special work */
    if (polygon_mode == POLY_MODE_POINT) {
        *gl_primitive_mode = GL_POINTS;
        return NULL;
    }

    /* Handle LINE and FILL mode */
    const char *layout_in = NULL;
    const char *layout_out = NULL;
    const char *body = NULL;
    switch (primitive_mode) {
    case PRIM_TYPE_POINTS: *gl_primitive_mode = GL_POINTS; return NULL;
    case PRIM_TYPE_LINES: *gl_primitive_mode = GL_LINES; return NULL;
    case PRIM_TYPE_LINE_LOOP: *gl_primitive_mode = GL_LINE_LOOP; return NULL;
    case PRIM_TYPE_LINE_STRIP: *gl_primitive_mode = GL_LINE_STRIP; return NULL;
    case PRIM_TYPE_TRIANGLES:
        *gl_primitive_mode = GL_TRIANGLES;
        if (polygon_mode == POLY_MODE_FILL) { return NULL; }
        assert(polygon_mode == POLY_MODE_LINE);
        layout_in = "layout(triangles) in;\n";
        layout_out = "layout(line_strip, max_vertices = 4) out;\n";
        body = "  emit_vertex(0);\n"
               "  emit_vertex(1);\n"
               "  emit_vertex(2);\n"
               "  emit_vertex(0);\n"
               "  EndPrimitive();\n";
        break;
    case PRIM_TYPE_TRIANGLE_STRIP:
        *gl_primitive_mode = GL_TRIANGLE_STRIP;
        if (polygon_mode == POLY_MODE_FILL) { return NULL; }
        assert(polygon_mode == POLY_MODE_LINE);
        layout_in = "layout(triangles) in;\n";
        layout_out = "layout(line_strip, max_vertices = 4) out;\n";
        /* Imagine a quad made of a tristrip, the comments tell you which
         * vertex we are using */
        body = "  if ((gl_PrimitiveIDIn & 1) == 0) {\n"
               "    if (gl_PrimitiveIDIn == 0) {\n"
               "      emit_vertex(0);\n" /* bottom right */
               "    }\n"
               "    emit_vertex(1);\n" /* top right */
               "    emit_vertex(2);\n" /* bottom left */
               "    emit_vertex(0);\n" /* bottom right */
               "  } else {\n"
               "    emit_vertex(2);\n" /* bottom left */
               "    emit_vertex(1);\n" /* top left */
               "    emit_vertex(0);\n" /* top right */
               "  }\n"
               "  EndPrimitive();\n";
        break;
    case PRIM_TYPE_TRIANGLE_FAN:
        *gl_primitive_mode = GL_TRIANGLE_FAN;
        if (polygon_mode == POLY_MODE_FILL) { return NULL; }
        assert(polygon_mode == POLY_MODE_LINE);
        layout_in = "layout(triangles) in;\n";
        layout_out = "layout(line_strip, max_vertices = 4) out;\n";
        body = "  if (gl_PrimitiveIDIn == 0) {\n"
               "    emit_vertex(0);\n"
               "  }\n"
               "  emit_vertex(1);\n"
               "  emit_vertex(2);\n"
               "  emit_vertex(0);\n"
               "  EndPrimitive();\n";
        break;
    case PRIM_TYPE_QUADS:
        *gl_primitive_mode = GL_LINES_ADJACENCY;
        layout_in = "layout(lines_adjacency) in;\n";
        if (polygon_mode == POLY_MODE_LINE) {
            layout_out = "layout(line_strip, max_vertices = 5) out;\n";
            body = "  emit_vertex(0);\n"
                   "  emit_vertex(1);\n"
                   "  emit_vertex(2);\n"
                   "  emit_vertex(3);\n"
                   "  emit_vertex(0);\n"
                   "  EndPrimitive();\n";
        } else if (polygon_mode == POLY_MODE_FILL) {
            layout_out = "layout(triangle_strip, max_vertices = 4) out;\n";
            body = "  emit_vertex(0);\n"
                   "  emit_vertex(1);\n"
                   "  emit_vertex(3);\n"
                   "  emit_vertex(2);\n"
                   "  EndPrimitive();\n";
        } else {
            assert(false);
            return NULL;
        }
        break;
    case PRIM_TYPE_QUAD_STRIP:
        *gl_primitive_mode = GL_LINE_STRIP_ADJACENCY;
        layout_in = "layout(lines_adjacency) in;\n";
        if (polygon_mode == POLY_MODE_LINE) {
            layout_out = "layout(line_strip, max_vertices = 5) out;\n";
            body = "  if ((gl_PrimitiveIDIn & 1) != 0) { return; }\n"
                   "  if (gl_PrimitiveIDIn == 0) {\n"
                   "    emit_vertex(0);\n"
                   "  }\n"
                   "  emit_vertex(1);\n"
                   "  emit_vertex(3);\n"
                   "  emit_vertex(2);\n"
                   "  emit_vertex(0);\n"
                   "  EndPrimitive();\n";
        } else if (polygon_mode == POLY_MODE_FILL) {
            layout_out = "layout(triangle_strip, max_vertices = 4) out;\n";
            body = "  if ((gl_PrimitiveIDIn & 1) != 0) { return; }\n"
                   "  emit_vertex(0);\n"
                   "  emit_vertex(1);\n"
                   "  emit_vertex(2);\n"
                   "  emit_vertex(3);\n"
                   "  EndPrimitive();\n";
        } else {
            assert(false);
            return NULL;
        }
        break;
    case PRIM_TYPE_POLYGON:
        if (polygon_mode == POLY_MODE_LINE) {
            *gl_primitive_mode = GL_LINE_LOOP;
        } else if (polygon_mode == POLY_MODE_FILL) {
            *gl_primitive_mode = GL_TRIANGLE_FAN;
        } else {
            assert(false);
        }
        return NULL;
    default:
        assert(false);
        return NULL;
    }

    /* generate a geometry shader to support deprecated primitive types */
    assert(layout_in);
    assert(layout_out);
    assert(body);
    QString* s = qstring_from_str("#version 330\n"
                                  "\n");
    qstring_append(s, layout_in);
    qstring_append(s, layout_out);
    qstring_append(s, "\n"
                      STRUCT_VERTEX_DATA
                      "noperspective in VertexData v_vtx[];\n"
                      "noperspective out VertexData g_vtx;\n"
                      "\n"
                      "void emit_vertex(int index) {\n"
                      "  gl_Position = gl_in[index].gl_Position;\n"
                      "  gl_PointSize = gl_in[index].gl_PointSize;\n"
                      "  g_vtx = v_vtx[index];\n"
                      "  EmitVertex();\n"
                      "}\n"
                      "\n"
                      "void main() {\n");
    qstring_append(s, body);
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

static void generate_fixed_function(const ShaderState state,
                                    QString *header, QString *body)
{
    int i, j;

    /* generate vertex shader mimicking fixed function */
    qstring_append(header,
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

    qstring_append(header,
/* FIXME: Add these uniforms using code when they are used */
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
    qstring_append_fmt(body, "/* Skinning mode %d */\n",
                       state.skinning);

    append_skinning_code(body, mix, count, "vec4",
                         "tPosition", "position",
                         "modelViewMat", "xyzw");
    append_skinning_code(body, mix, count, "vec3",
                         "tNormal", "vec4(normal, 0.0)",
                         "invModelViewMat", "xyz");

    /* Normalization */
    if (state.normalization) {
        qstring_append(body, "tNormal = normalize(tNormal);\n");
    }

    /* Texgen */
    for (i = 0; i < 4 /* FIXME: NV2A_MAX_TEXTURES */; i++) {
        qstring_append_fmt(body, "/* Texgen for stage %d */\n",
                           i);
        /* Set each component individually */
        /* FIXME: could be nicer if some channels share the same texgen */
        for (j = 0; j < 4; j++) {
            /* TODO: TexGen View Model missing! */
            char c = "xyzw"[j];
            char cSuffix = "STRQ"[j];
            switch (state.texgen[i][j]) {
            case TEXGEN_DISABLE:
                qstring_append_fmt(body, "oT%d.%c = texture%d.%c;\n",
                                   i, c, i, c);
                break;
            case TEXGEN_EYE_LINEAR:
                qstring_append_fmt(body, "oT%d.%c = dot(texPlane%c%d, tPosition);\n",
                                   i, c, cSuffix, i);
                break;
            case TEXGEN_OBJECT_LINEAR:
                qstring_append_fmt(body, "oT%d.%c = dot(texPlane%c%d, position);\n",
                                   i, c, cSuffix, i);
                assert(false); /* Untested */
                break;
            case TEXGEN_SPHERE_MAP:
                assert(i < 2);  /* Channels S,T only! */
                qstring_append(body, "{\n");
                /* FIXME: u, r and m only have to be calculated once */
                qstring_append(body, "  vec3 u = normalize(tPosition.xyz);\n");
                //FIXME: tNormal before or after normalization? Always normalize?
                qstring_append(body, "  vec3 r = reflect(u, tNormal);\n");

                /* FIXME: This would consume 1 division fewer and *might* be
                 *        faster than length:
                 *   // [z=1/(2*x) => z=1/x*0.5]
                 *   vec3 ro = r + vec3(0.0, 0.0, 1.0);
                 *   float m = inversesqrt(dot(ro,ro))*0.5;
                 */

                qstring_append(body, "  float invM = 1.0 / (2.0 * length(r + vec3(0.0, 0.0, 1.0)));\n");
                qstring_append_fmt(body, "  oT%d.%c = r.%c * invM + 0.5;\n",
                                   i, c, c);
                qstring_append(body, "}\n");
                assert(false); /* Untested */
                break;
            case TEXGEN_REFLECTION_MAP:
                assert(i < 3); /* Channels S,T,R only! */
                qstring_append(body, "{\n");
                /* FIXME: u and r only have to be calculated once, can share the one from SPHERE_MAP */
                qstring_append(body, "  vec3 u = normalize(tPosition.xyz);\n");
                qstring_append(body, "  vec3 r = reflect(u, tNormal);\n");
                qstring_append_fmt(body, "  oT%d.%c = r.%c;\n",
                                   i, c, c);
                qstring_append(body, "}\n");
                break;
            case TEXGEN_NORMAL_MAP:
                assert(i < 3); /* Channels S,T,R only! */
                qstring_append_fmt(body, "oT%d.%c = tNormal.%c;\n",
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
            qstring_append_fmt(body,
                               "oT%d = oT%d * texMat%d;\n",
                               i, i, i);
        }
    }

    /* Lighting */
    if (state.lighting) {

        //FIXME: Do 2 passes if we want 2 sided-lighting?
        qstring_append_fmt(header, "uniform vec3 sceneAmbientColor;\n");
        qstring_append(body, "oD0 = vec4(sceneAmbientColor, diffuse.a);\n");
        qstring_append(body, "oD1 = vec4(0.0, 0.0, 0.0, specular.a);\n");

        /* FIXME: Only add if necessary */
        qstring_append(header,
            "uniform vec4 eyePosition;\n");

        for (i = 0; i < NV2A_MAX_LIGHTS; i++) {
            if (state.light[i] == LIGHT_OFF) {
                continue;
            }

            qstring_append_fmt(header,
                "uniform vec3 lightAmbientColor%d;\n"
                "uniform vec3 lightDiffuseColor%d;\n"
                "uniform vec3 lightSpecularColor%d;\n",
                i, i, i);

            /* FIXME: It seems that we only have to handle the surface colors if
             *        they are not part of the material [= vertex colors].
             *        If they are material the cpu will premultiply light
             *        colors
             */

            qstring_append_fmt(body, "/* Light %d */ {\n", i);

            qstring_append_fmt(header,
                "uniform float lightLocalRange%d;\n", i);

            if (state.light[i] == LIGHT_LOCAL
                    || state.light[i] == LIGHT_SPOT) {

                qstring_append_fmt(header,
                    "uniform vec3 lightLocalPosition%d;\n"
                    "uniform vec3 lightLocalAttenuation%d;\n",
                    i, i);
                qstring_append_fmt(body,
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

                qstring_append_fmt(header,
                    "uniform vec3 lightInfiniteHalfVector%d;\n"
                    "uniform vec3 lightInfiniteDirection%d;\n",
                    i, i);
                qstring_append_fmt(body,
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
                qstring_append_fmt(header,
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

            qstring_append_fmt(body,
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

            qstring_append(body,
                "  oD0.xyz += lightAmbient;\n");

            qstring_append(body,
                "  oD0.xyz += diffuse.xyz * lightDiffuse;\n");

            qstring_append(body,
                "  oD1.xyz += specular.xyz * lightSpecular;\n");

            qstring_append(body, "}\n");
        }
    } else {
        qstring_append(body, "  oD0 = diffuse;\n");
        qstring_append(body, "  oD1 = specular;\n");
    }
    qstring_append(body, "  oB0 = backDiffuse;\n");
    qstring_append(body, "  oB1 = backSpecular;\n");

    /* Fog */
    if (state.fog_enable) {

        /* From: https://www.opengl.org/registry/specs/NV/fog_distance.txt */
        switch(state.foggen) {
        case FOGGEN_SPEC_ALPHA:
            /* FIXME: Do we have to clamp here? */
            qstring_append(body, "  float fogDistance = clamp(specular.a, 0.0, 1.0);\n");
            break;
        case FOGGEN_RADIAL:
            qstring_append(body, "  float fogDistance = length(tPosition.xyz)");
            break;
        case FOGGEN_PLANAR:
        case FOGGEN_ABS_PLANAR:
            qstring_append(body, "  float fogDistance = dot(fogPlane.xyz, tPosition.xyz) + fogPlane.w;\n");
            if (state.foggen == FOGGEN_ABS_PLANAR) {
                qstring_append(body, "  fogDistance = abs(fogDistance);\n");
            }
            break;
        case FOGGEN_FOG_X:
            qstring_append(body, "  float fogDistance = fogCoord;\n");
            break;
        default:
            assert(false);
            break;
        }

    }

    /* If skinning is off the composite matrix already includes the MV matrix */
    if (state.skinning == SKINNING_OFF) {
        qstring_append(body, "  tPosition = position;\n");
    }

    qstring_append(body,
    "   oPos = invViewport * (tPosition * compositeMat);\n"
    "   oPos.z = oPos.z * 2.0 - oPos.w;\n");

    qstring_append(body, "  vtx.inv_w = 1.0 / oPos.w;\n");

}

static QString *generate_vertex_shader(const ShaderState state,
                                       char vtx_prefix)
{
    int i;
    QString *header = qstring_from_str("#version 330\n"
                                  "\n"
                                  "uniform vec2 clipRange;\n"
                                  "uniform vec2 surfaceSize;\n"
                                  "\n"
                                  /* All constants in 1 array declaration */
                                  "layout(shared) uniform VertexConstants {\n"
                                  "  uniform vec4 c[192];\n"
                                  "};\n"
                                  "\n"
                                  /* FIXME: Most [all?] of these are probably part of the constant space */
                                  "uniform vec4 fogColor;\n"
                                  "uniform vec4 fogPlane;\n"
                                  "uniform float fogParam[2];\n"
                                  "\n"
                                  "vec4 oPos = vec4(0.0,0.0,0.0,1.0);\n"
                                  "vec4 oD0 = vec4(0.0,0.0,0.0,1.0);\n"
                                  "vec4 oD1 = vec4(0.0,0.0,0.0,1.0);\n"
                                  "vec4 oB0 = vec4(0.0,0.0,0.0,1.0);\n"
                                  "vec4 oB1 = vec4(0.0,0.0,0.0,1.0);\n"
                                  "vec4 oPts = vec4(0.0,0.0,0.0,1.0);\n"
    /* FIXME: NV_vertex_program says: "FOGC is the transformed vertex's fog
     * coordinate. The register's first floating-point component is interpolated
     * across the assembled primitive during rasterization and used as the fog
     * distance to compute per-fragment the fog factor when fog is enabled.
     * However, if both fog and vertex program mode are enabled, but the FOGC
     * vertex result register is not written, the fog factor is overridden to
     * 1.0. The register's other three components are ignored."
     *
     * That probably means it will read back as vec4(0.0, 0.0, 0.0, 1.0) but
     * will be set to 1.0 AFTER the VP if it was never written?
     * We should test on real hardware..
     *
     * We'll force 1.0 for oFog.x for now.
     */
                                  "vec4 oFog = vec4(1.0,0.0,0.0,1.0);\n"
                                  "vec4 oT0 = vec4(0.0,0.0,0.0,1.0);\n"
                                  "vec4 oT1 = vec4(0.0,0.0,0.0,1.0);\n"
                                  "vec4 oT2 = vec4(0.0,0.0,0.0,1.0);\n"
                                  "vec4 oT3 = vec4(0.0,0.0,0.0,1.0);\n"
                                  "\n"
                                  STRUCT_VERTEX_DATA);
    qstring_append_fmt(header, "noperspective out VertexData %c_vtx;\n",
                       vtx_prefix);
    qstring_append_fmt(header, "#define vtx %c_vtx\n",
                       vtx_prefix);
    qstring_append(header, "\n");
    for(i = 0; i < 16; i++) {
        qstring_append_fmt(header, "in vec4 v%d;\n", i);
    }
    qstring_append(header, "\n");

    QString *body = qstring_from_str("void main() {\n");

    if (state.fixed_function) {
        generate_fixed_function(state, header, body);

    } else if (state.vertex_program) {
        vsh_translate(VSH_VERSION_XVS,
                      (uint32_t*)state.program_data,
                      state.program_length,
                      state.z_perspective,
                      header, body);
    } else {
        assert(false);
    }


    /* Fog */

    if (state.fog_enable) {

        if (state.vertex_program) {
            /* FIXME: Does foggen do something here? Let's do some tracking..
             *
             *   "RollerCoaster Tycoon" has
             *      state.vertex_program = true; state.foggen == FOGGEN_PLANAR
             *      but expects oFog.x as fogdistance?! Writes oFog.xyzw = v0.z
             */
            qstring_append(body, "  float fogDistance = oFog.x;\n");
        }

        /* FIXME: Do this per pixel? */

        switch (state.fog_mode) {
        case FOG_MODE_LINEAR:
        case FOG_MODE_LINEAR_ABS:

            /* f = (end - d) / (end - start)
             *    fogParam[1] = 1 / (end - start)
             *    fogParam[0] = 1 + end * fogParam[1];
             */

            qstring_append(body, "  float fogFactor = fogParam[0] + fogDistance * fogParam[1];\n");
            qstring_append(body, "  fogFactor -= 1.0;\n"); /* FIXME: WHHYYY?!! */
            break;
        case FOG_MODE_EXP:
        case FOG_MODE_EXP_ABS:

            /* f = 1 / (e^(d * density))
             *    fogParam[1] = -density / (2 * ln(256))
             *    fogParam[0] = 1.5
             */

            qstring_append(body, "  float fogFactor = fogParam[0] + exp2(fogDistance * fogParam[1] * 16.0);\n");
            qstring_append(body, "  fogFactor -= 1.5;\n"); /* FIXME: WHHYYY?!! */
            break;
        case FOG_MODE_EXP2:
        case FOG_MODE_EXP2_ABS:

            /* f = 1 / (e^((d * density)^2))
             *    fogParam[1] = -density / (2 * sqrt(ln(256)))
             *    fogParam[0] = 1.5
             */

            qstring_append(body, "  float fogFactor = fogParam[0] + exp2(-fogDistance * fogDistance * fogParam[1] * fogParam[1] * 32.0);\n");
            qstring_append(body, "  fogFactor -= 1.5;\n"); /* FIXME: WHHYYY?!! */
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
            qstring_append(body, "  fogFactor = abs(fogFactor);\n");
            break;
        default:
            break;
        }
        /* FIXME: What about fog alpha?! */
        qstring_append(body, "  oFog.xyzw = vec4(fogFactor);\n");
    } else {
        /* FIXME: Is the fog still calculated / passed somehow?!
         */
        qstring_append(body, "  oFog.xyzw = vec4(1.0);\n");
    }

    /* Set outputs */
    qstring_append(body, "\n"
                      "  vtx.D0 = clamp(oD0, 0.0, 1.0) * vtx.inv_w;\n"
                      "  vtx.D1 = clamp(oD1, 0.0, 1.0) * vtx.inv_w;\n"
                      "  vtx.B0 = clamp(oB0, 0.0, 1.0) * vtx.inv_w;\n"
                      "  vtx.B1 = clamp(oB1, 0.0, 1.0) * vtx.inv_w;\n"
                      "  vtx.Fog = oFog.x * vtx.inv_w;\n"
                      "  vtx.T0 = oT0 * vtx.inv_w;\n"
                      "  vtx.T1 = oT1 * vtx.inv_w;\n"
                      "  vtx.T2 = oT2 * vtx.inv_w;\n"
                      "  vtx.T3 = oT3 * vtx.inv_w;\n"
                      "  gl_Position = oPos;\n"
                      "  gl_PointSize = oPts.x;\n"
                      "\n"
                      "}\n");


    /* Return combined header + source */
    qstring_append(header, qstring_get_str(body));
    QDECREF(body);
    return header;

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
    char vtx_prefix;
    GLuint program = glCreateProgram();

    /* Create an option geometry shader and find primitive type */

    GLenum gl_primitive_mode;
    QString* geometry_shader_code =
        generate_geometry_shader(state.polygon_front_mode,
                                 state.polygon_back_mode,
                                 state.primitive_mode,
                                 &gl_primitive_mode);
    if (geometry_shader_code) {
        const char* geometry_shader_code_str =
             qstring_get_str(geometry_shader_code);

        GLuint geometry_shader = create_gl_shader(GL_GEOMETRY_SHADER,
                                                  geometry_shader_code_str,
                                                  "geometry shader");
        glAttachShader(program, geometry_shader);

        QDECREF(geometry_shader_code);

        vtx_prefix = 'v';
    } else {
        vtx_prefix = 'g';
    }

    /* create the vertex shader */

    QString *vertex_shader_code = generate_vertex_shader(state, vtx_prefix);
    GLuint vertex_shader = create_gl_shader(GL_VERTEX_SHADER,
                                            qstring_get_str(vertex_shader_code),
                                            "vertex shader");
    glAttachShader(program, vertex_shader);
    QDECREF(vertex_shader_code);


    /* Bind attributes for vertices */
    char tmp[8];
    for(i = 0; i < 16; i++) {
        snprintf(tmp, sizeof(tmp), "v%d", i);
        glBindAttribLocation(program, i, tmp);
    }


    /* generate a fragment shader from register combiners */

    QString *fragment_shader_code = psh_translate(state.psh);

    const char *fragment_shader_code_str = qstring_get_str(fragment_shader_code);

    GLuint fragment_shader = create_gl_shader(GL_FRAGMENT_SHADER,
                                              fragment_shader_code_str,
                                              "fragment shader");
    glAttachShader(program, fragment_shader);

    QDECREF(fragment_shader_code);


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
    ret->gl_primitive_mode = gl_primitive_mode;

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
