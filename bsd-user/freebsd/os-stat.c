/*
 *  FreeBSD stat related conversion routines
 *
 *  Copyright (c) 2013 Stacey D. Son
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

/*
 * stat conversion
 */
abi_long h2t_freebsd11_stat(abi_ulong target_addr,
        struct freebsd11_stat *host_st)
{
    struct target_freebsd11_stat *target_st;

    if (!lock_user_struct(VERIFY_WRITE, target_st, target_addr, 0)) {
        return -TARGET_EFAULT;
    }
    memset(target_st, 0, sizeof(*target_st));
    __put_user(host_st->st_dev, &target_st->st_dev);
    __put_user(host_st->st_ino, &target_st->st_ino);
    __put_user(host_st->st_mode, &target_st->st_mode);
    __put_user(host_st->st_nlink, &target_st->st_nlink);
    __put_user(host_st->st_uid, &target_st->st_uid);
    __put_user(host_st->st_gid, &target_st->st_gid);
    __put_user(host_st->st_rdev, &target_st->st_rdev);
    __put_user(host_st->st_atim.tv_sec, &target_st->st_atim.tv_sec);
    __put_user(host_st->st_atim.tv_nsec, &target_st->st_atim.tv_nsec);
    __put_user(host_st->st_mtim.tv_sec, &target_st->st_mtim.tv_sec);
    __put_user(host_st->st_mtim.tv_nsec, &target_st->st_mtim.tv_nsec);
    __put_user(host_st->st_ctim.tv_sec, &target_st->st_ctim.tv_sec);
    __put_user(host_st->st_ctim.tv_nsec, &target_st->st_ctim.tv_nsec);
    __put_user(host_st->st_size, &target_st->st_size);
    __put_user(host_st->st_blocks, &target_st->st_blocks);
    __put_user(host_st->st_blksize, &target_st->st_blksize);
    __put_user(host_st->st_flags, &target_st->st_flags);
    __put_user(host_st->st_gen, &target_st->st_gen);
    /* st_lspare not used */
    __put_user(host_st->st_birthtim.tv_sec, &target_st->st_birthtim.tv_sec);
    __put_user(host_st->st_birthtim.tv_nsec, &target_st->st_birthtim.tv_nsec);
    unlock_user_struct(target_st, target_addr, 1);

    return 0;
}

abi_long h2t_freebsd_stat(abi_ulong target_addr,
        struct stat *host_st)
{
    struct target_stat *target_st;

    if (!lock_user_struct(VERIFY_WRITE, target_st, target_addr, 0)) {
        return -TARGET_EFAULT;
    }
    memset(target_st, 0, sizeof(*target_st));
    __put_user(host_st->st_dev, &target_st->st_dev);
    __put_user(host_st->st_ino, &target_st->st_ino);
    __put_user(host_st->st_nlink, &target_st->st_nlink);
    __put_user(host_st->st_mode, &target_st->st_mode);
    __put_user(host_st->st_uid, &target_st->st_uid);
    __put_user(host_st->st_gid, &target_st->st_gid);
    __put_user(host_st->st_rdev, &target_st->st_rdev);
    __put_user(host_st->st_atim.tv_sec, &target_st->st_atim.tv_sec);
    __put_user(host_st->st_atim.tv_nsec, &target_st->st_atim.tv_nsec);
#ifdef TARGET_HAS_STAT_TIME_T_EXT
/*    __put_user(host_st->st_mtim_ext, &target_st->st_mtim_ext); XXX */
#endif
    __put_user(host_st->st_mtim.tv_sec, &target_st->st_mtim.tv_sec);
    __put_user(host_st->st_mtim.tv_nsec, &target_st->st_mtim.tv_nsec);
#ifdef TARGET_HAS_STAT_TIME_T_EXT
/*    __put_user(host_st->st_ctim_ext, &target_st->st_ctim_ext); XXX */
#endif
    __put_user(host_st->st_ctim.tv_sec, &target_st->st_ctim.tv_sec);
    __put_user(host_st->st_ctim.tv_nsec, &target_st->st_ctim.tv_nsec);
#ifdef TARGET_HAS_STAT_TIME_T_EXT
/*    __put_user(host_st->st_birthtim_ext, &target_st->st_birthtim_ext); XXX */
#endif
    __put_user(host_st->st_birthtim.tv_sec, &target_st->st_birthtim.tv_sec);
    __put_user(host_st->st_birthtim.tv_nsec, &target_st->st_birthtim.tv_nsec);

    __put_user(host_st->st_size, &target_st->st_size);
    __put_user(host_st->st_blocks, &target_st->st_blocks);
    __put_user(host_st->st_blksize, &target_st->st_blksize);
    __put_user(host_st->st_flags, &target_st->st_flags);
    __put_user(host_st->st_gen, &target_st->st_gen);
    unlock_user_struct(target_st, target_addr, 1);

    return 0;
}

abi_long h2t_freebsd11_nstat(abi_ulong target_addr,
        struct freebsd11_stat *host_st)
{
    struct target_freebsd11_nstat *target_st;

    if (!lock_user_struct(VERIFY_WRITE, target_st, target_addr, 0)) {
        return -TARGET_EFAULT;
    }
    memset(target_st, 0, sizeof(*target_st));
    __put_user(host_st->st_dev, &target_st->st_dev);
    __put_user(host_st->st_ino, &target_st->st_ino);
    __put_user(host_st->st_mode, &target_st->st_mode);
    __put_user(host_st->st_nlink, &target_st->st_nlink);
    __put_user(host_st->st_uid, &target_st->st_uid);
    __put_user(host_st->st_gid, &target_st->st_gid);
    __put_user(host_st->st_rdev, &target_st->st_rdev);
    __put_user(host_st->st_atim.tv_sec, &target_st->st_atim.tv_sec);
    __put_user(host_st->st_atim.tv_nsec, &target_st->st_atim.tv_nsec);
    __put_user(host_st->st_mtim.tv_sec, &target_st->st_mtim.tv_sec);
    __put_user(host_st->st_mtim.tv_nsec, &target_st->st_mtim.tv_nsec);
    __put_user(host_st->st_ctim.tv_sec, &target_st->st_ctim.tv_sec);
    __put_user(host_st->st_ctim.tv_nsec, &target_st->st_ctim.tv_nsec);
    __put_user(host_st->st_size, &target_st->st_size);
    __put_user(host_st->st_blocks, &target_st->st_blocks);
    __put_user(host_st->st_blksize, &target_st->st_blksize);
    __put_user(host_st->st_flags, &target_st->st_flags);
    __put_user(host_st->st_gen, &target_st->st_gen);
    __put_user(host_st->st_birthtim.tv_sec, &target_st->st_birthtim.tv_sec);
    __put_user(host_st->st_birthtim.tv_nsec, &target_st->st_birthtim.tv_nsec);
    unlock_user_struct(target_st, target_addr, 1);

    return 0;
}

/*
 * file handle conversion
 */
abi_long t2h_freebsd_fhandle(fhandle_t *host_fh, abi_ulong target_addr)
{
    target_freebsd_fhandle_t *target_fh;

    if (!lock_user_struct(VERIFY_READ, target_fh, target_addr, 1)) {
        return -TARGET_EFAULT;
    }
    __get_user(host_fh->fh_fsid.val[0], &target_fh->fh_fsid.val[0]);
    __get_user(host_fh->fh_fsid.val[1], &target_fh->fh_fsid.val[0]);
    __get_user(host_fh->fh_fid.fid_len, &target_fh->fh_fid.fid_len);
    /* u_short         fid_data0; */
    memcpy(host_fh->fh_fid.fid_data, target_fh->fh_fid.fid_data,
        TARGET_MAXFIDSZ);
    unlock_user_struct(target_fh, target_addr, 0);
    return 0;
}

abi_long h2t_freebsd_fhandle(abi_ulong target_addr, fhandle_t *host_fh)
{
    target_freebsd_fhandle_t *target_fh;

    if (!lock_user_struct(VERIFY_WRITE, target_fh, target_addr, 0)) {
        return -TARGET_EFAULT;
    }
    __put_user(host_fh->fh_fsid.val[0], &target_fh->fh_fsid.val[0]);
    __put_user(host_fh->fh_fsid.val[1], &target_fh->fh_fsid.val[0]);
    __put_user(host_fh->fh_fid.fid_len, &target_fh->fh_fid.fid_len);
    /* u_short         fid_data0; */
    memcpy(target_fh->fh_fid.fid_data, host_fh->fh_fid.fid_data,
            TARGET_MAXFIDSZ);
    unlock_user_struct(target_fh, target_addr, 1);
    return 0;
}

/*
 *  file system stat
 */
abi_long h2t_freebsd11_statfs(abi_ulong target_addr,
        struct freebsd11_statfs *host_statfs)
{
    struct target_freebsd11_statfs *target_statfs;

    if (!lock_user_struct(VERIFY_WRITE, target_statfs, target_addr, 0)) {
        return -TARGET_EFAULT;
    }
    __put_user(host_statfs->f_version, &target_statfs->f_version);
    __put_user(host_statfs->f_type, &target_statfs->f_type);
    __put_user(host_statfs->f_flags, &target_statfs->f_flags);
    __put_user(host_statfs->f_bsize, &target_statfs->f_bsize);
    __put_user(host_statfs->f_iosize, &target_statfs->f_iosize);
    __put_user(host_statfs->f_blocks, &target_statfs->f_blocks);
    __put_user(host_statfs->f_bfree, &target_statfs->f_bfree);
    __put_user(host_statfs->f_bavail, &target_statfs->f_bavail);
    __put_user(host_statfs->f_files, &target_statfs->f_files);
    __put_user(host_statfs->f_ffree, &target_statfs->f_ffree);
    __put_user(host_statfs->f_syncwrites, &target_statfs->f_syncwrites);
    __put_user(host_statfs->f_asyncwrites, &target_statfs->f_asyncwrites);
    __put_user(host_statfs->f_syncreads, &target_statfs->f_syncreads);
    __put_user(host_statfs->f_asyncreads, &target_statfs->f_asyncreads);
    /* uint64_t f_spare[10]; */
    __put_user(host_statfs->f_namemax, &target_statfs->f_namemax);
    __put_user(host_statfs->f_owner, &target_statfs->f_owner);
    __put_user(host_statfs->f_fsid.val[0], &target_statfs->f_fsid.val[0]);
    __put_user(host_statfs->f_fsid.val[1], &target_statfs->f_fsid.val[1]);
    /* char f_charspace[80]; */
    strncpy(target_statfs->f_fstypename, host_statfs->f_fstypename,
        sizeof(target_statfs->f_fstypename));
    strncpy(target_statfs->f_mntfromname, host_statfs->f_mntfromname,
        sizeof(target_statfs->f_mntfromname));
    strncpy(target_statfs->f_mntonname, host_statfs->f_mntonname,
        sizeof(target_statfs->f_mntonname));
    unlock_user_struct(target_statfs, target_addr, 1);
    return 0;
}

abi_long h2t_freebsd_statfs(abi_ulong target_addr,
        struct statfs *host_statfs)
{
    struct target_statfs *target_statfs;

    if (!lock_user_struct(VERIFY_WRITE, target_statfs, target_addr, 0)) {
        return -TARGET_EFAULT;
    }
    __put_user(host_statfs->f_version, &target_statfs->f_version);
    __put_user(host_statfs->f_type, &target_statfs->f_type);
    __put_user(host_statfs->f_flags, &target_statfs->f_flags);
    __put_user(host_statfs->f_bsize, &target_statfs->f_bsize);
    __put_user(host_statfs->f_iosize, &target_statfs->f_iosize);
    __put_user(host_statfs->f_blocks, &target_statfs->f_blocks);
    __put_user(host_statfs->f_bfree, &target_statfs->f_bfree);
    __put_user(host_statfs->f_bavail, &target_statfs->f_bavail);
    __put_user(host_statfs->f_files, &target_statfs->f_files);
    __put_user(host_statfs->f_ffree, &target_statfs->f_ffree);
    __put_user(host_statfs->f_syncwrites, &target_statfs->f_syncwrites);
    __put_user(host_statfs->f_asyncwrites, &target_statfs->f_asyncwrites);
    __put_user(host_statfs->f_syncreads, &target_statfs->f_syncreads);
    __put_user(host_statfs->f_asyncreads, &target_statfs->f_asyncreads);
    /* uint64_t f_spare[10]; */
    __put_user(host_statfs->f_namemax, &target_statfs->f_namemax);
    __put_user(host_statfs->f_owner, &target_statfs->f_owner);
    __put_user(host_statfs->f_fsid.val[0], &target_statfs->f_fsid.val[0]);
    __put_user(host_statfs->f_fsid.val[1], &target_statfs->f_fsid.val[1]);
    /* char f_charspace[80]; */
    strncpy(target_statfs->f_fstypename, host_statfs->f_fstypename,
        sizeof(target_statfs->f_fstypename));
    strncpy(target_statfs->f_mntfromname, host_statfs->f_mntfromname,
        sizeof(target_statfs->f_mntfromname));
    strncpy(target_statfs->f_mntonname, host_statfs->f_mntonname,
        sizeof(target_statfs->f_mntonname));
    unlock_user_struct(target_statfs, target_addr, 1);
    return 0;
}

/*
 * fcntl cmd conversion
 */
abi_long target_to_host_fcntl_cmd(int cmd)
{
    return cmd;
}

