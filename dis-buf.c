/* Disassemble from a buffer, for GNU.
   Copyright (C) 1993, 1994 Free Software Foundation, Inc.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#include "dis-asm.h"
#include <errno.h>

/* Get LENGTH bytes from info's buffer, at target address memaddr.
   Transfer them to myaddr.  */
int
buffer_read_memory (memaddr, myaddr, length, info)
     bfd_vma memaddr;
     bfd_byte *myaddr;
     int length;
     struct disassemble_info *info;
{
  if (memaddr < info->buffer_vma
      || memaddr + length > info->buffer_vma + info->buffer_length)
    /* Out of bounds.  Use EIO because GDB uses it.  */
    return EIO;
  memcpy (myaddr, info->buffer + (memaddr - info->buffer_vma), length);
  return 0;
}

/* Print an error message.  We can assume that this is in response to
   an error return from buffer_read_memory.  */
void
perror_memory (status, memaddr, info)
     int status;
     bfd_vma memaddr;
     struct disassemble_info *info;
{
  if (status != EIO)
    /* Can't happen.  */
    (*info->fprintf_func) (info->stream, "Unknown error %d\n", status);
  else
    /* Actually, address between memaddr and memaddr + len was
       out of bounds.  */
    (*info->fprintf_func) (info->stream,
			   "Address 0x%x is out of bounds.\n", memaddr);
}

/* This could be in a separate file, to save miniscule amounts of space
   in statically linked executables.  */

/* Just print the address is hex.  This is included for completeness even
   though both GDB and objdump provide their own (to print symbolic
   addresses).  */

void
generic_print_address (addr, info)
     bfd_vma addr;
     struct disassemble_info *info;
{
  (*info->fprintf_func) (info->stream, "0x%x", addr);
}

/* Just return the given address.  */

int
generic_symbol_at_address (addr, info)
     bfd_vma addr;
     struct disassemble_info * info;
{
  return 1;
}
