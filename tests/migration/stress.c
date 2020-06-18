/*
 * Migration stress workload
 *
 * Copyright (c) 2016 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include <getopt.h>
#include <sys/reboot.h>
#include <sys/syscall.h>
#include <linux/random.h>
#include <pthread.h>
#include <sys/mount.h>

const char *argv0;

#define PAGE_SIZE 4096

static int gettid(void)
{
    return syscall(SYS_gettid);
}

static __attribute__((noreturn)) void exit_failure(void)
{
    if (getpid() == 1) {
        sync();
        reboot(RB_POWER_OFF);
        fprintf(stderr, "%s (%05d): ERROR: cannot reboot: %s\n",
                argv0, gettid(), strerror(errno));
        abort();
    } else {
        exit(1);
    }
}

static __attribute__((noreturn)) void exit_success(void)
{
    if (getpid() == 1) {
        sync();
        reboot(RB_POWER_OFF);
        fprintf(stderr, "%s (%05d): ERROR: cannot reboot: %s\n",
                argv0, gettid(), strerror(errno));
        abort();
    } else {
        exit(0);
    }
}

static int get_command_arg_str(const char *name,
                               char **val)
{
    static char line[1024];
    FILE *fp = fopen("/proc/cmdline", "r");
    char *start, *end;

    if (fp == NULL) {
        fprintf(stderr, "%s (%05d): ERROR: cannot open /proc/cmdline: %s\n",
                argv0, gettid(), strerror(errno));
        return -1;
    }

    if (!fgets(line, sizeof line, fp)) {
        fprintf(stderr, "%s (%05d): ERROR: cannot read /proc/cmdline: %s\n",
                argv0, gettid(), strerror(errno));
        fclose(fp);
        return -1;
    }
    fclose(fp);

    start = strstr(line, name);
    if (!start)
        return 0;

    start += strlen(name);

    if (*start != '=') {
        fprintf(stderr, "%s (%05d): ERROR: no value provided for '%s' in /proc/cmdline\n",
                argv0, gettid(), name);
    }
    start++;

    end = strstr(start, " ");
    if (!end)
        end = strstr(start, "\n");

    if (end == start) {
        fprintf(stderr, "%s (%05d): ERROR: no value provided for '%s' in /proc/cmdline\n",
                argv0, gettid(), name);
        return -1;
    }

    if (end)
        *val = g_strndup(start, end - start);
    else
        *val = g_strdup(start);
    return 1;
}


static int get_command_arg_ull(const char *name,
                               unsigned long long *val)
{
    char *valstr;
    char *end;

    int ret = get_command_arg_str(name, &valstr);
    if (ret <= 0)
        return ret;

    errno = 0;
    *val = strtoll(valstr, &end, 10);
    if (errno || *end) {
        fprintf(stderr, "%s (%05d): ERROR: cannot parse %s value %s\n",
                argv0, gettid(), name, valstr);
        g_free(valstr);
        return -1;
    }
    g_free(valstr);
    return 0;
}


static int random_bytes(char *buf, size_t len)
{
    int fd;

    fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "%s (%05d): ERROR: cannot open /dev/urandom: %s\n",
                argv0, gettid(), strerror(errno));
        return -1;
    }

    if (read(fd, buf, len) != len) {
        fprintf(stderr, "%s (%05d): ERROR: cannot read /dev/urandom: %s\n",
                argv0, gettid(), strerror(errno));
        close(fd);
        return -1;
    }

    close(fd);

    return 0;
}


static unsigned long long now(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);

    return (tv.tv_sec * 1000ull) + (tv.tv_usec / 1000ull);
}

static void stressone(unsigned long long ramsizeMB)
{
    size_t pagesPerMB = 1024 * 1024 / PAGE_SIZE;
    g_autofree char *ram = g_malloc(ramsizeMB * 1024 * 1024);
    char *ramptr;
    size_t i, j, k;
    g_autofree char *data = g_malloc(PAGE_SIZE);
    char *dataptr;
    size_t nMB = 0;
    unsigned long long before, after;

    /* We don't care about initial state, but we do want
     * to fault it all into RAM, otherwise the first iter
     * of the loop below will be quite slow. We can't use
     * 0x0 as the byte as gcc optimizes that away into a
     * calloc instead :-) */
    memset(ram, 0xfe, ramsizeMB * 1024 * 1024);

    if (random_bytes(data, PAGE_SIZE) < 0) {
        return;
    }

    before = now();

    while (1) {

        ramptr = ram;
        for (i = 0; i < ramsizeMB; i++, nMB++) {
            for (j = 0; j < pagesPerMB; j++) {
                dataptr = data;
                for (k = 0; k < PAGE_SIZE; k += sizeof(long long)) {
                    ramptr += sizeof(long long);
                    dataptr += sizeof(long long);
                    *(unsigned long long *)ramptr ^= *(unsigned long long *)dataptr;
                }
            }

            if (nMB == 1024) {
                after = now();
                fprintf(stderr, "%s (%05d): INFO: %06llums copied 1 GB in %05llums\n",
                        argv0, gettid(), after, after - before);
                before = now();
                nMB = 0;
            }
        }
    }
}


static void *stressthread(void *arg)
{
    unsigned long long ramsizeMB = *(unsigned long long *)arg;

    stressone(ramsizeMB);

    return NULL;
}

static void stress(unsigned long long ramsizeGB, int ncpus)
{
    size_t i;
    unsigned long long ramsizeMB = ramsizeGB * 1024 / ncpus;
    ncpus--;

    for (i = 0; i < ncpus; i++) {
        pthread_t thr;
        pthread_create(&thr, NULL,
                       stressthread,   &ramsizeMB);
    }

    stressone(ramsizeMB);
}


static int mount_misc(const char *fstype, const char *dir)
{
    if (mkdir(dir, 0755) < 0 && errno != EEXIST) {
        fprintf(stderr, "%s (%05d): ERROR: cannot create %s: %s\n",
                argv0, gettid(), dir, strerror(errno));
        return -1;
    }

    if (mount("none", dir, fstype, 0, NULL) < 0) {
        fprintf(stderr, "%s (%05d): ERROR: cannot mount %s: %s\n",
                argv0, gettid(), dir, strerror(errno));
        return -1;
    }

    return 0;
}

static int mount_all(void)
{
    if (mount_misc("proc", "/proc") < 0 ||
        mount_misc("sysfs", "/sys") < 0 ||
        mount_misc("tmpfs", "/dev") < 0)
        return -1;

    mknod("/dev/urandom", 0777 | S_IFCHR, makedev(1, 9));
    mknod("/dev/random", 0777 | S_IFCHR, makedev(1, 8));

    return 0;
}

int main(int argc, char **argv)
{
    unsigned long long ramsizeGB = 1;
    char *end;
    int ch;
    int opt_ind = 0;
    const char *sopt = "hr:c:";
    struct option lopt[] = {
        { "help", no_argument, NULL, 'h' },
        { "ramsize", required_argument, NULL, 'r' },
        { "cpus", required_argument, NULL, 'c' },
        { NULL, 0, NULL, 0 }
    };
    int ret;
    int ncpus = 0;

    argv0 = argv[0];

    while ((ch = getopt_long(argc, argv, sopt, lopt, &opt_ind)) != -1) {
        switch (ch) {
        case 'r':
            errno = 0;
            ramsizeGB = strtoll(optarg, &end, 10);
            if (errno != 0 || *end) {
                fprintf(stderr, "%s (%05d): ERROR: Cannot parse RAM size %s\n",
                        argv0, gettid(), optarg);
                exit_failure();
            }
            break;

        case 'c':
            errno = 0;
            ncpus = strtoll(optarg, &end, 10);
            if (errno != 0 || *end) {
                fprintf(stderr, "%s (%05d): ERROR: Cannot parse CPU count %s\n",
                        argv0, gettid(), optarg);
                exit_failure();
            }
            break;

        case '?':
        case 'h':
            fprintf(stderr, "%s: [--help][--ramsize GB][--cpus N]\n", argv0);
            exit_failure();
        }
    }

    if (getpid() == 1) {
        if (mount_all() < 0)
            exit_failure();

        ret = get_command_arg_ull("ramsize", &ramsizeGB);
        if (ret < 0)
            exit_failure();
    }

    if (ncpus == 0)
        ncpus = sysconf(_SC_NPROCESSORS_ONLN);

    fprintf(stdout, "%s (%05d): INFO: RAM %llu GiB across %d CPUs\n",
            argv0, gettid(), ramsizeGB, ncpus);

    stress(ramsizeGB, ncpus);

    exit_failure();
}
