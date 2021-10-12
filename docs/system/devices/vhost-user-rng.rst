QEMU vhost-user-rng - RNG emulation
===================================

Background
----------

What follows builds on the material presented in vhost-user.rst - it should
be reviewed before moving forward with the content in this file.

Description
-----------

The vhost-user-rng device implementation was designed to work with a random
number generator daemon such as the one found in the vhost-device crate of
the rust-vmm project available on github [1].

[1]. https://github.com/rust-vmm/vhost-device

Examples
--------

The daemon should be started first:

::

  host# vhost-device-rng --socket-path=rng.sock -c 1 -m 512 -p 1000

The QEMU invocation needs to create a chardev socket the device can
use to communicate as well as share the guests memory over a memfd.

::

  host# qemu-system								\
      -chardev socket,path=$(PATH)/rng.sock,id=rng0				\
      -device vhost-user-rng-pci,chardev=rng0					\
      -m 4096 									\
      -object memory-backend-file,id=mem,size=4G,mem-path=/dev/shm,share=on	\
      -numa node,memdev=mem							\
      ...
