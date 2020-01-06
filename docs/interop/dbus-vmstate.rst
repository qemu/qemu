=============
D-Bus VMState
=============

Introduction
============

The QEMU dbus-vmstate object's aim is to migrate helpers' data running
on a QEMU D-Bus bus. (refer to the :doc:`dbus` document for
some recommendations on D-Bus usage)

Upon migration, QEMU will go through the queue of
``org.qemu.VMState1`` D-Bus name owners and query their ``Id``. It
must be unique among the helpers.

It will then save arbitrary data of each Id to be transferred in the
migration stream and restored/loaded at the corresponding destination
helper.

For now, the data amount to be transferred is arbitrarily limited to
1Mb. The state must be saved quickly (a fraction of a second). (D-Bus
imposes a time limit on reply anyway, and migration would fail if data
isn't given quickly enough.)

dbus-vmstate object can be configured with the expected list of
helpers by setting its ``id-list`` property, with a comma-separated
``Id`` list.

Interface
=========

On object path ``/org/qemu/VMState1``, the following
``org.qemu.VMState1`` interface should be implemented:

.. code:: xml

  <interface name="org.qemu.VMState1">
    <property name="Id" type="s" access="read"/>
    <method name="Load">
      <arg type="ay" name="data" direction="in"/>
    </method>
    <method name="Save">
      <arg type="ay" name="data" direction="out"/>
    </method>
  </interface>

"Id" property
-------------

A string that identifies the helper uniquely. (maximum 256 bytes
including terminating NUL byte)

.. note::

   The helper ID namespace is a separate namespace. In particular, it is not
   related to QEMU "id" used in -object/-device objects.

Load(in u8[] bytes) method
--------------------------

The method called on destination with the state to restore.

The helper may be initially started in a waiting state (with
an --incoming argument for example), and it may resume on success.

An error may be returned to the caller.

Save(out u8[] bytes) method
---------------------------

The method called on the source to get the current state to be
migrated. The helper should continue to run normally.

An error may be returned to the caller.
