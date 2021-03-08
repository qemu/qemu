/*
 *  Copyright(c) 2019-2021 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

#include <stdio.h>

int gA[401];
int gB[401];
int gC[401];

void vector_add_int()
{
  int i;
  for (i = 0; i < 400; i++) {
    gA[i] = gB[i] + gC[i];
  }
}

int main()
{
  int error = 0;
  int i;
  for (i = 0; i < 400; i++) {
    gB[i] = i * 2;
    gC[i] = i * 3;
  }
  gA[400] = 17;
  vector_add_int();
  for (i = 0; i < 400; i++) {
    if (gA[i] != i * 5) {
        error++;
        printf("ERROR: gB[%d] = %d\t", i, gB[i]);
        printf("gC[%d] = %d\t", i, gC[i]);
        printf("gA[%d] = %d\n", i, gA[i]);
    }
  }
  if (gA[400] != 17) {
    error++;
    printf("ERROR: Overran the buffer\n");
  }
  if (!error) {
    printf("PASS\n");
    return 0;
  } else {
    printf("FAIL\n");
    return 1;
  }
}
