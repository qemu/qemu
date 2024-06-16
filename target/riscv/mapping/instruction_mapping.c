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

#include "instruction_mapping.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void to_uppercase(char* str) {
	for (; *str; ++str) *str = toupper(*str);
}

void to_lowercase(char* str) {
	for (; *str; ++str) *str = tolower(*str);
}

char* trim_whitespace(char* str) {
		char* end;
		while (isspace((unsigned char)*str)) str++;
		if (*str == 0)
				return str;
		end = str + strlen(str) - 1;
		while (end > str && isspace((unsigned char)*end)) end--;
		end[1] = '\0';

		return str;
}

char* read_file(const char* filename) {
		FILE* file = fopen(filename, "r");
		if (!file) {
				fprintf(stderr, "Could not open file %s\n", filename);
				return NULL;
		}

		fseek(file, 0, SEEK_END);
		long length = ftell(file);
		fseek(file, 0, SEEK_SET);

		char* content = (char*)malloc(length + 1);
		if (!content) {
				fprintf(stderr, "Could not allocate memory for file content\n");
				fclose(file);
				return NULL;
		}

		fread(content, 1, length, file);
		content[length] = '\0';

		fclose(file);
		return content;
}

RelevantCase* extract_relevant_case(const char* sail_function, const char* keyword) {
		char* uppercase_keyword = strdup(keyword);
		if (!uppercase_keyword) {
				fprintf(stderr, "Could not allocate memory for uppercase conversion\n");
				return NULL;
		}
		to_uppercase(uppercase_keyword);

		char* lowercase_keyword = strdup(keyword);
		if (!lowercase_keyword) {
				fprintf(stderr, "Could not allocate memory for lowercase conversion\n");
				free(uppercase_keyword);
				return NULL;
		}
		to_lowercase(lowercase_keyword);

		const char* match_start = strstr(sail_function, "match op {");
		if (!match_start) {
				free(uppercase_keyword);
				free(lowercase_keyword);
				return NULL;
		}

		const char* match_end = strstr(match_start, "};");
		if (!match_end) {
				free(uppercase_keyword);
				free(lowercase_keyword);
				return NULL;
		}

		char* keyword_case = strstr(match_start, "RISCV_");
		while (keyword_case && keyword_case < match_end) {
				char* keyword_case_end = strchr(keyword_case, ',');
				if (!keyword_case_end) {
						keyword_case_end = strchr(keyword_case, '}');
				}
				if (keyword_case_end) {
						size_t case_length = keyword_case_end - keyword_case;
						char* extracted_case = strndup(keyword_case, case_length);
						if (extracted_case) {
								char* case_keyword = extracted_case + strlen("RISCV_");
								to_lowercase(case_keyword);
								case_keyword = trim_whitespace(case_keyword);
								if (strstr(case_keyword, lowercase_keyword)) {
										RelevantCase* relevant_case = (RelevantCase*)malloc(sizeof(RelevantCase));
										if (!relevant_case) {
												fprintf(stderr, "Could not allocate memory for relevant case\n");
												free(uppercase_keyword);
												free(lowercase_keyword);
												free(extracted_case);
												return NULL;
										}
										relevant_case->keyword = strdup(keyword);
										relevant_case->extracted_case = extracted_case;
										free(uppercase_keyword);
										free(lowercase_keyword);
										return relevant_case;
								}
								free(extracted_case);
						}
				}
				keyword_case = strstr(keyword_case_end, "RISCV_");
		}

		free(uppercase_keyword);
		free(lowercase_keyword);
		return NULL;
}

char* get_extracted_case(const RelevantCase* relevant_case) {
		if (relevant_case) {
				return relevant_case->extracted_case;
		}
		return NULL;
}

char* get_rhs_of_extracted_case(const char* extracted_case) {
		const char* rhs_start = strstr(extracted_case, "=>");
		if (rhs_start) {
				rhs_start += 2;
				while (isspace((unsigned char)*rhs_start)) rhs_start++;
				return strdup(rhs_start);
		}
		return NULL;
}

char* extract_sail_function(const char* content, const char* keyword) {
		char* uppercase_keyword = strdup(keyword);
		if (!uppercase_keyword) {
				fprintf(stderr, "Could not allocate memory for uppercase conversion\n");
				return NULL;
		}
		to_uppercase(uppercase_keyword);

		char* start = strstr(content, "function clause execute");
		while (start) {
				char* end = strstr(start, "RETIRE_SUCCESS");
				if (end) {
						end += strlen("RETIRE_SUCCESS");
						size_t length = end - start;
						char* function = (char*)malloc(length + 1);
						if (!function) {
								fprintf(stderr, "Could not allocate memory for Sail function\n");
								free(uppercase_keyword);
								return NULL;
						}
						strncpy(function, start, length);
						function[length] = '\0';
						if (strstr(function, uppercase_keyword)) {
								free(uppercase_keyword);
								return function;
						}
						free(function);
				}
				start = strstr(start + 1, "function clause execute");
		}
		free(uppercase_keyword);
		return NULL;
}

char* replace_match_with_rhs(char* sail_function, const char* keyword) {
		RelevantCase* relevant_case = extract_relevant_case(sail_function, keyword);
		if (relevant_case) {
				char* rhs = get_rhs_of_extracted_case(relevant_case->extracted_case);
				if (rhs) {
						char* match_start = strstr(sail_function, "match op {");
						char* match_end = strstr(match_start, "};");
						if (match_start && match_end && match_end > match_start) {
								size_t before_match_length = match_start - sail_function;
								size_t after_match_length = strlen(match_end + 2);
								size_t new_length = before_match_length + strlen("let ret : xlenbits = ") + strlen(rhs) + after_match_length + 1;

								char* new_function = (char*)malloc(new_length);
								if (new_function) {
										strncpy(new_function, sail_function, before_match_length);
										new_function[before_match_length] = '\0';
										strcat(new_function, rhs);
										strcat(new_function, ";\n");
										strcat(new_function, match_end + 2);
								} else {
										fprintf(stderr, "Could not allocate memory for new function\n");
								}
								free(rhs);
								return new_function;
						}
						free(rhs);
				}
				free(relevant_case->keyword);
				free(relevant_case->extracted_case);
				free(relevant_case);
		}
		return sail_function;
}

char* extract_qemu_function(const char* content, const char* keyword) {
		char buffer[256];
		snprintf(buffer, sizeof(buffer), "static bool trans_%s", keyword);
		size_t keyword_length = strlen(keyword);

		char* start = strstr(content, "static bool trans_");
		while (start) {
				char* function_name_start = start + strlen("static bool trans_");
				char* function_name_end = strstr(function_name_start, "(");
				if (function_name_end) {
						size_t function_name_length = function_name_end - function_name_start;
						if (function_name_length == keyword_length &&
								strncasecmp(function_name_start, keyword, keyword_length) == 0) {
								char* end = strstr(function_name_end, "}");
								if (end) {
										end += 1;
										size_t length = end - start;
										char* function = (char*)malloc(length + 1);
										if (!function) {
												fprintf(stderr, "Could not allocate memory for QEMU function\n");
												return NULL;
										}
										strncpy(function, start, length);
										function[length] = '\0';
										return function;
								}
						}
				}
				start = strstr(start + 1, "static bool trans_");
		}
		return NULL;
}

char** extract_sail_instructions(const char* content, size_t* count) {
	char** instructions = NULL;
	*count = 0;

	const char* clause_start = "function clause execute";
	const char* keyword = "RISCV_";
	const char* start = strstr(content, clause_start);
	while (start) {
			const char* clause_end = strstr(start, "RETIRE_SUCCESS");
			if (!clause_end) break;

			const char* keyword_start = strstr(start, keyword);
			while (keyword_start && keyword_start < clause_end) {
					const char* end = keyword_start + strlen(keyword);
					while (isalnum(*end) || *end == '_') {
							end++;
					}

					size_t length = end - keyword_start - strlen(keyword);
					char* instruction = (char*)malloc(length + 1);
					if (!instruction) {
							fprintf(stderr, "Could not allocate memory for instruction\n");
							return NULL;
					}

					strncpy(instruction, keyword_start + strlen(keyword), length);
					instruction[length] = '\0';

					instructions = (char**)realloc(instructions, (*count + 1) * sizeof(char*));
					if (!instructions) {
							fprintf(stderr, "Could not allocate memory for instruction array\n");
							return NULL;
					}

					instructions[*count] = instruction;
					(*count)++;

					keyword_start = strstr(keyword_start + 1, keyword);
			}
			start = strstr(start + 1, clause_start);
	}

	return instructions;
}

char** extract_qemu_instructions(const char* content, size_t* count) {
	char** instructions = NULL;
	*count = 0;

	const char* keyword = "trans_";
	const char* start = content;
	while ((start = strstr(start, keyword)) != NULL) {
			const char* end = start + strlen(keyword);
			while (isalnum(*end) || *end == '_') {
					end++;
			}

			size_t length = end - start - strlen(keyword);
			char* instruction = (char*)malloc(length + 1);
			if (!instruction) {
					fprintf(stderr, "Could not allocate memory for instruction\n");
					return NULL;
			}

			strncpy(instruction, start + strlen(keyword), length);
			instruction[length] = '\0';

			instructions = (char**)realloc(instructions, (*count + 1) * sizeof(char*));
			if (!instructions) {
					fprintf(stderr, "Could not allocate memory for instruction array\n");
					return NULL;
			}

			instructions[*count] = instruction;
			(*count)++;

			start = end;
	}

	return instructions;
}

char** find_common_instructions(char** sail_instructions, size_t sail_count, char** qemu_instructions, size_t qemu_count, size_t* common_count) {
		char** common_instructions = NULL;
		*common_count = 0;

		for (size_t i = 0; i < sail_count; i++) {
				to_lowercase(sail_instructions[i]);
		}

		for (size_t j = 0; j < qemu_count; j++) {
				to_lowercase(qemu_instructions[j]);
		}

		for (size_t i = 0; i < sail_count; i++) {
				for (size_t j = 0; j < qemu_count; j++) {
						if (strcmp(sail_instructions[i], qemu_instructions[j]) == 0) {
								common_instructions = (char**)realloc(common_instructions, (*common_count + 1) * sizeof(char*));
								if (!common_instructions) {
										fprintf(stderr, "Could not allocate memory for common instructions array\n");
										return NULL;
								}

								common_instructions[*common_count] = strdup(sail_instructions[i]);
								(*common_count)++;
						}
				}
		}

		return common_instructions;
}

InstructionMapping* create_instruction_mapping(const char* sail_file, const char* qemu_file, const char* keyword) {
		char* sail_content = read_file(sail_file);
		if (!sail_content) return NULL;

		char* qemu_content = read_file(qemu_file);
		if (!qemu_content) {
				free(sail_content);
				return NULL;
		}

		char* sail_function = extract_sail_function(sail_content, keyword);
		char* qemu_function = extract_qemu_function(qemu_content, keyword);

		free(sail_content);
		free(qemu_content);

		if (!sail_function || !qemu_function) {
				free(sail_function);
				free(qemu_function);
				return NULL;
		}

		InstructionMapping* mapping = (InstructionMapping*)malloc(sizeof(InstructionMapping));
		if (!mapping) {
				fprintf(stderr, "Could not allocate memory for instruction mapping\n");
				free(sail_function);
				free(qemu_function);
				return NULL;
		}

		mapping->sail_function = sail_function;
		mapping->qemu_function = qemu_function;
		mapping->next = NULL;

		return mapping;
}

Hashmap* init_hashmap(size_t size) {
		Hashmap* hashmap = (Hashmap*)malloc(sizeof(Hashmap));
		if (!hashmap) {
				fprintf(stderr, "Could not allocate memory for hashmap\n");
				return NULL;
		}

		hashmap->entries = (HashmapEntry*)calloc(size, sizeof(HashmapEntry));
		if (!hashmap->entries) {
				fprintf(stderr, "Could not allocate memory for hashmap entries\n");
				free(hashmap);
				return NULL;
		}

		hashmap->size = size;
		return hashmap;
}

void insert_hashmap(Hashmap* hashmap, const char* key, char* sail_function, char* qemu_function) {
		size_t index = 0;
		for (size_t i = 0; i < hashmap->size; i++) {
				if (!hashmap->entries[i].key || strcmp(hashmap->entries[i].key, key) == 0) {
						index = i;
						break;
				}
		}

		if (!hashmap->entries[index].key) {
				hashmap->entries[index].key = strdup(key);
				hashmap->entries[index].value = NULL;
		}

		InstructionMapping* new_mapping = (InstructionMapping*)malloc(sizeof(InstructionMapping));
		if (!new_mapping) {
				fprintf(stderr, "Could not allocate memory for new instruction mapping\n");
				return;
		}

		new_mapping->sail_function = sail_function;
		new_mapping->qemu_function = qemu_function;
		new_mapping->next = hashmap->entries[index].value;
		hashmap->entries[index].value = new_mapping;
}

void perform_instruction_mapping(const char* sail_file, const char* qemu_file, const char* keyword, Hashmap* hashmap) {
		InstructionMapping* mapping = create_instruction_mapping(sail_file, qemu_file, keyword);
		if (mapping) {
				insert_hashmap(hashmap, keyword, mapping->sail_function, mapping->qemu_function);
				free(mapping);
		} else {
				printf("Mapping not found for keyword: %s\n", keyword);
		}
}

Hashmap* perform_full_instruction_mapping(const char* sail_file, const char* qemu_file) {
		char* sail_content = read_file(sail_file);
		char* qemu_content = read_file(qemu_file);

		if (!sail_content || !qemu_content) {
				fprintf(stderr, "Error: Could not read files\n");
				return NULL;
		}

		size_t sail_count = 0;
		size_t qemu_count = 0;

		char** sail_instructions = extract_sail_instructions(sail_content, &sail_count);
		char** qemu_instructions = extract_qemu_instructions(qemu_content, &qemu_count);

		if (!sail_instructions || !qemu_instructions) {
				free(sail_content);
				free(qemu_content);
				fprintf(stderr, "Error: Could not extract instructions\n");
				return NULL;
		}

		size_t common_count = 0;
		char** common_instructions = find_common_instructions(sail_instructions, sail_count, qemu_instructions, qemu_count, &common_count);

		if (common_count == 0) {
				fprintf(stderr, "No common instructions found\n");
				return NULL;
		}

		Hashmap* hashmap = init_hashmap(common_count);
		if (!hashmap) {
				fprintf(stderr, "Error: Could not initialize hashmap\n");
				return NULL;
		}

		for (size_t i = 0; i < common_count; i++) {
				perform_instruction_mapping(sail_file, qemu_file, common_instructions[i], hashmap);
		}

		for (size_t i = 0; i < common_count; i++) {
				free(common_instructions[i]);
		}
		free(common_instructions);
		for (size_t i = 0; i < sail_count; i++) {
				free(sail_instructions[i]);
		}
		free(sail_instructions);
		for (size_t i = 0; i < qemu_count; i++) {
				free(qemu_instructions[i]);
		}
		free(qemu_instructions);

		free(sail_content);
		free(qemu_content);

		return hashmap;
}

Hashmap* update_hashmap_with_replacement(Hashmap* hashmap) {
		if (!hashmap) {
				fprintf(stderr, "Error: NULL hashmap\n");
				return NULL;
		}

		for (size_t i = 0; i < hashmap->size; i++) {
				HashmapEntry* entry = &(hashmap->entries[i]);

				if (entry->value) {
						char* modified_record = replace_match_with_rhs(entry->value->sail_function, entry->key);
						if (modified_record) {
								free(entry->value->sail_function);
								entry->value->sail_function = modified_record;
						}
				}
		}

		return hashmap;
}

void print_hashmap(const Hashmap* hashmap) {
		printf("{\n");
		for (size_t i = 0; i < hashmap->size; i++) {
				if (hashmap->entries[i].key) {
						printf("  \"%s\": {\n", hashmap->entries[i].key);
						InstructionMapping* mapping = hashmap->entries[i].value;
						while (mapping) {
								// Print the sail_function and qemu_function without the labels
								// printf("    \"sail_function\": \"%s\",\n", mapping->sail_function);
								// printf("    \"qemu_function\": \"%s\"\n", mapping->qemu_function);
								printf("    \"%s\",\n", mapping->sail_function);
								printf("    \"%s\"\n", mapping->qemu_function);
								mapping = mapping->next;
						}
						printf("  }");
						if (i < hashmap->size - 1) {
								printf(",");
						}
						printf("\n");
				}
		}
		printf("}\n");
}
