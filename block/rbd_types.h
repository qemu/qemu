/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2004-2010 Sage Weil <sage@newdream.net>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.LIB.
 *
 */

#ifndef CEPH_RBD_TYPES_H
#define CEPH_RBD_TYPES_H


/*
 * rbd image 'foo' consists of objects
 *   foo.rbd      - image metadata
 *   foo.00000000
 *   foo.00000001
 *   ...          - data
 */

#define RBD_SUFFIX              ".rbd"
#define RBD_DIRECTORY           "rbd_directory"
#define RBD_INFO                "rbd_info"

#define RBD_DEFAULT_OBJ_ORDER   22   /* 4MB */

#define RBD_MAX_OBJ_NAME_SIZE   96
#define RBD_MAX_BLOCK_NAME_SIZE 24
#define RBD_MAX_SEG_NAME_SIZE   128

#define RBD_COMP_NONE           0
#define RBD_CRYPT_NONE          0

#define RBD_HEADER_TEXT         "<<< Rados Block Device Image >>>\n"
#define RBD_HEADER_SIGNATURE    "RBD"
#define RBD_HEADER_VERSION      "001.005"

struct rbd_info {
    uint64_t max_id;
} __attribute__ ((packed));

struct rbd_obj_snap_ondisk {
    uint64_t id;
    uint64_t image_size;
} __attribute__((packed));

struct rbd_obj_header_ondisk {
    char text[40];
    char block_name[RBD_MAX_BLOCK_NAME_SIZE];
    char signature[4];
    char version[8];
    struct {
        uint8_t order;
        uint8_t crypt_type;
        uint8_t comp_type;
        uint8_t unused;
    } __attribute__((packed)) options;
    uint64_t image_size;
    uint64_t snap_seq;
    uint32_t snap_count;
    uint32_t reserved;
    uint64_t snap_names_len;
    struct rbd_obj_snap_ondisk snaps[0];
} __attribute__((packed));


#endif
