#!/usr/bin/env python3
"""FabriCAD hard-coded colour audit and lint (docs/plans/ui-redesign.md section 2.4). Stdlib only.

Searches src/** (not third_party, not tests), resources/**/*.qss and resources/**/*.svg outside
resources/icons/ and the generated resources/ui/{dark,light}/ for colour literals:
  #RGB..#RRGGBBAA, QColor("..."/numbers/Qt::), Qt::white/black/..., Quantity_NOC_*,
  Quantity_Color(<numbers>), rgb()/rgba(), setStyleSheet(, float RGBA literals {0.x f, 0.x f, 0.x f
A line ending in a `colour-lint: ok` comment is not counted (for strings that only mention a colour).

Modes:
  audit_colors.py --report      per-file table (markdown) with owner and planned action
  audit_colors.py --lint        the ctest: every file with hits must be in tools/theme/color-allowlist.txt
                                with at least that many hits allowed (exit 1 otherwise)
SPDX-License-Identifier: LGPL-2.1-or-later
"""
import argparse
import os
import re
import sys

PATTERNS = [
    ("hex", re.compile(r"(?<![&\w])#[0-9A-Fa-f]{3,8}\b")),
    ("QColor", re.compile(r"\bQColor\s*\(\s*(\"|'|0x|\d|Qt::)")),
    ("Qt::colour", re.compile(r"\bQt::(white|black|red|green|blue|cyan|magenta|gray|darkGray|lightGray|yellow)\b")),
    ("Quantity_NOC", re.compile(r"\bQuantity_NOC_\w+")),
    ("Quantity_Color", re.compile(r"\bQuantity_Color\s*\(\s*[\d.]")),
    ("rgb()", re.compile(r"\brgba?\s*\(")),  # code files: only inside a string literal (see scan_file)
    ("setStyleSheet", re.compile(r"\bsetStyleSheet\s*\(")),
    ("float RGBA", re.compile(r"\{\s*[01]?\.\d+f\s*,\s*[01]?\.\d+f\s*,\s*[01]?\.\d+f")),
]
OK_MARK = re.compile(r"colou?r-lint:\s*ok")
CODE_EXT = (".cpp", ".h", ".hpp", ".cc", ".inc")


def files(root):
    for base, dirs, names in os.walk(os.path.join(root, "src")):
        dirs[:] = [d for d in dirs if d not in ("third_party", "tests")]
        for n in names:
            if n.endswith(CODE_EXT) and n != "ThemeTokens.inc":  # generated from tokens.json
                yield os.path.join(base, n)
    res = os.path.join(root, "resources")
    for base, dirs, names in os.walk(res):
        rel = os.path.relpath(base, res).replace(os.sep, "/")
        if rel == "icons" or rel.startswith("icons/") or rel in ("ui/dark", "ui/light"):
            continue
        for n in names:
            if n.endswith(".qss") or n.endswith(".svg"):
                yield os.path.join(base, n)


def scan_file(path):
    hits = []
    with open(path, encoding="utf-8", errors="replace") as f:
        in_block = False
        for i, line in enumerate(f, 1):
            if OK_MARK.search(line):
                continue
            text = line
            if path.endswith(CODE_EXT):
                # drop // comments and /* */ blocks: a comment naming a colour is not a literal
                if in_block:
                    if "*/" in text:
                        text = text.split("*/", 1)[1]
                        in_block = False
                    else:
                        continue
                text = re.sub(r"/\*.*?\*/", "", text)
                if "/*" in text:
                    text, in_block = text.split("/*", 1)[0], True
                text = re.sub(r"//.*$", "", text) if not re.search(r"\"[^\"]*//", text) else text
            code = path.endswith(CODE_EXT)
            strings = " ".join(re.findall(r'"(?:[^"\\]|\\.)*"', text)) if code else text
            for kind, rx in PATTERNS:
                for m in rx.finditer(strings if kind == "rgb()" else text):
                    hits.append((i, kind, line.strip()))
    return hits


def load_allowlist(path):
    allow = {}
    with open(path, encoding="utf-8") as f:
        for ln in f:
            ln = "" if ln.lstrip().startswith("#") else ln.strip()  # full-line comments only
            if not ln:
                continue
            parts = ln.split(None, 3)
            if len(parts) < 3:
                raise SystemExit("bad allow-list line: " + ln)
            allow[parts[0]] = (int(parts[1]), parts[2], parts[3] if len(parts) > 3 else "")
    return allow


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
    ap.add_argument("--lint", action="store_true")
    ap.add_argument("--report", action="store_true")
    ap.add_argument("--verbose", "-v", action="store_true")
    args = ap.parse_args()
    root = args.root
    allow_path = os.path.join(root, "tools/theme/color-allowlist.txt")
    allow = load_allowlist(allow_path)
    found = {}
    for p in sorted(files(root)):
        h = scan_file(p)
        if h:
            found[os.path.relpath(p, root).replace(os.sep, "/")] = h

    if args.report:
        print("| File | Hits | Owner | Allowed | Planned action |")
        print("|---|---|---|---|---|")
        for rel, h in sorted(found.items(), key=lambda kv: -len(kv[1])):
            mx, owner, why = allow.get(rel, (0, "—", "**not allow-listed**"))
            print("| `%s` | %d | %s | %d | %s |" % (rel, len(h), owner, mx, why))
        print()
        print("Total: %d hits in %d files." % (sum(len(h) for h in found.values()), len(found)))
        return 0

    failures, notes = [], []
    for rel, h in found.items():
        if rel not in allow:
            failures.append("%s: %d colour literal(s), not in the allow-list:\n%s" % (
                rel, len(h), "\n".join("    %d [%s] %s" % x for x in h[:20])))
        elif len(h) > allow[rel][0]:
            failures.append("%s: %d colour literals, the allow-list permits %d (use tokens, theme::color(Tok::...)):\n%s"
                            % (rel, len(h), allow[rel][0], "\n".join("    %d [%s] %s" % x for x in h[:40])))
        elif len(h) < allow[rel][0]:
            notes.append("%s: %d hits, allowance %d; lower it in color-allowlist.txt" % (rel, len(h), allow[rel][0]))
        if args.verbose:
            for x in h:
                print("%s:%d [%s] %s" % ((rel,) + x))
    for rel in allow:
        if rel not in found:
            notes.append("%s: no hits any more; remove it from color-allowlist.txt" % rel)
    for n in notes:
        print("note:", n)
    for f in failures:
        print("FAIL:", f)
    total = sum(len(h) for h in found.values())
    print("colour lint: %d hits in %d files, %d allow-listed files, %d failures" % (total, len(found), len(allow),
                                                                                   len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
