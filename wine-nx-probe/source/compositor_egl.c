/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later.
 * The compositor's Switch backend: Mesa's EGL on the screen's NWindow, with an
 * OpenGL 3.2 core context of its own. See compositor.h. */
#include <stdio.h>
#include <switch.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "compositor.h"
#include "compositor_gl.h"

/* runtime.c, which also leaves the text console before starting the compositor. */
extern void wine_nx_runtime_trace( const char *msg );

static EGLDisplay egl_display;
static EGLConfig egl_config;
static EGLContext egl_context;
static EGLSurface egl_surface;
static struct compositor_gl_funcs gl_funcs;

static const struct compositor_gl_funcs *egl_init( char *error, int size )
{
    static const EGLint config_attribs[] =
    {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    static const EGLint context_attribs[] =
    {
        EGL_CONTEXT_MAJOR_VERSION_KHR, 3,
        EGL_CONTEXT_MINOR_VERSION_KHR, 2,
        EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
        EGL_NONE
    };
    EGLint count;

    if (!(egl_display = eglGetDisplay( EGL_DEFAULT_DISPLAY )) || !eglInitialize( egl_display, NULL, NULL ))
    {
        snprintf( error, size, "eglInitialize failed, error 0x%x", eglGetError() );
        return NULL;
    }
    if (!eglBindAPI( EGL_OPENGL_API ))
    {
        snprintf( error, size, "eglBindAPI failed, error 0x%x", eglGetError() );
        return NULL;
    }
    /* Configs are sorted with the smallest depth buffer first, and none is needed. */
    if (!eglChooseConfig( egl_display, config_attribs, &egl_config, 1, &count ) || !count)
    {
        snprintf( error, size, "no RGBA8 window config, error 0x%x", eglGetError() );
        return NULL;
    }
    if (!(egl_context = eglCreateContext( egl_display, egl_config, EGL_NO_CONTEXT, context_attribs )))
    {
        snprintf( error, size, "no OpenGL 3.2 core context, error 0x%x", eglGetError() );
        return NULL;
    }
#define USE_GL_FUNC( ret, name, args ) \
    if (!(gl_funcs.name = (ret (*) args)eglGetProcAddress( "gl" #name ))) \
    { \
        snprintf( error, size, "no gl" #name ); \
        return NULL; \
    }
    COMPOSITOR_GL_FUNCS
#undef USE_GL_FUNC
    return &gl_funcs;
}

static int egl_attach( int width, int height, char *error, int size )
{
    NWindow *window = nwindowGetDefault();

    nwindowSetDimensions( window, width, height );
    if (!(egl_surface = eglCreateWindowSurface( egl_display, egl_config, (EGLNativeWindowType)window, NULL )))
    {
        snprintf( error, size, "eglCreateWindowSurface failed, error 0x%x", eglGetError() );
        return -1;
    }
    if (!eglMakeCurrent( egl_display, egl_surface, egl_surface, egl_context ))
    {
        snprintf( error, size, "eglMakeCurrent failed, error 0x%x", eglGetError() );
        eglDestroySurface( egl_display, egl_surface );
        egl_surface = EGL_NO_SURFACE;
        return -1;
    }
    /* Wait for the display, so the presenter draws at most one frame a refresh. */
    eglSwapInterval( egl_display, 1 );
    return 0;
}

static void egl_detach( void )
{
    /* Destroying the surface releases the NWindow's buffers for the program. */
    eglMakeCurrent( egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT );
    if (egl_surface) eglDestroySurface( egl_display, egl_surface );
    egl_surface = EGL_NO_SURFACE;
}

static void egl_swap( void )
{
    eglSwapBuffers( egl_display, egl_surface );
}

const struct compositor_backend wine_nx_compositor_egl_backend =
{
    .init = egl_init,
    .attach = egl_attach,
    .detach = egl_detach,
    .swap = egl_swap,
    .log = wine_nx_runtime_trace,
};
