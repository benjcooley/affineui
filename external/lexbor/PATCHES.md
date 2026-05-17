# AffineUI Lexbor Patch Stack

This branch carries the small Lexbor fixes AffineUI needs on top of
upstream `v2.4.0`. Keep patches focused, tested, and upstreamable.

## Patches

### html: guard style teardown when destroying cascade-matched nodes

`lxb_html_document_event_destroy` can fall through from the `el->style`
cleanup path to the inline-style list teardown even when `el->list` is
null. AffineUI hits this when imm-mode destroys cascade-matched nodes
that do not have inline `style=""` attributes.

### css/selectors: count simple pseudo-classes in specificity

Lexbor v2.4.0 parses simple pseudo-classes such as `:hover`,
`:active`, `:focus`, and `:first-child`, but did not increment the
selector specificity `b` bucket. AffineUI depends on browser cascade
ordering where `.btn:focus` outranks `.btn`.

## Syncing Into AffineUI

From the AffineUI repo:

```sh
scripts/sync_lexbor_from_fork.sh
```

That copies this checkout into `affineui/external/lexbor`, excluding
`.git` and build artifacts.
