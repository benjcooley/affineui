# Vendored fonts

AffineUI embeds **Roboto** as its default UI font so text rendering is
identical across Windows, macOS, and Linux (no dependency on whatever font
the host OS happens to ship). Applications may register their own fonts on
top of, or instead of, the default.

- **Roboto** (Regular, Bold) — © Google, licensed under the
  **Apache License, Version 2.0**.
  <https://github.com/googlefonts/roboto> ·
  <https://www.apache.org/licenses/LICENSE-2.0>

The `.ttf` files here are the committed source of truth; the build embeds
them into the library via `cmake/bin2c.c` (generated headers are not
checked in). To opt out of the embedded default and use system fonts
instead, build with `-DAFFINEUI_NO_EMBEDDED_FONTS` (or define that macro).

TODO: vendor Roboto Italic + Bold-Italic so emphasized text is true italic
rather than the upright fallback.
