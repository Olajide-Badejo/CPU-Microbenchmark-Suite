#!/usr/bin/env python3
"""Fail if any tracked text file contains an em dash or en dash.

Rationale (Section 4, rule 1 of the build spec): em dashes (U+2014) and en
dashes (U+2013) are banned repo wide. In LaTeX prose a literal "--" or "---"
renders as an en or em dash, so .tex files are additionally checked for those
ASCII sequences outside of comments and a small set of legitimate exceptions
(package option ranges are not used in this project, so the rule is blunt on
purpose).

Exit code 0 means clean. Non zero prints every offending file and line.

I run this from Phase 0 onward in `make check-style` and again as the final
step of every report build, because a single stray dash in a compiled PDF is
the kind of thing that slips through review.
"""

from __future__ import annotations

import sys
from pathlib import Path

EM_DASH = "—"
EN_DASH = "–"

# Directories that never contain prose I control.
SKIP_DIRS = {
    ".git",
    "build",
    "__pycache__",
    ".ruff_cache",
    ".venv",
    "raw",
}

# Individual files that are not authored prose I control. The originating build
# spec is a prompt artifact, not a deliverable, so its contents are out of
# scope for the house style rules.
SKIP_FILES = {
    "09_README_cpu_microbenchmark_suite.md",
}

# Extensions worth scanning. Everything else (PDF, PNG, binary) is skipped.
TEXT_SUFFIXES = {
    ".py",
    ".cpp",
    ".hpp",
    ".h",
    ".c",
    ".md",
    ".tex",
    ".bib",
    ".sh",
    ".yml",
    ".yaml",
    ".cmake",
    ".txt",
    ".json",
    ".cfg",
    ".toml",
}


def iter_text_files(root: Path):
    for path in root.rglob("*"):
        if not path.is_file():
            continue
        if any(part in SKIP_DIRS for part in path.parts):
            continue
        if path.name in SKIP_FILES:
            continue
        # This script itself defines the dash characters as constants, so it is
        # the one file allowed to contain them.
        if path.name == "check_no_dashes.py":
            continue
        if path.suffix.lower() in TEXT_SUFFIXES:
            yield path


def scan_file(path: Path) -> list[tuple[int, str]]:
    hits: list[tuple[int, str]] = []
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return hits
    is_tex = path.suffix.lower() == ".tex"
    for lineno, line in enumerate(text.splitlines(), start=1):
        if EM_DASH in line:
            hits.append((lineno, "U+2014 em dash"))
        if EN_DASH in line:
            hits.append((lineno, "U+2013 en dash"))
        if is_tex:
            # Ignore TeX comments (everything after an unescaped %).
            prose = _strip_tex_comment(line)
            if "--" in prose:
                hits.append((lineno, 'literal "--" in .tex prose'))
    return hits


def _strip_tex_comment(line: str) -> str:
    out = []
    escaped = False
    for ch in line:
        if ch == "%" and not escaped:
            break
        out.append(ch)
        escaped = ch == "\\" and not escaped
    return "".join(out)


def main(argv: list[str]) -> int:
    root = Path(argv[1]) if len(argv) > 1 else Path(__file__).resolve().parent.parent
    offenders: list[str] = []
    for path in iter_text_files(root):
        for lineno, reason in scan_file(path):
            rel = path.relative_to(root)
            offenders.append(f"{rel}:{lineno}: {reason}")
    if offenders:
        print("Dash check FAILED. Offending locations:", file=sys.stderr)
        for line in offenders:
            print(f"  {line}", file=sys.stderr)
        print(f"\n{len(offenders)} hit(s). Replace with prose, for example "
              f'"4 KB to 512 MB".', file=sys.stderr)
        return 1
    print("Dash check passed: zero em or en dashes in tracked text files.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
