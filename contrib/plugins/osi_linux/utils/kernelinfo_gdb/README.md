# Kernel Information Module via gdb

First. Go read the normal kernelinfo [readme](https://github.com/panda-re/panda/blob/master/panda/plugins/osi_linux/utils/kernelinfo/README.md).

## Requirements

- GDB 8 or above. This is needed for the GDB API to support `gdb.execute(to_string=True)`.
- Python 3.6 or above. This is to support fstrings.
- A kernel vmlinux file for linux 3.0.0 or later that is *not stripped and has debug symbols*.

## Where does this apply?

Kernels with debug symbols. Likely one that you built. If it's stripped go back to the other method.

Example: `vmlinux: ELF 32-bit MSB executable, MIPS, MIPS32 rel2 version 1 (SYSV), statically linked, BuildID[sha1]=181ca40a44bef701cf0559b185180053a152029d, with debug_info, not stripped`


## How does this work?

This crux of this is python script run inside of gdb to gather DWARF symbols.

That script creates a command `kernel_info` which runs a bunch of GDB commands to gather information from the DWARF symbols and output it.
 
## How do I use it?

Run `run.sh` with a vmlinux and an output file.
`./run.sh vmlinux file.out`

Alternatively, 
- start up gdb on the `vmlinux` file: `gdb ./vmlinux`
- load our python script: `source extract_kernelinfo.py`
- run our python script: `kernel_info output.file`
- quit: `q`


