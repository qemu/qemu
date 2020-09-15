/*
 * Test HMP commands.
 *
 * Copyright (c) 2017 Red Hat Inc.
 *
 * Author:
 *    Thomas Huth <thuth@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2
 * or later. See the COPYING file in the top-level directory.
 *
 * This test calls some HMP commands for all machines that the current
 * QEMU binary provides, to check whether they terminate successfully
 * (i.e. do not crash QEMU).
 */

#include "qemu/osdep.h"
#include "libqos/libqtest.h"

static int verbose;

static const char *hmp_cmds[] = {
    "announce_self",
    "boot_set ndc",
    "chardev-add null,id=testchardev1",
    "chardev-send-break testchardev1",
    "chardev-change testchardev1 ringbuf",
    "chardev-remove testchardev1",
    "commit all",
    "cpu 0",
    "device_add ?",
    "device_add usb-mouse,id=mouse1",
    "drive_add ignored format=help",
    "mouse_button 7",
    "mouse_move 10 10",
    "mouse_button 0",
    "device_del mouse1",
    "dump-guest-memory /dev/null 0 4096",
    "dump-guest-memory /dev/null",
    "gdbserver",
    "gva2gpa 0",
    "hostfwd_add tcp::43210-:43210",
    "hostfwd_remove tcp::43210-:43210",
    "i /w 0",
    "log all",
    "log none",
    "memsave 0 4096 \"/dev/null\"",
    "migrate_set_cache_size 1",
    "migrate_set_downtime 1",
    "migrate_set_speed 1",
    "netdev_add user,id=net1",
    "set_link net1 off",
    "set_link net1 on",
    "netdev_del net1",
    "nmi",
    "o /w 0 0x1234",
    "object_add memory-backend-ram,id=mem1,size=256M",
    "object_del mem1",
    "pmemsave 0 4096 \"/dev/null\"",
    "p $pc + 8",
    "qom-list /",
    "qom-set /machine initrd test",
    "qom-get /machine initrd",
    "screendump /dev/null",
    "sendkey x",
    "singlestep on",
    "wavcapture /dev/null",
    "stopcapture 0",
    "sum 0 512",
    "x /8i 0x100",
    "xp /16x 0",
    NULL
};

/* Run through the list of pre-defined commands */
static void test_commands(QTestState *qts)
{
    char *response;
    int i;

    for (i = 0; hmp_cmds[i] != NULL; i++) {
        response = qtest_hmp(qts, "%s", hmp_cmds[i]);
        if (verbose) {
            fprintf(stderr,
                    "\texecute HMP command: %s\n"
                    "\tresult             : %s\n",
                    hmp_cmds[i], response);
        }
        g_free(response);
    }

}

/* Run through all info commands and call them blindly (without arguments) */
static void test_info_commands(QTestState *qts)
{
    char *resp, *info, *info_buf, *endp;

    info_buf = info = qtest_hmp(qts, "help info");

    while (*info) {
        /* Extract the info command, ignore parameters and description */
        g_assert(strncmp(info, "info ", 5) == 0);
        endp = strchr(&info[5], ' ');
        g_assert(endp != NULL);
        *endp = '\0';
        /* Now run the info command */
        if (verbose) {
            fprintf(stderr, "\t%s\n", info);
        }
        resp = qtest_hmp(qts, "%s", info);
        g_free(resp);
        /* And move forward to the next line */
        info = strchr(endp + 1, '\n');
        if (!info) {
            break;
        }
        info += 1;
    }

    g_free(info_buf);
}

static void test_machine(gconstpointer data)
{
    const char *machine = data;
    char *args;
    QTestState *qts;

    args = g_strdup_printf("-S -M %s", machine);
    qts = qtest_init(args);

    test_info_commands(qts);
    test_commands(qts);

    qtest_quit(qts);
    g_free(args);
    g_free((void *)data);
}

static void add_machine_test_case(const char *mname)
{
    char *path;

    path = g_strdup_printf("hmp/%s", mname);
    qtest_add_data_func(path, g_strdup(mname), test_machine);
    g_free(path);
}

int main(int argc, char **argv)
{
    char *v_env = getenv("V");

    if (v_env && *v_env >= '2') {
        verbose = true;
    }

    g_test_init(&argc, &argv, NULL);

    qtest_cb_for_every_machine(add_machine_test_case, g_test_quick());

    /* as none machine has no memory by default, add a test case with memory */
    qtest_add_data_func("hmp/none+2MB", g_strdup("none -m 2"), test_machine);

    return g_test_run();
}
