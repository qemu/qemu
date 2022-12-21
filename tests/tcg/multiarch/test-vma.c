/*
 * Test very large vma allocations.
 * The qemu out-of-memory condition was within the mmap syscall itself.
 * If the syscall actually returns with MAP_FAILED, the test succeeded.
 */
#include <sys/mman.h>

int main()
{
    int n = sizeof(size_t) == 4 ? 32 : 45;

    for (int i = 28; i < n; i++) {
        size_t l = (size_t)1 << i;
        void *p = mmap(0, l, PROT_NONE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (p == MAP_FAILED) {
            break;
        }
        munmap(p, l);
    }
    return 0;
}
