.. _decodetree:

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

  field_def     := '%' identifier ( field )* ( !function=identifier )?
  field         := unnamed_field | named_field
  unnamed_field := number ':' ( 's' ) number
  named_field   := identifier ':' ( 's' ) number

For *unnamed_field*, the first number is the least-significant bit position
of the field and the second number is the length of the field.  If the 's' is
present, the field is considered signed.

A *named_field* refers to some other field in the instruction pattern
or format. Regardless of the length of the other field where it is
defined, it will be inserted into this field with the specified
signedness and bit width.

Field definitions that involve loops (i.e. where a field is defined
directly or indirectly in terms of itself) are errors.

A format can include fields that refer to named fields that are
defined in the instruction pattern(s) that use the format.
Conversely, an instruction pattern can include fields that refer to
named fields that are defined in the format it uses. However you
cannot currently do both at once (i.e. pattern P uses format F; F has
a field A that refers to a named field B that is defined in P, and P
has a field C that refers to a named field D that is defined in F).

If multiple ``fields`` are present, they are concatenated.
In this way one can define disjoint fields.

If ``!function`` is specified, the concatenated result is passed through the
named function, taking and returning an integral value.

One may use ``!function`` with zero ``fields``.  This case is called
a *parameter*, and the named function is only passed the ``DisasContext``
and returns an integral value extracted from there.

A field with no ``fields`` and no ``!function`` is in error.

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
| %sz_imm 10:2 sz:3         | expand_sz_imm(extract(i, 10, 2) << 3 |      |
|   !function=expand_sz_imm |               extract(a->sz, 0, 3))         |
+---------------------------+---------------------------------------------+

Argument Sets
=============

Syntax::

  args_def    := '&' identifier ( args_elt )+ ( !extern )?
  args_elt    := identifier (':' identifier)?

Each *args_elt* defines an argument within the argument set.
If the form of the *args_elt* contains a colon, the first
identifier is the argument name and the second identifier is
the argument type.  If the colon is missing, the argument
type will be ``int``.

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
  &longldst   reg base offset:int64_t


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

Pattern Groups
==============

Syntax::

  group            := overlap_group | no_overlap_group
  overlap_group    := '{' ( pat_def | group )+ '}'
  no_overlap_group := '[' ( pat_def | group )+ ']'

A *group* begins with a lone open-brace or open-bracket, with all
subsequent lines indented two spaces, and ending with a lone
close-brace or close-bracket.  Groups may be nested, increasing the
required indentation of the lines within the nested group to two
spaces per nesting level.

Patterns within overlap groups are allowed to overlap.  Conflicts are
resolved by selecting the patterns in order.  If all of the fixedbits
for a pattern match, its translate function will be called.  If the
translate function returns false, then subsequent patterns within the
group will be matched.

Patterns within no-overlap groups are not allowed to overlap, just
the same as ungrouped patterns.  Thus no-overlap groups are intended
to be nested inside overlap groups.

The following example from PA-RISC shows specialization of the *or*
instruction::

  {
    {
      nop   000010 ----- ----- 0000 001001 0 00000
      copy  000010 00000 r1:5  0000 001001 0 rt:5
    }
    or      000010 rt2:5 r1:5  cf:4 001001 0 rt:5
  }

When the *cf* field is zero, the instruction has no side effects,
and may be specialized.  When the *rt* field is zero, the output
is discarded and so the instruction has no effect.  When the *rt2*
field is zero, the operation is ``reg[r1] | 0`` and so encodes
the canonical register copy operation.

The output from the generator might look like::

  switch (insn & 0xfc000fe0) {
  case 0x08000240:
    /* 000010.. ........ ....0010 010..... */
    if ((insn & 0x0000f000) == 0x00000000) {
        /* 000010.. ........ 00000010 010..... */
        if ((insn & 0x0000001f) == 0x00000000) {
            /* 000010.. ........ 00000010 01000000 */
            extract_decode_Fmt_0(&u.f_decode0, insn);
            if (trans_nop(ctx, &u.f_decode0)) return true;
        }
        if ((insn & 0x03e00000) == 0x00000000) {
            /* 00001000 000..... 00000010 010..... */
            extract_decode_Fmt_1(&u.f_decode1, insn);
            if (trans_copy(ctx, &u.f_decode1)) return true;
        }
    }
    extract_decode_Fmt_2(&u.f_decode2, insn);
    if (trans_or(ctx, &u.f_decode2)) return true;
    return false;
  }
