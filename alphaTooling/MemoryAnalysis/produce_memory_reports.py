#!/usr/bin/env python3
"""
produce_memory_reports.py

Runs membrowse against the Meadow kernel (nuttx.elf) and user-space
(nuttx_user.elf) binaries, combines the resulting JSON reports, and
produces the following output files:

  - memory-report.txt   : human-readable report (mirrors membrowse console style)
  - memory_regions.csv  : region utilisation table
  - memory_symbols.csv  : full combined symbol table
  - memory_report.sql   : PostgreSQL-compatible DDL + INSERT statements

ELF and linker-script paths are derived from this script's location; no path
arguments are required.  The intermediate JSON files produced by membrowse are
deleted after use.  Each run is tagged with a run name to allow comparison and
tracking across multiple builds in the SQL database.

Usage:
    produce_memory_reports.py [--run-name <name>] [--output-dir <dir>] [--format text|csv|sql|all]
"""

import argparse
import csv
import json
import os
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path


# ---------------------------------------------------------------------------
# Paths — all derived from this script's location
#   alphaTooling/MemoryAnalysis/produce_memory_reports.py
#   └─ parent ──► alphaTooling/MemoryAnalysis
#   └─  alphaToolingparent ──
#   └─ parent ──► <repo root>
# ---------------------------------------------------------------------------

_REPO_ROOT      = Path(__file__).resolve().parent.parent.parent
_NUTTX_DIR      = _REPO_ROOT / "nuttx"
_KERNEL_ELF     = _NUTTX_DIR / "nuttx.elf"
_USER_ELF       = _NUTTX_DIR / "nuttx_user.elf"
_SCRIPTS_DIR    = _NUTTX_DIR / "boards/arm/stm32f7/stm32f777zit6-meadow/scripts"
_LINKER_SCRIPTS = (
    f"{_SCRIPTS_DIR}/memory.ld"
    f" {_SCRIPTS_DIR}/kernel-space.ld"
    f" {_SCRIPTS_DIR}/user-space.ld"
)
_TARGET_NAME    = "stm32f7"
_OUTPUT_DIR     = _REPO_ROOT / "tmp"


# ---------------------------------------------------------------------------
# membrowse execution
# ---------------------------------------------------------------------------

def run_membrowse(elf_path: str, linker_scripts: str, target_name: str) -> str:
    """Invoke ``membrowse report --json`` and capture the output to a temp file.

    The linker scripts are passed as a single space-separated string, matching
    the argument format expected by membrowse.

    Args:
        elf_path:        Absolute path to the ELF binary to analyse.
        linker_scripts:  Space-separated list of linker script paths.
        target_name:     Target architecture name passed to membrowse (e.g. ``stm32f7``).

    Returns:
        Path to a temporary JSON file containing the membrowse output.
        The caller is responsible for deleting the file after use.

    Exits:
        Calls ``sys.exit(1)`` if the ELF file is missing or membrowse fails.
    """
    if not os.path.isfile(elf_path):
        print(f"Error: ELF file not found: {elf_path}", file=sys.stderr)
        sys.exit(1)

    # Create a named temp file; membrowse stdout will be written into it.
    fd, tmp_path = tempfile.mkstemp(suffix=".json", prefix="membrowse_")
    os.close(fd)

    cmd = [
        "membrowse", "report",
        elf_path,
        linker_scripts,
        "--all-symbols",
        "--target-name", target_name,
        "--json",
    ]

    print(f"  Running: {' '.join(cmd)}")
    try:
        result = subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=True,
        )
    except FileNotFoundError:
        print("Error: 'membrowse' not found. Install it with: pip install membrowse", file=sys.stderr)
        os.unlink(tmp_path)
        sys.exit(1)
    except subprocess.CalledProcessError as exc:
        print(f"Error: membrowse failed for {elf_path}:", file=sys.stderr)
        print(exc.stderr.decode(errors="replace"), file=sys.stderr)
        os.unlink(tmp_path)
        sys.exit(1)

    with open(tmp_path, "wb") as fh:
        fh.write(result.stdout)

    return tmp_path


# ---------------------------------------------------------------------------
# JSON loading
# ---------------------------------------------------------------------------

def load_report(path: str, label: str) -> dict:
    """Load and minimally validate a membrowse JSON report.

    Args:
        path:   Path to the JSON file produced by ``membrowse --json``.
        label:  Short identifier for the binary, e.g. ``"kernel"`` or
                ``"userspace"``.  Stored as ``_label`` on the returned dict.

    Returns:
        Parsed report dict with ``_label`` attached.

    Exits:
        Calls ``sys.exit(1)`` if the file is missing, malformed, or lacks
        required top-level fields.
    """
    try:
        with open(path, encoding="utf-8") as fh:
            data = json.load(fh)
    except FileNotFoundError:
        print(f"Error: file not found: {path}", file=sys.stderr)
        sys.exit(1)
    except json.JSONDecodeError as exc:
        print(f"Error: invalid JSON in {path}: {exc}", file=sys.stderr)
        sys.exit(1)

    for required in ("file_path", "symbols", "memory_layout"):
        if required not in data:
            print(f"Error: {path} is missing required field '{required}'", file=sys.stderr)
            sys.exit(1)

    data["_label"] = label
    return data


# ---------------------------------------------------------------------------
# Human-readable report helpers
# ---------------------------------------------------------------------------

# Width of the ASCII utilisation bar rendered in the region table.
BAR_WIDTH  = 20
FILL_CHAR  = "█"
EMPTY_CHAR = "░"


def _bar(utilization_pct: float) -> str:
    """Return a fixed-width ASCII utilisation bar for the given percentage.

    Example: ``_bar(34.2)`` → ``[███████░░░░░░░░░░░░░]``
    """
    filled = max(0, min(BAR_WIDTH, round(utilization_pct / 100 * BAR_WIDTH)))
    return f"[{FILL_CHAR * filled}{EMPTY_CHAR * (BAR_WIDTH - filled)}]"


def _fmt_bytes(n: int) -> str:
    """Format an integer byte count with thousands separator, e.g. ``32,768 bytes``."""
    return f"{n:,} bytes"


def _fmt_hex(address: int) -> str:
    """Format an integer address as a zero-padded 8-digit hex string, e.g. ``0x20021bc8``."""
    return f"0x{address:08x}"


def _render_region_table(layout: dict) -> list[str]:
    """Render a region utilisation table for a single binary's ``memory_layout``.

    Each region is rendered as one row showing the address range, total size,
    used bytes, free bytes, utilisation bar, and percentage.  Child sections
    (e.g. ``.text``, ``.bss``) are indented beneath their parent region.

    Args:
        layout: The ``memory_layout`` dict from a membrowse JSON report.

    Returns:
        List of formatted lines ready to be joined with newlines.
    """
    lines: list[str] = []

    # Column widths chosen to align with the membrowse console output style.
    col_region = 20
    col_range  = 37
    col_size   = 22
    col_used   = 20
    col_free   = 20
    col_util   = 14

    # Header row
    lines.append(
        f"{'Region':<{col_region}}"
        f"{'Address Range':<{col_range}}"
        f"{'Size':>{col_size}}"
        f"{'Used':>{col_used}}"
        f"{'Free':>{col_free}}"
        f"  {'Utilization'}"
    )
    lines.append("-" * (col_region + col_range + col_size + col_used + col_free + col_util + 20))

    for region_name, region in layout.items():
        addr  = region.get("address", 0)
        limit = region.get("limit_size", 0)
        used  = region.get("used_size", 0)
        free  = region.get("free_size", limit)
        util  = region.get("utilization_percent", 0.0)
        addr_range = f"{_fmt_hex(addr)}-{_fmt_hex(addr + limit)}"

        lines.append(
            f"{region_name:<{col_region}}"
            f"{addr_range:<{col_range}}"
            f"{_fmt_bytes(limit):>{col_size}}"
            f"{_fmt_bytes(used):>{col_used}}"
            f"{_fmt_bytes(free):>{col_free}}"
            f"  {_bar(util)} {util:.1f}%"
        )

        # Indent child sections beneath the region row.
        for section in region.get("sections", []):
            lines.append(f"  {'•'} {section.get('name', '?'):<30} {_fmt_bytes(section.get('size', 0))}")

    return lines


# ---------------------------------------------------------------------------
# Human-readable report
# ---------------------------------------------------------------------------

def render_text_report(reports: list[dict], run_name: str) -> str:
    """Render the combined human-readable memory report as a single string.

    The report is structured as:
      1. Report header (run name + generation timestamp).
      2. Kernel Memory Summary — region utilisation table.
      3. User-Space Memory Summary — region utilisation table.
      4. Kernel Symbols — ELF metadata + symbol table sorted by size descending.
      5. User-Space Symbols — ELF metadata + symbol table sorted by size descending.

    Args:
        reports:   List of loaded membrowse report dicts (kernel first, then userspace).
        run_name:  Human-readable label for this build run.

    Returns:
        The complete report as a single string.
    """
    lines: list[str] = []
    ts = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")

    # --- Report header ---
    header = f"Meadow Memory Report  |  Run: {run_name}  |  Generated: {ts}"
    lines.append(header)
    lines.append("=" * len(header))
    lines.append("")

    # --- Per-binary region summaries, displayed together at the top ---
    titles = {
        "kernel":    "Kernel Memory Summary (nuttx.elf)",
        "userspace": "User-Space Memory Summary (nuttx_user.elf)",
    }

    for report in reports:
        label = report["_label"]
        title = titles.get(label, f"{label} Memory Summary")
        lines.append(title)
        lines.append("-" * len(title))
        lines.extend(_render_region_table(report.get("memory_layout", {})))
        lines.append("")

    # --- Detailed symbol tables, one section per binary ---
    section_titles = {
        "kernel":    "Kernel Symbols (nuttx.elf)",
        "userspace": "User-Space Symbols (nuttx_user.elf)",
    }

    col_name    = 40
    col_addr    = 18
    col_sz      = 20
    col_type    = 10
    col_section = 20
    col_source  = 30

    for report in reports:
        label = report["_label"]

        title = section_titles.get(label, f"{label} Symbols")
        lines.append("=" * 80)
        lines.append(title)
        lines.append("=" * 80)
        lines.append("")

        # ELF metadata line
        lines.append(
            f"ELF: {report.get('file_path', 'unknown')}"
            f"  |  Arch: {report.get('architecture', '?')}"
            f"  |  Machine: {report.get('machine', '?')}"
            f"  |  Toolchain: {report.get('toolchain', '?')}"
            f"  |  Entry: {_fmt_hex(report.get('entry_point', 0))}"
            f"  |  Type: {report.get('file_type', '?')}"
        )
        lines.append("")

        # Symbol table header
        lines.append(
            f"{'Name':<{col_name}}"
            f"{'Address':<{col_addr}}"
            f"{'Size':>{col_sz}}"
            f"  {'Type':<{col_type}}"
            f"{'Section':<{col_section}}"
            f"{'Source':<{col_source}}"
        )
        lines.append("-" * (col_name + col_addr + col_sz + col_type + col_section + col_source + 4))

        # Symbols sorted largest-first so the most significant consumers appear at the top.
        for sym in sorted(report.get("symbols", []), key=lambda s: s.get("size", 0), reverse=True):
            lines.append(
                f"{sym.get('name', '?'):<{col_name}}"
                f"{_fmt_hex(sym.get('address', 0)):<{col_addr}}"
                f"{_fmt_bytes(sym.get('size', 0)):>{col_sz}}"
                f"  {sym.get('type', '?'):<{col_type}}"
                f"{sym.get('section', '?'):<{col_section}}"
                f"{sym.get('source_file', '?'):<{col_source}}"
            )

        lines.append("")
        lines.append("")

    return "\n".join(lines)


# ---------------------------------------------------------------------------
# CSV output
# ---------------------------------------------------------------------------

def write_csv(reports: list[dict], output_dir: str, run_name: str) -> None:
    """Write region utilisation and symbol data to CSV files.

    Produces two files in ``output_dir``:
      - ``memory_regions.csv`` — one row per region per binary.
      - ``memory_symbols.csv`` — one row per symbol per binary.

    Both files include ``run_name`` as the first column to allow data from
    multiple builds to be stored in the same file and filtered by run.

    Args:
        reports:    List of loaded membrowse report dicts.
        output_dir: Directory to write CSV files into.
        run_name:   Build run label written into every row.
    """
    regions_path = os.path.join(output_dir, "memory_regions.csv")
    symbols_path = os.path.join(output_dir, "memory_symbols.csv")

    # Regions CSV — one row per region; sections summarised as "name=size; ..." string.
    with open(regions_path, "w", newline="", encoding="utf-8") as fh:
        writer = csv.writer(fh)
        writer.writerow([
            "run_name", "binary", "region", "address_hex", "address_dec",
            "size_bytes", "used_bytes", "free_bytes", "utilization_pct",
            "sections",
        ])
        for report in reports:
            label = report["_label"]
            for region_name, region in report.get("memory_layout", {}).items():
                addr  = region.get("address", 0)
                limit = region.get("limit_size", 0)
                used  = region.get("used_size", 0)
                free  = region.get("free_size", limit)
                util  = region.get("utilization_percent", 0.0)
                sections_summary = "; ".join(
                    f"{s['name']}={s['size']}"
                    for s in region.get("sections", [])
                )
                writer.writerow([
                    run_name, label, region_name, _fmt_hex(addr), addr,
                    limit, used, free, f"{util:.4f}",
                    sections_summary,
                ])

    # Symbols CSV — one row per symbol with full metadata.
    with open(symbols_path, "w", newline="", encoding="utf-8") as fh:
        writer = csv.writer(fh)
        writer.writerow([
            "run_name", "binary", "name", "address_hex", "address_dec",
            "size_bytes", "type", "binding", "section",
            "source_file", "visibility", "archive", "object_file",
        ])
        for report in reports:
            label = report["_label"]
            for sym in report.get("symbols", []):
                writer.writerow([
                    run_name,
                    label,
                    sym.get("name", ""),
                    _fmt_hex(sym.get("address", 0)),
                    sym.get("address", 0),
                    sym.get("size", 0),
                    sym.get("type", ""),
                    sym.get("binding", ""),
                    sym.get("section", ""),
                    sym.get("source_file", ""),
                    sym.get("visibility", ""),
                    sym.get("archive", ""),
                    sym.get("object_file", ""),
                ])

    print(f"  Wrote {regions_path}")
    print(f"  Wrote {symbols_path}")


# ---------------------------------------------------------------------------
# SQL output
# ---------------------------------------------------------------------------

def _sql_str(value) -> str:
    """Return ``value`` as a safely escaped SQL string literal, or ``NULL``.

    Single quotes within the value are doubled per the SQL standard.
    """
    if value is None:
        return "NULL"
    return "'" + str(value).replace("'", "''") + "'"


def write_sql(reports: list[dict], output_dir: str, run_name: str) -> None:
    """Write a PostgreSQL-compatible SQL script to ``output_dir/memory_report.sql``.

    The script uses ``CREATE TABLE IF NOT EXISTS`` so it is safe to run
    against an existing database.  A row is inserted into ``memory_runs``
    for this build run, and all region / symbol inserts reference it via a
    subselect on ``run_name`` so the script remains correct when appended to
    a database that already contains rows from prior runs.

    Schema:
        memory_runs             — one row per invocation (run_name UNIQUE, timestamp)
        memory_regions          — region utilisation; FK → memory_runs
        memory_region_sections  — section breakdown per region; FK → memory_regions
        memory_symbols          — full symbol table; FK → memory_runs

    Args:
        reports:    List of loaded membrowse report dicts.
        output_dir: Directory to write the SQL file into.
        run_name:   Build run label; used as the UNIQUE key in ``memory_runs``.
    """
    sql_path = os.path.join(output_dir, "memory_report.sql")

    lines: list[str] = []
    ts = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")

    lines.append(f"-- Memory report generated {ts}")
    lines.append(f"-- Run: {run_name}")
    lines.append("-- PostgreSQL-compatible")
    lines.append("")

    # --- DDL ---
    lines.append(
        "CREATE TABLE IF NOT EXISTS memory_runs ("
        "\n    id         SERIAL  PRIMARY KEY,"
        "\n    run_name   TEXT    NOT NULL UNIQUE,"
        "\n    timestamp  TEXT    NOT NULL"
        "\n);"
    )
    lines.append("")
    lines.append(
        "CREATE TABLE IF NOT EXISTS memory_regions ("
        "\n    id              SERIAL  PRIMARY KEY,"
        "\n    run_id          INTEGER NOT NULL REFERENCES memory_runs(id),"
        "\n    binary          TEXT    NOT NULL,"
        "\n    region          TEXT    NOT NULL,"
        "\n    address_hex     TEXT    NOT NULL,"
        "\n    address_dec     INTEGER NOT NULL,"
        "\n    size_bytes      INTEGER NOT NULL,"
        "\n    used_bytes      INTEGER NOT NULL,"
        "\n    free_bytes      INTEGER NOT NULL,"
        "\n    utilization_pct REAL    NOT NULL"
        "\n);"
    )
    lines.append("")
    lines.append(
        "CREATE TABLE IF NOT EXISTS memory_region_sections ("
        "\n    id              SERIAL  PRIMARY KEY,"
        "\n    region_id       INTEGER NOT NULL REFERENCES memory_regions(id),"
        "\n    name            TEXT    NOT NULL,"
        "\n    address_hex     TEXT    NOT NULL,"
        "\n    address_dec     INTEGER NOT NULL,"
        "\n    size_bytes      INTEGER NOT NULL,"
        "\n    section_type    TEXT"
        "\n);"
    )
    lines.append("")
    lines.append(
        "CREATE TABLE IF NOT EXISTS memory_symbols ("
        "\n    id              SERIAL  PRIMARY KEY,"
        "\n    run_id          INTEGER NOT NULL REFERENCES memory_runs(id),"
        "\n    binary          TEXT    NOT NULL,"
        "\n    name            TEXT    NOT NULL,"
        "\n    address_hex     TEXT    NOT NULL,"
        "\n    address_dec     INTEGER NOT NULL,"
        "\n    size_bytes      INTEGER NOT NULL,"
        "\n    type            TEXT,"
        "\n    binding         TEXT,"
        "\n    section         TEXT,"
        "\n    source_file     TEXT,"
        "\n    visibility      TEXT,"
        "\n    archive         TEXT,"
        "\n    object_file     TEXT"
        "\n);"
    )
    lines.append("")

    # --- Run record ---
    # Insert the run first; subsequent inserts resolve run_id via subselect so
    # this script can be safely applied to a database with pre-existing runs.
    lines.append(f"INSERT INTO memory_runs (run_name, timestamp) VALUES ({_sql_str(run_name)}, {_sql_str(ts)});")
    run_id_expr = f"(SELECT id FROM memory_runs WHERE run_name = {_sql_str(run_name)})"
    lines.append("")

    # --- Data ---
    for report in reports:
        label = report["_label"]
        lines.append(f"-- Binary: {label} ({report.get('file_path', '')})")

        for region_name, region in report.get("memory_layout", {}).items():
            addr  = region.get("address", 0)
            limit = region.get("limit_size", 0)
            used  = region.get("used_size", 0)
            free  = region.get("free_size", limit)
            util  = region.get("utilization_percent", 0.0)

            lines.append(
                f"INSERT INTO memory_regions "
                f"(run_id, binary, region, address_hex, address_dec, size_bytes, used_bytes, free_bytes, utilization_pct) VALUES "
                f"({run_id_expr}, {_sql_str(label)}, {_sql_str(region_name)}, {_sql_str(_fmt_hex(addr))}, "
                f"{addr}, {limit}, {used}, {free}, {util:.6f});"
            )

            # Resolve the parent region row for the section FK via a correlated subselect.
            region_id_expr = (
                f"(SELECT id FROM memory_regions WHERE run_id = {run_id_expr} "
                f"AND binary = {_sql_str(label)} AND region = {_sql_str(region_name)})"
            )
            for section in region.get("sections", []):
                saddr = section.get("address", 0)
                lines.append(
                    f"INSERT INTO memory_region_sections "
                    f"(region_id, name, address_hex, address_dec, size_bytes, section_type) VALUES "
                    f"({region_id_expr}, {_sql_str(section.get('name', ''))}, "
                    f"{_sql_str(_fmt_hex(saddr))}, {saddr}, "
                    f"{section.get('size', 0)}, {_sql_str(section.get('type', ''))});"
                )

        lines.append("")

        for sym in report.get("symbols", []):
            addr = sym.get("address", 0)
            lines.append(
                f"INSERT INTO memory_symbols "
                f"(run_id, binary, name, address_hex, address_dec, size_bytes, type, binding, section, "
                f"source_file, visibility, archive, object_file) VALUES "
                f"({run_id_expr}, {_sql_str(label)}, {_sql_str(sym.get('name', ''))}, "
                f"{_sql_str(_fmt_hex(addr))}, {addr}, {sym.get('size', 0)}, "
                f"{_sql_str(sym.get('type', ''))}, {_sql_str(sym.get('binding', ''))}, "
                f"{_sql_str(sym.get('section', ''))}, {_sql_str(sym.get('source_file', ''))}, "
                f"{_sql_str(sym.get('visibility', ''))}, {_sql_str(sym.get('archive', ''))}, "
                f"{_sql_str(sym.get('object_file', ''))});"
            )

        lines.append("")

    with open(sql_path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines))

    print(f"  Wrote {sql_path}")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    """Parse CLI arguments, run membrowse for both binaries, and write reports.

    Intermediate JSON files produced by membrowse are written to the system
    temp directory and deleted unconditionally in a ``finally`` block,
    ensuring clean-up even when an error occurs mid-run.
    """
    parser = argparse.ArgumentParser(
        description=(
            "Run membrowse against the Meadow kernel and user-space ELF binaries "
            "and produce human-readable and machine-readable memory reports."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "Examples:\n"
            "  %(prog)s\n"
            "  %(prog)s --run-name v1.2.3\n"
            "  %(prog)s -o ./reports --format csv\n"
        ),
    )
    parser.add_argument(
        "--run-name", default=None, metavar="NAME",
        help=(
            "Label for this run, used to differentiate builds in the SQL database. "
            "Defaults to the current date/time (yyyy-mm-dd hh:mm:ss)."
        ),
    )
    parser.add_argument(
        "-o", "--output-dir", default=str(_OUTPUT_DIR), metavar="DIR",
        help="Directory for output files (default: <repo>/tmp)",
    )
    parser.add_argument(
        "--format", choices=["text", "csv", "sql", "all"], default="all",
        help="Output format(s) to produce (default: all)",
    )

    args = parser.parse_args()

    # Default run name is the current local date/time in a human-readable format.
    run_name = args.run_name or datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    os.makedirs(args.output_dir, exist_ok=True)

    # Track temp file paths so the finally block can clean them up.
    kernel_json = None
    user_json   = None
    try:
        print(f"Run: {run_name}")

        print("Generating kernel report...")
        kernel_json = run_membrowse(str(_KERNEL_ELF), _LINKER_SCRIPTS, _TARGET_NAME)

        print("Generating user-space report...")
        user_json = run_membrowse(str(_USER_ELF), _LINKER_SCRIPTS, _TARGET_NAME)

        kernel_report = load_report(kernel_json, "kernel")
        user_report   = load_report(user_json,   "userspace")
        reports = [kernel_report, user_report]

        fmt = args.format

        if fmt in ("text", "all"):
            text      = render_text_report(reports, run_name)
            text_path = os.path.join(args.output_dir, "memory-report.txt")
            with open(text_path, "w", encoding="utf-8") as fh:
                fh.write(text)
            print(f"  Wrote {text_path}")
            if fmt == "text":
                # When text is the only requested format, also print to stdout.
                print()
                print(text)

        if fmt in ("csv", "all"):
            write_csv(reports, args.output_dir, run_name)

        if fmt in ("sql", "all"):
            write_sql(reports, args.output_dir, run_name)

    finally:
        # Remove the temporary JSON files generated by membrowse.
        for tmp in (kernel_json, user_json):
            if tmp and os.path.exists(tmp):
                os.unlink(tmp)
                print(f"  Deleted temporary file: {tmp}")

    print("\nDone.")


if __name__ == "__main__":
    main()
