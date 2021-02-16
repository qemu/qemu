/*
 * usb packet capture
 *
 * Copyright (c) 2021 Gerd Hoffmann <kraxel@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/usb.h"

#define PCAP_MAGIC                   0xa1b2c3d4
#define PCAP_MAJOR                   2
#define PCAP_MINOR                   4

/* https://wiki.wireshark.org/Development/LibpcapFileFormat */

struct pcap_hdr {
    uint32_t magic_number;   /* magic number */
    uint16_t version_major;  /* major version number */
    uint16_t version_minor;  /* minor version number */
    int32_t  thiszone;       /* GMT to local correction */
    uint32_t sigfigs;        /* accuracy of timestamps */
    uint32_t snaplen;        /* max length of captured packets, in octets */
    uint32_t network;        /* data link type */
};

struct pcaprec_hdr {
    uint32_t ts_sec;         /* timestamp seconds */
    uint32_t ts_usec;        /* timestamp microseconds */
    uint32_t incl_len;       /* number of octets of packet saved in file */
    uint32_t orig_len;       /* actual length of packet */
};

/* https://www.tcpdump.org/linktypes.html */
/* linux: Documentation/usb/usbmon.rst */
/* linux: drivers/usb/mon/mon_bin.c */

#define LINKTYPE_USB_LINUX           189  /* first 48 bytes only */
#define LINKTYPE_USB_LINUX_MMAPPED   220  /* full 64 byte header */

struct usbmon_packet {
    uint64_t id;             /*  0: URB ID - from submission to callback */
    unsigned char type;      /*  8: Same as text; extensible. */
    unsigned char xfer_type; /*     ISO (0), Intr, Control, Bulk (3) */
    unsigned char epnum;     /*     Endpoint number and transfer direction */
    unsigned char devnum;    /*     Device address */
    uint16_t busnum;         /* 12: Bus number */
    char flag_setup;         /* 14: Same as text */
    char flag_data;          /* 15: Same as text; Binary zero is OK. */
    int64_t ts_sec;          /* 16: gettimeofday */
    int32_t ts_usec;         /* 24: gettimeofday */
    int32_t status;          /* 28: */
    unsigned int length;     /* 32: Length of data (submitted or actual) */
    unsigned int len_cap;    /* 36: Delivered length */
    union {                  /* 40: */
        unsigned char setup[8];         /* Only for Control S-type */
        struct iso_rec {                /* Only for ISO */
            int32_t error_count;
            int32_t numdesc;
        } iso;
    } s;
    int32_t interval;        /* 48: Only for Interrupt and ISO */
    int32_t start_frame;     /* 52: For ISO */
    uint32_t xfer_flags;     /* 56: copy of URB's transfer_flags */
    uint32_t ndesc;          /* 60: Actual number of ISO descriptors */
};                           /* 64 total length */

/* ------------------------------------------------------------------------ */

#define CTRL_LEN                     4096
#define DATA_LEN                     256

static int usbmon_status(USBPacket *p)
{
    switch (p->status) {
    case USB_RET_SUCCESS:
        return 0;
    case USB_RET_NODEV:
        return -19;  /* -ENODEV */
    default:
        return -121; /* -EREMOTEIO */
    }
}

static unsigned int usbmon_epnum(USBPacket *p)
{
    unsigned epnum = 0;

    epnum |= p->ep->nr;
    epnum |= (p->pid == USB_TOKEN_IN) ? 0x80 : 0;
    return epnum;
}

static unsigned char usbmon_xfer_type[] = {
    [USB_ENDPOINT_XFER_CONTROL] = 2,
    [USB_ENDPOINT_XFER_ISOC]    = 0,
    [USB_ENDPOINT_XFER_BULK]    = 3,
    [USB_ENDPOINT_XFER_INT]     = 1,
};

static void do_usb_pcap_header(FILE *fp, struct usbmon_packet *packet)
{
    struct pcaprec_hdr header;
    struct timeval tv;

    gettimeofday(&tv, NULL);
    packet->ts_sec  = tv.tv_sec;
    packet->ts_usec = tv.tv_usec;

    header.ts_sec   = packet->ts_sec;
    header.ts_usec  = packet->ts_usec;
    header.incl_len = packet->len_cap;
    header.orig_len = packet->length + sizeof(*packet);
    fwrite(&header, sizeof(header), 1, fp);
    fwrite(packet, sizeof(*packet), 1, fp);
}

static void do_usb_pcap_ctrl(FILE *fp, USBPacket *p, bool setup)
{
    USBDevice *dev = p->ep->dev;
    bool in = dev->setup_buf[0] & USB_DIR_IN;
    struct usbmon_packet packet = {
        .id         = 0,
        .type       = setup ? 'S' : 'C',
        .xfer_type  = usbmon_xfer_type[USB_ENDPOINT_XFER_CONTROL],
        .epnum      = in ? 0x80 : 0,
        .devnum     = dev->addr,
        .flag_setup = setup ? 0 : '-',
        .flag_data  = '=',
        .length     = dev->setup_len,
    };
    int data_len = dev->setup_len;

    if (data_len > CTRL_LEN) {
        data_len = CTRL_LEN;
    }
    if (setup) {
        memcpy(packet.s.setup, dev->setup_buf, 8);
    } else {
        packet.status = usbmon_status(p);
    }

    if (in && setup) {
        packet.flag_data = '<';
        packet.length = 0;
        data_len  = 0;
    }
    if (!in && !setup) {
        packet.flag_data = '>';
        packet.length = 0;
        data_len  = 0;
    }

    packet.len_cap = data_len + sizeof(packet);
    do_usb_pcap_header(fp, &packet);
    if (data_len) {
        fwrite(dev->data_buf, data_len, 1, fp);
    }

    fflush(fp);
}

static void do_usb_pcap_data(FILE *fp, USBPacket *p, bool setup)
{
    struct usbmon_packet packet = {
        .id         = p->id,
        .type       = setup ? 'S' : 'C',
        .xfer_type  = usbmon_xfer_type[p->ep->type],
        .epnum      = usbmon_epnum(p),
        .devnum     = p->ep->dev->addr,
        .flag_setup = '-',
        .flag_data  = '=',
        .length     = p->iov.size,
    };
    int data_len = p->iov.size;

    if (p->ep->nr == 0) {
        /* ignore control pipe packets */
        return;
    }

    if (data_len > DATA_LEN) {
        data_len = DATA_LEN;
    }
    if (!setup) {
        packet.status = usbmon_status(p);
        if (packet.length > p->actual_length) {
            packet.length = p->actual_length;
        }
        if (data_len > p->actual_length) {
            data_len = p->actual_length;
        }
    }

    if (p->pid == USB_TOKEN_IN && setup) {
        packet.flag_data = '<';
        packet.length = 0;
        data_len  = 0;
    }
    if (p->pid == USB_TOKEN_OUT && !setup) {
        packet.flag_data = '>';
        packet.length = 0;
        data_len  = 0;
    }

    packet.len_cap = data_len + sizeof(packet);
    do_usb_pcap_header(fp, &packet);
    if (data_len) {
        void *buf = g_malloc(data_len);
        iov_to_buf(p->iov.iov, p->iov.niov, 0, buf, data_len);
        fwrite(buf, data_len, 1, fp);
        g_free(buf);
    }

    fflush(fp);
}

void usb_pcap_init(FILE *fp)
{
    struct pcap_hdr header = {
        .magic_number  = PCAP_MAGIC,
        .version_major = 2,
        .version_minor = 4,
        .snaplen       = MAX(CTRL_LEN, DATA_LEN) + sizeof(struct usbmon_packet),
        .network       = LINKTYPE_USB_LINUX_MMAPPED,
    };

    fwrite(&header, sizeof(header), 1, fp);
}

void usb_pcap_ctrl(USBPacket *p, bool setup)
{
    FILE *fp = p->ep->dev->pcap;

    if (!fp) {
        return;
    }

    do_usb_pcap_ctrl(fp, p, setup);
}

void usb_pcap_data(USBPacket *p, bool setup)
{
    FILE *fp = p->ep->dev->pcap;

    if (!fp) {
        return;
    }

    do_usb_pcap_data(fp, p, setup);
}
