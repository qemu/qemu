/*
 * Copyright (C) 2015-2016 Gerd Hoffmann <kraxel@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "qemu/drm.h"

#include <glob.h>
#include <dirent.h>

int qemu_drm_rendernode_open(const char *rendernode)
{
    DIR *dir;
    struct dirent *e;
    struct stat st;
    int r, fd, ret;
    char *p;

    if (rendernode) {
        return open(rendernode, O_RDWR | O_CLOEXEC | O_NOCTTY | O_NONBLOCK);
    }

    dir = opendir("/dev/dri");
    if (!dir) {
        return -1;
    }

    fd = -1;
    while ((e = readdir(dir))) {
        if (strncmp(e->d_name, "renderD", 7)) {
            continue;
        }

        p = g_strdup_printf("/dev/dri/%s", e->d_name);

        r = open(p, O_RDWR | O_CLOEXEC | O_NOCTTY | O_NONBLOCK);
        if (r < 0) {
            g_free(p);
            continue;
        }

        /*
         * prefer fstat() over checking e->d_type == DT_CHR for
         * portability reasons
         */
        ret = fstat(r, &st);
        if (ret < 0 || (st.st_mode & S_IFMT) != S_IFCHR) {
            close(r);
            g_free(p);
            continue;
        }

        fd = r;
        g_free(p);
        break;
    }

    closedir(dir);
    if (fd < 0) {
        return -1;
    }
    return fd;
}
