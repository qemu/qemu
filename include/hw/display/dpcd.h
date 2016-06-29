/*
 * dpcd.h
 *
 *  Copyright (C)2015 : GreenSocs Ltd
 *      http://www.greensocs.com/ , email: info@greensocs.com
 *
 *  Developed by :
 *  Frederic Konrad   <fred.konrad@greensocs.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option)any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef DPCD_H
#define DPCD_H

typedef struct DPCDState DPCDState;

#define TYPE_DPCD "dpcd"
#define DPCD(obj) OBJECT_CHECK(DPCDState, (obj), TYPE_DPCD)

/* DCPD Revision. */
#define DPCD_REVISION                           0x00
#define DPCD_REV_1_0                            0x10
#define DPCD_REV_1_1                            0x11

/* DCPD Max Link Rate. */
#define DPCD_MAX_LINK_RATE                      0x01
#define DPCD_1_62GBPS                           0x06
#define DPCD_2_7GBPS                            0x0A
#define DPCD_5_4GBPS                            0x14

#define DPCD_MAX_LANE_COUNT                     0x02
#define DPCD_ONE_LANE                           0x01
#define DPCD_TWO_LANES                          0x02
#define DPCD_FOUR_LANES                         0x04

/* DCPD Max down spread. */
#define DPCD_UP_TO_0_5                          0x01
#define DPCD_NO_AUX_HANDSHAKE_LINK_TRAINING     0x40

/* DCPD Downstream port type. */
#define DPCD_DISPLAY_PORT                       0x00
#define DPCD_ANALOG                             0x02
#define DPCD_DVI_HDMI                           0x04
#define DPCD_OTHER                              0x06

/* DPCD Format conversion. */
#define DPCD_FORMAT_CONVERSION                  0x08

/* Main link channel coding. */
#define DPCD_ANSI_8B_10B                        0x01

/* Down stream port count. */
#define DPCD_OUI_SUPPORTED                      0x80

/* Receiver port capability. */
#define DPCD_RECEIVE_PORT0_CAP_0                0x08
#define DPCD_RECEIVE_PORT0_CAP_1                0x09
#define DPCD_EDID_PRESENT                       0x02
#define DPCD_ASSOCIATED_TO_PRECEDING_PORT       0x04

/* Down stream port capability. */
#define DPCD_CAP_DISPLAY_PORT                   0x000
#define DPCD_CAP_ANALOG_VGA                     0x001
#define DPCD_CAP_DVI                            0x002
#define DPCD_CAP_HDMI                           0x003
#define DPCD_CAP_OTHER                          0x100

#define DPCD_LANE0_1_STATUS                     0x202
#define DPCD_LANE0_CR_DONE                      (1 << 0)
#define DPCD_LANE0_CHANNEL_EQ_DONE              (1 << 1)
#define DPCD_LANE0_SYMBOL_LOCKED                (1 << 2)
#define DPCD_LANE1_CR_DONE                      (1 << 4)
#define DPCD_LANE1_CHANNEL_EQ_DONE              (1 << 5)
#define DPCD_LANE1_SYMBOL_LOCKED                (1 << 6)

#define DPCD_LANE2_3_STATUS                     0x203
#define DPCD_LANE2_CR_DONE                      (1 << 0)
#define DPCD_LANE2_CHANNEL_EQ_DONE              (1 << 1)
#define DPCD_LANE2_SYMBOL_LOCKED                (1 << 2)
#define DPCD_LANE3_CR_DONE                      (1 << 4)
#define DPCD_LANE3_CHANNEL_EQ_DONE              (1 << 5)
#define DPCD_LANE3_SYMBOL_LOCKED                (1 << 6)

#define DPCD_LANE_ALIGN_STATUS_UPDATED          0x204
#define DPCD_INTERLANE_ALIGN_DONE               0x01
#define DPCD_DOWNSTREAM_PORT_STATUS_CHANGED     0x40
#define DPCD_LINK_STATUS_UPDATED                0x80

#define DPCD_SINK_STATUS                        0x205
#define DPCD_RECEIVE_PORT_0_STATUS              0x01

#endif /* DPCD_H */
