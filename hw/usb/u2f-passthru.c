/*
 * U2F USB Passthru device.
 *
 * Copyright (c) 2020 César Belley <cesar.belley@lse.epita.fr>
 * Written by César Belley <cesar.belley@lse.epita.fr>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/main-loop.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/usb.h"
#include "migration/vmstate.h"

#include "u2f.h"

#ifdef CONFIG_LIBUDEV
#include <libudev.h>
#endif
#include <linux/hidraw.h>
#include <sys/ioctl.h>

#define NONCE_SIZE 8
#define BROADCAST_CID 0xFFFFFFFF
#define TRANSACTION_TIMEOUT 120000

struct transaction {
    uint32_t cid;
    uint16_t resp_bcnt;
    uint16_t resp_size;

    /* Nonce for broadcast isolation */
    uint8_t nonce[NONCE_SIZE];
};

typedef struct U2FPassthruState U2FPassthruState;

#define CURRENT_TRANSACTIONS_NUM 4

struct U2FPassthruState {
    U2FKeyState base;

    /* Host device */
    char *hidraw;
    int hidraw_fd;

    /* Current Transactions */
    struct transaction current_transactions[CURRENT_TRANSACTIONS_NUM];
    uint8_t current_transactions_start;
    uint8_t current_transactions_end;
    uint8_t current_transactions_num;

    /* Transaction time checking */
    int64_t last_transaction_time;
    QEMUTimer timer;
};

#define TYPE_U2F_PASSTHRU "u2f-passthru"
#define PASSTHRU_U2F_KEY(obj) \
    OBJECT_CHECK(U2FPassthruState, (obj), TYPE_U2F_PASSTHRU)

/* Init packet sizes */
#define PACKET_INIT_HEADER_SIZE 7
#define PACKET_INIT_DATA_SIZE (U2FHID_PACKET_SIZE - PACKET_INIT_HEADER_SIZE)

/* Cont packet sizes */
#define PACKET_CONT_HEADER_SIZE 5
#define PACKET_CONT_DATA_SIZE (U2FHID_PACKET_SIZE - PACKET_CONT_HEADER_SIZE)

struct packet_init {
    uint32_t cid;
    uint8_t cmd;
    uint8_t bcnth;
    uint8_t bcntl;
    uint8_t data[PACKET_INIT_DATA_SIZE];
} QEMU_PACKED;

static inline uint32_t packet_get_cid(const void *packet)
{
    return *((uint32_t *)packet);
}

static inline bool packet_is_init(const void *packet)
{
    return ((uint8_t *)packet)[4] & (1 << 7);
}

static inline uint16_t packet_init_get_bcnt(
        const struct packet_init *packet_init)
{
    uint16_t bcnt = 0;
    bcnt |= packet_init->bcnth << 8;
    bcnt |= packet_init->bcntl;

    return bcnt;
}

static void u2f_passthru_reset(U2FPassthruState *key)
{
    timer_del(&key->timer);
    qemu_set_fd_handler(key->hidraw_fd, NULL, NULL, key);
    key->last_transaction_time = 0;
    key->current_transactions_start = 0;
    key->current_transactions_end = 0;
    key->current_transactions_num = 0;
}

static void u2f_timeout_check(void *opaque)
{
    U2FPassthruState *key = opaque;
    int64_t time = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL);

    if (time > key->last_transaction_time + TRANSACTION_TIMEOUT) {
        u2f_passthru_reset(key);
    } else {
        timer_mod(&key->timer, time + TRANSACTION_TIMEOUT / 4);
    }
}

static int u2f_transaction_get_index(U2FPassthruState *key, uint32_t cid)
{
    for (int i = 0; i < key->current_transactions_num; ++i) {
        int index = (key->current_transactions_start + i)
            % CURRENT_TRANSACTIONS_NUM;
        if (cid == key->current_transactions[index].cid) {
            return index;
        }
    }
    return -1;
}

static struct transaction *u2f_transaction_get(U2FPassthruState *key,
                                               uint32_t cid)
{
    int index = u2f_transaction_get_index(key, cid);
    if (index < 0) {
        return NULL;
    }
    return &key->current_transactions[index];
}

static struct transaction *u2f_transaction_get_from_nonce(U2FPassthruState *key,
                                const uint8_t nonce[NONCE_SIZE])
{
    for (int i = 0; i < key->current_transactions_num; ++i) {
        int index = (key->current_transactions_start + i)
            % CURRENT_TRANSACTIONS_NUM;
        if (key->current_transactions[index].cid == BROADCAST_CID
            && memcmp(nonce, key->current_transactions[index].nonce,
                      NONCE_SIZE) == 0) {
            return &key->current_transactions[index];
        }
    }
    return NULL;
}

static void u2f_transaction_close(U2FPassthruState *key, uint32_t cid)
{
    int index, next_index;
    index = u2f_transaction_get_index(key, cid);
    if (index < 0) {
        return;
    }
    next_index = (index + 1) % CURRENT_TRANSACTIONS_NUM;

    /* Rearrange to ensure the oldest is at the start position */
    while (next_index != key->current_transactions_end) {
        memcpy(&key->current_transactions[index],
               &key->current_transactions[next_index],
               sizeof(struct transaction));

        index = next_index;
        next_index = (index + 1) % CURRENT_TRANSACTIONS_NUM;
    }

    key->current_transactions_end = index;
    --key->current_transactions_num;

    if (key->current_transactions_num == 0) {
        u2f_passthru_reset(key);
    }
}

static void u2f_transaction_add(U2FPassthruState *key, uint32_t cid,
                                const uint8_t nonce[NONCE_SIZE])
{
    uint8_t index;
    struct transaction *transaction;

    if (key->current_transactions_num >= CURRENT_TRANSACTIONS_NUM) {
        /* Close the oldest transaction */
        index = key->current_transactions_start;
        transaction = &key->current_transactions[index];
        u2f_transaction_close(key, transaction->cid);
    }

    /* Index */
    index = key->current_transactions_end;
    key->current_transactions_end = (index + 1) % CURRENT_TRANSACTIONS_NUM;
    ++key->current_transactions_num;

    /* Transaction */
    transaction = &key->current_transactions[index];
    transaction->cid = cid;
    transaction->resp_bcnt = 0;
    transaction->resp_size = 0;

    /* Nonce */
    if (nonce != NULL) {
        memcpy(transaction->nonce, nonce, NONCE_SIZE);
    }
}

static void u2f_passthru_read(void *opaque);

static void u2f_transaction_start(U2FPassthruState *key,
                                  const struct packet_init *packet_init)
{
    int64_t time;

    /* Transaction */
    if (packet_init->cid == BROADCAST_CID) {
        u2f_transaction_add(key, packet_init->cid, packet_init->data);
    } else {
        u2f_transaction_add(key, packet_init->cid, NULL);
    }

    /* Time */
    time = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL);
    if (key->last_transaction_time == 0) {
        qemu_set_fd_handler(key->hidraw_fd, u2f_passthru_read, NULL, key);
        timer_init_ms(&key->timer, QEMU_CLOCK_VIRTUAL, u2f_timeout_check, key);
        timer_mod(&key->timer, time + TRANSACTION_TIMEOUT / 4);
    }
    key->last_transaction_time = time;
}

static void u2f_passthru_recv_from_host(U2FPassthruState *key,
                                    const uint8_t packet[U2FHID_PACKET_SIZE])
{
    struct transaction *transaction;
    uint32_t cid;

    /* Retrieve transaction */
    cid = packet_get_cid(packet);
    if (cid == BROADCAST_CID) {
        struct packet_init *packet_init;
        if (!packet_is_init(packet)) {
            return;
        }
        packet_init = (struct packet_init *)packet;
        transaction = u2f_transaction_get_from_nonce(key, packet_init->data);
    } else {
        transaction = u2f_transaction_get(key, cid);
    }

    /* Ignore no started transaction */
    if (transaction == NULL) {
        return;
    }

    if (packet_is_init(packet)) {
        struct packet_init *packet_init = (struct packet_init *)packet;
        transaction->resp_bcnt = packet_init_get_bcnt(packet_init);
        transaction->resp_size = PACKET_INIT_DATA_SIZE;

        if (packet_init->cid == BROADCAST_CID) {
            /* Nonce checking for legitimate response */
            if (memcmp(transaction->nonce, packet_init->data, NONCE_SIZE)
                != 0) {
                return;
            }
        }
    } else {
        transaction->resp_size += PACKET_CONT_DATA_SIZE;
    }

    /* Transaction end check */
    if (transaction->resp_size >= transaction->resp_bcnt) {
        u2f_transaction_close(key, cid);
    }
    u2f_send_to_guest(&key->base, packet);
}

static void u2f_passthru_read(void *opaque)
{
    U2FPassthruState *key = opaque;
    U2FKeyState *base = &key->base;
    uint8_t packet[2 * U2FHID_PACKET_SIZE];
    int ret;

    /* Full size base queue check */
    if (base->pending_in_num >= U2FHID_PENDING_IN_NUM) {
        return;
    }

    ret = read(key->hidraw_fd, packet, sizeof(packet));
    if (ret < 0) {
        /* Detach */
        if (base->dev.attached) {
            usb_device_detach(&base->dev);
            u2f_passthru_reset(key);
        }
        return;
    }
    if (ret != U2FHID_PACKET_SIZE) {
        return;
    }
    u2f_passthru_recv_from_host(key, packet);
}

static void u2f_passthru_recv_from_guest(U2FKeyState *base,
                                    const uint8_t packet[U2FHID_PACKET_SIZE])
{
    U2FPassthruState *key = PASSTHRU_U2F_KEY(base);
    uint8_t host_packet[U2FHID_PACKET_SIZE + 1];
    ssize_t written;

    if (packet_is_init(packet)) {
        u2f_transaction_start(key, (struct packet_init *)packet);
    }

    host_packet[0] = 0;
    memcpy(host_packet + 1, packet, U2FHID_PACKET_SIZE);

    written = write(key->hidraw_fd, host_packet, sizeof(host_packet));
    if (written != sizeof(host_packet)) {
        error_report("%s: Bad written size (req 0x%zu, val 0x%zd)",
                     TYPE_U2F_PASSTHRU, sizeof(host_packet), written);
    }
}

static bool u2f_passthru_is_u2f_device(int fd)
{
    int ret, rdesc_size;
    struct hidraw_report_descriptor rdesc;
    const uint8_t u2f_hid_report_desc_header[] = {
        0x06, 0xd0, 0xf1, /* Usage Page (FIDO) */
        0x09, 0x01,       /* Usage (FIDO) */
    };

    /* Get report descriptor size */
    ret = ioctl(fd, HIDIOCGRDESCSIZE, &rdesc_size);
    if (ret < 0 || rdesc_size < sizeof(u2f_hid_report_desc_header)) {
        return false;
    }

    /* Get report descriptor */
    memset(&rdesc, 0x0, sizeof(rdesc));
    rdesc.size = rdesc_size;
    ret = ioctl(fd, HIDIOCGRDESC, &rdesc);
    if (ret < 0) {
        return false;
    }

    /* Header bytes cover specific U2F rdesc values */
    return memcmp(u2f_hid_report_desc_header, rdesc.value,
                  sizeof(u2f_hid_report_desc_header)) == 0;
}

#ifdef CONFIG_LIBUDEV
static int u2f_passthru_open_from_device(struct udev_device *device)
{
    const char *devnode = udev_device_get_devnode(device);

    int fd = qemu_open_old(devnode, O_RDWR);
    if (fd < 0) {
        return -1;
    } else if (!u2f_passthru_is_u2f_device(fd)) {
        qemu_close(fd);
        return -1;
    }
    return fd;
}

static int u2f_passthru_open_from_enumerate(struct udev *udev,
                                            struct udev_enumerate *enumerate)
{
    struct udev_list_entry *devices, *entry;
    int ret, fd;

    ret = udev_enumerate_scan_devices(enumerate);
    if (ret < 0) {
        return -1;
    }

    devices = udev_enumerate_get_list_entry(enumerate);
    udev_list_entry_foreach(entry, devices) {
        struct udev_device *device;
        const char *syspath = udev_list_entry_get_name(entry);

        if (syspath == NULL) {
            continue;
        }

        device = udev_device_new_from_syspath(udev, syspath);
        if (device == NULL) {
            continue;
        }

        fd = u2f_passthru_open_from_device(device);
        udev_device_unref(device);
        if (fd >= 0) {
            return fd;
        }
    }
    return -1;
}

static int u2f_passthru_open_from_scan(void)
{
    struct udev *udev;
    struct udev_enumerate *enumerate;
    int ret, fd = -1;

    udev = udev_new();
    if (udev == NULL) {
        return -1;
    }

    enumerate = udev_enumerate_new(udev);
    if (enumerate == NULL) {
        udev_unref(udev);
        return -1;
    }

    ret = udev_enumerate_add_match_subsystem(enumerate, "hidraw");
    if (ret >= 0) {
        fd = u2f_passthru_open_from_enumerate(udev, enumerate);
    }

    udev_enumerate_unref(enumerate);
    udev_unref(udev);

    return fd;
}
#endif

static void u2f_passthru_unrealize(U2FKeyState *base)
{
    U2FPassthruState *key = PASSTHRU_U2F_KEY(base);

    u2f_passthru_reset(key);
    qemu_close(key->hidraw_fd);
}

static void u2f_passthru_realize(U2FKeyState *base, Error **errp)
{
    U2FPassthruState *key = PASSTHRU_U2F_KEY(base);
    int fd;

    if (key->hidraw == NULL) {
#ifdef CONFIG_LIBUDEV
        fd = u2f_passthru_open_from_scan();
        if (fd < 0) {
            error_setg(errp, "%s: Failed to find a U2F USB device",
                       TYPE_U2F_PASSTHRU);
            return;
        }
#else
        error_setg(errp, "%s: Missing hidraw", TYPE_U2F_PASSTHRU);
        return;
#endif
    } else {
        fd = qemu_open_old(key->hidraw, O_RDWR);
        if (fd < 0) {
            error_setg(errp, "%s: Failed to open %s", TYPE_U2F_PASSTHRU,
                       key->hidraw);
            return;
        }

        if (!u2f_passthru_is_u2f_device(fd)) {
            qemu_close(fd);
            error_setg(errp, "%s: Passed hidraw does not represent "
                       "a U2F HID device", TYPE_U2F_PASSTHRU);
            return;
        }
    }
    key->hidraw_fd = fd;
    u2f_passthru_reset(key);
}

static int u2f_passthru_post_load(void *opaque, int version_id)
{
    U2FPassthruState *key = opaque;
    u2f_passthru_reset(key);
    return 0;
}

static const VMStateDescription u2f_passthru_vmstate = {
    .name = "u2f-key-passthru",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = u2f_passthru_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_U2F_KEY(base, U2FPassthruState),
        VMSTATE_END_OF_LIST()
    }
};

static Property u2f_passthru_properties[] = {
    DEFINE_PROP_STRING("hidraw", U2FPassthruState, hidraw),
    DEFINE_PROP_END_OF_LIST(),
};

static void u2f_passthru_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    U2FKeyClass *kc = U2F_KEY_CLASS(klass);

    kc->realize = u2f_passthru_realize;
    kc->unrealize = u2f_passthru_unrealize;
    kc->recv_from_guest = u2f_passthru_recv_from_guest;
    dc->desc = "QEMU U2F passthrough key";
    dc->vmsd = &u2f_passthru_vmstate;
    device_class_set_props(dc, u2f_passthru_properties);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo u2f_key_passthru_info = {
    .name = TYPE_U2F_PASSTHRU,
    .parent = TYPE_U2F_KEY,
    .instance_size = sizeof(U2FPassthruState),
    .class_init = u2f_passthru_class_init
};

static void u2f_key_passthru_register_types(void)
{
    type_register_static(&u2f_key_passthru_info);
}

type_init(u2f_key_passthru_register_types)
