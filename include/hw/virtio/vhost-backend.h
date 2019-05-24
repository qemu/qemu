/*
 * vhost-backend
 *
 * Copyright (c) 2013 Virtual Open Systems Sarl.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef VHOST_BACKEND_H
#define VHOST_BACKEND_H

#include "exec/memory.h"

typedef enum VhostBackendType {
    VHOST_BACKEND_TYPE_NONE = 0,
    VHOST_BACKEND_TYPE_KERNEL = 1,
    VHOST_BACKEND_TYPE_USER = 2,
    VHOST_BACKEND_TYPE_MAX = 3,
} VhostBackendType;

typedef enum VhostSetConfigType {
    VHOST_SET_CONFIG_TYPE_MASTER = 0,
    VHOST_SET_CONFIG_TYPE_MIGRATION = 1,
} VhostSetConfigType;

struct vhost_inflight;
struct vhost_dev;
struct vhost_log;
struct vhost_memory;
struct vhost_vring_file;
struct vhost_vring_state;
struct vhost_vring_addr;
struct vhost_scsi_target;
struct vhost_iotlb_msg;

typedef int (*vhost_backend_init)(struct vhost_dev *dev, void *opaque);
typedef int (*vhost_backend_cleanup)(struct vhost_dev *dev);
typedef int (*vhost_backend_memslots_limit)(struct vhost_dev *dev);

typedef int (*vhost_net_set_backend_op)(struct vhost_dev *dev,
                                struct vhost_vring_file *file);
typedef int (*vhost_net_set_mtu_op)(struct vhost_dev *dev, uint16_t mtu);
typedef int (*vhost_scsi_set_endpoint_op)(struct vhost_dev *dev,
                                  struct vhost_scsi_target *target);
typedef int (*vhost_scsi_clear_endpoint_op)(struct vhost_dev *dev,
                                    struct vhost_scsi_target *target);
typedef int (*vhost_scsi_get_abi_version_op)(struct vhost_dev *dev,
                                             int *version);
typedef int (*vhost_set_log_base_op)(struct vhost_dev *dev, uint64_t base,
                                     struct vhost_log *log);
typedef int (*vhost_set_mem_table_op)(struct vhost_dev *dev,
                                      struct vhost_memory *mem);
typedef int (*vhost_set_vring_addr_op)(struct vhost_dev *dev,
                                       struct vhost_vring_addr *addr);
typedef int (*vhost_set_vring_endian_op)(struct vhost_dev *dev,
                                         struct vhost_vring_state *ring);
typedef int (*vhost_set_vring_num_op)(struct vhost_dev *dev,
                                      struct vhost_vring_state *ring);
typedef int (*vhost_set_vring_base_op)(struct vhost_dev *dev,
                                       struct vhost_vring_state *ring);
typedef int (*vhost_get_vring_base_op)(struct vhost_dev *dev,
                                       struct vhost_vring_state *ring);
typedef int (*vhost_set_vring_kick_op)(struct vhost_dev *dev,
                                       struct vhost_vring_file *file);
typedef int (*vhost_set_vring_call_op)(struct vhost_dev *dev,
                                       struct vhost_vring_file *file);
typedef int (*vhost_set_vring_busyloop_timeout_op)(struct vhost_dev *dev,
                                                   struct vhost_vring_state *r);
typedef int (*vhost_set_features_op)(struct vhost_dev *dev,
                                     uint64_t features);
typedef int (*vhost_get_features_op)(struct vhost_dev *dev,
                                     uint64_t *features);
typedef int (*vhost_set_owner_op)(struct vhost_dev *dev);
typedef int (*vhost_reset_device_op)(struct vhost_dev *dev);
typedef int (*vhost_get_vq_index_op)(struct vhost_dev *dev, int idx);
typedef int (*vhost_set_vring_enable_op)(struct vhost_dev *dev,
                                         int enable);
typedef bool (*vhost_requires_shm_log_op)(struct vhost_dev *dev);
typedef int (*vhost_migration_done_op)(struct vhost_dev *dev,
                                       char *mac_addr);
typedef bool (*vhost_backend_can_merge_op)(struct vhost_dev *dev,
                                           uint64_t start1, uint64_t size1,
                                           uint64_t start2, uint64_t size2);
typedef int (*vhost_vsock_set_guest_cid_op)(struct vhost_dev *dev,
                                            uint64_t guest_cid);
typedef int (*vhost_vsock_set_running_op)(struct vhost_dev *dev, int start);
typedef void (*vhost_set_iotlb_callback_op)(struct vhost_dev *dev,
                                           int enabled);
typedef int (*vhost_send_device_iotlb_msg_op)(struct vhost_dev *dev,
                                              struct vhost_iotlb_msg *imsg);
typedef int (*vhost_set_config_op)(struct vhost_dev *dev, const uint8_t *data,
                                   uint32_t offset, uint32_t size,
                                   uint32_t flags);
typedef int (*vhost_get_config_op)(struct vhost_dev *dev, uint8_t *config,
                                   uint32_t config_len);

typedef int (*vhost_crypto_create_session_op)(struct vhost_dev *dev,
                                              void *session_info,
                                              uint64_t *session_id);
typedef int (*vhost_crypto_close_session_op)(struct vhost_dev *dev,
                                             uint64_t session_id);

typedef bool (*vhost_backend_mem_section_filter_op)(struct vhost_dev *dev,
                                                MemoryRegionSection *section);

typedef int (*vhost_get_inflight_fd_op)(struct vhost_dev *dev,
                                        uint16_t queue_size,
                                        struct vhost_inflight *inflight);

typedef int (*vhost_set_inflight_fd_op)(struct vhost_dev *dev,
                                        struct vhost_inflight *inflight);

typedef struct VhostOps {
    VhostBackendType backend_type;
    vhost_backend_init vhost_backend_init;
    vhost_backend_cleanup vhost_backend_cleanup;
    vhost_backend_memslots_limit vhost_backend_memslots_limit;
    vhost_net_set_backend_op vhost_net_set_backend;
    vhost_net_set_mtu_op vhost_net_set_mtu;
    vhost_scsi_set_endpoint_op vhost_scsi_set_endpoint;
    vhost_scsi_clear_endpoint_op vhost_scsi_clear_endpoint;
    vhost_scsi_get_abi_version_op vhost_scsi_get_abi_version;
    vhost_set_log_base_op vhost_set_log_base;
    vhost_set_mem_table_op vhost_set_mem_table;
    vhost_set_vring_addr_op vhost_set_vring_addr;
    vhost_set_vring_endian_op vhost_set_vring_endian;
    vhost_set_vring_num_op vhost_set_vring_num;
    vhost_set_vring_base_op vhost_set_vring_base;
    vhost_get_vring_base_op vhost_get_vring_base;
    vhost_set_vring_kick_op vhost_set_vring_kick;
    vhost_set_vring_call_op vhost_set_vring_call;
    vhost_set_vring_busyloop_timeout_op vhost_set_vring_busyloop_timeout;
    vhost_set_features_op vhost_set_features;
    vhost_get_features_op vhost_get_features;
    vhost_set_owner_op vhost_set_owner;
    vhost_reset_device_op vhost_reset_device;
    vhost_get_vq_index_op vhost_get_vq_index;
    vhost_set_vring_enable_op vhost_set_vring_enable;
    vhost_requires_shm_log_op vhost_requires_shm_log;
    vhost_migration_done_op vhost_migration_done;
    vhost_backend_can_merge_op vhost_backend_can_merge;
    vhost_vsock_set_guest_cid_op vhost_vsock_set_guest_cid;
    vhost_vsock_set_running_op vhost_vsock_set_running;
    vhost_set_iotlb_callback_op vhost_set_iotlb_callback;
    vhost_send_device_iotlb_msg_op vhost_send_device_iotlb_msg;
    vhost_get_config_op vhost_get_config;
    vhost_set_config_op vhost_set_config;
    vhost_crypto_create_session_op vhost_crypto_create_session;
    vhost_crypto_close_session_op vhost_crypto_close_session;
    vhost_backend_mem_section_filter_op vhost_backend_mem_section_filter;
    vhost_get_inflight_fd_op vhost_get_inflight_fd;
    vhost_set_inflight_fd_op vhost_set_inflight_fd;
} VhostOps;

extern const VhostOps user_ops;

int vhost_set_backend_type(struct vhost_dev *dev,
                           VhostBackendType backend_type);

int vhost_backend_update_device_iotlb(struct vhost_dev *dev,
                                             uint64_t iova, uint64_t uaddr,
                                             uint64_t len,
                                             IOMMUAccessFlags perm);

int vhost_backend_invalidate_device_iotlb(struct vhost_dev *dev,
                                                 uint64_t iova, uint64_t len);

int vhost_backend_handle_iotlb_msg(struct vhost_dev *dev,
                                          struct vhost_iotlb_msg *imsg);

int vhost_user_gpu_set_socket(struct vhost_dev *dev, int fd);

#endif /* VHOST_BACKEND_H */
