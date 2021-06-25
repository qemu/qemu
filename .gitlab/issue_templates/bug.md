<!--
This is the upstream QEMU issue tracker.

If you are able to, it will greatly facilitate bug triage if you attempt
to reproduce the problem with the latest qemu.git master built from
source. See https://www.qemu.org/download/#source for instructions on
how to do this.

QEMU generally supports the last two releases advertised on
https://www.qemu.org/. Problems with distro-packaged versions of QEMU
older than this should be reported to the distribution instead.

See https://www.qemu.org/contribute/report-a-bug/ for additional
guidance.

If this is a security issue, please consult
https://www.qemu.org/contribute/security-process/
-->

## Host environment
 - Operating system: (Windows 10 21H1, Fedora 34, etc.)
 - OS/kernel version: (For POSIX hosts, use `uname -a`)
 - Architecture: (x86, ARM, s390x, etc.)
 - QEMU flavor: (qemu-system-x86_64, qemu-aarch64, qemu-img, etc.)
 - QEMU version: (e.g. `qemu-system-x86_64 --version`)
 - QEMU command line:
   <!--
   Give the smallest, complete command line that exhibits the problem.

   If you are using libvirt, virsh, or vmm, you can likely find the QEMU
   command line arguments in /var/log/libvirt/qemu/$GUEST.log.
   -->
   ```
   ./qemu-system-x86_64 -M q35 -m 4096 -enable-kvm -hda fedora32.qcow2
   ```

## Emulated/Virtualized environment
 - Operating system: (Windows 10 21H1, Fedora 34, etc.)
 - OS/kernel version: (For POSIX guests, use `uname -a`.)
 - Architecture: (x86, ARM, s390x, etc.)


## Description of problem
<!-- Describe the problem, including any error/crash messages seen. -->


## Steps to reproduce
1.
2.
3.


## Additional information

<!--
Attach logs, stack traces, screenshots, etc. Compress the files if necessary.
If using libvirt, libvirt logs and XML domain information may be relevant.
-->

<!--
The line below ensures that proper tags are added to the issue.
Please do not remove it.
-->
/label ~"kind::Bug"
