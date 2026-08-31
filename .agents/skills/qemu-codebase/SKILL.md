---
name: qemu-codebase
description: >-
  Route broad or unfamiliar QEMU tasks to relevant subsystem,
  documentation, build, and test entry points. Do not use for work
  where the files, subsystems and abstractions are already known.
---

# Finding your way around QEMU

Use this skill for orientation, not as a general prerequisite for QEMU
work. Start from the code and nearby tests when the request already
identifies a file, function, subsystem, or commit.

## Finding the subsystem

For a broad request, use `grep`/`rg` and `MAINTAINERS` to locate
likely code and subsystem boundaries. Use `scripts/get_maintainer.pl
--nogit -f <path>` when the maintainance status matters.  Read
`docs/devel/codebase.rst` only when the top-level directory or execution
mode is unclear.

## Focused documentation

Read documentation only when the task involves the corresponding
abstraction or its invariants:

- QOM types, properties, composition, or lifecycle: `docs/devel/qom.rst`.
- qdev realize/unrealize, buses, GPIO, or hotplug: `docs/devel/qdev-api.rst`.
- Device or machine reset behavior: `docs/devel/reset.rst`.
- MemoryRegion topology, transactions, mappings, or dirty tracking:
  `docs/devel/memory.rst`.
- TCG frontend/backend boundaries or IR: `docs/devel/tcg.rst`; for the TCG IR,
  also read `docs/devel/tcg-ops.rst`.
- RCU lifetime, grace periods, or atomic ordering: `docs/devel/rcu.rst` and
  `docs/devel/atomics.rst`.
- AioContext migration, iothreads, or block/device concurrency:
  `docs/devel/multiple-iothreads.rst`.
- BQL ownership or code running outside the BQL: `include/qemu/main-loop.h`
  and nearby callers.

Prefer source declarations and nearby callers when they answer the
question more directly. A `kernel-doc::` directive points to comments
in the source; inspect those comments without building the documentation.

## Building and testing

QEMU builds out of tree and a checkout may have several build
directories. When building or testing, find the applicable configured
tree from context (for example, directories containing `meson-info`)
instead of assuming one. Use that build directory's `run` wrapper for
`meson test` so its environment and Python paths are active.

For C changes, run `scripts/checkpatch.pl` on the patch; for Rust changes,
consider `make clippy` and `make rustfmt`.

Choose the narrowest test that exercises the behavior. Use `make
check-unit`, `make check-qtest`, `make check-functional`, or `make
check-rust` only when a suite-level run is justified.
