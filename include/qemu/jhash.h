/* jhash.h: Jenkins hash support.
  *
  * Copyright (C) 2006. Bob Jenkins (bob_jenkins@burtleburtle.net)
  *
  * http://burtleburtle.net/bob/hash/
  *
  * These are the credits from Bob's sources:
  *
  * lookup3.c, by Bob Jenkins, May 2006, Public Domain.
  *
  * These are functions for producing 32-bit hashes for hash table lookup.
  * hashword(), hashlittle(), hashlittle2(), hashbig(), mix(), and final()
  * are externally useful functions.  Routines to test the hash are included
  * if SELF_TEST is defined.  You can use this free for any purpose. It's in
  * the public domain.  It has no warranty.
  *
  * Copyright (C) 2009-2010 Jozsef Kadlecsik (kadlec@blackhole.kfki.hu)
  *
  * I've modified Bob's hash to be useful in the Linux kernel, and
  * any bugs present are my fault.
  * Jozsef
  */

#ifndef QEMU_JHASH_H__
#define QEMU_JHASH_H__

#include "qemu/bitops.h"

/*
 * hashtable relation copy from linux kernel jhash
 */

/* __jhash_mix -- mix 3 32-bit values reversibly. */
#define __jhash_mix(a, b, c)                \
{                                           \
    a -= c;  a ^= rol32(c, 4);  c += b;     \
    b -= a;  b ^= rol32(a, 6);  a += c;     \
    c -= b;  c ^= rol32(b, 8);  b += a;     \
    a -= c;  a ^= rol32(c, 16); c += b;     \
    b -= a;  b ^= rol32(a, 19); a += c;     \
    c -= b;  c ^= rol32(b, 4);  b += a;     \
}

/* __jhash_final - final mixing of 3 32-bit values (a,b,c) into c */
#define __jhash_final(a, b, c)  \
{                               \
    c ^= b; c -= rol32(b, 14);  \
    a ^= c; a -= rol32(c, 11);  \
    b ^= a; b -= rol32(a, 25);  \
    c ^= b; c -= rol32(b, 16);  \
    a ^= c; a -= rol32(c, 4);   \
    b ^= a; b -= rol32(a, 14);  \
    c ^= b; c -= rol32(b, 24);  \
}

/* An arbitrary initial parameter */
#define JHASH_INITVAL           0xdeadbeef

#endif /* QEMU_JHASH_H__ */
