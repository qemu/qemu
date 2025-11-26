/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef QEMU_PCAP_H
#define QEMU_PCAP_H

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

#endif /* QEMU_PCAP_H */
