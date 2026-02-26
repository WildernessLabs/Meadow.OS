#!/usr/bin/env python3
'''
Prepare the NuttX mainline directory for comparison with the Wilderness Labs copy of NuttX.
'''
    
import argparse
import os
import shutil
import sys

def parse_args():
    '''
    Parse command-line arguments for the script.
    Returns:
        argparse.Namespace: Parsed arguments.
    '''
    parser = argparse.ArgumentParser(description='Prepare NuttX mainline directory for comparison.')
    parser.add_argument('--mainline', required=True, help='Path to the NuttX mainline directory')
    parser.add_argument('--dryrun', action='store_true', help='Print files to be deleted without deleting')
    #
    # Show help if no arguments are provided
    #
    if len(sys.argv) == 1:
        parser.print_help(sys.stderr)
        sys.exit(1)
    return parser.parse_args()

#
# Combined list of files/dirs to delete from both arch and boards directories.
#
# Add new files and directories to this list as needed. This is a curated list based on 
# the differences between the mainline and Wilderness Labs copies of NuttX.
#
FILES_TO_DELETE = [
    # Architecture files/dirs
    'arch/arm64',
    'arch/avr',
    'arch/ceva',
    'arch/hc',
    'arch/mips',
    'arch/misoc',
    'arch/or1k',
    'arch/renesas',
    'arch/risc-v',
    'arch/sim',
    'arch/sparc',
    'arch/tricore',
    'arch/x86',
    'arch/x86_64',
    'arch/z16',
    'arch/z80',
    #
    #  ARM specific boards.
    #
    'arch/arm/include/a1x',
    'arch/arm/include/am335x',
    'arch/arm/include/armv6-m',
    'arch/arm/include/armv7-a',
    'arch/arm/include/armv7-r',
    'arch/arm/include/armv8-m',
    'arch/arm/include/armv8-r',
    'arch/arm/include/c5471',
    'arch/arm/include/cxd56xx',
    'arch/arm/include/dm320',
    'arch/arm/include/efm32',
    'arch/arm/include/eoss3',
    'arch/arm/include/fvp-v8r-aarch32',
    'arch/arm/include/gd32f4',
    'arch/arm/include/imx1',
    'arch/arm/include/imx6',
    'arch/arm/include/imxrt',
    'arch/arm/include/kinetis',
    'arch/arm/include/kl',
    'arch/arm/include/lc823450',
    'arch/arm/include/lpc17xx_40xx',
    'arch/arm/include/lpc214x',
    'arch/arm/include/lpc2378',
    'arch/arm/include/lpc31xx',
    'arch/arm/include/lpc43xx',
    'arch/arm/include/lpc54xx',
    'arch/arm/include/max326xx',
    'arch/arm/include/moxart',
    'arch/arm/include/mx8mp',
    'arch/arm/include/nrf52',
    'arch/arm/include/nrf53',
    'arch/arm/include/nrf91',
    'arch/arm/include/nuc1xx',
    'arch/arm/include/phy62xx',
    'arch/arm/include/qemu',
    'arch/arm/include/rp2040',
    'arch/arm/include/s32k1xx',
    'arch/arm/include/s32k3xx',
    'arch/arm/include/sam34',
    'arch/arm/include/sama5',
    'arch/arm/include/samd2l2',
    'arch/arm/include/samd5e5',
    'arch/arm/include/samv7',
    'arch/arm/include/stm32f0l0g0',
    'arch/arm/include/stm32h7',
    'arch/arm/include/stm32l4',
    'arch/arm/include/stm32l5',
    'arch/arm/include/stm32u5',
    'arch/arm/include/stm32wb',
    'arch/arm/include/stm32wl5',
    'arch/arm/include/str71x',
    'arch/arm/include/tiva',
    'arch/arm/include/tlsr82',
    'arch/arm/include/tms570',
    'arch/arm/include/xmc4',
    'arch/arm/include/at32',
    'arch/arm/include/csk6',
    'arch/arm/include/cxd32xx',
    'arch/arm/include/goldfish',
    'arch/arm/include/imx9',
    'arch/arm/include/mcx-nxxx',
    'arch/arm/include/mps',
    'arch/arm/include/ra4',
    'arch/arm/include/rp23xx',
    'arch/arm/include/stm32h5',
    'arch/arm/src/a1x',
    'arch/arm/src/am335x',
    'arch/arm/src/armv6-m',
    'arch/arm/src/armv7-a',
    'arch/arm/src/armv7-r',
    'arch/arm/src/armv8-m',
    'arch/arm/src/armv8-r',
    'arch/arm/src/c5471',
    'arch/arm/src/cxd56xx',
    'arch/arm/src/dm320',
    'arch/arm/src/efm32',
    'arch/arm/src/eoss3',
    'arch/arm/src/fvp-v8r-aarch32',
    'arch/arm/src/gd32f4',
    'arch/arm/src/imx1',
    'arch/arm/src/imx6',
    'arch/arm/src/imxrt',
    'arch/arm/src/kinetis',
    'arch/arm/src/kl',
    'arch/arm/src/lc823450',
    'arch/arm/src/lpc17xx_40xx',
    'arch/arm/src/lpc214x',
    'arch/arm/src/lpc2378',
    'arch/arm/src/lpc31xx',
    'arch/arm/src/lpc43xx',
    'arch/arm/src/lpc54xx',
    'arch/arm/src/max326xx',
    'arch/arm/src/moxart',
    'arch/arm/src/mx8mp',
    'arch/arm/src/nrf52',
    'arch/arm/src/nrf53',
    'arch/arm/src/nrf91',
    'arch/arm/src/nuc1xx',
    'arch/arm/src/phy62xx',
    'arch/arm/src/qemu',
    'arch/arm/src/rp2040',
    'arch/arm/src/s32k1xx',
    'arch/arm/src/s32k3xx',
    'arch/arm/src/sam34',
    'arch/arm/src/sama5',
    'arch/arm/src/samd2l2',
    'arch/arm/src/samd5e5',
    'arch/arm/src/samv7',
    'arch/arm/src/stm32f0l0g0',
    'arch/arm/src/stm32h7',
    'arch/arm/src/stm32l4',
    'arch/arm/src/stm32l5',
    'arch/arm/src/stm32u5',
    'arch/arm/src/stm32wb',
    'arch/arm/src/stm32wl5',
    'arch/arm/src/str71x',
    'arch/arm/src/tiva',
    'arch/arm/src/tlsr82',
    'arch/arm/src/tms570',
    'arch/arm/src/xmc4',
    'arch/arm/src/at32',
    'arch/arm/src/csk6',
    'arch/arm/src/cxd32xx',
    'arch/arm/src/goldfish',
    'arch/arm/src/imx9',
    'arch/arm/src/mcx-nxxx',
    'arch/arm/src/mps',
    'arch/arm/src/ra4',
    'arch/arm/src/rp23xx',
    'arch/arm/src/rtl8720c',
    'arch/arm/src/stm32h5',
    #
    # Board files and directories.
    #
    'boards/arm64',
    'boards/avr',
    'boards/hc',
    'boards/mips',
    'boards/misoc',
    'boards/or1k',
    'boards/renesas',
    'boards/risc-v',
    'boards/sim',
    'boards/sparc',
    'boards/tricore',
    'boards/x86',
    'boards/x86_64',
    'boards/z16',
    'boards/z80',
    'boards/arm/a1x',
    'boards/arm/am335x',
    'boards/arm/at32',
    'boards/arm/c5471',
    'boards/arm/csk6',
    'boards/arm/cxd56xx',
    'boards/arm/dm320',
    'boards/arm/efm32',
    'boards/arm/eoss3',
    'boards/arm/fvp-v8r-aarch32',
    'boards/arm/gd32f4',
    'boards/arm/imx6',
    'boards/arm/imx9',
    'boards/arm/imxrt',
    'boards/arm/kinetis',
    'boards/arm/kl',
    'boards/arm/lc823450',
    'boards/arm/lpc17xx_40xx',
    'boards/arm/lpc214x',
    'boards/arm/lpc2378',
    'boards/arm/lpc31xx',
    'boards/arm/lpc43xx',
    'boards/arm/lpc54xx',
    'boards/arm/max326xx',
    'boards/arm/mcx-nxxx',
    'boards/arm/moxart',
    'boards/arm/mps',
    'boards/arm/mx8mp',
    'boards/arm/nrf52',
    'boards/arm/nrf53',
    'boards/arm/nrf91',
    'boards/arm/nuc1xx',
    'boards/arm/phy62xx',
    'boards/arm/qemu',
    'boards/arm/ra4',
    'boards/arm/rp2040',
    'boards/arm/rp23xx',
    'boards/arm/s32k1xx',
    'boards/arm/s32k3xx',
    'boards/arm/sam34',
    'boards/arm/sama5',
    'boards/arm/samd2l2',
    'boards/arm/samd5e5',
    'boards/arm/samv7',
    'boards/arm/stm32f0l0g0',
    'boards/arm/stm32h5',
    'boards/arm/stm32h7',
    'boards/arm/stm32l4',
    'boards/arm/stm32l5',
    'boards/arm/stm32u5',
    'boards/arm/stm32wb',
    'boards/arm/stm32wl5',
    'boards/arm/str71x',
    'boards/arm/tiva',
    'boards/arm/tlsr82',
    'boards/arm/tms570',
    'boards/arm/xmc4',
    #
    # Now for the STM32 boards.
    #
    'boards/arm/stm32/axoloti',
    'boards/arm/stm32/b-g431b-esc1',
    'boards/arm/stm32/b-g474e-dpow1',
    'boards/arm/stm32/clicker2-stm32',
    'boards/arm/stm32/cloudctrl',
    'boards/arm/stm32/emw3162',
    'boards/arm/stm32/et-stm32-stamp',
    'boards/arm/stm32/fire-stm32v2',
    'boards/arm/stm32/hymini-stm32v',
    'boards/arm/stm32/maple',
    'boards/arm/stm32/mikroe-stm32f4',
    'boards/arm/stm32/nucleo-f103rb',
    'boards/arm/stm32/nucleo-f207zg',
    'boards/arm/stm32/nucleo-f302r8',
    'boards/arm/stm32/nucleo-f303re',
    'boards/arm/stm32/nucleo-f303ze',
    'boards/arm/stm32/nucleo-f334r8',
    'boards/arm/stm32/nucleo-f401re',
    'boards/arm/stm32/nucleo-f410rb',
    'boards/arm/stm32/nucleo-f411re',
    'boards/arm/stm32/nucleo-f412zg',
    'boards/arm/stm32/nucleo-f429zi',
    'boards/arm/stm32/nucleo-f446re',
    'boards/arm/stm32/nucleo-g431kb',
    'boards/arm/stm32/nucleo-g431rb',
    'boards/arm/stm32/nucleo-g474re',
    'boards/arm/stm32/nucleo-l152re',
    'boards/arm/stm32/odrive36',
    'boards/arm/stm32/olimex-stm32-e407',
    'boards/arm/stm32/olimex-stm32-h405',
    'boards/arm/stm32/olimex-stm32-h407',
    'boards/arm/stm32/olimex-stm32-p107',
    'boards/arm/stm32/olimex-stm32-p207',
    'boards/arm/stm32/olimex-stm32-p407',
    'boards/arm/stm32/olimexino-stm32',
    'boards/arm/stm32/omnibusf4',
    'boards/arm/stm32/photon',
    'boards/arm/stm32/shenzhou',
    'boards/arm/stm32/stm32_tiny',
    'boards/arm/stm32/stm3210e-eval',
    'boards/arm/stm32/stm3220g-eval',
    'boards/arm/stm32/stm3240g-eval',
    'boards/arm/stm32/stm32butterfly2',
    'boards/arm/stm32/stm32f103-minimum',
    'boards/arm/stm32/stm32f334-disco',
    'boards/arm/stm32/stm32f3discovery',
    'boards/arm/stm32/stm32f401rc-rs485',
    'boards/arm/stm32/stm32f411-minimum',
    'boards/arm/stm32/stm32f411e-disco',
    'boards/arm/stm32/stm32f429i-disco',
    'boards/arm/stm32/stm32f4discovery',
    'boards/arm/stm32/stm32ldiscovery',
    'boards/arm/stm32/stm32vldiscovery',
    'boards/arm/stm32/viewtool-stm32f107',
    'boards/arm/stm32f7/nucleo-f722ze',
    'boards/arm/stm32f7/nucleo-f746zg',
    'boards/arm/stm32f7/nucleo-f767zi',
    'boards/arm/stm32f7/steval-eth001v1',
    'boards/arm/stm32f7/stm32f746-ws',
    'boards/arm/stm32f7/stm32f746g-disco',
    'boards/arm/stm32f7/stm32f769i-disco',
    #
    # Now for the xtensa boards.
    #
    'boards/xtensa/esp32/esp32-2432S028',
    'boards/xtensa/esp32/esp32-audio-kit',
    'boards/xtensa/esp32/esp32-devkitc',
    'boards/xtensa/esp32/esp32-ethernet-kit',
    'boards/xtensa/esp32/esp32-lyrat',
    'boards/xtensa/esp32/esp32-pico-kit',
    'boards/xtensa/esp32/esp32-sparrow-kit',
    'boards/xtensa/esp32/esp32-wrover-kit',
    'boards/xtensa/esp32/heltec_wifi_lora32',
    'boards/xtensa/esp32/lilygo_tbeam_lora_gps',
    'boards/xtensa/esp32/ttgo_eink5_v2',
    'boards/xtensa/esp32/ttgo_lora_esp32',
    'boards/xtensa/esp32/ttgo_t_display_esp32',
    'boards/xtensa/esp32s2/esp32s2-kaluga-1',
    'boards/xtensa/esp32s2/esp32s2-saola-1',
    'boards/xtensa/esp32s2/franzininho-wifi',
    'boards/xtensa/esp32s3/esp32s3-8048S043',
    'boards/xtensa/esp32s3/esp32s3-box',
    'boards/xtensa/esp32s3/esp32s3-devkit',
    'boards/xtensa/esp32s3/esp32s3-eye',
    'boards/xtensa/esp32s3/esp32s3-korvo-2',
    'boards/xtensa/esp32s3/esp32s3-lcd-ev',
    'boards/xtensa/esp32s3/esp32s3-lhcbit',
    'boards/xtensa/esp32s3/esp32s3-xiao',
    'boards/xtensa/esp32s3/lckfb-szpi-esp32s3',
]

def delete_files(base_dir, files, dryrun):
    '''
    Delete or print files/directories listed in files from the mainline directory.
    Args:
        base_dir (str): Path to the mainline directory.
        files (list): List of relative file/directory paths to delete.
        dryrun (bool): If True, only print what would be deleted.
    Returns:
        list: List of deleted (or would-be deleted) paths.
    '''
    deleted = []
    for f in files:
        path = os.path.join(base_dir, f)
        if os.path.exists(path):
            if dryrun:
                print(f'    Would delete: {path}')
            else:
                if os.path.isdir(path):
                    shutil.rmtree(path)
                else:
                    os.remove(path)
                print(f'    Deleted: {path}')
            deleted.append(path)
    return deleted

def list_extra_dirs(mainline_dir, wl_dir, subdir):
    '''
    Print directories present in mainline but not in the Wilderness Labs repo for a given subdir.
    Args:
        mainline_dir (str): Path to the mainline directory.
        wl_dir (str): Path to the Wilderness Labs directory.
        subdir (str): Subdirectory to compare (e.g., 'arch', 'boards').
    '''
    mainline_path = os.path.join(mainline_dir, subdir)
    wl_path = os.path.join(wl_dir, subdir)
    mainline_dirs = set(os.listdir(mainline_path)) if os.path.exists(mainline_path) else set()
    wl_dirs = set(os.listdir(wl_path)) if os.path.exists(wl_path) else set()
    extra = mainline_dirs - wl_dirs
    if extra:
        print(f'    Directories in mainline {subdir} but not in Wilderness Labs:')
        for d in sorted(extra):
            print(f'      {d}')

def main():
    '''
    Main entry point for the script. Handles argument parsing, file deletion, and directory comparison.
    '''
    print('Preparing NuttX mainline directory for comparison...')
    args = parse_args()
    mainline_dir = os.path.abspath(args.mainline)
    wl_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'nuttx'))
    print(f'    Mainline directory: {mainline_dir}')
    print(f'    Wilderness Labs directory: {wl_dir}')
    #
    # Delete files and directories that are in the mainline but not in Wilderness Labs
    #
    delete_files(mainline_dir, FILES_TO_DELETE, args.dryrun)
    #
    # List directories in mainline that are not in Wilderness Labs as these may need adding to the delete list.
    #
    if not args.dryrun:
        print(f'    Checking for extra directories in mainline that are not in Wilderness Labs')
        list_extra_dirs(mainline_dir, wl_dir, 'arch')
        list_extra_dirs(mainline_dir, wl_dir, 'arch/arm/include')
        list_extra_dirs(mainline_dir, wl_dir, 'arch/src/src')
        list_extra_dirs(mainline_dir, wl_dir, 'arch/xtensa/include')
        list_extra_dirs(mainline_dir, wl_dir, 'arch/xtensa/src')
        list_extra_dirs(mainline_dir, wl_dir, 'boards')
        list_extra_dirs(mainline_dir, wl_dir, 'boards/arm')
        list_extra_dirs(mainline_dir, wl_dir, 'boards/arm/STM32')
        list_extra_dirs(mainline_dir, wl_dir, 'boards/arm/STM32/f7')
        list_extra_dirs(mainline_dir, wl_dir, 'boards/xtensa')
        list_extra_dirs(mainline_dir, wl_dir, 'boards/xtensa/esp32')
        list_extra_dirs(mainline_dir, wl_dir, 'boards/xtensa/esp32s2')
        list_extra_dirs(mainline_dir, wl_dir, 'boards/xtensa/esp32s3')

if __name__ == '__main__':
    main()
