/****************************************************************************
 * /Meadow.OS/include/meadow/hcom_nx_fatshim.h
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

#ifndef __INCLUDE_MEADOWOS_HCOM_NX_FATSHIM_H
#define __INCLUDE_MEADOWOS_HCOM_NX_FATSHIM_H

#include <nuttx/config.h>

//---------------------------------------------------------------------------
// A read-only FAT16 view of the LittleFS volume on /meadow0, presented as a
// block device so that USB mass storage can hand it to a host computer.
//
// No host operating system can read LittleFS, and USB mass storage is a block
// protocol in which the host runs the file system driver. The two facts
// together mean the only way for a host to browse /meadow0 is for the board to
// fabricate a file system the host already understands. That is what this
// does: it never stores FAT anywhere, it answers each 512 byte sector read by
// computing what a FAT16 volume containing those files would have had at that
// offset.
//
// The mapping is built once, when the first sector is read, by walking
// /meadow0 and assigning each file a contiguous run of clusters. Boot sector,
// FAT sectors and directory sectors are then synthesized from that map on
// demand, and reads that land in a file's cluster run are satisfied by reading
// the file itself.
//
// Read-only. Writes are refused, and the geometry reports the medium as
// write protected.

//---------------------------------------------------------------------------
// Registers the block device. Safe to call before /meadow0 is mounted: the
// geometry is fixed by configuration rather than by the contents of the
// volume, so the mass storage LUN can be bound early in the boot while the
// file system is still coming up. The directory scan is deferred until the
// first read arrives, which cannot happen before a host has enumerated the
// device.
//
// devpath - block device to register, for example "/dev/meadowmsc0"
//
// Returns OK, or a negated errno.
int hcom_nx_fatshim_initialize(const char *devpath);

#endif  // __INCLUDE_MEADOWOS_HCOM_NX_FATSHIM_H
