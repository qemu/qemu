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

#ifndef atheros_wlan_ap_h
#define atheros_wlan_ap_h 1

void Atheros_WLAN_setup_ap(NICInfo *nd, PCIAtheros_WLANState *d);

void Atheros_WLAN_handleTxBuffer(Atheros_WLANState *s, uint32_t queue);
void Atheros_WLAN_handleRxBuffer(Atheros_WLANState *s, struct mac80211_frame *frame, uint32_t frame_length);

void Atheros_WLAN_handle_frame(Atheros_WLANState *s, struct mac80211_frame *frame);

void Atheros_WLAN_insert_frame(Atheros_WLANState *s, struct mac80211_frame *frame);

void Atheros_WLAN_disable_irq(void *arg);
void Atheros_WLAN_enable_irq(void *arg);
void Atheros_WLAN_update_irq(void *arg);
void Atheros_WLAN_append_irq(Atheros_WLANState *s, struct pending_interrupt intr);

#endif // atheros_wlan_ap_h
