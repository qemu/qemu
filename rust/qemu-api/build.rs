// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

#[cfg(unix)]
use std::os::unix::fs::symlink as symlink_file;
#[cfg(windows)]
use std::os::windows::fs::symlink_file;
use std::{env, fs::remove_file, io::Result, path::Path};

use version_check as rustc;

fn main() -> Result<()> {
    // Placing bindings.inc.rs in the source directory is supported
    // but not documented or encouraged.
    let path = env::var("MESON_BUILD_ROOT")
        .unwrap_or_else(|_| format!("{}/src", env!("CARGO_MANIFEST_DIR")));

    let file = format!("{}/bindings.inc.rs", path);
    let file = Path::new(&file);
    if !Path::new(&file).exists() {
        panic!(concat!(
            "\n",
            "    No generated C bindings found! Maybe you wanted one of\n",
            "    `make clippy`, `make rustfmt`, `make rustdoc`?\n",
            "\n",
            "    For other uses of `cargo`, start a subshell with\n",
            "    `pyvenv/bin/meson devenv`, or point MESON_BUILD_ROOT to\n",
            "    the top of the build tree."
        ));
    }

    let out_dir = env::var("OUT_DIR").unwrap();
    let dest_path = format!("{}/bindings.inc.rs", out_dir);
    let dest_path = Path::new(&dest_path);
    if dest_path.symlink_metadata().is_ok() {
        remove_file(dest_path)?;
    }
    symlink_file(file, dest_path)?;

    // Check for available rustc features
    if rustc::is_min_version("1.77.0").unwrap_or(false) {
        println!("cargo:rustc-cfg=has_offset_of");
    }

    println!("cargo:rerun-if-changed=build.rs");
    Ok(())
}
