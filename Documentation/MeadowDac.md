# Meadow DAC

> **Status:** static-level output implemented (`F7MeadowOS/misc/meadow_dac.c`), with developer
> test 15 (`apps/system/hcom/tests/meadow_dac_tests.c`). Waveform output is not implemented.
> Tracked by issue #158. The customer request is Meadow_Issues #281.

## Overview

The STM32F777 has one DAC block with two 12-bit output channels. Meadow.OS has never
supported it, on the Legacy platform or here. This document describes a small native
DAC driver that Meadow.Core can use to implement analog output on the F7:

| Meadow pin | MCU pin | DAC channel | Also usable as        |
|------------|---------|-------------|-----------------------|
| A00        | PA4     | 1           | ADC1_IN4 (analog in)  |
| A01        | PA5     | 2           | ADC1_IN5 (analog in)  |

These pins are the same on Feather V1, Feather V2 and the Core Compute Module. The DAC
outputs are fixed to these pins by the MCU; no other pin can be used.

The first version sets static output levels only. That is everything the current managed
contract asks for (see [Meadow.Core Side](#meadowcore-side)). Waveform output is left for
later, and the API is shaped so it can be added without changing these calls.

## Why It Is Worth Doing

Typical uses of a built-in DAC on an IoT or industrial controller:

| Use                     | Example                                                        | Needs            |
|-------------------------|----------------------------------------------------------------|------------------|
| Control setpoints       | Heating valve and damper actuators, motor-drive speed inputs   | Static level     |
| Bias and thresholds     | Comparator trip level, LED driver current set, sensor bias     | Static level     |
| Self-test, calibration  | Feed a known voltage into an ADC input and check the reading   | Static level     |
| Waveforms               | Tones, alarm sounds, test signals                              | Timer + DMA      |

The first three are the common cases, and all of them only need a level that changes now
and then. Meadow_Issues #281 is a customer doing exactly the first one (a 7-zone slab
heating controller) who found the DACs were not implemented. Meadow.Foundation already
has drivers for external DAC chips (Mcp4728, Mcp492x), so users are adding parts today
for something the MCU already has.

Industrial signals (0-10 V, 4-20 mA) always need an external amplifier or
voltage-to-current stage. The built-in DAC replaces the DAC chip, not that stage.

## Hardware Summary

Figures from the DAC characteristics table (Table 83) of ST datasheet DS11532 Rev 8
(STM32F765/F767/F768/F769). The F777 uses the same DAC block. **TODO:** check these
against the STM32F777 datasheet itself.

| Parameter                          | Buffer ON                         | Buffer OFF                        |
|------------------------------------|-----------------------------------|-----------------------------------|
| Output range                       | 0.2 V to VDDA - 0.2 V             | about 0.5 mV to VREF+ - 1 LSB     |
| Minimum resistive load             | 5 kOhm to VSSA, 25 kOhm to VDDA   | 1.5 MOhm for 1% accuracy          |
| Output impedance                   | low (internal amplifier)          | up to 15 kOhm                     |
| Maximum capacitive load            | 50 pF                             | -                                 |
| Settling time, full scale          | 3 us typ, 6 us max                | -                                 |
| Wakeup time after setting ENx      | 6.5 us typ, 10 us max             | -                                 |
| VDDA current while enabled         | 280 to 625 uA (code dependent)    | -                                 |

So, with the buffer on, the DAC can drive about 0.6 mA (3.3 V into 5 kOhm). With it off,
it can only drive a high-impedance input, such as an op-amp or an ADC input, but it
reaches the full 0 V to VREF+ range.

The 0.2 V buffered margin is the datasheet's worst case at its 5 kOhm minimum load. A lightly
loaded output gets much closer to the rails (see [Hardware Results](#hardware-results)).

The enabled DAC draws up to about 0.6 mA from VDDA even with no load, so a channel should
be disabled when it is not in use.

### Registers Used

The DAC block is at `0x40007400` (`STM32_DAC_BASE` in the F7 memory map header). Its clock
is `RCC_APB1ENR` bit 29 (`DACEN`). NuttX has no F7 DAC register header, so the driver
defines the few it needs:

| Register   | Offset | Bits used                                                     |
|------------|--------|---------------------------------------------------------------|
| `DAC_CR`   | 0x00   | `EN1` (0), `BOFF1` (1), `TEN1` (2); `EN2` (16), `BOFF2` (17), `TEN2` (18) |
| `DHR12R1`  | 0x08   | Channel 1, 12-bit right-aligned value                         |
| `DHR12R2`  | 0x14   | Channel 2, 12-bit right-aligned value                         |
| `DOR1`     | 0x2C   | Channel 1 value currently on the output (read only)           |
| `DOR2`     | 0x30   | Channel 2 value currently on the output (read only)           |

With the trigger disabled (`TENx` = 0), a value written to `DHR12Rx` reaches the output one
APB1 clock later. No timer, DMA or interrupt is needed for static levels. The reference
manual asks for the pin to be set to analog mode before the channel is enabled, to avoid
parasitic current.

## Design Principles

Meadow is one firmware image. Meadow.Core decides at runtime which features an app uses,
so every native feature is always built in, even though only a small share of apps use
it. That leads to these rules for the DAC:

- **Always built in, near-zero cost when unused.** No Kconfig option, no buffers, a few
  bytes of state. Nothing touches PA4/PA5 or the DAC until an app opens a channel.
- **Configured at runtime, not at build time.** Everything is chosen by the caller.
- **Resources claimed on open, released on close.** The DAC clock and pin are only held
  while a channel is open.
- **Safe with a misbehaving app.** No call blocks or waits for hardware that may never
  respond. Bad arguments return an error; they never fault the kernel.
- **MCU details stay native.** Meadow.Core sees a small platform-neutral API; register
  details stay in the OS.

## Why Not the NuttX DAC Driver

NuttX has Kconfig options for the F7 DAC (`CONFIG_STM32F7_DAC1`, `CONFIG_STM32F7_DAC2`)
but no F7 driver behind them: there is no `stm32_dac.c` in `arch/arm/src/stm32f7/`, so
turning them on builds nothing. The only candidate to port is the F4 driver,
`arch/arm/src/stm32/stm32_dac.c`, used with the generic upper half
`drivers/analog/dac.c`. It does not fit Meadow:

- **Build-time configuration.** Sample rate, trigger timer and DMA buffer size are Kconfig
  values, and each DMA buffer is a static array reserved whether or not the DAC is used.
- **Broken DMA path.** The DMA completion callback is empty and nothing marks a sample
  as sent, so the upper half's queue stops after the first write. Writes then block, and
  `close()` never returns.
- **Buffer overrun.** `IO_DMABUFFER_INIT` copies `buffer_len * sizeof(buffer)` bytes,
  using the size of a pointer rather than of a 16-bit sample, which copies twice the
  buffer's size.
- **No data-cache maintenance.** The F4 has no data cache; the F7 does, so DMA buffers
  would need cleaning (compare issue #653).
- **Upper-half behaviour.** Every sample is a separate 5-byte message. `write()` waits
  uninterruptibly when the queue is full, and `close()` waits with no time limit for it to
  drain. Writes shorter than 5 bytes shift the value into the high bytes.

Any fix would be a copy in our own tree, because Meadow does not edit upstream NuttX
files. For static levels the whole job is a handful of register writes, so a Meadow driver
is smaller than the port. It follows the precedent of `meadow_adc.c`, which also does
not use the NuttX ADC driver.

## API

New file `F7MeadowOS/misc/meadow_dac.c`, built next to `meadow_adc.c` from the board
`src/Make.defs`. The prototypes go in `F7MeadowOS/include/meadow/meadow_syscall_support.h`
with the ADC calls, and each call gets a line in `nuttx/syscall/syscall.csv` so user space
(Meadow.Core, or the test) can reach it.

```c
/* Flags for meadow_dac_open() */

#define MEADOW_DAC_FLAG_BUFFER_OFF  (1 << 0)   /* Default (0) is buffer ON */

int meadow_dac_open(uint8_t pinid, uint32_t flags);
int meadow_dac_write(uint8_t pinid, uint32_t value);
int meadow_dac_read(uint8_t pinid, uint32_t *value);
int meadow_dac_close(uint8_t pinid);
```

`pinid` uses the same encoding Meadow.Core already passes to `meadow_adc_configure()`:
`(port << 4) | pin`, where port A is 0. So PA4 is `0x04` and PA5 is `0x05`.

All calls return `0` on success and a negative error code on failure. They do not set
`errno`. That matches the ADC calls, and Meadow.Core does not read `errno` after a direct
kernel call.

Error codes:

| Code       | Meaning                                                              |
|------------|----------------------------------------------------------------------|
| `-EBADSLT` | `pinid` is not a DAC pin (the code the ADC uses for a pin it cannot handle) |
| `-EINVAL`  | Unknown `flags` bits, or `value` above 4095                          |
| `-EBADF`   | The channel is not open                                              |
| other      | Passed back unchanged from the step that failed, such as taking the driver lock |

`-EBADF` follows the Linux convention for an operation on something that was never
opened, like `write()` on a closed file descriptor. No other step in the driver returns it,
so it always means "not open".

### meadow_dac_open

Claims a channel and starts driving its output.

1. Map `pinid` to a channel. Anything other than PA4 or PA5 returns `-EBADSLT`.
2. Reject unknown `flags` bits with `-EINVAL`.
3. If the channel is already open, clear `ENx` and set it up again from step 5. This is
   not an error. An app that stops without calling close leaves its channel open, and the
   next app to open it must not be locked out. Two users of the same pin within one app
   are already refused by Meadow.Core (`DeviceChannelManager.ReservePin()`), so the
   kernel does not need to check for that.
4. Turn on the DAC clock if neither channel is open yet.
5. Set the pin to analog mode.
6. Set or clear `BOFFx` from the flags, keep `TENx` clear, write 0 to `DHR12Rx`, then set
   `ENx`.

The output starts at 0 (up to 0.2 V with the buffer on, 0 V with it off). The caller can
confirm this with `meadow_dac_read()`, and the test does.

### meadow_dac_write

Sets the output level.

- `value` is 0 to 4095, right-aligned 12-bit. Anything larger returns `-EINVAL`; the value
  is not silently clipped.
- Returns `-EBADSLT` for a pin that is not a DAC pin, and `-EBADF` if the channel is not
  open.
- Writes `DHR12Rx` and returns. There is no waiting; the output settles within about 6 us.

The output voltage is roughly `VREF+ * value / 4096`, limited to the buffered range when
the buffer is on. VREF+ is tied to the 3.3 V rail on all Meadow boards, and the MCU has a
single VREF+ shared by the ADC and the DAC.

### meadow_dac_read

Returns the value currently on the output, read from `DORx`. The same errors as
`meadow_dac_write` apply. It is cheap to provide and lets the test check the digital path
without a meter. Meadow.Core does not need it for `GenerateOutput()`.

This is the value the DAC was given, not a measurement. The DAC cannot measure its own
output; only a meter, or the ADC in the loopback test, can measure the voltage.

### meadow_dac_close

Releases a channel.

1. Clear `ENx`, which stops the channel and its current draw.
2. Leave the pin in analog mode with no pull-up or pull-down. That is the lowest-power
   state, and the one the ADC wants if the pin is used for input next.
3. Turn off the DAC clock if neither channel is still open.

Closing a channel that is not open returns `-EBADF`, so double closes are visible in
testing.

### Locking

One kernel mutex protects the driver's state (which channels are open) and the shared
`DAC_CR` register, because both channels live in it. All calls are short, so no call holds
the mutex for long.

## Meadow.Core Side

For information only; this is managed code and is not part of this change.

- The contract is `IAnalogOutputController.CreateAnalogOutputPort(IPin)` and
  `IAnalogOutputPort.GenerateOutput(uint digitalValue)`, in Meadow.Contracts.
- The F7 platform does not implement `IAnalogOutputController` today. The A00/A01 pin
  definitions declare their analog channels as input only:
  `new AnalogChannelInfo("ADC1_IN4", ADCPrecisionBits, true, false)`. The last argument
  would become `true`, with precision 12.
- Pin conflicts, such as asking for analog output on a pin already used as analog input,
  are handled in managed code by `DeviceChannelManager.ReservePin()`, which already has a
  `ChannelConfigurationType.AnalogOutput`. The native driver does not arbitrate.
- A likely mapping: creating the port calls `meadow_dac_open()`, `GenerateOutput()` calls
  `meadow_dac_write()`, and `Dispose()` calls `meadow_dac_close()`.
- If the OS is too old to have these calls, the P/Invoke throws
  `EntryPointNotFoundException`. `F7AnalogInputArray` already uses this to report that the
  OS needs updating.

## Test Plan

The first and only customer is a user-space test that uses the DAC the way Meadow.Core
would, through the same kernel calls.

- **Where:** a new `apps/system/hcom/tests/meadow_dac_tests.c`, added to the user-space
  dispatcher table in `developer_tests.c`. Test number 15 (`MEADOW_TEST_DAC`) moves from
  the kernel dispatcher to the user-space one. The placeholder
  `F7MeadowOS/kerneltests/dig_to_ana_conv_tests.c` is removed.
- **Build:** `./build.sh -u=dac`, as today. The test stays out of the `all` sweep because
  it needs a meter or a jumper.
- **Run:** `meadow developer -p 15 -v <n>`.

Proposed `-v` values:

| `-v` | Test                                                                             |
|------|----------------------------------------------------------------------------------|
| 1    | API checks, no meter needed: bad pin, bad flags, bad value, and write, read and close before open, each returning the expected error. A second open of an open channel must succeed and restart it at 0. |
| 2    | Open A00 with the buffer on and report the value read back (0) and its expected voltage. Then step through 0, 1024, 2048, 3072 and 4095, holding each for a few seconds; each write is checked with `meadow_dac_read()` and its expected voltage logged. Measure with a meter. |
| 3    | The same steps with the buffer off.                                              |
| 4    | The same as 2 on A01.                                                            |
| 5    | Leave A00 at a mid-scale value, so the level can be measured at leisure. Test 6 closes it. |
| 6    | Close both channels.                                                             |

Later, a loopback test: jumper A00 to another analog input (for example A02), step the
DAC, read each level back with `meadow_adc`, and check it against the expected value.
That makes the test self-checking. It can use buffer on and off to show the difference
in range.

Hardware checks to cover:

- The output voltage matches the expected value at each step.
- With the buffer on, the ends of the range stay within 0.2 V of each rail.
- After close, the pin is not driven, and it can then be used as an ADC input.
- Board current drops back after close (the DAC draws up to about 0.6 mA while enabled).

### Hardware Results

Measured 2026-09-29 on a Core Compute Module whose 3.3 V rail measured 3.26 V, with a meter and
a 1.5 MOhm resistor from the pin to GND. Test 1 passed all 16 checks.

| Value | Ideal at 3.26 V | A00, buffer on | A00, buffer off | A01, buffer on |
|-------|-----------------|----------------|-----------------|----------------|
| 0     | 0 V             | 0.05 V         | 0.00 V          | 0.05 V         |
| 1024  | 0.815 V         | 0.81 V         | 0.80 V          | 0.81 V         |
| 2048  | 1.630 V         | 1.63 V         | 1.61 V          | 1.61 V         |
| 3072  | 2.445 V         | 2.45 V         | 2.42 V          | 2.45 V         |
| 4095  | 3.259 V         | 3.22 V         | 3.23 V          | 3.22 V         |

- With the buffer on, the middle steps are within 5 mV of ideal for the real rail voltage.
- With the buffer off, readings are about 1% low: the DAC's output resistance (up to 15 kOhm)
  and the 1.5 MOhm load form a divider.
- Test 5 held A00 at 1.63 V; after test 6 the pin was released and read 0.03 V.
- A pin that is not driven floats. With a meter alone it read 0.3 to 0.4 V, which at first
  looked like the output after open. The resistor removed that.

## Documentation Changes

- `Documentation/Readme.md`: index row for this page.
- `Documentation/RunningDeveloperTests.md`: describe test 15 and its `-v` values, and note
  that it is now a user-space test.
- `build.sh`: the `-u=dac` comment, which today says the test is in the kernel.

## Waveforms (Design, Issue #658)

> **Status:** buffer loops, triangle and noise implemented (`F7MeadowOS/misc/meadow_dac.c`),
> with developer tests 7 to 12. Streaming is not implemented.

A waveform is output paced by a timer instead of set by each `meadow_dac_write()`. The
first version has two kinds:

- **Buffer loop:** the app supplies up to 8,192 samples and a sample rate. DMA feeds one
  sample to the DAC per timer pulse and starts again at the first sample after the last,
  until stopped. One cycle of a sine wave in the buffer gives a continuous sine wave.
- **Triangle and noise:** the DAC generates these itself. Each timer pulse steps the
  triangle up or down by one, or loads a new pseudo-random value. No buffer or DMA is
  needed.

Streaming (continuous new data, such as audio) is described separately, in
[Streaming](#streaming-design-issue-662).

### How It Works

1. **TIM6 sets the sample rate.** It runs from 108 MHz with a time base only: prescaler,
   reload value, and master mode "update" (`CR2.MMS = 010`), so it pulses its trigger output
   (TRGO) once per sample. It has no pins and no interrupt. TIM6 was freed for this by #657.
2. **The DAC listens to TIM6.** With `TSELx = 000` (TIM6) and `TENx = 1`, each pulse moves the
   waiting value to the output.
3. **For a buffer loop, DMA supplies the next value.** With `DMAENx = 1` each pulse also
   requests the next sample from DMA1, which reads the kernel's copy of the buffer in
   circular mode:

   | DAC channel | Pin | DMA          | Request channel | NuttX name    |
   |-------------|-----|--------------|-----------------|---------------|
   | 1           | A00 | DMA1 stream 5 | 7              | `DMAMAP_DAC1` |
   | 2           | A01 | DMA1 stream 6 | 7              | `DMAMAP_DAC2` |

   Nothing else in the Meadow configuration uses these two streams; see
   [DMA Streams in Use](#dma-streams-in-use).
4. **For triangle or noise, the DAC does the rest.** `WAVEx` selects triangle or noise and
   `MAMPx` its amplitude. The output is the value last written plus the generated value.

The CPU does no work per sample in either kind.

### DMA Streams in Use

Checked in the current tree (standard build) and in Legacy (`mono/defconfig`). The NuttX F7
drivers that can claim a DMA stream are serial, SPI, SDMMC, QSPI, ADC, I2S and SAI; the F7
I2C driver has no DMA support. In Meadow's own code only `meadow_adc.c` claims one.

| User | DMA stream (request channel) | When it is claimed |
|------|------------------------------|--------------------|
| SPI2 RX / TX | DMA1 stream 3 / 4 (ch0) | first use of the bus, then held |
| SPI3 RX / TX | DMA1 stream 2 / 7 (ch0) | first use of the bus, then held |
| SPI5 RX / TX | DMA2 stream 3 (ch2) / stream 6 (ch7) | first use of the bus, then held |
| SDMMC2 (SD card) | DMA2 stream 0 (ch11) | at start-up |
| QSPI flash | DMA2 stream 7 (ch3) | at start-up (current tree only) |
| Meadow ADC array | DMA2 stream 4 (ch0) | while an ADC array is configured |
| **DAC channel 1 / 2** | **DMA1 stream 5 / 6 (ch7)** | **only while a waveform plays** |

No serial port has DMA enabled (USART1, UART4 and UART5 have it off), and I2S and SAI are not
enabled. SPI5 TX used to share DMA2 stream 4 with the ADC array, which hung whichever of the
two was used second; it was moved to stream 6 (`DMAMAP_SPI5_TX_2`) in #660. USART6 TX
(`DMAMAP_USART6_TX_1`) also maps to DMA2 stream 6, so it must not be given DMA while SPI5
uses that stream.

**Off limits while the DAC uses DMA1 streams 5 and 6.** These users are mapped to the same two
streams and must not be enabled without moving the DAC or them:

| User | DMA1 stream (request channel) |
|------|-------------------------------|
| I2C1 RX / TX | 5 / 6 (ch1) (no F7 driver support today) |
| I2C4 TX | 5 (ch2) or 6 (ch8) |
| USART2 RX / TX | 5 / 6 (ch4) |
| UART8 RX | 6 (ch5) |
| SPI3 TX, alternative mapping (`DMAMAP_SPI3_TX_1`) | 5 (ch0) |
| SPI2 TX, alternative mapping (`DMAMAP_SPI2_TX_1`) | 6 (ch9) |
| SPDIFRX | 6 (ch0) |
| TIM2 CH1/CH2/CH4, TIM3 CH2, TIM4 UP, TIM5 UP requests | 5 or 6 (ch2, 3, 5, 6) |

### Sharing TIM6

TIM6 is the only free timer the DAC can use, so both channels share it. If both channels
play waveforms at once, they run at the same sample rate. Starting a waveform on the second
channel at a different rate from the one already running fails with `-EBUSY`; the same rate
is fine. A static level on the other channel is not affected. TIM6 runs only while at least
one channel is playing a waveform.

### Sample Rate

The requested rate is `rate_hz`, a whole number from 1 Hz to 1,000,000 Hz (the DAC's
maximum update rate). The driver picks the prescaler and reload value that come closest,
and reports the rate it actually set in `actual_hz` (rounded to a whole number):
`actual = 108,000,000 / ((PSC + 1) * (ARR + 1))`. For example, 44,100 Hz is not exact and
would be reported as the nearest achievable rate.

### API

Waveforms use two calls. The set-up is passed as a struct rather than as separate
parameters, following the pattern of other Meadow syscalls such as
`meadow_measure_freq_configure()`. The struct can gain fields later, for example for
streaming, without changing the syscall, and its `size` field lets the kernel reject a caller
built against a different layout instead of misreading it.

```c
/* struct meadow_dac_wave_s types */

#define MEADOW_DAC_WAVE_BUFFER    1   /* Loop a buffer of samples */
#define MEADOW_DAC_WAVE_TRIANGLE  2   /* DAC triangle generator */
#define MEADOW_DAC_WAVE_NOISE     3   /* DAC noise generator */

struct meadow_dac_wave_s
{
  uint32_t        size;       /* sizeof(struct meadow_dac_wave_s), set by the caller */
  uint32_t        type;       /* MEADOW_DAC_WAVE_* */
  uint32_t        rate_hz;    /* Requested sample rate */
  uint32_t        actual_hz;  /* Out: the rate actually set */
  uint32_t        amplitude;  /* Triangle and noise only */
  uint32_t        count;      /* Buffer only: number of samples */
  const uint16_t *samples;    /* Buffer only: the samples */
};

int meadow_dac_wave_start(uint8_t pinid, struct meadow_dac_wave_s *wave);
int meadow_dac_wave_stop(uint8_t pinid);
```

Both are syscalls, return `0` or a negative error code like the static-level calls, and need
the channel to be open first (`meadow_dac_open()`), otherwise `-EBADF`.

On the managed side the struct is a `[StructLayout(LayoutKind.Sequential)]` struct of seven
32-bit fields, with `samples` as an `IntPtr` to a pinned `ushort[]`. Every field is 32 bits
(the F7 is a 32-bit MCU), so there is no padding to get wrong. Fields that a type does not
use are ignored, but should be set to 0.

#### meadow_dac_wave_start

Starts a waveform on an open channel.

1. Copy the struct from the caller and check everything before touching the hardware:
   - the pin and that the channel is open (`-EBADSLT`, `-EBADF`)
   - `wave` is not NULL and `size` is `sizeof(struct meadow_dac_wave_s)` (`-EINVAL`)
   - `type` is one of the `MEADOW_DAC_WAVE_*` values (`-EINVAL`)
   - `rate_hz` is 1 to 1,000,000 (`-EINVAL`)
   - no waveform is already running on this channel, and any waveform on the other channel
     runs at the same rate (`-EBUSY`)
   - the checks for the type, below
2. Start the waveform, as described for each type below.
3. Write the rate actually set into the caller's `actual_hz`.

**`MEADOW_DAC_WAVE_BUFFER`** plays a buffer of samples in a loop.

- `count` is 1 to 8,192, `samples` is not NULL, and every sample is 0 to 4095 (`-EINVAL`).
- The driver allocates its own copy of the buffer, aligned to the 32-byte data cache line,
  and copies the samples into it (`-ENOMEM` if there is no memory). The app can free or reuse
  its own buffer as soon as the call returns.
- It cleans the copy from the data cache, so DMA reads what was copied (compare #653).
- It claims the DMA stream, sets it up (memory to peripheral, 16-bit, memory increment,
  circular, peripheral address `DHR12Rx`) and starts it.
- It sets `TSELx` to TIM6, and `TENx` and `DMAENx` in `DAC_CR`, then starts TIM6 if it is not
  already running.

**`MEADOW_DAC_WAVE_TRIANGLE`** and **`MEADOW_DAC_WAVE_NOISE`** use the DAC's own generators.

- `amplitude` is one of 1, 3, 7, 15, 31, 63, 127, 255, 511, 1023, 2047 or 4095, the values the
  hardware supports (`-EINVAL` otherwise).
  - **Triangle:** the output ramps from the last value written up to that value plus
    `amplitude`, one step per pulse, and back down. One cycle takes `2 * amplitude` pulses, so
    the triangle's frequency is `rate_hz / (2 * amplitude)`. The last value written plus
    `amplitude` must not exceed 4095 (`-EINVAL`).
  - **Noise:** each pulse adds a new pseudo-random value, masked to `amplitude`, to the last
    value written.
- The driver sets `WAVEx`, `MAMPx`, `TSELx` to TIM6 and `TENx`, then starts TIM6 if it is not
  already running.

#### meadow_dac_wave_stop

Stops the waveform on a channel.

- Stops TIM6 if the other channel is not playing a waveform, then clears `TENx`, `DMAENx`,
  `WAVEx` and `MAMPx`, stops and releases the DMA stream, and frees the kernel's copy of the
  buffer.
- The channel stays open, back in static mode. With the trigger off, the output takes the
  value in the holding register: for a buffer loop, the sample DMA had loaded next; for
  triangle or noise, the base value (the last value written). `meadow_dac_write()` then works
  as before.
- Returns `-EBADF` if the channel is not open, and `-EINVAL` if no waveform is running on it.

#### Changes to the Static-Level Calls

- `meadow_dac_write()` during a waveform returns `-EBUSY`: the output register is being fed by
  DMA or the waveform generator.
- `meadow_dac_read()` during a waveform returns whatever value is on the output at that
  moment. The test uses this to check that a waveform is running without a scope.
- `meadow_dac_open()` of a channel that is playing a waveform stops the waveform first, then
  restarts the channel as before.
- `meadow_dac_close()` stops any waveform on the channel first.

### Details and Pitfalls

- **Blocking DMA claim.** NuttX's `stm32_dmachannel()` waits, uninterruptibly, if the stream is
  already held. The driver claims the stream only while a waveform plays, and nothing else in
  Meadow may use DMA1 streams 5 and 6. This must be kept in mind if UART or I2C DMA is ever
  switched on.
- **Loop interrupts.** In circular mode the NuttX DMA driver always enables the
  transfer-complete interrupt, which would fire once per loop of the buffer: at 1 MS/s with a
  short buffer, far too often. So the driver sets the stream up with `stm32_dmasetup()` and
  then enables it itself, with only the transfer-error interrupt; nothing needs to know when
  a loop ends.
- **DMA underrun.** If DMA ever failed to supply a sample in time, the DAC would set
  `DMAUDRx` in `DAC_SR` and stop requesting. That is not expected at these rates. The first
  version does not handle it; `meadow_dac_wave_stop()` clears the flag.
- **Memory.** At most 8,192 samples (16 KB) per channel, 32 KB for both, from the kernel heap,
  only while a waveform plays.
- **Locking.** The same mutex as the static-level calls. None of the waveform calls waits for
  hardware.

### Waveform Tests

Added to developer test 15. Like `-v 5`, each waveform test starts the waveform and returns,
leaving it playing until `-v 6` closes both channels. Watch A00 (and A01) on a scope.

| `-v` | Test |
|------|------|
| 7    | API checks for the waveform calls, no scope needed: wrong struct size, bad count, bad sample, bad rate, bad type and amplitude, write during a waveform, a different rate on the second channel, stop with no waveform. It also checks that `meadow_dac_read()` changes while a buffer loop plays. |
| 8    | Sine on A00: 60 samples at 60,000 Hz, a 1 kHz sine wave. |
| 9    | Sawtooth on A00: 250 samples at 25,000 Hz, a 100 Hz sawtooth. |
| 10   | Triangle on A00: amplitude 4095 from 0 at 1,000,000 Hz, about 122 Hz. |
| 11   | Noise on A00: amplitude 4095 at 100,000 Hz. |
| 12   | Sine on A00 and cosine on A01, both 60 samples at 60,000 Hz. On a scope in XY mode this draws a circle, which shows both channels running at the same rate from the shared TIM6. |

The test rates divide 108 MHz exactly, so the frequencies on the scope should match. For
each test, check the frequency and the voltage range, and that `-v 6` stops the output.

### Waveform Hardware Results

Measured 2026-09-29 on a Core Compute Module, output buffer on, with an oscilloscope.

| Test | Result |
|------|--------|
| 7    | All 30 checks passed. The actual rate was reported as exactly 60,000 Hz. A01 kept playing after A00 stopped, so stopping one channel leaves the shared TIM6 running for the other. |
| 8    | 1 kHz sine, a little over 3 V peak to peak (about 0.05 to 3.2 V, the buffered range). |
| 9    | 100 Hz sawtooth, the same amplitude. The scope's frequency reading varied from 99.951 to 100.083 Hz when triggered on the ramp; triggering on the falling edge steadied it. The sample timing comes from TIM6 in hardware, so this is the trigger point moving on the 40 us steps and the slewing drop, not the output. |
| 10   | Triangle at about 122 Hz, from the DAC's generator stepping at 1 MHz, the DAC's maximum. |
| 11   | Noise across the same full amplitude. |
| 12   | In XY mode, a steady, non-rotating circle centred at about (1.6 V, 1.6 V), so both channels are locked to the same TIM6 pulses. It is slightly out of round, as expected from the buffered range flattening the extremes, the small gain difference between the channels, and the 60-step staircase. |

## Streaming (Design, Issue #662)

> **Status:** implemented (`F7MeadowOS/misc/meadow_dac.c`), with developer tests 13 to 18.
> Stereo (the DAC's dual mode) is not implemented.

Streaming plays samples the app keeps supplying, for example audio or a long signal, instead
of a fixed buffer that loops. It is a fourth waveform type, `MEADOW_DAC_WAVE_STREAM`, started
with `meadow_dac_wave_start()` and stopped with `meadow_dac_wave_stop()` like the others, plus
two new calls: one to write samples and one to read the stream's status.

Meadow cannot know what apps will stream, so the defaults are generous and the caller can
change them.

### How It Works

The driver keeps its own **ring buffer** for the channel, which DMA plays in circular mode at
the TIM6 rate, exactly as for a buffer loop. The difference is that the ring is refilled while
it plays:

1. The ring is handled as **two halves**. DMA interrupts at the half-way point and at the end
   of the ring, which is when it has just finished playing one half and starts on the other.
2. When DMA finishes a half, that half becomes free. The interrupt handler first fills it
   with the **fill value** (see [Underruns](#underruns)), then wakes any writer waiting for
   space.
3. `meadow_dac_wave_write()` copies the app's samples into free space in order, and cleans
   what it copied from the data cache. Samples always go into a half that DMA is not playing.

There are only two interrupts per pass of the ring. At 44,100 Hz with the default ring of
8,192 samples, that is about 11 a second. The latency from write to output is at most the ring
size divided by the rate: about 186 ms for the default ring at 44,100 Hz, about 6 ms for a
256-sample ring.

Two channels can stream at once, each with its own ring and DMA stream, sharing TIM6's rate
like the other waveforms. Each starts when its own ring is ready, so they can be a few samples
apart, but they never drift. Stereo (the DAC's dual mode, both channels from one stream) is
left for later.

### Starting

`meadow_dac_wave_start()` with type `MEADOW_DAC_WAVE_STREAM` allocates the ring and fills it
with the fill value, but does not start output. The stream is **priming**: the app writes
samples, and output starts on its own once the ring is full, so playback never begins with
an underrun.

A clip shorter than the ring would never fill it, so the app can also start output at once by
calling `meadow_dac_wave_write()` with `count` 0. What has been written plays, followed by the
fill value if nothing more comes.

### Writing and Waiting

`meadow_dac_wave_write()` takes as many samples as fit.

- If they do not all fit, it waits for DMA to free space, up to `timeout_ms` milliseconds
  in all. A timeout of 0 means do not wait: take what fits now.
- It returns the number of samples taken, from 0 to `count`. Taking fewer than `count` is not
  an error; the app writes the rest later. Only a bad argument, or a stream that is not
  running, returns an error.
- It never waits forever, and the wait can be interrupted, for example when the app is
  stopped. It waits without holding the driver lock, so `meadow_dac_wave_stop()` or
  `meadow_dac_close()` from another thread ends the wait, and the write then returns what it
  had taken.

Every sample is checked (0 to 4095) before any is copied; one bad sample fails the whole call
with `-EINVAL` and nothing is taken.

### Underruns

If the app falls behind, DMA does not stop: it moves into the next half whatever it holds.
Because each half is filled with the fill value as soon as it is freed, a half the app did not
refill in time plays the fill value instead of old samples.

- The fill value is chosen by the caller in the struct. For audio it would be 2,048
  (mid-scale, silence); for a control signal, a safe level.
- If a half was only partly written when DMA reached it, the written part plays and the rest
  plays the fill value. The next write then continues in the following free half.
- Each half that DMA starts without being completely written counts as one **underrun**. The
  count is reported by `meadow_dac_wave_status()`.
- The stream keeps running; as soon as the app writes again, its samples play.
- The driver cannot tell an app that has fallen behind from one that has finished. So once
  the app's data runs out, every further half counts as an underrun until the stream is
  stopped. An app that reads the count to judge whether it kept up should read it right after
  its last write, not after waiting for the stream to drain.

### Stopping

`meadow_dac_wave_stop()` stops at once, like the other types: samples still queued are
dropped. An app that wants everything played first waits until `meadow_dac_wave_status()`
reports nothing queued, then stops.

### API Changes

`struct meadow_dac_wave_s` gains two fields at the end, used only by the stream type:

```c
#define MEADOW_DAC_WAVE_STREAM    4   /* Stream samples written by the app */

struct meadow_dac_wave_s
{
  uint32_t        size;       /* sizeof(struct meadow_dac_wave_s), set by the caller */
  uint32_t        type;       /* MEADOW_DAC_WAVE_* */
  uint32_t        rate_hz;    /* Requested sample rate */
  uint32_t        actual_hz;  /* Out: the rate actually set */
  uint32_t        amplitude;  /* Triangle and noise only */
  uint32_t        count;      /* Buffer only: number of samples */
  const uint16_t *samples;    /* Buffer only: the samples */
  uint32_t        ring;       /* Stream only: ring size in samples (even), 0 = 8,192 */
  uint32_t        fill;       /* Stream only: value played on underrun, 0 to 4095 */
};
```

`size` must match the struct as it is now. A caller built against an out-of-date layout is
rejected with `-EINVAL` instead of being misread.

Two new syscalls:

```c
/* struct meadow_dac_wave_status_s states */

#define MEADOW_DAC_STREAM_NONE     0   /* Not streaming */
#define MEADOW_DAC_STREAM_PRIMING  1   /* Started, waiting for the ring to fill */
#define MEADOW_DAC_STREAM_RUNNING  2   /* Playing */

struct meadow_dac_wave_status_s
{
  uint32_t size;       /* sizeof(struct meadow_dac_wave_status_s), set by the caller */
  uint32_t state;      /* Out: MEADOW_DAC_STREAM_NONE, _PRIMING or _RUNNING */
  uint32_t queued;     /* Out: samples written and not yet played */
  uint32_t space;      /* Out: samples that can be written now without waiting */
  uint32_t underruns;  /* Out: underruns since the stream started */
};

int meadow_dac_wave_write(uint8_t pinid, const uint16_t *samples, uint32_t count,
                          uint32_t timeout_ms);
int meadow_dac_wave_status(uint8_t pinid, struct meadow_dac_wave_status_s *status);
```

- **`meadow_dac_wave_start()`** with `MEADOW_DAC_WAVE_STREAM` checks, in addition to the
  common checks:
  - `ring` is 0 (the default, 8,192) or an even number from 256 to 16,384, since the ring
    is played in two halves (`-EINVAL`)
  - `fill` is 0 to 4095 (`-EINVAL`)

  It allocates the ring (`-ENOMEM` if there is no memory) and leaves the stream priming.
- **`meadow_dac_wave_write()`** returns the number of samples taken, or:
  - `-EBADSLT` or `-EBADF`, as for the other calls
  - `-EINVAL` for NULL `samples` with a non-zero `count`, or a sample above 4095
  - `-ENOTCONN` if the channel is not streaming: no stream was started, or it was stopped
- **`meadow_dac_wave_status()`** fills in the struct for any open channel. For a channel that is
  not streaming it reports `MEADOW_DAC_STREAM_NONE` and zeros. A wrong `size` is `-EINVAL`.
- `meadow_dac_write()` during a stream is `-EBUSY`, as for the other waveforms.

### Details

- **Interrupts:** unlike the buffer loop, the stream wants both DMA interrupts, so it uses
  NuttX's `stm32_dmastart()` with the half-transfer interrupt. The handler runs in interrupt
  context and does little: fill one half with the fill value, clean it from the cache, update
  the counts and wake the writer.
- **Interrupt rate** at the extremes: a 256-sample ring at 1,000,000 Hz interrupts every
  128 us, and each interrupt fills 128 samples. That is the worst case, and still small.
- **Memory:** up to 32 KB per channel (16,384 samples), from the kernel heap, only while the
  stream exists.
- **Cache:** the writer cleans each range it copies; the handler cleans each half it refills.
  DMA only reads the ring, so nothing needs invalidating.

### Streaming Tests

Added to developer test 15.

| `-v` | Test |
|------|------|
| 13   | Streaming API checks, no scope needed: bad ring size, bad fill value, write with no stream, a bad sample, write with timeout 0 into a full ring (takes 0), start with `count` 0, status while priming and running, stop and write again. |
| 14   | Streams tones on A00 for 10 seconds at 48,000 Hz with the default ring: 500 Hz, 1 kHz and 2 kHz, changing every second. On a scope the tone should change cleanly each second with no gaps. Passes if there are no underruns while writing; those counted afterwards, while the data drains, are expected and reported separately. |
| 15   | Deliberate underrun: streams a 1 kHz tone on A00 for 2 seconds, then stops writing for 2 seconds, then writes again. The scope should show the tone, a flat line at mid-scale (the fill value), then the tone again; the log reports the underruns. |
| 16   | The same as 14 with a 256-sample ring (about 5 ms). Reports the underruns while writing, which are times the writer (the HCOM thread) was late: a measure of how small a ring a writer keeps up with. |
| 17   | The same with a 512-sample ring (about 11 ms). |
| 18   | The same with a 1,024-sample ring (about 21 ms). |

Tests 14 to 18 run for up to 10 seconds, within the CLI's default 60-second test timeout.
48,000 Hz divides 108 MHz exactly.

### Streaming Hardware Results

Measured 2026-09-30 on a Core Compute Module at 48,000 Hz on A00, with an oscilloscope. The
test's writer is the HCOM thread, at priority 180.

| Test | Ring | Runs | Underruns while writing | Underruns while draining |
|------|------|------|-------------------------|--------------------------|
| 13   | -    | 1    | All 29 API checks passed | - |
| 14   | 8,192 | 1   | 0 (passed)               | 1 |
| 15   | 8,192 | 2   | 23 and 23, the 2 s gap   | 1 |
| 16   | 256  | 3    | 0, 0, 0                  | 3, 2, 2 |
| 17   | 512  | 3    | 0, 0, 0                  | 1, 2, 2 |
| 18   | 1,024 | 3   | 0, 0, 0                  | 2, 2, 1 |

- On the scope, 14 and 16 showed the tones stepping cleanly with no gaps. 15 showed the output
  at 0 V (the level the channel was opened at) until the stream started, the tone, the fill
  value at mid-scale during the gap, the tone again, and 0 V after the channel was closed.
- A 10-second run took 10.01 s, so playback runs at the true rate.
- No run had an underrun while writing, even with the smallest ring (a 2.7 ms refill
  deadline). The underruns while draining are expected: once the data runs out, each further
  half plays the fill value until the stream is stopped (see [Underruns](#underruns)).
- An earlier version of the tests reported a single count that included the drain. Its runs
  agree with these: 1 underrun in each of four runs of 14, 24 in 15, and 1 to 3 with the small
  rings, which looked like late writes. Separating the two counts showed they were all drain.

### Choosing a Ring Size

The writer must refill each half of the ring before DMA comes back to it, so the most a
writer can be late is about one half: `ring / 2 / rate`. With the default ring at 44,100 Hz
that is about 93 ms; with the largest (16,384), about 186 ms.

The test's writer runs at priority 180, so the results above are a best case. Managed code
runs differently: in Legacy all managed threads ran at NuttX priority 100 with round-robin
scheduling, and the same is expected here. That matters in two ways:

- **Higher-priority threads** (HCOM, the trace threads, the ESP32 threads, all above 100) can
  delay a managed writer. In normal use they run briefly.
- **Round-robin** shares the CPU between busy threads of equal priority in slices of
  `CONFIG_RR_INTERVAL`, 200 ms on Meadow. If another managed thread keeps the CPU busy, the
  writer can wait up to 200 ms, which is more than even the largest ring can cover at audio
  rates (about 171 ms at 48,000 Hz).

So for Meadow.Core:

- The default ring (8,192) suits most uses, provided the app is not also running CPU-bound
  managed code.
- For audio alongside busy code, use the largest ring, run the writer at a higher priority
  than other managed threads, or use a lower sample rate.
- Small rings (below about 1,024) are for low latency with a high-priority writer only.

If this proves limiting, the ring could give the writer the whole ring of slack instead of
half, by tracking DMA's position exactly rather than by halves; that would roughly double the
tolerance. It is not needed for the results above.

## Decisions

Settled in review:

1. **Opening a channel that is already open** restarts it rather than failing, so an app
   that stopped without closing never locks the next one out.
2. **VREF+** is the 3.3 V rail on all Meadow boards, shared by the ADC and the DAC. The test's
   expected voltages assume 3.3 V.
3. **Starting level:** open starts the output at 0, and the test reads it back and reports
   it along with its expected voltage.
4. **Error codes:** a failing step's own error code is passed back unchanged. The driver's
   own "not open" error is `-EBADF`, which nothing else in the driver returns.
