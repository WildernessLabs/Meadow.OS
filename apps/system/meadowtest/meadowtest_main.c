/****************************************************************************
 * apps/system/meadowtest/meadowtest_main.c
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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "../hcom/hcom_common.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: meadowtest_usage
 ****************************************************************************/

static void meadowtest_usage(const char *progname)
{
  printf("Usage: %s [-p <uint32_t>] [-v <uint32_t>]\n", progname);
  printf("  -p  Test to run, 0 lists the available tests (default 0)\n");
  printf("  -v  Value passed to the test (default 0)\n");
}

/****************************************************************************
 * Name: meadowtest_parse_u32
 *
 * Description:
 *  Parse a decimal, octal (leading 0) or hex (leading 0x) unsigned 32-bit
 *  number.  The whole string must be consumed.
 *
 * Returned Value:
 *  0 on success, -1 if str is not a valid uint32_t.
 *
 ****************************************************************************/

static int meadowtest_parse_u32(const char *str, uint32_t *value)
{
  unsigned long long parsed;
  char *end;

  /* strtoull() accepts a leading '-' and silently negates, reject it. */

  if (str[0] == '\0' || str[0] == '-')
    {
      return -1;
    }

  errno  = 0;
  parsed = strtoull(str, &end, 0);
  if (errno != 0 || *end != '\0' || parsed > UINT32_MAX)
    {
      return -1;
    }

  *value = (uint32_t)parsed;
  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: main
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  uint32_t param = 0;
  uint32_t value = 0;
  int option;

  optind = 1;
  while ((option = getopt(argc, argv, "p:v:")) != ERROR)
    {
      switch (option)
        {
          case 'p':
            if (meadowtest_parse_u32(optarg, &param) < 0)
              {
                printf("Invalid -p value '%s'\n", optarg);
                meadowtest_usage(argv[0]);
                return EXIT_FAILURE;
              }
            break;

          case 'v':
            if (meadowtest_parse_u32(optarg, &value) < 0)
              {
                printf("Invalid -v value '%s'\n", optarg);
                meadowtest_usage(argv[0]);
                return EXIT_FAILURE;
              }
            break;

          default:
            meadowtest_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

  if (optind < argc)
    {
      printf("Unexpected argument '%s'\n", argv[optind]);
      meadowtest_usage(argv[0]);
      return EXIT_FAILURE;
    }

  /* The test dispatcher takes the test ID as a uint16_t. */

  if (param > UINT16_MAX)
    {
      printf("-p must be in the range 0 to %u\n", UINT16_MAX);
      return EXIT_FAILURE;
    }

  hcom_developer_tests_developer((uint16_t)param, value);
  return EXIT_SUCCESS;
}
