/*
 * Copyright (c) 2010-2011 IBM
 *
 * Authors:
 *         Chunqiang Tang <ctang@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

/*=============================================================================
 * qemu-io-sim works with qemu-io to perform simulated testing. The 'sim'
 * command allows the user to control the order of disk I/O and callback
 * activities in order to test rare race conditions. Note that once 'sim
 * enable' is done, it can only test aio_read and aio_write. See block/sim.c
 * for the simulated block device driver.
 *============================================================================*/

#include "block/blksim.h"

void fvd_init_prefetch (BlockDriverState * bs);
static void sim_start_prefetch(BlockDriverState *bs)
{
    if (!bs->drv->format_name || !strncmp (bs->drv->format_name, "fvd", 3)) {
        printf ("This image does not support prefetching.\n");
        return;
    }
    fvd_init_prefetch (bs);
    printf ("Prefetching started\n");
}

static void sim_help (void)
{
    printf ("\n"
            " sim enable\t\tenable simulation\n"
            " sim list\t\tlist all simulation tasks\n"
            " sim <#task> [#ret]\trun a simulation task, optionally uing #ret as the return value of a read/write operation\n"
            " sim all [#ret]\t\trun all tasks, optionally using #ret as the return value of read/write tasks\n"
            " sim prefetch\t\tstart prefetching\n");
}

static int sim_f(BlockDriverState *bs, int argc, char **argv)
{
    int ret = 0;

    if (argc == 3) {
        ret = atoi (argv[2]);
    }
    else if (argc != 2) {
        sim_help ();
        return 0;
    }

    if (strcmp (argv[1], "enable") == 0) {
        if (bs) {
            printf ("Please close the image first. \"sim enable\" must be done before the\n"
                    "image is opened so that the image is opened with simulation support.\n");
        }
        else {
            enable_block_sim(1/*print*/, 0 /*no random time*/);
            printf ("Block device simulation is enabled.\n");
        }
        return 0;
    }

    if (!bs) {
        fprintf(stderr, "no file open, try 'help open'\n");
        return 0;
    }

    if (!bdrv_find_format("blksim")) {
        printf ("\"sim enable\" must be done before invoking any other sim commands.\n");
        return 0;
    }

    if (strcmp (argv[1], "list") == 0) {
        sim_list_tasks ();
    }
    else if (strcmp (argv[1], "prefetch") == 0) {
        sim_start_prefetch(bs);
    }
    else if (strcmp (argv[1], "all") == 0) {
        sim_set_disk_io_return_code (ret);
        int n = sim_all_tasks ();
        sim_set_disk_io_return_code (0);
        printf ("Executed %d tasks.\n", n);
    }
    else {
        sim_set_disk_io_return_code (ret);
        sim_task_by_uuid (atoll (argv[1]));
        sim_set_disk_io_return_code (0);
    }

    return 0;
}

static const cmdinfo_t sim_cmd = {
    .name = "sim",
    .altname = "s",
    .cfunc = sim_f,
    .argmin = 1,
    .argmax = 2,
    .args = "",
    .oneline = "use simulation to control the order of disk I/Os and callbacks",
    .flags = CMD_NOFILE_OK,
    .help = sim_help,
};
