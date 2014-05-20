/*
 * Media Transfer Protocol implementation, backed by host filesystem.
 *
 * Copyright Red Hat, Inc 2014
 *
 * Author:
 *   Gerd Hoffmann <kraxel@redhat.com>
 *
 * This code is licensed under the GPL v2 or later.
 */

#include <wchar.h>
#include <dirent.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/statvfs.h>

#include "qemu-common.h"
#include "qemu/iov.h"
#include "trace.h"
#include "hw/usb.h"
#include "hw/usb/desc.h"

/* ----------------------------------------------------------------------- */

enum mtp_container_type {
    TYPE_COMMAND  = 1,
    TYPE_DATA     = 2,
    TYPE_RESPONSE = 3,
    TYPE_EVENT    = 4,
};

enum mtp_code {
    /* command codes */
    CMD_GET_DEVICE_INFO            = 0x1001,
    CMD_OPEN_SESSION               = 0x1002,
    CMD_CLOSE_SESSION              = 0x1003,
    CMD_GET_STORAGE_IDS            = 0x1004,
    CMD_GET_STORAGE_INFO           = 0x1005,
    CMD_GET_NUM_OBJECTS            = 0x1006,
    CMD_GET_OBJECT_HANDLES         = 0x1007,
    CMD_GET_OBJECT_INFO            = 0x1008,
    CMD_GET_OBJECT                 = 0x1009,
    CMD_GET_PARTIAL_OBJECT         = 0x101b,

    /* response codes */
    RES_OK                         = 0x2001,
    RES_SESSION_NOT_OPEN           = 0x2003,
    RES_INVALID_TRANSACTION_ID     = 0x2004,
    RES_OPERATION_NOT_SUPPORTED    = 0x2005,
    RES_PARAMETER_NOT_SUPPORTED    = 0x2006,
    RES_INCOMPLETE_TRANSFER        = 0x2007,
    RES_INVALID_STORAGE_ID         = 0x2008,
    RES_INVALID_OBJECT_HANDLE      = 0x2009,
    RES_SPEC_BY_FORMAT_UNSUPPORTED = 0x2014,
    RES_INVALID_PARENT_OBJECT      = 0x201a,
    RES_INVALID_PARAMETER          = 0x201d,
    RES_SESSION_ALREADY_OPEN       = 0x201e,

    /* format codes */
    FMT_UNDEFINED_OBJECT           = 0x3000,
    FMT_ASSOCIATION                = 0x3001,
};

typedef struct {
    uint32_t length;
    uint16_t type;
    uint16_t code;
    uint32_t trans;
} QEMU_PACKED mtp_container;

/* ----------------------------------------------------------------------- */

typedef struct MTPState MTPState;
typedef struct MTPControl MTPControl;
typedef struct MTPData MTPData;
typedef struct MTPObject MTPObject;

enum {
    EP_DATA_IN = 1,
    EP_DATA_OUT,
    EP_EVENT,
};

struct MTPControl {
    uint16_t     code;
    uint32_t     trans;
    int          argc;
    uint32_t     argv[5];
};

struct MTPData {
    uint16_t     code;
    uint32_t     trans;
    uint32_t     offset;
    uint32_t     length;
    uint32_t     alloc;
    uint8_t      *data;
    bool         first;
    int          fd;
};

struct MTPObject {
    uint32_t     handle;
    uint16_t     format;
    char         *name;
    char         *path;
    struct stat  stat;
    MTPObject    *parent;
    MTPObject    **children;
    uint32_t     nchildren;
    bool         have_children;
    QTAILQ_ENTRY(MTPObject) next;
};

struct MTPState {
    USBDevice    dev;
    char         *root;
    char         *desc;
    uint32_t     flags;

    MTPData      *data_in;
    MTPData      *data_out;
    MTPControl   *result;
    uint32_t     session;
    uint32_t     next_handle;

    QTAILQ_HEAD(, MTPObject) objects;
};

#define QEMU_STORAGE_ID 0x00010001

#define MTP_FLAG_WRITABLE 0

#define FLAG_SET(_mtp, _flag)  ((_mtp)->flags & (1 << (_flag)))

/* ----------------------------------------------------------------------- */

#define MTP_MANUFACTURER  "QEMU"
#define MTP_PRODUCT       "QEMU filesharing"

enum {
    STR_MANUFACTURER = 1,
    STR_PRODUCT,
    STR_SERIALNUMBER,
    STR_CONFIG_FULL,
    STR_CONFIG_HIGH,
    STR_CONFIG_SUPER,
};

static const USBDescStrings desc_strings = {
    [STR_MANUFACTURER] = MTP_MANUFACTURER,
    [STR_PRODUCT]      = MTP_PRODUCT,
    [STR_SERIALNUMBER] = "34617",
    [STR_CONFIG_FULL]  = "Full speed config (usb 1.1)",
    [STR_CONFIG_HIGH]  = "High speed config (usb 2.0)",
    [STR_CONFIG_SUPER] = "Super speed config (usb 3.0)",
};

static const USBDescIface desc_iface_full = {
    .bInterfaceNumber              = 0,
    .bNumEndpoints                 = 3,
    .bInterfaceClass               = USB_CLASS_STILL_IMAGE,
    .bInterfaceSubClass            = 0x01,
    .bInterfaceProtocol            = 0x01,
    .eps = (USBDescEndpoint[]) {
        {
            .bEndpointAddress      = USB_DIR_IN | EP_DATA_IN,
            .bmAttributes          = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize        = 64,
        },{
            .bEndpointAddress      = USB_DIR_OUT | EP_DATA_OUT,
            .bmAttributes          = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize        = 64,
        },{
            .bEndpointAddress      = USB_DIR_IN | EP_EVENT,
            .bmAttributes          = USB_ENDPOINT_XFER_INT,
            .wMaxPacketSize        = 8,
            .bInterval             = 0x0a,
        },
    }
};

static const USBDescDevice desc_device_full = {
    .bcdUSB                        = 0x0200,
    .bMaxPacketSize0               = 8,
    .bNumConfigurations            = 1,
    .confs = (USBDescConfig[]) {
        {
            .bNumInterfaces        = 1,
            .bConfigurationValue   = 1,
            .iConfiguration        = STR_CONFIG_FULL,
            .bmAttributes          = USB_CFG_ATT_ONE | USB_CFG_ATT_WAKEUP,
            .bMaxPower             = 2,
            .nif = 1,
            .ifs = &desc_iface_full,
        },
    },
};

static const USBDescIface desc_iface_high = {
    .bInterfaceNumber              = 0,
    .bNumEndpoints                 = 3,
    .bInterfaceClass               = USB_CLASS_STILL_IMAGE,
    .bInterfaceSubClass            = 0x01,
    .bInterfaceProtocol            = 0x01,
    .eps = (USBDescEndpoint[]) {
        {
            .bEndpointAddress      = USB_DIR_IN | EP_DATA_IN,
            .bmAttributes          = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize        = 512,
        },{
            .bEndpointAddress      = USB_DIR_OUT | EP_DATA_OUT,
            .bmAttributes          = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize        = 512,
        },{
            .bEndpointAddress      = USB_DIR_IN | EP_EVENT,
            .bmAttributes          = USB_ENDPOINT_XFER_INT,
            .wMaxPacketSize        = 8,
            .bInterval             = 0x0a,
        },
    }
};

static const USBDescDevice desc_device_high = {
    .bcdUSB                        = 0x0200,
    .bMaxPacketSize0               = 64,
    .bNumConfigurations            = 1,
    .confs = (USBDescConfig[]) {
        {
            .bNumInterfaces        = 1,
            .bConfigurationValue   = 1,
            .iConfiguration        = STR_CONFIG_HIGH,
            .bmAttributes          = USB_CFG_ATT_ONE | USB_CFG_ATT_WAKEUP,
            .bMaxPower             = 2,
            .nif = 1,
            .ifs = &desc_iface_high,
        },
    },
};

static const USBDescMSOS desc_msos = {
    .CompatibleID = "MTP",
    .SelectiveSuspendEnabled = true,
};

static const USBDesc desc = {
    .id = {
        .idVendor          = 0x46f4, /* CRC16() of "QEMU" */
        .idProduct         = 0x0004,
        .bcdDevice         = 0,
        .iManufacturer     = STR_MANUFACTURER,
        .iProduct          = STR_PRODUCT,
        .iSerialNumber     = STR_SERIALNUMBER,
    },
    .full  = &desc_device_full,
    .high  = &desc_device_high,
    .str   = desc_strings,
    .msos  = &desc_msos,
};

/* ----------------------------------------------------------------------- */

static MTPObject *usb_mtp_object_alloc(MTPState *s, uint32_t handle,
                                       MTPObject *parent, char *name)
{
    MTPObject *o = g_new0(MTPObject, 1);

    if (name[0] == '.') {
        goto ignore;
    }

    o->handle = handle;
    o->parent = parent;
    o->name = g_strdup(name);
    if (parent == NULL) {
        o->path = g_strdup(name);
    } else {
        o->path = g_strdup_printf("%s/%s", parent->path, name);
    }

    if (lstat(o->path, &o->stat) != 0) {
        goto ignore;
    }
    if (S_ISREG(o->stat.st_mode)) {
        o->format = FMT_UNDEFINED_OBJECT;
    } else if (S_ISDIR(o->stat.st_mode)) {
        o->format = FMT_ASSOCIATION;
    } else {
        goto ignore;
    }

    if (access(o->path, R_OK) != 0) {
        goto ignore;
    }

    trace_usb_mtp_object_alloc(s->dev.addr, o->handle, o->path);

    QTAILQ_INSERT_TAIL(&s->objects, o, next);
    return o;

ignore:
    g_free(o->name);
    g_free(o->path);
    g_free(o);
    return NULL;
}

static void usb_mtp_object_free(MTPState *s, MTPObject *o)
{
    int i;

    trace_usb_mtp_object_free(s->dev.addr, o->handle, o->path);

    QTAILQ_REMOVE(&s->objects, o, next);
    for (i = 0; i < o->nchildren; i++) {
        usb_mtp_object_free(s, o->children[i]);
    }
    g_free(o->children);
    g_free(o->name);
    g_free(o->path);
    g_free(o);
}

static MTPObject *usb_mtp_object_lookup(MTPState *s, uint32_t handle)
{
    MTPObject *o;

    QTAILQ_FOREACH(o, &s->objects, next) {
        if (o->handle == handle) {
            return o;
        }
    }
    return NULL;
}

static void usb_mtp_object_readdir(MTPState *s, MTPObject *o)
{
    struct dirent *entry;
    DIR *dir;

    if (o->have_children) {
        return;
    }
    o->have_children = true;

    dir = opendir(o->path);
    if (!dir) {
        return;
    }
    while ((entry = readdir(dir)) != NULL) {
        if ((o->nchildren % 32) == 0) {
            o->children = g_realloc(o->children,
                                    (o->nchildren + 32) * sizeof(MTPObject *));
        }
        o->children[o->nchildren] =
            usb_mtp_object_alloc(s, s->next_handle++, o, entry->d_name);
        if (o->children[o->nchildren] != NULL) {
            o->nchildren++;
        }
    }
    closedir(dir);
}

/* ----------------------------------------------------------------------- */

static MTPData *usb_mtp_data_alloc(MTPControl *c)
{
    MTPData *data = g_new0(MTPData, 1);

    data->code  = c->code;
    data->trans = c->trans;
    data->fd    = -1;
    data->first = true;
    return data;
}

static void usb_mtp_data_free(MTPData *data)
{
    if (data == NULL) {
        return;
    }
    if (data->fd != -1) {
        close(data->fd);
    }
    g_free(data->data);
    g_free(data);
}

static void usb_mtp_realloc(MTPData *data, uint32_t bytes)
{
    if (data->length + bytes <= data->alloc) {
        return;
    }
    data->alloc = (data->length + bytes + 0xff) & ~0xff;
    data->data  = g_realloc(data->data, data->alloc);
}

static void usb_mtp_add_u8(MTPData *data, uint8_t val)
{
    usb_mtp_realloc(data, 1);
    data->data[data->length++] = val;
}

static void usb_mtp_add_u16(MTPData *data, uint16_t val)
{
    usb_mtp_realloc(data, 2);
    data->data[data->length++] = (val >> 0) & 0xff;
    data->data[data->length++] = (val >> 8) & 0xff;
}

static void usb_mtp_add_u32(MTPData *data, uint32_t val)
{
    usb_mtp_realloc(data, 4);
    data->data[data->length++] = (val >>  0) & 0xff;
    data->data[data->length++] = (val >>  8) & 0xff;
    data->data[data->length++] = (val >> 16) & 0xff;
    data->data[data->length++] = (val >> 24) & 0xff;
}

static void usb_mtp_add_u64(MTPData *data, uint64_t val)
{
    usb_mtp_realloc(data, 8);
    data->data[data->length++] = (val >>  0) & 0xff;
    data->data[data->length++] = (val >>  8) & 0xff;
    data->data[data->length++] = (val >> 16) & 0xff;
    data->data[data->length++] = (val >> 24) & 0xff;
    data->data[data->length++] = (val >> 32) & 0xff;
    data->data[data->length++] = (val >> 40) & 0xff;
    data->data[data->length++] = (val >> 48) & 0xff;
    data->data[data->length++] = (val >> 56) & 0xff;
}

static void usb_mtp_add_u16_array(MTPData *data, uint32_t len,
                                  const uint16_t *vals)
{
    int i;

    usb_mtp_add_u32(data, len);
    for (i = 0; i < len; i++) {
        usb_mtp_add_u16(data, vals[i]);
    }
}

static void usb_mtp_add_u32_array(MTPData *data, uint32_t len,
                                  const uint32_t *vals)
{
    int i;

    usb_mtp_add_u32(data, len);
    for (i = 0; i < len; i++) {
        usb_mtp_add_u32(data, vals[i]);
    }
}

static void usb_mtp_add_wstr(MTPData *data, const wchar_t *str)
{
    uint32_t len = wcslen(str);
    int i;

    if (len > 0) {
        len++; /* include terminating L'\0' */
    }

    usb_mtp_add_u8(data, len);
    for (i = 0; i < len; i++) {
        usb_mtp_add_u16(data, str[i]);
    }
}

static void usb_mtp_add_str(MTPData *data, const char *str)
{
    uint32_t len = strlen(str)+1;
    wchar_t wstr[len];
    size_t ret;

    ret = mbstowcs(wstr, str, len);
    if (ret == -1) {
        usb_mtp_add_wstr(data, L"Oops");
    } else {
        usb_mtp_add_wstr(data, wstr);
    }
}

static void usb_mtp_add_time(MTPData *data, time_t time)
{
    char buf[16];
    struct tm tm;

    gmtime_r(&time, &tm);
    strftime(buf, sizeof(buf), "%Y%m%dT%H%M%S", &tm);
    usb_mtp_add_str(data, buf);
}

/* ----------------------------------------------------------------------- */

static void usb_mtp_queue_result(MTPState *s, uint16_t code, uint32_t trans,
                                 int argc, uint32_t arg0, uint32_t arg1)
{
    MTPControl *c = g_new0(MTPControl, 1);

    c->code  = code;
    c->trans = trans;
    c->argc  = argc;
    if (argc > 0) {
        c->argv[0] = arg0;
    }
    if (argc > 1) {
        c->argv[1] = arg1;
    }

    assert(s->result == NULL);
    s->result = c;
}

/* ----------------------------------------------------------------------- */

static MTPData *usb_mtp_get_device_info(MTPState *s, MTPControl *c)
{
    static const uint16_t ops[] = {
        CMD_GET_DEVICE_INFO,
        CMD_OPEN_SESSION,
        CMD_CLOSE_SESSION,
        CMD_GET_STORAGE_IDS,
        CMD_GET_STORAGE_INFO,
        CMD_GET_NUM_OBJECTS,
        CMD_GET_OBJECT_HANDLES,
        CMD_GET_OBJECT_INFO,
        CMD_GET_OBJECT,
        CMD_GET_PARTIAL_OBJECT,
    };
    static const uint16_t fmt[] = {
        FMT_UNDEFINED_OBJECT,
        FMT_ASSOCIATION,
    };
    MTPData *d = usb_mtp_data_alloc(c);

    trace_usb_mtp_op_get_device_info(s->dev.addr);

    usb_mtp_add_u16(d, 100);
    usb_mtp_add_u32(d, 0xffffffff);
    usb_mtp_add_u16(d, 0x0101);
    usb_mtp_add_wstr(d, L"");
    usb_mtp_add_u16(d, 0x0000);

    usb_mtp_add_u16_array(d, ARRAY_SIZE(ops), ops);
    usb_mtp_add_u16_array(d, 0, NULL);
    usb_mtp_add_u16_array(d, 0, NULL);
    usb_mtp_add_u16_array(d, 0, NULL);
    usb_mtp_add_u16_array(d, ARRAY_SIZE(fmt), fmt);

    usb_mtp_add_wstr(d, L"" MTP_MANUFACTURER);
    usb_mtp_add_wstr(d, L"" MTP_PRODUCT);
    usb_mtp_add_wstr(d, L"0.1");
    usb_mtp_add_wstr(d, L"0123456789abcdef0123456789abcdef");

    return d;
}

static MTPData *usb_mtp_get_storage_ids(MTPState *s, MTPControl *c)
{
    static const uint32_t ids[] = {
        QEMU_STORAGE_ID,
    };
    MTPData *d = usb_mtp_data_alloc(c);

    trace_usb_mtp_op_get_storage_ids(s->dev.addr);

    usb_mtp_add_u32_array(d, ARRAY_SIZE(ids), ids);

    return d;
}

static MTPData *usb_mtp_get_storage_info(MTPState *s, MTPControl *c)
{
    MTPData *d = usb_mtp_data_alloc(c);
    struct statvfs buf;
    int rc;

    trace_usb_mtp_op_get_storage_info(s->dev.addr);

    if (FLAG_SET(s, MTP_FLAG_WRITABLE)) {
        usb_mtp_add_u16(d, 0x0003);
        usb_mtp_add_u16(d, 0x0002);
        usb_mtp_add_u16(d, 0x0000);
    } else {
        usb_mtp_add_u16(d, 0x0001);
        usb_mtp_add_u16(d, 0x0002);
        usb_mtp_add_u16(d, 0x0001);
    }

    rc = statvfs(s->root, &buf);
    if (rc == 0) {
        usb_mtp_add_u64(d, (uint64_t)buf.f_frsize * buf.f_blocks);
        usb_mtp_add_u64(d, (uint64_t)buf.f_bavail * buf.f_blocks);
        usb_mtp_add_u32(d, buf.f_ffree);
    } else {
        usb_mtp_add_u64(d, 0xffffffff);
        usb_mtp_add_u64(d, 0xffffffff);
        usb_mtp_add_u32(d, 0xffffffff);
    }

    usb_mtp_add_str(d, s->desc);
    usb_mtp_add_wstr(d, L"123456789abcdef");
    return d;
}

static MTPData *usb_mtp_get_object_handles(MTPState *s, MTPControl *c,
                                           MTPObject *o)
{
    MTPData *d = usb_mtp_data_alloc(c);
    uint32_t i, handles[o->nchildren];

    trace_usb_mtp_op_get_object_handles(s->dev.addr, o->handle, o->path);

    for (i = 0; i < o->nchildren; i++) {
        handles[i] = o->children[i]->handle;
    }
    usb_mtp_add_u32_array(d, o->nchildren, handles);

    return d;
}

static MTPData *usb_mtp_get_object_info(MTPState *s, MTPControl *c,
                                        MTPObject *o)
{
    MTPData *d = usb_mtp_data_alloc(c);

    trace_usb_mtp_op_get_object_info(s->dev.addr, o->handle, o->path);

    usb_mtp_add_u32(d, QEMU_STORAGE_ID);
    usb_mtp_add_u16(d, o->format);
    usb_mtp_add_u16(d, 0);
    usb_mtp_add_u32(d, o->stat.st_size);

    usb_mtp_add_u16(d, 0);
    usb_mtp_add_u32(d, 0);
    usb_mtp_add_u32(d, 0);
    usb_mtp_add_u32(d, 0);
    usb_mtp_add_u32(d, 0);
    usb_mtp_add_u32(d, 0);
    usb_mtp_add_u32(d, 0);

    if (o->parent) {
        usb_mtp_add_u32(d, o->parent->handle);
    } else {
        usb_mtp_add_u32(d, 0);
    }
    if (o->format == FMT_ASSOCIATION) {
        usb_mtp_add_u16(d, 0x0001);
        usb_mtp_add_u32(d, 0x00000001);
        usb_mtp_add_u32(d, 0);
    } else {
        usb_mtp_add_u16(d, 0);
        usb_mtp_add_u32(d, 0);
        usb_mtp_add_u32(d, 0);
    }

    usb_mtp_add_str(d, o->name);
    usb_mtp_add_time(d, o->stat.st_ctime);
    usb_mtp_add_time(d, o->stat.st_mtime);
    usb_mtp_add_wstr(d, L"");

    return d;
}

static MTPData *usb_mtp_get_object(MTPState *s, MTPControl *c,
                                   MTPObject *o)
{
    MTPData *d = usb_mtp_data_alloc(c);

    trace_usb_mtp_op_get_object(s->dev.addr, o->handle, o->path);

    d->fd = open(o->path, O_RDONLY);
    if (d->fd == -1) {
        usb_mtp_data_free(d);
        return NULL;
    }
    d->length = o->stat.st_size;
    d->alloc  = 512;
    d->data   = g_malloc(d->alloc);
    return d;
}

static MTPData *usb_mtp_get_partial_object(MTPState *s, MTPControl *c,
                                           MTPObject *o)
{
    MTPData *d = usb_mtp_data_alloc(c);
    off_t offset;

    trace_usb_mtp_op_get_partial_object(s->dev.addr, o->handle, o->path,
                                        c->argv[1], c->argv[2]);

    d->fd = open(o->path, O_RDONLY);
    if (d->fd == -1) {
        usb_mtp_data_free(d);
        return NULL;
    }

    offset = c->argv[1];
    if (offset > o->stat.st_size) {
        offset = o->stat.st_size;
    }
    if (lseek(d->fd, offset, SEEK_SET) < 0) {
        usb_mtp_data_free(d);
        return NULL;
    }

    d->length = c->argv[2];
    if (d->length > o->stat.st_size - offset) {
        d->length = o->stat.st_size - offset;
    }

    return d;
}

static void usb_mtp_command(MTPState *s, MTPControl *c)
{
    MTPData *data_in = NULL;
    MTPObject *o;
    uint32_t nres = 0, res0 = 0;

    /* sanity checks */
    if (c->code >= CMD_CLOSE_SESSION && s->session == 0) {
        usb_mtp_queue_result(s, RES_SESSION_NOT_OPEN,
                             c->trans, 0, 0, 0);
        return;
    }

    /* process commands */
    switch (c->code) {
    case CMD_GET_DEVICE_INFO:
        data_in = usb_mtp_get_device_info(s, c);
        break;
    case CMD_OPEN_SESSION:
        if (s->session) {
            usb_mtp_queue_result(s, RES_SESSION_ALREADY_OPEN,
                                 c->trans, 1, s->session, 0);
            return;
        }
        if (c->argv[0] == 0) {
            usb_mtp_queue_result(s, RES_INVALID_PARAMETER,
                                 c->trans, 0, 0, 0);
            return;
        }
        trace_usb_mtp_op_open_session(s->dev.addr);
        s->session = c->argv[0];
        usb_mtp_object_alloc(s, s->next_handle++, NULL, s->root);
        break;
    case CMD_CLOSE_SESSION:
        trace_usb_mtp_op_close_session(s->dev.addr);
        s->session = 0;
        s->next_handle = 0;
        usb_mtp_object_free(s, QTAILQ_FIRST(&s->objects));
        assert(QTAILQ_EMPTY(&s->objects));
        break;
    case CMD_GET_STORAGE_IDS:
        data_in = usb_mtp_get_storage_ids(s, c);
        break;
    case CMD_GET_STORAGE_INFO:
        if (c->argv[0] != QEMU_STORAGE_ID &&
            c->argv[0] != 0xffffffff) {
            usb_mtp_queue_result(s, RES_INVALID_STORAGE_ID,
                                 c->trans, 0, 0, 0);
            return;
        }
        data_in = usb_mtp_get_storage_info(s, c);
        break;
    case CMD_GET_NUM_OBJECTS:
    case CMD_GET_OBJECT_HANDLES:
        if (c->argv[0] != QEMU_STORAGE_ID &&
            c->argv[0] != 0xffffffff) {
            usb_mtp_queue_result(s, RES_INVALID_STORAGE_ID,
                                 c->trans, 0, 0, 0);
            return;
        }
        if (c->argv[1] != 0x00000000) {
            usb_mtp_queue_result(s, RES_SPEC_BY_FORMAT_UNSUPPORTED,
                                 c->trans, 0, 0, 0);
            return;
        }
        if (c->argv[2] == 0x00000000 ||
            c->argv[2] == 0xffffffff) {
            o = QTAILQ_FIRST(&s->objects);
        } else {
            o = usb_mtp_object_lookup(s, c->argv[2]);
        }
        if (o == NULL) {
            usb_mtp_queue_result(s, RES_INVALID_OBJECT_HANDLE,
                                 c->trans, 0, 0, 0);
            return;
        }
        if (o->format != FMT_ASSOCIATION) {
            usb_mtp_queue_result(s, RES_INVALID_PARENT_OBJECT,
                                 c->trans, 0, 0, 0);
            return;
        }
        usb_mtp_object_readdir(s, o);
        if (c->code == CMD_GET_NUM_OBJECTS) {
            trace_usb_mtp_op_get_num_objects(s->dev.addr, o->handle, o->path);
            nres = 1;
            res0 = o->nchildren;
        } else {
            data_in = usb_mtp_get_object_handles(s, c, o);
        }
        break;
    case CMD_GET_OBJECT_INFO:
        o = usb_mtp_object_lookup(s, c->argv[0]);
        if (o == NULL) {
            usb_mtp_queue_result(s, RES_INVALID_OBJECT_HANDLE,
                                 c->trans, 0, 0, 0);
            return;
        }
        data_in = usb_mtp_get_object_info(s, c, o);
        break;
    case CMD_GET_OBJECT:
        o = usb_mtp_object_lookup(s, c->argv[0]);
        if (o == NULL) {
            usb_mtp_queue_result(s, RES_INVALID_OBJECT_HANDLE,
                                 c->trans, 0, 0, 0);
            return;
        }
        if (o->format == FMT_ASSOCIATION) {
            usb_mtp_queue_result(s, RES_INVALID_OBJECT_HANDLE,
                                 c->trans, 0, 0, 0);
            return;
        }
        data_in = usb_mtp_get_object(s, c, o);
        if (NULL == data_in) {
            fprintf(stderr, "%s: TODO: handle error\n", __func__);
        }
        break;
    case CMD_GET_PARTIAL_OBJECT:
        o = usb_mtp_object_lookup(s, c->argv[0]);
        if (o == NULL) {
            usb_mtp_queue_result(s, RES_INVALID_OBJECT_HANDLE,
                                 c->trans, 0, 0, 0);
            return;
        }
        if (o->format == FMT_ASSOCIATION) {
            usb_mtp_queue_result(s, RES_INVALID_OBJECT_HANDLE,
                                 c->trans, 0, 0, 0);
            return;
        }
        data_in = usb_mtp_get_partial_object(s, c, o);
        if (NULL == data_in) {
            fprintf(stderr, "%s: TODO: handle error\n", __func__);
        }
        nres = 1;
        res0 = data_in->length;
        break;
    default:
        trace_usb_mtp_op_unknown(s->dev.addr, c->code);
        usb_mtp_queue_result(s, RES_OPERATION_NOT_SUPPORTED,
                             c->trans, 0, 0, 0);
        return;
    }

    /* return results on success */
    if (data_in) {
        assert(s->data_in == NULL);
        s->data_in = data_in;
    }
    usb_mtp_queue_result(s, RES_OK, c->trans, nres, res0, 0);
}

/* ----------------------------------------------------------------------- */

static void usb_mtp_handle_reset(USBDevice *dev)
{
    MTPState *s = DO_UPCAST(MTPState, dev, dev);

    trace_usb_mtp_reset(s->dev.addr);

    s->session = 0;
    usb_mtp_data_free(s->data_in);
    s->data_in = NULL;
    usb_mtp_data_free(s->data_out);
    s->data_out = NULL;
    g_free(s->result);
    s->result = NULL;
}

static void usb_mtp_handle_control(USBDevice *dev, USBPacket *p,
                                   int request, int value, int index,
                                   int length, uint8_t *data)
{
    int ret;

    ret = usb_desc_handle_control(dev, p, request, value, index, length, data);
    if (ret >= 0) {
        return;
    }

    trace_usb_mtp_stall(dev->addr, "unknown control request");
    p->status = USB_RET_STALL;
}

static void usb_mtp_cancel_packet(USBDevice *dev, USBPacket *p)
{
    /* we don't use async packets, so this should never be called */
    fprintf(stderr, "%s\n", __func__);
}

static void usb_mtp_handle_data(USBDevice *dev, USBPacket *p)
{
    MTPState *s = DO_UPCAST(MTPState, dev, dev);
    MTPControl cmd;
    mtp_container container;
    uint32_t params[5];
    int i, rc;

    switch (p->ep->nr) {
    case EP_DATA_IN:
        if (s->data_out != NULL) {
            /* guest bug */
            trace_usb_mtp_stall(s->dev.addr, "awaiting data-out");
            p->status = USB_RET_STALL;
            return;
        }
        if (p->iov.size < sizeof(container)) {
            trace_usb_mtp_stall(s->dev.addr, "packet too small");
            p->status = USB_RET_STALL;
            return;
        }
        if (s->data_in !=  NULL) {
            MTPData *d = s->data_in;
            int dlen = d->length - d->offset;
            if (d->first) {
                trace_usb_mtp_data_in(s->dev.addr, d->trans, d->length);
                container.length = cpu_to_le32(d->length + sizeof(container));
                container.type   = cpu_to_le16(TYPE_DATA);
                container.code   = cpu_to_le16(d->code);
                container.trans  = cpu_to_le32(d->trans);
                usb_packet_copy(p, &container, sizeof(container));
                d->first = false;
                if (dlen > p->iov.size - sizeof(container)) {
                    dlen = p->iov.size - sizeof(container);
                }
            } else {
                if (dlen > p->iov.size) {
                    dlen = p->iov.size;
                }
            }
            if (d->fd == -1) {
                usb_packet_copy(p, d->data + d->offset, dlen);
            } else {
                if (d->alloc < p->iov.size) {
                    d->alloc = p->iov.size;
                    d->data = g_realloc(d->data, d->alloc);
                }
                rc = read(d->fd, d->data, dlen);
                if (rc != dlen) {
                    memset(d->data, 0, dlen);
                    s->result->code = RES_INCOMPLETE_TRANSFER;
                }
                usb_packet_copy(p, d->data, dlen);
            }
            d->offset += dlen;
            if (d->offset == d->length) {
                usb_mtp_data_free(s->data_in);
                s->data_in = NULL;
            }
        } else if (s->result != NULL) {
            MTPControl *r = s->result;
            int length = sizeof(container) + r->argc * sizeof(uint32_t);
            if (r->code == RES_OK) {
                trace_usb_mtp_success(s->dev.addr, r->trans,
                                      (r->argc > 0) ? r->argv[0] : 0,
                                      (r->argc > 1) ? r->argv[1] : 0);
            } else {
                trace_usb_mtp_error(s->dev.addr, r->code, r->trans,
                                    (r->argc > 0) ? r->argv[0] : 0,
                                    (r->argc > 1) ? r->argv[1] : 0);
            }
            container.length = cpu_to_le32(length);
            container.type   = cpu_to_le16(TYPE_RESPONSE);
            container.code   = cpu_to_le16(r->code);
            container.trans  = cpu_to_le32(r->trans);
            for (i = 0; i < r->argc; i++) {
                params[i] = cpu_to_le32(r->argv[i]);
            }
            usb_packet_copy(p, &container, sizeof(container));
            usb_packet_copy(p, &params, length - sizeof(container));
            g_free(s->result);
            s->result = NULL;
        }
        break;
    case EP_DATA_OUT:
        if (p->iov.size < sizeof(container)) {
            trace_usb_mtp_stall(s->dev.addr, "packet too small");
            p->status = USB_RET_STALL;
            return;
        }
        usb_packet_copy(p, &container, sizeof(container));
        switch (le16_to_cpu(container.type)) {
        case TYPE_COMMAND:
            if (s->data_in || s->data_out || s->result) {
                trace_usb_mtp_stall(s->dev.addr, "transaction inflight");
                p->status = USB_RET_STALL;
                return;
            }
            cmd.code = le16_to_cpu(container.code);
            cmd.argc = (le32_to_cpu(container.length) - sizeof(container))
                / sizeof(uint32_t);
            cmd.trans = le32_to_cpu(container.trans);
            if (cmd.argc > ARRAY_SIZE(cmd.argv)) {
                cmd.argc = ARRAY_SIZE(cmd.argv);
            }
            if (p->iov.size < sizeof(container) + cmd.argc * sizeof(uint32_t)) {
                trace_usb_mtp_stall(s->dev.addr, "packet too small");
                p->status = USB_RET_STALL;
                return;
            }
            usb_packet_copy(p, &params, cmd.argc * sizeof(uint32_t));
            for (i = 0; i < cmd.argc; i++) {
                cmd.argv[i] = le32_to_cpu(params[i]);
            }
            trace_usb_mtp_command(s->dev.addr, cmd.code, cmd.trans,
                                  (cmd.argc > 0) ? cmd.argv[0] : 0,
                                  (cmd.argc > 1) ? cmd.argv[1] : 0,
                                  (cmd.argc > 2) ? cmd.argv[2] : 0,
                                  (cmd.argc > 3) ? cmd.argv[3] : 0,
                                  (cmd.argc > 4) ? cmd.argv[4] : 0);
            usb_mtp_command(s, &cmd);
            break;
        default:
            /* not needed as long as the mtp device is read-only */
            p->status = USB_RET_STALL;
            return;
        }
        break;
    case EP_EVENT:
        p->status = USB_RET_NAK;
        return;
    default:
        trace_usb_mtp_stall(s->dev.addr, "invalid endpoint");
        p->status = USB_RET_STALL;
        return;
    }

    if (p->actual_length == 0) {
        trace_usb_mtp_nak(s->dev.addr, p->ep->nr);
        p->status = USB_RET_NAK;
        return;
    } else {
        trace_usb_mtp_xfer(s->dev.addr, p->ep->nr, p->actual_length,
                           p->iov.size);
        return;
    }
}

static int usb_mtp_initfn(USBDevice *dev)
{
    MTPState *s = DO_UPCAST(MTPState, dev, dev);

    usb_desc_create_serial(dev);
    usb_desc_init(dev);
    QTAILQ_INIT(&s->objects);
    if (s->desc == NULL) {
        s->desc = strrchr(s->root, '/');
        if (s->desc && s->desc[0]) {
            s->desc = g_strdup(s->desc + 1);
        } else {
            s->desc = g_strdup("none");
        }
    }
    return 0;
}

static const VMStateDescription vmstate_usb_mtp = {
    .name = "usb-mtp",
    .unmigratable = 1,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_USB_DEVICE(dev, MTPState),
        VMSTATE_END_OF_LIST()
    }
};

static Property mtp_properties[] = {
    DEFINE_PROP_STRING("root", MTPState, root),
    DEFINE_PROP_STRING("desc", MTPState, desc),
    DEFINE_PROP_END_OF_LIST(),
};

static void usb_mtp_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);

    uc->init           = usb_mtp_initfn;
    uc->product_desc   = "QEMU USB MTP";
    uc->usb_desc       = &desc;
    uc->cancel_packet  = usb_mtp_cancel_packet;
    uc->handle_attach  = usb_desc_attach;
    uc->handle_reset   = usb_mtp_handle_reset;
    uc->handle_control = usb_mtp_handle_control;
    uc->handle_data    = usb_mtp_handle_data;
    dc->fw_name = "mtp";
    dc->vmsd = &vmstate_usb_mtp;
    dc->props = mtp_properties;
}

static TypeInfo mtp_info = {
    .name          = "usb-mtp",
    .parent        = TYPE_USB_DEVICE,
    .instance_size = sizeof(MTPState),
    .class_init    = usb_mtp_class_initfn,
};

static void usb_mtp_register_types(void)
{
    type_register_static(&mtp_info);
}

type_init(usb_mtp_register_types)
