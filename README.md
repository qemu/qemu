## Virtual Shell
A program to run an Unix shell command inside an virtual environment (assuming the disk image used is Linux).
### How It [Would] Work(s)
virtsh takes the iamge and emulates it using QEMU. However, because of bootloaders showing up, it takes the disk's filesystem and runs the command as if it had booted. It uses the power of emulation to create a seperate environment for the command
#### With Acceleration!
virtsh typically uses KVM by default to emulate. However, if KVM is not available, it will use another accelerator available. If there is none, then... no virtualization for you!
## ⚠⚠⚠⚠⚠⚠⚠⚠⚠WARNING⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠
I don't have any experience with emulation or C or C++. Yeah...
