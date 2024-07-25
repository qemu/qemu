#include "qemu/osdep.h"

#include "standard-headers/linux/virtio_blk.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-blk-common.h"
#include "hw/virtio/virtio-blk-spdm.h"


libspdm_return_t vblk_spdm_acquire_buffer(void *context, void **msg_buf_ptr)
{
    SpdmDev *spdm_dev = container_of(context, SpdmDev, spdm_context);

    LIBSPDM_ASSERT(!spdm_dev->sender_receiver_buffer_acquired);
    *msg_buf_ptr = spdm_dev->sender_receiver_buffer;
    libspdm_zero_mem (spdm_dev->sender_receiver_buffer, sizeof(sender_receiver_buffer));
    spdm_dev->sender_receiver_buffer_acquired = true;
    
    return LIBSPDM_STATUS_SUCCESS;
}

void vblk_spdm_release_buffer(void *context, const void* msg_buf_ptr)
{
    SpdmDev *spdm_dev = container_of(context, SpdmDev, spdm_context);

    LIBSPDM_ASSERT(spdm_dev->sender_receiver_buffer_acquired);
    LIBSPDM_ASSERT(msg_buf_ptr == spdm_dev->sender_receiver_buffer);
    g_free(msg_buf_ptr);
    spdm_dev->sender_receiver_buffer_acquired = false;

    return;
}

/*
libspdm_return_t vblk_spdm_send_message(void *spdm_context,
                                        size_t response_size,
                                        const void *response,
                                        uint64_t timeout);


libspdm_return_t vblk_spdm_receive_message(void *spdm_context,
                                           size_t *request_size,
                                           void **request,
                                           uint64_t timeout);

//*/

static const SpdmIO vblk_spdm_buffer_io = {
    .spdm_device_acquire_sender_buffer = vblk_spdm_acquire_buffer;
    .spdm_device_acquire_sender_buffer = vblk_spdm_acquire_buffer;
    .spdm_device_release_receiver_buffer = vblk_spdm_release_buffer;
    .spdm_device_release_receiver_buffer = vblk_spdm_release_buffer;
}

void *vblk_init_spdm_dev(VirtIOBlock *s)
{
    SpdmDev *spdm_dev = s->spdm_dev;
    
    spdm_dev->spdm_buffer_io = vblk_spdm_buffer_io;
}
//*/
