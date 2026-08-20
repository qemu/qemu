_chardev_rs = cargo_ws.package('chardev').library()
cargo_ws.package('chardev').override_dependency(declare_dependency(link_with: _chardev_rs))

chardev_rs = declare_dependency(link_with: [_chardev_rs], dependencies: [chardev, qemuutil])
