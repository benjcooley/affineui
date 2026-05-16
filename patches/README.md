# Upstream patches

Vendored third-party libraries under `external/` are pinned and
patched against known bugs. Each patch here is a unified diff
applied on top of the upstream tree, with a description of the
underlying bug and the commit (or version) it should be revisited
against.

`scripts/fetch_deps.sh` re-applies every `*.patch` here after pulling
fresh sources — drop a new patch in this directory and the next
fetch cycle picks it up automatically.

Each patch must:

- Have a descriptive filename: `<lib>-<short-issue>.patch`.
- Carry a top-of-file comment with: the bug, the trigger, the
  upstream issue link (if filed), and the version-tag the patch
  was developed against.
- Apply cleanly with `patch -p1` from the corresponding library
  root (e.g. `external/lexbor/`).

## Current patches

| File | Lib | Tag | What |
|---|---|---|---|
| `lexbor-html-event-destroy-null-list.patch` | lexbor | 2.4.0 | Guards a `el->list == NULL` deref in `lxb_html_document_event_destroy` that crashes when a cascade-matched element has no inline `style=""` attribute. |

## When upstream lands the fix

Remove the patch file, re-run `scripts/fetch_deps.sh`, verify the
demo no longer crashes (in particular, `examples/04_imm_counter`
exercises the path).
