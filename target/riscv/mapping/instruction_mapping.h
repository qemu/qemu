/*
 * RISC-V emulation for qemu: Instruction mapping for Sail Instruction Emulation and Qemu
 *
 * Copyright (c) 2024 Vedant Tewari, vtewari@uwm.edu
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef INSTRUCTION_MAPPING_H
#define INSTRUCTION_MAPPING_H

#include <stddef.h>

typedef struct InstructionMapping {
		char* sail_function;
		char* qemu_function;
		struct InstructionMapping* next;
} InstructionMapping;

typedef struct {
		char* key;
		InstructionMapping* value;
} HashmapEntry;

typedef struct {
		HashmapEntry* entries;
		size_t size;
} Hashmap;

Hashmap* init_hashmap(size_t size);

void insert_hashmap(Hashmap* hashmap, const char* key, char* sail_function, char* qemu_function);

char* read_file(const char* filename);

char* extract_sail_function(const char* content, const char* keyword);

char* extract_qemu_function(const char* content, const char* keyword);

InstructionMapping* create_instruction_mapping(const char* sail_file, const char* qemu_file, const char* keyword);

void perform_instruction_mapping(const char* sail_file, const char* qemu_file, const char* keyword, Hashmap* hashmap);

void print_hashmap(const Hashmap* hashmap);

char** extract_sail_instructions(const char* content, size_t* count);

char** extract_qemu_instructions(const char* content, size_t* count);

char** find_common_instructions(char** sail_instructions, size_t sail_count, char** qemu_instructions, size_t qemu_count, size_t* common_count);

void to_uppercase(char* str);
void to_lowercase(char* str);

#endif

