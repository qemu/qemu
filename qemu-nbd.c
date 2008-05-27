/*\
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <qemu-common.h>
#include "block_int.h"
#include "nbd.h"

#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <getopt.h>
#include <err.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

int verbose;

static void usage(const char *name)
{
    printf(
"Usage: %s [OPTIONS] FILE\n"
"QEMU Disk Network Block Device Server\n"
"\n"
"  -p, --port=PORT      port to listen on (default `1024')\n"
"  -o, --offset=OFFSET  offset into the image\n"
"  -b, --bind=IFACE     interface to bind to (default `0.0.0.0')\n"
"  -r, --read-only      export read-only\n"
"  -P, --partition=NUM  only expose partition NUM\n"
"  -v, --verbose        display extra debugging information\n"
"  -h, --help           display this help and exit\n"
"  -V, --version        output version information and exit\n"
"\n"
"Report bugs to <anthony@codemonkey.ws>\n"
    , name);
}

static void version(const char *name)
{
    printf(
"qemu-nbd version 0.0.1\n"
"Written by Anthony Liguori.\n"
"\n"
"Copyright (C) 2006 Anthony Liguori <anthony@codemonkey.ws>.\n"
"This is free software; see the source for copying conditions.  There is NO\n"
"warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n"
    );
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

int main(int argc, char **argv)
{
    BlockDriverState *bs;
    off_t dev_offset = 0;
    off_t offset = 0;
    bool readonly = false;
    const char *bindto = "0.0.0.0";
    int port = 1024;
    int sock, csock;
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    off_t fd_size;
    const char *sopt = "hVbo:p:rsP:v";
    struct option lopt[] = {
        { "help", 0, 0, 'h' },
        { "version", 0, 0, 'V' },
        { "bind", 1, 0, 'b' },
        { "port", 1, 0, 'p' },
        { "offset", 1, 0, 'o' },
        { "read-only", 0, 0, 'r' },
        { "partition", 1, 0, 'P' },
        { "snapshot", 0, 0, 's' },
        { "verbose", 0, 0, 'v' },
    };
    int ch;
    int opt_ind = 0;
    int li;
    char *end;
    bool snapshot = false;
    int partition = -1;

    while ((ch = getopt_long(argc, argv, sopt, lopt, &opt_ind)) != -1) {
        switch (ch) {
        case 's':
            snapshot = true;
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

    bdrv_init();

    bs = bdrv_new("hda");
    if (bs == NULL)
        return 1;

    if (bdrv_open(bs, argv[optind], snapshot) == -1)
        return 1;

    fd_size = bs->total_sectors * 512;

    if (partition != -1 &&
        find_partition(bs, partition, &dev_offset, &fd_size))
        errx(errno, "Could not find partition %d", partition);

    sock = tcp_socket_incoming(bindto, port);
    if (sock == -1)
        return 1;

    csock = accept(sock,
               (struct sockaddr *)&addr,
               &addr_len);
    if (csock == -1)
        return 1;

    /* new fd_size is calculated by find_partition */
    if (nbd_negotiate(bs, csock, fd_size) == -1)
        return 1;

    while (nbd_trip(bs, csock, fd_size, dev_offset, &offset, readonly) == 0);

    close(csock);
    close(sock);
    bdrv_close(bs);

    return 0;
}
