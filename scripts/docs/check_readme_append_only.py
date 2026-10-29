#!/usr/bin/env python3
"""README.md is append/update only: nothing is ever removed from it (CLAUDE.md guardrail 10, ADR-0512,
user-directed and strict).

usage: check_readme_append_only.py BASE [HEAD]
  BASE, HEAD: git object specs of the two README versions, e.g. origin/main:README.md, or :README.md
  for the staged version. HEAD defaults to README.md in the working tree.

Fails when, from BASE to HEAD:
  - a section heading (<h2>...</h2> or a Markdown heading) disappears,
  - the number of non-blank lines goes down, or
  - the number of words goes down.
Updating a line in place (a status that moves on, a corrected row) passes as long as nothing is cut.
Every BASE line no longer present verbatim is listed, so a reviewer sees each in-place update."""
import re
import subprocess
import sys

HEADING = re.compile(r"^\s*(?:<h[1-6][^>]*>(.*?)</h[1-6]>|#{1,6}\s+(.*?))\s*$")


def git_show(spec):
    r = subprocess.run(["git", "show", spec], capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"cannot read {spec}: {r.stderr.strip()}")
    return r.stdout


def headings(text):
    out = []
    for line in text.split("\n"):
        m = HEADING.match(line)
        if m:
            out.append((m.group(1) or m.group(2)).strip())
    return out


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    base = git_show(sys.argv[1])
    head = git_show(sys.argv[2]) if len(sys.argv) == 3 else open("README.md").read()
    errs = []
    gone = [h for h in headings(base) if h not in headings(head)]
    if gone:
        errs.append(f"section headings removed: {gone}")
    b_lines = [l for l in base.split("\n") if l.strip()]
    h_lines = [l for l in head.split("\n") if l.strip()]
    if len(h_lines) < len(b_lines):
        errs.append(f"non-blank lines went down: {len(b_lines)} -> {len(h_lines)}")
    b_words, h_words = len(base.split()), len(head.split())
    if h_words < b_words:
        errs.append(f"words went down: {b_words} -> {h_words}")
    head_set = set(h_lines)
    changed = [l for l in b_lines if l not in head_set]
    if changed:
        print(f"{len(changed)} line(s) of the base README are no longer present verbatim (updated in place or removed):")
        for l in changed:
            print("  -", l[:160])
    for e in errs:
        print("FAIL:", e)
    if errs:
        print("README.md is append/update only (CLAUDE.md guardrail 10, ADR-0512): never remove anything from it.")
        sys.exit(1)
    print(f"OK: README.md {len(b_lines)} -> {len(h_lines)} non-blank lines, {b_words} -> {h_words} words, no section removed")


if __name__ == "__main__":
    main()
