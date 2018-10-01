/*
 * QEMU EDID test tool.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/bswap.h"
#include "qemu/cutils.h"
#include "hw/display/edid.h"

static qemu_edid_info info;

static void usage(FILE *out)
{
    fprintf(out,
            "\n"
            "This is a test tool for the qemu edid generator.\n"
            "\n"
            "Typically you'll pipe the output into edid-decode\n"
            "to check if the generator works correctly.\n"
            "\n"
            "usage: qemu-edid <options>\n"
            "options:\n"
            "    -h             print this text\n"
            "    -o <file>      set output file (stdout by default)\n"
            "    -v <vendor>    set monitor vendor (three letters)\n"
            "    -n <name>      set monitor name\n"
            "    -s <serial>    set monitor serial\n"
            "    -d <dpi>       set display resolution\n"
            "    -x <prefx>     set preferred width\n"
            "    -y <prefy>     set preferred height\n"
            "    -X <maxx>      set maximum width\n"
            "    -Y <maxy>      set maximum height\n"
            "\n");
}

int main(int argc, char *argv[])
{
    FILE *outfile = NULL;
    uint8_t blob[256];
    int rc;

    for (;;) {
        rc = getopt(argc, argv, "ho:x:y:X:Y:d:v:n:s:");
        if (rc == -1) {
            break;
        }
        switch (rc) {
        case 'o':
            if (outfile) {
                fprintf(stderr, "outfile specified twice\n");
                exit(1);
            }
            outfile = fopen(optarg, "w");
            if (outfile == NULL) {
                fprintf(stderr, "open %s: %s\n", optarg, strerror(errno));
                exit(1);
            }
            break;
        case 'x':
            if (qemu_strtoui(optarg, NULL, 10, &info.prefx) < 0) {
                fprintf(stderr, "not a number: %s\n", optarg);
                exit(1);
            }
            break;
        case 'y':
            if (qemu_strtoui(optarg, NULL, 10, &info.prefy) < 0) {
                fprintf(stderr, "not a number: %s\n", optarg);
                exit(1);
            }
            break;
        case 'X':
            if (qemu_strtoui(optarg, NULL, 10, &info.maxx) < 0) {
                fprintf(stderr, "not a number: %s\n", optarg);
                exit(1);
            }
            break;
        case 'Y':
            if (qemu_strtoui(optarg, NULL, 10, &info.maxy) < 0) {
                fprintf(stderr, "not a number: %s\n", optarg);
                exit(1);
            }
            break;
        case 'd':
            if (qemu_strtoui(optarg, NULL, 10, &info.dpi) < 0) {
                fprintf(stderr, "not a number: %s\n", optarg);
                exit(1);
            }
            break;
        case 'v':
            info.vendor = optarg;
            break;
        case 'n':
            info.name = optarg;
            break;
        case 's':
            info.serial = optarg;
            break;
        case 'h':
            usage(stdout);
            exit(0);
        default:
            usage(stderr);
            exit(1);
        }
    }

    if (outfile == NULL) {
        outfile = stdout;
    }

    memset(blob, 0, sizeof(blob));
    qemu_edid_generate(blob, sizeof(blob), &info);
    fwrite(blob, sizeof(blob), 1, outfile);
    fflush(outfile);

    exit(0);
}
