# Contributing to Stacks IDE

Thanks for your interest! Stacks IDE welcomes PRs for bug fixes, new
language runners, UI polish, and platform support.

## Dev loop

```bash
./scripts/dev.sh
# open http://localhost:8000
```

Any change under `web/` is live on refresh. Changes under `core/` require
a rebuild (`cmake --build core/build -j`).

## Code style

- C++: C++17, 4-space indent, `snake_case` for functions, `CamelCase` for
  types, one header per compilation unit under `core/include/stacks/`.
- JS: modern ESM, 2-space indent, single quotes for strings, no
  semicolons optional (follow existing style in each file).
- CSS: Fluent design tokens live in `web/style.css` — prefer extending
  tokens over adding hard-coded colors.

## Adding a new language

1. Add a branch in `build_command()` in `core/src/runner.cpp`.
2. Add it to the `<select id="runLang">` in `web/index.html` and the
   `pickLanguage()` heuristic in `web/app.js`.
3. Update the README language list.

## Commit messages

Conventional Commits preferred: `feat: …`, `fix: …`, `docs: …`, `ui: …`.
