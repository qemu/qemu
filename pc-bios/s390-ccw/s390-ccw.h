/*
 * S390 CCW boot loader
 *
 * Copyright (c) 2013 Alexander Graf <agraf@suse.de>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef S390_CCW_H
#define S390_CCW_H

/* #define DEBUG */

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;
typedef unsigned long      ulong;
typedef long               size_t;
typedef int                bool;
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef unsigned char      __u8;
typedef unsigned short     __u16;
typedef unsigned int       __u32;
typedef unsigned long long __u64;

#define true 1
#define false 0
#define PAGE_SIZE 4096

#ifndef EIO
#define EIO     1
#endif
#ifndef EBUSY
#define EBUSY   2
#endif
#ifndef NULL
#define NULL    0
#endif

#include "cio.h"
#include "iplb.h"

typedef struct irb Irb;
typedef struct ccw1 Ccw1;
typedef struct cmd_orb CmdOrb;
typedef struct schib Schib;
typedef struct chsc_area_sda ChscAreaSda;
typedef struct senseid SenseId;
typedef struct subchannel_id SubChannelId;

/* start.s */
void disabled_wait(void);
void consume_sclp_int(void);

/* main.c */
void panic(const char *string);
void write_subsystem_identification(void);
extern char stack[PAGE_SIZE * 8] __attribute__((__aligned__(PAGE_SIZE)));

/* sclp-ascii.c */
void sclp_print(const char *string);
void sclp_setup(void);

/* virtio.c */
unsigned long virtio_load_direct(ulong rec_list1, ulong rec_list2,
                                 ulong subchan_id, void *load_addr);
bool virtio_is_supported(SubChannelId schid);
void virtio_setup_device(SubChannelId schid);
int virtio_read(ulong sector, void *load_addr);
int enable_mss_facility(void);
ulong get_second(void);

/* bootmap.c */
void zipl_load(void);

static inline void *memset(void *s, int c, size_t n)
{
    int i;
    unsigned char *p = s;

    for (i = 0; i < n; i++) {
        p[i] = c;
    }

    return s;
}

static inline void fill_hex(char *out, unsigned char val)
{
    const char hex[] = "0123456789abcdef";

    out[0] = hex[(val >> 4) & 0xf];
    out[1] = hex[val & 0xf];
}

static inline void fill_hex_val(char *out, void *ptr, unsigned size)
{
    unsigned char *value = ptr;
    unsigned int i;

    for (i = 0; i < size; i++) {
        fill_hex(&out[i*2], value[i]);
    }
}

static inline void print_int(const char *desc, u64 addr)
{
    char out[] = ": 0xffffffffffffffff\n";

    fill_hex_val(&out[4], &addr, sizeof(addr));

    sclp_print(desc);
    sclp_print(out);
}

static inline void debug_print_int(const char *desc, u64 addr)
{
#ifdef DEBUG
    print_int(desc, addr);
#endif
}

static inline void debug_print_addr(const char *desc, void *p)
{
#ifdef DEBUG
    debug_print_int(desc, (unsigned int)(unsigned long)p);
#endif
}

/***********************************************
 *           Hypercall functions               *
 ***********************************************/

#define KVM_S390_VIRTIO_NOTIFY          0
#define KVM_S390_VIRTIO_RESET           1
#define KVM_S390_VIRTIO_SET_STATUS      2
#define KVM_S390_VIRTIO_CCW_NOTIFY      3

static inline void yield(void)
{
    asm volatile ("diag 0,0,0x44"
                  : :
                  : "memory", "cc");
}

#define MAX_SECTOR_SIZE 4096

static inline void sleep(unsigned int seconds)
{
    ulong target = get_second() + seconds;

    while (get_second() < target) {
        yield();
    }
}

static inline void *memcpy(void *s1, const void *s2, size_t n)
{
    uint8_t *p1 = s1;
    const uint8_t *p2 = s2;

    while (n--) {
        p1[n] = p2[n];
    }
    return s1;
}

static inline void IPL_assert(bool term, const char *message)
{
    if (!term) {
        sclp_print("\n! ");
        sclp_print(message);
        panic(" !\n"); /* no return */
    }
}

static inline void IPL_check(bool term, const char *message)
{
    if (!term) {
        sclp_print("\n! WARNING: ");
        sclp_print(message);
        sclp_print(" !\n");
    }
}

#endif /* S390_CCW_H */
