Modelling a clock tree in QEMU
==============================

What are clocks?
----------------

Clocks are QOM objects developed for the purpose of modelling the
distribution of clocks in QEMU.

They allow us to model the clock distribution of a platform and detect
configuration errors in the clock tree such as badly configured PLL, clock
source selection or disabled clock.

The object is *Clock* and its QOM name is ``clock`` (in C code, the macro
``TYPE_CLOCK``).

Clocks are typically used with devices where they are used to model inputs
and outputs. They are created in a similar way to GPIOs. Inputs and outputs
of different devices can be connected together.

In these cases a Clock object is a child of a Device object, but this
is not a requirement. Clocks can be independent of devices. For
example it is possible to create a clock outside of any device to
model the main clock source of a machine.

Here is an example of clocks::

    +---------+      +----------------------+   +--------------+
    | Clock 1 |      |       Device B       |   |   Device C   |
    |         |      | +-------+  +-------+ |   | +-------+    |
    |         |>>-+-->>|Clock 2|  |Clock 3|>>--->>|Clock 6|    |
    +---------+   |  | | (in)  |  | (out) | |   | | (in)  |    |
                  |  | +-------+  +-------+ |   | +-------+    |
                  |  |            +-------+ |   +--------------+
                  |  |            |Clock 4|>>
                  |  |            | (out) | |   +--------------+
                  |  |            +-------+ |   |   Device D   |
                  |  |            +-------+ |   | +-------+    |
                  |  |            |Clock 5|>>--->>|Clock 7|    |
                  |  |            | (out) | |   | | (in)  |    |
                  |  |            +-------+ |   | +-------+    |
                  |  +----------------------+   |              |
                  |                             | +-------+    |
                  +----------------------------->>|Clock 8|    |
                                                | | (in)  |    |
                                                | +-------+    |
                                                +--------------+

Clocks are defined in the ``include/hw/clock.h`` header and device
related functions are defined in the ``include/hw/qdev-clock.h``
header.

The clock state
---------------

The state of a clock is its period; it is stored as an integer
representing it in units of 2 :sup:`-32` ns. The special value of 0 is used to
represent the clock being inactive or gated. The clocks do not model
the signal itself (pin toggling) or other properties such as the duty
cycle.

All clocks contain this state: outputs as well as inputs. This allows
the current period of a clock to be fetched at any time. When a clock
is updated, the value is immediately propagated to all connected
clocks in the tree.

To ease interaction with clocks, helpers with a unit suffix are defined for
every clock state setter or getter. The suffixes are:

- ``_ns`` for handling periods in nanoseconds
- ``_hz`` for handling frequencies in hertz

The 0 period value is converted to 0 in hertz and vice versa. 0 always means
that the clock is disabled.

Adding a new clock
------------------

Adding clocks to a device must be done during the init method of the Device
instance.

To add an input clock to a device, the function ``qdev_init_clock_in()``
must be used.  It takes the name, a callback, an opaque parameter
for the callback and a mask of events when the callback should be
called (this will be explained in a following section).
Output is simpler; only the name is required. Typically::

    qdev_init_clock_in(DEVICE(dev), "clk_in", clk_in_callback, dev, ClockUpdate);
    qdev_init_clock_out(DEVICE(dev), "clk_out");

Both functions return the created Clock pointer, which should be saved in the
device's state structure for further use.

These objects will be automatically deleted by the QOM reference mechanism.

Note that it is possible to create a static array describing clock inputs and
outputs. The function ``qdev_init_clocks()`` must be called with the array as
parameter to initialize the clocks: it has the same behaviour as calling the
``qdev_init_clock_in/out()`` for each clock in the array. To ease the array
construction, some macros are defined in ``include/hw/qdev-clock.h``.
As an example, the following creates 2 clocks to a device: one input and one
output.

.. code-block:: c

    /* device structure containing pointers to the clock objects */
    typedef struct MyDeviceState {
        DeviceState parent_obj;
        Clock *clk_in;
        Clock *clk_out;
    } MyDeviceState;

    /*
     * callback for the input clock (see "Callback on input clock
     * change" section below for more information).
     */
    static void clk_in_callback(void *opaque, ClockEvent event);

    /*
     * static array describing clocks:
     * + a clock input named "clk_in", whose pointer is stored in
     *   the clk_in field of a MyDeviceState structure with callback
     *   clk_in_callback.
     * + a clock output named "clk_out" whose pointer is stored in
     *   the clk_out field of a MyDeviceState structure.
     */
    static const ClockPortInitArray mydev_clocks = {
        QDEV_CLOCK_IN(MyDeviceState, clk_in, clk_in_callback, ClockUpdate),
        QDEV_CLOCK_OUT(MyDeviceState, clk_out),
        QDEV_CLOCK_END
    };

    /* device initialization function */
    static void mydev_init(Object *obj)
    {
        /* cast to MyDeviceState */
        MyDeviceState *mydev = MYDEVICE(obj);
        /* create and fill the pointer fields in the MyDeviceState */
        qdev_init_clocks(mydev, mydev_clocks);
        [...]
    }

An alternative way to create a clock is to simply call
``object_new(TYPE_CLOCK)``. In that case the clock will neither be an
input nor an output of a device. After the whole QOM hierarchy of the
clock has been set ``clock_setup_canonical_path()`` should be called.

At creation, the period of the clock is 0: the clock is disabled. You can
change it using ``clock_set_ns()`` or ``clock_set_hz()``.

Note that if you are creating a clock with a fixed period which will never
change (for example the main clock source of a board), then you'll have
nothing else to do. This value will be propagated to other clocks when
connecting the clocks together and devices will fetch the right value during
the first reset.

Clock callbacks
---------------

You can give a clock a callback function in several ways:

 * by passing it as an argument to ``qdev_init_clock_in()``
 * as an argument to the ``QDEV_CLOCK_IN()`` macro initializing an
   array to be passed to ``qdev_init_clocks()``
 * by directly calling the ``clock_set_callback()`` function

The callback function must be of this type:

.. code-block:: c

   typedef void ClockCallback(void *opaque, ClockEvent event);

The ``opaque`` argument is the pointer passed to ``qdev_init_clock_in()``
or ``clock_set_callback()``; for ``qdev_init_clocks()`` it is the
``dev`` device pointer.

The ``event`` argument specifies why the callback has been called.
When you register the callback you specify a mask of ClockEvent values
that you are interested in. The callback will only be called for those
events.

The events currently supported are:

 * ``ClockPreUpdate`` : called when the input clock's period is about to
   update. This is useful if the device needs to do some action for
   which it needs to know the old value of the clock period. During
   this callback, Clock API functions like ``clock_get()`` or
   ``clock_ticks_to_ns()`` will use the old period.
 * ``ClockUpdate`` : called after the input clock's period has changed.
   During this callback, Clock API functions like ``clock_ticks_to_ns()``
   will use the new period.

Note that a clock only has one callback: it is not possible to register
different functions for different events. You must register a single
callback which listens for all of the events you are interested in,
and use the ``event`` argument to identify which event has happened.

Retrieving clocks from a device
-------------------------------

``qdev_get_clock_in()`` and ``dev_get_clock_out()`` are available to
get the clock inputs or outputs of a device. For example:

.. code-block:: c

   Clock *clk = qdev_get_clock_in(DEVICE(mydev), "clk_in");

or:

.. code-block:: c

   Clock *clk = qdev_get_clock_out(DEVICE(mydev), "clk_out");

Connecting two clocks together
------------------------------

To connect two clocks together, use the ``clock_set_source()`` function.
Given two clocks ``clk1``, and ``clk2``, ``clock_set_source(clk2, clk1);``
configures ``clk2`` to follow the ``clk1`` period changes. Every time ``clk1``
is updated, ``clk2`` will be updated too.

When connecting clock between devices, prefer using the
``qdev_connect_clock_in()`` function to set the source of an input
device clock.  For example, to connect the input clock ``clk2`` of
``devB`` to the output clock ``clk1`` of ``devA``, do:

.. code-block:: c

    qdev_connect_clock_in(devB, "clk2", qdev_get_clock_out(devA, "clk1"))

We used ``qdev_get_clock_out()`` above, but any clock can drive an
input clock, even another input clock. The following diagram shows
some examples of connections. Note also that a clock can drive several
other clocks.

::

  +------------+  +--------------------------------------------------+
  |  Device A  |  |                   Device B                       |
  |            |  |               +---------------------+            |
  |            |  |               |       Device C      |            |
  |  +-------+ |  | +-------+     | +-------+ +-------+ |  +-------+ |
  |  |Clock 1|>>-->>|Clock 2|>>+-->>|Clock 3| |Clock 5|>>>>|Clock 6|>>
  |  | (out) | |  | | (in)  |  |  | | (in)  | | (out) | |  | (out) | |
  |  +-------+ |  | +-------+  |  | +-------+ +-------+ |  +-------+ |
  +------------+  |            |  +---------------------+            |
                  |            |                                     |
                  |            |  +--------------+                   |
                  |            |  |   Device D   |                   |
                  |            |  | +-------+    |                   |
                  |            +-->>|Clock 4|    |                   |
                  |               | | (in)  |    |                   |
                  |               | +-------+    |                   |
                  |               +--------------+                   |
                  +--------------------------------------------------+

In the above example, when *Clock 1* is updated by *Device A*, three
clocks get the new clock period value: *Clock 2*, *Clock 3* and *Clock 4*.

It is not possible to disconnect a clock or to change the clock connection
after it is connected.

Clock multiplier and divider settings
-------------------------------------

By default, when clocks are connected together, the child
clocks run with the same period as their source (parent) clock.
The Clock API supports a built-in period multiplier/divider
mechanism so you can configure a clock to make its children
run at a different period from its own. If you call the
``clock_set_mul_div()`` function you can specify the clock's
multiplier and divider values. The children of that clock
will all run with a period of ``parent_period * multiplier / divider``.
For instance, if the clock has a frequency of 8MHz and you set its
multiplier to 2 and its divider to 3, the child clocks will run
at 12MHz.

You can change the multiplier and divider of a clock at runtime,
so you can use this to model clock controller devices which
have guest-programmable frequency multipliers or dividers.

Similarly to ``clock_set()``, ``clock_set_mul_div()`` returns ``true`` if
the clock state was modified; that is, if the multiplier or the diviser
or both were changed by the call.

Note that ``clock_set_mul_div()`` does not automatically call
``clock_propagate()``. If you make a runtime change to the
multiplier or divider you must call clock_propagate() yourself.

Unconnected input clocks
------------------------

A newly created input clock is disabled (period of 0). This means the
clock will be considered as disabled until the period is updated. If
the clock remains unconnected it will always keep its initial value
of 0. If this is not the desired behaviour, ``clock_set()``,
``clock_set_ns()`` or ``clock_set_hz()`` should be called on the Clock
object during device instance init. For example:

.. code-block:: c

    clk = qdev_init_clock_in(DEVICE(dev), "clk-in", clk_in_callback,
                             dev, ClockUpdate);
    /* set initial value to 10ns / 100MHz */
    clock_set_ns(clk, 10);

To enforce that the clock is wired up by the board code, you can
call ``clock_has_source()`` in your device's realize method:

.. code-block:: c

   if (!clock_has_source(s->clk)) {
       error_setg(errp, "MyDevice: clk input must be connected");
       return;
   }

Note that this only checks that the clock has been wired up; it is
still possible that the output clock connected to it is disabled
or has not yet been configured, in which case the period will be
zero. You should use the clock callback to find out when the clock
period changes.

Fetching clock frequency/period
-------------------------------

To get the current state of a clock, use the functions ``clock_get()``
or ``clock_get_hz()``.

``clock_get()`` returns the period of the clock in its fully precise
internal representation, as an unsigned 64-bit integer in units of
2^-32 nanoseconds. (For many purposes ``clock_ticks_to_ns()`` will
be more convenient; see the section below on expiry deadlines.)

``clock_get_hz()`` returns the frequency of the clock, rounded to the
next lowest integer. This implies some inaccuracy due to the rounding,
so be cautious about using it in calculations.

It is also possible to register a callback on clock frequency changes.
Here is an example, which assumes that ``clock_callback`` has been
specified as the callback for the ``ClockUpdate`` event:

.. code-block:: c

    void clock_callback(void *opaque, ClockEvent event) {
        MyDeviceState *s = (MyDeviceState *) opaque;
        /*
         * 'opaque' is the argument passed to qdev_init_clock_in();
         * usually this will be the device state pointer.
         */

        /* do something with the new period */
        fprintf(stdout, "device new period is %" PRIu64 "* 2^-32 ns\n",
                        clock_get(dev->my_clk_input));
    }

If you are only interested in the frequency for displaying it to
humans (for instance in debugging), use ``clock_display_freq()``,
which returns a prettified string-representation, e.g. "33.3 MHz".
The caller must free the string with g_free() after use.

It's also possible to retrieve the clock period from a QTest by
accessing QOM property ``qtest-clock-period`` using a QMP command.
This property is only present when the device is being run under
the ``qtest`` accelerator; it is not available when QEMU is
being run normally.

Calculating expiry deadlines
----------------------------

A commonly required operation for a clock is to calculate how long
it will take for the clock to tick N times; this can then be used
to set a timer expiry deadline. Use the function ``clock_ticks_to_ns()``,
which takes an unsigned 64-bit count of ticks and returns the length
of time in nanoseconds required for the clock to tick that many times.

It is important not to try to calculate expiry deadlines using a
shortcut like multiplying a "period of clock in nanoseconds" value
by the tick count, because clocks can have periods which are not a
whole number of nanoseconds, and the accumulated error in the
multiplication can be significant.

For a clock with a very long period and a large number of ticks,
the result of this function could in theory be too large to fit in
a 64-bit value. To avoid overflow in this case, ``clock_ticks_to_ns()``
saturates the result to INT64_MAX (because this is the largest valid
input to the QEMUTimer APIs). Since INT64_MAX nanoseconds is almost
300 years, anything with an expiry later than that is in the "will
never happen" category. Callers of ``clock_ticks_to_ns()`` should
therefore generally not special-case the possibility of a saturated
result but just allow the timer to be set to that far-future value.
(If you are performing further calculations on the returned value
rather than simply passing it to a QEMUTimer function like
``timer_mod_ns()`` then you should be careful to avoid overflow
in those calculations, of course.)

Obtaining tick counts
---------------------

For calculations where you need to know the number of ticks in
a given duration, use ``clock_ns_to_ticks()``. This function handles
possible non-whole-number-of-nanoseconds periods and avoids
potential rounding errors. It will return '0' if the clock is stopped
(i.e. it has period zero). If the inputs imply a tick count that
overflows a 64-bit value (a very long duration for a clock with a
very short period) the output value is truncated, so effectively
the 64-bit output wraps around.

Changing a clock period
-----------------------

A device can change its outputs using the ``clock_update()``,
``clock_update_ns()`` or ``clock_update_hz()`` function. It will trigger
updates on every connected input.

For example, let's say that we have an output clock *clkout* and we
have a pointer to it in the device state because we did the following
in init phase:

.. code-block:: c

   dev->clkout = qdev_init_clock_out(DEVICE(dev), "clkout");

Then at any time (apart from the cases listed below), it is possible to
change the clock value by doing:

.. code-block:: c

   clock_update_hz(dev->clkout, 1000 * 1000 * 1000); /* 1GHz */

Because updating a clock may trigger any side effects through
connected clocks and their callbacks, this operation must be done
while holding the qemu io lock.

For the same reason, one can update clocks only when it is allowed to have
side effects on other objects. In consequence, it is forbidden:

* during migration,
* and in the enter phase of reset.

Note that calling ``clock_update[_ns|_hz]()`` is equivalent to calling
``clock_set[_ns|_hz]()`` (with the same arguments) then
``clock_propagate()`` on the clock. Thus, setting the clock value can
be separated from triggering the side-effects. This is often required
to factorize code to handle reset and migration in devices.

Aliasing clocks
---------------

Sometimes, one needs to forward, or inherit, a clock from another
device.  Typically, when doing device composition, a device might
expose a sub-device's clock without interfering with it.  The function
``qdev_alias_clock()`` can be used to achieve this behaviour. Note
that it is possible to expose the clock under a different name.
``qdev_alias_clock()`` works for both input and output clocks.

For example, if device B is a child of device A,
``device_a_instance_init()`` may do something like this:

.. code-block:: c

    void device_a_instance_init(Object *obj)
    {
        AState *A = DEVICE_A(obj);
        BState *B;
        /* create object B as child of A */
        [...]
        qdev_alias_clock(B, "clk", A, "b_clk");
        /*
         * Now A has a clock "b_clk" which is an alias to
         * the clock "clk" of its child B.
         */
    }

This function does not return any clock object. The new clock has the
same direction (input or output) as the original one. This function
only adds a link to the existing clock. In the above example, object B
remains the only object allowed to use the clock and device A must not
try to change the clock period or set a callback to the clock. This
diagram describes the example with an input clock::

    +--------------------------+
    |        Device A          |
    |         +--------------+ |
    |         |   Device B   | |
    |         | +-------+    | |
    >>"b_clk">>>| "clk" |    | |
    |  (in)   | |  (in) |    | |
    |         | +-------+    | |
    |         +--------------+ |
    +--------------------------+

Migration
---------

Clock state is not migrated automatically. Every device must handle its
clock migration. Alias clocks must not be migrated.

To ensure clock states are restored correctly during migration, there
are two solutions.

Clock states can be migrated by adding an entry into the device
vmstate description. You should use the ``VMSTATE_CLOCK`` macro for this.
This is typically used to migrate an input clock state. For example:

.. code-block:: c

    MyDeviceState {
        DeviceState parent_obj;
        [...] /* some fields */
        Clock *clk;
    };

    VMStateDescription my_device_vmstate = {
        .name = "my_device",
        .fields = (const VMStateField[]) {
            [...], /* other migrated fields */
            VMSTATE_CLOCK(clk, MyDeviceState),
            VMSTATE_END_OF_LIST()
        }
    };

The second solution is to restore the clock state using information already
at our disposal. This can be used to restore output clock states using the
device state. The functions ``clock_set[_ns|_hz]()`` can be used during the
``post_load()`` migration callback.

When adding clock support to an existing device, if you care about
migration compatibility you will need to be careful, as simply adding
a ``VMSTATE_CLOCK()`` line will break compatibility. Instead, you can
put the ``VMSTATE_CLOCK()`` line into a vmstate subsection with a
suitable ``needed`` function, and use ``clock_set()`` in a
``pre_load()`` function to set the default value that will be used if
the source virtual machine in the migration does not send the clock
state.

Care should be taken not to use ``clock_update[_ns|_hz]()`` or
``clock_propagate()`` during the whole migration procedure because it
will trigger side effects to other devices in an unknown state.
