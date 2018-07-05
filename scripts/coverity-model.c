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
typedef uint64_t hwaddr;
typedef uint32_t MemTxResult;
typedef uint64_t MemTxAttrs;

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

MemTxResult address_space_read(AddressSpace *as, hwaddr addr,
                               MemTxAttrs attrs,
                               uint8_t *buf, int len)
{
    MemTxResult result;
    // TODO: investigate impact of treating reads as producing
    // tainted data, with __coverity_tainted_data_argument__(buf).
    __bufwrite(buf, len);
    return result;
}

MemTxResult address_space_write(AddressSpace *as, hwaddr addr,
                                MemTxAttrs attrs,
                                const uint8_t *buf, int len)
{
    MemTxResult result;
    __bufread(buf, len);
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
    size_t sz;
    void *ptr;

    __coverity_negative_sink__(nmemb);
    __coverity_negative_sink__(size);
    sz = nmemb * size;
    ptr = __coverity_alloc__(sz);
    __coverity_mark_as_uninitialized_buffer__(ptr);
    __coverity_mark_as_afm_allocated__(ptr, "g_free");
    return ptr;
}

void *g_malloc0_n(size_t nmemb, size_t size)
{
    size_t sz;
    void *ptr;

    __coverity_negative_sink__(nmemb);
    __coverity_negative_sink__(size);
    sz = nmemb * size;
    ptr = __coverity_alloc__(sz);
    __coverity_writeall0__(ptr);
    __coverity_mark_as_afm_allocated__(ptr, "g_free");
    return ptr;
}

void *g_realloc_n(void *ptr, size_t nmemb, size_t size)
{
    size_t sz;

    __coverity_negative_sink__(nmemb);
    __coverity_negative_sink__(size);
    sz = nmemb * size;
    __coverity_escape__(ptr);
    ptr = __coverity_alloc__(sz);
    /*
     * Memory beyond the old size isn't actually initialized.  Can't
     * model that.  See Coverity's realloc() model
     */
    __coverity_writeall__(ptr);
    __coverity_mark_as_afm_allocated__(ptr, "g_free");
    return ptr;
}

void g_free(void *ptr)
{
    __coverity_free__(ptr);
    __coverity_mark_as_afm_freed__(ptr, "g_free");
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

/* Trivially derive the g_FOO() from the g_FOO_n() */

void *g_malloc(size_t size)
{
    return g_malloc_n(1, size);
}

void *g_malloc0(size_t size)
{
    return g_malloc0_n(1, size);
}

void *g_realloc(void *ptr, size_t size)
{
    return g_realloc_n(ptr, 1, size);
}

void *g_try_malloc(size_t size)
{
    return g_try_malloc_n(1, size);
}

void *g_try_malloc0(size_t size)
{
    return g_try_malloc0_n(1, size);
}

void *g_try_realloc(void *ptr, size_t size)
{
    return g_try_realloc_n(ptr, 1, size);
}

/* Other memory allocation functions */

void *g_memdup(const void *ptr, unsigned size)
{
    unsigned char *dup;
    unsigned i;

    if (!ptr) {
        return NULL;
    }

    dup = g_malloc(size);
    for (i = 0; i < size; i++)
        dup[i] = ((unsigned char *)ptr)[i];
    return dup;
}

/*
 * GLib string allocation functions
 */

char *g_strdup(const char *s)
{
    char *dup;
    size_t i;

    if (!s) {
        return NULL;
    }

    __coverity_string_null_sink__(s);
    __coverity_string_size_sink__(s);
    dup = __coverity_alloc_nosize__();
    __coverity_mark_as_afm_allocated__(dup, "g_free");
    for (i = 0; (dup[i] = s[i]); i++) ;
    return dup;
}

char *g_strndup(const char *s, size_t n)
{
    char *dup;
    size_t i;

    __coverity_negative_sink__(n);

    if (!s) {
        return NULL;
    }

    dup = g_malloc(n + 1);
    for (i = 0; i < n && (dup[i] = s[i]); i++) ;
    dup[i] = 0;
    return dup;
}

char *g_strdup_printf(const char *format, ...)
{
    char ch, *s;
    size_t len;

    __coverity_string_null_sink__(format);
    __coverity_string_size_sink__(format);

    ch = *format;

    s = __coverity_alloc_nosize__();
    __coverity_writeall__(s);
    __coverity_mark_as_afm_allocated__(s, "g_free");
    return s;
}

char *g_strdup_vprintf(const char *format, va_list ap)
{
    char ch, *s;
    size_t len;

    __coverity_string_null_sink__(format);
    __coverity_string_size_sink__(format);

    ch = *format;
    ch = *(char *)ap;

    s = __coverity_alloc_nosize__();
    __coverity_writeall__(s);
    __coverity_mark_as_afm_allocated__(s, "g_free");

    return len;
}

char *g_strconcat(const char *s, ...)
{
    char *s;

    /*
     * Can't model: last argument must be null, the others
     * null-terminated strings
     */

    s = __coverity_alloc_nosize__();
    __coverity_writeall__(s);
    __coverity_mark_as_afm_allocated__(s, "g_free");
    return s;
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
