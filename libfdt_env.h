/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright IBM Corp. 2008
 * Authors: Hollis Blanchard <hollisb@us.ibm.com>
 *
 */

#ifndef _LIBFDT_ENV_H
#define _LIBFDT_ENV_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <endian.h>
#include <byteswap.h>

#if __BYTE_ORDER == __BIG_ENDIAN
#define fdt32_to_cpu(x)		(x)
#define cpu_to_fdt32(x)		(x)
#define fdt64_to_cpu(x)		(x)
#define cpu_to_fdt64(x)		(x)
#else
#define fdt32_to_cpu(x)		(bswap_32((x)))
#define cpu_to_fdt32(x)		(bswap_32((x)))
#define fdt64_to_cpu(x)		(bswap_64((x)))
#define cpu_to_fdt64(x)		(bswap_64((x)))
#endif

#endif /* _LIBFDT_ENV_H */
