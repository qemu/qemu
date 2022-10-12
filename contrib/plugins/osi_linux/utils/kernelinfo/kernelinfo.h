/*!
 * @file kernelinfo.h
 * @brief Kernel specific information used for Linux OSI.
 *
 * @author Manolis Stamatogiannakis <manolis.stamatogiannakis@vu.nl>
 * @copyright This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */
#pragma once
#include <stdint.h>

/**
 * @brief Macro used to declare structs are as packed (or not).
 *
 * `kernelinfo_read.c` uses a bitmap to determine which members of
 * `struct kernelinfo` were not read from the kernel config file. Due to
 * alignment padding by the compiler, the bitmap check will have false
 * positives for the bytes of the padding.
 *
 * Declaring the structs as packed will eliminate the false positives.
 * This may be useful when debugging osi_linux or adding support for new
 * kernels. However, packed structs may be slower to access in practice.
 * For this, we keep packing off by default and bear with the false
 * positives.
 */
#if 0
#define PACKED_STRUCT(x) struct __attribute__((__packed__)) x
#else
#define PACKED_STRUCT(x) struct x
#endif

#define KERNEL_VERSION(a, b, c) (((a) << 16) + ((b) << 8) + (c))
#define PROFILE_KVER_EQ(ki, _va, _vb, _vc) (KERNEL_VERSION(ki.version.a, ki.version.b, ki.version.c) == KERNEL_VERSION(_va, _vb, _vc))
#define PROFILE_KVER_NE(ki, _va, _vb, _vc) (KERNEL_VERSION(ki.version.a, ki.version.b, ki.version.c) != KERNEL_VERSION(_va, _vb, _vc))
#define PROFILE_KVER_LT(ki, _va, _vb, _vc) (KERNEL_VERSION(ki.version.a, ki.version.b, ki.version.c) < KERNEL_VERSION(_va, _vb, _vc))
#define PROFILE_KVER_GT(ki, _va, _vb, _vc) (KERNEL_VERSION(ki.version.a, ki.version.b, ki.version.c) > KERNEL_VERSION(_va, _vb, _vc))
#define PROFILE_KVER_LE(ki, _va, _vb, _vc) (KERNEL_VERSION(ki.version.a, ki.version.b, ki.version.c) <= KERNEL_VERSION(_va, _vb, _vc))
#define PROFILE_KVER_GE(ki, _va, _vb, _vc) (KERNEL_VERSION(ki.version.a, ki.version.b, ki.version.c) >= KERNEL_VERSION(_va, _vb, _vc))
// BEGIN_PYPANDA_NEEDS_THIS -- do not delete this comment bc pypanda
// api autogen needs it.  And don't put any compiler directives
// between this and END_PYPANDA_NEEDS_THIS except includes of other
// files in this directory that contain subsections like this one.

/**
 * @brief Kernel Version information
 */
PACKED_STRUCT(version) {
	int a;
	int b;
	int c;
};

/**
 * @brief Information and offsets related to `struct task_struct`.
 */
PACKED_STRUCT(task_info) {
    uint64_t per_cpu_offsets_addr;
    uint64_t per_cpu_offset_0_addr;
    uint64_t switch_task_hook_addr; /**< Address to hook for task switch notifications. */
    uint64_t current_task_addr;
	uint64_t init_addr;			/**< Address of the `struct task_struct` of the init task. */
	size_t size;				/**< Size of `struct task_struct`. */
	union {
		int tasks_offset;			/**< TODO: add documentation for the rest of the struct members */
		int next_task_offset;
	};
	int pid_offset;
	int tgid_offset;
	int group_leader_offset;
	int thread_group_offset;
	union {
		int real_parent_offset;
		int p_opptr_offset;
	};
	union {
		int parent_offset;
		int p_pptr_offset;
	};
	int mm_offset;
	int stack_offset;
	int real_cred_offset;
	int cred_offset;
	int comm_offset;			/**< Offset of the command name in `struct task_struct`. */
	size_t comm_size;			/**< Size of the command name. */
	int files_offset;			/**< Offset for open files information. */
        int start_time_offset;                  /** offset of start_time */
};

/**
 * @brief Information and offsets related to `struct cred`.
 */
PACKED_STRUCT(cred_info) {
	int uid_offset;
	int gid_offset;
	int euid_offset;
	int egid_offset;
};

/**
 * @brief Information and offsets related to `struct mm_struct`.
 */
PACKED_STRUCT(mm_info) {
	size_t size;				/**< Size of `struct mm_struct`. */
	int mmap_offset;
	int pgd_offset;
	int arg_start_offset;
	int start_brk_offset;
	int brk_offset;
	int start_stack_offset;
};

/**
 * @brief Information and offsets related to `struct vm_area_struct`.
 */
PACKED_STRUCT(vma_info) {
	size_t size;				/**< Size of `struct vm_area_struct`. */
	int vm_mm_offset;
	int vm_start_offset;
	int vm_end_offset;
	int vm_next_offset;
	int vm_file_offset;
	int vm_flags_offset;
};

/**
 * @brief Filesystem information and offsets.
 */
PACKED_STRUCT(fs_info) {
	union {
		int f_path_dentry_offset;
		int f_dentry_offset;
	};
	union {
		int f_path_mnt_offset;
		int f_vfsmnt_offset;
	};
	int f_pos_offset;
	int fdt_offset;
	int fdtab_offset;
	int fd_offset;
};

/**
 * @brief qstr information and offsets
 */
PACKED_STRUCT(qstr_info) {
  size_t size;
  size_t name_offset;
};

/**
 * @brief Path related information and offsets.
 */
PACKED_STRUCT(path_info) {
	int d_name_offset;
	int d_iname_offset;
	int d_parent_offset;
	int d_op_offset;			/**< Offset of the dentry ops table. */
	int d_dname_offset;			/**< Offset of dynamic name function in dentry ops. */
	int mnt_root_offset;
	int mnt_parent_offset;
	int mnt_mountpoint_offset;
};

/**
 * @brief Wrapper for the structure-specific structs.
 */
PACKED_STRUCT(kernelinfo) {
	char *name;
	struct version version;
	struct task_info task;
	struct cred_info cred;
	struct mm_info mm;
	struct vma_info vma;
	struct fs_info fs;
	struct qstr_info qstr;
	struct path_info path;
};

// END_PYPANDA_NEEDS_THIS -- do not delete this comment!

#if defined(__G_LIB_H__) || defined(DOXYGEN)
/*!
 * \def DEFAULT_KERNELINFO_FILE
 * Default name for the kernel info configuration file.
 */
#define DEFAULT_KERNELINFO_FILE "kernelinfo.conf"

#ifdef __cplusplus
extern "C" {
#endif

int read_kernelinfo(gchar const *file, gchar const *group, struct kernelinfo *ki);
void list_kernelinfo_groups(gchar const *file);
#ifdef __cplusplus
}
#endif
#endif

/* vim:set tabstop=4 softtabstop=4 noexpandtab: */
