# Kernel Information Module

In order to be able to examine the structure of the running kernel,
several offsets within the data structures used by the kernel have to
be known.
E.g. what is the offset of ``pid`` within ``struct task_struct``?

These offsets are dependent on the kernel version *and* the flags used to
compile it. Some of them could be guessed using heuristics. 
A more robust approach is retrieving them by querying the running kernel.

The ``kernelinfo.c`` module in this directory implements this approach.
After compiling and inserting the module into the kernel, the required
offsets are printed in the kernel log.
From there, they have to be copied in a ``kernelinfo.conf`` file on the
host, for further use.
Note that the module initialization will (intentionally) always fail. But
the required offset will have been printed in the kernel log before that.

To copy the source for the module in your VM in order to compile and run
it, you can `git clone` the PANDA repository in order to get this directory, 
you can download a snapshot of this directory from July 2020:
```
wget https://panda-re.mit.edu/kernelinfos/generator_july20.tgz
```
or you can try to use SVN to download this directory (seems to no longer work as of 2020):
```
svn export https://github.com/panda-re/panda/trunk/panda/plugins/osi_linux/utils/kernelinfo
```

To compile the module, you will need to have installed the appropriate
linux-headers package.

## Kernels from v3.3-rc1 onwards
[Linux v3.3-rc1][linux-v3.3-rc1] introduced some changes in the interface
exported by VFS. Specifically, most of the members of `struct vfsmount`
are no longer exposed by the linux headers.
Among them are the pointers to `mnt_parent` and `mnt_mountpoint` which 
are used by osi\_linux for reversing file descriptors.
These members now "live" in `struct mount`, defined in the internal VFS
header `fs/mount.h`.
In order to make the kernelinfo module self-contained and avoid requiring
to install the full linux sources, we include `fs/mount.h` for different
kernels in the [ksrc](./ksrc) directory. If what is included doesn't work
for your kernel, feel free to add what is missing, update the sources and
send a PR to upstream.

[linux-v3.3-rc1]: https://github.com/torvalds/linux/releases/tag/v3.3-rc1

### Here be dragons! `__randomize_layout`
As of [v4.13-rc2](https://github.com/torvalds/linux/releases/tag/v4.13-rc2), `struct mount` (in `mount.h`) includes [the `__randomize_layout` annotation](https://lwn.net/Articles/723997/). We don't *think* this breaks anything, but it might be the case that the offsets are not transferrable between different builds of the same kernel.
