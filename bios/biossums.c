/*
 * $Id: biossums.c,v 1.3 2006/09/28 17:39:25 vruppert Exp $
 *
 *  This library is free software; you can redistribute it and/or
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

/* biossums.c  --- written by Eike W. for the Bochs BIOS */

#include <stdlib.h>
#include <stdio.h>

typedef unsigned char byte;

void check( int value, char* message );

#define LEN_BIOS_DATA 0x10000
#define MAX_OFFSET    (LEN_BIOS_DATA - 1)


#define BIOS_OFFSET 0xFFFF

long chksum_bios_get_offset( byte* data, long offset );
byte chksum_bios_calc_value( byte* data, long offset );
byte chksum_bios_get_value(  byte* data, long offset );
void chksum_bios_set_value(  byte* data, long offset, byte value );


#define _32__LEN         9
#define _32__CHKSUM     10

#define _32__MINHDR     16

long chksum__32__get_offset( byte* data, long offset );
byte chksum__32__calc_value( byte* data, long offset );
byte chksum__32__get_value(  byte* data, long offset );
void chksum__32__set_value(  byte* data, long offset, byte value );


#define _MP__LEN         8
#define _MP__CHKSUM     10

#define _MP__MINHDR     16

long chksum__mp__get_offset( byte* data, long offset );
byte chksum__mp__calc_value( byte* data, long offset );
byte chksum__mp__get_value(  byte* data, long offset );
void chksum__mp__set_value(  byte* data, long offset, byte value );


#define PCMP_BASELEN     4
#define PCMP_CHKSUM      7
#define PCMP_EXT_LEN    40
#define PCMP_EXT_CHKSUM 42

#define PCMP_MINHDR     42

long chksum_pcmp_get_offset( byte* data, long offset );
byte chksum_pcmp_calc_value( byte* data, long offset );
byte chksum_pcmp_get_value(  byte* data, long offset );
void chksum_pcmp_set_value(  byte* data, long offset, byte value );


#define _PIR_LEN         6
#define _PIR_CHKSUM     31

#define _PIR_MINHDR     32

long chksum__pir_get_offset( byte *data, long offset );
byte chksum__pir_calc_value( byte* data, long offset );
byte chksum__pir_get_value(  byte* data, long offset );
void chksum__pir_set_value(  byte* data, long offset, byte value );


byte bios_data[LEN_BIOS_DATA];
long bios_len;


int main(int argc, char* argv[]) {

  FILE* stream;
  long  offset, tmp_offset;
  byte  cur_val = 0, new_val = 0;
  int   arg = 1, hits, pad = 0;


  if ((argc == 3) && (!strcmp(argv[1], "-pad"))) {
    pad = 1;
    arg = 2;
  } else if (argc != 2) {
    printf("Error. Need a file-name as an argument.\n");
    exit(EXIT_FAILURE);
  }
  memset(bios_data, 0xff, LEN_BIOS_DATA);

  if ((stream = fopen(argv[arg], "rb")) == NULL) {
    printf("Error opening %s for reading.\n", argv[arg]);
    exit(EXIT_FAILURE);
  }
  bios_len = fread(bios_data, 1, LEN_BIOS_DATA, stream);
  if ((bios_len < LEN_BIOS_DATA) && (pad == 0)) {
    printf("Error reading 64KBytes from %s.\n", argv[arg]);
    fclose(stream);
    exit(EXIT_FAILURE);
  }
  fclose(stream);
  if (pad == 1) goto write_bios;

  hits   = 0;
  offset = 0L;
  while( (tmp_offset = chksum__32__get_offset( bios_data, offset )) != -1L ) {
    offset  = tmp_offset;
    cur_val = chksum__32__get_value(  bios_data, offset );
    new_val = chksum__32__calc_value( bios_data, offset );
    printf( "\n\nPCI-Bios header at: 0x%4lX\n", offset  );
    printf( "Current checksum:     0x%02X\n",   cur_val );
    printf( "Calculated checksum:  0x%02X  ",   new_val );
    hits++;
  }
  if( hits == 1 && cur_val != new_val ) {
    printf( "Setting checksum." );
    chksum__32__set_value( bios_data, offset, new_val );
  }
  if( hits >= 2 ) {
    printf( "Multiple PCI headers! No checksum set." );
  }
  if( hits ) {
    printf( "\n" );
  }


  hits   = 0;
  offset = 0L;
  while( (tmp_offset = chksum__mp__get_offset( bios_data, offset )) != -1L ) {
    offset  = tmp_offset;
    cur_val = chksum__mp__get_value(  bios_data, offset );
    new_val = chksum__mp__calc_value( bios_data, offset );
    printf( "\n\nMP header at:       0x%4lX\n", offset  );
    printf( "Current checksum:     0x%02X\n",   cur_val );
    printf( "Calculated checksum:  0x%02X  ",   new_val );
    hits++;
  }
  if( hits == 1 && cur_val != new_val ) {
    printf( "Setting checksum." );
    chksum__mp__set_value( bios_data, offset, new_val );
  }
  if( hits >= 2 ) {
    printf( "Warning! Multiple MP headers. No checksum set." );
  }
  if( hits ) {
    printf( "\n" );
  }


  hits   = 0;
  offset = 0L;
  while( (tmp_offset = chksum_pcmp_get_offset( bios_data, offset )) != -1L ) {
    offset  = tmp_offset;
    cur_val = chksum_pcmp_get_value(  bios_data, offset );
    new_val = chksum_pcmp_calc_value( bios_data, offset );
    printf( "\n\nPCMP header at:     0x%4lX\n", offset  );
    printf( "Current checksum:     0x%02X\n",   cur_val );
    printf( "Calculated checksum:  0x%02X  ",   new_val );
    hits++;
  }
  if( hits == 1 && cur_val != new_val ) {
    printf( "Setting checksum." );
    chksum_pcmp_set_value( bios_data, offset, new_val );
  }
  if( hits >= 2 ) {
    printf( "Warning! Multiple PCMP headers. No checksum set." );
  }
  if( hits ) {
    printf( "\n" );
  }


  hits   = 0;
  offset = 0L;
  while( (tmp_offset = chksum__pir_get_offset( bios_data, offset )) != -1L ) {
    offset  = tmp_offset;
    cur_val = chksum__pir_get_value(  bios_data, offset );
    new_val = chksum__pir_calc_value( bios_data, offset );
    printf( "\n\n$PIR header at:     0x%4lX\n", offset  );
    printf( "Current checksum:     0x%02X\n",   cur_val );
    printf( "Calculated checksum:  0x%02X\n  ",  new_val );
    hits++;
  }
  if( hits == 1 && cur_val != new_val ) {
    printf( "Setting checksum." );
    chksum__pir_set_value( bios_data, offset, new_val );
  }
  if( hits >= 2 ) {
    printf( "Warning! Multiple $PIR headers. No checksum set." );
  }
  if( hits ) {
    printf( "\n" );
  }


  offset  = 0L;
  offset  = chksum_bios_get_offset( bios_data, offset );
  cur_val = chksum_bios_get_value(  bios_data, offset );
  new_val = chksum_bios_calc_value( bios_data, offset );
  printf( "\n\nBios checksum at:   0x%4lX\n", offset  );
  printf( "Current checksum:     0x%02X\n",   cur_val );
  printf( "Calculated checksum:  0x%02X  ",   new_val );
  if( cur_val != new_val ) {
    printf( "Setting checksum." );
    chksum_bios_set_value( bios_data, offset, new_val );
  }
  printf( "\n" );

write_bios:
  if ((stream = fopen(argv[arg], "wb")) == NULL) {
    printf("Error opening %s for writing.\n", argv[arg]);
    exit(EXIT_FAILURE);
  }
  if (fwrite(bios_data, 1, LEN_BIOS_DATA, stream) < LEN_BIOS_DATA) {
    printf("Error writing 64KBytes to %s.\n", argv[arg]);
    fclose(stream);
    exit(EXIT_FAILURE);
  }
  fclose(stream);

  return(EXIT_SUCCESS);
}


void check(int okay, char* message) {

  if (!okay) {
    printf("\n\nError. %s.\n", message);
    exit(EXIT_FAILURE);
  }
}


long chksum_bios_get_offset( byte* data, long offset ) {

  return( BIOS_OFFSET );
}


byte chksum_bios_calc_value( byte* data, long offset ) {

  int   i;
  byte  sum;

  sum = 0;
  for( i = 0; i < MAX_OFFSET; i++ ) {
    sum = sum + *( data + i );
  }
  sum = -sum;          /* iso ensures -s + s == 0 on unsigned types */
  return( sum );
}


byte chksum_bios_get_value( byte* data, long offset ) {

  return( *( data + BIOS_OFFSET ) );
}


void chksum_bios_set_value( byte* data, long offset, byte value ) {

  *( data + BIOS_OFFSET ) = value;
}


byte chksum__32__calc_value( byte* data, long offset ) {

  int           i;
  int           len;
  byte sum;

  check( offset + _32__MINHDR <= MAX_OFFSET, "_32_ header out of bounds" );
  len = *( data + offset + _32__LEN ) << 4;
  check( offset + len <= MAX_OFFSET, "_32_ header-length out of bounds" );
  sum = 0;
  for( i = 0; i < len; i++ ) {
    if( i != _32__CHKSUM ) {
      sum = sum + *( data + offset + i );
    }
  }
  sum = -sum;
  return( sum );
}


long chksum__32__get_offset( byte* data, long offset ) {

  long result = -1L;

  offset = offset + 0x0F;
  offset = offset & ~( 0x0F );
  while( offset + 16 < MAX_OFFSET ) {
    offset = offset + 16;
    if( *( data + offset + 0 ) == '_' && \
        *( data + offset + 1 ) == '3' && \
        *( data + offset + 2 ) == '2' && \
        *( data + offset + 3 ) == '_' ) {
      result = offset;
      break;
    }
  }
  return( result );
}


byte chksum__32__get_value( byte* data, long offset ) {

  check( offset + _32__CHKSUM <= MAX_OFFSET, "PCI-Bios checksum out of bounds" );
  return(  *( data + offset + _32__CHKSUM ) );
}


void chksum__32__set_value( byte* data, long offset, byte value ) {

  check( offset + _32__CHKSUM <= MAX_OFFSET, "PCI-Bios checksum out of bounds" );
  *( data + offset + _32__CHKSUM ) = value;
}


byte chksum__mp__calc_value( byte* data, long offset ) {

  int   i;
  int   len;
  byte  sum;

  check( offset + _MP__MINHDR <= MAX_OFFSET, "_MP_ header out of bounds" );
  len = *( data + offset + _MP__LEN ) << 4;
  check( offset + len <= MAX_OFFSET, "_MP_ header-length out of bounds" );
  sum = 0;
  for( i = 0; i < len; i++ ) {
    if( i != _MP__CHKSUM ) {
      sum = sum + *( data + offset + i );
    }
  }
  sum = -sum;
  return( sum );
}


long chksum__mp__get_offset( byte* data, long offset ) {

  long result = -1L;

  offset = offset + 0x0F;
  offset = offset & ~( 0x0F );
  while( offset + 16 < MAX_OFFSET ) {
    offset = offset + 16;
    if( *( data + offset + 0 ) == '_' && \
        *( data + offset + 1 ) == 'M' && \
        *( data + offset + 2 ) == 'P' && \
        *( data + offset + 3 ) == '_' ) {
      result = offset;
      break;
    }
  }
  return( result );
}


byte chksum__mp__get_value( byte* data, long offset ) {

  check( offset + _MP__CHKSUM <= MAX_OFFSET, "MP checksum out of bounds" );
  return( *( data + offset + _MP__CHKSUM ) );
}


void chksum__mp__set_value( byte* data, long offset, byte value ) {

  check( offset + _MP__CHKSUM <= MAX_OFFSET, "MP checksum out of bounds" );
  *( data + offset + _MP__CHKSUM ) = value;
}


byte chksum_pcmp_calc_value( byte* data, long offset ) {

  int   i;
  int   len;
  byte  sum;

  check( offset + PCMP_MINHDR <= MAX_OFFSET, "PCMP header out of bounds" );
  len  =   *( data + offset + PCMP_BASELEN )      + \
         ( *( data + offset + PCMP_BASELEN + 1 ) << 8 );
  check( offset + len <= MAX_OFFSET, "PCMP header-length out of bounds" );
  if( *( data + offset + PCMP_EXT_LEN )     | \
      *( data + offset + PCMP_EXT_LEN + 1 ) | \
      *( data + offset + PCMP_EXT_CHKSUM ) ) {
    check( 0, "PCMP header indicates extended tables (unsupported)" );
  }
  sum = 0;
  for( i = 0; i < len; i++ ) {
    if( i != PCMP_CHKSUM ) {
      sum = sum + *( data + offset + i );
    }
  }
  sum = -sum;
  return( sum );
}


long chksum_pcmp_get_offset( byte* data, long offset ) {

  long result = -1L;

  offset = offset + 0x0F;
  offset = offset & ~( 0x0F );
  while( offset + 16 < MAX_OFFSET ) {
    offset = offset + 16;
    if( *( data + offset + 0 ) == 'P' && \
        *( data + offset + 1 ) == 'C' && \
        *( data + offset + 2 ) == 'M' && \
        *( data + offset + 3 ) == 'P' ) {
      result = offset;
      break;
    }
  }
  return( result );
}


byte chksum_pcmp_get_value( byte* data, long offset ) {

  check( offset + PCMP_CHKSUM <= MAX_OFFSET, "PCMP checksum out of bounds" );
  return( *( data + offset + PCMP_CHKSUM ) );
}


void chksum_pcmp_set_value( byte* data, long offset, byte value ) {

  check( offset + PCMP_CHKSUM <= MAX_OFFSET, "PCMP checksum out of bounds" );
  *( data + offset + PCMP_CHKSUM ) = value;
}


byte chksum__pir_calc_value( byte* data, long offset ) {

  int   i;
  int   len;
  byte  sum;

  check( offset + _PIR_MINHDR <= MAX_OFFSET, "$PIR header out of bounds" );
  len  =   *( data + offset + _PIR_LEN )      + \
         ( *( data + offset + _PIR_LEN + 1 ) << 8 );
  check( offset + len <= MAX_OFFSET, "$PIR header-length out of bounds" );
  sum = 0;
  for( i = 0; i < len; i++ ) {
    if( i != _PIR_CHKSUM ) {
      sum = sum + *( data + offset + i );
    }
  }
  sum = -sum;
  return( sum );
}


long chksum__pir_get_offset( byte* data, long offset ) {

  long result = -1L;

  offset = offset + 0x0F;
  offset = offset & ~( 0x0F );
  while( offset + 16 < MAX_OFFSET ) {
    offset = offset + 16;
    if( *( data + offset + 0 ) == '$' && \
        *( data + offset + 1 ) == 'P' && \
        *( data + offset + 2 ) == 'I' && \
        *( data + offset + 3 ) == 'R' ) {
      result = offset;
      break;
    }
  }
  return( result );
}


byte chksum__pir_get_value( byte* data, long offset ) {

  check( offset + _PIR_CHKSUM <= MAX_OFFSET, "$PIR checksum out of bounds" );
  return(  *( data + offset + _PIR_CHKSUM ) );
}


void chksum__pir_set_value( byte* data, long offset, byte value ) {

  check( offset + _PIR_CHKSUM <= MAX_OFFSET, "$PIR checksum out of bounds" );
  *( data + offset + _PIR_CHKSUM ) = value;
}

