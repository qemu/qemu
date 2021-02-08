/*
 * Simple C functions to supplement the C library
 *
 * Copyright (c) 2006 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include <math.h>

#include "qemu-common.h"
#include "qemu/sockets.h"
#include "qemu/iov.h"
#include "net/net.h"
#include "qemu/ctype.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"

void strpadcpy(char *buf, int buf_size, const char *str, char pad)
{
    int len = qemu_strnlen(str, buf_size);
    memcpy(buf, str, len);
    memset(buf + len, pad, buf_size - len);
}

void pstrcpy(char *buf, int buf_size, const char *str)
{
    int c;
    char *q = buf;

    if (buf_size <= 0)
        return;

    for(;;) {
        c = *str++;
        if (c == 0 || q >= buf + buf_size - 1)
            break;
        *q++ = c;
    }
    *q = '\0';
}

/* strcat and truncate. */
char *pstrcat(char *buf, int buf_size, const char *s)
{
    int len;
    len = strlen(buf);
    if (len < buf_size)
        pstrcpy(buf + len, buf_size - len, s);
    return buf;
}

int strstart(const char *str, const char *val, const char **ptr)
{
    const char *p, *q;
    p = str;
    q = val;
    while (*q != '\0') {
        if (*p != *q)
            return 0;
        p++;
        q++;
    }
    if (ptr)
        *ptr = p;
    return 1;
}

int stristart(const char *str, const char *val, const char **ptr)
{
    const char *p, *q;
    p = str;
    q = val;
    while (*q != '\0') {
        if (qemu_toupper(*p) != qemu_toupper(*q))
            return 0;
        p++;
        q++;
    }
    if (ptr)
        *ptr = p;
    return 1;
}

/* XXX: use host strnlen if available ? */
int qemu_strnlen(const char *s, int max_len)
{
    int i;

    for(i = 0; i < max_len; i++) {
        if (s[i] == '\0') {
            break;
        }
    }
    return i;
}

char *qemu_strsep(char **input, const char *delim)
{
    char *result = *input;
    if (result != NULL) {
        char *p;

        for (p = result; *p != '\0'; p++) {
            if (strchr(delim, *p)) {
                break;
            }
        }
        if (*p == '\0') {
            *input = NULL;
        } else {
            *p = '\0';
            *input = p + 1;
        }
    }
    return result;
}

time_t mktimegm(struct tm *tm)
{
    time_t t;
    int y = tm->tm_year + 1900, m = tm->tm_mon + 1, d = tm->tm_mday;
    if (m < 3) {
        m += 12;
        y--;
    }
    t = 86400ULL * (d + (153 * m - 457) / 5 + 365 * y + y / 4 - y / 100 + 
                 y / 400 - 719469);
    t += 3600 * tm->tm_hour + 60 * tm->tm_min + tm->tm_sec;
    return t;
}

/*
 * Make sure data goes on disk, but if possible do not bother to
 * write out the inode just for timestamp updates.
 *
 * Unfortunately even in 2009 many operating systems do not support
 * fdatasync and have to fall back to fsync.
 */
int qemu_fdatasync(int fd)
{
#ifdef CONFIG_FDATASYNC
    return fdatasync(fd);
#else
    return fsync(fd);
#endif
}

/**
 * Sync changes made to the memory mapped file back to the backing
 * storage. For POSIX compliant systems this will fallback
 * to regular msync call. Otherwise it will trigger whole file sync
 * (including the metadata case there is no support to skip that otherwise)
 *
 * @addr   - start of the memory area to be synced
 * @length - length of the are to be synced
 * @fd     - file descriptor for the file to be synced
 *           (mandatory only for POSIX non-compliant systems)
 */
int qemu_msync(void *addr, size_t length, int fd)
{
#ifdef CONFIG_POSIX
    size_t align_mask = ~(qemu_real_host_page_size - 1);

    /**
     * There are no strict reqs as per the length of mapping
     * to be synced. Still the length needs to follow the address
     * alignment changes. Additionally - round the size to the multiple
     * of PAGE_SIZE
     */
    length += ((uintptr_t)addr & (qemu_real_host_page_size - 1));
    length = (length + ~align_mask) & align_mask;

    addr = (void *)((uintptr_t)addr & align_mask);

    return msync(addr, length, MS_SYNC);
#else /* CONFIG_POSIX */
    /**
     * Perform the sync based on the file descriptor
     * The sync range will most probably be wider than the one
     * requested - but it will still get the job done
     */
    return qemu_fdatasync(fd);
#endif /* CONFIG_POSIX */
}

#ifndef _WIN32
/* Sets a specific flag */
int fcntl_setfl(int fd, int flag)
{
    int flags;

    flags = fcntl(fd, F_GETFL);
    if (flags == -1)
        return -errno;

    if (fcntl(fd, F_SETFL, flags | flag) == -1)
        return -errno;

    return 0;
}
#endif

static int64_t suffix_mul(char suffix, int64_t unit)
{
    switch (qemu_toupper(suffix)) {
    case 'B':
        return 1;
    case 'K':
        return unit;
    case 'M':
        return unit * unit;
    case 'G':
        return unit * unit * unit;
    case 'T':
        return unit * unit * unit * unit;
    case 'P':
        return unit * unit * unit * unit * unit;
    case 'E':
        return unit * unit * unit * unit * unit * unit;
    }
    return -1;
}

/*
 * Convert string to bytes, allowing either B/b for bytes, K/k for KB,
 * M/m for MB, G/g for GB or T/t for TB. End pointer will be returned
 * in *end, if not NULL. Return -ERANGE on overflow, and -EINVAL on
 * other error.
 */
static int do_strtosz(const char *nptr, const char **end,
                      const char default_suffix, int64_t unit,
                      uint64_t *result)
{
    int retval;
    const char *endptr;
    unsigned char c;
    int mul_required = 0;
    double val, mul, integral, fraction;

    retval = qemu_strtod_finite(nptr, &endptr, &val);
    if (retval) {
        goto out;
    }
    fraction = modf(val, &integral);
    if (fraction != 0) {
        mul_required = 1;
    }
    c = *endptr;
    mul = suffix_mul(c, unit);
    if (mul >= 0) {
        endptr++;
    } else {
        mul = suffix_mul(default_suffix, unit);
        assert(mul >= 0);
    }
    if (mul == 1 && mul_required) {
        retval = -EINVAL;
        goto out;
    }
    /*
     * Values near UINT64_MAX overflow to 2**64 when converting to double
     * precision.  Compare against the maximum representable double precision
     * value below 2**64, computed as "the next value after 2**64 (0x1p64) in
     * the direction of 0".
     */
    if ((val * mul > nextafter(0x1p64, 0)) || val < 0) {
        retval = -ERANGE;
        goto out;
    }
    *result = val * mul;
    retval = 0;

out:
    if (end) {
        *end = endptr;
    } else if (*endptr) {
        retval = -EINVAL;
    }

    return retval;
}

int qemu_strtosz(const char *nptr, const char **end, uint64_t *result)
{
    return do_strtosz(nptr, end, 'B', 1024, result);
}

int qemu_strtosz_MiB(const char *nptr, const char **end, uint64_t *result)
{
    return do_strtosz(nptr, end, 'M', 1024, result);
}

int qemu_strtosz_metric(const char *nptr, const char **end, uint64_t *result)
{
    return do_strtosz(nptr, end, 'B', 1000, result);
}

/**
 * Helper function for error checking after strtol() and the like
 */
static int check_strtox_error(const char *nptr, char *ep,
                              const char **endptr, int libc_errno)
{
    assert(ep >= nptr);
    if (endptr) {
        *endptr = ep;
    }

    /* Turn "no conversion" into an error */
    if (libc_errno == 0 && ep == nptr) {
        return -EINVAL;
    }

    /* Fail when we're expected to consume the string, but didn't */
    if (!endptr && *ep) {
        return -EINVAL;
    }

    return -libc_errno;
}

/**
 * Convert string @nptr to an integer, and store it in @result.
 *
 * This is a wrapper around strtol() that is harder to misuse.
 * Semantics of @nptr, @endptr, @base match strtol() with differences
 * noted below.
 *
 * @nptr may be null, and no conversion is performed then.
 *
 * If no conversion is performed, store @nptr in *@endptr and return
 * -EINVAL.
 *
 * If @endptr is null, and the string isn't fully converted, return
 * -EINVAL.  This is the case when the pointer that would be stored in
 * a non-null @endptr points to a character other than '\0'.
 *
 * If the conversion overflows @result, store INT_MAX in @result,
 * and return -ERANGE.
 *
 * If the conversion underflows @result, store INT_MIN in @result,
 * and return -ERANGE.
 *
 * Else store the converted value in @result, and return zero.
 */
int qemu_strtoi(const char *nptr, const char **endptr, int base,
                int *result)
{
    char *ep;
    long long lresult;

    assert((unsigned) base <= 36 && base != 1);
    if (!nptr) {
        if (endptr) {
            *endptr = nptr;
        }
        return -EINVAL;
    }

    errno = 0;
    lresult = strtoll(nptr, &ep, base);
    if (lresult < INT_MIN) {
        *result = INT_MIN;
        errno = ERANGE;
    } else if (lresult > INT_MAX) {
        *result = INT_MAX;
        errno = ERANGE;
    } else {
        *result = lresult;
    }
    return check_strtox_error(nptr, ep, endptr, errno);
}

/**
 * Convert string @nptr to an unsigned integer, and store it in @result.
 *
 * This is a wrapper around strtoul() that is harder to misuse.
 * Semantics of @nptr, @endptr, @base match strtoul() with differences
 * noted below.
 *
 * @nptr may be null, and no conversion is performed then.
 *
 * If no conversion is performed, store @nptr in *@endptr and return
 * -EINVAL.
 *
 * If @endptr is null, and the string isn't fully converted, return
 * -EINVAL.  This is the case when the pointer that would be stored in
 * a non-null @endptr points to a character other than '\0'.
 *
 * If the conversion overflows @result, store UINT_MAX in @result,
 * and return -ERANGE.
 *
 * Else store the converted value in @result, and return zero.
 *
 * Note that a number with a leading minus sign gets converted without
 * the minus sign, checked for overflow (see above), then negated (in
 * @result's type).  This is exactly how strtoul() works.
 */
int qemu_strtoui(const char *nptr, const char **endptr, int base,
                 unsigned int *result)
{
    char *ep;
    long long lresult;

    assert((unsigned) base <= 36 && base != 1);
    if (!nptr) {
        if (endptr) {
            *endptr = nptr;
        }
        return -EINVAL;
    }

    errno = 0;
    lresult = strtoull(nptr, &ep, base);

    /* Windows returns 1 for negative out-of-range values.  */
    if (errno == ERANGE) {
        *result = -1;
    } else {
        if (lresult > UINT_MAX) {
            *result = UINT_MAX;
            errno = ERANGE;
        } else if (lresult < INT_MIN) {
            *result = UINT_MAX;
            errno = ERANGE;
        } else {
            *result = lresult;
        }
    }
    return check_strtox_error(nptr, ep, endptr, errno);
}

/**
 * Convert string @nptr to a long integer, and store it in @result.
 *
 * This is a wrapper around strtol() that is harder to misuse.
 * Semantics of @nptr, @endptr, @base match strtol() with differences
 * noted below.
 *
 * @nptr may be null, and no conversion is performed then.
 *
 * If no conversion is performed, store @nptr in *@endptr and return
 * -EINVAL.
 *
 * If @endptr is null, and the string isn't fully converted, return
 * -EINVAL.  This is the case when the pointer that would be stored in
 * a non-null @endptr points to a character other than '\0'.
 *
 * If the conversion overflows @result, store LONG_MAX in @result,
 * and return -ERANGE.
 *
 * If the conversion underflows @result, store LONG_MIN in @result,
 * and return -ERANGE.
 *
 * Else store the converted value in @result, and return zero.
 */
int qemu_strtol(const char *nptr, const char **endptr, int base,
                long *result)
{
    char *ep;

    assert((unsigned) base <= 36 && base != 1);
    if (!nptr) {
        if (endptr) {
            *endptr = nptr;
        }
        return -EINVAL;
    }

    errno = 0;
    *result = strtol(nptr, &ep, base);
    return check_strtox_error(nptr, ep, endptr, errno);
}

/**
 * Convert string @nptr to an unsigned long, and store it in @result.
 *
 * This is a wrapper around strtoul() that is harder to misuse.
 * Semantics of @nptr, @endptr, @base match strtoul() with differences
 * noted below.
 *
 * @nptr may be null, and no conversion is performed then.
 *
 * If no conversion is performed, store @nptr in *@endptr and return
 * -EINVAL.
 *
 * If @endptr is null, and the string isn't fully converted, return
 * -EINVAL.  This is the case when the pointer that would be stored in
 * a non-null @endptr points to a character other than '\0'.
 *
 * If the conversion overflows @result, store ULONG_MAX in @result,
 * and return -ERANGE.
 *
 * Else store the converted value in @result, and return zero.
 *
 * Note that a number with a leading minus sign gets converted without
 * the minus sign, checked for overflow (see above), then negated (in
 * @result's type).  This is exactly how strtoul() works.
 */
int qemu_strtoul(const char *nptr, const char **endptr, int base,
                 unsigned long *result)
{
    char *ep;

    assert((unsigned) base <= 36 && base != 1);
    if (!nptr) {
        if (endptr) {
            *endptr = nptr;
        }
        return -EINVAL;
    }

    errno = 0;
    *result = strtoul(nptr, &ep, base);
    /* Windows returns 1 for negative out-of-range values.  */
    if (errno == ERANGE) {
        *result = -1;
    }
    return check_strtox_error(nptr, ep, endptr, errno);
}

/**
 * Convert string @nptr to an int64_t.
 *
 * Works like qemu_strtol(), except it stores INT64_MAX on overflow,
 * and INT64_MIN on underflow.
 */
int qemu_strtoi64(const char *nptr, const char **endptr, int base,
                 int64_t *result)
{
    char *ep;

    assert((unsigned) base <= 36 && base != 1);
    if (!nptr) {
        if (endptr) {
            *endptr = nptr;
        }
        return -EINVAL;
    }

    /* This assumes int64_t is long long TODO relax */
    QEMU_BUILD_BUG_ON(sizeof(int64_t) != sizeof(long long));
    errno = 0;
    *result = strtoll(nptr, &ep, base);
    return check_strtox_error(nptr, ep, endptr, errno);
}

/**
 * Convert string @nptr to an uint64_t.
 *
 * Works like qemu_strtoul(), except it stores UINT64_MAX on overflow.
 */
int qemu_strtou64(const char *nptr, const char **endptr, int base,
                  uint64_t *result)
{
    char *ep;

    assert((unsigned) base <= 36 && base != 1);
    if (!nptr) {
        if (endptr) {
            *endptr = nptr;
        }
        return -EINVAL;
    }

    /* This assumes uint64_t is unsigned long long TODO relax */
    QEMU_BUILD_BUG_ON(sizeof(uint64_t) != sizeof(unsigned long long));
    errno = 0;
    *result = strtoull(nptr, &ep, base);
    /* Windows returns 1 for negative out-of-range values.  */
    if (errno == ERANGE) {
        *result = -1;
    }
    return check_strtox_error(nptr, ep, endptr, errno);
}

/**
 * Convert string @nptr to a double.
  *
 * This is a wrapper around strtod() that is harder to misuse.
 * Semantics of @nptr and @endptr match strtod() with differences
 * noted below.
 *
 * @nptr may be null, and no conversion is performed then.
 *
 * If no conversion is performed, store @nptr in *@endptr and return
 * -EINVAL.
 *
 * If @endptr is null, and the string isn't fully converted, return
 * -EINVAL. This is the case when the pointer that would be stored in
 * a non-null @endptr points to a character other than '\0'.
 *
 * If the conversion overflows, store +/-HUGE_VAL in @result, depending
 * on the sign, and return -ERANGE.
 *
 * If the conversion underflows, store +/-0.0 in @result, depending on the
 * sign, and return -ERANGE.
 *
 * Else store the converted value in @result, and return zero.
 */
int qemu_strtod(const char *nptr, const char **endptr, double *result)
{
    char *ep;

    if (!nptr) {
        if (endptr) {
            *endptr = nptr;
        }
        return -EINVAL;
    }

    errno = 0;
    *result = strtod(nptr, &ep);
    return check_strtox_error(nptr, ep, endptr, errno);
}

/**
 * Convert string @nptr to a finite double.
 *
 * Works like qemu_strtod(), except that "NaN" and "inf" are rejected
 * with -EINVAL and no conversion is performed.
 */
int qemu_strtod_finite(const char *nptr, const char **endptr, double *result)
{
    double tmp;
    int ret;

    ret = qemu_strtod(nptr, endptr, &tmp);
    if (!ret && !isfinite(tmp)) {
        if (endptr) {
            *endptr = nptr;
        }
        ret = -EINVAL;
    }

    if (ret != -EINVAL) {
        *result = tmp;
    }
    return ret;
}

/**
 * Searches for the first occurrence of 'c' in 's', and returns a pointer
 * to the trailing null byte if none was found.
 */
#ifndef HAVE_STRCHRNUL
const char *qemu_strchrnul(const char *s, int c)
{
    const char *e = strchr(s, c);
    if (!e) {
        e = s + strlen(s);
    }
    return e;
}
#endif

/**
 * parse_uint:
 *
 * @s: String to parse
 * @value: Destination for parsed integer value
 * @endptr: Destination for pointer to first character not consumed
 * @base: integer base, between 2 and 36 inclusive, or 0
 *
 * Parse unsigned integer
 *
 * Parsed syntax is like strtoull()'s: arbitrary whitespace, a single optional
 * '+' or '-', an optional "0x" if @base is 0 or 16, one or more digits.
 *
 * If @s is null, or @base is invalid, or @s doesn't start with an
 * integer in the syntax above, set *@value to 0, *@endptr to @s, and
 * return -EINVAL.
 *
 * Set *@endptr to point right beyond the parsed integer (even if the integer
 * overflows or is negative, all digits will be parsed and *@endptr will
 * point right beyond them).
 *
 * If the integer is negative, set *@value to 0, and return -ERANGE.
 *
 * If the integer overflows unsigned long long, set *@value to
 * ULLONG_MAX, and return -ERANGE.
 *
 * Else, set *@value to the parsed integer, and return 0.
 */
int parse_uint(const char *s, unsigned long long *value, char **endptr,
               int base)
{
    int r = 0;
    char *endp = (char *)s;
    unsigned long long val = 0;

    assert((unsigned) base <= 36 && base != 1);
    if (!s) {
        r = -EINVAL;
        goto out;
    }

    errno = 0;
    val = strtoull(s, &endp, base);
    if (errno) {
        r = -errno;
        goto out;
    }

    if (endp == s) {
        r = -EINVAL;
        goto out;
    }

    /* make sure we reject negative numbers: */
    while (qemu_isspace(*s)) {
        s++;
    }
    if (*s == '-') {
        val = 0;
        r = -ERANGE;
        goto out;
    }

out:
    *value = val;
    *endptr = endp;
    return r;
}

/**
 * parse_uint_full:
 *
 * @s: String to parse
 * @value: Destination for parsed integer value
 * @base: integer base, between 2 and 36 inclusive, or 0
 *
 * Parse unsigned integer from entire string
 *
 * Have the same behavior of parse_uint(), but with an additional check
 * for additional data after the parsed number. If extra characters are present
 * after the parsed number, the function will return -EINVAL, and *@v will
 * be set to 0.
 */
int parse_uint_full(const char *s, unsigned long long *value, int base)
{
    char *endp;
    int r;

    r = parse_uint(s, value, &endp, base);
    if (r < 0) {
        return r;
    }
    if (*endp) {
        *value = 0;
        return -EINVAL;
    }

    return 0;
}

int qemu_parse_fd(const char *param)
{
    long fd;
    char *endptr;

    errno = 0;
    fd = strtol(param, &endptr, 10);
    if (param == endptr /* no conversion performed */                    ||
        errno != 0      /* not representable as long; possibly others */ ||
        *endptr != '\0' /* final string not empty */                     ||
        fd < 0          /* invalid as file descriptor */                 ||
        fd > INT_MAX    /* not representable as int */) {
        return -1;
    }
    return fd;
}

/*
 * Implementation of  ULEB128 (http://en.wikipedia.org/wiki/LEB128)
 * Input is limited to 14-bit numbers
 */
int uleb128_encode_small(uint8_t *out, uint32_t n)
{
    g_assert(n <= 0x3fff);
    if (n < 0x80) {
        *out = n;
        return 1;
    } else {
        *out++ = (n & 0x7f) | 0x80;
        *out = n >> 7;
        return 2;
    }
}

int uleb128_decode_small(const uint8_t *in, uint32_t *n)
{
    if (!(*in & 0x80)) {
        *n = *in;
        return 1;
    } else {
        *n = *in++ & 0x7f;
        /* we exceed 14 bit number */
        if (*in & 0x80) {
            return -1;
        }
        *n |= *in << 7;
        return 2;
    }
}

/*
 * helper to parse debug environment variables
 */
int parse_debug_env(const char *name, int max, int initial)
{
    char *debug_env = getenv(name);
    char *inv = NULL;
    long debug;

    if (!debug_env) {
        return initial;
    }
    errno = 0;
    debug = strtol(debug_env, &inv, 10);
    if (inv == debug_env) {
        return initial;
    }
    if (debug < 0 || debug > max || errno != 0) {
        warn_report("%s not in [0, %d]", name, max);
        return initial;
    }
    return debug;
}

/*
 * Helper to print ethernet mac address
 */
const char *qemu_ether_ntoa(const MACAddr *mac)
{
    static char ret[18];

    snprintf(ret, sizeof(ret), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac->a[0], mac->a[1], mac->a[2], mac->a[3], mac->a[4], mac->a[5]);

    return ret;
}

/*
 * Return human readable string for size @val.
 * @val can be anything that uint64_t allows (no more than "16 EiB").
 * Use IEC binary units like KiB, MiB, and so forth.
 * Caller is responsible for passing it to g_free().
 */
char *size_to_str(uint64_t val)
{
    static const char *suffixes[] = { "", "Ki", "Mi", "Gi", "Ti", "Pi", "Ei" };
    uint64_t div;
    int i;

    /*
     * The exponent (returned in i) minus one gives us
     * floor(log2(val * 1024 / 1000).  The correction makes us
     * switch to the higher power when the integer part is >= 1000.
     * (see e41b509d68afb1f for more info)
     */
    frexp(val / (1000.0 / 1024.0), &i);
    i = (i - 1) / 10;
    div = 1ULL << (i * 10);

    return g_strdup_printf("%0.3g %sB", (double)val / div, suffixes[i]);
}

char *freq_to_str(uint64_t freq_hz)
{
    static const char *const suffixes[] = { "", "K", "M", "G", "T", "P", "E" };
    double freq = freq_hz;
    size_t idx = 0;

    while (freq >= 1000.0) {
        freq /= 1000.0;
        idx++;
    }
    assert(idx < ARRAY_SIZE(suffixes));

    return g_strdup_printf("%0.3g %sHz", freq, suffixes[idx]);
}

int qemu_pstrcmp0(const char **str1, const char **str2)
{
    return g_strcmp0(*str1, *str2);
}

static inline bool starts_with_prefix(const char *dir)
{
    size_t prefix_len = strlen(CONFIG_PREFIX);
    return !memcmp(dir, CONFIG_PREFIX, prefix_len) &&
        (!dir[prefix_len] || G_IS_DIR_SEPARATOR(dir[prefix_len]));
}

/* Return the next path component in dir, and store its length in *p_len.  */
static inline const char *next_component(const char *dir, int *p_len)
{
    int len;
    while ((*dir && G_IS_DIR_SEPARATOR(*dir)) ||
           (*dir == '.' && (G_IS_DIR_SEPARATOR(dir[1]) || dir[1] == '\0'))) {
        dir++;
    }
    len = 0;
    while (dir[len] && !G_IS_DIR_SEPARATOR(dir[len])) {
        len++;
    }
    *p_len = len;
    return dir;
}

char *get_relocated_path(const char *dir)
{
    size_t prefix_len = strlen(CONFIG_PREFIX);
    const char *bindir = CONFIG_BINDIR;
    const char *exec_dir = qemu_get_exec_dir();
    GString *result;
    int len_dir, len_bindir;

    /* Fail if qemu_init_exec_dir was not called.  */
    assert(exec_dir[0]);
    if (!starts_with_prefix(dir) || !starts_with_prefix(bindir)) {
        return g_strdup(dir);
    }

    result = g_string_new(exec_dir);

    /* Advance over common components.  */
    len_dir = len_bindir = prefix_len;
    do {
        dir += len_dir;
        bindir += len_bindir;
        dir = next_component(dir, &len_dir);
        bindir = next_component(bindir, &len_bindir);
    } while (len_dir && len_dir == len_bindir && !memcmp(dir, bindir, len_dir));

    /* Ascend from bindir to the common prefix with dir.  */
    while (len_bindir) {
        bindir += len_bindir;
        g_string_append(result, "/..");
        bindir = next_component(bindir, &len_bindir);
    }

    if (*dir) {
        assert(G_IS_DIR_SEPARATOR(dir[-1]));
        g_string_append(result, dir - 1);
    }
    return result->str;
}
