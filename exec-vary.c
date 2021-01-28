/*
 * Variable page size handling
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"

#define IN_EXEC_VARY 1

#include "exec/exec-all.h"

#ifdef TARGET_PAGE_BITS_VARY
# ifdef CONFIG_ATTRIBUTE_ALIAS
/*
 * We want to declare the "target_page" variable as const, which tells
 * the compiler that it can cache any value that it reads across calls.
 * This avoids multiple assertions and multiple reads within any one user.
 *
 * This works because we finish initializing the data before we ever read
 * from the "target_page" symbol.
 *
 * This also requires that we have a non-constant symbol by which we can
 * perform the actual initialization, and which forces the data to be
 * allocated within writable memory.  Thus "init_target_page", and we use
 * that symbol exclusively in the two functions that initialize this value.
 *
 * The "target_page" symbol is created as an alias of "init_target_page".
 */
static TargetPageBits init_target_page;

/*
 * Note that this is *not* a redundant decl, this is the definition of
 * the "target_page" symbol.  The syntax for this definition requires
 * the use of the extern keyword.  This seems to be a GCC bug in
 * either the syntax for the alias attribute or in -Wredundant-decls.
 *
 * See https://gcc.gnu.org/bugzilla/show_bug.cgi?id=91765
 */
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wredundant-decls"

extern const TargetPageBits target_page
    __attribute__((alias("init_target_page")));

#  pragma GCC diagnostic pop
# else
/*
 * When aliases are not supported then we force two different declarations,
 * by way of suppressing the header declaration with IN_EXEC_VARY.
 * We assume that on such an old compiler, LTO cannot be used, and so the
 * compiler cannot not detect the mismatched declarations, and all is well.
 */
TargetPageBits target_page;
#  define init_target_page target_page
# endif
#endif

bool set_preferred_target_page_bits(int bits)
{
    /*
     * The target page size is the lowest common denominator for all
     * the CPUs in the system, so we can only make it smaller, never
     * larger. And we can't make it smaller once we've committed to
     * a particular size.
     */
#ifdef TARGET_PAGE_BITS_VARY
    assert(bits >= TARGET_PAGE_BITS_MIN);
    if (init_target_page.bits == 0 || init_target_page.bits > bits) {
        if (init_target_page.decided) {
            return false;
        }
        init_target_page.bits = bits;
    }
#endif
    return true;
}

void finalize_target_page_bits(void)
{
#ifdef TARGET_PAGE_BITS_VARY
    if (init_target_page.bits == 0) {
        init_target_page.bits = TARGET_PAGE_BITS_MIN;
    }
    init_target_page.mask = (target_long)-1 << init_target_page.bits;
    init_target_page.decided = true;

    /*
     * For the benefit of an -flto build, prevent the compiler from
     * hoisting a read from target_page before we finish initializing.
     */
    barrier();
#endif
}
