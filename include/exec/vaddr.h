/* Define vaddr.  */

#ifndef VADDR_H
#define VADDR_H

/**
 * vaddr:
 * Type wide enough to contain any #target_ulong virtual address.
 */
typedef uint64_t vaddr;
#define VADDR_PRId PRId64
#define VADDR_PRIu PRIu64
#define VADDR_PRIo PRIo64
#define VADDR_PRIx PRIx64
#define VADDR_PRIX PRIX64
#define VADDR_MAX UINT64_MAX

#endif
