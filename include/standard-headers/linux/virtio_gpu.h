/*
 * Virtio GPU Device
 *
 * Copyright Red Hat, Inc. 2013-2014
 *
 * Authors:
 *     Dave Airlie <airlied@redhat.com>
 *     Gerd Hoffmann <kraxel@redhat.com>
 *
 * This header is BSD licensed so anyone can use the definitions
 * to implement compatible drivers/servers:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of IBM nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL IBM OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef VIRTIO_GPU_HW_H
#define VIRTIO_GPU_HW_H

#include "standard-headers/linux/types.h"

/*
 * VIRTIO_GPU_CMD_CTX_*
 * VIRTIO_GPU_CMD_*_3D
 */
#define VIRTIO_GPU_F_VIRGL               0

/*
 * VIRTIO_GPU_CMD_GET_EDID
 */
#define VIRTIO_GPU_F_EDID                1
/*
 * VIRTIO_GPU_CMD_RESOURCE_ASSIGN_UUID
 */
#define VIRTIO_GPU_F_RESOURCE_UUID       2

/*
 * VIRTIO_GPU_CMD_RESOURCE_CREATE_BLOB
 */
#define VIRTIO_GPU_F_RESOURCE_BLOB       3
/*
 * VIRTIO_GPU_CMD_CREATE_CONTEXT with
 * context_init and multiple timelines
 */
#define VIRTIO_GPU_F_CONTEXT_INIT        4

enum virtio_gpu_ctrl_type {
	VIRTIO_GPU_UNDEFINED = 0,

	/* 2d commands */
	VIRTIO_GPU_CMD_GET_DISPLAY_INFO = 0x0100,
	VIRTIO_GPU_CMD_RESOURCE_CREATE_2D,
	VIRTIO_GPU_CMD_RESOURCE_UNREF,
	VIRTIO_GPU_CMD_SET_SCANOUT,
	VIRTIO_GPU_CMD_RESOURCE_FLUSH,
	VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D,
	VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING,
	VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING,
	VIRTIO_GPU_CMD_GET_CAPSET_INFO,
	VIRTIO_GPU_CMD_GET_CAPSET,
	VIRTIO_GPU_CMD_GET_EDID,
	VIRTIO_GPU_CMD_RESOURCE_ASSIGN_UUID,
	VIRTIO_GPU_CMD_RESOURCE_CREATE_BLOB,
	VIRTIO_GPU_CMD_SET_SCANOUT_BLOB,

	/* 3d commands */
	VIRTIO_GPU_CMD_CTX_CREATE = 0x0200,
	VIRTIO_GPU_CMD_CTX_DESTROY,
	VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE,
	VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE,
	VIRTIO_GPU_CMD_RESOURCE_CREATE_3D,
	VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D,
	VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D,
	VIRTIO_GPU_CMD_SUBMIT_3D,
	VIRTIO_GPU_CMD_RESOURCE_MAP_BLOB,
	VIRTIO_GPU_CMD_RESOURCE_UNMAP_BLOB,

	/* cursor commands */
	VIRTIO_GPU_CMD_UPDATE_CURSOR = 0x0300,
	VIRTIO_GPU_CMD_MOVE_CURSOR,

	/* success responses */
	VIRTIO_GPU_RESP_OK_NODATA = 0x1100,
	VIRTIO_GPU_RESP_OK_DISPLAY_INFO,
	VIRTIO_GPU_RESP_OK_CAPSET_INFO,
	VIRTIO_GPU_RESP_OK_CAPSET,
	VIRTIO_GPU_RESP_OK_EDID,
	VIRTIO_GPU_RESP_OK_RESOURCE_UUID,
	VIRTIO_GPU_RESP_OK_MAP_INFO,

	/* error responses */
	VIRTIO_GPU_RESP_ERR_UNSPEC = 0x1200,
	VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY,
	VIRTIO_GPU_RESP_ERR_INVALID_SCANOUT_ID,
	VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID,
	VIRTIO_GPU_RESP_ERR_INVALID_CONTEXT_ID,
	VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER,
};

enum virtio_gpu_shm_id {
	VIRTIO_GPU_SHM_ID_UNDEFINED = 0,
	/*
	 * VIRTIO_GPU_CMD_RESOURCE_MAP_BLOB
	 * VIRTIO_GPU_CMD_RESOURCE_UNMAP_BLOB
	 */
	VIRTIO_GPU_SHM_ID_HOST_VISIBLE = 1
};

#define VIRTIO_GPU_FLAG_FENCE         (1 << 0)
/*
 * If the following flag is set, then ring_idx contains the index
 * of the command ring that needs to used when creating the fence
 */
#define VIRTIO_GPU_FLAG_INFO_RING_IDX (1 << 1)

struct virtio_gpu_ctrl_hdr {
	uint32_t type;
	uint32_t flags;
	uint64_t fence_id;
	uint32_t ctx_id;
	uint8_t ring_idx;
	uint8_t padding[3];
};

/* data passed in the cursor vq */

struct virtio_gpu_cursor_pos {
	uint32_t scanout_id;
	uint32_t x;
	uint32_t y;
	uint32_t padding;
};

/* VIRTIO_GPU_CMD_UPDATE_CURSOR, VIRTIO_GPU_CMD_MOVE_CURSOR */
struct virtio_gpu_update_cursor {
	struct virtio_gpu_ctrl_hdr hdr;
	struct virtio_gpu_cursor_pos pos;  /* update & move */
	uint32_t resource_id;           /* update only */
	uint32_t hot_x;                 /* update only */
	uint32_t hot_y;                 /* update only */
	uint32_t padding;
};

/* data passed in the control vq, 2d related */

struct virtio_gpu_rect {
	uint32_t x;
	uint32_t y;
	uint32_t width;
	uint32_t height;
};

/* VIRTIO_GPU_CMD_RESOURCE_UNREF */
struct virtio_gpu_resource_unref {
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t resource_id;
	uint32_t padding;
};

/* VIRTIO_GPU_CMD_RESOURCE_CREATE_2D: create a 2d resource with a format */
struct virtio_gpu_resource_create_2d {
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t resource_id;
	uint32_t format;
	uint32_t width;
	uint32_t height;
};

/* VIRTIO_GPU_CMD_SET_SCANOUT */
struct virtio_gpu_set_scanout {
	struct virtio_gpu_ctrl_hdr hdr;
	struct virtio_gpu_rect r;
	uint32_t scanout_id;
	uint32_t resource_id;
};

/* VIRTIO_GPU_CMD_RESOURCE_FLUSH */
struct virtio_gpu_resource_flush {
	struct virtio_gpu_ctrl_hdr hdr;
	struct virtio_gpu_rect r;
	uint32_t resource_id;
	uint32_t padding;
};

/* VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D: simple transfer to_host */
struct virtio_gpu_transfer_to_host_2d {
	struct virtio_gpu_ctrl_hdr hdr;
	struct virtio_gpu_rect r;
	uint64_t offset;
	uint32_t resource_id;
	uint32_t padding;
};

struct virtio_gpu_mem_entry {
	uint64_t addr;
	uint32_t length;
	uint32_t padding;
};

/* VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING */
struct virtio_gpu_resource_attach_backing {
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t resource_id;
	uint32_t nr_entries;
};

/* VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING */
struct virtio_gpu_resource_detach_backing {
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t resource_id;
	uint32_t padding;
};

/* VIRTIO_GPU_RESP_OK_DISPLAY_INFO */
#define VIRTIO_GPU_MAX_SCANOUTS 16
struct virtio_gpu_resp_display_info {
	struct virtio_gpu_ctrl_hdr hdr;
	struct virtio_gpu_display_one {
		struct virtio_gpu_rect r;
		uint32_t enabled;
		uint32_t flags;
	} pmodes[VIRTIO_GPU_MAX_SCANOUTS];
};

/* data passed in the control vq, 3d related */

struct virtio_gpu_box {
	uint32_t x, y, z;
	uint32_t w, h, d;
};

/* VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D, VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D */
struct virtio_gpu_transfer_host_3d {
	struct virtio_gpu_ctrl_hdr hdr;
	struct virtio_gpu_box box;
	uint64_t offset;
	uint32_t resource_id;
	uint32_t level;
	uint32_t stride;
	uint32_t layer_stride;
};

/* VIRTIO_GPU_CMD_RESOURCE_CREATE_3D */
#define VIRTIO_GPU_RESOURCE_FLAG_Y_0_TOP (1 << 0)
struct virtio_gpu_resource_create_3d {
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t resource_id;
	uint32_t target;
	uint32_t format;
	uint32_t bind;
	uint32_t width;
	uint32_t height;
	uint32_t depth;
	uint32_t array_size;
	uint32_t last_level;
	uint32_t nr_samples;
	uint32_t flags;
	uint32_t padding;
};

/* VIRTIO_GPU_CMD_CTX_CREATE */
#define VIRTIO_GPU_CONTEXT_INIT_CAPSET_ID_MASK 0x000000ff
struct virtio_gpu_ctx_create {
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t nlen;
	uint32_t context_init;
	char debug_name[64];
};

/* VIRTIO_GPU_CMD_CTX_DESTROY */
struct virtio_gpu_ctx_destroy {
	struct virtio_gpu_ctrl_hdr hdr;
};

/* VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE, VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE */
struct virtio_gpu_ctx_resource {
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t resource_id;
	uint32_t padding;
};

/* VIRTIO_GPU_CMD_SUBMIT_3D */
struct virtio_gpu_cmd_submit {
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t size;
	uint32_t padding;
};

#define VIRTIO_GPU_CAPSET_VIRGL 1
#define VIRTIO_GPU_CAPSET_VIRGL2 2
/* 3 is reserved for gfxstream */
#define VIRTIO_GPU_CAPSET_VENUS 4
#define VIRTIO_GPU_CAPSET_DRM 6

/* VIRTIO_GPU_CMD_GET_CAPSET_INFO */
struct virtio_gpu_get_capset_info {
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t capset_index;
	uint32_t padding;
};

/* VIRTIO_GPU_RESP_OK_CAPSET_INFO */
struct virtio_gpu_resp_capset_info {
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t capset_id;
	uint32_t capset_max_version;
	uint32_t capset_max_size;
	uint32_t padding;
};

/* VIRTIO_GPU_CMD_GET_CAPSET */
struct virtio_gpu_get_capset {
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t capset_id;
	uint32_t capset_version;
};

/* VIRTIO_GPU_RESP_OK_CAPSET */
struct virtio_gpu_resp_capset {
	struct virtio_gpu_ctrl_hdr hdr;
	uint8_t capset_data[];
};

/* VIRTIO_GPU_CMD_GET_EDID */
struct virtio_gpu_cmd_get_edid {
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t scanout;
	uint32_t padding;
};

/* VIRTIO_GPU_RESP_OK_EDID */
struct virtio_gpu_resp_edid {
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t size;
	uint32_t padding;
	uint8_t edid[1024];
};

#define VIRTIO_GPU_EVENT_DISPLAY (1 << 0)

struct virtio_gpu_config {
	uint32_t events_read;
	uint32_t events_clear;
	uint32_t num_scanouts;
	uint32_t num_capsets;
};

/* simple formats for fbcon/X use */
enum virtio_gpu_formats {
	VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM  = 1,
	VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM  = 2,
	VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM  = 3,
	VIRTIO_GPU_FORMAT_X8R8G8B8_UNORM  = 4,

	VIRTIO_GPU_FORMAT_R8G8B8A8_UNORM  = 67,
	VIRTIO_GPU_FORMAT_X8B8G8R8_UNORM  = 68,

	VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM  = 121,
	VIRTIO_GPU_FORMAT_R8G8B8X8_UNORM  = 134,
};

/* VIRTIO_GPU_CMD_RESOURCE_ASSIGN_UUID */
struct virtio_gpu_resource_assign_uuid {
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t resource_id;
	uint32_t padding;
};

/* VIRTIO_GPU_RESP_OK_RESOURCE_UUID */
struct virtio_gpu_resp_resource_uuid {
	struct virtio_gpu_ctrl_hdr hdr;
	uint8_t uuid[16];
};

/* VIRTIO_GPU_CMD_RESOURCE_CREATE_BLOB */
struct virtio_gpu_resource_create_blob {
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t resource_id;
#define VIRTIO_GPU_BLOB_MEM_GUEST             0x0001
#define VIRTIO_GPU_BLOB_MEM_HOST3D            0x0002
#define VIRTIO_GPU_BLOB_MEM_HOST3D_GUEST      0x0003

#define VIRTIO_GPU_BLOB_FLAG_USE_MAPPABLE     0x0001
#define VIRTIO_GPU_BLOB_FLAG_USE_SHAREABLE    0x0002
#define VIRTIO_GPU_BLOB_FLAG_USE_CROSS_DEVICE 0x0004
	/* zero is invalid blob mem */
	uint32_t blob_mem;
	uint32_t blob_flags;
	uint32_t nr_entries;
	uint64_t blob_id;
	uint64_t size;
	/*
	 * sizeof(nr_entries * virtio_gpu_mem_entry) bytes follow
	 */
};

/* VIRTIO_GPU_CMD_SET_SCANOUT_BLOB */
struct virtio_gpu_set_scanout_blob {
	struct virtio_gpu_ctrl_hdr hdr;
	struct virtio_gpu_rect r;
	uint32_t scanout_id;
	uint32_t resource_id;
	uint32_t width;
	uint32_t height;
	uint32_t format;
	uint32_t padding;
	uint32_t strides[4];
	uint32_t offsets[4];
};

/* VIRTIO_GPU_CMD_RESOURCE_MAP_BLOB */
struct virtio_gpu_resource_map_blob {
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t resource_id;
	uint32_t padding;
	uint64_t offset;
};

/* VIRTIO_GPU_RESP_OK_MAP_INFO */
#define VIRTIO_GPU_MAP_CACHE_MASK     0x0f
#define VIRTIO_GPU_MAP_CACHE_NONE     0x00
#define VIRTIO_GPU_MAP_CACHE_CACHED   0x01
#define VIRTIO_GPU_MAP_CACHE_UNCACHED 0x02
#define VIRTIO_GPU_MAP_CACHE_WC       0x03
struct virtio_gpu_resp_map_info {
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t map_info;
	uint32_t padding;
};

/* VIRTIO_GPU_CMD_RESOURCE_UNMAP_BLOB */
struct virtio_gpu_resource_unmap_blob {
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t resource_id;
	uint32_t padding;
};

#endif
