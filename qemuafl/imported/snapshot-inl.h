/*
   american fuzzy lop++ - snapshot helpers routines
   ------------------------------------------------

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

 */

// From AFL-Snapshot-LKM/include/afl_snapshot.h (must be kept synced)

#include <sys/ioctl.h>
#include <stdlib.h>
#include <fcntl.h>

#define AFL_SNAPSHOT_FILE_NAME "/dev/afl_snapshot"

#define AFL_SNAPSHOT_IOCTL_MAGIC 44313

#define AFL_SNAPSHOT_IOCTL_DO _IO(AFL_SNAPSHOT_IOCTL_MAGIC, 1)
#define AFL_SNAPSHOT_IOCTL_CLEAN _IO(AFL_SNAPSHOT_IOCTL_MAGIC, 2)
#define AFL_SNAPSHOT_EXCLUDE_VMRANGE \
  _IOR(AFL_SNAPSHOT_IOCTL_MAGIC, 3, struct afl_snapshot_vmrange_args *)
#define AFL_SNAPSHOT_INCLUDE_VMRANGE \
  _IOR(AFL_SNAPSHOT_IOCTL_MAGIC, 4, struct afl_snapshot_vmrange_args *)
#define AFL_SNAPSHOT_IOCTL_TAKE _IOR(AFL_SNAPSHOT_IOCTL_MAGIC, 5, int)
#define AFL_SNAPSHOT_IOCTL_RESTORE _IO(AFL_SNAPSHOT_IOCTL_MAGIC, 6)

// Trace new mmaped ares and unmap them on restore.
#define AFL_SNAPSHOT_MMAP 1
// Do not snapshot any page (by default all writeable not-shared pages
// are shanpshotted.
#define AFL_SNAPSHOT_BLOCK 2
// Snapshot file descriptor state, close newly opened descriptors
#define AFL_SNAPSHOT_FDS 4
// Snapshot registers state
#define AFL_SNAPSHOT_REGS 8
// Perform a restore when exit_group is invoked
#define AFL_SNAPSHOT_EXIT 16
// TODO(andrea) allow not COW snapshots (high perf on small processes)
// Disable COW, restore all the snapshotted pages
#define AFL_SNAPSHOT_NOCOW 32
// Do not snapshot Stack pages
#define AFL_SNAPSHOT_NOSTACK 64

struct afl_snapshot_vmrange_args {

  unsigned long start, end;

};

static int afl_snapshot_dev_fd;

static int afl_snapshot_init(void) {

  afl_snapshot_dev_fd = open(AFL_SNAPSHOT_FILE_NAME, 0);
  return afl_snapshot_dev_fd;

}

static void afl_snapshot_exclude_vmrange(void *start, void *end) {

  struct afl_snapshot_vmrange_args args = {(unsigned long)start,
                                           (unsigned long)end};
  ioctl(afl_snapshot_dev_fd, AFL_SNAPSHOT_EXCLUDE_VMRANGE, &args);

}

static void afl_snapshot_include_vmrange(void *start, void *end) {

  struct afl_snapshot_vmrange_args args = {(unsigned long)start,
                                           (unsigned long)end};
  ioctl(afl_snapshot_dev_fd, AFL_SNAPSHOT_INCLUDE_VMRANGE, &args);

}

static int afl_snapshot_take(int config) {

  return ioctl(afl_snapshot_dev_fd, AFL_SNAPSHOT_IOCTL_TAKE, config);

}

static int afl_snapshot_do(void) {

  return ioctl(afl_snapshot_dev_fd, AFL_SNAPSHOT_IOCTL_DO);

}

static void afl_snapshot_restore(void) {

  ioctl(afl_snapshot_dev_fd, AFL_SNAPSHOT_IOCTL_RESTORE);

}

static void afl_snapshot_clean(void) {

  ioctl(afl_snapshot_dev_fd, AFL_SNAPSHOT_IOCTL_CLEAN);

}

