// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

//! Macro to register blocks of code that run as QEMU starts up.

#[macro_export]
macro_rules! module_init {
    ($type:ident => $body:block) => {
        const _: () = {
            #[used]
            #[cfg_attr(
                not(any(target_vendor = "apple", target_os = "windows")),
                link_section = ".init_array"
            )]
            #[cfg_attr(target_vendor = "apple", link_section = "__DATA,__mod_init_func")]
            #[cfg_attr(target_os = "windows", link_section = ".CRT$XCU")]
            pub static LOAD_MODULE: extern "C" fn() = {
                extern "C" fn init_fn() {
                    $body
                }

                extern "C" fn ctor_fn() {
                    unsafe {
                        $crate::bindings::register_module_init(
                            Some(init_fn),
                            $crate::bindings::module_init_type::$type,
                        );
                    }
                }

                ctor_fn
            };
        };
    };

    // shortcut because it's quite common that $body needs unsafe {}
    ($type:ident => unsafe $body:block) => {
        $crate::module_init! {
            $type => { unsafe { $body } }
        }
    };
}
