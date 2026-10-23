#!/usr/bin/env python3
"""Print the keyboard shortcut table of FabriCAD as Markdown, read from the sources.

Usage: tools/docs/shortcuts.py [--src DIR] [--check] [--update FILE]

--update FILE replaces the text between the markers
    <!-- shortcuts:begin -->  and  <!-- shortcuts:end -->
in FILE (docs/USER_GUIDE.md) with the generated tables instead of printing them.

No build is needed: the script greps the C++ sources for the places where shortcuts are
registered and prints one Markdown table per context (application, view, sketch, part,
assembly, drawing), followed by the keys the shortcut router handles itself.

Registration shapes it understands (keep new registrations in one of these, or extend it):
  * a helper call with the shortcut as a positional argument:
      app/AppTools.cpp      info(id, name, tip, SHORTCUT, aliases, icon)
      part/tools/*.cpp      info(id, name, tip, tab, group, order, promoted, SHORTCUT, aliases)
      drawing/tools/*.cpp   drawingToolInfo(id, name, tip, group, order, promoted, SHORTCUT, aliases)
  * aggregate tables in app/AppTools.cpp: Std{id, name, KEY, ...}, Orbit{id, name, KEY, ...}
  * kWorkspaceShortcuts in app/TitleBar.h: {choice, toolId, name, KEY, icon}
  * designated initialisers: ToolInfo{.id = ..., .name = ..., .shortcut = KEY, ...}
  * assignments after a helper: ToolInfo i = verb|assembleInfo|info(id, name, ...); i.shortcut = KEY;

--check exits non-zero when a `shortcut` assignment was found that none of the rules could
attribute to a tool, or when two tools of the same context share a key.
"""
from __future__ import annotations

import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

STR = r'"((?:[^"\\]|\\.)*)"'


def strip_comments(s: str) -> str:
    """Remove // and /* */ comments, keep string literals and line count."""
    out, i, n = [], 0, len(s)
    while i < n:
        c = s[i]
        if c == '"':
            j = i + 1
            while j < n and s[j] != '"':
                j += 2 if s[j] == "\\" else 1
            out.append(s[i:j + 1])
            i = j + 1
        elif c == "'" :
            j = i + 1
            while j < n and s[j] != "'":
                j += 2 if s[j] == "\\" else 1
            out.append(s[i:j + 1])
            i = j + 1
        elif s.startswith("//", i):
            j = s.find("\n", i)
            i = n if j < 0 else j
        elif s.startswith("/*", i):
            j = s.find("*/", i + 2)
            seg = s[i:(n if j < 0 else j + 2)]
            out.append("\n" * seg.count("\n"))
            i = n if j < 0 else j + 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


def balanced(s: str, start: int, open_ch: str, close_ch: str) -> tuple[str, int]:
    """Text between s[start] == open_ch and its matching close_ch."""
    depth, i, n = 0, start, len(s)
    while i < n:
        c = s[i]
        if c == '"':
            j = i + 1
            while j < n and s[j] != '"':
                j += 2 if s[j] == "\\" else 1
            i = j + 1
            continue
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
            if depth == 0:
                return s[start + 1:i], i
        i += 1
    return s[start + 1:], n


def split_args(s: str) -> list[str]:
    args, depth, cur, i = [], 0, [], 0
    while i < len(s):
        c = s[i]
        if c == '"':
            j = i + 1
            while j < len(s) and s[j] != '"':
                j += 2 if s[j] == "\\" else 1
            cur.append(s[i:j + 1])
            i = j + 1
            continue
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        if c == "," and depth == 0:
            args.append("".join(cur).strip())
            cur = []
        else:
            cur.append(c)
        i += 1
    if "".join(cur).strip():
        args.append("".join(cur).strip())
    return args


def lit(arg: str) -> str | None:
    m = re.fullmatch(r'\s*' + STR + r'\s*', arg or "")
    return m.group(1) if m else None


def line_of(s: str, pos: int) -> int:
    return s.count("\n", 0, pos) + 1


# (file glob, function name, index of id, name, shortcut)
CALLS = [
    ("app/AppTools.cpp", "info", 0, 1, 3),
    ("part/tools/*.cpp", "info", 0, 1, 7),
    ("drawing/tools/*.cpp", "drawingToolInfo", 0, 1, 6),
]
AGGREGATES = [("app/AppTools.cpp", "Std", 0, 1, 2), ("app/AppTools.cpp", "Orbit", 0, 1, 2)]
HEAD_HELPERS = r"(?:info|verb|assembleInfo|drawingToolInfo)"


class Entry:
    def __init__(self, key, tool_id, name, where):
        self.key, self.tool_id, self.name, self.where = key, tool_id, name, where


def collect(src: Path) -> tuple[list[Entry], list[str]]:
    entries: list[Entry] = []
    unattributed: list[str] = []
    seen_sites: set[tuple[str, int]] = set()
    texts = {p: strip_comments(p.read_text(encoding="utf-8", errors="replace"))
             for p in src.rglob("*") if p.suffix in (".cpp", ".h") and "/tests/" not in str(p)
             and "third_party" not in str(p)}

    def rel(p: Path) -> str:
        return str(p.relative_to(src))

    def add(key, tool_id, name, p, pos):
        if not key:
            return
        seen_sites.add((rel(p), line_of(texts[p], pos)))
        entries.append(Entry(key, tool_id or "?", name or "?", f"src/{rel(p)}:{line_of(texts[p], pos)}"))

    for pattern, fn, ii, ni, si in CALLS:
        for p in sorted(src.glob(pattern)):
            s = texts.get(p)
            if s is None:
                continue
            for m in re.finditer(r"\b" + fn + r"\s*\(", s):
                body, _ = balanced(s, m.end() - 1, "(", ")")
                if "std::string&" in body:  # the helper's own definition
                    continue
                a = split_args(body)
                if len(a) > si:
                    add(lit(a[si]), lit(a[ii]), lit(a[ni]), p, m.start())
    for pattern, struct, ii, ni, si in AGGREGATES:
        for p in sorted(src.glob(pattern)):
            s = texts[p]
            for m in re.finditer(r"\b" + struct + r'\{(?=")', s):
                body, _ = balanced(s, m.end() - 1, "{", "}")
                a = split_args(body)
                if len(a) > si:
                    add(lit(a[si]), lit(a[ii]), lit(a[ni]), p, m.start())
    tb = src / "app/TitleBar.h"
    if tb in texts:
        s = texts[tb]
        m = re.search(r"kWorkspaceShortcuts\[\]\s*=\s*\{", s)
        if m:
            body, _ = balanced(s, m.end() - 1, "{", "}")
            for row in re.finditer(r"\{([^{}]*)\}", body):
                a = split_args(row.group(1))
                if len(a) >= 4:
                    add(lit(a[3]), lit(a[1]), (lit(a[2]) or "") + " workspace", tb, m.start())

    for p, s in texts.items():
        # designated initialisers
        for m in re.finditer(r"(?<![\w)\]])\.shortcut\s*=\s*" + STR, s):
            depth, i = 0, m.start()
            while i > 0:
                i -= 1
                if s[i] == "}":
                    depth += 1
                elif s[i] == "{":
                    if depth == 0:
                        break
                    depth -= 1
            block, _ = balanced(s, i, "{", "}")
            tid = re.search(r"\.id\s*=\s*" + STR, block)
            nm = re.search(r"\.name\s*=\s*" + STR, block)
            if tid:
                add(m.group(1), tid.group(1), nm.group(1) if nm else None, p, m.start())
            else:
                unattributed.append(f"src/{rel(p)}:{line_of(s, m.start())} .shortcut = \"{m.group(1)}\"")
        # i.shortcut = "..." after a helper call or i.id
        for m in re.finditer(r"\b(\w+)\.shortcut\s*=\s*" + STR, s):
            var, key = m.group(1), m.group(2)
            before = s[max(0, m.start() - 2500):m.start()]
            heads = list(re.finditer(r"\b" + re.escape(var) + r"\s*=\s*" + HEAD_HELPERS + r"\s*\(\s*" + STR +
                                     r"\s*,\s*" + STR, before))
            ids = list(re.finditer(r"\b" + re.escape(var) + r"\.id\s*=\s*" + STR, before))
            if heads:
                h = heads[-1]
                add(key, h.group(1), h.group(2), p, m.start())
            elif ids and key != "" and "std::string&" not in before[ids[-1].start():]:
                nm = re.search(re.escape(var) + r"\.name\s*=\s*" + STR, before[ids[-1].start():])
                add(key, ids[-1].group(1), nm.group(1) if nm else None, p, m.start())
            elif not re.search(r"\.shortcut\s*=\s*shortcut\b", s[m.start():m.end()]):
                unattributed.append(f"src/{rel(p)}:{line_of(s, m.start())} {var}.shortcut = \"{key}\"")
    # dedupe (the same call can match two rules)
    uniq, keys = [], set()
    for e in entries:
        k = (e.key, e.tool_id)
        if k not in keys:
            keys.add(k)
            uniq.append(e)
    return uniq, unattributed


CONTEXTS = [
    ("app", "Application and documents"),
    ("view", "View (3D viewport)"),
    ("sketch", "Sketch (while a sketch is open)"),
    ("part", "Part (SOLID toolbar)"),
    ("assembly", "Assembly"),
    ("drawing", "Drawing workspace"),
]

# Keys the router (src/app/ShortcutRouter.cpp) handles itself, before any tool shortcut. The
# token after each key is the Qt key name that must still appear in that file (checked).
ROUTER_KEYS = [
    ("S", "Key_S", "Command search (canvas, toolbar or panel header focused)"),
    ("Ctrl+K", "Key_K", "Command search from anywhere except a text field"),
    ("Esc", "Key_Escape", "Cancel the active command / clear the selection"),
    ("Enter", "Key_Return", "OK / apply the active command; repeat the last command when idle"),
    ("Space", "Key_Space", "Repeat the last command when no command runs"),
    ("Tab / Shift+Tab", "Key_Tab", "Next value box during a command (then into the command card)"),
    (":", "Key_Colon", "Focus the command line"),
]
# Keys the main window handles without a tool (src/app/MainWindow.cpp), checked the same way.
WINDOW_KEYS = [
    ("Ctrl+F", '"Ctrl+F"', "Search the FEATURES list (the find button in the title bar)"),
]


def fmt_key(k: str) -> str:
    k = {"Del": "Delete", "Ctrl+Return": "Ctrl+Enter"}.get(k, k)
    return "`" + k + "`"


def markdown(entries: list[Entry], src: Path) -> str:
    by_ctx: dict[str, list[Entry]] = defaultdict(list)
    for e in entries:
        prefix = e.tool_id.split(".", 1)[0]
        prefix = {"inspect": "part"}.get(prefix, prefix)
        by_ctx[prefix if prefix in dict(CONTEXTS) else "app"].append(e)
    order = {c: i for i, c in enumerate("ABCDEFGHIJKLMNOPQRSTUVWXYZ")}

    def sort_key(e: Entry):
        mods = e.key.count("+")
        return (mods, e.key.split("+")[0] if mods else "", order.get(e.key[-1:], 50), e.key)

    lines = ["<!-- generated by tools/docs/shortcuts.py; do not edit by hand -->", ""]
    for ctx, title in CONTEXTS:
        rows = sorted(by_ctx.get(ctx, []), key=sort_key)
        if not rows:
            continue
        lines += [f"**{title}**", "", "| Key | Command | Tool id |", "|---|---|---|"]
        lines += [f"| {fmt_key(e.key)} | {e.name} | `{e.tool_id}` |" for e in rows]
        lines.append("")
    router = (src / "app/ShortcutRouter.cpp").read_text(encoding="utf-8") if (src / "app/ShortcutRouter.cpp").exists() else ""
    lines += ["**Keys handled by the window itself** (`src/app/ShortcutRouter.cpp`, `src/app/MainWindow.cpp`)", "",
              "| Key | Action |", "|---|---|"]
    mw_path = src / "app/MainWindow.cpp"
    mw = mw_path.read_text(encoding="utf-8") if mw_path.exists() else ""
    for key, qt, what in ROUTER_KEYS:
        if qt in router:
            lines.append(f"| `{key}` | {what} |")
    for key, token, what in WINDOW_KEYS:
        if token in mw:
            lines.append(f"| `{key}` | {what} |")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--src", type=Path, default=ROOT / "src")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--update", type=Path, metavar="FILE")
    a = ap.parse_args()
    entries, unattributed = collect(a.src)
    table = markdown(entries, a.src)
    if a.update:
        text = a.update.read_text(encoding="utf-8")
        begin, end = "<!-- shortcuts:begin -->", "<!-- shortcuts:end -->"
        i, j = text.find(begin), text.find(end)
        if i < 0 or j < i:
            print(f"error: {a.update} has no {begin} ... {end} block", file=sys.stderr)
            return 2
        a.update.write_text(text[:i + len(begin)] + "\n" + table + "\n" + text[j:], encoding="utf-8")
    else:
        print(table)
    bad = 0
    for u in unattributed:
        print(f"warning: unattributed shortcut {u}", file=sys.stderr)
        bad += 1
    seen: dict[tuple[str, str], Entry] = {}
    for e in entries:
        k = (e.tool_id.split(".", 1)[0], e.key.lower())
        if k in seen and seen[k].tool_id != e.tool_id:
            print(f"warning: {e.key} is bound to {seen[k].tool_id} and {e.tool_id}", file=sys.stderr)
            bad += 1
        seen[k] = e
    return 1 if (a.check and bad) else 0


if __name__ == "__main__":
    sys.exit(main())
