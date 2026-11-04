# Legacy -> Rebase Commit Review

Review date: 2026-10-30

## Scope and method

- Source: `../Legacy` (WildernessLabs/Meadow), first-parent history of `main` from 2023-07-31
  (creation of this repository) to `8b176b22bf9` (2026-10-19), about 300 commits.
- Excluded: the Legacy `meadow-3.0` branch, version bumps, the ESP32 tree (removed from Legacy in
  2024-07), other boards' configs and documentation-only commits.
- Pass 1: each commit's added lines were searched for in Rebase (plus the `../runtime` fork).
  About 80 commits matched >= 80% and were treated as ported.
- Pass 2: the ~120 commits below that threshold were checked by hand against Rebase behaviour,
  not text. Verdicts:
  - PORTED: the change is in Rebase.
  - SUPERSEDED: Rebase solves the same problem differently, or upstream NuttX 12 / .NET already
    covers it.
  - N-A: no longer applies.
  - PARTIAL / MISSING: listed below.
- Only the "OTA rollback never runs" and "Ethernet NTP start condition" findings were re-checked
  by hand after the review. Treat the other findings as leads to confirm before fixing.

## Not ported: bug fixes

| # | Legacy commit | PR | What is missing | Rebase location | Importance |
|---|---------------|----|-----------------|-----------------|------------|
| B1 | 30210d248fe (with 782671e3a72, 5dca941b718) | #689 | App OTA rollback never runs: the rollback block is entered only when `error` is true, but its loop runs only while `error` is false. `UPDATE_APP_DIR` is only deleted on failure. With no rollback dir the function returns 0, not -1. Port Legacy's `app_update()` (with `copy_directory()` and `process_removal_list()`) to fix B1, E1 and E2 together. Built only with `CONFIG_MEADOW_CLOUD`. | `apps/system/hcom/ota/hcom_ota.c:132-195` | Medium-high |
| B2 | 246957609c5 | #687 | UART5 TX pin is not switchable. `tx_gpio` is still `const` (PB13) and there is no `stm32_change_uart_tx_pin`. On F7v2/CCM, opening `/dev/ttyS2` (ESP32 UART monitor) turns PB13 (CCM D06, user GPIO) into UART5 TX. Shutdown and BREAK handling also use the wrong pin. | `nuttx/arch/arm/src/stm32f7/stm32_serial.c:471`, `F7MeadowOS/espcp/espcp_coprocessor.c` | Medium |
| B3 | 1c91a2dd519 | #590 | `ioctl(FIONREAD)` on WiFi sockets is forwarded to the ESP32, which returns EINVAL. As a result `Socket.Available` and `NetworkStream.DataAvailable` throw on WiFi, and some MQTT/HTTP libraries poll these. Fix by answering FIONREAD in espcp (Legacy used poll) or on the ESP32. | `F7MeadowOS/espcp/espcp_usrsock_sockif.c` (`espcp_usrsock_ioctl`) | Medium |
| B4 | 33dee5be7ed | #698 | Ethernet starts NTP when `refreshPeriod > 0` even if GetNetworkTimeAtStartup is false. Legacy and the Rebase WiFi path check only `get_network_time_at_startup`. Two-line fix. | `F7MeadowOS/ethernet/meadow_ethnet_connect.c:371` | Low-medium |
| B5 | 57c60cb3ffe | #463 | Runtime fork reports `ppp0` as Ethernet, not Ppp. Meadow.Core then treats a cell link as a wired adapter. Relevant to the cell work (#915). | `../runtime` (fork ac986f43b8d, NetworkInterface typing) | Medium (cell only) |
| B6 | a179250c803 | #527 (part) | `stm32_ethreset()` is still `void` and never returns -ENODEV, so the startup manager's "no PHY / CCM device" warning is dead code. The hang itself is fixed upstream by a reset timeout. Fixing it needs an upstream NuttX driver change. | `nuttx/arch/arm/src/stm32f7/stm32_ethernet.c:3562`, `F7MeadowOS/hcom_nx/hcom_nx_startup_mgr.c:401-425` | Low |

## Not ported: enhancements

| # | Legacy commit | PR | What is missing | Rebase location | Importance |
|---|---------------|----|-----------------|-----------------|------------|
| E1 | 782671e3a72 | #684 | App OTA ignores subdirectories (`DIRENT_ISFILE` only, "TODO: Recursive copying"). See B1. | `apps/system/hcom/ota/hcom_ota.c` | Medium |
| E2 | 5dca941b718 | #685 | App OTA ignores `remove.txt`, so files removed from the app stay on the device. See B1. | `apps/system/hcom/ota/hcom_ota.c` | Medium |
| E3 | 5f45be65ae7 | #606 | Mono error/assert details are not saved to `/meadow0/crash/mono_error.txt`. `MONO_CRASH_FILE` is defined but unused, so the cause of a managed runtime abort is lost after the reset. | `apps/system/mono/mono_main.c:393` | Medium |
| E4 | 0a06c29a45f | #666 | Large object store `USE_MALLOC` change not applied in the runtime fork: the LOS allocates whole sections instead of one malloc per object. This may add to SDRAM fragmentation and the OOM issues; measure before porting. | `../runtime` `sgen-los.c:139` | Medium |
| E5 | 30cd2ecd5ef | #733 | `CONFIG_NET_TCP_WRITE_BUFFERS` is not enabled, which limits kernel TCP throughput on Ethernet and PPP (the ESP32 path is unaffected). Needs IOB sizing review. | `nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/configs/meadow_os/defconfig` | Medium |
| E6 | 85d0df068ae | #555 (part) | `stm32_dmacapable()` lacks the `STM32_FMC_BASE5/6` cases, so SPI buffers in SDRAM (including the Mono heap) fall back to non-DMA transfers. The DMA threshold is 4, where Legacy used 32. Upstream file; check D-cache alignment. | `nuttx/arch/arm/src/stm32f7/stm32_dma.c:1015-1019` | Medium (performance) |
| E7 | 440fd56a78e | #643 (part) | Fault recording writer side is missing: nothing calls `meadow_os_fault_handler_save_os_state` on assert, so OS crashes are never recorded. Already tracked as #800 (on hold). | `F7MeadowOS/meadow_os/meadow_os_battery_backed_domain.c` | Medium (tracked) |
| E8 | 638f244e5c9 | #703 | Bluetooth MAC is decoded from the ESP32 but dropped: there is no `bluetooth_mac_address` in the config struct, it is not copied in the config manager and there is no `BtMAC` in CLI device info. | `F7MeadowOS/include/meadow/hcom_shared_common.h`, `F7MeadowOS/hcom_nx/hcom_nx_config_manager.c:2751`, `apps/system/hcom/nx_rqsts/hcom_misc_requests.c:135` | Low-medium |
| E9 | f678bdac3be | #528 | `Debug.WriteLine` output does not reach the CLI. In .NET it goes to syslog unless `DOTNET_DebugWriteToStdErr=1` is set. One `setenv` in `mono_main.c`. | `apps/system/mono/mono_main.c` | Low-medium |
| E10 | a1afd174f74 | #738 | w25qxxxjv 32K/64K block erase (Rebase erases 4K at a time). Do not port verbatim: Legacy picks the 64K erase without checking alignment and can erase data outside the requested range. | `nuttx/drivers/mtd/w25qxxxjv.c` | Low |
| E11 | 002003370cc | #690 | 4 s delay on the first remote-debugger message is still present. | `apps/system/hcom/mono/hcom_remote_vs_debug.c:452-462` | Low |
| E12 | 2968d72e25f | #566 (part) | Mono log profiler: the HCOM routes exist, but the runtime fork does not build the profiler (`NOT HOST_NUTTX`), so the CLI option does nothing. | `../runtime` `profiler/CMakeLists.txt:38` | Low |
| E13 | fc3c58830f0, 531047cf4a9, e0748ddd414, b3455416ea9 | #586/#591/#596 | mbedTLS error details and HResults never reach managed TLS exceptions: `CryptoNative_ErrGetErrorAlloc` and related functions return 0. | `apps/system/mono/pal_ssl_mbedtls.c:973-976` | Low-medium (diagnostics) |
| E14 | e1db693f055, bbf1d50bd51 | #781/#777 | About 74 numeric `syslog(1/2, ...)` priorities remain instead of `LOG_MDIAG`/`LOG_MTEST`, mostly in `hcom_nx_fs_flash_tests.c`, `hcom_file_dir_mgmt_utils.c` and `hcom_nx_startup_mgr.c`. Cosmetic. | various | Low |
| E15 | 5966c9a4fe8 | #536 | cppcheck CI workflow, `DockerFiles/CodeChecks` and `run-checks.sh` are absent. The #532 code fixes are present. | `.github/workflows` | Low (tooling) |

## Needs verification / related findings

| # | Item | Notes |
|---|------|-------|
| V1 | 9a310f01414 (#614) Ping | No Ping work in the runtime fork. .NET `Ping` needs a raw ICMP socket; NuttX 12 has one, but on WiFi the request goes through espcp to lwIP. Test `Ping.Send` on WiFi, Ethernet and cell. |
| V2 | ce44e61851c (#649) NTP on cell | Legacy moved cell NTP to user space because the kernel client caused "random reboots on cell". Rebase (`BuildCellCode`) uses the kernel `ntpc_start`; re-test on hardware. |
| V3 | Cell commits generally | Cell fixes (#579, #448, #743, #436, #417, #404, #668, #627) are in source on `main`, but `cell/hcom_pppd.c` is not built. The working reimplementation is on the unmerged `BuildCellCode` branch. |
| V4 | Rotary encoder managed export | `meadow_rotary_encoder_read_data` is a syscall, but `apps/system/mono/mappings-meadow.h` maps only `config` and `set_count`. `rotEncConfigParms_t` gained `delayMs`, so the Meadow.Core interop struct must match. |
| V5 | Round-robin timeslice | `CONFIG_RR_INTERVAL=200` vs Legacy's net 20 ms for the Mono task. Check this is intended. |
| V6 | Environment cache (not a Legacy commit) | `SystemNative_GetEnviron` returns an empty list on NuttX. After the first managed `SetEnvironmentVariable`/`GetEnvironmentVariables` call, `TMPDIR`, `DOTNET_*` and the other native `setenv` values are invisible to managed code. |
| V7 | File.Move same-file check (not a Legacy commit) | If `ConvertFileStatus` reports Dev/Ino as 0 (#721), `File.Move` onto an existing file renames over it instead of throwing. Adds weight to #721. |

## Suggested order

1. B1 + E1 + E2: replace `app_update()` with the Legacy version (one change).
2. B3: FIONREAD on WiFi sockets.
3. B2: UART5 TX pin selection for F7v2/CCM.
4. B4: Ethernet NTP start condition (trivial).
5. E3, E9, E11: small `mono_main.c`/HCOM changes.
6. E8: Bluetooth MAC in device info.
7. E5, E6, E4: performance and memory items; measure first.
8. B5, V2, V3: with the cell work (#915).
9. Remaining low items as time allows.

## Ported or no longer applicable (summary)

Checked by hand and found ported, superseded or N-A. Each entry below is the Legacy PR number.

- **HCOM / file transfer:**
  - #791, #746, #744, #741, #739, #750, #752, #769, #785, #808, #810, #651, #457, #486, #629,
    #641, #534, #526, #519, #511, #494, #675, #656, #567, #768, #727, #721, #724, #449.
- **Kernel / board:**
  - #793, #776, #770, #543, #546, #775, #783, #782, #714, #680, #707, #691, #679, #638, #662,
    #610, #639, #625, #676, #598, #587, #562, #495, #548, #484, #670, #604, #644, #664, #673,
    #539, #537, #446, #421, #522, #515, #584, #571, #467, #807, #813, #736, #700.
- **NuttX core / build:**
  - #730, #500, #509, #761, #767, #757, #672, #523, #491, #812, #811, #804, #801, #792, #789,
    #410, #532, #512, #764. Also two commits with no PR number: the Stage-1 OS flashing fix and
    `MEADOW_INCLUDE_CODE_FOR_TESTING_5MB_OF_FLASH`.
- **Mono glue / TLS / BCL:**
  - #734, #696, #688, #310, #611, #622, #671, #570, #480, #472, #487, #697, #524, #556, #560,
    #561, #755, #754, #706, #568, #454, #469, #499, #538, #520, #603, #535, #584, #678, #699,
    #723, #716, #735, #630, #592, #544.
  - #705 (Normalize under invariant globalization) is a partial port by design.
