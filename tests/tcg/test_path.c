/* Test path override code */
#include "../config-host.h"
#include "../qemu-malloc.c"
#include "../cutils.c"
#include "../path.c"
#include "../trace.c"
#ifdef CONFIG_TRACE_SIMPLE
#include "../simpletrace.c"
#endif

#include <stdarg.h>
#include <sys/stat.h>
#include <fcntl.h>

void qemu_log(const char *fmt, ...);

/* Any log message kills the test. */
void qemu_log(const char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "FATAL: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    exit(1);
}

#define NO_CHANGE(_path)						\
	do {								\
	    if (strcmp(path(_path), _path) != 0) return __LINE__;	\
	} while(0)

#define CHANGE_TO(_path, _newpath)					\
	do {								\
	    if (strcmp(path(_path), _newpath) != 0) return __LINE__;	\
	} while(0)

static void cleanup(void)
{
    unlink("/tmp/qemu-test_path/DIR1/DIR2/FILE");
    unlink("/tmp/qemu-test_path/DIR1/DIR2/FILE2");
    unlink("/tmp/qemu-test_path/DIR1/DIR2/FILE3");
    unlink("/tmp/qemu-test_path/DIR1/DIR2/FILE4");
    unlink("/tmp/qemu-test_path/DIR1/DIR2/FILE5");
    rmdir("/tmp/qemu-test_path/DIR1/DIR2");
    rmdir("/tmp/qemu-test_path/DIR1/DIR3");
    rmdir("/tmp/qemu-test_path/DIR1");
    rmdir("/tmp/qemu-test_path");
}

static unsigned int do_test(void)
{
    if (mkdir("/tmp/qemu-test_path", 0700) != 0)
	return __LINE__;

    if (mkdir("/tmp/qemu-test_path/DIR1", 0700) != 0)
	return __LINE__;

    if (mkdir("/tmp/qemu-test_path/DIR1/DIR2", 0700) != 0)
	return __LINE__;

    if (mkdir("/tmp/qemu-test_path/DIR1/DIR3", 0700) != 0)
	return __LINE__;

    if (close(creat("/tmp/qemu-test_path/DIR1/DIR2/FILE", 0600)) != 0)
	return __LINE__;

    if (close(creat("/tmp/qemu-test_path/DIR1/DIR2/FILE2", 0600)) != 0)
	return __LINE__;

    if (close(creat("/tmp/qemu-test_path/DIR1/DIR2/FILE3", 0600)) != 0)
	return __LINE__;

    if (close(creat("/tmp/qemu-test_path/DIR1/DIR2/FILE4", 0600)) != 0)
	return __LINE__;

    if (close(creat("/tmp/qemu-test_path/DIR1/DIR2/FILE5", 0600)) != 0)
	return __LINE__;

    init_paths("/tmp/qemu-test_path");

    NO_CHANGE("/tmp");
    NO_CHANGE("/tmp/");
    NO_CHANGE("/tmp/qemu-test_path");
    NO_CHANGE("/tmp/qemu-test_path/");
    NO_CHANGE("/tmp/qemu-test_path/D");
    NO_CHANGE("/tmp/qemu-test_path/DI");
    NO_CHANGE("/tmp/qemu-test_path/DIR");
    NO_CHANGE("/tmp/qemu-test_path/DIR1");
    NO_CHANGE("/tmp/qemu-test_path/DIR1/");

    NO_CHANGE("/D");
    NO_CHANGE("/DI");
    NO_CHANGE("/DIR");
    NO_CHANGE("/DIR2");
    NO_CHANGE("/DIR1.");

    CHANGE_TO("/DIR1", "/tmp/qemu-test_path/DIR1");
    CHANGE_TO("/DIR1/", "/tmp/qemu-test_path/DIR1");

    NO_CHANGE("/DIR1/D");
    NO_CHANGE("/DIR1/DI");
    NO_CHANGE("/DIR1/DIR");
    NO_CHANGE("/DIR1/DIR1");

    CHANGE_TO("/DIR1/DIR2", "/tmp/qemu-test_path/DIR1/DIR2");
    CHANGE_TO("/DIR1/DIR2/", "/tmp/qemu-test_path/DIR1/DIR2");

    CHANGE_TO("/DIR1/DIR3", "/tmp/qemu-test_path/DIR1/DIR3");
    CHANGE_TO("/DIR1/DIR3/", "/tmp/qemu-test_path/DIR1/DIR3");

    NO_CHANGE("/DIR1/DIR2/F");
    NO_CHANGE("/DIR1/DIR2/FI");
    NO_CHANGE("/DIR1/DIR2/FIL");
    NO_CHANGE("/DIR1/DIR2/FIL.");

    CHANGE_TO("/DIR1/DIR2/FILE", "/tmp/qemu-test_path/DIR1/DIR2/FILE");
    CHANGE_TO("/DIR1/DIR2/FILE2", "/tmp/qemu-test_path/DIR1/DIR2/FILE2");
    CHANGE_TO("/DIR1/DIR2/FILE3", "/tmp/qemu-test_path/DIR1/DIR2/FILE3");
    CHANGE_TO("/DIR1/DIR2/FILE4", "/tmp/qemu-test_path/DIR1/DIR2/FILE4");
    CHANGE_TO("/DIR1/DIR2/FILE5", "/tmp/qemu-test_path/DIR1/DIR2/FILE5");

    NO_CHANGE("/DIR1/DIR2/FILE6");
    NO_CHANGE("/DIR1/DIR2/FILE/X");

    CHANGE_TO("/DIR1/../DIR1", "/tmp/qemu-test_path/DIR1");
    CHANGE_TO("/DIR1/../DIR1/", "/tmp/qemu-test_path/DIR1");
    CHANGE_TO("/../DIR1", "/tmp/qemu-test_path/DIR1");
    CHANGE_TO("/../DIR1/", "/tmp/qemu-test_path/DIR1");
    CHANGE_TO("/DIR1/DIR2/../DIR2", "/tmp/qemu-test_path/DIR1/DIR2");
    CHANGE_TO("/DIR1/DIR2/../DIR2/../../DIR1/DIR2/FILE", "/tmp/qemu-test_path/DIR1/DIR2/FILE");
    CHANGE_TO("/DIR1/DIR2/../DIR2/FILE", "/tmp/qemu-test_path/DIR1/DIR2/FILE");

    NO_CHANGE("/DIR1/DIR2/../DIR1");
    NO_CHANGE("/DIR1/DIR2/../FILE");

    CHANGE_TO("/./DIR1/DIR2/FILE", "/tmp/qemu-test_path/DIR1/DIR2/FILE");
    CHANGE_TO("/././DIR1/DIR2/FILE", "/tmp/qemu-test_path/DIR1/DIR2/FILE");
    CHANGE_TO("/DIR1/./DIR2/FILE", "/tmp/qemu-test_path/DIR1/DIR2/FILE");
    CHANGE_TO("/DIR1/././DIR2/FILE", "/tmp/qemu-test_path/DIR1/DIR2/FILE");
    CHANGE_TO("/DIR1/DIR2/./FILE", "/tmp/qemu-test_path/DIR1/DIR2/FILE");
    CHANGE_TO("/DIR1/DIR2/././FILE", "/tmp/qemu-test_path/DIR1/DIR2/FILE");
    CHANGE_TO("/./DIR1/./DIR2/./FILE", "/tmp/qemu-test_path/DIR1/DIR2/FILE");

    return 0;
}

int main(int argc, char *argv[])
{
    int ret;

    ret = do_test();
    cleanup();
    if (ret) {
	fprintf(stderr, "test_path: failed on line %i\n", ret);
	return 1;
    }
    return 0;
}
