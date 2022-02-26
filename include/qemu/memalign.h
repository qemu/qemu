/*
 * Allocation and free functions for aligned memory
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_MEMALIGN_H
#define QEMU_MEMALIGN_H

/**
 * qemu_try_memalign: Allocate aligned memory
 * @alignment: required alignment, in bytes
 * @size: size of allocation, in bytes
 *
 * Allocate memory on an aligned boundary (i.e. the returned
 * address will be an exact multiple of @alignment).
 * @alignment must be a power of 2, or the function will assert().
 * On success, returns allocated memory; on failure, returns NULL.
 *
 * The memory allocated through this function must be freed via
 * qemu_vfree() (and not via free()).
 */
void *qemu_try_memalign(size_t alignment, size_t size);
/**
 * qemu_memalign: Allocate aligned memory, without failing
 * @alignment: required alignment, in bytes
 * @size: size of allocation, in bytes
 *
 * Allocate memory in the same way as qemu_try_memalign(), but
 * abort() with an error message if the memory allocation fails.
 *
 * The memory allocated through this function must be freed via
 * qemu_vfree() (and not via free()).
 */
void *qemu_memalign(size_t alignment, size_t size);
/**
 * qemu_vfree: Free memory allocated through qemu_memalign
 * @ptr: memory to free
 *
 * This function must be used to free memory allocated via qemu_memalign()
 * or qemu_try_memalign(). (Using the wrong free function will cause
 * subtle bugs on Windows hosts.)
 */
void qemu_vfree(void *ptr);
/*
 * It's an analog of GLIB's g_autoptr_cleanup_generic_gfree(), used to define
 * g_autofree macro.
 */
static inline void qemu_cleanup_generic_vfree(void *p)
{
  void **pp = (void **)p;
  qemu_vfree(*pp);
}

/*
 * Analog of g_autofree, but qemu_vfree is called on cleanup instead of g_free.
 */
#define QEMU_AUTO_VFREE __attribute__((cleanup(qemu_cleanup_generic_vfree)))

#endif
