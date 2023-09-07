#include <sys/mman.h>
#include <sys/shm.h>
#include <unistd.h>
#include <stdio.h>

int main()
{
    int psize = getpagesize();
    int id;
    void *p;

    /*
     * We need a shared mapping to enter CF_PARALLEL mode.
     * The easiest way to get that is shmat.
     */
    id = shmget(IPC_PRIVATE, 2 * psize, IPC_CREAT | 0600);
    if (id < 0) {
        perror("shmget");
        return 2;
    }
    p = shmat(id, NULL, 0);
    if (p == MAP_FAILED) {
        perror("shmat");
        return 2;
    }

    /* Protect the second page. */
    if (mprotect(p + psize, psize, PROT_NONE) < 0) {
        perror("mprotect");
        return 2;
    }

    /*
     * Load 4 bytes, 6 bytes from the end of the page.
     * On success this will load 0 from the newly allocated shm.
     */
    return *(int *)(p + psize - 6);
}
