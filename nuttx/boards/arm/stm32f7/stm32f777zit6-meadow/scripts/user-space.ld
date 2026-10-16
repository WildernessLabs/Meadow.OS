/****************************************************************************
 * boards/arm/stm32f7/stm32f777zit6-meadow/scripts/user-space.ld
 *
 * SPDX-License-Identifier: Apache-2.0
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

/* NOTE:  This depends on the memory.ld script having been included prior to
 * this script.
 */

OUTPUT_ARCH(arm)
ENTRY(_stext)

PHDRS
{
  text PT_LOAD;
  data PT_LOAD;
  comment PT_LOAD;
  mono_text PT_LOAD;
  mono_data PT_LOAD;
}

SECTIONS
{
    /* Mono runtime image, linked to the start of SDRAM.
     *
     * The runtime archives built by build-mono.sh are placed here instead of
     * in uflash, together with System.Native and the TLS shim from
     * apps/system/mono (mono_mbedtls.c, which holds the root CA table, and
     * pal_ssl_mbedtls.c).  The rest of the host code (mono_main.c and its
     * P/Invoke tables) stays in uflash: it runs before the image has been
     * copied into SDRAM.  build.sh extracts this region of nuttx_user.bin into
     * Meadow.OS.Runtime.bin; hcom flashes that to the raw QSPI partition and
     * the HCOM_NX_UPD_COPY_RUNTIME_TO_RAM ioctl copies it back to
     * 0xC0000000 before the runtime starts.  The user heap begins after the
     * region (CONFIG_HEAP2_BASE), and the sdram region in memory.ld limits
     * the image to HCOM_NX_FS_MONO_RAW_PARTITION_SIZE (build.sh checks that
     * the three agree).
     *
     * These output sections come before .text and friends on purpose: the
     * linker gives each input section to the first pattern that matches it,
     * so the uflash sections below need no EXCLUDE_FILE lists.
     *
     * .mono_signature must stay first: the configuration manager reads it
     * from the first block of the runtime partition.
     */

    /DISCARD/ : {
        /* Unwind tables for the runtime archives.  .ARM.exidx entries are
         * 31-bit offsets (R_ARM_PREL31) and cannot reach from uflash to
         * SDRAM. */
        *libmonosgen-2.0.a:*(.ARM.exidx .ARM.exidx.* .ARM.extab .ARM.extab.*)
        *libmono-component-marshal-ilgen-static.a:*(.ARM.exidx .ARM.exidx.* .ARM.extab .ARM.extab.*)
        *libmono-component-hot_reload-stub-static.a:*(.ARM.exidx .ARM.exidx.* .ARM.extab .ARM.extab.*)
        *libmono-component-debugger-stub-static.a:*(.ARM.exidx .ARM.exidx.* .ARM.extab .ARM.extab.*)
        *libmono-component-diagnostics_tracing-stub-static.a:*(.ARM.exidx .ARM.exidx.* .ARM.extab .ARM.extab.*)
        *libminipal.a:*(.ARM.exidx .ARM.exidx.* .ARM.extab .ARM.extab.*)
        *libaotminipal.a:*(.ARM.exidx .ARM.exidx.* .ARM.extab .ARM.extab.*)
        *libsgen_objects.a:*(.ARM.exidx .ARM.exidx.* .ARM.extab .ARM.extab.*)
        *libSystem.Native.a:*(.ARM.exidx .ARM.exidx.* .ARM.extab .ARM.extab.*)
        *libapps.a:mono_mbedtls.c.*(.ARM.exidx .ARM.exidx.* .ARM.extab .ARM.extab.*)
        *libapps.a:pal_ssl_mbedtls.c.*(.ARM.exidx .ARM.exidx.* .ARM.extab .ARM.extab.*)
    }

    .mono_signature : {
        _s_mono_signature = ABSOLUTE(.);
        FILL(0xDEADC0DE)
        BYTE(0xAA)
        BYTE(0xBB)
        BYTE(0xCC)
        BYTE(0xDD)
        /* Meadow OS version */
        LONG(###VERSION_BUILD###)
        LONG(###VERSION_REVISION###)
        LONG(###VERSION_MINOR###)
        LONG(###VERSION_MAJOR###)
        BYTE(###BUILD_DAY###)
        BYTE(###BUILD_MONTH###)
        BYTE(###BUILD_YEAR###)
        BYTE(###BUILD_HOUR###)
        BYTE(###BUILD_MINUTE###)
        BYTE(###BUILD_SECOND###)
        LONG(###BUILD_HASH###)
        ###MONO_GIT_REF###
        _e_mono_signature = ABSOLUTE(.);
    } > sdram :mono_text

    .mono : {
        _s_mono = ABSOLUTE(.);
        *libmonosgen-2.0.a:*(.text .text.*)
        *libmono-component-marshal-ilgen-static.a:*(.text .text.*)
        *libmono-component-hot_reload-stub-static.a:*(.text .text.*)
        *libmono-component-debugger-stub-static.a:*(.text .text.*)
        *libmono-component-diagnostics_tracing-stub-static.a:*(.text .text.*)
        *libminipal.a:*(.text .text.*)
        *libaotminipal.a:*(.text .text.*)
        *libsgen_objects.a:*(.text .text.*)
        *libSystem.Native.a:*(.text .text.*)
        *libapps.a:mono_mbedtls.c.*(.text .text.*)
        *libapps.a:pal_ssl_mbedtls.c.*(.text .text.*)
        _e_mono = ABSOLUTE(.);
    } > sdram :mono_text

    .mono_data : {
        . = ALIGN(8);
        _s_mono_data = ABSOLUTE(.);
        *libmonosgen-2.0.a:*(.rodata .rodata.* .data .data.* .gnu.linkonce.d.*)
        *libmono-component-marshal-ilgen-static.a:*(.rodata .rodata.* .data .data.* .gnu.linkonce.d.*)
        *libmono-component-hot_reload-stub-static.a:*(.rodata .rodata.* .data .data.* .gnu.linkonce.d.*)
        *libmono-component-debugger-stub-static.a:*(.rodata .rodata.* .data .data.* .gnu.linkonce.d.*)
        *libmono-component-diagnostics_tracing-stub-static.a:*(.rodata .rodata.* .data .data.* .gnu.linkonce.d.*)
        *libminipal.a:*(.rodata .rodata.* .data .data.* .gnu.linkonce.d.*)
        *libaotminipal.a:*(.rodata .rodata.* .data .data.* .gnu.linkonce.d.*)
        *libsgen_objects.a:*(.rodata .rodata.* .data .data.* .gnu.linkonce.d.*)
        *libSystem.Native.a:*(.rodata .rodata.* .data .data.* .gnu.linkonce.d.*)
        *libapps.a:mono_mbedtls.c.*(.rodata .rodata.* .data .data.* .gnu.linkonce.d.*)
        *libapps.a:pal_ssl_mbedtls.c.*(.rodata .rodata.* .data .data.* .gnu.linkonce.d.*)
        _e_mono_data = ABSOLUTE(.);
    } > sdram :mono_data

    /* Not loaded: the runtime host zeroes it before starting the runtime,
     * because a warm reset leaves stale pointers in SDRAM. */

    .mono_bss (NOLOAD) : {
        . = ALIGN(8);
        _s_mono_bss = ABSOLUTE(.);
        *libmonosgen-2.0.a:*(.bss .bss.* .gnu.linkonce.b.* COMMON)
        *libmono-component-marshal-ilgen-static.a:*(.bss .bss.* .gnu.linkonce.b.* COMMON)
        *libmono-component-hot_reload-stub-static.a:*(.bss .bss.* .gnu.linkonce.b.* COMMON)
        *libmono-component-debugger-stub-static.a:*(.bss .bss.* .gnu.linkonce.b.* COMMON)
        *libmono-component-diagnostics_tracing-stub-static.a:*(.bss .bss.* .gnu.linkonce.b.* COMMON)
        *libminipal.a:*(.bss .bss.* .gnu.linkonce.b.* COMMON)
        *libaotminipal.a:*(.bss .bss.* .gnu.linkonce.b.* COMMON)
        *libsgen_objects.a:*(.bss .bss.* .gnu.linkonce.b.* COMMON)
        *libSystem.Native.a:*(.bss .bss.* .gnu.linkonce.b.* COMMON)
        *libapps.a:mono_mbedtls.c.*(.bss .bss.* .gnu.linkonce.b.* COMMON)
        *libapps.a:pal_ssl_mbedtls.c.*(.bss .bss.* .gnu.linkonce.b.* COMMON)
        . = ALIGN(8);
        _e_mono_bss = ABSOLUTE(.);
    } > sdram :NONE

    .userspace : {
        _stext = ABSOLUTE(.);
        . += 512;
        _suserspace = ABSOLUTE(.);
        /* KEEP is needed in case --gc-sections is being used */
        KEEP(*(.userspace))
        _euserspace = ABSOLUTE(.);
    } > uflash :text

    .text : {
        _stext = ABSOLUTE(.);
        *(.text .text.*)
        *(.fixup)
        *(.gnu.warning)
        *(.rodata .rodata.*)
        *(.gnu.linkonce.t.*)
        *(.glue_7)
        *(.glue_7t)
        *(.got)
        *(.gcc_except_table)
        *(.gnu.linkonce.r.*)
        . = ALIGN(4);
        _etext = ABSOLUTE(.);
    } > uflash

    .init_section : {
        _sinit = ABSOLUTE(.);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
        KEEP(*(.init_array EXCLUDE_FILE(*crtbegin.o *crtbegin?.o *crtend.o *crtend?.o) .ctors))
        _einit = ABSOLUTE(.);
    } > uflash

    .ARM.extab : {
        *(.ARM.extab*)
    } > uflash

    __exidx_start = ABSOLUTE(.);
    .ARM.exidx : {
        *(.ARM.exidx*)
    } > uflash

    __exidx_end = ABSOLUTE(.);

    _eronly = ABSOLUTE(.);

    .data : {
        _sdata = ABSOLUTE(.);
        *(.data .data.*)
        *(.gnu.linkonce.d.*)
        CONSTRUCTORS
        . = ALIGN(4);
        _edata = ABSOLUTE(.);
    } > usram AT > uflash :data

    .bss : {
        _sbss = ABSOLUTE(.);
        *(.bss .bss.*)
        *(.gnu.linkonce.b.*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = ABSOLUTE(.);
    } > usram :NONE

    /* Stabs debugging sections */

    .stab 0 : { *(.stab) }
    .stabstr 0 : { *(.stabstr) }
    .stab.excl 0 : { *(.stab.excl) }
    .stab.exclstr 0 : { *(.stab.exclstr) }
    .stab.index 0 : { *(.stab.index) }
    .stab.indexstr 0 : { *(.stab.indexstr) }
    .comment 0 : { *(.comment) }
    .debug_abbrev 0 : { *(.debug_abbrev) }
    .debug_info 0 : { *(.debug_info) }
    .debug_line 0 : { *(.debug_line) }
    .debug_pubnames 0 : { *(.debug_pubnames) }
    .debug_aranges 0 : { *(.debug_aranges) }

}
