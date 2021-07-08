/*
 * Virtual Open Firmware
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdarg.h>

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned long uint32_t;
typedef unsigned long long uint64_t;
#define NULL (0)
typedef unsigned long ihandle;
typedef unsigned long phandle;
typedef int size_t;

/* globals */
extern void _prom_entry(void); /* OF CI entry point (i.e. this firmware) */

void do_boot(unsigned long addr, unsigned long r3, unsigned long r4);

/* libc */
int strlen(const char *s);
int strcmp(const char *s1, const char *s2);
void *memcpy(void *dest, const void *src, size_t n);
int memcmp(const void *ptr1, const void *ptr2, size_t n);
void *memmove(void *dest, const void *src, size_t n);
void *memset(void *dest, int c, size_t size);

/* CI wrappers */
void ci_panic(const char *str);
phandle ci_finddevice(const char *path);
uint32_t ci_getprop(phandle ph, const char *propname, void *prop, int len);

/* booting from -kernel */
void boot_from_memory(uint64_t initrd, uint64_t initrdsize);

/* Entry points for CI and RTAS */
extern uint32_t ci_entry(uint32_t params);
extern unsigned long hv_rtas(unsigned long params);
extern unsigned int hv_rtas_size;
