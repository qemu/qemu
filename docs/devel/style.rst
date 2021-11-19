.. _coding-style:

=================
QEMU Coding Style
=================

.. contents:: Table of Contents

Please use the script checkpatch.pl in the scripts directory to check
patches before submitting.

Formatting and style
********************

Whitespace
==========

Of course, the most important aspect in any coding style is whitespace.
Crusty old coders who have trouble spotting the glasses on their noses
can tell the difference between a tab and eight spaces from a distance
of approximately fifteen parsecs.  Many a flamewar has been fought and
lost on this issue.

QEMU indents are four spaces.  Tabs are never used, except in Makefiles
where they have been irreversibly coded into the syntax.
Spaces of course are superior to tabs because:

* You have just one way to specify whitespace, not two.  Ambiguity breeds
  mistakes.
* The confusion surrounding 'use tabs to indent, spaces to justify' is gone.
* Tab indents push your code to the right, making your screen seriously
  unbalanced.
* Tabs will be rendered incorrectly on editors who are misconfigured not
  to use tab stops of eight positions.
* Tabs are rendered badly in patches, causing off-by-one errors in almost
  every line.
* It is the QEMU coding style.

Do not leave whitespace dangling off the ends of lines.

Multiline Indent
----------------

There are several places where indent is necessary:

* if/else
* while/for
* function definition & call

When breaking up a long line to fit within line width, we need a proper indent
for the following lines.

In case of if/else, while/for, align the secondary lines just after the
opening parenthesis of the first.

For example:

.. code-block:: c

    if (a == 1 &&
        b == 2) {

    while (a == 1 &&
           b == 2) {

In case of function, there are several variants:

* 4 spaces indent from the beginning
* align the secondary lines just after the opening parenthesis of the first

For example:

.. code-block:: c

    do_something(x, y,
        z);

    do_something(x, y,
                 z);

    do_something(x, do_another(y,
                               z));

Line width
==========

Lines should be 80 characters; try not to make them longer.

Sometimes it is hard to do, especially when dealing with QEMU subsystems
that use long function or symbol names. If wrapping the line at 80 columns
is obviously less readable and more awkward, prefer not to wrap it; better
to have an 85 character line than one which is awkwardly wrapped.

Even in that case, try not to make lines much longer than 80 characters.
(The checkpatch script will warn at 100 characters, but this is intended
as a guard against obviously-overlength lines, not a target.)

Rationale:

* Some people like to tile their 24" screens with a 6x4 matrix of 80x24
  xterms and use vi in all of them.  The best way to punish them is to
  let them keep doing it.
* Code and especially patches is much more readable if limited to a sane
  line length.  Eighty is traditional.
* The four-space indentation makes the most common excuse ("But look
  at all that white space on the left!") moot.
* It is the QEMU coding style.

Naming
======

Variables are lower_case_with_underscores; easy to type and read.  Structured
type names are in CamelCase; harder to type but standing out.  Enum type
names and function type names should also be in CamelCase.  Scalar type
names are lower_case_with_underscores_ending_with_a_t, like the POSIX
uint64_t and family.  Note that this last convention contradicts POSIX
and is therefore likely to be changed.

Variable Naming Conventions
---------------------------

A number of short naming conventions exist for variables that use
common QEMU types. For example, the architecture independent CPUState
is often held as a ``cs`` pointer variable, whereas the concrete
CPUArchState is usually held in a pointer called ``env``.

Likewise, in device emulation code the common DeviceState is usually
called ``dev``.

Function Naming Conventions
---------------------------

Wrapped version of standard library or GLib functions use a ``qemu_``
prefix to alert readers that they are seeing a wrapped version, for
example ``qemu_strtol`` or ``qemu_mutex_lock``.  Other utility functions
that are widely called from across the codebase should not have any
prefix, for example ``pstrcpy`` or bit manipulation functions such as
``find_first_bit``.

The ``qemu_`` prefix is also used for functions that modify global
emulator state, for example ``qemu_add_vm_change_state_handler``.
However, if there is an obvious subsystem-specific prefix it should be
used instead.

Public functions from a file or subsystem (declared in headers) tend
to have a consistent prefix to show where they came from. For example,
``tlb_`` for functions from ``cputlb.c`` or ``cpu_`` for functions
from cpus.c.

If there are two versions of a function to be called with or without a
lock held, the function that expects the lock to be already held
usually uses the suffix ``_locked``.


Block structure
===============

Every indented statement is braced; even if the block contains just one
statement.  The opening brace is on the line that contains the control
flow statement that introduces the new block; the closing brace is on the
same line as the else keyword, or on a line by itself if there is no else
keyword.  Example:

.. code-block:: c

    if (a == 5) {
        printf("a was 5.\n");
    } else if (a == 6) {
        printf("a was 6.\n");
    } else {
        printf("a was something else entirely.\n");
    }

Note that 'else if' is considered a single statement; otherwise a long if/
else if/else if/.../else sequence would need an indent for every else
statement.

An exception is the opening brace for a function; for reasons of tradition
and clarity it comes on a line by itself:

.. code-block:: c

    void a_function(void)
    {
        do_something();
    }

Rationale: a consistent (except for functions...) bracing style reduces
ambiguity and avoids needless churn when lines are added or removed.
Furthermore, it is the QEMU coding style.

Declarations
============

Mixed declarations (interleaving statements and declarations within
blocks) are generally not allowed; declarations should be at the beginning
of blocks.

Every now and then, an exception is made for declarations inside a
#ifdef or #ifndef block: if the code looks nicer, such declarations can
be placed at the top of the block even if there are statements above.
On the other hand, however, it's often best to move that #ifdef/#ifndef
block to a separate function altogether.

Conditional statements
======================

When comparing a variable for (in)equality with a constant, list the
constant on the right, as in:

.. code-block:: c

    if (a == 1) {
        /* Reads like: "If a equals 1" */
        do_something();
    }

Rationale: Yoda conditions (as in 'if (1 == a)') are awkward to read.
Besides, good compilers already warn users when '==' is mis-typed as '=',
even when the constant is on the right.

Comment style
=============

We use traditional C-style /``*`` ``*``/ comments and avoid // comments.

Rationale: The // form is valid in C99, so this is purely a matter of
consistency of style. The checkpatch script will warn you about this.

Multiline comment blocks should have a row of stars on the left,
and the initial /``*`` and terminating ``*``/ both on their own lines:

.. code-block:: c

    /*
     * like
     * this
     */

This is the same format required by the Linux kernel coding style.

(Some of the existing comments in the codebase use the GNU Coding
Standards form which does not have stars on the left, or other
variations; avoid these when writing new comments, but don't worry
about converting to the preferred form unless you're editing that
comment anyway.)

Rationale: Consistency, and ease of visually picking out a multiline
comment from the surrounding code.

Language usage
**************

Preprocessor
============

Variadic macros
---------------

For variadic macros, stick with this C99-like syntax:

.. code-block:: c

    #define DPRINTF(fmt, ...)                                       \
        do { printf("IRQ: " fmt, ## __VA_ARGS__); } while (0)

Include directives
------------------

Order include directives as follows:

.. code-block:: c

    #include "qemu/osdep.h"  /* Always first... */
    #include <...>           /* then system headers... */
    #include "..."           /* and finally QEMU headers. */

The "qemu/osdep.h" header contains preprocessor macros that affect the behavior
of core system headers like <stdint.h>.  It must be the first include so that
core system headers included by external libraries get the preprocessor macros
that QEMU depends on.

Do not include "qemu/osdep.h" from header files since the .c file will have
already included it.

C types
=======

It should be common sense to use the right type, but we have collected
a few useful guidelines here.

Scalars
-------

If you're using "int" or "long", odds are good that there's a better type.
If a variable is counting something, it should be declared with an
unsigned type.

If it's host memory-size related, size_t should be a good choice (use
ssize_t only if required). Guest RAM memory offsets must use ram_addr_t,
but only for RAM, it may not cover whole guest address space.

If it's file-size related, use off_t.
If it's file-offset related (i.e., signed), use off_t.
If it's just counting small numbers use "unsigned int";
(on all but oddball embedded systems, you can assume that that
type is at least four bytes wide).

In the event that you require a specific width, use a standard type
like int32_t, uint32_t, uint64_t, etc.  The specific types are
mandatory for VMState fields.

Don't use Linux kernel internal types like u32, __u32 or __le32.

Use hwaddr for guest physical addresses except pcibus_t
for PCI addresses.  In addition, ram_addr_t is a QEMU internal address
space that maps guest RAM physical addresses into an intermediate
address space that can map to host virtual address spaces.  Generally
speaking, the size of guest memory can always fit into ram_addr_t but
it would not be correct to store an actual guest physical address in a
ram_addr_t.

For CPU virtual addresses there are several possible types.
vaddr is the best type to use to hold a CPU virtual address in
target-independent code. It is guaranteed to be large enough to hold a
virtual address for any target, and it does not change size from target
to target. It is always unsigned.
target_ulong is a type the size of a virtual address on the CPU; this means
it may be 32 or 64 bits depending on which target is being built. It should
therefore be used only in target-specific code, and in some
performance-critical built-per-target core code such as the TLB code.
There is also a signed version, target_long.
abi_ulong is for the ``*``-user targets, and represents a type the size of
'void ``*``' in that target's ABI. (This may not be the same as the size of a
full CPU virtual address in the case of target ABIs which use 32 bit pointers
on 64 bit CPUs, like sparc32plus.) Definitions of structures that must match
the target's ABI must use this type for anything that on the target is defined
to be an 'unsigned long' or a pointer type.
There is also a signed version, abi_long.

Of course, take all of the above with a grain of salt.  If you're about
to use some system interface that requires a type like size_t, pid_t or
off_t, use matching types for any corresponding variables.

Also, if you try to use e.g., "unsigned int" as a type, and that
conflicts with the signedness of a related variable, sometimes
it's best just to use the *wrong* type, if "pulling the thread"
and fixing all related variables would be too invasive.

Finally, while using descriptive types is important, be careful not to
go overboard.  If whatever you're doing causes warnings, or requires
casts, then reconsider or ask for help.

Pointers
--------

Ensure that all of your pointers are "const-correct".
Unless a pointer is used to modify the pointed-to storage,
give it the "const" attribute.  That way, the reader knows
up-front that this is a read-only pointer.  Perhaps more
importantly, if we're diligent about this, when you see a non-const
pointer, you're guaranteed that it is used to modify the storage
it points to, or it is aliased to another pointer that is.

Typedefs
--------

Typedefs are used to eliminate the redundant 'struct' keyword, since type
names have a different style than other identifiers ("CamelCase" versus
"snake_case").  Each named struct type should have a CamelCase name and a
corresponding typedef.

Since certain C compilers choke on duplicated typedefs, you should avoid
them and declare a typedef only in one header file.  For common types,
you can use "include/qemu/typedefs.h" for example.  However, as a matter
of convenience it is also perfectly fine to use forward struct
definitions instead of typedefs in headers and function prototypes; this
avoids problems with duplicated typedefs and reduces the need to include
headers from other headers.

Reserved namespaces in C and POSIX
----------------------------------

Underscore capital, double underscore, and underscore 't' suffixes should be
avoided.

Low level memory management
===========================

Use of the ``malloc/free/realloc/calloc/valloc/memalign/posix_memalign``
APIs is not allowed in the QEMU codebase. Instead of these routines,
use the GLib memory allocation routines
``g_malloc/g_malloc0/g_new/g_new0/g_realloc/g_free``
or QEMU's ``qemu_memalign/qemu_blockalign/qemu_vfree`` APIs.

Please note that ``g_malloc`` will exit on allocation failure, so
there is no need to test for failure (as you would have to with
``malloc``). Generally using ``g_malloc`` on start-up is fine as the
result of a failure to allocate memory is going to be a fatal exit
anyway. There may be some start-up cases where failing is unreasonable
(for example speculatively loading a large debug symbol table).

Care should be taken to avoid introducing places where the guest could
trigger an exit by causing a large allocation. For small allocations,
of the order of 4k, a failure to allocate is likely indicative of an
overloaded host and allowing ``g_malloc`` to ``exit`` is a reasonable
approach. However for larger allocations where we could realistically
fall-back to a smaller one if need be we should use functions like
``g_try_new`` and check the result. For example this is valid approach
for a time/space trade-off like ``tlb_mmu_resize_locked`` in the
SoftMMU TLB code.

If the lifetime of the allocation is within the function and there are
multiple exist paths you can also improve the readability of the code
by using ``g_autofree`` and related annotations. See :ref:`autofree-ref`
for more details.

Calling ``g_malloc`` with a zero size is valid and will return NULL.

Prefer ``g_new(T, n)`` instead of ``g_malloc(sizeof(T) * n)`` for the following
reasons:

* It catches multiplication overflowing size_t;
* It returns T ``*`` instead of void ``*``, letting compiler catch more type errors.

Declarations like

.. code-block:: c

    T *v = g_malloc(sizeof(*v))

are acceptable, though.

Memory allocated by ``qemu_memalign`` or ``qemu_blockalign`` must be freed with
``qemu_vfree``, since breaking this will cause problems on Win32.

String manipulation
===================

Do not use the strncpy function.  As mentioned in the man page, it does *not*
guarantee a NULL-terminated buffer, which makes it extremely dangerous to use.
It also zeros trailing destination bytes out to the specified length.  Instead,
use this similar function when possible, but note its different signature:

.. code-block:: c

    void pstrcpy(char *dest, int dest_buf_size, const char *src)

Don't use strcat because it can't check for buffer overflows, but:

.. code-block:: c

    char *pstrcat(char *buf, int buf_size, const char *s)

The same limitation exists with sprintf and vsprintf, so use snprintf and
vsnprintf.

QEMU provides other useful string functions:

.. code-block:: c

    int strstart(const char *str, const char *val, const char **ptr)
    int stristart(const char *str, const char *val, const char **ptr)
    int qemu_strnlen(const char *s, int max_len)

There are also replacement character processing macros for isxyz and toxyz,
so instead of e.g. isalnum you should use qemu_isalnum.

Because of the memory management rules, you must use g_strdup/g_strndup
instead of plain strdup/strndup.

Printf-style functions
======================

Whenever you add a new printf-style function, i.e., one with a format
string argument and following "..." in its prototype, be sure to use
gcc's printf attribute directive in the prototype.

This makes it so gcc's -Wformat and -Wformat-security options can do
their jobs and cross-check format strings with the number and types
of arguments.

C standard, implementation defined and undefined behaviors
==========================================================

C code in QEMU should be written to the C99 language specification. A copy
of the final version of the C99 standard with corrigenda TC1, TC2, and TC3
included, formatted as a draft, can be downloaded from:

    `<http://www.open-std.org/jtc1/sc22/WG14/www/docs/n1256.pdf>`_

The C language specification defines regions of undefined behavior and
implementation defined behavior (to give compiler authors enough leeway to
produce better code).  In general, code in QEMU should follow the language
specification and avoid both undefined and implementation defined
constructs. ("It works fine on the gcc I tested it with" is not a valid
argument...) However there are a few areas where we allow ourselves to
assume certain behaviors because in practice all the platforms we care about
behave in the same way and writing strictly conformant code would be
painful. These are:

* you may assume that integers are 2s complement representation
* you may assume that right shift of a signed integer duplicates
  the sign bit (ie it is an arithmetic shift, not a logical shift)

In addition, QEMU assumes that the compiler does not use the latitude
given in C99 and C11 to treat aspects of signed '<<' as undefined, as
documented in the GNU Compiler Collection manual starting at version 4.0.

.. _autofree-ref:

Automatic memory deallocation
=============================

QEMU has a mandatory dependency either the GCC or CLang compiler. As
such it has the freedom to make use of a C language extension for
automatically running a cleanup function when a stack variable goes
out of scope. This can be used to simplify function cleanup paths,
often allowing many goto jumps to be eliminated, through automatic
free'ing of memory.

The GLib2 library provides a number of functions/macros for enabling
automatic cleanup:

  `<https://developer.gnome.org/glib/stable/glib-Miscellaneous-Macros.html>`_

Most notably:

* g_autofree - will invoke g_free() on the variable going out of scope

* g_autoptr - for structs / objects, will invoke the cleanup func created
  by a previous use of G_DEFINE_AUTOPTR_CLEANUP_FUNC. This is
  supported for most GLib data types and GObjects

For example, instead of

.. code-block:: c

    int somefunc(void) {
        int ret = -1;
        char *foo = g_strdup_printf("foo%", "wibble");
        GList *bar = .....

        if (eek) {
           goto cleanup;
        }

        ret = 0;

      cleanup:
        g_free(foo);
        g_list_free(bar);
        return ret;
    }

Using g_autofree/g_autoptr enables the code to be written as:

.. code-block:: c

    int somefunc(void) {
        g_autofree char *foo = g_strdup_printf("foo%", "wibble");
        g_autoptr (GList) bar = .....

        if (eek) {
           return -1;
        }

        return 0;
    }

While this generally results in simpler, less leak-prone code, there
are still some caveats to beware of

* Variables declared with g_auto* MUST always be initialized,
  otherwise the cleanup function will use uninitialized stack memory

* If a variable declared with g_auto* holds a value which must
  live beyond the life of the function, that value must be saved
  and the original variable NULL'd out. This can be simpler using
  g_steal_pointer


.. code-block:: c

    char *somefunc(void) {
        g_autofree char *foo = g_strdup_printf("foo%", "wibble");
        g_autoptr (GList) bar = .....

        if (eek) {
           return NULL;
        }

        return g_steal_pointer(&foo);
    }


QEMU Specific Idioms
********************

Error handling and reporting
============================

Reporting errors to the human user
----------------------------------

Do not use printf(), fprintf() or monitor_printf().  Instead, use
error_report() or error_vreport() from error-report.h.  This ensures the
error is reported in the right place (current monitor or stderr), and in
a uniform format.

Use error_printf() & friends to print additional information.

error_report() prints the current location.  In certain common cases
like command line parsing, the current location is tracked
automatically.  To manipulate it manually, use the loc_``*``() from
error-report.h.

Propagating errors
------------------

An error can't always be reported to the user right where it's detected,
but often needs to be propagated up the call chain to a place that can
handle it.  This can be done in various ways.

The most flexible one is Error objects.  See error.h for usage
information.

Use the simplest suitable method to communicate success / failure to
callers.  Stick to common methods: non-negative on success / -1 on
error, non-negative / -errno, non-null / null, or Error objects.

Example: when a function returns a non-null pointer on success, and it
can fail only in one way (as far as the caller is concerned), returning
null on failure is just fine, and certainly simpler and a lot easier on
the eyes than propagating an Error object through an Error ``*````*`` parameter.

Example: when a function's callers need to report details on failure
only the function really knows, use Error ``*````*``, and set suitable errors.

Do not report an error to the user when you're also returning an error
for somebody else to handle.  Leave the reporting to the place that
consumes the error returned.

Handling errors
---------------

Calling exit() is fine when handling configuration errors during
startup.  It's problematic during normal operation.  In particular,
monitor commands should never exit().

Do not call exit() or abort() to handle an error that can be triggered
by the guest (e.g., some unimplemented corner case in guest code
translation or device emulation).  Guests should not be able to
terminate QEMU.

Note that &error_fatal is just another way to exit(1), and &error_abort
is just another way to abort().


trace-events style
==================

0x prefix
---------

In trace-events files, use a '0x' prefix to specify hex numbers, as in:

.. code-block:: c

    some_trace(unsigned x, uint64_t y) "x 0x%x y 0x" PRIx64

An exception is made for groups of numbers that are hexadecimal by
convention and separated by the symbols '.', '/', ':', or ' ' (such as
PCI bus id):

.. code-block:: c

    another_trace(int cssid, int ssid, int dev_num) "bus id: %x.%x.%04x"

However, you can use '0x' for such groups if you want. Anyway, be sure that
it is obvious that numbers are in hex, ex.:

.. code-block:: c

    data_dump(uint8_t c1, uint8_t c2, uint8_t c3) "bytes (in hex): %02x %02x %02x"

Rationale: hex numbers are hard to read in logs when there is no 0x prefix,
especially when (occasionally) the representation doesn't contain any letters
and especially in one line with other decimal numbers. Number groups are allowed
to not use '0x' because for some things notations like %x.%x.%x are used not
only in QEMU. Also dumping raw data bytes with '0x' is less readable.

'#' printf flag
---------------

Do not use printf flag '#', like '%#x'.

Rationale: there are two ways to add a '0x' prefix to printed number: '0x%...'
and '%#...'. For consistency the only one way should be used. Arguments for
'0x%' are:

* it is more popular
* '%#' omits the 0x for the value 0 which makes output inconsistent
