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

#define SYS_write		 64
#define SYS_exit_group           94
#define SYS_exit                 93

#define FD_STDOUT                1

	.type	str,@object
	.section	.rodata
str:
	.string	"Hello!\n"
	.size	str, 8

.text
.global _start
_start:
	r6 = #SYS_write
	r0 = #FD_STDOUT
	r1 = ##str
	r2 = #7
	trap0(#1)

	r0 = #0
	r6 = #SYS_exit_group
	trap0(#1)

.section ".note.ABI-tag", "a"
.align 4
.long 1f - 0f          /* name length */
.long 3f - 2f          /* data length */
.long  1               /* note type */

/*
 * vendor name seems like this should be MUSL but lldb doesn't agree.
 */
0:     .asciz "GNU"
1:     .align 4
2:     .long 0 /* linux */
       .long 3,0,0
3:     .align 4
