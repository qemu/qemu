/*
 *  Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

#ifndef _PRINTINSN_H_
#define _PRINTINSN_H_ 1

/*
 * printinsn.h
 * 
 * How to print out an instruction
 */


#include <stdio.h>
#include "insn.h"

void fprintinsn(FILE * file, insn_t * insn);
void printinsn(insn_t * insn);
void snprintinsn(char *buf, int n, insn_t * insn);
void snprint_a_pkt(char *buf, int n, packet_t * pkt);
void gdb_print_pkt(packet_t * pkt, struct ThreadState * thread);
void snprint_an_insn_tag(char *buf, int n, insn_t* insn);

/* Setting bits in the fields will print/not print some data
0: slot and tag information
1: EA and PA for memory accesses */
void snprint_a_pkt_fields(char *buf, int n, packet_t * pkt, struct ThreadState * thread, size4u_t fields);

#endif							/* _PRINTINSN_H_ */
