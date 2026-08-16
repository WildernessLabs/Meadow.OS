# Consolidate the UPD Drivers

The long term aim of this piece of work is to actually remove both the HCOM UPD driver and the Meadow UPD driver from the system and replace them with `syscalls`.  RThe first step in achieving this goal is to consolidate the HCOM UPD driver with the Meadow UPD driver.

The Meadow UPD driver code needs to remain in place as there are external dependencies on this code.  It has therefore been decided to remove the HCOM UPD driver and merge the functionality into the Meadow UPD driver.

---

# Implementation Plan

## 1. Current state

Two independent character drivers are registered, each with its own `file_operations`,
its own ioctl command space, and its own private copies of several identical structures.

| | Meadow UPD | HCOM UPD |
|---|---|---|
| Node | `/dev/upd` | `/dev/nxupd` |
| Driver source | `F7MeadowOS/meadow_os/meadow-upd.c` | `F7MeadowOS/hcom_nx/hcom_nx_upd.c` |
| Shared header | `F7MeadowOS/include/meadow/meadow-upd.h` | `F7MeadowOS/include/meadow/hcom_upd_shared.h` |
| Command prefix | `MUPD_*` | `HCOM_NX_UPD_*` |
| Registered by | `meadow_upd_initialize()`, called from `stm32_boot.c:263` | `hcom_nx_upd_initialize()`, called from `hcom_nx_startup_mgr.c:300` |
| `file_operations` | `open`, `close`, `ioctl` | `open`, `close`, `read`, `ioctl` |
| In-repo user-space client | none | `apps/system/hcom/nx_rqsts/hcom_via_nx_access.c` (sole client) |
| Out-of-repo client | Meadow.Core / .NET runtime (ABI frozen) | none known - to be confirmed |

Supporting facts established while surveying the code:

- Both `register_driver()` calls happen inside `board_late_initialize()`
  (`stm32_boot.c:263` runs before `stm32_boot.c:361 -> hcom_nx_setup_mgr()`), so the two
  registrations can be collapsed into one without reordering anything that matters.
- `meadow-upd.c` already `#include`s `hcom_nx/hcom_nx_common.h` and
  `hcom_nx/hcom_nx_config_manager.h`, and already calls `hcom_nx_config_get_set_config_value()`
  and `pwrmgmt_enter_stm32f7_stop_mode()`. The dependency direction needed for the merge
  therefore already exists.
- `hcom_nx_upd_route_cmd.c` is not a driver - it is the CLI command router reached via
  `HCOM_NX_UPD_CLI_COMMAND`. It is unaffected by this work beyond a possible file rename.
- The `hcom_nx_upd.c` `read` handler is documented as existing so that
  `open(..., O_RDONLY)` succeeds (`hcom_nx_upd.c:95`); `meadow-upd.c` has no `read`. On
  this NuttX version that reasoning no longer holds - a present `ioctl` method already
  satisfies the VFS access check - but the merged driver keeps the stub anyway. See 4.2.1.

## 2. Blocking findings that shape the design

These were found during the survey and must be resolved before or during the merge.

### 2.1 The two command number spaces overlap

Eleven command numbers are used by both drivers with completely different meanings:

| Value | `MUPD_*` | `HCOM_NX_UPD_*` |
|---|---|---|
| 1 | `SET_REGISTER` | `SET_REGISTER` (same semantics) |
| 2 | `GET_REGISTER` | `GET_REGISTER` (same semantics) |
| 3 | `UPDATE_REGISTER` | `UPDATE_REGISTER` (same semantics) |
| 5 | `REGISTER_GPIO_IRQ` | `GET_BBR_VALUE` |
| 11 | `PWM_SHUTDOWN` | `RESTORE_UART_CONFIG` |
| 20 | `I2C_SHUTDOWN` | `HOST_RESTART_MEADOW_MCU` |
| 21 | `I2C_DATA` | `ONLY_RESTART_MEADOW_MCU` |
| 31 | `SPI_DATA` | `RTC_READ_TIME` |
| 32 | `SPI_SPEED` | `RTC_WAKEUP_TIME` |
| 33 | `SPI_MODE` | `UPDATE_OS1` |
| 34 | `SPI_BITS` | `UPDATE_OS2` |

A single device node can only map a number to one handler, so one of the two sets must be
renumbered. `MUPD_*` is the externally consumed ABI and must not move; `HCOM_NX_UPD_*` is
referenced only from `apps/system/hcom/nx_rqsts/hcom_via_nx_access.c`, which is rebuilt in
lockstep with the kernel. **The `HCOM_NX_UPD_*` set is renumbered into a range of its
own** (step 4.4); `MUPD_*` values are left exactly where they are.

#### 2.1.1 The new range must also avoid NuttX's reserved ioctl bases

NuttX allocates ioctl command numbers in 256-entry blocks (`nuttx/include/nuttx/fs/ioctl.h`,
`_IOC_MASK` is `0x00ff`), and the VFS layer intercepts a handful of commands *before and
after* dispatching to the driver - `nuttx/fs/vfs/fs_ioctl.c:73` and `:173` switch on
`FIONBIO`, `FIOC_*`, `BIOC_*`, `FIOCLEX`, `FIONCLEX`. A Meadow command that happened to
equal one of those would be swallowed by the VFS and never reach `upd_ioctl()`.

Upstream's allocated bases currently run contiguously from `_TIOCBASE` (`0x0100`) up to
`_EEPIOCBASE` (`0x4600`), with outliers at `0x8b00` and `0xff00`, and upstream grows that
list sequentially from the bottom. So `0x0100` - the obvious first choice, and what the
earlier draft of this plan proposed - sits on top of `_TIOCBASE` and inside the block
upstream is actively filling. **Use `0x7000` instead**: it is clear of every current
allocation, clear of the intercepted `_FIOCBASE`/`_BIOCBASE` blocks, and has roughly 40
free blocks of headroom below it before upstream's sequential growth could reach it. A
rebase onto a newer NuttX only needs to re-check this one constant.

The existing `MUPD_*` values (1 to 82) sit in block `0x0000`, which NuttX does not
allocate to anything, so they are safe where they are - another reason not to move them.

### 2.2 `/dev/upd` is not registered in the current build

`meadow_upd_initialize()` at `stm32_boot.c:263` sits inside `#ifdef CONFIG_EXAMPLES_MONO`,
and `CONFIG_EXAMPLES_MONO` is commented out in
`nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/configs/meadow_os/defconfig:314`
("Leaving here as a reference for when Mono is integrated"). `meadow-upd.c` is still
compiled (it is in the unconditional `MEADOW_OS` block of `src/Make.defs:85`) but its
device node is never created in the shipped configuration.

This is the single biggest hazard in the whole task: moving HCOM's ioctls onto `/dev/upd`
while leaving registration behind `CONFIG_EXAMPLES_MONO` would take the entire host
communication path offline. Registration must become unconditional (step 4.3).

### 2.3 Seven HCOM ioctl commands are defined but never handled

`hcom_upd_shared.h` defines these; `hcom_nx_upd_ioctl()` has no `case` for any of them:

| Command | Value | User-space caller | Effect today |
|---|---|---|---|
| `HCOM_NX_UPD_EXECUTE_ESPCP_TESTS` | 18 | `hcom_via_nx_access.c:501` | falls to `default:`, logs "unknown", returns `ERROR` |
| `HCOM_NX_UPD_DIAG_APP_CMD` | 29 | `hcom_via_nx_access.c:386` | same |
| `HCOM_NX_UPD_GET_UPDATE_FLAG` | 35 | `hcom_via_nx_access.c:584` | same |
| `HCOM_NX_UPD_SET_UPDATE_FLAG` | 36 | `hcom_via_nx_access.c:600` | same |
| `HCOM_NX_UPD_GET_STRING` | 23 | none | dead define |
| `HCOM_NX_UPD_FLASH_OS_UPDATE` | 28 | none | dead define |
| `HCOM_NX_UPD_RTC_READ_TIME` | 31 | none | dead define (RTC read goes via `CLI_COMMAND`) |

The first four are live regressions, not merge artefacts. They are called out here so the
merge does not silently bake them in; see step 6 for the disposition.

### 2.4 Two switch fall-through defects in `hcom_nx_upd_ioctl()`

- `hcom_nx_upd.c:239-248`: the `HCOM_NX_UPD_RTC_WAKEUP_TIME` case has no `return`, so
  setting an RTC wakeup time falls straight through into `HCOM_NX_UPD_UPDATE_OS1` and
  stages an OS update. (Currently masked only because `HCOM_INCLUDE_ISO8601_SUPPORT` is 0
  in `hcom_nx_common.h:122`, which compiles the case out.)
- `hcom_nx_upd.c:248-256`: `HCOM_NX_UPD_UPDATE_OS1`'s `return` is inside
  `#if (MEADOW_INCLUDE_CODE_FOR_TESTING_5MB_OF_FLASH == 0)`, so with 5 MB flash testing
  enabled `UPDATE_OS1` falls through into `UPDATE_OS2`.

Both must be fixed while transcribing the cases - transcribing them faithfully would carry
live bugs into the consolidated driver.

### 2.5 Return-value conventions differ between the two drivers

`meadow-upd.c` returns *positive* `errno` values on failure (`ENODEV`, `EINVAL`,
`ENOTDIR`), which NuttX's ioctl path reports to the caller as success. `hcom_nx_upd.c`
returns `OK`/`ERROR`. **Do not normalise these during the merge.** Each command keeps its
existing return convention byte-for-byte; changing them silently changes behaviour for the
external .NET client. Normalisation, if wanted, is separate follow-up work.

## 3. Target design

One driver, one node, one file. `F7MeadowOS/hcom_nx/hcom_nx_upd.c` is deleted outright and
everything it did moves into `F7MeadowOS/meadow_os/meadow-upd.c`, which becomes the single
kernel-side entry point for user space:

```
  stm32_boot.c
      |
      +-> meadow_upd_initialize()      register_driver("/dev/upd", ...)
                |
                +-> upd_ioctl()        meadow_os/meadow-upd.c - one switch
                       |
                       +-- MUPD_*         cases  (unchanged, values 1 - 82)
                       +-- HCOM_NX_UPD_*  cases  (moved here, values 0x7000+)
                       +-- default:              one "unknown command" log
```

Consequences worth stating up front:

- `meadow-upd.c` grows from 778 lines to roughly 1000. The file gains a clearly delimited
  HCOM section (switch cases grouped together, moved helpers in their own block) so the two
  concerns stay legible even though they now share a file.
- The HCOM-specific includes come with it - `stm32_uid.h`, `stm32_gpio.h`,
  `hcom_dfu_mode_entry.h`, `hcom_bbreg_defn.h`, `meadow_hw_version.h`,
  `meadow_apps_core_share.h`, `espcp_coprocessor.h`, `espcp_usrsock.h` and
  `meadow/hcom_upd_shared.h`. Note the path change: `hcom_nx_upd.c` reaches espcp as
  `"../espcp/..."`, whereas from `meadow_os/` it is `"espcp/..."` - the form `meadow-upd.c`
  already uses for `espcp/espcp_common.h`.
- `hcom_nx/` keeps only `hcom_nx_upd_route_cmd.c`, the CLI router reached through
  `HCOM_NX_UPD_CLI_COMMAND`. It is not a driver and does not move.
- The `CONFIG_MEADOW_HCOM` guard that `hcom_nx/` sources sit behind in `src/Make.defs:103`
  is no longer supplied by the build system for this code, because `meadow-upd.c` is
  compiled unconditionally (`src/Make.defs:85`). It has to be reproduced inside the file
  instead - see step 4.2.
- A single switch is what makes the renumber in step 4.4 load-bearing rather than tidy:
  with both command sets in one `switch`, the eleven collisions from finding 2.1 become
  duplicate `case` labels and the compiler rejects them. Under the two-file design the same
  collisions would have failed silently at runtime. Do the renumber first (see the commit
  sequence in section 8) or the file will not compile.

This achieves the stated objective in full: no second `file_operations`, no second
`register_driver()`, no second device node, no second file - everything goes through the
Meadow UPD driver.

## 4. Implementation steps

### 4.1 Retire the HCOM driver

- Delete `F7MeadowOS/hcom_nx/hcom_nx_upd.c` in its entirety. Its contents are relocated by
  step 4.2; nothing in it survives in `hcom_nx/`.
- Drop it from the `CONFIG_MEADOW_HCOM` source list at `src/Make.defs:104`.
- Delete the `hcom_nx_upd_initialize()` prototype from
  `F7MeadowOS/hcom_nx/hcom_nx_common.h:131`. No replacement prototype is added - after the
  merge there is no cross-module entry point, only `upd_ioctl()`'s own static handlers.
- `g_hcom_nx_operations`, `hcom_upd_nx_open()`, `hcom_upd_nx_close()`, `hcom_upd_nx_read()`
  and `hcom_nx_upd_initialize()` simply go away; the merged driver already has, or gains,
  the equivalents.
- Leave `hcom_nx_upd_route_cmd.c` where it is. It is reached through
  `hcom_nx_route_in_bound_cli_command()`, whose prototype in `hcom_nx_common.h:141` is
  unchanged, and it needs no edits. Optionally rename it to `hcom_nx_cli_route_cmd.c` to
  drop the now-misleading `upd` in the name - cosmetic, and best done as its own commit if
  at all.

`git mv` cannot help here - the destination file already exists - so the deletion and the
relocation land as one commit that removes `hcom_nx_upd.c` and grows `meadow-upd.c`. State
in the commit message that the HCOM cases moved verbatim apart from the two fixes called
out in 4.2, so a reviewer knows what to look for in an otherwise large diff.

### 4.2 Move the HCOM commands into the Meadow driver

All of this is in `F7MeadowOS/meadow_os/meadow-upd.c`.

Includes and file-level data:

- Add the includes listed in section 3, adjusting the espcp paths from `"../espcp/..."` to
  `"espcp/..."`. `hcom_nx/hcom_nx_common.h` and `hcom_nx/hcom_nx_config_manager.h` are
  already there.
- Do **not** carry `hcom_nx_upd.c`'s `static char *thisFile = __FILE__;` across, and do not
  add one to `meadow-upd.c`. Every `thisFile` reference in the moved code becomes `__FILE__`
  used directly in the `syslog()` call. The format specifier stays `%s` and the output is
  identical, so no log-parsing behaviour changes; the file-scope static and its
  `-Wunused-variable` exposure just go away.

File operations:

- Add a `.read = upd_read` handler to `g_driver_operations` that returns `OK`, mirroring
  `hcom_nx_upd.c:128`, so the merged driver accepts the `O_RDONLY` open that
  `hcom_via_nx_upd_driver_open()` performs - matching the HCOM driver's behaviour exactly.
  See 4.2.1 for why this is belt-and-braces rather than load-bearing on this NuttX version.
- Keep the `open()` flags in `hcom_via_nx_access.c` as `O_RDONLY`, and keep
  `register_driver()`'s mode at `0666`. Neither changes.

The switch:

- Move every `HCOM_NX_UPD_*` case from `hcom_nx_upd_ioctl()` into `upd_ioctl()`, as one
  contiguous block after the existing `MUPD_*` cases, under a banner comment marking where
  the HCOM command set starts.
- Wrap that block, the moved helpers, and the HCOM-only includes in
  `#if defined(CONFIG_MEADOW_HCOM)`. The build system no longer provides this guard for
  the code once it lives in `meadow-upd.c` (section 3), so the file has to carry it.
- Move `hcom_nx_get_mcu_ser_numb()` and `hcom_nx_restore_uart_reconfig()` across as
  `static` functions, unchanged apart from the guard. Keep the names - they stay
  greppable against the deleted file's history.
- Fix the two fall-throughs from finding 2.4 during the move: add the missing `return ret;`
  to `RTC_WAKEUP_TIME`, and move `UPDATE_OS1`'s `return` outside the
  `#if (MEADOW_INCLUDE_CODE_FOR_TESTING_5MB_OF_FLASH == 0)`. These are the only two
  behavioural changes in the move; call them out in the commit message.
- Replace the bare `return ERROR;` at the end of `upd_ioctl()` with a single `default:`
  that logs once and returns `ERROR`, replacing both drivers' old unknown-command paths:

  ```c
    default:
      syslog(LOG_ERR, "%s@%d-unknown upd command:%d\n", __FILE__, __LINE__, cmd);
      return ERROR;
  ```

  Note this is a small behaviour change on the `MUPD_*` side: `upd_ioctl()` currently falls
  off the end of the switch and returns `ERROR` silently, so an unrecognised `MUPD_*`
  command now produces a log line where it produced none before. That is the intended
  outcome - it is how the HCOM driver already behaved - but it is worth knowing if a
  chatty log appears from the .NET client.
- Leave every existing `MUPD_*` case, handler and return value untouched, including the
  positive-`errno` returns (finding 2.5).

#### 4.2.1 Note on the `read` handler and `O_RDONLY`

The comment at `hcom_nx_upd.c:95` - "Added read functionality because open failed without
it. The open flag O_RDONLY == 0" - is not true of this NuttX tree, and it should not be
carried across into the merged driver as justification.

- `O_RDONLY` is `(1 << 0)`, not 0 (`nuttx/include/fcntl.h:41`).
- The access check at `nuttx/fs/vfs/fs_open.c:84-88` reads:

  ```c
    if (((oflags & O_RDOK) != 0 && !ops->readv && !ops->read && !ops->ioctl) ||
        ((oflags & O_WROK) != 0 && !ops->writev && !ops->write && !ops->ioctl))
      {
        return -EACCES;
      }
  ```

  A present `ioctl` method satisfies *both* the read and the write branch. So a driver
  exposing only `ioctl` - which is exactly what `meadow-upd.c` does today - can already be
  opened `O_RDONLY`, `O_WRONLY` or `O_RDWR`.

The `read` stub is therefore not load-bearing on this NuttX version, and an earlier draft
of this plan was wrong to state that HCOM's opens would fail without it. It is still worth
adding: it costs nothing, it makes the merged driver behave identically to the HCOM driver
it replaces, and it keeps the node openable read-only if the VFS access check tightens on a
future rebase. Replace the stale comment with a one-line note saying the stub exists to
keep `O_RDONLY` opens valid independently of the `ioctl` method.

This also means no `write` method is needed, whatever flags the external .NET client uses
to open `/dev/upd`.

### 4.3 Register the merged driver unconditionally, in one place

`/dev/upd` is registered unconditionally. It is no longer an optional Mono-only device: it
is the only path user space has into the kernel, so nothing may gate its existence.

- `nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_boot.c:262-268`: remove the
  `#ifdef CONFIG_EXAMPLES_MONO` wrapper around the `meadow_upd_initialize()` call
  (finding 2.2). This is the surviving registration site. Do not replace the removed
  `#ifdef` with a different config guard - `CONFIG_MEADOW_HCOM` included. The driver is
  registered for every build of this board; only the HCOM *command set* behind it is
  conditional, and that is handled inside `upd_ioctl()`'s `default:` branch (step 4.2).
- Promote the failure log there from `LOG_ERR` to `LOG_CRIT` and return, matching how
  `hcom_nx_startup_mgr.c:301-305` treats the same failure today - after the merge a failed
  registration means HCOM cannot run at all, so continuing the boot silently is wrong.
- `F7MeadowOS/hcom_nx/hcom_nx_startup_mgr.c:299-305`: delete the `hcom_nx_upd_initialize()`
  call block, leaving the surrounding `HCOM_DIAG_INCLUDE_NX_STARTUP_SYSLOG` markers
  consistent (renumber or leave the gap, whichever the file's convention favours).
- Confirm nothing else re-registers the node: after this change `register_driver` should
  appear exactly once for `/dev/upd` across the tree.

### 4.4 Renumber the HCOM command space

In `F7MeadowOS/include/meadow/hcom_upd_shared.h`, replace the whole `HCOM_NX_UPD_*` block
with a fresh, contiguous allocation based at `0x7000` (see finding 2.1.1 for why that
value). The old numbers are not preserved in any form - nothing outside this repository
consumes them, so carrying the historical values and their retired-value gaps forward
would only preserve the confusion. Values are grouped by function with room to grow inside
each group, so future additions do not have to be appended at the end.

```c
  /****************************************************************************
   * ioctl command number allocation for /dev/upd
   *
   *   0x0000 - 0x00ff   MUPD_*          Meadow UPD commands (meadow-upd.h).
   *                                     Frozen - external .NET dependency.
   *   0x7000 - 0x70ff   HCOM_NX_UPD_*   HCOM kernel-side commands (below).
   *
   * 0x7000 is chosen to sit clear of the ioctl command blocks NuttX reserves
   * in nuttx/include/nuttx/fs/ioctl.h (currently 0x0100 - 0x4600, plus 0x8b00
   * and 0xff00). Some of those - FIONBIO, FIOC_*, BIOC_* - are handled by the
   * VFS in fs_ioctl.c and never reach this driver, so overlapping them would
   * fail in a very confusing way. Re-check this constant on a NuttX rebase.
   ****************************************************************************/

  #define HCOM_NX_UPD_BASE                        0x7000

  /* Battery-backed register access */
  #define HCOM_NX_UPD_SET_BBR_VALUE               (HCOM_NX_UPD_BASE + 0x00)
  #define HCOM_NX_UPD_GET_BBR_VALUE               (HCOM_NX_UPD_BASE + 0x01)
  #define HCOM_NX_UPD_UPDATE_BBR_VALUE            (HCOM_NX_UPD_BASE + 0x02)

  /* Device identity and hardware information */
  #define HCOM_NX_UPD_GET_MCU_ID                  (HCOM_NX_UPD_BASE + 0x10)
  #define HCOM_NX_UPD_GET_MCU_SER_NUMB            (HCOM_NX_UPD_BASE + 0x11)
  #define HCOM_NX_UPD_GET_HW_VERSION              (HCOM_NX_UPD_BASE + 0x12)

  /* File system */
  #define HCOM_NX_UPD_IS_PART_MOUNTED             (HCOM_NX_UPD_BASE + 0x20)

  /* Host / CLI command routing */
  #define HCOM_NX_UPD_CLI_COMMAND                 (HCOM_NX_UPD_BASE + 0x30)
  #define HCOM_NX_UPD_DIAG_APP_CMD                (HCOM_NX_UPD_BASE + 0x31)

  /* Restart, DFU and power management */
  #define HCOM_NX_UPD_HOST_RESTART_MEADOW_MCU     (HCOM_NX_UPD_BASE + 0x40)
  #define HCOM_NX_UPD_ONLY_RESTART_MEADOW_MCU     (HCOM_NX_UPD_BASE + 0x41)
  #define HCOM_NX_UPD_ENTER_INTO_DFU_MODE         (HCOM_NX_UPD_BASE + 0x42)
  #define HCOM_NX_UPD_REG_PWR_MGMT_CB             (HCOM_NX_UPD_BASE + 0x43)

  /* Real-time clock */
  #define HCOM_NX_UPD_RTC_SET_TIME                (HCOM_NX_UPD_BASE + 0x50)
  #define HCOM_NX_UPD_RTC_WAKEUP_TIME             (HCOM_NX_UPD_BASE + 0x51)

  /* OS update / OTA */
  #define HCOM_NX_UPD_UPDATE_OS1                  (HCOM_NX_UPD_BASE + 0x60)
  #define HCOM_NX_UPD_UPDATE_OS2                  (HCOM_NX_UPD_BASE + 0x61)
  #define HCOM_NX_UPD_GET_UPDATE_FLAG             (HCOM_NX_UPD_BASE + 0x62)
  #define HCOM_NX_UPD_SET_UPDATE_FLAG             (HCOM_NX_UPD_BASE + 0x63)
  #define HCOM_NX_UPD_COPY_RUNTIME_TO_RAM         (HCOM_NX_UPD_BASE + 0x64)

  /* Mono runtime and UART restoration */
  #define HCOM_NX_UPD_MONO_HAS_STARTED            (HCOM_NX_UPD_BASE + 0x70)
  #define HCOM_NX_UPD_RESTORE_UART_CONFIG         (HCOM_NX_UPD_BASE + 0x71)

  /* Kernel-side test hooks */
  #define HCOM_NX_UPD_EXECUTE_ESPCP_TESTS         (HCOM_NX_UPD_BASE + 0x80)
```

That is 23 commands, down from the 29 currently defined. The mapping from old to new,
for review against the existing header:

| Symbol | Old | New |
|---|---|---|
| `HCOM_NX_UPD_SET_REGISTER` | 1 | deleted - duplicate of `MUPD_*`, no caller (4.4.1) |
| `HCOM_NX_UPD_GET_REGISTER` | 2 | deleted - duplicate of `MUPD_*`, no caller (4.4.1) |
| `HCOM_NX_UPD_UPDATE_REGISTER` | 3 | deleted - duplicate of `MUPD_*`, no caller (4.4.1) |
| `HCOM_NX_UPD_SET_BBR_VALUE` | 4 | `0x7000` |
| `HCOM_NX_UPD_GET_BBR_VALUE` | 5 | `0x7001` |
| `HCOM_NX_UPD_UPDATE_BBR_VALUE` | 6 | `0x7002` |
| `HCOM_NX_UPD_CLI_COMMAND` | 7 | `0x7030` |
| `HCOM_NX_UPD_GET_MCU_ID` | 8 | `0x7010` |
| `HCOM_NX_UPD_IS_PART_MOUNTED` | 9 | `0x7020` |
| `HCOM_NX_UPD_RESTORE_UART_CONFIG` | 11 | `0x7071` |
| `HCOM_NX_UPD_GET_MCU_SER_NUMB` | 16 | `0x7011` |
| `HCOM_NX_UPD_EXECUTE_ESPCP_TESTS` | 18 | `0x7080` |
| `HCOM_NX_UPD_ENTER_INTO_DFU_MODE` | 19 | `0x7042` |
| `HCOM_NX_UPD_HOST_RESTART_MEADOW_MCU` | 20 | `0x7040` |
| `HCOM_NX_UPD_ONLY_RESTART_MEADOW_MCU` | 21 | `0x7041` |
| `HCOM_NX_UPD_GET_STRING` | 23 | deleted - no caller, no handler |
| `HCOM_NX_UPD_MONO_HAS_STARTED` | 24 | `0x7070` |
| `HCOM_NX_UPD_GET_HW_VERSION` | 27 | `0x7012` |
| `HCOM_NX_UPD_FLASH_OS_UPDATE` | 28 | deleted - no caller, no handler |
| `HCOM_NX_UPD_DIAG_APP_CMD` | 29 | `0x7031` |
| `HCOM_NX_UPD_RTC_SET_TIME` | 30 | `0x7050` |
| `HCOM_NX_UPD_RTC_READ_TIME` | 31 | deleted - no caller, no handler |
| `HCOM_NX_UPD_RTC_WAKEUP_TIME` | 32 | `0x7051` |
| `HCOM_NX_UPD_UPDATE_OS1` | 33 | `0x7060` |
| `HCOM_NX_UPD_UPDATE_OS2` | 34 | `0x7061` |
| `HCOM_NX_UPD_GET_UPDATE_FLAG` | 35 | `0x7062` |
| `HCOM_NX_UPD_SET_UPDATE_FLAG` | 36 | `0x7063` |
| `HCOM_NX_UPD_COPY_RUNTIME_TO_RAM` | 37 | `0x7064` |
| `HCOM_NX_UPD_REG_PWR_MGMT_CB` | 39 | `0x7043` |

No call site anywhere references a raw number - every one of the 26 ioctl calls in
`hcom_via_nx_access.c` uses the symbolic name - so the renumber itself needs no user-space
edits at all. The only user-space change in this step is deleting the dead
`hcom_via_nx_set_any_reg()` wrapper, below.

Also in this step:

- Delete `HCOM_NX_UPD_SET_REGISTER`, `HCOM_NX_UPD_GET_REGISTER` and
  `HCOM_NX_UPD_UPDATE_REGISTER` outright - see 4.4.1. Also delete the three cases from the
  HCOM handler, `struct hcom_nx_upd_register_value`, `struct hcom_nx_upd_register_update`,
  and the dead `hcom_via_nx_set_any_reg()` wrapper in `hcom_via_nx_access.c:107-123`.
- The three dead defines from finding 2.3 (`GET_STRING`, `FLASH_OS_UPDATE`,
  `RTC_READ_TIME`) simply do not appear in the new block. The `// nn has been deleted`
  comments for the old values 12-15, 22, 25, 26 and 38 go with them - they documented gaps
  in a numbering scheme that no longer exists.

#### 4.4.1 Which HCOM commands have an `MUPD_*` equivalent

Where HCOM duplicates something `MUPD_*` already does, the right move is to remove the
HCOM command rather than carry it into the new range. Working through all 29:

**Register access (old 1, 2, 3) - remove, but by deletion, not substitution.**
These are byte-for-byte duplicates of `MUPD_SET_REGISTER` / `MUPD_GET_REGISTER` /
`MUPD_UPDATE_REGISTER`, down to the struct layout (`hcom_nx_upd_register_value` vs
`upd_register_value` - identical fields, identical order). But they do not need to be
re-pointed at the `MUPD_*` equivalents, because **nothing calls them**:
`HCOM_NX_UPD_GET_REGISTER` and `HCOM_NX_UPD_UPDATE_REGISTER` have no user-space wrapper at
all, and `HCOM_NX_UPD_SET_REGISTER`'s only wrapper - `hcom_via_nx_set_any_reg()`, carrying
a `// NEVER TESTED` comment at `hcom_via_nx_access.c:106` - has zero callers in the tree.
So the whole group, wrapper included, just goes. This is better than substitution: it
avoids having to expose `struct upd_register_value` to user space, which would otherwise
have meant either pulling the kernel-only `meadow-upd.h` into `apps/` or restating the
struct in a second header.

**Battery-backed register access (old 4, 5, 6) - looks like a duplicate, but keep it.**
`HCOM_NX_UPD_SET_BBR_VALUE` and friends are arithmetically identical to the `MUPD_*`
register ops with the address fixed at `HCOM_NX_MEADOW_BATTERY_BACKED_REGISTER`, so the
obvious move is to collapse them the same way. It does not work: that macro expands to
`STM32_RTC_BK31R` (`hcom_bbreg_defn.h:75`), which is defined in
`nuttx/arch/arm/src/stm32f7/hardware/stm32_rtcc.h` - a kernel-internal arch header that is
not on the user-space include path. `apps/` already includes `hcom_bbreg_defn.h` but never
expands the macro, so the dependency is latent and compiles today; the moment user space
has to name the address, it breaks. The alternative - hard-coding the peripheral address
into `apps/` - is not worth it.

There is also a design argument for keeping them. In the PROTECTED build,
`MUPD_SET_REGISTER` is an unrestricted "write any 32-bit word to any address" primitive
handed to user space, which is close to the opposite of what the MPU is there for. The BBR
commands are the better-shaped pair: the kernel owns the address and user space names the
operation. That is the direction the eventual syscall conversion should move in, so
deleting the register trio (above) and keeping the BBR trio narrows the hole slightly
rather than widening it. `MUPD_*`'s own register commands stay only because that ABI is
frozen.

**Restart (old 20, 21) - not duplicates of `MUPD_PWR_RESET`.** Three distinct behaviours:
`MUPD_PWR_RESET` is a bare `up_systemreset()`; `HCOM_NX_UPD_ONLY_RESTART_MEADOW_MCU` adds
a 500 ms delay so an in-flight host message can drain; `HCOM_NX_UPD_HOST_RESTART_MEADOW_MCU`
additionally sets `HCOM_BBREG_RESTART_INITIATED_BY_HOST_CMD_BIT` so HCOM knows on the next
boot to send a completion message (`hcom_nx_common_utils.c:74-95`). All three stay.

**The remaining 17 have no `MUPD_*` counterpart** - MCU ID and serial number, hardware
version, partition-mounted, CLI and diagnostic routing, DFU entry, power-management
callback registration, RTC set/wakeup, the five OTA commands, Mono-started, UART restore,
and the espcp test hook. Nothing to collapse; these are the commands the renumber exists
for.

Net effect: 29 defined commands become 23 - three deleted as unreachable duplicates, three
deleted as never-implemented (finding 2.3).

### 4.5 Point the user-space client at the surviving node

- Give the node name a single definition. Both sides already include
  `meadow/hcom_shared_common.h`, so put `#define MEADOW_UPD_DRIVER_NAME "/dev/upd"` there
  and have both `meadow-upd.c:770` and `hcom_upd_shared.h:52` use it.
- Replace `HCOM_NX_UPD_DRIVER_NAME` with `MEADOW_UPD_DRIVER_NAME` throughout
  `apps/system/hcom/nx_rqsts/hcom_via_nx_access.c` (26 references, all in `open()` calls
  and log strings). No other file in `apps/` references the name.
- No signature changes to any `hcom_via_nx_*()` wrapper, so the rest of `apps/system/hcom/`
  is untouched.

### 4.6 Documentation and tracking

- `Documentation/Kernelspace.md`: the "Universal Platform Driver (`hcom_nx_upd.c`)" section
  at line 68 describes a file that no longer exists - retarget it at `meadow-upd.c` and
  `/dev/upd`. The startup step at line 44 currently says the kernel "registers the
  Universal Platform Driver (`hcom_nx_upd`) syscall endpoint" as part of
  `hcom_nx_setup_mgr()`; registration moves to `board_late_initialize()` (step 4.3), so
  that line moves too. Update the "OTA Update Router" heading at line 76 only if
  `hcom_nx_upd_route_cmd.c` is renamed.
- `Documentation/Architecture.md:136-138`: the table has separate rows for `hcom_nx_upd.c`
  and `hcom_nx_upd_route_cmd.c`. Drop the first, keep the second, and add or amend a row
  for `meadow-upd.c` as the single Universal Platform Driver.
- `Documentation/CodeReviewTracking.md`: delete row C053 (`hcom_nx_upd.c`) since the file
  is gone, leave C054 (`hcom_nx_upd_route_cmd.c`) unless it is renamed, and reset C123
  (`meadow-upd.c`), C066 (`meadow-upd.h`) and the `hcom_upd_shared.h` row to "Not started"
  - all three change materially here, so any prior review of them is stale. Note the file
  count in the "Scope" section drops by one.
- `Documentation/RunningDeveloperTests.md:320` references `upd_handle_pwm()` by file - the
  path does not change, so this only needs a read-through.

## 5. What deliberately does not change

- The `MUPD_*` values, structures and handler behaviour, including the positive-`errno`
  returns. The external ABI is frozen.
- `hcom_nx_upd_route_cmd.c` and the `HCOM_NX_UPD_CLI_COMMAND` routing path.
- The kernel test suite's direct calls to `upd_handle_pwm()`,
  `upd_handle_esp32_command()` and `upd_handle_esp32_get_event_result()`
  (`F7MeadowOS/kerneltests/meadow_pwm_tests.c`, `espcp_tests.c`,
  `meadow_kt_bluetooth_tests.c`) - these bypass the ioctl layer entirely.
- The `hcom_via_nx_*()` user-space API surface in `apps/system/hcom/hcom_common.h`.

## 6. Pre-existing defects: disposition

Fix as part of this work (they are inside the code being moved, and carrying them forward
would be knowingly shipping a bug):

- The two switch fall-throughs (finding 2.4).

Do not fix here; raise as separate items so this change stays reviewable as a consolidation:

- The four live-but-unhandled ioctls (finding 2.3). Recommend confirming with the owner
  whether these regressed during the rebase; if they need restoring, do it in a follow-up
  change against the consolidated driver. In the meantime keep the defines and add a
  `TODO(<issue>)` comment naming the missing handler, so the gap is visible rather than
  buried in a `default:` branch.
- `MUPD_GET_DEVICE_INFO` (value 81) and `struct upd_device_info` are defined but have no
  case and no user - same treatment.
- `upd_handle_dir_enum()` (`meadow-upd.c:250`) never advances `len`, so its overflow guard
  never fires, and it `strcat`s onto a caller buffer it never terminates. Real bug, but
  outside the merge; raise separately.
- The unreachable `return EINVAL;` at `meadow-upd.c:239`.

## 7. Verification

Build commands used below: `./build.sh --clean --force` for a full build, plain
`./build.sh` for an incremental one.

1. `./build.sh --clean --force` - must build with zero new warnings (`src/Make.defs` sets
   `-Werror`, so a stale prototype or an unused `static` will hard-fail the build). Use
   plain `./build.sh` while iterating; do a full build before calling any commit done,
   since deleting `hcom_nx_upd.c` leaves a stale object that only a clean build clears.
2. `nuttx/tools/checkpatch.sh -f <file>` on every touched file, and fix pre-existing lint
   in those files per `CLAUDE.md`.
3. Confirm the node: boot and check `/dev/upd` exists and `/dev/nxupd` does not
   (`ls /dev` from NSH, `./build.sh --nsh`).
4. Confirm the `O_RDONLY` open path works - startup reaching
   `hcom_via_nx_upd_setup()` (`hcom_startup_manager.c:175`) without the
   "Failed to open" log is the gate for everything else.
5. Exercise one command from each group over the real transport:
   - BBR: any host command that reads/writes the battery-backed register.
   - CLI routing: a trace-level change (`HCOM_MDOW_REQUEST_SEND_TRACE_TO_HOST`).
   - Identity: MCU ID and serial number, and HW version (also on the Mono thread's
     separate fd via `hcom_via_nx_get_hw_version_alt()`).
   - Restart: `hcom_via_nx_only_restart_meadow()`.
   - OTA: `hcom_via_nx_update_OS1()` / `_OS2()` and
     `hcom_via_nx_copy_mono_runtime_to_ram()` (this one opens its own fd -
     `hcom_via_nx_access.c:463`).
6. Regression-check the `MUPD_*` side against a real .NET application: I2C, SPI, PWM and a
   GPIO interrupt registration, since those are the commands whose *numbers* now share a
   node with HCOM's. A wrong renumber shows up here as a plausible-looking wrong action
   (e.g. an OS update staged by an SPI call), so this test is the one that must not be
   skipped.
7. `./build.sh -u=all` and run the on-target suite - covers the direct-call handlers that
   bypass ioctl (`upd_handle_pwm()`, `upd_handle_esp32_command()`,
   `upd_handle_esp32_get_event_result()`) as well as everything else.
8. Build with `CONFIG_MEADOW_HCOM=n` if that configuration is still meant to be
   supported. Note that `meadow-upd.c` already calls `hcom_nx_config_get_set_config_value()`
   unconditionally, so this configuration may already be broken; if so, record that rather
   than expanding scope to fix it.

## 8. Risks

| Risk | Mitigation |
|---|---|
| A renumbered HCOM command silently aliases an `MUPD_*` command and triggers the wrong hardware action | Renumber into the `0x7000` block so no overlap is arithmetically possible; verification step 6 |
| A renumbered HCOM command collides with a NuttX ioctl the VFS intercepts, and never reaches the driver | `0x7000` is clear of every base in `nuttx/include/nuttx/fs/ioctl.h`; re-check on a NuttX rebase (finding 2.1.1) |
| An out-of-repo consumer opens `/dev/nxupd` (contradicting the assumption that only HCOM does) | Confirm with the team before deleting the node; if one exists, keep `/dev/nxupd` registered as a second name onto the same `file_operations` for one release |
| Meadow.Core's ioctl numbers do not actually match `meadow-upd.h` as assumed | Diff `meadow-upd.h` against the .NET P/Invoke constants before starting step 4.4 |
| Registration moves earlier in boot (`stm32_boot.c` rather than the HCOM startup manager) | Nothing in kernel space issues these ioctls; the first client is user-space HCOM, which starts much later. Verified by inspection - both call sites are already in `board_late_initialize()` |
| The change is large enough to obscure a behaviour change in review | Sequence the commits as below. Each builds and boots on its own |
| The single merged file becomes a dumping ground and drifts back into two concerns | Banner-delimited HCOM section, moved helpers kept together, and the `CONFIG_MEADOW_HCOM` guard around the block make the boundary explicit in the file itself |

### 8.1 Commit sequence

Two ordering constraints:

- The renumber has to land before the move, or `upd_ioctl()`'s single switch will not
  compile (section 3).
- Unconditional registration has to land before the move, not after. Deleting `/dev/nxupd`
  while `/dev/upd` is still behind `CONFIG_EXAMPLES_MONO` leaves a commit that compiles but
  gives HCOM no node to open - the board would build and boot but not talk to the host.

1. **Renumber `HCOM_NX_UPD_*` into the `0x7000` block, and delete the unreachable register
   commands and dead `hcom_via_nx_set_any_reg()` wrapper** (steps 4.4 and 4.4.1). These
   cannot be separated: removing `HCOM_NX_UPD_SET_REGISTER` while its wrapper still
   references it does not compile. Header and removals only, no behaviour change, and no
   user-space call site changes because every one uses a symbolic name.
2. **Register `/dev/upd` unconditionally** (step 4.3, `stm32_boot.c` half only).
   `/dev/nxupd` is untouched and HCOM still uses it, so this stands alone safely.
3. **Move the HCOM commands into `meadow-upd.c`, delete `hcom_nx_upd.c`, drop the second
   registration, and point the user-space client at `/dev/upd`** (steps 4.1, 4.2, the
   startup-manager half of 4.3, and 4.5), including the two fall-through fixes. This is the
   large commit and the one that changes which node HCOM talks to, so it is the one to
   bisect to if host communication breaks.
4. **Documentation and review-tracking updates** (step 4.6).

The steps in section 4 are grouped by subject rather than by commit; the mapping above is
the one to follow.

## 9. Follow-on work (out of scope, stated for direction)

This consolidation is step one toward replacing the driver with syscalls. It leaves the
codebase in the right shape for that: every command user space can reach is now a single
`case` in a single `switch` in a single file, with its argument marshalling immediately
alongside it. The next step is to convert commands to board syscalls a group at a time (see
the `nuttx-add-syscall` skill for the mechanics), starting with the HCOM set - it has no
external ABI constraint - and leaving the `MUPD_*` set on the ioctl path until the external
clients can be moved. Each converted group deletes its `case` from `upd_ioctl()`, so the
switch shrinks steadily toward the point where the driver can be removed altogether.

---

# Implementation Results

Implemented on branch `ConsolidateUpdDrivers`, in the five commits below plus a sixth
carrying this results section. All local - nothing pushed.

| Commit | Subject |
|---|---|
| `ecc3b89` | Add implementation plan for consolidating the UPD drivers |
| `5151b41` | Renumber the HCOM UPD ioctl commands into their own range |
| `4bfbbfc` | Register the Meadow UPD driver unconditionally |
| `d567d70` | Merge the HCOM UPD driver into the Meadow UPD driver |
| `2c9065a` | Update documentation for the consolidated UPD driver |

## What landed

- `hcom_nx/hcom_nx_upd.c` deleted (348 lines). `/dev/nxupd` no longer exists.
- All 23 HCOM commands moved into `upd_ioctl()` in `meadow_os/meadow-upd.c` (+229 lines),
  behind `#if defined (CONFIG_MEADOW_HCOM)`, in a banner-delimited block after the `MUPD_*`
  cases. The two moved helpers are now `static` and use `__FILE__` directly.
- `HCOM_NX_UPD_*` renumbered into the `0x7000` block, grouped by function. Six commands
  removed: the three unreachable register duplicates and the three defined-but-unhandled
  dead entries. 29 commands became 23.
- `HCOM_NX_UPD_DRIVER_NAME` is now an alias for `MEADOW_UPD_DRIVER_NAME`, which is defined
  once in `hcom_shared_common.h`. No user-space call site changed.
- `/dev/upd` registration moved out of `#ifdef CONFIG_EXAMPLES_MONO`, promoted to
  `LOG_CRIT` with an early return on failure. `hcom_nx_setup_mgr()` no longer registers a
  driver.
- Both switch fall-through defects from finding 2.4 fixed during the move.
- `read` handler added to `g_driver_operations`.
- `Kernelspace.md`, `Architecture.md` and `CodeReviewTracking.md` updated.

## Deviations from the plan

1. **Steps 4.4 and 4.4.1 became one commit.** They cannot be separated - removing
   `HCOM_NX_UPD_SET_REGISTER` while `hcom_via_nx_set_any_reg()` still references it does
   not compile. Section 8.1 has been corrected.
2. **Unconditional registration moved ahead of the move, not after it.** The original
   sequence put step 4.3 last, which would have produced a commit where `/dev/nxupd` was
   gone but `/dev/upd` was still behind `CONFIG_EXAMPLES_MONO`: it compiles, but HCOM has
   no node to open, so the board boots without host communication. Section 8.1 has been
   corrected.
3. **`CodeReviewTracking.md` needed no status resets.** Step 4.6 anticipated resetting
   C123, C066 and the `hcom_upd_shared.h` row; all affected rows were still "Not started".
   Only row C053 (deleted file) and the file counts changed - 169 to 168 for `F7MeadowOS/`,
   255 to 254 overall.

## Defect found during implementation

`meadow_upd_initialize()` had **no prototype anywhere in the tree**. Making the call
unconditional turned it into an implicit declaration, which `-Werror` rejects. Nothing had
ever caught it because the sole call site was inside the disabled `#ifdef` - so this would
have been a hard build failure for whoever next enabled `CONFIG_EXAMPLES_MONO` to integrate
Mono. Declared in `include/meadow/stm32f777zit6-meadow.h` alongside the other board
initialisation entry points, in the same commit that exposed it.

## Verification performed

| Check | Result |
|---|---|
| `./build.sh --clean --force` | Passes. Exit 0, zero errors |
| Warnings in touched files | None. The 21 remaining warnings are all vendored `mbedtls`, plus one Kconfig dependency warning - all pre-existing |
| Each code commit builds standalone | Verified by checking out `5151b41`, `4bfbbfc` and `d567d70` in turn and building each. All three clean |
| Command-number collisions | Scripted check across both headers: 23 `MUPD_*` + 23 `HCOM_NX_UPD_*` = 46 distinct values, no duplicates |
| Dangling references | No remaining references to `hcom_nx_upd_initialize`, `g_hcom_nx_operations` or `/dev/nxupd` outside intentional historical comments |
| `register_driver` for the node | Appears exactly once in the tree |
| Artifacts | `nuttx.bin`, `nuttx_user.bin`, `Meadow.OS.bin`, `Meadow.OS.Update.bin` all produced |

### On the checkpatch gate

Section 7 step 2 called for `checkpatch.sh -f` on every touched file with pre-existing lint
fixed, per `CLAUDE.md`. That is not achievable on these files: they use `//` comments and
camelCase throughout, which NuttX's checkpatch rejects wholesale, so the baseline error
counts run from 256 to 1970 per file. Fixing them would mean rewriting the files and would
bury the actual change.

The standard applied instead was **introduce no new errors**, measured by running checkpatch
against the `HEAD` version and the modified version of each file:

| File | Before | After |
|---|---|---|
| `hcom_upd_shared.h` | 110 | 70 |
| `hcom_via_nx_access.c` | 1970 | 1900 |
| `hcom_common.h` | 354 | 354 |
| `meadow-upd.c` | 256 | 638 |

`meadow-upd.c` rising is the ~230 lines moved in from `hcom_nx_upd.c` bringing their
existing style with them; `hcom_nx_upd.c`'s own count disappears at the same time. Every
other file went down or stayed level. Worth a separate decision on whether this codebase
should adopt a checkpatch configuration that matches its actual house style, since the
gate as documented cannot currently be met by any change.

## Not done - still open

- **On-target verification.** Section 7 steps 3 to 7 need real hardware: node presence,
  the `O_RDONLY` open path, one command per group over the real transport, the `MUPD_*`
  regression check against a real .NET application, and `./build.sh -u=all` with the
  on-target suite. Step 6 is the one that must not be skipped - a wrong renumber shows up
  there as a plausible-looking wrong action rather than an error.
- **Confirm no out-of-repo consumer opens `/dev/nxupd`** (section 8 risk row). The node is
  now gone.
- **Confirm Meadow.Core's ioctl constants match `meadow-upd.h`** (section 8 risk row).
  Untouched by this work, but the assumption underpins the whole renumbering decision.
- **The four live-but-unhandled ioctls** from finding 2.3 - `EXECUTE_ESPCP_TESTS`,
  `DIAG_APP_CMD`, `GET_UPDATE_FLAG`, `SET_UPDATE_FLAG`. Still called from user space, still
  no kernel handler, still silently failing. Deliberately out of scope per section 6;
  needs a decision from the owner on whether these regressed during the rebase.
- **`CONFIG_MEADOW_HCOM=n` build** (section 7 step 8) not attempted. `meadow-upd.c` still
  calls `hcom_nx_config_get_set_config_value()` unconditionally, so that configuration was
  most likely already broken before this change.
- **The other pre-existing defects** listed in section 6: `upd_handle_dir_enum()`'s
  non-advancing `len`, the unreachable `return EINVAL;`, and `MUPD_GET_DEVICE_INFO`.
