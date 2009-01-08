/*
 *  Copyright (C) 2005  Anthony Liguori <anthony@codemonkey.ws>
 *
 *  Network Block Device
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; under version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA
 */

#include <qemu-common.h>
#include "block_int.h"
#include "nbd.h"

#include <stdarg.h>
#include <stdio.h>
#include <getopt.h>
#include <err.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <signal.h>

#define SOCKET_PATH    "/var/lock/qemu-nbd-%s"

#define NBD_BUFFER_SIZE (1024*1024)

static int verbose;

static void usage(const char *name)
{
    printf(
"Usage: %s [OPTIONS] FILE\n"
"QEMU Disk Network Block Device Server\n"
"\n"
"  -p, --port=PORT      port to listen on (default `1024')\n"
"  -o, --offset=OFFSET  offset into the image\n"
"  -b, --bind=IFACE     interface to bind to (default `0.0.0.0')\n"
"  -k, --socket=PATH    path to the unix socket\n"
"                       (default '"SOCKET_PATH"')\n"
"  -r, --read-only      export read-only\n"
"  -P, --partition=NUM  only expose partition NUM\n"
"  -s, --snapshot       use snapshot file\n"
"  -n, --nocache        disable host cache\n"
"  -c, --connect=DEV    connect FILE to the local NBD device DEV\n"
"  -d, --disconnect     disconnect the specified device\n"
"  -e, --shared=NUM     device can be shared by NUM clients (default '1')\n"
"  -t, --persistent     don't exit on the last connection\n"
"  -v, --verbose        display extra debugging information\n"
"  -h, --help           display this help and exit\n"
"  -V, --version        output version information and exit\n"
"\n"
"Report bugs to <anthony@codemonkey.ws>\n"
    , name, "DEVICE");
}

static void version(const char *name)
{
    printf(
"%s version 0.0.1\n"
"Written by Anthony Liguori.\n"
"\n"
"Copyright (C) 2006 Anthony Liguori <anthony@codemonkey.ws>.\n"
"This is free software; see the source for copying conditions.  There is NO\n"
"warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n"
    , name);
}

struct partition_record
{
    uint8_t bootable;
    uint8_t start_head;
    uint32_t start_cylinder;
    uint8_t start_sector;
    uint8_t system;
    uint8_t end_head;
    uint8_t end_cylinder;
    uint8_t end_sector;
    uint32_t start_sector_abs;
    uint32_t nb_sectors_abs;
};

static void read_partition(uint8_t *p, struct partition_record *r)
{
    r->bootable = p[0];
    r->start_head = p[1];
    r->start_cylinder = p[3] | ((p[2] << 2) & 0x0300);
    r->start_sector = p[2] & 0x3f;
    r->system = p[4];
    r->end_head = p[5];
    r->end_cylinder = p[7] | ((p[6] << 2) & 0x300);
    r->end_sector = p[6] & 0x3f;
    r->start_sector_abs = p[8] | p[9] << 8 | p[10] << 16 | p[11] << 24;
    r->nb_sectors_abs = p[12] | p[13] << 8 | p[14] << 16 | p[15] << 24;
}

static int find_partition(BlockDriverState *bs, int partition,
                          off_t *offset, off_t *size)
{
    struct partition_record mbr[4];
    uint8_t data[512];
    int i;
    int ext_partnum = 4;

    if (bdrv_read(bs, 0, data, 1))
        errx(EINVAL, "error while reading");

    if (data[510] != 0x55 || data[511] != 0xaa) {
        errno = -EINVAL;
        return -1;
    }

    for (i = 0; i < 4; i++) {
        read_partition(&data[446 + 16 * i], &mbr[i]);

        if (!mbr[i].nb_sectors_abs)
            continue;

        if (mbr[i].system == 0xF || mbr[i].system == 0x5) {
            struct partition_record ext[4];
            uint8_t data1[512];
            int j;

            if (bdrv_read(bs, mbr[i].start_sector_abs, data1, 1))
                errx(EINVAL, "error while reading");

            for (j = 0; j < 4; j++) {
                read_partition(&data1[446 + 16 * j], &ext[j]);
                if (!ext[j].nb_sectors_abs)
                    continue;

                if ((ext_partnum + j + 1) == partition) {
                    *offset = (uint64_t)ext[j].start_sector_abs << 9;
                    *size = (uint64_t)ext[j].nb_sectors_abs << 9;
                    return 0;
                }
            }
            ext_partnum += 4;
        } else if ((i + 1) == partition) {
            *offset = (uint64_t)mbr[i].start_sector_abs << 9;
            *size = (uint64_t)mbr[i].nb_sectors_abs << 9;
            return 0;
        }
    }

    errno = -ENOENT;
    return -1;
}

static void show_parts(const char *device)
{
    if (fork() == 0) {
        int nbd;

        /* linux just needs an open() to trigger
         * the partition table update
         * but remember to load the module with max_part != 0 :
         *     modprobe nbd max_part=63
         */
        nbd = open(device, O_RDWR);
        if (nbd != -1)
              close(nbd);
        exit(0);
    }
}

int main(int argc, char **argv)
{
    BlockDriverState *bs;
    off_t dev_offset = 0;
    off_t offset = 0;
    bool readonly = false;
    bool disconnect = false;
    const char *bindto = "0.0.0.0";
    int port = 1024;
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    off_t fd_size;
    char *device = NULL;
    char *socket = NULL;
    char sockpath[128];
    const char *sopt = "hVb:o:p:rsnP:c:dvk:e:t";
    struct option lopt[] = {
        { "help", 0, 0, 'h' },
        { "version", 0, 0, 'V' },
        { "bind", 1, 0, 'b' },
        { "port", 1, 0, 'p' },
        { "socket", 1, 0, 'k' },
        { "offset", 1, 0, 'o' },
        { "read-only", 0, 0, 'r' },
        { "partition", 1, 0, 'P' },
        { "connect", 1, 0, 'c' },
        { "disconnect", 0, 0, 'd' },
        { "snapshot", 0, 0, 's' },
        { "nocache", 0, 0, 'n' },
        { "shared", 1, 0, 'e' },
        { "persistent", 0, 0, 't' },
        { "verbose", 0, 0, 'v' },
        { NULL, 0, 0, 0 }
    };
    int ch;
    int opt_ind = 0;
    int li;
    char *end;
    int flags = 0;
    int partition = -1;
    int ret;
    int shared = 1;
    uint8_t *data;
    fd_set fds;
    int *sharing_fds;
    int fd;
    int i;
    int nb_fds = 0;
    int max_fd;
    int persistent = 0;

    while ((ch = getopt_long(argc, argv, sopt, lopt, &opt_ind)) != -1) {
        switch (ch) {
        case 's':
            flags |= BDRV_O_SNAPSHOT;
            break;
        case 'n':
            flags |= BDRV_O_NOCACHE;
            break;
        case 'b':
            bindto = optarg;
            break;
        case 'p':
            li = strtol(optarg, &end, 0);
            if (*end) {
                errx(EINVAL, "Invalid port `%s'", optarg);
            }
            if (li < 1 || li > 65535) {
                errx(EINVAL, "Port out of range `%s'", optarg);
            }
            port = (uint16_t)li;
            break;
        case 'o':
                dev_offset = strtoll (optarg, &end, 0);
            if (*end) {
                errx(EINVAL, "Invalid offset `%s'", optarg);
            }
            if (dev_offset < 0) {
                errx(EINVAL, "Offset must be positive `%s'", optarg);
            }
            break;
        case 'r':
            readonly = true;
            break;
        case 'P':
            partition = strtol(optarg, &end, 0);
            if (*end)
                errx(EINVAL, "Invalid partition `%s'", optarg);
            if (partition < 1 || partition > 8)
                errx(EINVAL, "Invalid partition %d", partition);
            break;
        case 'k':
            socket = optarg;
            if (socket[0] != '/')
                errx(EINVAL, "socket path must be absolute\n");
            break;
        case 'd':
            disconnect = true;
            break;
        case 'c':
            device = optarg;
            break;
        case 'e':
            shared = strtol(optarg, &end, 0);
            if (*end) {
                errx(EINVAL, "Invalid shared device number '%s'", optarg);
            }
            if (shared < 1) {
                errx(EINVAL, "Shared device number must be greater than 0\n");
            }
            break;
	case 't':
	    persistent = 1;
	    break;
        case 'v':
            verbose = 1;
            break;
        case 'V':
            version(argv[0]);
            exit(0);
            break;
        case 'h':
            usage(argv[0]);
            exit(0);
            break;
        case '?':
            errx(EINVAL, "Try `%s --help' for more information.",
                 argv[0]);
        }
    }

    if ((argc - optind) != 1) {
        errx(EINVAL, "Invalid number of argument.\n"
             "Try `%s --help' for more information.",
             argv[0]);
    }

    if (disconnect) {
        fd = open(argv[optind], O_RDWR);
        if (fd == -1)
            errx(errno, "Cannot open %s", argv[optind]);

        nbd_disconnect(fd);

        close(fd);

        printf("%s disconnected\n", argv[optind]);

	return 0;
    }

    bdrv_init();

    bs = bdrv_new("hda");
    if (bs == NULL)
        return 1;

    if (bdrv_open(bs, argv[optind], flags) == -1)
        return 1;

    fd_size = bs->total_sectors * 512;

    if (partition != -1 &&
        find_partition(bs, partition, &dev_offset, &fd_size))
        errx(errno, "Could not find partition %d", partition);

    if (device) {
        pid_t pid;
        int sock;

        if (!verbose)
            daemon(0, 0);	/* detach client and server */

        if (socket == NULL) {
            sprintf(sockpath, SOCKET_PATH, basename(device));
            socket = sockpath;
        }

        pid = fork();
        if (pid < 0)
            return 1;
        if (pid != 0) {
            off_t size;
            size_t blocksize;

            ret = 0;
            bdrv_close(bs);

            do {
                sock = unix_socket_outgoing(socket);
                if (sock == -1) {
                    if (errno != ENOENT && errno != ECONNREFUSED)
                        goto out;
                    sleep(1);	/* wait children */
                }
            } while (sock == -1);

            fd = open(device, O_RDWR);
            if (fd == -1) {
                ret = 1;
                goto out;
            }

            ret = nbd_receive_negotiate(sock, &size, &blocksize);
            if (ret == -1) {
                ret = 1;
                goto out;
            }

            ret = nbd_init(fd, sock, size, blocksize);
            if (ret == -1) {
                ret = 1;
                goto out;
            }

            printf("NBD device %s is now connected to file %s\n",
                    device, argv[optind]);

	    /* update partition table */

            show_parts(device);

            nbd_client(fd, sock);
            close(fd);
 out:
            kill(pid, SIGTERM);
            unlink(socket);

            return ret;
        }
        /* children */
    }

    sharing_fds = qemu_malloc((shared + 1) * sizeof(int));
    if (sharing_fds == NULL)
        errx(ENOMEM, "Cannot allocate sharing fds");

    if (socket) {
        sharing_fds[0] = unix_socket_incoming(socket);
    } else {
        sharing_fds[0] = tcp_socket_incoming(bindto, port);
    }

    if (sharing_fds[0] == -1)
        return 1;
    max_fd = sharing_fds[0];
    nb_fds++;

    data = qemu_memalign(512, NBD_BUFFER_SIZE);
    if (data == NULL)
        errx(ENOMEM, "Cannot allocate data buffer");

    do {

        FD_ZERO(&fds);
        for (i = 0; i < nb_fds; i++)
            FD_SET(sharing_fds[i], &fds);

        ret = select(max_fd + 1, &fds, NULL, NULL, NULL);
        if (ret == -1)
            break;

        if (FD_ISSET(sharing_fds[0], &fds))
            ret--;
        for (i = 1; i < nb_fds && ret; i++) {
            if (FD_ISSET(sharing_fds[i], &fds)) {
                if (nbd_trip(bs, sharing_fds[i], fd_size, dev_offset,
                    &offset, readonly, data, NBD_BUFFER_SIZE) != 0) {
                    close(sharing_fds[i]);
                    nb_fds--;
                    sharing_fds[i] = sharing_fds[nb_fds];
                    i--;
                }
                ret--;
            }
        }
        /* new connection ? */
        if (FD_ISSET(sharing_fds[0], &fds)) {
            if (nb_fds < shared + 1) {
                sharing_fds[nb_fds] = accept(sharing_fds[0],
                                             (struct sockaddr *)&addr,
                                             &addr_len);
                if (sharing_fds[nb_fds] != -1 &&
                    nbd_negotiate(sharing_fds[nb_fds], fd_size) != -1) {
                        if (sharing_fds[nb_fds] > max_fd)
                            max_fd = sharing_fds[nb_fds];
                        nb_fds++;
                }
            }
        }
    } while (persistent || nb_fds > 1);
    qemu_free(data);

    close(sharing_fds[0]);
    bdrv_close(bs);
    qemu_free(sharing_fds);
    if (socket)
        unlink(socket);

    return 0;
}
