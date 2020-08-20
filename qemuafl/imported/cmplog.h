/*
   american fuzzy lop++ - cmplog header
   ------------------------------------

   Originally written by Michal Zalewski

   Forkserver design by Jann Horn <jannhorn@googlemail.com>

   Now maintained by Marc Heuse <mh@mh-sec.de>,
                     Heiko Eißfeldt <heiko.eissfeldt@hexco.de>,
                     Andrea Fioraldi <andreafioraldi@gmail.com>,
                     Dominik Maier <mail@dmnk.co>

   Copyright 2016, 2017 Google Inc. All rights reserved.
   Copyright 2019-2020 AFLplusplus Project. All rights reserved.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at:

     http://www.apache.org/licenses/LICENSE-2.0

   Shared code to handle the shared memory. This is used by the fuzzer
   as well the other components like afl-tmin, afl-showmap, etc...

 */

#ifndef _AFL_CMPLOG_H
#define _AFL_CMPLOG_H

#include "config.h"
//#include "forkserver.h"

#define CMP_MAP_W 65536
#define CMP_MAP_H 256
#define CMP_MAP_RTN_H (CMP_MAP_H / 4)

#define SHAPE_BYTES(x) (x + 1)

#define CMP_TYPE_INS 0
#define CMP_TYPE_RTN 1

struct cmp_header {

  unsigned hits : 20;

  unsigned cnt : 20;
  unsigned id : 16;

  unsigned shape : 5;  // from 0 to 31
  unsigned type : 1;

} __attribute__((packed));

struct cmp_operands {

  u64 v0;
  u64 v1;

};

struct cmpfn_operands {

  u8 v0[32];
  u8 v1[32];

};

typedef struct cmp_operands cmp_map_list[CMP_MAP_H];

struct cmp_map {

  struct cmp_header   headers[CMP_MAP_W];
  struct cmp_operands log[CMP_MAP_W][CMP_MAP_H];

};

/* Execs the child */

//void cmplog_exec_child(afl_forkserver_t *fsrv, char **argv);

#endif

