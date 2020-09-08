STRUCT_SPECIAL(termios)

STRUCT(winsize,
       TYPE_SHORT, TYPE_SHORT, TYPE_SHORT, TYPE_SHORT)

STRUCT(serial_multiport_struct,
       TYPE_INT, TYPE_INT, TYPE_CHAR, TYPE_CHAR, TYPE_INT, TYPE_CHAR, TYPE_CHAR,
       TYPE_INT, TYPE_CHAR, TYPE_CHAR, TYPE_INT, TYPE_CHAR, TYPE_CHAR, TYPE_INT,
       MK_ARRAY(TYPE_INT, 32))

STRUCT(serial_icounter_struct,
       TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, MK_ARRAY(TYPE_INT, 16))

STRUCT(sockaddr,
       TYPE_SHORT, MK_ARRAY(TYPE_CHAR, 14))

STRUCT(rtentry,
       TYPE_ULONG, MK_STRUCT(STRUCT_sockaddr), MK_STRUCT(STRUCT_sockaddr), MK_STRUCT(STRUCT_sockaddr),
       TYPE_SHORT, TYPE_SHORT, TYPE_ULONG, TYPE_PTRVOID, TYPE_SHORT, TYPE_PTRVOID,
       TYPE_ULONG, TYPE_ULONG, TYPE_SHORT)

STRUCT(ifmap,
       TYPE_ULONG, TYPE_ULONG, TYPE_SHORT, TYPE_CHAR, TYPE_CHAR, TYPE_CHAR,
       /* Spare 3 bytes */
       TYPE_CHAR, TYPE_CHAR, TYPE_CHAR)

/* The *_ifreq_list arrays deal with the fact that struct ifreq has unions */

STRUCT(sockaddr_ifreq,
       MK_ARRAY(TYPE_CHAR, IFNAMSIZ), MK_STRUCT(STRUCT_sockaddr))

STRUCT(short_ifreq,
       MK_ARRAY(TYPE_CHAR, IFNAMSIZ), TYPE_SHORT)

STRUCT(int_ifreq,
       MK_ARRAY(TYPE_CHAR, IFNAMSIZ), TYPE_INT)

STRUCT(ifmap_ifreq,
       MK_ARRAY(TYPE_CHAR, IFNAMSIZ), MK_STRUCT(STRUCT_ifmap))

STRUCT(char_ifreq,
       MK_ARRAY(TYPE_CHAR, IFNAMSIZ),
       MK_ARRAY(TYPE_CHAR, IFNAMSIZ))

STRUCT(ptr_ifreq,
       MK_ARRAY(TYPE_CHAR, IFNAMSIZ), TYPE_PTRVOID)

STRUCT(ifconf,
       TYPE_INT, TYPE_PTRVOID)

STRUCT(arpreq,
       MK_STRUCT(STRUCT_sockaddr), MK_STRUCT(STRUCT_sockaddr), TYPE_INT, MK_STRUCT(STRUCT_sockaddr),
       MK_ARRAY(TYPE_CHAR, 16))

STRUCT(arpreq_old,
       MK_STRUCT(STRUCT_sockaddr), MK_STRUCT(STRUCT_sockaddr), TYPE_INT, MK_STRUCT(STRUCT_sockaddr))

STRUCT(cdrom_read_audio,
       TYPE_CHAR, TYPE_CHAR, TYPE_CHAR, TYPE_CHAR, TYPE_CHAR, TYPE_INT, TYPE_PTRVOID,
       TYPE_NULL)

STRUCT(hd_geometry,
       TYPE_CHAR, TYPE_CHAR, TYPE_SHORT, TYPE_ULONG)

STRUCT(dirent,
       TYPE_LONG, TYPE_LONG, TYPE_SHORT, MK_ARRAY(TYPE_CHAR, 256))

STRUCT(kbentry,
       TYPE_CHAR, TYPE_CHAR, TYPE_SHORT)

STRUCT(kbsentry,
       TYPE_CHAR, MK_ARRAY(TYPE_CHAR, 512))

STRUCT(audio_buf_info,
       TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT)

STRUCT(count_info,
       TYPE_INT, TYPE_INT, TYPE_INT)

STRUCT(buffmem_desc,
       TYPE_PTRVOID, TYPE_INT)

STRUCT(mixer_info,
       MK_ARRAY(TYPE_CHAR, 16), MK_ARRAY(TYPE_CHAR, 32), TYPE_INT, MK_ARRAY(TYPE_INT, 10))

STRUCT(snd_timer_id,
       TYPE_INT, /* dev_class */
       TYPE_INT, /* dev_sclass */
       TYPE_INT, /* card */
       TYPE_INT, /* device */
       TYPE_INT) /* subdevice */

STRUCT(snd_timer_ginfo,
       MK_STRUCT(STRUCT_snd_timer_id), /* tid */
       TYPE_INT, /* flags */
       TYPE_INT, /* card */
       MK_ARRAY(TYPE_CHAR, 64), /* id */
       MK_ARRAY(TYPE_CHAR, 80), /* name */
       TYPE_ULONG, /* reserved0 */
       TYPE_ULONG, /* resolution */
       TYPE_ULONG, /* resolution_min */
       TYPE_ULONG, /* resolution_max */
       TYPE_INT, /* clients */
       MK_ARRAY(TYPE_CHAR, 32)) /* reserved */

STRUCT(snd_timer_gparams,
       MK_STRUCT(STRUCT_snd_timer_id), /* tid */
       TYPE_ULONG, /* period_num */
       TYPE_ULONG, /* period_den */
       MK_ARRAY(TYPE_CHAR, 32)) /* reserved */

STRUCT(snd_timer_gstatus,
       MK_STRUCT(STRUCT_snd_timer_id), /* tid */
       TYPE_ULONG, /* resolution */
       TYPE_ULONG, /* resolution_num */
       TYPE_ULONG, /* resolution_den */
       MK_ARRAY(TYPE_CHAR, 32)) /* reserved */

STRUCT(snd_timer_select,
       MK_STRUCT(STRUCT_snd_timer_id), /* id */
       MK_ARRAY(TYPE_CHAR, 32)) /* reserved */

STRUCT(snd_timer_info,
       TYPE_INT, /* flags */
       TYPE_INT, /* card */
       MK_ARRAY(TYPE_CHAR, 64), /* id */
       MK_ARRAY(TYPE_CHAR, 80), /* name */
       TYPE_ULONG, /* reserved0 */
       TYPE_ULONG, /* resolution */
       MK_ARRAY(TYPE_CHAR, 64)) /* reserved */

STRUCT(snd_timer_params,
       TYPE_INT, /* flags */
       TYPE_INT, /* ticks */
       TYPE_INT, /* queue_size */
       TYPE_INT, /* reserved0 */
       TYPE_INT, /* filter */
       MK_ARRAY(TYPE_CHAR, 60)) /* reserved */

#if defined(TARGET_SPARC64) && !defined(TARGET_ABI32)
STRUCT(timeval,
       TYPE_LONG, /* tv_sec */
       TYPE_INT) /* tv_usec */

STRUCT(_kernel_sock_timeval,
       TYPE_LONG, /* tv_sec */
       TYPE_INT) /* tv_usec */
#else
STRUCT(timeval,
       TYPE_LONG, /* tv_sec */
       TYPE_LONG) /* tv_usec */

STRUCT(_kernel_sock_timeval,
       TYPE_LONGLONG, /* tv_sec */
       TYPE_LONGLONG) /* tv_usec */
#endif

STRUCT(timespec,
       TYPE_LONG, /* tv_sec */
       TYPE_LONG) /* tv_nsec */

STRUCT(_kernel_timespec,
       TYPE_LONGLONG, /* tv_sec */
       TYPE_LONGLONG) /* tv_nsec */

STRUCT(snd_timer_status,
       MK_STRUCT(STRUCT_timespec), /* tstamp */
       TYPE_INT, /* resolution */
       TYPE_INT, /* lost */
       TYPE_INT, /* overrun */
       TYPE_INT, /* queue */
       MK_ARRAY(TYPE_CHAR, 64)) /* reserved */

/* loop device ioctls */
STRUCT(loop_info,
       TYPE_INT,                 /* lo_number */
       TYPE_OLDDEVT,             /* lo_device */
       TYPE_ULONG,               /* lo_inode */
       TYPE_OLDDEVT,             /* lo_rdevice */
       TYPE_INT,                 /* lo_offset */
       TYPE_INT,                 /* lo_encrypt_type */
       TYPE_INT,                 /* lo_encrypt_key_size */
       TYPE_INT,                 /* lo_flags */
       MK_ARRAY(TYPE_CHAR, 64),  /* lo_name */
       MK_ARRAY(TYPE_CHAR, 32),  /* lo_encrypt_key */
       MK_ARRAY(TYPE_ULONG, 2),  /* lo_init */
       MK_ARRAY(TYPE_CHAR, 4))   /* reserved */

STRUCT(loop_info64,
       TYPE_ULONGLONG,           /* lo_device */
       TYPE_ULONGLONG,           /* lo_inode */
       TYPE_ULONGLONG,           /* lo_rdevice */
       TYPE_ULONGLONG,           /* lo_offset */
       TYPE_ULONGLONG,           /* lo_sizelimit */
       TYPE_INT,                 /* lo_number */
       TYPE_INT,                 /* lo_encrypt_type */
       TYPE_INT,                 /* lo_encrypt_key_size */
       TYPE_INT,                 /* lo_flags */
       MK_ARRAY(TYPE_CHAR, 64),  /* lo_name */
       MK_ARRAY(TYPE_CHAR, 64),  /* lo_crypt_name */
       MK_ARRAY(TYPE_CHAR, 32),  /* lo_encrypt_key */
       MK_ARRAY(TYPE_ULONGLONG, 2))  /* lo_init */

/* mag tape ioctls */
STRUCT(mtop, TYPE_SHORT, TYPE_INT)
STRUCT(mtget, TYPE_LONG, TYPE_LONG, TYPE_LONG, TYPE_LONG, TYPE_LONG,
       TYPE_INT, TYPE_INT)
STRUCT(mtpos, TYPE_LONG)

STRUCT(fb_fix_screeninfo,
       MK_ARRAY(TYPE_CHAR, 16), /* id */
       TYPE_ULONG, /* smem_start */
       TYPE_INT, /* smem_len */
       TYPE_INT, /* type */
       TYPE_INT, /* type_aux */
       TYPE_INT, /* visual */
       TYPE_SHORT, /* xpanstep */
       TYPE_SHORT, /* ypanstep */
       TYPE_SHORT, /* ywrapstep */
       TYPE_INT, /* line_length */
       TYPE_ULONG, /* mmio_start */
       TYPE_INT, /* mmio_len */
       TYPE_INT, /* accel */
       MK_ARRAY(TYPE_CHAR, 3)) /* reserved */

STRUCT(fb_var_screeninfo,
       TYPE_INT, /* xres */
       TYPE_INT, /* yres */
       TYPE_INT, /* xres_virtual */
       TYPE_INT, /* yres_virtual */
       TYPE_INT, /* xoffset */
       TYPE_INT, /* yoffset */
       TYPE_INT, /* bits_per_pixel */
       TYPE_INT, /* grayscale */
       MK_ARRAY(TYPE_INT, 3), /* red */
       MK_ARRAY(TYPE_INT, 3), /* green */
       MK_ARRAY(TYPE_INT, 3), /* blue */
       MK_ARRAY(TYPE_INT, 3), /* transp */
       TYPE_INT, /* nonstd */
       TYPE_INT, /* activate */
       TYPE_INT, /* height */
       TYPE_INT, /* width */
       TYPE_INT, /* accel_flags */
       TYPE_INT, /* pixclock */
       TYPE_INT, /* left_margin */
       TYPE_INT, /* right_margin */
       TYPE_INT, /* upper_margin */
       TYPE_INT, /* lower_margin */
       TYPE_INT, /* hsync_len */
       TYPE_INT, /* vsync_len */
       TYPE_INT, /* sync */
       TYPE_INT, /* vmode */
       TYPE_INT, /* rotate */
       MK_ARRAY(TYPE_INT, 5)) /* reserved */

STRUCT(fb_cmap,
       TYPE_INT, /* start  */
       TYPE_INT, /* len    */
       TYPE_PTRVOID, /* red    */
       TYPE_PTRVOID, /* green  */
       TYPE_PTRVOID, /* blue   */
       TYPE_PTRVOID) /* transp */

STRUCT(fb_con2fbmap,
       TYPE_INT, /* console     */
       TYPE_INT) /* framebuffer */


STRUCT(vt_stat,
       TYPE_SHORT, /* v_active */
       TYPE_SHORT, /* v_signal */
       TYPE_SHORT) /* v_state */

STRUCT(vt_mode,
       TYPE_CHAR,  /* mode   */
       TYPE_CHAR,  /* waitv  */
       TYPE_SHORT, /* relsig */
       TYPE_SHORT, /* acqsig */
       TYPE_SHORT) /* frsig  */

STRUCT(dm_ioctl,
       MK_ARRAY(TYPE_INT, 3), /* version */
       TYPE_INT, /* data_size */
       TYPE_INT, /* data_start */
       TYPE_INT, /* target_count*/
       TYPE_INT, /* open_count */
       TYPE_INT, /* flags */
       TYPE_INT, /* event_nr */
       TYPE_INT, /* padding */
       TYPE_ULONGLONG, /* dev */
       MK_ARRAY(TYPE_CHAR, 128), /* name */
       MK_ARRAY(TYPE_CHAR, 129), /* uuid */
       MK_ARRAY(TYPE_CHAR, 7)) /* data */

STRUCT(dm_target_spec,
       TYPE_ULONGLONG, /* sector_start */
       TYPE_ULONGLONG, /* length */
       TYPE_INT, /* status */
       TYPE_INT, /* next */
       MK_ARRAY(TYPE_CHAR, 16)) /* target_type */

STRUCT(dm_target_deps,
       TYPE_INT, /* count */
       TYPE_INT) /* padding */

STRUCT(dm_name_list,
       TYPE_ULONGLONG, /* dev */
       TYPE_INT) /* next */

STRUCT(dm_target_versions,
       TYPE_INT, /* next */
       MK_ARRAY(TYPE_INT, 3)) /* version*/

STRUCT(dm_target_msg,
       TYPE_ULONGLONG) /* sector */

STRUCT(drm_version,
       TYPE_INT, /* version_major */
       TYPE_INT, /* version_minor */
       TYPE_INT, /* version_patchlevel */
       TYPE_ULONG, /* name_len */
       TYPE_PTRVOID, /* name */
       TYPE_ULONG, /* date_len */
       TYPE_PTRVOID, /* date */
       TYPE_ULONG, /* desc_len */
       TYPE_PTRVOID) /* desc */

STRUCT(drm_i915_getparam,
       TYPE_INT, /* param */
       TYPE_PTRVOID) /* value */

STRUCT(file_clone_range,
       TYPE_LONGLONG, /* src_fd */
       TYPE_ULONGLONG, /* src_offset */
       TYPE_ULONGLONG, /* src_length */
       TYPE_ULONGLONG) /* dest_offset */

STRUCT(fiemap_extent,
       TYPE_ULONGLONG, /* fe_logical */
       TYPE_ULONGLONG, /* fe_physical */
       TYPE_ULONGLONG, /* fe_length */
       MK_ARRAY(TYPE_ULONGLONG, 2), /* fe_reserved64[2] */
       TYPE_INT, /* fe_flags */
       MK_ARRAY(TYPE_INT, 3)) /* fe_reserved[3] */

STRUCT(fiemap,
       TYPE_ULONGLONG, /* fm_start */
       TYPE_ULONGLONG, /* fm_length */
       TYPE_INT, /* fm_flags */
       TYPE_INT, /* fm_mapped_extents */
       TYPE_INT, /* fm_extent_count */
       TYPE_INT) /* fm_reserved */

STRUCT(blkpg_partition,
       TYPE_LONGLONG, /* start */
       TYPE_LONGLONG, /* length */
       TYPE_INT, /* pno */
       MK_ARRAY(TYPE_CHAR, BLKPG_DEVNAMELTH), /* devname */
       MK_ARRAY(TYPE_CHAR, BLKPG_VOLNAMELTH)) /* volname */

#if defined(BTRFS_IOC_SUBVOL_CREATE) || defined(BTRFS_IOC_SNAP_CREATE) || \
    defined(BTRFS_IOC_SNAP_DESTROY)  || defined(BTRFS_IOC_SCAN_DEV)  || \
    defined(BTRFS_IOC_FORGET_DEV)    || defined(BTRFS_IOC_ADD_DEV) || \
    defined(BTRFS_IOC_RM_DEV)        || defined(BTRFS_IOC_DEV_INFO)
STRUCT(btrfs_ioctl_vol_args,
       TYPE_LONGLONG, /* fd */
       MK_ARRAY(TYPE_CHAR, BTRFS_PATH_NAME_MAX + 1)) /* name */
#endif

#ifdef BTRFS_IOC_GET_SUBVOL_INFO
STRUCT(btrfs_ioctl_timespec,
       TYPE_ULONGLONG, /* sec */
       TYPE_INT) /* nsec */

STRUCT(btrfs_ioctl_get_subvol_info_args,
       TYPE_ULONGLONG, /* treeid */
       MK_ARRAY(TYPE_CHAR, BTRFS_VOL_NAME_MAX + 1),
       TYPE_ULONGLONG, /* parentid */
       TYPE_ULONGLONG, /* dirid */
       TYPE_ULONGLONG, /* generation */
       TYPE_ULONGLONG, /* flags */
       MK_ARRAY(TYPE_CHAR, BTRFS_UUID_SIZE), /* uuid */
       MK_ARRAY(TYPE_CHAR, BTRFS_UUID_SIZE), /* parent_uuid */
       MK_ARRAY(TYPE_CHAR, BTRFS_UUID_SIZE), /* received_uuid */
       TYPE_ULONGLONG, /* ctransid */
       TYPE_ULONGLONG, /* otransid */
       TYPE_ULONGLONG, /* stransid */
       TYPE_ULONGLONG, /* rtransid */
       MK_STRUCT(STRUCT_btrfs_ioctl_timespec), /* ctime */
       MK_STRUCT(STRUCT_btrfs_ioctl_timespec), /* otime */
       MK_STRUCT(STRUCT_btrfs_ioctl_timespec), /* stime */
       MK_STRUCT(STRUCT_btrfs_ioctl_timespec), /* rtime */
       MK_ARRAY(TYPE_ULONGLONG, 8)) /* reserved */
#endif

#ifdef BTRFS_IOC_INO_LOOKUP
STRUCT(btrfs_ioctl_ino_lookup_args,
       TYPE_ULONGLONG, /* treeid */
       TYPE_ULONGLONG, /* objectid */
       MK_ARRAY(TYPE_CHAR, BTRFS_INO_LOOKUP_PATH_MAX)) /* name */
#endif

#ifdef BTRFS_IOC_INO_PATHS
STRUCT(btrfs_ioctl_ino_path_args,
       TYPE_ULONGLONG, /* inum */
       TYPE_ULONGLONG, /* size */
       MK_ARRAY(TYPE_ULONGLONG, 4), /* reserved */
       TYPE_ULONGLONG) /* fspath */
#endif

#if defined(BTRFS_IOC_LOGICAL_INO) || defined(BTRFS_IOC_LOGICAL_INO_V2)
STRUCT(btrfs_ioctl_logical_ino_args,
       TYPE_ULONGLONG, /* logical */
       TYPE_ULONGLONG, /* size */
       MK_ARRAY(TYPE_ULONGLONG, 3), /* reserved */
       TYPE_ULONGLONG, /* flags */
       TYPE_ULONGLONG) /* inodes */
#endif

#ifdef BTRFS_IOC_INO_LOOKUP_USER
STRUCT(btrfs_ioctl_ino_lookup_user_args,
       TYPE_ULONGLONG, /* dirid */
       TYPE_ULONGLONG, /* treeid */
       MK_ARRAY(TYPE_CHAR, BTRFS_VOL_NAME_MAX + 1), /* name */
       MK_ARRAY(TYPE_CHAR, BTRFS_INO_LOOKUP_USER_PATH_MAX)) /* path */
#endif

#if defined(BTRFS_IOC_SCRUB) || defined(BTRFS_IOC_SCRUB_PROGRESS)
STRUCT(btrfs_scrub_progress,
       TYPE_ULONGLONG, /* data_extents_scrubbed */
       TYPE_ULONGLONG, /* tree_extents_scrubbed */
       TYPE_ULONGLONG, /* data_bytes_scrubbed */
       TYPE_ULONGLONG, /* tree_bytes_scrubbed */
       TYPE_ULONGLONG, /* read_errors */
       TYPE_ULONGLONG, /* csum_errors */
       TYPE_ULONGLONG, /* verify_errors */
       TYPE_ULONGLONG, /* no_csum */
       TYPE_ULONGLONG, /* csum_discards */
       TYPE_ULONGLONG, /* super_errors */
       TYPE_ULONGLONG, /* malloc_errors */
       TYPE_ULONGLONG, /* uncorrectable_errors */
       TYPE_ULONGLONG, /* corrected_er */
       TYPE_ULONGLONG, /* last_physical */
       TYPE_ULONGLONG) /* unverified_errors */

STRUCT(btrfs_ioctl_scrub_args,
       TYPE_ULONGLONG, /* devid */
       TYPE_ULONGLONG, /* start */
       TYPE_ULONGLONG, /* end */
       TYPE_ULONGLONG, /* flags */
       MK_STRUCT(STRUCT_btrfs_scrub_progress), /* progress */
       MK_ARRAY(TYPE_ULONGLONG,
                (1024 - 32 -
                 sizeof(struct btrfs_scrub_progress)) / 8)) /* unused */
#endif

#ifdef BTRFS_IOC_DEV_INFO
STRUCT(btrfs_ioctl_dev_info_args,
       TYPE_ULONGLONG, /* devid */
       MK_ARRAY(TYPE_CHAR, BTRFS_UUID_SIZE), /* uuid */
       TYPE_ULONGLONG, /* bytes_used */
       TYPE_ULONGLONG, /* total_bytes */
       MK_ARRAY(TYPE_ULONGLONG, 379), /* unused */
       MK_ARRAY(TYPE_CHAR, BTRFS_DEVICE_PATH_NAME_MAX)) /* path */
#endif

#ifdef BTRFS_IOC_GET_SUBVOL_ROOTREF
STRUCT(rootref,
       TYPE_ULONGLONG, /* treeid */
       TYPE_ULONGLONG) /* dirid */

STRUCT(btrfs_ioctl_get_subvol_rootref_args,
       TYPE_ULONGLONG, /* min_treeid */
       MK_ARRAY(MK_STRUCT(STRUCT_rootref),
                BTRFS_MAX_ROOTREF_BUFFER_NUM), /* rootref */
       TYPE_CHAR, /* num_items */
       MK_ARRAY(TYPE_CHAR, 7)) /* align */
#endif

#ifdef BTRFS_IOC_GET_DEV_STATS
STRUCT(btrfs_ioctl_get_dev_stats,
       TYPE_ULONGLONG, /* devid */
       TYPE_ULONGLONG, /* nr_items */
       TYPE_ULONGLONG, /* flags */
       MK_ARRAY(TYPE_ULONGLONG, BTRFS_DEV_STAT_VALUES_MAX), /* values */
       MK_ARRAY(TYPE_ULONGLONG,
                128 - 2 - BTRFS_DEV_STAT_VALUES_MAX)) /* unused */
#endif

STRUCT(btrfs_ioctl_quota_ctl_args,
       TYPE_ULONGLONG, /* cmd */
       TYPE_ULONGLONG) /* status */

STRUCT(btrfs_ioctl_quota_rescan_args,
       TYPE_ULONGLONG, /* flags */
       TYPE_ULONGLONG, /* progress */
       MK_ARRAY(TYPE_ULONGLONG, 6)) /* reserved */

STRUCT(btrfs_ioctl_qgroup_assign_args,
       TYPE_ULONGLONG, /* assign */
       TYPE_ULONGLONG, /* src */
       TYPE_ULONGLONG) /* dst */

STRUCT(btrfs_ioctl_qgroup_create_args,
       TYPE_ULONGLONG, /* create */
       TYPE_ULONGLONG) /* qgroupid */

STRUCT(btrfs_qgroup_limit,
       TYPE_ULONGLONG, /* flags */
       TYPE_ULONGLONG, /* max_rfer */
       TYPE_ULONGLONG, /* max_excl */
       TYPE_ULONGLONG, /* rsv_rfer */
       TYPE_ULONGLONG) /* rsv_excl */

STRUCT(btrfs_ioctl_qgroup_limit_args,
       TYPE_ULONGLONG, /* qgroupid */
       MK_STRUCT(STRUCT_btrfs_qgroup_limit)) /* lim */

STRUCT(btrfs_ioctl_feature_flags,
       TYPE_ULONGLONG, /* compat_flags */
       TYPE_ULONGLONG, /* compat_ro_flags */
       TYPE_ULONGLONG) /* incompat_flags */

STRUCT(rtc_time,
       TYPE_INT, /* tm_sec */
       TYPE_INT, /* tm_min */
       TYPE_INT, /* tm_hour */
       TYPE_INT, /* tm_mday */
       TYPE_INT, /* tm_mon */
       TYPE_INT, /* tm_year */
       TYPE_INT, /* tm_wday */
       TYPE_INT, /* tm_yday */
       TYPE_INT) /* tm_isdst */

STRUCT(rtc_wkalrm,
       TYPE_CHAR, /* enabled */
       TYPE_CHAR, /* pending */
       MK_STRUCT(STRUCT_rtc_time)) /* time */

STRUCT(rtc_pll_info,
       TYPE_INT, /* pll_ctrl */
       TYPE_INT, /* pll_value */
       TYPE_INT, /* pll_max */
       TYPE_INT, /* pll_min */
       TYPE_INT, /* pll_posmult */
       TYPE_INT, /* pll_negmult */
       TYPE_LONG) /* pll_clock */

STRUCT(blkpg_ioctl_arg,
       TYPE_INT, /* op */
       TYPE_INT, /* flags */
       TYPE_INT, /* datalen */
       TYPE_PTRVOID) /* data */

STRUCT(format_descr,
       TYPE_INT,     /* device */
       TYPE_INT,     /* head */
       TYPE_INT)     /* track */

STRUCT(floppy_max_errors,
       TYPE_INT, /* abort */
       TYPE_INT, /* read_track */
       TYPE_INT, /* reset */
       TYPE_INT, /* recal */
       TYPE_INT) /* reporting */

#if defined(CONFIG_USBFS)
/* usb device ioctls */
STRUCT(usbdevfs_ctrltransfer,
        TYPE_CHAR, /* bRequestType */
        TYPE_CHAR, /* bRequest */
        TYPE_SHORT, /* wValue */
        TYPE_SHORT, /* wIndex */
        TYPE_SHORT, /* wLength */
        TYPE_INT, /* timeout */
        TYPE_PTRVOID) /* data */

STRUCT(usbdevfs_bulktransfer,
        TYPE_INT, /* ep */
        TYPE_INT, /* len */
        TYPE_INT, /* timeout */
        TYPE_PTRVOID) /* data */

STRUCT(usbdevfs_setinterface,
        TYPE_INT, /* interface */
        TYPE_INT) /* altsetting */

STRUCT(usbdevfs_disconnectsignal,
        TYPE_INT, /* signr */
        TYPE_PTRVOID) /* context */

STRUCT(usbdevfs_getdriver,
        TYPE_INT, /* interface */
        MK_ARRAY(TYPE_CHAR, USBDEVFS_MAXDRIVERNAME + 1)) /* driver */

STRUCT(usbdevfs_connectinfo,
        TYPE_INT, /* devnum */
        TYPE_CHAR) /* slow */

STRUCT(usbdevfs_iso_packet_desc,
        TYPE_INT, /* length */
        TYPE_INT, /* actual_length */
        TYPE_INT) /* status */

STRUCT(usbdevfs_urb,
        TYPE_CHAR, /* type */
        TYPE_CHAR, /* endpoint */
        TYPE_INT, /* status */
        TYPE_INT, /* flags */
        TYPE_PTRVOID, /* buffer */
        TYPE_INT, /* buffer_length */
        TYPE_INT, /* actual_length */
        TYPE_INT, /* start_frame */
        TYPE_INT, /* union number_of_packets stream_id */
        TYPE_INT, /* error_count */
        TYPE_INT, /* signr */
        TYPE_PTRVOID, /* usercontext */
        MK_ARRAY(MK_STRUCT(STRUCT_usbdevfs_iso_packet_desc), 0)) /* desc */

STRUCT(usbdevfs_ioctl,
        TYPE_INT, /* ifno */
        TYPE_INT, /* ioctl_code */
        TYPE_PTRVOID) /* data */

STRUCT(usbdevfs_hub_portinfo,
        TYPE_CHAR, /* nports */
        MK_ARRAY(TYPE_CHAR, 127)) /* port */

STRUCT(usbdevfs_disconnect_claim,
        TYPE_INT, /* interface */
        TYPE_INT, /* flags */
        MK_ARRAY(TYPE_CHAR, USBDEVFS_MAXDRIVERNAME + 1)) /* driver */
#endif /* CONFIG_USBFS */
