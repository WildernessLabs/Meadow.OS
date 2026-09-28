/****************************************************************************
 * boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_fat_dma.c
 *
 *   Copyright (C) 2026 Wilderness Labs. All rights reserved.
 *   Author: Wilderness Labs
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

/* DMA-capable buffers for the FAT file system (CONFIG_FAT_DMAMEMORY).
 *
 * FAT reads and writes the SD card through two kinds of sector buffer:
 * one per mounted volume and one per open file. The SDMMC driver transfers
 * them by DMA in 16-byte bursts, and stm32_dmacapable() rejects a buffer
 * that crosses a 1 KB boundary unless it is burst aligned. The heap only
 * guarantees 8-byte alignment (CONFIG_MM_DEFAULT_ALIGNMENT), so with a
 * plain allocation, whether the SD card mounts depends on where the volume
 * buffer happens to land, which changes from build to build (Issue #653).
 *
 * Aligning to 32 bytes meets the burst rule and also keeps each buffer on
 * its own Cortex-M7 data cache lines.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <nuttx/kmalloc.h>
#include <nuttx/fs/fat.h>

#ifdef CONFIG_FAT_DMAMEMORY

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MEADOW_FAT_DMA_ALIGNMENT  32  /* Cortex-M7 data cache line size */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

FAR void *fat_dma_alloc(size_t size)
{
  return kmm_memalign(MEADOW_FAT_DMA_ALIGNMENT, size);
}

void fat_dma_free(FAR void *memory, size_t size)
{
  UNUSED(size);
  kmm_free(memory);
}

#endif /* CONFIG_FAT_DMAMEMORY */
