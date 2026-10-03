/****************************************************************************
 * F7MeadowOS/misc/meadow_dac_stream.c
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

/* Streams (issue #662) for the Meadow DAC driver. See meadow_dac.c for the
 * rest of the driver and Documentation/MeadowDac.md for the design.
 *
 * A stream plays a ring buffer through DMA in circular mode, like a buffer
 * loop, but refills it while it plays: DMA interrupts at the half-way point
 * and the end of the ring, each freeing the half just played, which the
 * handler resets to the fill value so that a half the app does not refill
 * in time plays that value instead of old samples.
 *
 * meadow_dac_wave_start() and meadow_dac_wave_status() in meadow_dac.c
 * check the caller's arguments and take the driver lock, then hand a stream
 * to meadow_dac_stream_prepare() and meadow_dac_stream_status() here.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>

#include <string.h>

#include <nuttx/cache.h>
#include <nuttx/clock.h>
#include <nuttx/irq.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/semaphore.h>

#include "stm32_dma.h"

#include <meadow/meadow_syscall_support.h>

#include "meadow_dac_local.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Posted by the DMA handler when a stream half is freed, to wake a writer
 * waiting for space. Never destroyed, so a writer may still be waiting on
 * it when its stream is stopped.
 */

static sem_t g_dac_space[MEADOW_DAC_NCHANNELS] =
{
  NXSEM_INITIALIZER(0, SEM_PRIO_NONE),
  NXSEM_INITIALIZER(0, SEM_PRIO_NONE)
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: meadow_dac_space_post
 *
 * Description:
 *   Wake a writer waiting for space on a channel. The semaphore is kept at
 *   no more than 1, so posts with nobody waiting do not pile up.
 *
 ****************************************************************************/

void meadow_dac_space_post(int index)
{
  int value = 0;

  nxsem_get_value(&g_dac_space[index], &value);
  if (value < 1)
    {
      nxsem_post(&g_dac_space[index]);
    }
}

/****************************************************************************
 * Name: meadow_dac_stream_half
 *
 * Description:
 *   DMA has finished playing half 'done' of a stream's ring and started on
 *   half 'next'. Count an underrun if the app had not filled 'next', reset
 *   'done' to the fill value, and hand it to the app. Interrupt context.
 *
 ****************************************************************************/

static void meadow_dac_stream_half(int index, int done, int next)
{
  struct meadow_dac_wave_state_s *st = &g_dac_wave[index];
  uint16_t *half = st->buf + (done * st->half);
  uint32_t i;

  if (st->target == next && st->count[next] < st->half)
    {
      st->underruns++;
    }

  st->played[next] = st->count[next];

  for (i = 0; i < st->half; i++)
    {
      half[i] = st->fill;
    }

  up_clean_dcache((uintptr_t)half,
                  (uintptr_t)(half + st->half));

  st->count[done]  = 0;
  st->played[done] = 0;
  st->target       = done;
  st->seq++;

  meadow_dac_space_post(index);
}

/****************************************************************************
 * Name: meadow_dac_stream_dma
 *
 * Description:
 *   DMA callback for a stream: the half-transfer interrupt means half 0 has
 *   been played, the transfer-complete interrupt means half 1 has.
 *
 ****************************************************************************/

static void meadow_dac_stream_dma(DMA_HANDLE handle, uint8_t status,
                                  void *arg)
{
  int index = (int)(uintptr_t)arg;
  struct meadow_dac_wave_state_s *st = &g_dac_wave[index];

  UNUSED(handle);

  if (st->type != MEADOW_DAC_WAVE_STREAM ||
      st->sstate != MEADOW_DAC_STREAM_RUNNING)
    {
      return;
    }

  if ((status & DMA_STATUS_HTIF) != 0)
    {
      meadow_dac_stream_half(index, 0, 1);
    }

  if ((status & DMA_STATUS_TCIF) != 0)
    {
      meadow_dac_stream_half(index, 1, 0);
    }
}

/****************************************************************************
 * Name: meadow_dac_stream_run
 *
 * Description:
 *   Start playing a primed stream. Called with the driver lock held.
 *
 ****************************************************************************/

static void meadow_dac_stream_run(int index)
{
  const struct meadow_dac_chan_s *chan = &g_dac_chan[index];
  struct meadow_dac_wave_state_s *st = &g_dac_wave[index];
  irqstate_t flags;

  flags = enter_critical_section();
  st->played[0] = st->count[0];
  st->played[1] = st->count[1];
  st->target    = -1;
  st->sstate    = MEADOW_DAC_STREAM_RUNNING;
  leave_critical_section(flags);

  /* Both DMA interrupts are wanted here, so use stm32_dmastart() with the
   * half-transfer interrupt.
   */

  stm32_dmasetup(st->dma, chan->dhr, (uint32_t)(uintptr_t)st->buf,
                 st->ring, MEADOW_DAC_DMA_SCR);
  stm32_dmastart(st->dma, meadow_dac_stream_dma, (void *)(uintptr_t)index,
                 true);

  meadow_dac_set_mode(chan, MEADOW_DAC_CR_TEN | MEADOW_DAC_CR_DMAEN);
  meadow_dac_tim_run();
}

/****************************************************************************
 * Name: meadow_dac_stream_put
 *
 * Description:
 *   Copy as many samples as fit into a stream's ring and return how many
 *   were taken. Starts output once priming fills the ring. Called with the
 *   driver lock held; the samples have already been checked.
 *
 ****************************************************************************/

static uint32_t meadow_dac_stream_put(int index, const uint16_t *samples,
                                      uint32_t count)
{
  struct meadow_dac_wave_state_s *st = &g_dac_wave[index];
  irqstate_t flags;
  uint16_t *dest;
  uint32_t written;
  uint32_t space;
  uint32_t seq;
  uint32_t n;
  int target;

  if (st->sstate == MEADOW_DAC_STREAM_PRIMING)
    {
      /* Fill the ring from the start, half 0 then half 1 */

      written = st->count[0] + st->count[1];
      n = st->ring - written;
      if (n > count)
        {
          n = count;
        }

      dest = st->buf + written;
      memcpy(dest, samples, n * sizeof(uint16_t));
      up_clean_dcache((uintptr_t)dest, (uintptr_t)(dest + n));

      written += n;
      st->count[0] = written < st->half ? written : st->half;
      st->count[1] = written > st->half ? written - st->half : 0;

      if (written == st->ring)
        {
          meadow_dac_stream_run(index);
        }

      return n;
    }

  /* Running: write into the half DMA freed most recently */

  flags  = enter_critical_section();
  target = st->target;
  seq    = st->seq;
  space  = target < 0 ? 0 : st->half - st->count[target];
  n      = target < 0 ? 0 : st->count[target];
  leave_critical_section(flags);

  if (space == 0)
    {
      return 0;
    }

  dest  = st->buf + (target * st->half) + n;   /* n = samples already there */
  n     = space < count ? space : count;
  memcpy(dest, samples, n * sizeof(uint16_t));
  up_clean_dcache((uintptr_t)dest, (uintptr_t)(dest + n));

  /* If DMA moved on during the copy, this half is already playing and the
   * handler has handed the app the other one; the samples just copied play
   * if DMA has not passed them, and are counted as taken either way.
   */

  flags = enter_critical_section();
  if (st->seq == seq)
    {
      st->count[target] += n;
    }

  leave_critical_section(flags);
  return n;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: meadow_dac_stream_prepare
 *
 * Description:
 *   Set up a stream for meadow_dac_wave_start(): allocate its ring, take
 *   the channel's DMA stream and reset the stream's state to priming. The
 *   caller records the ring and DMA stream in the channel's state. Called
 *   with the driver lock held; the ring size and fill value have already
 *   been checked.
 *
 * Input Parameters:
 *   index - Channel index.
 *   ring  - Ring size in samples.
 *   fill  - The fill value.
 *   pbuf  - Set to the ring on success.
 *   pdma  - Set to the DMA stream on success.
 *
 * Returned Value:
 *   0 on success, -ENOMEM if the ring cannot be allocated, or -EBUSY if the
 *   channel's DMA stream is in use. On failure nothing is left allocated
 *   and the stream's state is unchanged.
 *
 ****************************************************************************/

int meadow_dac_stream_prepare(int index, uint32_t ring, uint16_t fill,
                              uint16_t **pbuf, DMA_HANDLE *pdma)
{
  const struct meadow_dac_chan_s *chan = &g_dac_chan[index];
  struct meadow_dac_wave_state_s *state = &g_dac_wave[index];
  DMA_HANDLE dma;
  uint16_t *buf;
  uint32_t i;

  /* The ring starts full of the fill value; output starts once the app
   * has filled it (or asks for it with a zero-length write).
   */

  buf = kmm_memalign(MEADOW_DAC_WAVE_ALIGN, ring * sizeof(uint16_t));
  if (buf == NULL)
    {
      return -ENOMEM;
    }

  for (i = 0; i < ring; i++)
    {
      buf[i] = fill;
    }

  up_clean_dcache((uintptr_t)buf, (uintptr_t)(buf + ring));

  dma = stm32_dmachannel(chan->dmamap);
  if (dma == NULL)
    {
      kmm_free(buf);
      return -EBUSY;
    }

  state->ring      = ring;
  state->half      = ring / 2;
  state->fill      = fill;
  state->target    = -1;
  state->count[0]  = 0;
  state->count[1]  = 0;
  state->played[0] = 0;
  state->played[1] = 0;
  state->underruns = 0;
  state->seq       = 0;
  state->ufrozen   = false;
  state->uend      = 0;
  state->sstate    = MEADOW_DAC_STREAM_PRIMING;

  *pbuf = buf;
  *pdma = dma;
  return OK;
}

/****************************************************************************
 * Name: meadow_dac_stream_status
 *
 * Description:
 *   Fill in a stream's part of meadow_dac_wave_status(): its state,
 *   underruns, samples queued and space free. Called with the driver lock
 *   held, on a channel that is streaming; 'out' has been zeroed.
 *
 ****************************************************************************/

void meadow_dac_stream_status(int index,
                              struct meadow_dac_wave_status_s *out)
{
  struct meadow_dac_wave_state_s *st = &g_dac_wave[index];
  irqstate_t flags;
  uint32_t pos;
  uint32_t off;
  int playing;

  out->state     = st->owner != 0 ? MEADOW_DAC_FILE_PLAYING : st->sstate;
  out->underruns = st->ufrozen ? st->uend : st->underruns;

  if (st->sstate == MEADOW_DAC_STREAM_PRIMING)
    {
      out->queued = st->count[0] + st->count[1];
      out->space  = st->ring - out->queued;
      return;
    }

  /* App samples left in the half DMA is playing, from its position in the
   * ring, plus those written into the app's half.
   */

  flags   = enter_critical_section();
  pos     = st->ring - (uint32_t)stm32_dmaresidual(st->dma);
  playing = pos < st->half ? 0 : 1;
  off     = pos - (playing * st->half);

  if (st->played[playing] > off)
    {
      out->queued = st->played[playing] - off;
    }

  if (st->target >= 0 && st->target != playing)
    {
      out->queued += st->count[st->target];
      out->space   = st->half - st->count[st->target];
    }

  leave_critical_section(flags);
}

/****************************************************************************
 * Name: meadow_dac_stream_write
 *
 * Description:
 *   Write samples to the stream on a channel, as meadow_dac_wave_write()
 *   describes, if the stream belongs to 'owner': the app's (0) or one the
 *   file player started. The samples have already been checked.
 *
 * Returned Value:
 *   As for meadow_dac_wave_write(), and -EBUSY if the app writes to a
 *   stream the file player owns.
 *
 ****************************************************************************/

int meadow_dac_stream_write(uint8_t pinid, const uint16_t *samples,
                            uint32_t count, uint32_t timeout_ms,
                            uint32_t owner)
{
  struct meadow_dac_wave_state_s *st;
  clock_t deadline;
  clock_t now;
  uint32_t taken = 0;
  int index;
  int ret;

  index = meadow_dac_find_chan(pinid);
  if (index < 0)
    {
      return index;
    }

  st = &g_dac_wave[index];
  deadline = clock_systime_ticks() + MSEC2TICK(timeout_ms);

  for (; ; )
    {
      index = meadow_dac_lock_open_chan(pinid);
      if (index < 0)
        {
          return taken > 0 ? (int)taken : index;
        }

      if (st->type != MEADOW_DAC_WAVE_STREAM || st->owner != owner)
        {
          ret = st->type == MEADOW_DAC_WAVE_STREAM && owner == 0 ?
                -EBUSY : -ENOTCONN;
          nxmutex_unlock(&g_dac_lock);
          return taken > 0 ? (int)taken : ret;
        }

      if (count == 0)
        {
          if (st->sstate == MEADOW_DAC_STREAM_PRIMING)
            {
              meadow_dac_stream_run(index);
            }

          nxmutex_unlock(&g_dac_lock);
          return 0;
        }

      taken += meadow_dac_stream_put(index, samples + taken,
                                     count - taken);
      nxmutex_unlock(&g_dac_lock);

      if (taken == count || timeout_ms == 0)
        {
          return (int)taken;
        }

      now = clock_systime_ticks();
      if (now >= deadline)
        {
          return (int)taken;
        }

      ret = nxsem_tickwait(&g_dac_space[index], (uint32_t)(deadline - now));
      if (ret < 0 && ret != -ETIMEDOUT)
        {
          /* Interrupted, for example because the app is being stopped */

          return taken > 0 ? (int)taken : ret;
        }
    }
}

/****************************************************************************
 * Name: meadow_dac_stream_lock_owned
 *
 * Description:
 *   Take the driver lock if the channel is open and streams for 'owner'.
 *   Returns the channel index with the lock held, or a negative error with
 *   it not held: -ENOTCONN if the stream has gone or belongs to another.
 *
 ****************************************************************************/

static int meadow_dac_stream_lock_owned(uint8_t pinid, uint32_t owner)
{
  int index;

  index = meadow_dac_lock_open_chan(pinid);
  if (index < 0)
    {
      return index;
    }

  if (g_dac_wave[index].type != MEADOW_DAC_WAVE_STREAM ||
      g_dac_wave[index].owner != owner)
    {
      nxmutex_unlock(&g_dac_lock);
      return -ENOTCONN;
    }

  return index;
}

/****************************************************************************
 * Name: meadow_dac_stream_end
 *
 * Description:
 *   The file player has written the whole file to its stream: freeze the
 *   underrun count, so the halves that underrun while the stream drains are
 *   not reported, and start output if the ring never filled.
 *
 * Returned Value:
 *   0, or -ENOTCONN if the stream has gone, or the error from taking the
 *   channel.
 *
 ****************************************************************************/

int meadow_dac_stream_end(uint8_t pinid, uint32_t owner)
{
  struct meadow_dac_wave_state_s *st;
  int index;

  index = meadow_dac_stream_lock_owned(pinid, owner);
  if (index < 0)
    {
      return index;
    }

  st = &g_dac_wave[index];
  st->uend    = st->underruns;
  st->ufrozen = true;

  if (st->sstate == MEADOW_DAC_STREAM_PRIMING)
    {
      meadow_dac_stream_run(index);
    }

  nxmutex_unlock(&g_dac_lock);
  return OK;
}

/****************************************************************************
 * Name: meadow_dac_stream_queued
 *
 * Description:
 *   Return the samples written to the file player's stream and not yet
 *   played, or -ENOTCONN if the stream has gone (or an error from taking
 *   the channel).
 *
 ****************************************************************************/

int meadow_dac_stream_queued(uint8_t pinid, uint32_t owner)
{
  struct meadow_dac_wave_status_s out;
  int index;

  index = meadow_dac_stream_lock_owned(pinid, owner);
  if (index < 0)
    {
      return index;
    }

  memset(&out, 0, sizeof(out));
  meadow_dac_stream_status(index, &out);

  nxmutex_unlock(&g_dac_lock);
  return (int)out.queued;
}

/****************************************************************************
 * Name: meadow_dac_stream_stop
 *
 * Description:
 *   Stop the file player's stream, if it is still the one on the channel.
 *   A stream that has been stopped or replaced is left alone.
 *
 ****************************************************************************/

void meadow_dac_stream_stop(uint8_t pinid, uint32_t owner)
{
  int index;

  index = meadow_dac_stream_lock_owned(pinid, owner);
  if (index >= 0)
    {
      meadow_dac_wave_stop_locked(index);
      nxmutex_unlock(&g_dac_lock);
    }
}

/****************************************************************************
 * Name: meadow_dac_wave_write
 *
 * Description:
 *   Write samples to a stream started with MEADOW_DAC_WAVE_STREAM. Takes as
 *   many as fit, waiting up to timeout_ms in all for DMA to free space. A
 *   count of 0 starts output of a priming stream at once. Waits without the
 *   driver lock, so a stop or close from another thread ends the wait.
 *
 * Input Parameters:
 *   pinid      - Pin identifier, as for meadow_dac_open().
 *   samples    - The samples, 0 to 4095 each.
 *   count      - Number of samples.
 *   timeout_ms - Longest wait for space, in ms; 0 = do not wait.
 *
 * Returned Value:
 *   The number of samples taken (0 to count), or -EBADSLT for a pin with no
 *   DAC output, -EBADF if the channel is not open, -EINVAL for NULL samples
 *   or a sample above 4095, -ENOTCONN if the channel is not streaming,
 *   -EBUSY if the file player owns the stream, or an error passed back from
 *   a lower-level call.
 *
 ****************************************************************************/

int meadow_dac_wave_write(uint8_t pinid, const uint16_t *samples,
                          uint32_t count, uint32_t timeout_ms)
{
  uint32_t i;

  if (count != 0 && samples == NULL)
    {
      return -EINVAL;
    }

  /* Check every sample before any is taken */

  for (i = 0; i < count; i++)
    {
      if (samples[i] > MEADOW_DAC_MAX_VALUE)
        {
          return -EINVAL;
        }
    }

  return meadow_dac_stream_write(pinid, samples, count, timeout_ms, 0);
}
