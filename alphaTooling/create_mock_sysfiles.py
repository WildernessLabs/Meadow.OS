#!/usr/bin/env python

# This script contains code to create 2 files, Meadow.OS.Runtime.bin.fake an
# and Meadow.OS.Update.bin.fake. These files will full the first 2 segments
# of Meadow's external flash and there pattern will be used to verify the
# boundaries.
import sys

destFilePath = "/mnt/c/HcomFiles/Write/Text/fake/"

runtimeFileName = destFilePath + "Meadow.OS.Runtime.bin.fake"
# This file needs to be 0x00300000 bytes long
runTimeSize = 0x00300000
runtimeIntro = "This is Region 1 used for the Meadow Runtime file, filled with A."

f_runtimeFile = open(runtimeFileName, "w")
f_runtimeFile.write(runtimeIntro)

# Start after intro and fill the rest of the file
for offset in range(len(runtimeIntro), runTimeSize, 1):
    f_runtimeFile.write('A')

f_runtimeFile.close

updateFileName = destFilePath + "Meadow.OS.Update.bin.fake"
# This file needs to be 0x001C0000 bytes
updateSize = 0x001C0000
updateIntro = "This is Region 2 used for the Meadow Update file, filled with B."

# Open the command file and read it at one time to solve the readline problem
# with detecting EOF
f_updateFile = open(updateFileName, "w")
f_updateFile.write(updateIntro)

# Start after intro and fill the rest of the file
for offset in range(len(updateIntro), updateSize):
    f_updateFile.write('B')

f_updateFile.close
