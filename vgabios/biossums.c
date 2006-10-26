/* biossums.c  --- written by Eike W. for the Bochs BIOS */
/* adapted for the LGPL'd VGABIOS by vruppert */

/*  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */
#include <stdlib.h>
#include <stdio.h>

typedef unsigned char byte;

void check( int value, char* message );

#define MAX_BIOS_DATA 0x10000

long chksum_bios_get_offset( byte* data, long offset );
byte chksum_bios_calc_value( byte* data, long offset );
byte chksum_bios_get_value(  byte* data, long offset );
void chksum_bios_set_value(  byte* data, long offset, byte value );


#define PMID_LEN        20
#define PMID_CHKSUM     19

long chksum_pmid_get_offset( byte* data, long offset );
byte chksum_pmid_calc_value( byte* data, long offset );
byte chksum_pmid_get_value(  byte* data, long offset );
void chksum_pmid_set_value(  byte* data, long offset, byte value );


byte bios_data[MAX_BIOS_DATA];
long bios_len;


int main(int argc, char* argv[])
{
  FILE* stream;
  long  offset, tmp_offset;
  byte  bios_len_byte, cur_val = 0, new_val = 0;
  int   hits, modified;

  if (argc != 2) {
    printf( "Error. Need a file-name as an argument.\n" );
    exit( EXIT_FAILURE );
  }

  if ((stream = fopen(argv[1], "rb")) == NULL) {
    printf("Error opening %s for reading.\n", argv[1]);
    exit(EXIT_FAILURE);
  }
  memset(bios_data, 0, MAX_BIOS_DATA);
  bios_len = fread(bios_data, 1, MAX_BIOS_DATA, stream);
  if (bios_len > MAX_BIOS_DATA) {
    printf("Error reading max. 65536 Bytes from %s.\n", argv[1]);
    fclose(stream);
    exit(EXIT_FAILURE);
  }
  fclose(stream);
  modified = 0;
  if (bios_len < 0x8000) {
    bios_len = 0x8000;
    modified = 1;
  } else if ((bios_len & 0x1FF) != 0) {
    bios_len = (bios_len + 0x200) & ~0x1FF;
    modified = 1;
  }
  bios_len_byte = (byte)(bios_len / 512);
  if (bios_len_byte != bios_data[2]) {
    if (modified == 0) {
      bios_len += 0x200;
    }
    bios_data[2] = (byte)(bios_len / 512);
    modified = 1;
  }

  hits   = 0;
  offset = 0L;
  while( (tmp_offset = chksum_pmid_get_offset( bios_data, offset )) != -1L ) {
    offset  = tmp_offset;
    cur_val = chksum_pmid_get_value(  bios_data, offset );
    new_val = chksum_pmid_calc_value( bios_data, offset );
    printf( "\nPMID entry at: 0x%4lX\n", offset  );
    printf( "Current checksum:     0x%02X\n",   cur_val );
    printf( "Calculated checksum:  0x%02X  ",   new_val );
    hits++;
  }
  if ((hits == 1) && (cur_val != new_val)) {
    printf("Setting checksum.");
    chksum_pmid_set_value( bios_data, offset, new_val );
    if (modified == 0) {
      bios_len += 0x200;
      bios_data[2]++;
    }
    modified = 1;
  }
  if (hits >= 2) {
    printf( "Multiple PMID entries! No checksum set." );
  }
  if (hits) {
    printf("\n");
  }

  offset  = 0L;
  do {
    offset  = chksum_bios_get_offset(bios_data, offset);
    cur_val = chksum_bios_get_value(bios_data, offset);
    new_val = chksum_bios_calc_value(bios_data, offset);
    if ((cur_val != new_val) && (modified == 0)) {
      bios_len += 0x200;
      bios_data[2]++;
      modified = 1;
    } else {
      printf("\nBios checksum at:   0x%4lX\n", offset);
      printf("Current checksum:     0x%02X\n", cur_val);
      printf("Calculated checksum:  0x%02X  ", new_val);
      if (cur_val != new_val) {
        printf("Setting checksum.");
        chksum_bios_set_value(bios_data, offset, new_val);
        cur_val = new_val;
        modified = 1;
      }
      printf( "\n" );
    }
  } while (cur_val != new_val);

  if (modified == 1) {
    if ((stream = fopen( argv[1], "wb")) == NULL) {
      printf("Error opening %s for writing.\n", argv[1]);
      exit(EXIT_FAILURE);
    }
    if (fwrite(bios_data, 1, bios_len, stream) < bios_len) {
      printf("Error writing %d KBytes to %s.\n", bios_len / 1024, argv[1]);
      fclose(stream);
      exit(EXIT_FAILURE);
    }
    fclose(stream);
  }

  return (EXIT_SUCCESS);
}


void check( int okay, char* message ) {

  if( !okay ) {
    printf( "\n\nError. %s.\n", message );
    exit( EXIT_FAILURE );
  }
}


long chksum_bios_get_offset( byte* data, long offset ) {

  return (bios_len - 1);
}


byte chksum_bios_calc_value( byte* data, long offset ) {

  int   i;
  byte  sum;

  sum = 0;
  for( i = 0; i < offset; i++ ) {
    sum = sum + *( data + i );
  }
  sum = -sum;          /* iso ensures -s + s == 0 on unsigned types */
  return( sum );
}


byte chksum_bios_get_value( byte* data, long offset ) {

  return( *( data + offset ) );
}


void chksum_bios_set_value( byte* data, long offset, byte value ) {

  *( data + offset ) = value;
}


byte chksum_pmid_calc_value( byte* data, long offset ) {

  int           i;
  int           len;
  byte sum;

  len = PMID_LEN;
  check((offset + len) <= (bios_len - 1), "PMID entry length out of bounds" );
  sum = 0;
  for( i = 0; i < len; i++ ) {
    if( i != PMID_CHKSUM ) {
      sum = sum + *( data + offset + i );
    }
  }
  sum = -sum;
  return( sum );
}


long chksum_pmid_get_offset( byte* data, long offset ) {

  long result = -1L;

  while ((offset + PMID_LEN) < (bios_len - 1)) {
    offset = offset + 1;
    if( *( data + offset + 0 ) == 'P' && \
        *( data + offset + 1 ) == 'M' && \
        *( data + offset + 2 ) == 'I' && \
        *( data + offset + 3 ) == 'D' ) {
      result = offset;
      break;
    }
  }
  return( result );
}


byte chksum_pmid_get_value( byte* data, long offset ) {

  check((offset + PMID_CHKSUM) <= (bios_len - 1), "PMID checksum out of bounds" );
  return(  *( data + offset + PMID_CHKSUM ) );
}


void chksum_pmid_set_value( byte* data, long offset, byte value ) {

  check((offset + PMID_CHKSUM) <= (bios_len - 1), "PMID checksum out of bounds" );
  *( data + offset + PMID_CHKSUM ) = value;
}
