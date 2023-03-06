Hexagon ISA instruction definitions to tinycode generator compiler
------------------------------------------------------------------

idef-parser is a small compiler able to translate the Hexagon ISA description
language into tinycode generator code, that can be easily integrated into QEMU.

Compilation Example
-------------------

To better understand the scope of the idef-parser, we'll explore an applicative
example. Let's start by one of the simplest Hexagon instruction: the ``add``.

The ISA description language represents the ``add`` instruction as
follows:

.. code:: c

   A2_add(RdV, in RsV, in RtV) {
       { RdV=RsV+RtV;}
   }

idef-parser will compile the above code into the following code:

.. code:: c

   /* A2_add */
   void emit_A2_add(DisasContext *ctx, Insn *insn, Packet *pkt, TCGv_i32 RdV,
                    TCGv_i32 RsV, TCGv_i32 RtV)
   /*  { RdV=RsV+RtV;} */
   {
       TCGv_i32 tmp_0 = tcg_temp_new_i32();
       tcg_gen_add_i32(tmp_0, RsV, RtV);
       tcg_gen_mov_i32(RdV, tmp_0);
   }

The output of the compilation process will be a function, containing the
tinycode generator code, implementing the correct semantics. That function will
not access any global variable, because all the accessed data structures will be
passed explicitly as function parameters. Among the passed parameters we will
have TCGv (tinycode variables) representing the input and output registers of
the architecture, integers representing the immediates that come from the code,
and other data structures which hold information about the disassemblation
context (``DisasContext`` struct).

Let's begin by describing the input code. The ``add`` instruction is associated
with a unique identifier, in this case ``A2_add``, which allows to distinguish
variants of the same instruction, and expresses the class to which the
instruction belongs, in this case ``A2`` corresponds to the Hexagon
``ALU32/ALU`` instruction subclass.

After the instruction identifier, we have a series of parameters that represents
TCG variables that will be passed to the generated function. Parameters marked
with ``in`` are already initialized, while the others are output parameters.

We will leverage this information to infer several information:

-  Fill in the output function signature with the correct TCGv registers
-  Fill in the output function signature with the immediate integers
-  Keep track of which registers, among the declared one, have been
   initialized

Let's now observe the actual instruction description code, in this case:

.. code:: c

   { RdV=RsV+RtV;}

This code is composed by a subset of the C syntax, and is the result of the
application of some macro definitions contained in the ``macros.h`` file.

This file is used to reduce the complexity of the input language where complex
variants of similar constructs can be mapped to a unique primitive, so that the
idef-parser has to handle a lower number of computation primitives.

As you may notice, the description code modifies the registers which have been
declared by the declaration statements. In this case all the three registers
will be declared, ``RsV`` and ``RtV`` will also be read and ``RdV`` will be
written.

Now let's have a quick look at the generated code, line by line.

::

   TCGv_i32 tmp_0 = tcg_temp_new_i32();

This code starts by declaring a temporary TCGv to hold the result from the sum
operation.

::

   tcg_gen_add_i32(tmp_0, RsV, RtV);

Then, we are generating the sum tinycode operator between the selected
registers, storing the result in the just declared temporary.

::

   tcg_gen_mov_i32(RdV, tmp_0);

The result of the addition is now stored in the temporary, we move it into the
correct destination register. This code may seem inefficient, but QEMU will
perform some optimizations on the tinycode, reducing the unnecessary copy.

Parser Input
------------

Before moving on to the structure of idef-parser itself, let us spend some words
on its' input. There are two preprocessing steps applied to the generated
instruction semantics in ``semantics_generated.pyinc`` that we need to consider.
Firstly,

::

    gen_idef_parser_funcs.py

which takes instruction semantics in ``semantics_generated.pyinc`` to C-like
pseudo code, output into ``idef_parser_input.h.inc``. For instance, the
``J2_jumpr`` instruction which jumps to an address stored in a register
argument. This is instruction is defined as

::

    SEMANTICS( \
        "J2_jumpr", \
        "jumpr Rs32", \
        """{fJUMPR(RsN,RsV,COF_TYPE_JUMPR);}""" \
    )

in ``semantics_generated.pyinc``. Running ``gen_idef_parser_funcs.py``
we obtain the pseudo code

::

    J2_jumpr(in RsV) {
        {fJUMPR(RsN,RsV,COF_TYPE_JUMPR);}
    }

with macros such as ``fJUMPR`` intact.

The second step is to expand macros into a form suitable for our parser.
These macros are defined in ``idef-parser/macros.inc`` and the step is
carried out by the ``prepare`` script which runs the C preprocessor on
``idef_parser_input.h.inc`` to produce
``idef_parser_input.preprocessed.h.inc``.

To finish the above example, after preprocessing ``J2_jumpr`` we obtain

::

    J2_jumpr(in RsV) {
        {(PC = RsV);}
    }

where ``fJUMPR(RsN,RsV,COF_TYPE_JUMPR);`` was expanded to ``(PC = RsV)``,
signifying a write to the Program Counter ``PC``.  Note, that ``PC`` in
this expression is not a variable in the strict C sense since it is not
declared anywhere, but rather a symbol which is easy to match in
idef-parser later on.

Parser Structure
----------------

The idef-parser is built using the ``flex`` and ``bison``.

``flex`` is used to split the input string into tokens, each described using a
regular expression. The token description is contained in the
``idef-parser.lex`` source file. The flex-generated scanner takes care also to
extract from the input text other meaningful information, e.g., the numerical
value in case of an immediate constant, and decorates the token with the
extracted information.

``bison`` is used to generate the actual parser, starting from the parsing
description contained in the ``idef-parser.y`` file. The generated parser
executes the ``main`` function at the end of the ``idef-parser.y`` file, which
opens input and output files, creates the parsing context, and eventually calls
the ``yyparse()`` function, which starts the execution of the LALR(1) parser
(see `Wikipedia <https://en.wikipedia.org/wiki/LALR_parser>`__ for more
information about LALR parsing techniques). The LALR(1) parser, whenever it has
to shift a token, calls the ``yylex()`` function, which is defined by the
flex-generated code, and reads the input file returning the next scanned token.

The tokens are mapped on the source language grammar, defined in the
``idef-parser.y`` file to build a unique syntactic tree, according to the
specified operator precedences and associativity rules.

The grammar describes the whole file which contains the Hexagon instruction
descriptions, therefore it starts from the ``input`` nonterminal, which is a
list of instructions, each instruction is represented by the following grammar
rule, representing the structure of the input file shown above:

::

   instruction : INAME arguments code
               | error

   arguments : '(' ')'
             | '(' argument_list ')';

   argument_list : argument_decl ',' argument_list
                 | argument_decl

   argument_decl : REG
                 | PRED
                 | IN REG
                 | IN PRED
                 | IMM
                 | var
                 ;

   code        : '{' statements '}'

   statements  : statements statement
               | statement

   statement   : control_statement
               | var_decl ';'
               | rvalue ';'
               | code_block
               | ';'

   code_block  : '{' statements '}'
               | '{' '}'

With this initial portion of the grammar we are defining the instruction, its'
arguments, and its' statements. Each argument is defined by the
``argument_decl`` rule, and can be either

::

    Description                  Example
    ----------------------------------------
    output register              RsV
    output predicate register    P0
    input register               in RsV
    input predicate register     in P0
    immediate value              1234
    local variable               EA

Note, the only local variable allowed to be used as an argument is the effective
address ``EA``. Similarly, each statement can be a ``control_statement``, a
variable declaration such as ``int a;``, a code block, which is just a
bracket-enclosed list of statements, a ``';'``, which is a ``nop`` instruction,
and an ``rvalue ';'``.

Expressions
~~~~~~~~~~~

Allowed in the input code are C language expressions with a few exceptions
to simplify parsing. For instance, variable names such as ``RdV``, ``RssV``,
``PdV``, ``CsV``, and other idiomatic register names from Hexagon, are
reserved specifically for register arguments. These arguments then map to
``TCGv_i32`` or ``TCGv_i64`` depending on the register size. Similarly, ``UiV``,
``riV``, etc. refer to immediate arguments and will map to C integers.

Also, as mentioned earlier, the names ``PC``, ``SP``, ``FP``, etc. are used to
refer to Hexagon registers such as the program counter, stack pointer, and frame
pointer seen here. Writes to these registers then correspond to assignments
``PC = ...``, and reads correspond to uses of the variable ``PC``.

Moreover, another example of one such exception is the selective expansion of
macros present in ``macros.h``. As an example, consider the ``fABS`` macro which
in plain C is defined as

::

    #define fABS(A) (((A) < 0) ? (-(A)) : (A))

and returns the absolute value of the argument ``A``. This macro is not included
in ``idef-parser/macros.inc`` and as such is not expanded and kept as a "call"
``fABS(...)``. Reason being, that ``fABS`` is easier to match and map to
``tcg_gen_abs_<width>``, compared to the full ternary expression above. Loads of
macros in ``macros.h`` are kept unexpanded to aid in parsing, as seen in the
example above, for more information see ``idef-parser/idef-parser.lex``.

Finally, in mapping these input expressions to tinycode generators, idef-parser
tries to perform as much as possible in plain C. Such as, performing binary
operations in C instead of tinycode generators, thus effectively constant
folding the expression.

Variables and Variable Declarations
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Similarly to C, variables in the input code must be explicitly declared, such as
``int var1;`` which declares an uninitialized variable ``var1``. Initialization
``int var2 = 0;`` is also allowed and behaves as expected. In tinycode
generators the previous declarations are mapped to

::

    int var1;           ->      TCGv_i32 var1 = tcg_temp_new_i32();

    int var2 = 0;       ->      TCGv_i32 var1 = tcg_temp_new_i32();
                                tcg_gen_movi_i32(j, ((int64_t) 0ULL));

which are later automatically freed at the end of the function they're declared
in. Contrary to C, we only allow variables to be declared with an integer type
specified in the following table (without permutation of keywords)

::

    type                        bit-width    signedness
    ----------------------------------------------------------
    int                         32           signed
    signed
    signed int

    unsigned                    32           unsigned
    unsigned int

    long                        64           signed
    long int
    signed long
    signed long int

    unsigned long               64           unsigned
    unsigned long int

    long long                   64           signed
    long long int
    signed long long
    signed long long int

    unsigned long long          64           unsigned
    unsigned long long int

    size[1,2,4,8][s,u]_t        8-64         signed or unsigned

In idef-parser, variable names are matched by a generic ``VARID`` token,
which will feature the variable name as a decoration. For a variable declaration
idef-parser calls ``gen_varid_allocate`` with the ``VARID`` token to save the
name, size, and bit width of the newly declared variable. In addition, this
function also ensures that variables aren't declared multiple times, and prints
and error message if that is the case. Upon use of a variable, the ``VARID``
token is used to lookup the size and bit width of the variable.

Type System
~~~~~~~~~~~

idef-parser features a simple type system which is used to correctly implement
the signedness and bit width of the operations.

The type of each ``rvalue`` is determined by two attributes: its bit width
(``unsigned bit_width``) and its signedness (``HexSignedness signedness``).

For each operation, the type of ``rvalue``\ s influence the way in which the
operands are handled and emitted. For example a right shift between signed
operators will be an arithmetic shift, while one between unsigned operators
will be a logical shift. If one of the two operands is signed, and the other
is unsigned, the operation will be signed.

The bit width also influences the outcome of the operations, in particular while
the input languages features a fine granularity type system, with types of 8,
16, 32, 64 (and more for vectorial instructions) bits, the tinycode only
features 32 and 64 bit widths. We propagate as much as possible the fine
granularity type, until the value has to be used inside an operation between
``rvalue``\ s; in that case if one of the two operands is greater than 32 bits
we promote the whole operation to 64 bit, taking care of properly extending the
two operands. Fortunately, the most critical instructions already feature
explicit casts and zero/sign extensions which are properly propagated down to
our parser.

The combination of ``rvalue``\ s are handled through the use of the
``gen_bin_op`` and ``gen_bin_cmp`` helper functions. These two functions handle
the appropriate compile-time or run-time emission of operations to perform the
required computation.

Control Statements
~~~~~~~~~~~~~~~~~~

``control_statement``\ s are all the statements which modify the order of
execution of the generated code according to input parameters. They are expanded
by the following grammar rule:

::

   control_statement : frame_check
                     | cancel_statement
                     | if_statement
                     | for_statement
                     | fpart1_statement

``if_statement``\ s require the emission of labels and branch instructions which
effectively perform conditional jumps (``tcg_gen_brcondi``) according to the
value of an expression. Note, the tinycode generators we produce for conditional
statements do not perfectly mirror what would be expected in C, for instance we
do not reproduce short-circuiting of the ``&&`` operator, and use of the ``||``
operator is disallowed. All the predicated instructions, and in general all the
instructions where there could be alternative values assigned to an ``lvalue``,
like C-style ternary expressions:

::

   rvalue            : rvalue QMARK rvalue COLON rvalue

are handled using the conditional move tinycode instruction
(``tcg_gen_movcond``), which avoids the additional complexity of managing labels
and jumps.

Instead, regarding the ``for`` loops, exploiting the fact that they always
iterate on immediate values, therefore their iteration ranges are always known
at compile time, we implemented those emitting plain C ``for`` loops. This is
possible because the loops will be executed in the QEMU code, leading to the
consequential unrolling of the for loop, since the tinycode generator
instructions will be executed multiple times, and the respective generated
tinycode will represent the unrolled execution of the loop.

Parsing Context
~~~~~~~~~~~~~~~

All the helper functions in ``idef-parser.y`` carry two fixed parameters, which
are the parsing context ``c`` and the ``YYLLOC`` location information. The
context is explicitly passed to all the functions because the parser we generate
is a reentrant one, meaning that it does not have any global variable, and
therefore the instruction compilation could easily be parallelized in the
future. Finally for each rule we propagate information about the location of the
involved tokens to generate pretty error reporting, able to highlight the
portion of the input code which generated each error.

Debugging
---------

Developing the idef-parser can lead to two types of errors: compile-time errors
and parsing errors.

Compile-time errors in Bison-generated parsers are usually due to conflicts in
the described grammar. Conflicts forbid the grammar to produce a unique
derivation tree, thus must be solved (except for the dangling else problem,
which is marked as expected through the ``%expect 1`` Bison option).

For solving conflicts you need a basic understanding of `shift-reduce conflicts
<https://www.gnu.org/software/Bison/manual/html_node/Shift_002fReduce.html>`__
and `reduce-reduce conflicts
<https://www.gnu.org/software/Bison/manual/html_node/Reduce_002fReduce.html>`__,
then, if you are using a Bison version > 3.7.1 you can ask Bison to generate
some counterexamples which highlight ambiguous derivations, passing the
``-Wcex`` option to Bison. In general shift/reduce conflicts are solved by
redesigning the grammar in an unambiguous way or by setting the token priority
correctly, while reduce/reduce conflicts are solved by redesigning the
interested part of the grammar.

Run-time errors can be divided between lexing and parsing errors, lexing errors
are hard to detect, since the ``var`` token will catch everything which is not
catched by other tokens, but easy to fix, because most of the time a simple
regex editing will be enough.

idef-parser features a fancy parsing error reporting scheme, which for each
parsing error reports the fragment of the input text which was involved in the
parsing rule that generated an error.

Implementing an instruction goes through several sequential steps, here are some
suggestions to make each instruction proceed to the next step.

-  not-emitted

   Means that the parsing of the input code relative to that instruction failed,
   this could be due to a lexical error or to some mismatch between the order of
   valid tokens and a parser rule. You should check that tokens are correctly
   identified and mapped, and that there is a rule matching the token sequence
   that you need to parse.

-  emitted

   This instruction class contains all the instructions which are emitted but
   fail to compile when included in QEMU. The compilation errors are shown by
   the QEMU building process and will lead to fixing the bug.  Most common
   errors regard the mismatch of parameters for tinycode generator functions,
   which boil down to errors in the idef-parser type system.

-  compiled

   These instruction generate valid tinycode generator code, which however fail
   the QEMU or the harness tests, these cases must be handled manually by
   looking into the failing tests and looking at the generated tinycode
   generator instruction and at the generated tinycode itself. Tip: handle the
   failing harness tests first, because they usually feature only a single
   instruction, thus will require less execution trace navigation. If a
   multi-threaded test fail, fixing all the other tests will be the easier
   option, hoping that the multi-threaded one will be indirectly fixed.

   An example of debugging this type of failure is provided in the following
   section.

-  tests-passed

   This is the final goal for each instruction, meaning that the instruction
   passes the test suite.

Another approach to fix QEMU system test, where many instructions might fail, is
to compare the execution trace of your implementation with the reference
implementations already present in QEMU. To do so you should obtain a QEMU build
where the instruction pass the test, and run it with the following command:

::

   sudo unshare -p sudo -u <USER> bash -c \
   'env -i <qemu-hexagon full path> -d cpu <TEST>'

And do the same for your implementation, the generated execution traces will be
inherently aligned and can be inspected for behavioral differences using the
``diff`` tool.

Example of debugging erroneous tinycode generator code
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The goal of this section is to provide a complete example of debugging
incorrectly emitted tinycode generator for a single instruction.

Let's first introduce a bug in the tinycode generator of the ``A2_add``
instruction,

::

    void emit_A2_add(DisasContext *ctx, Insn *insn, Packet *pkt, TCGv_i32 RdV,
                     TCGv_i32 RsV, TCGv_i32 RtV)
    /*  RdV=RsV+RtV;} */
    {
        TCGv_i32 tmp_0 = tcg_temp_new_i32();
        tcg_gen_add_i32(tmp_0, RsV, RsV);
        tcg_gen_mov_i32(RdV, tmp_0);
    }

Here the bug, albeit hard to spot, is in ``tcg_gen_add_i32(tmp_0, RsV, RsV);``
where we compute ``RsV + RsV`` instead of ``RsV + RtV``, as would be expected.
This particular bug is a bit tricky to pinpoint when debugging, since the
``A2_add`` instruction is so ubiquitous. As a result, pretty much all tests will
fail and therefore not provide a lot of information about the bug.

For example, let's run the ``check-tcg`` tests

::

    make check-tcg TIMEOUT=1200 \
                   DOCKER_IMAGE=debian-hexagon-cross \
                   ENGINE=podman V=1 \
                   DOCKER_CROSS_CC_GUEST=hexagon-unknown-linux-musl-clang

In the output, we find a failure in the very first test case ``float_convs``
due to a segmentation fault. Similarly, all harness and libc tests will fail as
well. At this point we have no clue where the actual bug lies, and need to start
ruling out instructions. As such a good starting point is to utilize the debug
options ``-d in_asm,cpu`` of QEMU to inspect the Hexagon instructions being run,
alongside the CPU state. We additionally need a working version of the emulator
to compare our buggy CPU state against, running

::

    meson configure -Dhexagon_idef_parser=false

will disable the idef-parser for all instructions and fallback on manual
tinycode generator overrides, or on helper function implementations. Recompiling
gives us ``qemu-hexagon`` which passes all tests. If ``qemu-hexagon-buggy`` is
our binary with the incorrect tinycode generators, we can compare the CPU state
between the two versions

::

    ./qemu-hexagon-buggy -d in_asm,cpu float_convs &> out_buggy
    ./qemu-hexagon       -d in_asm,cpu float_convs &> out_working

Looking at ``diff -u out_buggy out_working`` shows us that the CPU state begins
to diverge on line 141, with an incorrect value in the ``R1`` register

::

    @@ -138,7 +138,7 @@

     General Purpose Registers = {
       r0 = 0x4100f9c0
    -  r1 = 0x00042108
    +  r1 = 0x00000000
       r2 = 0x00021084
       r3 = 0x00000000
       r4 = 0x00000000

If we also look into ``out_buggy`` directly we can inspect the input assembly
which the caused the incorrect CPU state, around line 141 we find

::

    116 |  ----------------
    117 |  IN: _start_c
    118 |  0x000210b0:  0xa09dc002	{	allocframe(R29,#0x10):raw }
    ... |  ...
    137 |  0x000210fc:  0x5a00c4aa	{	call PC+2388 }
    138 |
    139 |  General Purpose Registers = {
    140 |    r0 = 0x4100fa70
    141 |    r1 = 0x00042108
    142 |    r2 = 0x00021084
    143 |    r3 = 0x00000000

Importantly, we see some Hexagon assembly followed by a dump of the CPU state,
now the CPU state is actually dumped before the input assembly above is ran.
As such, we are actually interested in the instructions ran before this.

Scrolling up a bit, we find

::

    54 |  ----------------
    55 |  IN: _start
    56 |  0x00021088:  0x6a09c002	{	R2 = C9/pc }
    57 |  0x0002108c:  0xbfe2ff82	{	R2 = add(R2,#0xfffffffc) }
    58 |  0x00021090:  0x9182c001	{	R1 = memw(R2+#0x0) }
    59 |  0x00021094:  0xf302c101	{	R1 = add(R2,R1) }
    60 |  0x00021098:  0x7800c01e	{	R30 = #0x0 }
    61 |  0x0002109c:  0x707dc000	{	R0 = R29 }
    62 |  0x000210a0:  0x763dfe1d	{	R29 = and(R29,#0xfffffff0) }
    63 |  0x000210a4:  0xa79dfdfe	{	memw(R29+#0xfffffff8) = R29 }
    64 |  0x000210a8:  0xbffdff1d	{	R29 = add(R29,#0xfffffff8) }
    65 |  0x000210ac:  0x5a00c002	{	call PC+4 }
    66 |
    67 |  General Purpose Registers = {
    68 |    r0 = 0x00000000
    69 |    r1 = 0x00000000
    70 |    r2 = 0x00000000
    71 |    r3 = 0x00000000

Remember, the instructions on lines 56-65 are ran on the CPU state shown below
instructions, and as the CPU state has not diverged at this point, we know the
starting state is accurate. The bug must then lie within the instructions shown
here. Next we may notice that ``R1`` is only touched by lines 57 and 58, that is
by

::

    58 |  0x00021090:  0x9182c001	{	R1 = memw(R2+#0x0) }
    59 |  0x00021094:  0xf302c101	{	R1 = add(R2,R1) }

Therefore, we are either dealing with an correct load instruction
``R1 = memw(R2+#0x0)`` or with an incorrect add ``R1 = add(R2,R1)``. At this
point it might be easy enough to go directly to the emitted code for the
instructions mentioned and look for bugs, but we could also run
``./qemu-heaxgon -d op,in_asm float_conv`` where we find for the following
tinycode for the Hexagon ``add`` instruction

::

   ---- 00021094
   mov_i32 pkt_has_store_s1,$0x0
   add_i32 tmp0,r2,r2
   mov_i32 loc2,tmp0
   mov_i32 new_r1,loc2
   mov_i32 r1,new_r1

Here we have finally located our bug ``add_i32 tmp0,r2,r2``.

Limitations and Future Development
----------------------------------

The main limitation of the current parser is given by the syntax-driven nature
of the Bison-generated parsers. This has the severe implication of only being
able to generate code in the order of evaluation of the various rules, without,
in any case, being able to backtrack and alter the generated code.

An example limitation is highlighted by this statement of the input language:

::

   { (PsV==0xff) ? (PdV=0xff) : (PdV=0x00); }

This ternary assignment, when written in this form requires us to emit some
proper control flow statements, which emit a jump to the first or to the second
code block, whose implementation is extremely convoluted, because when matching
the ternary assignment, the code evaluating the two assignments will be already
generated.

Instead we pre-process that statement, making it become:

::

   { PdV = ((PsV==0xff)) ? 0xff : 0x00; }

Which can be easily matched by the following parser rules:

::

   statement             | rvalue ';'

   rvalue                : rvalue QMARK rvalue COLON rvalue
                         | rvalue EQ rvalue
                         | LPAR rvalue RPAR
                         | assign_statement
                         | IMM

   assign_statement      : pred ASSIGN rvalue

Another example that highlight the limitation of the flex/bison parser can be
found even in the add operation we already saw:

::

   TCGv_i32 tmp_0 = tcg_temp_new_i32();
   tcg_gen_add_i32(tmp_0, RsV, RtV);
   tcg_gen_mov_i32(RdV, tmp_0);

The fact that we cannot directly use ``RdV`` as the destination of the sum is a
consequence of the syntax-driven nature of the parser. In fact when we parse the
assignment, the ``rvalue`` token, representing the sum has already been reduced,
and thus its code emitted and unchangeable. We rely on the fact that QEMU will
optimize our code reducing the useless move operations and the relative
temporaries.

A possible improvement of the parser regards the support for vectorial
instructions and floating point instructions, which will require the extension
of the scanner, the parser, and a partial re-design of the type system, allowing
to build the vectorial semantics over the available vectorial tinycode generator
primitives.

A more radical improvement will use the parser, not to generate directly the
tinycode generator code, but to generate an intermediate representation like the
LLVM IR, which in turn could be compiled using the clang TCG backend. That code
could be furtherly optimized, overcoming the limitations of the syntax-driven
parsing and could lead to a more optimized generated code.
