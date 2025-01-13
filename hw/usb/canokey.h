/*
 * CanoKey QEMU device header.
 *
 * Copyright (c) 2021-2022 Canokeys.org <contact@canokeys.org>
 * Written by Hongren (Zenithal) Zheng <i@zenithal.me>
 *
 * This code is licensed under the GPL v2 or later.
 */

#ifndef CANOKEY_H
#define CANOKEY_H

#include "hw/qdev-core.h"

#define TYPE_CANOKEY "canokey"
#define CANOKEY(obj) \
    OBJECT_CHECK(CanoKeyState, (obj), TYPE_CANOKEY)

/*
 * State of Canokey (i.e. hw/canokey.c)
 */

/* CTRL INTR BULK */
#define CANOKEY_EP_NUM 3
/* BULK/INTR IN can be up to 1352 bytes, e.g. get key info */
#define CANOKEY_EP_IN_BUFFER_SIZE 2048

typedef enum {
    CANOKEY_EP_IN_WAIT,
    CANOKEY_EP_IN_READY,
    CANOKEY_EP_IN_STALL
} CanoKeyEPState;

typedef struct CanoKeyState {
    USBDevice dev;

    /* IN packets from canokey device loop */
    uint8_t ep_in[CANOKEY_EP_NUM][CANOKEY_EP_IN_BUFFER_SIZE];
    /*
     * See canokey_emu_transmit
     *
     * For large INTR IN, receive multiple data from canokey device loop
     * in this case ep_in_size would increase with every call
     */
    uint32_t ep_in_size[CANOKEY_EP_NUM];
    /*
     * Used in canokey_handle_data
     * for IN larger than p->iov.size, we would do multiple handle_data()
     *
     * The difference between ep_in_pos and ep_in_size:
     * We first increase ep_in_size to fill ep_in buffer in device_loop,
     * then use ep_in_pos to submit data from ep_in buffer in handle_data
     */
    uint32_t ep_in_pos[CANOKEY_EP_NUM];
    CanoKeyEPState ep_in_state[CANOKEY_EP_NUM];

    /* OUT pointer to canokey recv buffer */
    uint8_t *ep_out[CANOKEY_EP_NUM];
    uint32_t ep_out_size[CANOKEY_EP_NUM];

    /* Properties */
    char *file; /* canokey-file */
} CanoKeyState;

#endif /* CANOKEY_H */
