/*
 * QTest testcase for ivshmem
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "libqtest.h"
#include "qemu/osdep.h"

static char dev_shm_path[] = "/dev/shm/qtest.XXXXXX";

/* Tests only initialization so far. TODO: Replace with functional tests */
static void nop(void)
{
}

int main(int argc, char **argv)
{
    QTestState *s1, *s2;
    char *cmd;
    int ret, fd;

    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/ivshmem/nop", nop);

    fd = mkstemp(dev_shm_path);
    g_assert(fd >= 0);
    close(fd);
    unlink(dev_shm_path);

    cmd = g_strdup_printf("-device ivshmem,shm=%s,size=1M", &dev_shm_path[9]);
    s1 = qtest_start(cmd);
    s2 = qtest_start(cmd);
    g_free(cmd);

    ret = g_test_run();

    qtest_quit(s1);
    qtest_quit(s2);

    unlink(dev_shm_path);

    return ret;
}
