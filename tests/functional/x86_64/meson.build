# SPDX-License-Identifier: GPL-2.0-or-later

test_x86_64_timeouts = {
  'acpi_bits' : 420,
  'intel_iommu': 300,
  'kvm_xen' : 180,
  'netdev_ethtool' : 180,
  'replay' : 480,
  'virtio_balloon': 120,
}

tests_x86_64_system_quick = [
  'bad_vmstate',
  'cpu_model_versions',
  'cpu_queries',
  'mem_addr_space',
  'migration',
  'pc_cpu_hotplug_props',
  'virtio_version',
  'vmstate',
]

# The address-sanitizer makes mlock() a no-op because it
# interacts badly with the sanitizer; this means the memlock
# test (which checks via /proc for whether pages were locked)
# will always fail on a sanitizer build, so don't run it.
if not get_option('asan')
  tests_x86_64_system_quick += [ 'memlock' ]
endif

tests_x86_64_system_thorough = [
  'acpi_bits',
  'hotplug_blk',
  'hotplug_cpu',
  'intel_iommu',
  'kvm_xen',
  'linux_initrd',
  'multiprocess',
  'netdev_ethtool',
  'replay',
  'reverse_debug',
  'tuxrun',
  'vfio_user_client',
  'vhost_user_bridge',
  'virtio_balloon',
  'virtio_gpu',
  'rebuild_vmfd',
]
