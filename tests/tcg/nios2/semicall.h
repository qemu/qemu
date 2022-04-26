/*
 * Nios2 semihosting interface.
 *
 * Copyright Linaro Ltd 2022
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SEMICALL_H
#define SEMICALL_H

#define HOSTED_EXIT          0
#define HOSTED_INIT_SIM      1
#define HOSTED_OPEN          2
#define HOSTED_CLOSE         3
#define HOSTED_READ          4
#define HOSTED_WRITE         5
#define HOSTED_LSEEK         6
#define HOSTED_RENAME        7
#define HOSTED_UNLINK        8
#define HOSTED_STAT          9
#define HOSTED_FSTAT         10
#define HOSTED_GETTIMEOFDAY  11
#define HOSTED_ISATTY        12
#define HOSTED_SYSTEM        13

#define semihosting_call     break 1

#endif /* SEMICALL_H */
