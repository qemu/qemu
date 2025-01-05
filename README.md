## Virtual Shell
A program to run an Unix shell command inside an virtual environment (assuming the disk image used is Linux).
### How It [Would] Work(s)
virtsh takes the iamge and emulates it using QEMU. However, because of bootloaders showing up, it takes the disk's filesystem and runs the command as if it had booted. It uses the system default shell and temporarily sets all environment variables... yeah, I know thats how chroot is...
#### Aw, Chroot!
Yeah, virtsh needs chroot to work, which is a problem since it requires root. however, it uses [lxroot](https://github.com/parke/lxroot) as its chroot as it is an safe, non-needing-root alternative to schroot, which was another workaround to using chroot without sudo.
### Chroot or QEMU?
QEMU is used for virtualization and for an seperate environment, and chroot is for running the actual shell command.
#### With Acceleration!
virtsh typically uses KVM by default to emulate. However, if KVM is not available, it will use another accelerator available. If there is none, then... no virtualization for you!
## ⚠⚠⚠⚠⚠⚠⚠⚠⚠WARNING⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠
I don't have any experience with emulation or C or C++. Yeah... good luck (for me...)
