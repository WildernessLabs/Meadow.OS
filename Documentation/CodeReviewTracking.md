# Code Review Tracking

Tracks progress of an ongoing file-by-file bug/concurrency/performance review across the
three source locations below. Reviews are performed with the `review-files` Claude Code
skill (`.claude/skills/review-files/SKILL.md`), a few files at a time.

**Review timeline:** started 2026-08-13, last updated 2026-08-24.

## Scope

- `nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/` (23 files) - Meadow's board port,
  not vendored NuttX core.
- `apps/system/hcom/` (63 files) - Meadow's user-space HCOM protocol handler.
- `F7MeadowOS/` (168 files) - Meadow's kernel-space extensions. `F7MeadowOS/libyaml/` and
  `F7MeadowOS/libcyaml/` are excluded (22 files): vendored third-party code that
  `Claude.md` marks "do not modify", so reviewing them for fixes here isn't useful.

Total in scope: 254 files.

## Status legend

- **Not started** - not yet reviewed.
- **Reviewed** - passed through `review-files`; see the `Issue` column for that file's
  findings (a `code-review`-labeled GitHub Issue), or `Notes` for a quick remark (e.g.
  "No confirmed findings" or a cross-reference to another file's issue).

This file is updated automatically by the `review-files` skill after each batch. If you
review a file some other way, update its row manually to keep this list accurate.

## nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/ (23 files)

| ID | File | Status | Date | Issue | Notes |
|----|------|--------|------|-------|-------|
| A01 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/board_corecomp.c | Not started |  |  |  |
| A02 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/board_projlab.c | Not started |  |  |  |
| A03 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_appinit.c | Not started |  |  |  |
| A04 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_appinitialize.c | Not started |  |  |  |
| A05 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_autoleds.c | Not started |  |  |  |
| A06 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_boot.c | Not started |  |  |  |
| A07 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_bringup.c | Not started |  |  |  |
| A08 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_buttons.c | Not started |  |  |  |
| A09 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_composite.c | Not started |  |  |  |
| A10 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_extmem.c | Not started |  |  |  |
| A11 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_i2c.c | Not started |  |  |  |
| A12 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_i2c.h | Not started |  |  |  |
| A13 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_idle.c | Not started |  |  |  |
| A14 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_ioctl.c | Not started |  |  |  |
| A15 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_ostest.c | Not started |  |  |  |
| A16 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_pwm.c | Not started |  |  |  |
| A17 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_reset.c | Not started |  |  |  |
| A18 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_sdmmc.c | Not started |  |  |  |
| A19 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_spi.c | Not started |  |  |  |
| A20 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_sporadic.c | Not started |  |  |  |
| A21 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_usb.c | Not started |  |  |  |
| A22 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_usbmsc.c | Not started |  |  |  |
| A23 | nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_userleds.c | Not started |  |  |  |

## apps/system/hcom/ (63 files)

| ID | File | Status | Date | Issue | Notes |
|----|------|--------|------|-------|-------|
| B01 | apps/system/hcom/cell/cmux/hcom_cmux.c | Not started |  |  |  |
| B02 | apps/system/hcom/cell/cmux/hcom_cmux.h | Not started |  |  |  |
| B03 | apps/system/hcom/cell/hcom_pppd.c | Not started |  |  |  |
| B04 | apps/system/hcom/cell/hcom_pppd.h | Not started |  |  |  |
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
| B19 | apps/system/hcom/esp32/hcom_esp32_comms.h | Not started |  |  |  |
| B20 | apps/system/hcom/esp32/hcom_esp32_execute.c | Not started |  |  |  |
| B21 | apps/system/hcom/esp32/hcom_esp32_network_monitor.c | Not started |  |  |  |
| B22 | apps/system/hcom/esp32/hcom_esp32_network_monitor.h | Not started |  |  |  |
| B23 | apps/system/hcom/esp32/hcom_esp32_proc_recv.c | Not started |  |  |  |
| B24 | apps/system/hcom/esp32/hcom_esp32_proc_util.c | Not started |  |  |  |
| B25 | apps/system/hcom/esp32/hcom_esp32_proc_xmit.c | Not started |  |  |  |
| B26 | apps/system/hcom/esp32/hcom_esp32_uart_comms.c | Not started |  |  |  |
| B27 | apps/system/hcom/file/hcom_file_delete_stm32f7.c | Reviewed | 2026-08-19 |  | No confirmed findings |
| B28 | apps/system/hcom/file/hcom_file_dir_mgmt_utils.c | Reviewed | 2026-08-19 | #309 |  |
| B29 | apps/system/hcom/file/hcom_file_dnld_esp32.c | Reviewed | 2026-08-19 | #310 |  |
| B30 | apps/system/hcom/file/hcom_file_dnld_stm32f7.c | Reviewed | 2026-08-19 | #311 |  |
| B31 | apps/system/hcom/file/hcom_file_lists.c | Reviewed | 2026-08-19 | #312 |  |
| B32 | apps/system/hcom/file/hcom_file_lists_subdir.c | Reviewed | 2026-08-19 |  | No confirmed findings |
| B33 | apps/system/hcom/file/hcom_file_misc.c | Reviewed | 2026-08-19 | #313 |  |
| B34 | apps/system/hcom/file/hcom_file_upld_proc.c | Reviewed | 2026-08-19 | #314 |  |
| B35 | apps/system/hcom/file/hcom_file_write_stm32f7.c | Reviewed | 2026-08-19 | #315 |  |
| B36 | apps/system/hcom/hcom_common.h | Not started |  |  |  |
| B37 | apps/system/hcom/hcom_startup_manager.c | Not started |  |  |  |
| B38 | apps/system/hcom/misc/espcp_utils.c | Reviewed | 2026-08-24 | #318 |  |
| B39 | apps/system/hcom/misc/espcp_utils.h | Reviewed | 2026-08-24 |  | No confirmed findings |
| B40 | apps/system/hcom/misc/hcom_common_utils.c | Reviewed | 2026-08-24 | #319 |  |
| B41 | apps/system/hcom/misc/meadow_cirbuf.c | Reviewed | 2026-08-24 | #320 |  |
| B42 | apps/system/hcom/misc/meadow_utils.c | Reviewed | 2026-08-24 |  | No confirmed findings (stub file, no function bodies) |
| B43 | apps/system/hcom/mono/hcom_mono_control.c | Not started |  |  |  |
| B44 | apps/system/hcom/mono/hcom_remote_vs_debug.c | Not started |  |  |  |
| B45 | apps/system/hcom/mono/hcom_stdxxx_redirect.c | Not started |  |  |  |
| B46 | apps/system/hcom/nx_rqsts/hcom_bbreg_access.c | Not started |  |  |  |
| B47 | apps/system/hcom/nx_rqsts/hcom_misc_requests.c | Not started |  |  |  |
| B48 | apps/system/hcom/nx_rqsts/hcom_via_nx_access.c | Not started |  |  |  |
| B49 | apps/system/hcom/ota/hcom_ota.c | Not started |  |  |  |
| B50 | apps/system/hcom/ota/hcom_ota.h | Not started |  |  |  |
| B51 | apps/system/hcom/tests/bbreg_tests.c | Not started |  |  |  |
| B52 | apps/system/hcom/tests/cell_tests.c | Not started |  |  |  |
| B53 | apps/system/hcom/tests/developer_tests.c | Not started |  |  |  |
| B54 | apps/system/hcom/tests/diag_ethernet_chat_test.c | Not started |  |  |  |
| B55 | apps/system/hcom/tests/diag_gpio_tests.c | Not started |  |  |  |
| B56 | apps/system/hcom/tests/diag_misc_tests.c | Not started |  |  |  |
| B57 | apps/system/hcom/tests/hcom_dir_mgmt_tests.c | Not started |  |  |  |
| B58 | apps/system/hcom/tests/meadow_bbd_tests.c | Not started |  |  |  |
| B59 | apps/system/hcom/tests/meadow_os_userspace_tests.c | Not started |  |  |  |
| B60 | apps/system/hcom/tests/meadow_sqlite_tests.c | Not started |  |  |  |
| B61 | apps/system/hcom/tests/sotest_tests.c | Not started |  |  |  |
| B62 | apps/system/hcom/tests/tensorflow_tests.c | Not started |  |  |  |
| B63 | apps/system/hcom/tests/vs_debug_tests.c | Not started |  |  |  |

## F7MeadowOS/ (169 files, libyaml/libcyaml excluded)

| ID | File | Status | Date | Issue | Notes |
|----|------|--------|------|-------|-------|
| C001 | F7MeadowOS/espcp/espcp_common.c | Not started |  |  |  |
| C002 | F7MeadowOS/espcp/espcp_common.h | Not started |  |  |  |
| C003 | F7MeadowOS/espcp/espcp_coprocessor.c | Not started |  |  |  |
| C004 | F7MeadowOS/espcp/espcp_coprocessor.h | Not started |  |  |  |
| C005 | F7MeadowOS/espcp/espcp_core_dump.c | Not started |  |  |  |
| C006 | F7MeadowOS/espcp/espcp_core_dump.h | Not started |  |  |  |
| C007 | F7MeadowOS/espcp/espcp_encoders.c | Not started |  |  |  |
| C008 | F7MeadowOS/espcp/espcp_encoders.h | Not started |  |  |  |
| C009 | F7MeadowOS/espcp/espcp_event_handlers.c | Not started |  |  |  |
| C010 | F7MeadowOS/espcp/espcp_event_handlers.h | Not started |  |  |  |
| C011 | F7MeadowOS/espcp/espcp_file_system.c | Not started |  |  |  |
| C012 | F7MeadowOS/espcp/espcp_file_system.h | Not started |  |  |  |
| C013 | F7MeadowOS/espcp/espcp_message.c | Not started |  |  |  |
| C014 | F7MeadowOS/espcp/espcp_message.h | Not started |  |  |  |
| C015 | F7MeadowOS/espcp/espcp_message_dispatcher.c | Not started |  |  |  |
| C016 | F7MeadowOS/espcp/espcp_message_dispatcher.h | Not started |  |  |  |
| C017 | F7MeadowOS/espcp/espcp_queue.c | Not started |  |  |  |
| C018 | F7MeadowOS/espcp/espcp_queue.h | Not started |  |  |  |
| C019 | F7MeadowOS/espcp/espcp_shared_enums.h | Not started |  |  |  |
| C020 | F7MeadowOS/espcp/espcp_system.c | Not started |  |  |  |
| C021 | F7MeadowOS/espcp/espcp_system.h | Not started |  |  |  |
| C022 | F7MeadowOS/espcp/espcp_test_heap_tracing.h | Not started |  |  |  |
| C023 | F7MeadowOS/espcp/espcp_thread.c | Not started |  |  |  |
| C024 | F7MeadowOS/espcp/espcp_thread.h | Not started |  |  |  |
| C025 | F7MeadowOS/espcp/espcp_uart_monitor.c | Not started |  |  |  |
| C026 | F7MeadowOS/espcp/espcp_uart_monitor.h | Not started |  |  |  |
| C027 | F7MeadowOS/espcp/espcp_usrsock.h | Not started |  |  |  |
| C028 | F7MeadowOS/espcp/espcp_usrsock_sockif.c | Not started |  |  |  |
| C029 | F7MeadowOS/espcp/espcp_wifi.c | Not started |  |  |  |
| C030 | F7MeadowOS/espcp/espcp_wifi.h | Not started |  |  |  |
| C031 | F7MeadowOS/espcp/generic_list.c | Not started |  |  |  |
| C032 | F7MeadowOS/espcp/generic_list.h | Not started |  |  |  |
| C033 | F7MeadowOS/ethernet/meadow_ethnet_connect.c | Not started |  |  |  |
| C034 | F7MeadowOS/ethernet/meadow_ethnet_dhcp.c | Not started |  |  |  |
| C035 | F7MeadowOS/ethernet/meadow_ethnet_local.h | Not started |  |  |  |
| C036 | F7MeadowOS/ethernet/meadow_ethnet_monitor.c | Not started |  |  |  |
| C037 | F7MeadowOS/ethernet/meadow_ethnet_start.c | Not started |  |  |  |
| C038 | F7MeadowOS/ethernet/meadow_ethnet_utils.c | Not started |  |  |  |
| C039 | F7MeadowOS/hcom_nx/create_fs/hcom_nx_fs_create.c | Not started |  |  |  |
| C040 | F7MeadowOS/hcom_nx/create_fs/hcom_nx_fs_flash.c | Not started |  |  |  |
| C041 | F7MeadowOS/hcom_nx/create_fs/hcom_nx_fs_flash_tests.c | Not started |  |  |  |
| C042 | F7MeadowOS/hcom_nx/create_fs/hcom_nx_fs_littlefs.c | Not started |  |  |  |
| C043 | F7MeadowOS/hcom_nx/create_fs/hcom_nx_fs_part_utils.c | Not started |  |  |  |
| C044 | F7MeadowOS/hcom_nx/diag/hcom_nx_trace_msg_proc.c | Not started |  |  |  |
| C045 | F7MeadowOS/hcom_nx/diag/hcom_traverse_ku_kernel.c | Not started |  |  |  |
| C046 | F7MeadowOS/hcom_nx/hcom_nx_bbreg_access.c | Not started |  |  |  |
| C047 | F7MeadowOS/hcom_nx/hcom_nx_common.h | Not started |  |  |  |
| C048 | F7MeadowOS/hcom_nx/hcom_nx_common_utils.c | Not started |  |  |  |
| C049 | F7MeadowOS/hcom_nx/hcom_nx_config_manager.c | Not started |  |  |  |
| C050 | F7MeadowOS/hcom_nx/hcom_nx_config_manager.h | Not started |  |  |  |
| C051 | F7MeadowOS/hcom_nx/hcom_nx_config_manager_yaml.h | Not started |  |  |  |
| C052 | F7MeadowOS/hcom_nx/hcom_nx_startup_mgr.c | Not started |  |  |  |
| C054 | F7MeadowOS/hcom_nx/hcom_nx_upd_route_cmd.c | Not started |  |  |  |
| C055 | F7MeadowOS/hcom_nx/tests/hcom_nx_qspi_flash_tests.c | Not started |  |  |  |
| C056 | F7MeadowOS/include/meadow/bootloader/meadow_os_partitions.h | Not started |  |  |  |
| C057 | F7MeadowOS/include/meadow/hcom_bbreg_defn.h | Not started |  |  |  |
| C058 | F7MeadowOS/include/meadow/hcom_dfu_mode_entry.h | Not started |  |  |  |
| C059 | F7MeadowOS/include/meadow/hcom_dnld_shared.h | Not started |  |  |  |
| C060 | F7MeadowOS/include/meadow/hcom_misc_diag.h | Not started |  |  |  |
| C061 | F7MeadowOS/include/meadow/hcom_nuttx_shared.h | Not started |  |  |  |
| C062 | F7MeadowOS/include/meadow/hcom_protocol.h | Not started |  |  |  |
| C063 | F7MeadowOS/include/meadow/hcom_shared_common.h | Not started |  |  |  |
| C064 | F7MeadowOS/include/meadow/hcom_traverse_ku.h | Not started |  |  |  |
| C065 | F7MeadowOS/include/meadow/hcom_upd_shared.h | Not started |  |  |  |
| C066 | F7MeadowOS/include/meadow/meadow-upd.h | Not started |  |  |  |
| C067 | F7MeadowOS/include/meadow/meadow_apps_core_share.h | Not started |  |  |  |
| C068 | F7MeadowOS/include/meadow/meadow_cirbuf.h | Not started |  |  |  |
| C069 | F7MeadowOS/include/meadow/meadow_client_cert.h | Not started |  |  |  |
| C070 | F7MeadowOS/include/meadow/meadow_cloud.h | Not started |  |  |  |
| C071 | F7MeadowOS/include/meadow/meadow_debug_breakpoint_macro.h | Not started |  |  |  |
| C072 | F7MeadowOS/include/meadow/meadow_debug_helpers.h | Not started |  |  |  |
| C073 | F7MeadowOS/include/meadow/meadow_ethnet_common.h | Not started |  |  |  |
| C074 | F7MeadowOS/include/meadow/meadow_extended_errno.h | Not started |  |  |  |
| C075 | F7MeadowOS/include/meadow/meadow_gpio_counter.h | Not started |  |  |  |
| C076 | F7MeadowOS/include/meadow/meadow_hw_version.h | Not started |  |  |  |
| C077 | F7MeadowOS/include/meadow/meadow_interrupt.h | Not started |  |  |  |
| C078 | F7MeadowOS/include/meadow/meadow_kernel_tests.h | Not started |  |  |  |
| C079 | F7MeadowOS/include/meadow/meadow_kt_dispatcher.h | Not started |  |  |  |
| C080 | F7MeadowOS/include/meadow/meadow_measure_freq_shared.h | Not started |  |  |  |
| C081 | F7MeadowOS/include/meadow/meadow_os.h | Not started |  |  |  |
| C082 | F7MeadowOS/include/meadow/meadow_os_battery_backed_domain.h | Not started |  |  |  |
| C083 | F7MeadowOS/include/meadow/meadow_os_fault_handling.h | Not started |  |  |  |
| C084 | F7MeadowOS/include/meadow/meadow_os_persistent_data.h | Not started |  |  |  |
| C085 | F7MeadowOS/include/meadow/meadow_partition_share.h | Not started |  |  |  |
| C086 | F7MeadowOS/include/meadow/meadow_pwr_mgmt.h | Not started |  |  |  |
| C087 | F7MeadowOS/include/meadow/meadow_rotary_encoder.h | Not started |  |  |  |
| C088 | F7MeadowOS/include/meadow/meadow_sdmmc.h | Not started |  |  |  |
| C089 | F7MeadowOS/include/meadow/meadow_syscall_support.h | Not started |  |  |  |
| C090 | F7MeadowOS/include/meadow/meadow_thread_config.h | Not started |  |  |  |
| C091 | F7MeadowOS/include/meadow/meadow_unit_test_framework.h | Not started |  |  |  |
| C092 | F7MeadowOS/include/meadow/semaphore_lock_unlock.h | Not started |  |  |  |
| C093 | F7MeadowOS/include/meadow/stm32f777zit6-meadow.h | Not started |  |  |  |
| C094 | F7MeadowOS/kerneltests/ana_to_dig_conv_tests.c | Not started |  |  |  |
| C095 | F7MeadowOS/kerneltests/bg77_tests.c | Not started |  |  |  |
| C096 | F7MeadowOS/kerneltests/bg77_tests.h | Not started |  |  |  |
| C097 | F7MeadowOS/kerneltests/dig_to_ana_conv_tests.c | Not started |  |  |  |
| C098 | F7MeadowOS/kerneltests/espcp_tests.c | Not started |  |  |  |
| C099 | F7MeadowOS/kerneltests/ethernet_tests.c | Not started |  |  |  |
| C100 | F7MeadowOS/kerneltests/ethernet_tests.h | Not started |  |  |  |
| C101 | F7MeadowOS/kerneltests/iso8601_time_tests.c | Not started |  |  |  |
| C102 | F7MeadowOS/kerneltests/meadow_gpio_counter_tests.c | Not started |  |  |  |
| C103 | F7MeadowOS/kerneltests/meadow_i2c_tests.c | Not started |  |  |  |
| C104 | F7MeadowOS/kerneltests/meadow_interrupt_tests.c | Not started |  |  |  |
| C105 | F7MeadowOS/kerneltests/meadow_kt_bluetooth_tests.c | Not started |  |  |  |
| C106 | F7MeadowOS/kerneltests/meadow_kt_dispatcher.c | Not started |  |  |  |
| C107 | F7MeadowOS/kerneltests/meadow_kt_log.c | Not started |  |  |  |
| C108 | F7MeadowOS/kerneltests/meadow_kt_log.h | Not started |  |  |  |
| C109 | F7MeadowOS/kerneltests/meadow_kt_traverse_ku_tests.c | Reviewed | 2026-08-17 | #316 | Found incidentally during F011 hardware verification, not from a full batch review of this file. |
| C110 | F7MeadowOS/kerneltests/meadow_measure_freq_tests.c | Not started |  |  |  |
| C111 | F7MeadowOS/kerneltests/meadow_os_tests.c | Not started |  |  |  |
| C112 | F7MeadowOS/kerneltests/meadow_pwm_tests.c | Not started |  |  |  |
| C113 | F7MeadowOS/kerneltests/meadow_rotary_encoder_tests.c | Not started |  |  |  |
| C114 | F7MeadowOS/kerneltests/meadow_spi_dma_tests.c | Not started |  |  |  |
| C115 | F7MeadowOS/kerneltests/network_tests.c | Not started |  |  |  |
| C116 | F7MeadowOS/kerneltests/network_tests.h | Not started |  |  |  |
| C117 | F7MeadowOS/kerneltests/power_management_tests.c | Not started |  |  |  |
| C118 | F7MeadowOS/kerneltests/quick_misc_tests.c | Not started |  |  |  |
| C119 | F7MeadowOS/kerneltests/sdcard_tests.c | Not started |  |  |  |
| C120 | F7MeadowOS/kerneltests/timing.h | Not started |  |  |  |
| C121 | F7MeadowOS/meadow_cloud/meadow_cloud.c | Not started |  |  |  |
| C122 | F7MeadowOS/meadow_os/meadow-gpio.c | Not started |  |  |  |
| C123 | F7MeadowOS/meadow_os/meadow-upd.c | Not started |  |  |  |
| C124 | F7MeadowOS/meadow_os/meadow_hw_version.c | Not started |  |  |  |
| C125 | F7MeadowOS/meadow_os/meadow_interrupt.c | Not started |  |  |  |
| C126 | F7MeadowOS/meadow_os/meadow_os.c | Not started |  |  |  |
| C127 | F7MeadowOS/meadow_os/meadow_os_battery_backed_domain.c | Not started |  |  |  |
| C128 | F7MeadowOS/meadow_os/meadow_os_config.c | Not started |  |  |  |
| C129 | F7MeadowOS/meadow_os/meadow_os_espcp.c | Not started |  |  |  |
| C130 | F7MeadowOS/meadow_os/meadow_os_exceptions.c | Not started |  |  |  |
| C131 | F7MeadowOS/meadow_os/meadow_os_fault_handling.c | Not started |  |  |  |
| C132 | F7MeadowOS/meadow_os/meadow_os_info.c | Not started |  |  |  |
| C133 | F7MeadowOS/meadow_os/meadow_sdmmc.c | Not started |  |  |  |
| C134 | F7MeadowOS/misc/meadow_adc.c | Not started |  |  |  |
| C135 | F7MeadowOS/misc/meadow_cirbuf.c | Not started |  |  |  |
| C136 | F7MeadowOS/misc/meadow_client_cert.c | Not started |  |  |  |
| C137 | F7MeadowOS/misc/meadow_logging.c | Not started |  |  |  |
| C138 | F7MeadowOS/misc/meadow_logging.h | Not started |  |  |  |
| C139 | F7MeadowOS/misc/meadow_reinit_flash_size.c | Not started |  |  |  |
| C140 | F7MeadowOS/misc/meadow_reinit_flash_size.h | Not started |  |  |  |
| C141 | F7MeadowOS/misc/meadow_stm32f7_clockenable.c | Not started |  |  |  |
| C142 | F7MeadowOS/misc/meadow_stm32f7_clockenable.h | Not started |  |  |  |
| C143 | F7MeadowOS/misc/meadow_watchdog.c | Not started |  |  |  |
| C144 | F7MeadowOS/misc/meadow_watchdog.h | Not started |  |  |  |
| C145 | F7MeadowOS/misc/parse_iso8601_time.c | Not started |  |  |  |
| C146 | F7MeadowOS/ntpclient/meadow_ntpclient.c | Not started |  |  |  |
| C147 | F7MeadowOS/ntpclient/meadow_ntpclient.h | Not started |  |  |  |
| C148 | F7MeadowOS/ntpclient/ntpv3.h | Not started |  |  |  |
| C149 | F7MeadowOS/pwrmgmt/pwrmgmt_calib_lsi_clock.c | Not started |  |  |  |
| C150 | F7MeadowOS/pwrmgmt/pwrmgmt_config_wakeup_alarm.c | Not started |  |  |  |
| C151 | F7MeadowOS/pwrmgmt/pwrmgmt_config_wakeup_timer.c | Not started |  |  |  |
| C152 | F7MeadowOS/pwrmgmt/pwrmgmt_control.c | Not started |  |  |  |
| C153 | F7MeadowOS/pwrmgmt/pwrmgmt_enter_stop_mode.c | Not started |  |  |  |
| C154 | F7MeadowOS/pwrmgmt/pwrmgmt_local.h | Not started |  |  |  |
| C155 | F7MeadowOS/pwrmgmt/pwrmgmt_misc.c | Not started |  |  |  |
| C156 | F7MeadowOS/pwrmgmt/pwrmgmt_switch_rtc_clock.c | Not started |  |  |  |
| C157 | F7MeadowOS/pwrmgmt/pwrmgmt_time_support.c | Not started |  |  |  |
| C158 | F7MeadowOS/specialized/meadow_gpio_counter.c | Not started |  |  |  |
| C159 | F7MeadowOS/specialized/meadow_idle_monitor.c | Not started |  |  |  |
| C160 | F7MeadowOS/specialized/meadow_measure_freq.c | Not started |  |  |  |
| C161 | F7MeadowOS/specialized/meadow_measure_freq_local.h | Not started |  |  |  |
| C162 | F7MeadowOS/specialized/meadow_rotary_encoder.c | Not started |  |  |  |
| C163 | F7MeadowOS/timers/cpu_measure.c | Not started |  |  |  |
| C164 | F7MeadowOS/timers/freq_duty.c | Not started |  |  |  |
| C165 | F7MeadowOS/timers/lsi_clk_freq_measure.c | Not started |  |  |  |
| C166 | F7MeadowOS/timers/meadow_timers.h | Not started |  |  |  |
| C167 | F7MeadowOS/timers/pulse_width.c | Not started |  |  |  |
| C168 | F7MeadowOS/timers/rc_servo_decode.c | Not started |  |  |  |
| C169 | F7MeadowOS/timers/timer_manager.c | Not started |  |  |  |
