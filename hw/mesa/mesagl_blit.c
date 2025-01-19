/*
 * QEMU MESA GL Pass-Through
 *
 *  Copyright (c) ... in a Galaxy far, far away ...
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library;
 * if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"

#include "mesagl_impl.h"

int mesa_gui_fullscreen(const void *);

static struct {
    unsigned vao, vbo;
    int prog, vert, frag, black;
    int adj, flip;
} blit;
static unsigned blit_program_setup(void)
{
    MESA_PFN(PFNGLATTACHSHADERPROC,       glAttachShader);
    MESA_PFN(PFNGLBINDATTRIBLOCATIONPROC, glBindAttribLocation);
    MESA_PFN(PFNGLCOMPILESHADERPROC,      glCompileShader);
    MESA_PFN(PFNGLCREATEPROGRAMPROC,      glCreateProgram);
    MESA_PFN(PFNGLCREATESHADERPROC,       glCreateShader);
    MESA_PFN(PFNGLGETINTEGERVPROC,        glGetIntegerv);
    MESA_PFN(PFNGLGETSTRINGPROC,          glGetString);
    MESA_PFN(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation);
    MESA_PFN(PFNGLLINKPROGRAMPROC,        glLinkProgram);
    MESA_PFN(PFNGLSHADERSOURCEPROC,       glShaderSource);
    MESA_PFN(PFNGLUSEPROGRAMPROC,         glUseProgram);
    const char *vert_src[] = {
        "#version 120\n"
        "attribute vec2 in_position;\n"
        "varying vec2 texcoord;\n"
        "void main() {\n"
        "  texcoord = vec2(1 + in_position.x, 1 + in_position.y) * 0.5;\n"
        "  gl_Position = vec4(in_position, 0, 1);\n"
        "}\n",
        "#version 140\n"
        "#extension GL_ARB_explicit_attrib_location : require\n"
        "layout (location = 0) in vec2 in_position;\n"
        "out vec2 texcoord;\n"
        "void main() {\n"
        "  texcoord = vec2(1 + in_position.x, 1 + in_position.y) * 0.5;\n"
        "  gl_Position = vec4(in_position, 0, 1);\n"
        "}\n"
    };
    const char *frag_src[] = {
        "#version 120\n"
        "uniform sampler2D screen_texture;\n"
        "uniform bool frag_just_black;\n"
        "varying vec2 texcoord;\n"
        "void main() {\n"
        "  if (frag_just_black)\n"
        "    gl_FragColor = vec4(0,0,0,1);\n"
        "  else\n"
        "    gl_FragColor = texture2D(screen_texture, texcoord);\n"
        "}\n",
        "#version 140\n"
        "uniform sampler2D screen_texture;\n"
        "uniform bool frag_just_black;\n"
        "in vec2 texcoord;\n"
        "out vec4 fragColor;\n"
        "void main() {\n"
        "  if (frag_just_black)\n"
        "    fragColor = vec4(0,0,0,1);\n"
        "  else\n"
        "    fragColor = texture(screen_texture, texcoord);\n"
        "}\n"
    };
    int prog;
    if (!blit.prog) {
        int i = memcmp(PFN_CALL(glGetString(GL_VERSION)), "2.1 Metal",
                sizeof("2.1 Metal") - 1)? 1:0,
            srclen = ALIGNED((strlen(vert_src[i])+1));
        char *srcbuf = g_new0(char, srclen);
        const char *vert_buf[] = { srcbuf };
        strncpy(srcbuf, vert_src[i], srclen);
        if (blit.flip) {
            char *flip = strstr(srcbuf, "+ in_position.y");
            *flip = '-';
        }
        blit.vert = PFN_CALL(glCreateShader(GL_VERTEX_SHADER));
        PFN_CALL(glShaderSource(blit.vert, 1, vert_buf, 0));
        PFN_CALL(glCompileShader(blit.vert));
        g_free(srcbuf);
        blit.frag = PFN_CALL(glCreateShader(GL_FRAGMENT_SHADER));
        PFN_CALL(glShaderSource(blit.frag, 1, &frag_src[i], 0));
        PFN_CALL(glCompileShader(blit.frag));
        prog = PFN_CALL(glCreateProgram());
        PFN_CALL(glAttachShader(prog, blit.vert));
        PFN_CALL(glAttachShader(prog, blit.frag));
        if (!i)
            PFN_CALL(glBindAttribLocation(prog, 0, "in_position"));
        PFN_CALL(glLinkProgram(prog));
        blit.prog = prog;
    }
    PFN_CALL(glGetIntegerv(GL_CURRENT_PROGRAM, &prog));
    PFN_CALL(glUseProgram(blit.prog));
    blit.black = PFN_CALL(glGetUniformLocation(blit.prog, "frag_just_black"));
    return prog;
}
void MesaBlitFree(void)
{
    MESA_PFN(PFNGLDELETEBUFFERSPROC,      glDeleteBuffers);
    MESA_PFN(PFNGLDELETEPROGRAMPROC,      glDeleteProgram);
    MESA_PFN(PFNGLDELETESHADERPROC,       glDeleteShader);
    MESA_PFN(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays);
    if (blit.prog) {
        PFN_CALL(glDeleteProgram(blit.prog));
        PFN_CALL(glDeleteShader(blit.vert));
        PFN_CALL(glDeleteShader(blit.frag));
    }
    if (blit.vbo)
        PFN_CALL(glDeleteBuffers(1, &blit.vbo));
    if (blit.vao)
        PFN_CALL(glDeleteVertexArrays(1, &blit.vao));
    memset(&blit, 0, sizeof(blit));
}
struct save_states {
    int view[4];
    int draw_binding, read_binding, texture, texture_binding,
        vao_binding, vbo_binding, boolean_map;
};
#define FRAMEBUFFER_SRGB_(s) \
    (s.boolean_map & 2)
struct states_mapping {
    int gl_enum, *iv;
};
static const int boolean_states[] = {
    GL_FRAMEBUFFER_SRGB,
    GL_BLEND,
    GL_CULL_FACE,
    GL_DEPTH_TEST,
    GL_SCISSOR_TEST,
    GL_STENCIL_TEST,
    0,
};
static int blit_program_buffer(void *save_map, const int size, const void *data)
{
    MESA_PFN(PFNGLBINDBUFFERPROC,      glBindBuffer);
    MESA_PFN(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray);
    MESA_PFN(PFNGLBUFFERDATAPROC,      glBufferData);
    MESA_PFN(PFNGLDISABLEPROC,         glDisable);
    MESA_PFN(PFNGLGENBUFFERSPROC,      glGenBuffers);
    MESA_PFN(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays);
    MESA_PFN(PFNGLGETINTEGERVPROC,     glGetIntegerv);
    MESA_PFN(PFNGLISENABLEDPROC,       glIsEnabled);

    struct save_states *last = (struct save_states *)save_map;

    struct states_mapping mapping[] = {
        { GL_VIEWPORT, last->view },
        { GL_FRAMEBUFFER_BINDING, &last->draw_binding },
        { GL_READ_FRAMEBUFFER_BINDING, &last->read_binding },
        { GL_ACTIVE_TEXTURE, &last->texture },
        { GL_TEXTURE_BINDING_2D, &last->texture_binding },
        { GL_VERTEX_ARRAY_BINDING, &last->vao_binding },
        { GL_ARRAY_BUFFER_BINDING, &last->vbo_binding },
        { GL_CONTEXT_PROFILE_MASK, &last->boolean_map },
        { 0, 0 },
    };
    for (int i = 0; mapping[i].gl_enum; i++)
        PFN_CALL(glGetIntegerv(mapping[i].gl_enum, mapping[i].iv));
    last->boolean_map &= GL_CONTEXT_CORE_PROFILE_BIT;

    for (int i = 0; boolean_states[i]; i++) {
        last->boolean_map |= PFN_CALL(glIsEnabled(boolean_states[i]))? (2 << i):0;
        if (last->boolean_map & (2 << i))
            PFN_CALL(glDisable(boolean_states[i]));
    }
    if (last->boolean_map & GL_CONTEXT_CORE_PROFILE_BIT) {
        if (!blit.vao)
            PFN_CALL(glGenVertexArrays(1, &blit.vao));
        PFN_CALL(glBindVertexArray(blit.vao));
    }
    if (!blit.vbo)
        PFN_CALL(glGenBuffers(1, &blit.vbo));
    PFN_CALL(glBindBuffer(GL_ARRAY_BUFFER, blit.vbo));
    PFN_CALL(glBufferData(GL_ARRAY_BUFFER, size, data, GL_STATIC_DRAW));
    return 0;
}
static void blit_restore_savemap(const void *save_map)
{
    MESA_PFN(PFNGLBINDBUFFERPROC,               glBindBuffer);
    MESA_PFN(PFNGLBINDVERTEXARRAYPROC,          glBindVertexArray);
    MESA_PFN(PFNGLENABLEPROC,                   glEnable);

    struct save_states *last = (struct save_states *)save_map;

    if (last->boolean_map & GL_CONTEXT_CORE_PROFILE_BIT)
        PFN_CALL(glBindVertexArray(last->vao_binding));

    PFN_CALL(glBindBuffer(GL_ARRAY_BUFFER, last->vbo_binding));

    for (int i = 0; boolean_states[i]; i++) {
        if ((boolean_states[i] == GL_FRAMEBUFFER_SRGB)
                && !(last->read_binding == last->draw_binding))
            continue;
        if (last->boolean_map & (2 << i))
            PFN_CALL(glEnable(boolean_states[i]));
    }
}
void MesaBlitScale(void)
{
    MESA_PFN(PFNGLACTIVETEXTUREPROC,            glActiveTexture);
    MESA_PFN(PFNGLBINDTEXTUREPROC,              glBindTexture);
    MESA_PFN(PFNGLBLITFRAMEBUFFERPROC,          glBlitFramebuffer);
    MESA_PFN(PFNGLCOPYTEXIMAGE2DPROC,           glCopyTexImage2D);
    MESA_PFN(PFNGLDELETETEXTURESPROC,           glDeleteTextures);
    MESA_PFN(PFNGLDISABLEVERTEXATTRIBARRAYPROC, glDisableVertexAttribArray);
    MESA_PFN(PFNGLDRAWARRAYSPROC,               glDrawArrays);
    MESA_PFN(PFNGLENABLEPROC,                   glEnable);
    MESA_PFN(PFNGLENABLEVERTEXATTRIBARRAYPROC,  glEnableVertexAttribArray);
    MESA_PFN(PFNGLGENTEXTURESPROC,              glGenTextures);
    MESA_PFN(PFNGLTEXPARAMETERIPROC,            glTexParameteri);
    MESA_PFN(PFNGLUNIFORM1IPROC,                glUniform1i);
    MESA_PFN(PFNGLUSEPROGRAMPROC,               glUseProgram);
    MESA_PFN(PFNGLVERTEXATTRIBPOINTERPROC,      glVertexAttribPointer);
    MESA_PFN(PFNGLVIEWPORTPROC,                 glViewport);

    int v[4], fullscreen = mesa_gui_fullscreen(v);

    if (blit.adj) {
        blit.adj = !blit.adj;
        return;
    }
    blit.flip = ScalerBlitFlip();

    if (DrawableContext() && ((!fullscreen && (v[3] > (v[1] & 0x7FFFU)))
            || RenderScalerOff())) {
        unsigned screen_texture, w = v[0], h = v[1] & 0x7FFFU,
                last_prog = blit_program_setup();
        int aspect = (v[1] & (1 << 15))? 0:1,
                offs_x = v[2] - ((v[0] * 1.f * v[3]) / (v[1] & 0x7FFFU));
        offs_x >>= 1;
        v[0] *= (1.f * v[3]) / (v[1] & 0x7FFFU);
        v[1] = v[3];
        const float coord[] = {
            1-((1.f * v[2] - v[0]) / v[2]),-1,  1,-1,
            1-((1.f * v[2] - v[0]) / v[2]), 1,  1, 1,
            -1,-1, ((1.f * v[2] - v[0]) / v[2])-1,-1,
            -1, 1, ((1.f * v[2] - v[0]) / v[2])-1, 1,
            -1,-1,  1,-1,  -1,1,  1,1,
        };

        struct save_states save_map;

        if (!blit_program_buffer(&save_map, sizeof(coord), coord)) {
            PFN_CALL(glUniform1i(blit.black, GL_TRUE));
            PFN_CALL(glViewport(0,0,  v[2], v[3]));
            PFN_CALL(glEnableVertexAttribArray(0));
            PFN_CALL(glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0));
            if (save_map.read_binding == save_map.draw_binding) {
                PFN_CALL(glActiveTexture(GL_TEXTURE0));
                PFN_CALL(glGenTextures(1, &screen_texture));
                PFN_CALL(glBindTexture(GL_TEXTURE_2D, screen_texture));
                PFN_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
                PFN_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
                PFN_CALL(glCopyTexImage2D(GL_TEXTURE_2D, 0, (FRAMEBUFFER_SRGB_(save_map) && ScalerSRGBCorr())?
                            GL_SRGB:GL_RGBA, 0,0, w,h, 0));
                if (aspect) {
                    PFN_CALL(glDrawArrays(GL_TRIANGLE_STRIP, 0, 4)); /* clear */
                    PFN_CALL(glDrawArrays(GL_TRIANGLE_STRIP, 4, 4)); /* clear */
                    PFN_CALL(glViewport(offs_x,0,  v[0],v[1]));
                }
                PFN_CALL(glUniform1i(blit.black, GL_FALSE));
                PFN_CALL(glDrawArrays(GL_TRIANGLE_STRIP, 8, 4)); /* scale */
                PFN_CALL(glDeleteTextures(1, &screen_texture));
                PFN_CALL(glActiveTexture(save_map.texture));
                PFN_CALL(glBindTexture(GL_TEXTURE_2D, save_map.texture_binding));
            }
            else {
                if (FRAMEBUFFER_SRGB_(save_map))
                    PFN_CALL(glEnable(boolean_states[0]));
                if (aspect) {
                    PFN_CALL(glDrawArrays(GL_TRIANGLE_STRIP, 0, 4)); /* clear */
                    PFN_CALL(glDrawArrays(GL_TRIANGLE_STRIP, 4, 4)); /* clear */
                    PFN_CALL(glBlitFramebuffer(0,0,w,h, offs_x,v[1],v[0]+offs_x,0,
                        (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT),
                        GL_NEAREST));
                }
                else
                    PFN_CALL(glBlitFramebuffer(0,0,w,h, 0,v[3],v[2],0,
                        (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT),
                        GL_NEAREST));
            }
            PFN_CALL(glDisableVertexAttribArray(0));
            PFN_CALL(glViewport(save_map.view[0], save_map.view[1],
                                save_map.view[2], save_map.view[3]));
            blit_restore_savemap(&save_map);
        }
        PFN_CALL(glUseProgram(last_prog));
    }
}

void MesaRenderScaler(const uint32_t FEnum, void *args)
{
    MESA_PFN(PFNGLGETINTEGERVPROC, glGetIntegerv);
    int v[4], fullscreen, framebuffer_binding, aspect, blit_adj = 0;
    uint32_t *box;

    PFN_CALL(glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer_binding));
    fullscreen = mesa_gui_fullscreen(v);
    aspect = (v[1] & (1 << 15))? 0:1;

    switch(FEnum) {
        case FEnum_glBlitFramebuffer:
        case FEnum_glBlitFramebufferEXT:
            box = &((uint32_t *)args)[4];
            blit_adj = 1;
            break;
        case FEnum_glScissor:
        case FEnum_glViewport:
            box = args;
            break;
        default:
            return;
    }
    if (DrawableContext() && !framebuffer_binding
            && fullscreen && !RenderScalerOff()) {
        int offs_x = v[2] - ((1.f * v[0] * v[3]) / (v[1] & 0x7FFFU));
        offs_x >>= 1;
        for (int i = 0; i < 4; i++)
            box[i] *= (1.f * v[3]) / (v[1] & 0x7FFFU);
        if (aspect) {
            box[0] += offs_x;
            box[2] += (blit_adj)? box[0]:0;
        }
        else {
            box[0] *= (1.f * v[2]) / box[2];
            box[2] = v[2];
        }
        blit.adj = blit_adj;
    }
}

