/*
 * Extended Boot Option ROM
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Copyright IBM Corporation, 2007
 *   Authors: Anthony Liguori <aliguori@us.ibm.com>
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

int main(int argc, char **argv)
{
	FILE *fin, *fout;
	char buffer[512], oldbuffer[512];
	int i, size, lag = 0;
	uint8_t sum = 0;

	if (argc != 3) {
		printf("Usage: %s ROM OUTPUT\n", argv[0]);
		return 1;
	}

	fin = fopen(argv[1], "rb");
	fout = fopen(argv[2], "wb");

	if (fin == NULL || fout == NULL) {
		fprintf(stderr, "Could not open input/output files\n");
		return 1;
	}

	do {
		size = fread(buffer, 512, 1, fin);
		if (size == 1) {
			for (i = 0; i < 512; i++)
				sum += buffer[i];

			if (lag) {
				if (fwrite(oldbuffer, 512, 1, fout) != 1) {
					fprintf(stderr, "Write failed\n");
					return 1;
				}
			}
			lag = 1;
			memcpy(oldbuffer, buffer, 512);
		}
	} while (size == 1);

	if (size != 0) {
		fprintf(stderr, "Failed to read from input file\n");
		return 1;
	}

	oldbuffer[511] = -sum;

	if (fwrite(oldbuffer, 512, 1, fout) != 1) {
		fprintf(stderr, "Failed to write to output file\n");
		return 1;
	}

	fclose(fin);
	fclose(fout);

	return 0;
}
