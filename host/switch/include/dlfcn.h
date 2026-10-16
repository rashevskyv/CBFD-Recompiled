/* Horizon has no dynamic libraries. volk (plume) includes this for its loader, which the Switch
   build never uses (it calls volkInitializeCustom with the driver linked in): every call fails. */
#ifndef CONKER_SWITCH_DLFCN_H
#define CONKER_SWITCH_DLFCN_H

#include <stddef.h>

#define RTLD_LAZY 1
#define RTLD_NOW 2
#define RTLD_LOCAL 0
#define RTLD_GLOBAL 4

static inline void* dlopen(const char* filename, int flags) {
    (void)filename;
    (void)flags;
    return NULL;
}

static inline void* dlsym(void* handle, const char* symbol) {
    (void)handle;
    (void)symbol;
    return NULL;
}

static inline int dlclose(void* handle) {
    (void)handle;
    return 0;
}

static inline const char* dlerror(void) {
    return "no dynamic libraries on the Switch";
}

#endif
