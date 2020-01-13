#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "libqtest.h"

#include "libqos/libqos-spapr.h"
#include "libqos/rtas.h"

static void test_rtas_get_time_of_day(void)
{
    QOSState *qs;
    struct tm tm;
    uint32_t ns;
    uint64_t ret;
    time_t t1, t2;

    qs = qtest_spapr_boot("-machine pseries");

    t1 = time(NULL);
    ret = qrtas_get_time_of_day(qs->qts, &qs->alloc, &tm, &ns);
    g_assert_cmpint(ret, ==, 0);
    t2 = mktimegm(&tm);
    g_assert(t2 - t1 < 5); /* 5 sec max to run the test */

    qtest_shutdown(qs);
}

int main(int argc, char *argv[])
{
    const char *arch = qtest_get_arch();

    g_test_init(&argc, &argv, NULL);

    if (strcmp(arch, "ppc64")) {
        g_printerr("RTAS requires ppc64-softmmu/qemu-system-ppc64\n");
        exit(EXIT_FAILURE);
    }
    qtest_add_func("rtas/get-time-of-day", test_rtas_get_time_of_day);

    return g_test_run();
}
