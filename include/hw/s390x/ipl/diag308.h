/*
 * S/390 DIAGNOSE 308 definitions and structures
 *
 * Copyright 2025 IBM Corp.
 * Author(s): Zhuoying Cai <zycai@linux.ibm.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef S390X_DIAG308_H
#define S390X_DIAG308_H

#define DIAG_308_RC_OK              0x0001
#define DIAG_308_RC_NO_CONF         0x0102
#define DIAG_308_RC_INVALID         0x0402
#define DIAG_308_RC_NO_PV_CONF      0x0902
#define DIAG_308_RC_INVAL_FOR_PV    0x0a02

#define DIAG308_RESET_MOD_CLR       0
#define DIAG308_RESET_LOAD_NORM     1
#define DIAG308_LOAD_CLEAR          3
#define DIAG308_LOAD_NORMAL_DUMP    4
#define DIAG308_SET                 5
#define DIAG308_STORE               6
#define DIAG308_PV_SET              8
#define DIAG308_PV_STORE            9
#define DIAG308_PV_START            10

#define DIAG308_FLAGS_LP_VALID 0x80

#define DIAG308_IPIB_FLAGS_SIPL 0x40
#define DIAG308_IPIB_FLAGS_IPLIR 0x20

#endif
