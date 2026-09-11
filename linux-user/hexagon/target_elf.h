/*
 *  Copyright(c) 2019-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HEXAGON_TARGET_ELF_H
#define HEXAGON_TARGET_ELF_H

#define ELF_CLASS               ELFCLASS32
#define ELF_MACHINE             EM_HEXAGON

#define HAVE_ELF_HWCAP          1

enum {
    HWCAP_HEXAGON_ISA_MASK          = 0x7F,
    HWCAP_HEXAGON_ISA_V2            = 1,
    HWCAP_HEXAGON_ISA_V3            = 2,
    HWCAP_HEXAGON_ISA_V4            = 3,
    HWCAP_HEXAGON_ISA_V5            = 4,
    HWCAP_HEXAGON_ISA_V55           = 5,
    HWCAP_HEXAGON_ISA_V60           = 6,
    HWCAP_HEXAGON_ISA_V62           = 7,
    HWCAP_HEXAGON_ISA_V65           = 8,
    HWCAP_HEXAGON_ISA_V66           = 9,
    HWCAP_HEXAGON_ISA_V67           = 10,
    HWCAP_HEXAGON_ISA_V68           = 11,
    HWCAP_HEXAGON_ISA_V69           = 12,
    HWCAP_HEXAGON_ISA_V71           = 13,
    HWCAP_HEXAGON_ISA_V73           = 14,
    HWCAP_HEXAGON_ISA_V75           = 15,
    HWCAP_HEXAGON_ISA_V77           = 16,
    HWCAP_HEXAGON_ISA_V79           = 17,
    HWCAP_HEXAGON_ISA_V81           = 18,
    HWCAP_HEXAGON_HVX               = 1 << 7,
    HWCAP_HEXAGON_CABAC             = 1 << 8,
    HWCAP_HEXAGON_HVX_LENGTH_128B   = 1 << 9,
    HWCAP_HEXAGON_HVX_IEEE_FP       = 1 << 10,
    HWCAP_HEXAGON_AUDIO             = 1 << 11,
    HWCAP_HEXAGON_HMX               = 1 << 12,
};

#endif
