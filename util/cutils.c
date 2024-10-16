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

#ifdef __FreeBSD__
#include <sys/sysctl.h>
#include <sys/user.h>
#endif

#ifdef __NetBSD__
#include <sys/sysctl.h>
#endif

#ifdef __HAIKU__
#include <kernel/image.h>
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#ifdef G_OS_WIN32
#include <pathcch.h>
#include <wchar.h>
#endif

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
 * Convert size string to bytes.
 *
 * The size parsing supports the following syntaxes
 * - 12345 - decimal, scale determined by @default_suffix and @unit
 * - 12345{bBkKmMgGtTpPeE} - decimal, scale determined by suffix and @unit
 * - 12345.678{kKmMgGtTpPeE} - decimal, scale determined by suffix, and
 *   fractional portion is truncated to byte, either side of . may be empty
 * - 0x7fEE - hexadecimal, unit determined by @default_suffix
 *
 * The following are intentionally not supported
 * - hex with scaling suffix, such as 0x20M or 0x1p3 (both fail with
 *   -EINVAL), while 0x1b is 27 (not 1 with byte scale)
 * - octal, such as 08 (parsed as decimal instead)
 * - binary, such as 0b1000 (parsed as 0b with trailing garbage "1000")
 * - fractional hex, such as 0x1.8 (parsed as 0 with trailing garbage "x1.8")
 * - negative values, including -0 (fail with -ERANGE)
 * - floating point exponents, such as 1e3 (parsed as 1e with trailing
 *   garbage "3") or 0x1p3 (rejected as hex with scaling suffix)
 * - non-finite values, such as inf or NaN (fail with -EINVAL)
 *
 * The end pointer will be returned in *end, if not NULL.  If there is
 * no fraction, the input can be decimal or hexadecimal; if there is a
 * non-zero fraction, then the input must be decimal and there must be
 * a suffix (possibly by @default_suffix) larger than Byte, and the
 * fractional portion may suffer from precision loss or rounding.  The
 * input must be positive.
 *
 * Return -ERANGE on overflow (with *@end advanced), and -EINVAL on
 * other error (with *@end at @nptr).  Unlike strtoull, *@result is
 * set to 0 on all errors, as returning UINT64_MAX on overflow is less
 * likely to be usable as a size.
 */
static int do_strtosz(const char *nptr, const char **end,
                      const char default_suffix, int64_t unit,
                      uint64_t *result)
{
    int retval;
    const char *endptr;
    unsigned char c;
    uint64_t val = 0, valf = 0;
    int64_t mul;

    /* Parse integral portion as decimal. */
    retval = parse_uint(nptr, &endptr, 10, &val);
    if (retval == -ERANGE || !nptr) {
        goto out;
    }
    if (retval == 0 && val == 0 && (*endptr == 'x' || *endptr == 'X')) {
        /* Input looks like hex; reparse, and insist on no fraction or suffix. */
        retval = qemu_strtou64(nptr, &endptr, 16, &val);
        if (retval) {
            goto out;
        }
        if (*endptr == '.' || suffix_mul(*endptr, unit) > 0) {
            endptr = nptr;
            retval = -EINVAL;
            goto out;
        }
    } else if (*endptr == '.' || (endptr == nptr && strchr(nptr, '.'))) {
        /*
         * Input looks like a fraction.  Make sure even 1.k works
         * without fractional digits.  strtod tries to treat 'e' as an
         * exponent, but we want to treat it as a scaling suffix;
         * doing this requires modifying a copy of the fraction.
         */
        double fraction = 0.0;

        if (retval == 0 && *endptr == '.' && !isdigit(endptr[1])) {
            /* If we got here, we parsed at least one digit already. */
            endptr++;
        } else {
            char *e;
            const char *tail;
            g_autofree char *copy = g_strdup(endptr);

            e = strchr(copy, 'e');
            if (e) {
                *e = '\0';
            }
            e = strchr(copy, 'E');
            if (e) {
                *e = '\0';
            }
            /*
             * If this is a floating point, we are guaranteed that '.'
             * appears before any possible digits in copy.  If it is
             * not a floating point, strtod will fail.  Either way,
             * there is now no exponent in copy, so if it parses, we
             * know 0.0 <= abs(result) <= 1.0 (after rounding), and
             * ERANGE is only possible on underflow which is okay.
             */
            retval = qemu_strtod_finite(copy, &tail, &fraction);
            endptr += tail - copy;
            if (signbit(fraction)) {
                retval = -ERANGE;
                goto out;
            }
        }

        /* Extract into a 64-bit fixed-point fraction. */
        if (fraction == 1.0) {
            if (val == UINT64_MAX) {
                retval = -ERANGE;
                goto out;
            }
            val++;
        } else if (retval == -ERANGE) {
            /* See comments above about underflow */
            valf = 1;
            retval = 0;
        } else {
            /* We want non-zero valf for any non-zero fraction */
            valf = (uint64_t)(fraction * 0x1p64);
            if (valf == 0 && fraction > 0.0) {
                valf = 1;
            }
        }
    }
    if (retval) {
        goto out;
    }
    c = *endptr;
    mul = suffix_mul(c, unit);
    if (mul > 0) {
        endptr++;
    } else {
        mul = suffix_mul(default_suffix, unit);
        assert(mul > 0);
    }
    if (mul == 1) {
        /* When a fraction is present, a scale is required. */
        if (valf != 0) {
            endptr = nptr;
            retval = -EINVAL;
            goto out;
        }
    } else {
        uint64_t valh, tmp;

        /* Compute exact result: 64.64 x 64.0 -> 128.64 fixed point */
        mulu64(&val, &valh, val, mul);
        mulu64(&valf, &tmp, valf, mul);
        val += tmp;
        valh += val < tmp;

        /* Round 0.5 upward. */
        tmp = valf >> 63;
        val += tmp;
        valh += val < tmp;

        /* Report overflow. */
        if (valh != 0) {
            retval = -ERANGE;
            goto out;
        }
    }

    retval = 0;

out:
    if (end) {
        *end = endptr;
    } else if (nptr && *endptr) {
        retval = -EINVAL;
    }
    if (retval == 0) {
        *result = val;
    } else {
        *result = 0;
        if (end && retval == -EINVAL) {
            *end = nptr;
        }
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
                              const char **endptr, bool check_zero,
                              int libc_errno)
{
    assert(ep >= nptr);

    /* Windows has a bug in that it fails to parse 0 from "0x" in base 16 */
    if (check_zero && ep == nptr && libc_errno == 0) {
        char *tmp;

        errno = 0;
        if (strtol(nptr, &tmp, 10) == 0 && errno == 0 &&
            (*tmp == 'x' || *tmp == 'X')) {
            ep = tmp;
        }
    }

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
 * If no conversion is performed, store @nptr in *@endptr, 0 in
 * @result, and return -EINVAL.
 *
 * If @endptr is null, and the string isn't fully converted, return
 * -EINVAL with @result set to the parsed value.  This is the case
 * when the pointer that would be stored in a non-null @endptr points
 * to a character other than '\0'.
 *
 * If the conversion overflows @result, store INT_MAX in @result,
 * and return -ERANGE.
 *
 * If the conversion underflows @result, store INT_MIN in @result,
 * and return -ERANGE.
 *
 * Else store the converted value in @result, and return zero.
 *
 * This matches the behavior of strtol() on 32-bit platforms, even on
 * platforms where long is 64-bits.
 */
int qemu_strtoi(const char *nptr, const char **endptr, int base,
                int *result)
{
    char *ep;
    long long lresult;

    assert((unsigned) base <= 36 && base != 1);
    if (!nptr) {
        *result = 0;
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
    return check_strtox_error(nptr, ep, endptr, lresult == 0, errno);
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
 * If no conversion is performed, store @nptr in *@endptr, 0 in
 * @result, and return -EINVAL.
 *
 * If @endptr is null, and the string isn't fully converted, return
 * -EINVAL with @result set to the parsed value.  This is the case
 * when the pointer that would be stored in a non-null @endptr points
 * to a character other than '\0'.
 *
 * If the conversion overflows @result, store UINT_MAX in @result,
 * and return -ERANGE.
 *
 * Else store the converted value in @result, and return zero.
 *
 * Note that a number with a leading minus sign gets converted without
 * the minus sign, checked for overflow (see above), then negated (in
 * @result's type).  This matches the behavior of strtoul() on 32-bit
 * platforms, even on platforms where long is 64-bits.
 */
int qemu_strtoui(const char *nptr, const char **endptr, int base,
                 unsigned int *result)
{
    char *ep;
    unsigned long long lresult;
    bool neg;

    assert((unsigned) base <= 36 && base != 1);
    if (!nptr) {
        *result = 0;
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
        /*
         * Note that platforms with 32-bit strtoul only accept input
         * in the range [-4294967295, 4294967295]; but we used 64-bit
         * strtoull which wraps -18446744073709551615 to 1 instead of
         * declaring overflow.  So we must check if '-' was parsed,
         * and if so, undo the negation before doing our bounds check.
         */
        neg = memchr(nptr, '-', ep - nptr) != NULL;
        if (neg) {
            lresult = -lresult;
        }
        if (lresult > UINT_MAX) {
            *result = UINT_MAX;
            errno = ERANGE;
        } else {
            *result = neg ? -lresult : lresult;
        }
    }
    return check_strtox_error(nptr, ep, endptr, lresult == 0, errno);
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
 * If no conversion is performed, store @nptr in *@endptr, 0 in
 * @result, and return -EINVAL.
 *
 * If @endptr is null, and the string isn't fully converted, return
 * -EINVAL with @result set to the parsed value.  This is the case
 * when the pointer that would be stored in a non-null @endptr points
 * to a character other than '\0'.
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
        *result = 0;
        if (endptr) {
            *endptr = nptr;
        }
        return -EINVAL;
    }

    errno = 0;
    *result = strtol(nptr, &ep, base);
    return check_strtox_error(nptr, ep, endptr, *result == 0, errno);
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
 * If no conversion is performed, store @nptr in *@endptr, 0 in
 * @result, and return -EINVAL.
 *
 * If @endptr is null, and the string isn't fully converted, return
 * -EINVAL with @result set to the parsed value.  This is the case
 * when the pointer that would be stored in a non-null @endptr points
 * to a character other than '\0'.
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
        *result = 0;
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
    return check_strtox_error(nptr, ep, endptr, *result == 0, errno);
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
        *result = 0;
        if (endptr) {
            *endptr = nptr;
        }
        return -EINVAL;
    }

    /* This assumes int64_t is long long TODO relax */
    QEMU_BUILD_BUG_ON(sizeof(int64_t) != sizeof(long long));
    errno = 0;
    *result = strtoll(nptr, &ep, base);
    return check_strtox_error(nptr, ep, endptr, *result == 0, errno);
}

/**
 * Convert string @nptr to an uint64_t.
 *
 * Works like qemu_strtoul(), except it stores UINT64_MAX on overflow.
 * (If you want to prohibit negative numbers that wrap around to
 * positive, use parse_uint()).
 */
int qemu_strtou64(const char *nptr, const char **endptr, int base,
                  uint64_t *result)
{
    char *ep;

    assert((unsigned) base <= 36 && base != 1);
    if (!nptr) {
        *result = 0;
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
    return check_strtox_error(nptr, ep, endptr, *result == 0, errno);
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
 * If no conversion is performed, store @nptr in *@endptr, +0.0 in
 * @result, and return -EINVAL.
 *
 * If @endptr is null, and the string isn't fully converted, return
 * -EINVAL with @result set to the parsed value.  This is the case
 * when the pointer that would be stored in a non-null @endptr points
 * to a character other than '\0'.
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
        *result = 0.0;
        if (endptr) {
            *endptr = nptr;
        }
        return -EINVAL;
    }

    errno = 0;
    *result = strtod(nptr, &ep);
    return check_strtox_error(nptr, ep, endptr, false, errno);
}

/**
 * Convert string @nptr to a finite double.
 *
 * Works like qemu_strtod(), except that "NaN", "inf", and strings
 * that cause ERANGE overflow errors are rejected with -EINVAL as if
 * no conversion is performed, storing 0.0 into @result regardless of
 * any sign.  -ERANGE failures for underflow still preserve the parsed
 * sign.
 */
int qemu_strtod_finite(const char *nptr, const char **endptr, double *result)
{
    const char *tmp;
    int ret;

    ret = qemu_strtod(nptr, &tmp, result);
    if (!isfinite(*result)) {
        if (endptr) {
            *endptr = nptr;
        }
        *result = 0.0;
        ret = -EINVAL;
    } else if (endptr) {
        *endptr = tmp;
    } else if (*tmp) {
        ret = -EINVAL;
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
 * @endptr: Destination for pointer to first character not consumed
 * @base: integer base, between 2 and 36 inclusive, or 0
 * @value: Destination for parsed integer value
 *
 * Parse unsigned integer
 *
 * Parsed syntax is like strtoull()'s: arbitrary whitespace, a single optional
 * '+' or '-', an optional "0x" if @base is 0 or 16, one or more digits.
 *
 * If @s is null, or @s doesn't start with an integer in the syntax
 * above, set *@value to 0, *@endptr to @s, and return -EINVAL.
 *
 * Set *@endptr to point right beyond the parsed integer (even if the integer
 * overflows or is negative, all digits will be parsed and *@endptr will
 * point right beyond them).  If @endptr is %NULL, any trailing character
 * instead causes a result of -EINVAL with *@value of 0.
 *
 * If the integer is negative, set *@value to 0, and return -ERANGE.
 * (If you want to allow negative numbers that wrap around within
 * bounds, use qemu_strtou64()).
 *
 * If the integer overflows unsigned long long, set *@value to
 * ULLONG_MAX, and return -ERANGE.
 *
 * Else, set *@value to the parsed integer, and return 0.
 */
int parse_uint(const char *s, const char **endptr, int base, uint64_t *value)
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
    if (endptr) {
        *endptr = endp;
    } else if (s && *endp) {
        r = -EINVAL;
        *value = 0;
    }
    return r;
}

/**
 * parse_uint_full:
 *
 * @s: String to parse
 * @base: integer base, between 2 and 36 inclusive, or 0
 * @value: Destination for parsed integer value
 *
 * Parse unsigned integer from entire string, rejecting any trailing slop.
 *
 * Shorthand for parse_uint(s, NULL, base, value).
 */
int parse_uint_full(const char *s, int base, uint64_t *value)
{
    return parse_uint(s, NULL, base, value);
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

const char *si_prefix(unsigned int exp10)
{
    static const char *prefixes[] = {
        "a", "f", "p", "n", "u", "m", "", "K", "M", "G", "T", "P", "E"
    };

    exp10 += 18;
    assert(exp10 % 3 == 0 && exp10 / 3 < ARRAY_SIZE(prefixes));
    return prefixes[exp10 / 3];
}

const char *iec_binary_prefix(unsigned int exp2)
{
    static const char *prefixes[] = { "", "Ki", "Mi", "Gi", "Ti", "Pi", "Ei" };

    assert(exp2 % 10 == 0 && exp2 / 10 < ARRAY_SIZE(prefixes));
    return prefixes[exp2 / 10];
}

/*
 * Return human readable string for size @val.
 * @val can be anything that uint64_t allows (no more than "16 EiB").
 * Use IEC binary units like KiB, MiB, and so forth.
 * Caller is responsible for passing it to g_free().
 */
char *size_to_str(uint64_t val)
{
    uint64_t div;
    int i;

    /*
     * The exponent (returned in i) minus one gives us
     * floor(log2(val * 1024 / 1000).  The correction makes us
     * switch to the higher power when the integer part is >= 1000.
     * (see e41b509d68afb1f for more info)
     */
    frexp(val / (1000.0 / 1024.0), &i);
    i = (i - 1) / 10 * 10;
    div = 1ULL << i;

    return g_strdup_printf("%0.3g %sB", (double)val / div, iec_binary_prefix(i));
}

char *freq_to_str(uint64_t freq_hz)
{
    double freq = freq_hz;
    size_t exp10 = 0;

    while (freq >= 1000.0) {
        freq /= 1000.0;
        exp10 += 3;
    }

    return g_strdup_printf("%0.3g %sHz", freq, si_prefix(exp10));
}

int qemu_pstrcmp0(const char **str1, const char **str2)
{
    return g_strcmp0(*str1, *str2);
}

static inline bool starts_with_prefix(const char *dir)
{
    size_t prefix_len = strlen(CONFIG_PREFIX);
    /*
     * dir[prefix_len] is only accessed if the length of dir is
     * >= prefix_len, so no out of bounds access is possible.
     */
#pragma GCC diagnostic push
#if !defined(__clang__) || __has_warning("-Warray-bounds=")
#pragma GCC diagnostic ignored "-Warray-bounds="
#endif
    return !memcmp(dir, CONFIG_PREFIX, prefix_len) &&
        (!dir[prefix_len] || G_IS_DIR_SEPARATOR(dir[prefix_len]));
#pragma GCC diagnostic pop
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

static const char *exec_dir;

void qemu_init_exec_dir(const char *argv0)
{
#ifdef G_OS_WIN32
    char *p;
    char buf[MAX_PATH];
    DWORD len;

    if (exec_dir) {
        return;
    }

    len = GetModuleFileName(NULL, buf, sizeof(buf) - 1);
    if (len == 0) {
        return;
    }

    buf[len] = 0;
    p = buf + len - 1;
    while (p != buf && *p != '\\') {
        p--;
    }
    *p = 0;
    if (access(buf, R_OK) == 0) {
        exec_dir = g_strdup(buf);
    } else {
        exec_dir = CONFIG_BINDIR;
    }
#else
    char *p = NULL;
    char buf[PATH_MAX];

    if (exec_dir) {
        return;
    }

#if defined(__linux__)
    {
        int len;
        len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (len > 0) {
            buf[len] = 0;
            p = buf;
        }
    }
#elif defined(__FreeBSD__) \
      || (defined(__NetBSD__) && defined(KERN_PROC_PATHNAME))
    {
#if defined(__FreeBSD__)
        static int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1};
#else
        static int mib[4] = {CTL_KERN, KERN_PROC_ARGS, -1, KERN_PROC_PATHNAME};
#endif
        size_t len = sizeof(buf) - 1;

        *buf = '\0';
        if (!sysctl(mib, ARRAY_SIZE(mib), buf, &len, NULL, 0) &&
            *buf) {
            buf[sizeof(buf) - 1] = '\0';
            p = buf;
        }
    }
#elif defined(__APPLE__)
    {
        char fpath[PATH_MAX];
        uint32_t len = sizeof(fpath);
        if (_NSGetExecutablePath(fpath, &len) == 0) {
            p = realpath(fpath, buf);
            if (!p) {
                return;
            }
        }
    }
#elif defined(__HAIKU__)
    {
        image_info ii;
        int32_t c = 0;

        *buf = '\0';
        while (get_next_image_info(0, &c, &ii) == B_OK) {
            if (ii.type == B_APP_IMAGE) {
                strncpy(buf, ii.name, sizeof(buf));
                buf[sizeof(buf) - 1] = 0;
                p = buf;
                break;
            }
        }
    }
#endif
    /* If we don't have any way of figuring out the actual executable
       location then try argv[0].  */
    if (!p && argv0) {
        p = realpath(argv0, buf);
    }
    if (p) {
        exec_dir = g_path_get_dirname(p);
    } else {
        exec_dir = CONFIG_BINDIR;
    }
#endif
}

char *get_relocated_path(const char *dir)
{
    size_t prefix_len = strlen(CONFIG_PREFIX);
    const char *bindir = CONFIG_BINDIR;
    GString *result;
    int len_dir, len_bindir;

    /* Fail if qemu_init_exec_dir was not called.  */
    assert(exec_dir[0]);

    result = g_string_new(exec_dir);
    g_string_append(result, "/qemu-bundle");
    if (access(result->str, R_OK) == 0) {
#ifdef G_OS_WIN32
        const char *src = dir;
        size_t size = mbsrtowcs(NULL, &src, 0, &(mbstate_t){0}) + 1;
        PWSTR wdir = g_new(WCHAR, size);
        mbsrtowcs(wdir, &src, size, &(mbstate_t){0});

        PCWSTR wdir_skipped_root;
        if (PathCchSkipRoot(wdir, &wdir_skipped_root) == S_OK) {
            size = wcsrtombs(NULL, &wdir_skipped_root, 0, &(mbstate_t){0});
            char *cursor = result->str + result->len;
            g_string_set_size(result, result->len + size);
            wcsrtombs(cursor, &wdir_skipped_root, size + 1, &(mbstate_t){0});
        } else {
            g_string_append(result, dir);
        }

        g_free(wdir);
#else
        g_string_append(result, dir);
#endif
        goto out;
    }

    if (IS_ENABLED(CONFIG_RELOCATABLE) &&
        starts_with_prefix(dir) && starts_with_prefix(bindir)) {
        g_string_assign(result, exec_dir);

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
        goto out;
    }

    g_string_assign(result, dir);
out:
    return g_string_free(result, false);
}
