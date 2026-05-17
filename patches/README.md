# Upstream patches

Vendored third-party libraries under `external/` are pinned and
patched against known bugs. Lexbor fixes live as normal commits on the
sibling fork branch `affineui/v2.4.0`; the matching patch files here are
kept as a reviewable ledger and as a fallback for rebuilding from the
upstream tag.

Lexbor patches are applied in the sibling fork as normal commits.
Keep matching patch files here as a reviewable ledger and as a fallback
for rebuilding the fork from the upstream tag.

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
| `lexbor-selector-pseudo-specificity.patch` | lexbor | 2.4.0 | Counts simple pseudo-classes such as `:focus` in selector specificity. |

## When upstream lands the fix

Remove the patch commit from the sibling fork or rebase onto the
upstream fix, run `scripts/sync_lexbor_from_fork.sh`, and verify the
demo no longer crashes (in particular, `examples/04_imm_counter`
exercises the path).
