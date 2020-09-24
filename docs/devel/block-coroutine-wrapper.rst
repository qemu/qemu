=======================
block-coroutine-wrapper
=======================

A lot of functions in QEMU block layer (see ``block/*``) can only be
called in coroutine context. Such functions are normally marked by the
coroutine_fn specifier. Still, sometimes we need to call them from
non-coroutine context; for this we need to start a coroutine, run the
needed function from it and wait for the coroutine to finish in a
BDRV_POLL_WHILE() loop. To run a coroutine we need a function with one
void* argument. So for each coroutine_fn function which needs a
non-coroutine interface, we should define a structure to pack the
parameters, define a separate function to unpack the parameters and
call the original function and finally define a new interface function
with same list of arguments as original one, which will pack the
parameters into a struct, create a coroutine, run it and wait in
BDRV_POLL_WHILE() loop. It's boring to create such wrappers by hand,
so we have a script to generate them.

Usage
=====

Assume we have defined the ``coroutine_fn`` function
``bdrv_co_foo(<some args>)`` and need a non-coroutine interface for it,
called ``bdrv_foo(<same args>)``. In this case the script can help. To
trigger the generation:

1. You need ``bdrv_foo`` declaration somewhere (for example, in
   ``block/coroutines.h``) with the ``generated_co_wrapper`` mark,
   like this:

.. code-block:: c

    int generated_co_wrapper bdrv_foo(<some args>);

2. You need to feed this declaration to block-coroutine-wrapper script.
   For this, add the .h (or .c) file with the declaration to the
   ``input: files(...)`` list of ``block_gen_c`` target declaration in
   ``block/meson.build``

You are done. During the build, coroutine wrappers will be generated in
``<BUILD_DIR>/block/block-gen.c``.

Links
=====

1. The script location is ``scripts/block-coroutine-wrapper.py``.

2. Generic place for private ``generated_co_wrapper`` declarations is
   ``block/coroutines.h``, for public declarations:
   ``include/block/block.h``

3. The core API of generated coroutine wrappers is placed in
   (not generated) ``block/block-gen.h``
