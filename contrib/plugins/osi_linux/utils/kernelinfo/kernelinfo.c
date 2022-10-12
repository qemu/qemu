/*!
 * @file kernelinfo.c
 * @brief Retrieves offset information from the running Linux kernel and prints them in the kernel log.
 *
 * @author Manolis Stamatogiannakis <manolis.stamatogiannakis@vu.nl>
 * @copyright This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */
#include <linux/types.h>
#include <linux/module.h>	/* Needed by all modules */
#include <linux/kernel.h>	/* Needed for KERN_INFO */
#include <linux/utsname.h>
#include <linux/version.h>
#include <linux/syscalls.h>
#include <linux/security.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/dcache.h>
#include <linux/mount.h>
#include <linux/version.h>

/*
 * Include the appropriate mount.h version.
 *
 * Linux commit 7d6fec45a5131 introduces struct mount in fs/mount.h.
 * The new struct contains all the fields that were previously members
 * of struct vfsmount but were touched only by core VFS.
 * It also contains an embedded struct vfsmount which now has been
 * stripped down to include only the fields shared between core VFS
 * and other components.
 *
 * XXX: identify the first kernel version after 7d6fec45a5131 to make
 * the conditionals more accurate.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)		/* 0.0  <= v < 2.6  */
#error Unsupported kernel.
#elif LINUX_VERSION_CODE < KERNEL_VERSION(3,0,0)	/* 2.6  <= v < 3.0  */
#define current_task per_cpu__current_task
#elif LINUX_VERSION_CODE < KERNEL_VERSION(3,3,0)	/* 3.0  <= v < 3.3  */
/* nothing */
#elif LINUX_VERSION_CODE < KERNEL_VERSION(4,4,0)	/* 3.3  <= v < 4.4  */
#include "ksrc/v3.3-rc1/fs/mount.h"
#elif LINUX_VERSION_CODE < KERNEL_VERSION(4,12,0)	/* 4.4  <= v < 4.12 */
#include "ksrc/v4.4/fs/mount.h"
#else												/* 4.12 <= v < x.xx */
#include "ksrc/v4.12/fs/mount.h"
#endif

/*
 * This function is used because to print offsets of members
 * of nested structs. It basically transforms '.' to '_', so
 * that we don't have to replicate all the nesting in the
 * structs used by the introspection program.
 *
 * E.g. for:
 * struct file {
 *	...
 *	struct dentry {
 *		struct *vfsmount;
 *		struct *dentry;
 *	}
 *	...
 * };
 *
 * Caveat: Because a static buffer is returned, the function
 * can only be used once in each invocation of printk.
 */
#define MAX_MEMBER_NAME 31
static char *cp_memb(const char *s) {
	static char memb[MAX_MEMBER_NAME+1];
	int i;
	for (i = 0; i<MAX_MEMBER_NAME && s[i]!='\0'; i++) {
		memb[i] = s[i] == '.' ? '_' : s[i];
	}
	memb[i] = 0;
	return memb;
}

/*
 * Printf offset of memb from the beginning of structp.
 */
#define PRINT_OFFSET(structp, memb, cfgname)\
	printk(KERN_INFO cfgname ".%s_offset = %d\n",\
		cp_memb(#memb),\
		(int)((void *)&(structp->memb) - (void *)structp))

/*
 * Prints offset between members memb_base and memb_dest.
 * Useful in case where we have a pointer to memb_base, but not to structp.
 * We emit the same name as if we were using PRINT_OFFSET() for memb_dest.
 */
#define PRINT_OFFSET_FROM_MEMBER(structp, memb_base, memb_dest, cfgname)\
	printk(KERN_INFO cfgname ".%s_offset = %d\n",\
		cp_memb(#memb_dest),\
		(int)((void *)&(structp->memb_dest) - (void *)&(structp->memb_base)))

/*
 * Prints the size of structv.
 */
#define PRINT_SIZE(structv, cfgmemb, cfgname) printk(KERN_INFO cfgname "." cfgmemb " = %zu\n", sizeof(structv))

int init_module(void)
{
	struct cred cred__s;
	struct vm_area_struct vm_area_struct__s;
	struct dentry dentry__s;
	struct dentry_operations dentry_operations__s;
	struct file file__s;
	struct files_struct files_struct__s;
	struct fdtable fdtable__s;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,3,0)
	struct mount mount__s;
#else
	struct vfsmount vfsmount__s;
#endif
	struct qstr qstr__s;

	struct task_struct *task_struct__p;
	struct cred *cred__p;
	struct mm_struct *mm_struct__p;
	struct vm_area_struct *vm_area_struct__p;
	struct dentry *dentry__p;
	struct dentry_operations *dentry_operations__p;
	struct file *file__p;
	struct fdtable *fdtable__p;
	struct files_struct *files_struct__p;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,3,0)
	struct mount *mount__p;
#endif
	struct vfsmount *vfsmount__p;
	struct qstr *qstr__p;

	task_struct__p = &init_task;
	cred__p = &cred__s;
	mm_struct__p = init_task.mm;
	vm_area_struct__p = &vm_area_struct__s;
	dentry__p = &dentry__s;
	dentry_operations__p = &dentry_operations__s;
	file__p = &file__s;
	files_struct__p = &files_struct__s;
	fdtable__p = &fdtable__s;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,3,0)
	mount__p = &mount__s;
	vfsmount__p = &mount__s.mnt;
#else
	vfsmount__p = &vfsmount__s;
#endif
	qstr__p = &qstr__s;

	printk(KERN_INFO "--KERNELINFO-BEGIN--\n");
	printk(KERN_INFO "name = %s|%s|%s\n", utsname()->release, utsname()->version, utsname()->machine);
	printk(KERN_INFO "version.a = %d\n", LINUX_VERSION_CODE >> 16);
	printk(KERN_INFO "version.b = %d\n", (LINUX_VERSION_CODE >> 8) & 0xFF);
	printk(KERN_INFO "version.c = %d\n", LINUX_VERSION_CODE & 0xFF);
	printk(KERN_INFO "#arch = %s", utsname()->machine);

#if defined __i386__ || defined __x86_64__
	printk(KERN_INFO "task.per_cpu_offsets_addr = %llu\n", (u64)(uintptr_t)&__per_cpu_offset);
	printk(KERN_INFO "task.per_cpu_offset_0_addr = %llu\n", (u64)(uintptr_t)__per_cpu_offset[0]);
	printk(KERN_INFO "task.current_task_addr = %llu\n", (u64)(uintptr_t)&current_task);
	printk(KERN_INFO "task.init_addr = %llu\n", (u64)(uintptr_t)(task_struct__p));
	printk(KERN_INFO "#task.per_cpu_offsets_addr = %08llX\n", (u64)(uintptr_t)&__per_cpu_offset);
	printk(KERN_INFO "#task.per_cpu_offset_0_addr = 0x%08llX\n", (u64)(uintptr_t)__per_cpu_offset[0]);
	printk(KERN_INFO "#task.current_task_addr = 0x%08llX\n", (u64)(uintptr_t)&current_task);
	printk(KERN_INFO "#task.init_addr = 0x%08llX\n", (u64)(uintptr_t)task_struct__p);

#else
	printk(KERN_INFO "task.per_cpu_offsets_addr = %d\n", 0);
	printk(KERN_INFO "task.per_cpu_offset_0_addr = %d\n", 0);
	printk(KERN_INFO "task.current_task_addr = %llu\n", (u64)(uintptr_t)(task_struct__p)); //set equal to task.init_addr
	printk(KERN_INFO "task.init_addr = %llu\n", (u64)(uintptr_t)(task_struct__p));
	printk(KERN_INFO "#task.per_cpu_offsets_addr = %x\n", 0);
	printk(KERN_INFO "#task.per_cpu_offset_0_addr = %x\n", 0);
	printk(KERN_INFO "#task.current_task_addr = 0x%08llX\n", (u64)(uintptr_t)task_struct__p);
	printk(KERN_INFO "#task.init_addr = 0x%08llX\n", (u64)(uintptr_t)task_struct__p);

#endif

	PRINT_SIZE(init_task,				"size",			"task");
	PRINT_OFFSET(task_struct__p,		tasks,			"task");
	PRINT_OFFSET(task_struct__p,		pid,			"task");
	PRINT_OFFSET(task_struct__p,		tgid,			"task");
	PRINT_OFFSET(task_struct__p,		group_leader,	"task");
	PRINT_OFFSET(task_struct__p,		thread_group,	"task");
	PRINT_OFFSET(task_struct__p,		real_parent,	"task");
	PRINT_OFFSET(task_struct__p,		parent,			"task");
	PRINT_OFFSET(task_struct__p,		mm,				"task");
	PRINT_OFFSET(task_struct__p,		stack,			"task");
	PRINT_OFFSET(task_struct__p,		real_cred,		"task");
	PRINT_OFFSET(task_struct__p,		cred,			"task");
	PRINT_OFFSET(task_struct__p,		comm,			"task");
	PRINT_SIZE(task_struct__p->comm,	"comm_size",	"task");
	PRINT_OFFSET(task_struct__p,		files,			"task");
	PRINT_OFFSET(task_struct__p,		start_time,			"task");

	PRINT_OFFSET(cred__p,				uid,			"cred");
	PRINT_OFFSET(cred__p,				gid,			"cred");
	PRINT_OFFSET(cred__p,				euid,			"cred");
	PRINT_OFFSET(cred__p,				egid,			"cred");

	PRINT_SIZE(*init_task.mm,			"size",			"mm");
	PRINT_OFFSET(mm_struct__p,			mmap,			"mm");
	PRINT_OFFSET(mm_struct__p,			pgd,			"mm");
	PRINT_OFFSET(mm_struct__p,			arg_start,		"mm");
	PRINT_OFFSET(mm_struct__p,			start_brk,		"mm");
	PRINT_OFFSET(mm_struct__p,			brk,			"mm");
	PRINT_OFFSET(mm_struct__p,			start_stack,	"mm");

	PRINT_SIZE(vm_area_struct__s,		"size",			"vma");
	PRINT_OFFSET(vm_area_struct__p,		vm_mm,			"vma");
	PRINT_OFFSET(vm_area_struct__p,		vm_start,		"vma");
	PRINT_OFFSET(vm_area_struct__p,		vm_end,			"vma");
	PRINT_OFFSET(vm_area_struct__p,		vm_next,		"vma");
	PRINT_OFFSET(vm_area_struct__p,		vm_flags,		"vma");
	PRINT_OFFSET(vm_area_struct__p,		vm_file,		"vma");

	/* used in reading file information */
	PRINT_OFFSET(file__p,				f_path.dentry,	"fs");
	PRINT_OFFSET(file__p,				f_path.mnt,		"fs"); // XXX: check if this changes across versions
	PRINT_OFFSET(file__p,				f_pos,			"fs");
	PRINT_OFFSET(files_struct__p,		fdt,			"fs");
	PRINT_OFFSET(files_struct__p,		fdtab,			"fs");
	PRINT_OFFSET(fdtable__p,			fd,				"fs");

	/* used for resolving path names */
	PRINT_SIZE(qstr__s,					"size",			"qstr");
	PRINT_OFFSET(qstr__p,				name,			"qstr");
	PRINT_OFFSET(dentry__p,				d_name,					"path");
	PRINT_OFFSET(dentry__p,				d_iname,				"path");
	PRINT_OFFSET(dentry__p,				d_parent,				"path");
	PRINT_OFFSET(dentry__p,				d_op,					"path");
	PRINT_OFFSET(dentry_operations__p,	d_dname,				"path");
	PRINT_OFFSET(vfsmount__p,			mnt_root,				"path");
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,3,0)
	/* fields in struct mount */
	PRINT_OFFSET_FROM_MEMBER(mount__p,	mnt, mnt_parent,		"path");
	PRINT_OFFSET_FROM_MEMBER(mount__p,	mnt, mnt_mountpoint,	"path");
#else
	/* fields in struct vfsmount */
	PRINT_OFFSET(vfsmount__p,			mnt_parent,				"path");
	PRINT_OFFSET(vfsmount__p,			mnt_mountpoint,			"path");
#endif
	printk(KERN_INFO "---KERNELINFO-END---\n");

	/* Return a failure. We only want to print the info. */
	return -1;
}

void cleanup_module(void)
{
	printk(KERN_INFO "Information module removed.\n");
}

MODULE_LICENSE("GPL");

/* vim:set tabstop=4 softtabstop=4 noexpandtab: */
