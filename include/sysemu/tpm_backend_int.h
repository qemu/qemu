/*
 *  common TPM backend driver functions
 *
 *  Copyright (c) 2012-2013 IBM Corporation
 *  Authors:
 *    Stefan Berger <stefanb@us.ibm.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#ifndef TPM_TPM_BACKEND_H
#define TPM_TPM_BACKEND_H

#include <glib.h>

typedef struct TPMBackendThread {
    GThreadPool *pool;
} TPMBackendThread;

void tpm_backend_thread_deliver_request(TPMBackendThread *tbt);
void tpm_backend_thread_create(TPMBackendThread *tbt,
                               GFunc func, gpointer user_data);
void tpm_backend_thread_end(TPMBackendThread *tbt);

typedef enum TPMBackendCmd {
    TPM_BACKEND_CMD_INIT = 1,
    TPM_BACKEND_CMD_PROCESS_CMD,
    TPM_BACKEND_CMD_END,
    TPM_BACKEND_CMD_TPM_RESET,
} TPMBackendCmd;

#endif /* TPM_TPM_BACKEND_H */
