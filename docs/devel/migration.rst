=========
Migration
=========

QEMU has code to load/save the state of the guest that it is running.
These are two complementary operations.  Saving the state just does
that, saves the state for each device that the guest is running.
Restoring a guest is just the opposite operation: we need to load the
state of each device.

For this to work, QEMU has to be launched with the same arguments the
two times.  I.e. it can only restore the state in one guest that has
the same devices that the one it was saved (this last requirement can
be relaxed a bit, but for now we can consider that configuration has
to be exactly the same).

Once that we are able to save/restore a guest, a new functionality is
requested: migration.  This means that QEMU is able to start in one
machine and being "migrated" to another machine.  I.e. being moved to
another machine.

Next was the "live migration" functionality.  This is important
because some guests run with a lot of state (specially RAM), and it
can take a while to move all state from one machine to another.  Live
migration allows the guest to continue running while the state is
transferred.  Only while the last part of the state is transferred has
the guest to be stopped.  Typically the time that the guest is
unresponsive during live migration is the low hundred of milliseconds
(notice that this depends on a lot of things).

Transports
==========

The migration stream is normally just a byte stream that can be passed
over any transport.

- tcp migration: do the migration using tcp sockets
- unix migration: do the migration using unix sockets
- exec migration: do the migration using the stdin/stdout through a process.
- fd migration: do the migration using a file descriptor that is
  passed to QEMU.  QEMU doesn't care how this file descriptor is opened.

In addition, support is included for migration using RDMA, which
transports the page data using ``RDMA``, where the hardware takes care of
transporting the pages, and the load on the CPU is much lower.  While the
internals of RDMA migration are a bit different, this isn't really visible
outside the RAM migration code.

All these migration protocols use the same infrastructure to
save/restore state devices.  This infrastructure is shared with the
savevm/loadvm functionality.

Common infrastructure
=====================

The files, sockets or fd's that carry the migration stream are abstracted by
the  ``QEMUFile`` type (see `migration/qemu-file.h`).  In most cases this
is connected to a subtype of ``QIOChannel`` (see `io/`).


Saving the state of one device
==============================

For most devices, the state is saved in a single call to the migration
infrastructure; these are *non-iterative* devices.  The data for these
devices is sent at the end of precopy migration, when the CPUs are paused.
There are also *iterative* devices, which contain a very large amount of
data (e.g. RAM or large tables).  See the iterative device section below.

General advice for device developers
------------------------------------

- The migration state saved should reflect the device being modelled rather
  than the way your implementation works.  That way if you change the implementation
  later the migration stream will stay compatible.  That model may include
  internal state that's not directly visible in a register.

- When saving a migration stream the device code may walk and check
  the state of the device.  These checks might fail in various ways (e.g.
  discovering internal state is corrupt or that the guest has done something bad).
  Consider carefully before asserting/aborting at this point, since the
  normal response from users is that *migration broke their VM* since it had
  apparently been running fine until then.  In these error cases, the device
  should log a message indicating the cause of error, and should consider
  putting the device into an error state, allowing the rest of the VM to
  continue execution.

- The migration might happen at an inconvenient point,
  e.g. right in the middle of the guest reprogramming the device, during
  guest reboot or shutdown or while the device is waiting for external IO.
  It's strongly preferred that migrations do not fail in this situation,
  since in the cloud environment migrations might happen automatically to
  VMs that the administrator doesn't directly control.

- If you do need to fail a migration, ensure that sufficient information
  is logged to identify what went wrong.

- The destination should treat an incoming migration stream as hostile
  (which we do to varying degrees in the existing code).  Check that offsets
  into buffers and the like can't cause overruns.  Fail the incoming migration
  in the case of a corrupted stream like this.

- Take care with internal device state or behaviour that might become
  migration version dependent.  For example, the order of PCI capabilities
  is required to stay constant across migration.  Another example would
  be that a special case handled by subsections (see below) might become
  much more common if a default behaviour is changed.

- The state of the source should not be changed or destroyed by the
  outgoing migration.  Migrations timing out or being failed by
  higher levels of management, or failures of the destination host are
  not unusual, and in that case the VM is restarted on the source.
  Note that the management layer can validly revert the migration
  even though the QEMU level of migration has succeeded as long as it
  does it before starting execution on the destination.

- Buses and devices should be able to explicitly specify addresses when
  instantiated, and management tools should use those.  For example,
  when hot adding USB devices it's important to specify the ports
  and addresses, since implicit ordering based on the command line order
  may be different on the destination.  This can result in the
  device state being loaded into the wrong device.

VMState
-------

Most device data can be described using the ``VMSTATE`` macros (mostly defined
in ``include/migration/vmstate.h``).

An example (from hw/input/pckbd.c)

.. code:: c

  static const VMStateDescription vmstate_kbd = {
      .name = "pckbd",
      .version_id = 3,
      .minimum_version_id = 3,
      .fields = (VMStateField[]) {
          VMSTATE_UINT8(write_cmd, KBDState),
          VMSTATE_UINT8(status, KBDState),
          VMSTATE_UINT8(mode, KBDState),
          VMSTATE_UINT8(pending, KBDState),
          VMSTATE_END_OF_LIST()
      }
  };

We are declaring the state with name "pckbd".
The `version_id` is 3, and the fields are 4 uint8_t in a KBDState structure.
We registered this with:

.. code:: c

    vmstate_register(NULL, 0, &vmstate_kbd, s);

For devices that are `qdev` based, we can register the device in the class
init function:

.. code:: c

    dc->vmsd = &vmstate_kbd_isa;

The VMState macros take care of ensuring that the device data section
is formatted portably (normally big endian) and make some compile time checks
against the types of the fields in the structures.

VMState macros can include other VMStateDescriptions to store substructures
(see ``VMSTATE_STRUCT_``), arrays (``VMSTATE_ARRAY_``) and variable length
arrays (``VMSTATE_VARRAY_``).  Various other macros exist for special
cases.

Note that the format on the wire is still very raw; i.e. a VMSTATE_UINT32
ends up with a 4 byte bigendian representation on the wire; in the future
it might be possible to use a more structured format.

Legacy way
----------

This way is going to disappear as soon as all current users are ported to VMSTATE;
although converting existing code can be tricky, and thus 'soon' is relative.

Each device has to register two functions, one to save the state and
another to load the state back.

.. code:: c

  int register_savevm_live(const char *idstr,
                           int instance_id,
                           int version_id,
                           SaveVMHandlers *ops,
                           void *opaque);

Two functions in the ``ops`` structure are the `save_state`
and `load_state` functions.  Notice that `load_state` receives a version_id
parameter to know what state format is receiving.  `save_state` doesn't
have a version_id parameter because it always uses the latest version.

Note that because the VMState macros still save the data in a raw
format, in many cases it's possible to replace legacy code
with a carefully constructed VMState description that matches the
byte layout of the existing code.

Changing migration data structures
----------------------------------

When we migrate a device, we save/load the state as a series
of fields.  Sometimes, due to bugs or new functionality, we need to
change the state to store more/different information.  Changing the migration
state saved for a device can break migration compatibility unless
care is taken to use the appropriate techniques.  In general QEMU tries
to maintain forward migration compatibility (i.e. migrating from
QEMU n->n+1) and there are users who benefit from backward compatibility
as well.

Subsections
-----------

The most common structure change is adding new data, e.g. when adding
a newer form of device, or adding that state that you previously
forgot to migrate.  This is best solved using a subsection.

A subsection is "like" a device vmstate, but with a particularity, it
has a Boolean function that tells if that values are needed to be sent
or not.  If this functions returns false, the subsection is not sent.
Subsections have a unique name, that is looked for on the receiving
side.

On the receiving side, if we found a subsection for a device that we
don't understand, we just fail the migration.  If we understand all
the subsections, then we load the state with success.  There's no check
that a subsection is loaded, so a newer QEMU that knows about a subsection
can (with care) load a stream from an older QEMU that didn't send
the subsection.

If the new data is only needed in a rare case, then the subsection
can be made conditional on that case and the migration will still
succeed to older QEMUs in most cases.  This is OK for data that's
critical, but in some use cases it's preferred that the migration
should succeed even with the data missing.  To support this the
subsection can be connected to a device property and from there
to a versioned machine type.

The 'pre_load' and 'post_load' functions on subsections are only
called if the subsection is loaded.

One important note is that the outer post_load() function is called "after"
loading all subsections, because a newer subsection could change the same
value that it uses.  A flag, and the combination of outer pre_load and
post_load can be used to detect whether a subsection was loaded, and to
fall back on default behaviour when the subsection isn't present.

Example:

.. code:: c

  static bool ide_drive_pio_state_needed(void *opaque)
  {
      IDEState *s = opaque;

      return ((s->status & DRQ_STAT) != 0)
          || (s->bus->error_status & BM_STATUS_PIO_RETRY);
  }

  const VMStateDescription vmstate_ide_drive_pio_state = {
      .name = "ide_drive/pio_state",
      .version_id = 1,
      .minimum_version_id = 1,
      .pre_save = ide_drive_pio_pre_save,
      .post_load = ide_drive_pio_post_load,
      .needed = ide_drive_pio_state_needed,
      .fields = (VMStateField[]) {
          VMSTATE_INT32(req_nb_sectors, IDEState),
          VMSTATE_VARRAY_INT32(io_buffer, IDEState, io_buffer_total_len, 1,
                               vmstate_info_uint8, uint8_t),
          VMSTATE_INT32(cur_io_buffer_offset, IDEState),
          VMSTATE_INT32(cur_io_buffer_len, IDEState),
          VMSTATE_UINT8(end_transfer_fn_idx, IDEState),
          VMSTATE_INT32(elementary_transfer_size, IDEState),
          VMSTATE_INT32(packet_transfer_size, IDEState),
          VMSTATE_END_OF_LIST()
      }
  };

  const VMStateDescription vmstate_ide_drive = {
      .name = "ide_drive",
      .version_id = 3,
      .minimum_version_id = 0,
      .post_load = ide_drive_post_load,
      .fields = (VMStateField[]) {
          .... several fields ....
          VMSTATE_END_OF_LIST()
      },
      .subsections = (const VMStateDescription*[]) {
          &vmstate_ide_drive_pio_state,
          NULL
      }
  };

Here we have a subsection for the pio state.  We only need to
save/send this state when we are in the middle of a pio operation
(that is what ``ide_drive_pio_state_needed()`` checks).  If DRQ_STAT is
not enabled, the values on that fields are garbage and don't need to
be sent.

Connecting subsections to properties
------------------------------------

Using a condition function that checks a 'property' to determine whether
to send a subsection allows backward migration compatibility when
new subsections are added, especially when combined with versioned
machine types.

For example:

   a) Add a new property using ``DEFINE_PROP_BOOL`` - e.g. support-foo and
      default it to true.
   b) Add an entry to the ``hw_compat_`` for the previous version that sets
      the property to false.
   c) Add a static bool  support_foo function that tests the property.
   d) Add a subsection with a .needed set to the support_foo function
   e) (potentially) Add an outer pre_load that sets up a default value
      for 'foo' to be used if the subsection isn't loaded.

Now that subsection will not be generated when using an older
machine type and the migration stream will be accepted by older
QEMU versions.

Not sending existing elements
-----------------------------

Sometimes members of the VMState are no longer needed:

  - removing them will break migration compatibility

  - making them version dependent and bumping the version will break backward migration
    compatibility.

Adding a dummy field into the migration stream is normally the best way to preserve
compatibility.

If the field really does need to be removed then:

  a) Add a new property/compatibility/function in the same way for subsections above.
  b) replace the VMSTATE macro with the _TEST version of the macro, e.g.:

   ``VMSTATE_UINT32(foo, barstruct)``

   becomes

   ``VMSTATE_UINT32_TEST(foo, barstruct, pre_version_baz)``

   Sometime in the future when we no longer care about the ancient versions these can be killed off.
   Note that for backward compatibility it's important to fill in the structure with
   data that the destination will understand.

Any difference in the predicates on the source and destination will end up
with different fields being enabled and data being loaded into the wrong
fields; for this reason conditional fields like this are very fragile.

Versions
--------

Version numbers are intended for major incompatible changes to the
migration of a device, and using them breaks backward-migration
compatibility; in general most changes can be made by adding Subsections
(see above) or _TEST macros (see above) which won't break compatibility.

Each version is associated with a series of fields saved.  The `save_state` always saves
the state as the newer version.  But `load_state` sometimes is able to
load state from an older version.

You can see that there are several version fields:

- `version_id`: the maximum version_id supported by VMState for that device.
- `minimum_version_id`: the minimum version_id that VMState is able to understand
  for that device.
- `minimum_version_id_old`: For devices that were not able to port to vmstate, we can
  assign a function that knows how to read this old state. This field is
  ignored if there is no `load_state_old` handler.

VMState is able to read versions from minimum_version_id to
version_id.  And the function ``load_state_old()`` (if present) is able to
load state from minimum_version_id_old to minimum_version_id.  This
function is deprecated and will be removed when no more users are left.

There are *_V* forms of many ``VMSTATE_`` macros to load fields for version dependent fields,
e.g.

.. code:: c

   VMSTATE_UINT16_V(ip_id, Slirp, 2),

only loads that field for versions 2 and newer.

Saving state will always create a section with the 'version_id' value
and thus can't be loaded by any older QEMU.

Massaging functions
-------------------

Sometimes, it is not enough to be able to save the state directly
from one structure, we need to fill the correct values there.  One
example is when we are using kvm.  Before saving the cpu state, we
need to ask kvm to copy to QEMU the state that it is using.  And the
opposite when we are loading the state, we need a way to tell kvm to
load the state for the cpu that we have just loaded from the QEMUFile.

The functions to do that are inside a vmstate definition, and are called:

- ``int (*pre_load)(void *opaque);``

  This function is called before we load the state of one device.

- ``int (*post_load)(void *opaque, int version_id);``

  This function is called after we load the state of one device.

- ``int (*pre_save)(void *opaque);``

  This function is called before we save the state of one device.

- ``int (*post_save)(void *opaque);``

  This function is called after we save the state of one device
  (even upon failure, unless the call to pre_save returned an error).

Example: You can look at hpet.c, that uses the first three functions
to massage the state that is transferred.

The ``VMSTATE_WITH_TMP`` macro may be useful when the migration
data doesn't match the stored device data well; it allows an
intermediate temporary structure to be populated with migration
data and then transferred to the main structure.

If you use memory API functions that update memory layout outside
initialization (i.e., in response to a guest action), this is a strong
indication that you need to call these functions in a `post_load` callback.
Examples of such memory API functions are:

  - memory_region_add_subregion()
  - memory_region_del_subregion()
  - memory_region_set_readonly()
  - memory_region_set_nonvolatile()
  - memory_region_set_enabled()
  - memory_region_set_address()
  - memory_region_set_alias_offset()

Iterative device migration
--------------------------

Some devices, such as RAM, Block storage or certain platform devices,
have large amounts of data that would mean that the CPUs would be
paused for too long if they were sent in one section.  For these
devices an *iterative* approach is taken.

The iterative devices generally don't use VMState macros
(although it may be possible in some cases) and instead use
qemu_put_*/qemu_get_* macros to read/write data to the stream.  Specialist
versions exist for high bandwidth IO.


An iterative device must provide:

  - A ``save_setup`` function that initialises the data structures and
    transmits a first section containing information on the device.  In the
    case of RAM this transmits a list of RAMBlocks and sizes.

  - A ``load_setup`` function that initialises the data structures on the
    destination.

  - A ``save_live_pending`` function that is called repeatedly and must
    indicate how much more data the iterative data must save.  The core
    migration code will use this to determine when to pause the CPUs
    and complete the migration.

  - A ``save_live_iterate`` function (called after ``save_live_pending``
    when there is significant data still to be sent).  It should send
    a chunk of data until the point that stream bandwidth limits tell it
    to stop.  Each call generates one section.

  - A ``save_live_complete_precopy`` function that must transmit the
    last section for the device containing any remaining data.

  - A ``load_state`` function used to load sections generated by
    any of the save functions that generate sections.

  - ``cleanup`` functions for both save and load that are called
    at the end of migration.

Note that the contents of the sections for iterative migration tend
to be open-coded by the devices; care should be taken in parsing
the results and structuring the stream to make them easy to validate.

Device ordering
---------------

There are cases in which the ordering of device loading matters; for
example in some systems where a device may assert an interrupt during loading,
if the interrupt controller is loaded later then it might lose the state.

Some ordering is implicitly provided by the order in which the machine
definition creates devices, however this is somewhat fragile.

The ``MigrationPriority`` enum provides a means of explicitly enforcing
ordering.  Numerically higher priorities are loaded earlier.
The priority is set by setting the ``priority`` field of the top level
``VMStateDescription`` for the device.

Stream structure
================

The stream tries to be word and endian agnostic, allowing migration between hosts
of different characteristics running the same VM.

  - Header

    - Magic
    - Version
    - VM configuration section

       - Machine type
       - Target page bits
  - List of sections
    Each section contains a device, or one iteration of a device save.

    - section type
    - section id
    - ID string (First section of each device)
    - instance id (First section of each device)
    - version id (First section of each device)
    - <device data>
    - Footer mark
  - EOF mark
  - VM Description structure
    Consisting of a JSON description of the contents for analysis only

The ``device data`` in each section consists of the data produced
by the code described above.  For non-iterative devices they have a single
section; iterative devices have an initial and last section and a set
of parts in between.
Note that there is very little checking by the common code of the integrity
of the ``device data`` contents, that's up to the devices themselves.
The ``footer mark`` provides a little bit of protection for the case where
the receiving side reads more or less data than expected.

The ``ID string`` is normally unique, having been formed from a bus name
and device address, PCI devices and storage devices hung off PCI controllers
fit this pattern well.  Some devices are fixed single instances (e.g. "pc-ram").
Others (especially either older devices or system devices which for
some reason don't have a bus concept) make use of the ``instance id``
for otherwise identically named devices.

Return path
-----------

Only a unidirectional stream is required for normal migration, however a
``return path`` can be created when bidirectional communication is desired.
This is primarily used by postcopy, but is also used to return a success
flag to the source at the end of migration.

``qemu_file_get_return_path(QEMUFile* fwdpath)`` gives the QEMUFile* for the return
path.

  Source side

     Forward path - written by migration thread
     Return path  - opened by main thread, read by return-path thread

  Destination side

     Forward path - read by main thread
     Return path  - opened by main thread, written by main thread AND postcopy
     thread (protected by rp_mutex)

Postcopy
========

'Postcopy' migration is a way to deal with migrations that refuse to converge
(or take too long to converge) its plus side is that there is an upper bound on
the amount of migration traffic and time it takes, the down side is that during
the postcopy phase, a failure of *either* side or the network connection causes
the guest to be lost.

In postcopy the destination CPUs are started before all the memory has been
transferred, and accesses to pages that are yet to be transferred cause
a fault that's translated by QEMU into a request to the source QEMU.

Postcopy can be combined with precopy (i.e. normal migration) so that if precopy
doesn't finish in a given time the switch is made to postcopy.

Enabling postcopy
-----------------

To enable postcopy, issue this command on the monitor (both source and
destination) prior to the start of migration:

``migrate_set_capability postcopy-ram on``

The normal commands are then used to start a migration, which is still
started in precopy mode.  Issuing:

``migrate_start_postcopy``

will now cause the transition from precopy to postcopy.
It can be issued immediately after migration is started or any
time later on.  Issuing it after the end of a migration is harmless.

Blocktime is a postcopy live migration metric, intended to show how
long the vCPU was in state of interruptable sleep due to pagefault.
That metric is calculated both for all vCPUs as overlapped value, and
separately for each vCPU. These values are calculated on destination
side.  To enable postcopy blocktime calculation, enter following
command on destination monitor:

``migrate_set_capability postcopy-blocktime on``

Postcopy blocktime can be retrieved by query-migrate qmp command.
postcopy-blocktime value of qmp command will show overlapped blocking
time for all vCPU, postcopy-vcpu-blocktime will show list of blocking
time per vCPU.

.. note::
  During the postcopy phase, the bandwidth limits set using
  ``migrate_set_speed`` is ignored (to avoid delaying requested pages that
  the destination is waiting for).

Postcopy device transfer
------------------------

Loading of device data may cause the device emulation to access guest RAM
that may trigger faults that have to be resolved by the source, as such
the migration stream has to be able to respond with page data *during* the
device load, and hence the device data has to be read from the stream completely
before the device load begins to free the stream up.  This is achieved by
'packaging' the device data into a blob that's read in one go.

Source behaviour
----------------

Until postcopy is entered the migration stream is identical to normal
precopy, except for the addition of a 'postcopy advise' command at
the beginning, to tell the destination that postcopy might happen.
When postcopy starts the source sends the page discard data and then
forms the 'package' containing:

   - Command: 'postcopy listen'
   - The device state

     A series of sections, identical to the precopy streams device state stream
     containing everything except postcopiable devices (i.e. RAM)
   - Command: 'postcopy run'

The 'package' is sent as the data part of a Command: ``CMD_PACKAGED``, and the
contents are formatted in the same way as the main migration stream.

During postcopy the source scans the list of dirty pages and sends them
to the destination without being requested (in much the same way as precopy),
however when a page request is received from the destination, the dirty page
scanning restarts from the requested location.  This causes requested pages
to be sent quickly, and also causes pages directly after the requested page
to be sent quickly in the hope that those pages are likely to be used
by the destination soon.

Destination behaviour
---------------------

Initially the destination looks the same as precopy, with a single thread
reading the migration stream; the 'postcopy advise' and 'discard' commands
are processed to change the way RAM is managed, but don't affect the stream
processing.

::

  ------------------------------------------------------------------------------
                          1      2   3     4 5                      6   7
  main -----DISCARD-CMD_PACKAGED ( LISTEN  DEVICE     DEVICE DEVICE RUN )
  thread                             |       |
                                     |     (page request)
                                     |        \___
                                     v            \
  listen thread:                     --- page -- page -- page -- page -- page --

                                     a   b        c
  ------------------------------------------------------------------------------

- On receipt of ``CMD_PACKAGED`` (1)

   All the data associated with the package - the ( ... ) section in the diagram -
   is read into memory, and the main thread recurses into qemu_loadvm_state_main
   to process the contents of the package (2) which contains commands (3,6) and
   devices (4...)

- On receipt of 'postcopy listen' - 3 -(i.e. the 1st command in the package)

   a new thread (a) is started that takes over servicing the migration stream,
   while the main thread carries on loading the package.   It loads normal
   background page data (b) but if during a device load a fault happens (5)
   the returned page (c) is loaded by the listen thread allowing the main
   threads device load to carry on.

- The last thing in the ``CMD_PACKAGED`` is a 'RUN' command (6)

   letting the destination CPUs start running.  At the end of the
   ``CMD_PACKAGED`` (7) the main thread returns to normal running behaviour and
   is no longer used by migration, while the listen thread carries on servicing
   page data until the end of migration.

Postcopy states
---------------

Postcopy moves through a series of states (see postcopy_state) from
ADVISE->DISCARD->LISTEN->RUNNING->END

 - Advise

    Set at the start of migration if postcopy is enabled, even
    if it hasn't had the start command; here the destination
    checks that its OS has the support needed for postcopy, and performs
    setup to ensure the RAM mappings are suitable for later postcopy.
    The destination will fail early in migration at this point if the
    required OS support is not present.
    (Triggered by reception of POSTCOPY_ADVISE command)

 - Discard

    Entered on receipt of the first 'discard' command; prior to
    the first Discard being performed, hugepages are switched off
    (using madvise) to ensure that no new huge pages are created
    during the postcopy phase, and to cause any huge pages that
    have discards on them to be broken.

 - Listen

    The first command in the package, POSTCOPY_LISTEN, switches
    the destination state to Listen, and starts a new thread
    (the 'listen thread') which takes over the job of receiving
    pages off the migration stream, while the main thread carries
    on processing the blob.  With this thread able to process page
    reception, the destination now 'sensitises' the RAM to detect
    any access to missing pages (on Linux using the 'userfault'
    system).

 - Running

    POSTCOPY_RUN causes the destination to synchronise all
    state and start the CPUs and IO devices running.  The main
    thread now finishes processing the migration package and
    now carries on as it would for normal precopy migration
    (although it can't do the cleanup it would do as it
    finishes a normal migration).

 - End

    The listen thread can now quit, and perform the cleanup of migration
    state, the migration is now complete.

Source side page maps
---------------------

The source side keeps two bitmaps during postcopy; 'the migration bitmap'
and 'unsent map'.  The 'migration bitmap' is basically the same as in
the precopy case, and holds a bit to indicate that page is 'dirty' -
i.e. needs sending.  During the precopy phase this is updated as the CPU
dirties pages, however during postcopy the CPUs are stopped and nothing
should dirty anything any more.

The 'unsent map' is used for the transition to postcopy. It is a bitmap that
has a bit cleared whenever a page is sent to the destination, however during
the transition to postcopy mode it is combined with the migration bitmap
to form a set of pages that:

   a) Have been sent but then redirtied (which must be discarded)
   b) Have not yet been sent - which also must be discarded to cause any
      transparent huge pages built during precopy to be broken.

Note that the contents of the unsentmap are sacrificed during the calculation
of the discard set and thus aren't valid once in postcopy.  The dirtymap
is still valid and is used to ensure that no page is sent more than once.  Any
request for a page that has already been sent is ignored.  Duplicate requests
such as this can happen as a page is sent at about the same time the
destination accesses it.

Postcopy with hugepages
-----------------------

Postcopy now works with hugetlbfs backed memory:

  a) The linux kernel on the destination must support userfault on hugepages.
  b) The huge-page configuration on the source and destination VMs must be
     identical; i.e. RAMBlocks on both sides must use the same page size.
  c) Note that ``-mem-path /dev/hugepages``  will fall back to allocating normal
     RAM if it doesn't have enough hugepages, triggering (b) to fail.
     Using ``-mem-prealloc`` enforces the allocation using hugepages.
  d) Care should be taken with the size of hugepage used; postcopy with 2MB
     hugepages works well, however 1GB hugepages are likely to be problematic
     since it takes ~1 second to transfer a 1GB hugepage across a 10Gbps link,
     and until the full page is transferred the destination thread is blocked.

Postcopy with shared memory
---------------------------

Postcopy migration with shared memory needs explicit support from the other
processes that share memory and from QEMU. There are restrictions on the type of
memory that userfault can support shared.

The Linux kernel userfault support works on `/dev/shm` memory and on `hugetlbfs`
(although the kernel doesn't provide an equivalent to `madvise(MADV_DONTNEED)`
for hugetlbfs which may be a problem in some configurations).

The vhost-user code in QEMU supports clients that have Postcopy support,
and the `vhost-user-bridge` (in `tests/`) and the DPDK package have changes
to support postcopy.

The client needs to open a userfaultfd and register the areas
of memory that it maps with userfault.  The client must then pass the
userfaultfd back to QEMU together with a mapping table that allows
fault addresses in the clients address space to be converted back to
RAMBlock/offsets.  The client's userfaultfd is added to the postcopy
fault-thread and page requests are made on behalf of the client by QEMU.
QEMU performs 'wake' operations on the client's userfaultfd to allow it
to continue after a page has arrived.

.. note::
  There are two future improvements that would be nice:
    a) Some way to make QEMU ignorant of the addresses in the clients
       address space
    b) Avoiding the need for QEMU to perform ufd-wake calls after the
       pages have arrived

Retro-fitting postcopy to existing clients is possible:
  a) A mechanism is needed for the registration with userfault as above,
     and the registration needs to be coordinated with the phases of
     postcopy.  In vhost-user extra messages are added to the existing
     control channel.
  b) Any thread that can block due to guest memory accesses must be
     identified and the implication understood; for example if the
     guest memory access is made while holding a lock then all other
     threads waiting for that lock will also be blocked.

Firmware
========

Migration migrates the copies of RAM and ROM, and thus when running
on the destination it includes the firmware from the source. Even after
resetting a VM, the old firmware is used.  Only once QEMU has been restarted
is the new firmware in use.

- Changes in firmware size can cause changes in the required RAMBlock size
  to hold the firmware and thus migration can fail.  In practice it's best
  to pad firmware images to convenient powers of 2 with plenty of space
  for growth.

- Care should be taken with device emulation code so that newer
  emulation code can work with older firmware to allow forward migration.

- Care should be taken with newer firmware so that backward migration
  to older systems with older device emulation code will work.

In some cases it may be best to tie specific firmware versions to specific
versioned machine types to cut down on the combinations that will need
support.  This is also useful when newer versions of firmware outgrow
the padding.

