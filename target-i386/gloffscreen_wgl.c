/*
 *  Offscreen OpenGL abstraction layer - WGL (windows) specific
 *
 *  Copyright (c) 2010 Intel
 *  Written by: 
 *    Gordon Williams <gordon.williams@collabora.co.uk>
 *    Ian Molton <ian.molton@collabora.co.uk>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#ifdef _WIN32

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <windows.h>
#include <wingdi.h>

#include <GL/glew.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/wglext.h>
#include <GL/glut.h>
#include "gloffscreen.h"

/* In Windows, you must create a window *before* you can create a pbuffer or
 * get a context. So we create a hidden Window on startup (see glo_init/GloMain).
 *
 * Also, you can't share contexts that have different pixel formats, so we can't just
 * create a new context from the window. We must create a whole new PBuffer just for
 * a context :(
 */

struct GloMain {
  HINSTANCE             hInstance;
  HDC                   hDC;
  HWND                  hWnd; /* Our hidden window */
  HGLRC                 hContext;
};
struct GloMain glo;
int glo_inited = 0;

struct _GloContext {
  int                   formatFlags;

  /* Pixel format returned by wglChoosePixelFormat */
  int                   wglPixelFormat;
  /* We need a pbuffer to make a context of the right pixelformat :( */
  HPBUFFERARB           hPBuffer; 
  HDC                   hDC;
  HGLRC                 hContext;
};

struct _GloSurface {
  GLuint                width;
  GLuint                height;  

  GloContext           *context;
  HPBUFFERARB           hPBuffer;
  HDC                   hDC;
};

#define GLO_WINDOW_CLASS "QEmuGLClass"
#define DEFAULT_DEPTH_BUFFER (16)

PFNWGLCHOOSEPIXELFORMATARBPROC wglChoosePixelFormatARB;
PFNWGLGETPBUFFERDCARBPROC wglGetPbufferDCARB;
PFNWGLRELEASEPBUFFERDCARBPROC wglReleasePbufferDCARB;
PFNWGLCREATEPBUFFERARBPROC wglCreatePbufferARB;
PFNWGLDESTROYPBUFFERARBPROC wglDestroyPbufferARB;

/* ------------------------------------------------------------------------ */

//extern const char *glo_glXQueryExtensionsString(void);

extern void glo_surface_getcontents_readpixels(int formatFlags, int stride,
                                    int bpp, int width, int height, void *data);

								
/* ------------------------------------------------------------------------ */


int glo_initialised(void) {
  return glo_inited;
}

/* Initialise gloffscreen */
void glo_init(void) {
    WNDCLASSEX wcx;
    PIXELFORMATDESCRIPTOR pfd;

    if (glo_inited) {
        printf( "gloffscreen already inited\n" );
        exit( EXIT_FAILURE );
    }

    glo.hInstance = GetModuleHandle(NULL); // Grab An Instance For Our Window

    wcx.cbSize = sizeof(wcx);
    wcx.style = 0;
    wcx.lpfnWndProc = DefWindowProc;
    wcx.cbClsExtra = 0;
    wcx.cbWndExtra = 0;
    wcx.hInstance = glo.hInstance;
    wcx.hIcon = NULL;
    wcx.hCursor = NULL;
    wcx.hbrBackground = NULL;
    wcx.lpszMenuName =  NULL;
    wcx.lpszClassName = GLO_WINDOW_CLASS;
    wcx.hIconSm = NULL;
    RegisterClassEx(&wcx);
    glo.hWnd = CreateWindow(
        GLO_WINDOW_CLASS,
        "QEmuGL",
        0,0,0,0,0,
        (HWND)NULL, (HMENU)NULL,
        glo.hInstance,
        (LPVOID) NULL);

    if (!glo.hWnd) {
      printf( "Unable to create window\n" );
      exit( EXIT_FAILURE );
    }
    glo.hDC = GetDC(glo.hWnd);

    memset(&pfd, 0, sizeof(PIXELFORMATDESCRIPTOR));
    pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;
    unsigned int pixelFormat = ChoosePixelFormat(glo.hDC, &pfd);
    DescribePixelFormat(glo.hDC, pixelFormat, sizeof(PIXELFORMATDESCRIPTOR), &pfd);
    if (!SetPixelFormat(glo.hDC, pixelFormat, &pfd))
        return;

    glo.hContext = wglCreateContext(glo.hDC);
    if (glo.hContext == NULL) {
      printf( "Unable to create GL context\n" );
      exit( EXIT_FAILURE );
    }
    wglMakeCurrent(glo.hDC, glo.hContext);

    // FIXME GW
    // Need to share lists AND copy state

    // load in the extensions we need
    //const char	*ext = wglGetExtensionsStringARB(hdc);
    //"WGL_ARB_pixel_format" "WGL_ARB_pbuffer"

    wglChoosePixelFormatARB = (PFNWGLCHOOSEPIXELFORMATARBPROC)wglGetProcAddress("wglChoosePixelFormatARB");
    wglGetPbufferDCARB = (PFNWGLGETPBUFFERDCARBPROC)wglGetProcAddress("wglGetPbufferDCARB");
    wglReleasePbufferDCARB = (PFNWGLRELEASEPBUFFERDCARBPROC)wglGetProcAddress("wglReleasePbufferDCARB");
    wglCreatePbufferARB = (PFNWGLCREATEPBUFFERARBPROC)wglGetProcAddress("wglCreatePbufferARB");
    wglDestroyPbufferARB = (PFNWGLDESTROYPBUFFERARBPROC)wglGetProcAddress("wglDestroyPbufferARB");
	
    if (!wglChoosePixelFormatARB ||
        !wglGetPbufferDCARB ||
        !wglReleasePbufferDCARB ||
        !wglCreatePbufferARB ||
        !wglDestroyPbufferARB) {
      printf( "Unable to load the required WGL extensions\n" );
      exit( EXIT_FAILURE );
    }

	// Initialize glew
	if (GLEW_OK != glewInit())
	{
		// GLEW failed!
		printf("Glew init failed.");
		exit(1);
	}
	
    glo_inited = 1;
}

/* Uninitialise gloffscreen */
void glo_kill(void) {
    if (glo.hContext) {
      wglMakeCurrent(NULL, NULL);
      wglDeleteContext(glo.hContext);
      glo.hContext = NULL;
    }
    if (glo.hDC) {
      ReleaseDC(glo.hWnd, glo.hDC);
      glo.hDC = NULL;
    }
    if (glo.hWnd) {
      DestroyWindow(glo.hWnd);
      glo.hWnd = NULL;
    }
    UnregisterClass(GLO_WINDOW_CLASS, glo.hInstance);
}


static const char *STANDARD_GL_FUNCTIONS ={
/* Miscellaneous */
"glClearIndex\0"                       
"glClearColor\0"                       
"glClear\0"                            
"glIndexMask\0"                        
"glColorMask\0"                        
"glAlphaFunc\0"                        
"glBlendFunc\0"                        
"glLogicOp\0"                          
"glCullFace\0"                         
"glFrontFace\0"                        
"glPointSize\0"                        
"glLineWidth\0"                        
"glLineStipple\0"                      
"glPolygonMode\0"                      
"glPolygonOffset\0"                    
"glPolygonStipple\0"                   
"glGetPolygonStipple\0"                
"glEdgeFlag\0"                         
"glEdgeFlagv\0"                        
"glScissor\0"                          
"glClipPlane\0"                        
"glGetClipPlane\0"                     
"glDrawBuffer\0"                       
"glReadBuffer\0"                       
"glEnable\0"                           
"glDisable\0"                          
"glIsEnabled\0"                        
"glEnableClientState\0"                
"glDisableClientState\0"               
"glGetBooleanv\0"                      
"glGetDoublev\0"                       
"glGetFloatv\0"                        
"glGetIntegerv\0"                      
"glPushAttrib\0"                       
"glPopAttrib\0"                        
"glPushClientAttrib\0"                 
"glPopClientAttrib\0"                  
"glRenderMode\0"                       
"glGetError\0"                         
"glGetString\0"                        
"glFinish\0"                           
"glFlush\0"                            
"glHint\0"                             
/* Depth Buffer */
"glClearDepth\0"                       
"glDepthFunc\0"                        
"glDepthMask\0"                        
"glDepthRange\0"                       
/* Accumulation Buffer */
"glClearAccum\0"                       
"glAccum\0"                            
/* Transformation */
"glMatrixMode\0"                       
"glOrtho\0"                            
"glFrustum\0"                          
"glViewport\0"                         
"glPushMatrix\0"                       
"glPopMatrix\0"                        
"glLoadIdentity\0"                     
"glLoadMatrixd\0"                      
"glLoadMatrixf\0"                      
"glMultMatrixd\0"                      
"glMultMatrixf\0"                      
"glRotated\0"                          
"glRotatef\0"                          
"glScaled\0"                           
"glScalef\0"                           
"glTranslated\0"                       
"glTranslatef\0"                       
/* Display Lists */
"glIsList\0"                           
"glDeleteLists\0"                      
"glGenLists\0"                         
"glNewList\0"                          
"glEndList\0"                          
"glCallList\0"                         
"glCallLists\0"                        
"glListBase\0"                         
/* Drawing Functions */
"glBegin\0"                            
"glEnd\0"                              
"glVertex2d\0"                         
"glVertex2f\0"                         
"glVertex2i\0"                         
"glVertex2s\0"                         
"glVertex3d\0"                         
"glVertex3f\0"                         
"glVertex3i\0"                         
"glVertex3s\0"                         
"glVertex4d\0"                         
"glVertex4f\0"                         
"glVertex4i\0"                         
"glVertex4s\0"                         
"glVertex2dv\0"                        
"glVertex2fv\0"                        
"glVertex2iv\0"                        
"glVertex2sv\0"                        
"glVertex3dv\0"                        
"glVertex3fv\0"                        
"glVertex3iv\0"                        
"glVertex3sv\0"                        
"glVertex4dv\0"                        
"glVertex4fv\0"                        
"glVertex4iv\0"                        
"glVertex4sv\0"                        
"glNormal3b\0"                         
"glNormal3d\0"                         
"glNormal3f\0"                         
"glNormal3i\0"                         
"glNormal3s\0"                         
"glNormal3bv\0"                        
"glNormal3dv\0"                        
"glNormal3fv\0"                        
"glNormal3iv\0"                        
"glNormal3sv\0"                        
"glIndexd\0"                           
"glIndexf\0"                           
"glIndexi\0"                           
"glIndexs\0"                           
"glIndexub\0"                          
"glIndexdv\0"                          
"glIndexfv\0"                          
"glIndexiv\0"                          
"glIndexsv\0"                          
"glIndexubv\0"                         
"glColor3b\0"                          
"glColor3d\0"                          
"glColor3f\0"                          
"glColor3i\0"                          
"glColor3s\0"                          
"glColor3ub\0"                         
"glColor3ui\0"                         
"glColor3us\0"                         
"glColor4b\0"                          
"glColor4d\0"                          
"glColor4f\0"                          
"glColor4i\0"                          
"glColor4s\0"                          
"glColor4ub\0"                         
"glColor4ui\0"                         
"glColor4us\0"                         
"glColor3bv\0"                         
"glColor3dv\0"                         
"glColor3fv\0"                         
"glColor3iv\0"                         
"glColor3sv\0"                         
"glColor3ubv\0"                        
"glColor3uiv\0"                        
"glColor3usv\0"                        
"glColor4bv\0"                         
"glColor4dv\0"                         
"glColor4fv\0"                         
"glColor4iv\0"                         
"glColor4sv\0"                         
"glColor4ubv\0"                        
"glColor4uiv\0"                        
"glColor4usv\0"                        
"glTexCoord1d\0"                       
"glTexCoord1f\0"                       
"glTexCoord1i\0"                       
"glTexCoord1s\0"                       
"glTexCoord2d\0"                       
"glTexCoord2f\0"                       
"glTexCoord2i\0"                       
"glTexCoord2s\0"                       
"glTexCoord3d\0"                       
"glTexCoord3f\0"                       
"glTexCoord3i\0"                       
"glTexCoord3s\0"                       
"glTexCoord4d\0"                       
"glTexCoord4f\0"                       
"glTexCoord4i\0"                       
"glTexCoord4s\0"                       
"glTexCoord1dv\0"                      
"glTexCoord1fv\0"                      
"glTexCoord1iv\0"                      
"glTexCoord1sv\0"                      
"glTexCoord2dv\0"                      
"glTexCoord2fv\0"                      
"glTexCoord2iv\0"                      
"glTexCoord2sv\0"                      
"glTexCoord3dv\0"                      
"glTexCoord3fv\0"                      
"glTexCoord3iv\0"                      
"glTexCoord3sv\0"                      
"glTexCoord4dv\0"                      
"glTexCoord4fv\0"                      
"glTexCoord4iv\0"                      
"glTexCoord4sv\0"                      
"glRasterPos2d\0"                      
"glRasterPos2f\0"                      
"glRasterPos2i\0"                      
"glRasterPos2s\0"                      
"glRasterPos3d\0"                      
"glRasterPos3f\0"                      
"glRasterPos3i\0"                      
"glRasterPos3s\0"                      
"glRasterPos4d\0"                      
"glRasterPos4f\0"                      
"glRasterPos4i\0"                      
"glRasterPos4s\0"                      
"glRasterPos2dv\0"                     
"glRasterPos2fv\0"                     
"glRasterPos2iv\0"                     
"glRasterPos2sv\0"                     
"glRasterPos3dv\0"                     
"glRasterPos3fv\0"                     
"glRasterPos3iv\0"                     
"glRasterPos3sv\0"                     
"glRasterPos4dv\0"                     
"glRasterPos4fv\0"                     
"glRasterPos4iv\0"                     
"glRasterPos4sv\0"                     
"glRectd\0"                            
"glRectf\0"                            
"glRecti\0"                            
"glRects\0"                            
"glRectdv\0"                           
"glRectfv\0"                           
"glRectiv\0"                           
"glRectsv\0"                           
/* Lighting */
"glShadeModel\0"                       
"glLightf\0"                           
"glLighti\0"                           
"glLightfv\0"                          
"glLightiv\0"                          
"glGetLightfv\0"                       
"glGetLightiv\0"                       
"glLightModelf\0"                      
"glLightModeli\0"                      
"glLightModelfv\0"                     
"glLightModeliv\0"                     
"glMaterialf\0"                        
"glMateriali\0"                        
"glMaterialfv\0"                       
"glMaterialiv\0"                       
"glGetMaterialfv\0"                    
"glGetMaterialiv\0"                    
"glColorMaterial\0"                    
/* Raster functions */
"glPixelZoom\0"                        
"glPixelStoref\0"                      
"glPixelStorei\0"                      
"glPixelTransferf\0"                   
"glPixelTransferi\0"                   
"glPixelMapfv\0"                       
"glPixelMapuiv\0"                      
"glPixelMapusv\0"                      
"glGetPixelMapfv\0"                    
"glGetPixelMapuiv\0"                   
"glGetPixelMapusv\0"                   
"glBitmap\0"                           
"glReadPixels\0"                       
"glDrawPixels\0"                       
"glCopyPixels\0"                       
/* Stenciling */
"glStencilFunc\0"                      
"glStencilMask\0"                      
"glStencilOp\0"                        
"glClearStencil\0"                     
/* Texture mapping */
"glTexGend\0"                          
"glTexGenf\0"                          
"glTexGeni\0"                          
"glTexGendv\0"                         
"glTexGenfv\0"                         
"glTexGeniv\0"                         
"glGetTexGendv\0"                      
"glGetTexGenfv\0"                      
"glGetTexGeniv\0"                      
"glTexEnvf\0"                          
"glTexEnvi\0"                          
"glTexEnvfv\0"                         
"glTexEnviv\0"                         
"glGetTexEnvfv\0"                      
"glGetTexEnviv\0"                      
"glTexParameterf\0"                    
"glTexParameteri\0"                    
"glTexParameterfv\0"                   
"glTexParameteriv\0"                   
"glGetTexParameterfv\0"                
"glGetTexParameteriv\0"                
"glGetTexLevelParameterfv\0"           
"glGetTexLevelParameteriv\0"           
"glTexImage1D\0"                       
"glTexImage2D\0"                       
"glGetTexImage\0"                      
/* Evaluators */
"glMap1d\0"                            
"glMap1f\0"                            
"glMap2d\0"                            
"glMap2f\0"                            
"glGetMapdv\0"                         
"glGetMapfv\0"                         
"glGetMapiv\0"                         
"glEvalCoord1d\0"                      
"glEvalCoord1f\0"                      
"glEvalCoord1dv\0"                     
"glEvalCoord1fv\0"                     
"glEvalCoord2d\0"                      
"glEvalCoord2f\0"                      
"glEvalCoord2dv\0"                     
"glEvalCoord2fv\0"                     
"glMapGrid1d\0"                        
"glMapGrid1f\0"                        
"glMapGrid2d\0"                        
"glMapGrid2f\0"                        
"glEvalPoint1\0"                       
"glEvalPoint2\0"                       
"glEvalMesh1\0"                        
"glEvalMesh2\0"                        
/* Fog */
"glFogf\0"                             
"glFogi\0"                             
"glFogfv\0"                            
"glFogiv\0"                            
/* Selection and Feedback */
"glFeedbackBuffer\0"                   
"glPassThrough\0"                      
"glSelectBuffer\0"                     
"glInitNames\0"                        
"glLoadName\0"                         
"glPushName\0"                         
"glPopName\0"                          
/* 1.1 functions */
/* texture objects */
"glGenTextures\0"                      
"glDeleteTextures\0"                   
"glBindTexture\0"                      
"glPrioritizeTextures\0"               
"glAreTexturesResident\0"              
"glIsTexture\0"                        
/* texture mapping */
"glTexSubImage1D\0"                    
"glTexSubImage2D\0"                    
"glCopyTexImage1D\0"                   
"glCopyTexImage2D\0"                   
"glCopyTexSubImage1D\0"                
"glCopyTexSubImage2D\0"                
/* vertex arrays */
"glVertexPointer\0"                    
"glNormalPointer\0"                    
"glColorPointer\0"                     
"glIndexPointer\0"                     
"glTexCoordPointer\0"                  
"glEdgeFlagPointer\0"                  
"glGetPointerv\0"                      
"glArrayElement\0"                     
"glDrawArrays\0"                       
"glDrawElements\0"                     
"glInterleavedArrays\0"                
/* GLX */
"glXChooseVisual\0"
"glXQueryExtensionsString\0"
"glXQueryServerString\0"
"glXGetClientString\0"
"glXCreateContext\0"
"glXCreateNewContext\0"
"glXCopyContext\0"
"glXDestroyContext\0"
"glXQueryVersion\0"
"glXMakeCurrent\0"
"glXSwapBuffers\0"
"glXGetConfig\0"
"glXQueryExtension\0"
"glXChooseFBConfig\0"
"glXGetFBConfigs\0"
"glXGetFBConfigAttrib\0"
"glXQueryContext\0"
"glXQueryDrawable\0"
"glXGetVisualFromFBConfig\0"
"glXIsDirect\0"
"\0"
};

/* Like wglGetProcAddress/glxGetProcAddress */
void *glo_getprocaddress(const char *procName) {
    HGLRC oldCtx;
    HDC oldDC;
    if (!glo_inited)
      glo_init();

    oldCtx = wglGetCurrentContext();
    oldDC = wglGetCurrentDC();
    if (oldDC!=glo.hDC || oldCtx!=glo.hContext)
        wglMakeCurrent(glo.hDC, glo.hContext);

    void *procAddr = wglGetProcAddress(procName);

    if (oldDC!=glo.hDC || oldCtx!=glo.hContext)
        wglMakeCurrent(oldDC, oldCtx);

    /* wgl doesn't know about the glx functions - but
    we never call these anyway (they're implemented in
    opengl_exec), so all we need to do is return a nunzero value...

    But we also have to check for 'standard' GL function names
    too as wgl doesn't return those either! */ 
    if (procAddr==0) {
        const char *p = STANDARD_GL_FUNCTIONS;
        while (*p) {
            if (!strcmp(procName, p)) {
                procAddr = (void*)1;
                break;
            }
            // skip to the next '0' and then just over it
            while (*p) p++;
            p++;
        }
    }

    /*printf("wglGetProcAddress '%s' -> %p\n", procName, procAddr);
    fflush(stdout);*/

    return procAddr;
}

/* ------------------------------------------------------------------------ */

/* Create an OpenGL context for a certain pixel format. formatflags are from the GLO_ constants */
GloContext *glo_context_create(int formatFlags) {
    GloContext *context;
    // pixel format attributes
    int          pf_attri[] = {
       WGL_SUPPORT_OPENGL_ARB, TRUE,      
       WGL_DRAW_TO_PBUFFER_ARB, TRUE,     
       WGL_RED_BITS_ARB, 8,             
       WGL_GREEN_BITS_ARB, 8,            
       WGL_BLUE_BITS_ARB, 8,            
       WGL_ALPHA_BITS_ARB, 8,
       WGL_DEPTH_BITS_ARB, 0,
       WGL_STENCIL_BITS_ARB, 0,
       WGL_DOUBLE_BUFFER_ARB, FALSE,      
       0                                
    };
    float        pf_attrf[] = {0, 0};
    unsigned int numReturned = 0;
    int          pb_attr[] = { 0 };
    int          rgbaBits[4];


    if (!glo_inited)
      glo_init();

    context = (GloContext*)malloc(sizeof(GloContext));
    memset(context, 0, sizeof(GloContext));
    context->formatFlags = formatFlags;

    // set up the surface format from the flags we were given
    glo_flags_get_rgba_bits(context->formatFlags, rgbaBits);
    pf_attri[5]  = rgbaBits[0];
    pf_attri[7]  = rgbaBits[1];
    pf_attri[9]  = rgbaBits[2];
    pf_attri[11] = rgbaBits[3];
    pf_attri[13] = glo_flags_get_depth_bits(context->formatFlags);
    pf_attri[15] = glo_flags_get_stencil_bits(context->formatFlags);

    // find out what pixel format to use
    wglChoosePixelFormatARB( glo.hDC, pf_attri, pf_attrf, 1, &context->wglPixelFormat, &numReturned);
    if( numReturned == 0 ) {
        printf( "No matching configs found.\n" );
        exit( EXIT_FAILURE );
    }

    // We create a tiny pbuffer - just so we can make a context of the right pixel format
    context->hPBuffer = wglCreatePbufferARB( glo.hDC, context->wglPixelFormat, 
                                             16, 16, pb_attr );
    if( !context->hPBuffer ) {
      printf( "Couldn't create the PBuffer\n" );
      exit( EXIT_FAILURE );
    }
    context->hDC      = wglGetPbufferDCARB( context->hPBuffer );
    if( !context->hDC ) {
      printf( "Couldn't create the DC\n" );
      exit( EXIT_FAILURE );
    }

    context->hContext = wglCreateContext(context->hDC);
    if (context->hContext == NULL) {
      printf( "Unable to create GL context\n" );
      exit( EXIT_FAILURE );
    }
/*
    if (shareLists) {
      // Need to share lists...
      wglShareLists(shareLists->hContext, context->hContext);
    }
*/
	glo_set_current(context);
    return context;
}

/* Set current context */
void glo_set_current(GloContext *context)
{
	if(context == NULL)
		wglMakeCurrent(NULL, NULL);
	else
		wglMakeCurrent(context->hDC, context->hContext);
}

/* Destroy a previously created OpenGL context */
void glo_context_destroy(GloContext *context) {
    if (!context) return;

    wglMakeCurrent( NULL, NULL );
    if( context->hPBuffer != NULL ) {
        wglReleasePbufferDCARB( context->hPBuffer, context->hDC );
        wglDestroyPbufferARB( context->hPBuffer );
    }
    if( context->hDC != NULL ) {
        ReleaseDC( glo.hWnd, context->hDC );
    }
    if (context->hContext) {
      wglDeleteContext(context->hContext);
    }
    free(context);
}

/* ------------------------------------------------------------------------ */

/* Create a surface with given width and height, formatflags are from the
 * GLO_ constants */
GloSurface *glo_surface_create(int width, int height, GloContext *context) {
    GloSurface           *surface;
    int                   pb_attr[] = { 0 };
    
    // Create the p-buffer...
    surface = (GloSurface*)malloc(sizeof(GloSurface));
    memset(surface, 0, sizeof(GloSurface));
    surface->width = width;
    surface->height = height;
    surface->context = context;

    surface->hPBuffer = wglCreatePbufferARB( glo.hDC, context->wglPixelFormat, 
                                             surface->width, surface->height, pb_attr );
    if( !surface->hPBuffer ) {
      printf( "Couldn't create the PBuffer\n" );
      exit( EXIT_FAILURE );
    }
    surface->hDC      = wglGetPbufferDCARB( surface->hPBuffer );
    if( !surface->hDC ) {
      printf( "Couldn't create the DC\n" );
      exit( EXIT_FAILURE );
    }

    return surface;
}

/* Destroy the given surface */
void glo_surface_destroy(GloSurface *surface) {
    if (!surface) return;

    wglMakeCurrent( NULL, NULL );
    if( surface->hPBuffer != NULL ) {
        wglReleasePbufferDCARB( surface->hPBuffer, surface->hDC );
        wglDestroyPbufferARB( surface->hPBuffer );
    }
    if( surface->hDC != NULL ) {
        ReleaseDC( glo.hWnd, surface->hDC );
    }
    free(surface);
}

/* Make the given surface current */
int glo_surface_makecurrent(GloSurface *surface) {
  if (surface) {
    return wglMakeCurrent( surface->hDC, surface->context->hContext );
  } else {
    return wglMakeCurrent( NULL, NULL );
  }
}

/* Get the contents of the given surface */
void glo_surface_getcontents(GloSurface *surface, int stride, int bpp, void *data) {

  if (!surface)
    return;
  // Compatible / fallback method.
  glo_surface_getcontents_readpixels(surface->context->formatFlags,
                                        stride, bpp, surface->width,
                                        surface->height, data);
}

/* Return the width and height of the given surface */
void glo_surface_get_size(GloSurface *surface, int *width, int *height) {
    if (width)
      *width = surface->width;
    if (height)
      *height = surface->height;
}

/* Fake glXQueryExtensionsString() */
const char *glo_glXQueryExtensionsString(void) {
  return "";
}

#endif
