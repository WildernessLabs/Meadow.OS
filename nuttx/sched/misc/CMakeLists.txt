# ##############################################################################
# sched/misc/CMakeLists.txt
<<<<<<< HEAD
=======
#
# SPDX-License-Identifier: Apache-2.0
>>>>>>> 5d8cdeaea847bcaf0b9145dd922e0a83045067ca
#
# Licensed to the Apache Software Foundation (ASF) under one or more contributor
# license agreements.  See the NOTICE file distributed with this work for
# additional information regarding copyright ownership.  The ASF licenses this
# file to you under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License.  You may obtain a copy of
# the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations under
# the License.
#
# ##############################################################################

set(SRCS assert.c panic_notifier.c reboot_notifier.c)

if(CONFIG_ARCH_DEADLOCKDUMP)
  list(APPEND SRCS deadlock.c)
endif()

if(CONFIG_BOARD_COREDUMP_SYSLOG OR CONFIG_BOARD_COREDUMP_BLKDEV)
  list(APPEND SRCS coredump.c)
endif()

target_sources(sched PRIVATE ${SRCS})
