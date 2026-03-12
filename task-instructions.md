# NuttX Overview

This project aims to get NuttX running on the Meadow hardware platform.  The code is split into two parts:

- Bootloader
- NuttX RTOS

NuttX communicates with a host computer over USB.

## Tech Stack

### Bootloader

This is a STM32CubeIDE project using the STM32 HAL.

### NuttX

This is a makefile project for the STM32 and it is written in C.

### Debugging

Both the bootloader and the NuttX RTOS are debugged using `openocd` and `gdb`.  The `.gdbinit` file for the debugging sessions can be found in the `gdb` directory in the root of the project workspace.

## Project Structure

### Bootloader

- Source code is in the `bootloader` directory in the root of the project workspace.
- This is built using `makefile`

### NuttX

The NuttX system is split into three areas:

- Applications that will run on NuttX, these are in the `apps` directory
- Core operating system components in the `nuttx` directory
- Hardware specific operating system files in the `Meadow.OS` directory and the `stm32f777zit6-meadow` directory

The RTOS is a protected mode code that starts the `hcom` application.  `hcom` runs in user space.

## Resources

- build.sh
  This builds the application using the various `makefiles`.  It also concatenates the various binary files to project the `Meadow.OS.bin` file.
- Linker
  The directory `/nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/scripts` directory contains the linker scripts for creating the `nuttx` binary.
- flash.sh
  This file writes the `Meaodw.OS.bin` file to the flash memory on the STM32.
- debug.sh
  This starts `openocd` when run with the `--server` command line argument.  Running this script with no arguments starts `gdb`
- gdb/.gdbinit
  This defines some gdb macros and executes some commands loading the various symbol files and setting up `gdb` ready for debugging through the command line.

## The Problem

### Without Bootloader

When the application is built without the bootloader operating system starts as expected and it can be debugged.  The application starts `hcom` as expected and the hardware responds as designed.  This is the way the system works in the last commit of the `main` branch.

In this mode `gdb` starts with the program counter (pc) set to 0x08040238.

Running the code with the gdb `continue` command causes the board to run as expected.

### With Bootloader

The files in the current branch are designed to move NuttX and `hcom` and to setup the system to allow the use of the bootloader.

In this mode `gdb` starts with the program counter (pc) set to 0x0804023c.

Running the code with the gdb `continue` command causes the board to lock.

## The Task

Analyse the current changes and advise why the code runs as expected without the bootloader but locks if the bootloader is present.