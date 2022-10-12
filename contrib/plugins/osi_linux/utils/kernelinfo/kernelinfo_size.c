/*!
 * @file kernelinfo_size.c
 * @brief Prints sizes of struct kernelinfo and its members.
 *
 * This is useful for debugging the output of read_kernelinfo().
 *
 * @author Manolis Stamatogiannakis <manolis.stamatogiannakis@vu.nl>
 * @copyright This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */
#include <stdio.h>
#include <stdint.h>
#include "kernelinfo.h"

#define PRINT_MEMBER_SIZE_FMT "%8s:%4zu:%03td-%03td\n"
#define PRINT_MEMBER_SIZE(ki, memb) printf(PRINT_MEMBER_SIZE_FMT,\
		#ki "." #memb,\
		sizeof(ki.memb),\
		((uint8_t *)&(ki.memb)-(uint8_t *)&ki), ((uint8_t *)&(ki.memb)-(uint8_t *)&ki)+sizeof(ki.memb)-1)

int main(int argc, char *argv[]) {
	struct kernelinfo ki;
	PRINT_MEMBER_SIZE(ki, name);
	PRINT_MEMBER_SIZE(ki, task);
	PRINT_MEMBER_SIZE(ki, cred);
	PRINT_MEMBER_SIZE(ki, mm);
	PRINT_MEMBER_SIZE(ki, vma);
	PRINT_MEMBER_SIZE(ki, fs);
	PRINT_MEMBER_SIZE(ki, path);
	printf("-----------------------------\n");
	printf(PRINT_MEMBER_SIZE_FMT, "ki", sizeof(ki), (size_t)0, sizeof(ki)-1);
}

/* vim:set tabstop=4 softtabstop=4 noexpandtab: */
