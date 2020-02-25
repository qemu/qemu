..
   Copyright (C) 2017, Emilio G. Cota <cota@braap.org>
   Copyright (c) 2019, Linaro Limited
   Written by Emilio Cota and Alex Bennée

================
QEMU TCG Plugins
================

QEMU TCG plugins provide a way for users to run experiments taking
advantage of the total system control emulation can have over a guest.
It provides a mechanism for plugins to subscribe to events during
translation and execution and optionally callback into the plugin
during these events. TCG plugins are unable to change the system state
only monitor it passively. However they can do this down to an
individual instruction granularity including potentially subscribing
to all load and store operations.

API Stability
=============

This is a new feature for QEMU and it does allow people to develop
out-of-tree plugins that can be dynamically linked into a running QEMU
process. However the project reserves the right to change or break the
API should it need to do so. The best way to avoid this is to submit
your plugin upstream so they can be updated if/when the API changes.

API versioning
--------------

All plugins need to declare a symbol which exports the plugin API
version they were built against. This can be done simply by::

  QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

The core code will refuse to load a plugin that doesn't export a
`qemu_plugin_version` symbol or if plugin version is outside of QEMU's
supported range of API versions.

Additionally the `qemu_info_t` structure which is passed to the
`qemu_plugin_install` method of a plugin will detail the minimum and
current API versions supported by QEMU. The API version will be
incremented if new APIs are added. The minimum API version will be
incremented if existing APIs are changed or removed.

Exposure of QEMU internals
--------------------------

The plugin architecture actively avoids leaking implementation details
about how QEMU's translation works to the plugins. While there are
conceptions such as translation time and translation blocks the
details are opaque to plugins. The plugin is able to query select
details of instructions and system configuration only through the
exported *qemu_plugin* functions.

Query Handle Lifetime
---------------------

Each callback provides an opaque anonymous information handle which
can usually be further queried to find out information about a
translation, instruction or operation. The handles themselves are only
valid during the lifetime of the callback so it is important that any
information that is needed is extracted during the callback and saved
by the plugin.

Usage
=====

The QEMU binary needs to be compiled for plugin support::

  configure --enable-plugins

Once built a program can be run with multiple plugins loaded each with
their own arguments::

  $QEMU $OTHER_QEMU_ARGS \
      -plugin tests/plugin/libhowvec.so,arg=inline,arg=hint \
      -plugin tests/plugin/libhotblocks.so

Arguments are plugin specific and can be used to modify their
behaviour. In this case the howvec plugin is being asked to use inline
ops to count and break down the hint instructions by type.

Plugin Life cycle
=================

First the plugin is loaded and the public qemu_plugin_install function
is called. The plugin will then register callbacks for various plugin
events. Generally plugins will register a handler for the *atexit*
if they want to dump a summary of collected information once the
program/system has finished running.

When a registered event occurs the plugin callback is invoked. The
callbacks may provide additional information. In the case of a
translation event the plugin has an option to enumerate the
instructions in a block of instructions and optionally register
callbacks to some or all instructions when they are executed.

There is also a facility to add an inline event where code to
increment a counter can be directly inlined with the translation.
Currently only a simple increment is supported. This is not atomic so
can miss counts. If you want absolute precision you should use a
callback which can then ensure atomicity itself.

Finally when QEMU exits all the registered *atexit* callbacks are
invoked.

Internals
=========

Locking
-------

We have to ensure we cannot deadlock, particularly under MTTCG. For
this we acquire a lock when called from plugin code. We also keep the
list of callbacks under RCU so that we do not have to hold the lock
when calling the callbacks. This is also for performance, since some
callbacks (e.g. memory access callbacks) might be called very
frequently.

  * A consequence of this is that we keep our own list of CPUs, so that
    we do not have to worry about locking order wrt cpu_list_lock.
  * Use a recursive lock, since we can get registration calls from
    callbacks.

As a result registering/unregistering callbacks is "slow", since it
takes a lock. But this is very infrequent; we want performance when
calling (or not calling) callbacks, not when registering them. Using
RCU is great for this.

We support the uninstallation of a plugin at any time (e.g. from
plugin callbacks). This allows plugins to remove themselves if they no
longer want to instrument the code. This operation is asynchronous
which means callbacks may still occur after the uninstall operation is
requested. The plugin isn't completely uninstalled until the safe work
has executed while all vCPUs are quiescent.
