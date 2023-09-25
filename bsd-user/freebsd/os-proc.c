/*
 *  FreeBSD process related emulation code
 *
 *  Copyright (c) 2013-15 Stacey D. Son
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/sysctl.h>
struct kinfo_proc;
#include <libprocstat.h>

#include "qemu.h"

/*
 * Get the filename for the given file descriptor.
 * Note that this may return NULL (fail) if no longer cached in the kernel.
 */
char *
get_filename_from_fd(pid_t pid, int fd, char *filename, size_t len);
char *
get_filename_from_fd(pid_t pid, int fd, char *filename, size_t len)
{
    char *ret = NULL;
    unsigned int cnt;
    struct procstat *procstat = NULL;
    struct kinfo_proc *kp = NULL;
    struct filestat_list *head = NULL;
    struct filestat *fst;

    procstat = procstat_open_sysctl();
    if (procstat == NULL) {
        goto out;
    }

    kp = procstat_getprocs(procstat, KERN_PROC_PID, pid, &cnt);
    if (kp == NULL) {
        goto out;
    }

    head = procstat_getfiles(procstat, kp, 0);
    if (head == NULL) {
        goto out;
    }

    STAILQ_FOREACH(fst, head, next) {
        if (fd == fst->fs_fd) {
            if (fst->fs_path != NULL) {
                (void)strlcpy(filename, fst->fs_path, len);
                ret = filename;
            }
            break;
        }
    }

out:
    if (head != NULL) {
        procstat_freefiles(procstat, head);
    }
    if (kp != NULL) {
        procstat_freeprocs(procstat, kp);
    }
    if (procstat != NULL) {
        procstat_close(procstat);
    }
    return ret;
}

