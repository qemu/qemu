/*
 * QEMU 3Dfx Glide Pass-Through
 *
 *  Copyright (c) 2018-2020
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library;
 * if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _VERTEX3X_H
#define _VERTEX3X_H

static const int slen[] = 
                    { 8, 4, 4, 4, 4, 4, 12, 4, 8, 4, 8, 4 };
static int vlut[] = { 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0 };
static void vlut_vvars(int param, int offs, int mode)
{
    vlut[GR_PARAM_IDX(param)] = (mode)? offs:0;
    if (GR_PARAM_PARGB == param) {
        vlut[GR_PARAM_IDX(GR_PARAM_A)] = 0;
        vlut[GR_PARAM_IDX(GR_PARAM_RGB)] = 0;
    }
    if (GR_PARAM_RGB == param)
        vlut[GR_PARAM_IDX(GR_PARAM_PARGB)] = 0;
}
static int size_vertex3x(void)
{
    int n = sizeof(slen) / sizeof(int), ret = slen[0];
    for (int i = 0; i < n; i++)
        ret = (vlut[i] && ((vlut[i] + slen[i]) > ret))? (vlut[i] + slen[i]):ret;

    return ret;
}

#endif //_VERTEX3X_H

