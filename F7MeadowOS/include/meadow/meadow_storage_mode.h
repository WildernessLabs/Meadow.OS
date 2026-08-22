/****************************************************************************
 * /Meadow.OS/include/meadow/meadow_storage_mode.h
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

#ifndef __INCLUDE_MEADOWOS_MEADOW_STORAGE_MODE_H
#define __INCLUDE_MEADOWOS_MEADOW_STORAGE_MODE_H

#include <nuttx/config.h>

#include <stdbool.h>

//---------------------------------------------------------------------------
// Storage mode is the state in which the Meadow file system may be handed to
// a host computer over USB mass storage.
//
// It is not a mode of its own with its own command set. It is derived from
// whether the .NET runtime is enabled, because that is already the only
// condition under which the runtime is guaranteed not to be writing to
// /meadow0:
//
//   runtime disabled  ->  storage mode active
//   runtime enabled   ->  storage mode inactive
//
// The runtime enable state lives in the battery backed register bit
// HCOM_BBREG_USER_RQST_MONO_ENABLE_BIT, and the existing host commands
// HCOM_MDOW_REQUEST_MONO_DISABLE and HCOM_MDOW_REQUEST_MONO_ENABLE already
// set or clear it and then restart the board. So entering and leaving storage
// mode needs no new protocol: 'meadow mono disable' enters it and
// 'meadow mono enable' leaves it, each by way of the restart those commands
// already perform.
//
// Deriving the mode this way is what makes the concurrency problem go away.
// A host cannot safely share a medium with the board, and rather than trying
// to quiesce a running runtime, storage mode simply never coexists with one.

//---------------------------------------------------------------------------
// Reports whether this boot is running in storage mode.
//
// The value is latched on the first call and is then fixed for the lifetime
// of the boot. That is deliberate, and it is required for correctness rather
// than being an optimization: hcom_mono_ctrl_disable_mono() sets the battery
// backed bit and only afterwards restarts the board, so between those two
// points the bit says "disabled" while the runtime is still running. Reading
// the bit live would briefly report storage mode while Mono still held the
// file system. Latching early, before the runtime can be started, reports
// what is true for this boot instead of what is pending for the next one.
bool meadow_storage_mode_active(void);

//---------------------------------------------------------------------------
// Reports whether board-side writes to a path are allowed right now.
//
// In storage mode a host computer may have the Meadow file system mounted, and
// the view it was given is a snapshot: the mapping from FAT clusters to files
// was computed once, when the host first read from the volume. A board-side
// write afterwards does not corrupt /meadow0, since the host's view is
// read-only, but it does invalidate that mapping, so the host would go on
// reading from cluster runs that no longer describe the files. USB mass storage
// has no way to tell an already-mounted host that anything changed.
//
// Freezing the file system for the duration is therefore the point, not a side
// effect. It is also what you want when the reason for entering storage mode
// was to copy the log off in the first place.
//
// Log lines produced while writes are blocked are not lost outright; they still
// reach syslog, they just do not reach the file.
//
// path - the file about to be written, so that only the Meadow file system is
//        affected
bool meadow_storage_mode_writes_permitted(const char *path);

//---------------------------------------------------------------------------
// Whether a host computer is actually using the exposed file system.
//
// This is narrower than storage mode, and the distinction matters. Storage mode
// is simply the runtime being disabled, which is also how an app is stopped
// before a new one is deployed. Refusing writes for the whole of storage mode
// would therefore break deployment, whereas what actually needs protecting is
// the window in which a host holds a view of the volume.
//
// It becomes true when the synthesized view is first built, which only happens
// when a host reads from the drive, and stays true for the rest of the boot.
// Erring that way is deliberate: a host that has read once may read again at
// any time, and there is no notification when it stops.
bool meadow_storage_mode_host_engaged(void);

//---------------------------------------------------------------------------
// Called by the block driver when a host first reads the exposed volume.
void meadow_storage_mode_set_host_engaged(void);

#endif  // __INCLUDE_MEADOWOS_MEADOW_STORAGE_MODE_H
