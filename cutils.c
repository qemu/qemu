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
#include "qemu-common.h"
#include "host-utils.h"

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

time_t mktimegm(struct tm *tm)
{
    time_t t;
    int y = tm->tm_year + 1900, m = tm->tm_mon + 1, d = tm->tm_mday;
    if (m < 3) {
        m += 12;
        y--;
    }
    t = 86400 * (d + (153 * m - 457) / 5 + 365 * y + y / 4 - y / 100 + 
                 y / 400 - 719469);
    t += 3600 * tm->tm_hour + 60 * tm->tm_min + tm->tm_sec;
    return t;
}

int qemu_fls(int i)
{
    return 32 - clz32(i);
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

/* io vectors */

void qemu_iovec_init(QEMUIOVector *qiov, int alloc_hint)
{
    qiov->iov = qemu_malloc(alloc_hint * sizeof(struct iovec));
    qiov->niov = 0;
    qiov->nalloc = alloc_hint;
    qiov->size = 0;
}

void qemu_iovec_init_external(QEMUIOVector *qiov, struct iovec *iov, int niov)
{
    int i;

    qiov->iov = iov;
    qiov->niov = niov;
    qiov->nalloc = -1;
    qiov->size = 0;
    for (i = 0; i < niov; i++)
        qiov->size += iov[i].iov_len;
}

void qemu_iovec_add(QEMUIOVector *qiov, void *base, size_t len)
{
    assert(qiov->nalloc != -1);

    if (qiov->niov == qiov->nalloc) {
        qiov->nalloc = 2 * qiov->nalloc + 1;
        qiov->iov = qemu_realloc(qiov->iov, qiov->nalloc * sizeof(struct iovec));
    }
    qiov->iov[qiov->niov].iov_base = base;
    qiov->iov[qiov->niov].iov_len = len;
    qiov->size += len;
    ++qiov->niov;
}

/*
 * Copies iovecs from src to the end dst until src is completely copied or the
 * total size of the copied iovec reaches size. The size of the last copied
 * iovec is changed in order to fit the specified total size if it isn't a
 * perfect fit already.
 */
void qemu_iovec_concat(QEMUIOVector *dst, QEMUIOVector *src, size_t size)
{
    int i;
    size_t done;

    assert(dst->nalloc != -1);

    done = 0;
    for (i = 0; (i < src->niov) && (done != size); i++) {
        if (done + src->iov[i].iov_len > size) {
            qemu_iovec_add(dst, src->iov[i].iov_base, size - done);
            break;
        } else {
            qemu_iovec_add(dst, src->iov[i].iov_base, src->iov[i].iov_len);
        }
        done += src->iov[i].iov_len;
    }
}

void qemu_iovec_destroy(QEMUIOVector *qiov)
{
    assert(qiov->nalloc != -1);

    qemu_free(qiov->iov);
}

void qemu_iovec_reset(QEMUIOVector *qiov)
{
    assert(qiov->nalloc != -1);

    qiov->niov = 0;
    qiov->size = 0;
}

void qemu_iovec_to_buffer(QEMUIOVector *qiov, void *buf)
{
    uint8_t *p = (uint8_t *)buf;
    int i;

    for (i = 0; i < qiov->niov; ++i) {
        memcpy(p, qiov->iov[i].iov_base, qiov->iov[i].iov_len);
        p += qiov->iov[i].iov_len;
    }
}

void qemu_iovec_from_buffer(QEMUIOVector *qiov, const void *buf, size_t count)
{
    const uint8_t *p = (const uint8_t *)buf;
    size_t copy;
    int i;

    for (i = 0; i < qiov->niov && count; ++i) {
        copy = count;
        if (copy > qiov->iov[i].iov_len)
            copy = qiov->iov[i].iov_len;
        memcpy(qiov->iov[i].iov_base, p, copy);
        p     += copy;
        count -= copy;
    }
}
