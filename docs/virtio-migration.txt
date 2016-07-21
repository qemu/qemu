Virtio devices and migration
============================

Copyright 2015 IBM Corp.

This work is licensed under the terms of the GNU GPL, version 2 or later.  See
the COPYING file in the top-level directory.

Saving and restoring the state of virtio devices is a bit of a twisty maze,
for several reasons:
- state is distributed between several parts:
  - virtio core, for common fields like features, number of queues, ...
  - virtio transport (pci, ccw, ...), for the different proxy devices and
    transport specific state (msix vectors, indicators, ...)
  - virtio device (net, blk, ...), for the different device types and their
    state (mac address, request queue, ...)
- most fields are saved via the stream interface; subsequently, subsections
  have been added to make cross-version migration possible

This file attempts to document the current procedure and point out some
caveats.


Save state procedure
====================

virtio core               virtio transport          virtio device
-----------               ----------------          -------------

                                                    save() function registered
                                                    via VMState wrapper on
                                                    device class
virtio_save()                                       <----------
             ------>      save_config()
                          - save proxy device
                          - save transport-specific
                            device fields
- save common device
  fields
- save common virtqueue
  fields
             ------>      save_queue()
                          - save transport-specific
                            virtqueue fields
             ------>                               save_device()
                                                   - save device-specific
                                                     fields
- save subsections
  - device endianness,
    if changed from
    default endianness
  - 64 bit features, if
    any high feature bit
    is set
  - virtio-1 virtqueue
    fields, if VERSION_1
    is set


Load state procedure
====================

virtio core               virtio transport          virtio device
-----------               ----------------          -------------

                                                    load() function registered
                                                    via VMState wrapper on
                                                    device class
virtio_load()                                       <----------
             ------>      load_config()
                          - load proxy device
                          - load transport-specific
                            device fields
- load common device
  fields
- load common virtqueue
  fields
             ------>      load_queue()
                          - load transport-specific
                            virtqueue fields
- notify guest
             ------>                               load_device()
                                                   - load device-specific
                                                     fields
- load subsections
  - device endianness
  - 64 bit features
  - virtio-1 virtqueue
    fields
- sanitize endianness
- sanitize features
- virtqueue index sanity
  check
                                                   - feature-dependent setup


Implications of this setup
==========================

Devices need to be careful in their state processing during load: The
load_device() procedure is invoked by the core before subsections have
been loaded. Any code that depends on information transmitted in subsections
therefore has to be invoked in the device's load() function _after_
virtio_load() returned (like e.g. code depending on features).

Any extension of the state being migrated should be done in subsections
added to the core for compatibility reasons. If transport or device specific
state is added, core needs to invoke a callback from the new subsection.
