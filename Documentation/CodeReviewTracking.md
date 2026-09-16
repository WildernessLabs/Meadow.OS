# Code Review Tracking

Tracks progress of an ongoing file-by-file bug/concurrency/performance review across the
three source locations below. Reviews are performed with the `review-files` Claude Code
skill (`.claude/skills/review-files/SKILL.md`), a few files at a time.

**Review timeline:** started 2026-08-13, last updated 2026-09-15. Tables A, B, and C are
all now fully reviewed (excluding Mark's `F7MeadowOS/espcp/` rows and all Cell-owned rows,
out of scope -- see the Original Author note below). B21/B22
(`hcom_esp32_network_monitor.{c,h}`) remain Mark's and out of scope; B19/B20/B23-B26 were
reassigned to Peter and reviewed 2026-09-15.

**"Cell Owner" placeholder (added 2026-09-14):** cellular-modem-related files
(`apps/system/hcom/cell/`, `apps/system/hcom/tests/cell_tests.c`,
`F7MeadowOS/kerneltests/bg77_tests.{c,h}`) are attributed to a generic "Cell Owner" rather
than a specific individual -- this is a feature/subsystem-based ownership designation, not
a real person. Cell-owned files are out of scope for this review effort going forward,
same as Mark's esp32/espcp rows: don't pick up new ones, and existing filed issues stay
open for that team to handle.

## Scope

- `nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/` (19 files) - Meadow's board port,
  not vendored NuttX core. Plus `include/board.h` and `include/boardctl.h` (2 files,
  added 2026-09-01) from the same board port's `include/` directory. 4 files
  (`stm32_appinit.c`, `stm32_i2c.c`, `stm32_i2c.h`, `stm32_sdmmc.c`) were removed
  2026-09-01 (PR #425): confirmed unreferenced by any build config -- wrong-MCU/wrong-layer
  copy-paste leftovers or superseded by another file, no remaining project value.
- `apps/system/hcom/` (63 files) - Meadow's user-space HCOM protocol handler.
- `F7MeadowOS/` (161 files) - Meadow's kernel-space extensions. `F7MeadowOS/libyaml/` and
  `F7MeadowOS/libcyaml/` are excluded (22 files): vendored third-party code that
  `Claude.md` marks "do not modify", so reviewing them for fixes here isn't useful.
  `F7MeadowOS/timers/` (7 files) was removed 2026-08-29: experimental MCU-timer code,
  never wired into any build config, no remaining project value.

Total in scope: 245 files.

## Status legend

- **Not started** - not yet reviewed.
- **Reviewed** - passed through `review-files`; see the `Issue` column for that file's
  findings (a `code-review`-labeled GitHub Issue), or `Notes` for a quick remark (e.g.
  "No confirmed findings" or a cross-reference to another file's issue).
- **Reviewed (light)** - same as **Reviewed**, but done at reduced depth (fewer parallel
  reviewer agents, no per-finding confidence-scoring swarm) because the file is
  rarely-exercised test/diagnostic code rather than a production path.
- **Blocked (runtime)** - reviewed and findings filed, but fixes can't be built/exercised
  and verified today because the code path depends on the Mono runtime, which isn't yet
  integrated into Meadow.OS (see `mono_not_yet_in_meadow_os` project context). Deferred
  until Mono build support lands rather than fixed blind.

A `*` prefix on a Status value (e.g. `*Reviewed`, `*Fixed`) marks a row that has been through
the follow-up fix-and-decide pass, not just the initial batch review: its GitHub issue is
either closed, or intentionally left open with an explicit disposition recorded (fixed but
untested/blocked, won't-fix, deferred, or handed off to another owner). An unmarked
`Reviewed`/`Blocked (runtime)` row still just means findings were filed -- nobody has acted
on it yet.

This file is updated automatically by the `review-files` skill after each batch. If you
review a file some other way, update its row manually to keep this list accurate.

## nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/ (21 files)

4 files removed 2026-09-01 (PR #425), rows retired rather than renumbered: A03
(`stm32_appinit.c`), A11 (`stm32_i2c.c`), A12 (`stm32_i2c.h`), A18 (`stm32_sdmmc.c`) --
each confirmed unreferenced by any build config; see the Scope section above for why.

| ID | File | Status | Date | Issue | Owner | Notes |
|----|------|--------|------|-------|-------|-------|
| A01 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/board_corecomp.c | Reviewed | 2026-09-01 | #443 | Mark | Meadow-specific board variant, no upstream equivalent |
| A02 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/board_projlab.c | Reviewed | 2026-09-01 | #442 | Mark | Meadow-specific board variant, no upstream equivalent |
| A04 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_appinitialize.c | Reviewed | 2026-09-01 |  | Peter | No confirmed findings; generic NuttX board_app_initialize() boilerplate |
| A05 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_autoleds.c | *Reviewed | 2026-09-16 | #433 | Peter | Derived from stm32f769i-disco; near-identical to upstream NuttX 12.13's own copy of this board. Stale "Active High" comment finding Won't Fix -- whole file is dead code, `CONFIG_ARCH_LEDS` structurally unselectable here (`depends on ARCH_HAVE_LEDS`, never selected by this board's Kconfig, confirmed same in Legacy, current, and upstream 12.13); kept (not deleted) in case ARCH_LEDS is wired up in the future; issue closed |
| A06 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_boot.c | *Reviewed | 2026-09-01 | #429 | Peter | Derived from stm32f769i-disco (see #426) |
| A07 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_bringup.c | *Reviewed | 2026-09-16 | #430 | Peter | Duplicate stm32_i2c_register()/stm32_i2ctool() block removed (branch `issue430-bringup-dedupe-i2ctool`); root cause traced via git blame to a botched Apr 2026 subtree-merge conflict resolution, confirmed not present in upstream NuttX 12.13's own copy of this board; hardware-verified; issue closed |
| A08 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_buttons.c | Reviewed | 2026-09-01 |  | Peter | No confirmed findings; near-unmodified copy of stm32f769i-disco, dead code (CONFIG_ARCH_BUTTONS unset everywhere) |
| A09 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_composite.c | *Reviewed | 2026-09-01 | #440 | Peter | Derived from nucleo-f767zi |
| A10 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_extmem.c | *Reviewed | 2026-09-16 | #431 | Peter | Both findings fixed (branch `issue431-extmem-clock-comment-fix`): stale 90MHz clock comment corrected to a symbolic STM32_HCLK_FREQUENCY reference (real value 108MHz), 3 magic numbers replaced with named constants; SDRAM chip identified as ISSI IS42S16160J; hardware-verified; issue closed. Separately-found refresh-counter bug (also derived from the wrong 90MHz assumption) filed as #594, lower priority, no correctness risk |
| A13 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_idle.c | Reviewed | 2026-09-01 | #436 | Peter | Meadow's own carried-forward Legacy code |
| A14 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_ioctl.c | Reviewed | 2026-09-01 | #441 | Peter | Meadow's own restored file (Issue200 precedent) |
| A15 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_ostest.c | Reviewed | 2026-09-01 | #439 | Peter | Meadow's own carried-forward Legacy code |
| A16 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_pwm.c | Reviewed | 2026-09-01 | #438 | Peter | Meadow's own carried-forward Legacy code |
| A17 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_reset.c | Reviewed | 2026-09-01 |  | Mark | No confirmed findings; Meadow's own restored file |
| A19 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_spi.c | Reviewed | 2026-09-01 |  | Peter | No confirmed findings; near-unmodified copy of stm32f769i-disco aside from CONFIG_STM32F7_* naming (see #426); no in-tree caller of its select/cmddata stubs found |
| A20 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_sporadic.c | *Fixed | 2026-09-16 | #434 | Peter | File deleted (dead code: `GPIO_SCHED_HIGHPRI`/`GPIO_SCHED_RUNNING` never defined, a bug inherited from the upstream stm32f769i-disco template; `CONFIG_SPORADIC_INSTRUMENTATION`/`CONFIG_SCHED_SPORADIC` never enabled on this board, which uses round-robin scheduling); build-verified; see branch `issue434-remove-dead-sporadic`; issue closed |
| A21 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_usb.c | Reviewed | 2026-09-01 | #435 | Peter | Derived from nucleo-f767zi (see also #426) |
| A22 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_usbmsc.c | Reviewed | 2026-09-01 | #437 | Peter | Meadow's own carried-forward Legacy code |
| A23 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_userleds.c | Reviewed | 2026-09-01 | #432 | Peter | Derived from stm32f769i-disco |
| A24 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/include/board.h | Reviewed | 2026-09-01 | #427 | Peter | In include/, not src/; see also #426 (upstream watch item) |
| A25 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/include/boardctl.h | *Reviewed | 2026-09-16 | #428 | Peter | Macro parenthesization fixed (branch `issue428-boardctl-macro-parens`); genuinely Meadow-only file (no upstream/sibling equivalent), bug present unchanged since 2020; issue closed |

## apps/system/hcom/ (63 files)

| ID | File | Status | Date | Issue | Owner | Notes |
|----|------|--------|------|-------|-------|-------|
| B01 | apps/system/hcom/cell/cmux/hcom_cmux.c | Reviewed | 2026-08-27 | #333 | Cell Owner |  |
| B02 | apps/system/hcom/cell/cmux/hcom_cmux.h | Reviewed | 2026-08-27 | #334 | Cell Owner |  |
| B03 | apps/system/hcom/cell/hcom_pppd.c | Reviewed | 2026-08-27 | #332 | Cell Owner | Confirmed not in HCOM_SRCS; wouldn't compile if re-enabled as-is |
| B04 | apps/system/hcom/cell/hcom_pppd.h | Reviewed | 2026-08-27 |  | Cell Owner | No confirmed findings |
| B05 | apps/system/hcom/comms/hcom_host_encode.c | *Reviewed | 2026-08-13 | #300 | Peter |  |
| B06 | apps/system/hcom/comms/hcom_host_enq_deq.c | *Reviewed | 2026-08-13 | #301 | Peter |  |
| B07 | apps/system/hcom/comms/hcom_host_preprocess.c | *Reviewed | 2026-08-13 | #302 | Peter | Issue #301 (hcom_host_enq_deq.c) |
| B08 | apps/system/hcom/comms/hcom_host_receive.c | *Reviewed | 2026-08-13 | #303 | Peter | Issue #301 (hcom_host_enq_deq.c) |
| B09 | apps/system/hcom/comms/hcom_host_route.c | Reviewed | 2026-08-13 |  | Peter | No findings |
| B10 | apps/system/hcom/comms/hcom_host_send.c | *Reviewed | 2026-08-13 | #304 | Peter | Issue #301 (hcom_host_enq_deq.c) |
| B11 | apps/system/hcom/comms/hcom_host_watchdog.c | Reviewed | 2026-08-13 |  | Peter | Issue #301 (hcom_host_enq_deq.c); Issue #310 (hcom_file_dnld_esp32.c); Issue #311 (hcom_file_dnld_stm32f7.c) |
| B12 | apps/system/hcom/diag/hcom_diag_decode_protocol.c | Reviewed | 2026-08-17 |  | Peter | No confirmed findings |
| B13 | apps/system/hcom/diag/hcom_diag_gpio.c | Reviewed | 2026-08-17 |  | Peter | Issue #305 (hcom_diag_gpio.h) |
| B14 | apps/system/hcom/diag/hcom_diag_gpio.h | *Reviewed | 2026-08-17 | #305 | Peter |  |
| B15 | apps/system/hcom/diag/hcom_diag_misc.c | *Reviewed | 2026-08-17 | #306 | Peter |  |
| B16 | apps/system/hcom/diag/hcom_diag_nsh_support.c | Reviewed | 2026-08-17 |  | Peter | No confirmed findings |
| B17 | apps/system/hcom/diag/hcom_diag_trace_priority.c | *Reviewed | 2026-08-17 | #307 | Peter |  |
| B18 | apps/system/hcom/diag/hcom_traverse_ku_user.c | *Reviewed | 2026-08-17 | #308 | Peter |  |
| B19 | apps/system/hcom/esp32/hcom_esp32_comms.h | *Reviewed | 2026-09-15 | #582 | Peter | Only finding fixed (branch `issue582-esp32-stale-header-paths`); also filed (per-file, redundantly by request) in #583/#584/#587/#585/#586 for execute.c/proc_recv.c/proc_util.c/proc_xmit.c/uart_comms.c; issue closed |
| B20 | apps/system/hcom/esp32/hcom_esp32_execute.c | *Reviewed | 2026-09-15 | #583 | Peter | All 3 findings fixed (branch `issue583-execute-error-code-fixes`); stale header path also tracked centrally in #582 (hcom_esp32_comms.h); issue closed |
| B21 | apps/system/hcom/esp32/hcom_esp32_network_monitor.c | Not started |  |  | Mark |  |
| B22 | apps/system/hcom/esp32/hcom_esp32_network_monitor.h | Not started |  |  | Mark |  |
| B23 | apps/system/hcom/esp32/hcom_esp32_proc_recv.c | *Reviewed | 2026-09-15 | #584 | Peter | 5/6 findings fixed (branch `issue584-proc-recv-leaks-and-fixes`); decodedMsg per-packet-alloc finding Won't Fix -- shared-buffer fix would trade away safety by relying on an implicit single-outstanding-command invariant for a bounded, low-frequency perf cost; stale header path also tracked centrally in #582 (hcom_esp32_comms.h); issue closed |
| B24 | apps/system/hcom/esp32/hcom_esp32_proc_util.c | *Reviewed | 2026-09-15 | #587 | Peter | Only finding fixed (branch `issue582-esp32-stale-header-paths`); also tracked centrally in #582 (hcom_esp32_comms.h); issue closed |
| B25 | apps/system/hcom/esp32/hcom_esp32_proc_xmit.c | *Reviewed | 2026-09-15 | #585 | Peter | All 5 findings fixed (branch `issue585-proc-xmit-fixes`, hardware-verified with 3 file downloads); highest-scored fix also touched hcom_esp32_proc_recv.c (#584, already closed); stale header path also tracked centrally in #582 (hcom_esp32_comms.h); issue closed |
| B26 | apps/system/hcom/esp32/hcom_esp32_uart_comms.c | *Reviewed | 2026-09-15 | #586 | Peter | All 4 findings fixed (branch `issue586-uart-comms-fixes`, hardware-verified including an ESP32 MAC-address read exercising the shutdown-ordering fix); stale header path also tracked centrally in #582 (hcom_esp32_comms.h); issue closed |
| B27 | apps/system/hcom/file/hcom_file_delete_stm32f7.c | Reviewed | 2026-08-19 |  | Peter | No confirmed findings |
| B28 | apps/system/hcom/file/hcom_file_dir_mgmt_utils.c | *Reviewed | 2026-08-19 | #309 | Peter |  |
| B29 | apps/system/hcom/file/hcom_file_dnld_esp32.c | *Reviewed | 2026-08-19 | #310 | Peter |  |
| B30 | apps/system/hcom/file/hcom_file_dnld_stm32f7.c | *Reviewed | 2026-08-19 | #311 | Peter |  |
| B31 | apps/system/hcom/file/hcom_file_lists.c | *Reviewed | 2026-08-19 | #312 | Peter |  |
| B32 | apps/system/hcom/file/hcom_file_lists_subdir.c | Reviewed | 2026-08-19 |  | Peter | No confirmed findings |
| B33 | apps/system/hcom/file/hcom_file_misc.c | *Reviewed | 2026-08-19 | #313 | Peter |  |
| B34 | apps/system/hcom/file/hcom_file_upld_proc.c | *Reviewed | 2026-08-19 | #314 | Peter |  |
| B35 | apps/system/hcom/file/hcom_file_write_stm32f7.c | *Reviewed | 2026-08-19 | #315 | Peter |  |
| B36 | apps/system/hcom/hcom_common.h | *Reviewed | 2026-08-27 | #330 | Peter |  |
| B37 | apps/system/hcom/hcom_startup_manager.c | *Reviewed | 2026-08-27 | #331 | Peter | Shutdown-path unlocked fd close finding tracked in #303 (hcom_host_receive.c) |
| B38 | apps/system/hcom/misc/espcp_utils.c | *Reviewed | 2026-08-24 | #318 | Cell Owner | 2/3 findings addressed (comments, `fixIssue318EspcpUtils`); blocking-`mq_send` finding split out to #324 |
| B39 | apps/system/hcom/misc/espcp_utils.h | Reviewed | 2026-08-24 |  | Cell Owner | No confirmed findings |
| B40 | apps/system/hcom/misc/hcom_common_utils.c | *Reviewed | 2026-08-24 | #319 | Peter |  |
| B41 | apps/system/hcom/misc/meadow_cirbuf.c | *Reviewed | 2026-08-24 | #320 | Peter |  |
| B42 | apps/system/hcom/misc/meadow_utils.c | Reviewed | 2026-08-24 |  | Mark | No confirmed findings (stub file, no function bodies) |
| B43 | apps/system/hcom/mono/hcom_mono_control.c | Blocked (runtime) | 2026-08-24 | #325 | Peter |  |
| B44 | apps/system/hcom/mono/hcom_remote_vs_debug.c | *Blocked (runtime) | 2026-08-24 | #326 | Peter | Findings addressed on branch issue326-hcom-remote-vs-debug (2026-09); UNTESTED, blocked on Mono runtime |
| B45 | apps/system/hcom/mono/hcom_stdxxx_redirect.c | Blocked (runtime) | 2026-08-24 | #327 | Peter |  |
| B46 | apps/system/hcom/nx_rqsts/hcom_bbreg_access.c | *Reviewed | 2026-08-27 | #336 | Peter |  |
| B47 | apps/system/hcom/nx_rqsts/hcom_misc_requests.c | *Reviewed | 2026-08-27 | #338 | Peter |  |
| B48 | apps/system/hcom/nx_rqsts/hcom_via_nx_access.c | Reviewed | 2026-08-27 |  | Peter | No confirmed findings |
| B49 | apps/system/hcom/ota/hcom_ota.c | *Reviewed | 2026-08-27 | #337 | HCOM OTA |  |
| B50 | apps/system/hcom/ota/hcom_ota.h | Reviewed | 2026-08-27 |  | HCOM OTA | No confirmed findings |
| B51 | apps/system/hcom/tests/bbreg_tests.c | Reviewed | 2026-08-25 |  | Peter | Developer-only, requires -u=bbr; no obvious hazard |
| B52 | apps/system/hcom/tests/cell_tests.c | Reviewed | 2026-08-25 |  | Cell Owner | Orphaned -- absent from HCOM_SRCS in apps/system/hcom/Makefile, not compiled, no .o produced, no dispatch entry in developer_tests.c |
| B53 | apps/system/hcom/tests/developer_tests.c | Reviewed | 2026-08-25 |  | Peter | CLI dispatcher for all tests/ files (not a test body itself); no obvious hazard in the dispatch logic |
| B54 | apps/system/hcom/tests/diag_ethernet_chat_test.c | Reviewed | 2026-08-25 |  | Peter | Developer-only, requires -u=chat; no obvious hazard |
| B55 | apps/system/hcom/tests/diag_gpio_tests.c | Reviewed | 2026-08-25 |  | Peter | Developer-only, requires -u=gpio; no obvious hazard |
| B56 | apps/system/hcom/tests/diag_misc_tests.c | Reviewed | 2026-08-25 |  | Peter | Developer-only, requires -u=overload/snprintf/usrwork; toggles debug-only GPIO pins for oscilloscope timing, no obvious hazard |
| B57 | apps/system/hcom/tests/hcom_dir_mgmt_tests.c | Reviewed | 2026-08-25 |  | Peter | Developer-only, requires -u=dirmgmt; unbounded strcat into a fixed currentPath buffer during deep directory traversal (minor, not escalated -- needs an unusually deep tree, developer-run manual test) |
| B58 | apps/system/hcom/tests/meadow_bbd_tests.c | Reviewed | 2026-08-25 |  | Mark | Developer-only, requires -u=bbr (Meadow OS test group); save/restore-wrapped, but a crash or power loss mid-test would leave live BBR flags in test-garbage state (inherent to the destructive-test design, not a coding bug -- not escalated) |
| B59 | apps/system/hcom/tests/meadow_os_userspace_tests.c | Reviewed | 2026-08-25 |  | Mark | Developer-only, requires -u=os; includes a deliberate board-reset test (expected/intentional), no obvious hazard |
| B60 | apps/system/hcom/tests/meadow_sqlite_tests.c | Reviewed | 2026-08-25 |  | Peter | Developer-only, requires -u=sqlite; no obvious hazard |
| B61 | apps/system/hcom/tests/sotest_tests.c | Reviewed | 2026-08-25 |  | Mark | Developer-only, requires -u=sotest; dlopen/dlsym exercise is the deliberate point of the test, no obvious hazard |
| B62 | apps/system/hcom/tests/tensorflow_tests.c | Reviewed | 2026-08-25 |  | Mark | Developer-only, requires -u=tensorflow; no obvious hazard |
| B63 | apps/system/hcom/tests/vs_debug_tests.c | Reviewed | 2026-08-25 |  | Peter | Only reachable via mono_main_proxy(), itself dead code pending Mono build support (see hcom_mono_module_dead_code_status); no obvious hazard |

## F7MeadowOS/ (168 files, libyaml/libcyaml excluded)

1 file removed 2026-09-08 (PR #518), row retired rather than renumbered: C122
(`meadow_os/meadow-gpio.c`) -- dead/unbuildable file deleted per Issue #387; the real
GPIO path is `apps/system/hcom/diag/hcom_diag_gpio.c`.

| ID | File | Status | Date | Issue | Owner | Notes |
|----|------|--------|------|-------|-------|-------|
| C001 | F7MeadowOS/espcp/espcp_common.c | Not started |  |  | Mark |  |
| C002 | F7MeadowOS/espcp/espcp_common.h | Not started |  |  | Mark |  |
| C003 | F7MeadowOS/espcp/espcp_coprocessor.c | Not started |  |  | Mark |  |
| C004 | F7MeadowOS/espcp/espcp_coprocessor.h | Not started |  |  | Mark |  |
| C005 | F7MeadowOS/espcp/espcp_core_dump.c | Not started |  |  | Mark |  |
| C006 | F7MeadowOS/espcp/espcp_core_dump.h | Not started |  |  | Mark |  |
| C007 | F7MeadowOS/espcp/espcp_encoders.c | Not started |  |  | Mark |  |
| C008 | F7MeadowOS/espcp/espcp_encoders.h | Not started |  |  | Mark |  |
| C009 | F7MeadowOS/espcp/espcp_event_handlers.c | Not started |  |  | Mark |  |
| C010 | F7MeadowOS/espcp/espcp_event_handlers.h | Not started |  |  | Mark |  |
| C011 | F7MeadowOS/espcp/espcp_file_system.c | Not started |  |  | Mark |  |
| C012 | F7MeadowOS/espcp/espcp_file_system.h | Not started |  |  | Mark |  |
| C013 | F7MeadowOS/espcp/espcp_message.c | Not started |  |  | Mark |  |
| C014 | F7MeadowOS/espcp/espcp_message.h | Not started |  |  | Mark |  |
| C015 | F7MeadowOS/espcp/espcp_message_dispatcher.c | Not started |  |  | Mark |  |
| C016 | F7MeadowOS/espcp/espcp_message_dispatcher.h | Not started |  |  | Mark |  |
| C017 | F7MeadowOS/espcp/espcp_queue.c | Not started |  |  | Mark |  |
| C018 | F7MeadowOS/espcp/espcp_queue.h | Not started |  |  | Mark |  |
| C019 | F7MeadowOS/espcp/espcp_shared_enums.h | Not started |  |  | Mark |  |
| C020 | F7MeadowOS/espcp/espcp_system.c | Not started |  |  | Mark |  |
| C021 | F7MeadowOS/espcp/espcp_system.h | Not started |  |  | Mark |  |
| C022 | F7MeadowOS/espcp/espcp_test_heap_tracing.h | Not started |  |  | Mark |  |
| C023 | F7MeadowOS/espcp/espcp_thread.c | Not started |  |  | Mark |  |
| C024 | F7MeadowOS/espcp/espcp_thread.h | Not started |  |  | Mark |  |
| C025 | F7MeadowOS/espcp/espcp_uart_monitor.c | Not started |  |  | Mark |  |
| C026 | F7MeadowOS/espcp/espcp_uart_monitor.h | Not started |  |  | Mark |  |
| C027 | F7MeadowOS/espcp/espcp_usrsock.h | Not started |  |  | Mark |  |
| C028 | F7MeadowOS/espcp/espcp_usrsock_sockif.c | Not started |  |  | Mark |  |
| C029 | F7MeadowOS/espcp/espcp_wifi.c | Not started |  |  | Mark |  |
| C030 | F7MeadowOS/espcp/espcp_wifi.h | Not started |  |  | Mark |  |
| C031 | F7MeadowOS/espcp/generic_list.c | Not started |  |  | Mark |  |
| C032 | F7MeadowOS/espcp/generic_list.h | Not started |  |  | Mark |  |
| C033 | F7MeadowOS/ethernet/meadow_ethnet_connect.c | *Reviewed | 2026-08-27 | #339 | Peter |  |
| C034 | F7MeadowOS/ethernet/meadow_ethnet_dhcp.c | *Reviewed | 2026-08-27 | #341 | Peter |  |
| C035 | F7MeadowOS/ethernet/meadow_ethnet_local.h | Reviewed | 2026-08-27 |  | Peter | No confirmed findings |
| C036 | F7MeadowOS/ethernet/meadow_ethnet_monitor.c | Reviewed | 2026-08-27 |  | Peter | ISR register-race and oversized busy-wait timeout findings tracked in #340 (meadow_ethnet_utils.c) |
| C037 | F7MeadowOS/ethernet/meadow_ethnet_start.c | Reviewed | 2026-08-27 |  | Peter | No confirmed findings |
| C038 | F7MeadowOS/ethernet/meadow_ethnet_utils.c | *Reviewed | 2026-08-27 | #340 | Peter |  |
| C039 | F7MeadowOS/hcom_nx/create_fs/hcom_nx_fs_create.c | *Reviewed | 2026-08-27 | #343 | Peter | 4/5 findings fixed (branch `issue343-hcom-nx-fs-create`); finding 3 (boot-time malloc/free) Won't Fix -- codebase's own buffer-size convention marks this size class heap-allocate, not stack; issue closed |
| C040 | F7MeadowOS/hcom_nx/create_fs/hcom_nx_fs_flash.c | *Reviewed | 2026-08-27 | #342 | Peter | 7/8 findings fixed (branch `issue342-346-hcom-nx-create-fs-cluster`); finding 8 (`copy_blocks_to_memory()` double-read) Won't Fix -- verify pass is the only check for a transient QSPI read glitch, not worth trading away; issue closed |
| C041 | F7MeadowOS/hcom_nx/create_fs/hcom_nx_fs_flash_tests.c | Reviewed | 2026-08-27 |  | Peter | No confirmed findings; is the live twin referenced by the dead-code finding tracked in #342 (hcom_nx_fs_flash.c) |
| C042 | F7MeadowOS/hcom_nx/create_fs/hcom_nx_fs_littlefs.c | Reviewed | 2026-08-27 |  | Peter | Boot-time malloc/free finding tracked in #343 (hcom_nx_fs_create.c) |
| C043 | F7MeadowOS/hcom_nx/create_fs/hcom_nx_fs_part_utils.c | *Reviewed | 2026-08-27 | #344 | Peter | 2/2 findings fixed (branch `issue344-hcom-nx-fs-part-utils`); issue closed |
| C044 | F7MeadowOS/hcom_nx/diag/hcom_nx_trace_msg_proc.c | *Reviewed | 2026-08-28 | #345 | Peter |  |
| C045 | F7MeadowOS/hcom_nx/diag/hcom_traverse_ku_kernel.c | *Reviewed | 2026-08-28 | #346 | Peter | 3/4 findings fixed (branch `issue346-hcom-traverse-ku-kernel`); finding 4 (fixed-size envelope) Won't Fix -- needs a wire-format change on both kernel+user sides, no correctness risk today; issue closed. Also surfaced a separate pre-existing bug tracked as Issue #550 |
| C046 | F7MeadowOS/hcom_nx/hcom_nx_bbreg_access.c | Reviewed | 2026-08-28 |  | Peter | No confirmed findings |
| C047 | F7MeadowOS/hcom_nx/hcom_nx_common.h | Reviewed | 2026-08-28 |  | Peter | No confirmed findings |
| C048 | F7MeadowOS/hcom_nx/hcom_nx_common_utils.c | *Reviewed | 2026-08-28 | #350 | Peter |  |
| C049 | F7MeadowOS/hcom_nx/hcom_nx_config_manager.c | Reviewed | 2026-08-28 | #349 | Mark | Unmatched config_lock unlock in espcp_event_handlers.c flagged to Mark, not filed here |
| C050 | F7MeadowOS/hcom_nx/hcom_nx_config_manager.h | Reviewed | 2026-08-28 |  | Mark | No confirmed findings |
| C051 | F7MeadowOS/hcom_nx/hcom_nx_config_manager_yaml.h | Reviewed | 2026-08-28 |  | Mark | No confirmed findings |
| C052 | F7MeadowOS/hcom_nx/hcom_nx_startup_mgr.c | *Reviewed | 2026-08-28 | #351 | Peter |  |
| C054 | F7MeadowOS/hcom_nx/hcom_nx_upd_route_cmd.c | Reviewed | 2026-08-28 |  | Peter | No confirmed findings |
| C055 | F7MeadowOS/kerneltests/hcom_nx_qspi_flash_tests.c | *Reviewed (light) | 2026-09-01 | #420 | Peter | Both findings fixed (0-byte VLA stack overflow, missing bounds checks); also fixed 46 format-string errors + 1 implicit-declaration error uncovered by temporarily enabling the file's build flag to verify compilation (file had apparently never been compiled against current toolchain checks); verified via a full local build with the flag enabled (zero errors), then reverted -- reachability restore remains Issue #347's task; issue closed via PR |
| C056 | F7MeadowOS/include/meadow/bootloader/meadow_os_partitions.h | Reviewed | 2026-08-28 |  | Mark | No confirmed findings |
| C057 | F7MeadowOS/include/meadow/hcom_bbreg_defn.h | Reviewed | 2026-08-28 |  | Peter | No confirmed findings |
| C058 | F7MeadowOS/include/meadow/hcom_dfu_mode_entry.h | Reviewed | 2026-08-28 |  | Peter | No confirmed findings |
| C059 | F7MeadowOS/include/meadow/hcom_dnld_shared.h | Reviewed | 2026-08-28 |  | Peter | No confirmed findings |
| C060 | F7MeadowOS/include/meadow/hcom_misc_diag.h | Reviewed | 2026-08-28 |  | Peter | No confirmed findings |
| C061 | F7MeadowOS/include/meadow/hcom_nuttx_shared.h | Reviewed | 2026-08-28 |  | Peter | No confirmed findings |
| C062 | F7MeadowOS/include/meadow/hcom_protocol.h | *Reviewed | 2026-08-28 | #352 | Peter |  |
| C063 | F7MeadowOS/include/meadow/hcom_shared_common.h | *Reviewed | 2026-08-28 | #353 | Peter |  |
| C064 | F7MeadowOS/include/meadow/hcom_traverse_ku.h | *Reviewed | 2026-08-28 | #354 | Peter | Concurrency findings for this transport's kernel-side implementation tracked in #346 (hcom_traverse_ku_kernel.c) |
| C065 | F7MeadowOS/include/meadow/hcom_upd_shared.h | Reviewed | 2026-08-28 |  | Peter | mcu_ser_numb_s struct field finding tracked in #393 (meadow-upd.c) |
| C066 | F7MeadowOS/include/meadow/meadow-upd.h | Reviewed | 2026-08-28 |  | Syscall UPD | No confirmed findings |
| C067 | F7MeadowOS/include/meadow/meadow_apps_core_share.h | Reviewed | 2026-08-28 |  | Peter | No confirmed findings |
| C068 | F7MeadowOS/include/meadow/meadow_cirbuf.h | *Reviewed | 2026-08-28 | #358 | Peter |  |
| C069 | F7MeadowOS/include/meadow/meadow_client_cert.h | Reviewed | 2026-08-28 | #357 | Cell Owner |  |
| C070 | F7MeadowOS/include/meadow/meadow_cloud.h | Reviewed | 2026-08-28 | #356 | Meadow Cloud |  |
| C071 | F7MeadowOS/include/meadow/meadow_debug_breakpoint_macro.h | Reviewed | 2026-08-28 | #359 | Mark |  |
| C072 | F7MeadowOS/include/meadow/meadow_debug_helpers.h | Reviewed | 2026-08-28 | #355 | Mark |  |
| C073 | F7MeadowOS/include/meadow/meadow_ethnet_common.h | Reviewed | 2026-08-28 |  | Peter | No confirmed findings |
| C074 | F7MeadowOS/include/meadow/meadow_extended_errno.h | Reviewed | 2026-08-28 |  | Peter | No confirmed findings |
| C075 | F7MeadowOS/include/meadow/meadow_gpio_counter.h | *Reviewed | 2026-08-28 | #361 | Peter |  |
| C076 | F7MeadowOS/include/meadow/meadow_hw_version.h | Reviewed | 2026-08-28 |  | Peter | No confirmed findings |
| C077 | F7MeadowOS/include/meadow/meadow_interrupt.h | *Reviewed | 2026-08-28 | #360 | Peter | All 3 findings now terminal: port-range comment fixed (PR #533), packed-struct unaligned access Won't Fix, speculative timestamp feature defaulted back off pending confirmed Meadow.Core support; built and smoketested; issue closed via PR |
| C078 | F7MeadowOS/include/meadow/meadow_kernel_tests.h | Reviewed | 2026-08-28 |  | Mark | No confirmed findings |
| C079 | F7MeadowOS/include/meadow/meadow_kt_dispatcher.h | Reviewed | 2026-08-28 | #363 | Mark |  |
| C080 | F7MeadowOS/include/meadow/meadow_measure_freq_shared.h | Reviewed | 2026-08-28 |  | Peter | No confirmed findings |
| C081 | F7MeadowOS/include/meadow/meadow_os.h | Reviewed | 2026-08-28 | #362 | Mark |  |
| C082 | F7MeadowOS/include/meadow/meadow_os_battery_backed_domain.h | Reviewed | 2026-08-28 | #367 | Mark |  |
| C083 | F7MeadowOS/include/meadow/meadow_os_fault_handling.h | Reviewed | 2026-08-28 |  | Mark | No confirmed findings |
| C084 | F7MeadowOS/include/meadow/meadow_os_persistent_data.h | Reviewed | 2026-08-28 | #364 | Mark |  |
| C085 | F7MeadowOS/include/meadow/meadow_partition_share.h | *Reviewed | 2026-08-28 | #366 | Peter | Both findings fixed: added missing #include <stdint.h>/<stdbool.h>/<sys/types.h> (header wasn't self-contained), added a compile-time #if guard against MEADOW_FLASH_PART_SIZE_UNUSED_SPACE going negative; built with no warnings and smoketested; issue closed via PR |
| C086 | F7MeadowOS/include/meadow/meadow_pwr_mgmt.h | *Reviewed | 2026-08-28 | #368 | Peter |  |
| C087 | F7MeadowOS/include/meadow/meadow_rotary_encoder.h | *Reviewed | 2026-08-28 | #365 | Peter |  |
| C088 | F7MeadowOS/include/meadow/meadow_sdmmc.h | Reviewed | 2026-08-28 |  | Peter | No confirmed findings |
| C089 | F7MeadowOS/include/meadow/meadow_syscall_support.h | Reviewed | 2026-08-28 |  | Peter | No confirmed findings |
| C090 | F7MeadowOS/include/meadow/meadow_thread_config.h | Reviewed | 2026-09-01 | #421 | Mark |  |
| C091 | F7MeadowOS/include/meadow/meadow_unit_test_framework.h | Reviewed | 2026-09-01 | #422 | Mark | No confirmed findings |
| C092 | F7MeadowOS/include/meadow/semaphore_lock_unlock.h | Reviewed | 2026-09-01 | #423 | Mark |  |
| C093 | F7MeadowOS/include/meadow/stm32f777zit6-meadow.h | *Reviewed | 2026-08-28 | #369 | Peter | 3/5 findings fixed (branch `issue369-stm32-meadow-header-cleanup`): dead `stm32_quadspi_init` declaration removed, `stm32_usbinitialize` and `stm32_spidev_initialize` doc comments corrected (wrong caller/board name, typo); the 2 GPIO-speed findings (`GPIO_OTGFS_PWRON`, LED pins) Won't Fix -- functional today, EMI-margin nitpicks only; issue closed via PR |
| C094 | F7MeadowOS/kerneltests/ana_to_dig_conv_tests.c | *Reviewed (light) | 2026-08-28 | #370 | Peter |  |
| C095 | F7MeadowOS/kerneltests/bg77_tests.c | Reviewed (light) | 2026-08-28 | #372 | Cell Owner | Original-Author corrected from Mark -- git history shows only Alexis Christoforides/Peter; reassigned to Cell Owner per feature-based ownership. Issue number corrected from #371 (mismatch) |
| C096 | F7MeadowOS/kerneltests/bg77_tests.h | Reviewed (light) | 2026-08-28 |  | Cell Owner | Byte-identical stub header, findings tracked in #372 (bg77_tests.c) |
| C097 | F7MeadowOS/kerneltests/dig_to_ana_conv_tests.c | *Reviewed (light) | 2026-08-28 | #373 | Peter |  |
| C098 | F7MeadowOS/kerneltests/espcp_tests.c | *Reviewed (light) | 2026-08-28 | #374 | Mark |  |
| C099 | F7MeadowOS/kerneltests/ethernet_tests.c | Reviewed (light) | 2026-08-28 |  | Mark | No confirmed findings |
| C100 | F7MeadowOS/kerneltests/ethernet_tests.h | Reviewed (light) | 2026-08-28 |  | Mark | No confirmed findings |
| C101 | F7MeadowOS/kerneltests/iso8601_time_tests.c | Reviewed (light) | 2026-08-28 | #372 | Mark |  |
| C102 | F7MeadowOS/kerneltests/meadow_gpio_counter_tests.c | Reviewed (light) | 2026-08-28 |  | Peter | No confirmed findings |
| C103 | F7MeadowOS/kerneltests/meadow_i2c_tests.c | Reviewed (light) | 2026-08-28 |  | Mark | No confirmed findings |
| C104 | F7MeadowOS/kerneltests/meadow_interrupt_tests.c | *Reviewed (light) | 2026-08-28 | #377 | Peter |  |
| C105 | F7MeadowOS/kerneltests/meadow_kt_bluetooth_tests.c | Reviewed (light) | 2026-08-28 | #384 | Mark |  |
| C106 | F7MeadowOS/kerneltests/meadow_kt_dispatcher.c | Reviewed (light) | 2026-08-28 | #376 | Mark |  |
| C107 | F7MeadowOS/kerneltests/meadow_kt_log.c | Reviewed (light) | 2026-08-28 | #375 | Mark |  |
| C108 | F7MeadowOS/kerneltests/meadow_kt_log.h | Reviewed (light) | 2026-08-28 |  | Mark | No confirmed findings |
| C109 | F7MeadowOS/kerneltests/meadow_kt_traverse_ku_tests.c | *Reviewed | 2026-08-17 | #316 | Peter |  |
| C110 | F7MeadowOS/kerneltests/meadow_measure_freq_tests.c | Reviewed (light) | 2026-08-28 |  | Peter | No confirmed findings |
| C111 | F7MeadowOS/kerneltests/meadow_os_tests.c | Reviewed (light) | 2026-08-28 |  | Mark | No confirmed findings (intentional NULL-deref assert test, not a bug) |
| C112 | F7MeadowOS/kerneltests/meadow_pwm_tests.c | Reviewed (light) | 2026-08-28 | #383 | Mark |  |
| C113 | F7MeadowOS/kerneltests/meadow_rotary_encoder_tests.c | Reviewed (light) | 2026-08-28 |  | Peter | No confirmed findings |
| C114 | F7MeadowOS/kerneltests/meadow_spi_dma_tests.c | *Reviewed (light) | 2026-08-28 | #381 | Peter |  |
| C115 | F7MeadowOS/kerneltests/network_tests.c | Reviewed (light) | 2026-08-28 | #378 | Mark |  |
| C116 | F7MeadowOS/kerneltests/network_tests.h | Reviewed (light) | 2026-08-28 | #379 | Mark |  |
| C117 | F7MeadowOS/kerneltests/power_management_tests.c | Reviewed (light) | 2026-08-28 | #380 | Mark |  |
| C118 | F7MeadowOS/kerneltests/quick_misc_tests.c | Reviewed (light) | 2026-08-28 |  | Peter | No confirmed findings |
| C119 | F7MeadowOS/kerneltests/sdcard_tests.c | Reviewed (light) | 2026-08-28 | #382 | Mark |  |
| C120 | F7MeadowOS/kerneltests/timing.h | Reviewed (light) | 2026-08-28 |  | Mark | No confirmed findings (dead file, all code commented out, not included anywhere) |
| C121 | F7MeadowOS/meadow_cloud/meadow_cloud.c | Reviewed | 2026-09-01 | #424 | Meadow Cloud |  |
| C123 | F7MeadowOS/meadow_os/meadow-upd.c | *Reviewed | 2026-08-28 | #393 | Syscall UPD | 4/9 findings fixed (branch `issue393-meadow-upd-fixes`): dir_enum overflow guard, unreachable EINVAL, dead watchdog/IRQ declarations, pointer-vs-NULL comparison; finding 6 (serial-number ABI gap) Won't Fix; findings 2/3/4/5 (I2C config race, I2C shutdown race, missing SPI_LOCK, I2C NULL check) deferred -- upd_handle_i2c()/upd_handle_spi_*() are only reachable via the /dev/upd ioctl path from Meadow.Core, not yet integrated, and the existing meadow_i2c_tests.c kernel test bypasses this ioctl layer entirely, so there is no way to verify a concurrency fix here today; issue left open pending real I2C/SPI hardware or Mono runtime access |
| C124 | F7MeadowOS/meadow_os/meadow_hw_version.c | *Reviewed | 2026-08-28 | #391 | Peter |  |
| C125 | F7MeadowOS/meadow_os/meadow_interrupt.c | *Reviewed | 2026-08-28 | #394 | Peter |  |
| C126 | F7MeadowOS/meadow_os/meadow_os.c | Reviewed | 2026-08-28 |  | Mark | No confirmed findings |
| C127 | F7MeadowOS/meadow_os/meadow_os_battery_backed_domain.c | Reviewed | 2026-08-28 | #367 | Mark | Stale doc comments on strdup_to/from_sram tracked in #392 (meadow_os_fault_handling.c) |
| C128 | F7MeadowOS/meadow_os/meadow_os_config.c | *Reviewed | 2026-08-28 | #389 | Mark |  |
| C129 | F7MeadowOS/meadow_os/meadow_os_espcp.c | Reviewed | 2026-08-28 | #388 | Mark |  |
| C130 | F7MeadowOS/meadow_os/meadow_os_exceptions.c | Reviewed | 2026-08-28 | #386 | Mark |  |
| C131 | F7MeadowOS/meadow_os/meadow_os_fault_handling.c | *Reviewed | 2026-08-28 | #392 | Mark |  |
| C132 | F7MeadowOS/meadow_os/meadow_os_info.c | Reviewed | 2026-08-28 | #390 | Mark |  |
| C133 | F7MeadowOS/meadow_os/meadow_sdmmc.c | *Reviewed | 2026-08-28 | #385 | Peter |  |
| C134 | F7MeadowOS/misc/meadow_adc.c | *Reviewed | 2026-08-29 | #403 | Peter |  |
| C135 | F7MeadowOS/misc/meadow_cirbuf.c | *Fixed | 2026-08-29 | #400 | Peter | File deleted (dead kernel-space duplicate); see branch issue400-remove-dead-cirbuf-kernel-copy |
| C136 | F7MeadowOS/misc/meadow_client_cert.c | *Reviewed | 2026-08-29 | #401 | Cell Owner |  |
| C137 | F7MeadowOS/misc/meadow_logging.c | *Reviewed | 2026-08-29 | #399 | Mark |  |
| C138 | F7MeadowOS/misc/meadow_logging.h | Reviewed | 2026-08-29 |  | Mark | No confirmed findings |
| C139 | F7MeadowOS/misc/meadow_reinit_flash_size.c | *Reviewed | 2026-08-29 | #395 | Peter | Both findings fixed (branch `issue395-qspi-reinit-fixes`): DCR FSIZE field now clears QSPI_DCR_FSIZE_MASK before OR-ing in the new value; added a BUSY-wait after ABORT before touching DCR. Hardware-verified on a physical Meadow F7 board; issue closed via PR |
| C140 | F7MeadowOS/misc/meadow_reinit_flash_size.h | Reviewed | 2026-08-29 |  | Peter | No confirmed findings |
| C141 | F7MeadowOS/misc/meadow_stm32f7_clockenable.c | *Reviewed | 2026-08-29 | #398 | Peter |  |
| C142 | F7MeadowOS/misc/meadow_stm32f7_clockenable.h | Reviewed | 2026-08-29 |  | Peter | No confirmed findings |
| C143 | F7MeadowOS/misc/meadow_watchdog.c | Reviewed | 2026-08-29 | #396 | Cell Owner |  |
| C144 | F7MeadowOS/misc/meadow_watchdog.h | Reviewed | 2026-08-29 | #397 | Cell Owner |  |
| C145 | F7MeadowOS/misc/parse_iso8601_time.c | *Reviewed | 2026-08-29 | #402 | Peter | All 3 findings fixed: a live HCOM-reachable heap OOB read (missing length check before offset-15 access), a `uint32_t`-returning-negative-errno type mismatch, and a parameter-name typo + `%ln`/`uint32_t` type mismatch; one sub-finding (dead debug scaffold) was already resolved by an unrelated commit; built (no warnings) and smoketested; issue closed via PR |
| C146 | F7MeadowOS/ntpclient/meadow_ntpclient.c | Reviewed | 2026-08-29 | #404 | Mark |  |
| C147 | F7MeadowOS/ntpclient/meadow_ntpclient.h | Reviewed | 2026-08-29 | #404 | Mark | Findings covered in C146 issue |
| C148 | F7MeadowOS/ntpclient/ntpv3.h | Reviewed | 2026-08-29 | #404 | Mark | Findings covered in C146 issue |
| C149 | F7MeadowOS/pwrmgmt/pwrmgmt_calib_lsi_clock.c | *Reviewed | 2026-08-29 | #412 | Peter | Findings 3-5 fixed (signed-diff underflow, `_lsiRtcPrer` volatile, style/hygiene); findings 1-2 (unbounded LSI-hardware waits) Won't Fix -- identical pattern verified in mainline Nuttx's own stm32_lsi.c, run reliably for years, not something a software timeout can fix; built and smoketested; issue closed via PR |
| C150 | F7MeadowOS/pwrmgmt/pwrmgmt_config_wakeup_alarm.c | *Reviewed | 2026-08-29 | #406 | Peter | All 3 findings fixed; unit tested via `--unittests=power` test 58 (full sleep/wake cycle through this file's alarm config/wakeup path, exact 10s sleep, no timeout errors); issue closed via PR. Touched again by Issue #413's cluster-wide cleanup (simplified this file's top-level compile guard once RTC Alarm became the only scheme) |
| C151 | F7MeadowOS/pwrmgmt/pwrmgmt_config_wakeup_timer.c | *Fixed | 2026-08-29 | #407 | Peter | File deleted (dead RTC Wakeup Timer scheme, superseded by RTC Alarm, unrevivable due to a separate macro-name mismatch); built and unit tested via `--unittests=power` test 58; issue closed via PR |
| C152 | F7MeadowOS/pwrmgmt/pwrmgmt_control.c | *Reviewed | 2026-08-29 | #409 | Peter | All 5 findings fixed, plus a folded-in cleanup removing dead RTC-Wakeup-Timer branches (unreachable since Issue #407 deleted that scheme); built and smoketested, including a 10-second sleep/wake cycle; issue closed via PR. Touched again by Issue #413's cluster-wide cleanup (remaining #if/#else/#error guards fully unwrapped once RTC Alarm became the only scheme) |
| C153 | F7MeadowOS/pwrmgmt/pwrmgmt_enter_stop_mode.c | *Reviewed | 2026-08-29 | #413 | Peter | All 4 findings fixed (`_isMeadowInStopMode`/`_wakeupReason` volatile + memory clobber, bounded SDRAM busy-wait timeout matching Nuttx's own `stm32_fmc.c` precedent, style/hygiene); plus a full cluster-wide cleanup removing every remaining `PWRMGMT_LOW_PWR_EXIT_USE_RTC_ALARM` guard (now unconditional) across this file, `pwrmgmt_control.c`, `pwrmgmt_config_wakeup_alarm.c`, `hcom_nx_common.h`, and `kerneltests/power_management_tests.c` (also deleting a fully-dead function that referenced a function deleted in Issue #407); built, flashed, and smoketested on hardware; issue closed via PR |
| C154 | F7MeadowOS/pwrmgmt/pwrmgmt_local.h | *Reviewed | 2026-08-29 | #408 | Peter | Dead #if/#else pair collapsed to a single always-0 define, with warning comments added at each site a maintainer might consider enabling it; built and unit tested via `--unittests=power`; issue closed via PR |
| C155 | F7MeadowOS/pwrmgmt/pwrmgmt_misc.c | *Reviewed | 2026-08-29 | #405 | Peter | All 3 findings fixed, built and smoketested; unit tested via `--unittests=power` test 58 (full sleep/wake cycle exercising both the RTC exitinit and alarm-wakeup fixes, exact 10s sleep, no timeout errors); issue closed via PR |
| C156 | F7MeadowOS/pwrmgmt/pwrmgmt_switch_rtc_clock.c | *Reviewed | 2026-08-29 | #410 | Peter | 3 of 4 findings fixed (enterinit() fallthrough masking failures, battery-backed-register restore ordering, style/hygiene); 4th (kernel-test semaphore bypass) already resolved by Issue #405's `_rtcAccessLock` mutex, which this file's wprunlock()/wprlock() calls now serialize through; built and smoketested; issue closed via PR |
| C157 | F7MeadowOS/pwrmgmt/pwrmgmt_time_support.c | *Reviewed | 2026-08-29 | #411 | Peter | Both findings fixed: clock_settime() now serializes against Meadow's own RTC access via the Issue #405 `_rtcAccessLock` mutex; style/hygiene cleanup (tautological NULL check, corrected a backwards UTC-offset comment on a kept dead-code stub); built and smoketested; issue closed via PR |
| C158 | F7MeadowOS/specialized/meadow_gpio_counter.c | *Reviewed | 2026-09-01 | #415 | Peter | All 5 findings fixed, unit tested (`--unittests=gpiocount`), smoketested, merged (PR #557); issue closed |
| C159 | F7MeadowOS/specialized/meadow_idle_monitor.c | *Reviewed | 2026-09-01 | #417 | Peter | Style/hygiene finding fixed, smoketested, merged (PR #552); issue closed |
| C160 | F7MeadowOS/specialized/meadow_measure_freq.c | *Reviewed | 2026-09-01 | #418 | Peter | All 11 findings fixed, unit tested (`--unittests=measfreq`), smoketested; duty-cycle math found separately broken during testing, unrelated to any of these 11 findings, tracked as Issue #560; issue closed via PR |
| C161 | F7MeadowOS/specialized/meadow_measure_freq_local.h | *Reviewed | 2026-09-01 | #419 | Peter | All 3 findings fixed, smoketested, merged (PR #554); issue closed |
| C162 | F7MeadowOS/specialized/meadow_rotary_encoder.c | *Reviewed | 2026-09-01 | #416 | Peter | All 3 findings fixed (finding 1 hardware-verified with a real encoder), unit tested (`--unittests=rotenc`), smoketested; issue closed via PR |
