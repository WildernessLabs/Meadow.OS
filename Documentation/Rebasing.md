# Rebasing

## Directory Structure

## Prepare the Directories

Mainline

git checkout releases/12.11
gpristine


releases/12.11 nuttx-12.11.0






git checkout -b merging12-11
gpristine

git remote add local-nuttx ../Mainline

## Merging

git subtree pull \
    --prefix=nuttx/ \
    local-nuttx \
    nuttx-12.11.0 \
    -m "Subtree merge NuttX 12.11 into nuttx"


PostSubtreeCommand.png

git status --short | grep "^DU" | grep -E "nuttx/(arch|boards)/" | awk '{print $2}' | xargs git rm

PostBoardArchitectureDeletion.png


## Quick Wins

git checkout --theirs nuttx/Documentation
git add nuttx/Documentation
git checkout --theirs nuttx/tools
git add nuttx/tools


## Anomalies

NuttxGitHub.png

RebaseFiles.png

ebase   merging12-11 | merge  git status --short | grep "^UA" | grep "nuttx/boards/arm/stm32f7/stm32f777zit"
 ✔  08:18:05
UA nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/hardware/stm32_fdcan.h
UA nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/rp2040_ads7046.c
UA nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/rp2040_tmp112.c
UA nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_fdcan.c
UA nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_fdcan_sock.c
UA nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_hx711.c
UA nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_uid.c
UA nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_uid.h
 Rebase   merging12-11 | merge  git status --short | grep "^UA" | grep "nuttx/boards/arm/stm32f7/stm32f777zit" | awk '{print $2}' | xargs git rm
 ✔  08:20:39
 
rm 'nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/hardware/stm32_fdcan.h'
rm 'nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/rp2040_ads7046.c'
rm 'nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/rp2040_tmp112.c'
rm 'nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_fdcan.c'
rm 'nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_fdcan_sock.c'
rm 'nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_hx711.c'
rm 'nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_uid.c'
rm 'nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_uid.h'


## kconfig

nuttx/arch/arm
nuttx/boards

Major differences.

## Manual Changes

## Build


Build of version 1.99.0.16254 (0abdfc3d:merging12-11) finished at 08:25:39

Getting device info...
Meadow by Wilderness Labs
Board Information 
    Model: F7Micro
    Hardware version: F7CoreComputeV2
    Device name: MeadowF7

Hardware Information 
    Processor type: STM32F777IIK6
    ID: 35-00-28-00-07-51-38-34-35-31-38-33
    Serial number: 336031743438
    Coprocessor type: ESP32

Firmware Versions 
    OS: 1.99.0.16254

    