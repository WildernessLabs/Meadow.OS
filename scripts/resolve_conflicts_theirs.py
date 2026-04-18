#!/usr/bin/env python3
"""
Resolve git conflict markers in files by accepting 'theirs' (incoming) changes.
Usage: python3 resolve_conflicts_theirs.py <directory> [<directory> ...]
"""

import re
import os
import sys


def resolve_conflicts_theirs(content):
    """Accept 'theirs' (incoming) side of all conflict markers."""
    lines = content.splitlines(keepends=True)
    result = []
    state = 'normal'  # 'normal', 'ours', 'theirs'

    for line in lines:
        stripped = line.rstrip('\r\n')
        if re.match(r'^<{7,8}[ \t]', stripped):
            state = 'ours'
        elif re.match(r'^={7,8}$', stripped) and state == 'ours':
            state = 'theirs'
        elif re.match(r'^>{7,8}[ \t]', stripped) and state == 'theirs':
            state = 'normal'
        else:
            if state != 'ours':
                result.append(line)

    return ''.join(result)


def has_conflict_markers(content):
    return bool(re.search(r'^<{7,8}[ \t]', content, re.MULTILINE))


def process_directory(directory):
    resolved = []
    skipped = []

    for root, dirs, files in os.walk(directory):
        # Skip hidden directories
        dirs[:] = [d for d in dirs if not d.startswith('.')]
        for fname in files:
            fpath = os.path.join(root, fname)
            try:
                with open(fpath, 'r', encoding='utf-8', errors='replace') as f:
                    content = f.read()
            except Exception as e:
                skipped.append((fpath, str(e)))
                continue

            if not has_conflict_markers(content):
                continue

            resolved_content = resolve_conflicts_theirs(content)

            if resolved_content == content:
                skipped.append((fpath, 'no change after resolution'))
                continue

            try:
                with open(fpath, 'w', encoding='utf-8') as f:
                    f.write(resolved_content)
                resolved.append(fpath)
            except Exception as e:
                skipped.append((fpath, str(e)))

    return resolved, skipped


def main():
    if len(sys.argv) < 2:
        print("Usage: resolve_conflicts_theirs.py <directory> [<directory> ...]")
        sys.exit(1)

    all_resolved = []
    all_skipped = []

    for directory in sys.argv[1:]:
        if not os.path.isdir(directory):
            print(f"Skipping (not a directory): {directory}")
            continue
        resolved, skipped = process_directory(directory)
        all_resolved.extend(resolved)
        all_skipped.extend(skipped)

    print(f"\nResolved {len(all_resolved)} file(s):")
    for f in all_resolved:
        print(f"  {f}")

    if all_skipped:
        print(f"\nSkipped {len(all_skipped)} file(s):")
        for f, reason in all_skipped:
            print(f"  {f}: {reason}")


if __name__ == '__main__':
    main()
