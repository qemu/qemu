.. _tcg-ops-ref:

*******************************
TCG Intermediate Representation
*******************************

Introduction
============

TCG (Tiny Code Generator) began as a generic backend for a C compiler.
It was simplified to be used in QEMU.  It also has its roots in the
QOP code generator written by Paul Brook.

Definitions
===========

The TCG *target* is the architecture for which we generate the code.
It is of course not the same as the "target" of QEMU which is the
emulated architecture.  As TCG started as a generic C backend used
for cross compiling, the assumption was that TCG target might be
different from the host, although this is never the case for QEMU.

In this document, we use *guest* to specify what architecture we are
emulating; *target* always means the TCG target, the machine on which
we are running QEMU.

An operation with *undefined behavior* may result in a crash.

An operation with *unspecified behavior* shall not crash.  However,
the result may be one of several possibilities so may be considered
an *undefined result*.

Basic Blocks
============

A TCG *basic block* is a single entry, multiple exit region which
corresponds to a list of instructions terminated by a label, or
any branch instruction.

A TCG *extended basic block* is a single entry, multiple exit region
which corresponds to a list of instructions terminated by a label or
an unconditional branch.  Specifically, an extended basic block is
a sequence of basic blocks connected by the fall-through paths of
zero or more conditional branch instructions.

Operations
==========

TCG instructions or *ops* operate on TCG *variables*, both of which
are strongly typed.  Each instruction has a fixed number of output
variable operands, input variable operands and constant operands.
Vector instructions have a field specifying the element size within
the vector.  The notable exception is the call instruction which has
a variable number of outputs and inputs.

In the textual form, output operands usually come first, followed by
input operands, followed by constant operands. The output type is
included in the instruction name. Constants are prefixed with a '$'.

.. code-block:: none

   add_i32 t0, t1, t2    /* (t0 <- t1 + t2) */

Variables
=========

* ``TEMP_FIXED``

  There is one TCG *fixed global* variable, ``cpu_env``, which is
  live in all translation blocks, and holds a pointer to ``CPUArchState``.
  This variable is held in a host cpu register at all times in all
  translation blocks.

* ``TEMP_GLOBAL``

  A TCG *global* is a variable which is live in all translation blocks,
  and corresponds to memory location that is within ``CPUArchState``.
  These may be specified as an offset from ``cpu_env``, in which case
  they are called *direct globals*, or may be specified as an offset
  from a direct global, in which case they are called *indirect globals*.
  Even indirect globals should still reference memory within
  ``CPUArchState``.  All TCG globals are defined during
  ``TCGCPUOps.initialize``, before any translation blocks are generated.

* ``TEMP_CONST``

  A TCG *constant* is a variable which is live throughout the entire
  translation block, and contains a constant value.  These variables
  are allocated on demand during translation and are hashed so that
  there is exactly one variable holding a given value.

* ``TEMP_TB``

  A TCG *translation block temporary* is a variable which is live
  throughout the entire translation block, but dies on any exit.
  These temporaries are allocated explicitly during translation.

* ``TEMP_EBB``

  A TCG *extended basic block temporary* is a variable which is live
  throughout an extended basic block, but dies on any exit.
  These temporaries are allocated explicitly during translation.

Types
=====

* ``TCG_TYPE_I32``

  A 32-bit integer.

* ``TCG_TYPE_I64``

  A 64-bit integer.  For 32-bit hosts, such variables are split into a pair
  of variables with ``type=TCG_TYPE_I32`` and ``base_type=TCG_TYPE_I64``.
  The ``temp_subindex`` for each indicates where it falls within the
  host-endian representation.

* ``TCG_TYPE_PTR``

  An alias for ``TCG_TYPE_I32`` or ``TCG_TYPE_I64``, depending on the size
  of a pointer for the host.

* ``TCG_TYPE_REG``

  An alias for ``TCG_TYPE_I32`` or ``TCG_TYPE_I64``, depending on the size
  of the integer registers for the host.  This may be larger
  than ``TCG_TYPE_PTR`` depending on the host ABI.

* ``TCG_TYPE_I128``

  A 128-bit integer.  For all hosts, such variables are split into a number
  of variables with ``type=TCG_TYPE_REG`` and ``base_type=TCG_TYPE_I128``.
  The ``temp_subindex`` for each indicates where it falls within the
  host-endian representation.

* ``TCG_TYPE_V64``

  A 64-bit vector.  This type is valid only if the TCG target
  sets ``TCG_TARGET_HAS_v64``.

* ``TCG_TYPE_V128``

  A 128-bit vector.  This type is valid only if the TCG target
  sets ``TCG_TARGET_HAS_v128``.

* ``TCG_TYPE_V256``

  A 256-bit vector.  This type is valid only if the TCG target
  sets ``TCG_TARGET_HAS_v256``.

Helpers
=======

Helpers are registered in a guest-specific ``helper.h``,
which is processed to generate ``tcg_gen_helper_*`` functions.
With these functions it is possible to call a function taking
i32, i64, i128 or pointer types.

By default, before calling a helper, all globals are stored at their
canonical location.  By default, the helper is allowed to modify the
CPU state (including the state represented by tcg globals)
or may raise an exception.  This default can be overridden using the
following function modifiers:

* ``TCG_CALL_NO_WRITE_GLOBALS``

  The helper does not modify any globals, but may read them.
  Globals will be saved to their canonical location before calling helpers,
  but need not be reloaded afterwards.

* ``TCG_CALL_NO_READ_GLOBALS``

  The helper does not read globals, either directly or via an exception.
  They will not be saved to their canonical locations before calling
  the helper.  This implies ``TCG_CALL_NO_WRITE_GLOBALS``.

* ``TCG_CALL_NO_SIDE_EFFECTS``

  The call to the helper function may be removed if the return value is
  not used.  This means that it may not modify any CPU state nor may it
  raise an exception.

Code Optimizations
==================

When generating instructions, you can count on at least the following
optimizations:

- Single instructions are simplified, e.g.

  .. code-block:: none

     and_i32 t0, t0, $0xffffffff

  is suppressed.

- A liveness analysis is done at the basic block level. The
  information is used to suppress moves from a dead variable to
  another one. It is also used to remove instructions which compute
  dead results. The later is especially useful for condition code
  optimization in QEMU.

  In the following example:

  .. code-block:: none

     add_i32 t0, t1, t2
     add_i32 t0, t0, $1
     mov_i32 t0, $1

  only the last instruction is kept.


Instruction Reference
=====================

Function call
-------------

.. list-table::

   * - call *<ret>* *<params>* ptr

     - |  call function 'ptr' (pointer type)
       |
       |  *<ret>* optional 32 bit or 64 bit return value
       |  *<params>* optional 32 bit or 64 bit parameters

Jumps/Labels
------------

.. list-table::

   * - set_label $label

     - | Define label 'label' at the current program point.

   * - br $label

     - | Jump to label.

   * - brcond_i32/i64 *t0*, *t1*, *cond*, *label*

     - | Conditional jump if *t0* *cond* *t1* is true. *cond* can be:
       |
       |   ``TCG_COND_EQ``
       |   ``TCG_COND_NE``
       |   ``TCG_COND_LT /* signed */``
       |   ``TCG_COND_GE /* signed */``
       |   ``TCG_COND_LE /* signed */``
       |   ``TCG_COND_GT /* signed */``
       |   ``TCG_COND_LTU /* unsigned */``
       |   ``TCG_COND_GEU /* unsigned */``
       |   ``TCG_COND_LEU /* unsigned */``
       |   ``TCG_COND_GTU /* unsigned */``

Arithmetic
----------

.. list-table::

   * - add_i32/i64 *t0*, *t1*, *t2*

     - | *t0* = *t1* + *t2*

   * - sub_i32/i64 *t0*, *t1*, *t2*

     - | *t0* = *t1* - *t2*

   * - neg_i32/i64 *t0*, *t1*

     - | *t0* = -*t1* (two's complement)

   * - mul_i32/i64 *t0*, *t1*, *t2*

     - | *t0* = *t1* * *t2*

   * - div_i32/i64 *t0*, *t1*, *t2*

     - | *t0* = *t1* / *t2* (signed)
       | Undefined behavior if division by zero or overflow.

   * - divu_i32/i64 *t0*, *t1*, *t2*

     - | *t0* = *t1* / *t2* (unsigned)
       | Undefined behavior if division by zero.

   * - rem_i32/i64 *t0*, *t1*, *t2*

     - | *t0* = *t1* % *t2* (signed)
       | Undefined behavior if division by zero or overflow.

   * - remu_i32/i64 *t0*, *t1*, *t2*

     - | *t0* = *t1* % *t2* (unsigned)
       | Undefined behavior if division by zero.


Logical
-------

.. list-table::

   * - and_i32/i64 *t0*, *t1*, *t2*

     - | *t0* = *t1* & *t2*

   * - or_i32/i64 *t0*, *t1*, *t2*

     - | *t0* = *t1* | *t2*

   * - xor_i32/i64 *t0*, *t1*, *t2*

     - | *t0* = *t1* ^ *t2*

   * - not_i32/i64 *t0*, *t1*

     - | *t0* = ~\ *t1*

   * - andc_i32/i64 *t0*, *t1*, *t2*

     - | *t0* = *t1* & ~\ *t2*

   * - eqv_i32/i64 *t0*, *t1*, *t2*

     - | *t0* = ~(*t1* ^ *t2*), or equivalently, *t0* = *t1* ^ ~\ *t2*

   * - nand_i32/i64 *t0*, *t1*, *t2*

     - | *t0* = ~(*t1* & *t2*)

   * - nor_i32/i64 *t0*, *t1*, *t2*

     - | *t0* = ~(*t1* | *t2*)

   * - orc_i32/i64 *t0*, *t1*, *t2*

     - | *t0* = *t1* | ~\ *t2*

   * - clz_i32/i64 *t0*, *t1*, *t2*

     - | *t0* = *t1* ? clz(*t1*) : *t2*

   * - ctz_i32/i64 *t0*, *t1*, *t2*

     - | *t0* = *t1* ? ctz(*t1*) : *t2*

   * - ctpop_i32/i64 *t0*, *t1*

     - | *t0* = number of bits set in *t1*
       |
       | With *ctpop* short for "count population", matching
       | the function name used in ``include/qemu/host-utils.h``.


Shifts/Rotates
--------------

.. list-table::

   * - shl_i32/i64 *t0*, *t1*, *t2*

     - | *t0* = *t1* << *t2*
       | Unspecified behavior if *t2* < 0 or *t2* >= 32 (resp 64)

   * - shr_i32/i64 *t0*, *t1*, *t2*

     - | *t0* = *t1* >> *t2* (unsigned)
       | Unspecified behavior if *t2* < 0 or *t2* >= 32 (resp 64)

   * - sar_i32/i64 *t0*, *t1*, *t2*

     - | *t0* = *t1* >> *t2* (signed)
       | Unspecified behavior if *t2* < 0 or *t2* >= 32 (resp 64)

   * - rotl_i32/i64 *t0*, *t1*, *t2*

     - | Rotation of *t2* bits to the left
       | Unspecified behavior if *t2* < 0 or *t2* >= 32 (resp 64)

   * - rotr_i32/i64 *t0*, *t1*, *t2*

     - | Rotation of *t2* bits to the right.
       | Unspecified behavior if *t2* < 0 or *t2* >= 32 (resp 64)


Misc
----

.. list-table::

   * - mov_i32/i64 *t0*, *t1*

     - | *t0* = *t1*
       | Move *t1* to *t0* (both operands must have the same type).

   * - ext8s_i32/i64 *t0*, *t1*

       ext8u_i32/i64 *t0*, *t1*

       ext16s_i32/i64 *t0*, *t1*

       ext16u_i32/i64 *t0*, *t1*

       ext32s_i64 *t0*, *t1*

       ext32u_i64 *t0*, *t1*

     - | 8, 16 or 32 bit sign/zero extension (both operands must have the same type)

   * - bswap16_i32/i64 *t0*, *t1*, *flags*

     - | 16 bit byte swap on the low bits of a 32/64 bit input.
       |
       | If *flags* & ``TCG_BSWAP_IZ``, then *t1* is known to be zero-extended from bit 15.
       | If *flags* & ``TCG_BSWAP_OZ``, then *t0* will be zero-extended from bit 15.
       | If *flags* & ``TCG_BSWAP_OS``, then *t0* will be sign-extended from bit 15.
       |
       | If neither ``TCG_BSWAP_OZ`` nor ``TCG_BSWAP_OS`` are set, then the bits of *t0* above bit 15 may contain any value.

   * - bswap32_i64 *t0*, *t1*, *flags*

     - | 32 bit byte swap on a 64-bit value.  The flags are the same as for bswap16,
         except they apply from bit 31 instead of bit 15.

   * - bswap32_i32 *t0*, *t1*, *flags*

       bswap64_i64 *t0*, *t1*, *flags*

     - | 32/64 bit byte swap. The flags are ignored, but still present
         for consistency with the other bswap opcodes.

   * - discard_i32/i64 *t0*

     - | Indicate that the value of *t0* won't be used later. It is useful to
         force dead code elimination.

   * - deposit_i32/i64 *dest*, *t1*, *t2*, *pos*, *len*

     - | Deposit *t2* as a bitfield into *t1*, placing the result in *dest*.
       |
       | The bitfield is described by *pos*/*len*, which are immediate values:
       |
       |     *len* - the length of the bitfield
       |     *pos* - the position of the first bit, counting from the LSB
       |
       | For example, "deposit_i32 dest, t1, t2, 8, 4" indicates a 4-bit field
         at bit 8. This operation would be equivalent to
       |
       |     *dest* = (*t1* & ~0x0f00) | ((*t2* << 8) & 0x0f00)

   * - extract_i32/i64 *dest*, *t1*, *pos*, *len*

       sextract_i32/i64 *dest*, *t1*, *pos*, *len*

     - | Extract a bitfield from *t1*, placing the result in *dest*.
       |
       | The bitfield is described by *pos*/*len*, which are immediate values,
         as above for deposit.  For extract_*, the result will be extended
         to the left with zeros; for sextract_*, the result will be extended
         to the left with copies of the bitfield sign bit at *pos* + *len* - 1.
       |
       | For example, "sextract_i32 dest, t1, 8, 4" indicates a 4-bit field
         at bit 8. This operation would be equivalent to
       |
       |    *dest* = (*t1* << 20) >> 28
       |
       | (using an arithmetic right shift).

   * - extract2_i32/i64 *dest*, *t1*, *t2*, *pos*

     - | For N = {32,64}, extract an N-bit quantity from the concatenation
         of *t2*:*t1*, beginning at *pos*. The tcg_gen_extract2_{i32,i64} expander
         accepts 0 <= *pos* <= N as inputs. The backend code generator will
         not see either 0 or N as inputs for these opcodes.

   * - extrl_i64_i32 *t0*, *t1*

     - | For 64-bit hosts only, extract the low 32-bits of input *t1* and place it
         into 32-bit output *t0*.  Depending on the host, this may be a simple move,
         or may require additional canonicalization.

   * - extrh_i64_i32 *t0*, *t1*

     - | For 64-bit hosts only, extract the high 32-bits of input *t1* and place it
         into 32-bit output *t0*.  Depending on the host, this may be a simple shift,
         or may require additional canonicalization.


Conditional moves
-----------------

.. list-table::

   * - setcond_i32/i64 *dest*, *t1*, *t2*, *cond*

     - | *dest* = (*t1* *cond* *t2*)
       |
       | Set *dest* to 1 if (*t1* *cond* *t2*) is true, otherwise set to 0.

   * - movcond_i32/i64 *dest*, *c1*, *c2*, *v1*, *v2*, *cond*

     - | *dest* = (*c1* *cond* *c2* ? *v1* : *v2*)
       |
       | Set *dest* to *v1* if (*c1* *cond* *c2*) is true, otherwise set to *v2*.


Type conversions
----------------

.. list-table::

   * - ext_i32_i64 *t0*, *t1*

     - | Convert *t1* (32 bit) to *t0* (64 bit) and does sign extension

   * - extu_i32_i64 *t0*, *t1*

     - | Convert *t1* (32 bit) to *t0* (64 bit) and does zero extension

   * - trunc_i64_i32 *t0*, *t1*

     - | Truncate *t1* (64 bit) to *t0* (32 bit)

   * - concat_i32_i64 *t0*, *t1*, *t2*

     - | Construct *t0* (64-bit) taking the low half from *t1* (32 bit) and the high half
         from *t2* (32 bit).

   * - concat32_i64 *t0*, *t1*, *t2*

     - | Construct *t0* (64-bit) taking the low half from *t1* (64 bit) and the high half
         from *t2* (64 bit).


Load/Store
----------

.. list-table::

   * - ld_i32/i64 *t0*, *t1*, *offset*

       ld8s_i32/i64 *t0*, *t1*, *offset*

       ld8u_i32/i64 *t0*, *t1*, *offset*

       ld16s_i32/i64 *t0*, *t1*, *offset*

       ld16u_i32/i64 *t0*, *t1*, *offset*

       ld32s_i64 t0, *t1*, *offset*

       ld32u_i64 t0, *t1*, *offset*

     - | *t0* = read(*t1* + *offset*)
       |
       | Load 8, 16, 32 or 64 bits with or without sign extension from host memory.
         *offset* must be a constant.

   * - st_i32/i64 *t0*, *t1*, *offset*

       st8_i32/i64 *t0*, *t1*, *offset*

       st16_i32/i64 *t0*, *t1*, *offset*

       st32_i64 *t0*, *t1*, *offset*

     - | write(*t0*, *t1* + *offset*)
       |
       | Write 8, 16, 32 or 64 bits to host memory.

All this opcodes assume that the pointed host memory doesn't correspond
to a global. In the latter case the behaviour is unpredictable.


Multiword arithmetic support
----------------------------

.. list-table::

   * - add2_i32/i64 *t0_low*, *t0_high*, *t1_low*, *t1_high*, *t2_low*, *t2_high*

       sub2_i32/i64 *t0_low*, *t0_high*, *t1_low*, *t1_high*, *t2_low*, *t2_high*

     - | Similar to add/sub, except that the double-word inputs *t1* and *t2* are
         formed from two single-word arguments, and the double-word output *t0*
         is returned in two single-word outputs.

   * - mulu2_i32/i64 *t0_low*, *t0_high*, *t1*, *t2*

     - | Similar to mul, except two unsigned inputs *t1* and *t2* yielding the full
         double-word product *t0*. The latter is returned in two single-word outputs.

   * - muls2_i32/i64 *t0_low*, *t0_high*, *t1*, *t2*

     - | Similar to mulu2, except the two inputs *t1* and *t2* are signed.

   * - mulsh_i32/i64 *t0*, *t1*, *t2*

       muluh_i32/i64 *t0*, *t1*, *t2*

     - | Provide the high part of a signed or unsigned multiply, respectively.
       |
       | If mulu2/muls2 are not provided by the backend, the tcg-op generator
         can obtain the same results by emitting a pair of opcodes, mul + muluh/mulsh.


Memory Barrier support
----------------------

.. list-table::

   * - mb *<$arg>*

     - | Generate a target memory barrier instruction to ensure memory ordering
         as being  enforced by a corresponding guest memory barrier instruction.
       |
       | The ordering enforced by the backend may be stricter than the ordering
         required by the guest. It cannot be weaker. This opcode takes a constant
         argument which is required to generate the appropriate barrier
         instruction. The backend should take care to emit the target barrier
         instruction only when necessary i.e., for SMP guests and when MTTCG is
         enabled.
       |
       | The guest translators should generate this opcode for all guest instructions
         which have ordering side effects.
       |
       | Please see :ref:`atomics-ref` for more information on memory barriers.


64-bit guest on 32-bit host support
-----------------------------------

The following opcodes are internal to TCG.  Thus they are to be implemented by
32-bit host code generators, but are not to be emitted by guest translators.
They are emitted as needed by inline functions within ``tcg-op.h``.

.. list-table::

   * - brcond2_i32 *t0_low*, *t0_high*, *t1_low*, *t1_high*, *cond*, *label*

     - | Similar to brcond, except that the 64-bit values *t0* and *t1*
         are formed from two 32-bit arguments.

   * - setcond2_i32 *dest*, *t1_low*, *t1_high*, *t2_low*, *t2_high*, *cond*

     - | Similar to setcond, except that the 64-bit values *t1* and *t2* are
         formed from two 32-bit arguments. The result is a 32-bit value.


QEMU specific operations
------------------------

.. list-table::

   * - exit_tb *t0*

     - | Exit the current TB and return the value *t0* (word type).

   * - goto_tb *index*

     - | Exit the current TB and jump to the TB index *index* (constant) if the
         current TB was linked to this TB. Otherwise execute the next
         instructions. Only indices 0 and 1 are valid and tcg_gen_goto_tb may be issued
         at most once with each slot index per TB.

   * - lookup_and_goto_ptr *tb_addr*

     - | Look up a TB address *tb_addr* and jump to it if valid. If not valid,
         jump to the TCG epilogue to go back to the exec loop.
       |
       | This operation is optional. If the TCG backend does not implement the
         goto_ptr opcode, emitting this op is equivalent to emitting exit_tb(0).

   * - qemu_ld_i32/i64 *t0*, *t1*, *flags*, *memidx*

       qemu_st_i32/i64 *t0*, *t1*, *flags*, *memidx*

       qemu_st8_i32 *t0*, *t1*, *flags*, *memidx*

     - | Load data at the guest address *t1* into *t0*, or store data in *t0* at guest
         address *t1*.  The _i32/_i64 size applies to the size of the input/output
         register *t0* only.  The address *t1* is always sized according to the guest,
         and the width of the memory operation is controlled by *flags*.
       |
       | Both *t0* and *t1* may be split into little-endian ordered pairs of registers
         if dealing with 64-bit quantities on a 32-bit host.
       |
       | The *memidx* selects the qemu tlb index to use (e.g. user or kernel access).
         The flags are the MemOp bits, selecting the sign, width, and endianness
         of the memory access.
       |
       | For a 32-bit host, qemu_ld/st_i64 is guaranteed to only be used with a
         64-bit memory access specified in *flags*.
       |
       | For i386, qemu_st8_i32 is exactly like qemu_st_i32, except the size of
         the memory operation is known to be 8-bit.  This allows the backend to
         provide a different set of register constraints.


Host vector operations
----------------------

All of the vector ops have two parameters, ``TCGOP_VECL`` & ``TCGOP_VECE``.
The former specifies the length of the vector in log2 64-bit units; the
latter specifies the length of the element (if applicable) in log2 8-bit units.
E.g. VECL = 1 -> 64 << 1 -> v128, and VECE = 2 -> 1 << 2 -> i32.

.. list-table::

   * - mov_vec *v0*, *v1*
       ld_vec *v0*, *t1*
       st_vec *v0*, *t1*

     - | Move, load and store.

   * - dup_vec *v0*, *r1*

     - | Duplicate the low N bits of *r1* into VECL/VECE copies across *v0*.

   * - dupi_vec *v0*, *c*

     - | Similarly, for a constant.
       | Smaller values will be replicated to host register size by the expanders.

   * - dup2_vec *v0*, *r1*, *r2*

     - | Duplicate *r2*:*r1* into VECL/64 copies across *v0*. This opcode is
         only present for 32-bit hosts.

   * - add_vec *v0*, *v1*, *v2*

     - | *v0* = *v1* + *v2*, in elements across the vector.

   * - sub_vec *v0*, *v1*, *v2*

     - | Similarly, *v0* = *v1* - *v2*.

   * - mul_vec *v0*, *v1*, *v2*

     - | Similarly, *v0* = *v1* * *v2*.

   * - neg_vec *v0*, *v1*

     - | Similarly, *v0* = -*v1*.

   * - abs_vec *v0*, *v1*

     - | Similarly, *v0* = *v1* < 0 ? -*v1* : *v1*, in elements across the vector.

   * - smin_vec *v0*, *v1*, *v2*

       umin_vec *v0*, *v1*, *v2*

     - | Similarly, *v0* = MIN(*v1*, *v2*), for signed and unsigned element types.

   * - smax_vec *v0*, *v1*, *v2*

       umax_vec *v0*, *v1*, *v2*

     - | Similarly, *v0* = MAX(*v1*, *v2*), for signed and unsigned element types.

   * - ssadd_vec *v0*, *v1*, *v2*

       sssub_vec *v0*, *v1*, *v2*

       usadd_vec *v0*, *v1*, *v2*

       ussub_vec *v0*, *v1*, *v2*

     - | Signed and unsigned saturating addition and subtraction.
       |
       | If the true result is not representable within the element type, the
         element is set to the minimum or maximum value for the type.

   * - and_vec *v0*, *v1*, *v2*

       or_vec *v0*, *v1*, *v2*

       xor_vec *v0*, *v1*, *v2*

       andc_vec *v0*, *v1*, *v2*

       orc_vec *v0*, *v1*, *v2*

       not_vec *v0*, *v1*

     - | Similarly, logical operations with and without complement.
       |
       | Note that VECE is unused.

   * - shli_vec *v0*, *v1*, *i2*

       shls_vec *v0*, *v1*, *s2*

     - | Shift all elements from v1 by a scalar *i2*/*s2*. I.e.

       .. code-block:: c

          for (i = 0; i < VECL/VECE; ++i) {
              v0[i] = v1[i] << s2;
          }

   * - shri_vec *v0*, *v1*, *i2*

       sari_vec *v0*, *v1*, *i2*

       rotli_vec *v0*, *v1*, *i2*

       shrs_vec *v0*, *v1*, *s2*

       sars_vec *v0*, *v1*, *s2*

     - | Similarly for logical and arithmetic right shift, and left rotate.

   * - shlv_vec *v0*, *v1*, *v2*

     - | Shift elements from *v1* by elements from *v2*. I.e.

       .. code-block:: c

          for (i = 0; i < VECL/VECE; ++i) {
              v0[i] = v1[i] << v2[i];
          }

   * - shrv_vec *v0*, *v1*, *v2*

       sarv_vec *v0*, *v1*, *v2*

       rotlv_vec *v0*, *v1*, *v2*

       rotrv_vec *v0*, *v1*, *v2*

     - | Similarly for logical and arithmetic right shift, and rotates.

   * - cmp_vec *v0*, *v1*, *v2*, *cond*

     - | Compare vectors by element, storing -1 for true and 0 for false.

   * - bitsel_vec *v0*, *v1*, *v2*, *v3*

     - | Bitwise select, *v0* = (*v2* & *v1*) | (*v3* & ~\ *v1*), across the entire vector.

   * - cmpsel_vec *v0*, *c1*, *c2*, *v3*, *v4*, *cond*

     - | Select elements based on comparison results:

       .. code-block:: c

          for (i = 0; i < n; ++i) {
              v0[i] = (c1[i] cond c2[i]) ? v3[i] : v4[i].
          }

**Note 1**: Some shortcuts are defined when the last operand is known to be
a constant (e.g. addi for add, movi for mov).

**Note 2**: When using TCG, the opcodes must never be generated directly
as some of them may not be available as "real" opcodes. Always use the
function tcg_gen_xxx(args).


Backend
=======

``tcg-target.h`` contains the target specific definitions. ``tcg-target.c.inc``
contains the target specific code; it is #included by ``tcg/tcg.c``, rather
than being a standalone C file.

Assumptions
-----------

The target word size (``TCG_TARGET_REG_BITS``) is expected to be 32 bit or
64 bit. It is expected that the pointer has the same size as the word.

On a 32 bit target, all 64 bit operations are converted to 32 bits. A
few specific operations must be implemented to allow it (see add2_i32,
sub2_i32, brcond2_i32).

On a 64 bit target, the values are transferred between 32 and 64-bit
registers using the following ops:

- trunc_shr_i64_i32
- ext_i32_i64
- extu_i32_i64

They ensure that the values are correctly truncated or extended when
moved from a 32-bit to a 64-bit register or vice-versa. Note that the
trunc_shr_i64_i32 is an optional op. It is not necessary to implement
it if all the following conditions are met:

- 64-bit registers can hold 32-bit values
- 32-bit values in a 64-bit register do not need to stay zero or
  sign extended
- all 32-bit TCG ops ignore the high part of 64-bit registers

Floating point operations are not supported in this version. A
previous incarnation of the code generator had full support of them,
but it is better to concentrate on integer operations first.

Constraints
----------------

GCC like constraints are used to define the constraints of every
instruction. Memory constraints are not supported in this
version. Aliases are specified in the input operands as for GCC.

The same register may be used for both an input and an output, even when
they are not explicitly aliased.  If an op expands to multiple target
instructions then care must be taken to avoid clobbering input values.
GCC style "early clobber" outputs are supported, with '``&``'.

A target can define specific register or constant constraints. If an
operation uses a constant input constraint which does not allow all
constants, it must also accept registers in order to have a fallback.
The constraint '``i``' is defined generically to accept any constant.
The constraint '``r``' is not defined generically, but is consistently
used by each backend to indicate all registers.

The movi_i32 and movi_i64 operations must accept any constants.

The mov_i32 and mov_i64 operations must accept any registers of the
same type.

The ld/st/sti instructions must accept signed 32 bit constant offsets.
This can be implemented by reserving a specific register in which to
compute the address if the offset is too big.

The ld/st instructions must accept any destination (ld) or source (st)
register.

The sti instruction may fail if it cannot store the given constant.

Function call assumptions
-------------------------

- The only supported types for parameters and return value are: 32 and
  64 bit integers and pointer.
- The stack grows downwards.
- The first N parameters are passed in registers.
- The next parameters are passed on the stack by storing them as words.
- Some registers are clobbered during the call.
- The function can return 0 or 1 value in registers. On a 32 bit
  target, functions must be able to return 2 values in registers for
  64 bit return type.


Recommended coding rules for best performance
=============================================

- Use globals to represent the parts of the QEMU CPU state which are
  often modified, e.g. the integer registers and the condition
  codes. TCG will be able to use host registers to store them.

- Don't hesitate to use helpers for complicated or seldom used guest
  instructions. There is little performance advantage in using TCG to
  implement guest instructions taking more than about twenty TCG
  instructions. Note that this rule of thumb is more applicable to
  helpers doing complex logic or arithmetic, where the C compiler has
  scope to do a good job of optimisation; it is less relevant where
  the instruction is mostly doing loads and stores, and in those cases
  inline TCG may still be faster for longer sequences.

- Use the 'discard' instruction if you know that TCG won't be able to
  prove that a given global is "dead" at a given program point. The
  x86 guest uses it to improve the condition codes optimisation.
