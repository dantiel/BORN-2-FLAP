#!/usr/bin/env python3
"""gen_code_reference.py — auto-generate a symbol-level code reference.

Scans the four source trees (Haskell MathCore, Native C/C++, Unreal C++,
Brain Ruby, plus Tools Python) and emits markdown symbol indexes into
web/_code_ref/. Zero dependencies beyond the Python standard library —
this is the "coverage" layer that guarantees every file/symbol in the repo
is discoverable and cannot drift from the source.

Regenerate after structural changes:

    python Tools/gen_code_reference.py

The website renders these files through the Rakefile (see web/Rakefile,
CODE_PAGES). Do not edit the _code_ref/*.md output by hand.
"""

import os
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "web" / "_code_ref"

# --------------------------------------------------------------------------
# Language scanners — each returns a list of (file, [ (symbol, kind, line) ])
# --------------------------------------------------------------------------

HS_SIG = re.compile(r"^([a-z][A-Za-z0-9_']*)\s*::")
HS_DATA = re.compile(r"^\s*(data|newtype|type|class)\s+(?:family\s+)?([A-Z][A-Za-z0-9_']*)")
HS_MODULE = re.compile(r"^\s*module\s+([A-Za-z0-9_.']+)")

def scan_haskell(root):
    result = []
    for p in sorted((root / "src").rglob("*.hs")):
        syms = []
        for i, line in enumerate(p.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            s = line.rstrip()
            m = HS_MODULE.match(s)
            if m:
                syms.append((m.group(1), "module", i))
                continue
            m = HS_DATA.match(s)
            if m:
                syms.append((m.group(2), m.group(1), i))
                continue
            m = HS_SIG.match(s)
            if m:
                syms.append((m.group(1), "sig", i))
        if syms:
            result.append((str(p.relative_to(root)), syms))
    return result


CPP_CLASS = re.compile(r"^\s*(?:class|struct)\s+(?:[A-Za-z_][A-Za-z0-9_:]*\s+)?([A-Z][A-Za-z0-9_]*)")
CPP_ENUM = re.compile(r"^\s*enum\s+(?:class\s+)?([A-Z][A-Za-z0-9_]*)")
CPP_METHOD = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)::([A-Za-z_~][A-Za-z0-9_]*)\s*\(")
CPP_DECL = re.compile(
    r"^[ \t]*(?:virtual\s+|static\s+|inline\s+|explicit\s+|constexpr\s+)*"
    r"(?:[\w:<>*&,\[\] ]+\s+)+?([A-Za-z_][A-Za-z0-9_]*)\s*\([^;]*\)\s*"
    r"(?:const\s*)?(?:override\s*)?(?:final\s*)?;"
)

def scan_cpp(root, exts):
    result = []
    files = []
    for ext in exts:
        files += list(root.rglob(ext))
    for p in sorted(files, key=lambda f: str(f).lower()):
        syms = []
        for i, line in enumerate(p.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            s = line.rstrip()
            m = CPP_CLASS.match(s)
            if m:
                syms.append((m.group(1), "class", i))
                continue
            m = CPP_ENUM.match(s)
            if m:
                syms.append((m.group(1), "enum", i))
                continue
            m = CPP_METHOD.search(s)
            if m:
                syms.append((f"{m.group(1)}::{m.group(2)}", "method", i))
                continue
            m = CPP_DECL.match(s)
            if m:
                syms.append((m.group(1), "decl", i))
        if syms:
            result.append((str(p.relative_to(root)), syms))
    return result


RB_CLASS = re.compile(r"^\s*(?:class|module)\s+([A-Z][A-Za-z0-9_:]*)")
RB_DEF = re.compile(r"^\s*def\s+([a-z_][A-Za-z0-9_!?=]*|\bself\.[A-Za-z_][A-Za-z0-9_!?=]*)\b")

def scan_ruby(root):
    result = []
    for p in sorted(root.rglob("*.rb")):
        syms = []
        for i, line in enumerate(p.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            s = line.rstrip()
            m = RB_CLASS.match(s)
            if m:
                syms.append((m.group(1), "class", i))
                continue
            m = RB_DEF.match(s)
            if m:
                syms.append((m.group(1), "def", i))
        if syms:
            result.append((str(p.relative_to(root)), syms))
    return result


PY_CLASS = re.compile(r"^\s*class\s+(\w+)")
PY_DEF = re.compile(r"^\s*(?:async\s+)?def\s+(\w+)")

def scan_python(root):
    result = []
    for p in sorted(root.rglob("*.py")):
        syms = []
        for i, line in enumerate(p.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            s = line.rstrip()
            m = PY_CLASS.match(s)
            if m:
                syms.append((m.group(1), "class", i))
                continue
            m = PY_DEF.match(s)
            if m:
                syms.append((m.group(1), "def", i))
        if syms:
            result.append((str(p.relative_to(root)), syms))
    return result


LANGS = [
    ("mathcore", "Haskell MathCore", "the canonical physics core",
     scan_haskell, ROOT / "MathCore"),
    ("native", "Native C/C++", "the C ABI and Windows/R C shims",
     lambda r: scan_cpp(r, ["*.h", "*.cpp"]), ROOT / "Native"),
    ("unreal", "Unreal Engine C++", "the UE5 game, pawn, UI and audio",
     lambda r: scan_cpp(r, ["*.h", "*.cpp"]), ROOT / "Unreal" / "Born2Flap" / "Source"),
    ("brain", "Brain (Ruby)", "the UMGHAML view framework and router",
     scan_ruby, ROOT / "Brain" / "lib"),
    ("tools", "Tools (Python)", "import/material/asset generation scripts",
     scan_python, ROOT / "Tools"),
]


def render_lang_page(key, title, blurb, entries):
    lines = []
    lines.append(f"# {title} — symbol reference")
    lines.append("")
    lines.append(blurb)
    lines.append("")
    lines.append("> Auto-generated by `Tools/gen_code_reference.py` — do not edit by hand.")
    lines.append("")
    n_files = len(entries)
    n_syms = sum(len(s) for _, s in entries)
    lines.append(f"**{n_files} files · {n_syms} symbols**")
    lines.append("")
    for path, syms in entries:
        lines.append(f"## `{path}`")
        lines.append("")
        for sym, kind, ln in syms:
            lines.append(f"- `{sym}` — *{kind}* (line {ln})")
        lines.append("")
    return "\n".join(lines)


def main():
    OUT.mkdir(parents=True, exist_ok=True)

    all_files = []
    for key, title, blurb, fn, root in LANGS:
        entries = fn(root)
        all_files.append((key, title, blurb, entries))
        page = render_lang_page(key, title, blurb, entries)
        (OUT / f"{key}.md").write_text(page, encoding="utf-8")
        print(f"wrote {key}.md  ({len(entries)} files, {sum(len(s) for _, s in entries)} symbols)")

    # Master API index
    lines = ["# Full API index", "",
             "Every source file and symbol in the repository, grouped by subsystem.",
             "",
             "> Auto-generated by `Tools/gen_code_reference.py`.",
             ""]
    total_f = total_s = 0
    for key, title, blurb, entries in all_files:
        total_f += len(entries)
        total_s += sum(len(s) for _, s in entries)
        lines.append(f"## {title}")
        lines.append("")
        for path, syms in entries:
            lines.append(f"- [`{path}`]({key}/#{_anchor(path)}) — {len(syms)} symbols")
        lines.append("")
    lines.append(f"**Totals: {total_f} files · {total_s} symbols**")
    (OUT / "api.md").write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote api.md  ({total_f} files, {total_s} symbols total)")


def _anchor(path):
    return path.lower().replace("/", "").replace("\\", "").replace(".", "-").replace("_", "-")


if __name__ == "__main__":
    main()
