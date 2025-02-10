
=======================================
Reset in QEMU: the Resettable interface
=======================================

The reset of qemu objects is handled using the resettable interface declared
in ``include/hw/resettable.h``.

This interface allows objects to be grouped (on a tree basis); so that the
whole group can be reset consistently. Each individual member object does not
have to care about others; in particular, problems of order (which object is
reset first) are addressed.

The main object types which implement this interface are DeviceClass
and BusClass.

Triggering reset
----------------

This section documents the APIs which "users" of a resettable object should use
to control it. All resettable control functions must be called while holding
the BQL.

You can apply a reset to an object using ``resettable_assert_reset()``. You need
to call ``resettable_release_reset()`` to release the object from reset. To
instantly reset an object, without keeping it in reset state, just call
``resettable_reset()``. These functions take two parameters: a pointer to the
object to reset and a reset type.

The Resettable interface handles reset types with an enum ``ResetType``:

``RESET_TYPE_COLD``
  Cold reset is supported by every resettable object. In QEMU, it means we reset
  to the initial state corresponding to the start of QEMU; this might differ
  from what is a real hardware cold reset. It differs from other resets (like
  warm or bus resets) which may keep certain parts untouched.

``RESET_TYPE_SNAPSHOT_LOAD``
  This is called for a reset which is being done to put the system into a
  clean state prior to loading a snapshot. (This corresponds to a reset
  with ``SHUTDOWN_CAUSE_SNAPSHOT_LOAD``.) Almost all devices should treat
  this the same as ``RESET_TYPE_COLD``. The main exception is devices which
  have some non-deterministic state they want to reinitialize to a different
  value on each cold reset, such as RNG seed information, and which they
  must not reinitialize on a snapshot-load reset.

``RESET_TYPE_WAKEUP``
  If the machine supports waking up from a suspended state and needs to reset
  its devices during wake-up (from the ``MachineClass::wakeup()`` method), this
  reset type should be used for such a request. Devices can utilize this reset
  type to differentiate the reset requested during machine wake-up from other
  reset requests. For example, RAM content must not be lost during wake-up, and
  memory devices like virtio-mem that provide additional RAM must not reset
  such state during wake-ups, but might do so during cold resets. However, this
  reset type should not be used for wake-up detection, as not every machine
  type issues a device reset request during wake-up.

``RESET_TYPE_S390_CPU_NORMAL``
  This is only used for S390 CPU objects; it clears interrupts, stops
  processing, and clears the TLB, but does not touch register contents.

``RESET_TYPE_S390_CPU_INITIAL``
  This is only used for S390 CPU objects; it does everything
  ``RESET_TYPE_S390_CPU_NORMAL`` does and also clears the PSW, prefix,
  FPC, timer and control registers. It does not touch gprs, fprs or acrs.

Devices which implement reset methods must treat any unknown ``ResetType``
as equivalent to ``RESET_TYPE_COLD``; this will reduce the amount of
existing code we need to change if we add more types in future.

Calling ``resettable_reset()`` is equivalent to calling
``resettable_assert_reset()`` then ``resettable_release_reset()``. It is
possible to interleave multiple calls to these three functions. There may
be several reset sources/controllers of a given object. The interface handles
everything and the different reset controllers do not need to know anything
about each others. The object will leave reset state only when each other
controllers end their reset operation. This point is handled internally by
maintaining a count of in-progress resets; it is crucial to call
``resettable_release_reset()`` one time and only one time per
``resettable_assert_reset()`` call.

For now migration of a device or bus in reset is not supported. Care must be
taken not to delay ``resettable_release_reset()`` after its
``resettable_assert_reset()`` counterpart.

Note that, since resettable is an interface, the API takes a simple Object as
parameter. Still, it is a programming error to call a resettable function on a
non-resettable object and it will trigger a run time assert error. Since most
calls to resettable interface are done through base class functions, such an
error is not likely to happen.

For Devices and Buses, the following helper functions exist:

- ``device_cold_reset()``
- ``bus_cold_reset()``

These are simple wrappers around resettable_reset() function; they only cast the
Device or Bus into an Object and pass the cold reset type. When possible
prefer to use these functions instead of ``resettable_reset()``.

Device and bus functions co-exist because there can be semantic differences
between resetting a bus and resetting the controller bridge which owns it.
For example, consider a SCSI controller. Resetting the controller puts all
its registers back to what reset state was as well as reset everything on the
SCSI bus, whereas resetting just the SCSI bus only resets everything that's on
it but not the controller.


Multi-phase mechanism
---------------------

This section documents the internals of the resettable interface.

The resettable interface uses a multi-phase system to relieve objects and
machines from reset ordering problems. To address this, the reset operation
of an object is split into three well defined phases.

When resetting several objects (for example the whole machine at simulation
startup), all first phases of all objects are executed, then all second phases
and then all third phases.

The three phases are:

1. The **enter** phase is executed when the object enters reset. It resets only
   local state of the object; it must not do anything that has a side-effect
   on other objects, such as raising or lowering a qemu_irq line or reading or
   writing guest memory.

2. The **hold** phase is executed for entry into reset, once every object in the
   group which is being reset has had its *enter* phase executed. At this point
   devices can do actions that affect other objects.

3. The **exit** phase is executed when the object leaves the reset state.
   Actions affecting other objects are permitted.

As said in previous section, the interface maintains a count of reset. This
count is used to ensure phases are executed only when required. *enter* and
*hold* phases are executed only when asserting reset for the first time
(if an object is already in reset state when calling
``resettable_assert_reset()`` or ``resettable_reset()``, they are not
executed).
The *exit* phase is executed only when the last reset operation ends. Therefore
the object does not need to care how many of reset controllers it has and how
many of them have started a reset.


Handling reset in a resettable object
-------------------------------------

This section documents the APIs that an implementation of a resettable object
must provide and what functions it has access to. It is intended for people
who want to implement or convert a class which has the resettable interface;
for example when specializing an existing device or bus.

Methods to implement
....................

Three methods should be defined or left empty. Each method corresponds to a
phase of the reset; they are name ``phases.enter()``, ``phases.hold()`` and
``phases.exit()``. They all take the object as parameter. The *enter* method
also take the reset type as second parameter.

When extending an existing class, these methods may need to be extended too.
The ``resettable_class_set_parent_phases()`` class function may be used to
backup parent class methods.

Here follows an example to implement reset for a Device which sets an IO while
in reset.

::

    static void mydev_reset_enter(Object *obj, ResetType type)
    {
        MyDevClass *myclass = MYDEV_GET_CLASS(obj);
        MyDevState *mydev = MYDEV(obj);
        /* call parent class enter phase */
        if (myclass->parent_phases.enter) {
            myclass->parent_phases.enter(obj, type);
        }
        /* initialize local state only */
        mydev->var = 0;
    }

    static void mydev_reset_hold(Object *obj, ResetType type)
    {
        MyDevClass *myclass = MYDEV_GET_CLASS(obj);
        MyDevState *mydev = MYDEV(obj);
        /* call parent class hold phase */
        if (myclass->parent_phases.hold) {
            myclass->parent_phases.hold(obj, type);
        }
        /* set an IO */
        qemu_set_irq(mydev->irq, 1);
    }

    static void mydev_reset_exit(Object *obj, ResetType type)
    {
        MyDevClass *myclass = MYDEV_GET_CLASS(obj);
        MyDevState *mydev = MYDEV(obj);
        /* call parent class exit phase */
        if (myclass->parent_phases.exit) {
            myclass->parent_phases.exit(obj, type);
        }
        /* clear an IO */
        qemu_set_irq(mydev->irq, 0);
    }

    typedef struct MyDevClass {
        MyParentClass parent_class;
        /* to store eventual parent reset methods */
        ResettablePhases parent_phases;
    } MyDevClass;

    static void mydev_class_init(ObjectClass *class, void *data)
    {
        MyDevClass *myclass = MYDEV_CLASS(class);
        ResettableClass *rc = RESETTABLE_CLASS(class);
        resettable_class_set_parent_phases(rc,
                                           mydev_reset_enter,
                                           mydev_reset_hold,
                                           mydev_reset_exit,
                                           &myclass->parent_phases);
    }

In the above example, we override all three phases. It is possible to override
only some of them by passing NULL instead of a function pointer to
``resettable_class_set_parent_phases()``. For example, the following will
only override the *enter* phase and leave *hold* and *exit* untouched::

    resettable_class_set_parent_phases(rc, mydev_reset_enter, NULL, NULL,
                                       &myclass->parent_phases);

This is equivalent to providing a trivial implementation of the hold and exit
phases which does nothing but call the parent class's implementation of the
phase.

Polling the reset state
.......................

Resettable interface provides the ``resettable_is_in_reset()`` function.
This function returns true if the object parameter is currently under reset.

An object is under reset from the beginning of the *enter* phase (before
either its children or its own enter method is called) to the *exit*
phase. During *enter* and *hold* phase only, the function will return that the
object is in reset. The state is changed after the *exit* is propagated to
its children and just before calling the object's own *exit* method.

This function may be used if the object behavior has to be adapted
while in reset state. For example if a device has an irq input,
it will probably need to ignore it while in reset; then it can for
example check the reset state at the beginning of the irq callback.

Note that until migration of the reset state is supported, an object
should not be left in reset. So apart from being currently executing
one of the reset phases, the only cases when this function will return
true is if an external interaction (like changing an io) is made during
*hold* or *exit* phase of another object in the same reset group.

Helpers ``device_is_in_reset()`` and ``bus_is_in_reset()`` are also provided
for devices and buses and should be preferred.


Base class handling of reset
----------------------------

This section documents parts of the reset mechanism that you only need to know
about if you are extending it to work with a new base class other than
DeviceClass or BusClass, or maintaining the existing code in those classes. Most
people can ignore it.

Methods to implement
....................

There are two other methods that need to exist in a class implementing the
interface: ``get_state()`` and ``child_foreach()``.

``get_state()`` is simple. *resettable* is an interface and, as a consequence,
does not have any class state structure. But in order to factorize the code, we
need one. This method must return a pointer to ``ResettableState`` structure.
The structure must be allocated by the base class; preferably it should be
located inside the object instance structure.

``child_foreach()`` is more complex. It should execute the given callback on
every reset child of the given resettable object. All children must be
resettable too. Additional parameters (a reset type and an opaque pointer) must
be passed to the callback too.

In ``DeviceClass`` and ``BusClass`` the ``ResettableState`` is located in the
``DeviceState`` and ``BusState`` structures. ``child_foreach()`` is implemented
to follow the bus hierarchy; for a bus, it calls the function on every child
device; for a device, it calls the function on every bus child. When we reset
the main system bus, we reset the whole machine bus tree.

Changing a resettable parent
............................

One thing which should be taken care of by the base class is handling reset
hierarchy changes.

The reset hierarchy is supposed to be static and built during machine creation.
But there are actually some exceptions. To cope with this, the resettable API
provides ``resettable_change_parent()``. This function allows to set, update or
remove the parent of a resettable object after machine creation is done. As
parameters, it takes the object being moved, the old parent if any and the new
parent if any.

This function can be used at any time when not in a reset operation. During
a reset operation it must be used only in *hold* phase. Using it in *enter* or
*exit* phase is an error.
Also it should not be used during machine creation, although it is harmless to
do so: the function is a no-op as long as old and new parent are NULL or not
in reset.

There is currently 2 cases where this function is used:

1. *device hotplug*; it means a new device is introduced on a live bus.

2. *hot bus change*; it means an existing live device is added, moved or
   removed in the bus hierarchy. At the moment, it occurs only in the raspi
   machines for changing the sdbus used by sd card.

Reset of the complete system
----------------------------

Reset of the complete system is a little complicated. The typical
flow is:

1. Code which wishes to reset the entire system does so by calling
   ``qemu_system_reset_request()``. This schedules a reset, but the
   reset will happen asynchronously after the function returns.
   That makes this safe to call from, for example, device models.

2. The function which is called to make the reset happen is
   ``qemu_system_reset()``. Generally only core system code should
   call this directly.

3. ``qemu_system_reset()`` calls the ``MachineClass::reset`` method of
   the current machine, if it has one. That method must call
   ``qemu_devices_reset()``. If the machine has no reset method,
   ``qemu_system_reset()`` calls ``qemu_devices_reset()`` directly.

4. ``qemu_devices_reset()`` performs a reset of the system, using
   the three-phase mechanism listed above. It resets all objects
   that were registered with it using ``qemu_register_resettable()``.
   It also calls all the functions registered with it using
   ``qemu_register_reset()``. Those functions are called during the
   "hold" phase of this reset.

5. The most important object that this reset resets is the
   'sysbus' bus. The sysbus bus is the root of the qbus tree. This
   means that all devices on the sysbus are reset, and all their
   child buses, and all the devices on those child buses.

6. Devices which are not on the qbus tree are *not* automatically
   reset! (The most obvious example of this is CPU objects, but
   anything that directly inherits from ``TYPE_OBJECT`` or ``TYPE_DEVICE``
   rather than from ``TYPE_SYS_BUS_DEVICE`` or some other plugs-into-a-bus
   type will be in this category.) You need to therefore arrange for these
   to be reset in some other way (e.g. using ``qemu_register_resettable()``
   or ``qemu_register_reset()``).
