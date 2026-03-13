---
name: Bug Report
about: Report a defect or unexpected behaviour
labels: bug
---

## Summary

<!-- A clear and concise description of the bug -->

## Area

<!-- Tick the area where the bug occurs -->

- [ ] Bootloader (`bootloader`)
- [ ] Kernel / Meadow.OS extensions (`Meadow.OS`)
- [ ] User space / hcom (`apps/system/hcom`)
- [ ] Build system
- [ ] Debugging / OpenOCD / GDB
- [ ] Unknown

## Steps to Reproduce

<!--
Provide the exact steps needed to trigger the bug, e.g.:
1. Build with ./build.sh --debug
2. Flash with ./flash.sh
3. Connect GDB with ./debug.sh
4. Set breakpoint at ...
5. Observe ...
-->

1.
2.
3.

## Expected Behaviour

<!-- What should have happened? -->

## Actual Behaviour

<!-- What actually happened? Include any error messages, fault addresses, or log output -->

## Diagnostic Information

### Build information

- OS version
  - OS: 
  - Runtime: 
  - Coprocessor: 
- Build flags used (e.g. `--debug`, `-u=`):
- Hardware version (v1 / v2 / CCM / Ethernet):

### Crash / fault details

<!-- If the bug caused a hard fault or assertion failure, include: -->

- Fault address:
- Backtrace (from GDB `bt` command):
- RAMLOG / syslog output (if available):

```
<!-- Paste output here -->
```

### Syslog / trace output

```
<!-- Paste relevant syslog or trace output here -->
```

## Reproducibility

- [ ] Consistently reproducible
- [ ] Intermittent — approximate frequency:
- [ ] Observed once

## Additional Context

<!-- Any other relevant information: recent changes, specific hardware conditions, related issues -->
