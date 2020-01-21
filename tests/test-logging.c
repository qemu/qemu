/*
 * logging unit-tests
 *
 * Copyright (C) 2016 Linaro Ltd.
 *
 *  Author: Alex Bennée <alex.bennee@linaro.org>
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
#include <glib/gstdio.h>

#include "qemu-common.h"
#include "qapi/error.h"
#include "qemu/log.h"

static void test_parse_range(void)
{
    Error *err = NULL;

    qemu_set_dfilter_ranges("0x1000+0x100", &error_abort);

    g_assert_false(qemu_log_in_addr_range(0xfff));
    g_assert(qemu_log_in_addr_range(0x1000));
    g_assert(qemu_log_in_addr_range(0x1001));
    g_assert(qemu_log_in_addr_range(0x10ff));
    g_assert_false(qemu_log_in_addr_range(0x1100));

    qemu_set_dfilter_ranges("0x1000-0x100", &error_abort);

    g_assert_false(qemu_log_in_addr_range(0x1001));
    g_assert(qemu_log_in_addr_range(0x1000));
    g_assert(qemu_log_in_addr_range(0x0f01));
    g_assert_false(qemu_log_in_addr_range(0x0f00));

    qemu_set_dfilter_ranges("0x1000..0x1100", &error_abort);

    g_assert_false(qemu_log_in_addr_range(0xfff));
    g_assert(qemu_log_in_addr_range(0x1000));
    g_assert(qemu_log_in_addr_range(0x1100));
    g_assert_false(qemu_log_in_addr_range(0x1101));

    qemu_set_dfilter_ranges("0x1000..0x1000", &error_abort);

    g_assert_false(qemu_log_in_addr_range(0xfff));
    g_assert(qemu_log_in_addr_range(0x1000));
    g_assert_false(qemu_log_in_addr_range(0x1001));

    qemu_set_dfilter_ranges("0x1000+0x100,0x2100-0x100,0x3000..0x3100",
                            &error_abort);
    g_assert(qemu_log_in_addr_range(0x1050));
    g_assert(qemu_log_in_addr_range(0x2050));
    g_assert(qemu_log_in_addr_range(0x3050));

    qemu_set_dfilter_ranges("0xffffffffffffffff-1", &error_abort);
    g_assert(qemu_log_in_addr_range(UINT64_MAX));
    g_assert_false(qemu_log_in_addr_range(UINT64_MAX - 1));

    qemu_set_dfilter_ranges("0..0xffffffffffffffff", &err);
    g_assert(qemu_log_in_addr_range(0));
    g_assert(qemu_log_in_addr_range(UINT64_MAX));
 
    qemu_set_dfilter_ranges("2..1", &err);
    error_free_or_abort(&err);

    qemu_set_dfilter_ranges("0x1000+onehundred", &err);
    error_free_or_abort(&err);

    qemu_set_dfilter_ranges("0x1000+0", &err);
    error_free_or_abort(&err);
}

static void set_log_path_tmp(char const *dir, char const *tpl, Error **errp)
{
    gchar *file_path = g_build_filename(dir, tpl, NULL);

    qemu_set_log_filename(file_path, errp);
    g_free(file_path);
}

static void test_parse_path(gconstpointer data)
{
    gchar const *tmp_path = data;
    Error *err = NULL;

    set_log_path_tmp(tmp_path, "qemu.log", &error_abort);
    set_log_path_tmp(tmp_path, "qemu-%d.log", &error_abort);
    set_log_path_tmp(tmp_path, "qemu.log.%d", &error_abort);

    set_log_path_tmp(tmp_path, "qemu-%d%d.log", &err);
    error_free_or_abort(&err);
}

static void test_logfile_write(gconstpointer data)
{
    QemuLogFile *logfile;
    QemuLogFile *logfile2;
    gchar const *dir = data;
    Error *err = NULL;
    g_autofree gchar *file_path = NULL;
    g_autofree gchar *file_path1 = NULL;
    FILE *orig_fd;

    /*
     * Before starting test, set log flags, to ensure the file gets
     * opened below with the call to qemu_set_log_filename().
     * In cases where a logging backend other than log is used,
     * this is needed.
     */
    qemu_set_log(CPU_LOG_TB_OUT_ASM);
    file_path = g_build_filename(dir, "qemu_test_log_write0.log", NULL);
    file_path1 = g_build_filename(dir, "qemu_test_log_write1.log", NULL);

    /*
     * Test that even if an open file handle is changed,
     * our handle remains valid due to RCU.
     */
    qemu_set_log_filename(file_path, &err);
    g_assert(!err);
    rcu_read_lock();
    logfile = atomic_rcu_read(&qemu_logfile);
    orig_fd = logfile->fd;
    g_assert(logfile && logfile->fd);
    fprintf(logfile->fd, "%s 1st write to file\n", __func__);
    fflush(logfile->fd);

    /* Change the logfile and ensure that the handle is still valid. */
    qemu_set_log_filename(file_path1, &err);
    g_assert(!err);
    logfile2 = atomic_rcu_read(&qemu_logfile);
    g_assert(logfile->fd == orig_fd);
    g_assert(logfile2->fd != logfile->fd);
    fprintf(logfile->fd, "%s 2nd write to file\n", __func__);
    fflush(logfile->fd);
    rcu_read_unlock();
}

static void test_logfile_lock(gconstpointer data)
{
    FILE *logfile;
    gchar const *dir = data;
    Error *err = NULL;
    g_autofree gchar *file_path = NULL;

    file_path = g_build_filename(dir, "qemu_test_logfile_lock0.log", NULL);

    /*
     * Test the use of the logfile lock, such
     * that even if an open file handle is closed,
     * our handle remains valid for use due to RCU.
     */
    qemu_set_log_filename(file_path, &err);
    logfile = qemu_log_lock();
    g_assert(logfile);
    fprintf(logfile, "%s 1st write to file\n", __func__);
    fflush(logfile);

    /*
     * Initiate a close file and make sure our handle remains
     * valid since we still have the logfile lock.
     */
    qemu_log_close();
    fprintf(logfile, "%s 2nd write to file\n", __func__);
    fflush(logfile);
    qemu_log_unlock(logfile);

    g_assert(!err);
}

/* Remove a directory and all its entries (non-recursive). */
static void rmdir_full(gchar const *root)
{
    GDir *root_gdir = g_dir_open(root, 0, NULL);
    gchar const *entry_name;

    g_assert_nonnull(root_gdir);
    while ((entry_name = g_dir_read_name(root_gdir)) != NULL) {
        gchar *entry_path = g_build_filename(root, entry_name, NULL);
        g_assert(g_remove(entry_path) == 0);
        g_free(entry_path);
    }
    g_dir_close(root_gdir);
    g_assert(g_rmdir(root) == 0);
}

int main(int argc, char **argv)
{
    gchar *tmp_path = g_dir_make_tmp("qemu-test-logging.XXXXXX", NULL);
    int rc;

    g_test_init(&argc, &argv, NULL);
    g_assert_nonnull(tmp_path);

    g_test_add_func("/logging/parse_range", test_parse_range);
    g_test_add_data_func("/logging/parse_path", tmp_path, test_parse_path);
    g_test_add_data_func("/logging/logfile_write_path",
                         tmp_path, test_logfile_write);
    g_test_add_data_func("/logging/logfile_lock_path",
                         tmp_path, test_logfile_lock);

    rc = g_test_run();

    rmdir_full(tmp_path);
    g_free(tmp_path);
    return rc;
}
