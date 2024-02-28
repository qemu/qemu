===================
Migration framework
===================

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
- file migration: do the migration using a file that is passed to QEMU
  by path. A file offset option is supported to allow a management
  application to add its own metadata to the start of the file without
  QEMU interference.

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
      .fields = (const VMStateField[]) {
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
      .fields = (const VMStateField[]) {
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
      .fields = (const VMStateField[]) {
          .... several fields ....
          VMSTATE_END_OF_LIST()
      },
      .subsections = (const VMStateDescription * const []) {
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

If you use memory or portio_list API functions that update memory layout outside
initialization (i.e., in response to a guest action), this is a strong
indication that you need to call these functions in a ``post_load`` callback.
Examples of such API functions are:

  - memory_region_add_subregion()
  - memory_region_del_subregion()
  - memory_region_set_readonly()
  - memory_region_set_nonvolatile()
  - memory_region_set_enabled()
  - memory_region_set_address()
  - memory_region_set_alias_offset()
  - portio_list_set_address()
  - portio_list_set_enabled()

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

