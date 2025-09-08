// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

#[cfg(unix)]
use std::os::unix::fs::symlink as symlink_file;
#[cfg(windows)]
use std::os::windows::fs::symlink_file;
use std::{env, fs::remove_file, io::Result, path::Path};

fn main() -> Result<()> {
    let manifest_dir = env!("CARGO_MANIFEST_DIR");
    let file = if let Ok(root) = env::var("MESON_BUILD_ROOT") {
        let sub = get_rust_subdir(manifest_dir).unwrap();
        format!("{root}/{sub}/bindings.inc.rs")
    } else {
        // Placing bindings.inc.rs in the source directory is supported
        // but not documented or encouraged.
        format!("{manifest_dir}/src/bindings.inc.rs")
    };

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
    let dest_path = format!("{out_dir}/bindings.inc.rs");
    let dest_path = Path::new(&dest_path);
    if dest_path.symlink_metadata().is_ok() {
        remove_file(dest_path)?;
    }
    symlink_file(file, dest_path)?;

    println!("cargo:rerun-if-changed=build.rs");
    Ok(())
}

fn get_rust_subdir(path: &str) -> Option<&str> {
    path.find("/rust").map(|index| &path[index + 1..])
}
