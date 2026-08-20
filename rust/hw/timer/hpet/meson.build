_libhpet_rs = cargo_ws.package('hpet').library()

rust_devices_ss.add(when: 'CONFIG_X_HPET_RUST', if_true: [declare_dependency(
  link_whole: [_libhpet_rs],
  variables: {'crate': 'hpet'},
)])
