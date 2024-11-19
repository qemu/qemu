// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

use std::path::Path;

use version_check as rustc;

fn main() {
    if !Path::new("src/bindings.rs").exists() {
        panic!(
            "No generated C bindings found! Either build them manually with bindgen or with meson \
             (`ninja bindings.rs`) and copy them to src/bindings.rs, or build through meson."
        );
    }

    // Check for available rustc features
    if rustc::is_min_version("1.77.0").unwrap_or(false) {
        println!("cargo:rustc-cfg=has_offset_of");
    }

    println!("cargo:rerun-if-changed=build.rs");
}
