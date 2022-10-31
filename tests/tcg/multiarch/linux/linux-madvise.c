#include <assert.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

static void test_anonymous(void)
{
    int pagesize = getpagesize();
    char *page;
    int ret;

    page = mmap(NULL, pagesize, PROT_READ, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    assert(page != MAP_FAILED);

    /* Check that mprotect() does not interfere with MADV_DONTNEED. */
    ret = mprotect(page, pagesize, PROT_READ | PROT_WRITE);
    assert(ret == 0);

    /* Check that MADV_DONTNEED clears the page. */
    *page = 42;
    ret = madvise(page, pagesize, MADV_DONTNEED);
    assert(ret == 0);
    assert(*page == 0);

    ret = munmap(page, pagesize);
    assert(ret == 0);
}

static void test_file(void)
{
    char tempname[] = "/tmp/.cmadviseXXXXXX";
    int pagesize = getpagesize();
    ssize_t written;
    char c = 42;
    char *page;
    int ret;
    int fd;

    fd = mkstemp(tempname);
    assert(fd != -1);
    ret = unlink(tempname);
    assert(ret == 0);
    written = write(fd, &c, sizeof(c));
    assert(written == sizeof(c));
    page = mmap(NULL, pagesize, PROT_READ, MAP_PRIVATE, fd, 0);
    assert(page != MAP_FAILED);

    /* Check that mprotect() does not interfere with MADV_DONTNEED. */
    ret = mprotect(page, pagesize, PROT_READ | PROT_WRITE);
    assert(ret == 0);

    /* Check that MADV_DONTNEED resets the page. */
    *page = 0;
    ret = madvise(page, pagesize, MADV_DONTNEED);
    assert(ret == 0);
    assert(*page == c);

    ret = munmap(page, pagesize);
    assert(ret == 0);
    ret = close(fd);
    assert(ret == 0);
}

int main(void)
{
    test_anonymous();
    test_file();

    return EXIT_SUCCESS;
}
