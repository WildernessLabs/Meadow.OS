#!/bin/bash

# Accept our deletions for DU conflicts in arch and boards
git status --short | grep "^DU" | grep -E "nuttx/(arch|boards)/" | awk '{print $2}' | xargs git rm

# Resolve UU conflicts in stm32f777zit6-meadow by keeping ours
git status --short | grep "^UU" | grep "nuttx/boards/arm/stm32f7/stm32f777zit" | awk '{print $2}' | xargs git checkout --ours
git status --short | grep "^UU" | grep "nuttx/boards/arm/stm32f7/stm32f777zit" | awk '{print $2}' | xargs git add

# Remove files added by them (UA) in stm32f777zit6-meadow
git status --short | grep "^UA" | grep "nuttx/boards/arm/stm32f7/stm32f777zit" | awk '{print $2}' | xargs git rm

# Accept our changes to socket.c
git checkout --ours nuttx/net/socket/socket.c
git add nuttx/net/socket/socket.c

# Restore our Config.mk (upstream removed ARCHIVE_ADD macro used by apps build)
git checkout HEAD -- nuttx/tools/Config.mk

# Accept all incoming changes to the tools and documentation
git checkout --theirs -- nuttx/tools
git add nuttx/tools
#
git checkout --theirs -- nuttx/Documentation
git add nuttx/Documentation

# NOTE: Build system changes required for the new upstream board build structure:
# - nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/Make.defs
#     Added: VPATH += :board  (so make finds board sources)
#     Added: CFLAGS += -I board/Meadow.OS  (Meadow header path)
#     Added: $(shell [ -L Meadow.OS ] || ln -sfn board/Meadow.OS Meadow.OS)  (object output dirs)
#     Removed: include $(TOPDIR)/Make.defs  (now included by parent common/Makefile)
#     Removed: include $(TOPDIR)/boards/Board.mk  (same reason)
# - nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/Makefile
#     Fixed: libyaml//writer.c -> libyaml/writer.c  (double-slash typo)
