/**
 * QEMU WLAN access point emulation
 *
 * Copyright (c) 2008 Clemens Kolbitsch
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * Modifications:
 *  2008-February-24  Clemens Kolbitsch :
 *                                  New implementation based on ne2000.c
 *
 */

#ifndef atheros_wlan_packet_h
#define atheros_wlan_packet_h 1

#include "hw/atheros_wlan.h"

void Atheros_WLAN_init_frame(Atheros_WLANState *s, struct mac80211_frame *frame);

int Atheros_WLAN_dumpFrame(struct mac80211_frame *frame, int frame_len, char *filename);

struct mac80211_frame *Atheros_WLAN_create_beacon_frame(void);
struct mac80211_frame *Atheros_WLAN_create_probe_response(void);
struct mac80211_frame *Atheros_WLAN_create_authentication(void);
struct mac80211_frame *Atheros_WLAN_create_deauthentication(void);
struct mac80211_frame *Atheros_WLAN_create_association_response(void);
struct mac80211_frame *Atheros_WLAN_create_disassociation(void);
struct mac80211_frame *Atheros_WLAN_create_data_reply(Atheros_WLANState *s, struct mac80211_frame *incoming);
struct mac80211_frame *Atheros_WLAN_create_data_packet(Atheros_WLANState *s, const uint8_t *buf, int size);

#endif // atheros_wlan_packet_h
