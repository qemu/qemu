============================
Control-Flow Integrity (CFI)
============================

This document describes the current control-flow integrity (CFI) mechanism in
QEMU. How it can be enabled, its benefits and deficiencies, and how it affects
new and existing code in QEMU

Basics
------

CFI is a hardening technique that focusing on guaranteeing that indirect
function calls have not been altered by an attacker.
The type used in QEMU is a forward-edge control-flow integrity that ensures
function calls performed through function pointers, always call a "compatible"
function. A compatible function is a function with the same signature of the
function pointer declared in the source code.

This type of CFI is entirely compiler-based and relies on the compiler knowing
the signature of every function and every function pointer used in the code.
As of now, the only compiler that provides support for CFI is Clang.

CFI is best used on production binaries, to protect against unknown attack
vectors.

In case of a CFI violation (i.e. call to a non-compatible function) QEMU will
terminate abruptly, to stop the possible attack.

Building with CFI
-----------------

NOTE: CFI requires the use of link-time optimization. Therefore, when CFI is
selected, LTO will be automatically enabled.

To build with CFI, the minimum requirement is Clang 6+. If you
are planning to also enable fuzzing, then Clang 11+ is needed (more on this
later).

Given the use of LTO, a version of AR that supports LLVM IR is required.
The easies way of doing this is by selecting the AR provided by LLVM::

 AR=llvm-ar-9 CC=clang-9 CXX=lang++-9 /path/to/configure --enable-cfi

CFI is enabled on every binary produced.

If desired, an additional flag to increase the verbosity of the output in case
of a CFI violation is offered (``--enable-debug-cfi``).

Using QEMU built with CFI
-------------------------

A binary with CFI will work exactly like a standard binary. In case of a CFI
violation, the binary will terminate with an illegal instruction signal.

Incompatible code with CFI
--------------------------

As mentioned above, CFI is entirely compiler-based and therefore relies on
compile-time knowledge of the code. This means that, while generally supported
for most code, some specific use pattern can break CFI compatibility, and
create false-positives. The two main patterns that can cause issues are:

* Just-in-time compiled code: since such code is created at runtime, the jump
  to the buffer containing JIT code will fail.

* Libraries loaded dynamically, e.g. with dlopen/dlsym, since the library was
  not known at compile time.

Current areas of QEMU that are not entirely compatible with CFI are:

1. TCG, since the idea of TCG is to pre-compile groups of instructions at
   runtime to speed-up interpretation, quite similarly to a JIT compiler

2. TCI, where the interpreter has to interpret the generic *call* operation

3. Plugins, since a plugin is implemented as an external library

4. Modules, since they are implemented as an external library

5. Directly calling signal handlers from the QEMU source code, since the
   signal handler may have been provided by an external library or even plugged
   at runtime.

Disabling CFI for a specific function
-------------------------------------

If you are working on function that is performing a call using an
incompatible way, as described before, you can selectively disable CFI checks
for such function by using the decorator ``QEMU_DISABLE_CFI`` at function
definition, and add an explanation on why the function is not compatible
with CFI. An example of the use of ``QEMU_DISABLE_CFI`` is provided here::

	/*
	 * Disable CFI checks.
	 * TCG creates binary blobs at runtime, with the transformed code.
	 * A TB is a blob of binary code, created at runtime and called with an
	 * indirect function call. Since such function did not exist at compile time,
	 * the CFI runtime has no way to verify its signature and would fail.
	 * TCG is not considered a security-sensitive part of QEMU so this does not
	 * affect the impact of CFI in environment with high security requirements
	 */
	QEMU_DISABLE_CFI
	static inline tcg_target_ulong cpu_tb_exec(CPUState *cpu, TranslationBlock *itb)

NOTE: CFI needs to be disabled at the **caller** function, (i.e. a compatible
cfi function that calls a non-compatible one), since the check is performed
when the function call is performed.

CFI and fuzzing
---------------

There is generally no advantage of using CFI and fuzzing together, because
they target different environments (production for CFI, debug for fuzzing).

CFI could be used in conjunction with fuzzing to identify a broader set of
bugs that may not end immediately in a segmentation fault or triggering
an assertion. However, other sanitizers such as address and ub sanitizers
can identify such bugs in a more precise way than CFI.

There is, however, an interesting use case in using CFI in conjunction with
fuzzing, that is to make sure that CFI is not triggering any false positive
in remote-but-possible parts of the code.

CFI can be enabled with fuzzing, but with some caveats:
1. Fuzzing relies on the linker performing function wrapping at link-time.
The standard BFD linker does not support function wrapping when LTO is
also enabled. The workaround is to use LLVM's lld linker.
2. Fuzzing also relies on a custom linker script, which is only supported by
lld with version 11+.

In other words, to compile with fuzzing and CFI, clang 11+ is required, and
lld needs to be used as a linker::

 AR=llvm-ar-11 CC=clang-11 CXX=lang++-11 /path/to/configure --enable-cfi \
                           -enable-fuzzing --extra-ldflags="-fuse-ld=lld"

and then, compile the fuzzers as usual.
