/*
 * Emulation of BSD signals
 *
 * Copyright (c) 2013 Stacey Son
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SIGNAL_COMMON_H
#define SIGNAL_COMMON_H

void force_sig_fault(int sig, int code, abi_ulong addr);

#endif
