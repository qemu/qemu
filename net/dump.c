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

#include "dump.h"
#include "qemu-common.h"
#include "qemu-error.h"
#include "qemu-log.h"
#include "qemu-timer.h"

typedef struct DumpState {
    VLANClientState nc;
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

static ssize_t dump_receive(VLANClientState *nc, const uint8_t *buf, size_t size)
{
    DumpState *s = DO_UPCAST(DumpState, nc, nc);
    struct pcap_sf_pkthdr hdr;
    int64_t ts;
    int caplen;

    /* Early return in case of previous error. */
    if (s->fd < 0) {
        return size;
    }

    ts = muldiv64(qemu_get_clock_ns(vm_clock), 1000000, get_ticks_per_sec());
    caplen = size > s->pcap_caplen ? s->pcap_caplen : size;

    hdr.ts.tv_sec = ts / 1000000 + s->start_ts;
    hdr.ts.tv_usec = ts % 1000000;
    hdr.caplen = caplen;
    hdr.len = size;
    if (write(s->fd, &hdr, sizeof(hdr)) != sizeof(hdr) ||
        write(s->fd, buf, caplen) != caplen) {
        qemu_log("-net dump write error - stop dump\n");
        close(s->fd);
        s->fd = -1;
    }

    return size;
}

static void dump_cleanup(VLANClientState *nc)
{
    DumpState *s = DO_UPCAST(DumpState, nc, nc);

    close(s->fd);
}

static NetClientInfo net_dump_info = {
    .type = NET_CLIENT_TYPE_DUMP,
    .size = sizeof(DumpState),
    .receive = dump_receive,
    .cleanup = dump_cleanup,
};

static int net_dump_init(VLANState *vlan, const char *device,
                         const char *name, const char *filename, int len)
{
    struct pcap_file_hdr hdr;
    VLANClientState *nc;
    DumpState *s;
    struct tm tm;
    int fd;

    fd = open(filename, O_CREAT | O_TRUNC | O_WRONLY | O_BINARY, 0644);
    if (fd < 0) {
        error_report("-net dump: can't open %s", filename);
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
        error_report("-net dump write error: %s", strerror(errno));
        close(fd);
        return -1;
    }

    nc = qemu_new_net_client(&net_dump_info, vlan, NULL, device, name);

    snprintf(nc->info_str, sizeof(nc->info_str),
             "dump to %s (len=%d)", filename, len);

    s = DO_UPCAST(DumpState, nc, nc);

    s->fd = fd;
    s->pcap_caplen = len;

    qemu_get_timedate(&tm, 0);
    s->start_ts = mktime(&tm);

    return 0;
}

int net_init_dump(QemuOpts *opts, Monitor *mon, const char *name, VLANState *vlan)
{
    int len;
    const char *file;
    char def_file[128];

    assert(vlan);

    file = qemu_opt_get(opts, "file");
    if (!file) {
        snprintf(def_file, sizeof(def_file), "qemu-vlan%d.pcap", vlan->id);
        file = def_file;
    }

    len = qemu_opt_get_size(opts, "len", 65536);

    return net_dump_init(vlan, "dump", name, file, len);
}
