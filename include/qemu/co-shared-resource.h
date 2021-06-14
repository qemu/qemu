/*
 * Helper functionality for distributing a fixed total amount of
 * an abstract resource among multiple coroutines.
 *
 * Copyright (c) 2019 Virtuozzo International GmbH
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef QEMU_CO_SHARED_RESOURCE_H
#define QEMU_CO_SHARED_RESOURCE_H

/* Accesses to co-shared-resource API are thread-safe */
typedef struct SharedResource SharedResource;

/*
 * Create SharedResource structure
 *
 * @total: total amount of some resource to be shared between clients
 */
SharedResource *shres_create(uint64_t total);

/*
 * Release SharedResource structure
 *
 * This function may only be called once everything allocated by all
 * clients has been deallocated.
 */
void shres_destroy(SharedResource *s);

/*
 * Try to allocate an amount of @n.  Return true on success, and false
 * if there is too little left of the collective resource to fulfill
 * the request.
 */
bool co_try_get_from_shres(SharedResource *s, uint64_t n);

/*
 * Allocate an amount of @n, and, if necessary, yield until
 * that becomes possible.
 */
void coroutine_fn co_get_from_shres(SharedResource *s, uint64_t n);

/*
 * Deallocate an amount of @n.  The total amount allocated by a caller
 * does not need to be deallocated/released with a single call, but may
 * be split over several calls.  For example, get(4), get(3), and then
 * put(5), put(2).
 */
void coroutine_fn co_put_to_shres(SharedResource *s, uint64_t n);


#endif /* QEMU_CO_SHARED_RESOURCE_H */
