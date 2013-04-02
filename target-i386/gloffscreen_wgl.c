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


#define GLO_WINDOW_CLASS "QEmuGLClass"
#define DEFAULT_DEPTH_BUFFER (16)

PFNWGLCHOOSEPIXELFORMATARBPROC wglChoosePixelFormatARB;
PFNWGLGETPBUFFERDCARBPROC wglGetPbufferDCARB;
PFNWGLRELEASEPBUFFERDCARBPROC wglReleasePbufferDCARB;
PFNWGLCREATEPBUFFERARBPROC wglCreatePbufferARB;
PFNWGLDESTROYPBUFFERARBPROC wglDestroyPbufferARB;

/*  */
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

	glo_set_current(context);
    return context;
}

/* Set current context */
void glo_set_current(GloContext *context) {

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


/* Check extension implementation for Windows. The Glu 1.2 framework in Windows doesn't include them... */
GLboolean glo_check_extension( const GLubyte *extName, const GLubyte *extString ) {

	char *p = (char *) glGetString(GL_EXTENSIONS); 
	char *end;
	if(p==NULL)
		return GL_FALSE;
	end = p + strlen(p);
	
	while (p < end) {
		int n = strcspn(p, " ");
		if((strlen(extName) == n) && (strncmp(extName,p,n) == 0)) {
			return GL_TRUE;
		}
		p += (n + 1);
	}
	return GL_FALSE;
}
