/* Define a_target_phys_addr if it exists.  */

#ifndef TARGPHYS_H
#define TARGPHYS_H

#ifdef TARGET_PHYS_ADDR_BITS
/* a_target_phys_addr is the type of a physical address (its size can
   be different from 'target_ulong'). We have sizeof(target_phys_addr)
   = max(sizeof(unsigned long),
   sizeof(size_of_target_physical_address)) because we must pass a
   host pointer to memory operations in some cases */

#if TARGET_PHYS_ADDR_BITS == 32
typedef uint32_t a_target_phys_addr;
#define TARGET_PHYS_ADDR_MAX UINT32_MAX
#define TARGET_FMT_plx "%08x"
#elif TARGET_PHYS_ADDR_BITS == 64
typedef uint64_t a_target_phys_addr;
#define TARGET_PHYS_ADDR_MAX UINT64_MAX
#define TARGET_FMT_plx "%016" PRIx64
#endif
#endif

#endif
