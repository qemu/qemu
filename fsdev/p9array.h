/*
 * P9Array - deep auto free C-array
 *
 * Copyright (c) 2021 Crudebyte
 *
 * Authors:
 *   Christian Schoenebeck <qemu_oss@crudebyte.com>
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
#ifndef QEMU_P9ARRAY_H
#define QEMU_P9ARRAY_H

/**
 * P9Array provides a mechanism to access arrays in common C-style (e.g. by
 * square bracket [] operator) in conjunction with reference variables that
 * perform deep auto free of the array when leaving the scope of the auto
 * reference variable. That means not only is the array itself automatically
 * freed, but also memory dynamically allocated by the individual array
 * elements.
 *
 * Example:
 *
 * Consider the following user struct @c Foo which shall be used as scalar
 * (element) type of an array:
 * @code
 * typedef struct Foo {
 *     int i;
 *     char *s;
 * } Foo;
 * @endcode
 * and assume it has the following function to free memory allocated by @c Foo
 * instances:
 * @code
 * void free_foo(Foo *foo) {
 *     free(foo->s);
 * }
 * @endcode
 * Add the following to a shared header file:
 * @code
 * P9ARRAY_DECLARE_TYPE(Foo);
 * @endcode
 * and the following to a C unit file:
 * @code
 * P9ARRAY_DEFINE_TYPE(Foo, free_foo);
 * @endcode
 * Finally the array may then be used like this:
 * @code
 * void doSomething(size_t n) {
 *     P9ARRAY_REF(Foo) foos = NULL;
 *     P9ARRAY_NEW(Foo, foos, n);
 *     for (size_t i = 0; i < n; ++i) {
 *         foos[i].i = i;
 *         foos[i].s = calloc(4096, 1);
 *         snprintf(foos[i].s, 4096, "foo %d", i);
 *         if (...) {
 *             return; // array auto freed here
 *         }
 *     }
 *     // array auto freed here
 * }
 * @endcode
 */

/**
 * P9ARRAY_DECLARE_TYPE() - Declares an array type for the passed @scalar_type.
 *
 * @scalar_type: type of the individual array elements
 *
 * This is typically used from a shared header file.
 */
#define P9ARRAY_DECLARE_TYPE(scalar_type) \
    typedef struct P9Array##scalar_type { \
        size_t len; \
        scalar_type first[]; \
    } P9Array##scalar_type; \
    \
    void p9array_new_##scalar_type(scalar_type **auto_var, size_t len); \
    void p9array_auto_free_##scalar_type(scalar_type **auto_var); \

/**
 * P9ARRAY_DEFINE_TYPE() - Defines an array type for the passed @scalar_type
 * and appropriate @scalar_cleanup_func.
 *
 * @scalar_type: type of the individual array elements
 * @scalar_cleanup_func: appropriate function to free memory dynamically
 *                       allocated by individual array elements before
 *
 * This is typically used from a C unit file.
 */
#define P9ARRAY_DEFINE_TYPE(scalar_type, scalar_cleanup_func) \
    void p9array_new_##scalar_type(scalar_type **auto_var, size_t len) \
    { \
        p9array_auto_free_##scalar_type(auto_var); \
        P9Array##scalar_type *arr = g_malloc0(sizeof(P9Array##scalar_type) + \
            len * sizeof(scalar_type)); \
        arr->len = len; \
        *auto_var = &arr->first[0]; \
    } \
    \
    void p9array_auto_free_##scalar_type(scalar_type **auto_var) \
    { \
        scalar_type *first = (*auto_var); \
        if (!first) { \
            return; \
        } \
        P9Array##scalar_type *arr = (P9Array##scalar_type *) ( \
            ((char *)first) - offsetof(P9Array##scalar_type, first) \
        ); \
        for (size_t i = 0; i < arr->len; ++i) { \
            scalar_cleanup_func(&arr->first[i]); \
        } \
        g_free(arr); \
    } \

/**
 * P9ARRAY_REF() - Declare a reference variable for an array.
 *
 * @scalar_type: type of the individual array elements
 *
 * Used to declare a reference variable (unique pointer) for an array. After
 * leaving the scope of the reference variable, the associated array is
 * automatically freed.
 */
#define P9ARRAY_REF(scalar_type) \
    __attribute((__cleanup__(p9array_auto_free_##scalar_type))) scalar_type*

/**
 * P9ARRAY_NEW() - Allocate a new array.
 *
 * @scalar_type: type of the individual array elements
 * @auto_var: destination reference variable
 * @len: amount of array elements to be allocated immediately
 *
 * Allocates a new array of passed @scalar_type with @len number of array
 * elements and assigns the created array to the reference variable
 * @auto_var.
 */
#define P9ARRAY_NEW(scalar_type, auto_var, len) \
    QEMU_BUILD_BUG_MSG( \
        !__builtin_types_compatible_p(scalar_type, typeof(*auto_var)), \
        "P9Array scalar type mismatch" \
    ); \
    p9array_new_##scalar_type((&auto_var), len)

#endif /* QEMU_P9ARRAY_H */
