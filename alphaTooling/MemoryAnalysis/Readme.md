# Meadow Memory Analysis — `produce_memory_reports.py`

Generates combined memory-usage reports for the Meadow kernel (`nuttx.elf`) and
user-space (`nuttx_user.elf`) ELF binaries.  Internally the script invokes
[`membrowse`](https://pypi.org/project/membrowse/) against both binaries, merges
the results, and writes four output artefacts.

---

## Prerequisites

| Requirement | Notes |
|---|---|
| Python 3.10+ | Standard library only (no third-party runtime deps) |
| `membrowse` | `pip install membrowse` |
| Built ELF files | `nuttx/nuttx.elf` and `nuttx/nuttx_user.elf` must exist (run `build.sh` first) |
| Linker scripts | Expected at `nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/scripts/` — present after a clone |

All paths are derived automatically from the script's own location; no manual
configuration is required.

---

## Output files

All files are written to `<repo>/tmp/` by default (the directory is created if it
does not exist).

| File | Description |
|---|---|
| `memory-report.txt` | Human-readable combined report (region summaries + symbol tables) |
| `memory_regions.csv` | Region utilisation table — one row per region per binary |
| `memory_symbols.csv` | Full combined symbol table — one row per ELF symbol |
| `memory_report.sql` | PostgreSQL-compatible DDL + `INSERT` statements for import into a database |

---

## Usage

Run the script from any directory within the repository:

```bash
python3 alphaTooling/MemoryAnalysis/produce_memory_reports.py
```

### Options

```
usage: produce_memory_reports.py [--run-name NAME] [-o DIR] [--format {text,csv,sql,all}]

options:
  --run-name NAME     Label for this run used to differentiate builds in the SQL
                      database.  Defaults to the current date/time:
                      yyyy-mm-dd hh:mm:ss
  -o, --output-dir DIR
                      Directory for output files (default: <repo>/tmp)
  --format {text,csv,sql,all}
                      Output format(s) to produce (default: all)
  -h, --help          Show this help message and exit
```

### Examples

```bash
# Produce all output formats with an auto-generated run name (date/time)
python3 alphaTooling/MemoryAnalysis/produce_memory_reports.py

# Tag the run with a release version
python3 alphaTooling/MemoryAnalysis/produce_memory_reports.py --run-name v1.14.0

# Write only the text report to a custom directory
python3 alphaTooling/MemoryAnalysis/produce_memory_reports.py -o ./reports --format text

# Print the text report to stdout as well as writing the file
python3 alphaTooling/MemoryAnalysis/produce_memory_reports.py --format text

# Write CSV and SQL outputs only
python3 alphaTooling/MemoryAnalysis/produce_memory_reports.py --format csv
python3 alphaTooling/MemoryAnalysis/produce_memory_reports.py --format sql
```

---

## Sample output

### Console output

```
Run: v1.14.0
Generating kernel report...
  Running: membrowse report nuttx/nuttx.elf "...memory.ld kernel-space.ld user-space.ld" --all-symbols --target-name stm32f7 --json
Generating user-space report...
  Running: membrowse report nuttx/nuttx_user.elf "...memory.ld kernel-space.ld user-space.ld" --all-symbols --target-name stm32f7 --json
  Wrote tmp/memory-report.txt
  Deleted temporary file: /tmp/membrowse_abc123.json
  Deleted temporary file: /tmp/membrowse_def456.json

Done.
```

### `memory-report.txt` (excerpt)

```
Meadow Memory Report  |  Run: v1.14.0  |  Generated: 2026-05-25 10:12:34 UTC
==============================================================================

Kernel Memory Summary (nuttx.elf)
----------------------------------
Region              Address Range                            Size                Used                Free    Utilization
-------------------------------------------------------------------------------------------------------------------------------------------------------------
FLASH               0x08000000-0x08200000              2.00 MiB           1.43 MiB         588.00 KiB  [###############     ] 71.5%
  • .text                              1.21 MiB
  • .rodata                          234.50 KiB
  • .data                              12.00 KiB
DTCM                0x20000000-0x20020000            128.00 KiB          94.25 KiB          33.75 KiB  [##############      ] 73.6%
  • .data                              12.00 KiB
  • .bss                               82.25 KiB
SRAM1               0x20020000-0x2004C000            176.00 KiB          51.00 KiB         125.00 KiB  [#####               ] 29.0%

User-Space Memory Summary (nuttx_user.elf)
-------------------------------------------
Region              Address Range                            Size                Used                Free    Utilization
-------------------------------------------------------------------------------------------------------------------------------------------------------------
FLASH               0x08180000-0x08200000            512.00 KiB         288.00 KiB         224.00 KiB  [###########         ] 56.3%
  • .text                            256.00 KiB
  • .rodata                           32.00 KiB
SRAM2               0x2004C000-0x20060000             80.00 KiB          44.00 KiB          36.00 KiB  [##########          ] 55.0%
  • .data                              4.00 KiB
  • .bss                               40.00 KiB

================================================================================
Kernel Symbols (nuttx.elf)
================================================================================

ELF: nuttx/nuttx.elf  |  Arch: ARM  |  Machine: ARM Cortex-M7  |  Toolchain: arm-none-eabi-gcc 13.2.1  |  Entry: 0x08000411  |  Type: EXEC

Name                                    Address               Size    Type      Section             Source
-----------------------------------------------------------------------------------------------------------------------
idle_stack                              0x20000000        32.00 KiB  OBJECT    .bss                sched/idle.c
g_mmheap                                0x20008000        16.00 KiB  OBJECT    .bss                mm/mm_heap.c
nx_start                                0x08001a00         8.25 KiB  FUNC      .text               sched/nx_start.c
arm_vectortable                         0x08000000         1.00 KiB  OBJECT    .text               arch/arm/src/stm32f7/vectors.c
...

================================================================================
User-Space Symbols (nuttx_user.elf)
================================================================================

ELF: nuttx/nuttx_user.elf  |  Arch: ARM  |  Machine: ARM Cortex-M7  |  Toolchain: arm-none-eabi-gcc 13.2.1  |  Entry: 0x08180411  |  Type: EXEC

Name                                    Address               Size    Type      Section             Source
-----------------------------------------------------------------------------------------------------------------------
hcom_main                               0x08181000        24.00 KiB  FUNC      .text               apps/hcom/hcom_main.c
g_uart_buffer                           0x2004C000        16.00 KiB  OBJECT    .bss                apps/hcom/uart.c
...
```

### `memory_regions.csv` (excerpt)

```csv
run_name,binary,region,address_hex,total_bytes,used_bytes,free_bytes,utilization_pct
v1.14.0,kernel,FLASH,0x08000000,2097152,1499136,598016,71.5
v1.14.0,kernel,DTCM,0x20000000,131072,96512,34560,73.6
v1.14.0,kernel,SRAM1,0x20020000,180224,52224,128000,29.0
v1.14.0,userspace,FLASH,0x08180000,524288,294912,229376,56.3
v1.14.0,userspace,SRAM2,0x2004C000,81920,45056,36864,55.0
```

### `memory_symbols.csv` (excerpt)

```csv
run_name,binary,name,address_hex,size_bytes,type,binding,section,source_file
v1.14.0,kernel,idle_stack,0x20000000,32768,OBJECT,LOCAL,.bss,sched/idle.c
v1.14.0,kernel,g_mmheap,0x20008000,16384,OBJECT,LOCAL,.bss,mm/mm_heap.c
v1.14.0,kernel,nx_start,0x08001a00,8448,FUNC,GLOBAL,.text,sched/nx_start.c
v1.14.0,userspace,hcom_main,0x08181000,24576,FUNC,GLOBAL,.text,apps/hcom/hcom_main.c
```

### `memory_report.sql` (excerpt)

```sql
-- Meadow Memory Report — PostgreSQL-compatible
-- Run: v1.14.0  |  Generated: 2026-05-25 10:12:34 UTC

CREATE TABLE IF NOT EXISTS memory_runs (
    id         SERIAL PRIMARY KEY,
    run_name   TEXT NOT NULL UNIQUE,
    created_at TIMESTAMP NOT NULL
);

CREATE TABLE IF NOT EXISTS memory_regions (
    id              SERIAL PRIMARY KEY,
    run_id          INTEGER NOT NULL REFERENCES memory_runs(id),
    binary          TEXT NOT NULL,
    region          TEXT NOT NULL,
    address         BIGINT,
    total_bytes     BIGINT,
    used_bytes      BIGINT,
    free_bytes      BIGINT,
    utilization_pct REAL
);

CREATE TABLE IF NOT EXISTS memory_symbols (
    id          SERIAL PRIMARY KEY,
    run_id      INTEGER NOT NULL REFERENCES memory_runs(id),
    binary      TEXT NOT NULL,
    name        TEXT,
    address     BIGINT,
    size_bytes  BIGINT,
    type        TEXT,
    binding     TEXT,
    section     TEXT,
    source_file TEXT
);

INSERT INTO memory_runs (run_name, created_at) VALUES ('v1.14.0', '2026-05-25 10:12:34');

INSERT INTO memory_regions (run_id, binary, region, address, total_bytes, used_bytes, free_bytes, utilization_pct)
VALUES (
    (SELECT id FROM memory_runs WHERE run_name = 'v1.14.0'),
    'kernel', 'FLASH', 134217728, 2097152, 1499136, 598016, 71.5
);
-- ... (one INSERT per region and symbol)
```

---

## Multi-build tracking

Each run is stored under a unique `run_name` in the SQL schema.  To compare two
builds side-by-side after importing into PostgreSQL:

```sql
-- Compare kernel FLASH usage between two builds
SELECT r.run_name,
       reg.used_bytes,
       reg.free_bytes,
       reg.utilization_pct
FROM   memory_regions reg
JOIN   memory_runs r ON r.id = reg.run_id
WHERE  reg.binary = 'kernel'
  AND  reg.region = 'FLASH'
  AND  r.run_name IN ('v1.13.0', 'v1.14.0')
ORDER BY r.run_name;
```

---

## Error handling

| Condition | Behaviour |
|---|---|
| ELF file not found | Exits with a clear error message (`Error: ELF file not found: …`) |
| `membrowse` not installed | Exits with install instructions |
| `membrowse` returns non-zero | Prints stderr from membrowse and exits |
| Temporary JSON files | Always deleted in a `finally` block, even on error |
