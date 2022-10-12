// NOTE. This file is a manually generated spec for the API to this plugin.
// It is intended to be consumed by apigen.py. Actually pycparser is what
// consumes it.

// Other plugin .c, .h, and cpp files SHOULD NOT include this file.
// It looks like a real header but its not. Those typedef voids below are a dead
// giveaway. Those are there to fake out pycparser which, otherwise, would require
// lots of code to be pulled in to get definitions for those types, which aren't
// necessary for autogenerating code.

// Please always put the actual prototypes in a separate file, XXX_int_fns.h.
// It is fine to #include that file.

// Also, you CANT put and typedefs or #includes in XXX_int_fns.h as it bollocks
// pycparser. Unless they are ones that are easy to find like stdint.h.
// Just the prototypes, please.


typedef void OsiProc;
typedef void OsiProcHandle;
typedef void CPUState;
typedef void target_ptr_t;

#include "osi_linux_int_fns.h"

/* vim:set tabstop=4 softtabstop=4 expandtab: */
