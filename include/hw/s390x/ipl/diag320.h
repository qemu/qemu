/*
 * S/390 DIAGNOSE 320 definitions and structures
 *
 * Copyright 2025 IBM Corp.
 * Author(s): Zhuoying Cai <zycai@linux.ibm.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef S390X_DIAG320_H
#define S390X_DIAG320_H

#define DIAG_320_SUBC_QUERY_ISM     0

#define DIAG_320_RC_OK              0x0001
#define DIAG_320_RC_NOT_SUPPORTED   0x0102

#define DIAG_320_ISM_QUERY_SUBCODES 0x80000000

#endif
