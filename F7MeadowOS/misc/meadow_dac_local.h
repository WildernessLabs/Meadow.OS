/****************************************************************************
 * F7MeadowOS/misc/meadow_dac_local.h
 *
 *   Copyright (C) 2026 Wilderness Labs. All rights reserved.
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

/* Definitions shared by the files of the Meadow DAC driver: meadow_dac.c
 * (static levels, buffer loops, triangle, noise and one-shots) and
 * meadow_dac_stream.c (streams). Not for use outside the driver.
 */

#ifndef __F7MEADOWOS_MISC_MEADOW_DAC_LOCAL_H
#define __F7MEADOWOS_MISC_MEADOW_DAC_LOCAL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

#include <nuttx/mutex.h>

#include "chip.h"
#include "stm32_dma.h"
#include "stm32_tim.h"

#include <meadow/meadow_syscall_support.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MEADOW_DAC_NCHANNELS      2

/* The largest value for 12-bit right-aligned data */

#define MEADOW_DAC_MAX_VALUE      4095

/* Pin identifiers use the meadow_adc_configure() encoding:
 * (port << 4) | pin, with port A = 0.
 */

#define MEADOW_DAC_PINID_PA4      0x04
#define MEADOW_DAC_PINID_PA5      0x05

#define MEADOW_DAC_GPIO_PA4       (GPIO_ANALOG | GPIO_FLOAT | GPIO_PORTA | \
                                   GPIO_PIN4)
#define MEADOW_DAC_GPIO_PA5       (GPIO_ANALOG | GPIO_FLOAT | GPIO_PORTA | \
                                   GPIO_PIN5)

/* DAC registers (RM0410, DAC chapter) */

#define MEADOW_DAC_CR             (STM32_DAC_BASE + 0x00)
#define MEADOW_DAC_DHR12R1        (STM32_DAC_BASE + 0x08)
#define MEADOW_DAC_DHR12R2        (STM32_DAC_BASE + 0x14)
#define MEADOW_DAC_DOR1           (STM32_DAC_BASE + 0x2c)
#define MEADOW_DAC_DOR2           (STM32_DAC_BASE + 0x30)
#define MEADOW_DAC_SR             (STM32_DAC_BASE + 0x34)

/* DAC_CR bits for channel 1. The channel 2 bits are the same, 16 bits up. */

#define MEADOW_DAC_CR_EN          (1 << 0)  /* Channel enable */
#define MEADOW_DAC_CR_BOFF        (1 << 1)  /* Output buffer disable */
#define MEADOW_DAC_CR_TEN         (1 << 2)  /* Trigger enable */
#define MEADOW_DAC_CR_TSEL_MASK   (7 << 3)  /* Trigger select; 0 = TIM6 */
#define MEADOW_DAC_CR_WAVE_NOISE  (1 << 6)  /* Noise generator */
#define MEADOW_DAC_CR_WAVE_TRI    (2 << 6)  /* Triangle generator */
#define MEADOW_DAC_CR_WAVE_MASK   (3 << 6)
#define MEADOW_DAC_CR_MAMP_SHIFT  8         /* Mask / amplitude selector */
#define MEADOW_DAC_CR_MAMP_MASK   (15 << 8)
#define MEADOW_DAC_CR_DMAEN       (1 << 12) /* DMA enable */
#define MEADOW_DAC_CR_CH_SHIFT    16

/* All the DAC_CR bits a waveform sets, for one channel */

#define MEADOW_DAC_CR_WAVE_BITS   (MEADOW_DAC_CR_TEN | MEADOW_DAC_CR_TSEL_MASK | \
                                   MEADOW_DAC_CR_WAVE_MASK | \
                                   MEADOW_DAC_CR_MAMP_MASK | \
                                   MEADOW_DAC_CR_DMAEN)

/* DAC_SR DMA underrun flag for channel 1; channel 2 is 16 bits up */

#define MEADOW_DAC_SR_DMAUDR      (1 << 13)

#define MEADOW_DAC_DATA_MASK      0x0fff

/* Waveform limits */

#define MEADOW_DAC_WAVE_MAX_SAMPLES  8192
#define MEADOW_DAC_WAVE_MAX_RATE     1000000   /* The DAC's maximum update rate */

/* Stream ring sizes, in samples */

#define MEADOW_DAC_STREAM_DEF_RING   8192
#define MEADOW_DAC_STREAM_MIN_RING   256
#define MEADOW_DAC_STREAM_MAX_RING   16384

/* The kernel's copy of a sample buffer is aligned to the data cache line */

#define MEADOW_DAC_WAVE_ALIGN     32

/* DMA set-up for a buffer loop: memory to peripheral, 16-bit on both sides,
 * memory increment, circular, high priority.
 */

#define MEADOW_DAC_DMA_SCR        (DMA_SCR_DIR_M2P | DMA_SCR_MINC | \
                                   DMA_SCR_PSIZE_16BITS | \
                                   DMA_SCR_MSIZE_16BITS | DMA_SCR_CIRC | \
                                   DMA_SCR_PRIHI)

/* The same for a one-shot, played once: normal mode instead of circular */

#define MEADOW_DAC_DMA_SCR_ONCE   (MEADOW_DAC_DMA_SCR & ~DMA_SCR_CIRC)

/* Fill values added after a one-shot clip. When DMA reports the last one
 * delivered, the first is already on the output, so every sample of the
 * clip has played for a full sample period (see MeadowDac.md).
 */

#define MEADOW_DAC_ONESHOT_TAIL   2

/* TIM6, the waveform trigger timer */

#define MEADOW_DAC_TIM_CLKIN      STM32_APB1_TIM6_CLKIN
#define MEADOW_DAC_TIM_CR1        (STM32_TIM6_BASE + STM32_BTIM_CR1_OFFSET)
#define MEADOW_DAC_TIM_CR2        (STM32_TIM6_BASE + STM32_BTIM_CR2_OFFSET)
#define MEADOW_DAC_TIM_EGR        (STM32_TIM6_BASE + STM32_BTIM_EGR_OFFSET)
#define MEADOW_DAC_TIM_PSC        (STM32_TIM6_BASE + STM32_BTIM_PSC_OFFSET)
#define MEADOW_DAC_TIM_ARR        (STM32_TIM6_BASE + STM32_BTIM_ARR_OFFSET)

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct meadow_dac_chan_s
{
  uint8_t   pinid;  /* meadow_adc_configure() style pin identifier */
  uint32_t  gpio;   /* stm32_configgpio() pin configuration */
  uint32_t  dhr;    /* 12-bit right-aligned data holding register */
  uint32_t  dor;    /* Data output register */
  uint8_t   shift;  /* Position of this channel's bits in DAC_CR */
  uint32_t  dmamap; /* stm32_dmachannel() stream and channel */
  uint32_t  dmacr;  /* That DMA stream's configuration register */
};

/* A waveform playing on one channel */

struct meadow_dac_wave_state_s
{
  uint32_t    type;  /* MEADOW_DAC_WAVE_*, or 0 when none is playing */
  DMA_HANDLE  dma;   /* Buffer loop, stream, one-shot: the DMA stream */
  uint16_t   *buf;   /* Buffer loop, one-shot: the kernel's copy;
                      * stream: the ring */

  /* One-shot only: set by the DMA handler when the clip has played to the
   * end. The clip's resources are released later, under the driver lock.
   */

  volatile bool done;

  /* Stream only. The ring is played in two halves. While running, at most
   * one half belongs to the app (target): the one DMA freed most recently.
   * The counts are updated from the DMA handler, so they are read and
   * changed with interrupts disabled.
   */

  uint32_t    sstate;     /* MEADOW_DAC_STREAM_* */
  uint32_t    ring;       /* Ring size in samples */
  uint32_t    half;       /* Half the ring */
  uint16_t    fill;       /* Value a half is reset to when freed */
  int8_t      target;     /* Half the app writes into; -1 = none yet */
  uint32_t    count[2];   /* App samples written into each half */
  uint32_t    played[2];  /* App samples in a half when DMA started it */
  uint32_t    underruns;  /* Halves DMA started without being full */
  uint32_t    seq;        /* Counts DMA half events */
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

/* Pin, register and DMA details of each channel */

extern const struct meadow_dac_chan_s g_dac_chan[MEADOW_DAC_NCHANNELS];

/* Protects the channels' open state and DAC_CR, which both channels share */

extern mutex_t g_dac_lock;

/* The waveform on each channel, if any */

extern struct meadow_dac_wave_state_s g_dac_wave[MEADOW_DAC_NCHANNELS];

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* meadow_dac.c */

int  meadow_dac_find_chan(uint8_t pinid);
int  meadow_dac_lock_open_chan(uint8_t pinid);
void meadow_dac_set_mode(const struct meadow_dac_chan_s *chan,
                         uint32_t bits);
void meadow_dac_tim_run(void);

/* meadow_dac_stream.c */

void meadow_dac_space_post(int index);

#endif /* __F7MEADOWOS_MISC_MEADOW_DAC_LOCAL_H */
