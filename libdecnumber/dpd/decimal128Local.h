/* Local definitions for use with the decNumber C Library.
   Copyright (C) 2007, 2009 Free Software Foundation, Inc.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it under
   the terms of the GNU General Public License as published by the Free
   Software Foundation; either version 3, or (at your option) any later
   version.

   GCC is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or
   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
   for more details.

Under Section 7 of GPL version 3, you are granted additional
permissions described in the GCC Runtime Library Exception, version
3.1, as published by the Free Software Foundation.

You should have received a copy of the GNU General Public License and
a copy of the GCC Runtime Library Exception along with this program;
see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
<http://www.gnu.org/licenses/>.  */

#if !defined(DECIMAL128LOCAL)

/* The compiler needs sign manipulation functions for decimal128 which
   are not part of the decNumber package.  */

/* Set sign; this assumes the sign was previously zero.  */
#define decimal128SetSign(d,b) \
  { (d)->bytes[WORDS_BIGENDIAN ? 0 : 15] |= ((unsigned) (b) << 7); }

/* Clear sign.  */
#define decimal128ClearSign(d) \
  { (d)->bytes[WORDS_BIGENDIAN ? 0 : 15] &= ~0x80; }

/* Flip sign.  */
#define decimal128FlipSign(d) \
  { (d)->bytes[WORDS_BIGENDIAN ? 0 : 15] ^= 0x80; }

#endif
