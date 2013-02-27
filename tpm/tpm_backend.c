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

#include "tpm/tpm.h"
#include "qemu/thread.h"
#include "tpm_backend.h"

void tpm_backend_thread_deliver_request(TPMBackendThread *tbt)
{
   g_thread_pool_push(tbt->pool, (gpointer)TPM_BACKEND_CMD_PROCESS_CMD, NULL);
}

void tpm_backend_thread_create(TPMBackendThread *tbt,
                               GFunc func, gpointer user_data)
{
    if (!tbt->pool) {
        tbt->pool = g_thread_pool_new(func, user_data, 1, TRUE, NULL);
        g_thread_pool_push(tbt->pool, (gpointer)TPM_BACKEND_CMD_INIT, NULL);
    }
}

void tpm_backend_thread_end(TPMBackendThread *tbt)
{
    if (tbt->pool) {
        g_thread_pool_push(tbt->pool, (gpointer)TPM_BACKEND_CMD_END, NULL);
        g_thread_pool_free(tbt->pool, FALSE, TRUE);
        tbt->pool = NULL;
    }
}

void tpm_backend_thread_tpm_reset(TPMBackendThread *tbt,
                                  GFunc func, gpointer user_data)
{
    if (!tbt->pool) {
        tpm_backend_thread_create(tbt, func, user_data);
    } else {
        g_thread_pool_push(tbt->pool, (gpointer)TPM_BACKEND_CMD_TPM_RESET,
                           NULL);
    }
}
