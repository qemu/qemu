/*
 * Minimal TPM emulator for TPM test cases
 *
 * Copyright (c) 2018 Red Hat, Inc.
 *
 * Authors:
 *   Marc-André Lureau <marcandre.lureau@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef TESTS_TPM_EMU_H
#define TESTS_TPM_EMU_H

#define TPM_RC_FAILURE 0x101
#define TPM2_ST_NO_SESSIONS 0x8001

#define TPM_FAIL 9
#define TPM_TAG_RSP_COMMAND 0xc4

#include "qemu/sockets.h"
#include "io/channel.h"
#include "system/tpm.h"
#include "libqtest.h"

struct tpm_hdr {
    uint16_t tag;
    uint32_t len;
    uint32_t code; /*ordinal/error */
    char buffer[];
} QEMU_PACKED;

#ifndef CONFIG_TPM
enum TPMVersion {
    TPM_VERSION_1_2 = 1,
    TPM_VERSION_2_0 = 2,
};
#endif

typedef struct TPMTestState {
    GMutex data_mutex;
    GCond data_cond;
    bool data_cond_signal;
    SocketAddress *addr;
    QIOChannel *tpm_ioc;
    GThread *emu_tpm_thread;
    struct tpm_hdr *tpm_msg;
    enum TPMVersion tpm_version;
} TPMTestState;

void tpm_emu_test_wait_cond(TPMTestState *s);
void *tpm_emu_ctrl_thread(void *data);
bool tpm_model_is_available(const char *args, const char *tpm_if);

#endif /* TESTS_TPM_EMU_H */
