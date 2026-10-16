/* switch-sdl2 calls EGL for OpenGL windows, which the game never opens: RT64 renders with Vulkan
   (Mesa's NVK, linked in, without its EGL). devkitPro's EGL (switch-mesa 20.1) can't be linked next to
   NVK, so SDL's calls land here and fail: SDL_CreateWindow without SDL_WINDOW_OPENGL never makes them. */

#include <stddef.h>

#include <EGL/egl.h>

EGLBoolean eglBindAPI(EGLenum api) { (void)api; return EGL_FALSE; }
EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint* attrib_list, EGLConfig* configs, EGLint config_size, EGLint* num_config) {
    (void)dpy; (void)attrib_list; (void)configs; (void)config_size;
    if (num_config != NULL) {
        *num_config = 0;
    }
    return EGL_FALSE;
}
EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig config, EGLContext share_context, const EGLint* attrib_list) {
    (void)dpy; (void)config; (void)share_context; (void)attrib_list;
    return EGL_NO_CONTEXT;
}
EGLSurface eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config, const EGLint* attrib_list) {
    (void)dpy; (void)config; (void)attrib_list;
    return EGL_NO_SURFACE;
}
EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config, EGLNativeWindowType win, const EGLint* attrib_list) {
    (void)dpy; (void)config; (void)win; (void)attrib_list;
    return EGL_NO_SURFACE;
}
EGLBoolean eglDestroyContext(EGLDisplay dpy, EGLContext ctx) { (void)dpy; (void)ctx; return EGL_FALSE; }
EGLBoolean eglDestroySurface(EGLDisplay dpy, EGLSurface surface) { (void)dpy; (void)surface; return EGL_FALSE; }
EGLBoolean eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config, EGLint attribute, EGLint* value) {
    (void)dpy; (void)config; (void)attribute; (void)value;
    return EGL_FALSE;
}
EGLDisplay eglGetDisplay(EGLNativeDisplayType display_id) { (void)display_id; return EGL_NO_DISPLAY; }
EGLDisplay eglGetPlatformDisplay(EGLenum platform, void* native_display, const EGLAttrib* attrib_list) {
    (void)platform; (void)native_display; (void)attrib_list;
    return EGL_NO_DISPLAY;
}
EGLint eglGetError(void) { return EGL_NOT_INITIALIZED; }
__eglMustCastToProperFunctionPointerType eglGetProcAddress(const char* procname) { (void)procname; return NULL; }
EGLBoolean eglInitialize(EGLDisplay dpy, EGLint* major, EGLint* minor) { (void)dpy; (void)major; (void)minor; return EGL_FALSE; }
EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx) {
    (void)dpy; (void)draw; (void)read; (void)ctx;
    return EGL_FALSE;
}
EGLenum eglQueryAPI(void) { return EGL_NONE; }
const char* eglQueryString(EGLDisplay dpy, EGLint name) { (void)dpy; (void)name; return NULL; }
EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) { (void)dpy; (void)surface; return EGL_FALSE; }
EGLBoolean eglSwapInterval(EGLDisplay dpy, EGLint interval) { (void)dpy; (void)interval; return EGL_FALSE; }
EGLBoolean eglTerminate(EGLDisplay dpy) { (void)dpy; return EGL_FALSE; }
EGLBoolean eglWaitGL(void) { return EGL_FALSE; }
EGLBoolean eglWaitNative(EGLint engine) { (void)engine; return EGL_FALSE; }
