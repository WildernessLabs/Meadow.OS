# Code Review Tracking

Tracks progress of an ongoing file-by-file bug/concurrency/performance review across the
three source locations below. Reviews are performed with the `review-files` Claude Code
skill (`.claude/skills/review-files/SKILL.md`), a few files at a time.

## Scope

- `nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/` (23 files) - Meadow's board port,
  not vendored NuttX core.
- `apps/system/hcom/` (63 files) - Meadow's user-space HCOM protocol handler.
- `F7MeadowOS/` (169 files) - Meadow's kernel-space extensions. `F7MeadowOS/libyaml/` and
  `F7MeadowOS/libcyaml/` are excluded (22 files): vendored third-party code that
  `Claude.md` marks "do not modify", so reviewing them for fixes here isn't useful.

Total in scope: 255 files.

## Status legend

- **Not started** - not yet reviewed.
- **Reviewed** - passed through `review-files`; see Notes for a findings summary.

This file is updated automatically by the `review-files` skill after each batch. If you
review a file some other way, update its row manually to keep this list accurate.

## Open Findings (fix tracking)

Every confirmed finding from a review batch gets a row here, regardless of how many
files it spans. Work through these one at a time -- oldest/most severe first is a
reasonable default, but use judgment. When a finding is fixed, flip Status to "Fixed",
fill in Fixed Date, and note the branch/commit. A finding can also be closed as "Won't
Fix" -- no code change, but the decision and reasoning are recorded in the Summary column
so it doesn't get silently re-flagged and re-litigated by a future review pass. Don't
delete resolved rows either way; they're the record of what's been decided.

| ID | Score | Files | Summary | Found | Status | Fixed | Branch/Commit |
|----|-------|-------|---------|-------|--------|-------|---------------|
| F001 | 80 | apps/system/hcom/comms/hcom_host_enq_deq.c:204-206, hcom_host_watchdog.c:109-152, hcom_host_preprocess.c | Stale watchdog-wakeup flag can abort an unrelated healthy download (single flag disambiguating two producers of the same semaphore). Risk if unfixed: an in-progress, healthy download can be silently aborted (file closed/deleted, buffer wiped) at a later unrelated moment, or a NULL pointer formatted into %s if idle when it fires. Cost/risk to fix: moderate -- needs the enqueue/dequeue fast path to consume or clear the stale flag/semaphore count, touches 3 files' interaction, worth a hardware-verified test of an interrupted download. | 2026-08-13 | Fixed | 2026-08-14 | fixF001WdogFlag |
| F002 | 80 | apps/system/hcom/comms/hcom_host_preprocess.c:478,541,587 | Missing minimum-length check lets stale reused-buffer bytes reach command dispatch, including destructive request types. Risk if unfixed: a short/garbled serial packet can cause Meadow to act on a bogus command (e.g. bulk flash erase, delete-file) built from leftover bytes of the previous real packet. Cost/risk to fix: small -- add a minimum-length check before casting to the header struct (and/or zero the reused decode buffer between packets); low regression risk, easy to unit-test with a short/garbled input. | 2026-08-13 | Fixed | 2026-08-14 | fixF002PacketLenCheck |
| F003 | 75 | apps/system/hcom/comms/hcom_host_receive.c:170,267-268,314 | Unlocked fd close race: `_comms_read_fd` closed/reassigned from both the receive pthread and a power-management callback with no lock, unlike the equivalent fd in hcom_host_send.c which is semaphore-guarded. Risk if unfixed: rare double-close of a recycled fd during low-power sleep/wake (a routine, frequent event), which could silently close an unrelated subsystem's fd -- hard to diagnose if it ever fires. Cost/risk to fix: small -- mirror the existing `_hostXmitSem`-guard pattern already used in hcom_host_send.c for the identical hazard; low regression risk since it copies a proven pattern. | 2026-08-13 | Fixed | 2026-08-14 | fixF003RecvFdRace |
| F004 | 75 | apps/system/hcom/comms/hcom_host_encode.c:127-129 | COBS decoder copies literal-byte runs one byte at a time instead of a single memcpy, despite already knowing the exact run length. Risk if unfixed: none to correctness, just wasted CPU cycles on this hot per-packet path during every download. Cost/risk to fix: trivial -- one-line memcpy swap; verified the source/dest buffers never alias, so it's a safe drop-in replacement. Purely a low-risk/high-return code-quality item. | 2026-08-13 | Fixed | 2026-08-14 | fixF004CobsMemcpy |
| F005 | 75 | apps/system/hcom/comms/hcom_host_send.c:676-701 | Write-blocked retry path polls with a fixed 50ms usleep (up to 30s worst case) instead of blocking on poll()/select() for POLLOUT, while holding the send semaphore the whole time. Risk if unfixed: added latency and, during large uploads, blocks all other host-bound messages for up to tens of seconds (the code's own comment documents up to 19.5s observed in the field) -- a responsiveness issue, not a correctness bug. Cost/risk to fix: moderate -- swap to poll()/select() with a remaining-timeout budget across retries; confirmed NuttX's serial driver supports POLLOUT on this device, so no redesign needed, but it's more than a one-line change. | 2026-08-13 | Fixed | 2026-08-14 | fixF005WriteRetryPoll |
| F006 | 60 | apps/system/hcom/comms/hcom_host_send.c:422-490 | hcom_host_send_buffered_msg mallocs/frees a fresh buffer per call with a non-empty body (deprecated-but-still-used path, e.g. app stdout/stderr redirection to CLI, file listing). Risk if unfixed: heap fragmentation risk over long device uptime; impact is bursty (active CLI session) not continuous. Fixed via lazy allocation (on first use, not at setup()) rather than a permanent buffer -- this comms path is rarely exercised on a deployed device (mostly during initial site provisioning) and Meadow has only 64MB total, so an always-allocated buffer was rejected in favor of one that costs nothing on devices that never use it. | 2026-08-13 | Fixed | 2026-08-14 | fixF006SendBufReuse |
| F007 | 25 | apps/system/hcom/comms/hcom_host_enq_deq.c:107-116, hcom_host_receive.c:204-213, hcom_host_send.c:151-163 | Several `*_shutdown()` functions free shared buffers/destroy semaphores with no synchronization against threads that may still be using them. Risk if unfixed: currently none live -- confirmed the sole caller (`hcom_manager_shutdown()`) has zero call sites anywhere and its own comment says "never called," so this is a dormant landmine, not an active defect. Decision: won't fix now -- it's dead code with nothing to verify against, so a fix here can't be built or hardware-tested, only reasoned about. If `hcom_manager_shutdown()` is ever wired up in the future, this file's `*_shutdown()` functions must gain proper thread-join/synchronization *before* that happens, or it becomes a real use-after-free/destroyed-semaphore bug at that point. | 2026-08-13 | Won't Fix | 2026-08-14 | N/A -- dead code, no fix applied |
| F008 | 80 | apps/system/hcom/diag/hcom_diag_trace_priority.c:90 | `meadow_configuration_t *config` is declared only inside `#if defined(CONFIG_STM32F7_PWR)` but dereferenced unconditionally afterward (syslog calls ~152,164 and a free-resources call ~168). Risk if unfixed: a hard compile error ("config undeclared") the moment anyone disables CONFIG_STM32F7_PWR on a build that still compiles this file -- it's a plain, user-selectable Kconfig bool defaulting to n, only the shipping defconfig happens to have it on today. Fixed by moving the `config` fetch outside the #if guard entirely (it's needed unconditionally for the startup banner, not just the PWR path); confirmed meadow_configuration_t/meadow_os_deep_copy_config() aren't themselves gated behind that config. Verified via normal build/flash (confirms no regression on the shipping CONFIG_STM32F7_PWR=y path; the no-PWR compile path itself wasn't separately built). | 2026-08-17 | Fixed | 2026-08-17 | fixF008ConfigScope |
| F009 | 75 | apps/system/hcom/diag/hcom_traverse_ku_user.c:115 | A Unicode em dash in a comment violates Claude.md's explicit ASCII-only character encoding rule. Risk if unfixed: none functionally, but it's a direct violation of an explicit repo rule (confirmed via hexdump: bytes e2 80 94). Fixed -- comment-only change, no compiled code affected, so no build/hardware test needed. | 2026-08-17 | Fixed | 2026-08-17 | fixF009AsciiEmDash |
| F010 | 75 | apps/system/hcom/diag/hcom_diag_gpio.h:47-50, hcom_diag_gpio.c | Four declared functions (`hcom_diag_gpio_config_alt`, `_set_high_alt`, `_set_low_alt`, `_set_pulse_alt`) are never defined anywhere and have zero callers repo-wide -- not even in this module's own test file. Risk if unfixed: none functionally (no link error since nothing references them), but it's dead, misleading API surface. Resolved as deletion, not implementation: unlike the `_alt(int alt_access_fd, ...)` convention elsewhere in hcom (e.g. `hcom_via_nx_update_bbr_alt`, needed because those go through an ioctl() to a kernel driver for tasks without the main hcom task's fds), the non-alt GPIO functions call stm32_configgpio/stm32_gpiowrite directly as plain syscalls with no fd involved at all -- there's no meaningful fd-taking variant to implement. Comment/header-only change, no compiled code affected. | 2026-08-17 | Fixed | 2026-08-17 | fixF010GpioAltCleanup |
| F011 | 65 | apps/system/hcom/diag/hcom_traverse_ku_user.c:128 | `traverse_ku_recv_pthread()`'s EOF branch doesn't reset the envelope byte-accumulator (`total`) to 0. Risk if unfixed: confirmed the kernel-side writer does close/reopen its FIFO write end after initial connection (both an on-target test and a real EPIPE-recovery path) -- if a partial envelope was abandoned mid-write when that happens, a reconnected writer's fresh envelope gets spliced onto the stale offset, corrupting the message forwarded to the CLI. Confidence capped by the kernel side's own (somewhat inconsistent) claim that envelope writes are meant to be atomic. Fixed by resetting `total = 0` in the EOF branch. Hardware-verified via kernel test 22 -v 8 (cross-context force-close/reopen): both `cross-context/opened-by-kthread` and `cross-context/sent-by-this-task` arrived at the CLI intact and un-spliced. | 2026-08-17 | Fixed | 2026-08-17 | fixF011TraverseEofReset |
| F012 | 50 | apps/system/hcom/diag/hcom_diag_misc.c:150,152,154,156 | `hcom_diag_print_buffer_x()`'s ASCII-column snprintf calls compute their size bound from `hexOffset` instead of `asciiOffset`, overstating real remaining buffer space (traced worst case: told 20 bytes available, only 4 actually remain). Risk if unfixed: none today (every write here is 1 char + NUL, and the function is gated behind a diagnostic flag that defaults off) but would become a real stack buffer overflow if ever modified to write longer per-byte representations. Cost/risk to fix: trivial -- swap `hexOffset` for `asciiOffset` in the four size arguments. | 2026-08-17 | Open | | |
| F013 | 30 | apps/system/hcom/diag/hcom_traverse_ku_user.c:125 | `traverse_ku_recv_pthread()`'s generic (non-EINTR) negative-read branch retries indefinitely at a fixed 5ms cadence with no backoff and no errno logging, on a thread that runs for the device's entire life. Risk if unfixed: traced the actual pipe driver and fd usage -- no realistic trigger identified today (this branch is a defensive catch-all), but if ever reached by a real persistent fault it would spin silently forever with zero diagnostic trace. Cost/risk to fix: small -- log the errno (at least rate-limited) and consider a backoff cap; pure hardening, no behavior change on the happy path. | 2026-08-17 | Open | | |
| F014 | 25 | apps/system/hcom/diag/hcom_traverse_ku_user.c:196 | `hcom_traverse_ku_init_user_setup()`'s "safe to call multiple times" guard has a genuine TOCTOU gap: `_thread_running` is only set true from inside the spawned thread itself, after `pthread_create()` already returned. Risk if unfixed: currently none live -- confirmed exactly one call site in the whole repo (`hcom_startup_manager.c:420`, inside NuttX's single once-only boot entry point), with no retry logic anywhere that could invoke it twice. Cost/risk to fix: small if ever needed -- set `_thread_running = true` synchronously in the setup function before `pthread_create()` rather than inside the thread; only worth doing if this function is ever called from more than one place in the future. | 2026-08-17 | Open | | |
| F015 | 80 | F7MeadowOS/kerneltests/meadow_kt_traverse_ku_tests.c:116,152,190,199,235,314 | All six `hcom_traverse_ku_send()` calls in this test file pass a hardcoded `0` (`HCOM_HOST_REQUEST_UNDEFINED_REQUEST`) for requestType, bypassing the two safe wrapper functions production code uses correctly. Risk if unfixed: the test's own stated verification method ("pass/fail is confirmed by observing the messages at the host CLI") never actually works -- the CLI has no rendering rule for an undefined request type, so `PASS` only proves the kernel-side send succeeded, not that the text was ever displayed. Confirmed scoped to this one file (all 6 of its own call sites); production wrappers (`hcom_traverse_ku_syslog_to_host()`, `hcom_traverse_ku_text_to_host()`) pass correct types, and no other kernel test file calls this API directly. Fixed: initially tried `HCOM_HOST_REQUEST_TEXT_TRACE_MSG` (matching the production SYSLOG_TEXT-type wrapper) but hardware-verified that Meadow.CLI silently drops that type -- switched all six call sites to `HCOM_HOST_REQUEST_TEXT_INFORMATION` instead, confirmed via hardware to actually display. Found while verifying F011 using this file's test case 8. | 2026-08-17 | Fixed | 2026-08-17 | fixF011TraverseEofReset |

## nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/ (23 files)

| ID | File | Status | Date | Notes |
|----|------|--------|------|-------|
| A01 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/board_corecomp.c | Not started | | |
| A02 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/board_projlab.c | Not started | | |
| A03 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_appinit.c | Not started | | |
| A04 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_appinitialize.c | Not started | | |
| A05 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_autoleds.c | Not started | | |
| A06 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_boot.c | Not started | | |
| A07 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_bringup.c | Not started | | |
| A08 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_buttons.c | Not started | | |
| A09 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_composite.c | Not started | | |
| A10 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_extmem.c | Not started | | |
| A11 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_i2c.c | Not started | | |
| A12 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_i2c.h | Not started | | |
| A13 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_idle.c | Not started | | |
| A14 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_ioctl.c | Not started | | |
| A15 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_ostest.c | Not started | | |
| A16 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_pwm.c | Not started | | |
| A17 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_reset.c | Not started | | |
| A18 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_sdmmc.c | Not started | | |
| A19 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_spi.c | Not started | | |
| A20 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_sporadic.c | Not started | | |
| A21 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_usb.c | Not started | | |
| A22 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_usbmsc.c | Not started | | |
| A23 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_userleds.c | Not started | | |

## apps/system/hcom/ (63 files)

| ID | File | Status | Date | Notes |
|----|------|--------|------|-------|
| B01 | apps/system/hcom/cell/cmux/hcom_cmux.c | Not started | | |
| B02 | apps/system/hcom/cell/cmux/hcom_cmux.h | Not started | | |
| B03 | apps/system/hcom/cell/hcom_pppd.c | Not started | | |
| B04 | apps/system/hcom/cell/hcom_pppd.h | Not started | | |
| B05 | apps/system/hcom/comms/hcom_host_encode.c | Reviewed | 2026-08-13 | F004 |
| B06 | apps/system/hcom/comms/hcom_host_enq_deq.c | Reviewed | 2026-08-13 | F001, F007 |
| B07 | apps/system/hcom/comms/hcom_host_preprocess.c | Reviewed | 2026-08-13 | F001, F002 |
| B08 | apps/system/hcom/comms/hcom_host_receive.c | Reviewed | 2026-08-13 | F003, F007 |
| B09 | apps/system/hcom/comms/hcom_host_route.c | Reviewed | 2026-08-13 | No findings |
| B10 | apps/system/hcom/comms/hcom_host_send.c | Reviewed | 2026-08-13 | F005, F006, F007 |
| B11 | apps/system/hcom/comms/hcom_host_watchdog.c | Reviewed | 2026-08-13 | F001 |
| B12 | apps/system/hcom/diag/hcom_diag_decode_protocol.c | Reviewed | 2026-08-17 | No confirmed findings |
| B13 | apps/system/hcom/diag/hcom_diag_gpio.c | Reviewed | 2026-08-17 | F010 |
| B14 | apps/system/hcom/diag/hcom_diag_gpio.h | Reviewed | 2026-08-17 | F010 |
| B15 | apps/system/hcom/diag/hcom_diag_misc.c | Reviewed | 2026-08-17 | F012 |
| B16 | apps/system/hcom/diag/hcom_diag_nsh_support.c | Reviewed | 2026-08-17 | No confirmed findings |
| B17 | apps/system/hcom/diag/hcom_diag_trace_priority.c | Reviewed | 2026-08-17 | F008 |
| B18 | apps/system/hcom/diag/hcom_traverse_ku_user.c | Reviewed | 2026-08-17 | F009, F011, F013, F014 |
| B19 | apps/system/hcom/esp32/hcom_esp32_comms.h | Not started | | |
| B20 | apps/system/hcom/esp32/hcom_esp32_execute.c | Not started | | |
| B21 | apps/system/hcom/esp32/hcom_esp32_network_monitor.c | Not started | | |
| B22 | apps/system/hcom/esp32/hcom_esp32_network_monitor.h | Not started | | |
| B23 | apps/system/hcom/esp32/hcom_esp32_proc_recv.c | Not started | | |
| B24 | apps/system/hcom/esp32/hcom_esp32_proc_util.c | Not started | | |
| B25 | apps/system/hcom/esp32/hcom_esp32_proc_xmit.c | Not started | | |
| B26 | apps/system/hcom/esp32/hcom_esp32_uart_comms.c | Not started | | |
| B27 | apps/system/hcom/file/hcom_file_delete_stm32f7.c | Not started | | |
| B28 | apps/system/hcom/file/hcom_file_dir_mgmt_utils.c | Not started | | |
| B29 | apps/system/hcom/file/hcom_file_dnld_esp32.c | Not started | | |
| B30 | apps/system/hcom/file/hcom_file_dnld_stm32f7.c | Not started | | |
| B31 | apps/system/hcom/file/hcom_file_lists.c | Not started | | |
| B32 | apps/system/hcom/file/hcom_file_lists_subdir.c | Not started | | |
| B33 | apps/system/hcom/file/hcom_file_misc.c | Not started | | |
| B34 | apps/system/hcom/file/hcom_file_upld_proc.c | Not started | | |
| B35 | apps/system/hcom/file/hcom_file_write_stm32f7.c | Not started | | |
| B36 | apps/system/hcom/hcom_common.h | Not started | | |
| B37 | apps/system/hcom/hcom_startup_manager.c | Not started | | |
| B38 | apps/system/hcom/misc/espcp_utils.c | Not started | | |
| B39 | apps/system/hcom/misc/espcp_utils.h | Not started | | |
| B40 | apps/system/hcom/misc/hcom_common_utils.c | Not started | | |
| B41 | apps/system/hcom/misc/meadow_cirbuf.c | Not started | | |
| B42 | apps/system/hcom/misc/meadow_utils.c | Not started | | |
| B43 | apps/system/hcom/mono/hcom_mono_control.c | Not started | | |
| B44 | apps/system/hcom/mono/hcom_remote_vs_debug.c | Not started | | |
| B45 | apps/system/hcom/mono/hcom_stdxxx_redirect.c | Not started | | |
| B46 | apps/system/hcom/nx_rqsts/hcom_bbreg_access.c | Not started | | |
| B47 | apps/system/hcom/nx_rqsts/hcom_misc_requests.c | Not started | | |
| B48 | apps/system/hcom/nx_rqsts/hcom_via_nx_access.c | Not started | | |
| B49 | apps/system/hcom/ota/hcom_ota.c | Not started | | |
| B50 | apps/system/hcom/ota/hcom_ota.h | Not started | | |
| B51 | apps/system/hcom/tests/bbreg_tests.c | Not started | | |
| B52 | apps/system/hcom/tests/cell_tests.c | Not started | | |
| B53 | apps/system/hcom/tests/developer_tests.c | Not started | | |
| B54 | apps/system/hcom/tests/diag_ethernet_chat_test.c | Not started | | |
| B55 | apps/system/hcom/tests/diag_gpio_tests.c | Not started | | |
| B56 | apps/system/hcom/tests/diag_misc_tests.c | Not started | | |
| B57 | apps/system/hcom/tests/hcom_dir_mgmt_tests.c | Not started | | |
| B58 | apps/system/hcom/tests/meadow_bbd_tests.c | Not started | | |
| B59 | apps/system/hcom/tests/meadow_os_userspace_tests.c | Not started | | |
| B60 | apps/system/hcom/tests/meadow_sqlite_tests.c | Not started | | |
| B61 | apps/system/hcom/tests/sotest_tests.c | Not started | | |
| B62 | apps/system/hcom/tests/tensorflow_tests.c | Not started | | |
| B63 | apps/system/hcom/tests/vs_debug_tests.c | Not started | | |

## F7MeadowOS/ (169 files, libyaml/libcyaml excluded)

| ID | File | Status | Date | Notes |
|----|------|--------|------|-------|
| C001 | F7MeadowOS/espcp/espcp_common.c | Not started | | |
| C002 | F7MeadowOS/espcp/espcp_common.h | Not started | | |
| C003 | F7MeadowOS/espcp/espcp_coprocessor.c | Not started | | |
| C004 | F7MeadowOS/espcp/espcp_coprocessor.h | Not started | | |
| C005 | F7MeadowOS/espcp/espcp_core_dump.c | Not started | | |
| C006 | F7MeadowOS/espcp/espcp_core_dump.h | Not started | | |
| C007 | F7MeadowOS/espcp/espcp_encoders.c | Not started | | |
| C008 | F7MeadowOS/espcp/espcp_encoders.h | Not started | | |
| C009 | F7MeadowOS/espcp/espcp_event_handlers.c | Not started | | |
| C010 | F7MeadowOS/espcp/espcp_event_handlers.h | Not started | | |
| C011 | F7MeadowOS/espcp/espcp_file_system.c | Not started | | |
| C012 | F7MeadowOS/espcp/espcp_file_system.h | Not started | | |
| C013 | F7MeadowOS/espcp/espcp_message.c | Not started | | |
| C014 | F7MeadowOS/espcp/espcp_message.h | Not started | | |
| C015 | F7MeadowOS/espcp/espcp_message_dispatcher.c | Not started | | |
| C016 | F7MeadowOS/espcp/espcp_message_dispatcher.h | Not started | | |
| C017 | F7MeadowOS/espcp/espcp_queue.c | Not started | | |
| C018 | F7MeadowOS/espcp/espcp_queue.h | Not started | | |
| C019 | F7MeadowOS/espcp/espcp_shared_enums.h | Not started | | |
| C020 | F7MeadowOS/espcp/espcp_system.c | Not started | | |
| C021 | F7MeadowOS/espcp/espcp_system.h | Not started | | |
| C022 | F7MeadowOS/espcp/espcp_test_heap_tracing.h | Not started | | |
| C023 | F7MeadowOS/espcp/espcp_thread.c | Not started | | |
| C024 | F7MeadowOS/espcp/espcp_thread.h | Not started | | |
| C025 | F7MeadowOS/espcp/espcp_uart_monitor.c | Not started | | |
| C026 | F7MeadowOS/espcp/espcp_uart_monitor.h | Not started | | |
| C027 | F7MeadowOS/espcp/espcp_usrsock.h | Not started | | |
| C028 | F7MeadowOS/espcp/espcp_usrsock_sockif.c | Not started | | |
| C029 | F7MeadowOS/espcp/espcp_wifi.c | Not started | | |
| C030 | F7MeadowOS/espcp/espcp_wifi.h | Not started | | |
| C031 | F7MeadowOS/espcp/generic_list.c | Not started | | |
| C032 | F7MeadowOS/espcp/generic_list.h | Not started | | |
| C033 | F7MeadowOS/ethernet/meadow_ethnet_connect.c | Not started | | |
| C034 | F7MeadowOS/ethernet/meadow_ethnet_dhcp.c | Not started | | |
| C035 | F7MeadowOS/ethernet/meadow_ethnet_local.h | Not started | | |
| C036 | F7MeadowOS/ethernet/meadow_ethnet_monitor.c | Not started | | |
| C037 | F7MeadowOS/ethernet/meadow_ethnet_start.c | Not started | | |
| C038 | F7MeadowOS/ethernet/meadow_ethnet_utils.c | Not started | | |
| C039 | F7MeadowOS/hcom_nx/create_fs/hcom_nx_fs_create.c | Not started | | |
| C040 | F7MeadowOS/hcom_nx/create_fs/hcom_nx_fs_flash.c | Not started | | |
| C041 | F7MeadowOS/hcom_nx/create_fs/hcom_nx_fs_flash_tests.c | Not started | | |
| C042 | F7MeadowOS/hcom_nx/create_fs/hcom_nx_fs_littlefs.c | Not started | | |
| C043 | F7MeadowOS/hcom_nx/create_fs/hcom_nx_fs_part_utils.c | Not started | | |
| C044 | F7MeadowOS/hcom_nx/diag/hcom_nx_trace_msg_proc.c | Not started | | |
| C045 | F7MeadowOS/hcom_nx/diag/hcom_traverse_ku_kernel.c | Not started | | |
| C046 | F7MeadowOS/hcom_nx/hcom_nx_bbreg_access.c | Not started | | |
| C047 | F7MeadowOS/hcom_nx/hcom_nx_common.h | Not started | | |
| C048 | F7MeadowOS/hcom_nx/hcom_nx_common_utils.c | Not started | | |
| C049 | F7MeadowOS/hcom_nx/hcom_nx_config_manager.c | Not started | | |
| C050 | F7MeadowOS/hcom_nx/hcom_nx_config_manager.h | Not started | | |
| C051 | F7MeadowOS/hcom_nx/hcom_nx_config_manager_yaml.h | Not started | | |
| C052 | F7MeadowOS/hcom_nx/hcom_nx_startup_mgr.c | Not started | | |
| C053 | F7MeadowOS/hcom_nx/hcom_nx_upd.c | Not started | | |
| C054 | F7MeadowOS/hcom_nx/hcom_nx_upd_route_cmd.c | Not started | | |
| C055 | F7MeadowOS/hcom_nx/tests/hcom_nx_qspi_flash_tests.c | Not started | | |
| C056 | F7MeadowOS/include/meadow/bootloader/meadow_os_partitions.h | Not started | | |
| C057 | F7MeadowOS/include/meadow/hcom_bbreg_defn.h | Not started | | |
| C058 | F7MeadowOS/include/meadow/hcom_dfu_mode_entry.h | Not started | | |
| C059 | F7MeadowOS/include/meadow/hcom_dnld_shared.h | Not started | | |
| C060 | F7MeadowOS/include/meadow/hcom_misc_diag.h | Not started | | |
| C061 | F7MeadowOS/include/meadow/hcom_nuttx_shared.h | Not started | | |
| C062 | F7MeadowOS/include/meadow/hcom_protocol.h | Not started | | |
| C063 | F7MeadowOS/include/meadow/hcom_shared_common.h | Not started | | |
| C064 | F7MeadowOS/include/meadow/hcom_traverse_ku.h | Not started | | |
| C065 | F7MeadowOS/include/meadow/hcom_upd_shared.h | Not started | | |
| C066 | F7MeadowOS/include/meadow/meadow-upd.h | Not started | | |
| C067 | F7MeadowOS/include/meadow/meadow_apps_core_share.h | Not started | | |
| C068 | F7MeadowOS/include/meadow/meadow_cirbuf.h | Not started | | |
| C069 | F7MeadowOS/include/meadow/meadow_client_cert.h | Not started | | |
| C070 | F7MeadowOS/include/meadow/meadow_cloud.h | Not started | | |
| C071 | F7MeadowOS/include/meadow/meadow_debug_breakpoint_macro.h | Not started | | |
| C072 | F7MeadowOS/include/meadow/meadow_debug_helpers.h | Not started | | |
| C073 | F7MeadowOS/include/meadow/meadow_ethnet_common.h | Not started | | |
| C074 | F7MeadowOS/include/meadow/meadow_extended_errno.h | Not started | | |
| C075 | F7MeadowOS/include/meadow/meadow_gpio_counter.h | Not started | | |
| C076 | F7MeadowOS/include/meadow/meadow_hw_version.h | Not started | | |
| C077 | F7MeadowOS/include/meadow/meadow_interrupt.h | Not started | | |
| C078 | F7MeadowOS/include/meadow/meadow_kernel_tests.h | Not started | | |
| C079 | F7MeadowOS/include/meadow/meadow_kt_dispatcher.h | Not started | | |
| C080 | F7MeadowOS/include/meadow/meadow_measure_freq_shared.h | Not started | | |
| C081 | F7MeadowOS/include/meadow/meadow_os.h | Not started | | |
| C082 | F7MeadowOS/include/meadow/meadow_os_battery_backed_domain.h | Not started | | |
| C083 | F7MeadowOS/include/meadow/meadow_os_fault_handling.h | Not started | | |
| C084 | F7MeadowOS/include/meadow/meadow_os_persistent_data.h | Not started | | |
| C085 | F7MeadowOS/include/meadow/meadow_partition_share.h | Not started | | |
| C086 | F7MeadowOS/include/meadow/meadow_pwr_mgmt.h | Not started | | |
| C087 | F7MeadowOS/include/meadow/meadow_rotary_encoder.h | Not started | | |
| C088 | F7MeadowOS/include/meadow/meadow_sdmmc.h | Not started | | |
| C089 | F7MeadowOS/include/meadow/meadow_syscall_support.h | Not started | | |
| C090 | F7MeadowOS/include/meadow/meadow_thread_config.h | Not started | | |
| C091 | F7MeadowOS/include/meadow/meadow_unit_test_framework.h | Not started | | |
| C092 | F7MeadowOS/include/meadow/semaphore_lock_unlock.h | Not started | | |
| C093 | F7MeadowOS/include/meadow/stm32f777zit6-meadow.h | Not started | | |
| C094 | F7MeadowOS/kerneltests/ana_to_dig_conv_tests.c | Not started | | |
| C095 | F7MeadowOS/kerneltests/bg77_tests.c | Not started | | |
| C096 | F7MeadowOS/kerneltests/bg77_tests.h | Not started | | |
| C097 | F7MeadowOS/kerneltests/dig_to_ana_conv_tests.c | Not started | | |
| C098 | F7MeadowOS/kerneltests/espcp_tests.c | Not started | | |
| C099 | F7MeadowOS/kerneltests/ethernet_tests.c | Not started | | |
| C100 | F7MeadowOS/kerneltests/ethernet_tests.h | Not started | | |
| C101 | F7MeadowOS/kerneltests/iso8601_time_tests.c | Not started | | |
| C102 | F7MeadowOS/kerneltests/meadow_gpio_counter_tests.c | Not started | | |
| C103 | F7MeadowOS/kerneltests/meadow_i2c_tests.c | Not started | | |
| C104 | F7MeadowOS/kerneltests/meadow_interrupt_tests.c | Not started | | |
| C105 | F7MeadowOS/kerneltests/meadow_kt_bluetooth_tests.c | Not started | | |
| C106 | F7MeadowOS/kerneltests/meadow_kt_dispatcher.c | Not started | | |
| C107 | F7MeadowOS/kerneltests/meadow_kt_log.c | Not started | | |
| C108 | F7MeadowOS/kerneltests/meadow_kt_log.h | Not started | | |
| C109 | F7MeadowOS/kerneltests/meadow_kt_traverse_ku_tests.c | Not started | | |
| C110 | F7MeadowOS/kerneltests/meadow_measure_freq_tests.c | Not started | | |
| C111 | F7MeadowOS/kerneltests/meadow_os_tests.c | Not started | | |
| C112 | F7MeadowOS/kerneltests/meadow_pwm_tests.c | Not started | | |
| C113 | F7MeadowOS/kerneltests/meadow_rotary_encoder_tests.c | Not started | | |
| C114 | F7MeadowOS/kerneltests/meadow_spi_dma_tests.c | Not started | | |
| C115 | F7MeadowOS/kerneltests/network_tests.c | Not started | | |
| C116 | F7MeadowOS/kerneltests/network_tests.h | Not started | | |
| C117 | F7MeadowOS/kerneltests/power_management_tests.c | Not started | | |
| C118 | F7MeadowOS/kerneltests/quick_misc_tests.c | Not started | | |
| C119 | F7MeadowOS/kerneltests/sdcard_tests.c | Not started | | |
| C120 | F7MeadowOS/kerneltests/timing.h | Not started | | |
| C121 | F7MeadowOS/meadow_cloud/meadow_cloud.c | Not started | | |
| C122 | F7MeadowOS/meadow_os/meadow-gpio.c | Not started | | |
| C123 | F7MeadowOS/meadow_os/meadow-upd.c | Not started | | |
| C124 | F7MeadowOS/meadow_os/meadow_hw_version.c | Not started | | |
| C125 | F7MeadowOS/meadow_os/meadow_interrupt.c | Not started | | |
| C126 | F7MeadowOS/meadow_os/meadow_os.c | Not started | | |
| C127 | F7MeadowOS/meadow_os/meadow_os_battery_backed_domain.c | Not started | | |
| C128 | F7MeadowOS/meadow_os/meadow_os_config.c | Not started | | |
| C129 | F7MeadowOS/meadow_os/meadow_os_espcp.c | Not started | | |
| C130 | F7MeadowOS/meadow_os/meadow_os_exceptions.c | Not started | | |
| C131 | F7MeadowOS/meadow_os/meadow_os_fault_handling.c | Not started | | |
| C132 | F7MeadowOS/meadow_os/meadow_os_info.c | Not started | | |
| C133 | F7MeadowOS/meadow_os/meadow_sdmmc.c | Not started | | |
| C134 | F7MeadowOS/misc/meadow_adc.c | Not started | | |
| C135 | F7MeadowOS/misc/meadow_cirbuf.c | Not started | | |
| C136 | F7MeadowOS/misc/meadow_client_cert.c | Not started | | |
| C137 | F7MeadowOS/misc/meadow_logging.c | Not started | | |
| C138 | F7MeadowOS/misc/meadow_logging.h | Not started | | |
| C139 | F7MeadowOS/misc/meadow_reinit_flash_size.c | Not started | | |
| C140 | F7MeadowOS/misc/meadow_reinit_flash_size.h | Not started | | |
| C141 | F7MeadowOS/misc/meadow_stm32f7_clockenable.c | Not started | | |
| C142 | F7MeadowOS/misc/meadow_stm32f7_clockenable.h | Not started | | |
| C143 | F7MeadowOS/misc/meadow_watchdog.c | Not started | | |
| C144 | F7MeadowOS/misc/meadow_watchdog.h | Not started | | |
| C145 | F7MeadowOS/misc/parse_iso8601_time.c | Not started | | |
| C146 | F7MeadowOS/ntpclient/meadow_ntpclient.c | Not started | | |
| C147 | F7MeadowOS/ntpclient/meadow_ntpclient.h | Not started | | |
| C148 | F7MeadowOS/ntpclient/ntpv3.h | Not started | | |
| C149 | F7MeadowOS/pwrmgmt/pwrmgmt_calib_lsi_clock.c | Not started | | |
| C150 | F7MeadowOS/pwrmgmt/pwrmgmt_config_wakeup_alarm.c | Not started | | |
| C151 | F7MeadowOS/pwrmgmt/pwrmgmt_config_wakeup_timer.c | Not started | | |
| C152 | F7MeadowOS/pwrmgmt/pwrmgmt_control.c | Not started | | |
| C153 | F7MeadowOS/pwrmgmt/pwrmgmt_enter_stop_mode.c | Not started | | |
| C154 | F7MeadowOS/pwrmgmt/pwrmgmt_local.h | Not started | | |
| C155 | F7MeadowOS/pwrmgmt/pwrmgmt_misc.c | Not started | | |
| C156 | F7MeadowOS/pwrmgmt/pwrmgmt_switch_rtc_clock.c | Not started | | |
| C157 | F7MeadowOS/pwrmgmt/pwrmgmt_time_support.c | Not started | | |
| C158 | F7MeadowOS/specialized/meadow_gpio_counter.c | Not started | | |
| C159 | F7MeadowOS/specialized/meadow_idle_monitor.c | Not started | | |
| C160 | F7MeadowOS/specialized/meadow_measure_freq.c | Not started | | |
| C161 | F7MeadowOS/specialized/meadow_measure_freq_local.h | Not started | | |
| C162 | F7MeadowOS/specialized/meadow_rotary_encoder.c | Not started | | |
| C163 | F7MeadowOS/timers/cpu_measure.c | Not started | | |
| C164 | F7MeadowOS/timers/freq_duty.c | Not started | | |
| C165 | F7MeadowOS/timers/lsi_clk_freq_measure.c | Not started | | |
| C166 | F7MeadowOS/timers/meadow_timers.h | Not started | | |
| C167 | F7MeadowOS/timers/pulse_width.c | Not started | | |
| C168 | F7MeadowOS/timers/rc_servo_decode.c | Not started | | |
| C169 | F7MeadowOS/timers/timer_manager.c | Not started | | |
