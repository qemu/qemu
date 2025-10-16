virtio sound
============

This document explains the setup and usage of the Virtio sound device.
The Virtio sound device is a paravirtualized sound card device.

Linux kernel support
--------------------

Virtio sound requires a guest Linux kernel built with the
``CONFIG_SND_VIRTIO`` option.

Description
-----------

Virtio sound implements capture and playback from inside a guest using the
configured audio backend of the host machine.

Device properties
-----------------

The Virtio sound device can be configured with the following properties:

 * ``jacks`` number of physical jacks (Unimplemented).
 * ``streams`` number of PCM streams. At the moment, no stream configuration is supported: the first one will always be a playback stream, an optional second will always be a capture stream. Adding more will cycle stream directions from playback to capture.
 * ``chmaps`` number of channel maps (Unimplemented).

All streams are stereo and have the default channel positions ``Front left, right``.

Examples
--------

Add an audio device and an audio backend at once with ``-audio`` and ``model=virtio``:

 * pulseaudio: ``-audio driver=pa,model=virtio``
   or ``-audio driver=pa,model=virtio,server=/run/user/1000/pulse/native``
 * sdl: ``-audio driver=sdl,model=virtio``
 * coreaudio: ``-audio driver=coreaudio,model=virtio``

etc.

To specifically add virtualized sound devices, you have to specify a PCI device
and an audio backend listed with ``-audio driver=help`` that works on your host
machine, e.g.:

::

  -device virtio-sound-pci,audiodev=my_audiodev \
  -audiodev alsa,id=my_audiodev
