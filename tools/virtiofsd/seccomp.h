/*
 * Seccomp sandboxing for virtiofsd
 *
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef VIRTIOFSD_SECCOMP_H
#define VIRTIOFSD_SECCOMP_H

#include <stdbool.h>

void setup_seccomp(bool enable_syslog);

#endif /* VIRTIOFSD_SECCOMP_H */
