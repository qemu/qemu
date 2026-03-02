/*
 * Copyright (C) 2016 Veertu Inc,
 * Copyright (C) 2017 Google Inc,
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef X86_MMU_H
#define X86_MMU_H

#define PT_PRESENT      (1 << 0)
#define PT_WRITE        (1 << 1)
#define PT_USER         (1 << 2)
#define PT_WT           (1 << 3)
#define PT_CD           (1 << 4)
#define PT_ACCESSED     (1 << 5)
#define PT_DIRTY        (1 << 6)
#define PT_PS           (1 << 7)
#define PT_GLOBAL       (1 << 8)
#define PT_NX           (1llu << 63)

typedef enum MMUTranslateFlags {
    MMU_TRANSLATE_VALIDATE_WRITE = BIT(1),
    MMU_TRANSLATE_VALIDATE_EXECUTE = BIT(2),
    MMU_TRANSLATE_PRIV_CHECKS_EXEMPT = BIT(3)
} MMUTranslateFlags;

typedef enum MMUTranslateResult {
    MMU_TRANSLATE_SUCCESS = 0,
    MMU_TRANSLATE_PAGE_NOT_MAPPED = 1,
    MMU_TRANSLATE_PRIV_VIOLATION = 2,
    MMU_TRANSLATE_INVALID_PT_FLAGS = 3,
    MMU_TRANSLATE_GPA_UNMAPPED = 4,
    MMU_TRANSLATE_GPA_NO_READ_ACCESS = 5,
    MMU_TRANSLATE_GPA_NO_WRITE_ACCESS = 6
} MMUTranslateResult;

MMUTranslateResult mmu_gva_to_gpa(CPUState *cpu, target_ulong gva, uint64_t *gpa, MMUTranslateFlags flags);

/* Thin wrappers x86_write_mem_ex/x86_read_mem_ex for code readability */
MMUTranslateResult x86_write_mem(CPUState *cpu, void *data, target_ulong gva, int bytes);
MMUTranslateResult x86_read_mem(CPUState *cpu, void *data, target_ulong gva, int bytes);

MMUTranslateResult x86_write_mem_priv(CPUState *cpu, void *data, target_ulong gva, int bytes);
MMUTranslateResult x86_read_mem_priv(CPUState *cpu, void *data, target_ulong gva, int bytes);


#endif /* X86_MMU_H */
