==================================
How to use the QAPI code generator
==================================

..
   Copyright IBM Corp. 2011
   Copyright (C) 2012-2016 Red Hat, Inc.

   This work is licensed under the terms of the GNU GPL, version 2 or
   later.  See the COPYING file in the top-level directory.


Introduction
============

QAPI is a native C API within QEMU which provides management-level
functionality to internal and external users.  For external
users/processes, this interface is made available by a JSON-based wire
format for the QEMU Monitor Protocol (QMP) for controlling qemu, as
well as the QEMU Guest Agent (QGA) for communicating with the guest.
The remainder of this document uses "Client JSON Protocol" when
referring to the wire contents of a QMP or QGA connection.

To map between Client JSON Protocol interfaces and the native C API,
we generate C code from a QAPI schema.  This document describes the
QAPI schema language, and how it gets mapped to the Client JSON
Protocol and to C.  It additionally provides guidance on maintaining
Client JSON Protocol compatibility.


The QAPI schema language
========================

The QAPI schema defines the Client JSON Protocol's commands and
events, as well as types used by them.  Forward references are
allowed.

It is permissible for the schema to contain additional types not used
by any commands or events, for the side effect of generated C code
used internally.

There are several kinds of types: simple types (a number of built-in
types, such as ``int`` and ``str``; as well as enumerations), arrays,
complex types (structs and unions), and alternate types (a choice
between other types).


Schema syntax
-------------

Syntax is loosely based on `JSON <http://www.ietf.org/rfc/rfc8259.txt>`_.
Differences:

* Comments: start with a hash character (``#``) that is not part of a
  string, and extend to the end of the line.

* Strings are enclosed in ``'single quotes'``, not ``"double quotes"``.

* Strings are restricted to printable ASCII, and escape sequences to
  just ``\\``.

* Numbers and ``null`` are not supported.

A second layer of syntax defines the sequences of JSON texts that are
a correctly structured QAPI schema.  We provide a grammar for this
syntax in an EBNF-like notation:

* Production rules look like ``non-terminal = expression``
* Concatenation: expression ``A B`` matches expression ``A``, then ``B``
* Alternation: expression ``A | B`` matches expression ``A`` or ``B``
* Repetition: expression ``A...`` matches zero or more occurrences of
  expression ``A``
* Repetition: expression ``A, ...`` matches zero or more occurrences of
  expression ``A`` separated by ``,``
* Grouping: expression ``( A )`` matches expression ``A``
* JSON's structural characters are terminals: ``{ } [ ] : ,``
* JSON's literal names are terminals: ``false true``
* String literals enclosed in ``'single quotes'`` are terminal, and match
  this JSON string, with a leading ``*`` stripped off
* When JSON object member's name starts with ``*``, the member is
  optional.
* The symbol ``STRING`` is a terminal, and matches any JSON string
* The symbol ``BOOL`` is a terminal, and matches JSON ``false`` or ``true``
* ALL-CAPS words other than ``STRING`` are non-terminals

The order of members within JSON objects does not matter unless
explicitly noted.

A QAPI schema consists of a series of top-level expressions::

    SCHEMA = TOP-LEVEL-EXPR...

The top-level expressions are all JSON objects.  Code and
documentation is generated in schema definition order.  Code order
should not matter.

A top-level expressions is either a directive or a definition::

    TOP-LEVEL-EXPR = DIRECTIVE | DEFINITION

There are two kinds of directives and six kinds of definitions::

    DIRECTIVE = INCLUDE | PRAGMA
    DEFINITION = ENUM | STRUCT | UNION | ALTERNATE | COMMAND | EVENT

These are discussed in detail below.


Built-in Types
--------------

The following types are predefined, and map to C as follows:

  ============= ============== ============================================
  Schema        C              JSON
  ============= ============== ============================================
  ``str``       ``char *``     any JSON string, UTF-8
  ``number``    ``double``     any JSON number
  ``int``       ``int64_t``    a JSON number without fractional part
                               that fits into the C integer type
  ``int8``      ``int8_t``     likewise
  ``int16``     ``int16_t``    likewise
  ``int32``     ``int32_t``    likewise
  ``int64``     ``int64_t``    likewise
  ``uint8``     ``uint8_t``    likewise
  ``uint16``    ``uint16_t``   likewise
  ``uint32``    ``uint32_t``   likewise
  ``uint64``    ``uint64_t``   likewise
  ``size``      ``uint64_t``   like ``uint64_t``, except
                               ``StringInputVisitor`` accepts size suffixes
  ``bool``      ``bool``       JSON ``true`` or ``false``
  ``null``      ``QNull *``    JSON ``null``
  ``any``       ``QObject *``  any JSON value
  ``QType``     ``QType``      JSON string matching enum ``QType`` values
  ============= ============== ============================================


Include directives
------------------

Syntax::

    INCLUDE = { 'include': STRING }

The QAPI schema definitions can be modularized using the 'include' directive::

 { 'include': 'path/to/file.json' }

The directive is evaluated recursively, and include paths are relative
to the file using the directive.  Multiple includes of the same file
are idempotent.

As a matter of style, it is a good idea to have all files be
self-contained, but at the moment, nothing prevents an included file
from making a forward reference to a type that is only introduced by
an outer file.  The parser may be made stricter in the future to
prevent incomplete include files.

.. _pragma:

Pragma directives
-----------------

Syntax::

    PRAGMA = { 'pragma': {
                   '*doc-required': BOOL,
                   '*command-name-exceptions': [ STRING, ... ],
                   '*command-returns-exceptions': [ STRING, ... ],
                   '*documentation-exceptions': [ STRING, ... ],
                   '*member-name-exceptions': [ STRING, ... ] } }

The pragma directive lets you control optional generator behavior.

Pragma's scope is currently the complete schema.  Setting the same
pragma to different values in parts of the schema doesn't work.

Pragma 'doc-required' takes a boolean value.  If true, documentation
is required.  Default is false.

Pragma 'command-name-exceptions' takes a list of commands whose names
may contain ``"_"`` instead of ``"-"``.  Default is none.

Pragma 'command-returns-exceptions' takes a list of commands that may
violate the rules on permitted return types.  Default is none.

Pragma 'documentation-exceptions' takes a list of types, commands, and
events whose members / arguments need not be documented.  Default is
none.

Pragma 'member-name-exceptions' takes a list of types whose member
names may contain uppercase letters, and ``"_"`` instead of ``"-"``.
Default is none.

.. _ENUM-VALUE:

Enumeration types
-----------------

Syntax::

    ENUM = { 'enum': STRING,
             'data': [ ENUM-VALUE, ... ],
             '*prefix': STRING,
             '*if': COND,
             '*features': FEATURES }
    ENUM-VALUE = STRING
               | { 'name': STRING,
                   '*if': COND,
                   '*features': FEATURES }

Member 'enum' names the enum type.

Each member of the 'data' array defines a value of the enumeration
type.  The form STRING is shorthand for :code:`{ 'name': STRING }`.  The
'name' values must be be distinct.

Example::

 { 'enum': 'MyEnum', 'data': [ 'value1', 'value2', 'value3' ] }

Nothing prevents an empty enumeration, although it is probably not
useful.

On the wire, an enumeration type's value is represented by its
(string) name.  In C, it's represented by an enumeration constant.
These are of the form PREFIX_NAME, where PREFIX is derived from the
enumeration type's name, and NAME from the value's name.  For the
example above, the generator maps 'MyEnum' to MY_ENUM and 'value1' to
VALUE1, resulting in the enumeration constant MY_ENUM_VALUE1.  The
optional 'prefix' member overrides PREFIX.

The generated C enumeration constants have values 0, 1, ..., N-1 (in
QAPI schema order), where N is the number of values.  There is an
additional enumeration constant PREFIX__MAX with value N.

Do not use string or an integer type when an enumeration type can do
the job satisfactorily.

The optional 'if' member specifies a conditional.  See `Configuring the
schema`_ below for more on this.

The optional 'features' member specifies features.  See Features_
below for more on this.


.. _TYPE-REF:

Type references and array types
-------------------------------

Syntax::

    TYPE-REF = STRING | ARRAY-TYPE
    ARRAY-TYPE = [ STRING ]

A string denotes the type named by the string.

A one-element array containing a string denotes an array of the type
named by the string.  Example: ``['int']`` denotes an array of ``int``.


Struct types
------------

Syntax::

    STRUCT = { 'struct': STRING,
               'data': MEMBERS,
               '*base': STRING,
               '*if': COND,
               '*features': FEATURES }
    MEMBERS = { MEMBER, ... }
    MEMBER = STRING : TYPE-REF
           | STRING : { 'type': TYPE-REF,
                        '*if': COND,
                        '*features': FEATURES }

Member 'struct' names the struct type.

Each MEMBER of the 'data' object defines a member of the struct type.

.. _MEMBERS:

The MEMBER's STRING name consists of an optional ``*`` prefix and the
struct member name.  If ``*`` is present, the member is optional.

The MEMBER's value defines its properties, in particular its type.
The form TYPE-REF_ is shorthand for :code:`{ 'type': TYPE-REF }`.

Example::

 { 'struct': 'MyType',
   'data': { 'member1': 'str', 'member2': ['int'], '*member3': 'str' } }

A struct type corresponds to a struct in C, and an object in JSON.
The C struct's members are generated in QAPI schema order.

The optional 'base' member names a struct type whose members are to be
included in this type.  They go first in the C struct.

Example::

 { 'struct': 'BlockdevOptionsGenericFormat',
   'data': { 'file': 'str' } }
 { 'struct': 'BlockdevOptionsGenericCOWFormat',
   'base': 'BlockdevOptionsGenericFormat',
   'data': { '*backing': 'str' } }

An example BlockdevOptionsGenericCOWFormat object on the wire could use
both members like this::

 { "file": "/some/place/my-image",
   "backing": "/some/place/my-backing-file" }

The optional 'if' member specifies a conditional.  See `Configuring
the schema`_ below for more on this.

The optional 'features' member specifies features.  See Features_
below for more on this.


Union types
-----------

Syntax::

    UNION = { 'union': STRING,
              'base': ( MEMBERS | STRING ),
              'discriminator': STRING,
              'data': BRANCHES,
              '*if': COND,
              '*features': FEATURES }
    BRANCHES = { BRANCH, ... }
    BRANCH = STRING : TYPE-REF
           | STRING : { 'type': TYPE-REF, '*if': COND }

Member 'union' names the union type.

The 'base' member defines the common members.  If it is a MEMBERS_
object, it defines common members just like a struct type's 'data'
member defines struct type members.  If it is a STRING, it names a
struct type whose members are the common members.

Member 'discriminator' must name a non-optional enum-typed member of
the base struct.  That member's value selects a branch by its name.
If no such branch exists, an empty branch is assumed.

Each BRANCH of the 'data' object defines a branch of the union.  A
union must have at least one branch.

The BRANCH's STRING name is the branch name.  It must be a value of
the discriminator enum type.

The BRANCH's value defines the branch's properties, in particular its
type.  The type must a struct type.  The form TYPE-REF_ is shorthand
for :code:`{ 'type': TYPE-REF }`.

In the Client JSON Protocol, a union is represented by an object with
the common members (from the base type) and the selected branch's
members.  The two sets of member names must be disjoint.

Example::

 { 'enum': 'BlockdevDriver', 'data': [ 'file', 'qcow2' ] }
 { 'union': 'BlockdevOptions',
   'base': { 'driver': 'BlockdevDriver', '*read-only': 'bool' },
   'discriminator': 'driver',
   'data': { 'file': 'BlockdevOptionsFile',
             'qcow2': 'BlockdevOptionsQcow2' } }

Resulting in these JSON objects::

 { "driver": "file", "read-only": true,
   "filename": "/some/place/my-image" }
 { "driver": "qcow2", "read-only": false,
   "backing": "/some/place/my-image", "lazy-refcounts": true }

The order of branches need not match the order of the enum values.
The branches need not cover all possible enum values.  In the
resulting generated C data types, a union is represented as a struct
with the base members in QAPI schema order, and then a union of
structures for each branch of the struct.

The optional 'if' member specifies a conditional.  See `Configuring
the schema`_ below for more on this.

The optional 'features' member specifies features.  See Features_
below for more on this.


Alternate types
---------------

Syntax::

    ALTERNATE = { 'alternate': STRING,
                  'data': ALTERNATIVES,
                  '*if': COND,
                  '*features': FEATURES }
    ALTERNATIVES = { ALTERNATIVE, ... }
    ALTERNATIVE = STRING : STRING
                | STRING : { 'type': STRING, '*if': COND }

Member 'alternate' names the alternate type.

Each ALTERNATIVE of the 'data' object defines a branch of the
alternate.  An alternate must have at least one branch.

The ALTERNATIVE's STRING name is the branch name.

The ALTERNATIVE's value defines the branch's properties, in particular
its type.  The form STRING is shorthand for :code:`{ 'type': STRING }`.

Example::

 { 'alternate': 'BlockdevRef',
   'data': { 'definition': 'BlockdevOptions',
             'reference': 'str' } }

An alternate type is like a union type, except there is no
discriminator on the wire.  Instead, the branch to use is inferred
from the value.  An alternate can only express a choice between types
represented differently on the wire.

If a branch is typed as the 'bool' built-in, the alternate accepts
true and false; if it is typed as any of the various numeric
built-ins, it accepts a JSON number; if it is typed as a 'str'
built-in or named enum type, it accepts a JSON string; if it is typed
as the 'null' built-in, it accepts JSON null; and if it is typed as a
complex type (struct or union), it accepts a JSON object.

The example alternate declaration above allows using both of the
following example objects::

 { "file": "my_existing_block_device_id" }
 { "file": { "driver": "file",
             "read-only": false,
             "filename": "/tmp/mydisk.qcow2" } }

The optional 'if' member specifies a conditional.  See `Configuring
the schema`_ below for more on this.

The optional 'features' member specifies features.  See Features_
below for more on this.


Commands
--------

Syntax::

    COMMAND = { 'command': STRING,
                (
                '*data': ( MEMBERS | STRING ),
                |
                'data': STRING,
                'boxed': true,
                )
                '*returns': TYPE-REF,
                '*success-response': false,
                '*gen': false,
                '*allow-oob': true,
                '*allow-preconfig': true,
                '*coroutine': true,
                '*if': COND,
                '*features': FEATURES }

Member 'command' names the command.

Member 'data' defines the arguments.  It defaults to an empty MEMBERS_
object.

If 'data' is a MEMBERS_ object, then MEMBERS defines arguments just
like a struct type's 'data' defines struct type members.

If 'data' is a STRING, then STRING names a complex type whose members
are the arguments.  A union type requires ``'boxed': true``.

Member 'returns' defines the command's return type.  It defaults to an
empty struct type.  It must normally be a complex type or an array of
a complex type.  To return anything else, the command must be listed
in pragma 'commands-returns-exceptions'.  If you do this, extending
the command to return additional information will be harder.  Use of
the pragma for new commands is strongly discouraged.

A command's error responses are not specified in the QAPI schema.
Error conditions should be documented in comments.

In the Client JSON Protocol, the value of the "execute" or "exec-oob"
member is the command name.  The value of the "arguments" member then
has to conform to the arguments, and the value of the success
response's "return" member will conform to the return type.

Some example commands::

 { 'command': 'my-first-command',
   'data': { 'arg1': 'str', '*arg2': 'str' } }
 { 'struct': 'MyType', 'data': { '*value': 'str' } }
 { 'command': 'my-second-command',
   'returns': [ 'MyType' ] }

which would validate this Client JSON Protocol transaction::

 => { "execute": "my-first-command",
      "arguments": { "arg1": "hello" } }
 <= { "return": { } }
 => { "execute": "my-second-command" }
 <= { "return": [ { "value": "one" }, { } ] }

The generator emits a prototype for the C function implementing the
command.  The function itself needs to be written by hand.  See
section `Code generated for commands`_ for examples.

The function returns the return type.  When member 'boxed' is absent,
it takes the command arguments as arguments one by one, in QAPI schema
order.  Else it takes them wrapped in the C struct generated for the
complex argument type.  It takes an additional ``Error **`` argument in
either case.

The generator also emits a marshalling function that extracts
arguments for the user's function out of an input QDict, calls the
user's function, and if it succeeded, builds an output QObject from
its return value.  This is for use by the QMP monitor core.

In rare cases, QAPI cannot express a type-safe representation of a
corresponding Client JSON Protocol command.  You then have to suppress
generation of a marshalling function by including a member 'gen' with
boolean value false, and instead write your own function.  For
example::

 { 'command': 'netdev_add',
   'data': {'type': 'str', 'id': 'str'},
   'gen': false }

Please try to avoid adding new commands that rely on this, and instead
use type-safe unions.

Normally, the QAPI schema is used to describe synchronous exchanges,
where a response is expected.  But in some cases, the action of a
command is expected to change state in a way that a successful
response is not possible (although the command will still return an
error object on failure).  When a successful reply is not possible,
the command definition includes the optional member 'success-response'
with boolean value false.  So far, only QGA makes use of this member.

Member 'allow-oob' declares whether the command supports out-of-band
(OOB) execution.  It defaults to false.  For example::

 { 'command': 'migrate_recover',
   'data': { 'uri': 'str' }, 'allow-oob': true }

See the :doc:`/interop/qmp-spec` for out-of-band execution syntax
and semantics.

Commands supporting out-of-band execution can still be executed
in-band.

When a command is executed in-band, its handler runs in the main
thread with the BQL held.

When a command is executed out-of-band, its handler runs in a
dedicated monitor I/O thread with the BQL *not* held.

An OOB-capable command handler must satisfy the following conditions:

- It terminates quickly.
- It does not invoke system calls that may block.
- It does not access guest RAM that may block when userfaultfd is
  enabled for postcopy live migration.
- It takes only "fast" locks, i.e. all critical sections protected by
  any lock it takes also satisfy the conditions for OOB command
  handler code.

The restrictions on locking limit access to shared state.  Such access
requires synchronization, but OOB commands can't take the BQL or any
other "slow" lock.

When in doubt, do not implement OOB execution support.

Member 'allow-preconfig' declares whether the command is available
before the machine is built.  It defaults to false.  For example::

 { 'enum': 'QMPCapability',
   'data': [ 'oob' ] }
 { 'command': 'qmp_capabilities',
   'data': { '*enable': [ 'QMPCapability' ] },
   'allow-preconfig': true }

QMP is available before the machine is built only when QEMU was
started with --preconfig.

Member 'coroutine' tells the QMP dispatcher whether the command handler
is safe to be run in a coroutine.  It defaults to false.  If it is true,
the command handler is called from coroutine context and may yield while
waiting for an external event (such as I/O completion) in order to avoid
blocking the guest and other background operations.

Coroutine safety can be hard to prove, similar to thread safety.  Common
pitfalls are:

- The BQL isn't held across ``qemu_coroutine_yield()``, so
  operations that used to assume that they execute atomically may have
  to be more careful to protect against changes in the global state.

- Nested event loops (``AIO_WAIT_WHILE()`` etc.) are problematic in
  coroutine context and can easily lead to deadlocks.  They should be
  replaced by yielding and reentering the coroutine when the condition
  becomes false.

Since the command handler may assume coroutine context, any callers
other than the QMP dispatcher must also call it in coroutine context.
In particular, HMP commands calling such a QMP command handler must be
marked ``.coroutine = true`` in hmp-commands.hx.

It is an error to specify both ``'coroutine': true`` and ``'allow-oob': true``
for a command.  We don't currently have a use case for both together and
without a use case, it's not entirely clear what the semantics should
be.

The optional 'if' member specifies a conditional.  See `Configuring
the schema`_ below for more on this.

The optional 'features' member specifies features.  See Features_
below for more on this.


Events
------

Syntax::

    EVENT = { 'event': STRING,
              (
              '*data': ( MEMBERS | STRING ),
              |
              'data': STRING,
              'boxed': true,
              )
              '*if': COND,
              '*features': FEATURES }

Member 'event' names the event.  This is the event name used in the
Client JSON Protocol.

Member 'data' defines the event-specific data.  It defaults to an
empty MEMBERS object.

If 'data' is a MEMBERS object, then MEMBERS defines event-specific
data just like a struct type's 'data' defines struct type members.

If 'data' is a STRING, then STRING names a complex type whose members
are the event-specific data.  A union type requires ``'boxed': true``.

An example event is::

 { 'event': 'EVENT_C',
   'data': { '*a': 'int', 'b': 'str' } }

Resulting in this JSON object::

 { "event": "EVENT_C",
   "data": { "b": "test string" },
   "timestamp": { "seconds": 1267020223, "microseconds": 435656 } }

The generator emits a function to send the event.  When member 'boxed'
is absent, it takes event-specific data one by one, in QAPI schema
order.  Else it takes them wrapped in the C struct generated for the
complex type.  See section `Code generated for events`_ for examples.

The optional 'if' member specifies a conditional.  See `Configuring
the schema`_ below for more on this.

The optional 'features' member specifies features.  See Features_
below for more on this.


.. _FEATURE:

Features
--------

Syntax::

    FEATURES = [ FEATURE, ... ]
    FEATURE = STRING
            | { 'name': STRING, '*if': COND }

Sometimes, the behaviour of QEMU changes compatibly, but without a
change in the QMP syntax (usually by allowing values or operations
that previously resulted in an error).  QMP clients may still need to
know whether the extension is available.

For this purpose, a list of features can be specified for definitions,
enumeration values, and struct members.  Each feature list member can
either be ``{ 'name': STRING, '*if': COND }``, or STRING, which is
shorthand for ``{ 'name': STRING }``.

The optional 'if' member specifies a conditional.  See `Configuring
the schema`_ below for more on this.

Example::

 { 'struct': 'TestType',
   'data': { 'number': 'int' },
   'features': [ 'allow-negative-numbers' ] }

The feature strings are exposed to clients in introspection, as
explained in section `Client JSON Protocol introspection`_.

Intended use is to have each feature string signal that this build of
QEMU shows a certain behaviour.


Special features
~~~~~~~~~~~~~~~~

Feature "deprecated" marks a command, event, enum value, or struct
member as deprecated.  It is not supported elsewhere so far.
Interfaces so marked may be withdrawn in future releases in accordance
with QEMU's deprecation policy.

Feature "unstable" marks a command, event, enum value, or struct
member as unstable.  It is not supported elsewhere so far.  Interfaces
so marked may be withdrawn or changed incompatibly in future releases.


Naming rules and reserved names
-------------------------------

All names must begin with a letter, and contain only ASCII letters,
digits, hyphen, and underscore.  There are two exceptions: enum values
may start with a digit, and names that are downstream extensions (see
section `Downstream extensions`_) start with underscore.

Names beginning with ``q_`` are reserved for the generator, which uses
them for munging QMP names that resemble C keywords or other
problematic strings.  For example, a member named ``default`` in qapi
becomes ``q_default`` in the generated C code.

Types, commands, and events share a common namespace.  Therefore,
generally speaking, type definitions should always use CamelCase for
user-defined type names, while built-in types are lowercase.

Type names ending with ``List`` are reserved for the generator, which
uses them for array types.

Command names, member names within a type, and feature names should be
all lower case with words separated by a hyphen.  However, some
existing older commands and complex types use underscore; when
extending them, consistency is preferred over blindly avoiding
underscore.

Event names should be ALL_CAPS with words separated by underscore.

Member name ``u`` and names starting with ``has-`` or ``has_`` are reserved
for the generator, which uses them for unions and for tracking
optional members.

Names beginning with ``x-`` used to signify "experimental".  This
convention has been replaced by special feature "unstable".

Pragmas ``command-name-exceptions`` and ``member-name-exceptions`` let
you violate naming rules.  Use for new code is strongly discouraged. See
`Pragma directives`_ for details.


Downstream extensions
---------------------

QAPI schema names that are externally visible, say in the Client JSON
Protocol, need to be managed with care.  Names starting with a
downstream prefix of the form __RFQDN_ are reserved for the downstream
who controls the valid, reverse fully qualified domain name RFQDN.
RFQDN may only contain ASCII letters, digits, hyphen and period.

Example: Red Hat, Inc. controls redhat.com, and may therefore add a
downstream command ``__com.redhat_drive-mirror``.


Configuring the schema
----------------------

Syntax::

    COND = STRING
         | { 'all: [ COND, ... ] }
         | { 'any: [ COND, ... ] }
         | { 'not': COND }

All definitions take an optional 'if' member.  Its value must be a
string, or an object with a single member 'all', 'any' or 'not'.

The C code generated for the definition will then be guarded by an #if
preprocessing directive with an operand generated from that condition:

 * STRING will generate defined(STRING)
 * { 'all': [COND, ...] } will generate (COND && ...)
 * { 'any': [COND, ...] } will generate (COND || ...)
 * { 'not': COND } will generate !COND

Example: a conditional struct ::

 { 'struct': 'IfStruct', 'data': { 'foo': 'int' },
   'if': { 'all': [ 'CONFIG_FOO', 'HAVE_BAR' ] } }

gets its generated code guarded like this::

 #if defined(CONFIG_FOO) && defined(HAVE_BAR)
 ... generated code ...
 #endif /* defined(HAVE_BAR) && defined(CONFIG_FOO) */

Individual members of complex types can also be made conditional.
This requires the longhand form of MEMBER.

Example: a struct type with unconditional member 'foo' and conditional
member 'bar' ::

 { 'struct': 'IfStruct',
   'data': { 'foo': 'int',
             'bar': { 'type': 'int', 'if': 'IFCOND'} } }

A union's discriminator may not be conditional.

Likewise, individual enumeration values may be conditional.  This
requires the longhand form of ENUM-VALUE_.

Example: an enum type with unconditional value 'foo' and conditional
value 'bar' ::

 { 'enum': 'IfEnum',
   'data': [ 'foo',
             { 'name' : 'bar', 'if': 'IFCOND' } ] }

Likewise, features can be conditional.  This requires the longhand
form of FEATURE_.

Example: a struct with conditional feature 'allow-negative-numbers' ::

 { 'struct': 'TestType',
   'data': { 'number': 'int' },
   'features': [ { 'name': 'allow-negative-numbers',
                   'if': 'IFCOND' } ] }

Please note that you are responsible to ensure that the C code will
compile with an arbitrary combination of conditions, since the
generator is unable to check it at this point.

The conditions apply to introspection as well, i.e. introspection
shows a conditional entity only when the condition is satisfied in
this particular build.


Documentation comments
----------------------

A multi-line comment that starts and ends with a ``##`` line is a
documentation comment.

If the documentation comment starts like ::

    ##
    # @SYMBOL:

it documents the definition of SYMBOL, else it's free-form
documentation.

See below for more on `Definition documentation`_.

Free-form documentation may be used to provide additional text and
structuring content.


Headings and subheadings
~~~~~~~~~~~~~~~~~~~~~~~~

A free-form documentation comment containing a line which starts with
some ``=`` symbols and then a space defines a section heading::

    ##
    # = This is a top level heading
    #
    # This is a free-form comment which will go under the
    # top level heading.
    ##

    ##
    # == This is a second level heading
    ##

A heading line must be the first line of the documentation
comment block.

Section headings must always be correctly nested, so you can only
define a third-level heading inside a second-level heading, and so on.


Documentation markup
~~~~~~~~~~~~~~~~~~~~

Documentation comments can use most rST markup.  In particular,
a ``::`` literal block can be used for examples::

    # ::
    #
    #   Text of the example, may span
    #   multiple lines

``*`` starts an itemized list::

    # * First item, may span
    #   multiple lines
    # * Second item

You can also use ``-`` instead of ``*``.

A decimal number followed by ``.`` starts a numbered list::

    # 1. First item, may span
    #    multiple lines
    # 2. Second item

The actual number doesn't matter.

Lists of either kind must be preceded and followed by a blank line.
If a list item's text spans multiple lines, then the second and
subsequent lines must be correctly indented to line up with the
first character of the first line.

The usual ****strong****, *\*emphasized\** and ````literal```` markup
should be used.  If you need a single literal ``*``, you will need to
backslash-escape it.

Use ``@foo`` to reference a name in the schema.  This is an rST
extension.  It is rendered the same way as ````foo````, but carries
additional meaning.

Example::

 ##
 # Some text foo with **bold** and *emphasis*
 #
 # 1. with a list
 # 2. like that
 #
 # And some code:
 #
 # ::
 #
 #   $ echo foo
 #   -> do this
 #   <- get that
 ##

For legibility, wrap text paragraphs so every line is at most 70
characters long.

Separate sentences with two spaces.


Definition documentation
~~~~~~~~~~~~~~~~~~~~~~~~

Definition documentation, if present, must immediately precede the
definition it documents.

When documentation is required (see pragma_ 'doc-required'), every
definition must have documentation.

Definition documentation starts with a line naming the definition,
followed by an optional overview, a description of each argument (for
commands and events), member (for structs and unions), branch (for
alternates), or value (for enums), a description of each feature (if
any), and finally optional tagged sections.

Descriptions start with '\@name:'.  The description text must be
indented like this::

 # @name: Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed
 #     do eiusmod tempor incididunt ut labore et dolore magna aliqua.

.. FIXME The parser accepts these things in almost any order.

.. FIXME union branches should be described, too.

Extensions added after the definition was first released carry a
"(since x.y.z)" comment.

The feature descriptions must be preceded by a blank line and then a
line "Features:", like this::

  #
  # Features:
  #
  # @feature: Description text

A tagged section begins with a paragraph that starts with one of the
following words: "Note:"/"Notes:", "Since:", "Example:"/"Examples:",
"Returns:", "Errors:", "TODO:".  It ends with the start of a new
section.

The second and subsequent lines of tagged sections must be indented
like this::

 # Note: Ut enim ad minim veniam, quis nostrud exercitation ullamco
 #     laboris nisi ut aliquip ex ea commodo consequat.
 #
 #     Duis aute irure dolor in reprehenderit in voluptate velit esse
 #     cillum dolore eu fugiat nulla pariatur.

"Returns" and "Errors" sections are only valid for commands.  They
document the success and the error response, respectively.

A "Since: x.y.z" tagged section lists the release that introduced the
definition.

An "Example" or "Examples" section is rendered entirely
as literal fixed-width text.  "TODO" sections are not rendered at all
(they are for developers, not users of QMP).  In other sections, the
text is formatted, and rST markup can be used.

For example::

 ##
 # @BlockStats:
 #
 # Statistics of a virtual block device or a block backing device.
 #
 # @device: If the stats are for a virtual block device, the name
 #     corresponding to the virtual block device.
 #
 # @node-name: The node name of the device.  (Since 2.3)
 #
 # ... more members ...
 #
 # Since: 0.14
 ##
 { 'struct': 'BlockStats',
   'data': {'*device': 'str', '*node-name': 'str',
            ... more members ... } }

 ##
 # @query-blockstats:
 #
 # Query the @BlockStats for all virtual block devices.
 #
 # @query-nodes: If true, the command will query all the block nodes
 #     ... explain, explain ...
 #     (Since 2.3)
 #
 # Returns: A list of @BlockStats for each virtual block devices.
 #
 # Since: 0.14
 #
 # Example:
 #
 #     -> { "execute": "query-blockstats" }
 #     <- {
 #          ... lots of output ...
 #        }
 ##
 { 'command': 'query-blockstats',
   'data': { '*query-nodes': 'bool' },
   'returns': ['BlockStats'] }


Markup pitfalls
~~~~~~~~~~~~~~~

A blank line is required between list items and paragraphs.  Without
it, the list may not be recognized, resulting in garbled output.  Good
example::

 # An event's state is modified if:
 #
 # - its name matches the @name pattern, and
 # - if @vcpu is given, the event has the "vcpu" property.

Without the blank line this would be a single paragraph.

Indentation matters.  Bad example::

 # @none: None (no memory side cache in this proximity domain,
 #              or cache associativity unknown)
 #     (since 5.0)

The last line's de-indent is wrong.  The second and subsequent lines
need to line up with each other, like this::

 # @none: None (no memory side cache in this proximity domain,
 #     or cache associativity unknown)
 #     (since 5.0)

Section tags are case-sensitive and end with a colon.  They are only
recognized after a blank line.  Good example::

 #
 # Since: 7.1

Bad examples (all ordinary paragraphs)::

 # since: 7.1

 # Since 7.1

 # Since : 7.1

Likewise, member descriptions require a colon.  Good example::

 # @interface-id: Interface ID

Bad examples (all ordinary paragraphs)::

 # @interface-id   Interface ID

 # @interface-id : Interface ID

Undocumented members are not flagged, yet.  Instead, the generated
documentation describes them as "Not documented".  Think twice before
adding more undocumented members.

When you change documentation comments, please check the generated
documentation comes out as intended!


Client JSON Protocol introspection
==================================

Clients of a Client JSON Protocol commonly need to figure out what
exactly the server (QEMU) supports.

For this purpose, QMP provides introspection via command
query-qmp-schema.  QGA currently doesn't support introspection.

While Client JSON Protocol wire compatibility should be maintained
between qemu versions, we cannot make the same guarantees for
introspection stability.  For example, one version of qemu may provide
a non-variant optional member of a struct, and a later version rework
the member to instead be non-optional and associated with a variant.
Likewise, one version of qemu may list a member with open-ended type
'str', and a later version could convert it to a finite set of strings
via an enum type; or a member may be converted from a specific type to
an alternate that represents a choice between the original type and
something else.

query-qmp-schema returns a JSON array of SchemaInfo objects.  These
objects together describe the wire ABI, as defined in the QAPI schema.
There is no specified order to the SchemaInfo objects returned; a
client must search for a particular name throughout the entire array
to learn more about that name, but is at least guaranteed that there
will be no collisions between type, command, and event names.

However, the SchemaInfo can't reflect all the rules and restrictions
that apply to QMP.  It's interface introspection (figuring out what's
there), not interface specification.  The specification is in the QAPI
schema.  To understand how QMP is to be used, you need to study the
QAPI schema.

Like any other command, query-qmp-schema is itself defined in the QAPI
schema, along with the SchemaInfo type.  This text attempts to give an
overview how things work.  For details you need to consult the QAPI
schema.

SchemaInfo objects have common members "name", "meta-type",
"features", and additional variant members depending on the value of
meta-type.

Each SchemaInfo object describes a wire ABI entity of a certain
meta-type: a command, event or one of several kinds of type.

SchemaInfo for commands and events have the same name as in the QAPI
schema.

Command and event names are part of the wire ABI, but type names are
not.  Therefore, the SchemaInfo for types have auto-generated
meaningless names.  For readability, the examples in this section use
meaningful type names instead.

Optional member "features" exposes the entity's feature strings as a
JSON array of strings.

To examine a type, start with a command or event using it, then follow
references by name.

QAPI schema definitions not reachable that way are omitted.

The SchemaInfo for a command has meta-type "command", and variant
members "arg-type", "ret-type" and "allow-oob".  On the wire, the
"arguments" member of a client's "execute" command must conform to the
object type named by "arg-type".  The "return" member that the server
passes in a success response conforms to the type named by "ret-type".
When "allow-oob" is true, it means the command supports out-of-band
execution.  It defaults to false.

If the command takes no arguments, "arg-type" names an object type
without members.  Likewise, if the command returns nothing, "ret-type"
names an object type without members.

Example: the SchemaInfo for command query-qmp-schema ::

 { "name": "query-qmp-schema", "meta-type": "command",
   "arg-type": "q_empty", "ret-type": "SchemaInfoList" }

   Type "q_empty" is an automatic object type without members, and type
   "SchemaInfoList" is the array of SchemaInfo type.

The SchemaInfo for an event has meta-type "event", and variant member
"arg-type".  On the wire, a "data" member that the server passes in an
event conforms to the object type named by "arg-type".

If the event carries no additional information, "arg-type" names an
object type without members.  The event may not have a data member on
the wire then.

Each command or event defined with 'data' as MEMBERS object in the
QAPI schema implicitly defines an object type.

Example: the SchemaInfo for EVENT_C from section Events_ ::

    { "name": "EVENT_C", "meta-type": "event",
      "arg-type": "q_obj-EVENT_C-arg" }

    Type "q_obj-EVENT_C-arg" is an implicitly defined object type with
    the two members from the event's definition.

The SchemaInfo for struct and union types has meta-type "object" and
variant member "members".

The SchemaInfo for a union type additionally has variant members "tag"
and "variants".

"members" is a JSON array describing the object's common members, if
any.  Each element is a JSON object with members "name" (the member's
name), "type" (the name of its type), "features" (a JSON array of
feature strings), and "default".  The latter two are optional.  The
member is optional if "default" is present.  Currently, "default" can
only have value null.  Other values are reserved for future
extensions.  The "members" array is in no particular order; clients
must search the entire object when learning whether a particular
member is supported.

Example: the SchemaInfo for MyType from section `Struct types`_ ::

    { "name": "MyType", "meta-type": "object",
      "members": [
          { "name": "member1", "type": "str" },
          { "name": "member2", "type": "int" },
          { "name": "member3", "type": "str", "default": null } ] }

"features" exposes the command's feature strings as a JSON array of
strings.

Example: the SchemaInfo for TestType from section Features_::

    { "name": "TestType", "meta-type": "object",
      "members": [
          { "name": "number", "type": "int" } ],
      "features": ["allow-negative-numbers"] }

"tag" is the name of the common member serving as type tag.
"variants" is a JSON array describing the object's variant members.
Each element is a JSON object with members "case" (the value of type
tag this element applies to) and "type" (the name of an object type
that provides the variant members for this type tag value).  The
"variants" array is in no particular order, and is not guaranteed to
list cases in the same order as the corresponding "tag" enum type.

Example: the SchemaInfo for union BlockdevOptions from section
`Union types`_ ::

    { "name": "BlockdevOptions", "meta-type": "object",
      "members": [
          { "name": "driver", "type": "BlockdevDriver" },
          { "name": "read-only", "type": "bool", "default": null } ],
      "tag": "driver",
      "variants": [
          { "case": "file", "type": "BlockdevOptionsFile" },
          { "case": "qcow2", "type": "BlockdevOptionsQcow2" } ] }

Note that base types are "flattened": its members are included in the
"members" array.

The SchemaInfo for an alternate type has meta-type "alternate", and
variant member "members".  "members" is a JSON array.  Each element is
a JSON object with member "type", which names a type.  Values of the
alternate type conform to exactly one of its member types.  There is
no guarantee on the order in which "members" will be listed.

Example: the SchemaInfo for BlockdevRef from section `Alternate types`_ ::

    { "name": "BlockdevRef", "meta-type": "alternate",
      "members": [
          { "type": "BlockdevOptions" },
          { "type": "str" } ] }

The SchemaInfo for an array type has meta-type "array", and variant
member "element-type", which names the array's element type.  Array
types are implicitly defined.  For convenience, the array's name may
resemble the element type; however, clients should examine member
"element-type" instead of making assumptions based on parsing member
"name".

Example: the SchemaInfo for ['str'] ::

    { "name": "[str]", "meta-type": "array",
      "element-type": "str" }

The SchemaInfo for an enumeration type has meta-type "enum" and
variant member "members".

"members" is a JSON array describing the enumeration values.  Each
element is a JSON object with member "name" (the member's name), and
optionally "features" (a JSON array of feature strings).  The
"members" array is in no particular order; clients must search the
entire array when learning whether a particular value is supported.

Example: the SchemaInfo for MyEnum from section `Enumeration types`_ ::

    { "name": "MyEnum", "meta-type": "enum",
      "members": [
        { "name": "value1" },
        { "name": "value2" },
        { "name": "value3" }
      ] }

The SchemaInfo for a built-in type has the same name as the type in
the QAPI schema (see section `Built-in Types`_), with one exception
detailed below.  It has variant member "json-type" that shows how
values of this type are encoded on the wire.

Example: the SchemaInfo for str ::

    { "name": "str", "meta-type": "builtin", "json-type": "string" }

The QAPI schema supports a number of integer types that only differ in
how they map to C.  They are identical as far as SchemaInfo is
concerned.  Therefore, they get all mapped to a single type "int" in
SchemaInfo.

As explained above, type names are not part of the wire ABI.  Not even
the names of built-in types.  Clients should examine member
"json-type" instead of hard-coding names of built-in types.


Compatibility considerations
============================

Maintaining backward compatibility at the Client JSON Protocol level
while evolving the schema requires some care.  This section is about
syntactic compatibility, which is necessary, but not sufficient, for
actual compatibility.

Clients send commands with argument data, and receive command
responses with return data and events with event data.

Adding opt-in functionality to the send direction is backwards
compatible: adding commands, optional arguments, enumeration values,
union and alternate branches; turning an argument type into an
alternate of that type; making mandatory arguments optional.  Clients
oblivious of the new functionality continue to work.

Incompatible changes include removing commands, command arguments,
enumeration values, union and alternate branches, adding mandatory
command arguments, and making optional arguments mandatory.

The specified behavior of an absent optional argument should remain
the same.  With proper documentation, this policy still allows some
flexibility; for example, when an optional 'buffer-size' argument is
specified to default to a sensible buffer size, the actual default
value can still be changed.  The specified default behavior is not the
exact size of the buffer, only that the default size is sensible.

Adding functionality to the receive direction is generally backwards
compatible: adding events, adding return and event data members.
Clients are expected to ignore the ones they don't know.

Removing "unreachable" stuff like events that can't be triggered
anymore, optional return or event data members that can't be sent
anymore, and return or event data member (enumeration) values that
can't be sent anymore makes no difference to clients, except for
introspection.  The latter can conceivably confuse clients, so tread
carefully.

Incompatible changes include removing return and event data members.

Any change to a command definition's 'data' or one of the types used
there (recursively) needs to consider send direction compatibility.

Any change to a command definition's 'return', an event definition's
'data', or one of the types used there (recursively) needs to consider
receive direction compatibility.

Any change to types used in both contexts need to consider both.

Enumeration type values and complex and alternate type members may be
reordered freely.  For enumerations and alternate types, this doesn't
affect the wire encoding.  For complex types, this might make the
implementation emit JSON object members in a different order, which
the Client JSON Protocol permits.

Since type names are not visible in the Client JSON Protocol, types
may be freely renamed.  Even certain refactorings are invisible, such
as splitting members from one type into a common base type.


Code generation
===============

The QAPI code generator qapi-gen.py generates code and documentation
from the schema.  Together with the core QAPI libraries, this code
provides everything required to take JSON commands read in by a Client
JSON Protocol server, unmarshal the arguments into the underlying C
types, call into the corresponding C function, map the response back
to a Client JSON Protocol response to be returned to the user, and
introspect the commands.

As an example, we'll use the following schema, which describes a
single complex user-defined type, along with command which takes a
list of that type as a parameter, and returns a single element of that
type.  The user is responsible for writing the implementation of
qmp_my_command(); everything else is produced by the generator. ::

    $ cat example-schema.json
    { 'struct': 'UserDefOne',
      'data': { 'integer': 'int', '*string': 'str', '*flag': 'bool' } }

    { 'command': 'my-command',
      'data': { 'arg1': ['UserDefOne'] },
      'returns': 'UserDefOne' }

    { 'event': 'MY_EVENT' }

We run qapi-gen.py like this::

    $ python scripts/qapi-gen.py --output-dir="qapi-generated" \
    --prefix="example-" example-schema.json

For a more thorough look at generated code, the testsuite includes
tests/qapi-schema/qapi-schema-tests.json that covers more examples of
what the generator will accept, and compiles the resulting C code as
part of 'make check-unit'.


Code generated for QAPI types
-----------------------------

The following files are created:

 ``$(prefix)qapi-types.h``
     C types corresponding to types defined in the schema

 ``$(prefix)qapi-types.c``
     Cleanup functions for the above C types

The $(prefix) is an optional parameter used as a namespace to keep the
generated code from one schema/code-generation separated from others so code
can be generated/used from multiple schemas without clobbering previously
created code.

Example::

    $ cat qapi-generated/example-qapi-types.h
    [Uninteresting stuff omitted...]

    #ifndef EXAMPLE_QAPI_TYPES_H
    #define EXAMPLE_QAPI_TYPES_H

    #include "qapi/qapi-builtin-types.h"

    typedef struct UserDefOne UserDefOne;

    typedef struct UserDefOneList UserDefOneList;

    typedef struct q_obj_my_command_arg q_obj_my_command_arg;

    struct UserDefOne {
        int64_t integer;
        char *string;
        bool has_flag;
        bool flag;
    };

    void qapi_free_UserDefOne(UserDefOne *obj);
    G_DEFINE_AUTOPTR_CLEANUP_FUNC(UserDefOne, qapi_free_UserDefOne)

    struct UserDefOneList {
        UserDefOneList *next;
        UserDefOne *value;
    };

    void qapi_free_UserDefOneList(UserDefOneList *obj);
    G_DEFINE_AUTOPTR_CLEANUP_FUNC(UserDefOneList, qapi_free_UserDefOneList)

    struct q_obj_my_command_arg {
        UserDefOneList *arg1;
    };

    #endif /* EXAMPLE_QAPI_TYPES_H */
    $ cat qapi-generated/example-qapi-types.c
    [Uninteresting stuff omitted...]

    void qapi_free_UserDefOne(UserDefOne *obj)
    {
        Visitor *v;

        if (!obj) {
            return;
        }

        v = qapi_dealloc_visitor_new();
        visit_type_UserDefOne(v, NULL, &obj, NULL);
        visit_free(v);
    }

    void qapi_free_UserDefOneList(UserDefOneList *obj)
    {
        Visitor *v;

        if (!obj) {
            return;
        }

        v = qapi_dealloc_visitor_new();
        visit_type_UserDefOneList(v, NULL, &obj, NULL);
        visit_free(v);
    }

    [Uninteresting stuff omitted...]

For a modular QAPI schema (see section `Include directives`_), code for
each sub-module SUBDIR/SUBMODULE.json is actually generated into ::

 SUBDIR/$(prefix)qapi-types-SUBMODULE.h
 SUBDIR/$(prefix)qapi-types-SUBMODULE.c

If qapi-gen.py is run with option --builtins, additional files are
created:

 ``qapi-builtin-types.h``
     C types corresponding to built-in types

 ``qapi-builtin-types.c``
     Cleanup functions for the above C types


Code generated for visiting QAPI types
--------------------------------------

These are the visitor functions used to walk through and convert
between a native QAPI C data structure and some other format (such as
QObject); the generated functions are named visit_type_FOO() and
visit_type_FOO_members().

The following files are generated:

 ``$(prefix)qapi-visit.c``
     Visitor function for a particular C type, used to automagically
     convert QObjects into the corresponding C type and vice-versa, as
     well as for deallocating memory for an existing C type

 ``$(prefix)qapi-visit.h``
     Declarations for previously mentioned visitor functions

Example::

    $ cat qapi-generated/example-qapi-visit.h
    [Uninteresting stuff omitted...]

    #ifndef EXAMPLE_QAPI_VISIT_H
    #define EXAMPLE_QAPI_VISIT_H

    #include "qapi/qapi-builtin-visit.h"
    #include "example-qapi-types.h"


    bool visit_type_UserDefOne_members(Visitor *v, UserDefOne *obj, Error **errp);

    bool visit_type_UserDefOne(Visitor *v, const char *name,
                     UserDefOne **obj, Error **errp);

    bool visit_type_UserDefOneList(Visitor *v, const char *name,
                     UserDefOneList **obj, Error **errp);

    bool visit_type_q_obj_my_command_arg_members(Visitor *v, q_obj_my_command_arg *obj, Error **errp);

    #endif /* EXAMPLE_QAPI_VISIT_H */
    $ cat qapi-generated/example-qapi-visit.c
    [Uninteresting stuff omitted...]

    bool visit_type_UserDefOne_members(Visitor *v, UserDefOne *obj, Error **errp)
    {
        bool has_string = !!obj->string;

        if (!visit_type_int(v, "integer", &obj->integer, errp)) {
            return false;
        }
        if (visit_optional(v, "string", &has_string)) {
            if (!visit_type_str(v, "string", &obj->string, errp)) {
                return false;
            }
        }
        if (visit_optional(v, "flag", &obj->has_flag)) {
            if (!visit_type_bool(v, "flag", &obj->flag, errp)) {
                return false;
            }
        }
        return true;
    }

    bool visit_type_UserDefOne(Visitor *v, const char *name,
                     UserDefOne **obj, Error **errp)
    {
        bool ok = false;

        if (!visit_start_struct(v, name, (void **)obj, sizeof(UserDefOne), errp)) {
            return false;
        }
        if (!*obj) {
            /* incomplete */
            assert(visit_is_dealloc(v));
            ok = true;
            goto out_obj;
        }
        if (!visit_type_UserDefOne_members(v, *obj, errp)) {
            goto out_obj;
        }
        ok = visit_check_struct(v, errp);
    out_obj:
        visit_end_struct(v, (void **)obj);
        if (!ok && visit_is_input(v)) {
            qapi_free_UserDefOne(*obj);
            *obj = NULL;
        }
        return ok;
    }

    bool visit_type_UserDefOneList(Visitor *v, const char *name,
                     UserDefOneList **obj, Error **errp)
    {
        bool ok = false;
        UserDefOneList *tail;
        size_t size = sizeof(**obj);

        if (!visit_start_list(v, name, (GenericList **)obj, size, errp)) {
            return false;
        }

        for (tail = *obj; tail;
             tail = (UserDefOneList *)visit_next_list(v, (GenericList *)tail, size)) {
            if (!visit_type_UserDefOne(v, NULL, &tail->value, errp)) {
                goto out_obj;
            }
        }

        ok = visit_check_list(v, errp);
    out_obj:
        visit_end_list(v, (void **)obj);
        if (!ok && visit_is_input(v)) {
            qapi_free_UserDefOneList(*obj);
            *obj = NULL;
        }
        return ok;
    }

    bool visit_type_q_obj_my_command_arg_members(Visitor *v, q_obj_my_command_arg *obj, Error **errp)
    {
        if (!visit_type_UserDefOneList(v, "arg1", &obj->arg1, errp)) {
            return false;
        }
        return true;
    }

    [Uninteresting stuff omitted...]

For a modular QAPI schema (see section `Include directives`_), code for
each sub-module SUBDIR/SUBMODULE.json is actually generated into ::

 SUBDIR/$(prefix)qapi-visit-SUBMODULE.h
 SUBDIR/$(prefix)qapi-visit-SUBMODULE.c

If qapi-gen.py is run with option --builtins, additional files are
created:

 ``qapi-builtin-visit.h``
     Visitor functions for built-in types

 ``qapi-builtin-visit.c``
     Declarations for these visitor functions


Code generated for commands
---------------------------

These are the marshaling/dispatch functions for the commands defined
in the schema.  The generated code provides qmp_marshal_COMMAND(), and
declares qmp_COMMAND() that the user must implement.

The following files are generated:

 ``$(prefix)qapi-commands.c``
     Command marshal/dispatch functions for each QMP command defined in
     the schema

 ``$(prefix)qapi-commands.h``
     Function prototypes for the QMP commands specified in the schema

 ``$(prefix)qapi-commands.trace-events``
     Trace event declarations, see :ref:`tracing`.

 ``$(prefix)qapi-init-commands.h``
     Command initialization prototype

 ``$(prefix)qapi-init-commands.c``
     Command initialization code

Example::

    $ cat qapi-generated/example-qapi-commands.h
    [Uninteresting stuff omitted...]

    #ifndef EXAMPLE_QAPI_COMMANDS_H
    #define EXAMPLE_QAPI_COMMANDS_H

    #include "example-qapi-types.h"

    UserDefOne *qmp_my_command(UserDefOneList *arg1, Error **errp);
    void qmp_marshal_my_command(QDict *args, QObject **ret, Error **errp);

    #endif /* EXAMPLE_QAPI_COMMANDS_H */

    $ cat qapi-generated/example-qapi-commands.trace-events
    # AUTOMATICALLY GENERATED, DO NOT MODIFY

    qmp_enter_my_command(const char *json) "%s"
    qmp_exit_my_command(const char *result, bool succeeded) "%s %d"

    $ cat qapi-generated/example-qapi-commands.c
    [Uninteresting stuff omitted...]

    static void qmp_marshal_output_UserDefOne(UserDefOne *ret_in,
                                    QObject **ret_out, Error **errp)
    {
        Visitor *v;

        v = qobject_output_visitor_new_qmp(ret_out);
        if (visit_type_UserDefOne(v, "unused", &ret_in, errp)) {
            visit_complete(v, ret_out);
        }
        visit_free(v);
        v = qapi_dealloc_visitor_new();
        visit_type_UserDefOne(v, "unused", &ret_in, NULL);
        visit_free(v);
    }

    void qmp_marshal_my_command(QDict *args, QObject **ret, Error **errp)
    {
        Error *err = NULL;
        bool ok = false;
        Visitor *v;
        UserDefOne *retval;
        q_obj_my_command_arg arg = {0};

        v = qobject_input_visitor_new_qmp(QOBJECT(args));
        if (!visit_start_struct(v, NULL, NULL, 0, errp)) {
            goto out;
        }
        if (visit_type_q_obj_my_command_arg_members(v, &arg, errp)) {
            ok = visit_check_struct(v, errp);
        }
        visit_end_struct(v, NULL);
        if (!ok) {
            goto out;
        }

        if (trace_event_get_state_backends(TRACE_QMP_ENTER_MY_COMMAND)) {
            g_autoptr(GString) req_json = qobject_to_json(QOBJECT(args));

            trace_qmp_enter_my_command(req_json->str);
        }

        retval = qmp_my_command(arg.arg1, &err);
        if (err) {
            trace_qmp_exit_my_command(error_get_pretty(err), false);
            error_propagate(errp, err);
            goto out;
        }

        qmp_marshal_output_UserDefOne(retval, ret, errp);

        if (trace_event_get_state_backends(TRACE_QMP_EXIT_MY_COMMAND)) {
            g_autoptr(GString) ret_json = qobject_to_json(*ret);

            trace_qmp_exit_my_command(ret_json->str, true);
        }

    out:
        visit_free(v);
        v = qapi_dealloc_visitor_new();
        visit_start_struct(v, NULL, NULL, 0, NULL);
        visit_type_q_obj_my_command_arg_members(v, &arg, NULL);
        visit_end_struct(v, NULL);
        visit_free(v);
    }

    [Uninteresting stuff omitted...]
    $ cat qapi-generated/example-qapi-init-commands.h
    [Uninteresting stuff omitted...]
    #ifndef EXAMPLE_QAPI_INIT_COMMANDS_H
    #define EXAMPLE_QAPI_INIT_COMMANDS_H

    #include "qapi/qmp/dispatch.h"

    void example_qmp_init_marshal(QmpCommandList *cmds);

    #endif /* EXAMPLE_QAPI_INIT_COMMANDS_H */
    $ cat qapi-generated/example-qapi-init-commands.c
    [Uninteresting stuff omitted...]
    void example_qmp_init_marshal(QmpCommandList *cmds)
    {
        QTAILQ_INIT(cmds);

        qmp_register_command(cmds, "my-command",
                             qmp_marshal_my_command, 0, 0);
    }
    [Uninteresting stuff omitted...]

For a modular QAPI schema (see section `Include directives`_), code for
each sub-module SUBDIR/SUBMODULE.json is actually generated into::

 SUBDIR/$(prefix)qapi-commands-SUBMODULE.h
 SUBDIR/$(prefix)qapi-commands-SUBMODULE.c


Code generated for events
-------------------------

This is the code related to events defined in the schema, providing
qapi_event_send_EVENT().

The following files are created:

 ``$(prefix)qapi-events.h``
     Function prototypes for each event type

 ``$(prefix)qapi-events.c``
     Implementation of functions to send an event

 ``$(prefix)qapi-emit-events.h``
     Enumeration of all event names, and common event code declarations

 ``$(prefix)qapi-emit-events.c``
     Common event code definitions

Example::

    $ cat qapi-generated/example-qapi-events.h
    [Uninteresting stuff omitted...]

    #ifndef EXAMPLE_QAPI_EVENTS_H
    #define EXAMPLE_QAPI_EVENTS_H

    #include "qapi/util.h"
    #include "example-qapi-types.h"

    void qapi_event_send_my_event(void);

    #endif /* EXAMPLE_QAPI_EVENTS_H */
    $ cat qapi-generated/example-qapi-events.c
    [Uninteresting stuff omitted...]

    void qapi_event_send_my_event(void)
    {
        QDict *qmp;

        qmp = qmp_event_build_dict("MY_EVENT");

        example_qapi_event_emit(EXAMPLE_QAPI_EVENT_MY_EVENT, qmp);

        qobject_unref(qmp);
    }

    [Uninteresting stuff omitted...]
    $ cat qapi-generated/example-qapi-emit-events.h
    [Uninteresting stuff omitted...]

    #ifndef EXAMPLE_QAPI_EMIT_EVENTS_H
    #define EXAMPLE_QAPI_EMIT_EVENTS_H

    #include "qapi/util.h"

    typedef enum example_QAPIEvent {
        EXAMPLE_QAPI_EVENT_MY_EVENT,
        EXAMPLE_QAPI_EVENT__MAX,
    } example_QAPIEvent;

    #define example_QAPIEvent_str(val) \
        qapi_enum_lookup(&example_QAPIEvent_lookup, (val))

    extern const QEnumLookup example_QAPIEvent_lookup;

    void example_qapi_event_emit(example_QAPIEvent event, QDict *qdict);

    #endif /* EXAMPLE_QAPI_EMIT_EVENTS_H */
    $ cat qapi-generated/example-qapi-emit-events.c
    [Uninteresting stuff omitted...]

    const QEnumLookup example_QAPIEvent_lookup = {
        .array = (const char *const[]) {
            [EXAMPLE_QAPI_EVENT_MY_EVENT] = "MY_EVENT",
        },
        .size = EXAMPLE_QAPI_EVENT__MAX
    };

    [Uninteresting stuff omitted...]

For a modular QAPI schema (see section `Include directives`_), code for
each sub-module SUBDIR/SUBMODULE.json is actually generated into ::

 SUBDIR/$(prefix)qapi-events-SUBMODULE.h
 SUBDIR/$(prefix)qapi-events-SUBMODULE.c


Code generated for introspection
--------------------------------

The following files are created:

 ``$(prefix)qapi-introspect.c``
     Defines a string holding a JSON description of the schema

 ``$(prefix)qapi-introspect.h``
     Declares the above string

Example::

    $ cat qapi-generated/example-qapi-introspect.h
    [Uninteresting stuff omitted...]

    #ifndef EXAMPLE_QAPI_INTROSPECT_H
    #define EXAMPLE_QAPI_INTROSPECT_H

    #include "qapi/qmp/qlit.h"

    extern const QLitObject example_qmp_schema_qlit;

    #endif /* EXAMPLE_QAPI_INTROSPECT_H */
    $ cat qapi-generated/example-qapi-introspect.c
    [Uninteresting stuff omitted...]

    const QLitObject example_qmp_schema_qlit = QLIT_QLIST(((QLitObject[]) {
        QLIT_QDICT(((QLitDictEntry[]) {
            { "arg-type", QLIT_QSTR("0"), },
            { "meta-type", QLIT_QSTR("command"), },
            { "name", QLIT_QSTR("my-command"), },
            { "ret-type", QLIT_QSTR("1"), },
            {}
        })),
        QLIT_QDICT(((QLitDictEntry[]) {
            { "arg-type", QLIT_QSTR("2"), },
            { "meta-type", QLIT_QSTR("event"), },
            { "name", QLIT_QSTR("MY_EVENT"), },
            {}
        })),
        /* "0" = q_obj_my-command-arg */
        QLIT_QDICT(((QLitDictEntry[]) {
            { "members", QLIT_QLIST(((QLitObject[]) {
                QLIT_QDICT(((QLitDictEntry[]) {
                    { "name", QLIT_QSTR("arg1"), },
                    { "type", QLIT_QSTR("[1]"), },
                    {}
                })),
                {}
            })), },
            { "meta-type", QLIT_QSTR("object"), },
            { "name", QLIT_QSTR("0"), },
            {}
        })),
        /* "1" = UserDefOne */
        QLIT_QDICT(((QLitDictEntry[]) {
            { "members", QLIT_QLIST(((QLitObject[]) {
                QLIT_QDICT(((QLitDictEntry[]) {
                    { "name", QLIT_QSTR("integer"), },
                    { "type", QLIT_QSTR("int"), },
                    {}
                })),
                QLIT_QDICT(((QLitDictEntry[]) {
                    { "default", QLIT_QNULL, },
                    { "name", QLIT_QSTR("string"), },
                    { "type", QLIT_QSTR("str"), },
                    {}
                })),
                QLIT_QDICT(((QLitDictEntry[]) {
                    { "default", QLIT_QNULL, },
                    { "name", QLIT_QSTR("flag"), },
                    { "type", QLIT_QSTR("bool"), },
                    {}
                })),
                {}
            })), },
            { "meta-type", QLIT_QSTR("object"), },
            { "name", QLIT_QSTR("1"), },
            {}
        })),
        /* "2" = q_empty */
        QLIT_QDICT(((QLitDictEntry[]) {
            { "members", QLIT_QLIST(((QLitObject[]) {
                {}
            })), },
            { "meta-type", QLIT_QSTR("object"), },
            { "name", QLIT_QSTR("2"), },
            {}
        })),
        QLIT_QDICT(((QLitDictEntry[]) {
            { "element-type", QLIT_QSTR("1"), },
            { "meta-type", QLIT_QSTR("array"), },
            { "name", QLIT_QSTR("[1]"), },
            {}
        })),
        QLIT_QDICT(((QLitDictEntry[]) {
            { "json-type", QLIT_QSTR("int"), },
            { "meta-type", QLIT_QSTR("builtin"), },
            { "name", QLIT_QSTR("int"), },
            {}
        })),
        QLIT_QDICT(((QLitDictEntry[]) {
            { "json-type", QLIT_QSTR("string"), },
            { "meta-type", QLIT_QSTR("builtin"), },
            { "name", QLIT_QSTR("str"), },
            {}
        })),
        QLIT_QDICT(((QLitDictEntry[]) {
            { "json-type", QLIT_QSTR("boolean"), },
            { "meta-type", QLIT_QSTR("builtin"), },
            { "name", QLIT_QSTR("bool"), },
            {}
        })),
        {}
    }));

    [Uninteresting stuff omitted...]
