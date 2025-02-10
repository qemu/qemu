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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;

#define true 1
#define false 0
#define PAGE_SIZE 4096

#define EIO     1
#define EBUSY   2
#define ENODEV  3
#define EINVAL  4

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef MIN_NON_ZERO
#define MIN_NON_ZERO(a, b) ((a) == 0 ? (b) : \
                            ((b) == 0 ? (a) : (MIN(a, b))))
#endif

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#include "cio.h"
#include "iplb.h"

/* start.s */
void disabled_wait(void) __attribute__ ((__noreturn__));
void consume_sclp_int(void);
void consume_io_int(void);

/* main.c */
void write_subsystem_identification(void);
void write_iplb_location(void);
unsigned int get_loadparm_index(void);
void main(void);

/* netmain.c */
int netmain(void);

/* sclp.c */
void sclp_print(const char *string);
void sclp_set_write_mask(uint32_t receive_mask, uint32_t send_mask);
void sclp_setup(void);
void sclp_get_loadparm_ascii(char *loadparm);
int sclp_read(char *str, size_t count);

/* virtio.c */
unsigned long virtio_load_direct(unsigned long rec_list1, unsigned long rec_list2,
                                 unsigned long subchan_id, void *load_addr);
bool virtio_is_supported(SubChannelId schid);
int virtio_blk_setup_device(SubChannelId schid);
int virtio_read(unsigned long sector, void *load_addr);

/* bootmap.c */
void zipl_load(void);

/* jump2ipl.c */
void write_reset_psw(uint64_t psw);
int jump_to_IPL_code(uint64_t address);
void jump_to_low_kernel(void);

/* menu.c */
void menu_set_parms(uint8_t boot_menu_flag, uint32_t boot_menu_timeout);
int menu_get_zipl_boot_index(const char *menu_data);
bool menu_is_enabled_zipl(void);
int menu_get_enum_boot_index(bool *valid_entries);
bool menu_is_enabled_enum(void);

#define MAX_BOOT_ENTRIES  31

__attribute__ ((__noreturn__))
static inline void panic(const char *string)
{
    printf("ERROR: %s\n ", string);
    disabled_wait();
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

static inline void debug_print_int(const char *desc, u64 addr)
{
#ifdef DEBUG
    printf("%s 0x%X\n", desc, addr);
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

#define MAX_SECTOR_SIZE 4096

static inline void IPL_assert(bool term, const char *message)
{
    if (!term) {
        panic(message); /* no return */
    }
}

static inline void IPL_check(bool term, const char *message)
{
    if (!term) {
        printf("WARNING: %s\n", message);
    }
}

extern const unsigned char ebc2asc[256];
static inline void ebcdic_to_ascii(const char *src,
                                   char *dst,
                                   unsigned int size)
{
    unsigned int i;

    for (i = 0; i < size; i++) {
        unsigned c = src[i];
        dst[i] = ebc2asc[c];
    }
}

#endif /* S390_CCW_H */
