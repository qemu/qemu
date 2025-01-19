/*
 * QEMU MESA GL Pass-Through
 *
 *  Copyright (c) 2020
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
#include "hw/hw.h"

#include "mesagl_impl.h"

#define DEBUG_MESAGL

#ifdef DEBUG_MESAGL
#define DPRINTF(fmt, ...) \
    do { fprintf(stderr, "mgl_trace: " fmt "\n" , ## __VA_ARGS__); } while(0)
#else
#define DPRINTF(fmt, ...)
#endif

#if defined(CONFIG_LINUX) || defined(CONFIG_DARWIN)
#include <dlfcn.h>
  #if defined(HOST_X86_64) || defined(HOST_AARCH64)
  #define __stdcall
  #endif
#endif

#define MESAGLCFG "mesagl.cfg"
#include "fgfont.h"
#include "mglfptbl.h"

static int getNumArgs(const char *sym)
{
    char *p = (char *)sym;
    while (*p != '@') p++;
    return (atoi(++p) >> 2);
}

int GLFEnumArgsCnt(const int FEnum)
{
    return getNumArgs(tblMesaGL[FEnum].sym);
}

void * GLFEnumFuncPtr(const int FEnum)
{
    return (void *)tblMesaGL[FEnum].ptr;
}

int ExtFuncIsValid(const char *name)
{
    int i;
    for (i = 0; i < FEnum_zzMGLFuncEnum_max; i++) {
        char func[64];
        strncpy(func, tblMesaGL[i].sym + 1, sizeof(func)-1);
        for (int j = 0; j < sizeof(func); j++) {
            if (func[j] == '@') {
                func[j] = 0;
                break;
            }
        }
        if (!strncmp(func, name, sizeof(func)))
            break;
    }
    return (i == FEnum_zzMGLFuncEnum_max)? 0:((tblMesaGL[i].ptr)? 1:0);
}

int GLIsD3D12(void)
{
    MESA_PFN(PFNGLGETSTRINGPROC,glGetString);
    const unsigned char d3d12[] = "D3D12",
          *str = PFN_CALL(glGetString(GL_RENDERER));
    return (!memcmp(str, d3d12, sizeof(d3d12) - 1));
}

int wrMapOrderPoints(uint32_t target)
{
    MESA_PFN(PFNGLGETMAPIVPROC, glGetMapiv);
    int v[2] = {1, 1};
    PFN_CALL(glGetMapiv(target, GL_ORDER, v));
    return (v[0]*v[1]);
}

int wrSizeTexture(const int target, const int level, const int compressed)
{
    MESA_PFN(PFNGLGETTEXLEVELPARAMETERIVPROC,glGetTexLevelParameteriv);
    int ret;
    if (compressed)
        PFN_CALL(glGetTexLevelParameteriv(target, level, GL_TEXTURE_COMPRESSED_IMAGE_SIZE, &ret));
    else {
        int w, h, d;
        PFN_CALL(glGetTexLevelParameteriv(target, level, GL_TEXTURE_WIDTH, &w));
        PFN_CALL(glGetTexLevelParameteriv(target, level, GL_TEXTURE_HEIGHT, &h));
        PFN_CALL(glGetTexLevelParameteriv(target, level, GL_TEXTURE_DEPTH, &d));
        ret = (w * h * d);
    }
    return ret;
}

int wrSizeMapBuffer(const int target)
{
    MESA_PFN(PFNGLGETBUFFERPARAMETERIVPROC, glGetBufferParameteriv);
    int ret;
    PFN_CALL(glGetBufferParameteriv(target, GL_BUFFER_SIZE, &ret));
    return ret;
}

void wrCompileShaderStatus(const int shader)
{
    MESA_PFN(PFNGLGETSHADERIVPROC, glGetShaderiv);
    MESA_PFN(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog);
    int status, length, type;
    char *errmsg;
    PFN_CALL(glGetShaderiv(shader, GL_SHADER_TYPE, &type));
    PFN_CALL(glGetShaderiv(shader, GL_SHADER_TYPE, &type));
    PFN_CALL(glGetShaderiv(shader, GL_COMPILE_STATUS, &status));
    if (!status) {
        PFN_CALL(glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length));
        errmsg = g_malloc(length);
        PFN_CALL(glGetShaderInfoLog(shader, length, &length, errmsg));
        fprintf(stderr, "%s\n", errmsg);
        g_free(errmsg);
    }
    DPRINTF("%s shader compilation %s", (type == GL_VERTEX_SHADER)? "vertex":"fragment",
        (status)? "PASS":"FAIL");
}

void wrFillBufObj(uint32_t target, void *dst, mapbufo_t *bufo)
{
    MESA_PFN(PFNGLMAPBUFFERRANGEPROC, glMapBufferRange);
    MESA_PFN(PFNGLMAPBUFFERPROC, glMapBuffer);
    MESA_PFN(PFNGLUNMAPBUFFERPROC, glUnmapBuffer);
    void *src;

    if (MGLUpdateGuestBufo(0, 0))
        return;

    switch (target) {
        case GL_PIXEL_UNPACK_BUFFER:
            break;
        default:
            src = (bufo->range)?
                PFN_CALL(glMapBufferRange(target, bufo->offst, bufo->range, GL_MAP_READ_BIT)):
                PFN_CALL(glMapBuffer(target, GL_READ_ONLY));
            if (src) {
                uint32_t szBuf = (bufo->range)? bufo->range:bufo->mapsz;
                memcpy((dst - ALIGNBO(szBuf)), src, szBuf);
                PFN_CALL(glUnmapBuffer(target));
            }
            break;
    }
}

void wrFlushBufObj(uint32_t target, mapbufo_t *bufo)
{
    if (MGLUpdateGuestBufo(0, 0))
        return;

    if (bufo->hva) {
        uint32_t szBuf = (bufo->range)? bufo->range:(bufo->mapsz - bufo->offst);
        memcpy((void *)(bufo->hva + bufo->offst), (void *)(bufo->gpa - ALIGNBO(bufo->mapsz) + bufo->offst), szBuf);
    }
}

void wrContextSRGB(int use_srgb)
{
    MESA_PFN(PFNGLENABLEPROC, glEnable);
    if (use_srgb)
        PFN_CALL(glEnable(GL_FRAMEBUFFER_SRGB));
}

void fgFontGenList(int first, int count, uint32_t listBase)
{
    MESA_PFN(PFNGLBITMAPPROC, glBitmap);
    MESA_PFN(PFNGLGETINTEGERVPROC, glGetIntegerv);
    MESA_PFN(PFNGLNEWLISTPROC, glNewList);
    MESA_PFN(PFNGLPIXELSTOREIPROC, glPixelStorei);
    MESA_PFN(PFNGLENDLISTPROC, glEndList);

    const SFG_Font *font = &fgFontFixed8x13;
    int org_alignment;
    PFN_CALL(glGetIntegerv(GL_UNPACK_ALIGNMENT, &org_alignment));
    PFN_CALL(glPixelStorei(GL_UNPACK_ALIGNMENT, 1));
    for (int i = first; i < (first + count); i++) {
        const unsigned char *face = font->Characters[i];
        PFN_CALL(glNewList(listBase++, GL_COMPILE));
        PFN_CALL(glBitmap(
            face[ 0 ], font->Height,
            font->xorig, font->yorig,
            ( float )( face [ 0 ] ), 0.0,
            ( face + 1 )
        ));
        PFN_CALL(glEndList());
    }
    PFN_CALL(glPixelStorei(GL_UNPACK_ALIGNMENT, org_alignment));
}

const char *getGLFuncStr(int FEnum)
{
    if (tblMesaGL[FEnum].impl == 0) {
        tblMesaGL[FEnum].impl = GLFuncTrace()? (2 - GLFuncTrace()):1;
        return tblMesaGL[FEnum].sym;
    }
    return 0;
}

void doMesaFunc(int FEnum, uint32_t *arg, uintptr_t *parg, uintptr_t *ret)
{
    int numArgs = getNumArgs(tblMesaGL[FEnum].sym);

    if (GLFuncTrace()) {
        const char *fstr = getGLFuncStr(FEnum);
        if (fstr) {
            DPRINTF("%-64s", fstr);
        }
    }

    /* Handle special GL funcs */
#define GLDONE() \
    numArgs = -1; break

    typedef union {
        /* ptr func proto */
        uint32_t (__stdcall *fpa0p1)(uint32_t, uintptr_t);
        uint32_t (__stdcall *fpa0p2)(uint32_t, uintptr_t, uintptr_t);
        uint32_t (__stdcall *fpa0p2a3)(uint32_t, uintptr_t, uintptr_t, uint32_t);
        uint32_t (__stdcall *fpa0p3)(uint32_t, uintptr_t, uintptr_t, uintptr_t);
        uint32_t (__stdcall *fpa1p2)(uint32_t, uint32_t, uintptr_t);
        uint32_t (__stdcall *fpa1p2a3)(uint32_t, uint32_t, uintptr_t, uint32_t);
        uint32_t (__stdcall *fpa1p3)(uint32_t, uint32_t, uintptr_t, uintptr_t);
        uint32_t (__stdcall *fpa2p3)(uint32_t, uint32_t, uint32_t, uintptr_t);
        uint32_t (__stdcall *fpa2p3a4)(uint32_t, uint32_t, uint32_t, uintptr_t, uint32_t);
        uint32_t (__stdcall *fpa2p3a5)(uint32_t, uint32_t, uint32_t, uintptr_t, uint32_t, uint32_t);
        uint32_t (__stdcall *fpa2p3a6)(uint32_t, uint32_t, uint32_t, uintptr_t, uint32_t, uint32_t, uint32_t);
        uint32_t (__stdcall *fpa2p4)(uint32_t, uint32_t, uint32_t, uintptr_t, uintptr_t);
        uint32_t (__stdcall *fpa2p6)(uint32_t, uint32_t, uint32_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
        uint32_t (__stdcall *fpa3p4)(uint32_t, uint32_t, uint32_t, uint32_t, uintptr_t);
        uint32_t (__stdcall *fpa4p5)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uintptr_t);
        uint32_t (__stdcall *fpa4p5a6)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uintptr_t, uint32_t);
        uint32_t (__stdcall *fpa5p6)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uintptr_t);
        uint32_t (__stdcall *fpa6p7)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uintptr_t);
        uint32_t (__stdcall *fpa7p8)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uintptr_t);
        uint32_t (__stdcall *fpa8p9)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uintptr_t);
        uint32_t (__stdcall *fpa9p10)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uintptr_t);
        uint32_t (__stdcall *fpp0)(uintptr_t);
        uint32_t (__stdcall *fpp1)(uintptr_t, uintptr_t);
        /* return ptr proto */
        uintptr_t (__stdcall *rpfpa0)(uint32_t);
        uintptr_t (__stdcall *rpfpa0p2a3)(uint32_t, uintptr_t, uintptr_t, uint32_t);
        uintptr_t (__stdcall *rpfpa1)(uint32_t, uint32_t);
        /* int64 func proto */
        uint32_t (__stdcall *fpx2)(uintptr_t);
        uint32_t (__stdcall *fpa1x2)(uintptr_t, uint32_t, uint64_t);
        /* float func proto */
        uint32_t (__stdcall *fpa0f1)(uint32_t, float);
        uint32_t (__stdcall *fpa0f2)(uint32_t, float, float);
        uint32_t (__stdcall *fpa0f2a3f5)(uint32_t, float, float, uint32_t, float, float);
        uint32_t (__stdcall *fpa0f2a4f6a8p9)(uint32_t, float, float, uint32_t, uint32_t, float, float, uint32_t, uint32_t, uintptr_t);
        uint32_t (__stdcall *fpa0f2a4p5)(uint32_t, float, float, uint32_t, uint32_t, uintptr_t);
        uint32_t (__stdcall *fpa0f2p6)(uint32_t, uint32_t, float, float, float, float, uintptr_t);
        uint32_t (__stdcall *fpa0f3)(uint32_t, float, float, float);
        uint32_t (__stdcall *fpa0f4)(uint32_t, float, float, float, float);
        uint32_t (__stdcall *fpa1f2)(uint32_t, uint32_t, float);
        uint32_t (__stdcall *fpa1f2a3)(uint32_t, uint32_t, float, uint32_t);
        uint32_t (__stdcall *fpa1f5)(uint32_t, uint32_t, float, float, float, float);
        uint32_t (__stdcall *fpa1p2f6)(uint32_t, uint32_t, uintptr_t, float, float, float, float);
        uint32_t (__stdcall *fpf0)(float);
        uint32_t (__stdcall *fpf1)(float, float);
        uint32_t (__stdcall *fpf2)(float, float, float);
        uint32_t (__stdcall *fpf3)(float, float, float, float);
        uint32_t (__stdcall *fpf5)(float, float, float, float, float, float);
        uint32_t (__stdcall *fpf7)(float, float, float, float, float, float, float, float);
        /* double func proto */
        uint32_t (__stdcall *fpa0d1)(uint32_t, double);
        uint32_t (__stdcall *fpa0d2)(uint32_t, double, double);
        uint32_t (__stdcall *fpa0d2a3d5)(uint32_t, double, double, uint32_t, double, double);
        uint32_t (__stdcall *fpa0d2a4d6a8p9)(uint32_t, double, double, uint32_t, uint32_t, double, double, uint32_t, uint32_t, uintptr_t);
        uint32_t (__stdcall *fpa0d2a4p5)(uint32_t, double, double, uint32_t, uint32_t, uintptr_t);
        uint32_t (__stdcall *fpa0d3)(uint32_t, double, double, double);
        uint32_t (__stdcall *fpa0d4)(uint32_t, double, double, double, double);
        uint32_t (__stdcall *fpa1d2)(uint32_t, uint32_t, double);
        uint32_t (__stdcall *fpa1d5)(uint32_t, uint32_t, double, double, double, double);
        uint32_t (__stdcall *fpa1p2d6)(uint32_t, uint32_t, uintptr_t, double, double, double, double);
        uint32_t (__stdcall *fpd0)(double);
        uint32_t (__stdcall *fpd1)(double, double);
        uint32_t (__stdcall *fpd2)(double, double, double);
        uint32_t (__stdcall *fpd3)(double, double, double, double);
        uint32_t (__stdcall *fpd5)(double, double, double, double, double, double);
    } USFP;
    USFP usfp;

    switch(FEnum) {
        case FEnum_glAreProgramsResidentNV:
        case FEnum_glAreTexturesResident:
        case FEnum_glAreTexturesResidentEXT:
        case FEnum_glFlushMappedBufferRange:
        case FEnum_glFlushMappedBufferRangeAPPLE:
        case FEnum_glFlushMappedNamedBufferRange:
        case FEnum_glPrioritizeTextures:
        case FEnum_glPrioritizeTexturesEXT:
            usfp.fpa0p2 = tblMesaGL[FEnum].ptr;
            *ret = (*usfp.fpa0p2)(arg[0], parg[1], parg[2]);
            GLDONE();
        case FEnum_glBufferSubData:
        case FEnum_glBufferSubDataARB:
        case FEnum_glGetBufferSubData:
        case FEnum_glGetBufferSubDataARB:
        case FEnum_glNamedBufferSubData:
        case FEnum_glNamedBufferSubDataEXT:
            usfp.fpa0p3 = tblMesaGL[FEnum].ptr;
            *ret = (*usfp.fpa0p3)(arg[0], parg[1], parg[2], parg[3]);
            GLDONE();
        case FEnum_glBindFragDataLocationIndexed:
        case FEnum_glColorPointer:
        case FEnum_glDrawElements:
        case FEnum_glGetCombinerOutputParameterfvNV:
        case FEnum_glGetCombinerOutputParameterivNV:
        case FEnum_glGetFramebufferAttachmentParameteriv:
        case FEnum_glGetFramebufferAttachmentParameterivEXT:
        case FEnum_glGetTexLevelParameterfv:
        case FEnum_glGetTexLevelParameteriv:
        case FEnum_glGetTrackMatrixivNV:
        case FEnum_glIndexPointerEXT:
        case FEnum_glLoadProgramNV:
        case FEnum_glNormalPointerEXT:
        case FEnum_glProgramEnvParameters4fvEXT:
        case FEnum_glProgramLocalParameters4fvEXT:
        case FEnum_glProgramParameters4dvNV:
        case FEnum_glProgramParameters4fvNV:
        case FEnum_glProgramStringARB:
        case FEnum_glSecondaryColorPointer:
        case FEnum_glSecondaryColorPointerEXT:
        case FEnum_glTexCoordPointer:
        case FEnum_glUniformMatrix2dv:
        case FEnum_glUniformMatrix2fv:
        case FEnum_glUniformMatrix2fvARB:
        case FEnum_glUniformMatrix2x3dv:
        case FEnum_glUniformMatrix2x3fv:
        case FEnum_glUniformMatrix2x4dv:
        case FEnum_glUniformMatrix2x4fv:
        case FEnum_glUniformMatrix3dv:
        case FEnum_glUniformMatrix3fv:
        case FEnum_glUniformMatrix3fvARB:
        case FEnum_glUniformMatrix3x2dv:
        case FEnum_glUniformMatrix3x2fv:
        case FEnum_glUniformMatrix3x4dv:
        case FEnum_glUniformMatrix3x4fv:
        case FEnum_glUniformMatrix4dv:
        case FEnum_glUniformMatrix4fv:
        case FEnum_glUniformMatrix4fvARB:
        case FEnum_glUniformMatrix4x2dv:
        case FEnum_glUniformMatrix4x2fv:
        case FEnum_glUniformMatrix4x3dv:
        case FEnum_glUniformMatrix4x3fv:
        case FEnum_glVertexPointer:
        case FEnum_glVertexWeightPointerEXT:
        case FEnum_glWeightPointerARB:
            usfp.fpa2p3 = tblMesaGL[FEnum].ptr;
            *ret = (*usfp.fpa2p3)(arg[0], arg[1], arg[2], parg[3]);
            GLDONE();
        case FEnum_glGetActiveUniform:
        case FEnum_glGetActiveUniformARB:
        case FEnum_glGetTransformFeedbackVarying:
        case FEnum_glGetTransformFeedbackVaryingEXT:
            usfp.fpa2p6 = tblMesaGL[FEnum].ptr;
            *ret = (*usfp.fpa2p6)(arg[0], arg[1], arg[2], parg[3], parg[0], parg[1], parg[2]);
            GLDONE();
        case FEnum_glGetActiveUniformName:
            usfp.fpa2p4 = tblMesaGL[FEnum].ptr;
            *ret = (*usfp.fpa2p4)(arg[0], arg[1], arg[2], parg[3], parg[0]);
            GLDONE();
        case FEnum_glDrawElementsBaseVertex:
        case FEnum_glDrawElementsInstanced:
        case FEnum_glDrawElementsInstancedARB:
        case FEnum_glDrawElementsInstancedEXT:
            usfp.fpa2p3a4 = tblMesaGL[FEnum].ptr;
            *ret = (*usfp.fpa2p3a4)(arg[0], arg[1], arg[2], parg[3], arg[4]);
            GLDONE();
        case FEnum_glDrawElementsInstancedBaseInstance:
        case FEnum_glDrawElementsInstancedBaseVertex:
            usfp.fpa2p3a5 = tblMesaGL[FEnum].ptr;
            *ret = (*usfp.fpa2p3a5)(arg[0], arg[1], arg[2], parg[3], arg[4], arg[5]);
            GLDONE();
        case FEnum_glDrawElementsInstancedBaseVertexBaseInstance:
            usfp.fpa2p3a6 = tblMesaGL[FEnum].ptr;
            *ret = (*usfp.fpa2p3a6)(arg[0], arg[1], arg[2], parg[3], arg[4], arg[5], arg[6]);
            GLDONE();
        case FEnum_glClearBufferData:
        case FEnum_glClearNamedBufferData:
        case FEnum_glClearNamedBufferDataEXT:
        case FEnum_glClearTexImage:
        case FEnum_glColorPointerEXT:
        case FEnum_glDrawPixels:
        case FEnum_glGetCombinerInputParameterfvNV:
        case FEnum_glGetCombinerInputParameterivNV:
        case FEnum_glGetInternalformativ:
        case FEnum_glGetTexImage:
        case FEnum_glTexCoordPointerEXT:
        case FEnum_glVertexAttribIPointer:
        case FEnum_glVertexAttribIPointerEXT:
        case FEnum_glVertexAttribLPointer:
        case FEnum_glVertexAttribLPointerEXT:
        case FEnum_glVertexAttribPointerNV:
        case FEnum_glVertexPointerEXT:
            usfp.fpa3p4 = tblMesaGL[FEnum].ptr;
            *ret = (*usfp.fpa3p4)(arg[0], arg[1], arg[2], arg[3], parg[0]);
            GLDONE();
        case FEnum_glColorSubTable:
        case FEnum_glColorSubTableEXT:
        case FEnum_glColorTable:
        case FEnum_glColorTableEXT:
        case FEnum_glDrawRangeElements:
        case FEnum_glDrawRangeElementsEXT:
        case FEnum_glVertexAttribPointer:
        case FEnum_glVertexAttribPointerARB:
            usfp.fpa4p5 = tblMesaGL[FEnum].ptr;
            *ret = (*usfp.fpa4p5)(arg[0], arg[1], arg[2], arg[3], arg[4], parg[1]);
            GLDONE();
        case FEnum_glDrawRangeElementsBaseVertex:
            usfp.fpa4p5a6 = tblMesaGL[FEnum].ptr;
            *ret = (*usfp.fpa4p5a6)(arg[0], arg[1], arg[2], arg[3], arg[4], parg[1], arg[6]);
            GLDONE();
        case FEnum_glGetString:
            usfp.rpfpa0 = tblMesaGL[FEnum].ptr;
            *ret = (*usfp.rpfpa0)(arg[0]);
            GLDONE();
        case FEnum_glFenceSync:
        case FEnum_glGetStringi:
        case FEnum_glMapBuffer:
        case FEnum_glMapBufferARB:
            usfp.rpfpa1 = tblMesaGL[FEnum].ptr;
            *ret = (*usfp.rpfpa1)(arg[0], arg[1]);
            GLDONE();
        case FEnum_glMapBufferRange:
            usfp.rpfpa0p2a3 = tblMesaGL[FEnum].ptr;
            *ret = (*usfp.rpfpa0p2a3)(arg[0], parg[1], parg[2], arg[3]);
            GLDONE();
        case FEnum_glClipPlane:
        case FEnum_glCombinerParameterfvNV:
        case FEnum_glCombinerParameterivNV:
        case FEnum_glDeleteBuffers:
        case FEnum_glDeleteBuffersARB:
        case FEnum_glDeleteFencesAPPLE:
        case FEnum_glDeleteFencesNV:
        case FEnum_glDeleteFramebuffers:
        case FEnum_glDeleteFramebuffersEXT:
        case FEnum_glDeleteOcclusionQueriesNV:
        case FEnum_glDeleteProgramsARB:
        case FEnum_glDeleteProgramsNV:
        case FEnum_glDeleteQueries:
        case FEnum_glDeleteQueriesARB:
        case FEnum_glDeleteRenderbuffers:
        case FEnum_glDeleteRenderbuffersEXT:
        case FEnum_glDeleteSamplers:
        case FEnum_glDeleteTextures:
        case FEnum_glDeleteTexturesEXT:
        case FEnum_glDeleteVertexArrays:
        case FEnum_glDrawArraysIndirect:
        case FEnum_glDrawBuffers:
        case FEnum_glDrawBuffersARB:
        case FEnum_glEdgeFlagPointer:
        case FEnum_glFogfv:
        case FEnum_glFogiv:
        case FEnum_glGenBuffers:
        case FEnum_glGenBuffersARB:
        case FEnum_glGenFencesAPPLE:
        case FEnum_glGenFencesNV:
        case FEnum_glGenFramebuffers:
        case FEnum_glGenFramebuffersEXT:
        case FEnum_glGenOcclusionQueriesNV:
        case FEnum_glGenProgramsARB:
        case FEnum_glGenProgramsNV:
        case FEnum_glGenQueries:
        case FEnum_glGenQueriesARB:
        case FEnum_glGenRenderbuffers:
        case FEnum_glGenRenderbuffersEXT:
        case FEnum_glGenSamplers:
        case FEnum_glGenTextures:
        case FEnum_glGenTexturesEXT:
        case FEnum_glGenVertexArrays:
        case FEnum_glGetAttribLocation:
        case FEnum_glGetAttribLocationARB:
        case FEnum_glGetBooleanv:
        case FEnum_glGetClipPlane:
        case FEnum_glGetDoublev:
        case FEnum_glGetFloatv:
        case FEnum_glGetIntegerv:
        case FEnum_glGetUniformBlockIndex:
        case FEnum_glGetUniformLocation:
        case FEnum_glGetUniformLocationARB:
        case FEnum_glLightModelfv:
        case FEnum_glLightModeliv:
        case FEnum_glMultiTexCoord1dv:
        case FEnum_glMultiTexCoord1dvARB:
        case FEnum_glMultiTexCoord1fv:
        case FEnum_glMultiTexCoord1fvARB:
        case FEnum_glMultiTexCoord1iv:
        case FEnum_glMultiTexCoord1ivARB:
        case FEnum_glMultiTexCoord1sv:
        case FEnum_glMultiTexCoord1svARB:
        case FEnum_glMultiTexCoord2dv:
        case FEnum_glMultiTexCoord2dvARB:
        case FEnum_glMultiTexCoord2fv:
        case FEnum_glMultiTexCoord2fvARB:
        case FEnum_glMultiTexCoord2iv:
        case FEnum_glMultiTexCoord2ivARB:
        case FEnum_glMultiTexCoord2sv:
        case FEnum_glMultiTexCoord2svARB:
        case FEnum_glMultiTexCoord3dv:
        case FEnum_glMultiTexCoord3dvARB:
        case FEnum_glMultiTexCoord3fv:
        case FEnum_glMultiTexCoord3fvARB:
        case FEnum_glMultiTexCoord3iv:
        case FEnum_glMultiTexCoord3ivARB:
        case FEnum_glMultiTexCoord3sv:
        case FEnum_glMultiTexCoord3svARB:
        case FEnum_glMultiTexCoord4dv:
        case FEnum_glMultiTexCoord4dvARB:
        case FEnum_glMultiTexCoord4fv:
        case FEnum_glMultiTexCoord4fvARB:
        case FEnum_glMultiTexCoord4iv:
        case FEnum_glMultiTexCoord4ivARB:
        case FEnum_glMultiTexCoord4sv:
        case FEnum_glMultiTexCoord4svARB:
        case FEnum_glPointParameterfv:
        case FEnum_glPointParameterfvARB:
        case FEnum_glPointParameterfvEXT:
        case FEnum_glPointParameteriv:
        case FEnum_glRequestResidentProgramsNV:
        case FEnum_glScissorIndexedv:
        case FEnum_glSelectBuffer:
        case FEnum_glSetFragmentShaderConstantATI:
        case FEnum_glVertexAttrib1dv:
        case FEnum_glVertexAttrib1dvARB:
        case FEnum_glVertexAttrib1dvNV:
        case FEnum_glVertexAttrib1fv:
        case FEnum_glVertexAttrib1fvARB:
        case FEnum_glVertexAttrib1fvNV:
        case FEnum_glVertexAttrib1sv:
        case FEnum_glVertexAttrib1svARB:
        case FEnum_glVertexAttrib1svNV:
        case FEnum_glVertexAttrib2dv:
        case FEnum_glVertexAttrib2dvARB:
        case FEnum_glVertexAttrib2dvNV:
        case FEnum_glVertexAttrib2fv:
        case FEnum_glVertexAttrib2fvARB:
        case FEnum_glVertexAttrib2fvNV:
        case FEnum_glVertexAttrib2sv:
        case FEnum_glVertexAttrib2svARB:
        case FEnum_glVertexAttrib2svNV:
        case FEnum_glVertexAttrib3dv:
        case FEnum_glVertexAttrib3dvARB:
        case FEnum_glVertexAttrib3dvNV:
        case FEnum_glVertexAttrib3fv:
        case FEnum_glVertexAttrib3fvARB:
        case FEnum_glVertexAttrib3fvNV:
        case FEnum_glVertexAttrib3sv:
        case FEnum_glVertexAttrib3svARB:
        case FEnum_glVertexAttrib3svNV:
        case FEnum_glVertexAttrib4Nbv:
        case FEnum_glVertexAttrib4NbvARB:
        case FEnum_glVertexAttrib4Niv:
        case FEnum_glVertexAttrib4NivARB:
        case FEnum_glVertexAttrib4Nsv:
        case FEnum_glVertexAttrib4NsvARB:
        case FEnum_glVertexAttrib4Nubv:
        case FEnum_glVertexAttrib4NubvARB:
        case FEnum_glVertexAttrib4Nuiv:
        case FEnum_glVertexAttrib4NuivARB:
        case FEnum_glVertexAttrib4Nusv:
        case FEnum_glVertexAttrib4NusvARB:
        case FEnum_glVertexAttrib4bv:
        case FEnum_glVertexAttrib4bvARB:
        case FEnum_glVertexAttrib4dv:
        case FEnum_glVertexAttrib4dvARB:
        case FEnum_glVertexAttrib4dvNV:
        case FEnum_glVertexAttrib4fv:
        case FEnum_glVertexAttrib4fvARB:
        case FEnum_glVertexAttrib4fvNV:
        case FEnum_glVertexAttrib4iv:
        case FEnum_glVertexAttrib4ivARB:
        case FEnum_glVertexAttrib4sv:
        case FEnum_glVertexAttrib4svARB:
        case FEnum_glVertexAttrib4svNV:
        case FEnum_glVertexAttrib4ubv:
        case FEnum_glVertexAttrib4ubvARB:
        case FEnum_glVertexAttrib4ubvNV:
        case FEnum_glVertexAttrib4uiv:
        case FEnum_glVertexAttrib4uivARB:
        case FEnum_glVertexAttrib4usv:
        case FEnum_glVertexAttrib4usvARB:
        case FEnum_glViewportIndexedfv:
        case FEnum_glWeightbvARB:
        case FEnum_glWeightdvARB:
        case FEnum_glWeightfvARB:
        case FEnum_glWeightivARB:
        case FEnum_glWeightsvARB:
        case FEnum_glWeightubvARB:
        case FEnum_glWeightuivARB:
        case FEnum_glWeightusvARB:
            usfp.fpa0p1 = tblMesaGL[FEnum].ptr;
            *ret = (*usfp.fpa0p1)(arg[0], parg[1]);
            GLDONE();
        case FEnum_glColor3bv:
        case FEnum_glColor3dv:
        case FEnum_glColor3fv:
        case FEnum_glColor3iv:
        case FEnum_glColor3sv:
        case FEnum_glColor3ubv:
        case FEnum_glColor3uiv:
        case FEnum_glColor3usv:
        case FEnum_glColor4bv:
        case FEnum_glColor4dv:
        case FEnum_glColor4fv:
        case FEnum_glColor4iv:
        case FEnum_glColor4sv:
        case FEnum_glColor4ubv:
        case FEnum_glColor4uiv:
        case FEnum_glColor4usv:
        case FEnum_glEdgeFlagv:
        case FEnum_glEvalCoord1dv:
        case FEnum_glEvalCoord1fv:
        case FEnum_glEvalCoord2dv:
        case FEnum_glEvalCoord2fv:
        case FEnum_glFogCoorddv:
        case FEnum_glFogCoorddvEXT:
        case FEnum_glFogCoordfv:
        case FEnum_glFogCoordfvEXT:
        case FEnum_glIndexdv:
        case FEnum_glIndexfv:
        case FEnum_glIndexiv:
        case FEnum_glIndexsv:
        case FEnum_glIndexubv:
        case FEnum_glLoadMatrixd:
        case FEnum_glLoadMatrixf:
        case FEnum_glMultMatrixd:
        case FEnum_glMultMatrixf:
        case FEnum_glNormal3bv:
        case FEnum_glNormal3dv:
        case FEnum_glNormal3fv:
        case FEnum_glNormal3iv:
        case FEnum_glNormal3sv:
        case FEnum_glPolygonStipple:
        case FEnum_glRasterPos2dv:
        case FEnum_glRasterPos2fv:
        case FEnum_glRasterPos2iv:
        case FEnum_glRasterPos2sv:
        case FEnum_glRasterPos3dv:
        case FEnum_glRasterPos3fv:
        case FEnum_glRasterPos3iv:
        case FEnum_glRasterPos3sv:
        case FEnum_glRasterPos4dv:
        case FEnum_glRasterPos4fv:
        case FEnum_glRasterPos4iv:
        case FEnum_glRasterPos4sv:
        case FEnum_glSecondaryColor3bv:
        case FEnum_glSecondaryColor3bvEXT:
        case FEnum_glSecondaryColor3dv:
        case FEnum_glSecondaryColor3dvEXT:
        case FEnum_glSecondaryColor3fv:
        case FEnum_glSecondaryColor3fvEXT:
        case FEnum_glSecondaryColor3iv:
        case FEnum_glSecondaryColor3ivEXT:
        case FEnum_glSecondaryColor3sv:
        case FEnum_glSecondaryColor3svEXT:
        case FEnum_glSecondaryColor3ubv:
        case FEnum_glSecondaryColor3ubvEXT:
        case FEnum_glSecondaryColor3uiv:
        case FEnum_glSecondaryColor3uivEXT:
        case FEnum_glSecondaryColor3usv:
        case FEnum_glSecondaryColor3usvEXT:
        case FEnum_glTexCoord2dv:
        case FEnum_glTexCoord2fv:
        case FEnum_glTexCoord2iv:
        case FEnum_glTexCoord2sv:
        case FEnum_glTexCoord3dv:
        case FEnum_glTexCoord3fv:
        case FEnum_glTexCoord3iv:
        case FEnum_glTexCoord3sv:
        case FEnum_glTexCoord4dv:
        case FEnum_glTexCoord4fv:
        case FEnum_glTexCoord4iv:
        case FEnum_glTexCoord4sv:
        case FEnum_glVertex2dv:
        case FEnum_glVertex2fv:
        case FEnum_glVertex2iv:
        case FEnum_glVertex2sv:
        case FEnum_glVertex3dv:
        case FEnum_glVertex3fv:
        case FEnum_glVertex3iv:
        case FEnum_glVertex3sv:
        case FEnum_glVertex4dv:
        case FEnum_glVertex4fv:
        case FEnum_glVertex4iv:
        case FEnum_glVertex4sv:
        case FEnum_glVertexWeightfvEXT:
            usfp.fpp0 = tblMesaGL[FEnum].ptr;
            *ret = (*usfp.fpp0)(parg[0]);
            GLDONE();
        case FEnum_glRectdv:
        case FEnum_glRectfv:
        case FEnum_glRectiv:
        case FEnum_glRectsv:
            usfp.fpp1 = tblMesaGL[FEnum].ptr;
            *ret = (*usfp.fpp1)(parg[0], parg[1]);
            GLDONE();
        case FEnum_glClearBufferSubData:
        case FEnum_glClearNamedBufferSubData:
        case FEnum_glClearNamedBufferSubDataEXT:
        case FEnum_glCompressedTexImage1D:
        case FEnum_glCompressedTexImage1DARB:
        case FEnum_glCompressedTexSubImage1D:
        case FEnum_glCompressedTexSubImage1DARB:
        case FEnum_glReadPixels:
        case FEnum_glTexSubImage1D:
        case FEnum_glTexSubImage1DEXT:
            usfp.fpa5p6 = tblMesaGL[FEnum].ptr;
            *ret = (*usfp.fpa5p6)(arg[0], arg[1], arg[2], arg[3], arg[4], arg[5], parg[2]);
            GLDONE();
        case FEnum_glBindAttribLocation:
        case FEnum_glBindAttribLocationARB:
        case FEnum_glBindFragDataLocation:
        case FEnum_glBindFragDataLocationEXT:
        case FEnum_glBindImageTextures:
        case FEnum_glBindSamplers:
        case FEnum_glCallLists:
        case FEnum_glClearBufferfv:
        case FEnum_glClearBufferiv:
        case FEnum_glClearBufferuiv:
        case FEnum_glCombinerStageParameterfvNV:
        case FEnum_glDepthRangeArrayv:
        case FEnum_glDrawElementsIndirect:
        case FEnum_glEdgeFlagPointerEXT:
        case FEnum_glExecuteProgramNV:
        case FEnum_glFeedbackBuffer:
        case FEnum_glFogCoordPointer:
        case FEnum_glFogCoordPointerEXT:
        case FEnum_glGetBufferParameteriv:
        case FEnum_glGetBufferParameterivARB:
        case FEnum_glGetCombinerStageParameterfvNV:
        case FEnum_glGetCompressedTexImage:
        case FEnum_glGetCompressedTexImageARB:
        case FEnum_glGetFenceivNV:
        case FEnum_glGetFinalCombinerInputParameterfvNV:
        case FEnum_glGetFinalCombinerInputParameterivNV:
        case FEnum_glGetLightfv:
        case FEnum_glGetLightiv:
        case FEnum_glGetMapdv:
        case FEnum_glGetMapfv:
        case FEnum_glGetMapiv:
        case FEnum_glGetMaterialfv:
        case FEnum_glGetMaterialiv:
        case FEnum_glGetObjectParameterfvARB:
        case FEnum_glGetObjectParameterivARB:
        case FEnum_glGetOcclusionQueryivNV:
        case FEnum_glGetOcclusionQueryuivNV:
        case FEnum_glGetProgramiv:
        case FEnum_glGetProgramivARB:
        case FEnum_glGetProgramivNV:
        case FEnum_glGetQueryObjecti64v:
        case FEnum_glGetQueryObjecti64vEXT:
        case FEnum_glGetQueryObjectiv:
        case FEnum_glGetQueryObjectivARB:
        case FEnum_glGetQueryObjectui64v:
        case FEnum_glGetQueryObjectui64vEXT:
        case FEnum_glGetQueryObjectuiv:
        case FEnum_glGetQueryObjectuivARB:
        case FEnum_glGetQueryiv:
        case FEnum_glGetQueryivARB:
        case FEnum_glGetRenderbufferParameteriv:
        case FEnum_glGetRenderbufferParameterivEXT:
        case FEnum_glGetShaderiv:
        case FEnum_glGetTexEnvfv:
        case FEnum_glGetTexEnviv:
        case FEnum_glGetTexGendv:
        case FEnum_glGetTexGenfv:
        case FEnum_glGetTexGeniv:
        case FEnum_glGetTexParameterfv:
        case FEnum_glGetTexParameteriv:
        case FEnum_glIndexPointer:
        case FEnum_glInterleavedArrays:
        case FEnum_glLightfv:
        case FEnum_glLightiv:
        case FEnum_glMaterialfv:
        case FEnum_glMaterialiv:
        case FEnum_glNormalPointer:
        case FEnum_glPixelMapfv:
        case FEnum_glPixelMapuiv:
        case FEnum_glPixelMapusv:
        case FEnum_glProgramEnvParameter4dvARB:
        case FEnum_glProgramEnvParameter4fvARB:
        case FEnum_glProgramLocalParameter4dvARB:
        case FEnum_glProgramLocalParameter4fvARB:
        case FEnum_glProgramParameter4dvNV:
        case FEnum_glProgramParameter4fvNV:
        case FEnum_glSamplerParameterIiv:
        case FEnum_glSamplerParameterIuiv:
        case FEnum_glSamplerParameterfv:
        case FEnum_glSamplerParameteriv:
        case FEnum_glScissorArrayv:
        case FEnum_glTexEnvfv:
        case FEnum_glTexEnviv:
        case FEnum_glTexGendv:
        case FEnum_glTexGenfv:
        case FEnum_glTexGeniv:
        case FEnum_glTexParameterfv:
        case FEnum_glTexParameteriv:
        case FEnum_glUniform1dv:
        case FEnum_glUniform1fv:
        case FEnum_glUniform1fvARB:
        case FEnum_glUniform1iv:
        case FEnum_glUniform1ivARB:
        case FEnum_glUniform1uiv:
        case FEnum_glUniform1uivEXT:
        case FEnum_glUniform2dv:
        case FEnum_glUniform2fv:
        case FEnum_glUniform2fvARB:
        case FEnum_glUniform2iv:
        case FEnum_glUniform2ivARB:
        case FEnum_glUniform2uiv:
        case FEnum_glUniform2uivEXT:
        case FEnum_glUniform3dv:
        case FEnum_glUniform3fv:
        case FEnum_glUniform3fvARB:
        case FEnum_glUniform3iv:
        case FEnum_glUniform3ivARB:
        case FEnum_glUniform3uiv:
        case FEnum_glUniform3uivEXT:
        case FEnum_glUniform4dv:
        case FEnum_glUniform4fv:
        case FEnum_glUniform4fvARB:
        case FEnum_glUniform4iv:
        case FEnum_glUniform4ivARB:
        case FEnum_glUniform4uiv:
        case FEnum_glUniform4uivEXT:
        case FEnum_glVertexAttribs1dvNV:
        case FEnum_glVertexAttribs1fvNV:
        case FEnum_glVertexAttribs1svNV:
        case FEnum_glVertexAttribs2dvNV:
        case FEnum_glVertexAttribs2fvNV:
        case FEnum_glVertexAttribs2svNV:
        case FEnum_glVertexAttribs3dvNV:
        case FEnum_glVertexAttribs3fvNV:
        case FEnum_glVertexAttribs3svNV:
        case FEnum_glVertexAttribs4dvNV:
        case FEnum_glVertexAttribs4fvNV:
        case FEnum_glVertexAttribs4svNV:
        case FEnum_glVertexAttribs4ubvNV:
        case FEnum_glViewportArrayv:
            usfp.fpa1p2 = tblMesaGL[FEnum].ptr;
            *ret = (*usfp.fpa1p2)(arg[0], arg[1], parg[2]);
            GLDONE();
        case FEnum_glTransformFeedbackVaryings:
        case FEnum_glTransformFeedbackVaryingsEXT:
            usfp.fpa1p2a3 = tblMesaGL[FEnum].ptr;
            *ret = (*usfp.fpa1p2a3)(arg[0], arg[1], parg[2], arg[3]);
            GLDONE();
        case FEnum_glGetAttachedShaders:
        case FEnum_glGetInfoLogARB:
        case FEnum_glGetProgramInfoLog:
        case FEnum_glGetShaderInfoLog:
        case FEnum_glProgramNamedParameter4dvNV:
        case FEnum_glProgramNamedParameter4fvNV:
        case FEnum_glShaderSource:
        case FEnum_glShaderSourceARB:
            usfp.fpa1p3 = tblMesaGL[FEnum].ptr;
            *ret = (*usfp.fpa1p3)(arg[0], arg[1], parg[2], parg[3]);
            GLDONE();
        case FEnum_glBufferData:
        case FEnum_glBufferDataARB:
        case FEnum_glBufferStorage:
        case FEnum_glNamedBufferData:
        case FEnum_glNamedBufferDataEXT:
        case FEnum_glNamedBufferStorage:
        case FEnum_glNamedBufferStorageEXT:
            usfp.fpa0p2a3 = tblMesaGL[FEnum].ptr;
            *ret = (*usfp.fpa0p2a3)(arg[0], parg[1], parg[2], arg[3]);
            GLDONE();
        case FEnum_glCompressedTexImage2D:
        case FEnum_glCompressedTexImage2DARB:
        case FEnum_glTexImage1D:
            usfp.fpa6p7 = tblMesaGL[FEnum].ptr;
            *ret = (*usfp.fpa6p7)(arg[0], arg[1], arg[2], arg[3], arg[4], arg[5], arg[6], parg[3]);
            GLDONE();
        case FEnum_glCompressedTexImage3D:
        case FEnum_glCompressedTexImage3DARB:
        case FEnum_glCompressedTexSubImage2D:
        case FEnum_glCompressedTexSubImage2DARB:
        case FEnum_glTexImage2D:
        case FEnum_glTexSubImage2D:
        case FEnum_glTexSubImage2DEXT:
            usfp.fpa7p8 = tblMesaGL[FEnum].ptr;
            *ret = (*usfp.fpa7p8)(arg[0], arg[1], arg[2], arg[3], arg[4], arg[5], arg[6], arg[7], parg[0]);
            GLDONE();
        case FEnum_glTexImage3D:
        case FEnum_glTexImage3DEXT:
            usfp.fpa8p9 = tblMesaGL[FEnum].ptr;
            *ret = (*usfp.fpa8p9)(arg[0], arg[1], arg[2], arg[3], arg[4], arg[5], arg[6], arg[7], arg[8], parg[1]);
            GLDONE();
        case FEnum_glClearTexSubImage:
        case FEnum_glCompressedTexSubImage3D:
        case FEnum_glCompressedTexSubImage3DARB:
        case FEnum_glTexSubImage3D:
        case FEnum_glTexSubImage3DEXT:
            usfp.fpa9p10 = tblMesaGL[FEnum].ptr;
            *ret = (*usfp.fpa9p10)(arg[0], arg[1], arg[2], arg[3], arg[4], arg[5], arg[6], arg[7], arg[8], arg[9], parg[2]);
            GLDONE();
        case FEnum_glDebugMessageCallback:
        case FEnum_glDebugMessageCallbackARB:
        case FEnum_glDebugMessageControl:
        case FEnum_glDebugMessageControlARB:
        case FEnum_glDebugMessageInsert:
        case FEnum_glDebugMessageInsertARB:
            GLDONE();

        /* GLFuncs with int64 args */
#define GLARGSX_N(a,i) \
            memcpy(&a, &arg[i], sizeof(uint64_t))
        case FEnum_glDeleteSync:
            {
                usfp.fpx2 = tblMesaGL[FEnum].ptr;
                *ret = (*usfp.fpx2)(DeleteSyncObj(LookupSyncObj(arg[0])));
            }
            GLDONE();
        case FEnum_glClientWaitSync:
        case FEnum_glWaitSync:
            {
                uint64_t x2;
                GLARGSX_N(x2, 2);
                usfp.fpa1x2 = tblMesaGL[FEnum].ptr;
                *ret = (*usfp.fpa1x2)(LookupSyncObj(arg[0]), arg[1], x2);
            }
            GLDONE();

        /* GLFuncs with float args */
#define GLARGSF_N(a,i) \
            memcpy(&a, &arg[i], sizeof(float))
#define GLARGS2F(a,b) \
            memcpy(&a, &arg[0], sizeof(float)); \
            memcpy(&b, &arg[1], sizeof(float))
#define GLARGS3F(a,b,c) \
            memcpy(&a, &arg[0], sizeof(float)); \
            memcpy(&b, &arg[1], sizeof(float)); \
            memcpy(&c, &arg[2], sizeof(float))
#define GLARGS4F(a,b,c,d) \
            memcpy(&a, &arg[0], sizeof(float)); \
            memcpy(&b, &arg[1], sizeof(float)); \
            memcpy(&c, &arg[2], sizeof(float)); \
            memcpy(&d, &arg[3], sizeof(float))
#define GLARGS6F(a,b,c,d,e,f) \
            memcpy(&a, &arg[0], sizeof(float)); \
            memcpy(&b, &arg[1], sizeof(float)); \
            memcpy(&c, &arg[2], sizeof(float)); \
            memcpy(&d, &arg[3], sizeof(float)); \
            memcpy(&e, &arg[4], sizeof(float)); \
            memcpy(&f, &arg[5], sizeof(float))
#define GLARGS8F(a,b,c,d,e,f,g,h) \
            memcpy(&a, &arg[0], sizeof(float)); \
            memcpy(&b, &arg[1], sizeof(float)); \
            memcpy(&c, &arg[2], sizeof(float)); \
            memcpy(&d, &arg[3], sizeof(float)); \
            memcpy(&e, &arg[4], sizeof(float)); \
            memcpy(&f, &arg[5], sizeof(float)); \
            memcpy(&g, &arg[6], sizeof(float)); \
            memcpy(&h, &arg[7], sizeof(float))
        case FEnum_glClearIndex:
        case FEnum_glLineWidth:
        case FEnum_glMinSampleShading:
        case FEnum_glMinSampleShadingARB:
        case FEnum_glPassThrough:
        case FEnum_glPointSize:
        case FEnum_glClearDepthf:
        case FEnum_glEvalCoord1f:
        case FEnum_glFogCoordf:
        case FEnum_glFogCoordfEXT:
        case FEnum_glIndexf:
        case FEnum_glTexCoord1f:
        case FEnum_glVertexWeightfEXT:
            {
                float a0;
                GLARGSF_N(a0,0);
                usfp.fpf0 = tblMesaGL[FEnum].ptr;
                *ret = (*usfp.fpf0)(a0);
            }
            GLDONE();
        case FEnum_glDepthRangef:
        case FEnum_glPathStencilDepthOffsetNV:
        case FEnum_glPixelZoom:
        case FEnum_glPolygonOffset:
        case FEnum_glPolygonOffsetEXT:
        case FEnum_glEvalCoord2f:
        case FEnum_glRasterPos2f:
        case FEnum_glTexCoord2f:
        case FEnum_glVertex2f:
            {
                float a0, a1;
                GLARGS2F(a0,a1);
                usfp.fpf1 = tblMesaGL[FEnum].ptr;
                *ret = (*usfp.fpf1)(a0,a1);
            }
            GLDONE();
        case FEnum_glColor3f:
        case FEnum_glNormal3f:
        case FEnum_glPolygonOffsetClamp:
        case FEnum_glPolygonOffsetClampEXT:
        case FEnum_glRasterPos3f:
        case FEnum_glScalef:
        case FEnum_glSecondaryColor3f:
        case FEnum_glSecondaryColor3fEXT:
        case FEnum_glTexCoord3f:
        case FEnum_glTranslatef:
        case FEnum_glVertex3f:
            {
                float a0, a1, a2;
                GLARGS3F(a0,a1,a2);
                usfp.fpf3 = tblMesaGL[FEnum].ptr;
                *ret = (*usfp.fpf2)(a0,a1,a2);
            }
            GLDONE();
        case FEnum_glBlendColor:
        case FEnum_glBlendColorEXT:
        case FEnum_glClearColor:
        case FEnum_glClearAccum:
        case FEnum_glRectf:
        case FEnum_glRotatef:
        case FEnum_glColor4f:
        case FEnum_glRasterPos4f:
        case FEnum_glTexCoord4f:
        case FEnum_glVertex4f:
            {
                float a0, a1, a2, a3;
                GLARGS4F(a0,a1,a2,a3);
                usfp.fpf3 = tblMesaGL[FEnum].ptr;
                *ret = (*usfp.fpf3)(a0,a1,a2,a3);
            }
            GLDONE();
        case FEnum_glFrustumfOES:
        case FEnum_glOrthofOES:
            {
                float a0, a1, a2, a3, a4, a5;
                GLARGS6F(a0,a1,a2,a3,a4,a5);
                usfp.fpf5 = tblMesaGL[FEnum].ptr;
                *ret = (*usfp.fpf5)(a0,a1,a2,a3,a4,a5);
            }
            GLDONE();
        case FEnum_glPrimitiveBoundingBoxARB:
            {
                float a0, a1, a2, a3, a4, a5, a6, a7;
                GLARGS8F(a0,a1,a2,a3,a4,a5,a6,a7);
                usfp.fpf7 = tblMesaGL[FEnum].ptr;
                *ret = (*usfp.fpf7)(a0,a1,a2,a3,a4,a5,a6,a7);
            }
            GLDONE();
        case FEnum_glAccum:
        case FEnum_glAlphaFunc:
        case FEnum_glCombinerParameterfNV:
        case FEnum_glFogf:
        case FEnum_glLightModelf:
        case FEnum_glMultiTexCoord1f:
        case FEnum_glMultiTexCoord1fARB:
        case FEnum_glPixelStoref:
        case FEnum_glPixelTransferf:
        case FEnum_glPointParameterf:
        case FEnum_glPointParameterfARB:
        case FEnum_glPointParameterfEXT:
        case FEnum_glUniform1f:
        case FEnum_glUniform1fARB:
        case FEnum_glVertexAttrib1f:
        case FEnum_glVertexAttrib1fARB:
        case FEnum_glVertexAttrib1fNV:
            {
                float a1;
                GLARGSF_N(a1,1);
                usfp.fpa0f1 = tblMesaGL[FEnum].ptr;
                *ret = (*usfp.fpa0f1)(arg[0], a1);
            }
            GLDONE();
        case FEnum_glMapGrid1f:
        case FEnum_glMultiTexCoord2f:
        case FEnum_glMultiTexCoord2fARB:
        case FEnum_glUniform2f:
        case FEnum_glUniform2fARB:
        case FEnum_glVertexAttrib2f:
        case FEnum_glVertexAttrib2fARB:
        case FEnum_glVertexAttrib2fNV:
            {
                float a1, a2;
                GLARGSF_N(a1,1);
                GLARGSF_N(a2,2);
                usfp.fpa0f2 = tblMesaGL[FEnum].ptr;
                *ret = (*usfp.fpa0f2)(arg[0], a1, a2);
            }
            GLDONE();
        case FEnum_glMultiTexCoord3f:
        case FEnum_glMultiTexCoord3fARB:
        case FEnum_glUniform3f:
        case FEnum_glUniform3fARB:
        case FEnum_glVertexAttrib3f:
        case FEnum_glVertexAttrib3fARB:
        case FEnum_glVertexAttrib3fNV:
            {
                float a1, a2, a3;
                GLARGSF_N(a1,1);
                GLARGSF_N(a2,2);
                GLARGSF_N(a3,3);
                usfp.fpa0f3 = tblMesaGL[FEnum].ptr;
                *ret = (*usfp.fpa0f3)(arg[0], a1, a2, a3);
            }
            GLDONE();
        case FEnum_glMultiTexCoord4f:
        case FEnum_glMultiTexCoord4fARB:
        case FEnum_glUniform4f:
        case FEnum_glUniform4fARB:
        case FEnum_glVertexAttrib4f:
        case FEnum_glVertexAttrib4fARB:
        case FEnum_glVertexAttrib4fNV:
        case FEnum_glViewportIndexedf:
            {
                float a1, a2, a3, a4;
                GLARGSF_N(a1,1);
                GLARGSF_N(a2,2);
                GLARGSF_N(a3,3);
                GLARGSF_N(a4,4);
                usfp.fpa0f4 = tblMesaGL[FEnum].ptr;
                *ret = (*usfp.fpa0f4)(arg[0], a1, a2, a3, a4);
            }
            GLDONE();
        case FEnum_glMapGrid2f:
            {
                float a1, a2, a4, a5;
                GLARGSF_N(a1, 1);
                GLARGSF_N(a2, 2);
                GLARGSF_N(a4, 4);
                GLARGSF_N(a5, 5);
                usfp.fpa0f2a3f5 = tblMesaGL[FEnum].ptr;
                *ret = (*usfp.fpa0f2a3f5)(arg[0], a1, a2, arg[3], a4, a5);
            }
            GLDONE();
        case FEnum_glMap1f:
            {
                float a1, a2;
                GLARGSF_N(a1, 1);
                GLARGSF_N(a2, 2);
                usfp.fpa0f2a4p5 = tblMesaGL[FEnum].ptr;
                *ret = (*usfp.fpa0f2a4p5)(arg[0], a1, a2, arg[3], arg[4], parg[1]);
            }
            GLDONE();
        case FEnum_glMap2f:
            {
                float a1, a2, a5, a6;
                GLARGSF_N(a1, 1);
                GLARGSF_N(a2, 2);
                GLARGSF_N(a5, 5);
                GLARGSF_N(a6, 6);
                usfp.fpa0f2a4f6a8p9 = tblMesaGL[FEnum].ptr;
                *ret = (*usfp.fpa0f2a4f6a8p9)(arg[0], a1, a2, arg[3], arg[4], a5, a6, arg[7], arg[8], parg[1]);
            }
            GLDONE();
        case FEnum_glLightf:
        case FEnum_glMaterialf:
        case FEnum_glSamplerParameterf:
        case FEnum_glTexEnvf:
        case FEnum_glTexGenf:
        case FEnum_glTexParameterf:
            {
                float a2;
                GLARGSF_N(a2,2);
                usfp.fpa1f2 = tblMesaGL[FEnum].ptr;
                *ret = (*usfp.fpa1f2)(arg[0], arg[1], a2);
            }
            GLDONE();
        case FEnum_glClearBufferfi:
            {
                float a2;
                GLARGSF_N(a2,2);
                usfp.fpa1f2a3 = tblMesaGL[FEnum].ptr;
                *ret = (*usfp.fpa1f2a3)(arg[0], arg[1], a2, arg[3]);
            }
            GLDONE();
        case FEnum_glProgramEnvParameter4fARB:
        case FEnum_glProgramLocalParameter4fARB:
        case FEnum_glProgramParameter4fNV:
            {
                float a2, a3, a4, a5;
                GLARGSF_N(a2,2);
                GLARGSF_N(a3,3);
                GLARGSF_N(a4,4);
                GLARGSF_N(a5,5);
                usfp.fpa1f5 = tblMesaGL[FEnum].ptr;
                *ret = (*usfp.fpa1f5)(arg[0], arg[1], a2, a3, a4, a5);
            }
            GLDONE();
        case FEnum_glProgramNamedParameter4fNV:
            {
                float a3, a4, a5, a6;
                GLARGSF_N(a3,3);
                GLARGSF_N(a4,4);
                GLARGSF_N(a5,5);
                GLARGSF_N(a6,6);
                usfp.fpa1p2f6 = tblMesaGL[FEnum].ptr;
                *ret = (*usfp.fpa1p2f6)(arg[0], arg[1], parg[2], a3, a4, a5, a6);
            }
            GLDONE();
        case FEnum_glBitmap:
            {
                float a2, a3, a4, a5;
                GLARGSF_N(a2,2);
                GLARGSF_N(a3,3);
                GLARGSF_N(a4,4);
                GLARGSF_N(a5,5);
                usfp.fpa0f2p6 = tblMesaGL[FEnum].ptr;
                *ret = (*usfp.fpa0f2p6)(arg[0], arg[1], a2, a3, a4, a5, parg[2]);
            }
            GLDONE();

        /* GLFuncs with double args */
#define GLARGSD_N(a,i) \
            memcpy((char *)&a, &arg[i], sizeof(uint32_t)); \
            memcpy(((char *)&a)+4, &arg[i+1], sizeof(uint32_t))
        case FEnum_glClearDepth:
        case FEnum_glEvalCoord1d:
        case FEnum_glIndexd:
        case FEnum_glTexCoord1d:
        case FEnum_glFogCoordd:
        case FEnum_glFogCoorddEXT:
        case FEnum_glClearDepthdNV:
        case FEnum_glGlobalAlphaFactordSUN:
            {
                double a0;
                GLARGSD_N(a0,0);
                usfp.fpd0 = tblMesaGL[FEnum].ptr;
                *ret = (*usfp.fpd0)(a0);
            }
            GLDONE();
        case FEnum_glDepthRange:
        case FEnum_glDepthRangedNV:
        case FEnum_glDepthBoundsEXT:
        case FEnum_glDepthBoundsdNV:
        case FEnum_glEvalCoord2d:
        case FEnum_glRasterPos2d:
        case FEnum_glTexCoord2d:
        case FEnum_glVertex2d:
        case FEnum_glWindowPos2d:
        case FEnum_glWindowPos2dARB:
        case FEnum_glWindowPos2dMESA:
            {
                double a0, a1;
                GLARGSD_N(a0,0);
                GLARGSD_N(a1,2);
                usfp.fpd1 = tblMesaGL[FEnum].ptr;
                *ret = (*usfp.fpd1)(a0,a1);
            }
            GLDONE();
        case FEnum_glScaled:
        case FEnum_glTranslated:
        case FEnum_glColor3d:
        case FEnum_glNormal3d:
        case FEnum_glRasterPos3d:
        case FEnum_glTexCoord3d:
        case FEnum_glVertex3d:
        case FEnum_glBinormal3dEXT:
        case FEnum_glSecondaryColor3d:
        case FEnum_glSecondaryColor3dEXT:
        case FEnum_glTangent3dEXT:
        case FEnum_glWindowPos3d:
        case FEnum_glWindowPos3dARB:
        case FEnum_glWindowPos3dMESA:
            {
                double a0, a1, a2;
                GLARGSD_N(a0,0);
                GLARGSD_N(a1,2);
                GLARGSD_N(a2,4);
                usfp.fpd2 = tblMesaGL[FEnum].ptr;
                *ret = (*usfp.fpd2)(a0,a1,a2);
            }
            GLDONE();
        case FEnum_glRectd:
        case FEnum_glRotated:
        case FEnum_glColor4d:
        case FEnum_glRasterPos4d:
        case FEnum_glTexCoord4d:
        case FEnum_glVertex4d:
        case FEnum_glWindowPos4dMESA:
            {
                double a0, a1, a2, a3;
                GLARGSD_N(a0,0);
                GLARGSD_N(a1,2);
                GLARGSD_N(a2,4);
                GLARGSD_N(a3,6);
                usfp.fpd3 = tblMesaGL[FEnum].ptr;
                *ret = (*usfp.fpd3)(a0,a1,a2,a3);
            }
            GLDONE();
        case FEnum_glFrustum:
        case FEnum_glOrtho:
            {
                double a0, a1, a2, a3, a4, a5;
                GLARGSD_N(a0,0);
                GLARGSD_N(a1,2);
                GLARGSD_N(a2,4);
                GLARGSD_N(a3,6);
                GLARGSD_N(a4,8);
                GLARGSD_N(a5,10);
                usfp.fpd5 = tblMesaGL[FEnum].ptr;
                *ret = (*usfp.fpd5)(a0,a1,a2,a3,a4,a5);
            }
            GLDONE();
        case FEnum_glMultiTexCoord1d:
        case FEnum_glMultiTexCoord1dARB:
        case FEnum_glUniform1d:
        case FEnum_glVertexAttrib1d:
        case FEnum_glVertexAttrib1dARB:
        case FEnum_glVertexAttrib1dNV:
            {
                double a1;
                GLARGSD_N(a1,1);
                usfp.fpa0d1 = tblMesaGL[FEnum].ptr;
                *ret = (*usfp.fpa0d1)(arg[0], a1);
            }
            GLDONE();
        case FEnum_glMapGrid1d:
        case FEnum_glMultiTexCoord2d:
        case FEnum_glMultiTexCoord2dARB:
        case FEnum_glUniform2d:
        case FEnum_glVertexAttrib2d:
        case FEnum_glVertexAttrib2dARB:
        case FEnum_glVertexAttrib2dNV:
            {
                double a1, a2;
                GLARGSD_N(a1,1);
                GLARGSD_N(a2,3);
                usfp.fpa0d2 = tblMesaGL[FEnum].ptr;
                *ret = (*usfp.fpa0d2)(arg[0], a1, a2);
            }
            GLDONE();
        case FEnum_glMultiTexCoord3d:
        case FEnum_glMultiTexCoord3dARB:
        case FEnum_glUniform3d:
        case FEnum_glVertexAttrib3d:
        case FEnum_glVertexAttrib3dARB:
        case FEnum_glVertexAttrib3dNV:
            {
                double a1, a2, a3;
                GLARGSD_N(a1,1);
                GLARGSD_N(a2,3);
                GLARGSD_N(a3,5);
                usfp.fpa0d3 = tblMesaGL[FEnum].ptr;
                *ret = (*usfp.fpa0d3)(arg[0], a1, a2, a3);
            }
            GLDONE();
        case FEnum_glMultiTexCoord4d:
        case FEnum_glMultiTexCoord4dARB:
        case FEnum_glUniform4d:
        case FEnum_glVertexAttrib4d:
        case FEnum_glVertexAttrib4dARB:
        case FEnum_glVertexAttrib4dNV:
            {
                double a1, a2, a3, a4;
                GLARGSD_N(a1,1);
                GLARGSD_N(a2,3);
                GLARGSD_N(a3,5);
                GLARGSD_N(a4,7);
                usfp.fpa0d4 = tblMesaGL[FEnum].ptr;
                *ret = (*usfp.fpa0d4)(arg[0], a1, a2, a3, a4);
            }
            GLDONE();
        case FEnum_glTexGend:
            {
                double a2;
                GLARGSD_N(a2,2);
                usfp.fpa1d2 = tblMesaGL[FEnum].ptr;
                *ret = (*usfp.fpa1d2)(arg[0], arg[1], a2);
            }
            GLDONE();
        case FEnum_glProgramEnvParameter4dARB:
        case FEnum_glProgramLocalParameter4dARB:
        case FEnum_glProgramParameter4dNV:
            {
                double a2, a3, a4, a5;
                GLARGSD_N(a2,2);
                GLARGSD_N(a3,4);
                GLARGSD_N(a4,6);
                GLARGSD_N(a5,8);
                usfp.fpa1d5 = tblMesaGL[FEnum].ptr;
                *ret = (*usfp.fpa1d5)(arg[0], arg[1], a2, a3, a4, a5);
            }
            GLDONE();
        case FEnum_glProgramNamedParameter4dNV:
            {
                double a3, a4, a5, a6;
                GLARGSD_N(a3,3);
                GLARGSD_N(a4,5);
                GLARGSD_N(a5,7);
                GLARGSD_N(a6,9);
                usfp.fpa1p2d6 = tblMesaGL[FEnum].ptr;
                *ret = (*usfp.fpa1p2d6)(arg[0], arg[1], parg[2], a3, a4, a5, a6);
            }
            GLDONE();
        case FEnum_glMapGrid2d:
            {
                double a1, a2, a4, a5;
                GLARGSD_N(a1, 1);
                GLARGSD_N(a2, 3);
                GLARGSD_N(a4, 6);
                GLARGSD_N(a5, 8);
                usfp.fpa0d2a3d5 = tblMesaGL[FEnum].ptr;
                *ret = (*usfp.fpa0d2a3d5)(arg[0], a1, a2, arg[5], a4, a5);
            }
            GLDONE();
        case FEnum_glMap1d:
            {
                double a1, a2;
                GLARGSD_N(a1, 1);
                GLARGSD_N(a2, 3);
                usfp.fpa0d2a4p5 = tblMesaGL[FEnum].ptr;
                *ret = (*usfp.fpa0d2a4p5)(arg[0], a1, a2, arg[5], arg[6], parg[3]);
            }
            GLDONE();
        case FEnum_glMap2d:
            {
                double a1, a2, a5, a6;
                GLARGSD_N(a1, 1);
                GLARGSD_N(a2, 3);
                GLARGSD_N(a5, 7);
                GLARGSD_N(a6, 9);
                usfp.fpa0d2a4d6a8p9 = tblMesaGL[FEnum].ptr;
                *ret = (*usfp.fpa0d2a4d6a8p9)(arg[0], a1, a2, arg[5], arg[6], a5, a6, arg[11], arg[12], parg[1]);
            }
        default:
            break;
    }

    /* Start - generated by hostgenfuncs */

    typedef union {
    uint32_t __stdcall (*fpra0)(void);
    uint32_t __stdcall (*fpra1)(uint32_t arg0);
    uint32_t __stdcall (*fpra2)(uint32_t arg0, uint32_t arg1);
    uint32_t __stdcall (*fpra3)(uint32_t arg0, uint32_t arg1, uint32_t arg2);
    uint32_t __stdcall (*fpra4)(uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3);
    uint32_t __stdcall (*fpra5)(uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
    uint32_t __stdcall (*fpra6)(uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5);
    uint32_t __stdcall (*fpra7)(uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5, uint32_t arg6);
    uint32_t __stdcall (*fpra8)(uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5, uint32_t arg6, uint32_t arg7);
    uint32_t __stdcall (*fpra9)(uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5, uint32_t arg6, uint32_t arg7, uint32_t arg8);
    uint32_t __stdcall (*fpra10)(uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5, uint32_t arg6, uint32_t arg7, uint32_t arg8, uint32_t arg9);
    uint32_t __stdcall (*fpra11)(uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5, uint32_t arg6, uint32_t arg7, uint32_t arg8, uint32_t arg9, uint32_t arg10);
    uint32_t __stdcall (*fpra12)(uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5, uint32_t arg6, uint32_t arg7, uint32_t arg8, uint32_t arg9, uint32_t arg10, uint32_t arg11);
    uint32_t __stdcall (*fpra13)(uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5, uint32_t arg6, uint32_t arg7, uint32_t arg8, uint32_t arg9, uint32_t arg10, uint32_t arg11, uint32_t arg12);
    uint32_t __stdcall (*fpra14)(uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5, uint32_t arg6, uint32_t arg7, uint32_t arg8, uint32_t arg9, uint32_t arg10, uint32_t arg11, uint32_t arg12, uint32_t arg13);
    uint32_t __stdcall (*fpra15)(uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5, uint32_t arg6, uint32_t arg7, uint32_t arg8, uint32_t arg9, uint32_t arg10, uint32_t arg11, uint32_t arg12, uint32_t arg13, uint32_t arg14);
    uint32_t __stdcall (*fpra16)(uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5, uint32_t arg6, uint32_t arg7, uint32_t arg8, uint32_t arg9, uint32_t arg10, uint32_t arg11, uint32_t arg12, uint32_t arg13, uint32_t arg14, uint32_t arg15);
    uint32_t __stdcall (*fpra17)(uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5, uint32_t arg6, uint32_t arg7, uint32_t arg8, uint32_t arg9, uint32_t arg10, uint32_t arg11, uint32_t arg12, uint32_t arg13, uint32_t arg14, uint32_t arg15, uint32_t arg16);
    uint32_t __stdcall (*fpra18)(uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5, uint32_t arg6, uint32_t arg7, uint32_t arg8, uint32_t arg9, uint32_t arg10, uint32_t arg11, uint32_t arg12, uint32_t arg13, uint32_t arg14, uint32_t arg15, uint32_t arg16, uint32_t arg17);
    uint32_t __stdcall (*fpra19)(uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5, uint32_t arg6, uint32_t arg7, uint32_t arg8, uint32_t arg9, uint32_t arg10, uint32_t arg11, uint32_t arg12, uint32_t arg13, uint32_t arg14, uint32_t arg15, uint32_t arg16, uint32_t arg17, uint32_t arg18);
    uint32_t __stdcall (*fpra20)(uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5, uint32_t arg6, uint32_t arg7, uint32_t arg8, uint32_t arg9, uint32_t arg10, uint32_t arg11, uint32_t arg12, uint32_t arg13, uint32_t arg14, uint32_t arg15, uint32_t arg16, uint32_t arg17, uint32_t arg18, uint32_t arg19);
    } UARG_FP;
    UARG_FP ufp;

    switch (numArgs) {
    case 0:
        ufp.fpra0 = tblMesaGL[FEnum].ptr;
        *ret = (*(ufp.fpra0))();
        break;
    case 1:
        ufp.fpra1 = tblMesaGL[FEnum].ptr;
        *ret = (*(ufp.fpra1))(arg[0]);
        break;
    case 2:
        ufp.fpra2 = tblMesaGL[FEnum].ptr;
        *ret = (*(ufp.fpra2))(arg[0], arg[1]);
        break;
    case 3:
        ufp.fpra3 = tblMesaGL[FEnum].ptr;
        *ret = (*(ufp.fpra3))(arg[0], arg[1], arg[2]);
        break;
    case 4:
        ufp.fpra4 = tblMesaGL[FEnum].ptr;
        *ret = (*(ufp.fpra4))(arg[0], arg[1], arg[2], arg[3]);
        break;
    case 5:
        ufp.fpra5 = tblMesaGL[FEnum].ptr;
        *ret = (*(ufp.fpra5))(arg[0], arg[1], arg[2], arg[3], arg[4]);
        break;
    case 6:
        ufp.fpra6 = tblMesaGL[FEnum].ptr;
        *ret = (*(ufp.fpra6))(arg[0], arg[1], arg[2], arg[3], arg[4], arg[5]);
        break;
    case 7:
        ufp.fpra7 = tblMesaGL[FEnum].ptr;
        *ret = (*(ufp.fpra7))(arg[0], arg[1], arg[2], arg[3], arg[4], arg[5], arg[6]);
        break;
    case 8:
        ufp.fpra8 = tblMesaGL[FEnum].ptr;
        *ret = (*(ufp.fpra8))(arg[0], arg[1], arg[2], arg[3], arg[4], arg[5], arg[6], arg[7]);
        break;
    case 9:
        ufp.fpra9 = tblMesaGL[FEnum].ptr;
        *ret = (*(ufp.fpra9))(arg[0], arg[1], arg[2], arg[3], arg[4], arg[5], arg[6], arg[7], arg[8]);
        break;
    case 10:
        ufp.fpra10 = tblMesaGL[FEnum].ptr;
        *ret = (*(ufp.fpra10))(arg[0], arg[1], arg[2], arg[3], arg[4], arg[5], arg[6], arg[7], arg[8], arg[9]);
        break;
    case 11:
        ufp.fpra11 = tblMesaGL[FEnum].ptr;
        *ret = (*(ufp.fpra11))(arg[0], arg[1], arg[2], arg[3], arg[4], arg[5], arg[6], arg[7], arg[8], arg[9], arg[10]);
        break;
    case 12:
        ufp.fpra12 = tblMesaGL[FEnum].ptr;
        *ret = (*(ufp.fpra12))(arg[0], arg[1], arg[2], arg[3], arg[4], arg[5], arg[6], arg[7], arg[8], arg[9], arg[10], arg[11]);
        break;
    case 13:
        ufp.fpra13 = tblMesaGL[FEnum].ptr;
        *ret = (*(ufp.fpra13))(arg[0], arg[1], arg[2], arg[3], arg[4], arg[5], arg[6], arg[7], arg[8], arg[9], arg[10], arg[11], arg[12]);
        break;
    case 14:
        ufp.fpra14 = tblMesaGL[FEnum].ptr;
        *ret = (*(ufp.fpra14))(arg[0], arg[1], arg[2], arg[3], arg[4], arg[5], arg[6], arg[7], arg[8], arg[9], arg[10], arg[11], arg[12], arg[13]);
        break;
    case 15:
        ufp.fpra15 = tblMesaGL[FEnum].ptr;
        *ret = (*(ufp.fpra15))(arg[0], arg[1], arg[2], arg[3], arg[4], arg[5], arg[6], arg[7], arg[8], arg[9], arg[10], arg[11], arg[12], arg[13], arg[14]);
        break;
    case 16:
        ufp.fpra16 = tblMesaGL[FEnum].ptr;
        *ret = (*(ufp.fpra16))(arg[0], arg[1], arg[2], arg[3], arg[4], arg[5], arg[6], arg[7], arg[8], arg[9], arg[10], arg[11], arg[12], arg[13], arg[14], arg[15]);
        break;
    case 17:
        ufp.fpra17 = tblMesaGL[FEnum].ptr;
        *ret = (*(ufp.fpra17))(arg[0], arg[1], arg[2], arg[3], arg[4], arg[5], arg[6], arg[7], arg[8], arg[9], arg[10], arg[11], arg[12], arg[13], arg[14], arg[15], arg[16]);
        break;
    case 18:
        ufp.fpra18 = tblMesaGL[FEnum].ptr;
        *ret = (*(ufp.fpra18))(arg[0], arg[1], arg[2], arg[3], arg[4], arg[5], arg[6], arg[7], arg[8], arg[9], arg[10], arg[11], arg[12], arg[13], arg[14], arg[15], arg[16], arg[17]);
        break;
    case 19:
        ufp.fpra19 = tblMesaGL[FEnum].ptr;
        *ret = (*(ufp.fpra19))(arg[0], arg[1], arg[2], arg[3], arg[4], arg[5], arg[6], arg[7], arg[8], arg[9], arg[10], arg[11], arg[12], arg[13], arg[14], arg[15], arg[16], arg[17], arg[18]);
        break;
    case 20:
        ufp.fpra20 = tblMesaGL[FEnum].ptr;
        *ret = (*(ufp.fpra20))(arg[0], arg[1], arg[2], arg[3], arg[4], arg[5], arg[6], arg[7], arg[8], arg[9], arg[10], arg[11], arg[12], arg[13], arg[14], arg[15], arg[16], arg[17], arg[18], arg[19]);
        break;
    default:
        break;
    }

    /* End - generated by hostgenfuncs */

    if (GLCheckError()) {
        MESA_PFN(PFNGLGETERRORPROC, glGetError);
        static int begin_prim;
        switch(FEnum) {
        case FEnum_glBegin:
            begin_prim = 1;
            break;
        case FEnum_glDebugMessageCallback:
        case FEnum_glDebugMessageCallbackARB:
        case FEnum_glDebugMessageControl:
        case FEnum_glDebugMessageControlARB:
        case FEnum_glDebugMessageInsert:
        case FEnum_glDebugMessageInsertARB:
        case FEnum_glGetError:
            break;
        case FEnum_glMapBufferRange:
            if (*ret) {
                PFN_CALL(glGetError());
                break;
            }
            /* fall through */
        case FEnum_glEnd:
            begin_prim = 0;
            /* fall through */
        default:
            if (!begin_prim) {
                int nargs = GLFEnumArgsCnt(FEnum),
                    e = PFN_CALL(glGetError());
                if (e) {
                    fprintf(stderr, "mgl_error: %s %s\n%s", tblMesaGL[FEnum].sym, tokglstr(e), (nargs)? "    args: ":"");
                    for (int i = 0; i < nargs; i++)
                        fprintf(stderr, "%08x ", arg[i]);
                    if (nargs)
                        fprintf(stderr, "\n");
                }
            }
            break;
        }
    }
}

static int cfg_xYear;
static int cfg_xLength;
static int cfg_xWine;
static int cfg_vertCacheMB;
static int cfg_dispTimerMS;
static int cfg_bufoAccelEN;
static int cfg_cntxMSAA;
static int cfg_cntxSRGB;
static int cfg_blitFlip;
static int cfg_cntxVsyncOff;
static int cfg_renderScalerOff;
static int cfg_fpsLimit;
static int cfg_shaderDump;
static int cfg_errorCheck;
static int cfg_traceFifo;
static int cfg_traceFunc;
static void conf_MGLOptions(void)
{
    cfg_xYear = 0;
    cfg_xLength = 0;
    cfg_vertCacheMB = 32;
    cfg_cntxSRGB = 0;
    cfg_cntxVsyncOff = 0;
    cfg_fpsLimit = 0;
    cfg_shaderDump = 0;
    cfg_errorCheck = 0;
    cfg_traceFifo = 0;
    cfg_traceFunc = 0;
    FILE *fp = fopen(MESAGLCFG, "r");
    if (fp != NULL) {
        char line[32];
        int i, v;
        while (fgets(line, 32, fp)) {
            i = sscanf(line, "ExtensionsYear,%d", &v);
            cfg_xYear = (i == 1)? v:cfg_xYear;
            i = sscanf(line, "ExtensionsLength,%d", &v);
            cfg_xLength = (i == 1)? v:cfg_xLength;
            i = sscanf(line, "VertexCacheMB,%d", &v);
            cfg_vertCacheMB = (i == 1)? v:cfg_vertCacheMB;
            i = sscanf(line, "DispTimerMS,%d", &v);
            cfg_dispTimerMS = (i == 1)? v:cfg_dispTimerMS;
            i = sscanf(line, "BufOAccelEN,%d", &v);
            cfg_bufoAccelEN = ((i == 1) && v)? 1:cfg_bufoAccelEN;
            i = sscanf(line, "ContextMSAA,%d", &v);
            cfg_cntxMSAA = (i == 1)? ((v & 0x03U) << 2):cfg_cntxMSAA;
            i = sscanf(line, "ContextSRGB,%d", &v);
            cfg_cntxSRGB = ((i == 1) && v)? 1:cfg_cntxSRGB;
            i = sscanf(line, "ContextVsyncOff,%d", &v);
            cfg_cntxVsyncOff = ((i == 1) && v)? 1:cfg_cntxVsyncOff;
            i = sscanf(line, "RenderScalerOff,%d", &v);
            cfg_renderScalerOff = ((i == 1) && v)? 1:cfg_renderScalerOff;
            i = sscanf(line, "FpsLimit,%d", &v);
            cfg_fpsLimit = (i == 1)? (v & 0x7FU):cfg_fpsLimit;
            i = sscanf(line, "DumpShader,%d", &v);
            cfg_shaderDump = ((i == 1) && v)? 1:cfg_shaderDump;
            i = sscanf(line, "CheckError,%d", &v);
            cfg_errorCheck = ((i == 1) && v)? 1:cfg_errorCheck;
            i = sscanf(line, "FifoTrace,%d", &v);
            cfg_traceFifo = ((i == 1) && v)? 1:cfg_traceFifo;
            i = sscanf(line, "FuncTrace,%d", &v);
            cfg_traceFunc = (i == 1)? (v % 3):cfg_traceFunc;
        }
        fclose(fp);
    }
}

int ContextUseSRGB(void)
{
    MESA_PFN(PFNGLISENABLEDPROC, glIsEnabled);
    return (cfg_cntxSRGB | PFN_CALL(glIsEnabled(GL_FRAMEBUFFER_SRGB)))? 1:0;
}
int SwapFpsLimit(int fps)
{
    int ret;
    if (fps && (fps != cfg_fpsLimit)) {
        cfg_fpsLimit = fps;
        ret = 1;
    }
    else
        ret = 0;
    return ret;
}
void GLBufOAccelCfg(int enable) { cfg_bufoAccelEN = enable; }
void GLRenderScaler(int disable) { cfg_renderScalerOff = disable; }
void GLContextMSAA(int msaa) { cfg_cntxMSAA = msaa; }
void GLBlitFlip(int flip) { cfg_blitFlip = flip; }
void GLDispTimerCfg(int msec) { cfg_dispTimerMS = msec; }
void GLExtUncapped(int xwine) { cfg_xWine = xwine; if (cfg_xWine) { cfg_xYear = 0; cfg_xLength = 0; }}
int GetGLExtYear(void) { return cfg_xYear; }
int GetGLExtLength(void) { return cfg_xLength; }
int GetVertCacheMB(void) { return cfg_vertCacheMB; }
int GetDispTimerMS(void) { return cfg_dispTimerMS; }
int GetBufOAccelEN(void) { return cfg_bufoAccelEN; }
int GetContextMSAA(void) { return (cfg_cntxMSAA > 8)? 16:cfg_cntxMSAA; }
int ContextVsyncOff(void) { return cfg_cntxVsyncOff; }
int RenderScalerOff(void) { return cfg_renderScalerOff; }
int ScalerBlitFlip(void) { return cfg_blitFlip; }
int ScalerSRGBCorr(void) { return cfg_xWine; }
int GetFpsLimit(void) { return cfg_fpsLimit; }
int GLShaderDump(void) { return cfg_shaderDump; }
int GLCheckError(void) { return cfg_errorCheck; }
int GLFifoTrace(void) { return cfg_traceFifo; }
int GLFuncTrace(void) { return (cfg_traceFifo)? 0:cfg_traceFunc; }

#ifdef CONFIG_WIN32
static HINSTANCE hDll = 0;
#endif
#if defined(CONFIG_LINUX) || defined(CONFIG_DARWIN)
static void *hDll = 0;
#endif

void FiniMesaGL(void)
{
    if (hDll) {
#ifdef CONFIG_WIN32
        FreeLibrary(hDll);
#endif
#if defined(CONFIG_LINUX) || defined(CONFIG_DARWIN)
        dlclose(hDll);
#endif
    }
    hDll = 0;
    for (int i = 0 ; i < FEnum_zzMGLFuncEnum_max; i++)
        tblMesaGL[i].ptr = 0;
}

void ImplMesaGLReset(void)
{
    for (int i = 0 ; i < FEnum_zzMGLFuncEnum_max; i++)
        tblMesaGL[i].impl = 0;
    conf_MGLOptions();
}

int InitMesaGL(void)
{
#ifdef CONFIG_WIN32
    const char dllname[] = "opengl32.dll";
    hDll = LoadLibrary(dllname);
#elif defined(CONFIG_LINUX)
    const char dllname[] = "libGL.so.1";
    hDll = dlopen(dllname, RTLD_NOW);
#elif defined(CONFIG_DARWIN)
/*
    -- XQuartz/GLX/OpenGL --
#define DEFAULT_OPENGL  "/opt/X11/lib/libGL.dylib"
    -- SDL2/NSOpenGL --
#define DEFAULT_OPENGL  "/System/Library/Frameworks/OpenGL.framework/Libraries/libGL.dylib"
*/
    extern const char dllname[];
    hDll = dlopen(dllname, RTLD_NOW);
#else
#error Unknown dynamic load library
#endif
    if (!hDll) {
        return 1;
    }

    for (int i = 0; i < FEnum_zzMGLFuncEnum_max; i++) {
        char func[64];
        strncpy(func, tblMesaGL[i].sym + 1, sizeof(func)-1);
        for (int j = 0; j < sizeof(func); j++) {
            if (func[j] == '@') {
                func[j] = 0;
                break;
            }
        }
#if defined(CONFIG_WIN32)
        tblMesaGL[i].ptr = (void *)GetProcAddress(hDll, func);
#endif
#if defined(CONFIG_LINUX) || defined(CONFIG_DARWIN)
        tblMesaGL[i].ptr = (void *)dlsym(hDll, func);
#endif
    }
    SetMesaFuncPtr(hDll);
    return 0;
}

void InitMesaGLExt(void)
{
    for (int i = 0; i < FEnum_zzMGLFuncEnum_max; i++) {
        char func[64];
        if (tblMesaGL[i].ptr == NULL) {
            strncpy(func, tblMesaGL[i].sym + 1, sizeof(func)-1);
            for (int j = 0; j < sizeof(func); j++) {
                if (func[j] == '@') {
                    func[j] = 0;
                    break;
                }
            }
            tblMesaGL[i].ptr = (void *)MesaGLGetProc(func);
        }
    }
}
