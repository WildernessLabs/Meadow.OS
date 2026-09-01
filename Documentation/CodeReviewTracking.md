# Code Review Tracking

Tracks progress of an ongoing file-by-file bug/concurrency/performance review across the
three source locations below. Reviews are performed with the `review-files` Claude Code
skill (`.claude/skills/review-files/SKILL.md`), a few files at a time.

**Review timeline:** started 2026-08-13, last updated 2026-09-01. Tables A, B, and C are
all now fully reviewed (excluding Mark's esp32/espcp rows, out of scope).

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

This file is updated automatically by the `review-files` skill after each batch. If you
review a file some other way, update its row manually to keep this list accurate.

## nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/ (21 files)

4 files removed 2026-09-01 (PR #425), rows retired rather than renumbered: A03
(`stm32_appinit.c`), A11 (`stm32_i2c.c`), A12 (`stm32_i2c.h`), A18 (`stm32_sdmmc.c`) --
each confirmed unreferenced by any build config; see the Scope section above for why.

| ID | File | Status | Date | Issue | Notes |
|----|------|--------|------|-------|-------|
| A01 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/board_corecomp.c | Reviewed | 2026-09-01 | #443 | Meadow-specific board variant, no upstream equivalent |
| A02 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/board_projlab.c | Reviewed | 2026-09-01 | #442 | Meadow-specific board variant, no upstream equivalent |
| A04 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_appinitialize.c | Reviewed | 2026-09-01 |  | No confirmed findings; generic NuttX board_app_initialize() boilerplate |
| A05 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_autoleds.c | Reviewed | 2026-09-01 | #433 | Derived from stm32f769i-disco |
| A06 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_boot.c | Reviewed | 2026-09-01 | #429 | Derived from stm32f769i-disco (see #426) |
| A07 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_bringup.c | Reviewed | 2026-09-01 | #430 | Derived from stm32f769i-disco |
| A08 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_buttons.c | Reviewed | 2026-09-01 |  | No confirmed findings; near-unmodified copy of stm32f769i-disco, dead code (CONFIG_ARCH_BUTTONS unset everywhere) |
| A09 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_composite.c | Reviewed | 2026-09-01 | #440 | Derived from nucleo-f767zi |
| A10 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_extmem.c | Reviewed | 2026-09-01 | #431 | Derived from stm32f769i-disco (only sibling board with an extmem.c) |
| A13 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_idle.c | Reviewed | 2026-09-01 | #436 | Meadow's own carried-forward Legacy code |
| A14 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_ioctl.c | Reviewed | 2026-09-01 | #441 | Meadow's own restored file (Issue200 precedent) |
| A15 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_ostest.c | Reviewed | 2026-09-01 | #439 | Meadow's own carried-forward Legacy code |
| A16 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_pwm.c | Reviewed | 2026-09-01 | #438 | Meadow's own carried-forward Legacy code |
| A17 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_reset.c | Reviewed | 2026-09-01 |  | No confirmed findings; Meadow's own restored file |
| A19 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_spi.c | Reviewed | 2026-09-01 |  | No confirmed findings; near-unmodified copy of stm32f769i-disco aside from CONFIG_STM32F7_* naming (see #426); no in-tree caller of its select/cmddata stubs found |
| A20 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_sporadic.c | Reviewed | 2026-09-01 | #434 | Derived from stm32f769i-disco |
| A21 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_usb.c | Reviewed | 2026-09-01 | #435 | Derived from nucleo-f767zi (see also #426) |
| A22 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_usbmsc.c | Reviewed | 2026-09-01 | #437 | Meadow's own carried-forward Legacy code |
| A23 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_userleds.c | Reviewed | 2026-09-01 | #432 | Derived from stm32f769i-disco |
| A24 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/include/board.h | Reviewed | 2026-09-01 | #427 | In include/, not src/; see also #426 (upstream watch item) |
| A25 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/include/boardctl.h | Reviewed | 2026-09-01 | #428 | In include/, not src/ |

## apps/system/hcom/ (63 files)

| ID | File | Status | Date | Issue | Notes |
|----|------|--------|------|-------|-------|
| B01 | apps/system/hcom/cell/cmux/hcom_cmux.c | Reviewed | 2026-08-27 | #333 |  |
| B02 | apps/system/hcom/cell/cmux/hcom_cmux.h | Reviewed | 2026-08-27 | #334 |  |
| B03 | apps/system/hcom/cell/hcom_pppd.c | Reviewed | 2026-08-27 | #332 | Confirmed not in HCOM_SRCS; wouldn't compile if re-enabled as-is |
| B04 | apps/system/hcom/cell/hcom_pppd.h | Reviewed | 2026-08-27 |  | No confirmed findings |
| B05 | apps/system/hcom/comms/hcom_host_encode.c | Reviewed | 2026-08-13 | #300 |  |
| B06 | apps/system/hcom/comms/hcom_host_enq_deq.c | Reviewed | 2026-08-13 | #301 |  |
| B07 | apps/system/hcom/comms/hcom_host_preprocess.c | Reviewed | 2026-08-13 | #302 | F001 also tracked in #301 (hcom_host_enq_deq.c) |
| B08 | apps/system/hcom/comms/hcom_host_receive.c | Reviewed | 2026-08-13 | #303 | F007 also tracked in #301 (hcom_host_enq_deq.c) |
| B09 | apps/system/hcom/comms/hcom_host_route.c | Reviewed | 2026-08-13 |  | No findings |
| B10 | apps/system/hcom/comms/hcom_host_send.c | Reviewed | 2026-08-13 | #304 | F007 also tracked in #301 (hcom_host_enq_deq.c) |
| B11 | apps/system/hcom/comms/hcom_host_watchdog.c | Reviewed | 2026-08-13 |  | F001 tracked in #301 (hcom_host_enq_deq.c); F038 tracked in #310 (hcom_file_dnld_esp32.c); F042 tracked in #311 (hcom_file_dnld_stm32f7.c) |
| B12 | apps/system/hcom/diag/hcom_diag_decode_protocol.c | Reviewed | 2026-08-17 |  | No confirmed findings |
| B13 | apps/system/hcom/diag/hcom_diag_gpio.c | Reviewed | 2026-08-17 |  | F010 tracked in #305 (hcom_diag_gpio.h) |
| B14 | apps/system/hcom/diag/hcom_diag_gpio.h | Reviewed | 2026-08-17 | #305 |  |
| B15 | apps/system/hcom/diag/hcom_diag_misc.c | Reviewed | 2026-08-17 | #306 |  |
| B16 | apps/system/hcom/diag/hcom_diag_nsh_support.c | Reviewed | 2026-08-17 |  | No confirmed findings |
| B17 | apps/system/hcom/diag/hcom_diag_trace_priority.c | Reviewed | 2026-08-17 | #307 |  |
| B18 | apps/system/hcom/diag/hcom_traverse_ku_user.c | Reviewed | 2026-08-17 | #308 |  |
| B19 | apps/system/hcom/esp32/hcom_esp32_comms.h | Not started |  |  | Owner: Mark |
| B20 | apps/system/hcom/esp32/hcom_esp32_execute.c | Not started |  |  | Owner: Mark |
| B21 | apps/system/hcom/esp32/hcom_esp32_network_monitor.c | Not started |  |  | Owner: Mark |
| B22 | apps/system/hcom/esp32/hcom_esp32_network_monitor.h | Not started |  |  | Owner: Mark |
| B23 | apps/system/hcom/esp32/hcom_esp32_proc_recv.c | Not started |  |  | Owner: Mark |
| B24 | apps/system/hcom/esp32/hcom_esp32_proc_util.c | Not started |  |  | Owner: Mark |
| B25 | apps/system/hcom/esp32/hcom_esp32_proc_xmit.c | Not started |  |  | Owner: Mark |
| B26 | apps/system/hcom/esp32/hcom_esp32_uart_comms.c | Not started |  |  | Owner: Mark |
| B27 | apps/system/hcom/file/hcom_file_delete_stm32f7.c | Reviewed | 2026-08-19 |  | No confirmed findings |
| B28 | apps/system/hcom/file/hcom_file_dir_mgmt_utils.c | Reviewed | 2026-08-19 | #309 |  |
| B29 | apps/system/hcom/file/hcom_file_dnld_esp32.c | Reviewed | 2026-08-19 | #310 |  |
| B30 | apps/system/hcom/file/hcom_file_dnld_stm32f7.c | Reviewed | 2026-08-19 | #311 |  |
| B31 | apps/system/hcom/file/hcom_file_lists.c | Reviewed | 2026-08-19 | #312 |  |
| B32 | apps/system/hcom/file/hcom_file_lists_subdir.c | Reviewed | 2026-08-19 |  | No confirmed findings |
| B33 | apps/system/hcom/file/hcom_file_misc.c | Reviewed | 2026-08-19 | #313 |  |
| B34 | apps/system/hcom/file/hcom_file_upld_proc.c | Reviewed | 2026-08-19 | #314 |  |
| B35 | apps/system/hcom/file/hcom_file_write_stm32f7.c | Reviewed | 2026-08-19 | #315 |  |
| B36 | apps/system/hcom/hcom_common.h | Reviewed | 2026-08-27 | #330 |  |
| B37 | apps/system/hcom/hcom_startup_manager.c | Reviewed | 2026-08-27 | #331 | Shutdown-path unlocked fd close finding tracked in #303 (hcom_host_receive.c) |
| B38 | apps/system/hcom/misc/espcp_utils.c | Reviewed | 2026-08-24 | #318 | 2/3 findings addressed (comments, `fixIssue318EspcpUtils`); blocking-`mq_send` finding split out to #324 |
| B39 | apps/system/hcom/misc/espcp_utils.h | Reviewed | 2026-08-24 |  | No confirmed findings |
| B40 | apps/system/hcom/misc/hcom_common_utils.c | Reviewed | 2026-08-24 | #319 |  |
| B41 | apps/system/hcom/misc/meadow_cirbuf.c | Reviewed | 2026-08-24 | #320 |  |
| B42 | apps/system/hcom/misc/meadow_utils.c | Reviewed | 2026-08-24 |  | No confirmed findings (stub file, no function bodies) |
| B43 | apps/system/hcom/mono/hcom_mono_control.c | Reviewed | 2026-08-24 | #325 |  |
| B44 | apps/system/hcom/mono/hcom_remote_vs_debug.c | Reviewed | 2026-08-24 | #326 |  |
| B45 | apps/system/hcom/mono/hcom_stdxxx_redirect.c | Reviewed | 2026-08-24 | #327 |  |
| B46 | apps/system/hcom/nx_rqsts/hcom_bbreg_access.c | Reviewed | 2026-08-27 | #336 |  |
| B47 | apps/system/hcom/nx_rqsts/hcom_misc_requests.c | Reviewed | 2026-08-27 | #338 |  |
| B48 | apps/system/hcom/nx_rqsts/hcom_via_nx_access.c | Reviewed | 2026-08-27 |  | No confirmed findings |
| B49 | apps/system/hcom/ota/hcom_ota.c | Reviewed | 2026-08-27 | #337 |  |
| B50 | apps/system/hcom/ota/hcom_ota.h | Reviewed | 2026-08-27 |  | No confirmed findings |
| B51 | apps/system/hcom/tests/bbreg_tests.c | Reviewed | 2026-08-25 |  | Developer-only, requires -u=bbr; no obvious hazard |
| B52 | apps/system/hcom/tests/cell_tests.c | Reviewed | 2026-08-25 |  | Orphaned -- absent from HCOM_SRCS in apps/system/hcom/Makefile, not compiled, no .o produced, no dispatch entry in developer_tests.c |
| B53 | apps/system/hcom/tests/developer_tests.c | Reviewed | 2026-08-25 |  | CLI dispatcher for all tests/ files (not a test body itself); no obvious hazard in the dispatch logic |
| B54 | apps/system/hcom/tests/diag_ethernet_chat_test.c | Reviewed | 2026-08-25 |  | Developer-only, requires -u=chat; no obvious hazard |
| B55 | apps/system/hcom/tests/diag_gpio_tests.c | Reviewed | 2026-08-25 |  | Developer-only, requires -u=gpio; no obvious hazard |
| B56 | apps/system/hcom/tests/diag_misc_tests.c | Reviewed | 2026-08-25 |  | Developer-only, requires -u=overload/snprintf/usrwork; toggles debug-only GPIO pins for oscilloscope timing, no obvious hazard |
| B57 | apps/system/hcom/tests/hcom_dir_mgmt_tests.c | Reviewed | 2026-08-25 |  | Developer-only, requires -u=dirmgmt; unbounded strcat into a fixed currentPath buffer during deep directory traversal (minor, not escalated -- needs an unusually deep tree, developer-run manual test) |
| B58 | apps/system/hcom/tests/meadow_bbd_tests.c | Reviewed | 2026-08-25 |  | Developer-only, requires -u=bbr (Meadow OS test group); save/restore-wrapped, but a crash or power loss mid-test would leave live BBR flags in test-garbage state (inherent to the destructive-test design, not a coding bug -- not escalated) |
| B59 | apps/system/hcom/tests/meadow_os_userspace_tests.c | Reviewed | 2026-08-25 |  | Developer-only, requires -u=os; includes a deliberate board-reset test (expected/intentional), no obvious hazard |
| B60 | apps/system/hcom/tests/meadow_sqlite_tests.c | Reviewed | 2026-08-25 |  | Developer-only, requires -u=sqlite; no obvious hazard |
| B61 | apps/system/hcom/tests/sotest_tests.c | Reviewed | 2026-08-25 |  | Developer-only, requires -u=sotest; dlopen/dlsym exercise is the deliberate point of the test, no obvious hazard |
| B62 | apps/system/hcom/tests/tensorflow_tests.c | Reviewed | 2026-08-25 |  | Developer-only, requires -u=tensorflow; no obvious hazard |
| B63 | apps/system/hcom/tests/vs_debug_tests.c | Reviewed | 2026-08-25 |  | Only reachable via mono_main_proxy(), itself dead code pending Mono build support (see hcom_mono_module_dead_code_status); no obvious hazard |

## F7MeadowOS/ (169 files, libyaml/libcyaml excluded)

| ID | File | Status | Date | Issue | Notes |
|----|------|--------|------|-------|-------|
| C001 | F7MeadowOS/espcp/espcp_common.c | Not started |  |  | Owner: Mark |
| C002 | F7MeadowOS/espcp/espcp_common.h | Not started |  |  | Owner: Mark |
| C003 | F7MeadowOS/espcp/espcp_coprocessor.c | Not started |  |  | Owner: Mark |
| C004 | F7MeadowOS/espcp/espcp_coprocessor.h | Not started |  |  | Owner: Mark |
| C005 | F7MeadowOS/espcp/espcp_core_dump.c | Not started |  |  | Owner: Mark |
| C006 | F7MeadowOS/espcp/espcp_core_dump.h | Not started |  |  | Owner: Mark |
| C007 | F7MeadowOS/espcp/espcp_encoders.c | Not started |  |  | Owner: Mark |
| C008 | F7MeadowOS/espcp/espcp_encoders.h | Not started |  |  | Owner: Mark |
| C009 | F7MeadowOS/espcp/espcp_event_handlers.c | Not started |  |  | Owner: Mark |
| C010 | F7MeadowOS/espcp/espcp_event_handlers.h | Not started |  |  | Owner: Mark |
| C011 | F7MeadowOS/espcp/espcp_file_system.c | Not started |  |  | Owner: Mark |
| C012 | F7MeadowOS/espcp/espcp_file_system.h | Not started |  |  | Owner: Mark |
| C013 | F7MeadowOS/espcp/espcp_message.c | Not started |  |  | Owner: Mark |
| C014 | F7MeadowOS/espcp/espcp_message.h | Not started |  |  | Owner: Mark |
| C015 | F7MeadowOS/espcp/espcp_message_dispatcher.c | Not started |  |  | Owner: Mark |
| C016 | F7MeadowOS/espcp/espcp_message_dispatcher.h | Not started |  |  | Owner: Mark |
| C017 | F7MeadowOS/espcp/espcp_queue.c | Not started |  |  | Owner: Mark |
| C018 | F7MeadowOS/espcp/espcp_queue.h | Not started |  |  | Owner: Mark |
| C019 | F7MeadowOS/espcp/espcp_shared_enums.h | Not started |  |  | Owner: Mark |
| C020 | F7MeadowOS/espcp/espcp_system.c | Not started |  |  | Owner: Mark |
| C021 | F7MeadowOS/espcp/espcp_system.h | Not started |  |  | Owner: Mark |
| C022 | F7MeadowOS/espcp/espcp_test_heap_tracing.h | Not started |  |  | Owner: Mark |
| C023 | F7MeadowOS/espcp/espcp_thread.c | Not started |  |  | Owner: Mark |
| C024 | F7MeadowOS/espcp/espcp_thread.h | Not started |  |  | Owner: Mark |
| C025 | F7MeadowOS/espcp/espcp_uart_monitor.c | Not started |  |  | Owner: Mark |
| C026 | F7MeadowOS/espcp/espcp_uart_monitor.h | Not started |  |  | Owner: Mark |
| C027 | F7MeadowOS/espcp/espcp_usrsock.h | Not started |  |  | Owner: Mark |
| C028 | F7MeadowOS/espcp/espcp_usrsock_sockif.c | Not started |  |  | Owner: Mark |
| C029 | F7MeadowOS/espcp/espcp_wifi.c | Not started |  |  | Owner: Mark |
| C030 | F7MeadowOS/espcp/espcp_wifi.h | Not started |  |  | Owner: Mark |
| C031 | F7MeadowOS/espcp/generic_list.c | Not started |  |  | Owner: Mark |
| C032 | F7MeadowOS/espcp/generic_list.h | Not started |  |  | Owner: Mark |
| C033 | F7MeadowOS/ethernet/meadow_ethnet_connect.c | Reviewed | 2026-08-27 | #339 |  |
| C034 | F7MeadowOS/ethernet/meadow_ethnet_dhcp.c | Reviewed | 2026-08-27 | #341 |  |
| C035 | F7MeadowOS/ethernet/meadow_ethnet_local.h | Reviewed | 2026-08-27 |  | No confirmed findings |
| C036 | F7MeadowOS/ethernet/meadow_ethnet_monitor.c | Reviewed | 2026-08-27 |  | ISR register-race and oversized busy-wait timeout findings tracked in #340 (meadow_ethnet_utils.c) |
| C037 | F7MeadowOS/ethernet/meadow_ethnet_start.c | Reviewed | 2026-08-27 |  | No confirmed findings |
| C038 | F7MeadowOS/ethernet/meadow_ethnet_utils.c | Reviewed | 2026-08-27 | #340 |  |
| C039 | F7MeadowOS/hcom_nx/create_fs/hcom_nx_fs_create.c | Reviewed | 2026-08-27 | #343 |  |
| C040 | F7MeadowOS/hcom_nx/create_fs/hcom_nx_fs_flash.c | Reviewed | 2026-08-27 | #342 |  |
| C041 | F7MeadowOS/hcom_nx/create_fs/hcom_nx_fs_flash_tests.c | Reviewed | 2026-08-27 |  | No confirmed findings; is the live twin referenced by the dead-code finding tracked in #342 (hcom_nx_fs_flash.c) |
| C042 | F7MeadowOS/hcom_nx/create_fs/hcom_nx_fs_littlefs.c | Reviewed | 2026-08-27 |  | Boot-time malloc/free finding tracked in #343 (hcom_nx_fs_create.c) |
| C043 | F7MeadowOS/hcom_nx/create_fs/hcom_nx_fs_part_utils.c | Reviewed | 2026-08-27 | #344 |  |
| C044 | F7MeadowOS/hcom_nx/diag/hcom_nx_trace_msg_proc.c | Reviewed | 2026-08-28 | #345 |  |
| C045 | F7MeadowOS/hcom_nx/diag/hcom_traverse_ku_kernel.c | Reviewed | 2026-08-28 | #346 |  |
| C046 | F7MeadowOS/hcom_nx/hcom_nx_bbreg_access.c | Reviewed | 2026-08-28 |  | No confirmed findings |
| C047 | F7MeadowOS/hcom_nx/hcom_nx_common.h | Reviewed | 2026-08-28 |  | No confirmed findings |
| C048 | F7MeadowOS/hcom_nx/hcom_nx_common_utils.c | Reviewed | 2026-08-28 | #350 |  |
| C049 | F7MeadowOS/hcom_nx/hcom_nx_config_manager.c | Reviewed | 2026-08-28 | #349 | Unmatched config_lock unlock in espcp_event_handlers.c flagged to Mark, not filed here |
| C050 | F7MeadowOS/hcom_nx/hcom_nx_config_manager.h | Reviewed | 2026-08-28 |  | No confirmed findings |
| C051 | F7MeadowOS/hcom_nx/hcom_nx_config_manager_yaml.h | Reviewed | 2026-08-28 |  | No confirmed findings |
| C052 | F7MeadowOS/hcom_nx/hcom_nx_startup_mgr.c | Reviewed | 2026-08-28 | #351 |  |
| C054 | F7MeadowOS/hcom_nx/hcom_nx_upd_route_cmd.c | Reviewed | 2026-08-28 |  | No confirmed findings |
| C055 | F7MeadowOS/kerneltests/hcom_nx_qspi_flash_tests.c | Reviewed (light) | 2026-09-01 | #420 | Moved from hcom_nx/tests/ to kerneltests/ (Issue #347); reachability restore still pending |
| C056 | F7MeadowOS/include/meadow/bootloader/meadow_os_partitions.h | Reviewed | 2026-08-28 |  | No confirmed findings |
| C057 | F7MeadowOS/include/meadow/hcom_bbreg_defn.h | Reviewed | 2026-08-28 |  | No confirmed findings |
| C058 | F7MeadowOS/include/meadow/hcom_dfu_mode_entry.h | Reviewed | 2026-08-28 |  | No confirmed findings |
| C059 | F7MeadowOS/include/meadow/hcom_dnld_shared.h | Reviewed | 2026-08-28 |  | No confirmed findings |
| C060 | F7MeadowOS/include/meadow/hcom_misc_diag.h | Reviewed | 2026-08-28 |  | No confirmed findings |
| C061 | F7MeadowOS/include/meadow/hcom_nuttx_shared.h | Reviewed | 2026-08-28 |  | No confirmed findings |
| C062 | F7MeadowOS/include/meadow/hcom_protocol.h | Reviewed | 2026-08-28 | #352 |  |
| C063 | F7MeadowOS/include/meadow/hcom_shared_common.h | Reviewed | 2026-08-28 | #353 |  |
| C064 | F7MeadowOS/include/meadow/hcom_traverse_ku.h | Reviewed | 2026-08-28 | #354 | Concurrency findings for this transport's kernel-side implementation tracked in #346 (hcom_traverse_ku_kernel.c) |
| C065 | F7MeadowOS/include/meadow/hcom_upd_shared.h | Reviewed | 2026-08-28 |  | mcu_ser_numb_s struct field finding tracked in #393 (meadow-upd.c) |
| C066 | F7MeadowOS/include/meadow/meadow-upd.h | Reviewed | 2026-08-28 |  | No confirmed findings |
| C067 | F7MeadowOS/include/meadow/meadow_apps_core_share.h | Reviewed | 2026-08-28 |  | No confirmed findings |
| C068 | F7MeadowOS/include/meadow/meadow_cirbuf.h | Reviewed | 2026-08-28 | #358 |  |
| C069 | F7MeadowOS/include/meadow/meadow_client_cert.h | Reviewed | 2026-08-28 | #357 |  |
| C070 | F7MeadowOS/include/meadow/meadow_cloud.h | Reviewed | 2026-08-28 | #356 |  |
| C071 | F7MeadowOS/include/meadow/meadow_debug_breakpoint_macro.h | Reviewed | 2026-08-28 | #359 |  |
| C072 | F7MeadowOS/include/meadow/meadow_debug_helpers.h | Reviewed | 2026-08-28 | #355 |  |
| C073 | F7MeadowOS/include/meadow/meadow_ethnet_common.h | Reviewed | 2026-08-28 |  | No confirmed findings |
| C074 | F7MeadowOS/include/meadow/meadow_extended_errno.h | Reviewed | 2026-08-28 |  | No confirmed findings |
| C075 | F7MeadowOS/include/meadow/meadow_gpio_counter.h | Reviewed | 2026-08-28 | #361 |  |
| C076 | F7MeadowOS/include/meadow/meadow_hw_version.h | Reviewed | 2026-08-28 |  | No confirmed findings |
| C077 | F7MeadowOS/include/meadow/meadow_interrupt.h | Reviewed | 2026-08-28 | #360 |  |
| C078 | F7MeadowOS/include/meadow/meadow_kernel_tests.h | Reviewed | 2026-08-28 |  | No confirmed findings |
| C079 | F7MeadowOS/include/meadow/meadow_kt_dispatcher.h | Reviewed | 2026-08-28 | #363 |  |
| C080 | F7MeadowOS/include/meadow/meadow_measure_freq_shared.h | Reviewed | 2026-08-28 |  | No confirmed findings |
| C081 | F7MeadowOS/include/meadow/meadow_os.h | Reviewed | 2026-08-28 | #362 |  |
| C082 | F7MeadowOS/include/meadow/meadow_os_battery_backed_domain.h | Reviewed | 2026-08-28 | #367 |  |
| C083 | F7MeadowOS/include/meadow/meadow_os_fault_handling.h | Reviewed | 2026-08-28 |  | No confirmed findings |
| C084 | F7MeadowOS/include/meadow/meadow_os_persistent_data.h | Reviewed | 2026-08-28 | #364 |  |
| C085 | F7MeadowOS/include/meadow/meadow_partition_share.h | Reviewed | 2026-08-28 | #366 |  |
| C086 | F7MeadowOS/include/meadow/meadow_pwr_mgmt.h | Reviewed | 2026-08-28 | #368 |  |
| C087 | F7MeadowOS/include/meadow/meadow_rotary_encoder.h | Reviewed | 2026-08-28 | #365 |  |
| C088 | F7MeadowOS/include/meadow/meadow_sdmmc.h | Reviewed | 2026-08-28 |  | No confirmed findings |
| C089 | F7MeadowOS/include/meadow/meadow_syscall_support.h | Reviewed | 2026-08-28 |  | No confirmed findings |
| C090 | F7MeadowOS/include/meadow/meadow_thread_config.h | Reviewed | 2026-09-01 | #421 |  |
| C091 | F7MeadowOS/include/meadow/meadow_unit_test_framework.h | Reviewed | 2026-09-01 | #422 | No confirmed findings |
| C092 | F7MeadowOS/include/meadow/semaphore_lock_unlock.h | Reviewed | 2026-09-01 | #423 |  |
| C093 | F7MeadowOS/include/meadow/stm32f777zit6-meadow.h | Reviewed | 2026-08-28 | #369 |  |
| C094 | F7MeadowOS/kerneltests/ana_to_dig_conv_tests.c | Reviewed (light) | 2026-08-28 | #370 |  |
| C095 | F7MeadowOS/kerneltests/bg77_tests.c | Reviewed (light) | 2026-08-28 | #371 |  |
| C096 | F7MeadowOS/kerneltests/bg77_tests.h | Reviewed (light) | 2026-08-28 |  | Byte-identical stub header, findings tracked in #371 (bg77_tests.c) |
| C097 | F7MeadowOS/kerneltests/dig_to_ana_conv_tests.c | Reviewed (light) | 2026-08-28 | #373 |  |
| C098 | F7MeadowOS/kerneltests/espcp_tests.c | Reviewed (light) | 2026-08-28 | #374 |  |
| C099 | F7MeadowOS/kerneltests/ethernet_tests.c | Reviewed (light) | 2026-08-28 |  | No confirmed findings |
| C100 | F7MeadowOS/kerneltests/ethernet_tests.h | Reviewed (light) | 2026-08-28 |  | No confirmed findings |
| C101 | F7MeadowOS/kerneltests/iso8601_time_tests.c | Reviewed (light) | 2026-08-28 | #372 |  |
| C102 | F7MeadowOS/kerneltests/meadow_gpio_counter_tests.c | Reviewed (light) | 2026-08-28 |  | No confirmed findings |
| C103 | F7MeadowOS/kerneltests/meadow_i2c_tests.c | Reviewed (light) | 2026-08-28 |  | No confirmed findings |
| C104 | F7MeadowOS/kerneltests/meadow_interrupt_tests.c | Reviewed (light) | 2026-08-28 | #377 |  |
| C105 | F7MeadowOS/kerneltests/meadow_kt_bluetooth_tests.c | Reviewed (light) | 2026-08-28 | #384 |  |
| C106 | F7MeadowOS/kerneltests/meadow_kt_dispatcher.c | Reviewed (light) | 2026-08-28 | #376 |  |
| C107 | F7MeadowOS/kerneltests/meadow_kt_log.c | Reviewed (light) | 2026-08-28 | #375 |  |
| C108 | F7MeadowOS/kerneltests/meadow_kt_log.h | Reviewed (light) | 2026-08-28 |  | No confirmed findings |
| C109 | F7MeadowOS/kerneltests/meadow_kt_traverse_ku_tests.c | Reviewed | 2026-08-17 | #316 | Found incidentally during F011 hardware verification, not from a full batch review of this file. |
| C110 | F7MeadowOS/kerneltests/meadow_measure_freq_tests.c | Reviewed (light) | 2026-08-28 |  | No confirmed findings |
| C111 | F7MeadowOS/kerneltests/meadow_os_tests.c | Reviewed (light) | 2026-08-28 |  | No confirmed findings (intentional NULL-deref assert test, not a bug) |
| C112 | F7MeadowOS/kerneltests/meadow_pwm_tests.c | Reviewed (light) | 2026-08-28 | #383 |  |
| C113 | F7MeadowOS/kerneltests/meadow_rotary_encoder_tests.c | Reviewed (light) | 2026-08-28 |  | No confirmed findings |
| C114 | F7MeadowOS/kerneltests/meadow_spi_dma_tests.c | Reviewed (light) | 2026-08-28 | #381 |  |
| C115 | F7MeadowOS/kerneltests/network_tests.c | Reviewed (light) | 2026-08-28 | #378 |  |
| C116 | F7MeadowOS/kerneltests/network_tests.h | Reviewed (light) | 2026-08-28 | #379 |  |
| C117 | F7MeadowOS/kerneltests/power_management_tests.c | Reviewed (light) | 2026-08-28 | #380 |  |
| C118 | F7MeadowOS/kerneltests/quick_misc_tests.c | Reviewed (light) | 2026-08-28 |  | No confirmed findings |
| C119 | F7MeadowOS/kerneltests/sdcard_tests.c | Reviewed (light) | 2026-08-28 | #382 |  |
| C120 | F7MeadowOS/kerneltests/timing.h | Reviewed (light) | 2026-08-28 |  | No confirmed findings (dead file, all code commented out, not included anywhere) |
| C121 | F7MeadowOS/meadow_cloud/meadow_cloud.c | Reviewed | 2026-09-01 | #424 |  |
| C122 | F7MeadowOS/meadow_os/meadow-gpio.c | Reviewed | 2026-08-28 | #387 |  |
| C123 | F7MeadowOS/meadow_os/meadow-upd.c | Reviewed | 2026-08-28 | #393 |  |
| C124 | F7MeadowOS/meadow_os/meadow_hw_version.c | Reviewed | 2026-08-28 | #391 |  |
| C125 | F7MeadowOS/meadow_os/meadow_interrupt.c | Reviewed | 2026-08-28 | #394 |  |
| C126 | F7MeadowOS/meadow_os/meadow_os.c | Reviewed | 2026-08-28 |  | No confirmed findings |
| C127 | F7MeadowOS/meadow_os/meadow_os_battery_backed_domain.c | Reviewed | 2026-08-28 | #367 | Stale doc comments on strdup_to/from_sram tracked in #392 (meadow_os_fault_handling.c) |
| C128 | F7MeadowOS/meadow_os/meadow_os_config.c | Reviewed | 2026-08-28 | #389 |  |
| C129 | F7MeadowOS/meadow_os/meadow_os_espcp.c | Reviewed | 2026-08-28 | #388 |  |
| C130 | F7MeadowOS/meadow_os/meadow_os_exceptions.c | Reviewed | 2026-08-28 | #386 |  |
| C131 | F7MeadowOS/meadow_os/meadow_os_fault_handling.c | Reviewed | 2026-08-28 | #392 |  |
| C132 | F7MeadowOS/meadow_os/meadow_os_info.c | Reviewed | 2026-08-28 | #390 |  |
| C133 | F7MeadowOS/meadow_os/meadow_sdmmc.c | Reviewed | 2026-08-28 | #385 |  |
| C134 | F7MeadowOS/misc/meadow_adc.c | Reviewed | 2026-08-29 | #403 |  |
| C135 | F7MeadowOS/misc/meadow_cirbuf.c | Reviewed | 2026-08-29 | #400 |  |
| C136 | F7MeadowOS/misc/meadow_client_cert.c | Reviewed | 2026-08-29 | #401 |  |
| C137 | F7MeadowOS/misc/meadow_logging.c | Reviewed | 2026-08-29 | #399 |  |
| C138 | F7MeadowOS/misc/meadow_logging.h | Reviewed | 2026-08-29 |  | No confirmed findings |
| C139 | F7MeadowOS/misc/meadow_reinit_flash_size.c | Reviewed | 2026-08-29 | #395 |  |
| C140 | F7MeadowOS/misc/meadow_reinit_flash_size.h | Reviewed | 2026-08-29 |  | No confirmed findings |
| C141 | F7MeadowOS/misc/meadow_stm32f7_clockenable.c | Reviewed | 2026-08-29 | #398 |  |
| C142 | F7MeadowOS/misc/meadow_stm32f7_clockenable.h | Reviewed | 2026-08-29 |  | No confirmed findings |
| C143 | F7MeadowOS/misc/meadow_watchdog.c | Reviewed | 2026-08-29 | #396 |  |
| C144 | F7MeadowOS/misc/meadow_watchdog.h | Reviewed | 2026-08-29 | #397 |  |
| C145 | F7MeadowOS/misc/parse_iso8601_time.c | Reviewed | 2026-08-29 | #402 |  |
| C146 | F7MeadowOS/ntpclient/meadow_ntpclient.c | Reviewed | 2026-08-29 | #404 |  |
| C147 | F7MeadowOS/ntpclient/meadow_ntpclient.h | Reviewed | 2026-08-29 | #404 | Findings covered in C146 issue |
| C148 | F7MeadowOS/ntpclient/ntpv3.h | Reviewed | 2026-08-29 | #404 | Findings covered in C146 issue |
| C149 | F7MeadowOS/pwrmgmt/pwrmgmt_calib_lsi_clock.c | Reviewed | 2026-08-29 | #412 |  |
| C150 | F7MeadowOS/pwrmgmt/pwrmgmt_config_wakeup_alarm.c | Reviewed | 2026-08-29 | #406 |  |
| C151 | F7MeadowOS/pwrmgmt/pwrmgmt_config_wakeup_timer.c | Reviewed | 2026-08-29 | #407 |  |
| C152 | F7MeadowOS/pwrmgmt/pwrmgmt_control.c | Reviewed | 2026-08-29 | #409 |  |
| C153 | F7MeadowOS/pwrmgmt/pwrmgmt_enter_stop_mode.c | Reviewed | 2026-08-29 | #413 |  |
| C154 | F7MeadowOS/pwrmgmt/pwrmgmt_local.h | Reviewed | 2026-08-29 | #408 |  |
| C155 | F7MeadowOS/pwrmgmt/pwrmgmt_misc.c | Reviewed | 2026-08-29 | #405 |  |
| C156 | F7MeadowOS/pwrmgmt/pwrmgmt_switch_rtc_clock.c | Reviewed | 2026-08-29 | #410 |  |
| C157 | F7MeadowOS/pwrmgmt/pwrmgmt_time_support.c | Reviewed | 2026-08-29 | #411 |  |
| C158 | F7MeadowOS/specialized/meadow_gpio_counter.c | Reviewed | 2026-09-01 | #415 |  |
| C159 | F7MeadowOS/specialized/meadow_idle_monitor.c | Reviewed | 2026-09-01 | #417 |  |
| C160 | F7MeadowOS/specialized/meadow_measure_freq.c | Reviewed | 2026-09-01 | #418 |  |
| C161 | F7MeadowOS/specialized/meadow_measure_freq_local.h | Reviewed | 2026-09-01 | #419 |  |
| C162 | F7MeadowOS/specialized/meadow_rotary_encoder.c | Reviewed | 2026-09-01 | #416 |  |
