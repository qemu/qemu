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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include <wchar.h>
#include <dirent.h>
#include <glib/gstdio.h>
#include <sys/statvfs.h>


#include "qemu/iov.h"
#include "qemu/module.h"
#include "qemu/filemonitor.h"
#include "trace.h"
#include "hw/qdev-properties.h"
#include "hw/usb.h"
#include "migration/vmstate.h"
#include "desc.h"
#include "qemu/units.h"
#include "qom/object.h"

/* ----------------------------------------------------------------------- */

enum mtp_container_type {
    TYPE_COMMAND  = 1,
    TYPE_DATA     = 2,
    TYPE_RESPONSE = 3,
    TYPE_EVENT    = 4,
};

/* MTP write stage, for internal use only */
enum mtp_write_status {
    WRITE_START    = 1,
    WRITE_CONTINUE = 2,
    WRITE_END      = 3,
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
    CMD_DELETE_OBJECT              = 0x100b,
    CMD_SEND_OBJECT_INFO           = 0x100c,
    CMD_SEND_OBJECT                = 0x100d,
    CMD_GET_PARTIAL_OBJECT         = 0x101b,
    CMD_GET_OBJECT_PROPS_SUPPORTED = 0x9801,
    CMD_GET_OBJECT_PROP_DESC       = 0x9802,
    CMD_GET_OBJECT_PROP_VALUE      = 0x9803,

    /* response codes */
    RES_OK                         = 0x2001,
    RES_GENERAL_ERROR              = 0x2002,
    RES_SESSION_NOT_OPEN           = 0x2003,
    RES_INVALID_TRANSACTION_ID     = 0x2004,
    RES_OPERATION_NOT_SUPPORTED    = 0x2005,
    RES_PARAMETER_NOT_SUPPORTED    = 0x2006,
    RES_INCOMPLETE_TRANSFER        = 0x2007,
    RES_INVALID_STORAGE_ID         = 0x2008,
    RES_INVALID_OBJECT_HANDLE      = 0x2009,
    RES_INVALID_OBJECT_FORMAT_CODE = 0x200b,
    RES_STORE_FULL                 = 0x200c,
    RES_STORE_READ_ONLY            = 0x200e,
    RES_PARTIAL_DELETE             = 0x2012,
    RES_STORE_NOT_AVAILABLE        = 0x2013,
    RES_SPEC_BY_FORMAT_UNSUPPORTED = 0x2014,
    RES_INVALID_OBJECTINFO         = 0x2015,
    RES_DESTINATION_UNSUPPORTED    = 0x2020,
    RES_INVALID_PARENT_OBJECT      = 0x201a,
    RES_INVALID_PARAMETER          = 0x201d,
    RES_SESSION_ALREADY_OPEN       = 0x201e,
    RES_INVALID_OBJECT_PROP_CODE   = 0xA801,

    /* format codes */
    FMT_UNDEFINED_OBJECT           = 0x3000,
    FMT_ASSOCIATION                = 0x3001,

    /* event codes */
    EVT_CANCEL_TRANSACTION         = 0x4001,
    EVT_OBJ_ADDED                  = 0x4002,
    EVT_OBJ_REMOVED                = 0x4003,
    EVT_OBJ_INFO_CHANGED           = 0x4007,

    /* object properties */
    PROP_STORAGE_ID                = 0xDC01,
    PROP_OBJECT_FORMAT             = 0xDC02,
    PROP_OBJECT_COMPRESSED_SIZE    = 0xDC04,
    PROP_PARENT_OBJECT             = 0xDC0B,
    PROP_PERSISTENT_UNIQUE_OBJECT_IDENTIFIER = 0xDC41,
    PROP_NAME                      = 0xDC44,
};

enum mtp_data_type {
    DATA_TYPE_UINT16  = 0x0004,
    DATA_TYPE_UINT32  = 0x0006,
    DATA_TYPE_UINT64  = 0x0008,
    DATA_TYPE_UINT128 = 0x000a,
    DATA_TYPE_STRING  = 0xffff,
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

typedef struct MTPMonEntry MTPMonEntry;

struct MTPMonEntry {
    uint32_t event;
    uint32_t handle;

    QTAILQ_ENTRY(MTPMonEntry) next;
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
    uint64_t     offset;
    uint64_t     length;
    uint64_t     alloc;
    uint8_t      *data;
    bool         first;
    /* Used for >4G file sizes */
    bool         pending;
    int          fd;
    uint8_t      write_status;
    /* Internal pointer per every MTP_WRITE_BUF_SZ */
    uint64_t     data_offset;
};

struct MTPObject {
    uint32_t     handle;
    uint16_t     format;
    char         *name;
    char         *path;
    struct stat  stat;
    /* file monitor watch id */
    int64_t      watchid;
    MTPObject    *parent;
    uint32_t     nchildren;
    QLIST_HEAD(, MTPObject) children;
    QLIST_ENTRY(MTPObject) list;
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
    bool         readonly;

    QTAILQ_HEAD(, MTPObject) objects;
    QFileMonitor *file_monitor;
    QTAILQ_HEAD(, MTPMonEntry) events;
    /* Responder is expecting a write operation */
    bool write_pending;
    struct {
        uint32_t parent_handle;
        uint16_t format;
        uint32_t size;
        char *filename;
    } dataset;
};

/*
 * ObjectInfo dataset received from initiator
 * Fields we don't care about are ignored
 */
typedef struct {
    uint32_t storage_id; /*unused*/
    uint16_t format;
    uint16_t protection_status; /*unused*/
    uint32_t size;
    uint16_t thumb_format; /*unused*/
    uint32_t thumb_comp_sz; /*unused*/
    uint32_t thumb_pix_width; /*unused*/
    uint32_t thumb_pix_height; /*unused*/
    uint32_t image_pix_width; /*unused*/
    uint32_t image_pix_height; /*unused*/
    uint32_t image_bit_depth; /*unused*/
    uint32_t parent; /*unused*/
    uint16_t assoc_type;
    uint32_t assoc_desc;
    uint32_t seq_no; /*unused*/
    uint8_t length; /*part of filename field*/
    uint8_t filename[0]; /* UTF-16 encoded */
    char date_created[0]; /*unused*/
    char date_modified[0]; /*unused*/
    char keywords[0]; /*unused*/
    /* string and other data follows */
} QEMU_PACKED ObjectInfo;

#define TYPE_USB_MTP "usb-mtp"
OBJECT_DECLARE_SIMPLE_TYPE(MTPState, USB_MTP)

#define QEMU_STORAGE_ID 0x00010001

#define MTP_FLAG_WRITABLE 0

#define FLAG_SET(_mtp, _flag)  ((_mtp)->flags & (1 << (_flag)))

/* ----------------------------------------------------------------------- */

#define MTP_MANUFACTURER  "QEMU"
#define MTP_PRODUCT       "QEMU filesharing"
#define MTP_WRITE_BUF_SZ  (512 * KiB)

enum {
    STR_MANUFACTURER = 1,
    STR_PRODUCT,
    STR_SERIALNUMBER,
    STR_MTP,
    STR_CONFIG_FULL,
    STR_CONFIG_HIGH,
    STR_CONFIG_SUPER,
};

static const USBDescStrings desc_strings = {
    [STR_MANUFACTURER] = MTP_MANUFACTURER,
    [STR_PRODUCT]      = MTP_PRODUCT,
    [STR_SERIALNUMBER] = "34617",
    [STR_MTP]          = "MTP",
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
    .iInterface                    = STR_MTP,
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
            .wMaxPacketSize        = 64,
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
    .iInterface                    = STR_MTP,
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
            .wMaxPacketSize        = 64,
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
                                       MTPObject *parent, const char *name)
{
    MTPObject *o = g_new0(MTPObject, 1);

    if (name[0] == '.') {
        goto ignore;
    }

    o->watchid = -1;
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
    MTPObject *iter;

    if (!o) {
        return;
    }

    trace_usb_mtp_object_free(s->dev.addr, o->handle, o->path);

    if (o->watchid != -1 && s->file_monitor) {
        qemu_file_monitor_remove_watch(s->file_monitor, o->path, o->watchid);
    }

    QTAILQ_REMOVE(&s->objects, o, next);
    if (o->parent) {
        QLIST_REMOVE(o, list);
        o->parent->nchildren--;
    }

    while (!QLIST_EMPTY(&o->children)) {
        iter = QLIST_FIRST(&o->children);
        usb_mtp_object_free(s, iter);
    }
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

static MTPObject *usb_mtp_add_child(MTPState *s, MTPObject *o,
                                    const char *name)
{
    MTPObject *child =
        usb_mtp_object_alloc(s, s->next_handle++, o, name);

    if (child) {
        trace_usb_mtp_add_child(s->dev.addr, child->handle, child->path);
        QLIST_INSERT_HEAD(&o->children, child, list);
        o->nchildren++;

        if (child->format == FMT_ASSOCIATION) {
            QLIST_INIT(&child->children);
        }
    }

    return child;
}

static MTPObject *usb_mtp_object_lookup_name(MTPObject *parent,
                                             const char *name, int len)
{
    MTPObject *iter;

    if (len == -1) {
        len = strlen(name);
    }

    QLIST_FOREACH(iter, &parent->children, list) {
        if (strncmp(iter->name, name, len) == 0) {
            return iter;
        }
    }

    return NULL;
}

static MTPObject *usb_mtp_object_lookup_id(MTPState *s, int64_t id)
{
    MTPObject *iter;

    QTAILQ_FOREACH(iter, &s->objects, next) {
        if (iter->watchid == id) {
            return iter;
        }
    }

    return NULL;
}

static void file_monitor_event(int64_t id,
                               QFileMonitorEvent ev,
                               const char *name,
                               void *opaque)
{
    MTPState *s = opaque;
    MTPObject *parent = usb_mtp_object_lookup_id(s, id);
    MTPMonEntry *entry = NULL;
    MTPObject *o;

    if (!parent) {
        return;
    }

    switch (ev) {
    case QFILE_MONITOR_EVENT_CREATED:
        if (usb_mtp_object_lookup_name(parent, name, -1)) {
            /* Duplicate create event */
            return;
        }
        entry = g_new0(MTPMonEntry, 1);
        entry->handle = s->next_handle;
        entry->event = EVT_OBJ_ADDED;
        o = usb_mtp_add_child(s, parent, name);
        if (!o) {
            g_free(entry);
            return;
        }
        trace_usb_mtp_file_monitor_event(s->dev.addr, name, "Obj Added");
        break;

    case QFILE_MONITOR_EVENT_DELETED:
        /*
         * The kernel issues a IN_IGNORED event
         * when a dir containing a watchpoint is
         * deleted, so we don't have to delete the
         * watchpoint
         */
        o = usb_mtp_object_lookup_name(parent, name, -1);
        if (!o) {
            return;
        }
        entry = g_new0(MTPMonEntry, 1);
        entry->handle = o->handle;
        entry->event = EVT_OBJ_REMOVED;
        trace_usb_mtp_file_monitor_event(s->dev.addr, o->path, "Obj Deleted");
        usb_mtp_object_free(s, o);
        break;

    case QFILE_MONITOR_EVENT_MODIFIED:
        o = usb_mtp_object_lookup_name(parent, name, -1);
        if (!o) {
            return;
        }
        entry = g_new0(MTPMonEntry, 1);
        entry->handle = o->handle;
        entry->event = EVT_OBJ_INFO_CHANGED;
        trace_usb_mtp_file_monitor_event(s->dev.addr, o->path, "Obj Modified");
        break;

    case QFILE_MONITOR_EVENT_IGNORED:
        trace_usb_mtp_file_monitor_event(s->dev.addr, parent->path,
                                    "Obj parent dir ignored");
        break;

    case QFILE_MONITOR_EVENT_ATTRIBUTES:
        break;

    default:
        g_assert_not_reached();
    }

    if (entry) {
        QTAILQ_INSERT_HEAD(&s->events, entry, next);
    }
}

static void usb_mtp_file_monitor_cleanup(MTPState *s)
{
    MTPMonEntry *e, *p;

    QTAILQ_FOREACH_SAFE(e, &s->events, next, p) {
        QTAILQ_REMOVE(&s->events, e, next);
        g_free(e);
    }

    qemu_file_monitor_free(s->file_monitor);
    s->file_monitor = NULL;
}


static void usb_mtp_object_readdir(MTPState *s, MTPObject *o)
{
    struct dirent *entry;
    DIR *dir;
    int fd;
    Error *err = NULL;

    if (o->have_children) {
        return;
    }
    o->have_children = true;

    fd = open(o->path, O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        return;
    }
    dir = fdopendir(fd);
    if (!dir) {
        close(fd);
        return;
    }

    if (s->file_monitor) {
        int64_t id = qemu_file_monitor_add_watch(s->file_monitor, o->path, NULL,
                                                 file_monitor_event, s, &err);
        if (id == -1) {
            error_reportf_err(err,
                              "usb-mtp: failed to add watch for %s: ",
                              o->path);
        } else {
            trace_usb_mtp_file_monitor_event(s->dev.addr, o->path,
                                             "Watch Added");
            o->watchid = id;
        }
    }

    while ((entry = readdir(dir)) != NULL) {
        usb_mtp_add_child(s, o, entry->d_name);
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
    wchar_t *wstr = g_new(wchar_t, len);
    size_t ret;

    ret = mbstowcs(wstr, str, len);
    if (ret == -1) {
        usb_mtp_add_wstr(data, L"Oops");
    } else {
        usb_mtp_add_wstr(data, wstr);
    }

    g_free(wstr);
}

static void usb_mtp_add_time(MTPData *data, time_t time)
{
    g_autoptr(GDateTime) then = g_date_time_new_from_unix_utc(time);
    g_autofree char *thenstr = g_date_time_format(then, "%Y%m%dT%H%M%S");
    usb_mtp_add_str(data, thenstr);
}

/* ----------------------------------------------------------------------- */

static void usb_mtp_queue_result(MTPState *s, uint16_t code, uint32_t trans,
                                 int argc, uint32_t arg0, uint32_t arg1,
                                 uint32_t arg2)
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
    if (argc > 2) {
        c->argv[2] = arg2;
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
        CMD_DELETE_OBJECT,
        CMD_SEND_OBJECT_INFO,
        CMD_SEND_OBJECT,
        CMD_GET_OBJECT,
        CMD_GET_PARTIAL_OBJECT,
        CMD_GET_OBJECT_PROPS_SUPPORTED,
        CMD_GET_OBJECT_PROP_DESC,
        CMD_GET_OBJECT_PROP_VALUE,
    };
    static const uint16_t fmt[] = {
        FMT_UNDEFINED_OBJECT,
        FMT_ASSOCIATION,
    };
    MTPData *d = usb_mtp_data_alloc(c);

    trace_usb_mtp_op_get_device_info(s->dev.addr);

    usb_mtp_add_u16(d, 100);
    usb_mtp_add_u32(d, 0x00000006);
    usb_mtp_add_u16(d, 0x0064);
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
        usb_mtp_add_u64(d, (uint64_t)buf.f_frsize * buf.f_bavail);
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
    uint32_t i = 0;
    g_autofree uint32_t *handles = g_new(uint32_t, o->nchildren);
    MTPObject *iter;

    trace_usb_mtp_op_get_object_handles(s->dev.addr, o->handle, o->path);

    QLIST_FOREACH(iter, &o->children, list) {
        handles[i++] = iter->handle;
    }
    assert(i == o->nchildren);
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

    if (o->stat.st_size > 0xFFFFFFFF) {
        usb_mtp_add_u32(d, 0xFFFFFFFF);
    } else {
        usb_mtp_add_u32(d, o->stat.st_size);
    }

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

    d->fd = open(o->path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
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
    MTPData *d;
    off_t offset;

    if (c->argc <= 2) {
        return NULL;
    }
    trace_usb_mtp_op_get_partial_object(s->dev.addr, o->handle, o->path,
                                        c->argv[1], c->argv[2]);

    d = usb_mtp_data_alloc(c);
    d->fd = open(o->path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
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

static MTPData *usb_mtp_get_object_props_supported(MTPState *s, MTPControl *c)
{
    static const uint16_t props[] = {
        PROP_STORAGE_ID,
        PROP_OBJECT_FORMAT,
        PROP_OBJECT_COMPRESSED_SIZE,
        PROP_PARENT_OBJECT,
        PROP_PERSISTENT_UNIQUE_OBJECT_IDENTIFIER,
        PROP_NAME,
    };
    MTPData *d = usb_mtp_data_alloc(c);
    usb_mtp_add_u16_array(d, ARRAY_SIZE(props), props);

    return d;
}

static MTPData *usb_mtp_get_object_prop_desc(MTPState *s, MTPControl *c)
{
    MTPData *d = usb_mtp_data_alloc(c);
    switch (c->argv[0]) {
    case PROP_STORAGE_ID:
        usb_mtp_add_u16(d, PROP_STORAGE_ID);
        usb_mtp_add_u16(d, DATA_TYPE_UINT32);
        usb_mtp_add_u8(d, 0x00);
        usb_mtp_add_u32(d, 0x00000000);
        usb_mtp_add_u32(d, 0x00000000);
        usb_mtp_add_u8(d, 0x00);
        break;
    case PROP_OBJECT_FORMAT:
        usb_mtp_add_u16(d, PROP_OBJECT_FORMAT);
        usb_mtp_add_u16(d, DATA_TYPE_UINT16);
        usb_mtp_add_u8(d, 0x00);
        usb_mtp_add_u16(d, 0x0000);
        usb_mtp_add_u32(d, 0x00000000);
        usb_mtp_add_u8(d, 0x00);
        break;
    case PROP_OBJECT_COMPRESSED_SIZE:
        usb_mtp_add_u16(d, PROP_OBJECT_COMPRESSED_SIZE);
        usb_mtp_add_u16(d, DATA_TYPE_UINT64);
        usb_mtp_add_u8(d, 0x00);
        usb_mtp_add_u64(d, 0x0000000000000000);
        usb_mtp_add_u32(d, 0x00000000);
        usb_mtp_add_u8(d, 0x00);
        break;
    case PROP_PARENT_OBJECT:
        usb_mtp_add_u16(d, PROP_PARENT_OBJECT);
        usb_mtp_add_u16(d, DATA_TYPE_UINT32);
        usb_mtp_add_u8(d, 0x00);
        usb_mtp_add_u32(d, 0x00000000);
        usb_mtp_add_u32(d, 0x00000000);
        usb_mtp_add_u8(d, 0x00);
        break;
    case PROP_PERSISTENT_UNIQUE_OBJECT_IDENTIFIER:
        usb_mtp_add_u16(d, PROP_PERSISTENT_UNIQUE_OBJECT_IDENTIFIER);
        usb_mtp_add_u16(d, DATA_TYPE_UINT128);
        usb_mtp_add_u8(d, 0x00);
        usb_mtp_add_u64(d, 0x0000000000000000);
        usb_mtp_add_u64(d, 0x0000000000000000);
        usb_mtp_add_u32(d, 0x00000000);
        usb_mtp_add_u8(d, 0x00);
        break;
    case PROP_NAME:
        usb_mtp_add_u16(d, PROP_NAME);
        usb_mtp_add_u16(d, DATA_TYPE_STRING);
        usb_mtp_add_u8(d, 0x00);
        usb_mtp_add_u8(d, 0x00);
        usb_mtp_add_u32(d, 0x00000000);
        usb_mtp_add_u8(d, 0x00);
        break;
    default:
        usb_mtp_data_free(d);
        return NULL;
    }

    return d;
}

static MTPData *usb_mtp_get_object_prop_value(MTPState *s, MTPControl *c,
                                              MTPObject *o)
{
    MTPData *d = usb_mtp_data_alloc(c);
    switch (c->argv[1]) {
    case PROP_STORAGE_ID:
        usb_mtp_add_u32(d, QEMU_STORAGE_ID);
        break;
    case PROP_OBJECT_FORMAT:
        usb_mtp_add_u16(d, o->format);
        break;
    case PROP_OBJECT_COMPRESSED_SIZE:
        usb_mtp_add_u64(d, o->stat.st_size);
        break;
    case PROP_PARENT_OBJECT:
        if (o->parent == NULL) {
            usb_mtp_add_u32(d, 0x00000000);
        } else {
            usb_mtp_add_u32(d, o->parent->handle);
        }
        break;
    case PROP_PERSISTENT_UNIQUE_OBJECT_IDENTIFIER:
        /* Should be persistent between sessions,
         * but using our objedt ID is "good enough"
         * for now */
        usb_mtp_add_u64(d, 0x0000000000000000);
        usb_mtp_add_u64(d, o->handle);
        break;
    case PROP_NAME:
        usb_mtp_add_str(d, o->name);
        break;
    default:
        usb_mtp_data_free(d);
        return NULL;
    }

    return d;
}

/*
 * Return values when object @o is deleted.
 * If at least one of the deletions succeeded,
 * DELETE_SUCCESS is set and if at least one
 * of the deletions failed, DELETE_FAILURE is
 * set. Both bits being set (DELETE_PARTIAL)
 * signifies a  RES_PARTIAL_DELETE being sent
 * back to the initiator.
 */
enum {
    DELETE_SUCCESS = (1 << 0),
    DELETE_FAILURE = (1 << 1),
    DELETE_PARTIAL = (DELETE_FAILURE | DELETE_SUCCESS),
};

static int usb_mtp_deletefn(MTPState *s, MTPObject *o, uint32_t trans)
{
    MTPObject *iter, *iter2;
    int ret = 0;

    /*
     * TODO: Add support for Protection Status
     */

    QLIST_FOREACH(iter, &o->children, list) {
        if (iter->format == FMT_ASSOCIATION) {
            QLIST_FOREACH(iter2, &iter->children, list) {
                ret |= usb_mtp_deletefn(s, iter2, trans);
            }
        }
    }

    if (o->format == FMT_UNDEFINED_OBJECT) {
        if (remove(o->path)) {
            ret |= DELETE_FAILURE;
        } else {
            usb_mtp_object_free(s, o);
            ret |= DELETE_SUCCESS;
        }
    } else if (o->format == FMT_ASSOCIATION) {
        if (rmdir(o->path)) {
            ret |= DELETE_FAILURE;
        } else {
            usb_mtp_object_free(s, o);
            ret |= DELETE_SUCCESS;
        }
    }

    return ret;
}

static void usb_mtp_object_delete(MTPState *s, uint32_t handle,
                                  uint32_t format_code, uint32_t trans)
{
    MTPObject *o;
    int ret;

    /* Return error if store is read-only */
    if (!FLAG_SET(s, MTP_FLAG_WRITABLE)) {
        usb_mtp_queue_result(s, RES_STORE_READ_ONLY,
                             trans, 0, 0, 0, 0);
        return;
    }

    if (format_code != 0) {
        usb_mtp_queue_result(s, RES_SPEC_BY_FORMAT_UNSUPPORTED,
                             trans, 0, 0, 0, 0);
        return;
    }

    if (handle == 0xFFFFFFF) {
        o = QTAILQ_FIRST(&s->objects);
    } else {
        o = usb_mtp_object_lookup(s, handle);
    }
    if (o == NULL) {
        usb_mtp_queue_result(s, RES_INVALID_OBJECT_HANDLE,
                             trans, 0, 0, 0, 0);
        return;
    }

    ret = usb_mtp_deletefn(s, o, trans);
    switch (ret) {
    case DELETE_SUCCESS:
        usb_mtp_queue_result(s, RES_OK, trans,
                             0, 0, 0, 0);
        break;
    case DELETE_FAILURE:
        usb_mtp_queue_result(s, RES_PARTIAL_DELETE,
                             trans, 0, 0, 0, 0);
        break;
    case DELETE_PARTIAL:
        usb_mtp_queue_result(s, RES_PARTIAL_DELETE,
                             trans, 0, 0, 0, 0);
        break;
    default:
        g_assert_not_reached();
    }

    return;
}

static void usb_mtp_command(MTPState *s, MTPControl *c)
{
    MTPData *data_in = NULL;
    MTPObject *o = NULL;
    uint32_t nres = 0, res0 = 0;
    Error *err = NULL;

    /* sanity checks */
    if (c->code >= CMD_CLOSE_SESSION && s->session == 0) {
        usb_mtp_queue_result(s, RES_SESSION_NOT_OPEN,
                             c->trans, 0, 0, 0, 0);
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
                                 c->trans, 1, s->session, 0, 0);
            return;
        }
        if (c->argv[0] == 0) {
            usb_mtp_queue_result(s, RES_INVALID_PARAMETER,
                                 c->trans, 0, 0, 0, 0);
            return;
        }
        trace_usb_mtp_op_open_session(s->dev.addr);
        s->session = c->argv[0];
        usb_mtp_object_alloc(s, s->next_handle++, NULL, s->root);

        s->file_monitor = qemu_file_monitor_new(&err);
        if (err) {
            error_reportf_err(err,
                              "usb-mtp: file monitoring init failed: ");
        } else {
            QTAILQ_INIT(&s->events);
        }
        break;
    case CMD_CLOSE_SESSION:
        trace_usb_mtp_op_close_session(s->dev.addr);
        s->session = 0;
        s->next_handle = 0;
        usb_mtp_file_monitor_cleanup(s);
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
                                 c->trans, 0, 0, 0, 0);
            return;
        }
        data_in = usb_mtp_get_storage_info(s, c);
        break;
    case CMD_GET_NUM_OBJECTS:
    case CMD_GET_OBJECT_HANDLES:
        if (c->argv[0] != QEMU_STORAGE_ID &&
            c->argv[0] != 0xffffffff) {
            usb_mtp_queue_result(s, RES_INVALID_STORAGE_ID,
                                 c->trans, 0, 0, 0, 0);
            return;
        }
        if (c->argv[1] != 0x00000000) {
            usb_mtp_queue_result(s, RES_SPEC_BY_FORMAT_UNSUPPORTED,
                                 c->trans, 0, 0, 0, 0);
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
                                 c->trans, 0, 0, 0, 0);
            return;
        }
        if (o->format != FMT_ASSOCIATION) {
            usb_mtp_queue_result(s, RES_INVALID_PARENT_OBJECT,
                                 c->trans, 0, 0, 0, 0);
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
                                 c->trans, 0, 0, 0, 0);
            return;
        }
        data_in = usb_mtp_get_object_info(s, c, o);
        break;
    case CMD_GET_OBJECT:
        o = usb_mtp_object_lookup(s, c->argv[0]);
        if (o == NULL) {
            usb_mtp_queue_result(s, RES_INVALID_OBJECT_HANDLE,
                                 c->trans, 0, 0, 0, 0);
            return;
        }
        if (o->format == FMT_ASSOCIATION) {
            usb_mtp_queue_result(s, RES_INVALID_OBJECT_HANDLE,
                                 c->trans, 0, 0, 0, 0);
            return;
        }
        data_in = usb_mtp_get_object(s, c, o);
        if (data_in == NULL) {
            usb_mtp_queue_result(s, RES_GENERAL_ERROR,
                                 c->trans, 0, 0, 0, 0);
            return;
        }
        break;
    case CMD_DELETE_OBJECT:
        usb_mtp_object_delete(s, c->argv[0], c->argv[1], c->trans);
        return;
    case CMD_GET_PARTIAL_OBJECT:
        o = usb_mtp_object_lookup(s, c->argv[0]);
        if (o == NULL) {
            usb_mtp_queue_result(s, RES_INVALID_OBJECT_HANDLE,
                                 c->trans, 0, 0, 0, 0);
            return;
        }
        if (o->format == FMT_ASSOCIATION) {
            usb_mtp_queue_result(s, RES_INVALID_OBJECT_HANDLE,
                                 c->trans, 0, 0, 0, 0);
            return;
        }
        data_in = usb_mtp_get_partial_object(s, c, o);
        if (data_in == NULL) {
            usb_mtp_queue_result(s, RES_GENERAL_ERROR,
                                 c->trans, 0, 0, 0, 0);
            return;
        }
        nres = 1;
        res0 = data_in->length;
        break;
    case CMD_SEND_OBJECT_INFO:
        /* Return error if store is read-only */
        if (!FLAG_SET(s, MTP_FLAG_WRITABLE)) {
            usb_mtp_queue_result(s, RES_STORE_READ_ONLY,
                                 c->trans, 0, 0, 0, 0);
        } else if (c->argv[0] && (c->argv[0] != QEMU_STORAGE_ID)) {
            /* First parameter points to storage id or is 0 */
            usb_mtp_queue_result(s, RES_STORE_NOT_AVAILABLE, c->trans,
                                 0, 0, 0, 0);
        } else if (c->argv[1] && !c->argv[0]) {
            /* If second parameter is specified, first must also be specified */
            usb_mtp_queue_result(s, RES_DESTINATION_UNSUPPORTED, c->trans,
                                 0, 0, 0, 0);
        } else {
            uint32_t handle = c->argv[1];
            if (handle == 0xFFFFFFFF || handle == 0) {
                /* root object */
                o = QTAILQ_FIRST(&s->objects);
            } else {
                o = usb_mtp_object_lookup(s, handle);
            }
            if (o == NULL) {
                usb_mtp_queue_result(s, RES_INVALID_OBJECT_HANDLE, c->trans,
                                     0, 0, 0, 0);
            } else if (o->format != FMT_ASSOCIATION) {
                usb_mtp_queue_result(s, RES_INVALID_PARENT_OBJECT, c->trans,
                                     0, 0, 0, 0);
            }
        }
        if (o) {
            s->dataset.parent_handle = o->handle;
        }
        s->data_out = usb_mtp_data_alloc(c);
        return;
    case CMD_SEND_OBJECT:
        if (!FLAG_SET(s, MTP_FLAG_WRITABLE)) {
            usb_mtp_queue_result(s, RES_STORE_READ_ONLY,
                                 c->trans, 0, 0, 0, 0);
            return;
        }
        if (!s->write_pending) {
            usb_mtp_queue_result(s, RES_INVALID_OBJECTINFO,
                                 c->trans, 0, 0, 0, 0);
            return;
        }
        s->data_out = usb_mtp_data_alloc(c);
        return;
    case CMD_GET_OBJECT_PROPS_SUPPORTED:
        if (c->argv[0] != FMT_UNDEFINED_OBJECT &&
            c->argv[0] != FMT_ASSOCIATION) {
            usb_mtp_queue_result(s, RES_INVALID_OBJECT_FORMAT_CODE,
                                 c->trans, 0, 0, 0, 0);
            return;
        }
        data_in = usb_mtp_get_object_props_supported(s, c);
        break;
    case CMD_GET_OBJECT_PROP_DESC:
        if (c->argv[1] != FMT_UNDEFINED_OBJECT &&
            c->argv[1] != FMT_ASSOCIATION) {
            usb_mtp_queue_result(s, RES_INVALID_OBJECT_FORMAT_CODE,
                                 c->trans, 0, 0, 0, 0);
            return;
        }
        data_in = usb_mtp_get_object_prop_desc(s, c);
        if (data_in == NULL) {
            usb_mtp_queue_result(s, RES_INVALID_OBJECT_PROP_CODE,
                                 c->trans, 0, 0, 0, 0);
            return;
        }
        break;
    case CMD_GET_OBJECT_PROP_VALUE:
        o = usb_mtp_object_lookup(s, c->argv[0]);
        if (o == NULL) {
            usb_mtp_queue_result(s, RES_INVALID_OBJECT_HANDLE,
                                 c->trans, 0, 0, 0, 0);
            return;
        }
        data_in = usb_mtp_get_object_prop_value(s, c, o);
        if (data_in == NULL) {
            usb_mtp_queue_result(s, RES_INVALID_OBJECT_PROP_CODE,
                                 c->trans, 0, 0, 0, 0);
            return;
        }
        break;
    default:
        trace_usb_mtp_op_unknown(s->dev.addr, c->code);
        usb_mtp_queue_result(s, RES_OPERATION_NOT_SUPPORTED,
                             c->trans, 0, 0, 0, 0);
        return;
    }

    /* return results on success */
    if (data_in) {
        assert(s->data_in == NULL);
        s->data_in = data_in;
    }
    usb_mtp_queue_result(s, RES_OK, c->trans, nres, res0, 0, 0);
}

/* ----------------------------------------------------------------------- */

static void usb_mtp_handle_reset(USBDevice *dev)
{
    MTPState *s = USB_MTP(dev);

    trace_usb_mtp_reset(s->dev.addr);

    usb_mtp_file_monitor_cleanup(s);
    usb_mtp_object_free(s, QTAILQ_FIRST(&s->objects));
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
    MTPState *s = USB_MTP(dev);
    uint16_t *event = (uint16_t *)data;

    switch (request) {
    case ClassInterfaceOutRequest | 0x64:
        if (*event == EVT_CANCEL_TRANSACTION) {
            g_free(s->result);
            s->result = NULL;
            usb_mtp_data_free(s->data_in);
            s->data_in = NULL;
            if (s->write_pending) {
                g_free(s->dataset.filename);
                s->write_pending = false;
                s->dataset.size = 0;
            }
            usb_mtp_data_free(s->data_out);
            s->data_out = NULL;
        } else {
            p->status = USB_RET_STALL;
        }
        break;
    default:
        ret = usb_desc_handle_control(dev, p, request,
                                      value, index, length, data);
        if (ret >= 0) {
            return;
        }
    }

    trace_usb_mtp_stall(dev->addr, "unknown control request");
}

static void usb_mtp_cancel_packet(USBDevice *dev, USBPacket *p)
{
    /* we don't use async packets, so this should never be called */
    fprintf(stderr, "%s\n", __func__);
}

static char *utf16_to_str(uint8_t len, uint8_t *str16)
{
    wchar_t *wstr = g_new0(wchar_t, len + 1);
    int count, dlen;
    char *dest;

    for (count = 0; count < len; count++) {
        /* FIXME: not working for surrogate pairs */
        wstr[count] = lduw_le_p(str16 + (count * 2));
    }
    wstr[count] = 0;

    dlen = wcstombs(NULL, wstr, 0) + 1;
    dest = g_malloc(dlen);
    wcstombs(dest, wstr, dlen);
    g_free(wstr);
    return dest;
}

/* Wrapper around write, returns 0 on failure */
static uint64_t write_retry(int fd, void *buf, uint64_t size, off_t offset)
{
        uint64_t ret = 0;

        if (lseek(fd, offset, SEEK_SET) < 0) {
            goto done;
        }

        ret = qemu_write_full(fd, buf, size);

done:
        return ret;
}

static int usb_mtp_update_object(MTPObject *parent, char *name)
{
    int ret = 0;

    MTPObject *o =
        usb_mtp_object_lookup_name(parent, name, strlen(name));

    if (o) {
        ret = lstat(o->path, &o->stat);
    }

    return ret;
}

static void usb_mtp_write_data(MTPState *s, uint32_t handle)
{
    MTPData *d = s->data_out;
    MTPObject *parent =
        usb_mtp_object_lookup(s, s->dataset.parent_handle);
    char *path = NULL;
    uint64_t rc;
    mode_t mask = 0755;
    int ret = 0;

    assert(d != NULL);

    switch (d->write_status) {
    case WRITE_START:
        if (!parent || !s->write_pending) {
            usb_mtp_queue_result(s, RES_INVALID_OBJECTINFO, d->trans,
                0, 0, 0, 0);
            return;
        }

        if (s->dataset.filename) {
            path = g_strdup_printf("%s/%s", parent->path, s->dataset.filename);
            if (s->dataset.format == FMT_ASSOCIATION) {
                ret = g_mkdir(path, mask);
                if (!ret) {
                    usb_mtp_queue_result(s, RES_OK, d->trans, 3,
                                         QEMU_STORAGE_ID,
                                         s->dataset.parent_handle,
                                         handle);
                    goto close;
                }
                goto done;
            }

            d->fd = open(path, O_CREAT | O_WRONLY |
                         O_CLOEXEC | O_NOFOLLOW, mask & 0666);
            if (d->fd == -1) {
                ret = 1;
                goto done;
            }

            /* Return success if initiator sent 0 sized data */
            if (!s->dataset.size) {
                goto done;
            }
            if (d->length != MTP_WRITE_BUF_SZ && !d->pending) {
                d->write_status = WRITE_END;
            }
        }
        /* fall through */
    case WRITE_CONTINUE:
    case WRITE_END:
        rc = write_retry(d->fd, d->data, d->data_offset,
                         d->offset - d->data_offset);
        if (rc != d->data_offset) {
            ret = 1;
            goto done;
        }
        if (d->write_status != WRITE_END) {
            g_free(path);
            return;
        } else {
            /*
             * Return an incomplete transfer if file size doesn't match
             * for < 4G file or if lstat fails which will result in an incorrect
             * file size
             */
            if ((s->dataset.size != 0xFFFFFFFF &&
                 d->offset != s->dataset.size) ||
                usb_mtp_update_object(parent, s->dataset.filename)) {
                usb_mtp_queue_result(s, RES_INCOMPLETE_TRANSFER, d->trans,
                                     0, 0, 0, 0);
                goto close;
            }
        }
    }

done:
    if (ret) {
        usb_mtp_queue_result(s, RES_STORE_FULL, d->trans,
                             0, 0, 0, 0);
    } else {
        usb_mtp_queue_result(s, RES_OK, d->trans,
                             0, 0, 0, 0);
    }
close:
    /*
     * The write dataset is kept around and freed only
     * on success or if another write request comes in
     */
    if (d->fd != -1) {
        close(d->fd);
        d->fd = -1;
    }
    g_free(s->dataset.filename);
    s->dataset.size = 0;
    g_free(path);
    s->write_pending = false;
}

static void usb_mtp_write_metadata(MTPState *s, uint64_t dlen)
{
    MTPData *d = s->data_out;
    ObjectInfo *dataset = (ObjectInfo *)d->data;
    char *filename;
    MTPObject *o;
    MTPObject *p = usb_mtp_object_lookup(s, s->dataset.parent_handle);
    uint32_t next_handle = s->next_handle;
    size_t filename_chars = dlen - offsetof(ObjectInfo, filename);

    /*
     * filename is utf-16. We're intentionally doing
     * integer division to truncate if malicious guest
     * sent an odd number of bytes.
     */
    filename_chars /= 2;

    assert(!s->write_pending);
    assert(p != NULL);

    filename = utf16_to_str(MIN(dataset->length, filename_chars),
                            dataset->filename);

    if (strchr(filename, '/')) {
        usb_mtp_queue_result(s, RES_PARAMETER_NOT_SUPPORTED, d->trans,
                             0, 0, 0, 0);
        g_free(filename);
        return;
    }

    o = usb_mtp_object_lookup_name(p, filename, -1);
    if (o != NULL) {
        next_handle = o->handle;
    }

    s->dataset.filename = filename;
    s->dataset.format = dataset->format;
    s->dataset.size = dataset->size;
    s->write_pending = true;

    if (s->dataset.format == FMT_ASSOCIATION) {
        usb_mtp_write_data(s, next_handle);
    } else {
        usb_mtp_queue_result(s, RES_OK, d->trans, 3, QEMU_STORAGE_ID,
                             s->dataset.parent_handle, next_handle);
    }
}

static void usb_mtp_get_data(MTPState *s, mtp_container *container,
                             USBPacket *p)
{
    MTPData *d = s->data_out;
    uint64_t dlen;
    uint32_t data_len = p->iov.size;
    uint64_t total_len;

    if (!d) {
            usb_mtp_queue_result(s, RES_INVALID_OBJECTINFO, 0,
                                 0, 0, 0, 0);
            return;
    }
    if (d->first) {
        /* Total length of incoming data */
        total_len = cpu_to_le32(container->length) - sizeof(mtp_container);
        /* Length of data in this packet */
        data_len -= sizeof(mtp_container);
        if (total_len < MTP_WRITE_BUF_SZ) {
                usb_mtp_realloc(d, total_len);
                d->length += total_len;
        } else {
                usb_mtp_realloc(d, MTP_WRITE_BUF_SZ - sizeof(mtp_container));
                d->length += MTP_WRITE_BUF_SZ - sizeof(mtp_container);
        }
        d->offset = 0;
        d->first = false;
        d->pending = false;
        d->data_offset = 0;
        d->write_status = WRITE_START;
    }

    if (d->pending) {
        memset(d->data, 0, d->length);
        if (d->length != MTP_WRITE_BUF_SZ) {
            usb_mtp_realloc(d, MTP_WRITE_BUF_SZ - d->length);
            d->length += (MTP_WRITE_BUF_SZ - d->length);
        }
        d->pending = false;
        d->write_status = WRITE_CONTINUE;
        d->data_offset = 0;
    }

    if (d->length - d->data_offset > data_len) {
        dlen = data_len;
    } else {
        dlen = d->length - d->data_offset;
    }

    switch (d->code) {
    case CMD_SEND_OBJECT_INFO:
        usb_packet_copy(p, d->data + d->data_offset, dlen);
        d->offset += dlen;
        d->data_offset += dlen;
        if (d->data_offset == d->length) {
            /* The operation might have already failed */
            if (!s->result) {
                usb_mtp_write_metadata(s, dlen);
            }
            usb_mtp_data_free(s->data_out);
            s->data_out = NULL;
            return;
        }
        break;
    case CMD_SEND_OBJECT:
        usb_packet_copy(p, d->data + d->data_offset, dlen);
        d->offset += dlen;
        d->data_offset += dlen;
        if ((p->iov.size % 64) || !p->iov.size) {
            assert((s->dataset.size == 0xFFFFFFFF) ||
                   (s->dataset.size == d->offset));

            if (d->length == MTP_WRITE_BUF_SZ) {
                d->write_status = WRITE_END;
            } else {
                d->write_status = WRITE_START;
            }
            usb_mtp_write_data(s, 0);
            usb_mtp_data_free(s->data_out);
            s->data_out = NULL;
            return;
        }
        if (d->data_offset == d->length) {
            d->pending = true;
            usb_mtp_write_data(s, 0);
        }
        break;
    default:
        p->status = USB_RET_STALL;
        return;
    }
}

static void usb_mtp_handle_data(USBDevice *dev, USBPacket *p)
{
    MTPState *s = USB_MTP(dev);
    MTPControl cmd;
    mtp_container container;
    uint32_t params[5];
    uint16_t container_type;
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
            uint64_t dlen = d->length - d->offset;
            if (d->first) {
                trace_usb_mtp_data_in(s->dev.addr, d->trans, d->length);
                if (d->length + sizeof(container) > 0xFFFFFFFF) {
                    container.length = cpu_to_le32(0xFFFFFFFF);
                } else {
                    container.length =
                        cpu_to_le32(d->length + sizeof(container));
                }
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
        if ((s->data_out != NULL) && !s->data_out->first) {
            container_type = TYPE_DATA;
        } else {
            usb_packet_copy(p, &container, sizeof(container));
            container_type = le16_to_cpu(container.type);
        }
        switch (container_type) {
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
        case TYPE_DATA:
            /* One of the previous transfers has already errored but the
             * responder is still sending data associated with it
             */
            if (s->result != NULL) {
                return;
            }
            usb_mtp_get_data(s, &container, p);
            break;
        default:
            /* not needed as long as the mtp device is read-only */
            p->status = USB_RET_STALL;
            return;
        }
        break;
    case EP_EVENT:
        if (!QTAILQ_EMPTY(&s->events)) {
            struct MTPMonEntry *e = QTAILQ_LAST(&s->events);
            uint32_t handle;
            int len = sizeof(container) + sizeof(uint32_t);

            if (p->iov.size < len) {
                trace_usb_mtp_stall(s->dev.addr,
                                    "packet too small to send event");
                p->status = USB_RET_STALL;
                return;
            }

            QTAILQ_REMOVE(&s->events, e, next);
            container.length = cpu_to_le32(len);
            container.type = cpu_to_le32(TYPE_EVENT);
            container.code = cpu_to_le16(e->event);
            container.trans = 0; /* no trans specific events */
            handle = cpu_to_le32(e->handle);
            usb_packet_copy(p, &container, sizeof(container));
            usb_packet_copy(p, &handle, sizeof(uint32_t));
            g_free(e);
            return;
        }
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

static void usb_mtp_realize(USBDevice *dev, Error **errp)
{
    MTPState *s = USB_MTP(dev);

    if ((s->root == NULL) || !g_path_is_absolute(s->root)) {
        error_setg(errp, "usb-mtp: rootdir must be configured and be an absolute path");
        return;
    }

    if (access(s->root, R_OK) != 0) {
        error_setg(errp, "usb-mtp: rootdir does not exist/not readable");
        return;
    } else if (!s->readonly && access(s->root, W_OK) != 0) {
        error_setg(errp, "usb-mtp: rootdir does not have write permissions");
        return;
    }

    /* Mark store as RW */
    if (!s->readonly) {
        s->flags |= (1 << MTP_FLAG_WRITABLE);
    }

    if (s->desc == NULL) {
        /*
         * This does not check if path exists
         * but we have the checks above
         */
        s->desc = g_path_get_basename(s->root);
    }

    usb_desc_create_serial(dev);
    usb_desc_init(dev);
    QTAILQ_INIT(&s->objects);

}

static const VMStateDescription vmstate_usb_mtp = {
    .name = "usb-mtp",
    .unmigratable = 1,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_USB_DEVICE(dev, MTPState),
        VMSTATE_END_OF_LIST()
    }
};

static const Property mtp_properties[] = {
    DEFINE_PROP_STRING("rootdir", MTPState, root),
    DEFINE_PROP_STRING("desc", MTPState, desc),
    DEFINE_PROP_BOOL("readonly", MTPState, readonly, true),
};

static void usb_mtp_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);

    uc->realize        = usb_mtp_realize;
    uc->product_desc   = "QEMU USB MTP";
    uc->usb_desc       = &desc;
    uc->cancel_packet  = usb_mtp_cancel_packet;
    uc->handle_attach  = usb_desc_attach;
    uc->handle_reset   = usb_mtp_handle_reset;
    uc->handle_control = usb_mtp_handle_control;
    uc->handle_data    = usb_mtp_handle_data;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    dc->desc = "USB Media Transfer Protocol device";
    dc->fw_name = "mtp";
    dc->vmsd = &vmstate_usb_mtp;
    device_class_set_props(dc, mtp_properties);
}

static const TypeInfo mtp_info = {
    .name          = TYPE_USB_MTP,
    .parent        = TYPE_USB_DEVICE,
    .instance_size = sizeof(MTPState),
    .class_init    = usb_mtp_class_initfn,
};

static void usb_mtp_register_types(void)
{
    type_register_static(&mtp_info);
}

type_init(usb_mtp_register_types)
