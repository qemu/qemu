/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
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
#include "clients.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/iov.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qapi/visitor.h"
#include "net/filter.h"
#include "qom/object.h"
#include "sysemu/rtc.h"

typedef struct DumpState {
    int64_t start_ts;
    int fd;
    int pcap_caplen;
} DumpState;

#define PCAP_MAGIC 0xa1b2c3d4

struct pcap_file_hdr {
    uint32_t magic;
    uint16_t version_major;
    uint16_t version_minor;
    int32_t thiszone;
    uint32_t sigfigs;
    uint32_t snaplen;
    uint32_t linktype;
};

struct pcap_sf_pkthdr {
    struct {
        int32_t tv_sec;
        int32_t tv_usec;
    } ts;
    uint32_t caplen;
    uint32_t len;
};

static ssize_t dump_receive_iov(DumpState *s, const struct iovec *iov, int cnt)
{
    struct pcap_sf_pkthdr hdr;
    int64_t ts;
    int caplen;
    size_t size = iov_size(iov, cnt);
    struct iovec dumpiov[cnt + 1];

    /* Early return in case of previous error. */
    if (s->fd < 0) {
        return size;
    }

    ts = qemu_clock_get_us(QEMU_CLOCK_VIRTUAL);
    caplen = size > s->pcap_caplen ? s->pcap_caplen : size;

    hdr.ts.tv_sec = ts / 1000000 + s->start_ts;
    hdr.ts.tv_usec = ts % 1000000;
    hdr.caplen = caplen;
    hdr.len = size;

    dumpiov[0].iov_base = &hdr;
    dumpiov[0].iov_len = sizeof(hdr);
    cnt = iov_copy(&dumpiov[1], cnt, iov, cnt, 0, caplen);

    if (writev(s->fd, dumpiov, cnt + 1) != sizeof(hdr) + caplen) {
        error_report("network dump write error - stopping dump");
        close(s->fd);
        s->fd = -1;
    }

    return size;
}

static void dump_cleanup(DumpState *s)
{
    close(s->fd);
    s->fd = -1;
}

static int net_dump_state_init(DumpState *s, const char *filename,
                               int len, Error **errp)
{
    struct pcap_file_hdr hdr;
    struct tm tm;
    int fd;

    fd = open(filename, O_CREAT | O_TRUNC | O_WRONLY | O_BINARY, 0644);
    if (fd < 0) {
        error_setg_errno(errp, errno, "net dump: can't open %s", filename);
        return -1;
    }

    hdr.magic = PCAP_MAGIC;
    hdr.version_major = 2;
    hdr.version_minor = 4;
    hdr.thiszone = 0;
    hdr.sigfigs = 0;
    hdr.snaplen = len;
    hdr.linktype = 1;

    if (write(fd, &hdr, sizeof(hdr)) < sizeof(hdr)) {
        error_setg_errno(errp, errno, "net dump write error");
        close(fd);
        return -1;
    }

    s->fd = fd;
    s->pcap_caplen = len;

    qemu_get_timedate(&tm, 0);
    s->start_ts = mktime(&tm);

    return 0;
}

#define TYPE_FILTER_DUMP "filter-dump"

OBJECT_DECLARE_SIMPLE_TYPE(NetFilterDumpState, FILTER_DUMP)

struct NetFilterDumpState {
    NetFilterState nfs;
    DumpState ds;
    char *filename;
    uint32_t maxlen;
};

static ssize_t filter_dump_receive_iov(NetFilterState *nf, NetClientState *sndr,
                                       unsigned flags, const struct iovec *iov,
                                       int iovcnt, NetPacketSent *sent_cb)
{
    NetFilterDumpState *nfds = FILTER_DUMP(nf);

    dump_receive_iov(&nfds->ds, iov, iovcnt);
    return 0;
}

static void filter_dump_cleanup(NetFilterState *nf)
{
    NetFilterDumpState *nfds = FILTER_DUMP(nf);

    dump_cleanup(&nfds->ds);
}

static void filter_dump_setup(NetFilterState *nf, Error **errp)
{
    NetFilterDumpState *nfds = FILTER_DUMP(nf);

    if (!nfds->filename) {
        error_setg(errp, "dump filter needs 'file' property set!");
        return;
    }

    net_dump_state_init(&nfds->ds, nfds->filename, nfds->maxlen, errp);
}

static void filter_dump_get_maxlen(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    NetFilterDumpState *nfds = FILTER_DUMP(obj);
    uint32_t value = nfds->maxlen;

    visit_type_uint32(v, name, &value, errp);
}

static void filter_dump_set_maxlen(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    NetFilterDumpState *nfds = FILTER_DUMP(obj);
    uint32_t value;

    if (!visit_type_uint32(v, name, &value, errp)) {
        return;
    }
    if (value == 0) {
        error_setg(errp, "Property '%s.%s' doesn't take value '%u'",
                   object_get_typename(obj), name, value);
        return;
    }
    nfds->maxlen = value;
}

static char *file_dump_get_filename(Object *obj, Error **errp)
{
    NetFilterDumpState *nfds = FILTER_DUMP(obj);

    return g_strdup(nfds->filename);
}

static void file_dump_set_filename(Object *obj, const char *value, Error **errp)
{
   NetFilterDumpState *nfds = FILTER_DUMP(obj);

    g_free(nfds->filename);
    nfds->filename = g_strdup(value);
}

static void filter_dump_instance_init(Object *obj)
{
    NetFilterDumpState *nfds = FILTER_DUMP(obj);

    nfds->maxlen = 65536;
}

static void filter_dump_instance_finalize(Object *obj)
{
    NetFilterDumpState *nfds = FILTER_DUMP(obj);

    g_free(nfds->filename);
}

static void filter_dump_class_init(ObjectClass *oc, void *data)
{
    NetFilterClass *nfc = NETFILTER_CLASS(oc);

    object_class_property_add(oc, "maxlen", "uint32", filter_dump_get_maxlen,
                              filter_dump_set_maxlen, NULL, NULL);
    object_class_property_add_str(oc, "file", file_dump_get_filename,
                                  file_dump_set_filename);

    nfc->setup = filter_dump_setup;
    nfc->cleanup = filter_dump_cleanup;
    nfc->receive_iov = filter_dump_receive_iov;
}

static const TypeInfo filter_dump_info = {
    .name = TYPE_FILTER_DUMP,
    .parent = TYPE_NETFILTER,
    .class_init = filter_dump_class_init,
    .instance_init = filter_dump_instance_init,
    .instance_finalize = filter_dump_instance_finalize,
    .instance_size = sizeof(NetFilterDumpState),
};

static void filter_dump_register_types(void)
{
    type_register_static(&filter_dump_info);
}

type_init(filter_dump_register_types);
