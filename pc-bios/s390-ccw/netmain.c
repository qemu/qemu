/*
 * S390 virtio-ccw network boot loading program
 *
 * Copyright 2017 Thomas Huth, Red Hat Inc.
 *
 * Based on the S390 virtio-ccw loading program (main.c)
 * Copyright (c) 2013 Alexander Graf <agraf@suse.de>
 *
 * And based on the network loading code from SLOF (netload.c)
 * Copyright (c) 2004, 2008 IBM Corporation
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <tftp.h>
#include <ethernet.h>
#include <dhcp.h>
#include <dhcpv6.h>
#include <ipv4.h>
#include <ipv6.h>
#include <dns.h>
#include <time.h>

#include "s390-ccw.h"
#include "virtio.h"

#define DEFAULT_BOOT_RETRIES 10
#define DEFAULT_TFTP_RETRIES 20

extern char _start[];

#define KERNEL_ADDR             ((void *)0L)
#define KERNEL_MAX_SIZE         ((long)_start)

char stack[PAGE_SIZE * 8] __attribute__((aligned(PAGE_SIZE)));
IplParameterBlock iplb __attribute__((aligned(PAGE_SIZE)));
static char cfgbuf[2048];

static SubChannelId net_schid = { .one = 1 };
static int ip_version = 4;
static uint64_t dest_timer;

static uint64_t get_timer_ms(void)
{
    uint64_t clk;

    asm volatile(" stck %0 " : : "Q"(clk) : "memory");

    /* Bit 51 is incremented each microsecond */
    return (clk >> (63 - 51)) / 1000;
}

void set_timer(int val)
{
    dest_timer = get_timer_ms() + val;
}

int get_timer(void)
{
    return dest_timer - get_timer_ms();
}

int get_sec_ticks(void)
{
    return 1000;    /* number of ticks in 1 second */
}

/**
 * Obtain IP and configuration info from DHCP server (either IPv4 or IPv6).
 * @param  fn_ip     contains the following configuration information:
 *                   client MAC, client IP, TFTP-server MAC, TFTP-server IP,
 *                   boot file name
 * @param  retries   Number of DHCP attempts
 * @return           0 : IP and configuration info obtained;
 *                   non-0 : error condition occurred.
 */
static int dhcp(struct filename_ip *fn_ip, int retries)
{
    int i = retries + 1;
    int rc = -1;

    printf("  Requesting information via DHCP:     ");

    dhcpv4_generate_transaction_id();
    dhcpv6_generate_transaction_id();

    do {
        printf("\b\b\b%03d", i - 1);
        if (!--i) {
            printf("\nGiving up after %d DHCP requests\n", retries);
            return -1;
        }
        ip_version = 4;
        rc = dhcpv4(NULL, fn_ip);
        if (rc == -1) {
            ip_version = 6;
            set_ipv6_address(fn_ip->fd, 0);
            rc = dhcpv6(NULL, fn_ip);
            if (rc == 0) {
                memcpy(&fn_ip->own_ip6, get_ipv6_address(), 16);
                break;
            }
        }
        if (rc != -1) {    /* either success or non-dhcp failure */
            break;
        }
    } while (1);
    printf("\b\b\b\bdone\n");

    return rc;
}

/**
 * Seed the random number generator with our mac and current timestamp
 */
static void seed_rng(uint8_t mac[])
{
    uint64_t seed;

    asm volatile(" stck %0 " : : "Q"(seed) : "memory");
    seed ^= (mac[2] << 24) | (mac[3] << 16) | (mac[4] << 8) | mac[5];
    srand(seed);
}

static int tftp_load(filename_ip_t *fnip, void *buffer, int len)
{
    tftp_err_t tftp_err;
    int rc;

    rc = tftp(fnip, buffer, len, DEFAULT_TFTP_RETRIES, &tftp_err, 1, 1428,
              ip_version);

    if (rc < 0) {
        /* Make sure that error messages are put into a new line */
        printf("\n  ");
    }

    if (rc > 1024) {
        printf("  TFTP: Received %s (%d KBytes)\n", fnip->filename, rc / 1024);
    } else if (rc > 0) {
        printf("  TFTP: Received %s (%d Bytes)\n", fnip->filename, rc);
    } else if (rc == -1) {
        puts("unknown TFTP error");
    } else if (rc == -2) {
        printf("TFTP buffer of %d bytes is too small for %s\n",
            len, fnip->filename);
    } else if (rc == -3) {
        printf("file not found: %s\n", fnip->filename);
    } else if (rc == -4) {
        puts("TFTP access violation");
    } else if (rc == -5) {
        puts("illegal TFTP operation");
    } else if (rc == -6) {
        puts("unknown TFTP transfer ID");
    } else if (rc == -7) {
        puts("no such TFTP user");
    } else if (rc == -8) {
        puts("TFTP blocksize negotiation failed");
    } else if (rc == -9) {
        puts("file exceeds maximum TFTP transfer size");
    } else if (rc <= -10 && rc >= -15) {
        const char *icmp_err_str;
        switch (rc) {
        case -ICMP_NET_UNREACHABLE - 10:
            icmp_err_str = "net unreachable";
            break;
        case -ICMP_HOST_UNREACHABLE - 10:
            icmp_err_str = "host unreachable";
            break;
        case -ICMP_PROTOCOL_UNREACHABLE - 10:
            icmp_err_str = "protocol unreachable";
            break;
        case -ICMP_PORT_UNREACHABLE - 10:
            icmp_err_str = "port unreachable";
            break;
        case -ICMP_FRAGMENTATION_NEEDED - 10:
            icmp_err_str = "fragmentation needed and DF set";
            break;
        case -ICMP_SOURCE_ROUTE_FAILED - 10:
            icmp_err_str = "source route failed";
            break;
        default:
            icmp_err_str = " UNKNOWN";
            break;
        }
        printf("ICMP ERROR \"%s\"\n", icmp_err_str);
    } else if (rc == -40) {
        printf("TFTP error occurred after %d bad packets received",
            tftp_err.bad_tftp_packets);
    } else if (rc == -41) {
        printf("TFTP error occurred after missing %d responses",
            tftp_err.no_packets);
    } else if (rc == -42) {
        printf("TFTP error missing block %d, expected block was %d",
            tftp_err.blocks_missed,
            tftp_err.blocks_received);
    }

    return rc;
}

static int net_init(filename_ip_t *fn_ip)
{
    uint8_t mac[6];
    int rc;

    memset(fn_ip, 0, sizeof(filename_ip_t));

    rc = virtio_net_init(mac);
    if (rc < 0) {
        puts("Could not initialize network device");
        return -101;
    }
    fn_ip->fd = rc;

    printf("  Using MAC address: %02x:%02x:%02x:%02x:%02x:%02x\n",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    set_mac_address(mac);    /* init ethernet layer */
    seed_rng(mac);

    rc = dhcp(fn_ip, DEFAULT_BOOT_RETRIES);
    if (rc >= 0) {
        if (ip_version == 4) {
            set_ipv4_address(fn_ip->own_ip);
        }
    } else {
        puts("Could not get IP address");
        return -101;
    }

    if (ip_version == 4) {
        printf("  Using IPv4 address: %d.%d.%d.%d\n",
              (fn_ip->own_ip >> 24) & 0xFF, (fn_ip->own_ip >> 16) & 0xFF,
              (fn_ip->own_ip >>  8) & 0xFF, fn_ip->own_ip & 0xFF);
    } else if (ip_version == 6) {
        char ip6_str[40];
        ipv6_to_str(fn_ip->own_ip6.addr, ip6_str);
        printf("  Using IPv6 address: %s\n", ip6_str);
    }

    if (rc == -2) {
        printf("ARP request to TFTP server (%d.%d.%d.%d) failed\n",
               (fn_ip->server_ip >> 24) & 0xFF, (fn_ip->server_ip >> 16) & 0xFF,
               (fn_ip->server_ip >>  8) & 0xFF, fn_ip->server_ip & 0xFF);
        return -102;
    }
    if (rc == -4 || rc == -3) {
        puts("Can't obtain TFTP server IP address");
        return -107;
    }

    printf("  Using TFTP server: ");
    if (ip_version == 4) {
        printf("%d.%d.%d.%d\n",
               (fn_ip->server_ip >> 24) & 0xFF, (fn_ip->server_ip >> 16) & 0xFF,
               (fn_ip->server_ip >>  8) & 0xFF, fn_ip->server_ip & 0xFF);
    } else if (ip_version == 6) {
        char ip6_str[40];
        ipv6_to_str(fn_ip->server_ip6.addr, ip6_str);
        printf("%s\n", ip6_str);
    }

    if (strlen((char *)fn_ip->filename) > 0) {
        printf("  Bootfile name: '%s'\n", fn_ip->filename);
    }

    return rc;
}

static void net_release(filename_ip_t *fn_ip)
{
    if (ip_version == 4) {
        dhcp_send_release(fn_ip->fd);
    }
}

/**
 * Load via information from a .INS file (which can be found on CD-ROMs
 * for example)
 */
static int handle_ins_cfg(filename_ip_t *fn_ip, char *cfg, int cfgsize)
{
    char *ptr;
    int rc = -1, llen;
    void *destaddr;
    char *insbuf = cfg;

    ptr = strchr(insbuf, '\n');
    if (!ptr) {
        puts("Does not seem to be a valid .INS file");
        return -1;
    }

    *ptr = 0;
    printf("\nParsing .INS file:\n %s\n", &insbuf[2]);

    insbuf = ptr + 1;
    while (*insbuf && insbuf < cfg + cfgsize) {
        ptr = strchr(insbuf, '\n');
        if (ptr) {
            *ptr = 0;
        }
        llen = strlen(insbuf);
        if (!llen) {
            insbuf = ptr + 1;
            continue;
        }
        ptr = strchr(insbuf, ' ');
        if (!ptr) {
            puts("Missing space separator in .INS file");
            return -1;
        }
        *ptr = 0;
        strncpy((char *)fn_ip->filename, insbuf, sizeof(fn_ip->filename));
        destaddr = (char *)atol(ptr + 1);
        rc = tftp_load(fn_ip, destaddr, (long)_start - (long)destaddr);
        if (rc <= 0) {
            break;
        }
        insbuf += llen + 1;
    }

    return rc;
}

static int net_try_direct_tftp_load(filename_ip_t *fn_ip)
{
    int rc;
    void *loadaddr = (void *)0x2000;  /* Load right after the low-core */

    rc = tftp_load(fn_ip, loadaddr, KERNEL_MAX_SIZE - (long)loadaddr);
    if (rc < 0) {
        return rc;
    } else if (rc < 8) {
        printf("'%s' is too small (%i bytes only).\n", fn_ip->filename, rc);
        return -1;
    }

    /* Check whether it is a configuration file instead of a kernel */
    if (rc < sizeof(cfgbuf) - 1) {
        memcpy(cfgbuf, loadaddr, rc);
        cfgbuf[rc] = 0;    /* Make sure that it is NUL-terminated */
        if (!strncmp("* ", cfgbuf, 2)) {
            return handle_ins_cfg(fn_ip, cfgbuf, rc);
        }
    }

    /* Move kernel to right location */
    memmove(KERNEL_ADDR, loadaddr, rc);

    return rc;
}

void panic(const char *string)
{
    sclp_print(string);
    for (;;) {
        disabled_wait();
    }
}

void write_subsystem_identification(void)
{
    SubChannelId *schid = (SubChannelId *) 184;
    uint32_t *zeroes = (uint32_t *) 188;

    *schid = net_schid;
    *zeroes = 0;
}

static bool find_net_dev(Schib *schib, int dev_no)
{
    int i, r;

    for (i = 0; i < 0x10000; i++) {
        net_schid.sch_no = i;
        r = stsch_err(net_schid, schib);
        if (r == 3 || r == -EIO) {
            break;
        }
        if (!schib->pmcw.dnv) {
            continue;
        }
        if (!virtio_is_supported(net_schid)) {
            continue;
        }
        if (virtio_get_device_type() != VIRTIO_ID_NET) {
            continue;
        }
        if (dev_no < 0 || schib->pmcw.dev == dev_no) {
            return true;
        }
    }

    return false;
}

static void virtio_setup(void)
{
    Schib schib;
    int ssid;
    bool found = false;
    uint16_t dev_no;

    /*
     * We unconditionally enable mss support. In every sane configuration,
     * this will succeed; and even if it doesn't, stsch_err() can deal
     * with the consequences.
     */
    enable_mss_facility();

    if (store_iplb(&iplb)) {
        IPL_assert(iplb.pbt == S390_IPL_TYPE_CCW, "IPL_TYPE_CCW expected");
        dev_no = iplb.ccw.devno;
        debug_print_int("device no. ", dev_no);
        net_schid.ssid = iplb.ccw.ssid & 0x3;
        debug_print_int("ssid ", net_schid.ssid);
        found = find_net_dev(&schib, dev_no);
    } else {
        for (ssid = 0; ssid < 0x3; ssid++) {
            net_schid.ssid = ssid;
            found = find_net_dev(&schib, -1);
            if (found) {
                break;
            }
        }
    }

    IPL_assert(found, "No virtio net device found");
}

void main(void)
{
    filename_ip_t fn_ip;
    int rc, fnlen;

    sclp_setup();
    sclp_print("Network boot starting...\n");

    virtio_setup();

    rc = net_init(&fn_ip);
    if (rc) {
        panic("Network initialization failed. Halting.\n");
    }

    fnlen = strlen((char *)fn_ip.filename);
    if (fnlen > 0 && fn_ip.filename[fnlen - 1] != '/') {
        rc = net_try_direct_tftp_load(&fn_ip);
    }

    net_release(&fn_ip);

    if (rc > 0) {
        sclp_print("Network loading done, starting kernel...\n");
        jump_to_low_kernel();
    }

    panic("Failed to load OS from network\n");
}
