## Description

<!-- What does this PR do and why? Link to any relevant issue: Fixes #123 -->

## Area of Change

<!-- Tick all that apply -->

- [ ] Bootloader (`bootloader`)
- [ ] Kernel / Meadow.OS extensions (`Meadow.OS`, `nuttx/boards`)
- [ ] User space / hcom (`apps/system/hcom`)
- [ ] NuttX configuration (`defconfig`, `Kconfig`)
- [ ] Build system (`build.sh`, `flash.sh`, `debug.sh`, makefiles)
- [ ] Documentation (`Documentation`, `README.md`)
- [ ] Tests

## Motivation

<!-- Why is this change needed? What problem does it solve or what capability does it add? -->

## Testing

<!-- Describe how this was tested. Untested changes will not be merged. -->

### Hardware tested

- [ ] Tested on physical Meadow F7 hardware
- [ ] Not applicable (docs / build only)

### Unit test suites run

<!-- List any `-u=` flags used with build.sh, e.g. `./build.sh -u=gpio,ethernet` -->

- [ ] None required
- [ ] `./build.sh -u=`

### Manual test steps

<!--
Describe the steps taken to verify the change works correctly, e.g.:
1. Flash with ./flash.sh
2. Connect GDB with ./debug.sh
3. Set breakpoint at ...
4. Observed ...
-->

## Memory & MPU Considerations

<!-- Any changes that affect memory layout, SRAM/flash usage, or MPU region boundaries must be noted here -->

- [ ] No impact on memory layout or MPU regions
- [ ] Memory layout or MPU regions affected — described below:

<!-- Describe impact, referencing memory.ld sections where applicable -->

## OTA / Bootloader Compatibility

- [ ] No impact on OTA update flow or bootloader interface
- [ ] Affects OTA state machine, battery-backed registers, or QSPI partition layout — described below:

<!-- Describe impact. Changes to the BL/OS interface must be backwards compatible or include a bootloader update -->

## Checklist

- [ ] Code follows the NuttX coding style (snake_case, `/*****` section banners, function header blocks)
- [ ] New Wilderness Labs source files include the standard copyright header
- [ ] `CONFIG_` / `MEADOW_` prefixes used correctly for kconfig options
- [ ] No `malloc` used in kernel-space code (`kmalloc` / `kzalloc` used instead)
- [ ] No secrets, credentials, or device-specific keys committed
- [ ] CI build passes (`./build.sh --clean --force`)
- [ ] Documentation updated if behaviour or public interfaces changed
