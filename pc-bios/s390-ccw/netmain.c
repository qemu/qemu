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
#include <pxelinux.h>

#include "s390-ccw.h"
#include "cio.h"
#include "virtio.h"
#include "s390-time.h"

#define DEFAULT_BOOT_RETRIES 10
#define DEFAULT_TFTP_RETRIES 20

extern char _start[];

#define KERNEL_ADDR             ((void *)0L)
#define KERNEL_MAX_SIZE         ((long)_start)
#define ARCH_COMMAND_LINE_SIZE  896              /* Taken from Linux kernel */

/* STSI 3.2.2 offset of first vmdb + offset of uuid inside vmdb */
#define STSI322_VMDB_UUID_OFFSET ((8 + 12) * 4)

static char cfgbuf[2048];

SubChannelId net_schid = { .one = 1 };
static uint8_t mac[6];
static uint64_t dest_timer;

void set_timer(int val)
{
    dest_timer = get_time_ms() + val;
}

int get_timer(void)
{
    return dest_timer - get_time_ms();
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
        fn_ip->ip_version = 4;
        rc = dhcpv4(NULL, fn_ip);
        if (rc == -1) {
            fn_ip->ip_version = 6;
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

    rc = tftp(fnip, buffer, len, DEFAULT_TFTP_RETRIES, &tftp_err);

    if (rc < 0) {
        /* Make sure that error messages are put into a new line */
        printf("\n  ");
    }

    if (rc > 1024) {
        printf("  TFTP: Received %s (%d KBytes)\n", fnip->filename, rc / 1024);
    } else if (rc > 0) {
        printf("  TFTP: Received %s (%d Bytes)\n", fnip->filename, rc);
    } else {
        const char *errstr = NULL;
        int ecode;
        tftp_get_error_info(fnip, &tftp_err, rc, &errstr, &ecode);
        printf("TFTP error: %s\n", errstr ? errstr : "unknown error");
    }

    return rc;
}

static int net_init_ip(filename_ip_t *fn_ip)
{
    int rc;

    printf("  Using MAC address: %02x:%02x:%02x:%02x:%02x:%02x\n",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    set_mac_address(mac);    /* init ethernet layer */
    seed_rng(mac);

    rc = dhcp(fn_ip, DEFAULT_BOOT_RETRIES);
    if (rc >= 0) {
        if (fn_ip->ip_version == 4) {
            set_ipv4_address(fn_ip->own_ip);
        }
    } else if (rc == -2) {
        printf("ARP request to TFTP server (%d.%d.%d.%d) failed\n",
               (fn_ip->server_ip >> 24) & 0xFF, (fn_ip->server_ip >> 16) & 0xFF,
               (fn_ip->server_ip >>  8) & 0xFF, fn_ip->server_ip & 0xFF);
        return -102;
    } else if (rc == -4 || rc == -3) {
        puts("Can't obtain TFTP server IP address");
        return -107;
    } else {
        puts("Could not get IP address");
        return -101;
    }

    if (fn_ip->ip_version == 4) {
        printf("  Using IPv4 address: %d.%d.%d.%d\n",
              (fn_ip->own_ip >> 24) & 0xFF, (fn_ip->own_ip >> 16) & 0xFF,
              (fn_ip->own_ip >>  8) & 0xFF, fn_ip->own_ip & 0xFF);
    } else if (fn_ip->ip_version == 6) {
        char ip6_str[40];
        ipv6_to_str(fn_ip->own_ip6.addr, ip6_str);
        printf("  Using IPv6 address: %s\n", ip6_str);
    }

    printf("  Using TFTP server: ");
    if (fn_ip->ip_version == 4) {
        printf("%d.%d.%d.%d\n",
               (fn_ip->server_ip >> 24) & 0xFF, (fn_ip->server_ip >> 16) & 0xFF,
               (fn_ip->server_ip >>  8) & 0xFF, fn_ip->server_ip & 0xFF);
    } else if (fn_ip->ip_version == 6) {
        char ip6_str[40];
        ipv6_to_str(fn_ip->server_ip6.addr, ip6_str);
        printf("%s\n", ip6_str);
    }

    if (strlen(fn_ip->filename) > 0) {
        printf("  Bootfile name: '%s'\n", fn_ip->filename);
    }

    return rc;
}

static int net_init(filename_ip_t *fn_ip)
{
    int rc;

    memset(fn_ip, 0, sizeof(filename_ip_t));

    rc = virtio_net_init(mac);
    if (rc < 0) {
        puts("Could not initialize network device");
        return -101;
    }
    fn_ip->fd = rc;

    rc = net_init_ip(fn_ip);
    if (rc < 0) {
        virtio_net_deinit();
    }

    return rc;
}

static void net_release(filename_ip_t *fn_ip)
{
    if (fn_ip->ip_version == 4) {
        dhcp_send_release(fn_ip->fd);
    }
    virtio_net_deinit();
}

/**
 * Retrieve the Universally Unique Identifier of the VM.
 * @return UUID string, or NULL in case of errors
 */
static const char *get_uuid(void)
{
    register int r0 asm("0");
    register int r1 asm("1");
    uint8_t *mem, *buf, uuid[16];
    int i, cc, chk = 0;
    static char uuid_str[37];

    mem = malloc(2 * PAGE_SIZE);
    if (!mem) {
        puts("Out of memory ... can not get UUID.");
        return NULL;
    }
    buf = (uint8_t *)(((uint64_t)mem + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));
    memset(buf, 0, PAGE_SIZE);

    /* Get SYSIB 3.2.2 */
    r0 = (3 << 28) | 2;
    r1 = 2;
    asm volatile(" stsi 0(%[addr])\n"
                 " ipm  %[cc]\n"
                 " srl  %[cc],28\n"
                 : [cc] "=d" (cc)
                 : "d" (r0), "d" (r1), [addr] "a" (buf)
                 : "cc", "memory");
    if (cc) {
        free(mem);
        return NULL;
    }

    for (i = 0; i < 16; i++) {
        uuid[i] = buf[STSI322_VMDB_UUID_OFFSET + i];
        chk |= uuid[i];
    }
    free(mem);
    if (!chk) {
        return NULL;
    }

    sprintf(uuid_str, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
            "%02x%02x%02x%02x%02x%02x", uuid[0], uuid[1], uuid[2], uuid[3],
            uuid[4], uuid[5], uuid[6], uuid[7], uuid[8], uuid[9], uuid[10],
            uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);

    return uuid_str;
}

/**
 * Load a kernel with initrd (i.e. with the information that we've got from
 * a pxelinux.cfg config file)
 */
static int load_kernel_with_initrd(filename_ip_t *fn_ip,
                                   struct pl_cfg_entry *entry)
{
    int rc;

    printf("Loading pxelinux.cfg entry '%s'\n", entry->label);

    if (!entry->kernel) {
        puts("Kernel entry is missing!\n");
        return -1;
    }

    strncpy(fn_ip->filename, entry->kernel, sizeof(fn_ip->filename));
    rc = tftp_load(fn_ip, KERNEL_ADDR, KERNEL_MAX_SIZE);
    if (rc < 0) {
        return rc;
    }

    if (entry->initrd) {
        uint64_t iaddr = (rc + 0xfff) & ~0xfffUL;

        strncpy(fn_ip->filename, entry->initrd, sizeof(fn_ip->filename));
        rc = tftp_load(fn_ip, (void *)iaddr, KERNEL_MAX_SIZE - iaddr);
        if (rc < 0) {
            return rc;
        }
        /* Patch location and size: */
        *(uint64_t *)0x10408 = iaddr;
        *(uint64_t *)0x10410 = rc;
        rc += iaddr;
    }

    if (entry->append) {
        strncpy((char *)0x10480, entry->append, ARCH_COMMAND_LINE_SIZE);
    }

    return rc;
}

static int net_boot_menu(int num_ent, int def_ent,
                         struct pl_cfg_entry *entries)
{
    bool valid_entries[MAX_BOOT_ENTRIES] = { false };
    int idx;

    puts("\ns390-ccw pxelinux.cfg boot menu:\n");
    printf(" [0] default (%d)\n", def_ent + 1);
    valid_entries[0] = true;

    for (idx = 1; idx <= num_ent; idx++) {
        printf(" [%d] %s\n", idx, entries[idx - 1].label);
        valid_entries[idx] = true;
    }
    putchar('\n');

    idx = menu_get_boot_index(valid_entries);
    putchar('\n');

    return idx;
}

static int net_select_and_load_kernel(filename_ip_t *fn_ip,
                                      int num_ent, int selected,
                                      struct pl_cfg_entry *entries)
{
    unsigned int loadparm = get_loadparm_index();

    if (num_ent <= 0) {
        return -1;
    }

    if (menu_is_enabled_enum() && num_ent > 1) {
        loadparm = net_boot_menu(num_ent, selected, entries);
    }

    IPL_assert(loadparm <= num_ent,
               "loadparm is set to an entry that is not available in the "
               "pxelinux.cfg file!");

    if (loadparm > 0) {
        selected = loadparm - 1;
    }

    return load_kernel_with_initrd(fn_ip, &entries[selected]);
}

static int net_try_pxelinux_cfg(filename_ip_t *fn_ip)
{
    struct pl_cfg_entry entries[MAX_BOOT_ENTRIES];
    int num_ent, def_ent = 0;

    num_ent = pxelinux_load_parse_cfg(fn_ip, mac, get_uuid(),
                                      DEFAULT_TFTP_RETRIES,
                                      cfgbuf, sizeof(cfgbuf),
                                      entries, MAX_BOOT_ENTRIES, &def_ent);

    return net_select_and_load_kernel(fn_ip, num_ent, def_ent, entries);
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
        strncpy(fn_ip->filename, insbuf, sizeof(fn_ip->filename));
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
        /*
         * pxelinux.cfg support via bootfile name is just here for developers'
         * convenience (it eases testing with the built-in DHCP server of QEMU
         * that does not support RFC 5071). The official way to configure a
         * pxelinux.cfg file name is to use DHCP options 209 and 210 instead.
         * So only use the pxelinux.cfg parser here for files that start with
         * a magic comment string.
         */
        if (!strncasecmp("# pxelinux", cfgbuf, 10)) {
            struct pl_cfg_entry entries[MAX_BOOT_ENTRIES];
            int num_ent, def_ent = 0;

            num_ent = pxelinux_parse_cfg(cfgbuf, sizeof(cfgbuf), entries,
                                         MAX_BOOT_ENTRIES, &def_ent);
            return net_select_and_load_kernel(fn_ip, num_ent, def_ent,
                                              entries);
        }
    }

    /* Move kernel to right location */
    memmove(KERNEL_ADDR, loadaddr, rc);

    return rc;
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
        enable_subchannel(net_schid);
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

static bool virtio_setup(void)
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

    if (have_iplb || store_iplb(&iplb)) {
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

    return found;
}

int netmain(void)
{
    filename_ip_t fn_ip;
    int rc, fnlen;

    sclp_setup();
    puts("Network boot starting...");

    if (!virtio_setup()) {
        puts("No virtio net device found.");
        return -1;
    }

    rc = net_init(&fn_ip);
    if (rc) {
        puts("Network initialization failed.");
        return -1;
    }

    fnlen = strlen(fn_ip.filename);
    if (fnlen > 0 && fn_ip.filename[fnlen - 1] != '/') {
        rc = net_try_direct_tftp_load(&fn_ip);
    }
    if (rc <= 0) {
        rc = net_try_pxelinux_cfg(&fn_ip);
    }

    net_release(&fn_ip);

    if (rc > 0) {
        puts("Network loading done, starting kernel...");
        jump_to_low_kernel();
    }

    puts("Failed to load OS from network.");
    return -1;
}
