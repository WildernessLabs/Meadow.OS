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

Some board specific files use classic C style coding style with some also using Allman style.  The key recommendation is to keep to the style of the file being worked on.  When creating new files use the style consistent with the files in the subsystem being modified.

### Naming

- `snake_case` for all identifiers: functions, variables, types, and file names
- `UPPER_SNAKE_CASE` for macros and `#define` constants
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

All functions require a NuttX-style header block:

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
 * Assumptions:
 *   Any assumptions / side effects or None.
 *
 ****************************************************************************/
```

### Formatting

- 2-space indentation; no tabs
- Lines no longer than 78 characters where practical
- No trailing whitespace
- One blank line between function definitions

## Kernel vs User Space

The build enforces a hard MPU boundary between kernel and user code. A violation will cause a fault at runtime.

**Kernel code** (`Meadow.OS`, `nuttx/boards`) runs in ARM privileged mode:

- Use `kmalloc` / `kzalloc` (preferred) — never `malloc`
- Do not call POSIX-only or user-space-only APIs

**User code** (`apps/system/hcom`) runs in ARM unprivileged mode:

- Use standard `malloc` / `free`
- Access kernel resources only via syscalls or file descriptors

## Memory & MPU

See [Documentation/Memory.md](Documentation/Memory.md) for the full memory map.

## OTA and Bootloader Interface

The bootloader and NuttX communicate via:

- Battery-backed registers (`BK29R`–`BK31R`) — see `Meadow.OS/include/meadow/hcom_bbreg_defn.h`
- OTA flag bytes in QSPI — see `bootloader/Core/Inc/ota_data.h`

Changes to these interfaces must maintain backwards compatibility with the deployed bootloader, or a bootloader update must be included in the same PR.

## Configuration Changes

NuttX and the Meadow OS are configured through the `defconfig` file.  Permanent configuration changes are normally made to this file.  For instance enabling the CAN driver would be performed through changes to the `defconfig` file.  Changes to the config are made using the following command sequence:

```bash
make -C nuttx menuconfig
make -C nuttx savedefconfig
```

Finally, copy the result back to the board config directory:

```
cp defconfig nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/configs/meadow_os
```

Temporary changes are normally made using the `kconfig-tweak` application.  For instance, the WiFi unit tests would be enabled using this mechanism as they would ne needed for builds on a developer machine but they would not be needed in production.

Use `kconfig-tweak` to modify the defconfig rather than editing `.config` directly:

## Testing

All non-trivial changes must be tested. The build system includes named test suites enabled at build time:

```bash
./build.sh -u=gpio,ethernet,os
```

Available suites: `esp`, `gpio`, `ethernet`, `os`, `misc`, `sdcard`, `adc`, `dac`, `bbr`, `power`, `spidma`, `rotenc`, `snprintf`, `sqlite`, `tensorflow`, `dirmgmt`, `chat`, `bg77`, `iso8601` — see `build.sh` for the complete list.

Pull requests should detail:

- The tests that have been performed
- Steps to reproduce the tests
- Results of the tests

## Commit Messages

Use the imperative mood in the subject line:

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
- The build automatically removes `secrets.h` at the end of `build.sh`
- Client certificates must not be committed to the repository

## Pull Request Process

1. Fork the repository and create a branch from `main`
2. Make your changes, following the style and structure guidelines above
3. Run `./build.sh --clean --force` and confirm it completes without errors
4. Run relevant test suites with `-u=` and test on hardware where required
5. Fill in the pull request template completely
6. A maintainer will review the PR

PRs that fail CI, are missing test evidence, or do not follow the code style will be rejected.


## Use of AI and LLMs

The use of AI / LLMs (hereafter referred to as AI) is permitted on the following basis:

- The AI does not inject copyright material into the changes
- A Software Engineer performs a quality check prior to submission of any changes.  This Software Engineer assumes responsibility for the change.
- The use of AI is acknowledged in the PR