/* Coverity Scan model
 *
 * Copyright (C) 2014 Red Hat, Inc.
 *
 * Authors:
 *  Markus Armbruster <armbru@redhat.com>
 *  Paolo Bonzini <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or, at your
 * option, any later version.  See the COPYING file in the top-level directory.
 */


/*
 * This is the source code for our Coverity user model file.  The
 * purpose of user models is to increase scanning accuracy by explaining
 * code Coverity can't see (out of tree libraries) or doesn't
 * sufficiently understand.  Better accuracy means both fewer false
 * positives and more true defects.  Memory leaks in particular.
 *
 * - A model file can't import any header files.  Some built-in primitives are
 *   available but not wchar_t, NULL etc.
 * - Modeling doesn't need full structs and typedefs. Rudimentary structs
 *   and similar types are sufficient.
 * - An uninitialized local variable signifies that the variable could be
 *   any value.
 *
 * The model file must be uploaded by an admin in the analysis settings of
 * http://scan.coverity.com/projects/378
 */

#define NULL ((void *)0)

typedef unsigned char uint8_t;
typedef char int8_t;
typedef unsigned int uint32_t;
typedef int int32_t;
typedef long ssize_t;
typedef unsigned long long uint64_t;
typedef long long int64_t;
typedef _Bool bool;

/* exec.c */

typedef struct AddressSpace AddressSpace;
typedef uint64_t hwaddr;

static void __write(uint8_t *buf, ssize_t len)
{
    int first, last;
    __coverity_negative_sink__(len);
    if (len == 0) return;
    buf[0] = first;
    buf[len-1] = last;
    __coverity_writeall__(buf);
}

static void __read(uint8_t *buf, ssize_t len)
{
    __coverity_negative_sink__(len);
    if (len == 0) return;
    int first = buf[0];
    int last = buf[len-1];
}

bool address_space_rw(AddressSpace *as, hwaddr addr, uint8_t *buf,
                      int len, bool is_write)
{
    bool result;

    // TODO: investigate impact of treating reads as producing
    // tainted data, with __coverity_tainted_data_argument__(buf).
    if (is_write) __write(buf, len); else __read(buf, len);

    return result;
}

/* Tainting */

typedef struct {} name2keysym_t;
static int get_keysym(const name2keysym_t *table,
                      const char *name)
{
    int result;
    if (result > 0) {
        __coverity_tainted_string_sanitize_content__(name);
        return result;
    } else {
        return 0;
    }
}

/* glib memory allocation functions.
 *
 * Note that we ignore the fact that g_malloc of 0 bytes returns NULL,
 * and g_realloc of 0 bytes frees the pointer.
 *
 * Modeling this would result in Coverity flagging a lot of memory
 * allocations as potentially returning NULL, and asking us to check
 * whether the result of the allocation is NULL or not.  However, the
 * resulting pointer should never be dereferenced anyway, and in fact
 * it is not in the vast majority of cases.
 *
 * If a dereference did happen, this would suppress a defect report
 * for an actual null pointer dereference.  But it's too unlikely to
 * be worth wading through the false positives, and with some luck
 * we'll get a buffer overflow reported anyway.
 */

void *malloc(size_t);
void *calloc(size_t, size_t);
void *realloc(void *, size_t);
void free(void *);

void *
g_malloc(size_t n_bytes)
{
    void *mem;
    __coverity_negative_sink__(n_bytes);
    mem = malloc(n_bytes == 0 ? 1 : n_bytes);
    if (!mem) __coverity_panic__();
    return mem;
}

void *
g_malloc0(size_t n_bytes)
{
    void *mem;
    __coverity_negative_sink__(n_bytes);
    mem = calloc(1, n_bytes == 0 ? 1 : n_bytes);
    if (!mem) __coverity_panic__();
    return mem;
}

void g_free(void *mem)
{
    free(mem);
}

void *g_realloc(void * mem, size_t n_bytes)
{
    __coverity_negative_sink__(n_bytes);
    mem = realloc(mem, n_bytes == 0 ? 1 : n_bytes);
    if (!mem) __coverity_panic__();
    return mem;
}

void *g_try_malloc(size_t n_bytes)
{
    __coverity_negative_sink__(n_bytes);
    return malloc(n_bytes == 0 ? 1 : n_bytes);
}

void *g_try_malloc0(size_t n_bytes)
{
    __coverity_negative_sink__(n_bytes);
    return calloc(1, n_bytes == 0 ? 1 : n_bytes);
}

void *g_try_realloc(void *mem, size_t n_bytes)
{
    __coverity_negative_sink__(n_bytes);
    return realloc(mem, n_bytes == 0 ? 1 : n_bytes);
}

/* Other glib functions */

typedef struct _GIOChannel GIOChannel;
GIOChannel *g_io_channel_unix_new(int fd)
{
    GIOChannel *c = g_malloc0(sizeof(GIOChannel));
    __coverity_escape__(fd);
    return c;
}

void g_assertion_message_expr(const char     *domain,
                              const char     *file,
                              int             line,
                              const char     *func,
                              const char     *expr)
{
    __coverity_panic__();
}
