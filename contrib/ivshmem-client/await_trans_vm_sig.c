//communicate with ivshmem_server using argument
//retrieve event fds, await on one of them.
/*
 * Copyright 6WIND S.A., 2014
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"

#include "ivshmem-client.h"

int
main(int argc, char *argv[])
{
    fd_set fds;
    int maxfd;
    struct sigaction sa;
    IvshmemClient client;
    IvshmemClientArgs args = {
        .verbose = IVSHMEM_CLIENT_DEFAULT_VERBOSE,
        .unix_sock_path = IVSHMEM_CLIENT_DEFAULT_UNIX_SOCK_PATH,
    };

    /* parse arguments, will exit on error */
    ivshmem_client_parse_args(&args, argc, argv);

    /* Ignore SIGPIPE, see this link for more info:
     * http://www.mail-archive.com/libevent-users@monkey.org/msg01606.html */
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = 0;
    if (sigemptyset(&sa.sa_mask) == -1 ||
        sigaction(SIGPIPE, &sa, 0) == -1) {
        perror("failed to ignore SIGPIPE; sigaction");
        return 1;
    }

    ivshmem_client_cmdline_help();
    printf("cmd> ");
    fflush(stdout);

    if (ivshmem_client_init(&client, args.unix_sock_path,
                            ivshmem_client_notification_cb, NULL,
                            args.verbose) < 0) {
        fprintf(stderr, "cannot init client\n");
        return 1;
    }

    if (ivshmem_client_connect(&client) < 0) {
      fprintf(stderr, "cannot connect to server\n");
      return 1;
    }

    fprintf(stdout, "listen on server socket %d\n", client.sock_fd);

    maxfd = 0;
    char junk[10];
    ivshmem_client_handle_server_msg(&client);
    ivshmem_client_handle_server_msg(&client);
    ivshmem_client_dump(&client);
    FD_ZERO(&fds);
    ivshmem_client_get_fds(&client, &fds, &maxfd);
    
    select(maxfd, &fds, NULL, NULL, NULL);
    fprintf(stdout, "read %ld from eventfd; vectors_count=%d, maxfd=%d\n",
            read(client.local.vectors[0], junk, 8), client.local.vectors_count, maxfd);

    ivshmem_client_close(&client);

    return 0;
}
