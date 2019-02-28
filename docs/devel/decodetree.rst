========================
Decodetree Specification
========================

A *decodetree* is built from instruction *patterns*.  A pattern may
represent a single architectural instruction or a group of same, depending
on what is convenient for further processing.

Each pattern has both *fixedbits* and *fixedmask*, the combination of which
describes the condition under which the pattern is matched::

  (insn & fixedmask) == fixedbits

Each pattern may have *fields*, which are extracted from the insn and
passed along to the translator.  Examples of such are registers,
immediates, and sub-opcodes.

In support of patterns, one may declare *fields*, *argument sets*, and
*formats*, each of which may be re-used to simplify further definitions.

Fields
======

Syntax::

  field_def     := '%' identifier ( unnamed_field )+ ( !function=identifier )?
  unnamed_field := number ':' ( 's' ) number

For *unnamed_field*, the first number is the least-significant bit position
of the field and the second number is the length of the field.  If the 's' is
present, the field is considered signed.  If multiple ``unnamed_fields`` are
present, they are concatenated.  In this way one can define disjoint fields.

If ``!function`` is specified, the concatenated result is passed through the
named function, taking and returning an integral value.

FIXME: the fields of the structure into which this result will be stored
is restricted to ``int``.  Which means that we cannot expand 64-bit items.

Field examples:

+---------------------------+---------------------------------------------+
| Input                     | Generated code                              |
+===========================+=============================================+
| %disp   0:s16             | sextract(i, 0, 16)                          |
+---------------------------+---------------------------------------------+
| %imm9   16:6 10:3         | extract(i, 16, 6) << 3 | extract(i, 10, 3)  |
+---------------------------+---------------------------------------------+
| %disp12 0:s1 1:1 2:10     | sextract(i, 0, 1) << 11 |                   |
|                           |    extract(i, 1, 1) << 10 |                 |
|                           |    extract(i, 2, 10)                        |
+---------------------------+---------------------------------------------+
| %shimm8 5:s8 13:1         | expand_shimm8(sextract(i, 5, 8) << 1 |      |
|   !function=expand_shimm8 |               extract(i, 13, 1))            |
+---------------------------+---------------------------------------------+

Argument Sets
=============

Syntax::

  args_def    := '&' identifier ( args_elt )+ ( !extern )?
  args_elt    := identifier

Each *args_elt* defines an argument within the argument set.
Each argument set will be rendered as a C structure "arg_$name"
with each of the fields being one of the member arguments.

If ``!extern`` is specified, the backing structure is assumed
to have been already declared, typically via a second decoder.

Argument sets are useful when one wants to define helper functions
for the translator functions that can perform operations on a common
set of arguments.  This can ensure, for instance, that the ``AND``
pattern and the ``OR`` pattern put their operands into the same named
structure, so that a common ``gen_logic_insn`` may be able to handle
the operations common between the two.

Argument set examples::

  &reg3       ra rb rc
  &loadstore  reg base offset


Formats
=======

Syntax::

  fmt_def      := '@' identifier ( fmt_elt )+
  fmt_elt      := fixedbit_elt | field_elt | field_ref | args_ref
  fixedbit_elt := [01.-]+
  field_elt    := identifier ':' 's'? number
  field_ref    := '%' identifier | identifier '=' '%' identifier
  args_ref     := '&' identifier

Defining a format is a handy way to avoid replicating groups of fields
across many instruction patterns.

A *fixedbit_elt* describes a contiguous sequence of bits that must
be 1, 0, or don't care.  The difference between '.' and '-'
is that '.' means that the bit will be covered with a field or a
final 0 or 1 from the pattern, and '-' means that the bit is really
ignored by the cpu and will not be specified.

A *field_elt* describes a simple field only given a width; the position of
the field is implied by its position with respect to other *fixedbit_elt*
and *field_elt*.

If any *fixedbit_elt* or *field_elt* appear, then all bits must be defined.
Padding with a *fixedbit_elt* of all '.' is an easy way to accomplish that.

A *field_ref* incorporates a field by reference.  This is the only way to
add a complex field to a format.  A field may be renamed in the process
via assignment to another identifier.  This is intended to allow the
same argument set be used with disjoint named fields.

A single *args_ref* may specify an argument set to use for the format.
The set of fields in the format must be a subset of the arguments in
the argument set.  If an argument set is not specified, one will be
inferred from the set of fields.

It is recommended, but not required, that all *field_ref* and *args_ref*
appear at the end of the line, not interleaving with *fixedbit_elf* or
*field_elt*.

Format examples::

  @opr    ...... ra:5 rb:5 ... 0 ....... rc:5
  @opi    ...... ra:5 lit:8    1 ....... rc:5

Patterns
========

Syntax::

  pat_def      := identifier ( pat_elt )+
  pat_elt      := fixedbit_elt | field_elt | field_ref | args_ref | fmt_ref | const_elt
  fmt_ref      := '@' identifier
  const_elt    := identifier '=' number

The *fixedbit_elt* and *field_elt* specifiers are unchanged from formats.
A pattern that does not specify a named format will have one inferred
from a referenced argument set (if present) and the set of fields.

A *const_elt* allows a argument to be set to a constant value.  This may
come in handy when fields overlap between patterns and one has to
include the values in the *fixedbit_elt* instead.

The decoder will call a translator function for each pattern matched.

Pattern examples::

  addl_r   010000 ..... ..... .... 0000000 ..... @opr
  addl_i   010000 ..... ..... .... 0000000 ..... @opi

which will, in part, invoke::

  trans_addl_r(ctx, &arg_opr, insn)

and::

  trans_addl_i(ctx, &arg_opi, insn)
