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
  /*
  mono_data PT_LOAD;
  mono_comment PT_LOAD; */
}

SECTIONS
{
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
    } > usram

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

    /* .mono : {
        _s_mono = ABSOLUTE(.);
        *libmonosgen.a:* (.text .text.*)
        *libmini.a:* (.text .text.*)
        *libmono-dbg.a:* (.text .text.*)
        *libmono-ee-interp.a:* (.text .text.*)
        *libeglib.a:* (.text .text.*)
        *libmonoruntime-config.a:* (.text .text.*)
        *libmonoruntimesgen.a:* (.text .text.*)
        *libmonoutils.a:* (.text .text.*)
        *libmonodis.a:* (.text .text.*)
        *libmono-native.a:* (.text .text.*)
        *libmonoruntime-support.a:* (.text .text.*)
        *libz.a:* (.text .text.*)
        *libmono-mbedtls.a:* (.text .text.*)
        _e_mono = ABSOLUTE(.);
    } > sdram

    .mono_data : {
        . = ALIGN(8192);
        _s_mono_data = ABSOLUTE(.);
        *libmonosgen.a:* (.data .data.* .rodata .rodata.* .gnu.linkonce.d.*)
        *libmini.a:* (.data .data.* .rodata .rodata.* .gnu.linkonce.d.*)
        *libmono-dbg.a:* (.data .data.* .rodata .rodata.* .gnu.linkonce.d.*)
        *libmono-ee-interp.a:* (.data .data.* .rodata .rodata.* .gnu.linkonce.d.*)
        *libeglib.a:* (.data .data.* .rodata .rodata.* .gnu.linkonce.d.*)
        *libmonoruntime-config.a:* (.data .data.* .rodata .rodata.* .gnu.linkonce.d.*)
        *libmonoruntimesgen.a:* (.data .data.* .rodata .rodata.* .gnu.linkonce.d.*)
        *libmonoutils.a:* (.data .data.* .rodata .rodata.* .gnu.linkonce.d.*)
        *libmonodis.a:* (.data .data.* .rodata .rodata.* .gnu.linkonce.d.*)
        *libmono-native.a:* (.data .data.* .rodata .rodata.* .gnu.linkonce.d.*)
        *libmonoruntime-support.a:* (.data .data.* .rodata .rodata.* .gnu.linkonce.d.*)
        *libz.a:* (.data .data.* .rodata .rodata.* .gnu.linkonce.d.*)
        *libmono-mbedtls.a:* (.data .data.* .rodata .rodata.* .gnu.linkonce.d.*)
        CONSTRUCTORS
        _e_mono_data = ABSOLUTE(.);
    } > sdram :mono_data

    .mono_comment : {
        . = ALIGN(1024);
        _s_mono_comment = ABSOLUTE(.);
        *libmonosgen.a:* (.comment)
        *libmini.a:* (.comment)
        *libmono-dbg.a:* (.comment)
        *libmono-ee-interp.a:* (.comment)
        *libeglib.a:* (.comment)
        *libmonoruntime-config.a:* (.comment)
        *libmonoruntimesgen.a:* (.comment)
        *libmonoutils.a:* (.comment)
        *libmonodis.a:* (.comment)
        *libmono-native.a:* (.comment)
        *libmonoruntime-support.a:* (.comment)
        *libz.a:* (.comment)
        *libmono-mbedtls.a:* (.comment)
        _e_mono_comment = ABSOLUTE(.);
    } > sdram

    .mono_bss : {
        . = ALIGN(1024);
        _s_mono_bss = ABSOLUTE(.);
        *libmonosgen.a:* (.bss .bss.* .gnu.linkonce.b.* )
        *libmini.a:* (.bss .bss.* .gnu.linkonce.b.* )
        *libmono-dbg.a:* (.bss .bss.* .gnu.linkonce.b.* )
        *libmono-ee-interp.a:* (.bss .bss.* .gnu.linkonce.b.* )
        *libeglib.a:* (.bss .bss.* .gnu.linkonce.b.* )
        *libmonoruntime-config.a:* (.bss .bss.* .gnu.linkonce.b.* )
        *libmonoruntimesgen.a:* (.bss .bss.* .gnu.linkonce.b.* )
        *libmonoutils.a:* (.bss .bss.* .gnu.linkonce.b.* )
        *libmonodis.a:* (.bss .bss.* .gnu.linkonce.b.* )
        *libmono-native.a:* (.bss .bss.* .gnu.linkonce.b.* )
        *libmonoruntime-support.a:* (.bss .bss.* .gnu.linkonce.b.* )
        *libz.a:* (.bss .bss.* .gnu.linkonce.b.* )
        *libmono-mbedtls.a:* (.bss .bss.* .gnu.linkonce.b.* )
        . = ALIGN(1048576);
        _e_mono_bss = ABSOLUTE(.);
    } > sdram */
}
