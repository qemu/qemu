/*
 * QEMU Crypto PBKDF support (Password-Based Key Derivation Function)
 *
 * Copyright (c) 2015-2016 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "crypto/pbkdf.h"
#ifndef _WIN32
#include <sys/resource.h>
#endif
#ifdef CONFIG_DARWIN
#include <mach/mach_init.h>
#include <mach/thread_act.h>
#include <mach/mach_port.h>
#endif


static int qcrypto_pbkdf2_get_thread_cpu(unsigned long long *val_ms,
                                         Error **errp)
{
#ifdef _WIN32
    FILETIME creation_time, exit_time, kernel_time, user_time;
    ULARGE_INTEGER thread_time;

    if (!GetThreadTimes(GetCurrentThread(), &creation_time, &exit_time,
                        &kernel_time, &user_time)) {
        error_setg(errp, "Unable to get thread CPU usage");
        return -1;
    }

    thread_time.LowPart = user_time.dwLowDateTime;
    thread_time.HighPart = user_time.dwHighDateTime;

    /* QuadPart is units of 100ns and we want ms as unit */
    *val_ms = thread_time.QuadPart / 10000ll;
    return 0;
#elif defined(CONFIG_DARWIN)
    mach_port_t thread;
    kern_return_t kr;
    mach_msg_type_number_t count;
    thread_basic_info_data_t info;

    thread = mach_thread_self();
    count = THREAD_BASIC_INFO_COUNT;
    kr = thread_info(thread, THREAD_BASIC_INFO, (thread_info_t)&info, &count);
    mach_port_deallocate(mach_task_self(), thread);
    if (kr != KERN_SUCCESS || (info.flags & TH_FLAGS_IDLE) != 0) {
        error_setg_errno(errp, errno, "Unable to get thread CPU usage");
        return -1;
    }

    *val_ms = ((info.user_time.seconds * 1000ll) +
               (info.user_time.microseconds / 1000));
    return 0;
#elif defined(RUSAGE_THREAD)
    struct rusage ru;
    if (getrusage(RUSAGE_THREAD, &ru) < 0) {
        error_setg_errno(errp, errno, "Unable to get thread CPU usage");
        return -1;
    }

    *val_ms = ((ru.ru_utime.tv_sec * 1000ll) +
               (ru.ru_utime.tv_usec / 1000));
    return 0;
#else
    *val_ms = 0;
    error_setg(errp, "Unable to calculate thread CPU usage on this platform");
    return -1;
#endif
}

uint64_t qcrypto_pbkdf2_count_iters(QCryptoHashAlgorithm hash,
                                    const uint8_t *key, size_t nkey,
                                    const uint8_t *salt, size_t nsalt,
                                    size_t nout,
                                    Error **errp)
{
    uint64_t ret = -1;
    g_autofree uint8_t *out = g_new(uint8_t, nout);
    uint64_t iterations = (1 << 15);
    unsigned long long delta_ms, start_ms, end_ms;

    while (1) {
        if (qcrypto_pbkdf2_get_thread_cpu(&start_ms, errp) < 0) {
            goto cleanup;
        }
        if (qcrypto_pbkdf2(hash,
                           key, nkey,
                           salt, nsalt,
                           iterations,
                           out, nout,
                           errp) < 0) {
            goto cleanup;
        }
        if (qcrypto_pbkdf2_get_thread_cpu(&end_ms, errp) < 0) {
            goto cleanup;
        }

        delta_ms = end_ms - start_ms;

        if (delta_ms > 500) {
            break;
        } else if (delta_ms < 100) {
            iterations = iterations * 10;
        } else {
            iterations = (iterations * 1000 / delta_ms);
        }
    }

    iterations = iterations * 1000 / delta_ms;

    ret = iterations;

 cleanup:
    memset(out, 0, nout);
    return ret;
}
