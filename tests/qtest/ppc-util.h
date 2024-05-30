/*
 * PowerPC misc useful things
 *
 * Copyright (c) 2024, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PPC_UTIL_H
#define PPC_UTIL_H

/* List of capabilities needed to silence warnings with TCG */
#define PSERIES_DEFAULT_CAPABILITIES             \
    "cap-cfpc=broken,"                           \
    "cap-sbbc=broken,"                           \
    "cap-ibs=broken,"                            \
    "cap-ccf-assist=off,"

#endif /* PPC_UTIL_H */
