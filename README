         QEMU README
         ===========

QEMU is a generic and open source machine & userspace emulator and
virtualizer.

QEMU is capable of emulating a complete machine in software without any
need for hardware virtualization support. By using dynamic translation,
it achieves very good performance. QEMU can also integrate with the Xen
and KVM hypervisors to provide emulated hardware while allowing the
hypervisor to manage the CPU. With hypervisor support, QEMU can achieve
near native performance for CPUs. When QEMU emulates CPUs directly it is
capable of running operating systems made for one machine (e.g. an ARMv7
board) on a different machine (e.g. an x86_64 PC board).

QEMU is also capable of providing userspace API virtualization for Linux
and BSD kernel interfaces. This allows binaries compiled against one
architecture ABI (e.g. the Linux PPC64 ABI) to be run on a host using a
different architecture ABI (e.g. the Linux x86_64 ABI). This does not
involve any hardware emulation, simply CPU and syscall emulation.

QEMU aims to fit into a variety of use cases. It can be invoked directly
by users wishing to have full control over its behaviour and settings.
It also aims to facilitate integration into higher level management
layers, by providing a stable command line interface and monitor API.
It is commonly invoked indirectly via the libvirt library when using
open source applications such as oVirt, OpenStack and virt-manager.

QEMU as a whole is released under the GNU General Public License,
version 2. For full licensing details, consult the LICENSE file.


Building
========

QEMU is multi-platform software intended to be buildable on all modern
Linux platforms, OS-X, Win32 (via the Mingw64 toolchain) and a variety
of other UNIX targets. The simple steps to build QEMU are:

  mkdir build
  cd build
  ../configure
  make

Complete details of the process for building and configuring QEMU for
all supported host platforms can be found in the qemu-tech.html file.
Additional information can also be found online via the QEMU website:

  http://qemu-project.org/Hosts/Linux
  http://qemu-project.org/Hosts/W32


Submitting patches
==================

The QEMU source code is maintained under the GIT version control system.

   git clone git://git.qemu-project.org/qemu.git

When submitting patches, the preferred approach is to use 'git
format-patch' and/or 'git send-email' to format & send the mail to the
qemu-devel@nongnu.org mailing list. All patches submitted must contain
a 'Signed-off-by' line from the author. Patches should follow the
guidelines set out in the HACKING and CODING_STYLE files.

Additional information on submitting patches can be found online via
the QEMU website

  http://qemu-project.org/Contribute/SubmitAPatch
  http://qemu-project.org/Contribute/TrivialPatches


Bug reporting
=============

The QEMU project uses Launchpad as its primary upstream bug tracker. Bugs
found when running code built from QEMU git or upstream released sources
should be reported via:

  https://bugs.launchpad.net/qemu/

If using QEMU via an operating system vendor pre-built binary package, it
is preferable to report bugs to the vendor's own bug tracker first. If
the bug is also known to affect latest upstream code, it can also be
reported via launchpad.

For additional information on bug reporting consult:

  http://qemu-project.org/Contribute/ReportABug


Contact
=======

The QEMU community can be contacted in a number of ways, with the two
main methods being email and IRC

 - qemu-devel@nongnu.org
   http://lists.nongnu.org/mailman/listinfo/qemu-devel
 - #qemu on irc.oftc.net

Information on additional methods of contacting the community can be
found online via the QEMU website:

  http://qemu-project.org/Contribute/StartHere

-- End
