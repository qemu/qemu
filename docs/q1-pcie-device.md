# Q1 PCIe Device - QEMU Model

This document describes the Q1 PCIe device model for QEMU, which emulates the Q1 AI accelerator SoC with 4x Q32 CIM (Compute-in-Memory) accelerators.

## PCIe Configuration

| Field | Value |
|-------|-------|
| Vendor ID | `0x1234` |
| Device ID | `0x0001` |
| Class | Co-processor (`0x0B40`) |
| Revision | `0x01` |

## BAR Layout

### BAR0: Registers (64KB)

| Offset | Region | Size | Description |
|--------|--------|------|-------------|
| `0x0000` | Control Block | 4KB | Host-to-device communication |
| `0x1000` | Q32[0] | 4KB | CIM accelerator 0 registers |
| `0x2000` | Q32[1] | 4KB | CIM accelerator 1 registers |
| `0x3000` | Q32[2] | 4KB | CIM accelerator 2 registers |
| `0x4000` | Q32[3] | 4KB | CIM accelerator 3 registers |
| `0x5000` | SFU | 4KB | Special Function Unit registers |
| `0x6000` | FA | 4KB | Fused Attention registers |
| `0x7000` | DMA | 4KB | DMA Engine registers |
| `0x8000` | Reserved | 32KB | Future use |

### BAR2: DDR Memory (512MB, 64-bit prefetchable)

| Offset | Region | Size |
|--------|--------|------|
| `0x00000000` | Q32[0] DDR | 120MB |
| `0x07800000` | Q32[1] DDR | 120MB |
| `0x0F000000` | Q32[2] DDR | 120MB |
| `0x16800000` | Q32[3] DDR | 120MB |
| `0x1E000000` | Shared/Firmware | 32MB |

## Control Block Registers (BAR0 + 0x0000)

| Offset | Register | R/W | Description |
|--------|----------|-----|-------------|
| `0x000` | DOORBELL | W | Write to notify firmware |
| `0x004` | STATUS | RW | Device status |
| `0x008` | IRQ_STATUS | RW | Interrupt status (W1C) |
| `0x00C` | IRQ_MASK | RW | Interrupt mask |
| `0x010` | CMD_BUF_ADDR_LO | RW | Command buffer address [31:0] |
| `0x014` | CMD_BUF_ADDR_HI | RW | Command buffer address [63:32] |
| `0x018` | CMD_BUF_SIZE | RW | Command buffer size |
| `0x01C` | FW_STATUS | RW | Firmware status |
| `0x020` | VERSION | R | Version (0xMMmmpp) |
| `0x024` | CAPS | R | Capabilities |

### IRQ Bits

| Bit | Name | Description |
|-----|------|-------------|
| 0 | DOORBELL | Doorbell interrupt |
| 1 | COMPLETE | Command completion |
| 2 | ERROR | Error occurred |

### Firmware Status Values

| Value | Name | Description |
|-------|------|-------------|
| `0x00` | RESET | Device in reset |
| `0x01` | INIT | Initializing |
| `0x02` | READY | Ready for commands |
| `0x03` | BUSY | Processing commands |
| `0xFF` | ERROR | Error state |

### Capabilities Register

| Bits | Field | Description |
|------|-------|-------------|
| [3:0] | NUM_Q32 | Number of Q32 cores (4) |
| [4] | HAS_SFU | SFU present |
| [5] | HAS_FA | Fused Attention present |
| [6] | HAS_DMA | DMA engine present |

## Q32 Registers (BAR0 + 0x1000 + core_id × 0x1000)

| Offset | Register | R/W | Description |
|--------|----------|-----|-------------|
| `0x00` | CONTROL | RW | Control register |
| `0x04` | STATUS | RW | Status register (W1C for DONE/ERROR) |
| `0x08` | SRC_ADDR_LO | RW | Source address [31:0] |
| `0x0C` | SRC_ADDR_HI | RW | Source address [63:32] |
| `0x10` | DST_ADDR_LO | RW | Destination address [31:0] |
| `0x14` | DST_ADDR_HI | RW | Destination address [63:32] |
| `0x18` | SCALE_ADDR_HI | RW | Scale address [63:32] |
| `0x1C` | SCALE_ADDR_LO | RW | Scale address [31:0] |
| `0x24` | CMD_FIFO_CTRL | RW | Command FIFO control (triggers execution) |
| `0x2C` | CMD_FIFO_STATUS | R | Command FIFO status (depth in [5:0]) |
| `0x30` | CIM_STATUS | R | CIM array status |
| `0x34` | DEBUG | RW | Debug register |

### Q32 Status Bits

| Bit | Name | Description |
|-----|------|-------------|
| 0 | BUSY | Core is busy |
| 1 | DONE | Last command completed |
| 2 | ERROR | Error occurred |
| 4 | FIFO_EMPTY | Command FIFO is empty |
| 5 | FIFO_FULL | Command FIFO is full |

## SFU Registers (BAR0 + 0x5000)

| Offset | Register | R/W | Description |
|--------|----------|-----|-------------|
| `0x00` | CONTROL | RW | Control register |
| `0x04` | STATUS | RW | Status register |
| `0x08` | SRC_ADDR_LO | RW | Source address [31:0] |
| `0x0C` | SRC_ADDR_HI | RW | Source address [63:32] |
| `0x10` | DST_ADDR_LO | RW | Destination address [31:0] |
| `0x14` | DST_ADDR_HI | RW | Destination address [63:32] |
| `0x18` | LENGTH | RW | Data length |
| `0x1C` | OPCODE | RW | Operation code |

## FA Registers (BAR0 + 0x6000)

| Offset | Register | R/W | Description |
|--------|----------|-----|-------------|
| `0x00` | CONTROL | RW | Control register |
| `0x04` | STATUS | RW | Status register |
| `0x08` | Q_ADDR_LO | RW | Query address [31:0] |
| `0x0C` | Q_ADDR_HI | RW | Query address [63:32] |
| `0x10` | K_ADDR_LO | RW | Key address [31:0] |
| `0x14` | K_ADDR_HI | RW | Key address [63:32] |
| `0x18` | V_ADDR_LO | RW | Value address [31:0] |
| `0x1C` | V_ADDR_HI | RW | Value address [63:32] |
| `0x20` | OUT_ADDR_LO | RW | Output address [31:0] |
| `0x24` | OUT_ADDR_HI | RW | Output address [63:32] |
| `0x28` | SEQ_LEN | RW | Sequence length |
| `0x2C` | HEAD_DIM | RW | Head dimension |

## DMA Registers (BAR0 + 0x7000)

| Offset | Register | R/W | Description |
|--------|----------|-----|-------------|
| `0x00` | CONTROL | RW | Control register |
| `0x04` | STATUS | RW | Status register |
| `0x08` | SRC_ADDR_LO | RW | Source address [31:0] |
| `0x0C` | SRC_ADDR_HI | RW | Source address [63:32] |
| `0x10` | DST_ADDR_LO | RW | Destination address [31:0] |
| `0x14` | DST_ADDR_HI | RW | Destination address [63:32] |
| `0x18` | LENGTH | RW | Transfer length |
| `0x1C` | STRIDE_SRC | RW | Source stride |
| `0x20` | STRIDE_DST | RW | Destination stride |

## Building QEMU

```bash
cd /Users/nishraptor/Projects/Qernel/qemu/build
ninja qemu-system-x86_64-unsigned
```

## Test Commands

### Basic test (device instantiation)

```bash
./qemu-system-x86_64-unsigned -machine q35 -device q1-pcie -m 2G -nographic
```

### With debug logging

```bash
./qemu-system-x86_64-unsigned -machine q35 -device q1-pcie -m 2G -display none -d unimp -D qemu.log
```

### Check device is registered

```bash
./qemu-system-x86_64-unsigned -device help | grep q1
# Output: name "q1-pcie", bus PCI, desc "Q1 AI Accelerator (4x Q32 CIM + SFU + FA + DMA)"
```

### View device options

```bash
./qemu-system-x86_64-unsigned -device q1-pcie,help
```

## Inside a Linux Guest

Once Linux boots in the guest:

```bash
# List PCI devices
lspci
# Should show: 00:xx.0 Co-processor: Device 1234:0001 (rev 01)

# View BAR resources
lspci -v -s 00:xx.0
# Should show BAR0 (64KB) and BAR2 (512MB)
```

## Future Work

- [ ] Embedded RISC-V CPU for firmware execution
- [ ] MSI-X interrupt support
- [ ] Actual Q32 CIM computation logic
- [ ] SFU activation function implementations
- [ ] FA attention computation
- [ ] DMA transfers
