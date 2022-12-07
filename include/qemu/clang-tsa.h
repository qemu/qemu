#ifndef CLANG_TSA_H
#define CLANG_TSA_H

/*
 * Copyright 2018 Jarkko Hietaniemi <jhi@iki.fi>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without
 * limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/* http://clang.llvm.org/docs/ThreadSafetyAnalysis.html
 *
 * TSA is available since clang 3.6-ish.
 */
#ifdef __clang__
#  define TSA(x)   __attribute__((x))
#else
#  define TSA(x)   /* No TSA, make TSA attributes no-ops. */
#endif

/* TSA_CAPABILITY() is used to annotate typedefs:
 *
 * typedef pthread_mutex_t TSA_CAPABILITY("mutex") tsa_mutex;
 */
#define TSA_CAPABILITY(x) TSA(capability(x))

/* TSA_GUARDED_BY() is used to annotate global variables,
 * the data is guarded:
 *
 * Foo foo TSA_GUARDED_BY(mutex);
 */
#define TSA_GUARDED_BY(x) TSA(guarded_by(x))

/* TSA_PT_GUARDED_BY() is used to annotate global pointers, the data
 * behind the pointer is guarded.
 *
 * Foo* ptr TSA_PT_GUARDED_BY(mutex);
 */
#define TSA_PT_GUARDED_BY(x) TSA(pt_guarded_by(x))

/* The TSA_REQUIRES() is used to annotate functions: the caller of the
 * function MUST hold the resource, the function will NOT release it.
 *
 * More than one mutex may be specified, comma-separated.
 *
 * void Foo(void) TSA_REQUIRES(mutex);
 */
#define TSA_REQUIRES(...) TSA(requires_capability(__VA_ARGS__))
#define TSA_REQUIRES_SHARED(...) TSA(requires_shared_capability(__VA_ARGS__))

/* TSA_EXCLUDES() is used to annotate functions: the caller of the
 * function MUST NOT hold resource, the function first acquires the
 * resource, and then releases it.
 *
 * More than one mutex may be specified, comma-separated.
 *
 * void Foo(void) TSA_EXCLUDES(mutex);
 */
#define TSA_EXCLUDES(...) TSA(locks_excluded(__VA_ARGS__))

/* TSA_ACQUIRE() is used to annotate functions: the caller of the
 * function MUST NOT hold the resource, the function will acquire the
 * resource, but NOT release it.
 *
 * More than one mutex may be specified, comma-separated.
 *
 * void Foo(void) TSA_ACQUIRE(mutex);
 */
#define TSA_ACQUIRE(...) TSA(acquire_capability(__VA_ARGS__))
#define TSA_ACQUIRE_SHARED(...) TSA(acquire_shared_capability(__VA_ARGS__))

/* TSA_RELEASE() is used to annotate functions: the caller of the
 * function MUST hold the resource, but the function will then release it.
 *
 * More than one mutex may be specified, comma-separated.
 *
 * void Foo(void) TSA_RELEASE(mutex);
 */
#define TSA_RELEASE(...) TSA(release_capability(__VA_ARGS__))
#define TSA_RELEASE_SHARED(...) TSA(release_shared_capability(__VA_ARGS__))

/* TSA_NO_TSA is used to annotate functions.  Use only when you need to.
 *
 * void Foo(void) TSA_NO_TSA;
 */
#define TSA_NO_TSA TSA(no_thread_safety_analysis)

/*
 * TSA_ASSERT() is used to annotate functions: This function will assert that
 * the lock is held. When it returns, the caller of the function is assumed to
 * already hold the resource.
 *
 * More than one mutex may be specified, comma-separated.
 */
#define TSA_ASSERT(...) TSA(assert_capability(__VA_ARGS__))
#define TSA_ASSERT_SHARED(...) TSA(assert_shared_capability(__VA_ARGS__))

#endif /* #ifndef CLANG_TSA_H */
