# OS X Hypervisor.framework support in QEMU

These sources (and ../hvf-all.c) are adapted from Veertu Inc's vdhh (Veertu Desktop Hosted Hypervisor) (last known location: https://github.com/veertuinc/vdhh) with some minor changes, the most significant of which were:

1. Adapt to our current QEMU's `CPUState` structure and `address_space_rw` API; many struct members have been moved around (emulated x86 state, xsave_buf) due to historical differences + QEMU needing to handle more emulation targets.
2. Removal of `apic_page` and hyperv-related functionality.
3. More relaxed use of `bql_lock`.
