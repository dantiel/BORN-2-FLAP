# BORN2FLAP — Documentation Website

The public website for **BORN2FLAP**, the ornithopter flight simulator.

> *Written in the æther by ferocious hearts.*

## Stack

Mirrors the [PteronautOS docs](https://github.com/dantiel/PteronautOS/tree/master/docs) toolchain:

- **HAML** — page templates (`index.haml`, `*/index.haml`)
- **Sass** (indented, dart-sass) — styling (`assets/css/style.sass`)
- **Markdown** — a zero-dependency subset renderer (`markdown_mini.rb`)
- **i18n** — one JSON per language in `_lang/`, English is canonical and other languages fall back to it

### Design language

"Drunken grass script (草书) written in the æther" — ink-black aether, a cinnabar seal-stroke
breaking the technical grid, martial-art precision wrapped in bird-like handwriting.

- **Liu Jian Mao Cao** — the 草书 brush wordmark / watermarks
- **Chakra Petch** — headings, nav (technical, martial)
- **Spectral** — body (elegant, handwriting italics)
- **Space Mono** — specs, code, telemetry

## Build

```sh
cd web
bundle install        # haml + sass-embedded + rake + webrick
bundle exec rake build   # haml + sass + pages
```

### Tasks

| Task | Purpose |
|---|---|
| `bundle exec rake build` | Build all HTML + CSS |
| `bundle exec rake rebuild` | Clean + build |
| `bundle exec rake watch` | Rebuild on change |
| `bundle exec rake server` | Serve at http://localhost:8000 |

## i18n

`LANGS = en de es fr zh ja ko ru`. English is the default and lives at the root
(`index.html`); the other languages live in `{lang}/`. Each language JSON only needs
the keys it overrides — anything missing falls back to English automatically.

To add a language:

1. Create `_lang/{lang}.json` with `lang`, `dir`, `asset_prefix: ".."`, and the
   `lang_href_*` / `lang_class_*` link map (copy from `de.json` and adapt).
2. Add `{lang}` to `LANGS` and `SUBFOLDERS` in `Rakefile`.
3. Add the footer link in `index.haml` and the sub-page footers.

## Publishing (GitHub Pages)

The built `.html` files and `assets/` are committed. Point GitHub Pages at the
repository root (or deploy the `web/` folder). `.nojekyll` is present so Pages
serves the files verbatim.
