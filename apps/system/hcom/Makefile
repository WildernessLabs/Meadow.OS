############################################################################
# apps/system/hcom/Makefile
#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.  The
# ASF licenses this file to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance with the
# License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations
# under the License.
#
############################################################################

include $(APPDIR)/Make.defs

# hcom built-in application info

CONFIG_EXAMPLES_HCOM_PRIORITY ?= SCHED_PRIORITY_DEFAULT
CONFIG_EXAMPLES_HCOM_STACKSIZE ?= 2048

APPNAME = hcom

PRIORITY  = $(CONFIG_EXAMPLES_HCOM_PRIORITY)
STACKSIZE = $(CONFIG_EXAMPLES_HCOM_STACKSIZE)

CFLAGS += -I "$(TOPDIR)/../mbedtls/include"

# hcom example

HCOM_DIR = ../../../hcom/
ASRCS =
HCOM_SRCS = comms/hcom_host_encode.c \
	comms/hcom_host_receive.c \
	comms/hcom_host_enq_deq.c \
	comms/hcom_host_preprocess.c \
	comms/hcom_host_route.c \
	comms/hcom_host_send.c \
	comms/hcom_host_watchdog.c \
	diag/hcom_diag_gpio.c \
	diag/hcom_diag_trace_priority.c \
	diag/hcom_diag_misc.c \
	diag/hcom_diag_nsh_support.c \
	diag/hcom_diag_decode_protocol.c \
	diag/hcom_diag_trace_to_cli.c \
	diag/hcom_diag_host_text_transport.c \
	diag/meadow_ku_traverse_user.c \
	file/hcom_file_delete_stm32f7.c \
	file/hcom_file_dir_mgmt_utils.c \
	file/hcom_file_dnld_esp32.c \
	file/hcom_file_dnld_stm32f7.c \
	file/hcom_file_lists.c \
	file/hcom_file_lists_subdir.c \
	file/hcom_file_misc.c \
	file/hcom_file_upld_proc.c \
	file/hcom_file_write_stm32f7.c \
	misc/meadow_cirbuf.c \
	misc/hcom_common_utils.c \
	misc/meadow_utils.c \
	mono/hcom_mono_control.c \
	mono/hcom_remote_vs_debug.c \
	mono/hcom_stdxxx_redirect.c \
	nx_rqsts/hcom_bbreg_access.c \
	nx_rqsts/hcom_misc_requests.c \
	nx_rqsts/hcom_via_nx_access.c \
	tests/bbreg_tests.c \
	tests/developer_tests.c \
	tests/hcom_dir_mgmt_tests.c \
	tests/vs_debug_tests.c \
	tests/diag_gpio_tests.c \
	tests/diag_misc_tests.c \
	tests/diag_ethernet_chat_test.c \
	tests/tensorflow_tests.c \
	tests/meadow_os_userspace_tests.c \
	tests/meadow_bbd_tests.c \
	misc/espcp_utils.c

ifeq ($(CONFIG_MEADOW_CLOUD),y)
 HCOM_SRCS += ota/hcom_ota.c
endif

ifeq ($(CONFIG_HCOM_ESP32_COMMS),y)
 HCOM_SRCS += esp32/hcom_esp32_execute.c \
 esp32/hcom_esp32_proc_recv.c \
 esp32/hcom_esp32_proc_util.c \
 esp32/hcom_esp32_proc_xmit.c \
 esp32/hcom_esp32_uart_comms.c \
 esp32/hcom_esp32_network_monitor.c
endif

MAINSRC = hcom_startup_manager.c

CSRCS += $(HCOM_SRCS) 

CONFIG_EXAMPLES_HCOM_PROGNAME ?= hcom$(EXEEXT)
PROGNAME = $(CONFIG_EXAMPLES_HCOM_PROGNAME)

include $(APPDIR)/Application.mk
