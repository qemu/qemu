/*
   SPARC micro operations

   Copyright (C) 2003 Thomas M. Ogrisegg <tom@fnord.at>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "exec.h"
#include "helper.h"

/* Load and store */
#define MEMSUFFIX _raw
#include "op_mem.h"
#if !defined(CONFIG_USER_ONLY)
#define MEMSUFFIX _user
#include "op_mem.h"

#define MEMSUFFIX _kernel
#include "op_mem.h"

#ifdef TARGET_SPARC64
#define MEMSUFFIX _hypv
#include "op_mem.h"
#endif
#endif

#define CHECK_ALIGN_OP(align)                           \
    void OPPROTO op_check_align_T0_ ## align (void)     \
    {                                                   \
        if (T0 & align)                                 \
            raise_exception(TT_UNALIGNED);              \
        FORCE_RET();                                    \
    }

CHECK_ALIGN_OP(1)
CHECK_ALIGN_OP(3)
CHECK_ALIGN_OP(7)
