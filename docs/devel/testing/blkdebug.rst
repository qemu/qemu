Block I/O error injection using ``blkdebug``
============================================

..
   Copyright (C) 2014-2015 Red Hat Inc

   This work is licensed under the terms of the GNU GPL, version 2 or later.  See
   the COPYING file in the top-level directory.

The ``blkdebug`` block driver is a rule-based error injection engine.  It can be
used to exercise error code paths in block drivers including ``ENOSPC`` (out of
space) and ``EIO``.

This document gives an overview of the features available in ``blkdebug``.

Background
----------
Block drivers have many error code paths that handle I/O errors.  Image formats
are especially complex since metadata I/O errors during cluster allocation or
while updating tables happen halfway through request processing and require
discipline to keep image files consistent.

Error injection allows test cases to trigger I/O errors at specific points.
This way, all error paths can be tested to make sure they are correct.

Rules
-----
The ``blkdebug`` block driver takes a list of "rules" that tell the error injection
engine when to fail an I/O request.

Each I/O request is evaluated against the rules.  If a rule matches the request
then its "action" is executed.

Rules can be placed in a configuration file; the configuration file
follows the same .ini-like format used by QEMU's ``-readconfig`` option, and
each section of the file represents a rule.

The following configuration file defines a single rule::

  $ cat blkdebug.conf
  [inject-error]
  event = "read_aio"
  errno = "28"

This rule fails all aio read requests with ``ENOSPC`` (28).  Note that the errno
value depends on the host.  On Linux, see
``/usr/include/asm-generic/errno-base.h`` for errno values.

Invoke QEMU as follows::

  $ qemu-system-x86_64
        -drive if=none,cache=none,file=blkdebug:blkdebug.conf:test.img,id=drive0 \
        -device virtio-blk-pci,drive=drive0,id=virtio-blk-pci0

Rules support the following attributes:

``event``
  which type of operation to match (e.g. ``read_aio``, ``write_aio``,
  ``flush_to_os``, ``flush_to_disk``).  See `Events`_ for
  information on events.

``state``
  (optional) the engine must be in this state number in order for this
  rule to match.  See `State transitions`_ for information
  on states.

``errno``
  the numeric errno value to return when a request matches this rule.
  The errno values depend on the host since the numeric values are not
  standardized in the POSIX specification.

``sector``
  (optional) a sector number that the request must overlap in order to
  match this rule

``once``
  (optional, default ``off``) only execute this action on the first
  matching request

``immediately``
  (optional, default ``off``) return a NULL ``BlockAIOCB``
  pointer and fail without an errno instead.  This
  exercises the code path where ``BlockAIOCB`` fails and the
  caller's ``BlockCompletionFunc`` is not invoked.

Events
------
Block drivers provide information about the type of I/O request they are about
to make so rules can match specific types of requests.  For example, the ``qcow2``
block driver tells ``blkdebug`` when it accesses the L1 table so rules can match
only L1 table accesses and not other metadata or guest data requests.

The core events are:

``read_aio``
  guest data read

``write_aio``
  guest data write

``flush_to_os``
  write out unwritten block driver state (e.g. cached metadata)

``flush_to_disk``
  flush the host block device's disk cache

See ``qapi/block-core.json:BlkdebugEvent`` for the full list of events.
You may need to grep block driver source code to understand the
meaning of specific events.

State transitions
-----------------
There are cases where more power is needed to match a particular I/O request in
a longer sequence of requests.  For example::

  write_aio
  flush_to_disk
  write_aio

How do we match the 2nd ``write_aio`` but not the first?  This is where state
transitions come in.

The error injection engine has an integer called the "state" that always starts
initialized to 1.  The state integer is internal to ``blkdebug`` and cannot be
observed from outside but rules can interact with it for powerful matching
behavior.

Rules can be conditional on the current state and they can transition to a new
state.

When a rule's "state" attribute is non-zero then the current state must equal
the attribute in order for the rule to match.

For example, to match the 2nd write_aio::

  [set-state]
  event = "write_aio"
  state = "1"
  new_state = "2"

  [inject-error]
  event = "write_aio"
  state = "2"
  errno = "5"

The first ``write_aio`` request matches the ``set-state`` rule and transitions from
state 1 to state 2.  Once state 2 has been entered, the ``set-state`` rule no
longer matches since it requires state 1.  But the ``inject-error`` rule now
matches the next ``write_aio`` request and injects ``EIO`` (5).

State transition rules support the following attributes:

``event``
  which type of operation to match (e.g. ``read_aio``, ``write_aio``,
  ``flush_to_os`, ``flush_to_disk``).  See `Events`_ for
  information on events.

``state``
  (optional) the engine must be in this state number in order for this
  rule to match

``new_state``
  transition to this state number

Suspend and resume
------------------
Exercising code paths in block drivers may require specific ordering amongst
concurrent requests.  The "breakpoint" feature allows requests to be halted on
a ``blkdebug`` event and resumed later.  This makes it possible to achieve
deterministic ordering when multiple requests are in flight.

Breakpoints on ``blkdebug`` events are associated with a user-defined ``tag`` string.
This tag serves as an identifier by which the request can be resumed at a later
point.

See the ``qemu-io(1)`` ``break``, ``resume``, ``remove_break``, and ``wait_break``
commands for details.
