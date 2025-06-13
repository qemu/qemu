/* Decimal 128-bit format module header for the decNumber C Library.
   Copyright (C) 2005, 2007 Free Software Foundation, Inc.
   Contributed by IBM Corporation.  Author Mike Cowlishaw.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it under
   the terms of the GNU General Public License as published by the Free
   Software Foundation; either version 2, or (at your option) any later
   version.

   In addition to the permissions in the GNU General Public License,
   the Free Software Foundation gives you unlimited permission to link
   the compiled version of this file into combinations with other
   programs, and to distribute those combinations without any
   restriction coming from the use of this file.  (The General Public
   License restrictions do apply in other respects; for example, they
   cover modification of the file, and distribution when not linked
   into a combine executable.)

   GCC is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or
   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
   for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, see
   <https://www.gnu.org/licenses/>.  */

/* ------------------------------------------------------------------ */
/* Decimal 128-bit format module header				      */
/* ------------------------------------------------------------------ */

#ifndef DECIMAL128_H
#define DECIMAL128_H

  #define DEC128NAME	 "decimal128"		      /* Short name   */
  #define DEC128FULLNAME "Decimal 128-bit Number"     /* Verbose name */
  #define DEC128AUTHOR	 "Mike Cowlishaw"	      /* Who to blame */

  /* parameters for decimal128s */
  #define DECIMAL128_Bytes  16		/* length		      */
  #define DECIMAL128_Pmax   34		/* maximum precision (digits) */
  #define DECIMAL128_Emax   6144	/* maximum adjusted exponent  */
  #define DECIMAL128_Emin  -6143	/* minimum adjusted exponent  */
  #define DECIMAL128_Bias   6176	/* bias for the exponent      */
  #define DECIMAL128_String 43		/* maximum string length, +1  */
  #define DECIMAL128_EconL  12		/* exp. continuation length   */
  /* highest biased exponent (Elimit-1)				      */
  #define DECIMAL128_Ehigh  (DECIMAL128_Emax+DECIMAL128_Bias-DECIMAL128_Pmax+1)

  /* check enough digits, if pre-defined			      */
  #if defined(DECNUMDIGITS)
    #if (DECNUMDIGITS<DECIMAL128_Pmax)
      #error decimal128.h needs pre-defined DECNUMDIGITS>=34 for safe use
    #endif
  #endif

  #ifndef DECNUMDIGITS
    #define DECNUMDIGITS DECIMAL128_Pmax /* size if not already defined*/
  #endif
  #include "libdecnumber/decNumber.h"

  /* Decimal 128-bit type, accessible by bytes			      */
  typedef struct {
    uint8_t bytes[DECIMAL128_Bytes]; /* decimal128: 1, 5, 12, 110 bits*/
    } decimal128;

  /* special values [top byte excluding sign bit; last two bits are   */
  /* don't-care for Infinity on input, last bit don't-care for NaN]   */
  #if !defined(DECIMAL_NaN)
    #define DECIMAL_NaN	    0x7c	/* 0 11111 00 NaN	      */
    #define DECIMAL_sNaN    0x7e	/* 0 11111 10 sNaN	      */
    #define DECIMAL_Inf	    0x78	/* 0 11110 00 Infinity	      */
  #endif

  #include "decimal128Local.h"

  /* ---------------------------------------------------------------- */
  /* Routines							      */
  /* ---------------------------------------------------------------- */


  /* String conversions						      */
  decimal128 * decimal128FromString(decimal128 *, const char *, decContext *);
  char * decimal128ToString(const decimal128 *, char *);
  char * decimal128ToEngString(const decimal128 *, char *);

  /* decNumber conversions					      */
  decimal128 * decimal128FromNumber(decimal128 *, const decNumber *,
				    decContext *);
  decNumber * decimal128ToNumber(const decimal128 *, decNumber *);

  /* Format-dependent utilities					      */
  uint32_t    decimal128IsCanonical(const decimal128 *);
  decimal128 * decimal128Canonical(decimal128 *, const decimal128 *);

#endif
