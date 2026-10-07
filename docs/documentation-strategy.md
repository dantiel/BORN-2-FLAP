# Documentation strategy

## Auto-generate vs hand-write — decision

The code documentation uses a **hybrid** approach:

1. **Auto-generated symbol reference** (the "coverage" layer) — `Tools/gen_code_reference.py` walks the four source trees (Haskell, C/C++, Ruby, Python) and emits `web/_code_ref/*.md` with every file and symbol plus line numbers. This guarantees nothing drifts from the source and every file is discoverable.

2. **Hand-written narrative guides** — `web/documentation/code/*.md` explain the most important files and their prominent functions, the architecture, and the modding map. This is the human understanding that no generator can produce.

## Why not full doc tools (Haddock / Doxygen / RDoc / coverage.py)?

- "Coverage" tools measure *test* coverage, not documentation coverage — they don't answer "is this file documented".
- Full API generators (Haddock/Doxygen/YARD) each need a per-language install and a build step, and they emit heavy, hard-to-theme output that doesn't fit the static Haml site.
- A single stdlib-only Python scanner covers all four languages uniformly, is reproducible from the repo, and emits markdown the site already renders.

## Regeneration

    python Tools/gen_code_reference.py

Run it after structural changes (new files, renamed symbols). The website's `rake build` task (`:coderef`) renders the markdown sources into the static pages.
