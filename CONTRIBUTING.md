# Contributing to Meadow.OS

Thank you for contributing. Please read these guidelines before opening a pull request.

## Repository Overview

See [README.md](README.md) for a full description of the project structure. In brief:

| Directory | Layer | Language |
|-----------|-------|----------|
| `bootloader` | Bootloader (STM32 HAL) | C |
| `Meadow.OS` | Kernel extensions & board support | C |
| `nuttx` | NuttX kernel source | C |
| `apps/system/hcom` | User-space application | C |

## Where to Put New Code

| What you are adding | Where it goes |
|---------------------|--------------|
| Kernel driver, OS service, or kernel-side protocol handler | `Meadow.OS/<subsystem>` |
| Board-specific initialisation | `nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src` |
| User-space feature for hcom | `apps/system/hcom/<subsystem>` |
| Shared header (kernel + user) | `Meadow.OS/include/meadow` |
| Kconfig option | Appropriate `Kconfig` file; prefix with `MEADOW_` |
| Build script change | `build.sh` or `scripts` |
| In-kernel test | `Meadow.OS/kerneltests` |
| User-space test | `apps/system/hcom/tests` |

## Code Style

Meadow.OS follows the [NuttX coding standard](https://nuttx.apache.org/docs/latest/contributing/coding_style.html). Key rules are summarised below.

### Naming

- `snake_case` for all identifiers: functions, variables, types, and file names
- `UPPER_SNAKE_CASE` for macros and `#define` constants
- `CONFIG_` prefix for all kconfig symbols
- `MEADOW_` prefix for Wilderness Labs-specific kconfig symbols and global identifiers
- Module-level prefix on all public symbols, e.g. `hcom_nx_`, `meadow_os_`, `espcp_`, `pwrmgmt_`

### File Structure

Every source file must open with the standard section banner and copyright header:

```c
/****************************************************************************
 * path/to/filename.c
 *
 *   Copyright (C) <year> Wilderness Labs. All rights reserved.
 *   Author:  Wilderness Labs
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * ...
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Types
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Public Functions
 ****************************************************************************/
```

### Function Headers

All non-trivial functions require a NuttX-style header block:

```c
/****************************************************************************
 * Name: function_name
 *
 * Description:
 *   One-paragraph description of what the function does.
 *
 * Input Parameters:
 *   param1 - Description
 *   param2 - Description
 *
 * Returned Value:
 *   Description of return value, or None.
 *
 ****************************************************************************/
```

### Formatting

- 2-space indentation; no tabs
- Lines no longer than 78 characters where practical
- Opening brace on the same line for control flow; on a new line for function definitions
- No trailing whitespace
- One blank line between function definitions

## Kernel vs User Space

The build enforces a hard MPU boundary between kernel and user code. A violation will cause a fault at runtime.

**Kernel code** (`Meadow.OS`, `nuttx/boards`) runs in ARM privileged mode:

- Use `kmalloc` / `kzalloc` — never `malloc`
- Do not call POSIX-only or user-space-only APIs
- Shared data with user space must go through the syscall interface (`meadow_syscall.h`)

**User code** (`apps/system/hcom`) runs in ARM unprivileged mode:

- Use standard `malloc` / `free`
- Access kernel resources only via syscalls or file descriptors

## Memory & MPU

Any change that affects memory layout must be reviewed carefully:

- Flash partitions are defined in `nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/scripts/memory.ld`
- QSPI partition layout is shared between the bootloader and NuttX — changes must be consistent in both
- MPU region sizes must be powers of 2 and naturally aligned
- Document any change to section sizes or addresses in the PR description

See [Documentation/Memory.md](Documentation/Memory.md) for the full memory map.

## OTA and Bootloader Interface

The bootloader and NuttX communicate via:

- RTC battery-backed registers (`BK29R`–`BK31R`) — see `Meadow.OS/include/meadow/hcom_bbreg_defn.h`
- OTA flag bytes in QSPI — see `bootloader/Core/Inc/ota_data.h`

Changes to these interfaces must maintain backwards compatibility with the deployed bootloader, or a bootloader update must be included in the same PR.

## Configuration Changes

Use `kconfig-tweak` to modify the defconfig rather than editing `.config` directly:

```bash
kconfig-tweak --file nuttx/.config --enable CONFIG_MY_NEW_OPTION
make -C nuttx savedefconfig
```

Copy the result back to the board config directory:

```
nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/configs/meadow_os/defconfig
```

## Testing

All non-trivial changes must be tested. The build system includes named test suites enabled at build time:

```bash
./build.sh -u=gpio,ethernet,os
```

Available suites: `esp`, `gpio`, `ethernet`, `os`, `misc`, `sdcard`, `adc`, `dac`, `bbr`, `power`, `spidma`, `rotenc`, `snprintf`, `sqllite`, `tensorflow`, `dirmgmt`, `chat`, `bg77`, `iso8601` — see `build.sh` for the complete list.

Hardware testing on a physical Meadow F7 board is required for any change touching:

- Drivers or peripheral configuration
- Memory layout or MPU regions
- OTA / bootloader interaction
- Power management
- Network interfaces (ESP32 WiFi or Ethernet)

Document your test steps in the PR.

## Commit Messages

Use the imperative mood in the subject line, 72 characters maximum:

```
Add Ethernet link-down recovery to meadow_ethnet_monitor

Without this change, a transient cable disconnect caused the network
interface to stall indefinitely. The monitor now attempts reconnection
after a configurable timeout.

Fixes #123
```

Reference issues where applicable: `Fixes #123`, `Relates to #456`.

## Security

- **Never** commit secrets, credentials, WiFi passwords, or device certificates
- The build automatically removes `secrets.h` at the end of `build.sh`; do not circumvent this
- Client certificates must not be committed to the repository

## Pull Request Process

1. Fork the repository and create a branch from `main`
2. Make your changes, following the style and structure guidelines above
3. Run `./build.sh --clean --force` and confirm it completes without errors
4. Run relevant test suites with `-u=` and test on hardware where required
5. Fill in the pull request template completely
6. A maintainer will review the PR; address all feedback before merge

PRs that fail CI, are missing test evidence, or do not follow the code style will not be merged.
