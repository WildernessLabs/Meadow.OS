/****************************************************************************
 * nuttx\include\meadow\meadow_syscall_support.h
 * 
 *   Copyright (C) 2022 Wilderness Labs. All rights reserved.
 *   Author:  Wilderness Labs
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

#ifndef __CONFIG_MEADOW_SRC_MEADOW_SYSCALL_SUPPORT__H
#define __CONFIG_MEADOW_SRC_MEADOW_SYSCALL_SUPPORT__H

#include <nuttx/config.h>

#include <sys/types.h>

#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <debug.h>

/************************************************************************************
 * Pre-processor Definitions
 ************************************************************************************/

// Meadow DAC: meadow_dac_open() flags. 0 = output buffer on.
#define MEADOW_DAC_FLAG_BUFFER_OFF  (1 << 0)

// Meadow DAC: struct meadow_dac_wave_s types
#define MEADOW_DAC_WAVE_BUFFER      1   // Loop a buffer of samples
#define MEADOW_DAC_WAVE_TRIANGLE    2   // DAC triangle generator
#define MEADOW_DAC_WAVE_NOISE       3   // DAC noise generator
#define MEADOW_DAC_WAVE_STREAM      4   // Stream samples written by the app

// Meadow DAC: a waveform for meadow_dac_wave_start(). Every field is 32 bits,
// so the managed side can mirror it with a sequential struct. Fields a type
// does not use are ignored and should be 0.
struct meadow_dac_wave_s
{
  uint32_t        size;       // sizeof(struct meadow_dac_wave_s), set by the caller
  uint32_t        type;       // MEADOW_DAC_WAVE_*
  uint32_t        rate_hz;    // Requested sample rate, 1 to 1,000,000
  uint32_t        actual_hz;  // Out: the rate actually set
  uint32_t        amplitude;  // Triangle and noise only: 2^n - 1, n = 1 to 12
  uint32_t        count;      // Buffer only: number of samples, 1 to 8192
  const uint16_t *samples;    // Buffer only: the samples, 0 to 4095 each
  uint32_t        ring;       // Stream only: ring size in samples, even, 256 to
                              //   16384; 0 = 8192
  uint32_t        fill;       // Stream only: value played on underrun, 0 to 4095
};

// Meadow DAC: struct meadow_dac_wave_status_s states
#define MEADOW_DAC_STREAM_NONE      0   // Not streaming
#define MEADOW_DAC_STREAM_PRIMING   1   // Started, waiting for the ring to fill
#define MEADOW_DAC_STREAM_RUNNING   2   // Playing

// Meadow DAC: a stream's status, from meadow_dac_wave_status()
struct meadow_dac_wave_status_s
{
  uint32_t size;       // sizeof(struct meadow_dac_wave_status_s), set by the caller
  uint32_t state;      // Out: MEADOW_DAC_STREAM_*
  uint32_t queued;     // Out: samples written and not yet played
  uint32_t space;      // Out: samples that can be written now without waiting
  uint32_t underruns;  // Out: underruns since the stream started
};

/************************************************************************************
 * Public Functions
 ************************************************************************************/

// Make GPIO accessible to user-side
int stm32_configgpio(uint32_t cfgset);
int stm32_unconfiggpio(uint32_t cfgset);
void stm32_gpiowrite(uint32_t pinset, bool value);

// Meadow ADC
int meadow_adc_configure(uint8_t *gpioList, uint32_t gpioCount, double *userDataBuf);
int meadow_adc_read_values(void);
int meadow_adc_read_temp_vbat(double *batteryVoltage, double *temperatureValue);

// Meadow DAC (see Documentation/MeadowDac.md)
int meadow_dac_open(uint8_t pinid, uint32_t flags);
int meadow_dac_write(uint8_t pinid, uint32_t value);
int meadow_dac_read(uint8_t pinid, uint32_t *value);
int meadow_dac_close(uint8_t pinid);
int meadow_dac_wave_start(uint8_t pinid, struct meadow_dac_wave_s *wave);
int meadow_dac_wave_stop(uint8_t pinid);
int meadow_dac_wave_write(uint8_t pinid, const uint16_t *samples,
                          uint32_t count, uint32_t timeout_ms);
int meadow_dac_wave_status(uint8_t pinid,
                           struct meadow_dac_wave_status_s *status);

int pwrmgmt_most_recent_wakeup_reason(void);

#endif // __CONFIG_MEADOW_SRC_MEADOW_SYSCALL_SUPPORT__H