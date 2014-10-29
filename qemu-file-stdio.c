/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
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
#include "block/coroutine.h"
#include "migration/qemu-file.h"

typedef struct QEMUFileStdio {
    FILE *stdio_file;
    QEMUFile *file;
} QEMUFileStdio;

static int stdio_get_fd(void *opaque)
{
    QEMUFileStdio *s = opaque;

    return fileno(s->stdio_file);
}

static int stdio_put_buffer(void *opaque, const uint8_t *buf, int64_t pos,
                            int size)
{
    QEMUFileStdio *s = opaque;
    int res;

    res = fwrite(buf, 1, size, s->stdio_file);

    if (res != size) {
        return -errno;
    }
    return res;
}

static int stdio_get_buffer(void *opaque, uint8_t *buf, int64_t pos, int size)
{
    QEMUFileStdio *s = opaque;
    FILE *fp = s->stdio_file;
    int bytes;

    for (;;) {
        clearerr(fp);
        bytes = fread(buf, 1, size, fp);
        if (bytes != 0 || !ferror(fp)) {
            break;
        }
        if (errno == EAGAIN) {
            yield_until_fd_readable(fileno(fp));
        } else if (errno != EINTR) {
            break;
        }
    }
    return bytes;
}

static int stdio_pclose(void *opaque)
{
    QEMUFileStdio *s = opaque;
    int ret;
    ret = pclose(s->stdio_file);
    if (ret == -1) {
        ret = -errno;
    } else if (!WIFEXITED(ret) || WEXITSTATUS(ret) != 0) {
        /* close succeeded, but non-zero exit code: */
        ret = -EIO; /* fake errno value */
    }
    g_free(s);
    return ret;
}

static int stdio_fclose(void *opaque)
{
    QEMUFileStdio *s = opaque;
    int ret = 0;

    if (qemu_file_is_writable(s->file)) {
        int fd = fileno(s->stdio_file);
        struct stat st;

        ret = fstat(fd, &st);
        if (ret == 0 && S_ISREG(st.st_mode)) {
            /*
             * If the file handle is a regular file make sure the
             * data is flushed to disk before signaling success.
             */
            ret = fsync(fd);
            if (ret != 0) {
                ret = -errno;
                return ret;
            }
        }
    }
    if (fclose(s->stdio_file) == EOF) {
        ret = -errno;
    }
    g_free(s);
    return ret;
}

static const QEMUFileOps stdio_pipe_read_ops = {
    .get_fd =     stdio_get_fd,
    .get_buffer = stdio_get_buffer,
    .close =      stdio_pclose
};

static const QEMUFileOps stdio_pipe_write_ops = {
    .get_fd =     stdio_get_fd,
    .put_buffer = stdio_put_buffer,
    .close =      stdio_pclose
};

QEMUFile *qemu_popen_cmd(const char *command, const char *mode)
{
    FILE *stdio_file;
    QEMUFileStdio *s;

    if (mode == NULL || (mode[0] != 'r' && mode[0] != 'w') || mode[1] != 0) {
        fprintf(stderr, "qemu_popen: Argument validity check failed\n");
        return NULL;
    }

    stdio_file = popen(command, mode);
    if (stdio_file == NULL) {
        return NULL;
    }

    s = g_malloc0(sizeof(QEMUFileStdio));

    s->stdio_file = stdio_file;

    if (mode[0] == 'r') {
        s->file = qemu_fopen_ops(s, &stdio_pipe_read_ops);
    } else {
        s->file = qemu_fopen_ops(s, &stdio_pipe_write_ops);
    }
    return s->file;
}

static const QEMUFileOps stdio_file_read_ops = {
    .get_fd =     stdio_get_fd,
    .get_buffer = stdio_get_buffer,
    .close =      stdio_fclose
};

static const QEMUFileOps stdio_file_write_ops = {
    .get_fd =     stdio_get_fd,
    .put_buffer = stdio_put_buffer,
    .close =      stdio_fclose
};

QEMUFile *qemu_fopen(const char *filename, const char *mode)
{
    QEMUFileStdio *s;

    if (qemu_file_mode_is_not_valid(mode)) {
        return NULL;
    }

    s = g_malloc0(sizeof(QEMUFileStdio));

    s->stdio_file = fopen(filename, mode);
    if (!s->stdio_file) {
        goto fail;
    }

    if (mode[0] == 'w') {
        s->file = qemu_fopen_ops(s, &stdio_file_write_ops);
    } else {
        s->file = qemu_fopen_ops(s, &stdio_file_read_ops);
    }
    return s->file;
fail:
    g_free(s);
    return NULL;
}
