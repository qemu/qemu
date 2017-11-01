#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "libqtest.h"

#include "libqos/libqos-spapr.h"
#include "libqos/rtas.h"

#define EVENT_MASK_EPOW (1 << 30)
#define EVENT_LOG_LEN 2048

#define RTAS_SENSOR_TYPE_ISOLATION_STATE 9001
#define RTAS_SENSOR_TYPE_ALLOCATION_STATE 9003
#define SPAPR_DR_ISOLATION_STATE_ISOLATED 0
#define SPAPR_DR_ALLOCATION_STATE_UNUSABLE 0
#define SPAPR_DR_ALLOCATION_STATE_USABLE  1
#define SPAPR_DR_ISOLATION_STATE_UNISOLATED 1

#define CC_WA_LEN 4096

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

/*
 * To test the 'set-indicator' RTAS call we will hotplug a device
 * (in this case a CPU) and then make its DRC state go from
 * the starting state UNUSABLE(1) to UNISOLATE(3). These DRC
 * states transitions are described in further detail in
 * PAPR 2.7+ 13.4.
 */
static void test_rtas_set_indicator(void)
{
    QOSState *qs;
    uint64_t ret;
    uintptr_t guest_buf_addr;
    uint32_t drc_index;
    uint8_t *buf = g_malloc0(EVENT_LOG_LEN);

    qs = qtest_spapr_boot("-machine pseries -cpu POWER8_v2.0 "
                          "-smp 1,sockets=4,cores=1,threads=1,maxcpus=4");

    guest_buf_addr = guest_alloc(qs->alloc, EVENT_LOG_LEN * sizeof(uint8_t));
    qtest_qmp_device_add("power8_v2.0-spapr-cpu-core", "id-1",
                         "'core-id':'1'");

    ret = qrtas_check_exception(qs->alloc, EVENT_MASK_EPOW,
                                guest_buf_addr, EVENT_LOG_LEN);

    memread(guest_buf_addr, buf, EVENT_LOG_LEN);
    guest_free(qs->alloc, guest_buf_addr);

    g_assert_cmpint(ret, ==, 0);

    /*
     * This time we can't ignore the event log written in the
     * check_exception call - we need the DRC index of the
     * recently added CPU to make the state changes using set_indicator.
     *
     * A bit of magic to go straight to the DRC index by checking the
     * error log format in hw/ppc/spapr_events.c:
     *
     * - rtas_error_log size = 8 bytes
     * - all other structures until the hotplug event log = 88 bytes
     * - inside the hotplug event log, skip 8 + 4 bytes to get to
     *   the drc_id union.
     *
     * This gives us a 108 bytes skip to get the drc info, which is
     * written in be32.
     */
    drc_index = be32toh(*((uint32_t *)(buf + 108)));
    g_free(buf);

    /*
     * According to the DRC state diagram, the guest first sets a device
     * to USABLE (2), then UNISOLATED (3). Both should return
     * RTAS_OUT_SUCCESS(0).
     */
    ret = qrtas_set_indicator(qs->alloc, RTAS_SENSOR_TYPE_ALLOCATION_STATE,
                              drc_index, SPAPR_DR_ALLOCATION_STATE_USABLE);
    g_assert_cmpint(ret, ==, 0);

    ret = qrtas_set_indicator(qs->alloc, RTAS_SENSOR_TYPE_ISOLATION_STATE,
                              drc_index, SPAPR_DR_ISOLATION_STATE_UNISOLATED);
    g_assert_cmpint(ret, ==, 0);

    qtest_shutdown(qs);
}

static void test_rtas_ibm_configure_connector(void)
{
    QOSState *qs;
    uint64_t ret;
    uintptr_t guest_buf_addr, guest_drc_addr;
    uint32_t drc_index;
    uint8_t *buf = g_malloc0(EVENT_LOG_LEN);

    qs = qtest_spapr_boot("-machine pseries -cpu POWER8_v2.0 "
                          "-smp 1,sockets=4,cores=1,threads=1,maxcpus=4");

    guest_buf_addr = guest_alloc(qs->alloc, EVENT_LOG_LEN * sizeof(uint8_t));
    qtest_qmp_device_add("power8_v2.0-spapr-cpu-core", "id-1",
                         "'core-id':'1'");

    ret = qrtas_check_exception(qs->alloc, EVENT_MASK_EPOW,
                                guest_buf_addr, EVENT_LOG_LEN);

    memread(guest_buf_addr, buf, EVENT_LOG_LEN);
    guest_free(qs->alloc, guest_buf_addr);

    g_assert_cmpint(ret, ==, 0);

    /*
     * Same 108 bytes offset magic used and explained in
     * test_rtas_set_indicator.
     */
    drc_index = be32toh(*((uint32_t *)(buf + 108)));
    g_free(buf);

    ret = qrtas_set_indicator(qs->alloc, RTAS_SENSOR_TYPE_ALLOCATION_STATE,
                              drc_index, SPAPR_DR_ALLOCATION_STATE_USABLE);
    g_assert_cmpint(ret, ==, 0);

    ret = qrtas_set_indicator(qs->alloc, RTAS_SENSOR_TYPE_ISOLATION_STATE,
                              drc_index, SPAPR_DR_ISOLATION_STATE_UNISOLATED);
    g_assert_cmpint(ret, ==, 0);

    /*
     * Call ibm,configure-connector to finish the hotplugged device
     * configuration, putting its DRC into 'ready' state.
     *
     * We're not interested in the generated FDTs during the config
     * process, thus we simply keep calling configure-connector
     * until it returns SUCCESS(0) or an error.
     *
     * The full explanation logic behind this process can be found
     * at PAPR 2.7+, 13.5.3.5.
     */
    guest_drc_addr = guest_alloc(qs->alloc, CC_WA_LEN * sizeof(uint32_t));
    writel(guest_drc_addr, drc_index);
    writel(guest_drc_addr + sizeof(uint32_t), 0);

    do {
        ret = qrtas_ibm_configure_connector(qs->alloc, guest_drc_addr);
    } while (ret > 0);

    guest_free(qs->alloc, guest_drc_addr);

    g_assert_cmpint(ret, ==, 0);

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
    qtest_add_func("rtas/test_rtas_set_indicator", test_rtas_set_indicator);
    qtest_add_func("rtas/test_rtas_ibm_configure_connector",
                   test_rtas_ibm_configure_connector);

    return g_test_run();
}
