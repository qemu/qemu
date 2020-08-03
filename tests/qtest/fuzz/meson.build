specific_fuzz_ss.add(files('fuzz.c', 'fork_fuzz.c', 'qos_fuzz.c',
                           'qtest_wrappers.c'), qos)

# Targets
specific_fuzz_ss.add(when: 'CONFIG_I440FX', if_true: files('i440fx_fuzz.c'))
specific_fuzz_ss.add(when: 'CONFIG_VIRTIO_NET', if_true: files('virtio_net_fuzz.c'))
specific_fuzz_ss.add(when: 'CONFIG_VIRTIO_SCSI', if_true: files('virtio_scsi_fuzz.c'))

# unfortunately declare_dependency does not support link_depends, so
# this will be duplicated in meson.build
fork_fuzz = declare_dependency(
  link_args: ['-fsanitize=fuzzer',
              '-Wl,-T,' + (meson.current_source_dir() / 'fork_fuzz.ld'),
              '-Wl,-wrap,qtest_inb',
              '-Wl,-wrap,qtest_inw',
              '-Wl,-wrap,qtest_inl',
              '-Wl,-wrap,qtest_outb',
              '-Wl,-wrap,qtest_outw',
              '-Wl,-wrap,qtest_outl',
              '-Wl,-wrap,qtest_readb',
              '-Wl,-wrap,qtest_readw',
              '-Wl,-wrap,qtest_readl',
              '-Wl,-wrap,qtest_readq',
              '-Wl,-wrap,qtest_writeb',
              '-Wl,-wrap,qtest_writew',
              '-Wl,-wrap,qtest_writel',
              '-Wl,-wrap,qtest_writeq',
              '-Wl,-wrap,qtest_memread',
              '-Wl,-wrap,qtest_bufread',
              '-Wl,-wrap,qtest_memwrite',
              '-Wl,-wrap,qtest_bufwrite',
              '-Wl,-wrap,qtest_memset']
)

specific_fuzz_ss.add(fork_fuzz)
