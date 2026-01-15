..
   Copyright (c) 2016, Xilinx Inc.

   This work is licensed under the terms of the GNU GPL, version 2 or later.  See
   the COPYING file in the top-level directory.

Generic Loader
--------------

The 'loader' device allows the user to load multiple images or values into
QEMU at startup.

Loading Data into Memory Values
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
The loader device allows memory values to be set from the command line. This
can be done by following the syntax below::

   -device loader,addr=<addr>,data=<data>,data-len=<data-len> \
                   [,data-be=<data-be>][,cpu-num=<cpu-num>]

``<addr>``
  The address to store the data in.

  Note that as usual with QEMU numeric option values, the default is to
  treat the argument as decimal.  To specify a value in hex, prefix it
  with '0x'.

``<data>``
  The value to be written to the address. The maximum size of the data
  is 8 bytes.

``<data-len>``
  The length of the data in bytes. This argument must be included if
  the data argument is.

``<data-be>``
  Set to true if the data to be stored on the guest should be written
  as big endian data. The default is to write little endian data.

``<cpu-num>``
  The number of the CPU's address space where the data should be
  loaded. If not specified the address space of the first CPU is used.


An example of loading value 0x8000000e to address 0xfd1a0104 is::

    -device loader,addr=0xfd1a0104,data=0x8000000e,data-len=4

Setting a CPU's Program Counter
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The loader device allows the CPU's PC to be set from the command line. This
can be done by following the syntax below::

     -device loader,addr=<addr>,cpu-num=<cpu-num>

``<addr>``
  The value to use as the CPU's PC.

  Note that as usual with QEMU numeric option values, the default is to
  treat the argument as decimal.  To specify a value in hex, prefix it
  with '0x'.

``<cpu-num>``
  The number of the CPU whose PC should be set to the specified value.

An example of setting CPU 0's PC to 0x8000 is::

    -device loader,addr=0x8000,cpu-num=0

Loading Files
^^^^^^^^^^^^^

The loader device also allows files to be loaded into memory. It can load ELF,
U-Boot, and Intel HEX executable formats as well as raw images.  The syntax is
shown below:

    -device loader,file=<file>[,addr=<addr>][,cpu-num=<cpu-num>][,force-raw=<raw>]

``<file>``
  A file to be loaded into memory

``<addr>``
  The memory address where the file should be loaded. This is required
  for raw images and ignored for non-raw files.

  Note that as usual with QEMU numeric option values, the default is to
  treat the argument as decimal.  To specify a value in hex, prefix it
  with '0x'.

``<cpu-num>``
  This specifies the CPU that should be used. This is an
  optional argument with two effects:

  * this CPU's address space is used to load the data
  * this CPU's PC will be set to the address where the raw file is loaded
    or the entry point specified in the executable format header

  If this option is not specified, then the data will be loaded via
  the address space of the first CPU, and no CPU will have its PC set.

  Note that there is currently no way to specify the address space to
  load the data without also causing that CPU's PC to be set.

  Since it sets the starting PC, this option should only be used for the boot
  image.

``<force-raw>``
  Setting 'force-raw=on' forces the file to be treated as a raw image.
  This can be used to load supported executable formats as if they
  were raw.


An example of loading an ELF file which CPU0 will boot is shown below::

    -device loader,file=./images/boot.elf,cpu-num=0
