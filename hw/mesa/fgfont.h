/*
 * fgfont.h
 *
 * Bitmap fonts displaying ripped from FreeGLUT
 *
 * Copyright (c) 1999-2000 Pawel W. Olszta. All Rights Reserved.
 * Written by Pawel W. Olszta, <olszta@sourceforge.net>
 * Creation date: Thu Dec 16 1999
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PAWEL W. OLSZTA BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */


/* The bitmap font structure */
typedef struct tagSFG_Font SFG_Font;
struct tagSFG_Font
{  
    const char*     Name;         /* The source font name             */
    int             Quantity;     /* Number of chars in font          */
    int             Height;       /* Height of the characters         */
    const unsigned char** Characters;   /* The characters mapping           */

    float           xorig, yorig; /* Relative origin of the character */
};


static const unsigned char Fixed8x13_Character_000[] = {  8,  0,  0,  0,170,  0,130,  0,130,  0,130,  0,170,  0,  0};
static const unsigned char Fixed8x13_Character_001[] = {  8,  0,  0,  0,  0, 16, 56,124,254,124, 56, 16,  0,  0,  0};
static const unsigned char Fixed8x13_Character_002[] = {  8,  0,170, 85,170, 85,170, 85,170, 85,170, 85,170, 85,170};
static const unsigned char Fixed8x13_Character_003[] = {  8,  0,  0,  0,  4,  4,  4,  4,174,160,224,160,160,  0,  0};
static const unsigned char Fixed8x13_Character_004[] = {  8,  0,  0,  0,  8,  8, 12,  8,142,128,192,128,224,  0,  0};
static const unsigned char Fixed8x13_Character_005[] = {  8,  0,  0,  0, 10, 10, 12, 10,108,128,128,128, 96,  0,  0};
static const unsigned char Fixed8x13_Character_006[] = {  8,  0,  0,  0,  8,  8, 12,  8,238,128,128,128,128,  0,  0};
static const unsigned char Fixed8x13_Character_007[] = {  8,  0,  0,  0,  0,  0,  0,  0,  0, 24, 36, 36, 24,  0,  0};
static const unsigned char Fixed8x13_Character_008[] = {  8,  0,  0,  0,  0,124,  0, 16, 16,124, 16, 16,  0,  0,  0};
static const unsigned char Fixed8x13_Character_009[] = {  8,  0,  0,  0, 14,  8,  8,  8,168,160,160,160,192,  0,  0};
static const unsigned char Fixed8x13_Character_010[] = {  8,  0,  0,  0,  4,  4,  4,  4, 46, 80, 80,136,136,  0,  0};
static const unsigned char Fixed8x13_Character_011[] = {  8,  0,  0,  0,  0,  0,  0,  0,240, 16, 16, 16, 16, 16, 16};
static const unsigned char Fixed8x13_Character_012[] = {  8,  0, 16, 16, 16, 16, 16, 16,240,  0,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_013[] = {  8,  0, 16, 16, 16, 16, 16, 16, 31,  0,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_014[] = {  8,  0,  0,  0,  0,  0,  0,  0, 31, 16, 16, 16, 16, 16, 16};
static const unsigned char Fixed8x13_Character_015[] = {  8,  0, 16, 16, 16, 16, 16, 16,255, 16, 16, 16, 16, 16, 16};
static const unsigned char Fixed8x13_Character_016[] = {  8,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,255};
static const unsigned char Fixed8x13_Character_017[] = {  8,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,255,  0,  0,  0};
static const unsigned char Fixed8x13_Character_018[] = {  8,  0,  0,  0,  0,  0,  0,  0,255,  0,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_019[] = {  8,  0,  0,  0,  0,255,  0,  0,  0,  0,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_020[] = {  8,  0,255,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_021[] = {  8,  0, 16, 16, 16, 16, 16, 16, 31, 16, 16, 16, 16, 16, 16};
static const unsigned char Fixed8x13_Character_022[] = {  8,  0, 16, 16, 16, 16, 16, 16,240, 16, 16, 16, 16, 16, 16};
static const unsigned char Fixed8x13_Character_023[] = {  8,  0,  0,  0,  0,  0,  0,  0,255, 16, 16, 16, 16, 16, 16};
static const unsigned char Fixed8x13_Character_024[] = {  8,  0, 16, 16, 16, 16, 16, 16,255,  0,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_025[] = {  8,  0, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16};
static const unsigned char Fixed8x13_Character_026[] = {  8,  0,  0,  0,254,  0, 14, 48,192, 48, 14,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_027[] = {  8,  0,  0,  0,254,  0,224, 24,  6, 24,224,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_028[] = {  8,  0,  0,  0, 68, 68, 68, 68, 68,254,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_029[] = {  8,  0,  0,  0, 32, 32,126, 16,  8,126,  4,  4,  0,  0,  0};
static const unsigned char Fixed8x13_Character_030[] = {  8,  0,  0,  0,220, 98, 32, 32, 32,112, 32, 34, 28,  0,  0};
static const unsigned char Fixed8x13_Character_031[] = {  8,  0,  0,  0,  0,  0,  0,  0, 24,  0,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_032[] = {  8,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_033[] = {  8,  0,  0,  0, 16,  0, 16, 16, 16, 16, 16, 16, 16,  0,  0};
static const unsigned char Fixed8x13_Character_034[] = {  8,  0,  0,  0,  0,  0,  0,  0,  0,  0, 36, 36, 36,  0,  0};
static const unsigned char Fixed8x13_Character_035[] = {  8,  0,  0,  0,  0, 36, 36,126, 36,126, 36, 36,  0,  0,  0};
static const unsigned char Fixed8x13_Character_036[] = {  8,  0,  0,  0, 16,120, 20, 20, 56, 80, 80, 60, 16,  0,  0};
static const unsigned char Fixed8x13_Character_037[] = {  8,  0,  0,  0, 68, 42, 36, 16,  8,  8, 36, 82, 34,  0,  0};
static const unsigned char Fixed8x13_Character_038[] = {  8,  0,  0,  0, 58, 68, 74, 48, 72, 72, 48,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_039[] = {  8,  0,  0,  0,  0,  0,  0,  0,  0,  0, 64, 48, 56,  0,  0};
static const unsigned char Fixed8x13_Character_040[] = {  8,  0,  0,  0,  4,  8,  8, 16, 16, 16,  8,  8,  4,  0,  0};
static const unsigned char Fixed8x13_Character_041[] = {  8,  0,  0,  0, 32, 16, 16,  8,  8,  8, 16, 16, 32,  0,  0};
static const unsigned char Fixed8x13_Character_042[] = {  8,  0,  0,  0,  0,  0, 36, 24,126, 24, 36,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_043[] = {  8,  0,  0,  0,  0,  0, 16, 16,124, 16, 16,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_044[] = {  8,  0,  0, 64, 48, 56,  0,  0,  0,  0,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_045[] = {  8,  0,  0,  0,  0,  0,  0,  0,126,  0,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_046[] = {  8,  0,  0, 16, 56, 16,  0,  0,  0,  0,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_047[] = {  8,  0,  0,  0,128,128, 64, 32, 16,  8,  4,  2,  2,  0,  0};
static const unsigned char Fixed8x13_Character_048[] = {  8,  0,  0,  0, 24, 36, 66, 66, 66, 66, 66, 36, 24,  0,  0};
static const unsigned char Fixed8x13_Character_049[] = {  8,  0,  0,  0,124, 16, 16, 16, 16, 16, 80, 48, 16,  0,  0};
static const unsigned char Fixed8x13_Character_050[] = {  8,  0,  0,  0,126, 64, 32, 24,  4,  2, 66, 66, 60,  0,  0};
static const unsigned char Fixed8x13_Character_051[] = {  8,  0,  0,  0, 60, 66,  2,  2, 28,  8,  4,  2,126,  0,  0};
static const unsigned char Fixed8x13_Character_052[] = {  8,  0,  0,  0,  4,  4,126, 68, 68, 36, 20, 12,  4,  0,  0};
static const unsigned char Fixed8x13_Character_053[] = {  8,  0,  0,  0, 60, 66,  2,  2, 98, 92, 64, 64,126,  0,  0};
static const unsigned char Fixed8x13_Character_054[] = {  8,  0,  0,  0, 60, 66, 66, 98, 92, 64, 64, 32, 28,  0,  0};
static const unsigned char Fixed8x13_Character_055[] = {  8,  0,  0,  0, 32, 32, 16, 16,  8,  8,  4,  2,126,  0,  0};
static const unsigned char Fixed8x13_Character_056[] = {  8,  0,  0,  0, 60, 66, 66, 66, 60, 66, 66, 66, 60,  0,  0};
static const unsigned char Fixed8x13_Character_057[] = {  8,  0,  0,  0, 56,  4,  2,  2, 58, 70, 66, 66, 60,  0,  0};
static const unsigned char Fixed8x13_Character_058[] = {  8,  0,  0, 16, 56, 16,  0,  0, 16, 56, 16,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_059[] = {  8,  0,  0, 64, 48, 56,  0,  0, 16, 56, 16,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_060[] = {  8,  0,  0,  0,  2,  4,  8, 16, 32, 16,  8,  4,  2,  0,  0};
static const unsigned char Fixed8x13_Character_061[] = {  8,  0,  0,  0,  0,  0,126,  0,  0,126,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_062[] = {  8,  0,  0,  0, 64, 32, 16,  8,  4,  8, 16, 32, 64,  0,  0};
static const unsigned char Fixed8x13_Character_063[] = {  8,  0,  0,  0,  8,  0,  8,  8,  4,  2, 66, 66, 60,  0,  0};
static const unsigned char Fixed8x13_Character_064[] = {  8,  0,  0,  0, 60, 64, 74, 86, 82, 78, 66, 66, 60,  0,  0};
static const unsigned char Fixed8x13_Character_065[] = {  8,  0,  0,  0, 66, 66, 66,126, 66, 66, 66, 36, 24,  0,  0};
static const unsigned char Fixed8x13_Character_066[] = {  8,  0,  0,  0,252, 66, 66, 66,124, 66, 66, 66,252,  0,  0};
static const unsigned char Fixed8x13_Character_067[] = {  8,  0,  0,  0, 60, 66, 64, 64, 64, 64, 64, 66, 60,  0,  0};
static const unsigned char Fixed8x13_Character_068[] = {  8,  0,  0,  0,252, 66, 66, 66, 66, 66, 66, 66,252,  0,  0};
static const unsigned char Fixed8x13_Character_069[] = {  8,  0,  0,  0,126, 64, 64, 64,120, 64, 64, 64,126,  0,  0};
static const unsigned char Fixed8x13_Character_070[] = {  8,  0,  0,  0, 64, 64, 64, 64,120, 64, 64, 64,126,  0,  0};
static const unsigned char Fixed8x13_Character_071[] = {  8,  0,  0,  0, 58, 70, 66, 78, 64, 64, 64, 66, 60,  0,  0};
static const unsigned char Fixed8x13_Character_072[] = {  8,  0,  0,  0, 66, 66, 66, 66,126, 66, 66, 66, 66,  0,  0};
static const unsigned char Fixed8x13_Character_073[] = {  8,  0,  0,  0,124, 16, 16, 16, 16, 16, 16, 16,124,  0,  0};
static const unsigned char Fixed8x13_Character_074[] = {  8,  0,  0,  0, 56, 68,  4,  4,  4,  4,  4,  4, 31,  0,  0};
static const unsigned char Fixed8x13_Character_075[] = {  8,  0,  0,  0, 66, 68, 72, 80, 96, 80, 72, 68, 66,  0,  0};
static const unsigned char Fixed8x13_Character_076[] = {  8,  0,  0,  0,126, 64, 64, 64, 64, 64, 64, 64, 64,  0,  0};
static const unsigned char Fixed8x13_Character_077[] = {  8,  0,  0,  0,130,130,130,146,146,170,198,130,130,  0,  0};
static const unsigned char Fixed8x13_Character_078[] = {  8,  0,  0,  0, 66, 66, 66, 70, 74, 82, 98, 66, 66,  0,  0};
static const unsigned char Fixed8x13_Character_079[] = {  8,  0,  0,  0, 60, 66, 66, 66, 66, 66, 66, 66, 60,  0,  0};
static const unsigned char Fixed8x13_Character_080[] = {  8,  0,  0,  0, 64, 64, 64, 64,124, 66, 66, 66,124,  0,  0};
static const unsigned char Fixed8x13_Character_081[] = {  8,  0,  0,  2, 60, 74, 82, 66, 66, 66, 66, 66, 60,  0,  0};
static const unsigned char Fixed8x13_Character_082[] = {  8,  0,  0,  0, 66, 68, 72, 80,124, 66, 66, 66,124,  0,  0};
static const unsigned char Fixed8x13_Character_083[] = {  8,  0,  0,  0, 60, 66,  2,  2, 60, 64, 64, 66, 60,  0,  0};
static const unsigned char Fixed8x13_Character_084[] = {  8,  0,  0,  0, 16, 16, 16, 16, 16, 16, 16, 16,254,  0,  0};
static const unsigned char Fixed8x13_Character_085[] = {  8,  0,  0,  0, 60, 66, 66, 66, 66, 66, 66, 66, 66,  0,  0};
static const unsigned char Fixed8x13_Character_086[] = {  8,  0,  0,  0, 16, 40, 40, 40, 68, 68, 68,130,130,  0,  0};
static const unsigned char Fixed8x13_Character_087[] = {  8,  0,  0,  0, 68,170,146,146,146,130,130,130,130,  0,  0};
static const unsigned char Fixed8x13_Character_088[] = {  8,  0,  0,  0,130,130, 68, 40, 16, 40, 68,130,130,  0,  0};
static const unsigned char Fixed8x13_Character_089[] = {  8,  0,  0,  0, 16, 16, 16, 16, 16, 40, 68,130,130,  0,  0};
static const unsigned char Fixed8x13_Character_090[] = {  8,  0,  0,  0,126, 64, 64, 32, 16,  8,  4,  2,126,  0,  0};
static const unsigned char Fixed8x13_Character_091[] = {  8,  0,  0,  0, 60, 32, 32, 32, 32, 32, 32, 32, 60,  0,  0};
static const unsigned char Fixed8x13_Character_092[] = {  8,  0,  0,  0,  2,  2,  4,  8, 16, 32, 64,128,128,  0,  0};
static const unsigned char Fixed8x13_Character_093[] = {  8,  0,  0,  0,120,  8,  8,  8,  8,  8,  8,  8,120,  0,  0};
static const unsigned char Fixed8x13_Character_094[] = {  8,  0,  0,  0,  0,  0,  0,  0,  0,  0, 68, 40, 16,  0,  0};
static const unsigned char Fixed8x13_Character_095[] = {  8,  0,  0,254,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_096[] = {  8,  0,  0,  0,  0,  0,  0,  0,  0,  0,  4, 24, 56,  0,  0};
static const unsigned char Fixed8x13_Character_097[] = {  8,  0,  0,  0, 58, 70, 66, 62,  2, 60,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_098[] = {  8,  0,  0,  0, 92, 98, 66, 66, 98, 92, 64, 64, 64,  0,  0};
static const unsigned char Fixed8x13_Character_099[] = {  8,  0,  0,  0, 60, 66, 64, 64, 66, 60,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_100[] = {  8,  0,  0,  0, 58, 70, 66, 66, 70, 58,  2,  2,  2,  0,  0};
static const unsigned char Fixed8x13_Character_101[] = {  8,  0,  0,  0, 60, 66, 64,126, 66, 60,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_102[] = {  8,  0,  0,  0, 32, 32, 32, 32,124, 32, 32, 34, 28,  0,  0};
static const unsigned char Fixed8x13_Character_103[] = {  8,  0, 60, 66, 60, 64, 56, 68, 68, 58,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_104[] = {  8,  0,  0,  0, 66, 66, 66, 66, 98, 92, 64, 64, 64,  0,  0};
static const unsigned char Fixed8x13_Character_105[] = {  8,  0,  0,  0,124, 16, 16, 16, 16, 48,  0, 16,  0,  0,  0};
static const unsigned char Fixed8x13_Character_106[] = {  8,  0, 56, 68, 68,  4,  4,  4,  4, 12,  0,  4,  0,  0,  0};
static const unsigned char Fixed8x13_Character_107[] = {  8,  0,  0,  0, 66, 68, 72,112, 72, 68, 64, 64, 64,  0,  0};
static const unsigned char Fixed8x13_Character_108[] = {  8,  0,  0,  0,124, 16, 16, 16, 16, 16, 16, 16, 48,  0,  0};
static const unsigned char Fixed8x13_Character_109[] = {  8,  0,  0,  0,130,146,146,146,146,236,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_110[] = {  8,  0,  0,  0, 66, 66, 66, 66, 98, 92,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_111[] = {  8,  0,  0,  0, 60, 66, 66, 66, 66, 60,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_112[] = {  8,  0, 64, 64, 64, 92, 98, 66, 98, 92,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_113[] = {  8,  0,  2,  2,  2, 58, 70, 66, 70, 58,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_114[] = {  8,  0,  0,  0, 32, 32, 32, 32, 34, 92,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_115[] = {  8,  0,  0,  0, 60, 66, 12, 48, 66, 60,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_116[] = {  8,  0,  0,  0, 28, 34, 32, 32, 32,124, 32, 32,  0,  0,  0};
static const unsigned char Fixed8x13_Character_117[] = {  8,  0,  0,  0, 58, 68, 68, 68, 68, 68,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_118[] = {  8,  0,  0,  0, 16, 40, 40, 68, 68, 68,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_119[] = {  8,  0,  0,  0, 68,170,146,146,130,130,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_120[] = {  8,  0,  0,  0, 66, 36, 24, 24, 36, 66,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_121[] = {  8,  0, 60, 66,  2, 58, 70, 66, 66, 66,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_122[] = {  8,  0,  0,  0,126, 32, 16,  8,  4,126,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_123[] = {  8,  0,  0,  0, 14, 16, 16,  8, 48,  8, 16, 16, 14,  0,  0};
static const unsigned char Fixed8x13_Character_124[] = {  8,  0,  0,  0, 16, 16, 16, 16, 16, 16, 16, 16, 16,  0,  0};
static const unsigned char Fixed8x13_Character_125[] = {  8,  0,  0,  0,112,  8,  8, 16, 12, 16,  8,  8,112,  0,  0};
static const unsigned char Fixed8x13_Character_126[] = {  8,  0,  0,  0,  0,  0,  0,  0,  0,  0, 72, 84, 36,  0,  0};
static const unsigned char Fixed8x13_Character_127[] = {  9,  0,  0,  0,  0,  0,  0,170,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,170,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_128[] = {  9,  0,  0,  0,  0,  0,  0,170,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,170,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_129[] = {  9,  0,  0,  0,  0,  0,  0,170,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,170,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_130[] = {  9,  0,  0,  0,  0,  0,  0,170,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,170,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_131[] = {  9,  0,  0,  0,  0,  0,  0,170,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,170,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_132[] = {  9,  0,  0,  0,  0,  0,  0,170,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,170,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_133[] = {  9,  0,  0,  0,  0,  0,  0,170,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,170,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_134[] = {  9,  0,  0,  0,  0,  0,  0,170,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,170,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_135[] = {  9,  0,  0,  0,  0,  0,  0,170,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,170,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_136[] = {  9,  0,  0,  0,  0,  0,  0,170,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,170,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_137[] = {  9,  0,  0,  0,  0,  0,  0,170,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,170,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_138[] = {  9,  0,  0,  0,  0,  0,  0,170,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,170,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_139[] = {  9,  0,  0,  0,  0,  0,  0,170,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,170,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_140[] = {  9,  0,  0,  0,  0,  0,  0,170,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,170,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_141[] = {  9,  0,  0,  0,  0,  0,  0,170,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,170,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_142[] = {  9,  0,  0,  0,  0,  0,  0,170,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,170,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_143[] = {  9,  0,  0,  0,  0,  0,  0,170,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,170,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_144[] = {  9,  0,  0,  0,  0,  0,  0,170,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,170,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_145[] = {  9,  0,  0,  0,  0,  0,  0,170,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,170,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_146[] = {  9,  0,  0,  0,  0,  0,  0,170,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,170,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_147[] = {  9,  0,  0,  0,  0,  0,  0,170,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,170,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_148[] = {  9,  0,  0,  0,  0,  0,  0,170,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,170,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_149[] = {  9,  0,  0,  0,  0,  0,  0,170,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,170,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_150[] = {  9,  0,  0,  0,  0,  0,  0,170,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,170,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_151[] = {  9,  0,  0,  0,  0,  0,  0,170,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,170,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_152[] = {  9,  0,  0,  0,  0,  0,  0,170,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,170,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_153[] = {  9,  0,  0,  0,  0,  0,  0,170,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,170,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_154[] = {  9,  0,  0,  0,  0,  0,  0,170,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,170,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_155[] = {  9,  0,  0,  0,  0,  0,  0,170,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,170,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_156[] = {  9,  0,  0,  0,  0,  0,  0,170,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,170,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_157[] = {  9,  0,  0,  0,  0,  0,  0,170,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,170,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_158[] = {  9,  0,  0,  0,  0,  0,  0,170,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,170,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_159[] = {  9,  0,  0,  0,  0,  0,  0,170,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,130,  0,  0,  0,170,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_160[] = {  8,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_161[] = {  8,  0,  0,  0, 16, 16, 16, 16, 16, 16, 16,  0, 16,  0,  0};
static const unsigned char Fixed8x13_Character_162[] = {  8,  0,  0,  0,  0, 16, 56, 84, 80, 80, 84, 56, 16,  0,  0};
static const unsigned char Fixed8x13_Character_163[] = {  8,  0,  0,  0,220, 98, 32, 32, 32,112, 32, 34, 28,  0,  0};
static const unsigned char Fixed8x13_Character_164[] = {  8,  0,  0,  0,  0, 66, 60, 36, 36, 60, 66,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_165[] = {  8,  0,  0,  0, 16, 16,124, 16,124, 40, 68,130,130,  0,  0};
static const unsigned char Fixed8x13_Character_166[] = {  8,  0,  0,  0, 16, 16, 16, 16,  0, 16, 16, 16, 16,  0,  0};
static const unsigned char Fixed8x13_Character_167[] = {  8,  0,  0,  0, 24, 36,  4, 24, 36, 36, 24, 32, 36, 24,  0};
static const unsigned char Fixed8x13_Character_168[] = {  8,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,108,  0,  0};
static const unsigned char Fixed8x13_Character_169[] = {  8,  0,  0,  0,  0, 56, 68,146,170,162,170,146, 68, 56,  0};
static const unsigned char Fixed8x13_Character_170[] = {  8,  0,  0,  0,  0,  0,124,  0, 60, 68, 60,  4, 56,  0,  0};
static const unsigned char Fixed8x13_Character_171[] = {  8,  0,  0,  0,  0, 18, 36, 72,144, 72, 36, 18,  0,  0,  0};
static const unsigned char Fixed8x13_Character_172[] = {  8,  0,  0,  0,  0,  2,  2,  2,126,  0,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_173[] = {  8,  0,  0,  0,  0,  0,  0,  0, 60,  0,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_174[] = {  8,  0,  0,  0,  0, 56, 68,170,178,170,170,146, 68, 56,  0};
static const unsigned char Fixed8x13_Character_175[] = {  8,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,126,  0,  0};
static const unsigned char Fixed8x13_Character_176[] = {  8,  0,  0,  0,  0,  0,  0,  0,  0, 24, 36, 36, 24,  0,  0};
static const unsigned char Fixed8x13_Character_177[] = {  8,  0,  0,  0,  0,124,  0, 16, 16,124, 16, 16,  0,  0,  0};
static const unsigned char Fixed8x13_Character_178[] = {  8,  0,  0,  0,  0,  0,  0,  0,120, 64, 48,  8, 72, 48,  0};
static const unsigned char Fixed8x13_Character_179[] = {  8,  0,  0,  0,  0,  0,  0,  0, 48, 72,  8, 16, 72, 48,  0};
static const unsigned char Fixed8x13_Character_180[] = {  8,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 16,  8,  0};
static const unsigned char Fixed8x13_Character_181[] = {  8,  0,  0, 64, 90,102, 66, 66, 66, 66,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_182[] = {  8,  0,  0,  0, 20, 20, 20, 20, 52,116,116,116, 62,  0,  0};
static const unsigned char Fixed8x13_Character_183[] = {  8,  0,  0,  0,  0,  0,  0,  0, 24,  0,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_184[] = {  8,  0, 24,  8,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_185[] = {  8,  0,  0,  0,  0,  0,  0,  0,112, 32, 32, 32, 96, 32,  0};
static const unsigned char Fixed8x13_Character_186[] = {  8,  0,  0,  0,  0,  0,  0,120,  0, 48, 72, 72, 48,  0,  0};
static const unsigned char Fixed8x13_Character_187[] = {  8,  0,  0,  0,  0,144, 72, 36, 18, 36, 72,144,  0,  0,  0};
static const unsigned char Fixed8x13_Character_188[] = {  8,  0,  0,  0,  6, 26, 18, 10,230, 66, 64, 64,192, 64,  0};
static const unsigned char Fixed8x13_Character_189[] = {  8,  0,  0,  0, 30, 16, 12,  2,242, 76, 64, 64,192, 64,  0};
static const unsigned char Fixed8x13_Character_190[] = {  8,  0,  0,  0,  6, 26, 18, 10,102,146, 16, 32,144, 96,  0};
static const unsigned char Fixed8x13_Character_191[] = {  8,  0,  0,  0, 60, 66, 66, 64, 32, 16, 16,  0, 16,  0,  0};
static const unsigned char Fixed8x13_Character_192[] = {  8,  0,  0,  0, 66, 66,126, 66, 66, 36, 24,  0,  8, 16,  0};
static const unsigned char Fixed8x13_Character_193[] = {  8,  0,  0,  0, 66, 66,126, 66, 66, 36, 24,  0, 16,  8,  0};
static const unsigned char Fixed8x13_Character_194[] = {  8,  0,  0,  0, 66, 66,126, 66, 66, 36, 24,  0, 36, 24,  0};
static const unsigned char Fixed8x13_Character_195[] = {  8,  0,  0,  0, 66, 66,126, 66, 66, 36, 24,  0, 76, 50,  0};
static const unsigned char Fixed8x13_Character_196[] = {  8,  0,  0,  0, 66, 66,126, 66, 66, 36, 24,  0, 36, 36,  0};
static const unsigned char Fixed8x13_Character_197[] = {  8,  0,  0,  0, 66, 66,126, 66, 66, 36, 24, 24, 36, 24,  0};
static const unsigned char Fixed8x13_Character_198[] = {  8,  0,  0,  0,158,144,144,240,156,144,144,144,110,  0,  0};
static const unsigned char Fixed8x13_Character_199[] = {  8,  0, 16,  8, 60, 66, 64, 64, 64, 64, 64, 66, 60,  0,  0};
static const unsigned char Fixed8x13_Character_200[] = {  8,  0,  0,  0,126, 64, 64,120, 64, 64,126,  0,  8, 16,  0};
static const unsigned char Fixed8x13_Character_201[] = {  8,  0,  0,  0,126, 64, 64,120, 64, 64,126,  0, 16,  8,  0};
static const unsigned char Fixed8x13_Character_202[] = {  8,  0,  0,  0,126, 64, 64,120, 64, 64,126,  0, 36, 24,  0};
static const unsigned char Fixed8x13_Character_203[] = {  8,  0,  0,  0,126, 64, 64,120, 64, 64,126,  0, 36, 36,  0};
static const unsigned char Fixed8x13_Character_204[] = {  8,  0,  0,  0,124, 16, 16, 16, 16, 16,124,  0, 16, 32,  0};
static const unsigned char Fixed8x13_Character_205[] = {  8,  0,  0,  0,124, 16, 16, 16, 16, 16,124,  0, 16,  8,  0};
static const unsigned char Fixed8x13_Character_206[] = {  8,  0,  0,  0,124, 16, 16, 16, 16, 16,124,  0, 36, 24,  0};
static const unsigned char Fixed8x13_Character_207[] = {  8,  0,  0,  0,124, 16, 16, 16, 16, 16,124,  0, 40, 40,  0};
static const unsigned char Fixed8x13_Character_208[] = {  8,  0,  0,  0,120, 68, 66, 66,226, 66, 66, 68,120,  0,  0};
static const unsigned char Fixed8x13_Character_209[] = {  8,  0,  0,  0,130,134,138,146,162,194,130,  0,152,100,  0};
static const unsigned char Fixed8x13_Character_210[] = {  8,  0,  0,  0,124,130,130,130,130,130,124,  0, 16, 32,  0};
static const unsigned char Fixed8x13_Character_211[] = {  8,  0,  0,  0,124,130,130,130,130,130,124,  0, 16,  8,  0};
static const unsigned char Fixed8x13_Character_212[] = {  8,  0,  0,  0,124,130,130,130,130,130,124,  0, 36, 24,  0};
static const unsigned char Fixed8x13_Character_213[] = {  8,  0,  0,  0,124,130,130,130,130,130,124,  0,152,100,  0};
static const unsigned char Fixed8x13_Character_214[] = {  8,  0,  0,  0,124,130,130,130,130,130,124,  0, 40, 40,  0};
static const unsigned char Fixed8x13_Character_215[] = {  8,  0,  0,  0,  0, 66, 36, 24, 24, 36, 66,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_216[] = {  8,  0,  0, 64, 60, 98, 82, 82, 82, 74, 74, 70, 60,  2,  0};
static const unsigned char Fixed8x13_Character_217[] = {  8,  0,  0,  0, 60, 66, 66, 66, 66, 66, 66,  0,  8, 16,  0};
static const unsigned char Fixed8x13_Character_218[] = {  8,  0,  0,  0, 60, 66, 66, 66, 66, 66, 66,  0, 16,  8,  0};
static const unsigned char Fixed8x13_Character_219[] = {  8,  0,  0,  0, 60, 66, 66, 66, 66, 66, 66,  0, 36, 24,  0};
static const unsigned char Fixed8x13_Character_220[] = {  8,  0,  0,  0, 60, 66, 66, 66, 66, 66, 66,  0, 36, 36,  0};
static const unsigned char Fixed8x13_Character_221[] = {  8,  0,  0,  0, 16, 16, 16, 16, 40, 68, 68,  0, 16,  8,  0};
static const unsigned char Fixed8x13_Character_222[] = {  8,  0,  0,  0, 64, 64, 64,124, 66, 66, 66,124, 64,  0,  0};
static const unsigned char Fixed8x13_Character_223[] = {  8,  0,  0,  0, 92, 66, 66, 76, 80, 72, 68, 68, 56,  0,  0};
static const unsigned char Fixed8x13_Character_224[] = {  8,  0,  0,  0, 58, 70, 66, 62,  2, 60,  0,  0,  8, 16,  0};
static const unsigned char Fixed8x13_Character_225[] = {  8,  0,  0,  0, 58, 70, 66, 62,  2, 60,  0,  0,  8,  4,  0};
static const unsigned char Fixed8x13_Character_226[] = {  8,  0,  0,  0, 58, 70, 66, 62,  2, 60,  0,  0, 36, 24,  0};
static const unsigned char Fixed8x13_Character_227[] = {  8,  0,  0,  0, 58, 70, 66, 62,  2, 60,  0,  0, 76, 50,  0};
static const unsigned char Fixed8x13_Character_228[] = {  8,  0,  0,  0, 58, 70, 66, 62,  2, 60,  0,  0, 36, 36,  0};
static const unsigned char Fixed8x13_Character_229[] = {  8,  0,  0,  0, 58, 70, 66, 62,  2, 60,  0, 24, 36, 24,  0};
static const unsigned char Fixed8x13_Character_230[] = {  8,  0,  0,  0,108,146,144,124, 18,108,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_231[] = {  8,  0, 16,  8, 60, 66, 64, 64, 66, 60,  0,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_232[] = {  8,  0,  0,  0, 60, 66, 64,126, 66, 60,  0,  0,  8, 16,  0};
static const unsigned char Fixed8x13_Character_233[] = {  8,  0,  0,  0, 60, 66, 64,126, 66, 60,  0,  0, 16,  8,  0};
static const unsigned char Fixed8x13_Character_234[] = {  8,  0,  0,  0, 60, 66, 64,126, 66, 60,  0,  0, 36, 24,  0};
static const unsigned char Fixed8x13_Character_235[] = {  8,  0,  0,  0, 60, 66, 64,126, 66, 60,  0,  0, 36, 36,  0};
static const unsigned char Fixed8x13_Character_236[] = {  8,  0,  0,  0,124, 16, 16, 16, 16, 48,  0,  0, 16, 32,  0};
static const unsigned char Fixed8x13_Character_237[] = {  8,  0,  0,  0,124, 16, 16, 16, 16, 48,  0,  0, 32, 16,  0};
static const unsigned char Fixed8x13_Character_238[] = {  8,  0,  0,  0,124, 16, 16, 16, 16, 48,  0,  0, 72, 48,  0};
static const unsigned char Fixed8x13_Character_239[] = {  8,  0,  0,  0,124, 16, 16, 16, 16, 48,  0,  0, 40, 40,  0};
static const unsigned char Fixed8x13_Character_240[] = {  8,  0,  0,  0, 60, 66, 66, 66, 66, 60,  4, 40, 24, 36,  0};
static const unsigned char Fixed8x13_Character_241[] = {  8,  0,  0,  0, 66, 66, 66, 66, 98, 92,  0,  0, 76, 50,  0};
static const unsigned char Fixed8x13_Character_242[] = {  8,  0,  0,  0, 60, 66, 66, 66, 66, 60,  0,  0, 16, 32,  0};
static const unsigned char Fixed8x13_Character_243[] = {  8,  0,  0,  0, 60, 66, 66, 66, 66, 60,  0,  0, 16,  8,  0};
static const unsigned char Fixed8x13_Character_244[] = {  8,  0,  0,  0, 60, 66, 66, 66, 66, 60,  0,  0, 36, 24,  0};
static const unsigned char Fixed8x13_Character_245[] = {  8,  0,  0,  0, 60, 66, 66, 66, 66, 60,  0,  0, 76, 50,  0};
static const unsigned char Fixed8x13_Character_246[] = {  8,  0,  0,  0, 60, 66, 66, 66, 66, 60,  0,  0, 36, 36,  0};
static const unsigned char Fixed8x13_Character_247[] = {  8,  0,  0,  0,  0, 16, 16,  0,124,  0, 16, 16,  0,  0,  0};
static const unsigned char Fixed8x13_Character_248[] = {  8,  0,  0, 64, 60, 98, 82, 74, 70, 60,  2,  0,  0,  0,  0};
static const unsigned char Fixed8x13_Character_249[] = {  8,  0,  0,  0, 58, 68, 68, 68, 68, 68,  0,  0, 16, 32,  0};
static const unsigned char Fixed8x13_Character_250[] = {  8,  0,  0,  0, 58, 68, 68, 68, 68, 68,  0,  0, 16,  8,  0};
static const unsigned char Fixed8x13_Character_251[] = {  8,  0,  0,  0, 58, 68, 68, 68, 68, 68,  0,  0, 36, 24,  0};
static const unsigned char Fixed8x13_Character_252[] = {  8,  0,  0,  0, 58, 68, 68, 68, 68, 68,  0,  0, 40, 40,  0};
static const unsigned char Fixed8x13_Character_253[] = {  8,  0, 60, 66,  2, 58, 70, 66, 66, 66,  0,  0, 16,  8,  0};
static const unsigned char Fixed8x13_Character_254[] = {  8,  0, 64, 64, 92, 98, 66, 66, 98, 92, 64, 64,  0,  0,  0};
static const unsigned char Fixed8x13_Character_255[] = {  8,  0, 60, 66,  2, 58, 70, 66, 66, 66,  0,  0, 36, 36,  0};

/* The font characters mapping: */
static const unsigned char* Fixed8x13_Character_Map[] = {Fixed8x13_Character_000,Fixed8x13_Character_001,Fixed8x13_Character_002,Fixed8x13_Character_003,Fixed8x13_Character_004,Fixed8x13_Character_005,Fixed8x13_Character_006,Fixed8x13_Character_007,Fixed8x13_Character_008,Fixed8x13_Character_009,Fixed8x13_Character_010,Fixed8x13_Character_011,Fixed8x13_Character_012,Fixed8x13_Character_013,Fixed8x13_Character_014,Fixed8x13_Character_015,
                                                   Fixed8x13_Character_016,Fixed8x13_Character_017,Fixed8x13_Character_018,Fixed8x13_Character_019,Fixed8x13_Character_020,Fixed8x13_Character_021,Fixed8x13_Character_022,Fixed8x13_Character_023,Fixed8x13_Character_024,Fixed8x13_Character_025,Fixed8x13_Character_026,Fixed8x13_Character_027,Fixed8x13_Character_028,Fixed8x13_Character_029,Fixed8x13_Character_030,Fixed8x13_Character_031,
                                                   Fixed8x13_Character_032,Fixed8x13_Character_033,Fixed8x13_Character_034,Fixed8x13_Character_035,Fixed8x13_Character_036,Fixed8x13_Character_037,Fixed8x13_Character_038,Fixed8x13_Character_039,Fixed8x13_Character_040,Fixed8x13_Character_041,Fixed8x13_Character_042,Fixed8x13_Character_043,Fixed8x13_Character_044,Fixed8x13_Character_045,Fixed8x13_Character_046,Fixed8x13_Character_047,
                                                   Fixed8x13_Character_048,Fixed8x13_Character_049,Fixed8x13_Character_050,Fixed8x13_Character_051,Fixed8x13_Character_052,Fixed8x13_Character_053,Fixed8x13_Character_054,Fixed8x13_Character_055,Fixed8x13_Character_056,Fixed8x13_Character_057,Fixed8x13_Character_058,Fixed8x13_Character_059,Fixed8x13_Character_060,Fixed8x13_Character_061,Fixed8x13_Character_062,Fixed8x13_Character_063,
                                                   Fixed8x13_Character_064,Fixed8x13_Character_065,Fixed8x13_Character_066,Fixed8x13_Character_067,Fixed8x13_Character_068,Fixed8x13_Character_069,Fixed8x13_Character_070,Fixed8x13_Character_071,Fixed8x13_Character_072,Fixed8x13_Character_073,Fixed8x13_Character_074,Fixed8x13_Character_075,Fixed8x13_Character_076,Fixed8x13_Character_077,Fixed8x13_Character_078,Fixed8x13_Character_079,
                                                   Fixed8x13_Character_080,Fixed8x13_Character_081,Fixed8x13_Character_082,Fixed8x13_Character_083,Fixed8x13_Character_084,Fixed8x13_Character_085,Fixed8x13_Character_086,Fixed8x13_Character_087,Fixed8x13_Character_088,Fixed8x13_Character_089,Fixed8x13_Character_090,Fixed8x13_Character_091,Fixed8x13_Character_092,Fixed8x13_Character_093,Fixed8x13_Character_094,Fixed8x13_Character_095,
                                                   Fixed8x13_Character_096,Fixed8x13_Character_097,Fixed8x13_Character_098,Fixed8x13_Character_099,Fixed8x13_Character_100,Fixed8x13_Character_101,Fixed8x13_Character_102,Fixed8x13_Character_103,Fixed8x13_Character_104,Fixed8x13_Character_105,Fixed8x13_Character_106,Fixed8x13_Character_107,Fixed8x13_Character_108,Fixed8x13_Character_109,Fixed8x13_Character_110,Fixed8x13_Character_111,
                                                   Fixed8x13_Character_112,Fixed8x13_Character_113,Fixed8x13_Character_114,Fixed8x13_Character_115,Fixed8x13_Character_116,Fixed8x13_Character_117,Fixed8x13_Character_118,Fixed8x13_Character_119,Fixed8x13_Character_120,Fixed8x13_Character_121,Fixed8x13_Character_122,Fixed8x13_Character_123,Fixed8x13_Character_124,Fixed8x13_Character_125,Fixed8x13_Character_126,Fixed8x13_Character_032,
                                                   Fixed8x13_Character_032,Fixed8x13_Character_032,Fixed8x13_Character_032,Fixed8x13_Character_032,Fixed8x13_Character_032,Fixed8x13_Character_032,Fixed8x13_Character_032,Fixed8x13_Character_032,Fixed8x13_Character_032,Fixed8x13_Character_032,Fixed8x13_Character_032,Fixed8x13_Character_032,Fixed8x13_Character_032,Fixed8x13_Character_032,Fixed8x13_Character_032,Fixed8x13_Character_032,
                                                   Fixed8x13_Character_032,Fixed8x13_Character_032,Fixed8x13_Character_032,Fixed8x13_Character_032,Fixed8x13_Character_032,Fixed8x13_Character_032,Fixed8x13_Character_032,Fixed8x13_Character_032,Fixed8x13_Character_032,Fixed8x13_Character_032,Fixed8x13_Character_032,Fixed8x13_Character_032,Fixed8x13_Character_032,Fixed8x13_Character_032,Fixed8x13_Character_032,Fixed8x13_Character_032,
                                                   Fixed8x13_Character_160,Fixed8x13_Character_161,Fixed8x13_Character_162,Fixed8x13_Character_163,Fixed8x13_Character_164,Fixed8x13_Character_165,Fixed8x13_Character_166,Fixed8x13_Character_167,Fixed8x13_Character_168,Fixed8x13_Character_169,Fixed8x13_Character_170,Fixed8x13_Character_171,Fixed8x13_Character_172,Fixed8x13_Character_173,Fixed8x13_Character_174,Fixed8x13_Character_175,
                                                   Fixed8x13_Character_176,Fixed8x13_Character_177,Fixed8x13_Character_178,Fixed8x13_Character_179,Fixed8x13_Character_180,Fixed8x13_Character_181,Fixed8x13_Character_182,Fixed8x13_Character_183,Fixed8x13_Character_184,Fixed8x13_Character_185,Fixed8x13_Character_186,Fixed8x13_Character_187,Fixed8x13_Character_188,Fixed8x13_Character_189,Fixed8x13_Character_190,Fixed8x13_Character_191,
                                                   Fixed8x13_Character_192,Fixed8x13_Character_193,Fixed8x13_Character_194,Fixed8x13_Character_195,Fixed8x13_Character_196,Fixed8x13_Character_197,Fixed8x13_Character_198,Fixed8x13_Character_199,Fixed8x13_Character_200,Fixed8x13_Character_201,Fixed8x13_Character_202,Fixed8x13_Character_203,Fixed8x13_Character_204,Fixed8x13_Character_205,Fixed8x13_Character_206,Fixed8x13_Character_207,
                                                   Fixed8x13_Character_208,Fixed8x13_Character_209,Fixed8x13_Character_210,Fixed8x13_Character_211,Fixed8x13_Character_212,Fixed8x13_Character_213,Fixed8x13_Character_214,Fixed8x13_Character_215,Fixed8x13_Character_216,Fixed8x13_Character_217,Fixed8x13_Character_218,Fixed8x13_Character_219,Fixed8x13_Character_220,Fixed8x13_Character_221,Fixed8x13_Character_222,Fixed8x13_Character_223,
                                                   Fixed8x13_Character_224,Fixed8x13_Character_225,Fixed8x13_Character_226,Fixed8x13_Character_227,Fixed8x13_Character_228,Fixed8x13_Character_229,Fixed8x13_Character_230,Fixed8x13_Character_231,Fixed8x13_Character_232,Fixed8x13_Character_233,Fixed8x13_Character_234,Fixed8x13_Character_235,Fixed8x13_Character_236,Fixed8x13_Character_237,Fixed8x13_Character_238,Fixed8x13_Character_239,
                                                   Fixed8x13_Character_240,Fixed8x13_Character_241,Fixed8x13_Character_242,Fixed8x13_Character_243,Fixed8x13_Character_244,Fixed8x13_Character_245,Fixed8x13_Character_246,Fixed8x13_Character_247,Fixed8x13_Character_248,Fixed8x13_Character_249,Fixed8x13_Character_250,Fixed8x13_Character_251,Fixed8x13_Character_252,Fixed8x13_Character_253,Fixed8x13_Character_254,Fixed8x13_Character_255,NULL};

/* The font structure: */
static const SFG_Font fgFontFixed8x13 = { "-misc-fixed-medium-r-normal--13-120-75-75-C-80-iso8859-1", 256, 14, Fixed8x13_Character_Map, 0, 3 };

