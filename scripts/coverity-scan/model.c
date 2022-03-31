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

typedef struct va_list_str *va_list;

/* exec.c */

typedef struct AddressSpace AddressSpace;
typedef struct MemoryRegionCache MemoryRegionCache;
typedef uint64_t hwaddr;
typedef uint32_t MemTxResult;
typedef struct MemTxAttrs {} MemTxAttrs;

static void __bufwrite(uint8_t *buf, ssize_t len)
{
    int first, last;
    __coverity_negative_sink__(len);
    if (len == 0) return;
    buf[0] = first;
    buf[len-1] = last;
    __coverity_writeall__(buf);
}

static void __bufread(uint8_t *buf, ssize_t len)
{
    __coverity_negative_sink__(len);
    if (len == 0) return;
    int first = buf[0];
    int last = buf[len-1];
}

MemTxResult address_space_read_cached(MemoryRegionCache *cache, hwaddr addr,
                                      MemTxAttrs attrs,
                                      void *buf, int len)
{
    MemTxResult result;
    // TODO: investigate impact of treating reads as producing
    // tainted data, with __coverity_tainted_data_argument__(buf).
    __bufwrite(buf, len);
    return result;
}

MemTxResult address_space_write_cached(MemoryRegionCache *cache, hwaddr addr,
                                MemTxAttrs attrs,
                                const void *buf, int len)
{
    MemTxResult result;
    __bufread(buf, len);
    return result;
}

MemTxResult address_space_rw_cached(MemoryRegionCache *cache, hwaddr addr,
                                    MemTxAttrs attrs,
                                    void *buf, int len, bool is_write)
{
    if (is_write) {
        return address_space_write_cached(cache, addr, attrs, buf, len);
    } else {
        return address_space_read_cached(cache, addr, attrs, buf, len);
    }
}

MemTxResult address_space_read(AddressSpace *as, hwaddr addr,
                               MemTxAttrs attrs,
                               void *buf, int len)
{
    MemTxResult result;
    // TODO: investigate impact of treating reads as producing
    // tainted data, with __coverity_tainted_data_argument__(buf).
    __bufwrite(buf, len);
    return result;
}

MemTxResult address_space_write(AddressSpace *as, hwaddr addr,
                                MemTxAttrs attrs,
                                const void *buf, int len)
{
    MemTxResult result;
    __bufread(buf, len);
    return result;
}

MemTxResult address_space_rw(AddressSpace *as, hwaddr addr,
                             MemTxAttrs attrs,
                             void *buf, int len, bool is_write)
{
    if (is_write) {
        return address_space_write(as, addr, attrs, buf, len);
    } else {
        return address_space_read(as, addr, attrs, buf, len);
    }
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

/* Replay data is considered trusted.  */
uint8_t replay_get_byte(void)
{
    uint8_t byte;
    return byte;
}


/*
 * GLib memory allocation functions.
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

/*
 * Allocation primitives, cannot return NULL
 * See also Coverity's library/generic/libc/all/all.c
 */

void *g_malloc_n(size_t nmemb, size_t size)
{
    void *ptr;

    __coverity_negative_sink__(nmemb);
    __coverity_negative_sink__(size);
    ptr = __coverity_alloc__(nmemb * size);
    if (!ptr) {
        __coverity_panic__();
    }
    __coverity_mark_as_uninitialized_buffer__(ptr);
    __coverity_mark_as_afm_allocated__(ptr, AFM_free);
    return ptr;
}

void *g_malloc0_n(size_t nmemb, size_t size)
{
    void *ptr;

    __coverity_negative_sink__(nmemb);
    __coverity_negative_sink__(size);
    ptr = __coverity_alloc__(nmemb * size);
    if (!ptr) {
        __coverity_panic__();
    }
    __coverity_writeall0__(ptr);
    __coverity_mark_as_afm_allocated__(ptr, AFM_free);
    return ptr;
}

void *g_realloc_n(void *ptr, size_t nmemb, size_t size)
{
    __coverity_negative_sink__(nmemb);
    __coverity_negative_sink__(size);
    __coverity_escape__(ptr);
    ptr = __coverity_alloc__(nmemb * size);
    if (!ptr) {
        __coverity_panic__();
    }
    /*
     * Memory beyond the old size isn't actually initialized.  Can't
     * model that.  See Coverity's realloc() model
     */
    __coverity_writeall__(ptr);
    __coverity_mark_as_afm_allocated__(ptr, AFM_free);
    return ptr;
}

void g_free(void *ptr)
{
    __coverity_free__(ptr);
    __coverity_mark_as_afm_freed__(ptr, AFM_free);
}

/*
 * Derive the g_try_FOO_n() from the g_FOO_n() by adding indeterminate
 * out of memory conditions
 */

void *g_try_malloc_n(size_t nmemb, size_t size)
{
    int nomem;

    if (nomem) {
        return NULL;
    }
    return g_malloc_n(nmemb, size);
}

void *g_try_malloc0_n(size_t nmemb, size_t size)
{
    int nomem;

    if (nomem) {
        return NULL;
    }
    return g_malloc0_n(nmemb, size);
}

void *g_try_realloc_n(void *ptr, size_t nmemb, size_t size)
{
    int nomem;

    if (nomem) {
        return NULL;
    }
    return g_realloc_n(ptr, nmemb, size);
}

/* Derive the g_FOO() from the g_FOO_n() */

void *g_malloc(size_t size)
{
    void *ptr;

    __coverity_negative_sink__(size);
    ptr = __coverity_alloc__(size);
    if (!ptr) {
        __coverity_panic__();
    }
    __coverity_mark_as_uninitialized_buffer__(ptr);
    __coverity_mark_as_afm_allocated__(ptr, AFM_free);
    return ptr;
}

void *g_malloc0(size_t size)
{
    void *ptr;

    __coverity_negative_sink__(size);
    ptr = __coverity_alloc__(size);
    if (!ptr) {
        __coverity_panic__();
    }
    __coverity_writeall0__(ptr);
    __coverity_mark_as_afm_allocated__(ptr, AFM_free);
    return ptr;
}

void *g_realloc(void *ptr, size_t size)
{
    __coverity_negative_sink__(size);
    __coverity_escape__(ptr);
    ptr = __coverity_alloc__(size);
    if (!ptr) {
        __coverity_panic__();
    }
    /*
     * Memory beyond the old size isn't actually initialized.  Can't
     * model that.  See Coverity's realloc() model
     */
    __coverity_writeall__(ptr);
    __coverity_mark_as_afm_allocated__(ptr, AFM_free);
    return ptr;
}

void *g_try_malloc(size_t size)
{
    int nomem;

    if (nomem) {
        return NULL;
    }
    return g_malloc(size);
}

void *g_try_malloc0(size_t size)
{
    int nomem;

    if (nomem) {
        return NULL;
    }
    return g_malloc0(size);
}

void *g_try_realloc(void *ptr, size_t size)
{
    int nomem;

    if (nomem) {
        return NULL;
    }
    return g_realloc(ptr, size);
}

/* Other glib functions */

typedef struct pollfd GPollFD;

int poll();

int g_poll (GPollFD *fds, unsigned nfds, int timeout)
{
    return poll(fds, nfds, timeout);
}

typedef struct _GIOChannel GIOChannel;
GIOChannel *g_io_channel_unix_new(int fd)
{
    /* cannot use incomplete type, the actual struct is roughly this size.  */
    GIOChannel *c = g_malloc0(20 * sizeof(void *));
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
