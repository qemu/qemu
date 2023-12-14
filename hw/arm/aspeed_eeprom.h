/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef ASPEED_EEPROM_H
#define ASPEED_EEPROM_H

#include "qemu/osdep.h"

extern const uint8_t tiogapass_bmc_fruid[];
extern const size_t tiogapass_bmc_fruid_len;

extern const uint8_t fby35_nic_fruid[];
extern const uint8_t fby35_bb_fruid[];
extern const uint8_t fby35_bmc_fruid[];
extern const size_t fby35_nic_fruid_len;
extern const size_t fby35_bb_fruid_len;
extern const size_t fby35_bmc_fruid_len;

extern const uint8_t yosemitev2_bmc_fruid[];
extern const size_t yosemitev2_bmc_fruid_len;

extern const uint8_t rainier_bb_fruid[];
extern const size_t rainier_bb_fruid_len;
extern const uint8_t rainier_bmc_fruid[];
extern const size_t rainier_bmc_fruid_len;

#endif
