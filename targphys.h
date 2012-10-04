/* Define target_phys_addr_t if it exists.  */

#ifndef TARGPHYS_H
#define TARGPHYS_H

#define TARGET_PHYS_ADDR_BITS 64
/* target_phys_addr_t is the type of a physical address (its size can
   be different from 'target_ulong').  */

typedef uint64_t target_phys_addr_t;
#define TARGET_PHYS_ADDR_MAX UINT64_MAX
#define TARGET_FMT_plx "%016" PRIx64
#define TARGET_PRIdPHYS PRId64
#define TARGET_PRIiPHYS PRIi64
#define TARGET_PRIoPHYS PRIo64
#define TARGET_PRIuPHYS PRIu64
#define TARGET_PRIxPHYS PRIx64
#define TARGET_PRIXPHYS PRIX64

#endif
