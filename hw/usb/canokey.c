/*
 * CanoKey QEMU device implementation.
 *
 * Copyright (c) 2021-2022 Canokeys.org <contact@canokeys.org>
 * Written by Hongren (Zenithal) Zheng <i@zenithal.me>
 *
 * This code is licensed under the GPL v2 or later.
 */

#include "qemu/osdep.h"
#include <canokey-qemu.h>

#include "qemu/module.h"
#include "qapi/error.h"
#include "hw/usb.h"
#include "hw/qdev-properties.h"
#include "trace.h"
#include "desc.h"
#include "canokey.h"

#define CANOKEY_EP_IN(ep) ((ep) & 0x7F)

#define CANOKEY_VENDOR_NUM     0x20a0
#define CANOKEY_PRODUCT_NUM    0x42d2

/*
 * placeholder, canokey-qemu implements its own usb desc
 * Namely we do not use usb_desc_handle_contorl
 */
enum {
    STR_MANUFACTURER = 1,
    STR_PRODUCT,
    STR_SERIALNUMBER
};

static const USBDescStrings desc_strings = {
    [STR_MANUFACTURER]     = "canokeys.org",
    [STR_PRODUCT]          = "CanoKey QEMU",
    [STR_SERIALNUMBER]     = "0"
};

static const USBDescDevice desc_device_canokey = {
    .bcdUSB                        = 0x0,
    .bMaxPacketSize0               = 16,
    .bNumConfigurations            = 0,
    .confs = NULL,
};

static const USBDesc desc_canokey = {
    .id = {
        .idVendor          = CANOKEY_VENDOR_NUM,
        .idProduct         = CANOKEY_PRODUCT_NUM,
        .bcdDevice         = 0x0100,
        .iManufacturer     = STR_MANUFACTURER,
        .iProduct          = STR_PRODUCT,
        .iSerialNumber     = STR_SERIALNUMBER,
    },
    .full = &desc_device_canokey,
    .str  = desc_strings,
};


/*
 * libcanokey-qemu.so side functions
 * All functions are called from canokey_emu_device_loop
 */
int canokey_emu_stall_ep(void *base, uint8_t ep)
{
    trace_canokey_emu_stall_ep(ep);
    CanoKeyState *key = base;
    uint8_t ep_in = CANOKEY_EP_IN(ep); /* INTR IN has ep 129 */
    key->ep_in_size[ep_in] = 0;
    key->ep_in_state[ep_in] = CANOKEY_EP_IN_STALL;
    return 0;
}

int canokey_emu_set_address(void *base, uint8_t addr)
{
    trace_canokey_emu_set_address(addr);
    CanoKeyState *key = base;
    key->dev.addr = addr;
    return 0;
}

int canokey_emu_prepare_receive(
        void *base, uint8_t ep, uint8_t *pbuf, uint16_t size)
{
    trace_canokey_emu_prepare_receive(ep, size);
    CanoKeyState *key = base;
    key->ep_out[ep] = pbuf;
    key->ep_out_size[ep] = size;
    return 0;
}

int canokey_emu_transmit(
        void *base, uint8_t ep, const uint8_t *pbuf, uint16_t size)
{
    trace_canokey_emu_transmit(ep, size);
    CanoKeyState *key = base;
    uint8_t ep_in = CANOKEY_EP_IN(ep); /* INTR IN has ep 129 */
    memcpy(key->ep_in[ep_in] + key->ep_in_size[ep_in],
            pbuf, size);
    key->ep_in_size[ep_in] += size;
    key->ep_in_state[ep_in] = CANOKEY_EP_IN_READY;
    /*
     * wake up controller if we NAKed IN token before
     * Note: this is a quirk for CanoKey CTAPHID
     */
    if (ep_in == CANOKEY_EMU_EP_CTAPHID) {
        usb_wakeup(usb_ep_get(&key->dev, USB_TOKEN_IN, ep_in), 0);
    }
    /*
     * ready for more data in device loop
     *
     * Note: this is a quirk for CanoKey CTAPHID
     * because it calls multiple emu_transmit in one device_loop
     * but w/o data_in it would stuck in device_loop
     * This has side effect for CCID since CCID can send ZLP
     * This also has side effect for Control transfer
     */
    if (ep_in == CANOKEY_EMU_EP_CTAPHID) {
        canokey_emu_data_in(ep_in);
    }
    return 0;
}

uint32_t canokey_emu_get_rx_data_size(void *base, uint8_t ep)
{
    CanoKeyState *key = base;
    return key->ep_out_size[ep];
}

/*
 * QEMU side functions
 */
static void canokey_handle_reset(USBDevice *dev)
{
    trace_canokey_handle_reset();
    CanoKeyState *key = CANOKEY(dev);
    for (int i = 0; i != CANOKEY_EP_NUM; ++i) {
        key->ep_in_state[i] = CANOKEY_EP_IN_WAIT;
        key->ep_in_pos[i] = 0;
        key->ep_in_size[i] = 0;
    }
    canokey_emu_reset();
}

static void canokey_handle_control(USBDevice *dev, USBPacket *p,
               int request, int value, int index, int length, uint8_t *data)
{
    trace_canokey_handle_control_setup(request, value, index, length);
    CanoKeyState *key = CANOKEY(dev);

    canokey_emu_setup(request, value, index, length);

    uint32_t dir_in = request & DeviceRequest;
    if (!dir_in) {
        /* OUT */
        trace_canokey_handle_control_out();
        if (key->ep_out[0] != NULL) {
            memcpy(key->ep_out[0], data, length);
        }
        canokey_emu_data_out(p->ep->nr, data);
    }

    canokey_emu_device_loop();

    /* IN */
    switch (key->ep_in_state[0]) {
    case CANOKEY_EP_IN_WAIT:
        p->status = USB_RET_NAK;
        break;
    case CANOKEY_EP_IN_STALL:
        p->status = USB_RET_STALL;
        break;
    case CANOKEY_EP_IN_READY:
        memcpy(data, key->ep_in[0], key->ep_in_size[0]);
        p->actual_length = key->ep_in_size[0];
        trace_canokey_handle_control_in(p->actual_length);
        /* reset state */
        key->ep_in_state[0] = CANOKEY_EP_IN_WAIT;
        key->ep_in_size[0] = 0;
        key->ep_in_pos[0] = 0;
        break;
    }
}

static void canokey_handle_data(USBDevice *dev, USBPacket *p)
{
    CanoKeyState *key = CANOKEY(dev);

    uint8_t ep_in = CANOKEY_EP_IN(p->ep->nr);
    uint8_t ep_out = p->ep->nr;
    uint32_t in_len;
    uint32_t out_pos;
    uint32_t out_len;
    switch (p->pid) {
    case USB_TOKEN_OUT:
        trace_canokey_handle_data_out(ep_out, p->iov.size);
        out_pos = 0;
        /* segment packet into (possibly multiple) ep_out */
        while (out_pos != p->iov.size) {
            /*
             * key->ep_out[ep_out] set by prepare_receive
             * to be a buffer inside libcanokey-qemu.so
             * key->ep_out_size[ep_out] set by prepare_receive
             * to be the buffer length
             */
            out_len = MIN(p->iov.size - out_pos, key->ep_out_size[ep_out]);
            /* usb_packet_copy would update the pos offset internally */
            usb_packet_copy(p, key->ep_out[ep_out], out_len);
            out_pos += out_len;
            /* update ep_out_size to actual len */
            key->ep_out_size[ep_out] = out_len;
            canokey_emu_data_out(ep_out, NULL);
        }
        /*
         * Note: this is a quirk for CanoKey CTAPHID
         *
         * There is one code path that uses this device loop
         * INTR IN -> useful data_in and useless device_loop -> NAKed
         * INTR OUT -> useful device loop -> transmit -> wakeup
         *   (useful thanks to both data_in and data_out having been called)
         * the next INTR IN -> actual data to guest
         *
         * if there is no such device loop, there would be no further
         * INTR IN, no device loop, no transmit hence no usb_wakeup
         * then qemu would hang
         */
        if (ep_in == CANOKEY_EMU_EP_CTAPHID) {
            canokey_emu_device_loop(); /* may call transmit multiple times */
        }
        break;
    case USB_TOKEN_IN:
        if (key->ep_in_pos[ep_in] == 0) { /* first time IN */
            canokey_emu_data_in(ep_in);
            canokey_emu_device_loop(); /* may call transmit multiple times */
        }
        switch (key->ep_in_state[ep_in]) {
        case CANOKEY_EP_IN_WAIT:
            /* NAK for early INTR IN */
            p->status = USB_RET_NAK;
            break;
        case CANOKEY_EP_IN_STALL:
            p->status = USB_RET_STALL;
            break;
        case CANOKEY_EP_IN_READY:
            /* submit part of ep_in buffer to USBPacket */
            in_len = MIN(key->ep_in_size[ep_in] - key->ep_in_pos[ep_in],
                    p->iov.size);
            usb_packet_copy(p,
                    key->ep_in[ep_in] + key->ep_in_pos[ep_in], in_len);
            key->ep_in_pos[ep_in] += in_len;
            /* reset state if all data submitted */
            if (key->ep_in_pos[ep_in] == key->ep_in_size[ep_in]) {
                key->ep_in_state[ep_in] = CANOKEY_EP_IN_WAIT;
                key->ep_in_size[ep_in] = 0;
                key->ep_in_pos[ep_in] = 0;
            }
            trace_canokey_handle_data_in(ep_in, in_len);
            break;
        }
        break;
    default:
        p->status = USB_RET_STALL;
        break;
    }
}

static void canokey_realize(USBDevice *base, Error **errp)
{
    trace_canokey_realize();
    CanoKeyState *key = CANOKEY(base);

    if (key->file == NULL) {
        error_setg(errp, "You must provide file=/path/to/canokey-file");
        return;
    }

    usb_desc_init(base);

    for (int i = 0; i != CANOKEY_EP_NUM; ++i) {
        key->ep_in_state[i] = CANOKEY_EP_IN_WAIT;
        key->ep_in_size[i] = 0;
        key->ep_in_pos[i] = 0;
    }

    if (canokey_emu_init(key, key->file)) {
        error_setg(errp, "canokey can not create or read %s", key->file);
        return;
    }
}

static void canokey_unrealize(USBDevice *base)
{
    trace_canokey_unrealize();
}

static const Property canokey_properties[] = {
    DEFINE_PROP_STRING("file", CanoKeyState, file),
};

static void canokey_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);

    uc->product_desc   = "CanoKey QEMU";
    uc->usb_desc       = &desc_canokey;
    uc->handle_reset   = canokey_handle_reset;
    uc->handle_control = canokey_handle_control;
    uc->handle_data    = canokey_handle_data;
    uc->handle_attach  = usb_desc_attach;
    uc->realize        = canokey_realize;
    uc->unrealize      = canokey_unrealize;
    dc->desc           = "CanoKey QEMU";
    device_class_set_props(dc, canokey_properties);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo canokey_info = {
    .name = TYPE_CANOKEY,
    .parent = TYPE_USB_DEVICE,
    .instance_size = sizeof(CanoKeyState),
    .class_init = canokey_class_init
};

static void canokey_register_types(void)
{
    type_register_static(&canokey_info);
}

type_init(canokey_register_types)
