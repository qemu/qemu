/*
 *  Copyright(c) 2023-2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include "strutils.h"

/* Defines in order of testing */

/* env/CLI-related */
#define HEX_SYS_GET_CMDLINE     0x15
#define HEX_SYS_GETCWD          0x104

/* File manipulation */
#define HEX_SYS_TMPNAM          0x0d
#define HEX_SYS_OPEN            0x01
#define HEX_SYS_ACCESS          0x105
#define HEX_SYS_ISTTY           0x09
#define HEX_SYS_WRITE           0x05
#define HEX_SYS_SEEK            0x0a
#define HEX_SYS_READ            0x06
#define HEX_SYS_FTELL           0x100
#define HEX_SYS_FSTAT           0x101
#define HEX_SYS_FTRUNC          0x186
#define HEX_SYS_FLEN            0x0c
#define HEX_SYS_CLOSE           0x02
#define HEX_SYS_ERRNO           0x13
#define HEX_SYS_RENAME          0x0f
#define HEX_SYS_STAT            0x103
#define HEX_SYS_REMOVE          0x0e

/* Time */
#define HEX_SYS_CLOCK           0x10
#define HEX_SYS_TIME            0x11

/* dirent */
#define HEX_SYS_OPENDIR         0x180
#define HEX_SYS_CLOSEDIR        0x181
#define HEX_SYS_READDIR         0x182

/* STDOUT */
#define HEX_SYS_WRITEC          0x03
#define HEX_SYS_WRITE0          0x04
#define HEX_SYS_WRITECREG       0x43

static uint32_t ret, err, args[4];

/*
 * Macro flavors:
 * - DIRECT_SWI takes up to two args an put them at r1 and r2.
 * - SWI takes up to four args and puts them in an array, placing the
 *   array address at r1.
 */

#define DO_SWI(CODE, ARG0, ARG1) \
    do { \
        asm volatile( \
                "r0 = %2\n" \
                "r1 = %3\n" \
                "r2 = %4\n" \
                "trap0(#0)\n" \
                "%0 = r0\n" \
                "%1 = r1\n" \
                : "=r"(ret), "=r"(err) \
                : "r"(CODE), "r"(ARG0), "r"(ARG1) \
                : "r0", "r1", "r2", "memory" \
                ); \
    } while (0)

#define SWI0(CODE) DO_SWI(CODE, args, 0)
#define SWI1(CODE, ARG0) \
    do { args[0] = (uint32_t)(ARG0); SWI0(CODE); } while (0)
#define SWI2(CODE, ARG0, ARG1) \
    do { args[1] = (uint32_t)(ARG1); SWI1(CODE, ARG0); } while (0)
#define SWI3(CODE, ARG0, ARG1, ARG2) \
    do { args[2] = (uint32_t)(ARG2); SWI2(CODE, ARG0, ARG1); } while (0)
#define SWI4(CODE, ARG0, ARG1, ARG2, ARG3) \
    do { args[3] = (uint32_t)(ARG3); SWI3(CODE, ARG0, ARG1, ARG2); } while (0)

#define GET_MACRO_5(_1, _2, _3, _4, _5, NAME, ...) NAME
#define SWI(...) \
    GET_MACRO_5(__VA_ARGS__, SWI4, SWI3, SWI2, SWI1, SWI0)(__VA_ARGS__)

#define DIRECT_SWI0(CODE) DO_SWI(CODE, 0, 0)
#define DIRECT_SWI1(CODE, ARG1) DO_SWI(CODE, ARG1, 0)
#define DIRECT_SWI2(CODE, ARG1, ARG2) DO_SWI(CODE, ARG1, ARG2)

#define GET_MACRO_3(_1, _2, _3, NAME, ...) NAME
#define DIRECT_SWI(...) \
    GET_MACRO_3(__VA_ARGS__, DIRECT_SWI2, DIRECT_SWI1, DIRECT_SWI0)(__VA_ARGS__)

#define is_path_sep(C) ((C) == '/' || (C) == '\\')

static int path_ends_with(const char *str, const char *suffix)
{
    const char *str_cursor = str + strlen(str) - 1;
    const char *suffix_cursor = suffix + strlen(suffix) - 1;
    while (str_cursor >= str && suffix_cursor >= suffix) {
        /* is_path_sep handles the semihosting-on-Windows case */
        if (*str_cursor != *suffix_cursor &&
            !(is_path_sep(*str_cursor) && is_path_sep(*suffix_cursor))) {
            return 0;
        }
        str_cursor--;
        suffix_cursor--;
    }
    return 1;
}

/*
 * This must match the caller's definition, it would be in the
 * caller's angel.h or equivalent header.
 */
struct __SYS_STAT {
    uint64_t dev;
    uint64_t ino;
    uint32_t mode;
    uint32_t nlink;
    uint64_t rdev;
    uint32_t size;
    uint32_t __pad1;
    uint32_t atime;
    uint32_t mtime;
    uint32_t ctime;
    uint32_t __pad2;
};

int main(int argc, char **argv)
{
    /* GET_CMDLINE */
    char argv_concat[1024];
    char *cursor = argv_concat;
    for (int i = 0; i < argc; i++) {
        strcpy(cursor, argv[i]);
        cursor += strlen(argv[i]);
        *cursor = ' ';
        cursor++;
    }
    *(cursor - 1) = '\0';
    char buf[4096];
    SWI(HEX_SYS_GET_CMDLINE, buf, sizeof(buf));
    assert(!ret && !strcmp(buf, argv_concat));

    /* GETCWD */
    const char *expected_cwd = "tests/tcg/hexagon-softmmu";
    SWI(HEX_SYS_GETCWD, buf, sizeof(buf));
    assert(ret && path_ends_with(buf, expected_cwd));

    /* TMPNAM */
    char fname[4096];
    SWI(HEX_SYS_TMPNAM, fname, 0, sizeof(fname));
    assert(!ret);

    /* OPEN */
    /* 13 is O_RDWR | O_CREAT | O_EXCL */
    SWI(HEX_SYS_OPEN, fname, 13, strlen(fname));
    int fd = (int)ret;
    assert(fd >= 0);

    /* ACCESS */
    SWI(HEX_SYS_ACCESS, fname, R_OK);
    assert(!ret);
    /* ACCESS with error */
    SWI(HEX_SYS_ACCESS, "non-existent-semihost-file", R_OK);
    assert(ret);
    assert(err == ENOENT);

    /* ISTTY */
    SWI(HEX_SYS_ISTTY, fd);
    assert(!ret);

    /* WRITE */
    char *str = "hello";
    SWI(HEX_SYS_WRITE, fd, str, strlen(str));
    assert(!ret);

    /* SEEK */
    SWI(HEX_SYS_SEEK, fd, 0);
    assert(!ret);

    /* READ */
    int n = strlen(str);
    SWI(HEX_SYS_READ, fd, buf, n);
    buf[n] = '\0';
    assert(!ret && !strcmp(str, buf));

    /* FTELL */
    SWI(HEX_SYS_FTELL, fd);
    assert(ret == strlen(str));

    /* FSTAT */
    struct __SYS_STAT st;
    SWI(HEX_SYS_FSTAT, fd, &st);
    assert(!ret);
    assert(st.atime && st.ctime && st.mtime);
    assert(st.size == strlen(str));
    assert((st.mode & S_IFMT) == S_IFREG);

    /* FTRUNC */
    SWI(HEX_SYS_FTRUNC, fd, 1, 0);
    assert(!ret);

    /* FLEN */
    SWI(HEX_SYS_FLEN, fd);
    assert(ret == 1);

    /* CLOSE */
    SWI(HEX_SYS_CLOSE, fd);
    assert(!ret);

    /* CLOSE w/ error && ERRNO */
    SWI(HEX_SYS_CLOSE, fd);
    assert(ret);
    assert(err == EBADF);
    SWI(HEX_SYS_ERRNO);
    assert(ret == EBADF);

    /* RENAME */
    char ogfname[4096];
    int len = strlen(fname);
    strcpy(ogfname, fname);
    fname[len - 1] = (fname[len - 1] == 'a' ? 'b' : 'a');
    SWI(HEX_SYS_RENAME, ogfname, len, fname, len);
    assert(!ret);

    /* STAT */
    SWI(HEX_SYS_STAT, fname, &st);
    assert(!ret);
    assert(st.atime && st.ctime && st.mtime);
    assert(st.size == 1);
    assert((st.mode & S_IFMT) == S_IFREG);

    /* REMOVE */
    SWI(HEX_SYS_REMOVE, fname, strlen(fname));
    assert(!ret);

    /* STAT w/ error */
    SWI(HEX_SYS_STAT, fname, &st);
    assert(ret);
    assert(err == ENOENT);

    /* TIME && CLOCK */
    SWI(HEX_SYS_TIME);
    assert(ret);
    SWI(HEX_SYS_CLOCK);
    assert(ret);

    /* OPENDIR */
    char *dname = "./_semihost_dir";
    DIRECT_SWI(HEX_SYS_OPENDIR, dname);
    assert(ret);
    int dir_index = ret;

    /* READDIR */
    char *expected_files[4] = { ".", "..", "fileA", "fileB" };
    char found_files_buffer[4][256];
    char *found_files[4];
    for (int i = 0; 1; i++) {
        struct __attribute__((__packed__)) { int32_t _; char d_name[256]; } dirent;
        DIRECT_SWI(HEX_SYS_READDIR, dir_index, &dirent);
        if (!ret) {
            break;
        }
        assert(i < 4);
        found_files[i] = found_files_buffer[i];
        strcpy(found_files[i], dirent.d_name);
    }

    sort_str_arr(found_files, 4);
    for (int i = 0; i < 4; i++) {
        assert(!strcmp(found_files[i], expected_files[i]));
    }

    /* CLOSEDIR */
    DIRECT_SWI(HEX_SYS_CLOSEDIR, dir_index);
    assert(!ret);

    /* WRITEC, WRITECREG, WRITE0 */
    /* We use DO_SWI directly here to bypass the args array */
    char *pass = "PASS\n";
    DIRECT_SWI(HEX_SYS_WRITEC, &pass[0]);
    DIRECT_SWI(HEX_SYS_WRITECREG, pass[1]);
    DIRECT_SWI(HEX_SYS_WRITE0, &pass[2]);

    return 0;
}
