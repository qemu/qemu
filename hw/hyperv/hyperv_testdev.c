/*
 * QEMU KVM Hyper-V test device to support Hyper-V kvm-unit-tests
 *
 * Copyright (C) 2015 Andrey Smetanin <asmetanin@virtuozzo.com>
 *
 * Authors:
 *  Andrey Smetanin <asmetanin@virtuozzo.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "qemu/queue.h"
#include "hw/qdev.h"
#include "hw/isa/isa.h"
#include "hw/hyperv/hyperv.h"

typedef struct TestSintRoute {
    QLIST_ENTRY(TestSintRoute) le;
    uint8_t vp_index;
    uint8_t sint;
    HvSintRoute *sint_route;
} TestSintRoute;

typedef struct TestMsgConn {
    QLIST_ENTRY(TestMsgConn) le;
    uint8_t conn_id;
    HvSintRoute *sint_route;
    struct hyperv_message msg;
} TestMsgConn;

typedef struct TestEvtConn {
    QLIST_ENTRY(TestEvtConn) le;
    uint8_t conn_id;
    HvSintRoute *sint_route;
    EventNotifier notifier;
} TestEvtConn;

struct HypervTestDev {
    ISADevice parent_obj;
    MemoryRegion sint_control;
    QLIST_HEAD(, TestSintRoute) sint_routes;
    QLIST_HEAD(, TestMsgConn) msg_conns;
    QLIST_HEAD(, TestEvtConn) evt_conns;
};
typedef struct HypervTestDev HypervTestDev;

#define TYPE_HYPERV_TEST_DEV "hyperv-testdev"
#define HYPERV_TEST_DEV(obj) \
        OBJECT_CHECK(HypervTestDev, (obj), TYPE_HYPERV_TEST_DEV)

enum {
    HV_TEST_DEV_SINT_ROUTE_CREATE = 1,
    HV_TEST_DEV_SINT_ROUTE_DESTROY,
    HV_TEST_DEV_SINT_ROUTE_SET_SINT,
    HV_TEST_DEV_MSG_CONN_CREATE,
    HV_TEST_DEV_MSG_CONN_DESTROY,
    HV_TEST_DEV_EVT_CONN_CREATE,
    HV_TEST_DEV_EVT_CONN_DESTROY,
};

static void sint_route_create(HypervTestDev *dev,
                              uint8_t vp_index, uint8_t sint)
{
    TestSintRoute *sint_route;

    sint_route = g_new0(TestSintRoute, 1);
    assert(sint_route);

    sint_route->vp_index = vp_index;
    sint_route->sint = sint;

    sint_route->sint_route = hyperv_sint_route_new(vp_index, sint, NULL, NULL);
    assert(sint_route->sint_route);

    QLIST_INSERT_HEAD(&dev->sint_routes, sint_route, le);
}

static TestSintRoute *sint_route_find(HypervTestDev *dev,
                                      uint8_t vp_index, uint8_t sint)
{
    TestSintRoute *sint_route;

    QLIST_FOREACH(sint_route, &dev->sint_routes, le) {
        if (sint_route->vp_index == vp_index && sint_route->sint == sint) {
            return sint_route;
        }
    }
    assert(false);
    return NULL;
}

static void sint_route_destroy(HypervTestDev *dev,
                               uint8_t vp_index, uint8_t sint)
{
    TestSintRoute *sint_route;

    sint_route = sint_route_find(dev, vp_index, sint);
    QLIST_REMOVE(sint_route, le);
    hyperv_sint_route_unref(sint_route->sint_route);
    g_free(sint_route);
}

static void sint_route_set_sint(HypervTestDev *dev,
                                uint8_t vp_index, uint8_t sint)
{
    TestSintRoute *sint_route;

    sint_route = sint_route_find(dev, vp_index, sint);

    hyperv_sint_route_set_sint(sint_route->sint_route);
}

static void msg_retry(void *opaque)
{
    TestMsgConn *conn = opaque;
    assert(!hyperv_post_msg(conn->sint_route, &conn->msg));
}

static void msg_cb(void *data, int status)
{
    TestMsgConn *conn = data;

    if (!status) {
        return;
    }

    assert(status == -EAGAIN);

    aio_bh_schedule_oneshot(qemu_get_aio_context(), msg_retry, conn);
}

static uint16_t msg_handler(const struct hyperv_post_message_input *msg,
                            void *data)
{
    int ret;
    TestMsgConn *conn = data;

    /* post the same message we've got */
    conn->msg.header.message_type = msg->message_type;
    assert(msg->payload_size < sizeof(conn->msg.payload));
    conn->msg.header.payload_size = msg->payload_size;
    memcpy(&conn->msg.payload, msg->payload, msg->payload_size);

    ret = hyperv_post_msg(conn->sint_route, &conn->msg);

    switch (ret) {
    case 0:
        return HV_STATUS_SUCCESS;
    case -EAGAIN:
        return HV_STATUS_INSUFFICIENT_BUFFERS;
    default:
        return HV_STATUS_INVALID_HYPERCALL_INPUT;
    }
}

static void msg_conn_create(HypervTestDev *dev, uint8_t vp_index,
                            uint8_t sint, uint8_t conn_id)
{
    TestMsgConn *conn;

    conn = g_new0(TestMsgConn, 1);
    assert(conn);

    conn->conn_id = conn_id;

    conn->sint_route = hyperv_sint_route_new(vp_index, sint, msg_cb, conn);
    assert(conn->sint_route);

    assert(!hyperv_set_msg_handler(conn->conn_id, msg_handler, conn));

    QLIST_INSERT_HEAD(&dev->msg_conns, conn, le);
}

static void msg_conn_destroy(HypervTestDev *dev, uint8_t conn_id)
{
    TestMsgConn *conn;

    QLIST_FOREACH(conn, &dev->msg_conns, le) {
        if (conn->conn_id == conn_id) {
            QLIST_REMOVE(conn, le);
            hyperv_set_msg_handler(conn->conn_id, NULL, NULL);
            hyperv_sint_route_unref(conn->sint_route);
            g_free(conn);
            return;
        }
    }
    assert(false);
}

static void evt_conn_handler(EventNotifier *notifier)
{
    TestEvtConn *conn = container_of(notifier, TestEvtConn, notifier);

    event_notifier_test_and_clear(notifier);

    /* signal the same event flag we've got */
    assert(!hyperv_set_event_flag(conn->sint_route, conn->conn_id));
}

static void evt_conn_create(HypervTestDev *dev, uint8_t vp_index,
                            uint8_t sint, uint8_t conn_id)
{
    TestEvtConn *conn;

    conn = g_new0(TestEvtConn, 1);
    assert(conn);

    conn->conn_id = conn_id;

    conn->sint_route = hyperv_sint_route_new(vp_index, sint, NULL, NULL);
    assert(conn->sint_route);

    assert(!event_notifier_init(&conn->notifier, false));

    event_notifier_set_handler(&conn->notifier, evt_conn_handler);

    assert(!hyperv_set_event_flag_handler(conn_id, &conn->notifier));

    QLIST_INSERT_HEAD(&dev->evt_conns, conn, le);
}

static void evt_conn_destroy(HypervTestDev *dev, uint8_t conn_id)
{
    TestEvtConn *conn;

    QLIST_FOREACH(conn, &dev->evt_conns, le) {
        if (conn->conn_id == conn_id) {
            QLIST_REMOVE(conn, le);
            hyperv_set_event_flag_handler(conn->conn_id, NULL);
            event_notifier_set_handler(&conn->notifier, NULL);
            event_notifier_cleanup(&conn->notifier);
            hyperv_sint_route_unref(conn->sint_route);
            g_free(conn);
            return;
        }
    }
    assert(false);
}

static uint64_t hv_test_dev_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void hv_test_dev_write(void *opaque, hwaddr addr, uint64_t data,
                                uint32_t len)
{
    HypervTestDev *dev = HYPERV_TEST_DEV(opaque);
    uint8_t sint = data & 0xFF;
    uint8_t vp_index = (data >> 8ULL) & 0xFF;
    uint8_t ctl = (data >> 16ULL) & 0xFF;
    uint8_t conn_id = (data >> 24ULL) & 0xFF;

    switch (ctl) {
    case HV_TEST_DEV_SINT_ROUTE_CREATE:
        sint_route_create(dev, vp_index, sint);
        break;
    case HV_TEST_DEV_SINT_ROUTE_DESTROY:
        sint_route_destroy(dev, vp_index, sint);
        break;
    case HV_TEST_DEV_SINT_ROUTE_SET_SINT:
        sint_route_set_sint(dev, vp_index, sint);
        break;
    case HV_TEST_DEV_MSG_CONN_CREATE:
        msg_conn_create(dev, vp_index, sint, conn_id);
        break;
    case HV_TEST_DEV_MSG_CONN_DESTROY:
        msg_conn_destroy(dev, conn_id);
        break;
    case HV_TEST_DEV_EVT_CONN_CREATE:
        evt_conn_create(dev, vp_index, sint, conn_id);
        break;
    case HV_TEST_DEV_EVT_CONN_DESTROY:
        evt_conn_destroy(dev, conn_id);
        break;
    default:
        break;
    }
}

static const MemoryRegionOps synic_test_sint_ops = {
    .read = hv_test_dev_read,
    .write = hv_test_dev_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void hv_test_dev_realizefn(DeviceState *d, Error **errp)
{
    ISADevice *isa = ISA_DEVICE(d);
    HypervTestDev *dev = HYPERV_TEST_DEV(d);
    MemoryRegion *io = isa_address_space_io(isa);

    QLIST_INIT(&dev->sint_routes);
    QLIST_INIT(&dev->msg_conns);
    QLIST_INIT(&dev->evt_conns);
    memory_region_init_io(&dev->sint_control, OBJECT(dev),
                          &synic_test_sint_ops, dev,
                          "hyperv-testdev-ctl", 4);
    memory_region_add_subregion(io, 0x3000, &dev->sint_control);
}

static void hv_test_dev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->realize = hv_test_dev_realizefn;
}

static const TypeInfo hv_test_dev_info = {
    .name           = TYPE_HYPERV_TEST_DEV,
    .parent         = TYPE_ISA_DEVICE,
    .instance_size  = sizeof(HypervTestDev),
    .class_init     = hv_test_dev_class_init,
};

static void hv_test_dev_register_types(void)
{
    type_register_static(&hv_test_dev_info);
}
type_init(hv_test_dev_register_types);
