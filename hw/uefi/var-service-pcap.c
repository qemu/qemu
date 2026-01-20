/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/pcap.h"
#include "system/dma.h"

#include "hw/uefi/var-service.h"

#define LINKTYPE_EDK2_MM  302

#define SNAPLEN   (64 * 1024)
#define TYPE_RESET       0x01
#define TYPE_REQUEST     0x02
#define TYPE_REPLY       0x03

static void uefi_vars_pcap_header(FILE *fp)
{
    static const struct pcap_hdr header = {
        .magic_number  = PCAP_MAGIC,
        .version_major = PCAP_MAJOR,
        .version_minor = PCAP_MINOR,
        .snaplen       = SNAPLEN,
        .network       = LINKTYPE_EDK2_MM,
    };

    fwrite(&header, sizeof(header), 1, fp);
    fflush(fp);
}

static void uefi_vars_pcap_packet(FILE *fp, uint32_t type,
                                  void *buffer, size_t size)
{
    struct pcaprec_hdr header;
    struct timeval tv;
    uint32_t orig_len = size + sizeof(type);
    uint32_t incl_len = MIN(orig_len, SNAPLEN);

    gettimeofday(&tv, NULL);
    header.ts_sec   = tv.tv_sec;
    header.ts_usec  = tv.tv_usec;
    header.incl_len = incl_len;
    header.orig_len = orig_len;

    fwrite(&header, sizeof(header), 1, fp);
    fwrite(&type, sizeof(type), 1, fp);
    if (buffer) {
        fwrite(buffer, incl_len - sizeof(type), 1, fp);
    }
    fflush(fp);
}

void uefi_vars_pcap_init(uefi_vars_state *uv, Error **errp)
{
    int fd;

    if (!uv->pcapfile) {
        return;
    }

    fd = qemu_create(uv->pcapfile,
                     O_WRONLY | O_TRUNC | O_BINARY,
                     0666, errp);
    if (fd < 0) {
        return;
    }

    uv->pcapfp = fdopen(fd, "wb");
    uefi_vars_pcap_header(uv->pcapfp);
}

void uefi_vars_pcap_reset(uefi_vars_state *uv)
{
    if (!uv->pcapfp) {
        return;
    }
    uefi_vars_pcap_packet(uv->pcapfp, TYPE_RESET, NULL, 0);
}

void uefi_vars_pcap_request(uefi_vars_state *uv, void *buffer, size_t size)
{
    if (!uv->pcapfp) {
        return;
    }
    uefi_vars_pcap_packet(uv->pcapfp, TYPE_REQUEST, buffer, size);
}

void uefi_vars_pcap_reply(uefi_vars_state *uv, void *buffer, size_t size)
{
    if (!uv->pcapfp) {
        return;
    }
    uefi_vars_pcap_packet(uv->pcapfp, TYPE_REPLY, buffer, size);
}
