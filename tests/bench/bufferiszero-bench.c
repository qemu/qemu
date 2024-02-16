/*
 * QEMU buffer_is_zero speed benchmark
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/units.h"

static void test(const void *opaque)
{
    size_t max = 64 * KiB;
    void *buf = g_malloc0(max);
    int accel_index = 0;

    do {
        if (accel_index != 0) {
            g_test_message("%s", "");  /* gnu_printf Werror for simple "" */
        }
        for (size_t len = 1 * KiB; len <= max; len *= 4) {
            double total = 0.0;

            g_test_timer_start();
            do {
                buffer_is_zero_ge256(buf, len);
                total += len;
            } while (g_test_timer_elapsed() < 0.5);

            total /= MiB;
            g_test_message("buffer_is_zero #%d: %2zuKB %8.0f MB/sec",
                           accel_index, len / (size_t)KiB,
                           total / g_test_timer_last());
        }
        accel_index++;
    } while (test_buffer_is_zero_next_accel());

    g_free(buf);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_data_func("/cutils/bufferiszero/speed", NULL, test);
    return g_test_run();
}
