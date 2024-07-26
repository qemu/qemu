#ifndef VIRTIO_BLK_SPDM_H
#define VIRTIO_BLK_SPDM_H 

#include "qemu/error-report.h"
#include "hw/virtio/virtio-blk.h"
#include "sysemu/spdm.h"

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
void *vblk_init_spdm_dev(VirtIOBlock *s);

#endif
