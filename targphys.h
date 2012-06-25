/* Define target_phys_addr_t if it exists.  */

#ifndef TARGPHYS_H
#define TARGPHYS_H

#ifdef TARGET_PHYS_ADDR_BITS
/* target_phys_addr_t is the type of a physical address (its size can
   be different from 'target_ulong').  */

#if TARGET_PHYS_ADDR_BITS == 32
typedef uint32_t target_phys_addr_t;
#define TARGET_PHYS_ADDR_MAX UINT32_MAX
#define TARGET_FMT_plx "%08x"
/* Format strings for printing target_phys_addr_t types.
 * These are recommended over the less flexible TARGET_FMT_plx,
 * which is retained for the benefit of existing code.
 */
#define TARGET_PRIdPHYS PRId32
#define TARGET_PRIiPHYS PRIi32
#define TARGET_PRIoPHYS PRIo32
#define TARGET_PRIuPHYS PRIu32
#define TARGET_PRIxPHYS PRIx32
#define TARGET_PRIXPHYS PRIX32
#elif TARGET_PHYS_ADDR_BITS == 64
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
#endif

#endif
