/*
 * Stubs for AMD IOMMU emulation
 *
 * Copyright (C) 2023 Bui Quang Minh <minhquangbui99@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "amd_iommu.h"

uint64_t amdvi_extended_feature_register(AMDVIState *s)
{
    return AMDVI_DEFAULT_EXT_FEATURES;
}
