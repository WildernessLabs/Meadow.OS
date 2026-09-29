# Documentation

For a project overview, architecture summary, build instructions, and hardware specifications, see the [README.md](../README.md) in the repository root.

## Documents

| Document | Contents |
|----------|---------|
| [Architecture.md](Architecture.md) | Protected build model, boot sequence, OTA state machine, NuttX kernel configuration |
| [Memory.md](Memory.md) | Full memory map, flash partition layout, MPU regions, battery-backed registers |
| [Kernelspace.md](Kernelspace.md) | Kernel startup flow, board support package, all kernel modules and their responsibilities |
| [Userspace.md](Userspace.md) | hcom application structure, all user-space modules and shared headers |
| [Debugging.md](Debugging.md) | OpenOCD/GDB setup, flashing options, QEMU support, Python GDB extensions |
| [Rebasing](Rebasing.md) | Rebasing with mainline NuttX and NuttX apps |
| [Developer Tests](RunningDeveloperTests.md) | How to build and run developer (unit) tests |
| [Dual USB CDC/ACM](DualCdcAcm.md) | Developer-only `--dualcdc` build: second USB serial port, pinning Meadow.CLI to HCOM |
| [Meadow DAC](MeadowDac.md) | Draft design: native DAC driver for A00/A01 (issue #158) |

## In-Tree Documentation

Some source directories have their own, more detailed documentation that isn't duplicated here:

| Document | Contents |
|----------|---------|
| [`F7MeadowOS/espcp/readme.md`](../F7MeadowOS/espcp/readme.md) | ESP32 coprocessor overview |
| [`F7MeadowOS/espcp/doc/coprocessor.md`](../F7MeadowOS/espcp/doc/coprocessor.md) | ESP32/STM32 wire protocol and GPIO handshake lines |
| [`F7MeadowOS/espcp/doc/ImplementingPoll.md`](../F7MeadowOS/espcp/doc/ImplementingPoll.md) | Cross-layer `poll()` implementation (Mono/NuttX/usrsock/ESP32) |
