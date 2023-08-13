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

