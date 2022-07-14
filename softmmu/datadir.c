/*
 * QEMU firmware and keymap file search
 *
 * Copyright (c) 2003-2020 QEMU contributors
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
#include "qemu/datadir.h"
#include "qemu/cutils.h"
#include "trace.h"

static const char *data_dir[16];
static int data_dir_idx;

char *qemu_find_file(int type, const char *name)
{
    int i;
    const char *subdir;
    char *buf;

    /* Try the name as a straight path first */
    if (access(name, R_OK) == 0) {
        trace_load_file(name, name);
        return g_strdup(name);
    }

    switch (type) {
    case QEMU_FILE_TYPE_BIOS:
        subdir = "";
        break;
    case QEMU_FILE_TYPE_KEYMAP:
        subdir = "keymaps/";
        break;
    default:
        abort();
    }

    for (i = 0; i < data_dir_idx; i++) {
        buf = g_strdup_printf("%s/%s%s", data_dir[i], subdir, name);
        if (access(buf, R_OK) == 0) {
            trace_load_file(name, buf);
            return buf;
        }
        g_free(buf);
    }
    return NULL;
}

void qemu_add_data_dir(char *path)
{
    int i;

    if (path == NULL) {
        return;
    }
    if (data_dir_idx == ARRAY_SIZE(data_dir)) {
        return;
    }
    for (i = 0; i < data_dir_idx; i++) {
        if (strcmp(data_dir[i], path) == 0) {
            g_free(path); /* duplicate */
            return;
        }
    }
    data_dir[data_dir_idx++] = path;
}

void qemu_add_default_firmwarepath(void)
{
    static const char * const dirs[] = {
        CONFIG_QEMU_FIRMWAREPATH
        NULL
    };

    size_t i;

    /* add configured firmware directories */
    for (i = 0; dirs[i] != NULL; i++) {
        qemu_add_data_dir(get_relocated_path(dirs[i]));
    }

    /* try to find datadir relative to the executable path */
    qemu_add_data_dir(get_relocated_path(CONFIG_QEMU_DATADIR));
}

void qemu_list_data_dirs(void)
{
    int i;
    for (i = 0; i < data_dir_idx; i++) {
        printf("%s\n", data_dir[i]);
    }
}
