/* firmware/extract.c - tool for extracting firmwares out of linux
 * binary drivers
 *
 * --------------------------------------------------------------------
 *
 * Copyright (C) 2003  ACX100 Open Source Project
 *
 *   The contents of this file are subject to the Mozilla Public
 *   License Version 1.1 (the "License"); you may not use this file
 *   except in compliance with the License. You may obtain a copy of
 *   the License at http://www.mozilla.org/MPL/
 *
 *   Software distributed under the License is distributed on an "AS
 *   IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 *   implied. See the License for the specific language governing
 *   rights and limitations under the License.
 *
 *   Alternatively, the contents of this file may be used under the
 *   terms of the GNU Public License version 2 (the "GPL"), in which
 *   case the provisions of the GPL are applicable instead of the
 *   above.  If you wish to allow the use of your version of this file
 *   only under the terms of the GPL and not to allow others to use
 *   your version of this file under the MPL, indicate your decision
 *   by deleting the provisions above and replace them with the notice
 *   and other provisions required by the GPL.  If you do not delete
 *   the provisions above, a recipient may use your version of this
 *   file under either the MPL or the GPL.
 *
 * --------------------------------------------------------------------
 *
 * Inquiries regarding the ACX100 Open Source Project can be
 * made directly to:
 *
 * acx100-users@lists.sf.net
 * http://acx100.sf.net
 *
 * --------------------------------------------------------------------
 */

/* TODO:
 *	- get the firmware size from the debug symbols in the binary
 *	- check for version string, not only 4 first bytes
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#define FRMWR_SIZE 29612
#define NR_COLS 10

int main(int argc, char *argv[])
{
	FILE *driver;
	unsigned char c;
	int size, pos, file_size;
	unsigned char *mem, *begin;

	/* this is the signature for firmware version 1.5.0 which is present in all the linux binary drivers. */
	unsigned char signature[] = {0x58, 0xb2, 0x24, 0x00};

	/* check the arguments */
	if (argc != 2)
	{
		fprintf(stderr, "Wrong call arguments.\n");
		fprintf(stderr, "USAGE: extract binary_driver.o\n");
		fprintf(stderr, " The firmware will be extracted, and will be written to stdout.\n");
		exit(1);
	}

	/* try to open the file */
	driver = fopen(argv[1], "r");
	if(!driver)
	{
		fprintf(stderr, "ERROR: couldn't open or find given binary file %s to extract firmware image from it, aborting!\n", argv[1]);
		exit(1);
	}

	/* get file stats */
	fseek(driver, 0, SEEK_END);
	file_size = ftell(driver);
	fseek(driver, 0, SEEK_SET);

	/* alloc buffer to hold complete file */
	mem = malloc(file_size);
	if(!mem)
	{
		fprintf(stderr, "Could not allocate enough memory\n");
		exit(1);
	}

	/* read the complete file */
	size = fread(mem, 1, file_size, driver);
	if ( size != file_size)
	{
		fprintf(stderr, "Was unable to read complete file (%i, %i)\n", size, file_size);
		exit(1);
	}


	/* find the firmware */
	begin = memmem(mem, file_size, signature, 4);
	if(!begin)
	{
		fprintf(stderr, "Firmware was not found\n");
		exit(1);
	}

	/* start processing the firmware */
	pos = 0;
	do {
		c = begin[pos];
		printf("%c", c);
		pos++;
	} while(pos != FRMWR_SIZE);

	free(driver);

	exit(0);
}

