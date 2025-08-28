# QEMU ZeroTier Network Backend

This implementation adds ZeroTier network support to QEMU, allowing VMs to connect to ZeroTier virtual networks without requiring a ZeroTier interface on the host.

## Overview

ZeroTier operates at Layer 2 (Ethernet/Data Link layer), creating virtual Ethernet networks that span across the internet. This QEMU backend integrates libzt to provide direct access to ZeroTier networks for virtual machines.

## Features

- **Layer 2 Integration**: Full Ethernet frame support with proper MAC addressing
- **No Host Interface Required**: VMs connect directly to ZeroTier networks via userland networking
- **Automatic MAC Assignment**: Uses ZeroTier-assigned MAC addresses for network consistency
- **Network ID Support**: Join any ZeroTier network using its 16-character network ID

## Building

1. Clone with submodules:
```bash
git submodule update --init --recursive
```

2. Build libzt:
```bash
cd third_party/libzt
./build.sh host release
```

3. Configure and build QEMU:
```bash
./configure
make
```

## Usage

Use the `zerotier` network backend in QEMU:

```bash
qemu-system-x86_64 \
  -netdev zerotier,id=zt0,network=8056c2e21c000001 \
  -device e1000,netdev=zt0,mac=52:54:00:12:34:56 \
  ...
```

### Parameters

- `network`: ZeroTier network ID (16 hex characters, required)
- `port`: Local ZeroTier service port (optional, default: 9994)  
- `storage`: Storage path for ZeroTier identity (optional, default: /tmp/qemu-zerotier)

### Example

Join ZeroTier Earth network (8056c2e21c000001):
```bash
qemu-system-x86_64 \
  -netdev zerotier,id=earth,network=8056c2e21c000001,storage=/tmp/zt-vm1 \
  -device virtio-net,netdev=earth \
  -hda disk.img
```

## Network Setup

1. **Create/Join ZeroTier Network**: Use ZeroTier Central or create a self-hosted network
2. **Authorize the Node**: The VM will generate a ZeroTier node ID that needs authorization
3. **Configure Network**: Set up IP ranges, routing, and access control as needed

## Technical Details

### Architecture
- Uses libzt for ZeroTier userland networking
- Implements Layer 2 frame handling with Ethernet header construction
- Integrates with QEMU's network subsystem via NetClientState
- Supports standard QEMU network device models (e1000, virtio-net, etc.)

### Frame Processing
- **Outbound**: Extracts Ethernet frames from QEMU and sends via ZeroTier L2 API
- **Inbound**: Receives ZeroTier frames and reconstructs Ethernet headers for QEMU
- **MAC Handling**: Uses ZeroTier-assigned MAC addresses for network consistency

### Limitations
- Single ZeroTier network backend per QEMU instance
- Requires network authorization via ZeroTier controller
- Performance depends on ZeroTier network topology and latency

## Files Changed

- `qapi/net.json`: Added ZeroTier network backend definition
- `net/zerotier.c`: ZeroTier network backend implementation  
- `net/net.c`: Added ZeroTier backend registration
- `net/clients.h`: Added ZeroTier function declarations
- `meson.build`: Added ZeroTier build configuration
- `meson_options.txt`: Added ZeroTier build option

## Dependencies

- libzt (included as git submodule)
- CMake (for building libzt)
- ZeroTier network membership and authorization