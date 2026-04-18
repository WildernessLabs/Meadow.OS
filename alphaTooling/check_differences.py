#!/usr/bin/env python3
#
#   This script checks two directories for differences in their contents.  It is intended to check for merge conflicts
#   that have not been resolved correctly.
#
#   Usage:
#       python check_differences.py <source_directory>
#
#   The script will compare the contents of the specified source directory with the contents of the nuttx directory
#   in this repository.  The local nuttx directory can be inferred relative to the script directory.
#
#   Not all files will exist in both directories.  The general rule is that if a file exists in both directories then
#   they should be identical.  If a file exists in only one directory then it is not a conflict.
#   The script will report any files that exist in both directories but are different.
#
import argparse
import filecmp
import os
import sys

def parse_args():
    """
    Parse command-line arguments.

    Returns:
        argparse.Namespace: Parsed arguments with a 'source_directory' attribute.
    """
    parser = argparse.ArgumentParser(
        description='Compare a source directory against the local nuttx directory and report differing files.'
    )
    parser.add_argument('source_directory', help='Path to the source nuttx directory to compare against')
    if len(sys.argv) == 1:
        parser.print_help(sys.stderr)
        sys.exit(1)
    return parser.parse_args()


def find_differences(source_dir, local_dir):
    """
    Walk source_dir and, for every file that also exists in local_dir,
    compare the two.  Return a list of relative paths where the files differ.

    Files that exist in only one directory are ignored — they are not conflicts.
    Comparison is byte-for-byte (shallow=False) to catch identical-size differences.

    Args:
        source_dir (str): Absolute path to the external/reference nuttx directory.
        local_dir (str):  Absolute path to the local nuttx directory in this repository.

    Returns:
        list[str]: Relative paths (from their respective roots) of files present in
                   both directories whose contents differ.
    """
    differences = []

    for dirpath, _dirnames, filenames in os.walk(source_dir):
        for filename in filenames:
            source_file = os.path.join(dirpath, filename)
            rel_path = os.path.relpath(source_file, source_dir)
            local_file = os.path.join(local_dir, rel_path)

            if not os.path.isfile(local_file):
                # File only exists in source — not a conflict
                continue

            if not filecmp.cmp(source_file, local_file, shallow=False):
                differences.append(rel_path)

    return differences


def main():
    """
    Entry point.  Resolves directories, delegates comparison to find_differences(),
    and prints a sorted report of differing files.

    Returns:
        int: 0 if no differences were found, 1 otherwise.
    """
    args = parse_args()

    source_dir = os.path.realpath(args.source_directory)
    if not os.path.isdir(source_dir):
        print(f"Error: source directory '{source_dir}' does not exist.", file=sys.stderr)
        sys.exit(1)

    # Infer the local nuttx directory relative to this script's location
    script_dir = os.path.dirname(os.path.realpath(__file__))
    local_dir = os.path.realpath(os.path.join(script_dir, '..', 'nuttx'))
    if not os.path.isdir(local_dir):
        print(f"Error: local nuttx directory not found at '{local_dir}'.", file=sys.stderr)
        sys.exit(1)

    print(f"Source : {source_dir}")
    print(f"Local  : {local_dir}")
    print()

    differences = find_differences(source_dir, local_dir)

    if not differences:
        print("No differences found.")
    else:
        print(f"Files present in both directories but different ({len(differences)}):")
        for rel_path in sorted(differences):
            print(f"  {rel_path}")

    return 0 if not differences else 1


if __name__ == '__main__':
    sys.exit(main())