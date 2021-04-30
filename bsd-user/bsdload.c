/*
 *  Load BSD executables.
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

#include "qemu.h"

/* ??? This should really be somewhere else.  */
abi_long memcpy_to_target(abi_ulong dest, const void *src,
                          unsigned long len)
{
    void *host_ptr;

    host_ptr = lock_user(VERIFY_WRITE, dest, len, 0);
    if (!host_ptr) {
        return -TARGET_EFAULT;
    }
    memcpy(host_ptr, src, len);
    unlock_user(host_ptr, dest, 1);
    return 0;
}

static int count(char **vec)
{
    int         i;

    for (i = 0; *vec; i++) {
        vec++;
    }

    return i;
}

static int prepare_binprm(struct bsd_binprm *bprm)
{
    struct stat         st;
    int mode;
    int retval;

    if (fstat(bprm->fd, &st) < 0) {
        return -errno;
    }

    mode = st.st_mode;
    if (!S_ISREG(mode)) {        /* Must be regular file */
        return -EACCES;
    }
    if (!(mode & 0111)) {        /* Must have at least one execute bit set */
        return -EACCES;
    }

    bprm->e_uid = geteuid();
    bprm->e_gid = getegid();

    /* Set-uid? */
    if (mode & S_ISUID) {
        bprm->e_uid = st.st_uid;
    }

    /* Set-gid? */
    /*
     * If setgid is set but no group execute bit then this
     * is a candidate for mandatory locking, not a setgid
     * executable.
     */
    if ((mode & (S_ISGID | S_IXGRP)) == (S_ISGID | S_IXGRP)) {
        bprm->e_gid = st.st_gid;
    }

    memset(bprm->buf, 0, sizeof(bprm->buf));
    retval = lseek(bprm->fd, 0L, SEEK_SET);
    if (retval >= 0) {
        retval = read(bprm->fd, bprm->buf, 128);
    }
    if (retval < 0) {
        perror("prepare_binprm");
        exit(-1);
    } else {
        return retval;
    }
}

/* Construct the envp and argv tables on the target stack.  */
abi_ulong loader_build_argptr(int envc, int argc, abi_ulong sp,
                              abi_ulong stringp)
{
    int n = sizeof(abi_ulong);
    abi_ulong envp;
    abi_ulong argv;

    sp -= (envc + 1) * n;
    envp = sp;
    sp -= (argc + 1) * n;
    argv = sp;
    sp -= n;
    /* FIXME - handle put_user() failures */
    put_user_ual(argc, sp);

    while (argc-- > 0) {
        /* FIXME - handle put_user() failures */
        put_user_ual(stringp, argv);
        argv += n;
        stringp += target_strlen(stringp) + 1;
    }
    /* FIXME - handle put_user() failures */
    put_user_ual(0, argv);
    while (envc-- > 0) {
        /* FIXME - handle put_user() failures */
        put_user_ual(stringp, envp);
        envp += n;
        stringp += target_strlen(stringp) + 1;
    }
    /* FIXME - handle put_user() failures */
    put_user_ual(0, envp);

    return sp;
}

static bool is_there(const char *candidate)
{
    struct stat fin;

    /* XXX work around access(2) false positives for superuser */
    if (access(candidate, X_OK) == 0 && stat(candidate, &fin) == 0 &&
            S_ISREG(fin.st_mode) && (getuid() != 0 ||
                (fin.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0)) {
        return true;
    }

    return false;
}

int loader_exec(const char *filename, char **argv, char **envp,
                struct target_pt_regs *regs, struct image_info *infop,
                struct bsd_binprm *bprm)
{
    char *path, fullpath[PATH_MAX];
    int retval, i;

    bprm->p = TARGET_PAGE_SIZE * MAX_ARG_PAGES;
    for (i = 0; i < MAX_ARG_PAGES; i++) {       /* clear page-table */
        bprm->page[i] = NULL;
    }

    if (strchr(filename, '/') != NULL) {
        path = realpath(filename, fullpath);
        if (path == NULL) {
            /* Failed to resolve. */
            return -1;
        }
        if (!is_there(path)) {
            return -1;
        }
    } else {
        path = g_find_program_in_path(filename);
        if (path == NULL) {
            return -1;
        }
    }

    retval = open(path, O_RDONLY);
    if (retval < 0) {
        g_free(path);
        return retval;
    }

    bprm->fullpath = path;
    bprm->fd = retval;
    bprm->filename = (char *)filename;
    bprm->argc = count(argv);
    bprm->argv = argv;
    bprm->envc = count(envp);
    bprm->envp = envp;

    retval = prepare_binprm(bprm);

    if (retval >= 0) {
        if (bprm->buf[0] == 0x7f
                && bprm->buf[1] == 'E'
                && bprm->buf[2] == 'L'
                && bprm->buf[3] == 'F') {
            retval = load_elf_binary(bprm, regs, infop);
        } else {
            fprintf(stderr, "Unknown binary format\n");
            return -1;
        }
    }

    if (retval >= 0) {
        /* success.  Initialize important registers */
        do_init_thread(regs, infop);
        return retval;
    }

    /* Something went wrong, return the inode and free the argument pages*/
    for (i = 0 ; i < MAX_ARG_PAGES ; i++) {
        g_free(bprm->page[i]);
    }
    return retval;
}
