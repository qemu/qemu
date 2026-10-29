/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Unit tests for the CPR fd table
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "migration/cpr.h"
#include "migration/vmstate.h"
#include "monitor/monitor.h"
#include "hw/vfio/vfio-cpr.h"

/* Stubs for the parts of migration/cpr.c that these tests don't exercise */

const VMStateDescription vmstate_cpr_vfio_devices = {
    .name = "cpr vfio devices",
    .fields = (const VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

QEMUFile *cpr_transfer_output(MigrationChannel *channel, Error **errp)
{
    g_assert_not_reached();
}

QEMUFile *cpr_transfer_input(MigrationChannel *channel, Error **errp)
{
    g_assert_not_reached();
}

QEMUFile *cpr_exec_output(Error **errp)
{
    g_assert_not_reached();
}

QEMUFile *cpr_exec_input(Error **errp)
{
    g_assert_not_reached();
}

bool cpr_exec_persist_state(QEMUFile *f, Error **errp)
{
    g_assert_not_reached();
}

bool cpr_exec_has_state(void)
{
    return false;
}

void cpr_exec_unpreserve_fds(void)
{
}

int monitor_fd_param(Monitor *mon, const char *fdname, Error **errp)
{
    g_assert_not_reached();
}

static void test_cpr_fd_duplicate(void)
{
    Error *err = NULL;

    g_assert_true(cpr_save_fd("dup", 0, 10, &error_abort));

    /* A second registration of the same (name, id) key must be refused */
    g_assert_false(cpr_save_fd("dup", 0, 11, &err));
    g_assert_nonnull(err);
    g_assert_nonnull(strstr(error_get_pretty(err), "already registered"));
    error_free(err);

    /* and the first registration must be left intact */
    g_assert_cmpint(cpr_find_fd("dup", 0), ==, 10);

    /* A different id or name is a different key */
    g_assert_true(cpr_save_fd("dup", 1, 12, &error_abort));
    g_assert_true(cpr_save_fd("other", 0, 13, &error_abort));
    g_assert_cmpint(cpr_find_fd("dup", 1), ==, 12);
    g_assert_cmpint(cpr_find_fd("other", 0), ==, 13);

    /* Once deleted, the key can be registered again */
    cpr_delete_fd("dup", 0);
    g_assert_cmpint(cpr_find_fd("dup", 0), ==, -1);
    g_assert_true(cpr_save_fd("dup", 0, 14, &error_abort));
    g_assert_cmpint(cpr_find_fd("dup", 0), ==, 14);

    cpr_delete_fd("dup", 0);
    cpr_delete_fd("dup", 1);
    cpr_delete_fd("other", 0);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/cpr/fd/duplicate", test_cpr_fd_duplicate);
    return g_test_run();
}
