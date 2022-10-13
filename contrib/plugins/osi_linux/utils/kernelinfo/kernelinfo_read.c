/*!
 * @file kernelinfo_read.c
 * @brief Reads kernel information (struct offsets and such) from key-value config files.
 *
 * @see https://developer.gnome.org/glib/stable/glib-Key-value-file-parser.html
 *
 * @author Manolis Stamatogiannakis <manolis.stamatogiannakis@vu.nl>
 * @copyright This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <glib.h>
#include "kernelinfo.h"

#define LOG_ERROR printf
#define LOG_WARNING printf

/*!
 * @brief Wrapper for error counters.
 */
struct kernelinfo_errors {
	int version;
	int name;
	int task;
	int cred;
	int mm;
	int vma;
	int fs;
	int qstr;
	int path;
};

/* make sure PANDA_MSG is defined somewhere */
#if defined(PLUGIN_NAME)
#include "panda/debug.h"
#else
#define PANDA_MSG
#endif

/*!
 * @brief Wrapper for reading information from keyfile and handle errors.
 */
#define READ_INFO_X(key_file_get, ki, memb, gerr, errcount, errbmp)\
	((ki)->memb) = key_file_get(keyfile, group_real, #memb, &gerr);\
	if (gerr != NULL) { errcount++; g_error_free(gerr); gerr = NULL; LOG_ERROR("failed to read " #memb); }\
	else { memset(&(errbmp)->memb, 0xff, sizeof((errbmp)->memb)); }

#define READ_INFO_INT(ki, memb, gerr, errcount, errbmp)\
	READ_INFO_X(g_key_file_get_integer, ki, memb, gerr, errcount, errbmp)

#define READ_INFO_UINT64(ki, memb, gerr, errcount, errbmp)\
	READ_INFO_X(g_key_file_get_uint64, ki, memb, gerr, errcount, errbmp)

#define READ_INFO_STRING(ki, memb, gerr, errcount, errbmp)\
	READ_INFO_X(g_key_file_get_string, ki, memb, gerr, errcount, errbmp)


#define OPTIONAL_READ_INFO_X(key_file_get, ki, memb, gerr, errcount, errbmp)\
	((ki)->memb) = key_file_get(keyfile, group_real, #memb, &gerr);\
	if (gerr != NULL) { g_error_free(gerr); gerr = NULL; LOG_WARNING("WARNING failed to read " #memb "\n"); ((ki)->memb) = (uint64_t)NULL;}\
	memset(&(errbmp)->memb, 0xff, sizeof((errbmp)->memb));

#define OPTIONAL_READ_INFO_INT(ki, memb, gerr, errcount, errbmp)\
	OPTIONAL_READ_INFO_X(g_key_file_get_integer, ki, memb, gerr, errcount, errbmp)

#define OPTIONAL_READ_INFO_UINT64(ki, memb, gerr, errcount, errbmp)\
	OPTIONAL_READ_INFO_X(g_key_file_get_uint64, ki, memb, gerr, errcount, errbmp)


/*! Reads kernel information (struct offsets and such) from the specified file.
 *
 * Each file may contain several contain information for many different kernels
 * in groups. A specific group can be chosen with \p group.
 *
 * \param file The name of the kernel information file. When `NULL` the default #DEFAULT_KERNELINFO_FILE is used.
 * \param group The name of the group to use from the kernel information file. When `NULL`, the first group is used.
 * \param ki A structure used to read the kernel information.
 * \return 0 for success. -1 for failure.
 */
int read_kernelinfo(gchar const *file, gchar const *group, struct kernelinfo *ki) {
	int rval = 0;						/**< return value */
	GKeyFile *keyfile;
	gchar *group_real = NULL;

	GError *gerr = NULL;				/**< glib errors */
	struct kernelinfo_errors err = {0};	/**< error counters for kernelinfo */
	struct kernelinfo errbmp = {0};		/**< error bitmap for kernelinfo */


	/* open file */
	memset(ki, 0, sizeof(struct kernelinfo));
	keyfile = g_key_file_new();
	g_key_file_load_from_file (keyfile, (file != NULL ? file : DEFAULT_KERNELINFO_FILE), G_KEY_FILE_NONE, &gerr);
	if (gerr != NULL) { rval = -1; goto end; }

	/* get group */
	if (group != NULL) group_real = g_strdup(group);
	else group_real = g_key_file_get_start_group(keyfile);
	if (!g_key_file_has_group(keyfile, group_real)) { rval = -1; goto end; }

	/* read kernel full name */
	READ_INFO_STRING(ki, name, gerr, err.name, &errbmp);

	/* read kernel version information */
	READ_INFO_INT(ki, version.a, gerr, err.version, &errbmp);
	READ_INFO_INT(ki, version.b, gerr, err.version, &errbmp);
	READ_INFO_INT(ki, version.c, gerr, err.version, &errbmp);

	/* read init task address */
	READ_INFO_UINT64(ki, task.init_addr, gerr, err.task, &errbmp);

	/* read task information */
	READ_INFO_INT(ki, task.size, gerr, err.task, &errbmp);

	if (KERNEL_VERSION(ki->version.a, ki->version.b, ki->version.c) > KERNEL_VERSION(2, 4, 254)) {
		READ_INFO_INT(ki, task.tasks_offset, gerr, err.task, &errbmp);

		READ_INFO_UINT64(ki, task.per_cpu_offsets_addr, gerr, err.task, &errbmp);
		READ_INFO_UINT64(ki, task.per_cpu_offset_0_addr, gerr, err.task, &errbmp);
		READ_INFO_UINT64(ki, task.current_task_addr, gerr, err.task, &errbmp);
		READ_INFO_INT(ki, task.group_leader_offset, gerr, err.task, &errbmp);
		READ_INFO_INT(ki, task.stack_offset, gerr, err.task, &errbmp);
		READ_INFO_INT(ki, task.real_cred_offset, gerr, err.task, &errbmp);
		READ_INFO_INT(ki, task.cred_offset, gerr, err.task, &errbmp);
		READ_INFO_INT(ki, task.real_parent_offset, gerr, err.task, &errbmp);
		READ_INFO_INT(ki, task.parent_offset, gerr, err.task, &errbmp);

		/* read cred information */
		READ_INFO_INT(ki, cred.uid_offset, gerr, err.cred, &errbmp);
		READ_INFO_INT(ki, cred.gid_offset, gerr, err.cred, &errbmp);
		READ_INFO_INT(ki, cred.euid_offset, gerr, err.cred, &errbmp);
		READ_INFO_INT(ki, cred.egid_offset, gerr, err.cred, &errbmp);

		READ_INFO_INT(ki, fs.f_path_dentry_offset, gerr, err.fs, &errbmp);
		READ_INFO_INT(ki, fs.f_path_mnt_offset, gerr, err.fs, &errbmp);
		READ_INFO_INT(ki, fs.fdt_offset, gerr, err.fs, &errbmp);
		READ_INFO_INT(ki, fs.fdtab_offset, gerr, err.fs, &errbmp);
		READ_INFO_INT(ki, path.d_dname_offset, gerr, err.path, &errbmp);
	} else if (KERNEL_VERSION(ki->version.a, ki->version.b, ki->version.c) >= KERNEL_VERSION(2, 4, 0)) {
		READ_INFO_INT(ki, task.p_opptr_offset, gerr, err.task, &errbmp);
		READ_INFO_INT(ki, task.p_pptr_offset, gerr, err.task, &errbmp);
		READ_INFO_INT(ki, task.next_task_offset, gerr, err.task, &errbmp);

		READ_INFO_INT(ki, fs.f_dentry_offset, gerr, err.fs, &errbmp);	
		READ_INFO_INT(ki, fs.f_vfsmnt_offset, gerr, err.fs, &errbmp);
	}

	READ_INFO_INT(ki, task.thread_group_offset, gerr, err.task, &errbmp);
	READ_INFO_INT(ki, task.pid_offset, gerr, err.task, &errbmp);
	READ_INFO_INT(ki, task.tgid_offset, gerr, err.task, &errbmp);
	READ_INFO_INT(ki, task.mm_offset, gerr, err.task, &errbmp);
	READ_INFO_INT(ki, task.comm_offset, gerr, err.task, &errbmp);
	READ_INFO_INT(ki, task.comm_size, gerr, err.task, &errbmp);
	READ_INFO_INT(ki, task.files_offset, gerr, err.task, &errbmp);
	OPTIONAL_READ_INFO_INT(ki, task.start_time_offset, gerr, err.task, &errbmp);
    OPTIONAL_READ_INFO_UINT64(ki, task.switch_task_hook_addr, gerr, err.task, &errbmp);
        
	/* read mm information */
	READ_INFO_INT(ki, mm.size, gerr, err.mm, &errbmp);
	READ_INFO_INT(ki, mm.mmap_offset, gerr, err.mm, &errbmp);
	READ_INFO_INT(ki, mm.pgd_offset, gerr, err.mm, &errbmp);
	READ_INFO_INT(ki, mm.arg_start_offset, gerr, err.mm, &errbmp);
	READ_INFO_INT(ki, mm.start_brk_offset, gerr, err.mm, &errbmp);
	READ_INFO_INT(ki, mm.brk_offset, gerr, err.mm, &errbmp);
	READ_INFO_INT(ki, mm.start_stack_offset, gerr, err.mm, &errbmp);

	/* read vma information */
	READ_INFO_INT(ki, vma.size, gerr, err.vma, &errbmp);
	READ_INFO_INT(ki, vma.vm_mm_offset, gerr, err.vma, &errbmp);
	READ_INFO_INT(ki, vma.vm_start_offset, gerr, err.vma, &errbmp);
	READ_INFO_INT(ki, vma.vm_end_offset, gerr, err.vma, &errbmp);
	READ_INFO_INT(ki, vma.vm_next_offset, gerr, err.vma, &errbmp);
	READ_INFO_INT(ki, vma.vm_file_offset, gerr, err.vma, &errbmp);
	READ_INFO_INT(ki, vma.vm_flags_offset, gerr, err.vma, &errbmp);

	/* read fs information */
	READ_INFO_INT(ki, fs.f_pos_offset, gerr, err.fs, &errbmp);
	READ_INFO_INT(ki, fs.fd_offset, gerr, err.fs, &errbmp);

	/* read qstr information */
	READ_INFO_INT(ki, qstr.size, gerr, err.qstr, &errbmp);
	READ_INFO_INT(ki, qstr.name_offset, gerr, err.qstr, &errbmp);

	/* read path information */
	READ_INFO_INT(ki, path.d_name_offset, gerr, err.path, &errbmp);
	READ_INFO_INT(ki, path.d_iname_offset, gerr, err.path, &errbmp);
	READ_INFO_INT(ki, path.d_parent_offset, gerr, err.path, &errbmp);
	READ_INFO_INT(ki, path.d_op_offset, gerr, err.path, &errbmp);
	READ_INFO_INT(ki, path.mnt_root_offset, gerr, err.path, &errbmp);
	READ_INFO_INT(ki, path.mnt_parent_offset, gerr, err.path, &errbmp);
	READ_INFO_INT(ki, path.mnt_mountpoint_offset, gerr, err.path, &errbmp);

	/* check number of errors */
	{
		int nerrors = 0;
		int *e = (int *)&err;
		int *e_last = (int *)((uint8_t *)&err + sizeof(err));
		while (e < e_last) {
			nerrors += *e;
			e++;
		}
		if (nerrors > 0) {
			LOG_ERROR("%d errors reading from group %s", nerrors, group_real);
			rval = -1;
		}
	}

	/* check the bitmap for values that were not read */
	{
		int notread = 0;
		uint8_t *b_first = (uint8_t *)&errbmp;;
		uint8_t *b_last = (uint8_t *)&errbmp + sizeof(errbmp);
		uint8_t *b = b_first;
		while (b < b_last) {
			bool doprint = false;

			if (*b != 0xff) {
				notread++;
				if (!(b+1 < b_last)) {
					doprint = true;
				}
			}
			else if (notread > 0) {
				doprint = true;
			}

			if (doprint) {
				/* don't make errors critical - alignment padding bytes are never written */
				LOG_WARNING("WARNING kernelinfo bytes [%td-%td] not read\n", b-b_first-notread, b-b_first-1);
				notread = 0;
				/* rval = -1; */
			}

			/* debug */
			/* printf("%3td %x:%x\n", b-b_first, *b, ((uint8_t *)ki)[b-b_first]); */

			b++;
		}
	}

end:
		g_key_file_free(keyfile);
		g_free(group_real);
		return rval;
}

/*!
 * @brief Print a list of valid groupnames in a kernelinfo file
 */
void list_kernelinfo_groups(gchar const *file) {
    GKeyFile *keyfile;
	gchar ** groups;
	GError *gerr = NULL;
	int idx = 0;
	const char* fname = (file != NULL ? file : DEFAULT_KERNELINFO_FILE);

	keyfile = g_key_file_new();
	g_key_file_load_from_file (keyfile, fname, G_KEY_FILE_NONE, &gerr);
	if (gerr != NULL) {
		printf("\tError parsing %s\n", fname);
		return;
	}

	groups = g_key_file_get_groups(keyfile, NULL);
	while (groups[idx] != NULL) {
		printf("\t%s\n", groups[idx]);
		idx++;
	}
}
/* vim:set tabstop=4 softtabstop=4 noexpandtab: */
