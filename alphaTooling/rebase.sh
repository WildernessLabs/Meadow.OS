#!/bin/bash

# Accept our deletions for DU conflicts in arch and boards
git status --short | grep "^DU" | grep -E "nuttx/(arch|boards)/" | awk '{print $2}' | xargs git rm

# cd arch/arm/src/
# git rm -rf -- armv7-a armv7-r armv8-m cxd56xx goldfish imx9 lc823450 max326xx mcx-nxxx mps qemu ra4 rp2040 rp23xx sam34 stm32f0l0g0 stm32h5 stm32l4 tiva xmc4
# cd ../include
# git rm -rf -- cxd32xx imxrt max326xx rp23xx stm32h5 mcx-nxxx samv7 armv7-m goldfish mps stm32f0l0g0 at32 csk6 imx9 ra4
# cd ../..
# cd boards/arm/
# git rm -rf -- cxd56xx mcx-nxxx nrf52 qemu rp2040 sama5 samv7 stm32f0l0g0 stm32h5 stm32l4 imx9 mps nrf53 ra4 rp23xx  samd2l2 stm32h7 tiva
# cd ../
# git rm -rf -- arm64 avr risc-v sim x86_64 xtensa
# cd ..cd


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
#     Added: CFLAGS += -I board/F7MeadowOSLink  (Meadow header path)
#     Added: $(shell [ -L F7MeadowOSLink ] || ln -sfn board/F7MeadowOSLink F7MeadowOSLink)  (object output dirs)
#     Removed: include $(TOPDIR)/Make.defs  (now included by parent common/Makefile)
#     Removed: include $(TOPDIR)/boards/Board.mk  (same reason)
# - nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/Makefile
#     Fixed: libyaml//writer.c -> libyaml/writer.c  (double-slash typo)
