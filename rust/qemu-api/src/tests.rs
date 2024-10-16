// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

use crate::{
    bindings::*, declare_properties, define_property, device_class_init, vm_state_description,
};

#[test]
fn test_device_decl_macros() {
    // Test that macros can compile.
    vm_state_description! {
        VMSTATE,
        name: c"name",
        unmigratable: true,
    }

    #[repr(C)]
    pub struct DummyState {
        pub char_backend: CharBackend,
        pub migrate_clock: bool,
    }

    declare_properties! {
        DUMMY_PROPERTIES,
            define_property!(
                c"chardev",
                DummyState,
                char_backend,
                unsafe { &qdev_prop_chr },
                CharBackend
            ),
            define_property!(
                c"migrate-clk",
                DummyState,
                migrate_clock,
                unsafe { &qdev_prop_bool },
                bool
            ),
    }

    device_class_init! {
        dummy_class_init,
        props => DUMMY_PROPERTIES,
        realize_fn => None,
        reset_fn => None,
        vmsd => VMSTATE,
    }
}
