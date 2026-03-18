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

#include "qemu/osdep.h"
#include "monitor/monitor.h"
#include "qapi/error.h"
#include "util.h"

int net_parse_macaddr(uint8_t *macaddr, const char *p)
{
    int i;
    char *last_char;
    long int offset;

    errno = 0;
    offset = strtol(p, &last_char, 0);
    if (errno == 0 && *last_char == '\0' &&
        offset >= 0 && offset <= 0xFFFFFF) {
        macaddr[3] = (offset & 0xFF0000) >> 16;
        macaddr[4] = (offset & 0xFF00) >> 8;
        macaddr[5] = offset & 0xFF;
        return 0;
    }

    for (i = 0; i < 6; i++) {
        macaddr[i] = strtol(p, (char **)&p, 16);
        if (i == 5) {
            if (*p != '\0') {
                return -1;
            }
        } else {
            if (*p != ':' && *p != '-') {
                return -1;
            }
            p++;
        }
    }

    return 0;
}

void net_free_fds(int *fds, int nfds)
{
    int i;

    if (!fds || nfds <= 0) {
        return;
    }

    for (i = 0; i < nfds; i++) {
        if (fds[i] != -1) {
            close(fds[i]);
        }
    }

    g_free(fds);
}

int net_parse_fds(const char *fds_param, int **fds, int expected_nfds,
                  Error **errp)
{
    g_auto(GStrv) fdnames = g_strsplit(fds_param, ":", -1);
    unsigned nfds = g_strv_length(fdnames);
    int i;

    if (nfds > INT_MAX) {
        error_setg(errp, "fds parameter exceeds maximum of %d", INT_MAX);
        return -1;
    }

    if (expected_nfds && nfds != expected_nfds) {
        error_setg(errp, "expected %u socket fds, got %u", expected_nfds, nfds);
        return -1;
    }

    *fds = g_new(int, nfds);

    for (i = 0; i < nfds; i++) {
        (*fds)[i] = monitor_fd_param(monitor_cur(), fdnames[i], errp);
        if ((*fds)[i] == -1) {
            net_free_fds(*fds, i);
            *fds = NULL;
            return -1;
        }
    }

    return nfds;
}
