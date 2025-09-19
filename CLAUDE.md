# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build System and Configuration

QEMU uses a hybrid build system with autotools-style configure script and Meson:

1. **Initial Configuration**: `./configure` (creates `build/` directory and switches to it automatically)
2. **Compilation**: `make` (runs from build directory, delegates to Meson/Ninja)
3. **Build Files**:
   - Main build configuration: `meson.build`
   - Generated: `build.ninja`, `config-host.mak`
   - Build artifacts go in `build/` directory

### Common Build Commands

```bash
# Basic build process
mkdir build && cd build
../configure
make

# Or let configure handle build directory
./configure  # automatically creates and uses build/
make

# Clean builds
make distclean  # Complete cleanup
make clean      # Partial cleanup
```

## Testing Infrastructure

QEMU has comprehensive testing organized into several categories:

### Test Commands
- `make check` - Run all main tests (unit, qtest, qapi-schema, iotests)
- `make check-unit` - Unit tests only
- `make check-qtest` - Functional tests using QTest framework
- `make check-functional` - Python-based functional tests
- `make check-block` - Block layer tests
- `make bench` - Performance benchmarks

### Test Organization
- **Unit tests**: `tests/unit/` - Simple C tests for individual components
- **QTests**: `tests/qtest/` - Functional tests using QTest framework for full system testing
- **Functional tests**: `tests/functional/` - Python-based system tests
- **Block tests**: `tests/qemu-iotests/` - Block layer and storage format tests

### Test Execution Options
- Test suites: `quick` (default), `slow`, `thorough`
- Individual target tests: `make check-qtest-TARGET` (e.g., `check-qtest-x86_64`)
- Meson test runner: `meson test --suite SUITE`

## Architecture Overview

QEMU is structured around several key subsystems:

### Core Components
- **`system/`** - Main system emulation code (vl.c is main entry, memory.c handles memory subsystem)
- **`target/`** - CPU architecture implementations (x86, ARM, RISC-V, etc.)
- **`hw/`** - Hardware device emulation organized by device type
- **`accel/`** - Acceleration frameworks (TCG, KVM, HAX, etc.)
- **`block/`** - Block device and storage format handling
- **`net/`** - Network device emulation
- **`ui/`** - User interface backends (VNC, SDL, GTK, etc.)

### Emulation Modes
- **System Emulation** (`*-softmmu`): Full system with virtual hardware
- **User Emulation** (`linux-user`, `bsd-user`): Application-level emulation
- **Acceleration**: TCG (Tiny Code Generator) for cross-platform, KVM/HAX for native

### Key Subsystems
- **QOM** (QEMU Object Model): Object-oriented framework for devices and machines
- **QDev**: Device modeling and management framework
- **Memory API**: Unified memory management for emulated systems
- **TCG**: Dynamic binary translation engine
- **Migration**: Live migration and save/restore functionality

## Development Workflow

### Code Organization
- Each target architecture has its own directory in `target/`
- Hardware devices are categorized in `hw/` by function (e.g., `hw/pci/`, `hw/usb/`)
- Utility code in `util/`, common headers in `include/`
- Build definitions are in `meson.build` files throughout the tree

### Key Development Files
- **Device Models**: Inherit from QOM base classes, use QDev properties
- **Machine Definitions**: Define system-level configuration and device layout
- **Memory Maps**: Defined per-machine for address space layout
- **TCG Operations**: Target-specific instruction translation in `target/ARCH/translate.c`

### Adding New Components
1. **New Device**: Add to appropriate `hw/` subdirectory, update `meson.build`
2. **New Machine**: Add machine definition file, register with machine type
3. **New Tests**: Add unit tests to `tests/unit/`, qtests to `tests/qtest/`
4. **Target Support**: Extensive changes across `target/`, `hw/`, `accel/tcg/`

## Important Scripts and Tools

- **`scripts/checkpatch.pl`** - Code style checker (run before submitting patches)
- **`scripts/get_maintainer.pl`** - Find maintainers for code areas
- **`scripts/qemu-gdb.py`** - GDB integration for debugging QEMU itself
- **`scripts/device-crash-test`** - Test device initialization robustness
- **`scripts/analyze-migration.py`** - Migration stream analysis

## Submitting Code

QEMU follows a patch-based development model:
- Patches sent to qemu-devel@nongnu.org mailing list
- Use `git format-patch` and `git send-email`
- All patches require `Signed-off-by` line
- Follow coding style guidelines and run `scripts/checkpatch.pl`
- Consider using `git-publish` tool for patch series management

## Build Dependencies

QEMU has minimal required dependencies but many optional features:
- **Required**: C compiler, Python 3, pkg-config, glib2, pixman
- **Common Optional**: SDL2, GTK3, VNC server, curl, zlib
- **Advanced**: KVM support, various acceleration libraries, specialized device emulation libs