/*
 * Test that shmat() does not break /proc/self/maps.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>

int main(void)
{
    char buf[128];
    int err, fd;
    int shmid;
    ssize_t n;
    void *p;

    shmid = shmget(IPC_PRIVATE, 1, IPC_CREAT | 0600);
    assert(shmid != -1);

    /*
     * The original bug required a non-NULL address, which skipped the
     * mmap_find_vma step, which could result in a host mapping smaller
     * than the target mapping.  Choose an address at random.
     */
    p = shmat(shmid, (void *)0x800000, SHM_RND);
    if (p == (void *)-1) {
        /*
         * Because we are now running the testcase for all guests for which
         * we have a cross-compiler, the above random address might conflict
         * with the guest executable in some way.  Rather than stopping,
         * continue with a system supplied address, which should never fail.
         */
        p = shmat(shmid, NULL, 0);
        assert(p != (void *)-1);
    }

    fd = open("/proc/self/maps", O_RDONLY);
    assert(fd != -1);
    do {
        n = read(fd, buf, sizeof(buf));
        assert(n >= 0);
    } while (n != 0);
    close(fd);

    err = shmdt(p);
    assert(err == 0);
    err = shmctl(shmid, IPC_RMID, NULL);
    assert(err == 0);

    return EXIT_SUCCESS;
}
