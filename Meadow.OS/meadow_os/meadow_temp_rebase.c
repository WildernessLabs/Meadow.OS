/****************************************************************************
 * Meadow.OS/Meadow.OS/meadow_temp_rebase.c
 *   Copyright (C) 2024 Wilderness Labs. All rights reserved.
 *   Author:  Wilderness Labs
*/
// This file is temporary while bringing up HCOM on Nuttx v12. It contains
// things required to get HCOM working. At this time we are attempting to
// not add modify Nuttx itself. Other Meadow features maybe included
// here as stubs, proxies or actual code, whatever is needed to get HCOM
// running.
//
// Please add comments to all additions, so they can be understood at a
// future time
//

#include <nuttx/config.h>

#include <debug.h>
#include <assert.h>

#include <meadow/meadow_temp_rebase.h>
#include <arm_internal.h>
#include "hardware/stm32_qspi.h"

// [MAKE-IT-BUILD]
//==========================================================================
// The function net_lockedwait() has been removed from Nuttx between 7.9 and 12. 
// It is now only used in espcp_common.c
// The Legacy Meadow version was implemented in Meadow/nuttx/net/utils/net_lock.c
// Started to reimplement but began to look like a rabbit hole.
//==========================================================================

int net_lockedwait(sem_t *sem)
{
  return false;
}
