/*
   american fuzzy lop++ - type definitions and minor macros
   --------------------------------------------------------

   Originally written by Michal Zalewski

   Now maintained by Marc Heuse <mh@mh-sec.de>,
                     Heiko Eissfeldt <heiko.eissfeldt@hexco.de>,
                     Andrea Fioraldi <andreafioraldi@gmail.com>,
                     Dominik Maier <mail@dmnk.co>

   Copyright 2016, 2017 Google Inc. All rights reserved.
   Copyright 2019-2024 AFLplusplus Project. All rights reserved.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at:

     https://www.apache.org/licenses/LICENSE-2.0

 */

#ifndef _HAVE_TYPES_H
#define _HAVE_TYPES_H

#include <stdint.h>
#include <stdlib.h>
#include "config.h"

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
#ifdef WORD_SIZE_64
typedef unsigned __int128 uint128_t;
typedef uint128_t         u128;
#endif

/* Extended forkserver option values */

/* Reporting errors */
#define FS_OPT_ERROR 0xf800008f
#define FS_OPT_GET_ERROR(x) ((x & 0x00ffff00) >> 8)
#define FS_OPT_SET_ERROR(x) ((x & 0x0000ffff) << 8)
#define FS_ERROR_MAP_SIZE 1
#define FS_ERROR_MAP_ADDR 2
#define FS_ERROR_SHM_OPEN 4
#define FS_ERROR_SHMAT 8
#define FS_ERROR_MMAP 16
#define FS_ERROR_OLD_CMPLOG 32
#define FS_ERROR_OLD_CMPLOG_QEMU 64

/* New Forkserver */
#define FS_NEW_VERSION_MIN 1
#define FS_NEW_VERSION_MAX 1
#define FS_NEW_ERROR 0xeffe0000
#define FS_NEW_OPT_MAPSIZE 0x00000001      // parameter: 32 bit value
#define FS_NEW_OPT_SHDMEM_FUZZ 0x00000002  // parameter: none
#define FS_NEW_OPT_AUTODICT 0x00000800     // autodictionary data

/* Reporting options */
#define FS_OPT_ENABLED 0x80000001
#define FS_OPT_MAPSIZE 0x40000000
#define FS_OPT_SNAPSHOT 0x20000000
#define FS_OPT_AUTODICT 0x10000000
#define FS_OPT_SHDMEM_FUZZ 0x01000000
#define FS_OPT_NEWCMPLOG 0x02000000
#define FS_OPT_OLD_AFLPP_WORKAROUND 0x0f000000
// FS_OPT_MAX_MAPSIZE is 8388608 = 0x800000 = 2^23 = 1 << 23
#define FS_OPT_MAX_MAPSIZE ((0x00fffffeU >> 1) + 1)
#define FS_OPT_GET_MAPSIZE(x) (((x & 0x00fffffe) >> 1) + 1)
#define FS_OPT_SET_MAPSIZE(x) \
  (x <= 1 || x > FS_OPT_MAX_MAPSIZE ? 0 : ((x - 1) << 1))

typedef unsigned long long u64;

typedef int8_t  s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
#ifdef WORD_SIZE_64
typedef __int128 int128_t;
typedef int128_t s128;
#endif

#ifndef MIN
  #define MIN(a, b)           \
    ({                        \
                              \
      __typeof__(a) _a = (a); \
      __typeof__(b) _b = (b); \
      _a < _b ? _a : _b;      \
                              \
    })

  #define MAX(a, b)           \
    ({                        \
                              \
      __typeof__(a) _a = (a); \
      __typeof__(b) _b = (b); \
      _a > _b ? _a : _b;      \
                              \
    })

#endif                                                              /* !MIN */

#define SWAP16(_x)                    \
  ({                                  \
                                      \
    u16 _ret = (_x);                  \
    (u16)((_ret << 8) | (_ret >> 8)); \
                                      \
  })

#define SWAP32(_x)                                                   \
  ({                                                                 \
                                                                     \
    u32 _ret = (_x);                                                 \
    (u32)((_ret << 24) | (_ret >> 24) | ((_ret << 8) & 0x00FF0000) | \
          ((_ret >> 8) & 0x0000FF00));                               \
                                                                     \
  })

#define SWAP64(_x)                                                             \
  ({                                                                           \
                                                                               \
    u64 _ret = (_x);                                                           \
    _ret =                                                                     \
        (_ret & 0x00000000FFFFFFFF) << 32 | (_ret & 0xFFFFFFFF00000000) >> 32; \
    _ret =                                                                     \
        (_ret & 0x0000FFFF0000FFFF) << 16 | (_ret & 0xFFFF0000FFFF0000) >> 16; \
    _ret =                                                                     \
        (_ret & 0x00FF00FF00FF00FF) << 8 | (_ret & 0xFF00FF00FF00FF00) >> 8;   \
    _ret;                                                                      \
                                                                               \
  })

// It is impossible to define 128 bit constants, so ...
#ifdef WORD_SIZE_64
  #define SWAPN(_x, _l)                            \
    ({                                             \
                                                   \
      u128  _res = (_x), _ret;                     \
      char *d = (char *)&_ret, *s = (char *)&_res; \
      int   i;                                     \
      for (i = 0; i < 16; i++)                     \
        d[15 - i] = s[i];                          \
      u32 sr = 128U - ((_l) << 3U);                \
      (_ret >>= sr);                               \
      (u128) _ret;                                 \
                                                   \
    })
#endif

#define SWAPNN(_x, _y, _l)                     \
  ({                                           \
                                               \
    char *d = (char *)(_x), *s = (char *)(_y); \
    u32   i, l = (_l) - 1;                     \
    for (i = 0; i <= l; i++)                   \
      d[l - i] = s[i];                         \
                                               \
  })

#ifdef AFL_LLVM_PASS
  #if defined(__linux__) || !defined(__ANDROID__)
    #define AFL_SR(s) (srandom(s))
    #define AFL_R(x) (random() % (x))
  #else
    #define AFL_SR(s) ((void)s)
    #define AFL_R(x) (arc4random_uniform(x))
  #endif
#else
  #if defined(__linux__) || !defined(__ANDROID__)
    #define SR(s) (srandom(s))
    #define R(x) (random() % (x))
  #else
    #define SR(s) ((void)s)
    #define R(x) (arc4random_uniform(x))
  #endif
#endif                                                    /* ^AFL_LLVM_PASS */

#define STRINGIFY_INTERNAL(x) #x
#define STRINGIFY(x) STRINGIFY_INTERNAL(x)

#define MEM_BARRIER() __asm__ volatile("" ::: "memory")

#if __GNUC__ < 6
  #ifndef likely
    #define likely(_x) (_x)
  #endif
  #ifndef unlikely
    #define unlikely(_x) (_x)
  #endif
#else
  #ifndef likely
    #define likely(_x) __builtin_expect(!!(_x), 1)
  #endif
  #ifndef unlikely
    #define unlikely(_x) __builtin_expect(!!(_x), 0)
  #endif
#endif

#endif                                                   /* ! _HAVE_TYPES_H */

