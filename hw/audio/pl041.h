/*
 * Arm PrimeCell PL041 Advanced Audio Codec Interface
 *
 * Copyright (c) 2011
 * Written by Mathieu Sonet - www.elasticsheep.com
 *
 * This code is licensed under the GPL.
 *
 * *****************************************************************
 */

#ifndef HW_PL041_H
#define HW_PL041_H

/* Register file */
#define REGISTER(name, offset) uint32_t name;
typedef struct {
    #include "pl041.hx"
} pl041_regfile;
#undef REGISTER

/* Register addresses */
#define REGISTER(name, offset) PL041_##name = offset,
enum {
    #include "pl041.hx"

    PL041_periphid0 = 0xFE0,
    PL041_periphid1 = 0xFE4,
    PL041_periphid2 = 0xFE8,
    PL041_periphid3 = 0xFEC,
    PL041_pcellid0  = 0xFF0,
    PL041_pcellid1  = 0xFF4,
    PL041_pcellid2  = 0xFF8,
    PL041_pcellid3  = 0xFFC,
};
#undef REGISTER

/* Register bits */

/* IEx */
#define TXCIE           (1 << 0)
#define RXTIE           (1 << 1)
#define TXIE            (1 << 2)
#define RXIE            (1 << 3)
#define RXOIE           (1 << 4)
#define TXUIE           (1 << 5)
#define RXTOIE          (1 << 6)

/* TXCRx */
#define TXEN            (1 << 0)
#define TXSLOT1         (1 << 1)
#define TXSLOT2         (1 << 2)
#define TXSLOT3         (1 << 3)
#define TXSLOT4         (1 << 4)
#define TXCOMPACT       (1 << 15)
#define TXFEN           (1 << 16)

#define TXSLOT_MASK_BIT (1)
#define TXSLOT_MASK     (0xFFF << TXSLOT_MASK_BIT)

#define TSIZE_MASK_BIT  (13)
#define TSIZE_MASK      (0x3 << TSIZE_MASK_BIT)

#define TSIZE_16BITS    (0x0 << TSIZE_MASK_BIT)
#define TSIZE_18BITS    (0x1 << TSIZE_MASK_BIT)
#define TSIZE_20BITS    (0x2 << TSIZE_MASK_BIT)
#define TSIZE_12BITS    (0x3 << TSIZE_MASK_BIT)

/* SRx */
#define RXFE         (1 << 0)
#define TXFE         (1 << 1)
#define RXHF         (1 << 2)
#define TXHE         (1 << 3)
#define RXFF         (1 << 4)
#define TXFF         (1 << 5)
#define RXBUSY       (1 << 6)
#define TXBUSY       (1 << 7)
#define RXOVERRUN    (1 << 8)
#define TXUNDERRUN   (1 << 9)
#define RXTIMEOUT    (1 << 10)
#define RXTOFE       (1 << 11)

/* ISRx */
#define TXCINTR      (1 << 0)
#define RXTOINTR     (1 << 1)
#define TXINTR       (1 << 2)
#define RXINTR       (1 << 3)
#define ORINTR       (1 << 4)
#define URINTR       (1 << 5)
#define RXTOFEINTR   (1 << 6)

/* SLFR */
#define SL1RXBUSY    (1 << 0)
#define SL1TXBUSY    (1 << 1)
#define SL2RXBUSY    (1 << 2)
#define SL2TXBUSY    (1 << 3)
#define SL12RXBUSY   (1 << 4)
#define SL12TXBUSY   (1 << 5)
#define SL1RXVALID   (1 << 6)
#define SL1TXEMPTY   (1 << 7)
#define SL2RXVALID   (1 << 8)
#define SL2TXEMPTY   (1 << 9)
#define SL12RXVALID  (1 << 10)
#define SL12TXEMPTY  (1 << 11)
#define RAWGPIOINT   (1 << 12)
#define RWIS         (1 << 13)

/* MAINCR */
#define AACIFE       (1 << 0)
#define LOOPBACK     (1 << 1)
#define LOWPOWER     (1 << 2)
#define SL1RXEN      (1 << 3)
#define SL1TXEN      (1 << 4)
#define SL2RXEN      (1 << 5)
#define SL2TXEN      (1 << 6)
#define SL12RXEN     (1 << 7)
#define SL12TXEN     (1 << 8)
#define DMAENABLE    (1 << 9)

/* INTCLR */
#define WISC         (1 << 0)
#define RXOEC1       (1 << 1)
#define RXOEC2       (1 << 2)
#define RXOEC3       (1 << 3)
#define RXOEC4       (1 << 4)
#define TXUEC1       (1 << 5)
#define TXUEC2       (1 << 6)
#define TXUEC3       (1 << 7)
#define TXUEC4       (1 << 8)
#define RXTOFEC1     (1 << 9)
#define RXTOFEC2     (1 << 10)
#define RXTOFEC3     (1 << 11)
#define RXTOFEC4     (1 << 12)

#endif /* HW_PL041_H */
