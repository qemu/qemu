#ifndef VIRTIO_BLK_SPDM_H
#define VIRTIO_BLK_SPDM_H 

#include "qemu/osdep.h"
#include "standard-headers/linux/virtio_blk.h"
#include "sysemu/iothread.h"
#include "qemu/error-report.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-blk.h"
#include "hw/virtio/virtio-blk-common.h"

extern QemuMutex m_spdm_mutex;

#define VIRTIO_BLK_T_SPDM 28
#define VIRTIO_BLK_T_SPDM_APP 30

libspdm_return_t vblk_spdm_acquire_buffer(void *context, void **msg_buf_ptr);
void vblk_spdm_release_buffer(void *context, const void* msg_buf_ptr);
libspdm_return_t vblk_spdm_send_message(void *spdm_context,
                                        size_t response_size,
                                        const void *response,
                                        uint64_t timeout);
libspdm_return_t vblk_spdm_receive_message(void *spdm_context,
                                           size_t *request_size,
                                           void **request,
                                           uint64_t timeout);
void vblk_init_spdm_dev(VirtIOBlock *s);
void *vblk_spdm_io_thread(void *opaque);
void vblk_spdm_connection_state_callback(void *spdm_context,
                                         libspdm_connection_state_t connection_state);
void vblk_spdm_fix_internal_seqno(libspdm_context_t *spdm_context, uint8_t *msg_buffer);
#endif
