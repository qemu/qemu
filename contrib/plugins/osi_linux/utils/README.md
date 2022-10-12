Utilities for generating Kernelinfo files
======

There are two approaches for generating a valid kernelinfo profile for OSI:
1) By building a kernel module and loading it inside a guest
2) By using gdb to pull information out of a kernel built with debug symbols.


To build a kernel module inside the guest, see the code and instructions in `kernelinfo/` (Kernel >2.4) or `kernelinfo24/` (Kernel <= 2.4). Note that when you insert the kernel module, getting a permision denied error is expected behavior - your output will still end up in dmesg. Trim this output between `--KERNELINFO-BEGIN--` and `--KERNELINFO-END``. You'll need to generate a name for the kernelinfo at the top of the ouptut, something like `[linux:3.1.3:64]`

To build a kernelinfo profile with GDB see `kernelinfo_gdb/`. This will produce a file and attempt to automatiaclly name the profile.
