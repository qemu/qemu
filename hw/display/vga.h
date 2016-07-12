/*
 * linux/include/video/vga.h -- standard VGA chipset interaction
 *
 * Copyright 1999 Jeff Garzik <jgarzik@pobox.com>
 *
 * Copyright history from vga16fb.c:
 *	Copyright 1999 Ben Pfaff and Petr Vandrovec
 *	Based on VGA info at http://www.osdever.net/FreeVGA/home.htm
 *	Based on VESA framebuffer (c) 1998 Gerd Knorr
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 */

#ifndef LINUX_VIDEO_VGA_H
#define LINUX_VIDEO_VGA_H

/* Some of the code below is taken from SVGAlib.  The original,
   unmodified copyright notice for that code is below. */
/* VGAlib version 1.2 - (c) 1993 Tommy Frandsen                    */
/*                                                                 */
/* This library is free software; you can redistribute it and/or   */
/* modify it without any restrictions. This library is distributed */
/* in the hope that it will be useful, but without any warranty.   */

/* Multi-chipset support Copyright 1993 Harm Hanemaayer */
/* partially copyrighted (C) 1993 by Hartmut Schirmer */

/* VGA data register ports */
#define VGA_CRT_DC      0x3D5   /* CRT Controller Data Register - color emulation */
#define VGA_CRT_DM      0x3B5   /* CRT Controller Data Register - mono emulation */
#define VGA_ATT_R       0x3C1   /* Attribute Controller Data Read Register */
#define VGA_ATT_W       0x3C0   /* Attribute Controller Data Write Register */
#define VGA_GFX_D       0x3CF   /* Graphics Controller Data Register */
#define VGA_SEQ_D       0x3C5   /* Sequencer Data Register */
#define VGA_MIS_R       0x3CC   /* Misc Output Read Register */
#define VGA_MIS_W       0x3C2   /* Misc Output Write Register */
#define VGA_FTC_R       0x3CA   /* Feature Control Read Register */
#define VGA_IS1_RC      0x3DA   /* Input Status Register 1 - color emulation */
#define VGA_IS1_RM      0x3BA   /* Input Status Register 1 - mono emulation */
#define VGA_PEL_D       0x3C9   /* PEL Data Register */
#define VGA_PEL_MSK     0x3C6   /* PEL mask register */

/* EGA-specific registers */
#define EGA_GFX_E0      0x3CC   /* Graphics enable processor 0 */
#define EGA_GFX_E1      0x3CA   /* Graphics enable processor 1 */

/* VGA index register ports */
#define VGA_CRT_IC      0x3D4   /* CRT Controller Index - color emulation */
#define VGA_CRT_IM      0x3B4   /* CRT Controller Index - mono emulation */
#define VGA_ATT_IW      0x3C0   /* Attribute Controller Index & Data Write Register */
#define VGA_GFX_I       0x3CE   /* Graphics Controller Index */
#define VGA_SEQ_I       0x3C4   /* Sequencer Index */
#define VGA_PEL_IW      0x3C8   /* PEL Write Index */
#define VGA_PEL_IR      0x3C7   /* PEL Read Index */

/* standard VGA indexes max counts */
#define VGA_CRT_C       0x19    /* Number of CRT Controller Registers */
#define VGA_ATT_C       0x15    /* Number of Attribute Controller Registers */
#define VGA_GFX_C       0x09    /* Number of Graphics Controller Registers */
#define VGA_SEQ_C       0x05    /* Number of Sequencer Registers */
#define VGA_MIS_C       0x01    /* Number of Misc Output Register */

/* VGA misc register bit masks */
#define VGA_MIS_COLOR           0x01
#define VGA_MIS_ENB_MEM_ACCESS  0x02
#define VGA_MIS_DCLK_28322_720  0x04
#define VGA_MIS_ENB_PLL_LOAD    (0x04 | 0x08)
#define VGA_MIS_SEL_HIGH_PAGE   0x20

/* VGA CRT controller register indices */
#define VGA_CRTC_H_TOTAL        0
#define VGA_CRTC_H_DISP         1
#define VGA_CRTC_H_BLANK_START  2
#define VGA_CRTC_H_BLANK_END    3
#define VGA_CRTC_H_SYNC_START   4
#define VGA_CRTC_H_SYNC_END     5
#define VGA_CRTC_V_TOTAL        6
#define VGA_CRTC_OVERFLOW       7
#define VGA_CRTC_PRESET_ROW     8
#define VGA_CRTC_MAX_SCAN       9
#define VGA_CRTC_CURSOR_START   0x0A
#define VGA_CRTC_CURSOR_END     0x0B
#define VGA_CRTC_START_HI       0x0C
#define VGA_CRTC_START_LO       0x0D
#define VGA_CRTC_CURSOR_HI      0x0E
#define VGA_CRTC_CURSOR_LO      0x0F
#define VGA_CRTC_V_SYNC_START   0x10
#define VGA_CRTC_V_SYNC_END     0x11
#define VGA_CRTC_V_DISP_END     0x12
#define VGA_CRTC_OFFSET         0x13
#define VGA_CRTC_UNDERLINE      0x14
#define VGA_CRTC_V_BLANK_START  0x15
#define VGA_CRTC_V_BLANK_END    0x16
#define VGA_CRTC_MODE           0x17
#define VGA_CRTC_LINE_COMPARE   0x18
#define VGA_CRTC_REGS           VGA_CRT_C

/* VGA CRT controller bit masks */
#define VGA_CR11_LOCK_CR0_CR7   0x80 /* lock writes to CR0 - CR7 */
#define VGA_CR17_H_V_SIGNALS_ENABLED 0x80

/* VGA attribute controller register indices */
#define VGA_ATC_PALETTE0        0x00
#define VGA_ATC_PALETTE1        0x01
#define VGA_ATC_PALETTE2        0x02
#define VGA_ATC_PALETTE3        0x03
#define VGA_ATC_PALETTE4        0x04
#define VGA_ATC_PALETTE5        0x05
#define VGA_ATC_PALETTE6        0x06
#define VGA_ATC_PALETTE7        0x07
#define VGA_ATC_PALETTE8        0x08
#define VGA_ATC_PALETTE9        0x09
#define VGA_ATC_PALETTEA        0x0A
#define VGA_ATC_PALETTEB        0x0B
#define VGA_ATC_PALETTEC        0x0C
#define VGA_ATC_PALETTED        0x0D
#define VGA_ATC_PALETTEE        0x0E
#define VGA_ATC_PALETTEF        0x0F
#define VGA_ATC_MODE            0x10
#define VGA_ATC_OVERSCAN        0x11
#define VGA_ATC_PLANE_ENABLE    0x12
#define VGA_ATC_PEL             0x13
#define VGA_ATC_COLOR_PAGE      0x14

#define VGA_AR_ENABLE_DISPLAY   0x20

/* VGA sequencer register indices */
#define VGA_SEQ_RESET           0x00
#define VGA_SEQ_CLOCK_MODE      0x01
#define VGA_SEQ_PLANE_WRITE     0x02
#define VGA_SEQ_CHARACTER_MAP   0x03
#define VGA_SEQ_MEMORY_MODE     0x04

/* VGA sequencer register bit masks */
#define VGA_SR01_CHAR_CLK_8DOTS 0x01 /* bit 0: character clocks 8 dots wide are generated */
#define VGA_SR01_SCREEN_OFF     0x20 /* bit 5: Screen is off */
#define VGA_SR02_ALL_PLANES     0x0F /* bits 3-0: enable access to all planes */
#define VGA_SR04_EXT_MEM        0x02 /* bit 1: allows complete mem access to 256K */
#define VGA_SR04_SEQ_MODE       0x04 /* bit 2: directs system to use a sequential addressing mode */
#define VGA_SR04_CHN_4M         0x08 /* bit 3: selects modulo 4 addressing for CPU access to display memory */

/* VGA graphics controller register indices */
#define VGA_GFX_SR_VALUE        0x00
#define VGA_GFX_SR_ENABLE       0x01
#define VGA_GFX_COMPARE_VALUE   0x02
#define VGA_GFX_DATA_ROTATE     0x03
#define VGA_GFX_PLANE_READ      0x04
#define VGA_GFX_MODE            0x05
#define VGA_GFX_MISC            0x06
#define VGA_GFX_COMPARE_MASK    0x07
#define VGA_GFX_BIT_MASK        0x08

/* VGA graphics controller bit masks */
#define VGA_GR06_GRAPHICS_MODE  0x01

#endif /* LINUX_VIDEO_VGA_H */
