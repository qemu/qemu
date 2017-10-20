#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "libqtest.h"

#include "libqos/libqos-spapr.h"
#include "libqos/rtas.h"

#define EVENT_MASK_EPOW (1 << 30)
#define EVENT_LOG_LEN 2048

static void test_rtas_get_time_of_day(void)
{
    QOSState *qs;
    struct tm tm;
    uint32_t ns;
    uint64_t ret;
    time_t t1, t2;

    qs = qtest_spapr_boot("-machine pseries");

    t1 = time(NULL);
    ret = qrtas_get_time_of_day(qs->alloc, &tm, &ns);
    g_assert_cmpint(ret, ==, 0);
    t2 = mktimegm(&tm);
    g_assert(t2 - t1 < 5); /* 5 sec max to run the test */

    qtest_shutdown(qs);
}

static void test_rtas_check_exception_no_events(void)
{
    QOSState *qs;
    uint64_t ret;
    uintptr_t guest_buf_addr;
    uint8_t *buf = g_malloc0(EVENT_LOG_LEN);

    qs = qtest_spapr_boot("-machine pseries");
    guest_buf_addr = guest_alloc(qs->alloc, EVENT_LOG_LEN * sizeof(uint8_t));

    /*
     * mask = 0 should return no events, returning
     * RTAS_OUT_NO_ERRORS_FOUND (1).
     */
    ret = qrtas_check_exception(qs->alloc, 0, guest_buf_addr, EVENT_LOG_LEN);
    g_assert_cmpint(ret, ==, 1);

    /*
     * Using a proper event mask should also return
     * no events since no hotplugs happened.
     */
    ret = qrtas_check_exception(qs->alloc, EVENT_MASK_EPOW, guest_buf_addr,
                                EVENT_LOG_LEN);
    g_assert_cmpint(ret, ==, 1);

    guest_free(qs->alloc, guest_buf_addr);
    g_free(buf);

    qtest_shutdown(qs);
}

static void test_rtas_check_exception_hotplug_event(void)
{
    QOSState *qs;
    uint64_t ret;
    uintptr_t guest_buf_addr;
    uint8_t *buf = g_malloc0(EVENT_LOG_LEN);
    uint8_t *zero_buf = g_malloc0(EVENT_LOG_LEN);

    qs = qtest_spapr_boot("-machine pseries -cpu POWER8_v2.0 "
                          "-smp 1,sockets=4,cores=1,threads=1,maxcpus=4");

    guest_buf_addr = guest_alloc(qs->alloc, EVENT_LOG_LEN * sizeof(uint8_t));

    qtest_qmp_device_add("power8_v2.0-spapr-cpu-core", "id-1",
                         "'core-id':'1'");
    /*
     * We use EPOW mask instead of HOTPLUG because the code defaults
     * the hotplug interrupt source to EPOW if the guest didn't change
     * OV5_HP_EVT during CAS.
     */
    ret = qrtas_check_exception(qs->alloc, EVENT_MASK_EPOW,
                                guest_buf_addr, EVENT_LOG_LEN);

    memread(guest_buf_addr, buf, EVENT_LOG_LEN);
    guest_free(qs->alloc, guest_buf_addr);

    /*
     * Calling check_exception after a hotplug needs to return
     * RTAS_OUT_SUCCESS (0) and a non-zero error_log.
     */
    g_assert_cmpint(ret, ==, 0);
    g_assert(memcmp(buf, zero_buf, EVENT_LOG_LEN) != 0);

    g_free(buf);
    g_free(zero_buf);

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
    qtest_add_func("rtas/rtas-check-exception-no-events",
                   test_rtas_check_exception_no_events);
    qtest_add_func("rtas/rtas-check-exception-hotplug-event",
                   test_rtas_check_exception_hotplug_event);

    return g_test_run();
}
