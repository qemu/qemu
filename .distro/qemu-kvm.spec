%global libfdt_version 1.6.0
%global libseccomp_version 2.4.0
%global libusbx_version 1.0.23
%global meson_version 0.61.3
%global usbredir_version 0.7.1
%global ipxe_version 20200823-5.git4bd064de

# LTO does not work with the coroutines of QEMU on non-x86 architectures
# (see BZ 1952483 and 1950192 for more information)
%ifnarch x86_64
    %global _lto_cflags %%{nil}
%endif

%global have_usbredir 1
%global have_opengl   1
%global have_fdt      1
%global have_modules_load 0
%global have_memlock_limits 0
# Some of these are not relevant for RHEL, but defining them
# makes it easier to sync the dependency list with Fedora
%global have_block_rbd 1
%global enable_werror 1
%global have_clang 1
%global have_safe_stack 0


%if %{have_clang}
%global toolchain clang
%ifarch x86_64
%global have_safe_stack 1
%endif
%else
%global toolchain gcc
%global cc_suffix .gcc
%endif



# Release candidate version tracking
# global rcver rc4
%if 0%{?rcver:1}
%global rcrel .%{rcver}
%global rcstr -%{rcver}
%endif

%global have_pmem 1
%ifnarch x86_64
    %global have_pmem 0
%endif

%global have_numactl 1
%ifarch s390x
    %global have_numactl 0
%endif

%global tools_only 0
%ifarch %{power64}
    %global tools_only 1
%endif

%ifnarch %{ix86} x86_64 aarch64
    %global have_usbredir 0
%endif

%ifnarch s390x
    %global have_librdma 1
%else
    %global have_librdma 0
%endif

%global modprobe_kvm_conf %{_sourcedir}/kvm.conf
%ifarch s390x
    %global modprobe_kvm_conf %{_sourcedir}/kvm-s390x.conf
%endif
%ifarch %{ix86} x86_64
    %global modprobe_kvm_conf %{_sourcedir}/kvm-x86.conf
%endif

%ifarch %{ix86}
    %global kvm_target    i386
%endif
%ifarch x86_64
    %global kvm_target    x86_64
%else
    %global have_opengl  0
%endif
%ifarch %{power64}
    %global kvm_target    ppc64
    %global have_memlock_limits 1
%endif
%ifarch s390x
    %global kvm_target    s390x
    %global have_modules_load 1
%endif
%ifarch ppc
    %global kvm_target    ppc
%endif
%ifarch aarch64
    %global kvm_target    aarch64
%endif

%global target_list %{kvm_target}-softmmu
%global block_drivers_rw_list qcow2,raw,file,host_device,nbd,iscsi,rbd,blkdebug,luks,null-co,nvme,copy-on-read,throttle,compress,virtio-blk-vhost-vdpa,virtio-blk-vfio-pci,virtio-blk-vhost-user,io_uring,nvme-io_uring
%global block_drivers_ro_list vdi,vmdk,vhdx,vpc,https
%define qemudocdir %{_docdir}/%{name}
%global firmwaredirs "%{_datadir}/qemu-firmware:%{_datadir}/ipxe/qemu:%{_datadir}/seavgabios:%{_datadir}/seabios"

#Versions of various parts:

%global requires_all_modules                                     \
%if %{have_opengl}                                               \
Requires: %{name}-ui-opengl = %{epoch}:%{version}-%{release}     \
Requires: %{name}-ui-egl-headless = %{epoch}:%{version}-%{release}     \
%endif                                                           \
Requires: %{name}-device-display-virtio-gpu = %{epoch}:%{version}-%{release}   \
%ifarch s390x                                                    \
Requires: %{name}-device-display-virtio-gpu-ccw = %{epoch}:%{version}-%{release}   \
%else                                                            \
Requires: %{name}-device-display-virtio-gpu-pci = %{epoch}:%{version}-%{release}   \
%endif                                                           \
%ifarch x86_64 %{power64}                                        \
Requires: %{name}-device-display-virtio-vga = %{epoch}:%{version}-%{release}   \
%endif                                                           \
Requires: %{name}-device-usb-host = %{epoch}:%{version}-%{release}   \
%if %{have_usbredir}                                             \
Requires: %{name}-device-usb-redirect = %{epoch}:%{version}-%{release}   \
%endif                                                           \
Requires: %{name}-block-blkio = %{epoch}:%{version}-%{release}   \
Requires: %{name}-block-rbd = %{epoch}:%{version}-%{release}     \
Requires: %{name}-audio-pa = %{epoch}:%{version}-%{release}

# Since SPICE is removed from RHEL-9, the following Obsoletes:
# removes {name}-ui-spice for upgrades from RHEL-8
# The "<= {version}" assumes RHEL-9 version >= RHEL-8 version (in
# other words RHEL-9 rebases are done together/before RHEL-8 ones)

# In addition, we obsolete some block drivers as we are no longer support
# them in default qemu-kvm installation.

# Note: ssh driver wasn't removed yet just disabled due to late handling

%global obsoletes_some_modules                                  \
Obsoletes: %{name}-ui-spice <= %{epoch}:%{version}                       \
Obsoletes: %{name}-block-gluster <= %{epoch}:%{version}                  \
Obsoletes: %{name}-block-iscsi <= %{epoch}:%{version}                    \
Obsoletes: %{name}-block-ssh <= %{epoch}:%{version}                    \


Summary: QEMU is a machine emulator and virtualizer
Name: qemu-kvm
Version: 9.1.0
Release: 9%{?rcrel}%{?dist}%{?cc_suffix}
# Epoch because we pushed a qemu-1.0 package. AIUI this can't ever be dropped
# Epoch 15 used for RHEL 8
# Epoch 17 used for RHEL 9 (due to release versioning offset in RHEL 8.5)
Epoch: 17
License: GPLv2 and GPLv2+ and CC-BY
URL: http://www.qemu.org/
ExclusiveArch: x86_64 %{power64} aarch64 s390x


Source0: http://wiki.qemu.org/download/qemu-%{version}%{?rcstr}.tar.xz

Source10: qemu-guest-agent.service
Source11: 99-qemu-guest-agent.rules
Source12: bridge.conf
Source13: qemu-ga.sysconfig
Source21: modules-load.conf
Source26: vhost.conf
Source27: kvm.conf
Source28: 95-kvm-memlock.conf
Source30: kvm-s390x.conf
Source31: kvm-x86.conf
Source36: README.tests


Patch0004: 0004-Initial-redhat-build.patch
Patch0005: 0005-Enable-disable-devices-for-RHEL.patch
Patch0006: 0006-Machine-type-related-general-changes.patch
Patch0007: 0007-meson-temporarily-disable-Wunused-function.patch
Patch0008: 0008-Remove-upstream-machine-type-versions-for-aarch64-s3.patch
Patch0009: 0009-Adapt-versioned-machine-type-macros-for-RHEL.patch
Patch0010: 0010-Increase-deletion-schedule-to-3-releases.patch
Patch0011: 0011-Add-downstream-aarch64-versioned-virt-machine-types.patch
Patch0012: 0012-Add-downstream-ppc64-versioned-spapr-machine-types.patch
Patch0013: 0013-Add-downstream-s390x-versioned-s390-ccw-virtio-machi.patch
Patch0014: 0014-Add-downstream-x86_64-versioned-pc-q35-machine-types.patch
Patch0015: 0015-Revert-meson-temporarily-disable-Wunused-function.patch
Patch0016: 0016-Enable-make-check.patch
Patch0017: 0017-vfio-cap-number-of-devices-that-can-be-assigned.patch
Patch0018: 0018-Add-support-statement-to-help-output.patch
Patch0019: 0019-Use-qemu-kvm-in-documentation-instead-of-qemu-system.patch
Patch0020: 0020-qcow2-Deprecation-warning-when-opening-v2-images-rw.patch
Patch0021: 0021-qemu-guest-agent-Update-the-logfile-path-of-qga-fsfr.patch
Patch0023: 0023-Add-upstream-compatibility-bits.patch
Patch0024: 0024-redhat-Add-QEMU-9.1-compat-handling-to-the-s390x-mac.patch
Patch0025: 0025-redhat-Add-rhel9.6.0-machine-type.patch
Patch0026: 0026-x86-ensure-compatibility-of-pc-q35-rhel9-and-pc-i440.patch
Patch0027: 0027-arm-ensure-compatibility-of-virt-rhel9.patch
Patch0028: 0028-arm-create-new-virt-machine-type-for-rhel-9.6.patch
Patch0029: 0029-x86-create-new-pc-q35-machine-type-for-rhel-9.6.patch
Patch0030: 0030-hw-arm-virt-Fix-Manufacturer-and-Product-Name-in-emu.patch
# For RHEL-11424 - [IBM 9.6 FEAT] KVM: Full boot order support - qemu part
Patch31: kvm-hw-s390x-ipl-Provide-more-memory-to-the-s390-ccw.img.patch
# For RHEL-11424 - [IBM 9.6 FEAT] KVM: Full boot order support - qemu part
Patch32: kvm-pc-bios-s390-ccw-Use-the-libc-from-SLOF-and-remove-s.patch
# For RHEL-11424 - [IBM 9.6 FEAT] KVM: Full boot order support - qemu part
Patch33: kvm-pc-bios-s390-ccw-Link-the-netboot-code-into-the-main.patch
# For RHEL-11424 - [IBM 9.6 FEAT] KVM: Full boot order support - qemu part
Patch35: kvm-hw-s390x-Remove-the-possibility-to-load-the-s390-net.patch
# For RHEL-11424 - [IBM 9.6 FEAT] KVM: Full boot order support - qemu part
Patch36: kvm-pc-bios-s390-ccw-Merge-netboot.mak-into-the-main-Mak.patch
# For RHEL-11424 - [IBM 9.6 FEAT] KVM: Full boot order support - qemu part
Patch37: kvm-docs-system-s390x-bootdevices-Update-the-documentati.patch
# For RHEL-11424 - [IBM 9.6 FEAT] KVM: Full boot order support - qemu part
Patch38: kvm-pc-bios-s390-ccw-Remove-panics-from-ISO-IPL-path.patch
# For RHEL-11424 - [IBM 9.6 FEAT] KVM: Full boot order support - qemu part
Patch39: kvm-pc-bios-s390-ccw-Remove-panics-from-ECKD-IPL-path.patch
# For RHEL-11424 - [IBM 9.6 FEAT] KVM: Full boot order support - qemu part
Patch40: kvm-pc-bios-s390-ccw-Remove-panics-from-SCSI-IPL-path.patch
# For RHEL-11424 - [IBM 9.6 FEAT] KVM: Full boot order support - qemu part
Patch41: kvm-pc-bios-s390-ccw-Remove-panics-from-DASD-IPL-path.patch
# For RHEL-11424 - [IBM 9.6 FEAT] KVM: Full boot order support - qemu part
Patch42: kvm-pc-bios-s390-ccw-Remove-panics-from-Netboot-IPL-path.patch
# For RHEL-11424 - [IBM 9.6 FEAT] KVM: Full boot order support - qemu part
Patch43: kvm-pc-bios-s390-ccw-Enable-failed-IPL-to-return-after-e.patch
# For RHEL-11424 - [IBM 9.6 FEAT] KVM: Full boot order support - qemu part
Patch44: kvm-include-hw-s390x-Add-include-files-for-common-IPL-st.patch
# For RHEL-11424 - [IBM 9.6 FEAT] KVM: Full boot order support - qemu part
Patch45: kvm-s390x-Add-individual-loadparm-assignment-to-CCW-devi.patch
# For RHEL-11424 - [IBM 9.6 FEAT] KVM: Full boot order support - qemu part
Patch46: kvm-hw-s390x-Build-an-IPLB-for-each-boot-device.patch
# For RHEL-11424 - [IBM 9.6 FEAT] KVM: Full boot order support - qemu part
Patch47: kvm-s390x-Rebuild-IPLB-for-SCSI-device-directly-from-DIA.patch
# For RHEL-11424 - [IBM 9.6 FEAT] KVM: Full boot order support - qemu part
Patch48: kvm-pc-bios-s390x-Enable-multi-device-boot-loop.patch
# For RHEL-11424 - [IBM 9.6 FEAT] KVM: Full boot order support - qemu part
Patch49: kvm-docs-system-Update-documentation-for-s390x-IPL.patch
# For RHEL-11424 - [IBM 9.6 FEAT] KVM: Full boot order support - qemu part
Patch50: kvm-tests-qtest-Add-s390x-boot-order-tests-to-cdrom-test.patch
# For RHEL-11424 - [IBM 9.6 FEAT] KVM: Full boot order support - qemu part
Patch51: kvm-pc-bios-s390-ccw-Clarify-alignment-is-in-bytes.patch
# For RHEL-11424 - [IBM 9.6 FEAT] KVM: Full boot order support - qemu part
Patch52: kvm-pc-bios-s390-ccw-Don-t-generate-TEXTRELs.patch
# For RHEL-11424 - [IBM 9.6 FEAT] KVM: Full boot order support - qemu part
Patch53: kvm-pc-bios-s390-ccw-Introduce-EXTRA_LDFLAGS.patch
# For RHEL-64307 - High threshold value observed in vGPU live migration
Patch54: kvm-vfio-migration-Report-only-stop-copy-size-in-vfio_st.patch
# For RHEL-64307 - High threshold value observed in vGPU live migration
Patch55: kvm-vfio-migration-Change-trace-formats-from-hex-to-deci.patch
# For RHEL-60914 - Fail migration properly when put cpu register fails
Patch56: kvm-kvm-Allow-kvm_arch_get-put_registers-to-accept-Error.patch
# For RHEL-60914 - Fail migration properly when put cpu register fails
Patch57: kvm-target-i386-kvm-Report-which-action-failed-in-kvm_ar.patch
# For RHEL-11043 - [RFE] [HPEMC] [RHEL-9.6] qemu-kvm: support up to 4096 VCPUs
Patch58: kvm-pc-q35-Bump-max_cpus-to-4096-vcpus.patch
# For RHEL-57682 - Bad migration performance when performing vGPU VM live migration 
Patch59: kvm-kvm-replace-fprintf-with-error_report-printf-in-kvm_.patch
# For RHEL-57682 - Bad migration performance when performing vGPU VM live migration 
Patch60: kvm-kvm-refactor-core-virtual-machine-creation-into-its-.patch
# For RHEL-57682 - Bad migration performance when performing vGPU VM live migration 
Patch61: kvm-accel-kvm-refactor-dirty-ring-setup.patch
# For RHEL-57682 - Bad migration performance when performing vGPU VM live migration 
Patch62: kvm-KVM-Dynamic-sized-kvm-memslots-array.patch
# For RHEL-57682 - Bad migration performance when performing vGPU VM live migration 
Patch63: kvm-KVM-Define-KVM_MEMSLOTS_NUM_MAX_DEFAULT.patch
# For RHEL-57682 - Bad migration performance when performing vGPU VM live migration 
Patch64: kvm-KVM-Rename-KVMMemoryListener.nr_used_slots-to-nr_slo.patch
# For RHEL-57682 - Bad migration performance when performing vGPU VM live migration 
Patch65: kvm-KVM-Rename-KVMState-nr_slots-to-nr_slots_max.patch
# For RHEL-67844 - qemu crashed after killed virtiofsd during migration
Patch66: kvm-migration-Ensure-vmstate_save-sets-errp.patch
# For RHEL-67935 - QEMU should fail gracefully with passthrough devices in SEV-SNP guests
Patch67: kvm-vfio-container-Fix-container-object-destruction.patch
# For RHEL-68289 - [RHEL-9.6] QEMU core dump on applying merge property to memory backend
Patch68: kvm-hostmem-Apply-merge-property-after-the-memory-region.patch
# For RHEL-69477 - qemu crashed when migrate vm with multiqueue from rhel9.4 to rhel9.6
Patch69: kvm-virtio-net-Add-queues-before-loading-them.patch
# For RHEL-68440 - The new "boot order" feature is sometimes not working as expected [RHEL 9]
Patch70: kvm-docs-system-s390x-bootdevices-Update-loadparm-docume.patch
# For RHEL-68440 - The new "boot order" feature is sometimes not working as expected [RHEL 9]
Patch71: kvm-docs-system-bootindex-Make-it-clear-that-s390x-can-a.patch
# For RHEL-68440 - The new "boot order" feature is sometimes not working as expected [RHEL 9]
Patch72: kvm-hw-s390x-Restrict-loadparm-property-to-devices-that-.patch
# For RHEL-68440 - The new "boot order" feature is sometimes not working as expected [RHEL 9]
Patch73: kvm-hw-Add-loadparm-property-to-scsi-disk-devices-for-bo.patch
# For RHEL-68440 - The new "boot order" feature is sometimes not working as expected [RHEL 9]
Patch74: kvm-scsi-fix-allocation-for-s390x-loadparm.patch
# For RHEL-68440 - The new "boot order" feature is sometimes not working as expected [RHEL 9]
Patch75: kvm-pc-bios-s390x-Initialize-cdrom-type-to-false-for-eac.patch
# For RHEL-68440 - The new "boot order" feature is sometimes not working as expected [RHEL 9]
Patch76: kvm-pc-bios-s390x-Initialize-machine-loadparm-before-pro.patch
# For RHEL-68440 - The new "boot order" feature is sometimes not working as expected [RHEL 9]
Patch77: kvm-pc-bios-s390-ccw-Re-initialize-receive-queue-index-b.patch
# For RHEL-61633 - Qemu-kvm  crashed  if  no display device setting and switching display by remote-viewer [rhel-9]
Patch78: kvm-vnc-fix-crash-when-no-console-attached.patch
# For RHEL-66089 - warning: fd: migration to a file is deprecated when create or revert a snapshot
Patch79: kvm-migration-Allow-pipes-to-keep-working-for-fd-migrati.patch
# For RHEL-50212 - [IBM 9.6 FEAT] KVM: CPU model for new IBM Z HW - qemu part
Patch80: kvm-linux-headers-Update-to-Linux-v6.12-rc5.patch
# For RHEL-50212 - [IBM 9.6 FEAT] KVM: CPU model for new IBM Z HW - qemu part
Patch81: kvm-s390x-cpumodel-add-msa10-subfunctions.patch
# For RHEL-50212 - [IBM 9.6 FEAT] KVM: CPU model for new IBM Z HW - qemu part
Patch82: kvm-s390x-cpumodel-add-msa11-subfunctions.patch
# For RHEL-50212 - [IBM 9.6 FEAT] KVM: CPU model for new IBM Z HW - qemu part
Patch83: kvm-s390x-cpumodel-add-msa12-changes.patch
# For RHEL-50212 - [IBM 9.6 FEAT] KVM: CPU model for new IBM Z HW - qemu part
Patch84: kvm-s390x-cpumodel-add-msa13-subfunctions.patch
# For RHEL-50212 - [IBM 9.6 FEAT] KVM: CPU model for new IBM Z HW - qemu part
Patch85: kvm-s390x-cpumodel-Add-ptff-Query-Time-Stamp-Event-QTSE-.patch
# For RHEL-50212 - [IBM 9.6 FEAT] KVM: CPU model for new IBM Z HW - qemu part
Patch86: kvm-linux-headers-Update-to-Linux-6.13-rc1.patch
# For RHEL-50212 - [IBM 9.6 FEAT] KVM: CPU model for new IBM Z HW - qemu part
Patch87: kvm-s390x-cpumodel-add-Concurrent-functions-facility-sup.patch
# For RHEL-50212 - [IBM 9.6 FEAT] KVM: CPU model for new IBM Z HW - qemu part
Patch88: kvm-s390x-cpumodel-add-Vector-Enhancements-facility-3.patch
# For RHEL-50212 - [IBM 9.6 FEAT] KVM: CPU model for new IBM Z HW - qemu part
Patch89: kvm-s390x-cpumodel-add-Miscellaneous-Instruction-Extensi.patch
# For RHEL-50212 - [IBM 9.6 FEAT] KVM: CPU model for new IBM Z HW - qemu part
Patch90: kvm-s390x-cpumodel-add-Vector-Packed-Decimal-Enhancement.patch
# For RHEL-50212 - [IBM 9.6 FEAT] KVM: CPU model for new IBM Z HW - qemu part
Patch91: kvm-s390x-cpumodel-add-Ineffective-nonconstrained-transa.patch
# For RHEL-50212 - [IBM 9.6 FEAT] KVM: CPU model for new IBM Z HW - qemu part
Patch92: kvm-s390x-cpumodel-Add-Sequential-Instruction-Fetching-f.patch
# For RHEL-50212 - [IBM 9.6 FEAT] KVM: CPU model for new IBM Z HW - qemu part
Patch93: kvm-s390x-cpumodel-correct-PLO-feature-wording.patch
# For RHEL-50212 - [IBM 9.6 FEAT] KVM: CPU model for new IBM Z HW - qemu part
Patch94: kvm-s390x-cpumodel-Add-PLO-extension-facility.patch
# For RHEL-50212 - [IBM 9.6 FEAT] KVM: CPU model for new IBM Z HW - qemu part
Patch95: kvm-s390x-cpumodel-gen17-model.patch
# For RHEL-71940 - qemu-ga cannot freeze filesystems with sentinelone
Patch96: kvm-qga-skip-bind-mounts-in-fs-list.patch
# For RHEL-27832 - The post-copy migration of RT-VM leads to race while accessing vhost-user device and hung/stalled target VM
Patch97: kvm-vhost-fail-device-start-if-iotlb-update-fails.patch
# For RHEL-67107 - [aarch64] [rhel-9.6] Backport some important post 9.1 qemu fixes
Patch98: kvm-hw-char-pl011-Use-correct-masks-for-IBRD-and-FBRD.patch
# For RHEL-39948 - qom-get iothread-vq-mapping is empty on new hotplug disk [rhel-9.5]
Patch99: kvm-qdev-Fix-set_pci_devfn-to-visit-option-only-once.patch
# For RHEL-39948 - qom-get iothread-vq-mapping is empty on new hotplug disk [rhel-9.5]
Patch100: kvm-tests-avocado-hotplug_blk-Fix-addr-in-device_add-com.patch
# For RHEL-39948 - qom-get iothread-vq-mapping is empty on new hotplug disk [rhel-9.5]
Patch101: kvm-qdev-monitor-avoid-QemuOpts-in-QMP-device_add.patch
# For RHEL-39948 - qom-get iothread-vq-mapping is empty on new hotplug disk [rhel-9.5]
Patch102: kvm-vl-use-qmp_device_add-in-qemu_create_cli_devices.patch

%if %{have_clang}
BuildRequires: clang
%if %{have_safe_stack}
BuildRequires: compiler-rt
%endif
%else
BuildRequires: gcc
%endif
BuildRequires: meson >= %{meson_version}
BuildRequires: ninja-build
BuildRequires: zlib-devel
BuildRequires: libzstd-devel
BuildRequires: glib2-devel
BuildRequires: gnutls-devel
BuildRequires: cyrus-sasl-devel
BuildRequires: libaio-devel
BuildRequires: libblkio-devel
BuildRequires: liburing-devel
BuildRequires: python3-devel
BuildRequires: libattr-devel
BuildRequires: libusbx-devel >= %{libusbx_version}
%if %{have_usbredir}
BuildRequires: usbredir-devel >= %{usbredir_version}
%endif
BuildRequires: texinfo
BuildRequires: python3-sphinx
BuildRequires: python3-sphinx_rtd_theme
BuildRequires: libseccomp-devel >= %{libseccomp_version}
# For network block driver
BuildRequires: libcurl-devel
%if %{have_block_rbd}
BuildRequires: librbd-devel
%endif
# We need both because the 'stap' binary is probed for by configure
BuildRequires: systemtap
BuildRequires: systemtap-sdt-devel
# Required as we use dtrace for trace backend
BuildRequires: /usr/bin/dtrace
# For VNC PNG support
BuildRequires: libpng-devel
# For virtiofs
BuildRequires: libcap-ng-devel
# Hard requirement for version >= 1.3
BuildRequires: pixman-devel
# For rdma
%if %{have_librdma}
BuildRequires: rdma-core-devel
%endif
%if %{have_fdt}
BuildRequires: libfdt-devel >= %{libfdt_version}
%endif
# For compressed guest memory dumps
BuildRequires: lzo-devel snappy-devel
# For NUMA memory binding
%if %{have_numactl}
BuildRequires: numactl-devel
%endif
# qemu-pr-helper multipath support (requires libudev too)
BuildRequires: device-mapper-multipath-devel
BuildRequires: systemd-devel
%if %{have_pmem}
BuildRequires: libpmem-devel
%endif
# qemu-keymap
BuildRequires: pkgconfig(xkbcommon)
%if %{have_opengl}
BuildRequires: pkgconfig(epoxy)
BuildRequires: pkgconfig(libdrm)
BuildRequires: pkgconfig(gbm)
%endif
BuildRequires: perl-Test-Harness
BuildRequires: libslirp-devel
BuildRequires: pulseaudio-libs-devel
BuildRequires: spice-protocol
BuildRequires: capstone-devel
BuildRequires: python3-tomli

# Requires for qemu-kvm package
Requires: %{name}-core = %{epoch}:%{version}-%{release}
Requires: %{name}-docs = %{epoch}:%{version}-%{release}
Requires: %{name}-tools = %{epoch}:%{version}-%{release}
Requires: qemu-pr-helper = %{epoch}:%{version}-%{release}
Requires: virtiofsd >= 1.5.0
%{requires_all_modules}

%description
%{name} is an open source virtualizer that provides hardware
emulation for the KVM hypervisor. %{name} acts as a virtual
machine monitor together with the KVM kernel modules, and emulates the
hardware for a full system such as a PC and its associated peripherals.


%package core
Summary: %{name} core components
%{obsoletes_some_modules}
Requires: %{name}-common = %{epoch}:%{version}-%{release}
Requires: qemu-img = %{epoch}:%{version}-%{release}
%ifarch %{ix86} x86_64
Requires: edk2-ovmf
%endif
%ifarch aarch64
Requires: edk2-aarch64
%endif

Requires: libseccomp >= %{libseccomp_version}
Requires: libusbx >= %{libusbx_version}
Requires: capstone
%if %{have_fdt}
Requires: libfdt >= %{libfdt_version}
%endif

%description core
%{name} is an open source virtualizer that provides hardware
emulation for the KVM hypervisor. %{name} acts as a virtual
machine monitor together with the KVM kernel modules, and emulates the
hardware for a full system such as a PC and its associated peripherals.
This is a minimalistic installation of %{name}. Functionality provided by
this package is not ensured and it can change in a future version as some
functionality can be split out to separate package.
Before updating this package, it is recommended to check the package
changelog for information on functionality which might have been moved to
a separate package to prevent issues due to the moved functionality.
If apps opt-in to minimalist packaging by depending on %{name}-core, they
explicitly accept that features may disappear from %{name}-core in future
updates.

%package common
Summary: QEMU common files needed by all QEMU targets
Requires(post): /usr/bin/getent
Requires(post): /usr/sbin/groupadd
Requires(post): /usr/sbin/useradd
Requires(post): systemd-units
Requires(preun): systemd-units
Requires(postun): systemd-units
%ifarch %{ix86} x86_64
Requires: seabios-bin >= 1.10.2-1
%endif
%ifnarch aarch64 s390x
Requires: seavgabios-bin >= 1.12.0-3
Requires: ipxe-roms-qemu >= %{ipxe_version}
%endif
# Removal -gl modules as they do not provide any functionality - see bz#2149022
Obsoletes: %{name}-device-display-virtio-gpu-gl <= %{epoch}:%{version}
Obsoletes: %{name}-device-display-virtio-gpu-pci-gl <= %{epoch}:%{version}
Obsoletes: %{name}-device-display-virtio-vga-gl <= %{epoch}:%{version}

%description common
%{name} is an open source virtualizer that provides hardware emulation for
the KVM hypervisor.

This package provides documentation and auxiliary programs used with %{name}.


%package tools
Summary: %{name} support tools
%description tools
%{name}-tools provides various tools related to %{name} usage.


%package docs
Summary: %{name} documentation
%description docs
%{name}-docs provides documentation files regarding %{name}.


%package -n qemu-pr-helper
Summary: qemu-pr-helper utility for %{name}
%description -n qemu-pr-helper
This package provides the qemu-pr-helper utility that is required for certain
SCSI features.


%package -n qemu-img
Summary: QEMU command line tool for manipulating disk images
%description -n qemu-img
This package provides a command line tool for manipulating disk images.


%package -n qemu-guest-agent
Summary: QEMU guest agent
Requires(post): systemd-units
Requires(preun): systemd-units
Requires(postun): systemd-units
%description -n qemu-guest-agent
%{name} is an open source virtualizer that provides hardware emulation for
the KVM hypervisor.

This package provides an agent to run inside guests, which communicates
with the host over a virtio-serial channel named "org.qemu.guest_agent.0"

This package does not need to be installed on the host OS.


%package tests
Summary: tests for the %{name} package
Requires: %{name} = %{epoch}:%{version}-%{release}

%define testsdir %{_libdir}/%{name}/tests-src

%description tests
The %{name}-tests rpm contains tests that can be used to verify
the functionality of the installed %{name} package

Install this package if you want access to the avocado_qemu
tests, or qemu-iotests.


%package  block-blkio
Summary: QEMU libblkio block drivers
Requires: %{name}-common%{?_isa} = %{epoch}:%{version}-%{release}
%description block-blkio
This package provides the additional libblkio block drivers for QEMU.

Install this package if you want to use virtio-blk-vdpa-blk,
virtio-blk-vfio-pci, virtio-blk-vhost-user, io_uring, and nvme-io_uring block
drivers provided by libblkio.


%package  block-curl
Summary: QEMU CURL block driver
Requires: %{name}-common%{?_isa} = %{epoch}:%{version}-%{release}
%description block-curl
This package provides the additional CURL block driver for QEMU.

Install this package if you want to access remote disks over
http, https, ftp and other transports provided by the CURL library.


%if %{have_block_rbd}
%package  block-rbd
Summary: QEMU Ceph/RBD block driver
Requires: %{name}-common%{?_isa} = %{epoch}:%{version}-%{release}
%description block-rbd
This package provides the additional Ceph/RBD block driver for QEMU.

Install this package if you want to access remote Ceph volumes
using the rbd protocol.
%endif


%package  audio-pa
Summary: QEMU PulseAudio audio driver
Requires: %{name}-common%{?_isa} = %{epoch}:%{version}-%{release}
%description audio-pa
This package provides the additional PulseAudio audio driver for QEMU.


%if %{have_opengl}
%package  ui-opengl
Summary: QEMU opengl support
Requires: %{name}-common%{?_isa} = %{epoch}:%{version}-%{release}
Requires: mesa-libGL
Requires: mesa-libEGL
Requires: mesa-dri-drivers
%description ui-opengl
This package provides opengl support.

%package  ui-egl-headless
Summary: QEMU EGL headless driver
Requires: %{name}-common%{?_isa} = %{epoch}:%{version}-%{release}
Requires: %{name}-ui-opengl%{?_isa} = %{epoch}:%{version}-%{release}
%description ui-egl-headless
This package provides the additional egl-headless UI for QEMU.
%endif


%package device-display-virtio-gpu
Summary: QEMU virtio-gpu display device
Requires: %{name}-common%{?_isa} = %{epoch}:%{version}-%{release}
%description device-display-virtio-gpu
This package provides the virtio-gpu display device for QEMU.

%ifarch s390x
%package device-display-virtio-gpu-ccw
Summary: QEMU virtio-gpu-ccw display device
Requires: %{name}-common%{?_isa} = %{epoch}:%{version}-%{release}
Requires: %{name}-device-display-virtio-gpu = %{epoch}:%{version}-%{release}
%description device-display-virtio-gpu-ccw
This package provides the virtio-gpu-ccw display device for QEMU.
%else
%package device-display-virtio-gpu-pci
Summary: QEMU virtio-gpu-pci display device
Requires: %{name}-common%{?_isa} = %{epoch}:%{version}-%{release}
Requires: %{name}-device-display-virtio-gpu = %{epoch}:%{version}-%{release}
%description device-display-virtio-gpu-pci
This package provides the virtio-gpu-pci display device for QEMU.
%endif

%ifarch x86_64 %{power64}
%package device-display-virtio-vga
Summary: QEMU virtio-vga display device
Requires: %{name}-common%{?_isa} = %{epoch}:%{version}-%{release}
%description device-display-virtio-vga
This package provides the virtio-vga display device for QEMU.
%endif

%package device-usb-host
Summary: QEMU usb host device
Requires: %{name}-common%{?_isa} = %{epoch}:%{version}-%{release}
%description device-usb-host
This package provides the USB pass through driver for QEMU.

%if %{have_usbredir}
%package  device-usb-redirect
Summary: QEMU usbredir support
Requires: %{name}-common%{?_isa} = %{epoch}:%{version}-%{release}
Requires: usbredir >= 0.7.1
Provides: %{name}-hw-usbredir
Obsoletes: %{name}-hw-usbredir <= %{epoch}:%{version}

%description device-usb-redirect
This package provides usbredir support.
%endif

%package  ui-dbus
Summary: QEMU D-Bus UI driver
Requires: %{name}-common%{?_isa} = %{epoch}:%{version}-%{release}
%description ui-dbus
This package provides the additional D-Bus UI for QEMU.

%package  audio-dbus
Summary: QEMU D-Bus audio driver
Requires: %{name}-common%{?_isa} = %{epoch}:%{version}-%{release}
Requires: %{name}-ui-dbus = %{epoch}:%{version}-%{release}
%description audio-dbus
This package provides the additional D-Bus audio driver for QEMU.

%prep
%setup -q -n qemu-%{version}%{?rcstr}
%autopatch -p1

%global qemu_kvm_build qemu_kvm_build
mkdir -p %{qemu_kvm_build}


%build

# Necessary hack for ZUUL CI
ulimit -n 10240

%define disable_everything         \\\
  --audio-drv-list=                \\\
  --disable-alsa                   \\\
  --disable-attr                   \\\
  --disable-auth-pam               \\\
  --disable-avx2                   \\\
  --disable-avx512bw               \\\
  --disable-blkio                  \\\
  --disable-block-drv-whitelist-in-tools \\\
  --disable-bochs                  \\\
  --disable-bpf                    \\\
  --disable-brlapi                 \\\
  --disable-bsd-user               \\\
  --disable-bzip2                  \\\
  --disable-cap-ng                 \\\
  --disable-capstone               \\\
  --disable-cfi                    \\\
  --disable-cfi-debug              \\\
  --disable-cloop                  \\\
  --disable-cocoa                  \\\
  --disable-coreaudio              \\\
  --disable-coroutine-pool         \\\
  --disable-crypto-afalg           \\\
  --disable-curl                   \\\
  --disable-curses                 \\\
  --disable-dbus-display           \\\
  --disable-debug-info             \\\
  --disable-debug-mutex            \\\
  --disable-debug-tcg              \\\
  --disable-dmg                    \\\
  --disable-docs                   \\\
  --disable-download               \\\
  --disable-dsound                 \\\
  --disable-fdt                    \\\
  --disable-fuse                   \\\
  --disable-fuse-lseek             \\\
  --disable-gcrypt                 \\\
  --disable-gettext                \\\
  --disable-gio                    \\\
  --disable-glusterfs              \\\
  --disable-gnutls                 \\\
  --disable-gtk                    \\\
  --disable-guest-agent            \\\
  --disable-guest-agent-msi        \\\
  --disable-hvf                    \\\
  --disable-iconv                  \\\
  --disable-jack                   \\\
  --disable-kvm                    \\\
  --disable-l2tpv3                 \\\
  --disable-libdaxctl              \\\
  --disable-libdw                  \\\
  --disable-libiscsi               \\\
  --disable-libnfs                 \\\
  --disable-libpmem                \\\
  --disable-libssh                 \\\
  --disable-libudev                \\\
  --disable-libusb                 \\\
  --disable-libvduse               \\\
  --disable-linux-aio              \\\
  --disable-linux-io-uring         \\\
  --disable-linux-user             \\\
  --disable-lto                    \\\
  --disable-lzfse                  \\\
  --disable-lzo                    \\\
  --disable-malloc-trim            \\\
  --disable-membarrier             \\\
  --disable-modules                \\\
  --disable-module-upgrades        \\\
  --disable-mpath                  \\\
  --disable-multiprocess           \\\
  --disable-netmap                 \\\
  --disable-nettle                 \\\
  --disable-numa                   \\\
  --disable-nvmm                   \\\
  --disable-opengl                 \\\
  --disable-oss                    \\\
  --disable-pa                     \\\
  --disable-parallels              \\\
  --disable-pie                    \\\
  --disable-plugins                \\\
  --disable-qcow1                  \\\
  --disable-qed                    \\\
  --disable-qga-vss                \\\
  --disable-qom-cast-debug         \\\
  --disable-rbd                    \\\
  --disable-rdma                   \\\
  --disable-replication            \\\
  --disable-rng-none               \\\
  --disable-safe-stack             \\\
  --disable-sanitizers             \\\
  --disable-sdl                    \\\
  --disable-sdl-image              \\\
  --disable-seccomp                \\\
  --disable-selinux                \\\
  --disable-slirp                  \\\
  --disable-slirp-smbd             \\\
  --disable-smartcard              \\\
  --disable-snappy                 \\\
  --disable-sndio                  \\\
  --disable-sparse                 \\\
  --disable-spice                  \\\
  --disable-spice-protocol         \\\
  --disable-strip                  \\\
  --disable-system                 \\\
  --disable-tcg                    \\\
  --disable-tools                  \\\
  --disable-tpm                    \\\
  --disable-u2f                    \\\
  --disable-usb-redir              \\\
  --disable-user                   \\\
  --disable-vde                    \\\
  --disable-vdi                    \\\
  --disable-vduse-blk-export       \\\
  --disable-vhost-crypto           \\\
  --disable-vhost-kernel           \\\
  --disable-vhost-net              \\\
  --disable-vhost-user             \\\
  --disable-vhost-user-blk-server  \\\
  --disable-vhost-vdpa             \\\
  --disable-virglrenderer          \\\
  --disable-virtfs                 \\\
  --disable-vnc                    \\\
  --disable-vnc-jpeg               \\\
  --disable-png                    \\\
  --disable-vnc-sasl               \\\
  --disable-vte                    \\\
  --disable-vvfat                  \\\
  --disable-werror                 \\\
  --disable-whpx                   \\\
  --disable-xen                    \\\
  --disable-xen-pci-passthrough    \\\
  --disable-xkbcommon              \\\
  --disable-zstd                   \\\
  --without-default-devices


run_configure() {
    ../configure \
        --cc=%{__cc} \
        --cxx=/bin/false \
        --prefix="%{_prefix}" \
        --libdir="%{_libdir}" \
        --datadir="%{_datadir}" \
        --sysconfdir="%{_sysconfdir}" \
        --interp-prefix=%{_prefix}/qemu-%M \
        --localstatedir="%{_localstatedir}" \
        --docdir="%{_docdir}" \
        --libexecdir="%{_libexecdir}" \
        --extra-ldflags="%{build_ldflags}" \
        --extra-cflags="%{optflags} -Wno-string-plus-int" \
        --with-pkgversion="%{name}-%{version}-%{release}" \
        --with-suffix="%{name}" \
        --firmwarepath=%{firmwaredirs} \
        --enable-trace-backends=dtrace \
        --with-coroutine=ucontext \
        --tls-priority=@QEMU,SYSTEM \
        %{disable_everything} \
        --with-devices-%{kvm_target}=%{kvm_target}-rh-devices \
	--rhel-version=9 \
        "$@"

    echo "config-host.mak contents:"
    echo "==="
    cat config-host.mak
    echo "==="
}


pushd %{qemu_kvm_build}
run_configure \
%if %{defined target_list}
  --target-list="%{target_list}" \
%endif
%if %{defined block_drivers_rw_list}
  --block-drv-rw-whitelist=%{block_drivers_rw_list} \
%endif
%if %{defined block_drivers_ro_list}
  --block-drv-ro-whitelist=%{block_drivers_ro_list} \
%endif
  --enable-attr \
  --enable-blkio \
  --enable-cap-ng \
  --enable-capstone \
  --enable-coroutine-pool \
  --enable-curl \
  --enable-dbus-display \
  --enable-debug-info \
  --enable-docs \
%if %{have_fdt}
  --enable-fdt=system \
%endif
  --enable-gio \
  --enable-gnutls \
  --enable-guest-agent \
  --enable-iconv \
  --enable-kvm \
%if %{have_pmem}
  --enable-libpmem \
%endif
  --enable-libusb \
  --enable-libudev \
  --enable-linux-aio \
  --enable-linux-io-uring \
  --enable-lzo \
  --enable-malloc-trim \
  --enable-modules \
  --enable-mpath \
%if %{have_numactl}
  --enable-numa \
%endif
%if %{have_opengl}
  --enable-opengl \
%endif
  --enable-pa \
  --enable-pie \
%if %{have_block_rbd}
  --enable-rbd \
%endif
%if %{have_librdma}
  --enable-rdma \
%endif
  --enable-seccomp \
  --enable-selinux \
  --enable-slirp \
  --enable-snappy \
  --enable-spice-protocol \
  --enable-system \
  --enable-tcg \
  --enable-tools \
  --enable-tpm \
%if %{have_usbredir}
  --enable-usb-redir \
%endif
  --enable-vdi \
  --enable-vhost-kernel \
  --enable-vhost-net \
  --enable-vhost-user \
  --enable-vhost-user-blk-server \
  --enable-vhost-vdpa \
  --enable-vnc \
  --enable-png \
  --enable-vnc-sasl \
%if %{enable_werror}
  --enable-werror \
%endif
  --enable-xkbcommon \
  --enable-zstd \
%if %{have_safe_stack}
  --enable-safe-stack \
%endif

%if %{tools_only}
%make_build qemu-img
%make_build qemu-io
%make_build qemu-nbd
%make_build storage-daemon/qemu-storage-daemon

%make_build docs/qemu-img.1
%make_build docs/qemu-nbd.8
%make_build docs/qemu-storage-daemon.1
%make_build docs/qemu-storage-daemon-qmp-ref.7

%make_build qga/qemu-ga
%make_build docs/qemu-ga.8
# endif tools_only
%endif


%if !%{tools_only}
%make_build

# Setup back compat qemu-kvm binary
%{__python3} scripts/tracetool.py --backend dtrace --format stap \
  --group=all --binary %{_libexecdir}/qemu-kvm --probe-prefix qemu.kvm \
  trace/trace-events-all qemu-kvm.stp

%{__python3} scripts/tracetool.py --backends=dtrace --format=log-stap \
  --group=all --binary %{_libexecdir}/qemu-kvm --probe-prefix qemu.kvm \
  trace/trace-events-all qemu-kvm-log.stp

%{__python3} scripts/tracetool.py --backend dtrace --format simpletrace-stap \
  --group=all --binary %{_libexecdir}/qemu-kvm --probe-prefix qemu.kvm \
  trace/trace-events-all qemu-kvm-simpletrace.stp

cp -a qemu-system-%{kvm_target} qemu-kvm

%ifarch s390x
    # Copy the built new images into place for "make check":
    cp pc-bios/s390-ccw/s390-ccw.img pc-bios/
%endif

popd
# endif !tools_only
%endif



%install
# Install qemu-guest-agent service and udev rules
install -D -m 0644 %{_sourcedir}/qemu-guest-agent.service %{buildroot}%{_unitdir}/qemu-guest-agent.service
install -D -m 0644 %{_sourcedir}/qemu-ga.sysconfig %{buildroot}%{_sysconfdir}/sysconfig/qemu-ga
install -D -m 0644 %{_sourcedir}/99-qemu-guest-agent.rules %{buildroot}%{_udevrulesdir}/99-qemu-guest-agent.rules


# Install qemu-ga fsfreeze bits
mkdir -p %{buildroot}%{_sysconfdir}/qemu-ga/fsfreeze-hook.d
install -p scripts/qemu-guest-agent/fsfreeze-hook %{buildroot}%{_sysconfdir}/qemu-ga/fsfreeze-hook
mkdir -p %{buildroot}%{_datadir}/%{name}/qemu-ga/fsfreeze-hook.d/
install -p -m 0644 scripts/qemu-guest-agent/fsfreeze-hook.d/*.sample %{buildroot}%{_datadir}/%{name}/qemu-ga/fsfreeze-hook.d/
mkdir -p -v %{buildroot}%{_localstatedir}/log/qemu-ga/


%if %{tools_only}
pushd %{qemu_kvm_build}
install -D -p -m 0755 qga/qemu-ga %{buildroot}%{_bindir}/qemu-ga
install -D -p -m 0755 qemu-img %{buildroot}%{_bindir}/qemu-img
install -D -p -m 0755 qemu-io %{buildroot}%{_bindir}/qemu-io
install -D -p -m 0755 qemu-nbd %{buildroot}%{_bindir}/qemu-nbd
install -D -p -m 0755 storage-daemon/qemu-storage-daemon %{buildroot}%{_bindir}/qemu-storage-daemon

mkdir -p %{buildroot}%{_mandir}/man1/
mkdir -p %{buildroot}%{_mandir}/man7/
mkdir -p %{buildroot}%{_mandir}/man8/

install -D -p -m 644 docs/qemu-img.1* %{buildroot}%{_mandir}/man1
install -D -p -m 644 docs/qemu-nbd.8* %{buildroot}%{_mandir}/man8
install -D -p -m 644 docs/qemu-storage-daemon.1* %{buildroot}%{_mandir}/man1
install -D -p -m 644 docs/qemu-storage-daemon-qmp-ref.7* %{buildroot}%{_mandir}/man7
install -D -p -m 644 docs/qemu-ga.8* %{buildroot}%{_mandir}/man8
popd
# endif tools_only
%endif

%if !%{tools_only}

install -D -p -m 0644 %{_sourcedir}/vhost.conf %{buildroot}%{_sysconfdir}/modprobe.d/vhost.conf
install -D -p -m 0644 %{modprobe_kvm_conf} $RPM_BUILD_ROOT%{_sysconfdir}/modprobe.d/kvm.conf

# Create new directories and put them all under tests-src
mkdir -p %{buildroot}%{testsdir}/python
mkdir -p %{buildroot}%{testsdir}/tests
mkdir -p %{buildroot}%{testsdir}/tests/avocado
mkdir -p %{buildroot}%{testsdir}/tests/qemu-iotests
mkdir -p %{buildroot}%{testsdir}/scripts/qmp


install -m 0644 scripts/dump-guest-memory.py \
                %{buildroot}%{_datadir}/%{name}

# Install avocado_qemu tests
cp -R %{qemu_kvm_build}/tests/avocado/* %{buildroot}%{testsdir}/tests/avocado/

# Install qemu.py and qmp/ scripts required to run avocado_qemu tests
cp -R %{qemu_kvm_build}/python/qemu %{buildroot}%{testsdir}/python
cp -R %{qemu_kvm_build}/scripts/qmp/* %{buildroot}%{testsdir}/scripts/qmp
install -p -m 0644 tests/Makefile.include %{buildroot}%{testsdir}/tests/

# Install qemu-iotests
cp -R tests/qemu-iotests/* %{buildroot}%{testsdir}/tests/qemu-iotests/
cp -ur %{qemu_kvm_build}/tests/qemu-iotests/* %{buildroot}%{testsdir}/tests/qemu-iotests/

install -p -m 0644 %{_sourcedir}/README.tests %{buildroot}%{testsdir}/README

# Do the actual qemu tree install
pushd %{qemu_kvm_build}
%make_install
popd

mkdir -p %{buildroot}%{_datadir}/systemtap/tapset

install -m 0755 %{qemu_kvm_build}/qemu-system-%{kvm_target} %{buildroot}%{_libexecdir}/qemu-kvm
install -m 0644 %{qemu_kvm_build}/qemu-kvm.stp %{buildroot}%{_datadir}/systemtap/tapset/
install -m 0644 %{qemu_kvm_build}/qemu-kvm-log.stp %{buildroot}%{_datadir}/systemtap/tapset/
install -m 0644 %{qemu_kvm_build}/qemu-kvm-simpletrace.stp %{buildroot}%{_datadir}/systemtap/tapset/
install -d -m 0755 "%{buildroot}%{_datadir}/%{name}/systemtap/script.d"
install -c -m 0644 %{qemu_kvm_build}/scripts/systemtap/script.d/qemu_kvm.stp "%{buildroot}%{_datadir}/%{name}/systemtap/script.d/"
install -d -m 0755 "%{buildroot}%{_datadir}/%{name}/systemtap/conf.d"
install -c -m 0644 %{qemu_kvm_build}/scripts/systemtap/conf.d/qemu_kvm.conf "%{buildroot}%{_datadir}/%{name}/systemtap/conf.d/"


rm %{buildroot}/%{_datadir}/applications/qemu.desktop
rm %{buildroot}%{_bindir}/qemu-system-%{kvm_target}
rm %{buildroot}%{_datadir}/systemtap/tapset/qemu-system-%{kvm_target}.stp
rm %{buildroot}%{_datadir}/systemtap/tapset/qemu-system-%{kvm_target}-simpletrace.stp
rm %{buildroot}%{_datadir}/systemtap/tapset/qemu-system-%{kvm_target}-log.stp

# Install simpletrace
install -m 0755 scripts/simpletrace.py %{buildroot}%{_datadir}/%{name}/simpletrace.py
# Avoid ambiguous 'python' interpreter name
mkdir -p %{buildroot}%{_datadir}/%{name}/tracetool
install -m 0644 -t %{buildroot}%{_datadir}/%{name}/tracetool scripts/tracetool/*.py
mkdir -p %{buildroot}%{_datadir}/%{name}/tracetool/backend
install -m 0644 -t %{buildroot}%{_datadir}/%{name}/tracetool/backend scripts/tracetool/backend/*.py
mkdir -p %{buildroot}%{_datadir}/%{name}/tracetool/format
install -m 0644 -t %{buildroot}%{_datadir}/%{name}/tracetool/format scripts/tracetool/format/*.py

mkdir -p %{buildroot}%{qemudocdir}
install -p -m 0644 -t %{buildroot}%{qemudocdir} README.rst README.systemtap COPYING COPYING.LIB LICENSE

# Rename man page
pushd %{buildroot}%{_mandir}/man1/
for fn in qemu.1*; do
     mv $fn "qemu-kvm${fn#qemu}"
done
popd

install -D -p -m 0644 qemu.sasl %{buildroot}%{_sysconfdir}/sasl2/%{name}.conf

# Provided by package openbios
rm -rf %{buildroot}%{_datadir}/%{name}/openbios-ppc
rm -rf %{buildroot}%{_datadir}/%{name}/openbios-sparc32
rm -rf %{buildroot}%{_datadir}/%{name}/openbios-sparc64
# Provided by package SLOF
rm -rf %{buildroot}%{_datadir}/%{name}/slof.bin

# Remove unpackaged files.
rm -rf %{buildroot}%{_datadir}/%{name}/palcode-clipper
rm -rf %{buildroot}%{_datadir}/%{name}/petalogix*.dtb
rm -f %{buildroot}%{_datadir}/%{name}/bamboo.dtb
rm -f %{buildroot}%{_datadir}/%{name}/ppc_rom.bin
rm -rf %{buildroot}%{_datadir}/%{name}/s390-zipl.rom
rm -rf %{buildroot}%{_datadir}/%{name}/u-boot.e500
rm -rf %{buildroot}%{_datadir}/%{name}/qemu_vga.ndrv
rm -rf %{buildroot}%{_datadir}/%{name}/skiboot.lid
rm -rf %{buildroot}%{_datadir}/%{name}/qboot.rom

rm -rf %{buildroot}%{_datadir}/%{name}/s390-ccw.img
rm -rf %{buildroot}%{_datadir}/%{name}/hppa-firmware.img
rm -rf %{buildroot}%{_datadir}/%{name}/hppa-firmware64.img
rm -rf %{buildroot}%{_datadir}/%{name}/canyonlands.dtb
rm -rf %{buildroot}%{_datadir}/%{name}/u-boot-sam460-20100605.bin

rm -rf %{buildroot}%{_datadir}/%{name}/firmware
rm -rf %{buildroot}%{_datadir}/%{name}/edk2-*.fd
rm -rf %{buildroot}%{_datadir}/%{name}/edk2-licenses.txt

rm -rf %{buildroot}%{_datadir}/%{name}/opensbi-riscv32-sifive_u-fw_jump.bin
rm -rf %{buildroot}%{_datadir}/%{name}/opensbi-riscv32-virt-fw_jump.bin
rm -rf %{buildroot}%{_datadir}/%{name}/opensbi-riscv32-generic-fw_dynamic.*
rm -rf %{buildroot}%{_datadir}/%{name}/opensbi-riscv64-sifive_u-fw_jump.bin
rm -rf %{buildroot}%{_datadir}/%{name}/opensbi-riscv64-virt-fw_jump.bin
rm -rf %{buildroot}%{_datadir}/%{name}/opensbi-riscv64-generic-fw_dynamic.*
rm -rf %{buildroot}%{_datadir}/%{name}/qemu-nsis.bmp
rm -rf %{buildroot}%{_datadir}/%{name}/npcm7xx_bootrom.bin

# Remove virtfs-proxy-helper files
rm -rf %{buildroot}%{_libexecdir}/virtfs-proxy-helper
rm -rf %{buildroot}%{_mandir}/man1/virtfs-proxy-helper*

%ifarch s390x
    # Use the s390-ccw.img that we've just built, not the pre-built one
    install -m 0644 %{qemu_kvm_build}/pc-bios/s390-ccw/s390-ccw.img %{buildroot}%{_datadir}/%{name}/
%else
    rm -rf %{buildroot}%{_libdir}/%{name}/hw-s390x-virtio-gpu-ccw.so
%endif

%ifnarch x86_64
    rm -rf %{buildroot}%{_datadir}/%{name}/kvmvapic.bin
    rm -rf %{buildroot}%{_datadir}/%{name}/linuxboot.bin
    rm -rf %{buildroot}%{_datadir}/%{name}/multiboot.bin
    rm -rf %{buildroot}%{_datadir}/%{name}/multiboot_dma.bin
    rm -rf %{buildroot}%{_datadir}/%{name}/pvh.bin
%else
    rm -rf %{buildroot}%{_bindir}/qemu-vmsr-helper
%endif

# Remove sparc files
rm -rf %{buildroot}%{_datadir}/%{name}/QEMU,tcx.bin
rm -rf %{buildroot}%{_datadir}/%{name}/QEMU,cgthree.bin

# Remove ivshmem example programs
rm -rf %{buildroot}%{_bindir}/ivshmem-client
rm -rf %{buildroot}%{_bindir}/ivshmem-server

# Remove efi roms
rm -rf %{buildroot}%{_datadir}/%{name}/efi*.rom

# Provided by package ipxe
rm -rf %{buildroot}%{_datadir}/%{name}/pxe*rom
# Provided by package vgabios
rm -rf %{buildroot}%{_datadir}/%{name}/vgabios*bin
# Provided by package seabios
rm -rf %{buildroot}%{_datadir}/%{name}/bios*.bin
rm -rf %{buildroot}%{_datadir}/%{name}/sgabios.bin

# Remove vof roms
rm -rf %{buildroot}%{_datadir}/%{name}/vof-nvram.bin
rm -rf %{buildroot}%{_datadir}/%{name}/vof.bin

%if %{have_modules_load}
    install -D -p -m 644 %{_sourcedir}/modules-load.conf %{buildroot}%{_sysconfdir}/modules-load.d/kvm.conf
%endif

%if %{have_memlock_limits}
    install -D -p -m 644 %{_sourcedir}/95-kvm-memlock.conf %{buildroot}%{_sysconfdir}/security/limits.d/95-kvm-memlock.conf
%endif

# Install rules to use the bridge helper with libvirt's virbr0
install -D -m 0644 %{_sourcedir}/bridge.conf %{buildroot}%{_sysconfdir}/%{name}/bridge.conf

# Install qemu-pr-helper service
install -m 0644 contrib/systemd/qemu-pr-helper.service %{buildroot}%{_unitdir}
install -m 0644 contrib/systemd/qemu-pr-helper.socket %{buildroot}%{_unitdir}

# We do not support gl display devices so we can remove their modules as they
# do not have expected functionality included.
#
# https://gitlab.com/qemu-project/qemu/-/issues/1352 was filed to stop building these
# modules in case all dependencies are not satisfied.

rm -rf %{buildroot}%{_libdir}/%{name}/hw-display-virtio-gpu-gl.so
rm -rf %{buildroot}%{_libdir}/%{name}/hw-display-virtio-gpu-pci-gl.so
rm -rf %{buildroot}%{_libdir}/%{name}/hw-display-virtio-vga-gl.so

# We need to make the block device modules and other qemu SO files executable
# otherwise RPM won't pick up their dependencies.
chmod +x %{buildroot}%{_libdir}/%{name}/*.so

# Remove docs we don't care about
find %{buildroot}%{qemudocdir} -name .buildinfo -delete
rm -rf %{buildroot}%{qemudocdir}/specs

# endif !tools_only
%endif

%check
%if !%{tools_only}

pushd %{qemu_kvm_build}
echo "Testing %{name}-build"
#%make_build check
make V=1 check
popd

# endif !tools_only
%endif

%post -n qemu-guest-agent
%systemd_post qemu-guest-agent.service
%preun -n qemu-guest-agent
%systemd_preun qemu-guest-agent.service
%postun -n qemu-guest-agent
%systemd_postun_with_restart qemu-guest-agent.service

%if !%{tools_only}
%post common
getent group kvm >/dev/null || groupadd -g 36 -r kvm
getent group qemu >/dev/null || groupadd -g 107 -r qemu
getent passwd qemu >/dev/null || \
useradd -r -u 107 -g qemu -G kvm -d / -s /sbin/nologin \
  -c "qemu user" qemu

# If this is a new installation, then load kvm modules now, so we can make
# sure that the user gets a system where KVM is ready to use. In case of
# an upgrade, don't try to modprobe again in case the user unloaded the
# kvm module on purpose.
%if %{have_modules_load}
    if [ "$1" = "1" ]; then
        modprobe -b kvm  &> /dev/null || :
    fi
%endif
# endif !tools_only
%endif



%files -n qemu-img
%{_bindir}/qemu-img
%{_bindir}/qemu-io
%{_bindir}/qemu-nbd
%{_bindir}/qemu-storage-daemon
%{_mandir}/man1/qemu-img.1*
%{_mandir}/man8/qemu-nbd.8*
%{_mandir}/man1/qemu-storage-daemon.1*
%{_mandir}/man7/qemu-storage-daemon-qmp-ref.7*


%files -n qemu-guest-agent
%doc COPYING README.rst
%{_bindir}/qemu-ga
%{_mandir}/man8/qemu-ga.8*
%{_unitdir}/qemu-guest-agent.service
%{_udevrulesdir}/99-qemu-guest-agent.rules
%config(noreplace) %{_sysconfdir}/sysconfig/qemu-ga
%{_sysconfdir}/qemu-ga
%{_datadir}/%{name}/qemu-ga
%dir %{_localstatedir}/log/qemu-ga


%if !%{tools_only}
%files
# Deliberately empty

%files tools
%{_bindir}/qemu-keymap
%{_bindir}/qemu-edid
%{_bindir}/qemu-trace-stap
%{_bindir}/elf2dmp
%{_datadir}/%{name}/simpletrace.py*
%{_datadir}/%{name}/tracetool/*.py*
%{_datadir}/%{name}/tracetool/backend/*.py*
%{_datadir}/%{name}/tracetool/format/*.py*
%{_datadir}/%{name}/dump-guest-memory.py*
%{_datadir}/%{name}/trace-events-all
%{_mandir}/man1/qemu-trace-stap.1*

%files -n qemu-pr-helper
%{_bindir}/qemu-pr-helper
%{_unitdir}/qemu-pr-helper.service
%{_unitdir}/qemu-pr-helper.socket
%{_mandir}/man8/qemu-pr-helper.8*

%files docs
%doc %{qemudocdir}

%files common
%license COPYING COPYING.LIB LICENSE
%{_mandir}/man7/qemu-qmp-ref.7*
%{_mandir}/man7/qemu-cpu-models.7*
%{_mandir}/man7/qemu-ga-ref.7*

%dir %{_datadir}/%{name}/
%{_datadir}/%{name}/keymaps/
%{_mandir}/man1/%{name}.1*
%{_mandir}/man7/qemu-block-drivers.7*
%attr(4755, -, -) %{_libexecdir}/qemu-bridge-helper
%config(noreplace) %{_sysconfdir}/sasl2/%{name}.conf
%ghost %{_sysconfdir}/kvm
%dir %{_sysconfdir}/%{name}
%config(noreplace) %{_sysconfdir}/%{name}/bridge.conf
%config(noreplace) %{_sysconfdir}/modprobe.d/vhost.conf
%config(noreplace) %{_sysconfdir}/modprobe.d/kvm.conf

%ifarch x86_64
    %{_datadir}/%{name}/linuxboot.bin
    %{_datadir}/%{name}/multiboot.bin
    %{_datadir}/%{name}/multiboot_dma.bin
    %{_datadir}/%{name}/kvmvapic.bin
    %{_datadir}/%{name}/pvh.bin
%endif
%ifarch s390x
    %{_datadir}/%{name}/s390-ccw.img
%endif
%{_datadir}/icons/*
%{_datadir}/%{name}/linuxboot_dma.bin
%if %{have_modules_load}
    %{_sysconfdir}/modules-load.d/kvm.conf
%endif
%if %{have_memlock_limits}
    %{_sysconfdir}/security/limits.d/95-kvm-memlock.conf
%endif

%files core
%{_libexecdir}/qemu-kvm
%{_datadir}/systemtap/tapset/qemu-kvm.stp
%{_datadir}/systemtap/tapset/qemu-kvm-log.stp
%{_datadir}/systemtap/tapset/qemu-kvm-simpletrace.stp
%{_datadir}/%{name}/systemtap/script.d/qemu_kvm.stp
%{_datadir}/%{name}/systemtap/conf.d/qemu_kvm.conf
%{_datadir}/systemtap/tapset/qemu-img*.stp
%{_datadir}/systemtap/tapset/qemu-io*.stp
%{_datadir}/systemtap/tapset/qemu-nbd*.stp
%{_datadir}/systemtap/tapset/qemu-storage-daemon*.stp

%ifarch x86_64
    %{_libdir}/%{name}/accel-tcg-%{kvm_target}.so
%endif

%files device-display-virtio-gpu
%{_libdir}/%{name}/hw-display-virtio-gpu.so

%ifarch s390x
%files device-display-virtio-gpu-ccw
    %{_libdir}/%{name}/hw-s390x-virtio-gpu-ccw.so
%else
%files device-display-virtio-gpu-pci
    %{_libdir}/%{name}/hw-display-virtio-gpu-pci.so
%endif

%ifarch x86_64 %{power64}
%files device-display-virtio-vga
    %{_libdir}/%{name}/hw-display-virtio-vga.so
%endif

%files tests
%{testsdir}
%{_libdir}/%{name}/accel-qtest-%{kvm_target}.so

%files block-blkio
%{_libdir}/%{name}/block-blkio.so

%files block-curl
%{_libdir}/%{name}/block-curl.so
%if %{have_block_rbd}
%files block-rbd
%{_libdir}/%{name}/block-rbd.so
%endif
%files audio-pa
%{_libdir}/%{name}/audio-pa.so

%if %{have_opengl}
%files ui-opengl
%{_libdir}/%{name}/ui-opengl.so
%files ui-egl-headless
%{_libdir}/%{name}/ui-egl-headless.so
%endif

%files device-usb-host
%{_libdir}/%{name}/hw-usb-host.so

%if %{have_usbredir}
%files device-usb-redirect
    %{_libdir}/%{name}/hw-usb-redirect.so
%endif

%files audio-dbus
%{_libdir}/%{name}/audio-dbus.so

%files ui-dbus
%{_libdir}/%{name}/ui-dbus.so

# endif !tools_only
%endif

%changelog
* Mon Jan 13 2025 Miroslav Rezanina <mrezanin@redhat.com> - 9.1.0-9
- kvm-qdev-Fix-set_pci_devfn-to-visit-option-only-once.patch [RHEL-39948]
- kvm-tests-avocado-hotplug_blk-Fix-addr-in-device_add-com.patch [RHEL-39948]
- kvm-qdev-monitor-avoid-QemuOpts-in-QMP-device_add.patch [RHEL-39948]
- kvm-vl-use-qmp_device_add-in-qemu_create_cli_devices.patch [RHEL-39948]
- Resolves: RHEL-39948
  (qom-get iothread-vq-mapping is empty on new hotplug disk [rhel-9.5])

* Mon Jan 06 2025 Miroslav Rezanina <mrezanin@redhat.com> - 9.1.0-8
- kvm-linux-headers-Update-to-Linux-v6.12-rc5.patch [RHEL-50212]
- kvm-s390x-cpumodel-add-msa10-subfunctions.patch [RHEL-50212]
- kvm-s390x-cpumodel-add-msa11-subfunctions.patch [RHEL-50212]
- kvm-s390x-cpumodel-add-msa12-changes.patch [RHEL-50212]
- kvm-s390x-cpumodel-add-msa13-subfunctions.patch [RHEL-50212]
- kvm-s390x-cpumodel-Add-ptff-Query-Time-Stamp-Event-QTSE-.patch [RHEL-50212]
- kvm-linux-headers-Update-to-Linux-6.13-rc1.patch [RHEL-50212]
- kvm-s390x-cpumodel-add-Concurrent-functions-facility-sup.patch [RHEL-50212]
- kvm-s390x-cpumodel-add-Vector-Enhancements-facility-3.patch [RHEL-50212]
- kvm-s390x-cpumodel-add-Miscellaneous-Instruction-Extensi.patch [RHEL-50212]
- kvm-s390x-cpumodel-add-Vector-Packed-Decimal-Enhancement.patch [RHEL-50212]
- kvm-s390x-cpumodel-add-Ineffective-nonconstrained-transa.patch [RHEL-50212]
- kvm-s390x-cpumodel-Add-Sequential-Instruction-Fetching-f.patch [RHEL-50212]
- kvm-s390x-cpumodel-correct-PLO-feature-wording.patch [RHEL-50212]
- kvm-s390x-cpumodel-Add-PLO-extension-facility.patch [RHEL-50212]
- kvm-s390x-cpumodel-gen17-model.patch [RHEL-50212]
- kvm-qga-skip-bind-mounts-in-fs-list.patch [RHEL-71940]
- kvm-vhost-fail-device-start-if-iotlb-update-fails.patch [RHEL-27832]
- kvm-hw-char-pl011-Use-correct-masks-for-IBRD-and-FBRD.patch [RHEL-67107]
- Resolves: RHEL-50212
  ([IBM 9.6 FEAT] KVM: CPU model for new IBM Z HW - qemu part)
- Resolves: RHEL-71940
  (qemu-ga cannot freeze filesystems with sentinelone)
- Resolves: RHEL-27832
  (The post-copy migration of RT-VM leads to race while accessing vhost-user device and hung/stalled target VM)
- Resolves: RHEL-67107
  ([aarch64] [rhel-9.6] Backport some important post 9.1 qemu fixes)

* Fri Dec 13 2024 Miroslav Rezanina <mrezanin@redhat.com> - 9.1.0-7
- kvm-migration-Allow-pipes-to-keep-working-for-fd-migrati.patch [RHEL-66089]
- Resolves: RHEL-66089
  (warning: fd: migration to a file is deprecated when create or revert a snapshot)

* Thu Dec 05 2024 Miroslav Rezanina <mrezanin@redhat.com> - 9.1.0-6
- kvm-virtio-net-Add-queues-before-loading-them.patch [RHEL-69477]
- kvm-docs-system-s390x-bootdevices-Update-loadparm-docume.patch [RHEL-68440]
- kvm-docs-system-bootindex-Make-it-clear-that-s390x-can-a.patch [RHEL-68440]
- kvm-hw-s390x-Restrict-loadparm-property-to-devices-that-.patch [RHEL-68440]
- kvm-hw-Add-loadparm-property-to-scsi-disk-devices-for-bo.patch [RHEL-68440]
- kvm-scsi-fix-allocation-for-s390x-loadparm.patch [RHEL-68440]
- kvm-pc-bios-s390x-Initialize-cdrom-type-to-false-for-eac.patch [RHEL-68440]
- kvm-pc-bios-s390x-Initialize-machine-loadparm-before-pro.patch [RHEL-68440]
- kvm-pc-bios-s390-ccw-Re-initialize-receive-queue-index-b.patch [RHEL-68440]
- kvm-vnc-fix-crash-when-no-console-attached.patch [RHEL-61633]
- Resolves: RHEL-69477
  (qemu crashed when migrate vm with multiqueue from rhel9.4 to rhel9.6)
- Resolves: RHEL-68440
  (The new "boot order" feature is sometimes not working as expected [RHEL 9])
- Resolves: RHEL-61633
  (Qemu-kvm  crashed  if  no display device setting and switching display by remote-viewer [rhel-9])

* Mon Nov 25 2024 Jon Maloy <jmaloy@redhat.com> - 9.1.0-5
- kvm-vfio-container-Fix-container-object-destruction.patch [RHEL-67935]
- kvm-hostmem-Apply-merge-property-after-the-memory-region.patch [RHEL-68289]
- Resolves: RHEL-67935
  (QEMU should fail gracefully with passthrough devices in SEV-SNP guests)
- Resolves: RHEL-68289
  ([RHEL-9.6] QEMU core dump on applying merge property to memory backend)

* Sun Nov 24 2024 Jon Maloy <jmaloy@redhat.com> - 9.1.0-4
- kvm-migration-Ensure-vmstate_save-sets-errp.patch [RHEL-67844]
- Resolves: RHEL-67844
  (qemu crashed after killed virtiofsd during migration)

* Tue Nov 19 2024 Miroslav Rezanina <mrezanin@redhat.com> - 9.1.0-3
- kvm-pc-q35-Bump-max_cpus-to-4096-vcpus.patch [RHEL-11043]
- kvm-kvm-replace-fprintf-with-error_report-printf-in-kvm_.patch [RHEL-57682]
- kvm-kvm-refactor-core-virtual-machine-creation-into-its-.patch [RHEL-57682]
- kvm-accel-kvm-refactor-dirty-ring-setup.patch [RHEL-57682]
- kvm-KVM-Dynamic-sized-kvm-memslots-array.patch [RHEL-57682]
- kvm-KVM-Define-KVM_MEMSLOTS_NUM_MAX_DEFAULT.patch [RHEL-57682]
- kvm-KVM-Rename-KVMMemoryListener.nr_used_slots-to-nr_slo.patch [RHEL-57682]
- kvm-KVM-Rename-KVMState-nr_slots-to-nr_slots_max.patch [RHEL-57682]
- kvm-Require-new-dtrace-package.patch [RHEL-67900]
- Resolves: RHEL-11043
  ([RFE] [HPEMC] [RHEL-9.6] qemu-kvm: support up to 4096 VCPUs)
- Resolves: RHEL-57682
  (Bad migration performance when performing vGPU VM live migration )
- Resolves: RHEL-67900
  (Failed to build qemu-kvm due to missing dtrace [rhel-9.6])

* Mon Nov 11 2024 Miroslav Rezanina <mrezanin@redhat.com> - 9.1.0-2
- kvm-hw-s390x-ipl-Provide-more-memory-to-the-s390-ccw.img.patch [RHEL-11424]
- kvm-pc-bios-s390-ccw-Use-the-libc-from-SLOF-and-remove-s.patch [RHEL-11424]
- kvm-pc-bios-s390-ccw-Link-the-netboot-code-into-the-main.patch [RHEL-11424]
- kvm-redhat-Remove-the-s390-netboot.img-from-the-spec-fil.patch [RHEL-11424]
- kvm-hw-s390x-Remove-the-possibility-to-load-the-s390-net.patch [RHEL-11424]
- kvm-pc-bios-s390-ccw-Merge-netboot.mak-into-the-main-Mak.patch [RHEL-11424]
- kvm-docs-system-s390x-bootdevices-Update-the-documentati.patch [RHEL-11424]
- kvm-pc-bios-s390-ccw-Remove-panics-from-ISO-IPL-path.patch [RHEL-11424]
- kvm-pc-bios-s390-ccw-Remove-panics-from-ECKD-IPL-path.patch [RHEL-11424]
- kvm-pc-bios-s390-ccw-Remove-panics-from-SCSI-IPL-path.patch [RHEL-11424]
- kvm-pc-bios-s390-ccw-Remove-panics-from-DASD-IPL-path.patch [RHEL-11424]
- kvm-pc-bios-s390-ccw-Remove-panics-from-Netboot-IPL-path.patch [RHEL-11424]
- kvm-pc-bios-s390-ccw-Enable-failed-IPL-to-return-after-e.patch [RHEL-11424]
- kvm-include-hw-s390x-Add-include-files-for-common-IPL-st.patch [RHEL-11424]
- kvm-s390x-Add-individual-loadparm-assignment-to-CCW-devi.patch [RHEL-11424]
- kvm-hw-s390x-Build-an-IPLB-for-each-boot-device.patch [RHEL-11424]
- kvm-s390x-Rebuild-IPLB-for-SCSI-device-directly-from-DIA.patch [RHEL-11424]
- kvm-pc-bios-s390x-Enable-multi-device-boot-loop.patch [RHEL-11424]
- kvm-docs-system-Update-documentation-for-s390x-IPL.patch [RHEL-11424]
- kvm-tests-qtest-Add-s390x-boot-order-tests-to-cdrom-test.patch [RHEL-11424]
- kvm-pc-bios-s390-ccw-Clarify-alignment-is-in-bytes.patch [RHEL-11424]
- kvm-pc-bios-s390-ccw-Don-t-generate-TEXTRELs.patch [RHEL-11424]
- kvm-pc-bios-s390-ccw-Introduce-EXTRA_LDFLAGS.patch [RHEL-11424]
- kvm-vfio-migration-Report-only-stop-copy-size-in-vfio_st.patch [RHEL-64307]
- kvm-vfio-migration-Change-trace-formats-from-hex-to-deci.patch [RHEL-64307]
- kvm-kvm-Allow-kvm_arch_get-put_registers-to-accept-Error.patch [RHEL-60914]
- kvm-target-i386-kvm-Report-which-action-failed-in-kvm_ar.patch [RHEL-60914]
- Resolves: RHEL-11424
  ([IBM 9.6 FEAT] KVM: Full boot order support - qemu part)
- Resolves: RHEL-64307
  (High threshold value observed in vGPU live migration)
- Resolves: RHEL-60914
  (Fail migration properly when put cpu register fails)

* Thu Sep 26 2024 Miroslav Rezanina <mrezanin@redhat.com> - 9.1.0-1
- Rebase to QEMU 9.1 [RHEL-41247]
- Resolves: RHEL-41247
  (Rebase qemu-9.1 for RHEL 9.6)
- 
* Mon Sep 02 2024 Miroslav Rezanina <mrezanin@redhat.com> - 9.0.0-10
- kvm-nbd-server-CVE-2024-7409-Avoid-use-after-free-when-c.patch [RHEL-52617]
- Resolves: RHEL-52617
  (CVE-2024-7409 qemu-kvm: Denial of Service via Improper Synchronization in QEMU NBD Server During Socket Closure [rhel-9.5])

* Mon Aug 26 2024 Miroslav Rezanina <mrezanin@redhat.com> - 9.0.0-9
- kvm-qemu-guest-agent-Update-the-logfile-path-of-qga-fsfr.patch [RHEL-52250]
- Resolves: RHEL-52250
  (fsfreeze hooks break on the systems first restorecon)

* Wed Aug 14 2024 Miroslav Rezanina <mrezanin@redhat.com> - 9.0.0-8
- kvm-introduce-pc_rhel_9_5_compat.patch [RHEL-39544]
- kvm-target-i386-add-guest-phys-bits-cpu-property.patch [RHEL-39544]
- kvm-kvm-add-support-for-guest-physical-bits.patch [RHEL-39544]
- kvm-i386-kvm-Move-architectural-CPUID-leaf-generation-to.patch [RHEL-39544]
- kvm-target-i386-Introduce-Icelake-Server-v7-to-enable-TS.patch [RHEL-39544]
- kvm-target-i386-Add-new-CPU-model-SierraForest.patch [RHEL-39544]
- kvm-target-i386-Export-RFDS-bit-to-guests.patch [RHEL-39544]
- kvm-pci-host-q35-Move-PAM-initialization-above-SMRAM-ini.patch [RHEL-39544]
- kvm-q35-Introduce-smm_ranges-property-for-q35-pci-host.patch [RHEL-39544]
- kvm-hw-i386-acpi-Set-PCAT_COMPAT-bit-only-when-pic-is-no.patch [RHEL-39544]
- kvm-confidential-guest-support-Add-kvm_init-and-kvm_rese.patch [RHEL-39544]
- kvm-i386-sev-Switch-to-use-confidential_guest_kvm_init.patch [RHEL-39544]
- kvm-ppc-pef-switch-to-use-confidential_guest_kvm_init-re.patch [RHEL-39544]
- kvm-s390-Switch-to-use-confidential_guest_kvm_init.patch [RHEL-39544]
- kvm-scripts-update-linux-headers-Add-setup_data.h-to-imp.patch [RHEL-39544]
- kvm-scripts-update-linux-headers-Add-bits.h-to-file-impo.patch [RHEL-39544]
- kvm-linux-headers-update-to-current-kvm-next.patch [RHEL-39544]
- kvm-runstate-skip-initial-CPU-reset-if-reset-is-not-actu.patch [RHEL-39544]
- kvm-KVM-track-whether-guest-state-is-encrypted.patch [RHEL-39544]
- kvm-KVM-remove-kvm_arch_cpu_check_are_resettable.patch [RHEL-39544]
- kvm-target-i386-introduce-x86-confidential-guest.patch [RHEL-39544]
- kvm-target-i386-Implement-mc-kvm_type-to-get-VM-type.patch [RHEL-39544]
- kvm-target-i386-SEV-use-KVM_SEV_INIT2-if-possible.patch [RHEL-39544]
- kvm-i386-sev-Add-legacy-vm-type-parameter-for-SEV-guest-.patch [RHEL-39544]
- kvm-hw-i386-sev-Use-legacy-SEV-VM-types-for-older-machin.patch [RHEL-39544]
- kvm-trace-kvm-Split-address-space-and-slot-id-in-trace_k.patch [RHEL-39544]
- kvm-kvm-Introduce-support-for-memory_attributes.patch [RHEL-39544]
- kvm-RAMBlock-Add-support-of-KVM-private-guest-memfd.patch [RHEL-39544]
- kvm-kvm-Enable-KVM_SET_USER_MEMORY_REGION2-for-memslot.patch [RHEL-39544]
- kvm-kvm-memory-Make-memory-type-private-by-default-if-it.patch [RHEL-39544]
- kvm-HostMem-Add-mechanism-to-opt-in-kvm-guest-memfd-via-.patch [RHEL-39544]
- kvm-RAMBlock-make-guest_memfd-require-uncoordinated-disc.patch [RHEL-39544]
- kvm-physmem-Introduce-ram_block_discard_guest_memfd_rang.patch [RHEL-39544]
- kvm-kvm-handle-KVM_EXIT_MEMORY_FAULT.patch [RHEL-39544]
- kvm-kvm-tdx-Don-t-complain-when-converting-vMMIO-region-.patch [RHEL-39544]
- kvm-kvm-tdx-Ignore-memory-conversion-to-shared-of-unassi.patch [RHEL-39544]
- kvm-hw-i386-x86-Eliminate-two-if-statements-in-x86_bios_.patch [RHEL-39544]
- kvm-hw-i386-Have-x86_bios_rom_init-take-X86MachineState-.patch [RHEL-39544]
- kvm-hw-i386-pc_sysfw-Remove-unused-parameter-from-pc_isa.patch [RHEL-39544]
- kvm-hw-i386-x86-Don-t-leak-isa-bios-memory-regions.patch [RHEL-39544]
- kvm-hw-i386-x86-Don-t-leak-pc.bios-memory-region.patch [RHEL-39544]
- kvm-hw-i386-x86-Extract-x86_isa_bios_init-from-x86_bios_.patch [RHEL-39544]
- kvm-hw-i386-pc_sysfw-Alias-rather-than-copy-isa-bios-reg.patch [RHEL-39544]
- kvm-i386-correctly-select-code-in-hw-i386-that-depends-o.patch [RHEL-39544]
- kvm-i386-pc-remove-unnecessary-MachineClass-overrides.patch [RHEL-39544]
- kvm-hw-i386-split-x86.c-in-multiple-parts.patch [RHEL-39544]
- kvm-scripts-update-linux-header.sh-be-more-src-tree-frie.patch [RHEL-39544]
- kvm-scripts-update-linux-headers.sh-Remove-temporary-dir.patch [RHEL-39544]
- kvm-scripts-update-linux-headers.sh-Fix-the-path-of-setu.patch [RHEL-39544]
- kvm-update-linux-headers-fix-forwarding-to-asm-generic-h.patch [RHEL-39544]
- kvm-update-linux-headers-move-pvpanic.h-to-correct-direc.patch [RHEL-39544]
- kvm-linux-headers-Update-to-current-kvm-next.patch [RHEL-39544]
- kvm-update-linux-headers-import-linux-kvm_para.h-header.patch [RHEL-39544]
- kvm-machine-allow-early-use-of-machine_require_guest_mem.patch [RHEL-39544]
- kvm-i386-sev-Replace-error_report-with-error_setg.patch [RHEL-39544]
- kvm-i386-sev-Introduce-sev-common-type-to-encapsulate-co.patch [RHEL-39544]
- kvm-i386-sev-Move-sev_launch_update-to-separate-class-me.patch [RHEL-39544]
- kvm-i386-sev-Move-sev_launch_finish-to-separate-class-me.patch [RHEL-39544]
- kvm-i386-sev-Introduce-sev-snp-guest-object.patch [RHEL-39544]
- kvm-i386-sev-Add-a-sev_snp_enabled-helper.patch [RHEL-39544]
- kvm-i386-sev-Add-sev_kvm_init-override-for-SEV-class.patch [RHEL-39544]
- kvm-i386-sev-Add-snp_kvm_init-override-for-SNP-class.patch [RHEL-39544]
- kvm-i386-cpu-Set-SEV-SNP-CPUID-bit-when-SNP-enabled.patch [RHEL-39544]
- kvm-i386-sev-Don-t-return-launch-measurements-for-SEV-SN.patch [RHEL-39544]
- kvm-i386-sev-Add-a-class-method-to-determine-KVM-VM-type.patch [RHEL-39544]
- kvm-i386-sev-Update-query-sev-QAPI-format-to-handle-SEV-.patch [RHEL-39544]
- kvm-i386-sev-Add-the-SNP-launch-start-context.patch [RHEL-39544]
- kvm-i386-sev-Add-handling-to-encrypt-finalize-guest-laun.patch [RHEL-39544]
- kvm-i386-sev-Set-CPU-state-to-protected-once-SNP-guest-p.patch [RHEL-39544]
- kvm-hw-i386-sev-Add-function-to-get-SEV-metadata-from-OV.patch [RHEL-39544]
- kvm-i386-sev-Add-support-for-populating-OVMF-metadata-pa.patch [RHEL-39544]
- kvm-i386-sev-Add-support-for-SNP-CPUID-validation.patch [RHEL-39544]
- kvm-hw-i386-sev-Add-support-to-encrypt-BIOS-when-SEV-SNP.patch [RHEL-39544]
- kvm-i386-sev-Invoke-launch_updata_data-for-SEV-class.patch [RHEL-39544]
- kvm-i386-sev-Invoke-launch_updata_data-for-SNP-class.patch [RHEL-39544]
- kvm-i386-kvm-Add-KVM_EXIT_HYPERCALL-handling-for-KVM_HC_.patch [RHEL-39544]
- kvm-i386-sev-Enable-KVM_HC_MAP_GPA_RANGE-hcall-for-SNP-g.patch [RHEL-39544]
- kvm-i386-sev-Extract-build_kernel_loader_hashes.patch [RHEL-39544]
- kvm-i386-sev-Reorder-struct-declarations.patch [RHEL-39544]
- kvm-i386-sev-Allow-measured-direct-kernel-boot-on-SNP.patch [RHEL-39544]
- kvm-memory-Introduce-memory_region_init_ram_guest_memfd.patch [RHEL-39544]
- kvm-hw-i386-sev-Use-guest_memfd-for-legacy-ROMs.patch [RHEL-39544]
- kvm-hw-i386-Add-support-for-loading-BIOS-using-guest_mem.patch [RHEL-39544]
- kvm-i386-sev-fix-unreachable-code-coverity-issue.patch [RHEL-39544]
- kvm-i386-sev-Move-SEV_COMMON-null-check-before-dereferen.patch [RHEL-39544]
- kvm-i386-sev-Return-when-sev_common-is-null.patch [RHEL-39544]
- kvm-target-i386-SEV-fix-formatting-of-CPUID-mismatch-mes.patch [RHEL-39544]
- kvm-i386-sev-Fix-error-message-in-sev_get_capabilities.patch [RHEL-39544]
- kvm-i386-sev-Fallback-to-the-default-SEV-device-if-none-.patch [RHEL-39544]
- kvm-i386-sev-Don-t-allow-automatic-fallback-to-legacy-KV.patch [RHEL-39544]
- kvm-target-i386-SEV-fix-mismatch-in-vcek-disabled-proper.patch [RHEL-39544]
- kvm-virtio-rng-block-max-bytes-0.patch [RHEL-50336]
- kvm-scsi-disk-Use-positive-return-value-for-status-in-dm.patch [RHEL-50000]
- kvm-scsi-block-Don-t-skip-callback-for-sgio-error-status.patch [RHEL-50000]
- kvm-scsi-disk-Add-warning-comments-that-host_status-erro.patch [RHEL-50000]
- kvm-scsi-disk-Always-report-RESERVATION_CONFLICT-to-gues.patch [RHEL-50000]
- kvm-nbd-server-Plumb-in-new-args-to-nbd_client_add.patch [RHEL-52617]
- kvm-nbd-server-CVE-2024-7409-Cap-default-max-connections.patch [RHEL-52617]
- kvm-nbd-server-CVE-2024-7409-Drop-non-negotiating-client.patch [RHEL-52617]
- kvm-nbd-server-CVE-2024-7409-Close-stray-clients-at-serv.patch [RHEL-52617]
- Resolves: RHEL-39544
  ([QEMU] Add support for AMD SEV-SNP to Qemu)
- Resolves: RHEL-50336
  (Fail to boot up the guest including vtpm and virtio-rng (max-bytes=0) devices)
- Resolves: RHEL-50000
  (scsi-block: Cannot setup Windows Failover Cluster, qemu crashes on assert)
- Resolves: RHEL-52617
  (CVE-2024-7409 qemu-kvm: Denial of Service via Improper Synchronization in QEMU NBD Server During Socket Closure [rhel-9.5])

* Mon Jul 15 2024 Miroslav Rezanina <mrezanin@redhat.com> - 9.0.0-7
- kvm-hw-virtio-Fix-the-de-initialization-of-vhost-user-de.patch [RHEL-40708]
- kvm-hw-arm-virt-Avoid-unexpected-warning-from-Linux-gues.patch [RHEL-39936]
- Resolves: RHEL-40708
  ([RHEL9.5.0][virtio_fs][s390x] after hot-unplug the vhost-user-fs-ccw device, the device is failed to hot-plug again )
- Resolves: RHEL-39936
  (ARCH_DMA_MINALIGN smaller than CTR_EL0.CWG (128 < 256) on FUJITSU)

* Thu Jul 04 2024 Miroslav Rezanina <mrezanin@redhat.com> - 9.0.0-6
- kvm-qcow2-Don-t-open-data_file-with-BDRV_O_NO_IO.patch [RHEL-35611]
- kvm-iotests-244-Don-t-store-data-file-with-protocol-in-i.patch [RHEL-35611]
- kvm-iotests-270-Don-t-store-data-file-with-json-prefix-i.patch [RHEL-35611]
- kvm-block-Parse-filenames-only-when-explicitly-requested.patch [RHEL-35611]
- Resolves: RHEL-35611
  (CVE-2024-4467 qemu-kvm: QEMU: 'qemu-img info' leads to host file read/write [rhel-9.5])

* Tue Jun 25 2024 Miroslav Rezanina <mrezanin@redhat.com> - 9.0.0-5
- kvm-linux-aio-add-IO_CMD_FDSYNC-command-support.patch [RHEL-42411]
- kvm-Revert-monitor-use-aio_co_reschedule_self.patch [RHEL-34618 RHEL-38697]
- kvm-aio-warn-about-iohandler_ctx-special-casing.patch [RHEL-34618 RHEL-38697]
- kvm-block-crypto-create-ciphers-on-demand.patch [RHEL-36159]
- kvm-crypto-block-drop-qcrypto_block_open-n_threads-argum.patch [RHEL-36159]
- Resolves: RHEL-42411
  (qemu-kvm: linux-aio: add support for IO_CMD_FDSYNC command)
- Resolves: RHEL-34618
  (aio=io_uring: Assertion failure `luringcb->co->ctx == s->aio_context' with block_resize)
- Resolves: RHEL-38697
  (aio=native: Assertion failure `laiocb->co->ctx == laiocb->ctx->aio_context' with block_resize)
- Resolves: RHEL-36159
  (qemu crash on Assertion `block->n_free_ciphers > 0' failed in guest installation with luks and iothread-vq-mapping)

* Mon Jun 17 2024 Miroslav Rezanina <mrezanin@redhat.com> - 9.0.0-4
- kvm-qio-Inherit-follow_coroutine_ctx-across-TLS.patch [RHEL-33440]
- kvm-iotests-test-NBD-TLS-iothread.patch [RHEL-33440]
- kvm-virtio-gpu-fix-v2-migration.patch [RHEL-34621]
- kvm-rhel-9.4.0-machine-type-compat-for-virtio-gpu-migrat.patch [RHEL-34621]
- Resolves: RHEL-33440
  (Qemu hang when quit dst vm after storage migration(nbd+tls))
- Resolves: RHEL-34621
  ([RHEL9.5.0][stable_guest_abi]Failed to migrate VM with (qemu) qemu-kvm: Missing section footer for 0000:00:01.0/virtio-gpu qemu-kvm: load of migration failed: Invalid argument)

* Tue May 21 2024 Miroslav Rezanina <mrezanin@redhat.com> - 9.0.0-3
- kvm-nbd-server-do-not-poll-within-a-coroutine-context.patch [RHEL-33440]
- kvm-nbd-server-Mark-negotiation-functions-as-coroutine_f.patch [RHEL-33440]
- Resolves: RHEL-33440
  (Qemu hang when quit dst vm after storage migration(nbd+tls))

* Tue May 07 2024 Miroslav Rezanina <mrezanin@redhat.com> - 9.0.0-2
- kvm-hw-arm-virt-Fix-spurious-call-to-arm_virt_compat_set.patch [RHEL-34945]
- kvm-Revert-x86-rhel-9.4.0-machine-type-compat-fix.patch [RHEL-30362]
- Resolves: RHEL-34945
  ([aarch64, kvm-unit-tests] all tests tagged as FAIL [qemu-kvm: GLib: g_ptr_array_add: assertion 'rarray' failed] )
- Resolves: RHEL-30362
  (Check/fix machine type compatibility for QEMU 9.0.0 [x86_64][rhel-9.5.0])


* Wed Apr 24 2024 Miroslav Rezanina <mrezanin@redhat.com> - 9.0.0-1
- Rebase to QEMU 9.0.0 [RHEL-28073]
- Resolves: RHEL-28073
  (Rebase qemu-kvm to QEMU 9.0.0 for RHEL 9.5)

* Tue Mar 26 2024 Miroslav Rezanina <mrezanin@redhat.com> - 8.2.0-11
- kvm-coroutine-cap-per-thread-local-pool-size.patch [RHEL-28947]
- kvm-coroutine-reserve-5-000-mappings.patch [RHEL-28947]
- Resolves: RHEL-28947
  (Qemu crashing with "failed to set up stack guard page: Cannot allocate memory")

* Thu Mar 21 2024 Miroslav Rezanina <mrezanin@redhat.com> - 8.2.0-10
- kvm-chardev-lower-priority-of-the-HUP-GSource-in-socket-.patch [RHEL-24614]
- kvm-Revert-chardev-char-socket-Fix-TLS-io-channels-sendi.patch [RHEL-24614]
- kvm-Revert-chardev-use-a-child-source-for-qio-input-sour.patch [RHEL-24614]
- Resolves: RHEL-24614
  ([RHEL9][chardev] qemu hit core dump while using TLS server from host to guest)

* Wed Mar 20 2024 Miroslav Rezanina <mrezanin@redhat.com> - 8.2.0-9
- kvm-mirror-Don-t-call-job_pause_point-under-graph-lock.patch [RHEL-28125]
- kvm-nbd-server-Fix-race-in-draining-the-export.patch [RHEL-28125]
- kvm-iotests-Add-test-for-reset-AioContext-switches-with-.patch [RHEL-28125]
- kvm-pc-smbios-fixup-manufacturer-product-version-to-matc.patch [RHEL-21705]
- Resolves: RHEL-28125
  (RHEL9.4 - KVM : Live migration of guest with multiple qcow devices remains incomplete.)
- Resolves: RHEL-21705
  (pc-q35-rhel9.4.0 does not provide proper computer information)

* Mon Mar 18 2024 Miroslav Rezanina <mrezanin@redhat.com> - 8.2.0-8
- kvm-ui-clipboard-mark-type-as-not-available-when-there-i.patch [RHEL-19629]
- kvm-ui-clipboard-add-asserts-for-update-and-request.patch [RHEL-19629]
- kvm-hw-i386-pc-Defer-smbios_set_defaults-to-machine_done.patch [RHEL-21705]
- kvm-Implement-base-of-SMBIOS-type-9-descriptor.patch [RHEL-21705]
- kvm-Implement-SMBIOS-type-9-v2.6.patch [RHEL-21705]
- kvm-smbios-cleanup-smbios_get_tables-from-legacy-handlin.patch [RHEL-21705]
- kvm-smbios-get-rid-of-smbios_smp_sockets-global.patch [RHEL-21705]
- kvm-smbios-get-rid-of-smbios_legacy-global.patch [RHEL-21705]
- kvm-smbios-avoid-mangling-user-provided-tables.patch [RHEL-21705]
- kvm-smbios-don-t-check-type4-structures-in-legacy-mode.patch [RHEL-21705]
- kvm-smbios-add-smbios_add_usr_blob_size-helper.patch [RHEL-21705]
- kvm-smbios-rename-expose-structures-bitmaps-used-by-both.patch [RHEL-21705]
- kvm-smbios-build-legacy-mode-code-only-for-pc-machine.patch [RHEL-21705]
- kvm-smbios-handle-errors-consistently.patch [RHEL-21705]
- kvm-smbios-get-rid-of-global-smbios_ep_type.patch [RHEL-21705]
- kvm-smbios-clear-smbios_type4_count-before-building-tabl.patch [RHEL-21705]
- kvm-smbios-extend-smbios-entry-point-type-with-auto-valu.patch [RHEL-21705]
- kvm-smbios-in-case-of-entry-point-is-auto-try-to-build-v.patch [RHEL-21705]
- kvm-smbios-error-out-when-building-type-4-table-is-not-p.patch [RHEL-21705]
- kvm-pc-q35-set-SMBIOS-entry-point-type-to-auto-by-defaul.patch [RHEL-21705]
- Resolves: RHEL-19629
  (CVE-2023-6683 qemu-kvm: QEMU: VNC: NULL pointer dereference in qemu_clipboard_request() [rhel-9])
- Resolves: RHEL-21705
  (pc-q35-rhel9.4.0 does not provide proper computer information)

* Fri Mar 08 2024 Miroslav Rezanina <mrezanin@redhat.com> - 8.2.0-7
- kvm-qemu_init-increase-NOFILE-soft-limit-on-POSIX.patch [RHEL-26049]
- kvm-chardev-char-socket-Fix-TLS-io-channels-sending-too-.patch [RHEL-24614]
- Resolves: RHEL-26049
  (When max vcpu is greater than or equal to 246, qemu unable to init event notifier)
- Resolves: RHEL-24614
  ([RHEL9][chardev][s390x] qemu hit core dump while using TLS server from host to guest)

* Mon Feb 19 2024 Miroslav Rezanina <mrezanin@redhat.com> - 8.2.0-6
- kvm-virtio-scsi-Attach-event-vq-notifier-with-no_poll.patch [RHEL-3934]
- kvm-virtio-Re-enable-notifications-after-drain.patch [RHEL-3934]
- kvm-virtio-blk-Use-ioeventfd_attach-in-start_ioeventfd.patch [RHEL-3934]
- kvm-virtio-blk-avoid-using-ioeventfd-state-in-irqfd-cond.patch [RHEL-15394]
- kvm-hw-arm-virt-deprecate-virt-rhel9.-0-2-.0-machine-typ.patch [RHEL-24988]
- Resolves: RHEL-3934
  ([qemu-kvm] Failed on repeatedly hotplug/unplug  disk iothread enabled  )
- Resolves: RHEL-15394
  (virtio-blk: qemu hang on "no response on QMP query-status" when write data to disk without enough space)
- Resolves: RHEL-24988
  (Mark virt-rhel9.{0,2}.0 machine types as deprecated)

* Mon Feb 12 2024 Miroslav Rezanina <mrezanin@redhat.com> - 8.2.0-5
- kvm-hv-balloon-use-get_min_alignment-to-express-32-GiB-a.patch [RHEL-20341]
- kvm-memory-device-reintroduce-memory-region-size-check.patch [RHEL-20341]
- kvm-block-backend-Allow-concurrent-context-changes.patch [RHEL-24593]
- kvm-scsi-Await-request-purging.patch [RHEL-24593]
- kvm-string-output-visitor-show-structs-as-omitted.patch [RHEL-17369 RHEL-20764 RHEL-7356]
- kvm-string-output-visitor-Fix-pseudo-struct-handling.patch [RHEL-17369 RHEL-20764 RHEL-7356]
- kvm-qdev-properties-alias-all-object-class-properties.patch [RHEL-17369 RHEL-20764 RHEL-7356]
- kvm-qdev-add-IOThreadVirtQueueMappingList-property-type.patch [RHEL-17369 RHEL-20764 RHEL-7356]
- kvm-virtio-blk-add-iothread-vq-mapping-parameter.patch [RHEL-17369 RHEL-20764 RHEL-7356]
- kvm-virtio-blk-Fix-potential-nullpointer-read-access-in-.patch [RHEL-17369 RHEL-20764 RHEL-7356]
- kvm-iotests-add-filter_qmp_generated_node_ids.patch [RHEL-17369 RHEL-20764 RHEL-7356]
- kvm-iotests-port-141-to-Python-for-reliable-QMP-testing.patch [RHEL-17369 RHEL-20764 RHEL-7356]
- kvm-monitor-only-run-coroutine-commands-in-qemu_aio_cont.patch [RHEL-17369 RHEL-20764 RHEL-7356]
- kvm-virtio-blk-move-dataplane-code-into-virtio-blk.c.patch [RHEL-17369 RHEL-20764 RHEL-7356]
- kvm-virtio-blk-rename-dataplane-create-destroy-functions.patch [RHEL-17369 RHEL-20764 RHEL-7356]
- kvm-virtio-blk-rename-dataplane-to-ioeventfd.patch [RHEL-17369 RHEL-20764 RHEL-7356]
- kvm-virtio-blk-restart-s-rq-reqs-in-vq-AioContexts.patch [RHEL-17369 RHEL-20764 RHEL-7356]
- kvm-virtio-blk-tolerate-failure-to-set-BlockBackend-AioC.patch [RHEL-17369 RHEL-20764 RHEL-7356]
- kvm-virtio-blk-always-set-ioeventfd-during-startup.patch [RHEL-17369 RHEL-20764 RHEL-7356]
- kvm-tests-unit-Bump-test-replication-timeout-to-60-secon.patch [RHEL-17369 RHEL-20764 RHEL-7356]
- kvm-iotests-iothreads-stream-Use-the-right-TimeoutError.patch [RHEL-17369 RHEL-20764 RHEL-7356]
- kvm-virtio-mem-default-enable-dynamic-memslots.patch [RHEL-24045]
- Resolves: RHEL-20341
  (memory-device size alignment check invalid in QEMU 8.2)
- Resolves: RHEL-24593
  (qemu crash blk_get_aio_context(BlockBackend *): Assertion `ctx == blk->ctx' when repeatedly hotplug/unplug disk)
- Resolves: RHEL-17369
  ([nfv virt][rt][post-copy migration] qemu-kvm: ../block/qcow2.c:5263: ImageInfoSpecific *qcow2_get_specific_info(BlockDriverState *, Error **): Assertion `false' failed.)
- Resolves: RHEL-20764
  ([qemu-kvm] Enable qemu multiqueue block layer support)
- Resolves: RHEL-7356
  ([qemu-kvm] no response with QMP command device_add when repeatedly hotplug/unplug virtio disks [RHEL-9])
- Resolves: RHEL-24045
  (QEMU: default-enable dynamically using multiple memslots for virtio-mem)

* Tue Jan 30 2024 Miroslav Rezanina <mrezanin@redhat.com> - 8.2.0-4
- kvm-vfio-pci-Clear-MSI-X-IRQ-index-always.patch [RHEL-21293]
- Resolves: RHEL-21293
  ([emulated igb] Failed to set up TRIGGER eventfd signaling for interrupt INTX-0: VFIO_DEVICE_SET_IRQS failure: Invalid argument)

* Wed Jan 24 2024 Miroslav Rezanina <mrezanin@redhat.com> - 8.2.0-3
- kvm-hw-arm-virt-Add-properties-to-disable-high-memory-re.patch [RHEL-19738]
- kvm-vfio-Introduce-base-object-for-VFIOContainer-and-tar.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-container-Introduce-a-empty-VFIOIOMMUOps.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-container-Switch-to-dma_map-unmap-API.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-common-Introduce-vfio_container_init-destroy-he.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-common-Move-giommu_list-in-base-container.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-container-Move-space-field-to-base-container.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-container-Switch-to-IOMMU-BE-set_dirty_page_tra.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-container-Move-per-container-device-list-in-bas.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-container-Convert-functions-to-base-container.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-container-Move-pgsizes-and-dma_max_mappings-to-.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-container-Move-vrdl_list-to-base-container.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-container-Move-listener-to-base-container.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-container-Move-dirty_pgsizes-and-max_dirty_bitm.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-container-Move-iova_ranges-to-base-container.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-container-Implement-attach-detach_device.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-spapr-Introduce-spapr-backend-and-target-interf.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-spapr-switch-to-spapr-IOMMU-BE-add-del_section_.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-spapr-Move-prereg_listener-into-spapr-container.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-spapr-Move-hostwin_list-into-spapr-container.patch [RHEL-19302 RHEL-21057]
- kvm-backends-iommufd-Introduce-the-iommufd-object.patch [RHEL-19302 RHEL-21057]
- kvm-util-char_dev-Add-open_cdev.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-common-return-early-if-space-isn-t-empty.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-iommufd-Implement-the-iommufd-backend.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-iommufd-Relax-assert-check-for-iommufd-backend.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-iommufd-Add-support-for-iova_ranges-and-pgsizes.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-pci-Extract-out-a-helper-vfio_pci_get_pci_hot_r.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-pci-Introduce-a-vfio-pci-hot-reset-interface.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-iommufd-Enable-pci-hot-reset-through-iommufd-cd.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-pci-Allow-the-selection-of-a-given-iommu-backen.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-pci-Make-vfio-cdev-pre-openable-by-passing-a-fi.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-platform-Allow-the-selection-of-a-given-iommu-b.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-platform-Make-vfio-cdev-pre-openable-by-passing.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-ap-Allow-the-selection-of-a-given-iommu-backend.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-ap-Make-vfio-cdev-pre-openable-by-passing-a-fil.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-ccw-Allow-the-selection-of-a-given-iommu-backen.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-ccw-Make-vfio-cdev-pre-openable-by-passing-a-fi.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-Make-VFIOContainerBase-poiner-parameter-const-i.patch [RHEL-19302 RHEL-21057]
- kvm-hw-arm-Activate-IOMMUFD-for-virt-machines.patch [RHEL-19302 RHEL-21057]
- kvm-kconfig-Activate-IOMMUFD-for-s390x-machines.patch [RHEL-19302 RHEL-21057]
- kvm-hw-i386-Activate-IOMMUFD-for-q35-machines.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-pci-Move-VFIODevice-initializations-in-vfio_ins.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-platform-Move-VFIODevice-initializations-in-vfi.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-ap-Move-VFIODevice-initializations-in-vfio_ap_i.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-ccw-Move-VFIODevice-initializations-in-vfio_ccw.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-Introduce-a-helper-function-to-initialize-VFIOD.patch [RHEL-19302 RHEL-21057]
- kvm-docs-devel-Add-VFIO-iommufd-backend-documentation.patch [RHEL-19302 RHEL-21057]
- kvm-hw-ppc-Kconfig-Imply-VFIO_PCI.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-spapr-Extend-VFIOIOMMUOps-with-a-release-handle.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-container-Introduce-vfio_legacy_setup-for-furth.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-container-Initialize-VFIOIOMMUOps-under-vfio_in.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-container-Introduce-a-VFIOIOMMU-QOM-interface.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-container-Introduce-a-VFIOIOMMU-legacy-QOM-inte.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-container-Intoduce-a-new-VFIOIOMMUClass-setup-h.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-spapr-Introduce-a-sPAPR-VFIOIOMMU-QOM-interface.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-iommufd-Introduce-a-VFIOIOMMU-iommufd-QOM-inter.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-spapr-Only-compile-sPAPR-IOMMU-support-when-nee.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-iommufd-Remove-CONFIG_IOMMUFD-usage.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-container-Replace-basename-with-g_path_get_base.patch [RHEL-19302 RHEL-21057]
- kvm-hw-vfio-fix-iteration-over-global-VFIODevice-list.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-iommufd-Remove-the-use-of-stat-to-check-file-ex.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-container-Rename-vfio_init_container-to-vfio_se.patch [RHEL-19302 RHEL-21057]
- kvm-vfio-migration-Add-helper-function-to-set-state-or-r.patch [RHEL-19302 RHEL-21057]
- kvm-backends-iommufd-Remove-check-on-number-of-backend-u.patch [RHEL-19302 RHEL-21057]
- kvm-backends-iommufd-Remove-mutex.patch [RHEL-19302 RHEL-21057]
- kvm-Compile-IOMMUFD-object-on-aarch64.patch [RHEL-19302 RHEL-21057]
- kvm-Compile-IOMMUFD-on-s390x.patch [RHEL-19302 RHEL-21057]
- kvm-Compile-IOMMUFD-on-x86_64.patch [RHEL-19302 RHEL-21057]
- kvm-target-s390x-kvm-pv-Provide-some-more-useful-informa.patch [RHEL-18212]
- kvm-nbd-server-avoid-per-NBDRequest-nbd_client_get-put.patch [RHEL-15965]
- kvm-nbd-server-only-traverse-NBDExport-clients-from-main.patch [RHEL-15965]
- kvm-nbd-server-introduce-NBDClient-lock-to-protect-field.patch [RHEL-15965]
- kvm-block-file-posix-set-up-Linux-AIO-and-io_uring-in-th.patch [RHEL-15965]
- kvm-virtio-blk-add-lock-to-protect-s-rq.patch [RHEL-15965]
- kvm-virtio-blk-don-t-lock-AioContext-in-the-completion-c.patch [RHEL-15965]
- kvm-virtio-blk-don-t-lock-AioContext-in-the-submission-c.patch [RHEL-15965]
- kvm-scsi-only-access-SCSIDevice-requests-from-one-thread.patch [RHEL-15965]
- kvm-virtio-scsi-don-t-lock-AioContext-around-virtio_queu.patch [RHEL-15965]
- kvm-scsi-don-t-lock-AioContext-in-I-O-code-path.patch [RHEL-15965]
- kvm-dma-helpers-don-t-lock-AioContext-in-dma_blk_cb.patch [RHEL-15965]
- kvm-virtio-scsi-replace-AioContext-lock-with-tmf_bh_lock.patch [RHEL-15965]
- kvm-scsi-assert-that-callbacks-run-in-the-correct-AioCon.patch [RHEL-15965]
- kvm-tests-remove-aio_context_acquire-tests.patch [RHEL-15965]
- kvm-aio-make-aio_context_acquire-aio_context_release-a-n.patch [RHEL-15965]
- kvm-graph-lock-remove-AioContext-locking.patch [RHEL-15965]
- kvm-block-remove-AioContext-locking.patch [RHEL-15965]
- kvm-block-remove-bdrv_co_lock.patch [RHEL-15965]
- kvm-scsi-remove-AioContext-locking.patch [RHEL-15965]
- kvm-aio-wait-draw-equivalence-between-AIO_WAIT_WHILE-and.patch [RHEL-15965]
- kvm-aio-remove-aio_context_acquire-aio_context_release-A.patch [RHEL-15965]
- kvm-docs-remove-AioContext-lock-from-IOThread-docs.patch [RHEL-15965]
- kvm-scsi-remove-outdated-AioContext-lock-comment.patch [RHEL-15965]
- kvm-job-remove-outdated-AioContext-locking-comments.patch [RHEL-15965]
- kvm-block-remove-outdated-AioContext-locking-comments.patch [RHEL-15965]
- kvm-block-coroutine-wrapper-use-qemu_get_current_aio_con.patch [RHEL-15965]
- kvm-s390x-pci-avoid-double-enable-disable-of-aif.patch [RHEL-21169]
- kvm-s390x-pci-refresh-fh-before-disabling-aif.patch [RHEL-21169]
- kvm-s390x-pci-drive-ISM-reset-from-subsystem-reset.patch [RHEL-21169]
- kvm-include-ui-rect.h-fix-qemu_rect_init-mis-assignment.patch [RHEL-21570]
- kvm-virtio-gpu-block-migration-of-VMs-with-blob-true.patch [RHEL-7565]
- kvm-spec-Enable-zstd.patch [RHEL-7361]
- Resolves: RHEL-19738
  (Enable properties allowing to disable high memory regions)
- Resolves: RHEL-19302
  (NVIDIA:Grace-Hopper Backport QEMU IOMMUFD Backend)
- Resolves: RHEL-21057
  (Request backport of 9353b6da430f90e47f352dbf6dc31120c8914da6)
- Resolves: RHEL-18212
  ([RHEL9][Secure-execution][s390x] The error message is not clear when boot up a SE guest with wrong encryption)
- Resolves: RHEL-15965
  ( [qemu-kvm] Remove AioContext lock (no response with QMP command block_resize))
- Resolves: RHEL-21169
  ([s390x] VM fails to start with ISM passed through QEMU 8.2)
- Resolves: RHEL-21570
  (Critical performance degradation for input devices in virtio vnc session)
- Resolves: RHEL-7565
  (qemu crashed when migrate guest with blob resources enabled)
- Resolves: RHEL-7361
  ([qemu-kvm] Enable zstd support for qcow2 files)

* Mon Jan 08 2024 Miroslav Rezanina <mrezanin@redhat.com> - 8.2.0-2
- kvm-hw-arm-virt-Fix-compats.patch [RHEL-17168]
- Resolves: RHEL-17168
  (Introduce virt-rhel9.4.0 arm-virt machine type [aarch64])

* Tue Jan 02 2024 Miroslav Rezanina <mrezanin@redhat.com> - 8.2.0-1
- Rebase to QEMU 8.2.0 [RHEL-14111]
- Fix machine type compatibility [RHEL-17067 RHEL-17068]
- Add 9.4.0 machine type [RHEL-17168 RHEL-19117 RHEL-19119]
- Resolves: RHEL-14111
  (Rebase qemu-kvm to QEMU 8.2.0)
- Resolves: RHEL-17067
  (Check/fix machine type compatibility for qemu-kvm 8.2.0 [s390x])
- Resolves: RHEL-17068
  (Check/fix machine type compatibility for qemu-kvm 8.2.0 [x86_64])
- Resolves: RHEL-17168
  (Introduce virt-rhel9.4.0 arm-virt machine type [aarch64])
- Resolves: RHEL-19117
  (Introduce virt-rhel9.4.0 arm-virt machine type [x86_64])
- Resolves: RHEL-19119
  (Introduce virt-rhel9.4.0 arm-virt machine type [s390x])

* Thu Nov 30 2023 Miroslav Rezanina <mrezanin@redhat.com> - 8.1.0-5
- kvm-Preparation-for-using-allow-rpcs-list-in-guest-agent.patch [RHEL-955]
- kvm-Use-allow-rpcs-instead-of-block-rpcs-in-guest-agent..patch [RHEL-955]
- Resolves: RHEL-955
  (Use allow-rpcs instead of block-rpcs in guest-agent.service)

* Mon Nov 13 2023 Miroslav Rezanina <mrezanin@redhat.com> - 8.1.0-4
- kvm-hw-scsi-scsi-disk-Disallow-block-sizes-smaller-than-.patch [RHEL-2828]
- kvm-Enable-igb-on-x86_64.patch [RHEL-1308]
- kvm-host-include-generic-host-atomic128-Fix-compilation-.patch [RHEL-12991]
- kvm-Enable-qemu-kvm-device-usb-redirec-for-aarch64.patch [RHEL-7561]
- Resolves: RHEL-2828
  (CVE-2023-42467 qemu-kvm: qemu: denial of service due to division by zero [rhel-9])
- Resolves: RHEL-1308
  ([RFE] iGB: Add an emulated SR-IOV network card)
- Resolves: RHEL-12991
  (qemu-kvm fails to build on s390x with clang-17)
- Resolves: RHEL-7561
  (Missing the rpm package qemu-kvm-device-usb-redirect on Arm64 platform)

* Mon Oct 16 2023 Miroslav Rezanina <mrezanin@redhat.com> - 8.1.0-3
- kvm-migration-Fix-race-that-dest-preempt-thread-close-to.patch [RHEL-11219]
- kvm-migration-Fix-possible-race-when-setting-rp_state.er.patch [RHEL-11219]
- kvm-migration-Fix-possible-races-when-shutting-down-the-.patch [RHEL-11219]
- kvm-migration-Fix-possible-race-when-shutting-down-to_ds.patch [RHEL-11219]
- kvm-migration-Remove-redundant-cleanup-of-postcopy_qemuf.patch [RHEL-11219]
- kvm-migration-Consolidate-return-path-closing-code.patch [RHEL-11219]
- kvm-migration-Replace-the-return-path-retry-logic.patch [RHEL-11219]
- kvm-migration-Move-return-path-cleanup-to-main-migration.patch [RHEL-11219]
- kvm-file-posix-Clear-bs-bl.zoned-on-error.patch [RHEL-7360]
- kvm-file-posix-Check-bs-bl.zoned-for-zone-info.patch [RHEL-7360]
- kvm-file-posix-Fix-zone-update-in-I-O-error-path.patch [RHEL-7360]
- kvm-file-posix-Simplify-raw_co_prw-s-out-zone-code.patch [RHEL-7360]
- kvm-tests-file-io-error-New-test.patch [RHEL-7360]
- Resolves: RHEL-11219
  (migration tests failing for RHEL 9.4 sometimes)
- Resolves: RHEL-7360
  (Qemu Core Dumped When Writing Larger Size Than The Size of A Data Disk)

* Mon Oct 02 2023 Miroslav Rezanina <mrezanin@redhat.com> - 8.1.0-2
- kvm-virtio-Drop-out-of-coroutine-context-in-virtio_load.patch [RHEL-832]
- Resolves: RHEL-832
  (qemu-kvm crashed when migrating guest with failover vf)

* Mon Sep 04 2023 Miroslav Rezanina <mrezanin@redhat.com> - 8.1.0-1
- Rebase to QEMU 8.1 [RHEL-870]
- Resolves: RHEL-870
  (Rebase qemu-kvm to QEMU 8.1.0)

* Thu Aug 24 2023 Miroslav Rezanina <mrezanin@redhat.com> - 8.0.0-13
- kvm-vdpa-return-errno-in-vhost_vdpa_get_vring_group-erro.patch [RHEL-923]
- kvm-vdpa-move-CVQ-isolation-check-to-net_init_vhost_vdpa.patch [RHEL-923]
- kvm-vdpa-use-first-queue-SVQ-state-for-CVQ-default.patch [RHEL-923]
- kvm-vdpa-export-vhost_vdpa_set_vring_ready.patch [RHEL-923]
- kvm-vdpa-rename-vhost_vdpa_net_load-to-vhost_vdpa_net_cv.patch [RHEL-923]
- kvm-vdpa-move-vhost_vdpa_set_vring_ready-to-the-caller.patch [RHEL-923]
- kvm-vdpa-remove-net-cvq-migration-blocker.patch [RHEL-923]
- Resolves: RHEL-923
  (vhost shadow virtqueue: state restore through CVQ)

* Mon Aug 21 2023 Miroslav Rezanina <mrezanin@redhat.com> - 8.0.0-12
- kvm-target-i386-allow-versioned-CPUs-to-specify-new-cach.patch [bz#2094913]
- kvm-target-i386-Add-new-EPYC-CPU-versions-with-updated-c.patch [bz#2094913]
- kvm-target-i386-Add-a-couple-of-feature-bits-in-8000_000.patch [bz#2094913]
- kvm-target-i386-Add-feature-bits-for-CPUID_Fn80000021_EA.patch [bz#2094913]
- kvm-target-i386-Add-missing-feature-bits-in-EPYC-Milan-m.patch [bz#2094913]
- kvm-target-i386-Add-VNMI-and-automatic-IBRS-feature-bits.patch [bz#2094913]
- kvm-target-i386-Add-EPYC-Genoa-model-to-support-Zen-4-pr.patch [bz#2094913]
- Resolves: bz#2094913
  (Add EPYC-Genoa CPU model in qemu)

* Mon Aug 07 2023 Miroslav Rezanina <mrezanin@redhat.com> - 8.0.0-11
- kvm-block-blkio-enable-the-completion-eventfd.patch [bz#2225354 bz#2225439]
- kvm-block-blkio-do-not-use-open-flags-in-qemu_open.patch [bz#2225354 bz#2225439]
- kvm-block-blkio-move-blkio_connect-in-the-drivers-functi.patch [bz#2225354 bz#2225439]
- kvm-block-blkio-retry-blkio_connect-if-it-fails-using-fd.patch [bz#2225354 bz#2225439]
- kvm-block-blkio-fall-back-on-using-path-when-fd-setting-.patch [bz#2225354 bz#2225439]
- kvm-block-blkio-use-blkio_set_int-fd-to-check-fd-support.patch [bz#2225354 bz#2225439]
- kvm-hw-virtio-iommu-Fix-potential-OOB-access-in-virtio_i.patch [bz#2229133]
- kvm-virtio-iommu-Standardize-granule-extraction-and-form.patch [bz#2229133]
- kvm-hw-arm-smmu-Handle-big-endian-hosts-correctly.patch [bz#2229133]
- kvm-qapi-i386-sev-Change-the-reduced-phys-bits-value-fro.patch [bz#2214839]
- kvm-qemu-options.hx-Update-the-reduced-phys-bits-documen.patch [bz#2214839]
- kvm-i386-sev-Update-checks-and-information-related-to-re.patch [bz#2214839]
- kvm-i386-cpu-Update-how-the-EBX-register-of-CPUID-0x8000.patch [bz#2214839]
- kvm-Provide-elf2dmp-binary-in-qemu-tools.patch [bz#2165917]
- Resolves: bz#2225354
  ([vdpa-blk] The new driver virtio-blk-vhost-user not work in VM booting)
- Resolves: bz#2225439
  ([vdpa-blk] read-only=on option not work on driver virtio-blk-vhost-vdpa)
- Resolves: bz#2229133
  (Backport some virtio-iommu and smmu fixes)
- Resolves: bz#2214839
  ([AMDSERVER 9.3 Bug] Qemu SEV reduced-phys-bits fixes)
- Resolves: bz#2165917
  (qemu-kvm: contrib/elf2dmp: Windows Server 2022 support)

* Mon Jul 31 2023 Miroslav Rezanina <mrezanin@redhat.com> - 8.0.0-10
- kvm-util-iov-Make-qiov_slice-public.patch [bz#2174676]
- kvm-block-Collapse-padded-I-O-vecs-exceeding-IOV_MAX.patch [bz#2174676]
- kvm-util-iov-Remove-qemu_iovec_init_extended.patch [bz#2174676]
- kvm-iotests-iov-padding-New-test.patch [bz#2174676]
- kvm-block-Fix-pad_request-s-request-restriction.patch [bz#2174676]
- kvm-vdpa-do-not-block-migration-if-device-has-cvq-and-x-.patch [RHEL-573]
- kvm-virtio-net-correctly-report-maximum-tx_queue_size-va.patch [bz#2040509]
- kvm-hw-pci-Disable-PCI_ERR_UNCOR_MASK-reg-for-machine-ty.patch [bz#2223691]
- kvm-vhost-vdpa-mute-unaligned-memory-error-report.patch [bz#2141965]
- Resolves: bz#2174676
  (Guest hit EXT4-fs error on host 4K disk  when repeatedly hot-plug/unplug running IO disk [RHEL9])
- Resolves: RHEL-573
  ([mlx vhost_vdpa][rhel 9.3]live migration fail with "net vdpa cannot migrate with CVQ feature")
- Resolves: bz#2040509
  ([RFE]:Add support for changing "tx_queue_size" to a setable value)
- Resolves: bz#2223691
  ([machine type 9.2]Failed to migrate VM from RHEL 9.3 to RHEL 9.2)
- Resolves: bz#2141965
  ([TPM][vhost-vdpa][rhel9.2]Boot a guest with "vhost-vdpa + TPM emulator", qemu output: qemu-kvm: vhost_vdpa_listener_region_add received unaligned region)

* Mon Jul 24 2023 Miroslav Rezanina <mrezanin@redhat.com> - 8.0.0-9
- kvm-scsi-fetch-unit-attention-when-creating-the-request.patch [bz#2176702]
- kvm-scsi-cleanup-scsi_clear_unit_attention.patch [bz#2176702]
- kvm-scsi-clear-unit-attention-only-for-REPORT-LUNS-comma.patch [bz#2176702]
- kvm-s390x-ap-Wire-up-the-device-request-notifier-interfa.patch [RHEL-794]
- kvm-multifd-Create-property-multifd-flush-after-each-sec.patch [bz#2196295]
- kvm-multifd-Protect-multifd_send_sync_main-calls.patch [bz#2196295]
- kvm-multifd-Only-flush-once-each-full-round-of-memory.patch [bz#2196295]
- kvm-net-socket-prepare-to-cleanup-net_init_socket.patch [RHEL-582]
- kvm-net-socket-move-fd-type-checking-to-its-own-function.patch [RHEL-582]
- kvm-net-socket-remove-net_init_socket.patch [RHEL-582]
- kvm-pcie-Add-hotplug-detect-state-register-to-cmask.patch [bz#2215819]
- kvm-spec-Build-DBUS-display.patch [bz#2207940]
- Resolves: bz#2176702
  ([RHEL9][virtio-scsi] scsi-hd cannot hot-plug successfully after hot-plug it repeatly)
- Resolves: RHEL-794
  (Backport s390x fixes from QEMU 8.1)
- Resolves: bz#2196295
  (Multifd flushes its channels 10 times per second)
- Resolves: RHEL-582
  ([passt][rhel 9.3] qemu core dump occurs when guest is shutdown after hotunplug/hotplug a passt interface)
- Resolves: bz#2215819
  (Migration test failed while guest with PCIe devices)
- Resolves: bz#2207940
  ([RFE] Enable qemu-ui-dbus subpackage)

* Mon Jul 17 2023 Miroslav Rezanina <mrezanin@redhat.com> - 8.0.0-8
- kvm-virtio-iommu-Fix-64kB-host-page-size-VFIO-device-ass.patch [bz#2211609 bz#2211634]
- kvm-virtio-iommu-Rework-the-traces-in-virtio_iommu_set_p.patch [bz#2211609 bz#2211634]
- kvm-vfio-pci-add-support-for-VF-token.patch [bz#2192818]
- kvm-vfio-migration-Skip-log_sync-during-migration-SETUP-.patch [bz#2192818]
- kvm-vfio-pci-Static-Resizable-BAR-capability.patch [bz#2192818]
- kvm-vfio-pci-Fix-a-use-after-free-issue.patch [bz#2192818]
- kvm-util-vfio-helpers-Use-g_file_read_link.patch [bz#2192818]
- kvm-migration-Make-all-functions-check-have-the-same-for.patch [bz#2192818]
- kvm-migration-Move-migration_properties-to-options.c.patch [bz#2192818]
- kvm-migration-Add-switchover-ack-capability.patch [bz#2192818]
- kvm-migration-Implement-switchover-ack-logic.patch [bz#2192818]
- kvm-migration-Enable-switchover-ack-capability.patch [bz#2192818]
- kvm-vfio-migration-Refactor-vfio_save_block-to-return-sa.patch [bz#2192818]
- kvm-vfio-migration-Store-VFIO-migration-flags-in-VFIOMig.patch [bz#2192818]
- kvm-vfio-migration-Add-VFIO-migration-pre-copy-support.patch [bz#2192818]
- kvm-vfio-migration-Add-support-for-switchover-ack-capabi.patch [bz#2192818]
- kvm-vfio-Implement-a-common-device-info-helper.patch [bz#2192818]
- kvm-hw-vfio-pci-quirks-Support-alternate-offset-for-GPUD.patch [bz#2192818]
- kvm-vfio-pci-Call-vfio_prepare_kvm_msi_virq_batch-in-MSI.patch [bz#2192818]
- kvm-vfio-migration-Reset-bytes_transferred-properly.patch [bz#2192818]
- kvm-vfio-migration-Make-VFIO-migration-non-experimental.patch [bz#2192818]
- kvm-vfio-pci-Fix-a-segfault-in-vfio_realize.patch [bz#2192818]
- kvm-vfio-pci-Free-leaked-timer-in-vfio_realize-error-pat.patch [bz#2192818]
- kvm-hw-vfio-pci-quirks-Sanitize-capability-pointer.patch [bz#2192818]
- kvm-vfio-pci-Disable-INTx-in-vfio_realize-error-path.patch [bz#2192818]
- kvm-vfio-migration-Change-vIOMMU-blocker-from-global-to-.patch [bz#2192818]
- kvm-vfio-migration-Free-resources-when-vfio_migration_re.patch [bz#2192818]
- kvm-vfio-migration-Remove-print-of-Migration-disabled.patch [bz#2192818]
- kvm-vfio-migration-Return-bool-type-for-vfio_migration_r.patch [bz#2192818]
- kvm-vfio-Fix-null-pointer-dereference-bug-in-vfio_bars_f.patch [bz#2192818]
- kvm-pc-bios-s390-ccw-Makefile-Use-z-noexecstack-to-silen.patch [bz#2220866]
- kvm-pc-bios-s390-ccw-Fix-indentation-in-start.S.patch [bz#2220866]
- kvm-pc-bios-s390-ccw-Provide-space-for-initial-stack-fra.patch [bz#2220866]
- kvm-pc-bios-s390-ccw-Don-t-use-__bss_start-with-the-larl.patch [bz#2220866]
- kvm-ui-Fix-pixel-colour-channel-order-for-PNG-screenshot.patch [bz#2222579]
- kvm-block-blkio-fix-module_block.py-parsing.patch [bz#2213317]
- kvm-Fix-virtio-blk-vhost-vdpa-typo-in-spec-file.patch [bz#2213317]
- Resolves: bz#2211609
  (With virtio-iommu and vfio-pci, qemu reports "warning: virtio-iommu page mask 0xfffffffffffff000 does not match 0x40201000")
- Resolves: bz#2211634
  ([aarch64] With virtio-iommu and vfio-pci, qemu coredump when host using kernel-64k package)
- Resolves: bz#2192818
  ([VFIO LM] Live migration)
- Resolves: bz#2220866
  (Misaligned symbol for s390-ccw image during qemu-kvm build)
- Resolves: bz#2222579
  (PNG screendump doesn't save screen correctly)
- Resolves: bz#2213317
  (Enable libblkio-based block drivers in QEMU)

* Mon Jul 10 2023 Miroslav Rezanina <mrezanin@redhat.com> - 8.0.0-7
- kvm-numa-Validate-cluster-and-NUMA-node-boundary-if-requ.patch [bz#2171363]
- kvm-hw-arm-Validate-cluster-and-NUMA-node-boundary.patch [bz#2171363]
- kvm-hw-arm-virt-Validate-cluster-and-NUMA-node-boundary-.patch [bz#2171363]
- kvm-vhost-fix-vhost_dev_enable_notifiers-error-case.patch [RHEL-330]
- kvm-kvm-reuse-per-vcpu-stats-fd-to-avoid-vcpu-interrupti.patch [bz#2218644]
- kvm-vhost-vdpa-do-not-cleanup-the-vdpa-vhost-net-structu.patch [bz#2128929]
- Resolves: bz#2171363
  ([aarch64] Kernel hits Call trace with irregular CPU-to-NUMA association)
- Resolves: RHEL-330
  ([virtual network][qemu-kvm-8.0.0-rc1]qemu core dump: qemu-kvm: ../softmmu/memory.c:2592: void memory_region_del_eventfd(MemoryRegion *, hwaddr, unsigned int, _Bool, uint64_t, EventNotifier *): Assertion `i != mr->ioeventfd_nb' failed)
- Resolves: bz#2218644
  (query-stats QMP command interrupts vcpus, the Max Latencies could be more than 100us (rhel 9.3.0 clone))
- Resolves: bz#2128929
  ([rhel9.2] hotplug/hotunplug mlx vdpa device to the occupied addr port, then qemu core dump occurs after shutdown guest)

* Mon Jun 26 2023 Miroslav Rezanina <mrezanin@redhat.com> - 8.0.0-6
- kvm-target-i386-add-support-for-FLUSH_L1D-feature.patch [bz#2216201]
- kvm-target-i386-add-support-for-FB_CLEAR-feature.patch [bz#2216201]
- kvm-block-blkio-use-qemu_open-to-support-fd-passing-for-.patch [bz#2180076]
- kvm-qapi-add-fdset-feature-for-BlockdevOptionsVirtioBlkV.patch [bz#2180076]
- kvm-Enable-libblkio-block-drivers.patch [bz#2213317]
- Resolves: bz#2216201
  ([qemu-kvm]VM reports vulnerabilty to mmio_stale_data on patched host with microcode)
- Resolves: bz#2180076
  ([qemu-kvm] support fd passing for libblkio QEMU BlockDrivers)
- Resolves: bz#2213317
  (Enable libblkio-based block drivers in QEMU)

* Tue Jun 13 2023 Miroslav Rezanina <mrezanin@redhat.com> - 8.0.0-5
- kvm-block-compile-out-assert_bdrv_graph_readable-by-defa.patch [bz#2186725]
- kvm-graph-lock-Disable-locking-for-now.patch [bz#2186725]
- kvm-nbd-server-Fix-drained_poll-to-wake-coroutine-in-rig.patch [bz#2186725]
- kvm-iotests-Test-commit-with-iothreads-and-ongoing-I-O.patch [bz#2186725]
- kvm-memory-prevent-dma-reentracy-issues.patch [RHEL-516]
- kvm-async-Add-an-optional-reentrancy-guard-to-the-BH-API.patch [RHEL-516]
- kvm-checkpatch-add-qemu_bh_new-aio_bh_new-checks.patch [RHEL-516]
- kvm-hw-replace-most-qemu_bh_new-calls-with-qemu_bh_new_g.patch [RHEL-516]
- kvm-lsi53c895a-disable-reentrancy-detection-for-script-R.patch [RHEL-516]
- kvm-bcm2835_property-disable-reentrancy-detection-for-io.patch [RHEL-516]
- kvm-raven-disable-reentrancy-detection-for-iomem.patch [RHEL-516]
- kvm-apic-disable-reentrancy-detection-for-apic-msi.patch [RHEL-516]
- kvm-async-avoid-use-after-free-on-re-entrancy-guard.patch [RHEL-516]
- kvm-loongarch-mark-loongarch_ipi_iocsr-re-entrnacy-safe.patch [RHEL-516]
- kvm-memory-stricter-checks-prior-to-unsetting-engaged_in.patch [RHEL-516]
- kvm-lsi53c895a-disable-reentrancy-detection-for-MMIO-reg.patch [RHEL-516]
- kvm-hw-scsi-lsi53c895a-Fix-reentrancy-issues-in-the-LSI-.patch [RHEL-516]
- kvm-hw-pci-Disable-PCI_ERR_UNCOR_MASK-register-for-machi.patch [bz#2189423]
- kvm-multifd-Fix-the-number-of-channels-ready.patch [bz#2196289]
- kvm-util-async-teardown-wire-up-query-command-line-optio.patch [bz#2168500]
- kvm-s390x-pv-Fix-spurious-warning-with-asynchronous-tear.patch [bz#2168500]
- Resolves: bz#2186725
  (Qemu hang when commit during fio running(iothread enable))
- Resolves: RHEL-516
  (CVE-2023-2680 qemu-kvm: QEMU: hcd-ehci: DMA reentrancy issue (incomplete fix for CVE-2021-3750) [rhel-9])
- Resolves: bz#2189423
  (Failed to migrate VM from rhel 9.3 to rhel 9.2)
- Resolves: bz#2196289
  (Fix number of ready channels on multifd)
- Resolves: bz#2168500
  ([IBM 9.3 FEAT] KVM: Improve memory reclaiming for z15 Secure Execution guests - qemu part)

* Mon May 22 2023 Miroslav Rezanina <mrezanin@redhat.com> - 8.0.0-4
- kvm-migration-Attempt-disk-reactivation-in-more-failure-.patch [bz#2058982]
- kvm-util-mmap-alloc-qemu_fd_getfs.patch [bz#2057267]
- kvm-vl.c-Create-late-backends-before-migration-object.patch [bz#2057267]
- kvm-migration-postcopy-Detect-file-system-on-dest-host.patch [bz#2057267]
- kvm-migration-mark-mixed-functions-that-can-suspend.patch [bz#2057267]
- kvm-postcopy-ram-do-not-use-qatomic_mb_read.patch [bz#2057267]
- kvm-migration-remove-extra-whitespace-character-for-code.patch [bz#2057267]
- kvm-migration-Merge-ram_counters-and-ram_atomic_counters.patch [bz#2057267]
- kvm-migration-Update-atomic-stats-out-of-the-mutex.patch [bz#2057267]
- kvm-migration-Make-multifd_bytes-atomic.patch [bz#2057267]
- kvm-migration-Make-dirty_sync_missed_zero_copy-atomic.patch [bz#2057267]
- kvm-migration-Make-precopy_bytes-atomic.patch [bz#2057267]
- kvm-migration-Make-downtime_bytes-atomic.patch [bz#2057267]
- kvm-migration-Make-dirty_sync_count-atomic.patch [bz#2057267]
- kvm-migration-Make-postcopy_requests-atomic.patch [bz#2057267]
- kvm-migration-Rename-duplicate-to-zero_pages.patch [bz#2057267]
- kvm-migration-Rename-normal-to-normal_pages.patch [bz#2057267]
- kvm-migration-rename-enabled_capabilities-to-capabilitie.patch [bz#2057267]
- kvm-migration-Pass-migrate_caps_check-the-old-and-new-ca.patch [bz#2057267]
- kvm-migration-move-migration_global_dump-to-migration-hm.patch [bz#2057267]
- kvm-spice-move-client_migrate_info-command-to-ui.patch [bz#2057267]
- kvm-migration-Create-migrate_cap_set.patch [bz#2057267]
- kvm-migration-Create-options.c.patch [bz#2057267]
- kvm-migration-Move-migrate_colo_enabled-to-options.c.patch [bz#2057267]
- kvm-migration-Move-migrate_use_compression-to-options.c.patch [bz#2057267]
- kvm-migration-Move-migrate_use_events-to-options.c.patch [bz#2057267]
- kvm-migration-Move-migrate_use_multifd-to-options.c.patch [bz#2057267]
- kvm-migration-Move-migrate_use_zero_copy_send-to-options.patch [bz#2057267]
- kvm-migration-Move-migrate_use_xbzrle-to-options.c.patch [bz#2057267]
- kvm-migration-Move-migrate_use_block-to-options.c.patch [bz#2057267]
- kvm-migration-Move-migrate_use_return-to-options.c.patch [bz#2057267]
- kvm-migration-Create-migrate_rdma_pin_all-function.patch [bz#2057267]
- kvm-migration-Move-migrate_caps_check-to-options.c.patch [bz#2057267]
- kvm-migration-Move-qmp_query_migrate_capabilities-to-opt.patch [bz#2057267]
- kvm-migration-Move-qmp_migrate_set_capabilities-to-optio.patch [bz#2057267]
- kvm-migration-Move-migrate_cap_set-to-options.c.patch [bz#2057267]
- kvm-migration-Move-parameters-functions-to-option.c.patch [bz#2057267]
- kvm-migration-Use-migrate_max_postcopy_bandwidth.patch [bz#2057267]
- kvm-migration-Move-migrate_use_block_incremental-to-opti.patch [bz#2057267]
- kvm-migration-Create-migrate_throttle_trigger_threshold.patch [bz#2057267]
- kvm-migration-Create-migrate_checkpoint_delay.patch [bz#2057267]
- kvm-migration-Create-migrate_max_cpu_throttle.patch [bz#2057267]
- kvm-migration-Move-migrate_announce_params-to-option.c.patch [bz#2057267]
- kvm-migration-Create-migrate_cpu_throttle_initial-to-opt.patch [bz#2057267]
- kvm-migration-Create-migrate_cpu_throttle_increment-func.patch [bz#2057267]
- kvm-migration-Create-migrate_cpu_throttle_tailslow-funct.patch [bz#2057267]
- kvm-migration-Move-migrate_postcopy-to-options.c.patch [bz#2057267]
- kvm-migration-Create-migrate_max_bandwidth-function.patch [bz#2057267]
- kvm-migration-Move-migrate_use_tls-to-options.c.patch [bz#2057267]
- kvm-migration-Move-qmp_migrate_set_parameters-to-options.patch [bz#2057267]
- kvm-migration-Allow-postcopy_ram_supported_by_host-to-re.patch [bz#2057267]
- kvm-block-bdrv-blk_co_unref-for-calls-in-coroutine-conte.patch [bz#2185688]
- kvm-block-Don-t-call-no_coroutine_fns-in-qmp_block_resiz.patch [bz#2185688]
- kvm-iotests-Use-alternative-CPU-type-that-is-not-depreca.patch [bz#2185688]
- kvm-iotests-Test-resizing-image-attached-to-an-iothread.patch [bz#2185688]
- kvm-Enable-Linux-io_uring.patch [bz#1947230]
- Resolves: bz#2058982
  (Qemu core dump if cut off nfs storage during migration)
- Resolves: bz#2057267
  (Migration with postcopy fail when vm set with shared memory)
- Resolves: bz#2185688
  ([qemu-kvm] no response with QMP command block_resize)
- Resolves: bz#1947230
  (Enable QEMU support for io_uring in RHEL9)

* Mon May 15 2023 Miroslav Rezanina <mrezanin@redhat.com> - 8.0.0-3
- kvm-migration-Handle-block-device-inactivation-failures-.patch [bz#2058982]
- kvm-migration-Minor-control-flow-simplification.patch [bz#2058982]
- Resolves: bz#2058982
  (Qemu core dump if cut off nfs storage during migration)

* Mon May 08 2023 Miroslav Rezanina <mrezanin@redhat.com> - 8.0.0-2
- kvm-acpi-pcihp-allow-repeating-hot-unplug-requests.patch [bz#2087047]
- kvm-hw-acpi-limit-warning-on-acpi-table-size-to-pc-machi.patch [bz#1934134]
- kvm-hw-acpi-Mark-acpi-blobs-as-resizable-on-RHEL-pc-mach.patch [bz#1934134]
- Resolves: bz#2087047
  (Disk detach is unsuccessful while the guest is still booting)
- Resolves: bz#1934134
  (ACPI table limits warning when booting guest with 512 VCPUs)

* Thu Apr 20 2023 Miroslav Rezanina <mrezanin@redhat.com> - 8.0.0-1
- Rebase to QEMU 8.0.0
- Resolves: bz#2180898
  (Rebase to QEMU 8.0.0 for RHEL 9.3.0)

* Mon Mar 20 2023 Miroslav Rezanina <mrezanin@redhat.com> - 7.2.0-14
- Rebuild for 9.2 release
- Resolves: bz#2173590
  (bugs in emulation of BMI instructions (for libguestfs without KVM))
- Resolves: bz#2156876
  ([virtual network][rhel7.9_guest] qemu-kvm: vhost vring error in virtqueue 1: Invalid argument (22))

* Mon Mar 20 2023 Miroslav Rezanina <mrezanin@redhat.com> - 7.2.0-13
- kvm-target-i386-fix-operand-size-of-unary-SSE-operations.patch [bz#2173590]
- kvm-tests-tcg-i386-Introduce-and-use-reg_t-consistently.patch [bz#2173590]
- kvm-target-i386-Fix-BEXTR-instruction.patch [bz#2173590]
- kvm-target-i386-Fix-C-flag-for-BLSI-BLSMSK-BLSR.patch [bz#2173590]
- kvm-target-i386-fix-ADOX-followed-by-ADCX.patch [bz#2173590]
- kvm-target-i386-Fix-32-bit-AD-CO-X-insns-in-64-bit-mode.patch [bz#2173590]
- kvm-target-i386-Fix-BZHI-instruction.patch [bz#2173590]
- kvm-intel-iommu-fail-DEVIOTLB_UNMAP-without-dt-mode.patch [bz#2156876]
- Resolves: bz#2173590
  (bugs in emulation of BMI instructions (for libguestfs without KVM))
- Resolves: bz#2156876
  ([virtual network][rhel7.9_guest] qemu-kvm: vhost vring error in virtqueue 1: Invalid argument (22))

* Sun Mar 12 2023 Miroslav Rezanina <mrezanin@redhat.com> - 7.2.0-12
- kvm-scsi-protect-req-aiocb-with-AioContext-lock.patch [bz#2155748]
- kvm-dma-helpers-prevent-dma_blk_cb-vs-dma_aio_cancel-rac.patch [bz#2155748]
- kvm-virtio-scsi-reset-SCSI-devices-from-main-loop-thread.patch [bz#2155748]
- kvm-qatomic-add-smp_mb__before-after_rmw.patch [bz#2175660]
- kvm-qemu-thread-posix-cleanup-fix-document-QemuEvent.patch [bz#2175660]
- kvm-qemu-thread-win32-cleanup-fix-document-QemuEvent.patch [bz#2175660]
- kvm-edu-add-smp_mb__after_rmw.patch [bz#2175660]
- kvm-aio-wait-switch-to-smp_mb__after_rmw.patch [bz#2175660]
- kvm-qemu-coroutine-lock-add-smp_mb__after_rmw.patch [bz#2175660]
- kvm-physmem-add-missing-memory-barrier.patch [bz#2175660]
- kvm-async-update-documentation-of-the-memory-barriers.patch [bz#2175660]
- kvm-async-clarify-usage-of-barriers-in-the-polling-case.patch [bz#2175660]
- Resolves: bz#2155748
  (qemu crash on void blk_drain(BlockBackend *): Assertion qemu_in_main_thread() failed)
- Resolves: bz#2175660
  (Guest hangs when starting or rebooting)

* Mon Mar 06 2023 Miroslav Rezanina <mrezanin@redhat.com> - 7.2.0-11
- kvm-hw-smbios-fix-field-corruption-in-type-4-table.patch [bz#2169904]
- Resolves: bz#2169904
  ([SVVP] job 'Check SMBIOS Table Specific Requirements' failed on win2022)

* Tue Feb 21 2023 Miroslav Rezanina <mrezanin@redhat.com> - 7.2.0-10
- kvm-block-temporarily-hold-the-new-AioContext-of-bs_top-.patch [bz#2168209]
- Resolves: bz#2168209
  (Qemu coredump after do snapshot of mirrored top image and its converted base image(iothread enabled))

* Fri Feb 17 2023 Miroslav Rezanina <mrezanin@redhat.com> - 7.2.0-9
- kvm-tests-qtest-netdev-test-stream-and-dgram-backends.patch [bz#2169232]
- kvm-net-stream-add-a-new-option-to-automatically-reconne.patch [bz#2169232]
- kvm-linux-headers-Update-to-v6.1.patch [bz#2158704]
- kvm-util-userfaultfd-Add-uffd_open.patch [bz#2158704]
- kvm-util-userfaultfd-Support-dev-userfaultfd.patch [bz#2158704]
- kvm-io-Add-support-for-MSG_PEEK-for-socket-channel.patch [bz#2169732]
- kvm-migration-check-magic-value-for-deciding-the-mapping.patch [bz#2169732]
- kvm-target-s390x-arch_dump-Fix-memory-corruption-in-s390.patch [bz#2168172]
- Resolves: bz#2169232
  (RFE: reconnect option for stream socket back-end)
- Resolves: bz#2158704
  (RFE: Prefer /dev/userfaultfd over userfaultfd(2) syscall)
- Resolves: bz#2169732
  (Multifd migration fails under a weak network/socket ordering race)
- Resolves: bz#2168172
  ([s390x] qemu-kvm coredumps when SE crashes)

* Thu Feb 09 2023 Miroslav Rezanina <mrezanin@redhat.com> - 7.2.0-8
- kvm-qcow2-Fix-theoretical-corruption-in-store_bitmap-err.patch [bz#2150180]
- kvm-qemu-img-commit-Report-errors-while-closing-the-imag.patch [bz#2150180]
- kvm-qemu-img-bitmap-Report-errors-while-closing-the-imag.patch [bz#2150180]
- kvm-qemu-iotests-Test-qemu-img-bitmap-commit-exit-code-o.patch [bz#2150180]
- kvm-accel-tcg-Test-CPUJumpCache-in-tb_jmp_cache_clear_pa.patch [bz#2165280]
- kvm-block-Improve-empty-format-specific-info-dump.patch [bz#1860292]
- kvm-block-file-Add-file-specific-image-info.patch [bz#1860292]
- kvm-block-vmdk-Change-extent-info-type.patch [bz#1860292]
- kvm-block-Split-BlockNodeInfo-off-of-ImageInfo.patch [bz#1860292]
- kvm-qemu-img-Use-BlockNodeInfo.patch [bz#1860292]
- kvm-block-qapi-Let-bdrv_query_image_info-recurse.patch [bz#1860292]
- kvm-block-qapi-Introduce-BlockGraphInfo.patch [bz#1860292]
- kvm-block-qapi-Add-indentation-to-bdrv_node_info_dump.patch [bz#1860292]
- kvm-iotests-Filter-child-node-information.patch [bz#1860292]
- kvm-iotests-106-214-308-Read-only-one-size-line.patch [bz#1860292]
- kvm-qemu-img-Let-info-print-block-graph.patch [bz#1860292]
- kvm-qemu-img-Change-info-key-names-for-protocol-nodes.patch [bz#1860292]
- kvm-Revert-vhost-user-Monitor-slave-channel-in-vhost_use.patch [bz#2155173]
- kvm-Revert-vhost-user-Introduce-nested-event-loop-in-vho.patch [bz#2155173]
- kvm-virtio-rng-pci-fix-transitional-migration-compat-for.patch [bz#2162569]
- Resolves: bz#2150180
  (qemu-img finishes successfully while having errors in commit or bitmaps operations)
- Resolves: bz#2165280
  ([kvm-unit-tests] debug-wp-migration fails)
- Resolves: bz#1860292
  (RFE: add extent_size_hint information to qemu-img info)
- Resolves: bz#2155173
  ([vhost-user] unable to start vhost net: 71: falling back on userspace)
- Resolves: bz#2162569
  ([transitional device][virtio-rng-pci-transitional]Stable Guest ABI failed between RHEL 8.6 to RHEL 9.2)

* Mon Feb 06 2023 Miroslav Rezanina <mrezanin@redhat.com> - 7.2.0-7
- kvm-vdpa-use-v-shadow_vqs_enabled-in-vhost_vdpa_svqs_sta.patch [bz#2104412]
- kvm-vhost-set-SVQ-device-call-handler-at-SVQ-start.patch [bz#2104412]
- kvm-vhost-allocate-SVQ-device-file-descriptors-at-device.patch [bz#2104412]
- kvm-vhost-move-iova_tree-set-to-vhost_svq_start.patch [bz#2104412]
- kvm-vdpa-add-vhost_vdpa_net_valid_svq_features.patch [bz#2104412]
- kvm-vdpa-request-iova_range-only-once.patch [bz#2104412]
- kvm-vdpa-move-SVQ-vring-features-check-to-net.patch [bz#2104412]
- kvm-vdpa-allocate-SVQ-array-unconditionally.patch [bz#2104412]
- kvm-vdpa-add-asid-parameter-to-vhost_vdpa_dma_map-unmap.patch [bz#2104412]
- kvm-vdpa-store-x-svq-parameter-in-VhostVDPAState.patch [bz#2104412]
- kvm-vdpa-add-shadow_data-to-vhost_vdpa.patch [bz#2104412]
- kvm-vdpa-always-start-CVQ-in-SVQ-mode-if-possible.patch [bz#2104412]
- kvm-vdpa-fix-VHOST_BACKEND_F_IOTLB_ASID-flag-check.patch [bz#2104412]
- kvm-spec-Disable-VDUSE.patch [bz#2128222]
- Resolves: bz#2104412
  (vDPA ASID support in Qemu)
- Resolves: bz#2128222
  (VDUSE block export should be disabled in builds for now)

* Mon Jan 30 2023 Miroslav Rezanina <mrezanin@redhat.com> - 7.2.0-6
- kvm-virtio_net-Modify-virtio_net_get_config-to-early-ret.patch [bz#2141088]
- kvm-virtio_net-copy-VIRTIO_NET_S_ANNOUNCE-if-device-mode.patch [bz#2141088]
- kvm-vdpa-handle-VIRTIO_NET_CTRL_ANNOUNCE-in-vhost_vdpa_n.patch [bz#2141088]
- kvm-vdpa-do-not-handle-VIRTIO_NET_F_GUEST_ANNOUNCE-in-vh.patch [bz#2141088]
- kvm-s390x-pv-Implement-a-CGS-check-helper.patch [bz#2122523]
- kvm-s390x-pci-coalesce-unmap-operations.patch [bz#2163701]
- kvm-s390x-pci-shrink-DMA-aperture-to-be-bound-by-vfio-DM.patch [bz#2163701]
- kvm-s390x-pci-reset-ISM-passthrough-devices-on-shutdown-.patch [bz#2163701]
- kvm-qga-linux-add-usb-support-to-guest-get-fsinfo.patch [bz#2149191]
- Resolves: bz#2141088
  (vDPA SVQ guest announce support)
- Resolves: bz#2122523
  (Secure guest can't boot with maximal number of vcpus (248))
- Resolves: bz#2163701
  ([s390x] VM fails to start with ISM passed through)
- Resolves: bz#2149191
  ([RFE][guest-agent] - USB bus type support)

* Tue Jan 17 2023 Miroslav Rezanina <mrezanin@redhat.com> - 7.2.0-5
- kvm-virtio-introduce-macro-VIRTIO_CONFIG_IRQ_IDX.patch [bz#1905805]
- kvm-virtio-pci-decouple-notifier-from-interrupt-process.patch [bz#1905805]
- kvm-virtio-pci-decouple-the-single-vector-from-the-inter.patch [bz#1905805]
- kvm-vhost-introduce-new-VhostOps-vhost_set_config_call.patch [bz#1905805]
- kvm-vhost-vdpa-add-support-for-config-interrupt.patch [bz#1905805]
- kvm-virtio-add-support-for-configure-interrupt.patch [bz#1905805]
- kvm-vhost-add-support-for-configure-interrupt.patch [bz#1905805]
- kvm-virtio-net-add-support-for-configure-interrupt.patch [bz#1905805]
- kvm-virtio-mmio-add-support-for-configure-interrupt.patch [bz#1905805]
- kvm-virtio-pci-add-support-for-configure-interrupt.patch [bz#1905805]
- kvm-s390x-s390-virtio-ccw-Activate-zPCI-features-on-s390.patch [bz#2159408]
- kvm-vhost-fix-vq-dirty-bitmap-syncing-when-vIOMMU-is-ena.patch [bz#2124856]
- kvm-block-drop-bdrv_remove_filter_or_cow_child.patch [bz#2155112]
- kvm-qed-Don-t-yield-in-bdrv_qed_co_drain_begin.patch [bz#2155112]
- kvm-test-bdrv-drain-Don-t-yield-in-.bdrv_co_drained_begi.patch [bz#2155112]
- kvm-block-Revert-.bdrv_drained_begin-end-to-non-coroutin.patch [bz#2155112]
- kvm-block-Remove-drained_end_counter.patch [bz#2155112]
- kvm-block-Inline-bdrv_drain_invoke.patch [bz#2155112]
- kvm-block-Fix-locking-for-bdrv_reopen_queue_child.patch [bz#2155112]
- kvm-block-Drain-individual-nodes-during-reopen.patch [bz#2155112]
- kvm-block-Don-t-use-subtree-drains-in-bdrv_drop_intermed.patch [bz#2155112]
- kvm-stream-Replace-subtree-drain-with-a-single-node-drai.patch [bz#2155112]
- kvm-block-Remove-subtree-drains.patch [bz#2155112]
- kvm-block-Call-drain-callbacks-only-once.patch [bz#2155112]
- kvm-block-Remove-ignore_bds_parents-parameter-from-drain.patch [bz#2155112]
- kvm-block-Drop-out-of-coroutine-in-bdrv_do_drained_begin.patch [bz#2155112]
- kvm-block-Don-t-poll-in-bdrv_replace_child_noperm.patch [bz#2155112]
- kvm-block-Remove-poll-parameter-from-bdrv_parent_drained.patch [bz#2155112]
- kvm-accel-introduce-accelerator-blocker-API.patch [bz#1979276]
- kvm-KVM-keep-track-of-running-ioctls.patch [bz#1979276]
- kvm-kvm-Atomic-memslot-updates.patch [bz#1979276]
- Resolves: bz#1905805
  (support config interrupt in vhost-vdpa qemu)
- Resolves: bz#2159408
  ([s390x] VMs with ISM passthrough don't autostart after leapp upgrade from RHEL 8)
- Resolves: bz#2124856
  (VM with virtio interface and iommu=on will crash when try to migrate)
- Resolves: bz#2155112
  (Qemu coredump after do snapshot of mirrored top image and its converted base image(iothread enabled))
- Resolves: bz#1979276
  (SVM: non atomic memslot updates cause boot failure with seabios and cpu-pm=on)

* Thu Jan 12 2023 Miroslav Rezanina <mrezanin@redhat.com> - 7.2.0-4
- kvm-virtio-rng-pci-fix-migration-compat-for-vectors.patch [bz#2155749]
- kvm-Update-QGA-service-for-new-command-line.patch [bz#2156515]
- Resolves: bz#2155749
  ([regression][stable guest abi][qemu-kvm7.2]Migration failed due to virtio-rng device between RHEL8.8 and RHEL9.2/MSI-X)
- Resolves: bz#2156515
  ([guest-agent] Replace '-blacklist' to '-block-rpcs' in qemu-ga config file)

* Wed Jan 04 2023 Miroslav Rezanina <mrezanin@redhat.com> - 7.2.0-3
- kvm-hw-arm-virt-Introduce-virt_set_high_memmap-helper.patch [bz#2113840]
- kvm-hw-arm-virt-Rename-variable-size-to-region_size-in-v.patch [bz#2113840]
- kvm-hw-arm-virt-Introduce-variable-region_base-in-virt_s.patch [bz#2113840]
- kvm-hw-arm-virt-Introduce-virt_get_high_memmap_enabled-h.patch [bz#2113840]
- kvm-hw-arm-virt-Improve-high-memory-region-address-assig.patch [bz#2113840]
- kvm-hw-arm-virt-Add-compact-highmem-property.patch [bz#2113840]
- kvm-hw-arm-virt-Add-properties-to-disable-high-memory-re.patch [bz#2113840]
- kvm-hw-arm-virt-Enable-compat-high-memory-region-address.patch [bz#2113840]
- Resolves: bz#2113840
  ([RHEL9.2] Memory mapping optimization for virt machine)

* Tue Dec 20 2022 Miroslav Rezanina <mrezanin@redhat.com> - 7.2.0-2
- Fix updating from 7.1.0
- kvm-redhat-fix-virt-rhel9.2.0-compat-props.patch[bz#2154640]
- Resolves: bz#2154640
  ([aarch64] qemu fails to load "efi-virtio.rom" romfile when creating virtio-net-pci)

* Thu Dec 15 2022 Miroslav Rezanina <mrezanin@redhat.com> - 7.2.0-1
- Rebase to QEMU 7.2.0 [bz#2135806]
- Resolves: bz#2135806
  (Rebase to QEMU 7.2 for RHEL 9.2.0)

* Wed Dec 14 2022 Jon Maloy <jmaloy@redhat.com> - 7.1.0-7
- kvm-hw-acpi-erst.c-Fix-memory-handling-issues.patch [bz#2149108]
- Resolves: bz#2149108
  (CVE-2022-4172 qemu-kvm: QEMU: ACPI ERST: memory corruption issues in read_erst_record and write_erst_record [rhel-9])

* Fri Dec 02 2022 Miroslav Rezanina <mrezanin@redhat.com> - 7.1.0-6
- kvm-block-move-bdrv_qiov_is_aligned-to-file-posix.patch [bz#2143170]
- kvm-block-use-the-request-length-for-iov-alignment.patch [bz#2143170]
- Resolves: bz#2143170
  (The installation can not start when install files (iso) locate on a 4k disk)

* Mon Nov 14 2022 Miroslav Rezanina <mrezanin@redhat.com> - 7.1.0-5
- kvm-rtl8139-Remove-unused-variable.patch [bz#2141218]
- kvm-qemu-img-remove-unused-variable.patch [bz#2141218]
- kvm-host-libusb-Remove-unused-variable.patch [bz#2141218]
- Resolves: bz#2141218
  (qemu-kvm build fails with clang 15.0.1 due to false unused variable error)

* Tue Nov 01 2022 Miroslav Rezanina <mrezanin@redhat.com> - 7.1.0-4
- kvm-Revert-intel_iommu-Fix-irqchip-X2APIC-configuration-.patch [bz#2126095]
- Resolves: bz#2126095
  ([rhel9.2][intel_iommu]Booting guest with "-device intel-iommu,intremap=on,device-iotlb=on,caching-mode=on" causes kernel call trace)

* Thu Oct 13 2022 Jon Maloy <jmaloy@redhat.com> - 7.1.0-3
- kvm-target-i386-kvm-fix-kvmclock_current_nsec-Assertion-.patch [bz#2108531]
- Resolves: bz#2108531
  (Windows guest reboot after migration with wsl2 installed inside)

* Thu Sep 29 2022 Miroslav Rezanina <mrezanin@redhat.com> - 7.1.0-2
- kvm-vdpa-Skip-the-maps-not-in-the-iova-tree.patch [RHELX-57]
- kvm-vdpa-do-not-save-failed-dma-maps-in-SVQ-iova-tree.patch [RHELX-57]
- kvm-util-accept-iova_tree_remove_parameter-by-value.patch [RHELX-57]
- kvm-vdpa-Remove-SVQ-vring-from-iova_tree-at-shutdown.patch [RHELX-57]
- kvm-vdpa-Make-SVQ-vring-unmapping-return-void.patch [RHELX-57]
- kvm-vhost-Always-store-new-kick-fd-on-vhost_svq_set_svq_.patch [RHELX-57]
- kvm-vdpa-Use-ring-hwaddr-at-vhost_vdpa_svq_unmap_ring.patch [RHELX-57]
- kvm-vhost-stop-transfer-elem-ownership-in-vhost_handle_g.patch [RHELX-57]
- kvm-vhost-use-SVQ-element-ndescs-instead-of-opaque-data-.patch [RHELX-57]
- kvm-vhost-Delete-useless-read-memory-barrier.patch [RHELX-57]
- kvm-vhost-Do-not-depend-on-NULL-VirtQueueElement-on-vhos.patch [RHELX-57]
- kvm-vhost_net-Add-NetClientInfo-start-callback.patch [RHELX-57]
- kvm-vhost_net-Add-NetClientInfo-stop-callback.patch [RHELX-57]
- kvm-vdpa-add-net_vhost_vdpa_cvq_info-NetClientInfo.patch [RHELX-57]
- kvm-vdpa-Move-command-buffers-map-to-start-of-net-device.patch [RHELX-57]
- kvm-vdpa-extract-vhost_vdpa_net_cvq_add-from-vhost_vdpa_.patch [RHELX-57]
- kvm-vhost_net-add-NetClientState-load-callback.patch [RHELX-57]
- kvm-vdpa-Add-virtio-net-mac-address-via-CVQ-at-start.patch [RHELX-57]
- kvm-vdpa-Delete-CVQ-migration-blocker.patch [RHELX-57]
- kvm-vdpa-Make-VhostVDPAState-cvq_cmd_in_buffer-control-a.patch [RHELX-57]
- kvm-vdpa-extract-vhost_vdpa_net_load_mac-from-vhost_vdpa.patch [RHELX-57]
- kvm-vdpa-Add-vhost_vdpa_net_load_mq.patch [RHELX-57]
- kvm-vdpa-validate-MQ-CVQ-commands.patch [RHELX-57]
- kvm-virtio-net-Update-virtio-net-curr_queue_pairs-in-vdp.patch [RHELX-57]
- kvm-vdpa-Allow-MQ-feature-in-SVQ.patch [RHELX-57]
- kvm-i386-reset-KVM-nested-state-upon-CPU-reset.patch [bz#2125281]
- kvm-i386-do-kvm_put_msr_feature_control-first-thing-when.patch [bz#2125281]
- kvm-Revert-Re-enable-capstone-internal-build.patch [bz#2127825]
- kvm-spec-Use-capstone-package.patch [bz#2127825]
- Resolves: RHELX-57
  (vDPA SVQ Multiqueue support )
- Resolves: bz#2125281
  ([RHEL9.1] Guests in VMX root operation fail to reboot with QEMU's 'system_reset' command [rhel-9.2.0])
- Resolves: bz#2127825
  (Use capstone for qemu-kvm build)

* Mon Sep 05 2022 Miroslav Rezanina <mrezanin@redhat.com> - 7.1.0-1
- Rebase to QEMU 7.1.0 [bz#2111769]
- Resolves: bz#2111769
  (Rebase to QEMU 7.1.0)

* Mon Aug 15 2022 Miroslav Rezanina <mrezanin@redhat.com> - 7.0.0-11
- kvm-QIOChannelSocket-Fix-zero-copy-flush-returning-code-.patch [bz#2107466]
- kvm-Add-dirty-sync-missed-zero-copy-migration-stat.patch [bz#2107466]
- kvm-migration-multifd-Report-to-user-when-zerocopy-not-w.patch [bz#2107466]
- kvm-migration-Avoid-false-positive-on-non-supported-scen.patch [bz#2107466]
- kvm-migration-add-remaining-params-has_-true-in-migratio.patch [bz#2107466]
- kvm-QIOChannelSocket-Add-support-for-MSG_ZEROCOPY-IPV6.patch [bz#2107466]
- kvm-pc-bios-s390-ccw-Fix-booting-with-logical-block-size.patch [bz#2112303]
- kvm-vdpa-Fix-bad-index-calculus-at-vhost_vdpa_get_vring_.patch [bz#2116876]
- kvm-vdpa-Fix-index-calculus-at-vhost_vdpa_svqs_start.patch [bz#2116876]
- kvm-vdpa-Fix-memory-listener-deletions-of-iova-tree.patch [bz#2116876]
- kvm-vdpa-Fix-file-descriptor-leak-on-get-features-error.patch [bz#2116876]
- Resolves: bz#2107466
  (zerocopy capability can be enabled when set migrate capabilities with multifd and compress/xbzrle together)
- Resolves: bz#2112303
  (virtio-blk: Can't boot fresh installation from used 512 cluster_size image under certain conditions)
- Resolves: bz#2116876
  (Fixes for vDPA control virtqueue support in Qemu)

* Mon Aug 08 2022 Miroslav Rezanina <mrezanin@redhat.com> - 7.0.0-10
- kvm-vhost-Track-descriptor-chain-in-private-at-SVQ.patch [bz#1939363]
- kvm-vhost-Fix-device-s-used-descriptor-dequeue.patch [bz#1939363]
- kvm-hw-virtio-Replace-g_memdup-by-g_memdup2.patch [bz#1939363]
- kvm-vhost-Fix-element-in-vhost_svq_add-failure.patch [bz#1939363]
- kvm-meson-create-have_vhost_-variables.patch [bz#1939363]
- kvm-meson-use-have_vhost_-variables-to-pick-sources.patch [bz#1939363]
- kvm-vhost-move-descriptor-translation-to-vhost_svq_vring.patch [bz#1939363]
- kvm-virtio-net-Expose-MAC_TABLE_ENTRIES.patch [bz#1939363]
- kvm-virtio-net-Expose-ctrl-virtqueue-logic.patch [bz#1939363]
- kvm-vdpa-Avoid-compiler-to-squash-reads-to-used-idx.patch [bz#1939363]
- kvm-vhost-Reorder-vhost_svq_kick.patch [bz#1939363]
- kvm-vhost-Move-vhost_svq_kick-call-to-vhost_svq_add.patch [bz#1939363]
- kvm-vhost-Check-for-queue-full-at-vhost_svq_add.patch [bz#1939363]
- kvm-vhost-Decouple-vhost_svq_add-from-VirtQueueElement.patch [bz#1939363]
- kvm-vhost-Add-SVQDescState.patch [bz#1939363]
- kvm-vhost-Track-number-of-descs-in-SVQDescState.patch [bz#1939363]
- kvm-vhost-add-vhost_svq_push_elem.patch [bz#1939363]
- kvm-vhost-Expose-vhost_svq_add.patch [bz#1939363]
- kvm-vhost-add-vhost_svq_poll.patch [bz#1939363]
- kvm-vhost-Add-svq-avail_handler-callback.patch [bz#1939363]
- kvm-vdpa-Export-vhost_vdpa_dma_map-and-unmap-calls.patch [bz#1939363]
- kvm-vhost-net-vdpa-add-stubs-for-when-no-virtio-net-devi.patch [bz#1939363]
- kvm-vdpa-manual-forward-CVQ-buffers.patch [bz#1939363]
- kvm-vdpa-Buffer-CVQ-support-on-shadow-virtqueue.patch [bz#1939363]
- kvm-vdpa-Extract-get-features-part-from-vhost_vdpa_get_m.patch [bz#1939363]
- kvm-vdpa-Add-device-migration-blocker.patch [bz#1939363]
- kvm-vdpa-Add-x-svq-to-NetdevVhostVDPAOptions.patch [bz#1939363]
- kvm-redhat-Update-linux-headers-linux-kvm.h-to-v5.18-rc6.patch [bz#2111994]
- kvm-target-s390x-kvm-Honor-storage-keys-during-emulation.patch [bz#2111994]
- kvm-kvm-don-t-use-perror-without-useful-errno.patch [bz#2095608]
- kvm-multifd-Copy-pages-before-compressing-them-with-zlib.patch [bz#2099934]
- kvm-Revert-migration-Simplify-unqueue_page.patch [bz#2099934]
- Resolves: bz#1939363
  (vDPA control virtqueue support in Qemu)
- Resolves: bz#2111994
  (RHEL9: skey test in kvm_unit_test got failed)
- Resolves: bz#2095608
  (Please correct the error message when try to start qemu with "-M kernel-irqchip=split")
- Resolves: bz#2099934
  (Guest reboot on destination host after postcopy migration completed)

* Mon Jul 18 2022 Miroslav Rezanina <mrezanin@redhat.com> - 7.0.0-9
- kvm-virtio-iommu-Add-bypass-mode-support-to-assigned-dev.patch [bz#2100106]
- kvm-virtio-iommu-Use-recursive-lock-to-avoid-deadlock.patch [bz#2100106]
- kvm-virtio-iommu-Add-an-assert-check-in-translate-routin.patch [bz#2100106]
- kvm-virtio-iommu-Fix-the-partial-copy-of-probe-request.patch [bz#2100106]
- kvm-virtio-iommu-Fix-migration-regression.patch [bz#2100106]
- kvm-pc-bios-s390-ccw-virtio-Introduce-a-macro-for-the-DA.patch [bz#2098077]
- kvm-pc-bios-s390-ccw-bootmap-Improve-the-guessing-logic-.patch [bz#2098077]
- kvm-pc-bios-s390-ccw-virtio-blkdev-Simplify-fix-virtio_i.patch [bz#2098077]
- kvm-pc-bios-s390-ccw-virtio-blkdev-Remove-virtio_assume_.patch [bz#2098077]
- kvm-pc-bios-s390-ccw-virtio-Set-missing-status-bits-whil.patch [bz#2098077]
- kvm-pc-bios-s390-ccw-virtio-Read-device-config-after-fea.patch [bz#2098077]
- kvm-pc-bios-s390-ccw-virtio-Beautify-the-code-for-readin.patch [bz#2098077]
- kvm-pc-bios-s390-ccw-Split-virtio-scsi-code-from-virtio_.patch [bz#2098077]
- kvm-pc-bios-s390-ccw-virtio-blkdev-Request-the-right-fea.patch [bz#2098077]
- kvm-pc-bios-s390-ccw-netboot.mak-Ignore-Clang-s-warnings.patch [bz#2098077]
- kvm-hw-block-fdc-Prevent-end-of-track-overrun-CVE-2021-3.patch [bz#1951522]
- kvm-tests-qtest-fdc-test-Add-a-regression-test-for-CVE-2.patch [bz#1951522]
- Resolves: bz#2100106
  (Fix virtio-iommu/vfio bypass)
- Resolves: bz#2098077
  (virtio-blk: Can't boot fresh installation from used virtio-blk dasd disk under certain conditions)
- Resolves: bz#1951522
  (CVE-2021-3507 qemu-kvm: QEMU: fdc: heap buffer overflow in DMA read data transfers [rhel-9.0])

* Tue Jul 05 2022 Camilla Conte <cconte@redhat.com> - 7.0.0-8
- kvm-tests-avocado-update-aarch64_virt-test-to-exercise-c.patch [bz#2060839]
- kvm-RHEL-only-tests-avocado-Switch-aarch64-tests-from-a5.patch [bz#2060839]
- kvm-RHEL-only-AArch64-Drop-unsupported-CPU-types.patch [bz#2060839]
- kvm-target-i386-deprecate-CPUs-older-than-x86_64-v2-ABI.patch [bz#2060839]
- kvm-target-s390x-deprecate-CPUs-older-than-z14.patch [bz#2060839]
- kvm-target-arm-deprecate-named-CPU-models.patch [bz#2060839]
- kvm-meson.build-Fix-docker-test-build-alpine-when-includ.patch [bz#1968509]
- kvm-QIOChannel-Add-flags-on-io_writev-and-introduce-io_f.patch [bz#1968509]
- kvm-QIOChannelSocket-Implement-io_writev-zero-copy-flag-.patch [bz#1968509]
- kvm-migration-Add-zero-copy-send-parameter-for-QMP-HMP-f.patch [bz#1968509]
- kvm-migration-Add-migrate_use_tls-helper.patch [bz#1968509]
- kvm-multifd-multifd_send_sync_main-now-returns-negative-.patch [bz#1968509]
- kvm-multifd-Send-header-packet-without-flags-if-zero-cop.patch [bz#1968509]
- kvm-multifd-Implement-zero-copy-write-in-multifd-migrati.patch [bz#1968509]
- kvm-QIOChannelSocket-Introduce-assert-and-reduce-ifdefs-.patch [bz#1968509]
- kvm-QIOChannelSocket-Fix-zero-copy-send-so-socket-flush-.patch [bz#1968509]
- kvm-migration-Change-zero_copy_send-from-migration-param.patch [bz#1968509]
- kvm-migration-Allow-migrate-recover-to-run-multiple-time.patch [bz#2096143]
- Resolves: bz#2060839
  (Consider deprecating CPU models like "kvm64" / "qemu64" on RHEL 9)
- Resolves: bz#1968509
  (Use MSG_ZEROCOPY on QEMU Live Migration)
- Resolves: bz#2096143
  (The migration port is not released if use it again for recovering postcopy migration)

* Mon Jun 27 2022 Miroslav Rezanina <mrezanin@redhat.com> - 7.0.0-7
- kvm-coroutine-ucontext-use-QEMU_DEFINE_STATIC_CO_TLS.patch [bz#1952483]
- kvm-coroutine-use-QEMU_DEFINE_STATIC_CO_TLS.patch [bz#1952483]
- kvm-coroutine-win32-use-QEMU_DEFINE_STATIC_CO_TLS.patch [bz#1952483]
- kvm-Enable-virtio-iommu-pci-on-x86_64.patch [bz#2094252]
- kvm-linux-aio-fix-unbalanced-plugged-counter-in-laio_io_.patch [bz#2092788]
- kvm-linux-aio-explain-why-max-batch-is-checked-in-laio_i.patch [bz#2092788]
- Resolves: bz#1952483
  (RFE: QEMU's coroutines fail with CFLAGS=-flto on non-x86_64 architectures)
- Resolves: bz#2094252
  (Compile the virtio-iommu device on x86_64)
- Resolves: bz#2092788
  (Stalled IO Operations in VM)

* Mon Jun 13 2022 Miroslav Rezanina <mrezanin@redhat.com> - 7.0.0-6
- kvm-Introduce-event-loop-base-abstract-class.patch [bz#2031024]
- kvm-util-main-loop-Introduce-the-main-loop-into-QOM.patch [bz#2031024]
- kvm-util-event-loop-base-Introduce-options-to-set-the-th.patch [bz#2031024]
- kvm-qcow2-Improve-refcount-structure-rebuilding.patch [bz#2072379]
- kvm-iotests-108-Test-new-refcount-rebuild-algorithm.patch [bz#2072379]
- kvm-qcow2-Add-errp-to-rebuild_refcount_structure.patch [bz#2072379]
- kvm-iotests-108-Fix-when-missing-user_allow_other.patch [bz#2072379]
- kvm-virtio-net-setup-vhost_dev-and-notifiers-for-cvq-onl.patch [bz#2070804]
- kvm-virtio-net-align-ctrl_vq-index-for-non-mq-guest-for-.patch [bz#2070804]
- kvm-vhost-vdpa-fix-improper-cleanup-in-net_init_vhost_vd.patch [bz#2070804]
- kvm-vhost-net-fix-improper-cleanup-in-vhost_net_start.patch [bz#2070804]
- kvm-vhost-vdpa-backend-feature-should-set-only-once.patch [bz#2070804]
- kvm-vhost-vdpa-change-name-and-polarity-for-vhost_vdpa_o.patch [bz#2070804]
- kvm-virtio-net-don-t-handle-mq-request-in-userspace-hand.patch [bz#2070804]
- kvm-Revert-globally-limit-the-maximum-number-of-CPUs.patch [bz#2094270]
- kvm-vfio-common-remove-spurious-warning-on-vfio_listener.patch [bz#2086262]
- Resolves: bz#2031024
  (Add support for fixing thread pool size [QEMU])
- Resolves: bz#2072379
  (Fail to rebuild the reference count tables of qcow2 image on host block devices (e.g. LVs))
- Resolves: bz#2070804
  (PXE boot crash qemu when using multiqueue vDPA)
- Resolves: bz#2094270
  (Do not set the hard vCPU limit to the soft vCPU limit in downstream qemu-kvm anymore)
- Resolves: bz#2086262
  ([Win11][tpm]vfio_listener_region_del received unaligned region)

* Mon May 30 2022 Miroslav Rezanina <mrezanin@redhat.com> - 7.0.0-5
- kvm-qemu-nbd-Pass-max-connections-to-blockdev-layer.patch [bz#1708300]
- kvm-nbd-server-Allow-MULTI_CONN-for-shared-writable-expo.patch [bz#1708300]
- Resolves: bz#1708300
  (RFE: qemu-nbd vs NBD_FLAG_CAN_MULTI_CONN)

* Thu May 19 2022 Miroslav Rezanina <mrezanin@redhat.com> - 7.0.0-4
- kvm-qapi-machine.json-Add-cluster-id.patch [bz#2041823]
- kvm-qtest-numa-test-Specify-CPU-topology-in-aarch64_numa.patch [bz#2041823]
- kvm-hw-arm-virt-Consider-SMP-configuration-in-CPU-topolo.patch [bz#2041823]
- kvm-qtest-numa-test-Correct-CPU-and-NUMA-association-in-.patch [bz#2041823]
- kvm-hw-arm-virt-Fix-CPU-s-default-NUMA-node-ID.patch [bz#2041823]
- kvm-hw-acpi-aml-build-Use-existing-CPU-topology-to-build.patch [bz#2041823]
- kvm-coroutine-Rename-qemu_coroutine_inc-dec_pool_size.patch [bz#2079938]
- kvm-coroutine-Revert-to-constant-batch-size.patch [bz#2079938]
- kvm-virtio-scsi-fix-ctrl-and-event-handler-functions-in-.patch [bz#2079347]
- kvm-virtio-scsi-don-t-waste-CPU-polling-the-event-virtqu.patch [bz#2079347]
- kvm-virtio-scsi-clean-up-virtio_scsi_handle_event_vq.patch [bz#2079347]
- kvm-virtio-scsi-clean-up-virtio_scsi_handle_ctrl_vq.patch [bz#2079347]
- kvm-virtio-scsi-clean-up-virtio_scsi_handle_cmd_vq.patch [bz#2079347]
- kvm-virtio-scsi-move-request-related-items-from-.h-to-.c.patch [bz#2079347]
- kvm-Revert-virtio-scsi-Reject-scsi-cd-if-data-plane-enab.patch [bz#1995710]
- kvm-migration-Fix-operator-type.patch [bz#2064530]
- Resolves: bz#2041823
  ([aarch64][numa] When there are at least 6 Numa nodes serial log shows 'arch topology borken')
- Resolves: bz#2079938
  (qemu coredump when boot with multi disks (qemu) failed to set up stack guard page: Cannot allocate memory)
- Resolves: bz#2079347
  (Guest boot blocked when scsi disks using same iothread and 100% CPU consumption)
- Resolves: bz#1995710
  (RFE: Allow virtio-scsi CD-ROM media change with IOThreads)
- Resolves: bz#2064530
  (Rebuild qemu-kvm with clang-14)

* Thu May 12 2022 Miroslav Rezanina <mrezanin@redhat.com> - 7.0.0-3
- kvm-hw-arm-virt-Remove-the-dtb-kaslr-seed-machine-option.patch [bz#2046029]
- kvm-hw-arm-virt-Fix-missing-initialization-in-instance-c.patch [bz#2046029]
- kvm-Enable-virtio-iommu-pci-on-aarch64.patch [bz#1477099]
- kvm-sysemu-tpm-Add-a-stub-function-for-TPM_IS_CRB.patch [bz#2037612]
- kvm-vfio-common-remove-spurious-tpm-crb-cmd-misalignment.patch [bz#2037612]
- Resolves: bz#2046029
  ([WRB] New machine type property - dtb-kaslr-seed)
- Resolves: bz#1477099
  (virtio-iommu (including ACPI, VHOST/VFIO integration, migration support))
- Resolves: bz#2037612
  ([Win11][tpm][QL41112 PF]  vfio_listener_region_add received unaligned region)

* Fri May 06 2022 Miroslav Rezanina <mrezanin@redhat.com> - 7.0.0-2
- kvm-configs-devices-aarch64-softmmu-Enable-CONFIG_VIRTIO.patch [bz#2044162]
- kvm-target-ppc-cpu-models-Fix-ppc_cpu_aliases-list-for-R.patch [bz#2081022]
- Resolves: bz#2044162
  ([RHEL9.1] Enable virtio-mem as tech-preview on ARM64 QEMU)
- Resolves: bz#2081022
  (Build regression on ppc64le with c9s qemu-kvm 7.0.0-1 changes)

* Wed Apr 20 2022 Miroslav Rezanina <mrezanin@redhat.com> - 7.0.0-1
- Rebase to QEMU 7.0.0 [bz#2064757]
- Do not build ssh block driver anymore [bz#2064500]
- Removed hpet and parallel port support [bz#2065042]
- Compatibility support [bz#2064782 bz#2064771]
- Resolves: bz#2064757
  (Rebase to QEMU 7.0.0)
- Resolves: bz#2064500
  (Install qemu-kvm-6.2.0-11.el9_0.1 failed as conflict with qemu-kvm-block-ssh-6.2.0-11.el9_0.1)
- Resolves: bz#2065042
  (Remove upstream-only devices from the qemu-kvm binary)
- Resolves: bz#2064782
  (Update machine type compatibility for QEMU 7.0.0 update [s390x])
- Resolves: bz#2064771
  (Update machine type compatibility for QEMU 7.0.0 update [x86_64])

* Thu Apr 14 2022 Miroslav Rezanina <mrezanin@redhat.com> - 6.2.0-13
- kvm-RHEL-disable-seqpacket-for-vhost-vsock-device-in-rhe.patch [bz#2065589]
- Resolves: bz#2065589
  (RHEL 9.0 guest with vsock device migration failed from RHEL 9.0 > RHEL 8.6 [rhel-9.1.0])

* Mon Mar 21 2022 Miroslav Rezanina <mrezanin@redhat.com> - 6.2.0-12
- kvm-RHEL-mark-old-machine-types-as-deprecated.patch [bz#2062813]
- kvm-hw-virtio-vdpa-Fix-leak-of-host-notifier-memory-regi.patch [bz#2062828]
- kvm-spec-Fix-obsolete-for-spice-subpackages.patch [bz#2062819 bz#2062817]
- kvm-spec-Obsolete-old-usb-redir-subpackage.patch [bz#2062819]
- kvm-spec-Obsolete-ssh-driver.patch [bz#2062817]
- Resolves: bz#2062828
  ([virtual network][rhel9][vDPA] qemu crash after hot unplug vdpa device [rhel-9.1.0])
- Resolves: bz#2062819
  (Broken upgrade path due to qemu-kvm-hw-usbredir  rename [rhel-9.1.0])
- Resolves: bz#2062817
  (Missing qemu-kvm-block-ssh obsolete breaks upgrade path [rhel-9.1.0])
- Resolves: bz#2062813
  (Mark all RHEL-8 and earlier machine types as deprecated [rhel-9.1.0])

* Tue Mar 01 2022 Miroslav Rezanina <mrezanin@redhat.com> - 6.2.0-11
- kvm-spec-Remove-qemu-virtiofsd.patch [bz#2055284]
- Resolves: bz#2055284
  (Remove the qemu-virtiofsd subpackage)

* Thu Feb 24 2022 Miroslav Rezanina <mrezanin@redhat.com> - 6.2.0-10
- kvm-Revert-ui-clipboard-Don-t-use-g_autoptr-just-to-free.patch [bz#2042820]
- kvm-ui-avoid-compiler-warnings-from-unused-clipboard-inf.patch [bz#2042820]
- kvm-ui-clipboard-fix-use-after-free-regression.patch [bz#2042820]
- kvm-ui-vnc.c-Fixed-a-deadlock-bug.patch [bz#2042820]
- kvm-memory-Fix-incorrect-calls-of-log_global_start-stop.patch [bz#2044818]
- kvm-memory-Fix-qemu-crash-on-starting-dirty-log-twice-wi.patch [bz#2044818]
- Resolves: bz#2042820
  (qemu crash when try to copy and paste contents from client to VM)
- Resolves: bz#2044818
  (Qemu Core Dumped when migrate -> migrate_cancel -> migrate again during guest is paused)

* Thu Feb 17 2022 Miroslav Rezanina <mrezanin@redhat.com> - 6.2.0-9
- kvm-block-Lock-AioContext-for-drain_end-in-blockdev-reop.patch [bz#2046659]
- kvm-iotests-Test-blockdev-reopen-with-iothreads-and-thro.patch [bz#2046659]
- kvm-block-nbd-Delete-reconnect-delay-timer-when-done.patch [bz#2033626]
- kvm-block-nbd-Assert-there-are-no-timers-when-closed.patch [bz#2033626]
- kvm-iotests.py-Add-QemuStorageDaemon-class.patch [bz#2033626]
- kvm-iotests-281-Test-lingering-timers.patch [bz#2033626]
- kvm-block-nbd-Move-s-ioc-on-AioContext-change.patch [bz#2033626]
- kvm-iotests-281-Let-NBD-connection-yield-in-iothread.patch [bz#2033626]
- Resolves: bz#2046659
  (qemu crash after execute blockdev-reopen with  iothread)
- Resolves: bz#2033626
  (Qemu core dump when start guest with nbd node or do block jobs to nbd node)

* Mon Feb 14 2022 Miroslav Rezanina <mrezanin@redhat.com> - 6.2.0-8
- kvm-numa-Enable-numa-for-SGX-EPC-sections.patch [bz#2033708]
- kvm-numa-Support-SGX-numa-in-the-monitor-and-Libvirt-int.patch [bz#2033708]
- kvm-doc-Add-the-SGX-numa-description.patch [bz#2033708]
- kvm-Enable-SGX-RH-Only.patch [bz#2033708]
- kvm-qapi-Cleanup-SGX-related-comments-and-restore-sectio.patch [bz#2033708]
- kvm-block-io-Update-BSC-only-if-want_zero-is-true.patch [bz#2041461]
- kvm-iotests-block-status-cache-New-test.patch [bz#2041461]
- kvm-iotests-Test-qemu-img-convert-of-zeroed-data-cluster.patch [bz#1882917]
- kvm-qemu-img-make-is_allocated_sectors-more-efficient.patch [bz#1882917]
- kvm-block-backend-prevent-dangling-BDS-pointers-across-a.patch [bz#2040123]
- kvm-iotests-stream-error-on-reset-New-test.patch [bz#2040123]
- kvm-hw-arm-smmuv3-Fix-device-reset.patch [bz#2042481]
- Resolves: bz#2033708
  ([Intel 9.0 Feat] qemu-kvm: SGX 1.5 (SGX1 + Flexible Launch Control) support)
- Resolves: bz#2041461
  (Inconsistent block status reply in qemu-nbd)
- Resolves: bz#1882917
  (the target image size is incorrect when converting a badly fragmented file)
- Resolves: bz#2040123
  (Qemu core dumped when do block-stream to a snapshot node on non-enough space storage)
- Resolves: bz#2042481
  ([aarch64] Launch guest with "default-bus-bypass-iommu=off,iommu=smmuv3" and "iommu_platform=on", guest hangs after system_reset)

* Mon Feb 07 2022 Miroslav Rezanina <mrezanin@redhat.com> - 6.2.0-7
- kvm-qemu-storage-daemon-Add-vhost-user-blk-help.patch [bz#1962088]
- kvm-qemu-storage-daemon-Fix-typo-in-vhost-user-blk-help.patch [bz#1962088]
- kvm-virtiofsd-Drop-membership-of-all-supplementary-group.patch [bz#2046201]
- kvm-block-rbd-fix-handling-of-holes-in-.bdrv_co_block_st.patch [bz#2034791]
- kvm-block-rbd-workaround-for-ceph-issue-53784.patch [bz#2034791]
- Resolves: bz#1962088
  ([QSD] wrong help message for the fuse)
- Resolves: bz#2046201
  (CVE-2022-0358 qemu-kvm: QEMU: virtiofsd: potential privilege escalation via CVE-2018-13405 [rhel-9.0])
- Resolves: bz#2034791
  (Booting from Local Snapshot Core Dumped Whose Backing File Is Based on RBD)

* Wed Feb 02 2022 Miroslav Rezanina <mrezanin@redhat.com> - 6.2.0-6
- Moving feature support out of qemu-kvm-core to separate packages (can
  cause loss of functionality when using only qemu-kvm-core - qemu-kvm keeps
  same feature set).
- kvm-spec-Rename-qemu-kvm-hw-usbredir-to-qemu-kvm-device-.patch [bz#2022847]
- kvm-spec-Split-qemu-kvm-ui-opengl.patch [bz#2022847]
- kvm-spec-Introduce-packages-for-virtio-gpu-modules.patch [bz#2022847]
- kvm-spec-Introduce-device-display-virtio-vga-packages.patch [bz#2022847]
- kvm-spec-Move-usb-host-module-to-separate-package.patch [bz#2022847]
- kvm-spec-Move-qtest-accel-module-to-tests-package.patch [bz#2022847]
- kvm-spec-Extend-qemu-kvm-core-description.patch [bz#2022847]
- Resolves: bz#2022847
  (qemu-kvm: Align package split with Fedora)

* Tue Jan 25 2022 Miroslav Rezanina <mrezanin@redhat.com> - 6.2.0-5
- kvm-x86-Add-q35-RHEL-8.6.0-machine-type.patch [bz#1945666]
- kvm-x86-Add-q35-RHEL-9.0.0-machine-type.patch [bz#1945666]
- kvm-softmmu-fix-device-deletion-events-with-device-JSON-.patch [bz#2036669]
- Resolves: bz#1945666
  (9.0: x86 machine types)
- Resolves: bz#2036669
  (DEVICE_DELETED event is not delivered for device frontend if -device is configured via JSON)

* Mon Jan 17 2022 Miroslav Rezanina <mrezanin@redhat.com> - 6.2.0-4
- kvm-block-nvme-fix-infinite-loop-in-nvme_free_req_queue_.patch [bz#2024544]
- kvm-rhel-machine-types-x86-set-prefer_sockets.patch [bz#2028623]
- Resolves: bz#2024544
  (Fio workers hangs when running fio with 32 jobs iodepth 32 and QEMU's userspace NVMe driver)
- Resolves: bz#2028623
  ([9.0] machine types: 6.2: Fix prefer_sockets)

* Mon Jan 10 2022 Miroslav Rezanina <mrezanin@redhat.com> - 6.2.0-3
- kvm-hw-arm-virt-Register-iommu-as-a-class-property.patch [bz#2031044]
- kvm-hw-arm-virt-Register-its-as-a-class-property.patch [bz#2031044]
- kvm-hw-arm-virt-Rename-default_bus_bypass_iommu.patch [bz#2031044]
- kvm-hw-arm-virt-Expose-the-RAS-option.patch [bz#2031044]
- kvm-hw-arm-virt-Add-9.0-machine-type-and-remove-8.5-one.patch [bz#2031044]
- kvm-hw-arm-virt-Check-no_tcg_its-and-minor-style-changes.patch [bz#2031044]
- Resolves: bz#2031044
  (Add rhel-9.0.0 machine types for RHEL 9.0 [aarch64])

* Fri Jan 07 2022 Miroslav Rezanina <mrezanin@redhat.com> - 6.2.0-2
- kvm-redhat-Add-rhel8.6.0-and-rhel9.0.0-machine-types-for.patch [bz#2008060]
- kvm-redhat-Enable-virtio-mem-as-tech-preview-on-x86-64.patch [bz#2014484]
- Resolves: bz#2008060
  (Fix CPU Model for new IBM Z Hardware - qemu part)
- Resolves: bz#2014484
  ([RHEL9] Enable virtio-mem as tech-preview on x86-64 - QEMU)

* Thu Dec 16 2021 Miroslav Rezanina <mrezanin@redhat.com> - 6.2.0-1
- Rebase to QEMU 6.2.0 [bz#2027697]
- Resolves: bz#2027697
  (Rebase to QEMU 6.2.0)

* Wed Nov 24 2021 Miroslav Rezanina <mrezanin@redhat.com> - 6.1.0-8
- kvm-Move-ksmtuned-files-to-separate-package.patch [bz#1971678]
- Resolves: bz#1971678
  (Split out ksmtuned package from qemu-kvm)

* Fri Nov 19 2021 Miroslav Rezanina <mrezanin@redhat.com> - 6.1.0-7
- kvm-migration-Make-migration-blocker-work-for-snapshots-.patch [bz#1996609]
- kvm-migration-Add-migrate_add_blocker_internal.patch [bz#1996609]
- kvm-dump-guest-memory-Block-live-migration.patch [bz#1996609]
- kvm-spec-Build-the-VDI-block-driver.patch [bz#2013331]
- kvm-spec-Explicitly-include-compress-filter.patch [bz#1980035]
- Resolves: bz#1996609
  (Qemu hit core dump when dump guest memory during live migration)
- Resolves: bz#2013331
  (RFE: qemu-img cannot convert from vdi format)
- Resolves: bz#1980035
  (RFE: Enable compress filter so we can create new, compressed qcow2 files via qemu-nbd)

* Mon Oct 18 2021 Miroslav Rezanina <mrezanin@redhat.com> - 6.1.0-6
- kvm-hw-arm-virt-Add-hw_compat_rhel_8_5-to-8.5-machine-ty.patch [bz#1998942]
- Resolves: bz#1998942
  (Add machine type compatibility update for 6.1 rebase [aarch64])

* Mon Oct 11 2021 Miroslav Rezanina <mrezanin@redhat.com> - 6.1.0-5
- kvm-virtio-balloon-Fix-page-poison-subsection-name.patch [bz#1984401]
- kvm-spec-Remove-block-curl-and-block-ssh-dependency.patch [bz#2010985]
- Resolves: bz#1984401
  (fails to revert snapshot of a VM [balloon/page-poison])
- Resolves: bz#2010985
  (Remove dependency on qemu-kvm-block-curl and qemu-kvm-block-ssh [rhel-9.0.0])

* Tue Oct 05 2021 Miroslav Rezanina <mrezanin@redhat.com> - 6.1.0-4
- kvm-redhat-Define-hw_compat_rhel_8_5.patch [bz#1998943]
- kvm-redhat-Add-s390x-machine-type-compatibility-update-f.patch [bz#1998943]
- Resolves: bz#1998943
  (Add machine type compatibility update for 6.1 rebase [s390x])

* Fri Sep 24 2021 Miroslav Rezanina <mrezanin@redhat.com> - 6.1.0-3
- kvm-disable-sga-device.patch [bz#2000845]
- kvm-tools-virtiofsd-Add-fstatfs64-syscall-to-the-seccomp.patch [bz#2005026]
- Resolves: bz#2000845
  (RFE: Remove SGA, deprecate cirrus, and set defaults for QEMU machine-types in RHEL9)
- Resolves: bz#2005026
  ([s390][virtio-fs] Umount virtiofs shared folder failure from guest side [rhel-9.0.0])

* Fri Sep 10 2021 Miroslav Rezanina <mrezanin@redhat.com> - 6.1.0-2
- kvm-hw-arm-virt-Remove-9.0-machine-type.patch [bz#2002937]
- kvm-remove-sgabios-dependency.patch [bz#2000845]
- kvm-enable-pulseaudio.patch [bz#1997725]
- kvm-spec-disable-use-of-gcrypt-for-crypto-backends-in-fa.patch [bz#1990068]
- Resolves: bz#2002937
  ([qemu][aarch64] Remove 9.0 machine types in arm virt for 9-Beta)
- Resolves: bz#2000845
  (RFE: Remove SGA, deprecate cirrus, and set defaults for QEMU machine-types in RHEL9)
- Resolves: bz#1997725
  (RFE: enable pulseaudio backend on QEMU)
- Resolves: bz#1990068
  (Disable use of gcrypt for crypto backends in favour of gnutls)

* Thu Sep 02 2021 Miroslav Rezanina <mrezanin@redhat.com> - 6.1.0-1
- Rebase to QEMU 6.1.0 [bz#1997408]
- Resolves: #bz#1997408
  (Rebase to QEMU 6.1.0)

* Fri Aug 27 2021 Miroslav Rezanina <mrezanin@redhat.com> - 6.0.0-13
- kvm-qcow2-Deprecation-warning-when-opening-v2-images-rw.patch [bz#1951814]
- kvm-disable-ac97-audio.patch [bz#1995819]
- kvm-redhat-Disable-LTO-on-non-x86-architectures.patch [bz#1950192]
- kvm-redhat-Enable-the-test-block-iothread-test-again.patch [bz#1950192]
- Resolves: bz#1951814
  (RFE: Warning when using qcow2-v2 (compat=0.10))
- Resolves: bz#1995819
  (RFE: Remove ac97 audio support from QEMU)
- Resolves: bz#1950192
  (RHEL9: when ioeventfd=off and 8.4guest, (qemu) qemu-kvm: ../util/qemu-coroutine-lock.c:57: qemu_co_queue_wait_impl: Assertion `qemu_in_coroutine()' failed.)

* Fri Aug 20 2021 Miroslav Rezanina <mrezanin@redhat.com> - 6.0.0-12.el9
- kvm-migration-Move-yank-outside-qemu_start_incoming_migr.patch [bz#1974683]
- kvm-migration-Allow-reset-of-postcopy_recover_triggered-.patch [bz#1974683]
- kvm-Remove-RHEL-7.0.0-machine-type.patch [bz#1968519]
- kvm-Remove-RHEL-7.1.0-machine-type.patch [bz#1968519]
- kvm-Remove-RHEL-7.2.0-machine-type.patch [bz#1968519]
- kvm-Remove-RHEL-7.3.0-machine-types.patch [bz#1968519]
- kvm-Remove-RHEL-7.4.0-machine-types.patch [bz#1968519]
- kvm-Remove-RHEL-7.5.0-machine-types.patch [bz#1968519]
- kvm-acpi-pc-revert-back-to-v5.2-PCI-slot-enumeration.patch [bz#1957194]
- kvm-migration-failover-reset-partially_hotplugged.patch [bz#1957194]
- kvm-hmp-Fix-loadvm-to-resume-the-VM-on-success-instead-o.patch [bz#1957194]
- kvm-migration-Move-bitmap_mutex-out-of-migration_bitmap_.patch [bz#1957194]
- kvm-i386-cpu-Expose-AVX_VNNI-instruction-to-guest.patch [bz#1957194]
- kvm-ratelimit-protect-with-a-mutex.patch [bz#1957194]
- kvm-Update-Linux-headers-to-5.13-rc4.patch [bz#1957194]
- kvm-i386-Add-ratelimit-for-bus-locks-acquired-in-guest.patch [bz#1957194]
- kvm-iothread-generalize-iothread_set_param-iothread_get_.patch [bz#1957194]
- kvm-iothread-add-aio-max-batch-parameter.patch [bz#1957194]
- kvm-linux-aio-limit-the-batch-size-using-aio-max-batch-p.patch [bz#1957194]
- kvm-block-nvme-Fix-VFIO_MAP_DMA-failed-No-space-left-on-.patch [bz#1957194]
- kvm-migration-move-wait-unplug-loop-to-its-own-function.patch [bz#1957194]
- kvm-migration-failover-continue-to-wait-card-unplug-on-e.patch [bz#1957194]
- kvm-aarch64-Add-USB-storage-devices.patch [bz#1957194]
- kvm-iotests-Improve-and-rename-test-291-to-qemu-img-bitm.patch [bz#1957194]
- kvm-qemu-img-Fail-fast-on-convert-bitmaps-with-inconsist.patch [bz#1957194]
- kvm-qemu-img-Add-skip-broken-bitmaps-for-convert-bitmaps.patch [bz#1957194]
- kvm-audio-Never-send-migration-section.patch [bz#1957194]
- kvm-pc-bios-s390-ccw-bootmap-Silence-compiler-warning-fr.patch [bz#1939509 bz#1940132]
- kvm-pc-bios-s390-ccw-Use-reset_psw-pointer-instead-of-ha.patch [bz#1939509 bz#1940132]
- kvm-pc-bios-s390-ccw-netboot-Use-Wl-prefix-to-pass-param.patch [bz#1939509 bz#1940132]
- kvm-pc-bios-s390-ccw-Silence-warning-from-Clang-by-marki.patch [bz#1939509 bz#1940132]
- kvm-pc-bios-s390-ccw-Fix-the-cc-option-macro-in-the-Make.patch [bz#1939509 bz#1940132]
- kvm-pc-bios-s390-ccw-Silence-GCC-11-stringop-overflow-wa.patch [bz#1939509 bz#1940132]
- kvm-pc-bios-s390-ccw-Allow-building-with-Clang-too.patch [bz#1939509 bz#1940132]
- kvm-pc-bios-s390-ccw-Fix-inline-assembly-for-older-versi.patch [bz#1939509 bz#1940132]
- kvm-configure-Fix-endianess-test-with-LTO.patch [bz#1939509 bz#1940132]
- kvm-spec-Switch-toolchain-to-Clang-LLVM.patch [bz#1939509 bz#1940132]
- kvm-spec-Use-safe-stack-for-x86_64.patch [bz#1939509 bz#1940132]
- kvm-spec-Reenable-write-support-for-VMDK-etc.-in-tools.patch [bz#1989841]
- Resolves: bz#1974683
  (Fail to set migrate incoming for 2nd time after the first time failed)
- Resolves: bz#1968519
  (Remove all the old 7.0-7.5 machine types)
- Resolves: bz#1957194
  (Synchronize RHEL-AV 8.5.0 changes to RHEL 9.0.0 Beta)
- Resolves: bz#1939509
  (QEMU: enable SafeStack)
- Resolves: bz#1940132
  (QEMU: switch build toolchain to Clang/LLVM)
- Resolves: bz#1989841
  (RFE: qemu-img cannot convert images into vmdk and vpc formats)

* Tue Aug 10 2021 Mohan Boddu <mboddu@redhat.com> - 17:6.0.0-11.1
- Rebuilt for IMA sigs, glibc 2.34, aarch64 flags
  Related: rhbz#1991688

* Sat Aug 07 2021 Miroslav Rezanina <mrezanin@redhat.com> - 6.0.0-11
- kvm-arm-virt-Register-iommu-as-a-class-property.patch [bz#1838608]
- kvm-arm-virt-Register-its-as-a-class-property.patch [bz#1838608]
- kvm-arm-virt-Enable-ARM-RAS-support.patch [bz#1838608]
- kvm-block-Fix-in_flight-leak-in-request-padding-error-pa.patch [bz#1972079]
- kvm-spec-Remove-buildldflags.patch [bz#1973029]
- kvm-spec-Use-make_build-macro.patch [bz#1973029]
- kvm-spec-Drop-make-install-sharedir-and-datadir-usage.patch [bz#1973029]
- kvm-spec-use-make_install-macro.patch [bz#1973029]
- kvm-spec-parallelize-make-check.patch [bz#1973029]
- kvm-spec-Drop-explicit-build-id.patch [bz#1973029]
- kvm-spec-use-build_ldflags.patch [bz#1973029]
- kvm-Move-virtiofsd-to-separate-package.patch [bz#1979728]
- kvm-Utilize-firmware-configure-option.patch [bz#1980139]
- Resolves: bz#1838608
  (aarch64: Enable ARMv8 RAS virtualization support)
- Resolves: bz#1972079
  (Windows Installation blocked on 4k disk when using blk+raw+iothread)
- Resolves: bz#1973029
  (Spec file cleanups)
- Resolves: bz#1979728
  (Split out virtiofsd subpackage)
- Resolves: bz#1980139
  (Use configure --firmwarepath more)

* Sun Jul 25 2021 Miroslav Rezanina <mrezanin@redhat.com> - 6.0.0-10
- kvm-s390x-css-Introduce-an-ESW-struct.patch [bz#1957194]
- kvm-s390x-css-Split-out-the-IRB-sense-data.patch [bz#1957194]
- kvm-s390x-css-Refactor-IRB-construction.patch [bz#1957194]
- kvm-s390x-css-Add-passthrough-IRB.patch [bz#1957194]
- kvm-vhost-user-blk-Fail-gracefully-on-too-large-queue-si.patch [bz#1957194]
- kvm-vhost-user-blk-Make-sure-to-set-Error-on-realize-fai.patch [bz#1957194]
- kvm-vhost-user-blk-Don-t-reconnect-during-initialisation.patch [bz#1957194]
- kvm-vhost-user-blk-Improve-error-reporting-in-realize.patch [bz#1957194]
- kvm-vhost-user-blk-Get-more-feature-flags-from-vhost-dev.patch [bz#1957194]
- kvm-virtio-Fail-if-iommu_platform-is-requested-but-unsup.patch [bz#1957194]
- kvm-vhost-user-blk-Check-that-num-queues-is-supported-by.patch [bz#1957194]
- kvm-vhost-user-Fix-backends-without-multiqueue-support.patch [bz#1957194]
- kvm-file-posix-fix-max_iov-for-dev-sg-devices.patch [bz#1957194]
- kvm-scsi-generic-pass-max_segments-via-max_iov-field-in-.patch [bz#1957194]
- kvm-osdep-provide-ROUND_DOWN-macro.patch [bz#1957194]
- kvm-block-backend-align-max_transfer-to-request-alignmen.patch [bz#1957194]
- kvm-block-add-max_hw_transfer-to-BlockLimits.patch [bz#1957194]
- kvm-file-posix-try-BLKSECTGET-on-block-devices-too-do-no.patch [bz#1957194]
- kvm-block-Add-option-to-use-driver-whitelist-even-in-too.patch [bz#1957782]
- kvm-spec-Restrict-block-drivers-in-tools.patch [bz#1957782]
- kvm-Move-tools-to-separate-package.patch [bz#1972285]
- kvm-Split-qemu-pr-helper-to-separate-package.patch [bz#1972300]
- kvm-spec-RPM_BUILD_ROOT-buildroot.patch [bz#1973029]
- kvm-spec-More-use-of-name-instead-of-qemu-kvm.patch [bz#1973029]
- kvm-spec-Use-qemu-pr-helper.service-from-qemu.git.patch [bz#1973029]
- kvm-spec-Use-_sourcedir-for-referencing-sources.patch [bz#1973029]
- kvm-spec-Add-tools_only.patch [bz#1973029]
- kvm-spec-build-Add-run_configure-helper.patch [bz#1973029]
- kvm-spec-build-Disable-more-bits-with-disable_everything.patch [bz#1973029]
- kvm-spec-build-Add-macros-for-some-configure-parameters.patch [bz#1973029]
- kvm-spec-files-Move-qemu-guest-agent-and-qemu-img-earlie.patch [bz#1973029]
- kvm-spec-install-Remove-redundant-bits.patch [bz#1973029]
- kvm-spec-install-Add-modprobe_kvm_conf-macro.patch [bz#1973029]
- kvm-spec-install-Remove-qemu-guest-agent-etc-qemu-kvm-us.patch [bz#1973029]
- kvm-spec-install-clean-up-qemu-ga-section.patch [bz#1973029]
- kvm-spec-install-Use-a-single-tools_only-section.patch [bz#1973029]
- kvm-spec-Make-tools_only-not-cross-spec-sections.patch [bz#1973029]
- kvm-spec-install-Limit-time-spent-in-qemu_kvm_build.patch [bz#1973029]
- kvm-spec-misc-syntactic-merges-with-Fedora.patch [bz#1973029]
- kvm-spec-Use-Fedora-s-pattern-for-specifying-rc-version.patch [bz#1973029]
- kvm-spec-files-don-t-use-fine-grained-docs-file-list.patch [bz#1973029]
- kvm-spec-files-Add-licenses-to-qemu-common-too.patch [bz#1973029]
- kvm-spec-install-Drop-python3-shebang-fixup.patch [bz#1973029]
- Resolves: bz#1957194
  (Synchronize RHEL-AV 8.5.0 changes to RHEL 9.0.0 Beta)
- Resolves: bz#1957782
  (VMDK support should be read-only)
- Resolves: bz#1972285
  (Split out a qemu-kvm-tools subpackage)
- Resolves: bz#1972300
  (Split out a qemu-pr-helper subpackage)
- Resolves: bz#1973029
  (Spec file cleanups)

* Mon Jul 19 2021 Miroslav Rezanina <mrezanin@redhat.com> - 6.0.0-9
- kvm-s390x-cpumodel-add-3931-and-3932.patch [bz#1932191]
- kvm-spapr-Fix-EEH-capability-issue-on-KVM-guest-for-PCI-.patch [bz#1957194]
- kvm-ppc-pef.c-initialize-cgs-ready-in-kvmppc_svm_init.patch [bz#1957194]
- kvm-redhat-Move-qemu-kvm-docs-dependency-to-qemu-kvm.patch [bz#1957194]
- kvm-redhat-introducting-qemu-kvm-hw-usbredir.patch [bz#1957194]
- kvm-redhat-use-the-standard-vhost-user-JSON-path.patch [bz#1957194]
- Resolves: bz#1932191
  ([IBM 9.0 FEAT] CPU Model for new IBM Z Hardware - qemu part (kvm))
- Resolves: bz#1957194
  (Synchronize RHEL-AV 8.5.0 changes to RHEL 9.0.0 Beta)

* Mon Jul 12 2021 Miroslav Rezanina <mrezanin@redhat.com> - 6.0.0-8
- kvm-Disable-TPM-passthrough.patch [bz#1978911]
- kvm-redhat-Replace-the-kvm-setup.service-with-a-etc-modu.patch [bz#1978837]
- Resolves: bz#1978911
  (Remove TPM Passthrough option from RHEL 9)
- Resolves: bz#1978837
  (Remove/replace kvm-setup.service)

* Mon Jun 28 2021 Miroslav Rezanina <mrezanin@redhat.com> - 6.0.0-7
- kvm-aarch64-rh-devices-add-CONFIG_PXB.patch [bz#1967502]
- kvm-virtio-gpu-handle-partial-maps-properly.patch [bz#1974795]
- kvm-x86-Add-x86-rhel8.5-machine-types.patch [bz#1957194]
- kvm-redhat-x86-Enable-kvm-asyncpf-int-by-default.patch [bz#1957194]
- kvm-block-backend-add-drained_poll.patch [bz#1957194]
- kvm-nbd-server-Use-drained-block-ops-to-quiesce-the-serv.patch [bz#1957194]
- kvm-disable-CONFIG_USB_STORAGE_BOT.patch [bz#1957194]
- kvm-doc-Fix-some-mistakes-in-the-SEV-documentation.patch [bz#1957194]
- kvm-docs-Add-SEV-ES-documentation-to-amd-memory-encrypti.patch [bz#1957194]
- kvm-docs-interop-firmware.json-Add-SEV-ES-support.patch [bz#1957194]
- kvm-qga-drop-StandardError-syslog.patch [bz#1947977]
- kvm-Remove-iscsi-support.patch [bz#1967133]
- Resolves: bz#1967502
  ([aarch64] [qemu] Compile the PCIe expander bridge)
- Resolves: bz#1974795
  ([RHEL9-beta] [aarch64] Launch guest with virtio-gpu-pci and virtual smmu causes "virtio_gpu_dequeue_ctrl_func" ERROR)
- Resolves: bz#1957194
  (Synchronize RHEL-AV 8.5.0 changes to RHEL 9.0.0 Beta)
- Resolves: bz#1947977
  (remove StandardError=syslog from qemu-guest-agent.service)
- Resolves: bz#1967133
  (QEMU: disable libiscsi in RHEL-9)

* Mon Jun 21 2021 Miroslav Rezanina <mrezanin@redhat.com> - 6.0.0-6
- kvm-yank-Unregister-function-when-using-TLS-migration.patch [bz#1972462]
- kvm-pc-bios-s390-ccw-don-t-try-to-read-the-next-block-if.patch [bz#1957194]
- kvm-redhat-Install-the-s390-netboot.img-that-we-ve-built.patch [bz#1957194]
- kvm-sockets-update-SOCKET_ADDRESS_TYPE_FD-listen-2-backl.patch [bz#1957194]
- kvm-target-i386-sev-add-support-to-query-the-attestation.patch [bz#1957194]
- kvm-spapr-Don-t-hijack-current_machine-boot_order.patch [bz#1957194]
- kvm-target-i386-Add-CPU-model-versions-supporting-xsaves.patch [bz#1957194]
- kvm-spapr-Remove-stale-comment-about-power-saving-LPCR-b.patch [bz#1957194]
- kvm-spapr-Set-LPCR-to-current-AIL-mode-when-starting-a-n.patch [bz#1957194]
- Specfile cleanup [bz#1973029]
- Resolves: bz#1972462
  (QEMU core dump when doing TLS migration via TCP)
- Resolves: bz#1957194
  (Synchronize RHEL-AV 8.5.0 changes to RHEL 9.0.0 Beta)
- Resolves: bz#1973029
  (Spec file cleanups)

* Tue Jun 08 2021 Miroslav Rezanina <mrezanin@redhat.com> - 6.0.0-5
- kvm-arm-virt-Register-highmem-and-gic-version-as-class-p.patch [bz#1952449]
- kvm-hw-arm-virt-Add-8.5-and-9.0-machine-types-and-remove.patch [bz#1952449]
- kvm-aarch64-rh-devices-add-CONFIG_PVPANIC_PCI.patch [bz#1747467]
- kvm-spec-Do-not-build-qemu-kvm-block-gluster.patch [bz#1964795]
- kvm-spec-Do-not-link-pcnet-and-ne2k_pci-roms.patch [bz#1965961]
- kvm-redhat-s390x-add-rhel-8.5.0-compat-machine.patch [bz#1957194]
- kvm-redhat-add-missing-entries-in-hw_compat_rhel_8_4.patch [bz#1957194]
- kvm-redhat-Define-pseries-rhel8.5.0-machine-type.patch [bz#1957194]
- kvm-virtio-net-failover-add-missing-remove_migration_sta.patch [bz#1957194]
- kvm-hw-arm-virt-Disable-PL011-clock-migration-through-hw.patch [bz#1957194]
- kvm-virtio-blk-Fix-rollback-path-in-virtio_blk_data_plan.patch [bz#1957194]
- kvm-virtio-blk-Configure-all-host-notifiers-in-a-single-.patch [bz#1957194]
- kvm-virtio-scsi-Set-host-notifiers-and-callbacks-separat.patch [bz#1957194]
- kvm-virtio-scsi-Configure-all-host-notifiers-in-a-single.patch [bz#1957194]
- kvm-hw-arm-smmuv3-Another-range-invalidation-fix.patch [bz#1957194]
- Resolves: bz#1952449
  ([aarch64] define RHEL9 machine types)
- Resolves: bz#1747467
  ([aarch64] [qemu] PVPANIC support)
- Resolves: bz#1964795
  (Remove qemu-kvm-block-gluster package)
- Resolves: bz#1965961
  (Remove links to not build roms)
- Resolves: bz#1957194
  (Synchronize RHEL-AV 8.5.0 changes to RHEL 9.0.0 Beta)

* Mon May 31 2021 Miroslav Rezanina <mrezanin@redhat.com> - 6.0.0-4
- kvm-s390x-redhat-disable-experimental-3270-device.patch
- Resolves: bz#1962479
  (Disable the 'x-terminal3270' device in qemu-kvm on s390x)

* Tue May 25 2021 Miroslav Reznaina <mrezanin@redhat.com> - 6.0.0-3
- kvm-hw-s390x-Remove-the-RHEL7-only-machine-type.patch [bz#1944730]
- Resolves: bz#1944730
  (Remove RHEL7 machine type (s390-ccw-virtio-rhel7.5.0))

* Thu May 13 2021 Miroslav Rezanina <mrezanin@redhat.com> - 6.0.0-2
- kvm-Remove-message-with-running-VM-count.patch [bz#1914461]
- kvm-Remove-SPICE-and-QXL-from-x86_64-rh-devices.mak.patch [bz#1906168]
- kvm-spec-file-build-qemu-kvm-without-SPICE-and-QXL.patch [bz#1906168]
- kvm-spec-file-Obsolete-qemu-kvm-ui-spice.patch [bz#1906168]
- Resolves: bz#1914461
  (Remove KVM guest count and limit info message)
- Resolves: bz#1906168
  ([RHEL-9] qemu-kvm spec-file: Do not BuildRequire spice)

* Fri Apr 30 2021 Miroslav Rezanina <mrezanin@redhat.com> - 6.0.0-1
- Rebase to QEMU 6.0
- Resolves: bz#1872569

* Mon Apr 26 2021 Miroslav Rezanina <mrezanin@redhat.com> - 5.2.0-16
- kvm-Limit-build-on-Power-to-qemu-img-and-qemu-ga-only.patch [bz#1944056]
- Resolves: bz#1944056
  (Do not build qemu-kvm for Power)

* Fri Apr 16 2021 Mohan Boddu <mboddu@redhat.com> - 15:5.2.0-15
- Rebuilt for RHEL 9 BETA on Apr 15th 2021. Related: rhbz#1947937

* Sat Mar 20 2021 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 5.2.0-14.el8
- kvm-vhost-user-blk-fix-blkcfg-num_queues-endianness.patch [bz#1937004]
- kvm-block-export-fix-blk_size-double-byteswap.patch [bz#1937004]
- kvm-block-export-use-VIRTIO_BLK_SECTOR_BITS.patch [bz#1937004]
- kvm-block-export-fix-vhost-user-blk-export-sector-number.patch [bz#1937004]
- kvm-block-export-port-virtio-blk-discard-write-zeroes-in.patch [bz#1937004]
- kvm-block-export-port-virtio-blk-read-write-range-check.patch [bz#1937004]
- kvm-spec-ui-spice-sub-package.patch [bz#1936373]
- kvm-spec-ui-opengl-sub-package.patch [bz#1936373]
- Resolves: bz#1937004
  (vhost-user-blk server endianness and input validation fixes)
- Resolves: bz#1936373
  (move spice & opengl modules to rpm subpackages)

* Tue Mar 16 2021 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 5.2.0-13.el8
- kvm-i386-acpi-restore-device-paths-for-pre-5.1-vms.patch [bz#1934158]
- Resolves: bz#1934158
  (Windows guest looses network connectivity when NIC was configured with static IP)

* Mon Mar 15 2021 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 5.2.0-12.el8
- kvm-scsi-disk-move-scsi_handle_rw_error-earlier.patch [bz#1927530]
- kvm-scsi-disk-do-not-complete-requests-early-for-rerror-.patch [bz#1927530]
- kvm-scsi-introduce-scsi_sense_from_errno.patch [bz#1927530]
- kvm-scsi-disk-pass-SCSI-status-to-scsi_handle_rw_error.patch [bz#1927530]
- kvm-scsi-disk-pass-guest-recoverable-errors-through-even.patch [bz#1927530]
- kvm-hw-intc-arm_gic-Fix-interrupt-ID-in-GICD_SGIR-regist.patch [bz#1936948]
- Resolves: bz#1927530
  (RHEL8 Hypervisor - OVIRT  - Issues seen on a virtualization guest with direct passthrough LUNS  pausing when a host gets a Thin threshold warning)
- Resolves: bz#1936948
  (CVE-2021-20221 virt:av/qemu-kvm: qemu: out-of-bound heap buffer access via an interrupt ID field [rhel-av-8.4.0])

* Mon Mar 08 2021 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 5.2.0-11.el8
- kvm-qxl-set-qxl.ssd.dcl.con-on-secondary-devices.patch [bz#1932190]
- kvm-qxl-also-notify-the-rendering-is-done-when-skipping-.patch [bz#1932190]
- kvm-virtiofsd-Save-error-code-early-at-the-failure-calls.patch [bz#1935071]
- kvm-virtiofs-drop-remapped-security.capability-xattr-as-.patch [bz#1935071]
- Resolves: bz#1932190
  (Timeout when dump the screen from 2nd VGA)
- Resolves: bz#1935071
  (CVE-2021-20263 virt:8.4/qemu-kvm: QEMU: virtiofsd: 'security.capabilities' is not dropped with xattrmap option [rhel-av-8])

* Wed Mar 03 2021 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 5.2.0-10.el8
- kvm-migration-dirty-bitmap-Use-struct-for-alias-map-inne.patch [bz#1930757]
- kvm-migration-dirty-bitmap-Allow-control-of-bitmap-persi.patch [bz#1930757]
- kvm-qemu-iotests-300-Add-test-case-for-modifying-persist.patch [bz#1930757]
- kvm-failover-fix-indentantion.patch [bz#1819991]
- kvm-failover-Use-always-atomics-for-primary_should_be_hi.patch [bz#1819991]
- kvm-failover-primary-bus-is-only-used-once-and-where-it-.patch [bz#1819991]
- kvm-failover-Remove-unused-parameter.patch [bz#1819991]
- kvm-failover-Remove-external-partially_hotplugged-proper.patch [bz#1819991]
- kvm-failover-qdev_device_add-returns-err-or-dev-set.patch [bz#1819991]
- kvm-failover-Rename-bool-to-failover_primary_hidden.patch [bz#1819991]
- kvm-failover-g_strcmp0-knows-how-to-handle-NULL.patch [bz#1819991]
- kvm-failover-Remove-primary_device_opts.patch [bz#1819991]
- kvm-failover-remove-standby_id-variable.patch [bz#1819991]
- kvm-failover-Remove-primary_device_dict.patch [bz#1819991]
- kvm-failover-Remove-memory-leak.patch [bz#1819991]
- kvm-failover-simplify-virtio_net_find_primary.patch [bz#1819991]
- kvm-failover-should_be_hidden-should-take-a-bool.patch [bz#1819991]
- kvm-failover-Rename-function-to-hide_device.patch [bz#1819991]
- kvm-failover-virtio_net_connect_failover_devices-does-no.patch [bz#1819991]
- kvm-failover-Rename-to-failover_find_primary_device.patch [bz#1819991]
- kvm-failover-simplify-qdev_device_add-failover-case.patch [bz#1819991]
- kvm-failover-simplify-qdev_device_add.patch [bz#1819991]
- kvm-failover-make-sure-that-id-always-exist.patch [bz#1819991]
- kvm-failover-remove-failover_find_primary_device-error-p.patch [bz#1819991]
- kvm-failover-split-failover_find_primary_device_id.patch [bz#1819991]
- kvm-failover-We-don-t-need-to-cache-primary_device_id-an.patch [bz#1819991]
- kvm-failover-Caller-of-this-two-functions-already-have-p.patch [bz#1819991]
- kvm-failover-simplify-failover_unplug_primary.patch [bz#1819991]
- kvm-failover-Remove-primary_dev-member.patch [bz#1819991]
- kvm-virtio-net-add-missing-object_unref.patch [bz#1819991]
- kvm-x86-cpu-Populate-SVM-CPUID-feature-bits.patch [bz#1926785]
- kvm-i386-Add-the-support-for-AMD-EPYC-3rd-generation-pro.patch [bz#1926785]
- Resolves: bz#1930757
  (Allow control of block-dirty-bitmap persistence via 'block-bitmap-mapping')
- Resolves: bz#1819991
  (Hostdev type interface with net failover enabled exists in domain xml and doesn't reattach to host after hot-unplug)
- Resolves: bz#1926785
  ([RFE] AMD Milan - Add KVM/support for EPYC-Milan CPU Model - Fast Train)

* Mon Mar 01 2021 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 5.2.0-9.el8
- kvm-docs-generate-qemu-storage-daemon-qmp-ref-7-man-page.patch [bz#1901323]
- kvm-docs-add-qemu-storage-daemon-1-man-page.patch [bz#1901323]
- kvm-docs-Add-qemu-storage-daemon-1-manpage-to-meson.buil.patch [bz#1901323]
- kvm-qemu-storage-daemon-Enable-object-add.patch [bz#1901323]
- kvm-spec-Package-qemu-storage-daemon.patch [bz#1901323]
- kvm-default-configs-Enable-vhost-user-blk.patch [bz#1930033]
- kvm-qemu-nbd-Use-SOMAXCONN-for-socket-listen-backlog.patch [bz#1925345]
- kvm-pcie-don-t-set-link-state-active-if-the-slot-is-empt.patch [bz#1917654]
- Resolves: bz#1901323
  (QSD (QEMU Storage Daemon): basic support - TechPreview)
- Resolves: bz#1930033
  (enable vhost-user-blk device)
- Resolves: bz#1925345
  (qemu-nbd needs larger backlog for Unix socket listen())
- Resolves: bz#1917654
  ([failover vf migration][RHEL84 vm] After start a vm with a failover vf + a failover virtio net device, the failvoer vf do not exist in the vm)

* Fri Feb 19 2021 Eduardo Lima (Etrunko) <elima@redhat.com> - 5.2.0-8.el8
- kvm-block-nbd-only-detach-existing-iochannel-from-aio_co.patch [bz#1887883]
- kvm-block-nbd-only-enter-connection-coroutine-if-it-s-pr.patch [bz#1887883]
- kvm-nbd-make-nbd_read-return-EIO-on-error.patch [bz#1887883]
- kvm-virtio-move-use-disabled-flag-property-to-hw_compat_.patch [bz#1907255]
- kvm-virtiofsd-extract-lo_do_open-from-lo_open.patch [bz#1920740]
- kvm-virtiofsd-optionally-return-inode-pointer-from-lo_do.patch [bz#1920740]
- kvm-virtiofsd-prevent-opening-of-special-files-CVE-2020-.patch [bz#1920740]
- kvm-spapr-Adjust-firmware-path-of-PCI-devices.patch [bz#1920941]
- kvm-pci-reject-too-large-ROMs.patch [bz#1917830]
- kvm-pci-add-romsize-property.patch [bz#1917830]
- kvm-redhat-Add-some-devices-for-exporting-upstream-machi.patch [bz#1917826]
- kvm-vhost-Check-for-valid-vdev-in-vhost_backend_handle_i.patch [bz#1880299]
- Resolves: bz#1887883
  (qemu blocks client progress with various NBD actions)
- Resolves: bz#1907255
  (Migrate failed with vhost-vsock-pci from RHEL-AV 8.3.1 to RHEL-AV 8.2.1)
- Resolves: bz#1920740
  (CVE-2020-35517 virt:8.4/qemu-kvm: QEMU: virtiofsd: potential privileged host device access from guest [rhel-av-8.4.0])
- Resolves: bz#1920941
  ([ppc64le] [AV]--disk cdimage.iso,bus=usb fails to boot)
- Resolves: bz#1917830
  (Add romsize property to qemu-kvm)
- Resolves: bz#1917826
  (Add extra device support to qemu-kvm, but not to rhel machine types)
- Resolves: bz#1880299
  (vhost-user mq connection fails to restart after kill host testpmd which acts as vhost-user client)

* Fri Feb 12 2021 Eduardo Lima (Etrunko) <elima@redhat.com> - 5.2.0-7.el8
- kvm-virtio-Add-corresponding-memory_listener_unregister-.patch [bz#1903521]
- kvm-block-Honor-blk_set_aio_context-context-requirements.patch [bz#1918966 bz#1918968]
- kvm-nbd-server-Quiesce-coroutines-on-context-switch.patch [bz#1918966 bz#1918968]
- kvm-block-Avoid-processing-BDS-twice-in-bdrv_set_aio_con.patch [bz#1918966 bz#1918968]
- kvm-storage-daemon-Call-bdrv_close_all-on-exit.patch [bz#1918966 bz#1918968]
- kvm-block-move-blk_exp_close_all-to-qemu_cleanup.patch [bz#1918966 bz#1918968]
- Resolves: bz#1903521
  (hot unplug vhost-user cause qemu crash: qemu-kvm: ../softmmu/memory.c:2818: do_address_space_destroy: Assertion `QTAILQ_EMPTY(&as->listeners)' failed.)
- Resolves: bz#1918966
  ([incremental_backup] qemu aborts if guest reboot during backup when using virtio-blk: "aio_co_schedule: Co-routine was already scheduled in 'aio_co_schedule'")
- Resolves: bz#1918968
  ([incremental_backup] qemu deadlock after poweroff in guest during backup in nbd_export_close_all())

* Tue Feb 09 2021 Eduardo Lima (Etrunko) <elima@redhat.com> - 5.2.0-6.el8
- kvm-scsi-fix-device-removal-race-vs-IO-restart-callback-.patch [bz#1854811]
- kvm-tracetool-also-strip-l-and-ll-from-systemtap-format-.patch [bz#1907264]
- kvm-redhat-moving-all-documentation-files-to-qemu-kvm-do.patch [bz#1881170 bz#1924766]
- kvm-hw-arm-smmuv3-Fix-addr_mask-for-range-based-invalida.patch [bz#1834152]
- kvm-redhat-makes-qemu-respect-system-s-crypto-profile.patch [bz#1902219]
- kvm-vhost-Unbreak-SMMU-and-virtio-iommu-on-dev-iotlb-sup.patch [bz#1925028]
- kvm-docs-set-CONFDIR-when-running-sphinx.patch [bz#1902537]
- Resolves: bz#1854811
  (scsi-bus.c: use-after-free due to race between device unplug and I/O operation causes guest crash)
- Resolves: bz#1907264
  (systemtap: invalid or missing conversion specifier at the trace event vhost_vdpa_set_log_base)
- Resolves: bz#1881170
  (split documentation from the qemu-kvm-core package to its own subpackage)
- Resolves: bz#1924766
  (split documentation from the qemu-kvm-core package to its own subpackage [av-8.4.0])
- Resolves: bz#1834152
  ([aarch64] QEMU SMMUv3 device: Support range invalidation)
- Resolves: bz#1902219
  (QEMU doesn't honour system crypto policies)
- Resolves: bz#1925028
  (vsmmuv3/vhost and virtio-iommu/vhost regression)
- Resolves: bz#1902537
  (The default fsfreeze-hook path from man page and qemu-ga --help command are different)

* Tue Feb 02 2021 Eduardo Lima (Etrunko) <elima@redhat.com> - 5.2.0-5.el8
- kvm-spapr-Allow-memory-unplug-to-always-succeed.patch [bz#1914069]
- kvm-spapr-Improve-handling-of-memory-unplug-with-old-gue.patch [bz#1914069]
- kvm-x86-cpu-Add-AVX512_FP16-cpu-feature.patch [bz#1838738]
- kvm-q35-Increase-max_cpus-to-710-on-pc-q35-rhel8-machine.patch [bz#1904268]
- kvm-config-enable-VFIO_CCW.patch [bz#1922170]
- Resolves: bz#1914069
  ([ppc64le] have this fix for rhel8.4 av (spapr: Allow memory unplug to always succeed))
- Resolves: bz#1838738
  ([Intel 8.4 FEAT] qemu-kvm Sapphire Rapids (SPR) New Instructions (NIs) - Fast Train)
- Resolves: bz#1904268
  ([RFE] [HPEMC] qemu-kvm: support up to 710 VCPUs)
- Resolves: bz#1922170
  (Enable vfio-ccw in AV)

* Wed Jan 27 2021 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 5.2.0-4.el8
- kvm-Drop-bogus-IPv6-messages.patch [bz#1918061]
- Resolves: bz#1918061
  (CVE-2020-10756 virt:rhel/qemu-kvm: QEMU: slirp: networking out-of-bounds read information disclosure vulnerability [rhel-av-8])

* Mon Jan 18 2021 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 5.2.0-3.el8
- kvm-block-nvme-Implement-fake-truncate-coroutine.patch [bz#1848834]
- kvm-spec-find-system-python-via-meson.patch [bz#1899619]
- kvm-build-system-use-b_staticpic-false.patch [bz#1899619]
- kvm-spapr-Fix-buffer-overflow-in-spapr_numa_associativit.patch [bz#1908693]
- kvm-usb-hcd-xhci-pci-Fixup-capabilities-ordering-again.patch [bz#1912846]
- kvm-qga-commands-posix-Send-CCW-address-on-s390x-with-th.patch [bz#1755075]
- kvm-AArch64-machine-types-cleanup.patch [bz#1895276]
- kvm-hw-arm-virt-Add-8.4-Machine-type.patch [bz#1895276]
- kvm-udev-kvm-check-remove-the-exceeded-subscription-limi.patch [bz#1914463]
- kvm-memory-Rename-memory_region_notify_one-to-memory_reg.patch [bz#1845758]
- kvm-memory-Add-IOMMUTLBEvent.patch [bz#1845758]
- kvm-memory-Add-IOMMU_NOTIFIER_DEVIOTLB_UNMAP-IOMMUTLBNot.patch [bz#1845758]
- kvm-intel_iommu-Skip-page-walking-on-device-iotlb-invali.patch [bz#1845758]
- kvm-memory-Skip-bad-range-assertion-if-notifier-is-DEVIO.patch [bz#1845758]
- kvm-RHEL-Switch-pvpanic-test-to-q35.patch [bz#1885555]
- kvm-8.4-x86-machine-type.patch [bz#1885555]
- kvm-memory-clamp-cached-translation-in-case-it-points-to.patch [bz#1904392]
- Resolves: bz#1848834
  (Failed to create luks format image on NVMe device)
- Resolves: bz#1899619
  (QEMU 5.2 is built with PIC objects instead of PIE)
- Resolves: bz#1908693
  ([ppc64le]boot up a guest with 128 numa nodes ,qemu got coredump)
- Resolves: bz#1912846
  (qemu-kvm: Failed to load xhci:parent_obj during migration)
- Resolves: bz#1755075
  ([qemu-guest-agent] fsinfo doesn't return disk info on s390x)
- Resolves: bz#1895276
  (Machine types update for aarch64 for QEMU 5.2.0)
- Resolves: bz#1914463
  (Remove KVM guest count and limit info message)
- Resolves: bz#1845758
  (qemu core dumped: qemu-kvm: /builddir/build/BUILD/qemu-4.2.0/memory.c:1928: memory_region_notify_one: Assertion `entry->iova >= notifier->start && entry_end <= notifier->end' failed.)
- Resolves: bz#1885555
  (8.4 machine types for x86)
- Resolves: bz#1904392
  (CVE-2020-27821 virt:8.4/qemu-kvm: QEMU: heap buffer overflow in msix_table_mmio_write() in hw/pci/msix.c [rhel-av-8])

* Tue Dec 15 2020 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 5.2.0-2.el8
- kvm-redhat-Define-hw_compat_8_3.patch [bz#1893935]
- kvm-redhat-Add-spapr_machine_rhel_default_class_options.patch [bz#1893935]
- kvm-redhat-Define-pseries-rhel8.4.0-machine-type.patch [bz#1893935]
- kvm-redhat-s390x-add-rhel-8.4.0-compat-machine.patch [bz#1836282]
- Resolves: bz#1836282
  (New machine type for qemu-kvm on s390x in RHEL-AV)
- Resolves: bz#1893935
  (New machine type on RHEL-AV 8.4 for ppc64le)

* Wed Dec 09 2020 Miroslav Rezanina <mrezanin@redhat.com> - 5.2.0-1.el8
- Rebase to QEMU 5.2.0 [bz#1905933]
- Resolves: bz#1905933
  (Rebase qemu-kvm to version 5.2.0)

* Tue Dec 01 2020 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 5.1.0-16.el8
- kvm-redhat-introduces-disable_everything-macro-into-the-.patch [bz#1884611]
- kvm-redhat-scripts-extract_build_cmd.py-Avoid-listing-em.patch [bz#1884611]
- kvm-redhat-Removing-unecessary-configurations.patch [bz#1884611]
- kvm-redhat-Fixing-rh-local-build.patch [bz#1884611]
- kvm-redhat-allow-Makefile-rh-prep-builddep-to-fail.patch [bz#1884611]
- kvm-redhat-adding-rh-rpm-target.patch [bz#1884611]
- kvm-redhat-move-shareable-files-from-qemu-kvm-core-to-qe.patch [bz#1884611]
- kvm-redhat-Add-qemu-kiwi-subpackage.patch [bz#1884611]
- Resolves: bz#1884611
  (Build kata-specific version of qemu)

* Mon Nov 16 2020 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 5.1.0-15.el8
- kvm-redhat-add-un-pre-install-systemd-hooks-for-qemu-ga.patch [bz#1882719]
- kvm-rcu-Implement-drain_call_rcu.patch [bz#1812399 bz#1866707]
- kvm-libqtest-Rename-qmp_assert_error_class-to-qmp_expect.patch [bz#1812399 bz#1866707]
- kvm-qtest-rename-qtest_qmp_receive-to-qtest_qmp_receive_.patch [bz#1812399 bz#1866707]
- kvm-qtest-Reintroduce-qtest_qmp_receive-with-QMP-event-b.patch [bz#1812399 bz#1866707]
- kvm-qtest-remove-qtest_qmp_receive_success.patch [bz#1812399 bz#1866707]
- kvm-device-plug-test-use-qtest_qmp-to-send-the-device_de.patch [bz#1812399 bz#1866707]
- kvm-qtest-switch-users-back-to-qtest_qmp_receive.patch [bz#1812399 bz#1866707]
- kvm-qtest-check-that-drives-are-really-appearing-and-dis.patch [bz#1812399 bz#1866707]
- kvm-qemu-iotests-qtest-rewrite-test-067-as-a-qtest.patch [bz#1812399 bz#1866707]
- kvm-qdev-add-check-if-address-free-callback-for-buses.patch [bz#1812399 bz#1866707]
- kvm-scsi-scsi_bus-switch-search-direction-in-scsi_device.patch [bz#1812399 bz#1866707]
- kvm-device_core-use-drain_call_rcu-in-in-qmp_device_add.patch [bz#1812399 bz#1866707]
- kvm-device-core-use-RCU-for-list-of-children-of-a-bus.patch [bz#1812399 bz#1866707]
- kvm-scsi-switch-to-bus-check_address.patch [bz#1812399 bz#1866707]
- kvm-device-core-use-atomic_set-on-.realized-property.patch [bz#1812399 bz#1866707]
- kvm-scsi-scsi-bus-scsi_device_find-don-t-return-unrealiz.patch [bz#1812399]
- kvm-scsi-scsi_bus-Add-scsi_device_get.patch [bz#1812399 bz#1866707]
- kvm-virtio-scsi-use-scsi_device_get.patch [bz#1812399 bz#1866707]
- kvm-scsi-scsi_bus-fix-races-in-REPORT-LUNS.patch [bz#1812399 bz#1866707]
- kvm-tests-migration-fix-memleak-in-wait_command-wait_com.patch [bz#1812399 bz#1866707]
- kvm-libqtest-fix-the-order-of-buffered-events.patch [bz#1812399 bz#1866707]
- kvm-libqtest-fix-memory-leak-in-the-qtest_qmp_event_ref.patch [bz#1812399 bz#1866707]
- kvm-iotests-add-filter_qmp_virtio_scsi-function.patch [bz#1812399 bz#1866707]
- kvm-iotests-rewrite-iotest-240-in-python.patch [bz#1812399 bz#1866707]
- Resolves: bz#1812399
  (Qemu crash when detach disk with cache="none" discard="ignore" io="native")
- Resolves: bz#1866707
  (qemu-kvm is crashing with error "scsi_target_emulate_report_luns: Assertion `i == n + 8' failed")
- Resolves: bz#1882719
  (qemu-ga service still active and can work after qemu-guest-agent been removed)

* Tue Oct 13 2020 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 5.1.0-14.el8_3
- kvm-virtiofsd-avoid-proc-self-fd-tempdir.patch [bz#1884276]
- Resolves: bz#1884276
  (Pod with kata-runtime won't start, QEMU: "vhost_user_dev init failed, Operation not permitted" [mkdtemp failing in sandboxing])

* Thu Oct 08 2020 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 5.1.0-13.el8_3
- kvm-x86-lpc9-let-firmware-negotiate-CPU-hotplug-with-SMI.patch [bz#1846886]
- kvm-x86-cpuhp-prevent-guest-crash-on-CPU-hotplug-when-br.patch [bz#1846886]
- kvm-x86-cpuhp-refuse-cpu-hot-unplug-request-earlier-if-n.patch [bz#1846886]
- Resolves: bz#1846886
  (Guest hit soft lockup or reboots if hotplug vcpu under ovmf)

* Mon Oct 05 2020 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 5.1.0-12.el8_3
- kvm-virtio-skip-legacy-support-check-on-machine-types-le.patch [bz#1868449]
- kvm-vhost-vsock-pci-force-virtio-version-1.patch [bz#1868449]
- kvm-vhost-user-vsock-pci-force-virtio-version-1.patch [bz#1868449]
- kvm-vhost-vsock-ccw-force-virtio-version-1.patch [bz#1868449]
- Resolves: bz#1868449
  (vhost_vsock error: device is modern-only, use disable-legacy=on)

* Mon Oct 05 2020 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 5.1.0-11.el8_3
- kvm-migration-increase-max-bandwidth-to-128-MiB-s-1-Gib-.patch [bz#1874004]
- kvm-redhat-Make-all-generated-so-files-executable-not-on.patch [bz#1876635]
- Resolves: bz#1874004
  (Live migration performance is poor during guest installation process on power host)
- Resolves: bz#1876635
  (VM fails to start with a passthrough smartcard)

* Mon Sep 28 2020 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 5.1.0-10.el8
- kvm-qemu-img-Support-bitmap-merge-into-backing-image.patch [bz#1877209]
- Resolves: bz#1877209
  ('qemu-img bitmaps --merge' failed when trying to merge top volume bitmap to base volume bitmap)

* Mon Sep 21 2020 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 5.1.0-9.el8
- kvm-hw-nvram-fw_cfg-fix-FWCfgDataGeneratorClass-get_data.patch [bz#1688978]
- Resolves: bz#1688978
  (RFE: forward host preferences for cipher suites and CA certs to guest firmware)

* Thu Sep 17 2020 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 5.1.0-8.el8
- kvm-redhat-link-etc-qemu-ga-fsfreeze-hook-to-etc-qemu-kv.patch [bz#1738820]
- kvm-seccomp-fix-killing-of-whole-process-instead-of-thre.patch [bz#1752376]
- kvm-Revert-Drop-bogus-IPv6-messages.patch [bz#1867075]
- kvm-block-rbd-add-namespace-to-qemu_rbd_strong_runtime_o.patch [bz#1821528]
- Resolves: bz#1738820
  ('-F' option of qemu-ga command  cause the guest-fsfreeze-freeze command doesn't work)
- Resolves: bz#1752376
  (qemu use SCMP_ACT_TRAP even SCMP_ACT_KILL_PROCESS is available)
- Resolves: bz#1821528
  (missing namespace attribute when access the rbd image with namespace)
- Resolves: bz#1867075
  (CVE-2020-10756 virt:8.3/qemu-kvm: QEMU: slirp: networking out-of-bounds read information disclosure vulnerability [rhel-av-8])

* Tue Sep 15 2020 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 5.1.0-7.el8
- kvm-target-ppc-Add-experimental-option-for-enabling-secu.patch [bz#1789757 bz#1870384]
- kvm-target-arm-Move-start-powered-off-property-to-generi.patch [bz#1849483]
- kvm-target-arm-Move-setting-of-CPU-halted-state-to-gener.patch [bz#1849483]
- kvm-ppc-spapr-Use-start-powered-off-CPUState-property.patch [bz#1849483]
- Resolves: bz#1789757
  ([IBM 8.4 FEAT] Add machine option to enable secure VM support)
- Resolves: bz#1849483
  (Failed to boot up guest when hotplugging vcpus on bios stage)
- Resolves: bz#1870384
  ([IBM 8.3 FEAT] Add interim/unsupported machine option to enable secure VM support for testing purposes)

* Thu Sep 10 2020 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 5.1.0-6.el8
- kvm-spec-Move-qemu-pr-helper-back-to-usr-bin.patch [bz#1869635]
- kvm-Bump-required-libusbx-version.patch [bz#1856591]
- Resolves: bz#1856591
  (libusbx isn't updated with qemu-kvm)
- Resolves: bz#1869635
  ('/usr/bin/qemu-pr-helper' is not a suitable pr helper: No such file or directory)

* Tue Sep 08 2020 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 5.1.0-5.el8
- kvm-Revert-i386-Fix-pkg_id-offset-for-EPYC-cpu-models.patch [bz#1873417]
- kvm-Revert-target-i386-Enable-new-apic-id-encoding-for-E.patch [bz#1873417]
- kvm-Revert-hw-i386-Move-arch_id-decode-inside-x86_cpus_i.patch [bz#1873417]
- kvm-Revert-i386-Introduce-use_epyc_apic_id_encoding-in-X.patch [bz#1873417]
- kvm-Revert-hw-i386-Introduce-apicid-functions-inside-X86.patch [bz#1873417]
- kvm-Revert-target-i386-Cleanup-and-use-the-EPYC-mode-top.patch [bz#1873417]
- kvm-Revert-hw-386-Add-EPYC-mode-topology-decoding-functi.patch [bz#1873417]
- kvm-nvram-Exit-QEMU-if-NVRAM-cannot-contain-all-prom-env.patch [bz#1867739]
- kvm-usb-fix-setup_len-init-CVE-2020-14364.patch [bz#1869715]
- kvm-Remove-explicit-glusterfs-api-dependency.patch [bz#1872853]
- kvm-disable-virgl.patch [bz#1831271]
- Resolves: bz#1831271
  (Drop virgil acceleration support and remove virglrenderer dependency)
- Resolves: bz#1867739
  (-prom-env does not validate input)
- Resolves: bz#1869715
  (CVE-2020-14364 qemu-kvm: QEMU: usb: out-of-bounds r/w access issue while processing usb packets [rhel-av-8.3.0])
- Resolves: bz#1872853
  (move the glusterfs dependency out of qemu-kvm-core to the glusterfs module)
- Resolves: bz#1873417
  (AMD/NUMA topology - revert 5.1 changes)

* Thu Aug 27 2020 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 5.1.0-4.el8
- kvm-Drop-bogus-IPv6-messages.patch [bz#1867075]
- kvm-machine-types-numa-set-numa_mem_supported-on-old-mac.patch [bz#1849707]
- kvm-machine_types-numa-compatibility-for-auto_enable_num.patch [bz#1849707]
- kvm-migration-Add-block-bitmap-mapping-parameter.patch [bz#1790492]
- kvm-iotests.py-Let-wait_migration-return-on-failure.patch [bz#1790492]
- kvm-iotests-Test-node-bitmap-aliases-during-migration.patch [bz#1790492]
- Resolves: bz#1790492
  ('dirty-bitmaps' migration capability should allow configuring target nodenames)
- Resolves: bz#1849707
  (8.3 machine types for x86 - 5.1 update)
- Resolves: bz#1867075
  (CVE-2020-10756 virt:8.3/qemu-kvm: QEMU: slirp: networking out-of-bounds read information disclosure vulnerability [rhel-av-8])

* Wed Aug 19 2020 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 5.1.0-3.el8
- kvm-redhat-Update-hw_compat_8_2.patch [bz#1843348]
- kvm-redhat-update-pseries-rhel8.2.0-machine-type.patch [bz#1843348]
- kvm-Disable-TPM-passthrough-backend-on-ARM.patch [bz#1801242]
- kvm-Require-libfdt-1.6.0.patch [bz#1867847]
- Resolves: bz#1801242
  ([aarch64] vTPM support in machvirt)
- Resolves: bz#1843348
  (8.3 machine types for POWER)
- Resolves: bz#1867847
  ([ppc] virt module 7629: /usr/libexec/qemu-kvm: undefined symbol: fdt_check_full, version LIBFDT_1.2)

* Wed Aug 12 2020 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 5.1.0-2.el8
- kvm-redhat-define-hw_compat_8_2.patch [bz#1853265]
- Resolves: bz#1853265
  (Forward and backward migration from rhel-av-8.3.0(qemu-kvm-5.0.0) to rhel-av-8.2.1(qemu-kvm-4.2.0) failed with "qemu-kvm: error while loading state for instance 0x0 of device 'spapr'")

* Wed Aug 12 2020 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 5.1.0-1.el8
- Quick changelog fix to reflect the current fixes:
- Resolve: bz#1781911
- Resolve: bz#1841529
- Resolve: bz#1842902
- Resolve: bz#1818843
- Resolve: bz#1819292
- Resolve: bz#1801242

* Wed Aug 12 2020 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 5.1.0-0.el8
- Rebase to 5.1.0
- Resolves: bz#1809650

* Tue Jul 07 2020 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.2.0-29.el8
- kvm-virtio-net-fix-removal-of-failover-device.patch [bz#1820120]
- Resolves: bz#1820120
  (After hotunplugging the vitrio device and netdev, hotunpluging the failover VF will cause qemu core dump)

* Sun Jun 28 2020 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.2.0-28.el8
- kvm-virtio-blk-Refactor-the-code-that-processes-queued-r.patch [bz#1812765]
- kvm-virtio-blk-On-restart-process-queued-requests-in-the.patch [bz#1812765]
- kvm-Fix-use-afte-free-in-ip_reass-CVE-2020-1983.patch [bz#1838082]
- Resolves: bz#1812765
  (qemu with iothreads enabled crashes on resume after enospc pause for disk extension)
- Resolves: bz#1838082
  (CVE-2020-1983 virt:8.2/qemu-kvm: QEMU: slirp: use-after-free in ip_reass() function in ip_input.c [rhel-av-8])

* Thu Jun 18 2020 Eduardo Lima (Etrunko) <elima@redhat.com> - 4.2.0-27.el8
- kvm-hw-pci-pcie-Move-hot-plug-capability-check-to-pre_pl.patch [bz#1820531]
- kvm-spec-Fix-python-shenigans-for-tests.patch [bz#1845779]
- kvm-target-i386-Add-ARCH_CAPABILITIES-related-bits-into-.patch [bz#1840342]
- Resolves: bz#1820531
  (qmp command query-pci get wrong result after hotplug device under hotplug=off controller)
- Resolves: bz#1840342
  ([Intel 8.2.1 Bug] qemu-kvm Add ARCH_CAPABILITIES to Icelake-Server cpu model - Fast Train)
- Resolves: bz#1845779
  (Install 'qemu-kvm-tests' failed as nothing provides /usr/libexec/platform-python3 - virt module 6972)

* Wed Jun 17 2020 Eduardo Lima (Etrunko) <elima@redhat.com> - 4.2.0-26.el8
- kvm-nbd-server-Avoid-long-error-message-assertions-CVE-2.patch [bz#1845384]
- kvm-block-Call-attention-to-truncation-of-long-NBD-expor.patch [bz#1845384]
- Resolves: bz#1845384
  (CVE-2020-10761 virt:8.2/qemu-kvm: QEMU: nbd: reachable assertion failure in nbd_negotiate_send_rep_verr via remote client [rhel-av-8])

* Tue Jun 09 2020 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.2.0-25.el8
- kvm-enable-ramfb.patch [bz#1841068]
- kvm-block-Add-flags-to-BlockDriver.bdrv_co_truncate.patch [bz#1780574]
- kvm-block-Add-flags-to-bdrv-_co-_truncate.patch [bz#1780574]
- kvm-block-backend-Add-flags-to-blk_truncate.patch [bz#1780574]
- kvm-qcow2-Support-BDRV_REQ_ZERO_WRITE-for-truncate.patch [bz#1780574]
- kvm-raw-format-Support-BDRV_REQ_ZERO_WRITE-for-truncate.patch [bz#1780574]
- kvm-file-posix-Support-BDRV_REQ_ZERO_WRITE-for-truncate.patch [bz#1780574]
- kvm-block-truncate-Don-t-make-backing-file-data-visible.patch [bz#1780574]
- kvm-iotests-Add-qemu_io_log.patch [bz#1780574]
- kvm-iotests-Filter-testfiles-out-in-filter_img_info.patch [bz#1780574]
- kvm-iotests-Test-committing-to-short-backing-file.patch [bz#1780574]
- kvm-qcow2-Forward-ZERO_WRITE-flag-for-full-preallocation.patch [bz#1780574]
- kvm-i386-Add-MSR-feature-bit-for-MDS-NO.patch [bz#1769912]
- kvm-i386-Add-macro-for-stibp.patch [bz#1769912]
- kvm-target-i386-Add-new-bit-definitions-of-MSR_IA32_ARCH.patch [bz#1769912]
- kvm-i386-Add-new-CPU-model-Cooperlake.patch [bz#1769912]
- kvm-target-i386-Add-missed-features-to-Cooperlake-CPU-mo.patch [bz#1769912]
- Resolves: bz#1769912
  ([Intel 8.2.1 Feature] introduce Cooper Lake cpu model - qemu-kvm Fast Train)
- Resolves: bz#1780574
  (Data corruption with resizing short overlay over longer backing files)
- Resolves: bz#1841068
  (RFE: please support the "ramfb" display device model)

* Mon Jun 08 2020 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.2.0-24.el8
- kvm-target-i386-set-the-CPUID-level-to-0x14-on-old-machi.patch [bz#1513681]
- kvm-block-curl-HTTP-header-fields-allow-whitespace-aroun.patch [bz#1841038]
- kvm-block-curl-HTTP-header-field-names-are-case-insensit.patch [bz#1841038]
- kvm-MAINTAINERS-fix-qcow2-bitmap.c-under-Dirty-Bitmaps-h.patch [bz#1779893 bz#1779904]
- kvm-iotests-Let-_make_test_img-parse-its-parameters.patch [bz#1779893 bz#1779904]
- kvm-qemu_img-add-cvtnum_full-to-print-error-reports.patch [bz#1779893 bz#1779904]
- kvm-block-Make-it-easier-to-learn-which-BDS-support-bitm.patch [bz#1779893 bz#1779904]
- kvm-blockdev-Promote-several-bitmap-functions-to-non-sta.patch [bz#1779893 bz#1779904]
- kvm-blockdev-Split-off-basic-bitmap-operations-for-qemu-.patch [bz#1779893 bz#1779904]
- kvm-qemu-img-Add-bitmap-sub-command.patch [bz#1779893 bz#1779904]
- kvm-iotests-Fix-test-178.patch [bz#1779893 bz#1779904]
- kvm-qcow2-Expose-bitmaps-size-during-measure.patch [bz#1779893 bz#1779904]
- kvm-qemu-img-Factor-out-code-for-merging-bitmaps.patch [bz#1779893 bz#1779904]
- kvm-qemu-img-Add-convert-bitmaps-option.patch [bz#1779893 bz#1779904]
- kvm-iotests-Add-test-291-to-for-qemu-img-bitmap-coverage.patch [bz#1779893 bz#1779904]
- kvm-iotests-Add-more-skip_if_unsupported-statements-to-t.patch [bz#1778593]
- kvm-iotests-don-t-use-format-for-drive_add.patch [bz#1778593]
- kvm-iotests-055-refactor-compressed-backup-to-vmdk.patch [bz#1778593]
- kvm-iotests-055-skip-vmdk-target-tests-if-vmdk-is-not-wh.patch [bz#1778593]
- kvm-backup-Improve-error-for-bdrv_getlength-failure.patch [bz#1778593]
- kvm-backup-Make-sure-that-source-and-target-size-match.patch [bz#1778593]
- kvm-iotests-Backup-with-different-source-target-size.patch [bz#1778593]
- kvm-iotests-109-Don-t-mirror-with-mismatched-size.patch [bz#1778593]
- kvm-iotests-229-Use-blkdebug-to-inject-an-error.patch [bz#1778593]
- kvm-mirror-Make-sure-that-source-and-target-size-match.patch [bz#1778593]
- kvm-iotests-Mirror-with-different-source-target-size.patch [bz#1778593]
- Resolves: bz#1513681
  ([Intel 8.2.1 Feat] qemu-kvm PT VMX -- Fast Train)
- Resolves: bz#1778593
  (Qemu coredump when backup to a existing small size image)
- Resolves: bz#1779893
  (RFE: Copy bitmaps with qemu-img convert)
- Resolves: bz#1779904
  (RFE: ability to estimate bitmap space utilization for qcow2)
- Resolves: bz#1841038
  (qemu-img: /var/tmp/v2vovl56bced.qcow2: CURL: Error opening file: Server does not support 'range' (byte ranges) with HTTP/2 server in VMware ESXi 7)

* Thu Jun 04 2020 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.2.0-23.el8
- kvm-target-arm-Fix-PAuth-sbox-functions.patch [bz#1813940]
- kvm-Don-t-leak-memory-when-reallocation-fails.patch [bz#1749737]
- kvm-Replace-remaining-malloc-free-user-with-glib.patch [bz#1749737]
- kvm-Revert-RHEL-disable-hostmem-memfd.patch [bz#1839030]
- kvm-block-introducing-bdrv_co_delete_file-interface.patch [bz#1827630]
- kvm-block.c-adding-bdrv_co_delete_file.patch [bz#1827630]
- kvm-crypto.c-cleanup-created-file-when-block_crypto_co_c.patch [bz#1827630]
- Resolves: bz#1749737
  (CVE-2019-15890 qemu-kvm: QEMU: Slirp: use-after-free during packet reassembly [rhel-av-8])
- Resolves: bz#1813940
  (CVE-2020-10702 virt:8.1/qemu-kvm: qemu: weak signature generation in Pointer Authentication support for ARM [rhel-av-8])
- Resolves: bz#1827630
  (volume creation leaving uncleaned stuff behind on error (vol-clone/libvirt/qemu-kvm))
- Resolves: bz#1839030
  (RFE: enable the "memfd" memory backend)

* Mon May 25 2020 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.2.0-22.el8
- kvm-block-always-fill-entire-LUKS-header-space-with-zero.patch [bz#1775462]
- kvm-numa-remove-not-needed-check.patch [bz#1600217]
- kvm-numa-properly-check-if-numa-is-supported.patch [bz#1600217]
- kvm-numa-Extend-CLI-to-provide-initiator-information-for.patch [bz#1600217]
- kvm-numa-Extend-CLI-to-provide-memory-latency-and-bandwi.patch [bz#1600217]
- kvm-numa-Extend-CLI-to-provide-memory-side-cache-informa.patch [bz#1600217]
- kvm-hmat-acpi-Build-Memory-Proximity-Domain-Attributes-S.patch [bz#1600217]
- kvm-hmat-acpi-Build-System-Locality-Latency-and-Bandwidt.patch [bz#1600217]
- kvm-hmat-acpi-Build-Memory-Side-Cache-Information-Struct.patch [bz#1600217]
- kvm-tests-numa-Add-case-for-QMP-build-HMAT.patch [bz#1600217]
- kvm-tests-bios-tables-test-add-test-cases-for-ACPI-HMAT.patch [bz#1600217]
- kvm-ACPI-add-expected-files-for-HMAT-tests-acpihmat.patch [bz#1600217]
- Resolves: bz#1600217
  ([Intel 8.2.1 FEAT] KVM ACPI HMAT support - qemu-kvm  Fast Train)
- Resolves: bz#1775462
  (Creating luks-inside-qcow2 images with cluster_size=2k/4k will get a corrupted image)

* Mon May 11 2020 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.2.0-21.el8
- kvm-hw-pci-pcie-Forbid-hot-plug-if-it-s-disabled-on-the-.patch [bz#1820531]
- kvm-hw-pci-pcie-Replace-PCI_DEVICE-casts-with-existing-v.patch [bz#1820531]
- kvm-tools-virtiofsd-passthrough_ll-Fix-double-close.patch [bz#1817445]
- kvm-virtiofsd-add-rlimit-nofile-NUM-option.patch [bz#1817445]
- kvm-virtiofsd-stay-below-fs.file-max-sysctl-value-CVE-20.patch [bz#1817445]
- kvm-virtiofsd-jail-lo-proc_self_fd.patch [bz#1817445]
- kvm-virtiofsd-Show-submounts.patch [bz#1817445]
- kvm-virtiofsd-only-retain-file-system-capabilities.patch [bz#1817445]
- kvm-virtiofsd-drop-all-capabilities-in-the-wait-parent-p.patch [bz#1817445]
- Resolves: bz#1817445
  (CVE-2020-10717 virt:8.2/qemu-kvm: QEMU: virtiofsd: guest may open maximum file descriptor to cause DoS [rhel-av-8])
- Resolves: bz#1820531
  (qmp command query-pci get wrong result after hotplug device under hotplug=off controller)

* Fri May 01 2020 Jon Maloy <jmaloy@redhat.com> - 4.2.0-20.el8
- kvm-pcie_root_port-Add-hotplug-disabling-option.patch [bz#1790899]
- kvm-compat-disable-edid-for-virtio-gpu-ccw.patch [bz#1816793]
- Resolves: bz#1790899
  ([RFE] QEMU devices should have the option to enable/disable hotplug/unplug)
- Resolves: bz#1816793
  ('edid' compat handling missing for virtio-gpu-ccw)

* Tue Apr 14 2020 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.2.0-19.el8_2
- kvm-target-i386-do-not-set-unsupported-VMX-secondary-exe.patch [bz#1822682]
- Resolves: bz#1822682
  (QEMU-4.2 fails to start a VM on Azure)

* Thu Apr 09 2020 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.2.0-18.el8_2
- kvm-job-take-each-job-s-lock-individually-in-job_txn_app.patch [bz#1817621]
- kvm-replication-assert-we-own-context-before-job_cancel_.patch [bz#1817621]
- kvm-backup-don-t-acquire-aio_context-in-backup_clean.patch [bz#1817621]
- kvm-block-backend-Reorder-flush-pdiscard-function-defini.patch [bz#1817621]
- kvm-block-Increase-BB.in_flight-for-coroutine-and-sync-i.patch [bz#1817621]
- kvm-block-Fix-blk-in_flight-during-blk_wait_while_draine.patch [bz#1817621]
- Resolves: bz#1817621
  (Crash and deadlock with block jobs when using io-threads)

* Mon Mar 30 2020 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.2.0-17.el8
- kvm-block-pass-BlockDriver-reference-to-the-.bdrv_co_cre.patch [bz#1816007]
- kvm-block-trickle-down-the-fallback-image-creation-funct.patch [bz#1816007]
- kvm-Revert-mirror-Don-t-let-an-operation-wait-for-itself.patch [bz#1794692]
- kvm-mirror-Wait-only-for-in-flight-operations.patch [bz#1794692]
- Resolves: bz#1794692
  (Mirror block job stops making progress)
- Resolves: bz#1816007
  (qemu-img convert failed to convert with block device as target)

* Tue Mar 24 2020 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.2.0-16.el8
- kvm-migration-Rate-limit-inside-host-pages.patch [bz#1814336]
- kvm-build-sys-do-not-make-qemu-ga-link-with-pixman.patch [bz#1811670]
- Resolves: bz#1811670
  (Unneeded qemu-guest-agent dependency on pixman)
- Resolves: bz#1814336
  ([POWER9] QEMU migration-test triggers a kernel warning)

* Tue Mar 17 2020 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.2.0-15.el8
- kvm-block-nbd-Fix-hang-in-.bdrv_close.patch [bz#1640894]
- kvm-block-Generic-file-creation-fallback.patch [bz#1640894]
- kvm-file-posix-Drop-hdev_co_create_opts.patch [bz#1640894]
- kvm-iscsi-Drop-iscsi_co_create_opts.patch [bz#1640894]
- kvm-iotests-Add-test-for-image-creation-fallback.patch [bz#1640894]
- kvm-block-Fix-leak-in-bdrv_create_file_fallback.patch [bz#1640894]
- kvm-iotests-Use-complete_and_wait-in-155.patch [bz#1790482 bz#1805143]
- kvm-block-Introduce-bdrv_reopen_commit_post-step.patch [bz#1790482 bz#1805143]
- kvm-block-qcow2-Move-bitmap-reopen-into-bdrv_reopen_comm.patch [bz#1790482 bz#1805143]
- kvm-iotests-Refactor-blockdev-reopen-test-for-iothreads.patch [bz#1790482 bz#1805143]
- kvm-block-bdrv_reopen-with-backing-file-in-different-Aio.patch [bz#1790482 bz#1805143]
- kvm-block-Versioned-x-blockdev-reopen-API-with-feature-f.patch [bz#1790482 bz#1805143]
- kvm-block-Make-bdrv_get_cumulative_perm-public.patch [bz#1790482 bz#1805143]
- kvm-block-Relax-restrictions-for-blockdev-snapshot.patch [bz#1790482 bz#1805143]
- kvm-iotests-Fix-run_job-with-use_log-False.patch [bz#1790482 bz#1805143]
- kvm-iotests-Test-mirror-with-temporarily-disabled-target.patch [bz#1790482 bz#1805143]
- kvm-block-Fix-cross-AioContext-blockdev-snapshot.patch [bz#1790482 bz#1805143]
- kvm-iotests-Add-iothread-cases-to-155.patch [bz#1790482 bz#1805143]
- kvm-qapi-Add-allow-write-only-overlay-feature-for-blockd.patch [bz#1790482 bz#1805143]
- kvm-exec-rom_reset-Free-rom-data-during-inmigrate-skip.patch [bz#1809380]
- Resolves: bz#1640894
  (Fix generic file creation fallback for qemu-img nvme:// image creation support)
- Resolves: bz#1790482
  (bitmaps in backing images can't be modified)
- Resolves: bz#1805143
  (allow late/lazy opening of backing chain for shallow blockdev-mirror)
- Resolves: bz#1809380
  (guest hang during reboot process after migration from RHEl7.8 to RHEL8.2.0.)

* Wed Mar 11 2020 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.2.0-14.el8
- kvm-hw-smbios-set-new-default-SMBIOS-fields-for-Windows-.patch [bz#1782529]
- kvm-migration-multifd-clean-pages-after-filling-packet.patch [bz#1738451]
- kvm-migration-Make-sure-that-we-don-t-call-write-in-case.patch [bz#1738451]
- kvm-migration-multifd-fix-nullptr-access-in-terminating-.patch [bz#1738451]
- kvm-migration-multifd-fix-destroyed-mutex-access-in-term.patch [bz#1738451]
- kvm-multifd-Make-sure-that-we-don-t-do-any-IO-after-an-e.patch [bz#1738451]
- kvm-qemu-file-Don-t-do-IO-after-shutdown.patch [bz#1738451]
- kvm-migration-Don-t-send-data-if-we-have-stopped.patch [bz#1738451]
- kvm-migration-Create-migration_is_running.patch [bz#1738451]
- kvm-migration-multifd-fix-nullptr-access-in-multifd_send.patch [bz#1738451]
- kvm-migration-Maybe-VM-is-paused-when-migration-is-cance.patch [bz#1738451]
- kvm-virtiofsd-Remove-fuse_req_getgroups.patch [bz#1797064]
- kvm-virtiofsd-fv_create_listen_socket-error-path-socket-.patch [bz#1797064]
- kvm-virtiofsd-load_capng-missing-unlock.patch [bz#1797064]
- kvm-virtiofsd-do_read-missing-NULL-check.patch [bz#1797064]
- kvm-tools-virtiofsd-fuse_lowlevel-Fix-fuse_out_header-er.patch [bz#1797064]
- kvm-virtiofsd-passthrough_ll-cleanup-getxattr-listxattr.patch [bz#1797064]
- kvm-virtiofsd-Fix-xattr-operations.patch [bz#1797064]
- Resolves: bz#1738451
  (qemu on src host core dump after set multifd-channels and do migration twice (first migration execute migrate_cancel))
- Resolves: bz#1782529
  (Windows Update Enablement with default smbios strings in qemu)
- Resolves: bz#1797064
  (virtiofsd: Fixes)

* Sat Feb 29 2020 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.2.0-13.el8
- kvm-target-i386-kvm-initialize-feature-MSRs-very-early.patch [bz#1791648]
- kvm-target-i386-add-a-ucode-rev-property.patch [bz#1791648]
- kvm-target-i386-kvm-initialize-microcode-revision-from-K.patch [bz#1791648]
- kvm-target-i386-fix-TCG-UCODE_REV-access.patch [bz#1791648]
- kvm-target-i386-check-for-availability-of-MSR_IA32_UCODE.patch [bz#1791648]
- kvm-target-i386-enable-monitor-and-ucode-revision-with-c.patch [bz#1791648]
- kvm-qcow2-Fix-qcow2_alloc_cluster_abort-for-external-dat.patch [bz#1703907]
- kvm-mirror-Store-MirrorOp.co-for-debuggability.patch [bz#1794692]
- kvm-mirror-Don-t-let-an-operation-wait-for-itself.patch [bz#1794692]
- Resolves: bz#1703907
  ([upstream]QEMU coredump when converting to qcow2: external data file images on block devices with copy_offloading)
- Resolves: bz#1791648
  ([RFE] Passthrough host CPU microcode version to KVM guest if using CPU passthrough)
- Resolves: bz#1794692
  (Mirror block job stops making progress)

* Mon Feb 24 2020 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.2.0-12.el8
- kvm-vhost-user-gpu-Drop-trailing-json-comma.patch [bz#1805334]
- Resolves: bz#1805334
  (vhost-user/50-qemu-gpu.json is not valid JSON)

* Sun Feb 23 2020 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.2.0-11.el8
- kvm-spapr-Enable-DD2.3-accelerated-count-cache-flush-in-.patch [bz#1796240]
- kvm-util-add-slirp_fmt-helpers.patch [bz#1798994]
- kvm-tcp_emu-fix-unsafe-snprintf-usages.patch [bz#1798994]
- kvm-virtio-add-ability-to-delete-vq-through-a-pointer.patch [bz#1791590]
- kvm-virtio-make-virtio_delete_queue-idempotent.patch [bz#1791590]
- kvm-virtio-reset-region-cache-when-on-queue-deletion.patch [bz#1791590]
- kvm-virtio-net-delete-also-control-queue-when-TX-RX-dele.patch [bz#1791590]
- Resolves: bz#1791590
  ([Q35] No "DEVICE_DELETED" event in qmp after unplug virtio-net-pci device)
- Resolves: bz#1796240
  (Enable hw accelerated cache-count-flush by default for POWER9 DD2.3 cpus)
- Resolves: bz#1798994
  (CVE-2020-8608 qemu-kvm: QEMU: Slirp: potential OOB access due to unsafe snprintf() usages [rhel-av-8.2.0])

* Fri Feb 14 2020 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.2.0-10.el8
- kvm-i386-Resolve-CPU-models-to-v1-by-default.patch [bz#1779078 bz#1787291 bz#1779078 bz#1779078]
- kvm-iotests-Support-job-complete-in-run_job.patch [bz#1781637]
- kvm-iotests-Create-VM.blockdev_create.patch [bz#1781637]
- kvm-block-Activate-recursively-even-for-already-active-n.patch [bz#1781637]
- kvm-hmp-Allow-using-qdev-ID-for-qemu-io-command.patch [bz#1781637]
- kvm-iotests-Test-external-snapshot-with-VM-state.patch [bz#1781637]
- kvm-iotests.py-Let-wait_migration-wait-even-more.patch [bz#1781637]
- kvm-blockdev-fix-coding-style-issues-in-drive_backup_pre.patch [bz#1745606 bz#1746217 bz#1773517 bz#1779036 bz#1782111 bz#1782175 bz#1783965]
- kvm-blockdev-unify-qmp_drive_backup-and-drive-backup-tra.patch [bz#1745606 bz#1746217 bz#1773517 bz#1779036 bz#1782111 bz#1782175 bz#1783965]
- kvm-blockdev-unify-qmp_blockdev_backup-and-blockdev-back.patch [bz#1745606 bz#1746217 bz#1773517 bz#1779036 bz#1782111 bz#1782175 bz#1783965]
- kvm-blockdev-honor-bdrv_try_set_aio_context-context-requ.patch [bz#1745606 bz#1746217 bz#1773517 bz#1779036 bz#1782111 bz#1782175 bz#1783965]
- kvm-backup-top-Begin-drain-earlier.patch [bz#1745606 bz#1746217 bz#1773517 bz#1779036 bz#1782111 bz#1782175 bz#1783965]
- kvm-block-backup-top-Don-t-acquire-context-while-droppin.patch [bz#1745606 bz#1746217 bz#1773517 bz#1779036 bz#1782111 bz#1782175 bz#1783965]
- kvm-blockdev-Acquire-AioContext-on-dirty-bitmap-function.patch [bz#1745606 bz#1746217 bz#1773517 bz#1779036 bz#1782111 bz#1782175 bz#1783965]
- kvm-blockdev-Return-bs-to-the-proper-context-on-snapshot.patch [bz#1745606 bz#1746217 bz#1773517 bz#1779036 bz#1782111 bz#1782175 bz#1783965]
- kvm-iotests-Test-handling-of-AioContexts-with-some-block.patch [bz#1745606 bz#1746217 bz#1773517 bz#1779036 bz#1782111 bz#1782175 bz#1783965]
- kvm-target-arm-monitor-query-cpu-model-expansion-crashed.patch [bz#1801320]
- kvm-docs-arm-cpu-features-Make-kvm-no-adjvtime-comment-c.patch [bz#1801320]
- Resolves: bz#1745606
  (Qemu hang when do incremental live backup in transaction mode without bitmap)
- Resolves: bz#1746217
  (Src qemu hang when do storage vm migration during guest installation)
- Resolves: bz#1773517
  (Src qemu hang when do storage vm migration with dataplane enable)
- Resolves: bz#1779036
  (Qemu coredump when do snapshot in transaction mode with one snapshot path not exist)
- Resolves: bz#1779078
  (RHVH 4.4: Failed to run VM on 4.3/4.4 engine (Exit message: the CPU is incompatible with host CPU: Host CPU does not provide required features: hle, rtm))
- Resolves: bz#1781637
  (qemu crashed when do mem and disk snapshot)
- Resolves: bz#1782111
  (Qemu hang when do full backup on multi-disks with one job's 'job-id' missed in transaction mode(data plane enable))
- Resolves: bz#1782175
  (Qemu core dump when add persistent bitmap(data plane enable))
- Resolves: bz#1783965
  (Qemu core dump when do backup with sync: bitmap and no bitmap provided)
- Resolves: bz#1787291
  (RHVH 4.4: Failed to run VM on 4.3/4.4 engine (Exit message: the CPU is incompatible with host CPU: Host CPU does not provide required features: hle, rtm) [rhel-8.1.0.z])
- Resolves: bz#1801320
  (aarch64: backport query-cpu-model-expansion and adjvtime document fixes)

* Mon Feb 10 2020 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.2.0-9.el8
- kvm-ppc-Deassert-the-external-interrupt-pin-in-KVM-on-re.patch [bz#1776638]
- kvm-xics-Don-t-deassert-outputs.patch [bz#1776638]
- kvm-ppc-Don-t-use-CPUPPCState-irq_input_state-with-moder.patch [bz#1776638]
- kvm-trace-update-qemu-trace-stap-to-Python-3.patch [bz#1787395]
- kvm-redhat-Remove-redundant-fix-for-qemu-trace-stap.patch [bz#1787395]
- kvm-iscsi-Cap-block-count-from-GET-LBA-STATUS-CVE-2020-1.patch [bz#1794503]
- kvm-tpm-ppi-page-align-PPI-RAM.patch [bz#1787444]
- kvm-target-arm-kvm-trivial-Clean-up-header-documentation.patch [bz#1647366]
- kvm-target-arm-kvm64-kvm64-cpus-have-timer-registers.patch [bz#1647366]
- kvm-tests-arm-cpu-features-Check-feature-default-values.patch [bz#1647366]
- kvm-target-arm-kvm-Implement-virtual-time-adjustment.patch [bz#1647366]
- kvm-target-arm-cpu-Add-the-kvm-no-adjvtime-CPU-property.patch [bz#1647366]
- kvm-migration-Define-VMSTATE_INSTANCE_ID_ANY.patch [bz#1529231]
- kvm-migration-Change-SaveStateEntry.instance_id-into-uin.patch [bz#1529231]
- kvm-apic-Use-32bit-APIC-ID-for-migration-instance-ID.patch [bz#1529231]
- Resolves: bz#1529231
  ([q35] VM hangs after migration with 200 vCPUs)
- Resolves: bz#1647366
  (aarch64: Add support for the kvm-no-adjvtime ARM CPU feature)
- Resolves: bz#1776638
  (Guest failed to boot up after system_reset  20 times)
- Resolves: bz#1787395
  (qemu-trace-stap list : TypeError: startswith first arg must be bytes or a tuple of bytes, not str)
- Resolves: bz#1787444
  (Broken postcopy migration with vTPM device)
- Resolves: bz#1794503
  (CVE-2020-1711 qemu-kvm: QEMU: block: iscsi: OOB heap access via an unexpected response of iSCSI Server [rhel-av-8.2.0])

* Fri Jan 31 2020 Miroslav Rezanina <mrezanin@redhat.com> - 4.2.0-8.el8
- kvm-target-arm-arch_dump-Add-SVE-notes.patch [bz#1725084]
- kvm-vhost-Add-names-to-section-rounded-warning.patch [bz#1779041]
- kvm-vhost-Only-align-sections-for-vhost-user.patch [bz#1779041]
- kvm-vhost-coding-style-fix.patch [bz#1779041]
- kvm-virtio-fs-fix-MSI-X-nvectors-calculation.patch [bz#1694164]
- kvm-vhost-user-fs-remove-vhostfd-property.patch [bz#1694164]
- kvm-build-rename-CONFIG_LIBCAP-to-CONFIG_LIBCAP_NG.patch [bz#1694164]
- kvm-virtiofsd-Pull-in-upstream-headers.patch [bz#1694164]
- kvm-virtiofsd-Pull-in-kernel-s-fuse.h.patch [bz#1694164]
- kvm-virtiofsd-Add-auxiliary-.c-s.patch [bz#1694164]
- kvm-virtiofsd-Add-fuse_lowlevel.c.patch [bz#1694164]
- kvm-virtiofsd-Add-passthrough_ll.patch [bz#1694164]
- kvm-virtiofsd-Trim-down-imported-files.patch [bz#1694164]
- kvm-virtiofsd-Format-imported-files-to-qemu-style.patch [bz#1694164]
- kvm-virtiofsd-remove-mountpoint-dummy-argument.patch [bz#1694164]
- kvm-virtiofsd-remove-unused-notify-reply-support.patch [bz#1694164]
- kvm-virtiofsd-Remove-unused-enum-fuse_buf_copy_flags.patch [bz#1694164]
- kvm-virtiofsd-Fix-fuse_daemonize-ignored-return-values.patch [bz#1694164]
- kvm-virtiofsd-Fix-common-header-and-define-for-QEMU-buil.patch [bz#1694164]
- kvm-virtiofsd-Trim-out-compatibility-code.patch [bz#1694164]
- kvm-vitriofsd-passthrough_ll-fix-fallocate-ifdefs.patch [bz#1694164]
- kvm-virtiofsd-Make-fsync-work-even-if-only-inode-is-pass.patch [bz#1694164]
- kvm-virtiofsd-Add-options-for-virtio.patch [bz#1694164]
- kvm-virtiofsd-add-o-source-PATH-to-help-output.patch [bz#1694164]
- kvm-virtiofsd-Open-vhost-connection-instead-of-mounting.patch [bz#1694164]
- kvm-virtiofsd-Start-wiring-up-vhost-user.patch [bz#1694164]
- kvm-virtiofsd-Add-main-virtio-loop.patch [bz#1694164]
- kvm-virtiofsd-get-set-features-callbacks.patch [bz#1694164]
- kvm-virtiofsd-Start-queue-threads.patch [bz#1694164]
- kvm-virtiofsd-Poll-kick_fd-for-queue.patch [bz#1694164]
- kvm-virtiofsd-Start-reading-commands-from-queue.patch [bz#1694164]
- kvm-virtiofsd-Send-replies-to-messages.patch [bz#1694164]
- kvm-virtiofsd-Keep-track-of-replies.patch [bz#1694164]
- kvm-virtiofsd-Add-Makefile-wiring-for-virtiofsd-contrib.patch [bz#1694164]
- kvm-virtiofsd-Fast-path-for-virtio-read.patch [bz#1694164]
- kvm-virtiofsd-add-fd-FDNUM-fd-passing-option.patch [bz#1694164]
- kvm-virtiofsd-make-f-foreground-the-default.patch [bz#1694164]
- kvm-virtiofsd-add-vhost-user.json-file.patch [bz#1694164]
- kvm-virtiofsd-add-print-capabilities-option.patch [bz#1694164]
- kvm-virtiofs-Add-maintainers-entry.patch [bz#1694164]
- kvm-virtiofsd-passthrough_ll-create-new-files-in-caller-.patch [bz#1694164]
- kvm-virtiofsd-passthrough_ll-add-lo_map-for-ino-fh-indir.patch [bz#1694164]
- kvm-virtiofsd-passthrough_ll-add-ino_map-to-hide-lo_inod.patch [bz#1694164]
- kvm-virtiofsd-passthrough_ll-add-dirp_map-to-hide-lo_dir.patch [bz#1694164]
- kvm-virtiofsd-passthrough_ll-add-fd_map-to-hide-file-des.patch [bz#1694164]
- kvm-virtiofsd-passthrough_ll-add-fallback-for-racy-ops.patch [bz#1694164]
- kvm-virtiofsd-validate-path-components.patch [bz#1694164]
- kvm-virtiofsd-Plumb-fuse_bufvec-through-to-do_write_buf.patch [bz#1694164]
- kvm-virtiofsd-Pass-write-iov-s-all-the-way-through.patch [bz#1694164]
- kvm-virtiofsd-add-fuse_mbuf_iter-API.patch [bz#1694164]
- kvm-virtiofsd-validate-input-buffer-sizes-in-do_write_bu.patch [bz#1694164]
- kvm-virtiofsd-check-input-buffer-size-in-fuse_lowlevel.c.patch [bz#1694164]
- kvm-virtiofsd-prevent-.-escape-in-lo_do_lookup.patch [bz#1694164]
- kvm-virtiofsd-prevent-.-escape-in-lo_do_readdir.patch [bz#1694164]
- kvm-virtiofsd-use-proc-self-fd-O_PATH-file-descriptor.patch [bz#1694164]
- kvm-virtiofsd-sandbox-mount-namespace.patch [bz#1694164]
- kvm-virtiofsd-move-to-an-empty-network-namespace.patch [bz#1694164]
- kvm-virtiofsd-move-to-a-new-pid-namespace.patch [bz#1694164]
- kvm-virtiofsd-add-seccomp-whitelist.patch [bz#1694164]
- kvm-virtiofsd-Parse-flag-FUSE_WRITE_KILL_PRIV.patch [bz#1694164]
- kvm-virtiofsd-cap-ng-helpers.patch [bz#1694164]
- kvm-virtiofsd-Drop-CAP_FSETID-if-client-asked-for-it.patch [bz#1694164]
- kvm-virtiofsd-set-maximum-RLIMIT_NOFILE-limit.patch [bz#1694164]
- kvm-virtiofsd-fix-libfuse-information-leaks.patch [bz#1694164]
- kvm-virtiofsd-add-syslog-command-line-option.patch [bz#1694164]
- kvm-virtiofsd-print-log-only-when-priority-is-high-enoug.patch [bz#1694164]
- kvm-virtiofsd-Add-ID-to-the-log-with-FUSE_LOG_DEBUG-leve.patch [bz#1694164]
- kvm-virtiofsd-Add-timestamp-to-the-log-with-FUSE_LOG_DEB.patch [bz#1694164]
- kvm-virtiofsd-Handle-reinit.patch [bz#1694164]
- kvm-virtiofsd-Handle-hard-reboot.patch [bz#1694164]
- kvm-virtiofsd-Kill-threads-when-queues-are-stopped.patch [bz#1694164]
- kvm-vhost-user-Print-unexpected-slave-message-types.patch [bz#1694164]
- kvm-contrib-libvhost-user-Protect-slave-fd-with-mutex.patch [bz#1694164]
- kvm-virtiofsd-passthrough_ll-add-renameat2-support.patch [bz#1694164]
- kvm-virtiofsd-passthrough_ll-disable-readdirplus-on-cach.patch [bz#1694164]
- kvm-virtiofsd-passthrough_ll-control-readdirplus.patch [bz#1694164]
- kvm-virtiofsd-rename-unref_inode-to-unref_inode_lolocked.patch [bz#1694164]
- kvm-virtiofsd-fail-when-parent-inode-isn-t-known-in-lo_d.patch [bz#1694164]
- kvm-virtiofsd-extract-root-inode-init-into-setup_root.patch [bz#1694164]
- kvm-virtiofsd-passthrough_ll-clean-up-cache-related-opti.patch [bz#1694164]
- kvm-virtiofsd-passthrough_ll-use-hashtable.patch [bz#1694164]
- kvm-virtiofsd-Clean-up-inodes-on-destroy.patch [bz#1694164]
- kvm-virtiofsd-support-nanosecond-resolution-for-file-tim.patch [bz#1694164]
- kvm-virtiofsd-fix-error-handling-in-main.patch [bz#1694164]
- kvm-virtiofsd-cleanup-allocated-resource-in-se.patch [bz#1694164]
- kvm-virtiofsd-fix-memory-leak-on-lo.source.patch [bz#1694164]
- kvm-virtiofsd-add-helper-for-lo_data-cleanup.patch [bz#1694164]
- kvm-virtiofsd-Prevent-multiply-running-with-same-vhost_u.patch [bz#1694164]
- kvm-virtiofsd-enable-PARALLEL_DIROPS-during-INIT.patch [bz#1694164]
- kvm-virtiofsd-fix-incorrect-error-handling-in-lo_do_look.patch [bz#1694164]
- kvm-Virtiofsd-fix-memory-leak-on-fuse-queueinfo.patch [bz#1694164]
- kvm-virtiofsd-Support-remote-posix-locks.patch [bz#1694164]
- kvm-virtiofsd-use-fuse_lowlevel_is_virtio-in-fuse_sessio.patch [bz#1694164]
- kvm-virtiofsd-prevent-fv_queue_thread-vs-virtio_loop-rac.patch [bz#1694164]
- kvm-virtiofsd-make-lo_release-atomic.patch [bz#1694164]
- kvm-virtiofsd-prevent-races-with-lo_dirp_put.patch [bz#1694164]
- kvm-virtiofsd-rename-inode-refcount-to-inode-nlookup.patch [bz#1694164]
- kvm-libvhost-user-Fix-some-memtable-remap-cases.patch [bz#1694164]
- kvm-virtiofsd-passthrough_ll-fix-refcounting-on-remove-r.patch [bz#1694164]
- kvm-virtiofsd-introduce-inode-refcount-to-prevent-use-af.patch [bz#1694164]
- kvm-virtiofsd-do-not-always-set-FUSE_FLOCK_LOCKS.patch [bz#1694164]
- kvm-virtiofsd-convert-more-fprintf-and-perror-to-use-fus.patch [bz#1694164]
- kvm-virtiofsd-Reset-O_DIRECT-flag-during-file-open.patch [bz#1694164]
- kvm-virtiofsd-Fix-data-corruption-with-O_APPEND-write-in.patch [bz#1694164]
- kvm-virtiofsd-passthrough_ll-Use-cache_readdir-for-direc.patch [bz#1694164]
- kvm-virtiofsd-add-definition-of-fuse_buf_writev.patch [bz#1694164]
- kvm-virtiofsd-use-fuse_buf_writev-to-replace-fuse_buf_wr.patch [bz#1694164]
- kvm-virtiofsd-process-requests-in-a-thread-pool.patch [bz#1694164]
- kvm-virtiofsd-prevent-FUSE_INIT-FUSE_DESTROY-races.patch [bz#1694164]
- kvm-virtiofsd-fix-lo_destroy-resource-leaks.patch [bz#1694164]
- kvm-virtiofsd-add-thread-pool-size-NUM-option.patch [bz#1694164]
- kvm-virtiofsd-Convert-lo_destroy-to-take-the-lo-mutex-lo.patch [bz#1694164]
- kvm-virtiofsd-passthrough_ll-Pass-errno-to-fuse_reply_er.patch [bz#1694164]
- kvm-virtiofsd-stop-all-queue-threads-on-exit-in-virtio_l.patch [bz#1694164]
- kvm-virtiofsd-add-some-options-to-the-help-message.patch [bz#1694164]
- kvm-redhat-ship-virtiofsd-vhost-user-device-backend.patch [bz#1694164]
- Resolves: bz#1694164
  (virtio-fs: host<->guest shared file system (qemu))
- Resolves: bz#1725084
  (aarch64: support dumping SVE registers)
- Resolves: bz#1779041
  (netkvm: no connectivity Windows guest with q35 + hugepages + vhost + hv_synic)

* Tue Jan 21 2020 Miroslav Rezanina <mrezanin@redhat.com> - 4.2.0-7.el8
- kvm-tcp_emu-Fix-oob-access.patch [bz#1791568]
- kvm-slirp-use-correct-size-while-emulating-IRC-commands.patch [bz#1791568]
- kvm-slirp-use-correct-size-while-emulating-commands.patch [bz#1791568]
- kvm-RHEL-hw-i386-disable-nested-PERF_GLOBAL_CTRL-MSR-sup.patch [bz#1559846]
- Resolves: bz#1559846
  (Nested KVM: limit VMX features according to CPU models - Fast Train)
- Resolves: bz#1791568
  (CVE-2020-7039 qemu-kvm: QEMU: slirp: OOB buffer access while emulating tcp protocols in tcp_emu() [rhel-av-8.2.0])

* Wed Jan 15 2020 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.2.0-6.el8
- kvm-spapr-Don-t-trigger-a-CAS-reboot-for-XICS-XIVE-mode-.patch [bz#1733893]
- kvm-vfio-pci-Don-t-remove-irqchip-notifier-if-not-regist.patch [bz#1782678]
- kvm-virtio-don-t-enable-notifications-during-polling.patch [bz#1789301]
- kvm-usbredir-Prevent-recursion-in-usbredir_write.patch [bz#1790844]
- kvm-xhci-recheck-slot-status.patch [bz#1790844]
- Resolves: bz#1733893
  (Boot a guest with "-prom-env 'auto-boot?=false'", SLOF failed to enter the boot entry after input "boot" followed by "0 > " on VNC)
- Resolves: bz#1782678
  (qemu core dump after hot-unplugging the   XXV710/XL710 PF)
- Resolves: bz#1789301
  (virtio-blk/scsi: fix notification suppression during AioContext polling)
- Resolves: bz#1790844
  (USB related fixes)

* Tue Jan 07 2020 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.2.0-5.el8
- kvm-i386-Remove-cpu64-rhel6-CPU-model.patch [bz#1741345]
- kvm-Reallocate-dirty_bmap-when-we-change-a-slot.patch [bz#1772774]
- Resolves: bz#1741345
  (Remove the "cpu64-rhel6" CPU from qemu-kvm)
- Resolves: bz#1772774
  (qemu-kvm core dump during migration+reboot ( Assertion `mem->dirty_bmap' failed ))

* Fri Dec 13 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.2.0-4.el8
- Rebase to qemu-4.2
- Resolves: bz#1783250
  (rebase qemu-kvm to 4.2)

* Tue Dec 10 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.1.0-18.el8
- kvm-LUKS-support-preallocation.patch [bz#1534951]
- kvm-nbd-add-empty-.bdrv_reopen_prepare.patch [bz#1718727]
- kvm-qdev-qbus-add-hidden-device-support.patch [bz#1757796]
- kvm-pci-add-option-for-net-failover.patch [bz#1757796]
- kvm-pci-mark-devices-partially-unplugged.patch [bz#1757796]
- kvm-pci-mark-device-having-guest-unplug-request-pending.patch [bz#1757796]
- kvm-qapi-add-unplug-primary-event.patch [bz#1757796]
- kvm-qapi-add-failover-negotiated-event.patch [bz#1757796]
- kvm-migration-allow-unplug-during-migration-for-failover.patch [bz#1757796]
- kvm-migration-add-new-migration-state-wait-unplug.patch [bz#1757796]
- kvm-libqos-tolerate-wait-unplug-migration-state.patch [bz#1757796]
- kvm-net-virtio-add-failover-support.patch [bz#1757796]
- kvm-vfio-unplug-failover-primary-device-before-migration.patch [bz#1757796]
- kvm-net-virtio-fix-dev_unplug_pending.patch [bz#1757796]
- kvm-net-virtio-return-early-when-failover-primary-alread.patch [bz#1757796]
- kvm-net-virtio-fix-re-plugging-of-primary-device.patch [bz#1757796]
- kvm-net-virtio-return-error-when-device_opts-arg-is-NULL.patch [bz#1757796]
- kvm-vfio-don-t-ignore-return-value-of-migrate_add_blocke.patch [bz#1757796]
- kvm-hw-vfio-pci-Fix-double-free-of-migration_blocker.patch [bz#1757796]
- Resolves: bz#1534951
  (RFE: Support preallocation mode for luks format)
- Resolves: bz#1718727
  (Committing changes to the backing file over NBD fails with reopening files not supported)
- Resolves: bz#1757796
  (RFE: support for net failover devices in qemu)

* Mon Dec 02 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.1.0-17.el8
- kvm-qemu-pr-helper-fix-crash-in-mpath_reconstruct_sense.patch [bz#1772322]
- Resolves: bz#1772322
  (qemu-pr-helper: fix crash in mpath_reconstruct_sense)

* Wed Nov 27 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.1.0-16.el8
- kvm-curl-Keep-pointer-to-the-CURLState-in-CURLSocket.patch [bz#1745209]
- kvm-curl-Keep-socket-until-the-end-of-curl_sock_cb.patch [bz#1745209]
- kvm-curl-Check-completion-in-curl_multi_do.patch [bz#1745209]
- kvm-curl-Pass-CURLSocket-to-curl_multi_do.patch [bz#1745209]
- kvm-curl-Report-only-ready-sockets.patch [bz#1745209]
- kvm-curl-Handle-success-in-multi_check_completion.patch [bz#1745209]
- kvm-curl-Check-curl_multi_add_handle-s-return-code.patch [bz#1745209]
- kvm-vhost-user-save-features-if-the-char-dev-is-closed.patch [bz#1738768]
- kvm-block-snapshot-Restrict-set-of-snapshot-nodes.patch [bz#1658981]
- kvm-iotests-Test-internal-snapshots-with-blockdev.patch [bz#1658981]
- kvm-qapi-Add-feature-flags-to-commands-in-qapi-introspec.patch [bz#1658981]
- kvm-qapi-Allow-introspecting-fix-for-savevm-s-cooperatio.patch [bz#1658981]
- kvm-block-Remove-backing-null-from-bs-explicit_-options.patch [bz#1773925]
- kvm-iotests-Test-multiple-blockdev-snapshot-calls.patch [bz#1773925]
- Resolves: bz#1658981
  (qemu failed to create internal snapshot via 'savevm' when using blockdev)
- Resolves: bz#1738768
  (Guest fails to recover receiving packets after vhost-user reconnect)
- Resolves: bz#1745209
  (qemu-img gets stuck when stream-converting from http)
- Resolves: bz#1773925
  (Fail to do blockcommit with more than one snapshots)

* Thu Nov 14 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.1.0-15.el8
- kvm-virtio-blk-Add-blk_drain-to-virtio_blk_device_unreal.patch [bz#1706759]
- kvm-Revert-qcow2-skip-writing-zero-buffers-to-empty-COW-.patch [bz#1772473]
- kvm-coroutine-Add-qemu_co_mutex_assert_locked.patch [bz#1772473]
- kvm-qcow2-Fix-corruption-bug-in-qcow2_detect_metadata_pr.patch [bz#1772473]
- Resolves: bz#1706759
  (qemu core dump when unplug a 16T GPT type disk from win2019 guest)
- Resolves: bz#1772473
  (Import fixes from 8.1.0 into 8.1.1 branch)

* Tue Oct 29 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.1.0-14.el8
- kvm-Revert-qcow2-skip-writing-zero-buffers-to-empty-COW-.patch [bz#1751934]
- kvm-coroutine-Add-qemu_co_mutex_assert_locked.patch [bz#1764721]
- kvm-qcow2-Fix-corruption-bug-in-qcow2_detect_metadata_pr.patch [bz#1764721]
- Resolves: bz#1751934
  (Fail to install guest when xfs is the host filesystem)
- Resolves: bz#1764721
  (qcow2 image corruption due to incorrect locking in preallocation detection)

* Fri Sep 27 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.1.0-13.el8
- kvm-nbd-server-attach-client-channel-to-the-export-s-Aio.patch [bz#1748253]
- kvm-virtio-blk-schedule-virtio_notify_config-to-run-on-m.patch [bz#1744955]
- Resolves: bz#1744955
  (Qemu hang when block resize a qcow2 image)
- Resolves: bz#1748253
  (QEMU crashes (core dump) when using the integrated NDB server with data-plane)

* Thu Sep 26 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.1.0-12.el8
- kvm-block-Use-QEMU_IS_ALIGNED.patch [bz#1745922]
- kvm-block-qcow2-Fix-corruption-introduced-by-commit-8ac0.patch [bz#1745922]
- kvm-block-qcow2-refactor-encryption-code.patch [bz#1745922]
- kvm-qemu-iotests-Add-test-for-bz-1745922.patch [bz#1745922]
- Resolves: bz#1745922
  (Luks-inside-qcow2 snapshot cannot boot after 'qemu-img rebase')

* Mon Sep 23 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.1.0-11.el8
- kvm-blockjob-update-nodes-head-while-removing-all-bdrv.patch [bz#1746631]
- kvm-hostmem-file-fix-pmem-file-size-check.patch [bz#1724008 bz#1736788]
- kvm-memory-fetch-pmem-size-in-get_file_size.patch [bz#1724008 bz#1736788]
- kvm-pr-manager-Fix-invalid-g_free-crash-bug.patch [bz#1753992]
- Resolves: bz#1724008
  (QEMU core dumped "memory_region_get_ram_ptr: Assertion `mr->ram_block' failed")
- Resolves: bz#1736788
  (QEMU core dumped if boot guest with nvdimm backed by /dev/dax0.0 and option pmem=off)
- Resolves: bz#1746631
  (Qemu core dump when do block commit under stress)
- Resolves: bz#1753992
  (core dump when testing persistent reservation in guest)

* Mon Sep 16 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.1.0-10.el8
- kvm-spapr-xive-Mask-the-EAS-when-allocating-an-IRQ.patch [bz#1748725]
- kvm-block-create-Do-not-abort-if-a-block-driver-is-not-a.patch [bz#1746267]
- kvm-virtio-blk-Cancel-the-pending-BH-when-the-dataplane-.patch [bz#1717321]
- kvm-Using-ip_deq-after-m_free-might-read-pointers-from-a.patch [bz#1749737]
- Resolves: bz#1717321
  (qemu-kvm core dumped when repeat "system_reset" multiple times during guest boot)
- Resolves: bz#1746267
  (qemu coredump: qemu-kvm: block/create.c:68: qmp_blockdev_create: Assertion `drv' failed)
- Resolves: bz#1748725
  ([ppc][migration][v6.3-rc1-p1ce8930]basic migration failed with "qemu-kvm: KVM_SET_DEVICE_ATTR failed: Group 3 attr 0x0000000000001309: Device or resource busy")
- Resolves: bz#1749737
  (CVE-2019-15890 qemu-kvm: QEMU: Slirp: use-after-free during packet reassembly [rhel-av-8])

* Tue Sep 10 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.1.0-9.el8
- kvm-migration-always-initialise-ram_counters-for-a-new-m.patch [bz#1734316]
- kvm-migration-add-qemu_file_update_transfer-interface.patch [bz#1734316]
- kvm-migration-add-speed-limit-for-multifd-migration.patch [bz#1734316]
- kvm-migration-update-ram_counters-for-multifd-sync-packe.patch [bz#1734316]
- kvm-spapr-pci-Consolidate-de-allocation-of-MSIs.patch [bz#1750200]
- kvm-spapr-pci-Free-MSIs-during-reset.patch [bz#1750200]
- Resolves: bz#1734316
  (multifd migration does not honour speed limits, consumes entire bandwidth of NIC)
- Resolves: bz#1750200
  ([RHEL8.1][QEMU4.1]boot up guest with vf device,then system_reset guest,error prompt(qemu-kvm: Can't allocate MSIs for device 2800: IRQ 4904 is not free))

* Mon Sep 09 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.1.0-8.el8
- kvm-migration-Do-not-re-read-the-clock-on-pre_save-in-ca.patch [bz#1747836]
- kvm-ehci-fix-queue-dev-null-ptr-dereference.patch [bz#1746790]
- kvm-spapr-Use-SHUTDOWN_CAUSE_SUBSYSTEM_RESET-for-CAS-reb.patch [bz#1743477]
- kvm-file-posix-Handle-undetectable-alignment.patch [bz#1749134]
- kvm-block-posix-Always-allocate-the-first-block.patch [bz#1749134]
- kvm-iotests-Test-allocate_first_block-with-O_DIRECT.patch [bz#1749134]
- Resolves: bz#1743477
  (Since bd94bc06479a "spapr: change default interrupt mode to 'dual'", QEMU resets the machine to select the appropriate interrupt controller. And -no-reboot prevents that.)
- Resolves: bz#1746790
  (qemu core dump while migrate from RHEL7.6 to RHEL8.1)
- Resolves: bz#1747836
  (Call traces after guest migration due to incorrect handling of the timebase)
- Resolves: bz#1749134
  (I/O error when virtio-blk disk is backed by a raw image on 4k disk)

* Fri Sep 06 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.1.0-7.el8
- kvm-trace-Clarify-DTrace-SystemTap-help-message.patch [bz#1516220]
- kvm-socket-Add-backlog-parameter-to-socket_listen.patch [bz#1726898]
- kvm-socket-Add-num-connections-to-qio_channel_socket_syn.patch [bz#1726898]
- kvm-socket-Add-num-connections-to-qio_channel_socket_asy.patch [bz#1726898]
- kvm-socket-Add-num-connections-to-qio_net_listener_open_.patch [bz#1726898]
- kvm-multifd-Use-number-of-channels-as-listen-backlog.patch [bz#1726898]
- kvm-pseries-Fix-compat_pvr-on-reset.patch [bz#1744107]
- kvm-spapr-Set-compat-mode-in-spapr_core_plug.patch [bz#1744107]
- Resolves: bz#1516220
  (-trace help prints an incomplete list of trace events)
- Resolves: bz#1726898
  (Parallel migration fails with error "Unable to write to socket: Connection reset by peer" now and then)
- Resolves: bz#1744107
  (Migration from P8(qemu4.1) to P9(qemu4.1), after migration, qemu crash on destination with error message "qemu-kvm: error while loading state for instance 0x1 of device 'cpu'")

* Wed Sep 04 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.1.0-6.el8
- kvm-memory-Refactor-memory_region_clear_coalescing.patch [bz#1743142]
- kvm-memory-Split-zones-when-do-coalesced_io_del.patch [bz#1743142]
- kvm-memory-Remove-has_coalesced_range-counter.patch [bz#1743142]
- kvm-memory-Fix-up-memory_region_-add-del-_coalescing.patch [bz#1743142]
- kvm-enable-virgl-for-real-this-time.patch [bz#1559740]
- Resolves: bz#1559740
  ([RFE] Enable virgl as TechPreview (qemu))
- Resolves: bz#1743142
  (Boot guest with multiple e1000 devices, qemu will crash after several guest reboots: kvm_mem_ioeventfd_add: error adding ioeventfd: No space left on device (28))

* Tue Aug 27 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.1.0-5.el8
- kvm-redhat-s390x-Rename-s390-ccw-virtio-rhel8.0.0-to-s39.patch [bz#1693772]
- kvm-redhat-s390x-Add-proper-compatibility-options-for-th.patch [bz#1693772]
- kvm-enable-virgl.patch [bz#1559740]
- kvm-redhat-update-pseries-rhel8.1.0-machine-type.patch [bz#1744170]
- kvm-Do-not-run-iotests-on-brew-build.patch [bz#1742197 bz#1742819]
- Resolves: bz#1559740
  ([RFE] Enable virgl as TechPreview (qemu))
- Resolves: bz#1693772
  ([IBM zKVM] RHEL AV 8.1.0 machine type update for s390x)
- Resolves: bz#1742197
  (Remove iotests from qemu-kvm builds [RHEL AV 8.1.0])
- Resolves: bz#1742819
  (Remove iotests from qemu-kvm builds [RHEL 8.1.0])
- Resolves: bz#1744170
  ([IBM Power] New 8.1.0 machine type for pseries)

* Tue Aug 20 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.1.0-4.el8
- kvm-RHEL-disable-hostmem-memfd.patch [bz#1738626 bz#1740797]
- Resolves: bz#1738626
  (Disable memfd in QEMU)
- Resolves: bz#1740797
  (Disable memfd in QEMU)

* Mon Aug 19 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.1.0-3.el8
- kvm-x86-machine-types-pc_rhel_8_0_compat.patch [bz#1719649]
- kvm-x86-machine-types-q35-Fixup-units_per_default_bus.patch [bz#1719649]
- kvm-x86-machine-types-Fixup-dynamic-sysbus-entries.patch [bz#1719649]
- kvm-x86-machine-types-add-pc-q35-rhel8.1.0.patch [bz#1719649]
- kvm-machine-types-Update-hw_compat_rhel_8_0-from-hw_comp.patch [bz#1719649]
- kvm-virtio-Make-disable-legacy-disable-modern-compat-pro.patch [bz#1719649]
- Resolves: bz#1719649
  (8.1 machine type for x86)

* Mon Aug 19 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.1.0-2.el8
- kvm-spec-Update-seavgabios-dependency.patch [bz#1725664]
- kvm-pc-Don-t-make-die-id-mandatory-unless-necessary.patch [bz#1741451]
- kvm-display-bochs-fix-pcie-support.patch [bz#1733977 bz#1740692]
- kvm-spapr-Reset-CAS-IRQ-subsystem-after-devices.patch [bz#1733977]
- kvm-spapr-xive-Fix-migration-of-hot-plugged-CPUs.patch [bz#1733977]
- kvm-riscv-roms-Fix-make-rules-for-building-sifive_u-bios.patch [bz#1733977 bz#1740692]
- kvm-Update-version-for-v4.1.0-release.patch [bz#1733977 bz#1740692]
- Resolves: bz#1725664
  (Update seabios dependency)
- Resolves: bz#1733977
  (Qemu core dumped: /home/ngu/qemu/hw/intc/xics_kvm.c:321: ics_kvm_set_irq: Assertion `kernel_xics_fd != -1' failed)
- Resolves: bz#1740692
  (Backport QEMU 4.1.0 rc5 & ga patches)
- Resolves: bz#1741451
  (Failed to hot-plug vcpus)

* Wed Aug 14 2019 Miroslav Rezanina <mrezanin@redhat.com> - 4.1.0-1.el8
- Rebase to qemu 4.1.0 rc4 [bz#1705235]
- Resolves: bz#1705235
  (Rebase qemu-kvm for RHEL-AV 8.1.0)

* Tue Jul 23 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.0.0-6.el8
- kvm-x86_64-rh-devices-add-missing-TPM-passthrough.patch [bz#1519013]
- kvm-x86_64-rh-devices-enable-TPM-emulation.patch [bz#1519013]
- kvm-vfio-increase-the-cap-on-number-of-assigned-devices-.patch [bz#1719823]
- Resolves: bz#1519013
  ([RFE] QEMU Software TPM support (vTPM, or TPM emulation))
- Resolves: bz#1719823
  ([RHEL 8.1] [RFE] increase the maximum of vfio devices to more than 32 in qemu-kvm)

* Mon Jul 08 2019 Miroslav Rezanina <mrezanin@redhat.com> - 4.0.0-5.el8
- kvm-qemu-kvm.spec-bump-libseccomp-2.4.0.patch [bz#1720306]
- kvm-qxl-check-release-info-object.patch [bz#1712717]
- kvm-target-i386-add-MDS-NO-feature.patch [bz#1722839]
- kvm-block-file-posix-Unaligned-O_DIRECT-block-status.patch [bz#1588356]
- kvm-iotests-Test-unaligned-raw-images-with-O_DIRECT.patch [bz#1588356]
- kvm-rh-set-CONFIG_BOCHS_DISPLAY-y-for-x86.patch [bz#1707118]
- Resolves: bz#1588356
  (qemu crashed on the source host when do storage migration with source qcow2 disk created by 'qemu-img')
- Resolves: bz#1707118
  (enable device: bochs-display (QEMU))
- Resolves: bz#1712717
  (CVE-2019-12155 qemu-kvm: QEMU: qxl: null pointer dereference while releasing spice resources [rhel-av-8])
- Resolves: bz#1720306
  (VM failed to start with error "failed to install seccomp syscall filter in the kernel")
- Resolves: bz#1722839
  ([Intel 8.1 FEAT] MDS_NO exposure to guest - Fast Train)

* Tue Jun 11 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.0.0-4.el8
- kvm-Disable-VXHS-support.patch [bz#1714937]
- kvm-aarch64-Add-virt-rhel8.1.0-machine-type-for-ARM.patch [bz#1713735]
- kvm-aarch64-Allow-ARM-VIRT-iommu-option-in-RHEL8.1-machi.patch [bz#1713735]
- kvm-usb-call-reset-handler-before-updating-state.patch [bz#1713679]
- kvm-usb-host-skip-reset-for-untouched-devices.patch [bz#1713679]
- kvm-usb-host-avoid-libusb_set_configuration-calls.patch [bz#1713679]
- kvm-aarch64-Compile-out-IOH3420.patch [bz#1627283]
- kvm-vl-Fix-drive-blockdev-persistent-reservation-managem.patch [bz#1714891]
- kvm-vl-Document-why-objects-are-delayed.patch [bz#1714891]
- Resolves: bz#1627283
  (Compile out IOH3420 on aarch64)
- Resolves: bz#1713679
  (Detached device when trying to upgrade USB device firmware when in doing USB Passthrough via QEMU)
- Resolves: bz#1713735
  (Allow ARM VIRT iommu option in RHEL8.1 machine)
- Resolves: bz#1714891
  (Guest with persistent reservation manager for a disk fails to start)
- Resolves: bz#1714937
  (Disable VXHS support)

* Tue May 28 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.0.0-3.el8
- kvm-redhat-fix-cut-n-paste-garbage-in-hw_compat-comments.patch [bz#1709726]
- kvm-compat-Generic-hw_compat_rhel_8_0.patch [bz#1709726]
- kvm-redhat-sync-pseries-rhel7.6.0-with-rhel-av-8.0.1.patch [bz#1709726]
- kvm-redhat-define-pseries-rhel8.1.0-machine-type.patch [bz#1709726]
- Resolves: bz#1709726
  (Forward and backward migration failed with "qemu-kvm: error while loading state for instance 0x0 of device 'spapr'")

* Sat May 25 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 4.0.0-2.el8
- kvm-target-i386-define-md-clear-bit.patch [bz#1703297 bz#1703304 bz#1703310 bz#1707274]
- Resolves: bz#1703297
  (CVE-2018-12126 virt:8.0.0/qemu-kvm: hardware: Microarchitectural Store Buffer Data Sampling (MSBDS) [rhel-av-8])
- Resolves: bz#1703304
  (CVE-2018-12130 virt:8.0.0/qemu-kvm: hardware: Microarchitectural Fill Buffer Data Sampling (MFBDS) [rhel-av-8])
- Resolves: bz#1703310
  (CVE-2018-12127 virt:8.0.0/qemu-kvm: hardware: Micro-architectural Load Port Data Sampling - Information Leak (MLPDS) [rhel-av-8])
- Resolves: bz#1707274
  (CVE-2019-11091 virt:8.0.0/qemu-kvm: hardware: Microarchitectural Data Sampling Uncacheable Memory (MDSUM) [rhel-av-8.1.0])

* Wed May 15 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 3.1.0-26.el8
- kvm-target-ppc-spapr-Add-SPAPR_CAP_LARGE_DECREMENTER.patch [bz#1698711]
- kvm-target-ppc-spapr-Add-workaround-option-to-SPAPR_CAP_.patch [bz#1698711]
- kvm-target-ppc-spapr-Add-SPAPR_CAP_CCF_ASSIST.patch [bz#1698711]
- kvm-target-ppc-tcg-make-spapr_caps-apply-cap-cfpc-sbbc-i.patch [bz#1698711]
- kvm-target-ppc-spapr-Enable-mitigations-by-default-for-p.patch [bz#1698711]
- kvm-slirp-ensure-there-is-enough-space-in-mbuf-to-null-t.patch [bz#1693076]
- kvm-slirp-don-t-manipulate-so_rcv-in-tcp_emu.patch [bz#1693076]
- Resolves: bz#1693076
  (CVE-2019-6778 qemu-kvm: QEMU: slirp: heap buffer overflow in tcp_emu() [rhel-av-8])
- Resolves: bz#1698711
  (Enable Spectre / Meltdown mitigations by default in pseries-rhel8.0.0 machine type)

* Mon May 06 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 3.1.0-25.el8
- kvm-redhat-enable-tpmdev-passthrough.patch [bz#1688312]
- kvm-exec-Only-count-mapped-memory-backends-for-qemu_getr.patch [bz#1680492]
- kvm-Enable-libpmem-to-support-nvdimm.patch [bz#1705149]
- Resolves: bz#1680492
  (Qemu quits suddenly while system_reset after hot-plugging unsupported memory by compatible guest on P9 with 1G huge page set)
- Resolves: bz#1688312
  ([RFE] enable TPM passthrough at compile time (qemu-kvm))
- Resolves: bz#1705149
  (libpmem support is not enabled in qemu-kvm)

* Fri Apr 26 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 3.1.0-24.el8
- kvm-x86-host-phys-bits-limit-option.patch [bz#1688915]
- kvm-rhel-Set-host-phys-bits-limit-48-on-rhel-machine-typ.patch [bz#1688915]
- Resolves: bz#1688915
  ([Intel 8.0 Alpha] physical bits should  <= 48  when host with 5level paging &EPT5 and qemu command with "-cpu qemu64" parameters.)

* Tue Apr 23 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 3.1.0-23.el8
- kvm-device_tree-Fix-integer-overflowing-in-load_device_t.patch [bz#1693173]
- Resolves: bz#1693173
  (CVE-2018-20815 qemu-kvm: QEMU: device_tree: heap buffer overflow while loading device tree blob [rhel-av-8])

* Mon Apr 15 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 3.1.0-22.el8
- kvm-i386-kvm-Disable-arch_capabilities-if-MSR-can-t-be-s.patch [bz#1687578]
- kvm-i386-Make-arch_capabilities-migratable.patch [bz#1687578]
- Resolves: bz#1687578
  (Incorrect CVE vulnerabilities reported on Cascade Lake cpus)

* Thu Apr 11 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 3.1.0-21.el8
- kvm-Remove-7-qcow2-and-luks-iotests-that-are-taking-25-s.patch [bz#1683473]
- kvm-spapr-fix-out-of-bounds-write-in-spapr_populate_drme.patch [bz#1674438]
- kvm-qcow2-include-LUKS-payload-overhead-in-qemu-img-meas.patch [bz#1655065]
- kvm-iotests-add-LUKS-payload-overhead-to-178-qemu-img-me.patch [bz#1655065]
- kvm-vnc-detect-and-optimize-pageflips.patch [bz#1666206]
- kvm-Load-kvm-module-during-boot.patch [bz#1676907 bz#1685995]
- kvm-hostmem-file-reject-invalid-pmem-file-sizes.patch [bz#1669053]
- kvm-iotests-Fix-test-200-on-s390x-without-virtio-pci.patch [bz#1687582]
- kvm-block-file-posix-do-not-fail-on-unlock-bytes.patch [bz#1652572]
- Resolves: bz#1652572
  (QEMU core dumped if stop nfs service during migration)
- Resolves: bz#1655065
  ([rhel.8.0][fast train]'qemu-img measure' size does not match the real allocated size for luks-inside-qcow2 image)
- Resolves: bz#1666206
  (vnc server should detect page-flips and avoid sending fullscreen updates then.)
- Resolves: bz#1669053
  (Guest call trace when boot with nvdimm device backed by /dev/dax)
- Resolves: bz#1674438
  (RHEL8.0 - Guest reboot fails after memory hotplug multiple times (kvm))
- Resolves: bz#1676907
  (/dev/kvm device exists but kernel module is not loaded on boot up causing VM start to fail in libvirt)
- Resolves: bz#1683473
  (Remove 7 qcow2 & luks iotests from rhel8 fast train build %check phase)
- Resolves: bz#1685995
  (/dev/kvm device exists but kernel module is not loaded on boot up causing VM start to fail in libvirt)
- Resolves: bz#1687582
  (QEMU IOTEST 200 fails with 'virtio-scsi-pci is not a valid device model name')

* Fri Mar 15 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 3.1.0-20.el8
- kvm-i386-Add-stibp-flag-name.patch [bz#1686260]
- Resolves: bz#1686260
  (stibp is missing on qemu 3.0 and qemu 3.1)

* Fri Mar 15 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 3.1.0-19.el8
- kvm-migration-Fix-cancel-state.patch [bz#1608649]
- kvm-migration-rdma-Fix-qemu_rdma_cleanup-null-check.patch [bz#1608649]
- Resolves: bz#1608649
  (Query-migrate get "failed" status after migrate-cancel)

* Tue Feb 26 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 3.1.0-18.el8
- kvm-target-i386-Disable-MPX-support-on-named-CPU-models.patch [bz#1661030]
- kvm-i386-remove-the-new-CPUID-PCONFIG-from-Icelake-Serve.patch [bz#1661515]
- kvm-i386-remove-the-INTEL_PT-CPUID-bit-from-named-CPU-mo.patch [bz#1661515]
- kvm-Revert-i386-Add-CPUID-bit-for-PCONFIG.patch [bz#1661515]
- Resolves: bz#1661030
  (Remove MPX support from 8.0 machine types)
- Resolves: bz#1661515
  (Remove PCONFIG and INTEL_PT from Icelake-* CPU models)

* Tue Feb 26 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 3.1.0-17.el8
- kvm-block-Apply-auto-read-only-for-ro-whitelist-drivers.patch [bz#1678968]
- Resolves: bz#1678968
  (-blockdev: auto-read-only is ineffective for drivers on read-only whitelist)

* Mon Feb 25 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 3.1.0-16.el8
- kvm-fdc-Revert-downstream-disablement-of-device-floppy.patch [bz#1664997]
- kvm-fdc-Restrict-floppy-controllers-to-RHEL-7-machine-ty.patch [bz#1664997]
- Resolves: bz#1664997
  (Restrict floppy device to RHEL-7 machine types)

* Wed Feb 13 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 3.1.0-15.el8
- kvm-Add-raw-qcow2-nbd-and-luks-iotests-to-run-during-the.patch [bz#1664855]
- kvm-Introduce-the-qemu-kvm-tests-rpm.patch [bz#1669924]
- Resolves: bz#1664855
  (Run iotests in qemu-kvm build %check phase)
- Resolves: bz#1669924
  (qemu-kvm packaging: Package the avocado_qemu tests and qemu-iotests in a new rpm)

* Tue Feb 12 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 3.1.0-14.el8
- kvm-doc-fix-the-configuration-path.patch [bz#1644985]
- Resolves: bz#1644985
  (The "fsfreeze-hook" script path shown by command "qemu-ga --help" or "man qemu-ga" is wrong - Fast Train)

* Mon Feb 11 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 3.1.0-13.el8
- kvm-Acceptance-tests-add-Linux-initrd-checking-test.patch [bz#1669922]
- kvm-mmap-alloc-unfold-qemu_ram_mmap.patch [bz#1671519]
- kvm-mmap-alloc-fix-hugetlbfs-misaligned-length-in-ppc64.patch [bz#1671519]
- kvm-BZ1653590-Require-at-least-64kiB-pages-for-downstrea.patch [bz#1653590]
- kvm-block-Fix-invalidate_cache-error-path-for-parent-act.patch [bz#1673014]
- kvm-virtio-scsi-Move-BlockBackend-back-to-the-main-AioCo.patch [bz#1656276 bz#1662508]
- kvm-scsi-disk-Acquire-the-AioContext-in-scsi_-_realize.patch [bz#1656276 bz#1662508]
- kvm-virtio-scsi-Forbid-devices-with-different-iothreads-.patch [bz#1656276 bz#1662508]
- Resolves: bz#1653590
  ([Fast train]had better stop qemu immediately while guest was making use of an improper page size)
- Resolves: bz#1656276
  (qemu-kvm core dumped after hotplug the deleted disk with iothread parameter)
- Resolves: bz#1662508
  (Qemu core dump when start guest with two disks using same drive)
- Resolves: bz#1669922
  (Backport avocado-qemu tests for QEMU 3.1)
- Resolves: bz#1671519
  (RHEL8.0 Snapshot3 - qemu doesn't free up hugepage memory when hotplug/hotunplug using memory-backend-file (qemu-kvm))
- Resolves: bz#1673014
  (Local VM and migrated VM on the same host can run with same RAW file as visual disk source while without shareable configured or lock manager enabled)

* Fri Feb 08 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 3.1.0-12.el8
- kvm-io-ensure-UNIX-client-doesn-t-unlink-server-socket.patch [bz#1665896]
- kvm-scsi-disk-Don-t-use-empty-string-as-device-id.patch [bz#1668248]
- kvm-scsi-disk-Add-device_id-property.patch [bz#1668248]
- Resolves: bz#1665896
  (VNC unix listener socket is deleted after first client quits)
- Resolves: bz#1668248
  ("An unknown error has occurred" when using cdrom to install the system with two blockdev disks.(when choose installation destination))

* Thu Jan 31 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 3.1.0-11.el8
- kvm-Fix-fsfreeze-hook-path-in-the-man-page.patch [bz#1644985]
- kvm-json-Fix-handling-when-not-interpolating.patch [bz#1668244]
- Resolves: bz#1644985
  (The "fsfreeze-hook" script path shown by command "qemu-ga --help" or "man qemu-ga" is wrong - Fast Train)
- Resolves: bz#1668244
  (qemu-img: /var/tmp/v2vovl9951f8.qcow2: CURL: Error opening file: The requested URL returned error: 404 Not Found)

* Tue Jan 29 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 3.1.0-10.el8
- kvm-throttle-groups-fix-restart-coroutine-iothread-race.patch [bz#1655947]
- kvm-iotests-add-238-for-throttling-tgm-unregister-iothre.patch [bz#1655947]
- Resolves: bz#1655947
  (qemu-kvm core dumped after unplug the device which was set io throttling parameters)

* Tue Jan 29 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 3.1.0-9.el8
- kvm-migration-rdma-unregister-fd-handler.patch [bz#1666601]
- kvm-s390x-tod-Properly-stop-the-KVM-TOD-while-the-guest-.patch [bz#1659127]
- kvm-hw-s390x-Fix-bad-mask-in-time2tod.patch [bz#1659127]
- Resolves: bz#1659127
  (Stress guest and stop it, then do live migration, guest hit call trace on destination end)
- Resolves: bz#1666601
  ([q35] dst qemu core dumped when do rdma migration with Mellanox IB QDR card)

* Thu Jan 24 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 3.1.0-7.el8
- kvm-i386-kvm-expose-HV_CPUID_ENLIGHTMENT_INFO.EAX-and-HV.patch [bz#1653511]
- kvm-i386-kvm-add-a-comment-explaining-why-.feat_names-ar.patch [bz#1653511]
- Resolves: bz#1653511
  (qemu doesn't report all support cpu features which cause libvirt cannot get the support status of hv_tlbflush)

* Wed Jan 23 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 3.1.0-6.el8
- kvm-spapr-Fix-ibm-max-associativity-domains-property-num.patch [bz#1653114]
- kvm-cpus-ignore-ESRCH-in-qemu_cpu_kick_thread.patch [bz#1668205]
- Resolves: bz#1653114
  (Incorrect NUMA nodes passed to qemu-kvm guest in ibm,max-associativity-domains property)
- Resolves: bz#1668205
  (Guest quit with error when hotunplug cpu)

* Mon Jan 21 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 3.1.0-5.el8
- kvm-virtio-Helper-for-registering-virtio-device-types.patch [bz#1648023]
- kvm-virtio-Provide-version-specific-variants-of-virtio-P.patch [bz#1648023]
- kvm-globals-Allow-global-properties-to-be-optional.patch [bz#1648023]
- kvm-virtio-Make-disable-legacy-disable-modern-compat-pro.patch [bz#1648023]
- kvm-aarch64-Add-virt-rhel8.0.0-machine-type-for-ARM.patch [bz#1656504]
- kvm-aarch64-Set-virt-rhel8.0.0-max_cpus-to-512.patch [bz#1656504]
- kvm-aarch64-Use-256MB-ECAM-region-by-default.patch [bz#1656504]
- Resolves: bz#1648023
  (Provide separate device types for transitional virtio PCI devices - Fast Train)
- Resolves: bz#1656504
  (Machine types for qemu-kvm based on rebase to qemu-3.1 (aarch64))

* Fri Jan 11 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 3.1.0-4.el8
- kvm-hw-s390x-s390-virtio-ccw-Add-machine-types-for-RHEL8.patch [bz#1656510]
- kvm-spapr-Add-H-Call-H_HOME_NODE_ASSOCIATIVITY.patch [bz#1661967]
- kvm-redhat-Fixing-.gitpublish-to-include-AV-information.patch []
- Resolves: bz#1656510
  (Machine types for qemu-kvm based on rebase to qemu-3.1 (s390x))
- Resolves: bz#1661967
  (Kernel prints the message "VPHN is not supported. Disabling polling...")

* Thu Jan 03 2019 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 3.1.0-3.el8
- kvm-redhat-define-pseries-rhel8.0.0-machine-type.patch [bz#1656508]
- Resolves: bz#1656508
  (Machine types for qemu-kvm based on rebase to qemu-3.1 (ppc64le))

* Fri Dec 21 2018 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 3.1.0-2.el8
- kvm-pc-7.5-compat-entries.patch [bz#1655820]
- kvm-compat-Generic-HW_COMPAT_RHEL7_6.patch [bz#1655820]
- kvm-pc-PC_RHEL7_6_COMPAT.patch [bz#1655820]
- kvm-pc-Add-compat-for-pc-i440fx-rhel7.6.0-machine-type.patch [bz#1655820]
- kvm-pc-Add-pc-q35-8.0.0-machine-type.patch [bz#1655820]
- kvm-pc-Add-x-migrate-smi-count-off-to-PC_RHEL7_6_COMPAT.patch [bz#1655820]
- kvm-clear-out-KVM_ASYNC_PF_DELIVERY_AS_PF_VMEXIT-for.patch [bz#1659604]
- kvm-Add-edk2-Requires-to-qemu-kvm.patch [bz#1660208]
- Resolves: bz#1655820
  (Can't migarate between rhel8 and rhel7 when guest has device "video")
- Resolves: bz#1659604
  (8->7 migration failed: qemu-kvm: error: failed to set MSR 0x4b564d02 to 0x27fc13285)
- Resolves: bz#1660208
  (qemu-kvm: Should depend on the architecture-appropriate guest firmware)

* Thu Dec 13 2018 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 3.1.0-1.el8
- Rebase to qemu-kvm 3.1.0

* Tue Dec 11 2018 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - qemu-kvm-2.12.0-47
- kvm-Disable-CONFIG_IPMI-and-CONFIG_I2C-for-ppc64.patch [bz#1640044]
- kvm-Disable-CONFIG_CAN_BUS-and-CONFIG_CAN_SJA1000.patch [bz#1640042]
- Resolves: bz#1640042
  (Disable CONFIG_CAN_BUS and CONFIG_CAN_SJA1000 config switches)
- Resolves: bz#1640044
  (Disable CONFIG_I2C and CONFIG_IPMI in default-configs/ppc64-softmmu.mak)

* Tue Dec 11 2018 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - qemu-kvm-2.12.0-46
- kvm-qcow2-Give-the-refcount-cache-the-minimum-possible-s.patch [bz#1656507]
- kvm-docs-Document-the-new-default-sizes-of-the-qcow2-cac.patch [bz#1656507]
- kvm-qcow2-Fix-Coverity-warning-when-calculating-the-refc.patch [bz#1656507]
- kvm-include-Add-IEC-binary-prefixes-in-qemu-units.h.patch [bz#1656507]
- kvm-qcow2-Options-documentation-fixes.patch [bz#1656507]
- kvm-include-Add-a-lookup-table-of-sizes.patch [bz#1656507]
- kvm-qcow2-Make-sizes-more-humanly-readable.patch [bz#1656507]
- kvm-qcow2-Avoid-duplication-in-setting-the-refcount-cach.patch [bz#1656507]
- kvm-qcow2-Assign-the-L2-cache-relatively-to-the-image-si.patch [bz#1656507]
- kvm-qcow2-Increase-the-default-upper-limit-on-the-L2-cac.patch [bz#1656507]
- kvm-qcow2-Resize-the-cache-upon-image-resizing.patch [bz#1656507]
- kvm-qcow2-Set-the-default-cache-clean-interval-to-10-min.patch [bz#1656507]
- kvm-qcow2-Explicit-number-replaced-by-a-constant.patch [bz#1656507]
- kvm-block-backend-Set-werror-rerror-defaults-in-blk_new.patch [bz#1657637]
- kvm-qcow2-Fix-cache-clean-interval-documentation.patch [bz#1656507]
- Resolves: bz#1656507
  ([RHEL.8] qcow2 cache is too small)
- Resolves: bz#1657637
  (Wrong werror default for -device drive=<node-name>)

* Thu Dec 06 2018 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - qemu-kvm-2.12.0-45
- kvm-target-ppc-add-basic-support-for-PTCR-on-POWER9.patch [bz#1639069]
- kvm-linux-headers-Update-for-nested-KVM-HV-downstream-on.patch [bz#1639069]
- kvm-target-ppc-Add-one-reg-id-for-ptcr.patch [bz#1639069]
- kvm-ppc-spapr_caps-Add-SPAPR_CAP_NESTED_KVM_HV.patch [bz#1639069]
- kvm-Re-enable-CONFIG_HYPERV_TESTDEV.patch [bz#1651195]
- kvm-qxl-use-guest_monitor_config-for-local-renderer.patch [bz#1610163]
- kvm-Declare-cirrus-vga-as-deprecated.patch [bz#1651994]
- kvm-Do-not-build-bluetooth-support.patch [bz#1654651]
- kvm-vfio-helpers-Fix-qemu_vfio_open_pci-crash.patch [bz#1645840]
- kvm-balloon-Allow-multiple-inhibit-users.patch [bz#1650272]
- kvm-Use-inhibit-to-prevent-ballooning-without-synchr.patch [bz#1650272]
- kvm-vfio-Inhibit-ballooning-based-on-group-attachment-to.patch [bz#1650272]
- kvm-vfio-ccw-pci-Allow-devices-to-opt-in-for-ballooning.patch [bz#1650272]
- kvm-vfio-pci-Handle-subsystem-realpath-returning-NULL.patch [bz#1650272]
- kvm-vfio-pci-Fix-failure-to-close-file-descriptor-on-err.patch [bz#1650272]
- kvm-postcopy-Synchronize-usage-of-the-balloon-inhibitor.patch [bz#1650272]
- Resolves: bz#1610163
  (guest shows border blurred screen with some resolutions when qemu boot with -device qxl-vga ,and guest on rhel7.6 has no  such question)
- Resolves: bz#1639069
  ([IBM 8.0 FEAT] POWER9 - Nested virtualization in RHEL8.0 KVM for ppc64le - qemu-kvm side)
- Resolves: bz#1645840
  (Qemu core dump when hotplug nvme:// drive via -blockdev)
- Resolves: bz#1650272
  (Ballooning is incompatible with vfio assigned devices, but not prevented)
- Resolves: bz#1651195
  (Re-enable hyperv-testdev device)
- Resolves: bz#1651994
  (Declare the "Cirrus VGA" device emulation of QEMU as deprecated in RHEL8)
- Resolves: bz#1654651
  (Qemu: hw: bt: keep bt/* objects from building [rhel-8.0])

* Tue Nov 27 2018 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - qemu-kvm-2.12.0-43
- kvm-block-Make-more-block-drivers-compile-time-configura.patch [bz#1598842 bz#1598842]
- kvm-RHEL8-Add-disable-configure-options-to-qemu-spec-fil.patch [bz#1598842]
- Resolves: bz#1598842
  (Compile out unused block drivers)

* Mon Nov 26 2018 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - qemu-kvm-2.12.0-43

- kvm-configure-add-test-for-libudev.patch [bz#1636185]
- kvm-qga-linux-report-disk-serial-number.patch [bz#1636185]
- kvm-qga-linux-return-disk-device-in-guest-get-fsinfo.patch [bz#1636185]
- kvm-qemu-error-introduce-error-warn-_report_once.patch [bz#1625173]
- kvm-intel-iommu-start-to-use-error_report_once.patch [bz#1625173]
- kvm-intel-iommu-replace-more-vtd_err_-traces.patch [bz#1625173]
- kvm-intel_iommu-introduce-vtd_reset_caches.patch [bz#1625173]
- kvm-intel_iommu-better-handling-of-dmar-state-switch.patch [bz#1625173]
- kvm-intel_iommu-move-ce-fetching-out-when-sync-shadow.patch [bz#1625173 bz#1629616]
- kvm-intel_iommu-handle-invalid-ce-for-shadow-sync.patch [bz#1625173 bz#1629616]
- kvm-block-remove-bdrv_dirty_bitmap_make_anon.patch [bz#1518989]
- kvm-block-simplify-code-around-releasing-bitmaps.patch [bz#1518989]
- kvm-hbitmap-Add-advance-param-to-hbitmap_iter_next.patch [bz#1518989]
- kvm-test-hbitmap-Add-non-advancing-iter_next-tests.patch [bz#1518989]
- kvm-block-dirty-bitmap-Add-bdrv_dirty_iter_next_area.patch [bz#1518989]
- kvm-blockdev-backup-add-bitmap-argument.patch [bz#1518989]
- kvm-dirty-bitmap-switch-assert-fails-to-errors-in-bdrv_m.patch [bz#1518989]
- kvm-dirty-bitmap-rename-bdrv_undo_clear_dirty_bitmap.patch [bz#1518989]
- kvm-dirty-bitmap-make-it-possible-to-restore-bitmap-afte.patch [bz#1518989]
- kvm-blockdev-rename-block-dirty-bitmap-clear-transaction.patch [bz#1518989]
- kvm-qapi-add-transaction-support-for-x-block-dirty-bitma.patch [bz#1518989]
- kvm-block-dirty-bitmaps-add-user_locked-status-checker.patch [bz#1518989]
- kvm-block-dirty-bitmaps-fix-merge-permissions.patch [bz#1518989]
- kvm-block-dirty-bitmaps-allow-clear-on-disabled-bitmaps.patch [bz#1518989]
- kvm-block-dirty-bitmaps-prohibit-enable-disable-on-locke.patch [bz#1518989]
- kvm-block-backup-prohibit-backup-from-using-in-use-bitma.patch [bz#1518989]
- kvm-nbd-forbid-use-of-frozen-bitmaps.patch [bz#1518989]
- kvm-bitmap-Update-count-after-a-merge.patch [bz#1518989]
- kvm-iotests-169-drop-deprecated-autoload-parameter.patch [bz#1518989]
- kvm-block-qcow2-improve-error-message-in-qcow2_inactivat.patch [bz#1518989]
- kvm-bloc-qcow2-drop-dirty_bitmaps_loaded-state-variable.patch [bz#1518989]
- kvm-dirty-bitmaps-clean-up-bitmaps-loading-and-migration.patch [bz#1518989]
- kvm-iotests-improve-169.patch [bz#1518989]
- kvm-iotests-169-add-cases-for-source-vm-resuming.patch [bz#1518989]
- kvm-pc-dimm-turn-alignment-assert-into-check.patch [bz#1630116]
- Resolves: bz#1518989
  (RFE: QEMU Incremental live backup)
- Resolves: bz#1625173
  ([NVMe Device Assignment] Guest could not boot up with q35+iommu)
- Resolves: bz#1629616
  (boot guest with q35+vIOMMU+ device assignment, qemu terminal shows "qemu-kvm: VFIO_UNMAP_DMA: -22" when return assigned network devices from vfio driver to ixgbe in guest)
- Resolves: bz#1630116
  (pc_dimm_get_free_addr: assertion failed: (QEMU_ALIGN_UP(address_space_start, align) == address_space_start))
- Resolves: bz#1636185
  ([RFE] Report disk device name and serial number (qemu-guest-agent on Linux))

* Mon Nov 05 2018 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 2.12.0-42.el8
- kvm-luks-Allow-share-rw-on.patch [bz#1629701]
- kvm-redhat-reenable-gluster-support.patch [bz#1599340]
- kvm-redhat-bump-libusb-requirement.patch [bz#1627970]
- Resolves: bz#1599340
  (Reenable glusterfs in qemu-kvm once BZ#1567292 gets fixed)
- Resolves: bz#1627970
  (symbol lookup error: /usr/libexec/qemu-kvm: undefined symbol: libusb_set_option)
- Resolves: bz#1629701
  ("share-rw=on" does not work for luks format image - Fast Train)

* Tue Oct 16 2018 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 2.12.0-41.el8
- kvm-block-rbd-pull-out-qemu_rbd_convert_options.patch [bz#1635585]
- kvm-block-rbd-Attempt-to-parse-legacy-filenames.patch [bz#1635585]
- kvm-block-rbd-add-deprecation-documentation-for-filename.patch [bz#1635585]
- kvm-block-rbd-add-iotest-for-rbd-legacy-keyvalue-filenam.patch [bz#1635585]
- Resolves: bz#1635585
  (rbd json format of 7.6 is incompatible with 7.5)

* Tue Oct 16 2018 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 2.12.0-40.el8
- kvm-vnc-call-sasl_server_init-only-when-required.patch [bz#1609327]
- kvm-nbd-server-fix-NBD_CMD_CACHE.patch [bz#1636142]
- kvm-nbd-fix-NBD_FLAG_SEND_CACHE-value.patch [bz#1636142]
- kvm-test-bdrv-drain-bdrv_drain-works-with-cross-AioConte.patch [bz#1637976]
- kvm-block-Use-bdrv_do_drain_begin-end-in-bdrv_drain_all.patch [bz#1637976]
- kvm-block-Remove-recursive-parameter-from-bdrv_drain_inv.patch [bz#1637976]
- kvm-block-Don-t-manually-poll-in-bdrv_drain_all.patch [bz#1637976]
- kvm-tests-test-bdrv-drain-bdrv_drain_all-works-in-corout.patch [bz#1637976]
- kvm-block-Avoid-unnecessary-aio_poll-in-AIO_WAIT_WHILE.patch [bz#1637976]
- kvm-block-Really-pause-block-jobs-on-drain.patch [bz#1637976]
- kvm-block-Remove-bdrv_drain_recurse.patch [bz#1637976]
- kvm-test-bdrv-drain-Add-test-for-node-deletion.patch [bz#1637976]
- kvm-block-Drain-recursively-with-a-single-BDRV_POLL_WHIL.patch [bz#1637976]
- kvm-test-bdrv-drain-Test-node-deletion-in-subtree-recurs.patch [bz#1637976]
- kvm-block-Don-t-poll-in-parent-drain-callbacks.patch [bz#1637976]
- kvm-test-bdrv-drain-Graph-change-through-parent-callback.patch [bz#1637976]
- kvm-block-Defer-.bdrv_drain_begin-callback-to-polling-ph.patch [bz#1637976]
- kvm-test-bdrv-drain-Test-that-bdrv_drain_invoke-doesn-t-.patch [bz#1637976]
- kvm-block-Allow-AIO_WAIT_WHILE-with-NULL-ctx.patch [bz#1637976]
- kvm-block-Move-bdrv_drain_all_begin-out-of-coroutine-con.patch [bz#1637976]
- kvm-block-ignore_bds_parents-parameter-for-drain-functio.patch [bz#1637976]
- kvm-block-Allow-graph-changes-in-bdrv_drain_all_begin-en.patch [bz#1637976]
- kvm-test-bdrv-drain-Test-graph-changes-in-drain_all-sect.patch [bz#1637976]
- kvm-block-Poll-after-drain-on-attaching-a-node.patch [bz#1637976]
- kvm-test-bdrv-drain-Test-bdrv_append-to-drained-node.patch [bz#1637976]
- kvm-block-linux-aio-acquire-AioContext-before-qemu_laio_.patch [bz#1637976]
- kvm-util-async-use-qemu_aio_coroutine_enter-in-co_schedu.patch [bz#1637976]
- kvm-job-Fix-nested-aio_poll-hanging-in-job_txn_apply.patch [bz#1637976]
- kvm-job-Fix-missing-locking-due-to-mismerge.patch [bz#1637976]
- kvm-blockjob-Wake-up-BDS-when-job-becomes-idle.patch [bz#1637976]
- kvm-aio-wait-Increase-num_waiters-even-in-home-thread.patch [bz#1637976]
- kvm-test-bdrv-drain-Drain-with-block-jobs-in-an-I-O-thre.patch [bz#1637976]
- kvm-test-blockjob-Acquire-AioContext-around-job_cancel_s.patch [bz#1637976]
- kvm-job-Use-AIO_WAIT_WHILE-in-job_finish_sync.patch [bz#1637976]
- kvm-test-bdrv-drain-Test-AIO_WAIT_WHILE-in-completion-ca.patch [bz#1637976]
- kvm-block-Add-missing-locking-in-bdrv_co_drain_bh_cb.patch [bz#1637976]
- kvm-block-backend-Add-.drained_poll-callback.patch [bz#1637976]
- kvm-block-backend-Fix-potential-double-blk_delete.patch [bz#1637976]
- kvm-block-backend-Decrease-in_flight-only-after-callback.patch [bz#1637976]
- kvm-blockjob-Lie-better-in-child_job_drained_poll.patch [bz#1637976]
- kvm-block-Remove-aio_poll-in-bdrv_drain_poll-variants.patch [bz#1637976]
- kvm-test-bdrv-drain-Test-nested-poll-in-bdrv_drain_poll_.patch [bz#1637976]
- kvm-job-Avoid-deadlocks-in-job_completed_txn_abort.patch [bz#1637976]
- kvm-test-bdrv-drain-AIO_WAIT_WHILE-in-job-.commit-.abort.patch [bz#1637976]
- kvm-test-bdrv-drain-Fix-outdated-comments.patch [bz#1637976]
- kvm-block-Use-a-single-global-AioWait.patch [bz#1637976]
- kvm-test-bdrv-drain-Test-draining-job-source-child-and-p.patch [bz#1637976]
- kvm-qemu-img-Fix-assert-when-mapping-unaligned-raw-file.patch [bz#1639374]
- kvm-iotests-Add-test-221-to-catch-qemu-img-map-regressio.patch [bz#1639374]
- Resolves: bz#1609327
  (qemu-kvm[37046]: Could not find keytab file: /etc/qemu/krb5.tab: Unknown error 49408)
- Resolves: bz#1636142
  (qemu NBD_CMD_CACHE flaws impacting non-qemu NBD clients)
- Resolves: bz#1637976
  (Crashes and hangs with iothreads vs. block jobs)
- Resolves: bz#1639374
  (qemu-img map 'Aborted (core dumped)' when specifying a plain file)

* Tue Oct 16 2018 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> -
- kvm-linux-headers-update.patch [bz#1508142]
- kvm-s390x-cpumodel-Set-up-CPU-model-for-AP-device-suppor.patch [bz#1508142]
- kvm-s390x-kvm-enable-AP-instruction-interpretation-for-g.patch [bz#1508142]
- kvm-s390x-ap-base-Adjunct-Processor-AP-object-model.patch [bz#1508142]
- kvm-s390x-vfio-ap-Introduce-VFIO-AP-device.patch [bz#1508142]
- kvm-s390-doc-detailed-specifications-for-AP-virtualizati.patch [bz#1508142]
- Resolves: bz#1508142
  ([IBM 8.0 FEAT] KVM: Guest-dedicated Crypto Adapters - qemu part)

* Mon Oct 15 2018 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 2.12.0-38.el8
- kvm-Revert-hw-acpi-build-build-SRAT-memory-affinity-stru.patch [bz#1609235]
- kvm-add-udev-kvm-check.patch [bz#1552663]
- kvm-aio-posix-Don-t-count-ctx-notifier-as-progress-when-.patch [bz#1623085]
- kvm-aio-Do-aio_notify_accept-only-during-blocking-aio_po.patch [bz#1623085]
- kvm-aio-posix-fix-concurrent-access-to-poll_disable_cnt.patch [bz#1632622]
- kvm-aio-posix-compute-timeout-before-polling.patch [bz#1632622]
- kvm-aio-posix-do-skip-system-call-if-ctx-notifier-pollin.patch [bz#1632622]
- kvm-intel-iommu-send-PSI-always-even-if-across-PDEs.patch [bz#1450712]
- kvm-intel-iommu-remove-IntelIOMMUNotifierNode.patch [bz#1450712]
- kvm-intel-iommu-add-iommu-lock.patch [bz#1450712]
- kvm-intel-iommu-only-do-page-walk-for-MAP-notifiers.patch [bz#1450712]
- kvm-intel-iommu-introduce-vtd_page_walk_info.patch [bz#1450712]
- kvm-intel-iommu-pass-in-address-space-when-page-walk.patch [bz#1450712]
- kvm-intel-iommu-trace-domain-id-during-page-walk.patch [bz#1450712]
- kvm-util-implement-simple-iova-tree.patch [bz#1450712]
- kvm-intel-iommu-rework-the-page-walk-logic.patch [bz#1450712]
- kvm-i386-define-the-ssbd-CPUID-feature-bit-CVE-2018-3639.patch [bz#1633928]
- Resolves: bz#1450712
  (Booting nested guest with vIOMMU, the assigned network devices can not receive packets (qemu))
- Resolves: bz#1552663
  (81-kvm-rhel.rules is no longer part of initscripts)
- Resolves: bz#1609235
  (Win2016 guest can't recognize pc-dimm hotplugged to node 0)
- Resolves: bz#1623085
  (VM doesn't boot from HD)
- Resolves: bz#1632622
  (~40% virtio_blk disk performance drop for win2012r2 guest when comparing qemu-kvm-rhev-2.12.0-9 with qemu-kvm-rhev-2.12.0-12)
- Resolves: bz#1633928
  (CVE-2018-3639 qemu-kvm: hw: cpu: speculative store bypass [rhel-8.0])

* Fri Oct 12 2018 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 2.12.0-37.el8
- kvm-block-for-jobs-do-not-clear-user_paused-until-after-.patch [bz#1635583]
- kvm-iotests-Add-failure-matching-to-common.qemu.patch [bz#1635583]
- kvm-block-iotest-to-catch-abort-on-forced-blockjob-cance.patch [bz#1635583]
- Resolves: bz#1635583
  (Quitting VM causes qemu core dump once the block mirror job paused for no enough target space)

* Fri Oct 12 2018 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - qemu-kvm-2.12.0-36
- kvm-check-Only-test-ivshm-when-it-is-compiled-in.patch [bz#1621817]
- kvm-Disable-ivshmem.patch [bz#1621817]
- kvm-mirror-Fail-gracefully-for-source-target.patch [bz#1637963]
- kvm-commit-Add-top-node-base-node-options.patch [bz#1637970]
- kvm-qemu-iotests-Test-commit-with-top-node-base-node.patch [bz#1637970]
- Resolves: bz#1621817
  (Disable IVSHMEM in RHEL 8)
- Resolves: bz#1637963
  (Segfault on 'blockdev-mirror' with same node as source and target)
- Resolves: bz#1637970
  (allow using node-names with block-commit)

* Thu Oct 11 2018 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 2.12.0-35.el8
- kvm-redhat-make-the-plugins-executable.patch [bz#1638304]
- Resolves: bz#1638304
  (the driver packages lack all the library Requires)

* Thu Oct 11 2018 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 2.12.0-34.el8
- kvm-seccomp-allow-sched_setscheduler-with-SCHED_IDLE-pol.patch [bz#1618356]
- kvm-seccomp-use-SIGSYS-signal-instead-of-killing-the-thr.patch [bz#1618356]
- kvm-seccomp-prefer-SCMP_ACT_KILL_PROCESS-if-available.patch [bz#1618356]
- kvm-configure-require-libseccomp-2.2.0.patch [bz#1618356]
- kvm-seccomp-set-the-seccomp-filter-to-all-threads.patch [bz#1618356]
- kvm-memory-cleanup-side-effects-of-memory_region_init_fo.patch [bz#1600365]
- Resolves: bz#1600365
  (QEMU core dumped when hotplug memory exceeding host hugepages and with discard-data=yes)
- Resolves: bz#1618356
  (qemu-kvm: Qemu: seccomp: blacklist is not applied to all threads [rhel-8])

* Fri Oct 05 2018 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 2.12.0-33.el8
- kvm-migration-postcopy-Clear-have_listen_thread.patch [bz#1608765]
- kvm-migration-cleanup-in-error-paths-in-loadvm.patch [bz#1608765]
- kvm-jobs-change-start-callback-to-run-callback.patch [bz#1632939]
- kvm-jobs-canonize-Error-object.patch [bz#1632939]
- kvm-jobs-add-exit-shim.patch [bz#1632939]
- kvm-block-commit-utilize-job_exit-shim.patch [bz#1632939]
- kvm-block-mirror-utilize-job_exit-shim.patch [bz#1632939]
- kvm-jobs-utilize-job_exit-shim.patch [bz#1632939]
- kvm-block-backup-make-function-variables-consistently-na.patch [bz#1632939]
- kvm-jobs-remove-ret-argument-to-job_completed-privatize-.patch [bz#1632939]
- kvm-jobs-remove-job_defer_to_main_loop.patch [bz#1632939]
- kvm-block-commit-add-block-job-creation-flags.patch [bz#1632939]
- kvm-block-mirror-add-block-job-creation-flags.patch [bz#1632939]
- kvm-block-stream-add-block-job-creation-flags.patch [bz#1632939]
- kvm-block-commit-refactor-commit-to-use-job-callbacks.patch [bz#1632939]
- kvm-block-mirror-don-t-install-backing-chain-on-abort.patch [bz#1632939]
- kvm-block-mirror-conservative-mirror_exit-refactor.patch [bz#1632939]
- kvm-block-stream-refactor-stream-to-use-job-callbacks.patch [bz#1632939]
- kvm-tests-blockjob-replace-Blockjob-with-Job.patch [bz#1632939]
- kvm-tests-test-blockjob-remove-exit-callback.patch [bz#1632939]
- kvm-tests-test-blockjob-txn-move-.exit-to-.clean.patch [bz#1632939]
- kvm-jobs-remove-.exit-callback.patch [bz#1632939]
- kvm-qapi-block-commit-expose-new-job-properties.patch [bz#1632939]
- kvm-qapi-block-mirror-expose-new-job-properties.patch [bz#1632939]
- kvm-qapi-block-stream-expose-new-job-properties.patch [bz#1632939]
- kvm-block-backup-qapi-documentation-fixup.patch [bz#1632939]
- kvm-blockdev-document-transactional-shortcomings.patch [bz#1632939]
- Resolves: bz#1608765
  (After postcopy migration,  do savevm and loadvm, guest hang and call trace)
- Resolves: bz#1632939
  (qemu blockjobs other than backup do not support job-finalize or job-dismiss)

* Fri Sep 28 2018 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 2.12.0-32.el8
- kvm-Re-enable-disabled-Hyper-V-enlightenments.patch [bz#1625185]
- kvm-Fix-annocheck-issues.patch [bz#1624164]
- kvm-exec-check-that-alignment-is-a-power-of-two.patch [bz#1630746]
- kvm-curl-Make-sslverify-off-disable-host-as-well-as-peer.patch [bz#1575925]
- Resolves: bz#1575925
  ("SSL: no alternative certificate subject name matches target host name" error even though sslverify = off)
- Resolves: bz#1624164
  (Review annocheck distro flag failures in qemu-kvm)
- Resolves: bz#1625185
  (Re-enable disabled Hyper-V enlightenments)
- Resolves: bz#1630746
  (qemu_ram_mmap: Assertion `is_power_of_2(align)' failed)

* Tue Sep 11 2018 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 2.12.0-31.el8
- kvm-i386-Disable-TOPOEXT-by-default-on-cpu-host.patch [bz#1619804]
- kvm-redhat-enable-opengl-add-build-and-runtime-deps.patch [bz#1618412]
- Resolves: bz#1618412
  (Enable opengl (for intel vgpu display))
- Resolves: bz#1619804
  (kernel panic in init_amd_cacheinfo)

* Wed Sep 05 2018 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 2.12.0-30.el8
- kvm-redhat-Disable-vhost-crypto.patch [bz#1625668]
- Resolves: bz#1625668
  (Decide if we should disable 'vhost-crypto' or not)

* Wed Sep 05 2018 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 2.12.0-29.el8
- kvm-target-i386-sev-fix-memory-leaks.patch [bz#1615717]
- kvm-i386-Fix-arch_query_cpu_model_expansion-leak.patch [bz#1615717]
- kvm-redhat-Update-build-configuration.patch [bz#1573156]
- Resolves: bz#1573156
  (Update build configure for QEMU 2.12.0)
- Resolves: bz#1615717
  (Memory leaks)

* Wed Aug 29 2018 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 2.12.0-27.el8
- kvm-Fix-libusb-1.0.22-deprecated-libusb_set_debug-with-l.patch [bz#1622656]
- Resolves: bz#1622656
  (qemu-kvm fails to build due to libusb_set_debug being deprecated)

* Fri Aug 17 2018 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 2.12.0-26.el8
- kvm-redhat-remove-extra-in-rhel_rhev_conflicts-macro.patch [bz#1618752]
- Resolves: bz#1618752
  (qemu-kvm can't be installed in RHEL-8 as it Conflicts with itself.)

* Thu Aug 16 2018 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 2.12.0-25.el8
- kvm-Migration-TLS-Fix-crash-due-to-double-cleanup.patch [bz#1594384]
- Resolves: bz#1594384
  (2.12 migration fixes)

* Tue Aug 14 2018 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 2.12.0-24.el8
- kvm-Add-qemu-keymap-to-qemu-kvm-common.patch [bz#1593117]
- Resolves: bz#1593117
  (add qemu-keymap utility)

* Fri Aug 10 2018 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 2.12.0-23.el8
- Fixing an issue with some old command in the spec file

* Fri Aug 10 2018 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 2.12.0-22.el8
- Fix an issue with the build_configure script.
- Resolves: bz#1425820
  (Improve QEMU packaging layout with modularization of the block layer)


* Fri Aug 10 2018 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 2.12.0-20.el8
- kvm-migration-stop-compressing-page-in-migration-thread.patch [bz#1594384]
- kvm-migration-stop-compression-to-allocate-and-free-memo.patch [bz#1594384]
- kvm-migration-stop-decompression-to-allocate-and-free-me.patch [bz#1594384]
- kvm-migration-detect-compression-and-decompression-error.patch [bz#1594384]
- kvm-migration-introduce-control_save_page.patch [bz#1594384]
- kvm-migration-move-some-code-to-ram_save_host_page.patch [bz#1594384]
- kvm-migration-move-calling-control_save_page-to-the-comm.patch [bz#1594384]
- kvm-migration-move-calling-save_zero_page-to-the-common-.patch [bz#1594384]
- kvm-migration-introduce-save_normal_page.patch [bz#1594384]
- kvm-migration-remove-ram_save_compressed_page.patch [bz#1594384]
- kvm-migration-block-dirty-bitmap-fix-memory-leak-in-dirt.patch [bz#1594384]
- kvm-migration-fix-saving-normal-page-even-if-it-s-been-c.patch [bz#1594384]
- kvm-migration-update-index-field-when-delete-or-qsort-RD.patch [bz#1594384]
- kvm-migration-introduce-decompress-error-check.patch [bz#1594384]
- kvm-migration-Don-t-activate-block-devices-if-using-S.patch [bz#1594384]
- kvm-migration-not-wait-RDMA_CM_EVENT_DISCONNECTED-event-.patch [bz#1594384]
- kvm-migration-block-dirty-bitmap-fix-dirty_bitmap_load.patch [bz#1594384]
- kvm-s390x-add-RHEL-7.6-machine-type-for-ccw.patch [bz#1595718]
- kvm-s390x-cpumodel-default-enable-bpb-and-ppa15-for-z196.patch [bz#1595718]
- kvm-linux-headers-asm-s390-kvm.h-header-sync.patch [bz#1612938]
- kvm-s390x-kvm-add-etoken-facility.patch [bz#1612938]
- Resolves: bz#1594384
  (2.12 migration fixes)
- Resolves: bz#1595718
  (Add ppa15/bpb to the default cpu model for z196 and higher in the 7.6 s390-ccw-virtio machine)
- Resolves: bz#1612938
  (Add etoken support to qemu-kvm for s390x KVM guests)

* Fri Aug 10 2018 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 2.12.0-18.el8
  Mass import from RHEL 7.6 qemu-kvm-rhev, including fixes to the following BZs:

- kvm-AArch64-Add-virt-rhel7.6-machine-type.patch [bz#1558723]
- kvm-cpus-Fix-event-order-on-resume-of-stopped-guest.patch [bz#1566153]
- kvm-qemu-img-Check-post-truncation-size.patch [bz#1523065]
- kvm-vga-catch-depth-0.patch [bz#1575541]
- kvm-Fix-x-hv-max-vps-compat-value-for-7.4-machine-type.patch [bz#1583959]
- kvm-ccid-card-passthru-fix-regression-in-realize.patch [bz#1584984]
- kvm-Use-4-MB-vram-for-cirrus.patch [bz#1542080]
- kvm-spapr_pci-Remove-unhelpful-pagesize-warning.patch [bz#1505664]
- kvm-rpm-Add-nvme-VFIO-driver-to-rw-whitelist.patch [bz#1416180]
- kvm-qobject-Use-qobject_to-instead-of-type-cast.patch [bz#1557995]
- kvm-qobject-Ensure-base-is-at-offset-0.patch [bz#1557995]
- kvm-qobject-use-a-QObjectBase_-struct.patch [bz#1557995]
- kvm-qobject-Replace-qobject_incref-QINCREF-qobject_decre.patch [bz#1557995]
- kvm-qobject-Modify-qobject_ref-to-return-obj.patch [bz#1557995]
- kvm-rbd-Drop-deprecated-drive-parameter-filename.patch [bz#1557995]
- kvm-iscsi-Drop-deprecated-drive-parameter-filename.patch [bz#1557995]
- kvm-block-Add-block-specific-QDict-header.patch [bz#1557995]
- kvm-qobject-Move-block-specific-qdict-code-to-block-qdic.patch [bz#1557995]
- kvm-block-Fix-blockdev-for-certain-non-string-scalars.patch [bz#1557995]
- kvm-block-Fix-drive-for-certain-non-string-scalars.patch [bz#1557995]
- kvm-block-Clean-up-a-misuse-of-qobject_to-in-.bdrv_co_cr.patch [bz#1557995]
- kvm-block-Factor-out-qobject_input_visitor_new_flat_conf.patch [bz#1557995]
- kvm-block-Make-remaining-uses-of-qobject-input-visitor-m.patch [bz#1557995]
- kvm-block-qdict-Simplify-qdict_flatten_qdict.patch [bz#1557995]
- kvm-block-qdict-Tweak-qdict_flatten_qdict-qdict_flatten_.patch [bz#1557995]
- kvm-block-qdict-Clean-up-qdict_crumple-a-bit.patch [bz#1557995]
- kvm-block-qdict-Simplify-qdict_is_list-some.patch [bz#1557995]
- kvm-check-block-qdict-Rename-qdict_flatten-s-variables-f.patch [bz#1557995]
- kvm-check-block-qdict-Cover-flattening-of-empty-lists-an.patch [bz#1557995]
- kvm-block-Fix-blockdev-blockdev-add-for-empty-objects-an.patch [bz#1557995]
- kvm-rbd-New-parameter-auth-client-required.patch [bz#1557995]
- kvm-rbd-New-parameter-key-secret.patch [bz#1557995]
- kvm-block-mirror-honor-ratelimit-again.patch [bz#1572856]
- kvm-block-mirror-Make-cancel-always-cancel-pre-READY.patch [bz#1572856]
- kvm-iotests-Add-test-for-cancelling-a-mirror-job.patch [bz#1572856]
- kvm-iotests-Split-214-off-of-122.patch [bz#1518738]
- kvm-block-Add-COR-filter-driver.patch [bz#1518738]
- kvm-block-BLK_PERM_WRITE-includes-._UNCHANGED.patch [bz#1518738]
- kvm-block-Add-BDRV_REQ_WRITE_UNCHANGED-flag.patch [bz#1518738]
- kvm-block-Set-BDRV_REQ_WRITE_UNCHANGED-for-COR-writes.patch [bz#1518738]
- kvm-block-quorum-Support-BDRV_REQ_WRITE_UNCHANGED.patch [bz#1518738]
- kvm-block-Support-BDRV_REQ_WRITE_UNCHANGED-in-filters.patch [bz#1518738]
- kvm-iotests-Clean-up-wrap-image-in-197.patch [bz#1518738]
- kvm-iotests-Copy-197-for-COR-filter-driver.patch [bz#1518738]
- kvm-iotests-Add-test-for-COR-across-nodes.patch [bz#1518738]
- kvm-qemu-io-Use-purely-string-blockdev-options.patch [bz#1576598]
- kvm-qemu-img-Use-only-string-options-in-img_open_opts.patch [bz#1576598]
- kvm-iotests-Add-test-for-U-force-share-conflicts.patch [bz#1576598]
- kvm-qemu-io-Drop-command-functions-return-values.patch [bz#1519617]
- kvm-qemu-io-Let-command-functions-return-error-code.patch [bz#1519617]
- kvm-qemu-io-Exit-with-error-when-a-command-failed.patch [bz#1519617]
- kvm-iotests.py-Add-qemu_io_silent.patch [bz#1519617]
- kvm-iotests-Let-216-make-use-of-qemu-io-s-exit-code.patch [bz#1519617]
- kvm-qcow2-Repair-OFLAG_COPIED-when-fixing-leaks.patch [bz#1527085]
- kvm-iotests-Repairing-error-during-snapshot-deletion.patch [bz#1527085]
- kvm-block-Make-bdrv_is_writable-public.patch [bz#1588039]
- kvm-qcow2-Do-not-mark-inactive-images-corrupt.patch [bz#1588039]
- kvm-iotests-Add-case-for-a-corrupted-inactive-image.patch [bz#1588039]
- kvm-main-loop-drop-spin_counter.patch [bz#1168213]
- kvm-target-ppc-Factor-out-the-parsing-in-kvmppc_get_cpu_.patch [bz#1560847]
- kvm-target-ppc-Don-t-require-private-l1d-cache-on-POWER8.patch [bz#1560847]
- kvm-ppc-spapr_caps-Don-t-disable-cap_cfpc-on-POWER8-by-d.patch [bz#1560847]
- kvm-qxl-fix-local-renderer-crash.patch [bz#1567733]
- kvm-qemu-img-Amendment-support-implies-create_opts.patch [bz#1537956]
- kvm-block-Add-Error-parameter-to-bdrv_amend_options.patch [bz#1537956]
- kvm-qemu-option-Pull-out-Supported-options-print.patch [bz#1537956]
- kvm-qemu-img-Add-print_amend_option_help.patch [bz#1537956]
- kvm-qemu-img-Recognize-no-creation-support-in-o-help.patch [bz#1537956]
- kvm-iotests-Test-help-option-for-unsupporting-formats.patch [bz#1537956]
- kvm-iotests-Rework-113.patch [bz#1537956]
- kvm-qemu-img-Resolve-relative-backing-paths-in-rebase.patch [bz#1569835]
- kvm-iotests-Add-test-for-rebasing-with-relative-paths.patch [bz#1569835]
- kvm-qemu-img-Special-post-backing-convert-handling.patch [bz#1527898]
- kvm-iotests-Test-post-backing-convert-target-behavior.patch [bz#1527898]
- kvm-migration-calculate-expected_downtime-with-ram_bytes.patch [bz#1564576]
- kvm-sheepdog-Fix-sd_co_create_opts-memory-leaks.patch [bz#1513543]
- kvm-qemu-iotests-reduce-chance-of-races-in-185.patch [bz#1513543]
- kvm-blockjob-do-not-cancel-timer-in-resume.patch [bz#1513543]
- kvm-nfs-Fix-error-path-in-nfs_options_qdict_to_qapi.patch [bz#1513543]
- kvm-nfs-Remove-processed-options-from-QDict.patch [bz#1513543]
- kvm-blockjob-drop-block_job_pause-resume_all.patch [bz#1513543]
- kvm-blockjob-expose-error-string-via-query.patch [bz#1513543]
- kvm-blockjob-Fix-assertion-in-block_job_finalize.patch [bz#1513543]
- kvm-blockjob-Wrappers-for-progress-counter-access.patch [bz#1513543]
- kvm-blockjob-Move-RateLimit-to-BlockJob.patch [bz#1513543]
- kvm-blockjob-Implement-block_job_set_speed-centrally.patch [bz#1513543]
- kvm-blockjob-Introduce-block_job_ratelimit_get_delay.patch [bz#1513543]
- kvm-blockjob-Add-block_job_driver.patch [bz#1513543]
- kvm-blockjob-Update-block-job-pause-resume-documentation.patch [bz#1513543]
- kvm-blockjob-Improve-BlockJobInfo.offset-len-documentati.patch [bz#1513543]
- kvm-job-Create-Job-JobDriver-and-job_create.patch [bz#1513543]
- kvm-job-Rename-BlockJobType-into-JobType.patch [bz#1513543]
- kvm-job-Add-JobDriver.job_type.patch [bz#1513543]
- kvm-job-Add-job_delete.patch [bz#1513543]
- kvm-job-Maintain-a-list-of-all-jobs.patch [bz#1513543]
- kvm-job-Move-state-transitions-to-Job.patch [bz#1513543]
- kvm-job-Add-reference-counting.patch [bz#1513543]
- kvm-job-Move-cancelled-to-Job.patch [bz#1513543]
- kvm-job-Add-Job.aio_context.patch [bz#1513543]
- kvm-job-Move-defer_to_main_loop-to-Job.patch [bz#1513543]
- kvm-job-Move-coroutine-and-related-code-to-Job.patch [bz#1513543]
- kvm-job-Add-job_sleep_ns.patch [bz#1513543]
- kvm-job-Move-pause-resume-functions-to-Job.patch [bz#1513543]
- kvm-job-Replace-BlockJob.completed-with-job_is_completed.patch [bz#1513543]
- kvm-job-Move-BlockJobCreateFlags-to-Job.patch [bz#1513543]
- kvm-blockjob-Split-block_job_event_pending.patch [bz#1513543]
- kvm-job-Add-job_event_.patch [bz#1513543]
- kvm-job-Move-single-job-finalisation-to-Job.patch [bz#1513543]
- kvm-job-Convert-block_job_cancel_async-to-Job.patch [bz#1513543]
- kvm-job-Add-job_drain.patch [bz#1513543]
- kvm-job-Move-.complete-callback-to-Job.patch [bz#1513543]
- kvm-job-Move-job_finish_sync-to-Job.patch [bz#1513543]
- kvm-job-Switch-transactions-to-JobTxn.patch [bz#1513543]
- kvm-job-Move-transactions-to-Job.patch [bz#1513543]
- kvm-job-Move-completion-and-cancellation-to-Job.patch [bz#1513543]
- kvm-block-Cancel-job-in-bdrv_close_all-callers.patch [bz#1513543]
- kvm-job-Add-job_yield.patch [bz#1513543]
- kvm-job-Add-job_dismiss.patch [bz#1513543]
- kvm-job-Add-job_is_ready.patch [bz#1513543]
- kvm-job-Add-job_transition_to_ready.patch [bz#1513543]
- kvm-job-Move-progress-fields-to-Job.patch [bz#1513543]
- kvm-job-Introduce-qapi-job.json.patch [bz#1513543]
- kvm-job-Add-JOB_STATUS_CHANGE-QMP-event.patch [bz#1513543]
- kvm-job-Add-lifecycle-QMP-commands.patch [bz#1513543]
- kvm-job-Add-query-jobs-QMP-command.patch [bz#1513543]
- kvm-blockjob-Remove-BlockJob.driver.patch [bz#1513543]
- kvm-iotests-Move-qmp_to_opts-to-VM.patch [bz#1513543]
- kvm-qemu-iotests-Test-job-with-block-jobs.patch [bz#1513543]
- kvm-vdi-Fix-vdi_co_do_create-return-value.patch [bz#1513543]
- kvm-vhdx-Fix-vhdx_co_create-return-value.patch [bz#1513543]
- kvm-job-Add-error-message-for-failing-jobs.patch [bz#1513543]
- kvm-block-create-Make-x-blockdev-create-a-job.patch [bz#1513543]
- kvm-qemu-iotests-Add-VM.get_qmp_events_filtered.patch [bz#1513543]
- kvm-qemu-iotests-Add-VM.qmp_log.patch [bz#1513543]
- kvm-qemu-iotests-Add-iotests.img_info_log.patch [bz#1513543]
- kvm-qemu-iotests-Add-VM.run_job.patch [bz#1513543]
- kvm-qemu-iotests-iotests.py-helper-for-non-file-protocol.patch [bz#1513543]
- kvm-qemu-iotests-Rewrite-206-for-blockdev-create-job.patch [bz#1513543]
- kvm-qemu-iotests-Rewrite-207-for-blockdev-create-job.patch [bz#1513543]
- kvm-qemu-iotests-Rewrite-210-for-blockdev-create-job.patch [bz#1513543]
- kvm-qemu-iotests-Rewrite-211-for-blockdev-create-job.patch [bz#1513543]
- kvm-qemu-iotests-Rewrite-212-for-blockdev-create-job.patch [bz#1513543]
- kvm-qemu-iotests-Rewrite-213-for-blockdev-create-job.patch [bz#1513543]
- kvm-block-create-Mark-blockdev-create-stable.patch [bz#1513543]
- kvm-jobs-fix-stale-wording.patch [bz#1513543]
- kvm-jobs-fix-verb-references-in-docs.patch [bz#1513543]
- kvm-iotests-Fix-219-s-timing.patch [bz#1513543]
- kvm-iotests-improve-pause_job.patch [bz#1513543]
- kvm-rpm-Whitelist-copy-on-read-block-driver.patch [bz#1518738]
- kvm-rpm-add-throttle-driver-to-rw-whitelist.patch [bz#1591076]
- kvm-usb-host-skip-open-on-pending-postload-bh.patch [bz#1572851]
- kvm-i386-Define-the-Virt-SSBD-MSR-and-handling-of-it-CVE.patch [bz#1574216]
- kvm-i386-define-the-AMD-virt-ssbd-CPUID-feature-bit-CVE-.patch [bz#1574216]
- kvm-block-file-posix-Pass-FD-to-locking-helpers.patch [bz#1519144]
- kvm-block-file-posix-File-locking-during-creation.patch [bz#1519144]
- kvm-iotests-Add-creation-test-to-153.patch [bz#1519144]
- kvm-vhost-user-add-Net-prefix-to-internal-state-structur.patch [bz#1526645]
- kvm-virtio-support-setting-memory-region-based-host-noti.patch [bz#1526645]
- kvm-vhost-user-support-receiving-file-descriptors-in-sla.patch [bz#1526645]
- kvm-osdep-add-wait.h-compat-macros.patch [bz#1526645]
- kvm-vhost-user-bridge-support-host-notifier.patch [bz#1526645]
- kvm-vhost-allow-backends-to-filter-memory-sections.patch [bz#1526645]
- kvm-vhost-user-allow-slave-to-send-fds-via-slave-channel.patch [bz#1526645]
- kvm-vhost-user-introduce-shared-vhost-user-state.patch [bz#1526645]
- kvm-vhost-user-support-registering-external-host-notifie.patch [bz#1526645]
- kvm-libvhost-user-support-host-notifier.patch [bz#1526645]
- kvm-block-Introduce-API-for-copy-offloading.patch [bz#1482537]
- kvm-raw-Check-byte-range-uniformly.patch [bz#1482537]
- kvm-raw-Implement-copy-offloading.patch [bz#1482537]
- kvm-qcow2-Implement-copy-offloading.patch [bz#1482537]
- kvm-file-posix-Implement-bdrv_co_copy_range.patch [bz#1482537]
- kvm-iscsi-Query-and-save-device-designator-when-opening.patch [bz#1482537]
- kvm-iscsi-Create-and-use-iscsi_co_wait_for_task.patch [bz#1482537]
- kvm-iscsi-Implement-copy-offloading.patch [bz#1482537]
- kvm-block-backend-Add-blk_co_copy_range.patch [bz#1482537]
- kvm-qemu-img-Convert-with-copy-offloading.patch [bz#1482537]
- kvm-qcow2-Fix-src_offset-in-copy-offloading.patch [bz#1482537]
- kvm-iscsi-Don-t-blindly-use-designator-length-in-respons.patch [bz#1482537]
- kvm-file-posix-Fix-EINTR-handling.patch [bz#1482537]
- kvm-usb-storage-Add-rerror-werror-properties.patch [bz#1595180]
- kvm-numa-clarify-error-message-when-node-index-is-out-of.patch [bz#1578381]
- kvm-qemu-iotests-Update-026.out.nocache-reference-output.patch [bz#1528541]
- kvm-qcow2-Free-allocated-clusters-on-write-error.patch [bz#1528541]
- kvm-qemu-iotests-Test-qcow2-not-leaking-clusters-on-writ.patch [bz#1528541]
- kvm-qemu-options-Add-missing-newline-to-accel-help-text.patch [bz#1586313]
- kvm-xhci-fix-guest-triggerable-assert.patch [bz#1594135]
- kvm-virtio-gpu-tweak-scanout-disable.patch [bz#1589634]
- kvm-virtio-gpu-update-old-resource-too.patch [bz#1589634]
- kvm-virtio-gpu-disable-scanout-when-backing-resource-is-.patch [bz#1589634]
- kvm-block-Don-t-silently-truncate-node-names.patch [bz#1549654]
- kvm-pr-helper-fix-socket-path-default-in-help.patch [bz#1533158]
- kvm-pr-helper-fix-assertion-failure-on-failed-multipath-.patch [bz#1533158]
- kvm-pr-manager-helper-avoid-SIGSEGV-when-writing-to-the-.patch [bz#1533158]
- kvm-pr-manager-put-stubs-in-.c-file.patch [bz#1533158]
- kvm-pr-manager-add-query-pr-managers-QMP-command.patch [bz#1533158]
- kvm-pr-manager-helper-report-event-on-connection-disconn.patch [bz#1533158]
- kvm-pr-helper-avoid-error-on-PR-IN-command-with-zero-req.patch [bz#1533158]
- kvm-pr-helper-Rework-socket-path-handling.patch [bz#1533158]
- kvm-pr-manager-helper-fix-memory-leak-on-event.patch [bz#1533158]
- kvm-object-fix-OBJ_PROP_LINK_UNREF_ON_RELEASE-ambivalenc.patch [bz#1556678]
- kvm-usb-hcd-xhci-test-add-a-test-for-ccid-hotplug.patch [bz#1556678]
- kvm-Revert-usb-release-the-created-buses.patch [bz#1556678]
- kvm-file-posix-Fix-creation-locking.patch [bz#1599335]
- kvm-file-posix-Unlock-FD-after-creation.patch [bz#1599335]
- kvm-ahci-trim-signatures-on-raise-lower.patch [bz#1584914]
- kvm-ahci-fix-PxCI-register-race.patch [bz#1584914]
- kvm-ahci-don-t-schedule-unnecessary-BH.patch [bz#1584914]
- kvm-qcow2-Fix-qcow2_truncate-error-return-value.patch [bz#1595173]
- kvm-block-Convert-.bdrv_truncate-callback-to-coroutine_f.patch [bz#1595173]
- kvm-qcow2-Remove-coroutine-trampoline-for-preallocate_co.patch [bz#1595173]
- kvm-block-Move-bdrv_truncate-implementation-to-io.c.patch [bz#1595173]
- kvm-block-Use-tracked-request-for-truncate.patch [bz#1595173]
- kvm-file-posix-Make-.bdrv_co_truncate-asynchronous.patch [bz#1595173]
- kvm-block-Fix-copy-on-read-crash-with-partial-final-clus.patch [bz#1590640]
- kvm-block-fix-QEMU-crash-with-scsi-hd-and-drive_del.patch [bz#1599515]
- kvm-virtio-rng-process-pending-requests-on-DRIVER_OK.patch [bz#1576743]
- kvm-file-posix-specify-expected-filetypes.patch [bz#1525829]
- kvm-iotests-add-test-226-for-file-driver-types.patch [bz#1525829]
- kvm-block-dirty-bitmap-add-lock-to-bdrv_enable-disable_d.patch [bz#1207657]
- kvm-qapi-add-x-block-dirty-bitmap-enable-disable.patch [bz#1207657]
- kvm-qmp-transaction-support-for-x-block-dirty-bitmap-ena.patch [bz#1207657]
- kvm-qapi-add-x-block-dirty-bitmap-merge.patch [bz#1207657]
- kvm-qapi-add-disabled-parameter-to-block-dirty-bitmap-ad.patch [bz#1207657]
- kvm-block-dirty-bitmap-add-bdrv_enable_dirty_bitmap_lock.patch [bz#1207657]
- kvm-dirty-bitmap-fix-double-lock-on-bitmap-enabling.patch [bz#1207657]
- kvm-block-qcow2-bitmap-fix-free_bitmap_clusters.patch [bz#1207657]
- kvm-qcow2-add-overlap-check-for-bitmap-directory.patch [bz#1207657]
- kvm-blockdev-enable-non-root-nodes-for-backup-source.patch [bz#1207657]
- kvm-iotests-add-222-to-test-basic-fleecing.patch [bz#1207657]
- kvm-qcow2-Remove-dead-check-on-ret.patch [bz#1207657]
- kvm-block-Move-request-tracking-to-children-in-copy-offl.patch [bz#1207657]
- kvm-block-Fix-parameter-checking-in-bdrv_co_copy_range_i.patch [bz#1207657]
- kvm-block-Honour-BDRV_REQ_NO_SERIALISING-in-copy-range.patch [bz#1207657]
- kvm-backup-Use-copy-offloading.patch [bz#1207657]
- kvm-block-backup-disable-copy-offloading-for-backup.patch [bz#1207657]
- kvm-iotests-222-Don-t-run-with-luks.patch [bz#1207657]
- kvm-block-io-fix-copy_range.patch [bz#1207657]
- kvm-block-split-flags-in-copy_range.patch [bz#1207657]
- kvm-block-add-BDRV_REQ_SERIALISING-flag.patch [bz#1207657]
- kvm-block-backup-fix-fleecing-scheme-use-serialized-writ.patch [bz#1207657]
- kvm-nbd-server-Reject-0-length-block-status-request.patch [bz#1207657]
- kvm-nbd-server-fix-trace.patch [bz#1207657]
- kvm-nbd-server-refactor-NBDExportMetaContexts.patch [bz#1207657]
- kvm-nbd-server-add-nbd_meta_empty_or_pattern-helper.patch [bz#1207657]
- kvm-nbd-server-implement-dirty-bitmap-export.patch [bz#1207657]
- kvm-qapi-new-qmp-command-nbd-server-add-bitmap.patch [bz#1207657]
- kvm-docs-interop-add-nbd.txt.patch [bz#1207657]
- kvm-nbd-server-introduce-NBD_CMD_CACHE.patch [bz#1207657]
- kvm-nbd-server-Silence-gcc-false-positive.patch [bz#1207657]
- kvm-nbd-server-Fix-dirty-bitmap-logic-regression.patch [bz#1207657]
- kvm-nbd-server-fix-nbd_co_send_block_status.patch [bz#1207657]
- kvm-nbd-client-Add-x-dirty-bitmap-to-query-bitmap-from-s.patch [bz#1207657]
- kvm-iotests-New-test-223-for-exporting-dirty-bitmap-over.patch [bz#1207657]
- kvm-hw-char-serial-Only-retry-if-qemu_chr_fe_write-retur.patch [bz#1592817]
- kvm-hw-char-serial-retry-write-if-EAGAIN.patch [bz#1592817]
- kvm-throttle-groups-fix-hang-when-group-member-leaves.patch [bz#1535914]
- kvm-Disable-aarch64-devices-reappeared-after-2.12-rebase.patch [bz#1586357]
- kvm-Disable-split-irq-device.patch [bz#1586357]
- kvm-Disable-AT24Cx-i2c-eeprom.patch [bz#1586357]
- kvm-Disable-CAN-bus-devices.patch [bz#1586357]
- kvm-Disable-new-superio-devices.patch [bz#1586357]
- kvm-Disable-new-pvrdma-device.patch [bz#1586357]
- kvm-qdev-add-HotplugHandler-post_plug-callback.patch [bz#1607891]
- kvm-virtio-scsi-fix-hotplug-reset-vs-event-race.patch [bz#1607891]
- kvm-e1000-Fix-tso_props-compat-for-82540em.patch [bz#1608778]
- kvm-slirp-correct-size-computation-while-concatenating-m.patch [bz#1586255]
- kvm-s390x-sclp-fix-maxram-calculation.patch [bz#1595740]
- kvm-redhat-Make-gitpublish-profile-the-default-one.patch [bz#1425820]
- Resolves: bz#1168213
  (main-loop: WARNING: I/O thread spun for 1000 iterations while doing stream block device.)
- Resolves: bz#1207657
  (RFE: QEMU Incremental live backup - push and pull modes)
- Resolves: bz#1416180
  (QEMU VFIO based block driver for NVMe devices)
- Resolves: bz#1425820
  (Improve QEMU packaging layout with modularization of the block layer)
- Resolves: bz#1482537
  ([RFE] qemu-img copy-offloading (convert command))
- Resolves: bz#1505664
  ("qemu-kvm: System page size 0x1000000 is not enabled in page_size_mask (0x11000). Performance may be slow" show up while using hugepage as guest's memory)
- Resolves: bz#1513543
  ([RFE] Add block job to create format on a storage device)
- Resolves: bz#1518738
  (Add 'copy-on-read' filter driver for use with blockdev-add)
- Resolves: bz#1519144
  (qemu-img: image locking doesn't cover image creation)
- Resolves: bz#1519617
  (The exit code should be non-zero when qemu-io reports an error)
- Resolves: bz#1523065
  ("qemu-img resize" should fail to decrease the size of logical partition/lvm/iSCSI image with raw format)
- Resolves: bz#1525829
  (can not boot up a scsi-block passthrough disk via -blockdev with error "cannot get SG_IO version number: Operation not supported.  Is this a SCSI device?")
- Resolves: bz#1526645
  ([Intel 7.6 FEAT] vHost Data Plane Acceleration (vDPA) - vhost user client - qemu-kvm-rhev)
- Resolves: bz#1527085
  (The copied flag should be updated during  '-r leaks')
- Resolves: bz#1527898
  ([RFE] qemu-img should leave cluster unallocated if it's read as zero throughout the backing chain)
- Resolves: bz#1528541
  (qemu-img check reports tons of leaked clusters after re-start nfs service to resume writing data in guest)
- Resolves: bz#1533158
  (QEMU support for libvirtd restarting qemu-pr-helper)
- Resolves: bz#1535914
  (Disable io throttling for one member disk of a group during io will induce the other one hang with io)
- Resolves: bz#1537956
  (RFE: qemu-img amend should list the true supported options)
- Resolves: bz#1542080
  (Qemu core dump at cirrus_invalidate_region)
- Resolves: bz#1549654
  (Reject node-names which would be truncated by the block layer commands)
- Resolves: bz#1556678
  (Hot plug usb-ccid for the 2nd time with the same ID as the 1st time failed)
- Resolves: bz#1557995
  (QAPI schema for RBD storage misses the 'password-secret' option)
- Resolves: bz#1558723
  (Create RHEL-7.6 QEMU machine type for AArch64)
- Resolves: bz#1560847
  ([Power8][FW b0320a_1812.861][rhel7.5rc2 3.10.0-861.el7.ppc64le][qemu-kvm-{ma,rhev}-2.10.0-21.el7_5.1.ppc64le] KVM guest does not default to ori type flush even with pseries-rhel7.5.0-sxxm)
- Resolves: bz#1564576
  (Pegas 1.1 - Require to backport qemu-kvm patch that fixes expected_downtime calculation during migration)
- Resolves: bz#1566153
  (IOERROR pause code lost after resuming a VM while I/O error is still present)
- Resolves: bz#1567733
  (qemu abort when migrate during guest reboot)
- Resolves: bz#1569835
  (qemu-img get wrong backing file path after rebasing image with relative path)
- Resolves: bz#1572851
  (Core dumped after migration when with usb-host)
- Resolves: bz#1572856
  ('block-job-cancel' can not cancel a "drive-mirror" job)
- Resolves: bz#1574216
  (CVE-2018-3639 qemu-kvm-rhev: hw: cpu: speculative store bypass [rhel-7.6])
- Resolves: bz#1575541
  (qemu core dump while installing win10 guest)
- Resolves: bz#1576598
  (Segfault in qemu-io and qemu-img with -U --image-opts force-share=off)
- Resolves: bz#1576743
  (virtio-rng hangs when running on recent (2.x) QEMU versions)
- Resolves: bz#1578381
  (Error message need update when specify numa distance with node index >=128)
- Resolves: bz#1583959
  (Incorrect vcpu count limit for 7.4 machine types for windows guests)
- Resolves: bz#1584914
  (SATA emulator lags and hangs)
- Resolves: bz#1584984
  (Vm starts failed with 'passthrough' smartcard)
- Resolves: bz#1586255
  (CVE-2018-11806 qemu-kvm-rhev: QEMU: slirp: heap buffer overflow while reassembling fragmented datagrams [rhel-7.6])
- Resolves: bz#1586313
  (-smp option is not easily found in the output of qemu help)
- Resolves: bz#1586357
  (Disable new devices in 2.12)
- Resolves: bz#1588039
  (Possible assertion failure in qemu when a corrupted image is used during an incoming migration)
- Resolves: bz#1589634
  (Migration failed when rebooting guest with multiple virtio videos)
- Resolves: bz#1590640
  (qemu-kvm: block/io.c:1098: bdrv_co_do_copy_on_readv: Assertion `skip_bytes < pnum' failed.)
- Resolves: bz#1591076
  (The driver of 'throttle' is not whitelisted)
- Resolves: bz#1592817
  (Retrying on serial_xmit if the pipe is broken may compromise the Guest)
- Resolves: bz#1594135
  (system_reset many times linux guests cause qemu process Aborted)
- Resolves: bz#1595173
  (blockdev-create is blocking)
- Resolves: bz#1595180
  (Can't set rerror/werror with usb-storage)
- Resolves: bz#1595740
  (RHEL-Alt-7.6 - qemu has error during migration of larger guests)
- Resolves: bz#1599335
  (Image creation locking is too tight and is not properly released)
- Resolves: bz#1599515
  (qemu core-dump with aio_read via hmp (util/qemu-thread-posix.c:64: qemu_mutex_lock_impl: Assertion `mutex->initialized' failed))
- Resolves: bz#1607891
  (Hotplug events are sometimes lost with virtio-scsi + iothread)
- Resolves: bz#1608778
  (qemu/migration: migrate failed from RHEL.7.6 to RHEL.7.5 with e1000-82540em)

* Mon Aug 06 2018 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 2.12.0-17.el8
- kvm-linux-headers-Update-to-include-KVM_CAP_S390_HPAGE_1.patch [bz#1610906]
- kvm-s390x-Enable-KVM-huge-page-backing-support.patch [bz#1610906]
- kvm-redhat-s390x-add-hpage-1-to-kvm.conf.patch [bz#1610906]
- Resolves: bz#1610906
  ([IBM 8.0 FEAT] KVM: Huge Pages - libhugetlbfs Enablement - qemu-kvm part)

* Tue Jul 31 2018 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 2.12.0-16.el8
- kvm-spapr-Correct-inverted-test-in-spapr_pc_dimm_node.patch [bz#1601671]
- kvm-osdep-powerpc64-align-memory-to-allow-2MB-radix-THP-.patch [bz#1601317]
- kvm-RHEL-8.0-Add-pseries-rhel7.6.0-sxxm-machine-type.patch [bz#1595501]
- kvm-i386-Helpers-to-encode-cache-information-consistentl.patch [bz#1597739]
- kvm-i386-Add-cache-information-in-X86CPUDefinition.patch [bz#1597739]
- kvm-i386-Initialize-cache-information-for-EPYC-family-pr.patch [bz#1597739]
- kvm-i386-Add-new-property-to-control-cache-info.patch [bz#1597739]
- kvm-i386-Clean-up-cache-CPUID-code.patch [bz#1597739]
- kvm-i386-Populate-AMD-Processor-Cache-Information-for-cp.patch [bz#1597739]
- kvm-i386-Add-support-for-CPUID_8000_001E-for-AMD.patch [bz#1597739]
- kvm-i386-Fix-up-the-Node-id-for-CPUID_8000_001E.patch [bz#1597739]
- kvm-i386-Enable-TOPOEXT-feature-on-AMD-EPYC-CPU.patch [bz#1597739]
- kvm-i386-Remove-generic-SMT-thread-check.patch [bz#1597739]
- kvm-i386-Allow-TOPOEXT-to-be-enabled-on-older-kernels.patch [bz#1597739]
- Resolves: bz#1595501
  (Create pseries-rhel7.6.0-sxxm machine type)
- Resolves: bz#1597739
  (AMD EPYC/Zen SMT support for KVM / QEMU guest (qemu-kvm))
- Resolves: bz#1601317
  (RHEL8.0 - qemu patch to align memory to allow 2MB THP)
- Resolves: bz#1601671
  (After rebooting guest,all the hot plug memory will be assigned to the 1st numa node.)

* Tue Jul 24 2018 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 2.12.0-15.el8
- kvm-spapr-Add-ibm-max-associativity-domains-property.patch [bz#1599593]
- kvm-Revert-spapr-Don-t-allow-memory-hotplug-to-memory-le.patch [bz#1599593]
- kvm-simpletrace-Convert-name-from-mapping-record-to-str.patch [bz#1594969]
- kvm-tests-fix-TLS-handshake-failure-with-TLS-1.3.patch [bz#1602403]
- Resolves: bz#1594969
  (simpletrace.py fails when running with Python 3)
- Resolves: bz#1599593
  (User can't hotplug memory to less memory numa node on rhel8)
- Resolves: bz#1602403
  (test-crypto-tlssession unit test fails with assertions)

* Mon Jul 09 2018 Danilo Cesar Lemes de Paula <ddepaula@redhat.com> - 2.12.0-14.el8
- kvm-vfio-pci-Default-display-option-to-off.patch [bz#1590511]
- kvm-python-futurize-f-libfuturize.fixes.fix_print_with_i.patch [bz#1571533]
- kvm-python-futurize-f-lib2to3.fixes.fix_except.patch [bz#1571533]
- kvm-Revert-Defining-a-shebang-for-python-scripts.patch [bz#1571533]
- kvm-spec-Fix-ambiguous-python-interpreter-name.patch [bz#1571533]
- kvm-qemu-ga-blacklisting-guest-exec-and-guest-exec-statu.patch [bz#1518132]
- kvm-redhat-rewrap-build_configure.sh-cmdline-for-the-rh-.patch []
- kvm-redhat-remove-the-VTD-LIVE_BLOCK_OPS-and-RHV-options.patch []
- kvm-redhat-fix-the-rh-env-prep-target-s-dependency-on-th.patch []
- kvm-redhat-remove-dead-code-related-to-s390-not-s390x.patch []
- kvm-redhat-sync-compiler-flags-from-the-spec-file-to-rh-.patch []
- kvm-redhat-sync-guest-agent-enablement-and-tcmalloc-usag.patch []
- kvm-redhat-fix-up-Python-3-dependency-for-building-QEMU.patch []
- kvm-redhat-fix-up-Python-dependency-for-SRPM-generation.patch []
- kvm-redhat-disable-glusterfs-dependency-support-temporar.patch []
- Resolves: bz#1518132
  (Ensure file access RPCs are disabled by default)
- Resolves: bz#1571533
  (Convert qemu-kvm python scripts to python3)
- Resolves: bz#1590511
  (Fails to start guest with Intel vGPU device)

* Thu Jun 21 2018 Danilo C. L. de Paula <ddepaula@redhat.com> - 2.12.0-13.el8
- Resolves: bz#1508137
  ([IBM 8.0 FEAT] KVM: Interactive Bootloader (qemu))
- Resolves: bz#1513558
  (Remove RHEL6 machine types)
- Resolves: bz#1568600
  (pc-i440fx-rhel7.6.0 and pc-q35-rhel7.6.0 machine types (x86))
- Resolves: bz#1570029
  ([IBM 8.0 FEAT] KVM: 3270 Connectivity - qemu part)
- Resolves: bz#1578855
  (Enable Native Ceph support on non x86_64 CPUs)
- Resolves: bz#1585651
  (RHEL 7.6 new pseries machine type (ppc64le))
- Resolves: bz#1592337
  ([IBM 8.0 FEAT] KVM: CPU Model z14 ZR1 (qemu-kvm))

* Tue May 15 2018 Danilo C. L. de Paula <ddepaula@redhat.com> - 2.12.0-11.el8.1
- Resolves: bz#1576468
  (Enable vhost_user in qemu-kvm 2.12)

* Wed May 09 2018 Danilo de Paula <ddepaula@redhat.com> - 2.12.0-11.el8
- Resolves: bz#1574406
  ([RHEL 8][qemu-kvm] Failed to find romfile "efi-virtio.rom")
- Resolves: bz#1569675
  (Backwards compatibility of pc-*-rhel7.5.0 and older machine-types)
- Resolves: bz#1576045
  (Fix build issue by using python3)
- Resolves: bz#1571145
  (qemu-kvm segfaults on RHEL 8 when run guestfsd under TCG)

* Fri Apr 20 2018 Danilo de Paula <ddepaula@redhat.com> - 2.12.0-10.el
- Fixing some issues with packaging.
- Rebasing to 2.12.0-rc4

* Fri Apr 13 2018 Danilo de Paula <ddepaula@redhat.com> - 2.11.0-7.el8
- Bumping epoch for RHEL8 and dropping self-obsoleting

* Thu Apr 12 2018 Danilo de Paula <ddepaula@redhat.com> - 2.11.0-6.el8
- Rebuilding

* Mon Mar 05 2018 Danilo de Paula <ddepaula@redhat.com> - 2.11.0-5.el8
- Prepare building on RHEL-8.0
