/*!
 * @file osi_types.h
 * @brief Base data types for PANDA OSI.
 */
#pragma once
#include <gmodule.h>
#include <stdio.h>
#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif
#include <qemu-plugin.h>
#ifdef __cplusplus
}
#endif

/**
 * @brief Wrapper macro for quashing warnings for unused variables.
 */
#if defined(UNUSED)
#elif defined(__GNUC__)
#define UNUSED(x) x __attribute__((unused))
#elif defined(__LCLINT__)
#define UNUSED(x) /*@unused@*/ x
#else
#define UNUSED(x) x
#endif

//Intentional changes for QEMU
typedef void CPUState;
typedef uint64_t target_ulong; // This is iffy?
// Stupid panda shim
static int panda_virtual_memory_rw(uint64_t gva, void* buf, size_t length, bool is_write) {
  assert(is_write == 0); // Not yet implemented
  return qemu_plugin_read_guest_virt_mem(gva, buf, length);
}


#define LOG_INFO printf
#define LOG_ERROR printf
#define unlikely(x)   __builtin_expect(!!(x), 0)

// PANDA types from panda/include/panda.types.h
#define TARGET_PTR_FMT "%lx"
#define TARGET_PID_FMT "%u"
typedef target_ulong target_ptr_t;
typedef int32_t target_pid_t;



// BEGIN_PYPANDA_NEEDS_THIS -- do not delete this comment bc pypanda
// api autogen needs it.  And don't put any compiler directives
// between this and END_PYPANDA_NEEDS_THIS except includes of other
// files in this directory that contain subsections like this one.


/**
 * @brief Minimal handle for a process. Contains a unique identifier \p asid
 * and a task descriptor pointer \p taskd that can be used to retrieve the full
 * details of the process.
 */
typedef struct osi_proc_handle_struct {
    target_ptr_t taskd;
    target_ptr_t asid;
} OsiProcHandle;

/**
 * @brief Minimal information about a process thread.
 * Address space and open resources are shared between threads
 * of the same process. This information is stored in OsiProc.
 */
typedef struct osi_thread_struct {
    target_pid_t pid;
    target_pid_t tid;
} OsiThread;

/**
 * @brief Represents a page in the address space of a process.
 *
 * @note This has not been implemented/used so far.
 */
typedef struct osi_page_struct {
    target_ptr_t start;
    target_ulong len;
} OsiPage;

/**
 * @brief Represents information about a guest OS module (kernel module
 * or shared library).
 */
typedef struct osi_module_struct {
    target_ptr_t modd;
    target_ptr_t base;
    target_ptr_t size;
    char *file;
    char *name;
} OsiModule;

/**
 * @brief Detailed information for a process.
 */
typedef struct osi_proc_struct {
    target_ptr_t taskd;
    target_ptr_t asid;
    target_pid_t pid;
    target_pid_t ppid;
    char *name;
    OsiPage *pages;
    uint64_t create_time;
} OsiProc;
// END_PYPANDA_NEEDS_THIS -- do not delete this comment!


/* ******************************************************************
 * Helper functions for freeing/copying osi structs.
 ******************************************************************* */

/**
 * @brief Dummy function for freeing contents of OsiProcHandle.
 * Meant to be passed to g_array_set_clear_func().
 * Defining a NULL function pointer rather than an an empty function
 * avoids unneeded calls during g_array_free().
 */
static void UNUSED((*free_osiprochandle_contents)(OsiProcHandle *)) = NULL;

/**
 * @brief Frees an OsiProcHandle struct and its contents.
 * To be used for freeing standalone OsiProcHandle structs.
 */
static inline void free_osiprochandle(OsiProcHandle *h) {
    g_free(h);
}

/**
 * @brief Dummy function for freeing contents of OsiThread.
 * Meant to be passed to g_array_set_clear_func().
 * Defining a NULL function pointer rather than an an empty function
 * avoids unneeded calls during g_array_free().
 */
static void UNUSED((*free_osithread_contents)(OsiThread *)) = NULL;

/**
 * @brief Frees an OsiThread struct and its contents.
 * To be used for freeing standalone OsiThread structs.
 */
static inline void free_osithread(OsiThread *t) {
    g_free(t);
}

/**
 * @brief Dummy function for freeing contents of OsiPage.
 * Meant to be passed to g_array_set_clear_func().
 * Defining a NULL function pointer rather than an an empty function
 * avoids unneeded calls during g_array_free().
 */
static void UNUSED((*free_osipage_contents)(OsiPage *)) = NULL;

/**
 * @brief Frees an OsiPage struct and its contents.
 * To be used for freeing standalone OsiPage structs.
 */
static inline void free_osipage(OsiPage *p) {
    g_free(p);
}

/**
 * @brief Frees the contents of an OsiModule struct.
 * Meant to be passed to g_array_set_clear_func.
 */
static inline void free_osimodule_contents(OsiModule *m) {
    if (m == NULL) return;
    g_free(m->file);
    g_free(m->name);
}

/**
 * @brief Frees an OsiModule struct and its contents.
 * To be used for freeing standalone OsiModule structs.
 */
static inline void free_osimodule(OsiModule *m) {
    free_osimodule_contents(m);
    g_free(m);
}

/**
 * @brief Frees the contents of an OsiProc struct.
 * Meant to be passed to g_array_set_clear_func.
 */
static inline void free_osiproc_contents(OsiProc *p) {
    if (p == NULL) return;
    g_free(p->name);
    g_free(p->pages);
}

/**
 * @brief Frees an OsiProc struct and its contents.
 * To be used for freeing standalone OsiProc structs.
 */
static inline void free_osiproc(OsiProc *p) {
    free_osiproc_contents(p);
    g_free(p);
}

/**
 * @brief Copies an OsiProcHandle struct.
 * Returns a pointer to the destination location.
 */
static inline OsiProcHandle *copy_osiprochandle(OsiProcHandle *from, OsiProcHandle *to) {
    if (from == NULL) return NULL;
    if (to == NULL) {
        to = (OsiProcHandle *)g_malloc0(sizeof(OsiProc));
    } else if (free_osiprochandle_contents != NULL) {
        free_osiprochandle_contents(to);
    }
    memcpy(to, from, sizeof(OsiProc));
    return to;
}

/**
 * @brief Copies an OsiProc struct.
 * Returns a pointer to the destination location.
 */
static inline OsiProc *copy_osiproc(OsiProc *from, OsiProc *to) {
    if (from == NULL) return NULL;
    if (to == NULL) {
        to = (OsiProc *)g_malloc0(sizeof(OsiProc));
    } else {
        free_osiproc_contents(to);
    }
    memcpy(to, from, sizeof(OsiProc));
    to->name = g_strdup(from->name);
    to->pages = NULL;  // OsiPage - TODO
    return to;
}

/**
 * @brief Copies an OsiModule struct.
 * Returns a pointer to the destination location.
 */
static inline OsiModule *copy_osimod(OsiModule *from, OsiModule *to) {
    if (from == NULL) return NULL;
    if (to == NULL) {
        to = (OsiModule *)g_malloc0(sizeof(OsiModule));
    } else {
        free_osimodule_contents(to);
    }
    memcpy(to, from, sizeof(OsiModule));
    to->name = g_strdup(from->name);
    to->file = g_strdup(from->file);
    return to;
}

/* vim:set tabstop=4 softtabstop=4 expandtab: */
