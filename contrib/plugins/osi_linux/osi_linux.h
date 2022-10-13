/*!
 * @file osi_linux.h
 * @brief Definitions for the implementation of Linux OSI.
 *
 * This header file is not meant to be used by plugins building
 * upon the functionality of Linux OSI.
 * For <a href="https://github.com/panda-re/panda/blob/master/panda/docs/manual.md#plugin-plugin-interaction">Plugin-Plugin</a>
 * interactions, `osi_linux_ext.h` should be used.
 *
 * The offset getter macros have been based off the code from
 * linux_vmi plugin and TEMU's read_linux.
 *
 *
 * @author Manolis Stamatogiannakis <manolis.stamatogiannakis@vu.nl>
 * @copyright This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */
#pragma once
#if defined(__cplusplus)
#include <cstdint>
#include <initializer_list>
#include <glib.h>
#endif
#include "osi_types.h"

extern "C" {
#include "utils/kernelinfo/kernelinfo.h"
}
#include "osi_linux_debug.h"
#include "kernel_profile.h"
#include "endian_helpers.h"
#include <assert.h>
#include <stdio.h>
extern "C" {
#include <qemu-plugin.h>
}

#ifdef TARGET_MIPS
#include "hw_proc_id/hw_proc_id_ext.h"
#endif

//Intentional changes for QEMU
typedef void CPUState;
typedef uint64_t target_ulong; // This is iffy?

#define LOG_INFO printf
#define LOG_ERROR printf
#define unlikely(x)   __builtin_expect(!!(x), 0)

// PANDA types from panda/include/panda.types.h
#define TARGET_PTR_FMT "%lx"
#define TARGET_PID_FMT "%u"
typedef target_ulong target_ptr_t;
typedef int32_t target_pid_t;

extern struct kernelinfo ki;
extern struct KernelProfile const *kernel_profile;

#if defined(__cplusplus)
typedef enum : int8_t {
    ERROR_DEREF = -10,
    ERROR_MEMORY,
    SUCCESS = 0
} struct_get_ret_t;

// End shims

/**
 * @brief Template function for reading a struct member given a pointer
 * to the struct and the offset of the member.
 * This is a proper C++ replacement for the preprocessor hack macro of
 * IMPLEMENT_OFFSET_GET.
 */
template <typename T>
struct_get_ret_t struct_get(T *v, target_ptr_t ptr, off_t offset) {
    if (ptr == (target_ptr_t)NULL) {
        printf("WARNING: struct get of NULLPTR\n");
        memset((uint8_t *)v, 0, sizeof(T));
        return struct_get_ret_t::ERROR_DEREF;
    }

    switch(panda_virtual_memory_rw(ptr+offset, (void *)v, sizeof(T), 0)) {
        case -1:
            memset((uint8_t *)v, 0, sizeof(T));
            return struct_get_ret_t::ERROR_MEMORY;
            break;
        default:
            return struct_get_ret_t::SUCCESS;
            break;
    }
}

/**
 * @brief Template function for reading a nested struct member given a
 * pointer to the top level struct and a series of offsets.
 * This is a proper C++ replacement for the preprocessor hack macro of
 * IMPLEMENT_OFFSET_GET*.
 */
template <typename T>
struct_get_ret_t struct_get(T *v, target_ptr_t ptr, std::initializer_list<off_t> offsets) {
    // read all but last item as pointers
    // After each pointer read, flip endianness as necessary
    auto it = offsets.begin();
    auto o = *it;
    while (true) {
        it++;
        if (it == offsets.end()) break;
        auto r = struct_get(&ptr, ptr, o);
        if (r != struct_get_ret_t::SUCCESS) {
            memset((uint8_t *)v, 0, sizeof(T));
            return r;
        }
        o = *it;
        // We just read a pointer so we may need to fix its endianness
        if (sizeof(T) == 4) fixupendian(ptr); // XXX wrong for 64-bit guests
    }

    // last item is read using the size of the type of v
    // this isn't a pointer so there's no need to fix its endianness
    auto ret = struct_get(v, ptr, o); // deref ptr into v, result in ret
    fixupendian(*v);
    return ret;
}
#endif

/**
 * @brief IMPLEMENT_OFFSET_GET is a macro for generating uniform
 * inlines for retrieving data based on a location+offset.
 *
 * @deprecated Directly returning a value complicates error handling
 * and doesn't work for arrays or simple structs.
 * Use IMPLEMENT_OFFSET_GETN instead.
 */
#define IMPLEMENT_OFFSET_GET(_name, _paramName, _retType, _offset, _errorRetValue) \
static inline _retType _name(target_ptr_t _paramName) { \
    _retType _t; \
    if (-1 == panda_virtual_memory_rw(_paramName + _offset, &_t, sizeof(_retType), 0)) { \
        return (_errorRetValue); \
    } \
    return (flipbadendian(_t)); \
}

/**
 * @brief IMPLEMENT_OPTIONAL_OFFSET_GET is a macro for generating uniform
 * inlines for retrieving data based on a location+offset as above, but
 * it returns 0 if the underlying offset was not read in the first place
 * and was optional.
 *
 * @deprecated Directly returning a value complicates error handling
 * and doesn't work for arrays or simple structs.
 * Use IMPLEMENT_OFFSET_GETN instead.
 */
#define IMPLEMENT_OPTIONAL_OFFSET_GET(_name, _paramName, _retType, _offset, _errorRetValue) \
static inline _retType _name(target_ptr_t _paramName) { \
    _retType _t; \
    if (_offset == NULL)\
        return 0; \
    if (-1 == panda_virtual_memory_rw(_paramName + _offset, &_t, sizeof(_retType), 0)) { \
        return (_errorRetValue); \
    } \
    return (flipbadendian(_t)); \
}




/**
 * @brief IMPLEMENT_OFFSET_GET2L is a macro for generating uniform
 * inlines for retrieving data based on a *(location+offset1) + offset2.
 *
 * @deprecated Directly returning a value complicates error handling
 * and doesn't work for arrays or simple structs.
 * Use IMPLEMENT_OFFSET_GET2LN instead.
 */
#define IMPLEMENT_OFFSET_GET2L(_name, _paramName, _retType1, _offset1, _retType2, _offset2, _errorRetValue) \
static inline _retType2 _name(target_ptr_t _paramName) { \
    _retType1 _t1; \
    _retType2 _t2; \
    if (-1 == panda_virtual_memory_rw(_paramName + _offset1, &_t1, sizeof(_retType1), 0)) { \
        return (_errorRetValue); \
    } \
    if (-1 == panda_virtual_memory_rw(flipbadendian(_t1) + _offset2, &_t2, sizeof(_retType2), 0)) { \
        return (_errorRetValue); \
    } \
    return (flipbadendian(_t2)); \
}

#define OG_AUTOSIZE 0
#define OG_SUCCESS 0
#define OG_ERROR_MEMORY -1
#define OG_ERROR_DEREF -2
#define OG_printf(...)
//#define OG_printf(...) printf(__VA_ARGS__)

/**
 * @brief IMPLEMENT_OFFSET_GETN is a macro for generating uniform
 * inlines for retrieving data based on a location+offset.
 * It provides better error handling than IMPLEMENT_OFFSET_GET and is not
 * limited to retrieving only primitive types.
 */
#define IMPLEMENT_OFFSET_GETN(_funcName, _paramName, _retType, _retName, _retSize, _offset) \
static inline int _funcName(target_ptr_t _paramName, _retType* _retName) { \
    size_t ret_size = ((_retSize) == OG_AUTOSIZE) ? sizeof(_retType) : (_retSize); \
    OG_printf(#_funcName ":1:" TARGET_PTR_FMT ":%d\n", _paramName, _offset); \
    OG_printf(#_funcName ":2:" TARGET_PTR_FMT ":%zu\n", _paramName + _offset, ret_size); \
    if (-1 == panda_virtual_memory_rw(_paramName + _offset, _retName, ret_size, 0)) { \
        return OG_ERROR_MEMORY; \
    } \
    OG_printf(#_funcName ":3:ok\n"); \
    return OG_SUCCESS; \
}

/**
 * @brief IMPLEMENT_OFFSET_GET2LN is an improved macro for generating uniform
 * inlines for retrieving data based on a *(location+offset1) + offset2.
 * It provides better error handling than IMPLEMENT_OFFSET_GET2L and is not
 * limited to retrieving only primitive types.
 */
#define IMPLEMENT_OFFSET_GET2LN(_funcName, _paramName, _retType, _retName, _retSize, _offset1, _offset2) \
static inline int _funcName(target_ptr_t _paramName, _retType* _retName) { \
    target_ptr_t _p1; \
    size_t ret_size = ((_retSize) == OG_AUTOSIZE) ? sizeof(_retType) : (_retSize); \
    OG_printf(#_funcName ":1:" TARGET_PTR_FMT ":%d\n", _paramName, _offset1); \
    OG_printf(#_funcName ":2:" TARGET_PTR_FMT ":%zu\n", _paramName + _offset1, sizeof(target_ptr_t)); \
    if (-1 == panda_virtual_memory_rw(_paramName + _offset1, &_p1, sizeof(target_ptr_t), 0)) { \
        return OG_ERROR_MEMORY; \
    } \
    OG_printf(#_funcName ":3:" TARGET_PTR_FMT ":%d\n", _p1, _offset2); \
    if (_p1 == (target_ptr_t)NULL) { \
        return OG_ERROR_DEREF; \
    } \
    OG_printf(#_funcName ":4:" TARGET_PTR_FMT ":%zu\n", _p1 + _offset2, ret_size); \
    if (-1 == panda_virtual_memory_rw(_p1 + _offset2, _retName, ret_size, 0)) { \
        return OG_ERROR_MEMORY; \
    } \
    OG_printf(#_funcName ":5:ok\n"); \
    return OG_SUCCESS; \
}



/* ******************************************************************
 Offset getters are defined below. Only the getters used by the
 plugin have been defined. See kernelinfo.conf to see what additional
 getters can be added.
****************************************************************** */

/**
 * @brief Retrieves the thread group address from task_struct.
 * If the thread group address points back to itself, then the task_struct
 * corresponds to a process.
 */
IMPLEMENT_OFFSET_GET(get_thread_group, task_struct, target_ptr_t, ki.task.thread_group_offset, 0)

/**
 * @brief Retrieves the pid from a task_struct.
 */
IMPLEMENT_OFFSET_GET(get_pid, task_struct, int, ki.task.pid_offset, 0)

/**
 * @brief Retrieves the tgid from a task_struct.
 */
IMPLEMENT_OFFSET_GET(get_tgid, task_struct, int, ki.task.tgid_offset, 0)

/**
 * @brief Retrieves the start_time from a task_struct.
 */
IMPLEMENT_OPTIONAL_OFFSET_GET(get_start_time, task_struct, uint64_t, ki.task.start_time_offset, 0)


/**
 * @brief Retrieves the address of the mm_struct from a task_struct.
 */
IMPLEMENT_OFFSET_GET(get_mm_start_brk, mm_struct, target_ptr_t, ki.mm.start_brk_offset, 0)

/**
 * @brief Retrieves the address of the mm_struct from a task_struct.
 */
IMPLEMENT_OFFSET_GET(get_mm_brk, mm_struct, target_ptr_t, ki.mm.brk_offset, 0)

/**
 * @brief Retrieves the address of the mm_struct from a task_struct.
 */
IMPLEMENT_OFFSET_GET(get_mm_start_stack, mm_struct, target_ptr_t, ki.mm.start_stack_offset, 0)

/**
 * @brief Retrieves the address of the first vm_area_struct of the task.
 */
IMPLEMENT_OFFSET_GET2L(get_vma_first, task_struct, target_ptr_t, ki.task.mm_offset, target_ptr_t, ki.mm.mmap_offset, 0)

/**
 * @brief Retrieves the address of the following vm_area_struct.
 * This is used to iterate the mmap list.
 */
IMPLEMENT_OFFSET_GET(get_vma_next, vma_struct, target_ptr_t, ki.vma.vm_next_offset, 0)

/**
 * @brief Retrieves the of the mm_struct where this vm_area_struct belongs to.
 */
IMPLEMENT_OFFSET_GET(get_vma_vm_mm, vma_struct, target_ptr_t, ki.vma.vm_mm_offset, 0)

/**
 * @todo Retrieves the address of the following vm_area_struct.
 */
IMPLEMENT_OFFSET_GET(get_vma_start, vma_struct, target_ulong, ki.vma.vm_start_offset, 0)

/**
 * @todo Retrieves the address of the following vm_area_struct.
 */
IMPLEMENT_OFFSET_GET(get_vma_end, vma_struct, target_ulong, ki.vma.vm_end_offset, 0)

/**
 * @todo Retrieves the address of the following vm_area_struct.
 */
IMPLEMENT_OFFSET_GET(get_vma_flags, vma_struct, target_ulong, ki.vma.vm_flags_offset, 0)

/**
 * @brief Retrieves the address of the mm_struct from a task_struct.
 */
IMPLEMENT_OFFSET_GET(get_vma_vm_file, vma_struct, target_ptr_t, ki.vma.vm_file_offset, 0)

/**
 * @brief Retrieves the dentry associated with a vma_struct.
 *
 * XXX: Convert uses of this to the single level getter of f_path_dentry_offset.
 * Operating on file structs vs vma structs, will help to share code between
 * mm resolution and fd resolution.
 */
IMPLEMENT_OFFSET_GET2L(get_vma_dentry, vma_struct, target_ptr_t, ki.vma.vm_file_offset, target_ptr_t, ki.fs.f_path_dentry_offset, 0)

/**
 * @brief Retrieves the vfsmount dentry associated with a vma_struct.
 *
 * XXX: Reading the vfsmount dentry is required to get the full pathname of files not located in the root fs.
 * This hasn't been implemented yet...
 */
IMPLEMENT_OFFSET_GET2L(get_vma_vfsmount_dentry, vma_struct, target_ptr_t, ki.vma.vm_file_offset, target_ptr_t, ki.fs.f_path_dentry_offset, 0)

/**
 * @brief Retrieves the address of the files struct associated with a task_struct.
 */
IMPLEMENT_OFFSET_GET(get_files, task_struct, target_ptr_t, ki.task.files_offset, 0)

/**
 * @brief Retrieves the dentry struct associated with a file struct.
 */
IMPLEMENT_OFFSET_GET(get_file_dentry, file_struct, target_ptr_t, ki.fs.f_path_dentry_offset, 0)

/**
 * @brief Retrieves the vfsmount struct associated with a file struct.
 */
IMPLEMENT_OFFSET_GET(get_file_mnt, file_struct, target_ptr_t, ki.fs.f_path_mnt_offset, 0)

IMPLEMENT_OFFSET_GET(get_file_pos, file_struct, target_ptr_t, ki.fs.f_pos_offset, 0)

/**
 * @brief Retrieves the mnt_parent vfsmount struct associated with a vfsmount struct.
 */
IMPLEMENT_OFFSET_GETN(get_vfsmount_parent, vfsmount, target_ptr_t, vfsmount_parent, OG_AUTOSIZE, ki.path.mnt_parent_offset)

/**
 * @brief Retrieves the dentry struct associated with a vfsmount struct.
 */
IMPLEMENT_OFFSET_GETN(get_vfsmount_dentry, vfsmount, target_ptr_t, vfsmount_dentry, OG_AUTOSIZE, ki.path.mnt_mountpoint_offset)

/**
 * @brief Retrieves the mnt_root dentry struct associated with a vfsmount struct.
 */
IMPLEMENT_OFFSET_GETN(get_vfsmount_root_dentry, vfsmount, target_ptr_t, root_dentry, OG_AUTOSIZE, ki.path.mnt_root_offset)

/**
 * @brief Retrieves the qstr for a dentry.
 */
IMPLEMENT_OFFSET_GETN(get_dentry_name, dentry, uint8_t, dname_qstr, ki.qstr.size*sizeof(uint8_t), ki.path.d_name_offset)

/**
 * @brief Retrieves the dynamic name function for a dentry.
 */
IMPLEMENT_OFFSET_GET2LN(get_dentry_dname, dentry, target_ptr_t, dname_funcp, OG_AUTOSIZE, ki.path.d_op_offset, ki.path.d_dname_offset)

/**
 * @brief Retrieves the parent of a dentry.
 */
IMPLEMENT_OFFSET_GETN(get_dentry_parent, dentry, target_ptr_t, dentry_parent, OG_AUTOSIZE, ki.path.d_parent_offset)

/* ******************************************************************
 Slightly more complex inlines that can't be implemented as simple
 offset getters.
****************************************************************** */
/**
 * @brief Retrieves the n-th file struct from an fd file array. (pp 479)
 */
static inline target_ptr_t get_fd_file(target_ptr_t fd_file_array, int n) {
    target_ptr_t fd_file, fd_file_ptr;
    // Compute address of the pointer to the file struct of the n-th fd.
    fd_file_ptr = fd_file_array+n*sizeof(target_ptr_t);

    // Read address of the file struct.
    if (-1 == panda_virtual_memory_rw(fd_file_ptr, &fd_file, sizeof(target_ptr_t), 0)) {
        return (target_ptr_t)NULL;
    }
    fixupendian(fd_file_ptr);
    return fd_file_ptr;
}

/**
 * @brief Retrieves the name of the file associated with a dentry struct.
 *
 * The function traverses all the path components it meets until it
 * reaches a mount point. 
 *
 * @note We can always use dentry.d_name->name and ignore dentry.d_iname.
 * When the latter is used, the former will be set to point to it.
 */
static inline char *read_dentry_name(target_ptr_t dentry) {
    char *name = NULL;

    // current path component
    char *pcomp = NULL;
    uint32_t pcomp_length = 0;
    uint32_t pcomp_capacity = 0;

    // all path components read so far
    char **pcomps = NULL;
    uint32_t pcomps_idx = 0;
    uint32_t pcomps_capacity = 0;

    // for reversing pcomps
    char **pcomps_start, **pcomps_end;

    target_ptr_t current_dentry_parent = dentry;
    target_ptr_t current_dentry = (target_ptr_t)NULL;
    uint8_t *d_name = (uint8_t *)g_malloc(ki.qstr.size * sizeof(uint8_t));
    while (current_dentry_parent != current_dentry) {
        int og_err1, og_err2;
        current_dentry = current_dentry_parent;
        //printf("1#%lx\n", (uintptr_t)(current_dentry + ki.path.d_name_offset));

        // read dentry d_parent and d_name
        memset(d_name, 0, ki.qstr.size * sizeof(uint8_t));
        og_err1 = get_dentry_name(current_dentry, d_name);
        og_err2 = get_dentry_parent(current_dentry, &current_dentry_parent);

        // Note we don't fix endian of dentry name because it's a large(r than 4)
        // byte buffer. Instead we'll fix it just before use (in guest_addr)
        fixupendian(current_dentry_parent);

        //HEXDUMP(d_name, ki.path.qstr_size, current_dentry + ki.path.d_name_offset);
        if (OG_SUCCESS != og_err1 || OG_SUCCESS != og_err2) {
            break;
        }

        // read d_dname function pointer - indicates a dynamic name
        target_ptr_t d_dname;
        og_err1 = get_dentry_dname(current_dentry, &d_dname);
        if (OG_SUCCESS != og_err1) {
            // static name
            d_dname = (target_ptr_t)NULL;
        }

        // read component
        pcomp_length = *(uint32_t *)(d_name + sizeof(uint32_t));
        fixupendian(pcomp_length);
        if (pcomp_length == (uint32_t)-1) { // Not sure why this happens, but it does
              printf("Warning: OSI_linux Unhandled pcomp value, ignoring\n");
              break;
        }
        pcomp_length += 1; // space for string terminator

        if (pcomp_capacity < pcomp_length) {
            pcomp_capacity = pcomp_length + 16;
            pcomp = (char *)g_realloc(pcomp, pcomp_capacity * sizeof(char));
            if (pcomp == NULL) {
              printf("Warning: OSI_linux pcomp g_realloc failed\n");
              break;
            }
        }

        target_ptr_t guest_addr = *(target_ptr_t *)(d_name + ki.qstr.name_offset);
        fixupendian(guest_addr);
        og_err1 = panda_virtual_memory_rw(guest_addr, (void *)pcomp, pcomp_length*sizeof(char), 0);

    // I think this aims to be a re-implementation of the Linux kernel function
    // __dentry_path but the logic seems pretty different.
        //printf("2#%lx\n", (uintptr_t)*(target_ptr_t *)(d_name + 2*sizeof(uint32_t)));
        //printf("3#%s\n", pcomp);
        if (-1 == og_err1) {
            break;
        }

        // use the empty string for "/" components (mountpoints?)
        if (pcomp[0] == '/' && pcomp[1] == '\0') {
            pcomp[0] = '\0';
        }

        // copy component
        if (pcomps_idx + 1 >= pcomps_capacity) { // +1 accounts for the terminating NULL
            pcomps_capacity += 16;
            pcomps = (char **)g_realloc(pcomps, pcomps_capacity * sizeof(char *));
        }
        if (d_dname == (target_ptr_t)NULL) {
            // static name
            pcomps[pcomps_idx++] = g_strdup(pcomp);
        }
        else {
            // XXX: full reconstruction of dynamic names in not currently supported
            pcomps[pcomps_idx++] = g_strdup(pcomp);
        }
    }

    // reverse components order and join them
    g_free(d_name);
    g_free(pcomp);
    if (pcomps != NULL) {
        pcomps_start = pcomps;
        pcomps_end = &pcomps[pcomps_idx - 1];
        while (pcomps_start < pcomps_end) {
            pcomp = *pcomps_start;
            *pcomps_start = *pcomps_end;
            *pcomps_end = pcomp;
            pcomps_start++;
            pcomps_end--;
        }
        pcomps[pcomps_idx] = NULL; // NULL terminate vector
        name = g_strjoinv("/", pcomps);
        g_strfreev(pcomps);
    }

#if defined(OSI_LINUX_FDNDEBUG)
    if (name == NULL) {
        LOG_WARN("Error reading d_entry.");
    }
#endif
    return name;
}

/**
 * @brief Retrieves the name of the file associated with a dentry struct.
 *
 * The function traverses all the mount points to the root mount.
 */
static inline char *read_vfsmount_name(target_ptr_t vfsmount) {
    char *name = NULL;

    // current path component
    char *pcomp = NULL;

    // all path components read so far
    char **pcomps = NULL;
    uint32_t pcomps_idx = 0;
    uint32_t pcomps_capacity = 0;

    target_ptr_t current_vfsmount_parent = vfsmount;
    target_ptr_t current_vfsmount = (target_ptr_t)NULL;
    while(current_vfsmount != current_vfsmount_parent) {
        int og_err0, og_err1;
        target_ptr_t current_vfsmount_dentry;
        //int og_err2;
        //target_ptr_t root_dentry;
        current_vfsmount = current_vfsmount_parent;

        // retrieve vfsmount members
        og_err0 = get_vfsmount_dentry(current_vfsmount, &current_vfsmount_dentry);
        og_err1 = get_vfsmount_parent(current_vfsmount, &current_vfsmount_parent);
        fixupendian(current_vfsmount_dentry);
        fixupendian(current_vfsmount_parent);

        //printf("###D:%d:" TARGET_PTR_FMT ":" TARGET_PTR_FMT "\n", og_err0, current_vfsmount, current_vfsmount_dentry);
        //printf("###R:%d:" TARGET_PTR_FMT ":" TARGET_PTR_FMT "\n", og_err2, current_vfsmount, root_dentry);
        //og_err2 = get_vfsmount_root_dentry(current_vfsmount, &root_dentry);
        //printf("###P:%d:" TARGET_PTR_FMT ":" TARGET_PTR_FMT "\n", og_err1, current_vfsmount, current_vfsmount_parent);

        // check whether we should break out
        if (OG_SUCCESS != og_err0 || OG_SUCCESS != og_err1) {
            break;
        }
        if (current_vfsmount_dentry == (target_ptr_t)NULL) {
            break;
        }

        // read and copy component
        pcomp = read_dentry_name(current_vfsmount_dentry);
        //printf("###S:%s\n", pcomp);

        // this may hapen it seems
        if (pcomp == NULL) {
            continue;
        }

        if (pcomps_idx + 1 >= pcomps_capacity) { // +1 accounts for the terminating NULL
            pcomps_capacity += 16;
            pcomps = (char **)g_realloc(pcomps, pcomps_capacity * sizeof(char *));
        }
        pcomps[pcomps_idx++] = pcomp;
    }

    // reverse components order and join them
    if (pcomps != NULL) {
        char **pcomps_start = pcomps;
        char **pcomps_end = &pcomps[pcomps_idx - 1];
        while (pcomps_start < pcomps_end) {
            pcomp = *pcomps_start;
            *pcomps_start = *pcomps_end;
            *pcomps_end = pcomp;
            pcomps_start++;
            pcomps_end--;
        }
        pcomps[pcomps_idx] = NULL;            // NULL terminate vector
        name = g_strjoinv("", pcomps);        // slashes are included in pcomps
        g_strfreev(pcomps);
    }

    //printf("###F:%s\n", name);
    return name;
}

/**
 * @brief Retrieves the command name from a task_struct.
 *
 * @note task.comm is a fixed length array.
 * This means that we don't have to account for the terminating '\0'.
 */
static inline char *get_name(target_ptr_t task_struct, char *name) {
    if (name == NULL) { name = (char *)g_malloc0(ki.task.comm_size * sizeof(char)); }
    else { name = (char *)g_realloc(name, ki.task.comm_size * sizeof(char)); }
    if (-1 == panda_virtual_memory_rw(task_struct + ki.task.comm_offset, (void *)name, ki.task.comm_size * sizeof(char), 0)) {
        strncpy(name, "N/A", ki.task.comm_size*sizeof(char));
    }
    return name;
}

void fill_osiproc(OsiProc *p, target_ptr_t task_addr);
void fill_osithread(OsiThread *t, target_ptr_t task_addr);

#if defined(__cplusplus)
/**
 * @brief Template function for extracting data for all running processes.
 * This can be used to quickly implement extraction of partial process
 * information without having to rewrite the process list traversal
 * code.
 *
 * @note The ascii pictogram in kernel_structs.html roughly explains how the
 * process list traversal works. However, it may be inacurrate for some corner
 * cases. E.g. it doesn't explain why some inifnite loop cases manifest.
 * Avoiding these infinite loops was mostly a trial+error process.
 */
template <typename ET>
void get_process_info(GArray **out,
                      void (*fill_element)(ET *, target_ptr_t),
                      void (*free_element_contents)(ET *)) {
    ET element;
    target_ptr_t ts_first, ts_current;
    target_ptr_t UNUSED(tg_first), UNUSED(tg_next);

    if (*out == NULL) {
        // g_array_sized_new() args: zero_term, clear, element_sz, reserved_sz
        *out = g_array_sized_new(false, false, sizeof(ET), 128);
        g_array_set_clear_func(*out, (GDestroyNotify)free_element_contents);
    }

#if defined(OSI_LINUX_LIST_FROM_INIT)
    // Start process enumeration from the init task.
    ts_first = ki.task.init_addr;
#else
    // Start process enumeration (roughly) from the current task. This is the default.
    ts_first = kernel_profile->get_current_task_struct();

    // To avoid infinite loops, we need to actually start traversal from the next
    // process after the thread group leader of the current task.
    ts_first = kernel_profile->get_group_leader(ts_first);
    ts_first = kernel_profile->get_task_struct_next(ts_first);
#endif

    ts_current = ts_first;

    if (ts_first == (target_ptr_t)NULL) goto error;
#if defined(OSI_LINUX_PSDEBUG)
    LOG_INFO("START %c:%c " TARGET_PTR_FMT " " TARGET_PTR_FMT, TS_THREAD_CHR(ts_first),  TS_LEADER_CHR(ts_first), ts_first, ts_first);
#endif

    do {
#if defined(OSI_LINUX_PSDEBUG)
         LOG_INFO("\t %03u:" TARGET_PTR_FMT ":" TARGET_PID_FMT ":" TARGET_PID_FMT ":%c:%c", (*out)->len, ts_current, get_pid(ts_current), get_tgid(ts_current), TS_THREAD_CHR(ts_current), TS_LEADER_CHR(ts_current));
#endif
        memset(&element, 0, sizeof(ET));
        fill_element(&element, ts_current);
        g_array_append_val(*out, element);
        OSI_MAX_PROC_CHECK((*out)->len, "traversing process list");

#if defined(OSI_LINUX_LIST_THREADS)
        // Traverse thread group list.
        // It is assumed that ts_current is a thread group leader.
        tg_first = ts_current + ki.task.thread_group_offset;
        while ((tg_next = get_thread_group(ts_current)) != tg_first) {
            ts_current = tg_next - ki.task.thread_group_offset;
#if defined(OSI_LINUX_PSDEBUG)
            LOG_INFO("\t %03u:" TARGET_PTR_FMT ":" TARGET_PID_FMT ":" TARGET_PID_FMT ":%c:%c", a->len, ts_current, get_pid(ts_current), get_tgid(ts_current), TS_THREAD_CHR(ts_current), TS_LEADER_CHR(ts_current));
#endif
            memset(&element, 0, sizeof(ET));
            element_fill(&element, ts_current);
            g_array_append_val(*out, element);
            OSI_MAX_PROC_CHECK((*out)->len, "traversing thread group list");
        }
        ts_current = tg_first - ki.task.thread_group_offset;
#endif

        ts_current = kernel_profile->get_task_struct_next(ts_current);
    } while(ts_current != (target_ptr_t)NULL && ts_current != ts_first);

    // memory read error
    if (ts_current == (target_ptr_t)NULL) goto error;

    return;

error:
    if(*out != NULL) {
        g_array_free(*out, true);
    }
    *out = NULL;
    return;
}
#endif

/* vim:set tabstop=4 softtabstop=4 expandtab: */
