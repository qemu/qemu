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

STRUCT(timeval,
       MK_ARRAY(TYPE_LONG, 2))

STRUCT(timespec,
       MK_ARRAY(TYPE_LONG, 2))

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

STRUCT(blkpg_ioctl_arg,
       TYPE_INT, /* op */
       TYPE_INT, /* flags */
       TYPE_INT, /* datalen */
       TYPE_PTRVOID) /* data */

#if defined(CONFIG_LIBDRM) && HOST_LONG_BITS == TARGET_ABI_BITS && (defined(HOST_WORDS_BIGENDIAN) == defined(TARGET_WORDS_BIGENDIAN))
STRUCT(drm_version,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_ULONG,
       TYPE_PTRVOID,
       TYPE_ULONG,
       TYPE_PTRVOID,
       TYPE_ULONG,
       TYPE_PTRVOID
)

STRUCT(drm_unique,
       TYPE_ULONG,
       TYPE_PTRVOID
)

STRUCT(drm_block,
       TYPE_INT
)

STRUCT(drm_control,
       TYPE_INT,
       TYPE_INT
)

STRUCT(drm_auth,
       TYPE_INT
)

STRUCT(drm_irq_busid,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT
)

STRUCT(drm_map,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_INT,
       TYPE_INT,
       TYPE_PTRVOID,
       TYPE_INT
)

STRUCT(drm_client,
       TYPE_INT,
       TYPE_INT,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG
)

STRUCT(drm_set_version,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT
)

STRUCT(drm_modeset_ctl,
       TYPE_INT,
       TYPE_INT
)

STRUCT(drm_gem_close,
       TYPE_INT,
       TYPE_INT
)

STRUCT(drm_gem_flink,
       TYPE_INT,
       TYPE_INT
)

STRUCT(drm_gem_open,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT
)

STRUCT(drm_get_cap,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG
)

STRUCT(drm_set_client_cap,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG
)

STRUCT(drm_prime_handle,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT
)

STRUCT(drm_i915_init_t,
       TYPE_INT, /*func*/
       TYPE_INT, /*mmio_offset*/
       TYPE_INT, /*sarea_priv_offset*/
       TYPE_INT, /*ring_start*/
       TYPE_INT, /*ring_end*/
       TYPE_INT, /*ring_size*/
       TYPE_INT, /*front_offset*/
       TYPE_INT, /*back_offset*/
       TYPE_INT, /*depth_offset*/
       TYPE_INT, /*w*/
       TYPE_INT, /*h*/
       TYPE_INT, /*pitch*/
       TYPE_INT, /*pitch_bits*/
       TYPE_INT, /*back_pitch*/
       TYPE_INT, /*depth_pitch*/
       TYPE_INT, /*cpp*/
       TYPE_INT  /*chipset*/
)

STRUCT(drm_i915_gem_init,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG
)

STRUCT(drm_i915_gem_create,
       TYPE_ULONGLONG,
       TYPE_INT,
       TYPE_INT
)

STRUCT(drm_i915_getparam_t,
       TYPE_INT,
       TYPE_PTRVOID
)

STRUCT(drm_i915_setparam_t,
       TYPE_INT,
       TYPE_INT
)

STRUCT(drm_i915_gem_execbuffer,
       TYPE_ULONGLONG,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_ULONGLONG
)

STRUCT(drm_i915_gem_exec_object2,
       TYPE_INT,
       TYPE_INT,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG
)

STRUCT(drm_i915_gem_execbuffer2,
       TYPE_ULONGLONG,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG
)

STRUCT(drm_i915_gem_busy,
       TYPE_INT,
       TYPE_INT
)

STRUCT(drm_i915_gem_pread,
       TYPE_INT,
       TYPE_INT,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG
)

STRUCT(drm_i915_gem_pwrite,
       TYPE_INT,
       TYPE_INT,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG
)

STRUCT(drm_i915_gem_mmap,
       TYPE_INT,
       TYPE_INT,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG
)

STRUCT(drm_i915_gem_mmap_gtt,
       TYPE_INT,
       TYPE_INT,
       TYPE_ULONGLONG
)

STRUCT(drm_i915_gem_set_domain,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT
)

STRUCT(drm_i915_gem_sw_finish,
       TYPE_INT
)

STRUCT(drm_i915_gem_caching,
       TYPE_INT,
       TYPE_INT
)

STRUCT(drm_i915_gem_set_tiling,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT
)

STRUCT(drm_i915_gem_get_tiling,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT
)

STRUCT(drm_i915_gem_get_aperture,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG
)

STRUCT(drm_i915_gem_madvise,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT
)

STRUCT(drm_i915_gem_context_create,
       TYPE_INT,
       TYPE_INT
)

STRUCT(drm_i915_gem_context_destroy,
       TYPE_INT,
       TYPE_INT
)

STRUCT(drm_i915_reg_read,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG
)

STRUCT(drm_i915_reset_stats,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT
)

STRUCT(drm_i915_gem_userptr,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_INT,
       TYPE_INT
)

STRUCT(drm_i915_gem_context_param,
       TYPE_INT,
       TYPE_INT,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG
)

STRUCT(drm_radeon_init_t,
       TYPE_INT,
       TYPE_ULONG,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,

       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,

       TYPE_ULONG,
       TYPE_ULONG,
       TYPE_ULONG,
       TYPE_ULONG,
       TYPE_ULONG,
       TYPE_ULONG
)

STRUCT(drm_radeon_cp_stop_t,
       TYPE_INT,
       TYPE_INT
)

STRUCT(drm_radeon_fullscreen_t,
       TYPE_INT
)

STRUCT(drm_radeon_clear_t,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_PTRVOID
)

STRUCT(drm_radeon_vertex_t,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT
)

STRUCT(drm_radeon_indices_t,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT
)

STRUCT(drm_radeon_vertex2_t,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_PTRVOID,
       TYPE_INT,
       TYPE_PTRVOID
)

STRUCT(drm_radeon_cmd_buffer_t,
       TYPE_INT,
       TYPE_PTRVOID,
       TYPE_INT,
       TYPE_PTRVOID
)

STRUCT(drm_radeon_texture_t,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_PTRVOID
)

STRUCT(drm_radeon_stipple_t,
       TYPE_PTRVOID
)

STRUCT(drm_radeon_indirect_t,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT
)

STRUCT(drm_radeon_getparam_t,
       TYPE_INT,
       TYPE_PTRVOID
)

STRUCT(drm_radeon_mem_alloc_t,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_PTRVOID
)

STRUCT(drm_radeon_mem_free_t,
       TYPE_INT,
       TYPE_INT
)

STRUCT(drm_radeon_mem_init_heap_t,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT
)

STRUCT(drm_radeon_irq_emit_t,
       TYPE_PTRVOID
)

STRUCT(drm_radeon_irq_wait_t,
       TYPE_INT
)

STRUCT(drm_radeon_setparam_t,
       TYPE_INT,
       TYPE_LONGLONG
)

STRUCT(drm_radeon_surface_alloc_t,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT
)

STRUCT(drm_radeon_surface_free_t,
       TYPE_INT
)

STRUCT(drm_radeon_gem_info,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG
)

STRUCT(drm_radeon_gem_create,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT
)

STRUCT(drm_radeon_gem_userptr,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_INT,
       TYPE_INT
)

STRUCT(drm_radeon_gem_set_tiling,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT
)

STRUCT(drm_radeon_gem_get_tiling,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT
)

STRUCT(drm_radeon_gem_mmap,
       TYPE_INT,
       TYPE_INT,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG
)

STRUCT(drm_radeon_gem_set_domain,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT
)

STRUCT(drm_radeon_gem_wait_idle,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT
)

STRUCT(drm_radeon_gem_busy,
       TYPE_INT,
       TYPE_INT
)

STRUCT(drm_radeon_gem_pread,
       TYPE_INT,
       TYPE_INT,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG
)

STRUCT(drm_radeon_gem_pwrite,
       TYPE_INT,
       TYPE_INT,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG
)

STRUCT(drm_radeon_gem_op,
       TYPE_INT,
       TYPE_INT,
       TYPE_ULONGLONG
)

STRUCT(drm_radeon_gem_va,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_INT,
       TYPE_ULONGLONG
)

STRUCT(drm_radeon_cs,
       TYPE_INT,
       TYPE_INT,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG,
       TYPE_ULONGLONG
)

STRUCT(drm_radeon_info,
       TYPE_INT,
       TYPE_INT,
       TYPE_ULONGLONG
)

#endif
