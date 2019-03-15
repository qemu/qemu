/* Definitions for QEMU's persistent reservation helper daemon
 *
 * Copyright (C) 2017 Red Hat, Inc.
 *
 * Author:
 *   Paolo Bonzini <pbonzini@redhat.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef QEMU_PR_HELPER_H
#define QEMU_PR_HELPER_H

#define PR_HELPER_CDB_SIZE     16
#define PR_HELPER_SENSE_SIZE   96
#define PR_HELPER_DATA_SIZE    8192

typedef struct PRHelperResponse {
    int32_t result;
    int32_t sz;
    uint8_t sense[PR_HELPER_SENSE_SIZE];
} PRHelperResponse;

#endif
