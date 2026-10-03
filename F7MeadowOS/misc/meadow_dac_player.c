/****************************************************************************
 * F7MeadowOS/misc/meadow_dac_player.c
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

/* The file player (issue #664) for the Meadow DAC driver: plays a .WAV file
 * on a DAC channel. See Documentation/MeadowDac.md, "File Player".
 *
 * meadow_dac_play_file() opens the file and reads its header before it
 * returns, so a missing file or an unsupported format is reported to the
 * caller. It then starts a stream (meadow_dac_stream.c) at the file's rate,
 * owned by the player, and a kernel thread that reads the file a block at a
 * time, converts the samples to 12 bits and writes them to the stream. At
 * the end of the file the thread waits for the stream to drain, stops it
 * and exits, or with MEADOW_DAC_PLAY_LOOP starts again from the beginning.
 *
 * Each stream the player starts gets a new number (its owner). The thread
 * passes it with every call, so once the stream has been stopped, or
 * replaced by another file, the thread only closes its file and exits.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <syslog.h>

#include <nuttx/fs/fs.h>
#include <nuttx/kmalloc.h>
#include <nuttx/kthread.h>
#include <nuttx/mutex.h>
#include <nuttx/signal.h>

#include <meadow/meadow_syscall_support.h>
#include <meadow/meadow_thread_config.h>

#include "meadow_dac_local.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Most samples converted and written at a time, and the bytes read for
 * them (16-bit stereo: 2 bytes per sample, 2 samples per frame, but only
 * 2048 bytes are read, so at most 512 frames)
 */

#define MEADOW_DAC_PLAYER_SAMPLES  1024
#define MEADOW_DAC_PLAYER_BYTES    2048

/* The stream's fill value, and the level playback starts from: mid-scale,
 * silence for audio
 */

#define MEADOW_DAC_PLAYER_FILL     2048

/* Longest wait for each write to the stream. The stream always takes
 * samples while it runs, so a write returns long before this unless the
 * stream is stopped; the thread then writes again.
 */

#define MEADOW_DAC_PLAYER_WAIT_MS  1000

/* WAV format tags */

#define MEADOW_DAC_WAV_PCM         0x0001
#define MEADOW_DAC_WAV_EXTENSIBLE  0xfffe

/* The part of a fmt chunk the player reads: up to the end of the
 * WAVE_FORMAT_EXTENSIBLE sub-format GUID
 */

#define MEADOW_DAC_WAV_FMT_MAX     40

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* A file playing on one channel, owned by its thread */

struct meadow_dac_player_s
{
  struct file file;      /* The open .WAV file */
  uint8_t     pinid;     /* The channel's pin */
  uint32_t    owner;     /* The number of the stream the player started */
  bool        loop;      /* MEADOW_DAC_PLAY_LOOP */
  uint16_t    channels;  /* 1 or 2 */
  uint16_t    bits;      /* 8 or 16 */
  uint32_t    rate;      /* Sample rate, Hz */
  uint32_t    frame;     /* Bytes per frame: channels * bits / 8 */
  off_t       datapos;   /* Offset of the sample data in the file */
  uint32_t    datalen;   /* Bytes of sample data, whole frames only */
  uint8_t     bytes[MEADOW_DAC_PLAYER_BYTES];
  uint16_t    samples[MEADOW_DAC_PLAYER_SAMPLES];
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* KSDATAFORMAT_SUBTYPE_PCM, the sub-format GUID of an extensible PCM file */

static const uint8_t g_dac_wav_pcm_guid[16] =
{
  0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
  0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71
};

/* The number given to the last stream the player started; 0 is never used.
 * Changed under the driver lock.
 */

static uint32_t g_dac_player_owner;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint16_t meadow_dac_le16(const uint8_t *p)
{
  return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t meadow_dac_le32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/****************************************************************************
 * Name: meadow_dac_player_read
 *
 * Description:
 *   Read exactly nbytes of the file's header. Returns 0, the error from
 *   the read, or -EINVAL if the file ends first.
 *
 ****************************************************************************/

static int meadow_dac_player_read(struct meadow_dac_player_s *pl,
                                  void *buf, size_t nbytes)
{
  ssize_t ret;

  ret = file_read(&pl->file, buf, nbytes);
  if (ret < 0)
    {
      return (int)ret;
    }

  return (size_t)ret == nbytes ? OK : -EINVAL;
}

/****************************************************************************
 * Name: meadow_dac_player_parse
 *
 * Description:
 *   Read a .WAV file's header: check that it is a RIFF WAVE file, read its
 *   fmt chunk, skip any other chunks, and find its data chunk. On success
 *   the file is positioned at the start of the sample data.
 *
 * Returned Value:
 *   0 on success, -EINVAL if the file is not a WAVE file or has no fmt or
 *   data chunk, -ENOTSUP for a format the player does not support, or the
 *   error from a file call.
 *
 ****************************************************************************/

static int meadow_dac_player_parse(struct meadow_dac_player_s *pl)
{
  uint8_t hdr[MEADOW_DAC_WAV_FMT_MAX];
  struct stat sb;
  bool havefmt = false;
  uint32_t size;
  uint32_t skip;
  uint32_t n;
  uint16_t tag;
  uint16_t align;
  off_t avail;
  off_t pos;
  int ret;

  ret = file_fstat(&pl->file, &sb);
  if (ret < 0)
    {
      return ret;
    }

  ret = meadow_dac_player_read(pl, hdr, 12);
  if (ret < 0)
    {
      return ret;
    }

  if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0)
    {
      return -EINVAL;
    }

  pos = 12;

  for (; ; )
    {
      /* Each chunk: a 4-character id and a 32-bit length, then the data,
       * padded to an even length. A file that ends before a data chunk
       * fails the read with -EINVAL.
       */

      ret = meadow_dac_player_read(pl, hdr, 8);
      if (ret < 0)
        {
          return ret;
        }

      size = meadow_dac_le32(hdr + 4);
      pos += 8;

      if (memcmp(hdr, "fmt ", 4) == 0)
        {
          if (size < 16)
            {
              return -EINVAL;
            }

          n = size < MEADOW_DAC_WAV_FMT_MAX ? size : MEADOW_DAC_WAV_FMT_MAX;
          ret = meadow_dac_player_read(pl, hdr, n);
          if (ret < 0)
            {
              return ret;
            }

          tag          = meadow_dac_le16(hdr);
          pl->channels = meadow_dac_le16(hdr + 2);
          pl->rate     = meadow_dac_le32(hdr + 4);
          align        = meadow_dac_le16(hdr + 12);
          pl->bits     = meadow_dac_le16(hdr + 14);

          if (tag == MEADOW_DAC_WAV_EXTENSIBLE)
            {
              if (n < MEADOW_DAC_WAV_FMT_MAX ||
                  memcmp(hdr + 24, g_dac_wav_pcm_guid, 16) != 0)
                {
                  return -ENOTSUP;
                }
            }
          else if (tag != MEADOW_DAC_WAV_PCM)
            {
              return -ENOTSUP;
            }

          if ((pl->channels != 1 && pl->channels != 2) ||
              (pl->bits != 8 && pl->bits != 16) ||
              pl->rate > MEADOW_DAC_WAVE_MAX_RATE)
            {
              return -ENOTSUP;
            }

          pl->frame = pl->channels * (pl->bits / 8);
          if (pl->rate == 0 || align != pl->frame)
            {
              return -EINVAL;
            }

          havefmt = true;
          pos += n;
          skip = (size - n) + (size & 1);
        }
      else if (memcmp(hdr, "data", 4) == 0)
        {
          if (!havefmt)
            {
              return -EINVAL;
            }

          /* Play no further than the end of the file, and whole frames */

          avail = sb.st_size > pos ? sb.st_size - pos : 0;
          pl->datapos = pos;
          pl->datalen = (off_t)size < avail ? size : (uint32_t)avail;
          pl->datalen -= pl->datalen % pl->frame;
          return OK;
        }
      else
        {
          skip = size + (size & 1);
        }

      if (skip != 0)
        {
          pos = file_seek(&pl->file, pos + skip, SEEK_SET);
          if (pos < 0)
            {
              return (int)pos;
            }
        }

      if (pos >= sb.st_size)
        {
          return -EINVAL;
        }
    }
}

/****************************************************************************
 * Name: meadow_dac_player_convert
 *
 * Description:
 *   Convert nbytes of sample data in pl->bytes to 12-bit samples in
 *   pl->samples, mixing a stereo file down to mono. Returns the number of
 *   samples; a part frame at the end is dropped.
 *
 ****************************************************************************/

static uint32_t meadow_dac_player_convert(struct meadow_dac_player_s *pl,
                                          size_t nbytes)
{
  const uint8_t *b = pl->bytes;
  uint32_t frames = nbytes / pl->frame;
  uint32_t i;
  int32_t v;

  for (i = 0; i < frames; i++, b += pl->frame)
    {
      if (pl->bits == 16)
        {
          /* Signed 16-bit: -32768 to 32767 becomes 0 to 4095 */

          v = (int16_t)meadow_dac_le16(b);
          if (pl->channels == 2)
            {
              v = (v + (int16_t)meadow_dac_le16(b + 2)) / 2;
            }

          pl->samples[i] = (uint16_t)((v + 32768) >> 4);
        }
      else
        {
          /* Unsigned 8-bit: 0 to 255 becomes 0 to 4080 */

          v = b[0];
          if (pl->channels == 2)
            {
              v = (v + b[1]) / 2;
            }

          pl->samples[i] = (uint16_t)(v << 4);
        }
    }

  return frames;
}

/****************************************************************************
 * Name: meadow_dac_player_drain
 *
 * Description:
 *   The whole file has been written: start output if the ring never
 *   filled, wait for everything written to play, then stop the stream.
 *   Gives up waiting after the ring's playing time plus a second, so a
 *   stream that stopped moving cannot keep the thread forever.
 *
 ****************************************************************************/

static void meadow_dac_player_drain(struct meadow_dac_player_s *pl)
{
  uint32_t tries;
  int queued;

  if (meadow_dac_stream_end(pl->pinid, pl->owner) < 0)
    {
      return;   /* Stopped or replaced */
    }

  tries = (MEADOW_DAC_STREAM_DEF_RING * 100) / pl->rate + 100;

  for (; ; )
    {
      queued = meadow_dac_stream_queued(pl->pinid, pl->owner);
      if (queued <= 0 || tries-- == 0)
        {
          break;
        }

      nxsig_usleep(10000);
    }

  if (queued >= 0)
    {
      meadow_dac_stream_stop(pl->pinid, pl->owner);
    }
}

/****************************************************************************
 * Name: meadow_dac_player_thread
 *
 * Description:
 *   The player thread: read, convert and write the file until it ends or
 *   the stream is stopped or replaced, then close the file and free the
 *   player. argv[1] is the player's address.
 *
 ****************************************************************************/

static int meadow_dac_player_thread(int argc, char *argv[])
{
  struct meadow_dac_player_s *pl;
  uint32_t remaining;
  uint32_t block;
  uint32_t count;
  uint32_t done;
  ssize_t nread;
  int ret;

  UNUSED(argc);

  pl = (struct meadow_dac_player_s *)(uintptr_t)strtoul(argv[1], NULL, 16);

  /* Whole frames that fit both buffers */

  block = MEADOW_DAC_PLAYER_BYTES / pl->frame;
  if (block > MEADOW_DAC_PLAYER_SAMPLES)
    {
      block = MEADOW_DAC_PLAYER_SAMPLES;
    }

  block *= pl->frame;
  remaining = pl->datalen;

  for (; ; )
    {
      if (remaining == 0)
        {
          if (!pl->loop || pl->datalen == 0 ||
              file_seek(&pl->file, pl->datapos, SEEK_SET) < 0)
            {
              break;
            }

          remaining = pl->datalen;
        }

      nread = file_read(&pl->file, pl->bytes,
                        remaining < block ? remaining : block);
      if (nread <= 0)
        {
          /* A read error, or the file got shorter: end here */

          if (nread < 0)
            {
              syslog(LOG_ERR, "DAC player: read failed, %d\n", (int)nread);
            }

          break;
        }

      remaining -= (uint32_t)nread;
      count = meadow_dac_player_convert(pl, (size_t)nread);

      for (done = 0; done < count; done += (uint32_t)ret)
        {
          ret = meadow_dac_stream_write(pl->pinid, pl->samples + done,
                                        count - done,
                                        MEADOW_DAC_PLAYER_WAIT_MS,
                                        pl->owner);
          if (ret < 0)
            {
              goto out;   /* Stopped or replaced */
            }
        }
    }

  meadow_dac_player_drain(pl);

out:
  file_close(&pl->file);
  kmm_free(pl);
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: meadow_dac_play_file
 *
 * Description:
 *   Play a .WAV file on an open channel: 8- or 16-bit PCM, mono or stereo
 *   (mixed down to mono), at the file's sample rate. Returns once the file
 *   has started; a kernel thread plays it. A file already playing on the
 *   channel is replaced. meadow_dac_wave_stop(), meadow_dac_close() and
 *   meadow_dac_open() stop it; meadow_dac_wave_status() reports
 *   MEADOW_DAC_FILE_PLAYING while it plays.
 *
 * Input Parameters:
 *   pinid - Pin identifier, as for meadow_dac_open().
 *   path  - Absolute path of the file.
 *   flags - 0 or MEADOW_DAC_PLAY_LOOP.
 *
 * Returned Value:
 *   0 once the file has started, -EBADSLT for a pin with no DAC output,
 *   -EBADF if the channel is not open, -EINVAL for a bad argument or a file
 *   that is not a WAVE file, -ENAMETOOLONG for a path longer than PATH_MAX,
 *   -ENOTSUP for an unsupported WAVE format, -EBUSY if the channel plays a
 *   waveform other than a file or the other channel plays at a different
 *   rate, -ENOMEM, or the error from opening or reading the file.
 *
 ****************************************************************************/

int meadow_dac_play_file(uint8_t pinid, const char *path, uint32_t flags)
{
  struct meadow_dac_player_s *pl;
  struct meadow_dac_wave_s w;
  char arg[16];
  char *argv[2];
  char *kpath;
  size_t len;
  int ret;

  ret = meadow_dac_find_chan(pinid);
  if (ret < 0)
    {
      return ret;
    }

  if ((flags & ~MEADOW_DAC_PLAY_LOOP) != 0 || path == NULL)
    {
      return -EINVAL;
    }

  /* Copy the path, so the caller cannot change it while it is used */

  len = strnlen(path, PATH_MAX);
  if (len == PATH_MAX)
    {
      return -ENAMETOOLONG;
    }

  kpath = kmm_malloc(len + 1);
  if (kpath == NULL)
    {
      return -ENOMEM;
    }

  memcpy(kpath, path, len);
  kpath[len] = '\0';

  if (kpath[0] != '/')
    {
      kmm_free(kpath);
      return -EINVAL;
    }

  pl = kmm_zalloc(sizeof(struct meadow_dac_player_s));
  if (pl == NULL)
    {
      kmm_free(kpath);
      return -ENOMEM;
    }

  /* The kernel's own file, not one in the caller's file table, so it stays
   * open for the thread after this call returns
   */

  ret = file_open(&pl->file, kpath, O_RDONLY);
  kmm_free(kpath);
  if (ret < 0)
    {
      kmm_free(pl);
      return ret;
    }

  ret = meadow_dac_player_parse(pl);
  if (ret < 0)
    {
      goto errout;
    }

  pl->pinid = pinid;
  pl->loop  = (flags & MEADOW_DAC_PLAY_LOOP) != 0;

  nxmutex_lock(&g_dac_lock);
  if (++g_dac_player_owner == 0)
    {
      g_dac_player_owner = 1;
    }

  pl->owner = g_dac_player_owner;
  nxmutex_unlock(&g_dac_lock);

  /* A stream at the file's rate with the default ring, owned by the player.
   * This replaces a file already playing on the channel.
   */

  memset(&w, 0, sizeof(w));
  w.size    = sizeof(w);
  w.type    = MEADOW_DAC_WAVE_STREAM;
  w.rate_hz = pl->rate;
  w.fill    = MEADOW_DAC_PLAYER_FILL;

  ret = meadow_dac_wave_start_owned(pinid, &w, pl->owner);
  if (ret < 0)
    {
      goto errout;
    }

  snprintf(arg, sizeof(arg), "%lx", (unsigned long)(uintptr_t)pl);
  argv[0] = arg;
  argv[1] = NULL;

  ret = kthread_create(DAC_PLAYER_THREAD_NAME, DAC_PLAYER_THREAD_PRIORITY,
                       DAC_PLAYER_THREAD_STACKSIZE,
                       meadow_dac_player_thread, argv);
  if (ret < 0)
    {
      meadow_dac_stream_stop(pinid, pl->owner);
      goto errout;
    }

  return OK;

errout:
  file_close(&pl->file);
  kmm_free(pl);
  return ret;
}
