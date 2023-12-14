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

.. contents::

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

Debugging
=========

The migration stream can be analyzed thanks to ``scripts/analyze-migration.py``.

Example usage:

.. code-block:: shell

  $ qemu-system-x86_64 -display none -monitor stdio
  (qemu) migrate "exec:cat > mig"
  (qemu) q
  $ ./scripts/analyze-migration.py -f mig
  {
    "ram (3)": {
        "section sizes": {
            "pc.ram": "0x0000000008000000",
  ...

See also ``analyze-migration.py -h`` help for more options.

Common infrastructure
=====================

The files, sockets or fd's that carry the migration stream are abstracted by
the  ``QEMUFile`` type (see ``migration/qemu-file.h``).  In most cases this
is connected to a subtype of ``QIOChannel`` (see ``io/``).


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

We are declaring the state with name "pckbd".  The ``version_id`` is
3, and there are 4 uint8_t fields in the KBDState structure.  We
registered this ``VMSTATEDescription`` with one of the following
functions.  The first one will generate a device ``instance_id``
different for each registration.  Use the second one if you already
have an id that is different for each instance of the device:

.. code:: c

    vmstate_register_any(NULL, &vmstate_kbd, s);
    vmstate_register(NULL, instance_id, &vmstate_kbd, s);

For devices that are ``qdev`` based, we can register the device in the class
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

Two functions in the ``ops`` structure are the ``save_state``
and ``load_state`` functions.  Notice that ``load_state`` receives a version_id
parameter to know what state format is receiving.  ``save_state`` doesn't
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

Each version is associated with a series of fields saved.  The ``save_state`` always saves
the state as the newer version.  But ``load_state`` sometimes is able to
load state from an older version.

You can see that there are two version fields:

- ``version_id``: the maximum version_id supported by VMState for that device.
- ``minimum_version_id``: the minimum version_id that VMState is able to understand
  for that device.

VMState is able to read versions from minimum_version_id to version_id.

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
indication that you need to call these functions in a ``post_load`` callback.
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

  - A ``state_pending_exact`` function that indicates how much more
    data we must save.  The core migration code will use this to
    determine when to pause the CPUs and complete the migration.

  - A ``state_pending_estimate`` function that indicates how much more
    data we must save.  When the estimated amount is smaller than the
    threshold, we call ``state_pending_exact``.

  - A ``save_live_iterate`` function should send a chunk of data until
    the point that stream bandwidth limits tell it to stop.  Each call
    generates one section.

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

Dirty limit
=====================
The dirty limit, short for dirty page rate upper limit, is a new capability
introduced in the 8.1 QEMU release that uses a new algorithm based on the KVM
dirty ring to throttle down the guest during live migration.

The algorithm framework is as follows:

::

  ------------------------------------------------------------------------------
  main   --------------> throttle thread ------------> PREPARE(1) <--------
  thread  \                                                |              |
           \                                               |              |
            \                                              V              |
             -\                                        CALCULATE(2)       |
               \                                           |              |
                \                                          |              |
                 \                                         V              |
                  \                                    SET PENALTY(3) -----
                   -\                                      |
                     \                                     |
                      \                                    V
                       -> virtual CPU thread -------> ACCEPT PENALTY(4)
  ------------------------------------------------------------------------------

When the qmp command qmp_set_vcpu_dirty_limit is called for the first time,
the QEMU main thread starts the throttle thread. The throttle thread, once
launched, executes the loop, which consists of three steps:

  - PREPARE (1)

     The entire work of PREPARE (1) is preparation for the second stage,
     CALCULATE(2), as the name implies. It involves preparing the dirty
     page rate value and the corresponding upper limit of the VM:
     The dirty page rate is calculated via the KVM dirty ring mechanism,
     which tells QEMU how many dirty pages a virtual CPU has had since the
     last KVM_EXIT_DIRTY_RING_FULL exception; The dirty page rate upper
     limit is specified by caller, therefore fetch it directly.

  - CALCULATE (2)

     Calculate a suitable sleep period for each virtual CPU, which will be
     used to determine the penalty for the target virtual CPU. The
     computation must be done carefully in order to reduce the dirty page
     rate progressively down to the upper limit without oscillation. To
     achieve this, two strategies are provided: the first is to add or
     subtract sleep time based on the ratio of the current dirty page rate
     to the limit, which is used when the current dirty page rate is far
     from the limit; the second is to add or subtract a fixed time when
     the current dirty page rate is close to the limit.

  - SET PENALTY (3)

     Set the sleep time for each virtual CPU that should be penalized based
     on the results of the calculation supplied by step CALCULATE (2).

After completing the three above stages, the throttle thread loops back
to step PREPARE (1) until the dirty limit is reached.

On the other hand, each virtual CPU thread reads the sleep duration and
sleeps in the path of the KVM_EXIT_DIRTY_RING_FULL exception handler, that
is ACCEPT PENALTY (4). Virtual CPUs tied with writing processes will
obviously exit to the path and get penalized, whereas virtual CPUs involved
with read processes will not.

In summary, thanks to the KVM dirty ring technology, the dirty limit
algorithm will restrict virtual CPUs as needed to keep their dirty page
rate inside the limit. This leads to more steady reading performance during
live migration and can aid in improving large guest responsiveness.

Postcopy
========

'Postcopy' migration is a way to deal with migrations that refuse to converge
(or take too long to converge) its plus side is that there is an upper bound on
the amount of migration traffic and time it takes, the down side is that during
the postcopy phase, a failure of *either* side causes the guest to be lost.

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
long the vCPU was in state of interruptible sleep due to pagefault.
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
  ``migrate_set_parameter`` is ignored (to avoid delaying requested pages that
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

Postcopy Recovery
-----------------

Comparing to precopy, postcopy is special on error handlings.  When any
error happens (in this case, mostly network errors), QEMU cannot easily
fail a migration because VM data resides in both source and destination
QEMU instances.  On the other hand, when issue happens QEMU on both sides
will go into a paused state.  It'll need a recovery phase to continue a
paused postcopy migration.

The recovery phase normally contains a few steps:

  - When network issue occurs, both QEMU will go into PAUSED state

  - When the network is recovered (or a new network is provided), the admin
    can setup the new channel for migration using QMP command
    'migrate-recover' on destination node, preparing for a resume.

  - On source host, the admin can continue the interrupted postcopy
    migration using QMP command 'migrate' with resume=true flag set.

  - After the connection is re-established, QEMU will continue the postcopy
    migration on both sides.

During a paused postcopy migration, the VM can logically still continue
running, and it will not be impacted from any page access to pages that
were already migrated to destination VM before the interruption happens.
However, if any of the missing pages got accessed on destination VM, the VM
thread will be halted waiting for the page to be migrated, it means it can
be halted until the recovery is complete.

The impact of accessing missing pages can be relevant to different
configurations of the guest.  For example, when with async page fault
enabled, logically the guest can proactively schedule out the threads
accessing missing pages.

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

 - Paused

    Postcopy can run into a paused state (normally on both sides when
    happens), where all threads will be temporarily halted mostly due to
    network errors.  When reaching paused state, migration will make sure
    the qemu binary on both sides maintain the data without corrupting
    the VM.  To continue the migration, the admin needs to fix the
    migration channel using the QMP command 'migrate-recover' on the
    destination node, then resume the migration using QMP command 'migrate'
    again on source node, with resume=true flag set.

 - End

    The listen thread can now quit, and perform the cleanup of migration
    state, the migration is now complete.

Source side page map
--------------------

The 'migration bitmap' in postcopy is basically the same as in the precopy,
where each of the bit to indicate that page is 'dirty' - i.e. needs
sending.  During the precopy phase this is updated as the CPU dirties
pages, however during postcopy the CPUs are stopped and nothing should
dirty anything any more. Instead, dirty bits are cleared when the relevant
pages are sent during postcopy.

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

The Linux kernel userfault support works on ``/dev/shm`` memory and on ``hugetlbfs``
(although the kernel doesn't provide an equivalent to ``madvise(MADV_DONTNEED)``
for hugetlbfs which may be a problem in some configurations).

The vhost-user code in QEMU supports clients that have Postcopy support,
and the ``vhost-user-bridge`` (in ``tests/``) and the DPDK package have changes
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

Postcopy Preemption Mode
------------------------

Postcopy preempt is a new capability introduced in 8.0 QEMU release, it
allows urgent pages (those got page fault requested from destination QEMU
explicitly) to be sent in a separate preempt channel, rather than queued in
the background migration channel.  Anyone who cares about latencies of page
faults during a postcopy migration should enable this feature.  By default,
it's not enabled.

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


Backwards compatibility
=======================

How backwards compatibility works
---------------------------------

When we do migration, we have two QEMU processes: the source and the
target.  There are two cases, they are the same version or they are
different versions.  The easy case is when they are the same version.
The difficult one is when they are different versions.

There are two things that are different, but they have very similar
names and sometimes get confused:

- QEMU version
- machine type version

Let's start with a practical example, we start with:

- qemu-system-x86_64 (v5.2), from now on qemu-5.2.
- qemu-system-x86_64 (v5.1), from now on qemu-5.1.

Related to this are the "latest" machine types defined on each of
them:

- pc-q35-5.2 (newer one in qemu-5.2) from now on pc-5.2
- pc-q35-5.1 (newer one in qemu-5.1) from now on pc-5.1

First of all, migration is only supposed to work if you use the same
machine type in both source and destination. The QEMU hardware
configuration needs to be the same also on source and destination.
Most aspects of the backend configuration can be changed at will,
except for a few cases where the backend features influence frontend
device feature exposure.  But that is not relevant for this section.

I am going to list the number of combinations that we can have.  Let's
start with the trivial ones, QEMU is the same on source and
destination:

1 - qemu-5.2 -M pc-5.2  -> migrates to -> qemu-5.2 -M pc-5.2

  This is the latest QEMU with the latest machine type.
  This have to work, and if it doesn't work it is a bug.

2 - qemu-5.1 -M pc-5.1  -> migrates to -> qemu-5.1 -M pc-5.1

  Exactly the same case than the previous one, but for 5.1.
  Nothing to see here either.

This are the easiest ones, we will not talk more about them in this
section.

Now we start with the more interesting cases.  Consider the case where
we have the same QEMU version in both sides (qemu-5.2) but we are using
the latest machine type for that version (pc-5.2) but one of an older
QEMU version, in this case pc-5.1.

3 - qemu-5.2 -M pc-5.1  -> migrates to -> qemu-5.2 -M pc-5.1

  It needs to use the definition of pc-5.1 and the devices as they
  were configured on 5.1, but this should be easy in the sense that
  both sides are the same QEMU and both sides have exactly the same
  idea of what the pc-5.1 machine is.

4 - qemu-5.1 -M pc-5.2  -> migrates to -> qemu-5.1 -M pc-5.2

  This combination is not possible as the qemu-5.1 doesn't understand
  pc-5.2 machine type.  So nothing to worry here.

Now it comes the interesting ones, when both QEMU processes are
different.  Notice also that the machine type needs to be pc-5.1,
because we have the limitation than qemu-5.1 doesn't know pc-5.2.  So
the possible cases are:

5 - qemu-5.2 -M pc-5.1  -> migrates to -> qemu-5.1 -M pc-5.1

  This migration is known as newer to older.  We need to make sure
  when we are developing 5.2 we need to take care about not to break
  migration to qemu-5.1.  Notice that we can't make updates to
  qemu-5.1 to understand whatever qemu-5.2 decides to change, so it is
  in qemu-5.2 side to make the relevant changes.

6 - qemu-5.1 -M pc-5.1  -> migrates to -> qemu-5.2 -M pc-5.1

  This migration is known as older to newer.  We need to make sure
  than we are able to receive migrations from qemu-5.1. The problem is
  similar to the previous one.

If qemu-5.1 and qemu-5.2 were the same, there will not be any
compatibility problems.  But the reason that we create qemu-5.2 is to
get new features, devices, defaults, etc.

If we get a device that has a new feature, or change a default value,
we have a problem when we try to migrate between different QEMU
versions.

So we need a way to tell qemu-5.2 that when we are using machine type
pc-5.1, it needs to **not** use the feature, to be able to migrate to
real qemu-5.1.

And the equivalent part when migrating from qemu-5.1 to qemu-5.2.
qemu-5.2 has to expect that it is not going to get data for the new
feature, because qemu-5.1 doesn't know about it.

How do we tell QEMU about these device feature changes?  In
hw/core/machine.c:hw_compat_X_Y arrays.

If we change a default value, we need to put back the old value on
that array.  And the device, during initialization needs to look at
that array to see what value it needs to get for that feature.  And
what are we going to put in that array, the value of a property.

To create a property for a device, we need to use one of the
DEFINE_PROP_*() macros. See include/hw/qdev-properties.h to find the
macros that exist.  With it, we set the default value for that
property, and that is what it is going to get in the latest released
version.  But if we want a different value for a previous version, we
can change that in the hw_compat_X_Y arrays.

hw_compat_X_Y is an array of registers that have the format:

- name_device
- name_property
- value

Let's see a practical example.

In qemu-5.2 virtio-blk-device got multi queue support.  This is a
change that is not backward compatible.  In qemu-5.1 it has one
queue. In qemu-5.2 it has the same number of queues as the number of
cpus in the system.

When we are doing migration, if we migrate from a device that has 4
queues to a device that have only one queue, we don't know where to
put the extra information for the other 3 queues, and we fail
migration.

Similar problem when we migrate from qemu-5.1 that has only one queue
to qemu-5.2, we only sent information for one queue, but destination
has 4, and we have 3 queues that are not properly initialized and
anything can happen.

So, how can we address this problem.  Easy, just convince qemu-5.2
that when it is running pc-5.1, it needs to set the number of queues
for virtio-blk-devices to 1.

That way we fix the cases 5 and 6.

5 - qemu-5.2 -M pc-5.1  -> migrates to -> qemu-5.1 -M pc-5.1

    qemu-5.2 -M pc-5.1 sets number of queues to be 1.
    qemu-5.1 -M pc-5.1 expects number of queues to be 1.

    correct.  migration works.

6 - qemu-5.1 -M pc-5.1  -> migrates to -> qemu-5.2 -M pc-5.1

    qemu-5.1 -M pc-5.1 sets number of queues to be 1.
    qemu-5.2 -M pc-5.1 expects number of queues to be 1.

    correct.  migration works.

And now the other interesting case, case 3.  In this case we have:

3 - qemu-5.2 -M pc-5.1  -> migrates to -> qemu-5.2 -M pc-5.1

    Here we have the same QEMU in both sides.  So it doesn't matter a
    lot if we have set the number of queues to 1 or not, because
    they are the same.

    WRONG!

    Think what happens if we do one of this double migrations:

    A -> migrates -> B -> migrates -> C

    where:

    A: qemu-5.1 -M pc-5.1
    B: qemu-5.2 -M pc-5.1
    C: qemu-5.2 -M pc-5.1

    migration A -> B is case 6, so number of queues needs to be 1.

    migration B -> C is case 3, so we don't care.  But actually we
    care because we haven't started the guest in qemu-5.2, it came
    migrated from qemu-5.1.  So to be in the safe place, we need to
    always use number of queues 1 when we are using pc-5.1.

Now, how was this done in reality?  The following commit shows how it
was done::

  commit 9445e1e15e66c19e42bea942ba810db28052cd05
  Author: Stefan Hajnoczi <stefanha@redhat.com>
  Date:   Tue Aug 18 15:33:47 2020 +0100

  virtio-blk-pci: default num_queues to -smp N

The relevant parts for migration are::

    @@ -1281,7 +1284,8 @@ static Property virtio_blk_properties[] = {
     #endif
         DEFINE_PROP_BIT("request-merging", VirtIOBlock, conf.request_merging, 0,
                         true),
    -    DEFINE_PROP_UINT16("num-queues", VirtIOBlock, conf.num_queues, 1),
    +    DEFINE_PROP_UINT16("num-queues", VirtIOBlock, conf.num_queues,
    +                       VIRTIO_BLK_AUTO_NUM_QUEUES),
         DEFINE_PROP_UINT16("queue-size", VirtIOBlock, conf.queue_size, 256),

It changes the default value of num_queues.  But it fishes it for old
machine types to have the right value::

    @@ -31,6 +31,7 @@
     GlobalProperty hw_compat_5_1[] = {
         ...
    +    { "virtio-blk-device", "num-queues", "1"},
         ...
     };

A device with different features on both sides
----------------------------------------------

Let's assume that we are using the same QEMU binary on both sides,
just to make the things easier.  But we have a device that has
different features on both sides of the migration.  That can be
because the devices are different, because the kernel driver of both
devices have different features, whatever.

How can we get this to work with migration.  The way to do that is
"theoretically" easy.  You have to get the features that the device
has in the source of the migration.  The features that the device has
on the target of the migration, you get the intersection of the
features of both sides, and that is the way that you should launch
QEMU.

Notice that this is not completely related to QEMU.  The most
important thing here is that this should be handled by the managing
application that launches QEMU.  If QEMU is configured correctly, the
migration will succeed.

That said, actually doing it is complicated.  Almost all devices are
bad at being able to be launched with only some features enabled.
With one big exception: cpus.

You can read the documentation for QEMU x86 cpu models here:

https://qemu-project.gitlab.io/qemu/system/qemu-cpu-models.html

See when they talk about migration they recommend that one chooses the
newest cpu model that is supported for all cpus.

Let's say that we have:

Host A:

Device X has the feature Y

Host B:

Device X has not the feature Y

If we try to migrate without any care from host A to host B, it will
fail because when migration tries to load the feature Y on
destination, it will find that the hardware is not there.

Doing this would be the equivalent of doing with cpus:

Host A:

$ qemu-system-x86_64 -cpu host

Host B:

$ qemu-system-x86_64 -cpu host

When both hosts have different cpu features this is guaranteed to
fail.  Especially if Host B has less features than host A.  If host A
has less features than host B, sometimes it works.  Important word of
last sentence is "sometimes".

So, forgetting about cpu models and continuing with the -cpu host
example, let's see that the differences of the cpus is that Host A and
B have the following features:

Features:   'pcid'  'stibp' 'taa-no'
Host A:        X       X
Host B:                        X

And we want to migrate between them, the way configure both QEMU cpu
will be:

Host A:

$ qemu-system-x86_64 -cpu host,pcid=off,stibp=off

Host B:

$ qemu-system-x86_64 -cpu host,taa-no=off

And you would be able to migrate between them.  It is responsibility
of the management application or of the user to make sure that the
configuration is correct.  QEMU doesn't know how to look at this kind
of features in general.

Notice that we don't recommend to use -cpu host for migration.  It is
used in this example because it makes the example simpler.

Other devices have worse control about individual features.  If they
want to be able to migrate between hosts that show different features,
the device needs a way to configure which ones it is going to use.

In this section we have considered that we are using the same QEMU
binary in both sides of the migration.  If we use different QEMU
versions process, then we need to have into account all other
differences and the examples become even more complicated.

How to mitigate when we have a backward compatibility error
-----------------------------------------------------------

We broke migration for old machine types continuously during
development.  But as soon as we find that there is a problem, we fix
it.  The problem is what happens when we detect after we have done a
release that something has gone wrong.

Let see how it worked with one example.

After the release of qemu-8.0 we found a problem when doing migration
of the machine type pc-7.2.

- $ qemu-7.2 -M pc-7.2  ->  qemu-7.2 -M pc-7.2

  This migration works

- $ qemu-8.0 -M pc-7.2  ->  qemu-8.0 -M pc-7.2

  This migration works

- $ qemu-8.0 -M pc-7.2  ->  qemu-7.2 -M pc-7.2

  This migration fails

- $ qemu-7.2 -M pc-7.2  ->  qemu-8.0 -M pc-7.2

  This migration fails

So clearly something fails when migration between qemu-7.2 and
qemu-8.0 with machine type pc-7.2.  The error messages, and git bisect
pointed to this commit.

In qemu-8.0 we got this commit::

    commit 010746ae1db7f52700cb2e2c46eb94f299cfa0d2
    Author: Jonathan Cameron <Jonathan.Cameron@huawei.com>
    Date:   Thu Mar 2 13:37:02 2023 +0000

    hw/pci/aer: Implement PCI_ERR_UNCOR_MASK register


The relevant bits of the commit for our example are this ones::

    --- a/hw/pci/pcie_aer.c
    +++ b/hw/pci/pcie_aer.c
    @@ -112,6 +112,10 @@ int pcie_aer_init(PCIDevice *dev,

         pci_set_long(dev->w1cmask + offset + PCI_ERR_UNCOR_STATUS,
                      PCI_ERR_UNC_SUPPORTED);
    +    pci_set_long(dev->config + offset + PCI_ERR_UNCOR_MASK,
    +                 PCI_ERR_UNC_MASK_DEFAULT);
    +    pci_set_long(dev->wmask + offset + PCI_ERR_UNCOR_MASK,
    +                 PCI_ERR_UNC_SUPPORTED);

         pci_set_long(dev->config + offset + PCI_ERR_UNCOR_SEVER,
                     PCI_ERR_UNC_SEVERITY_DEFAULT);

The patch changes how we configure PCI space for AER.  But QEMU fails
when the PCI space configuration is different between source and
destination.

The following commit shows how this got fixed::

    commit 5ed3dabe57dd9f4c007404345e5f5bf0e347317f
    Author: Leonardo Bras <leobras@redhat.com>
    Date:   Tue May 2 21:27:02 2023 -0300

    hw/pci: Disable PCI_ERR_UNCOR_MASK register for machine type < 8.0

    [...]

The relevant parts of the fix in QEMU are as follow:

First, we create a new property for the device to be able to configure
the old behaviour or the new behaviour::

    diff --git a/hw/pci/pci.c b/hw/pci/pci.c
    index 8a87ccc8b0..5153ad63d6 100644
    --- a/hw/pci/pci.c
    +++ b/hw/pci/pci.c
    @@ -79,6 +79,8 @@ static Property pci_props[] = {
         DEFINE_PROP_STRING("failover_pair_id", PCIDevice,
                            failover_pair_id),
         DEFINE_PROP_UINT32("acpi-index",  PCIDevice, acpi_index, 0),
    +    DEFINE_PROP_BIT("x-pcie-err-unc-mask", PCIDevice, cap_present,
    +                    QEMU_PCIE_ERR_UNC_MASK_BITNR, true),
         DEFINE_PROP_END_OF_LIST()
     };

Notice that we enable the feature for new machine types.

Now we see how the fix is done.  This is going to depend on what kind
of breakage happens, but in this case it is quite simple::

    diff --git a/hw/pci/pcie_aer.c b/hw/pci/pcie_aer.c
    index 103667c368..374d593ead 100644
    --- a/hw/pci/pcie_aer.c
    +++ b/hw/pci/pcie_aer.c
    @@ -112,10 +112,13 @@ int pcie_aer_init(PCIDevice *dev, uint8_t cap_ver,
    uint16_t offset,

         pci_set_long(dev->w1cmask + offset + PCI_ERR_UNCOR_STATUS,
                      PCI_ERR_UNC_SUPPORTED);
    -    pci_set_long(dev->config + offset + PCI_ERR_UNCOR_MASK,
    -                 PCI_ERR_UNC_MASK_DEFAULT);
    -    pci_set_long(dev->wmask + offset + PCI_ERR_UNCOR_MASK,
    -                 PCI_ERR_UNC_SUPPORTED);
    +
    +    if (dev->cap_present & QEMU_PCIE_ERR_UNC_MASK) {
    +        pci_set_long(dev->config + offset + PCI_ERR_UNCOR_MASK,
    +                     PCI_ERR_UNC_MASK_DEFAULT);
    +        pci_set_long(dev->wmask + offset + PCI_ERR_UNCOR_MASK,
    +                     PCI_ERR_UNC_SUPPORTED);
    +    }

         pci_set_long(dev->config + offset + PCI_ERR_UNCOR_SEVER,
                      PCI_ERR_UNC_SEVERITY_DEFAULT);

I.e. If the property bit is enabled, we configure it as we did for
qemu-8.0.  If the property bit is not set, we configure it as it was in 7.2.

And now, everything that is missing is disabling the feature for old
machine types::

    diff --git a/hw/core/machine.c b/hw/core/machine.c
    index 47a34841a5..07f763eb2e 100644
    --- a/hw/core/machine.c
    +++ b/hw/core/machine.c
    @@ -48,6 +48,7 @@ GlobalProperty hw_compat_7_2[] = {
         { "e1000e", "migrate-timadj", "off" },
         { "virtio-mem", "x-early-migration", "false" },
         { "migration", "x-preempt-pre-7-2", "true" },
    +    { TYPE_PCI_DEVICE, "x-pcie-err-unc-mask", "off" },
     };
     const size_t hw_compat_7_2_len = G_N_ELEMENTS(hw_compat_7_2);

And now, when qemu-8.0.1 is released with this fix, all combinations
are going to work as supposed.

- $ qemu-7.2 -M pc-7.2  ->  qemu-7.2 -M pc-7.2 (works)
- $ qemu-8.0.1 -M pc-7.2  ->  qemu-8.0.1 -M pc-7.2 (works)
- $ qemu-8.0.1 -M pc-7.2  ->  qemu-7.2 -M pc-7.2 (works)
- $ qemu-7.2 -M pc-7.2  ->  qemu-8.0.1 -M pc-7.2 (works)

So the normality has been restored and everything is ok, no?

Not really, now our matrix is much bigger.  We started with the easy
cases, migration from the same version to the same version always
works:

- $ qemu-7.2 -M pc-7.2  ->  qemu-7.2 -M pc-7.2
- $ qemu-8.0 -M pc-7.2  ->  qemu-8.0 -M pc-7.2
- $ qemu-8.0.1 -M pc-7.2  ->  qemu-8.0.1 -M pc-7.2

Now the interesting ones.  When the QEMU processes versions are
different.  For the 1st set, their fail and we can do nothing, both
versions are released and we can't change anything.

- $ qemu-7.2 -M pc-7.2  ->  qemu-8.0 -M pc-7.2
- $ qemu-8.0 -M pc-7.2  ->  qemu-7.2 -M pc-7.2

This two are the ones that work. The whole point of making the
change in qemu-8.0.1 release was to fix this issue:

- $ qemu-7.2 -M pc-7.2  ->  qemu-8.0.1 -M pc-7.2
- $ qemu-8.0.1 -M pc-7.2  ->  qemu-7.2 -M pc-7.2

But now we found that qemu-8.0 neither can migrate to qemu-7.2 not
qemu-8.0.1.

- $ qemu-8.0 -M pc-7.2  ->  qemu-8.0.1 -M pc-7.2
- $ qemu-8.0.1 -M pc-7.2  ->  qemu-8.0 -M pc-7.2

So, if we start a pc-7.2 machine in qemu-8.0 we can't migrate it to
anything except to qemu-8.0.

Can we do better?

Yeap.  If we know that we are going to do this migration:

- $ qemu-8.0 -M pc-7.2  ->  qemu-8.0.1 -M pc-7.2

We can launch the appropriate devices with::

  --device...,x-pci-e-err-unc-mask=on

And now we can receive a migration from 8.0.  And from now on, we can
do that migration to new machine types if we remember to enable that
property for pc-7.2.  Notice that we need to remember, it is not
enough to know that the source of the migration is qemu-8.0.  Think of
this example:

$ qemu-8.0 -M pc-7.2 -> qemu-8.0.1 -M pc-7.2 -> qemu-8.2 -M pc-7.2

In the second migration, the source is not qemu-8.0, but we still have
that "problem" and have that property enabled.  Notice that we need to
continue having this mark/property until we have this machine
rebooted.  But it is not a normal reboot (that don't reload QEMU) we
need the machine to poweroff/poweron on a fixed QEMU.  And from now
on we can use the proper real machine.
