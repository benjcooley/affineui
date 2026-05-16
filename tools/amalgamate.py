#!/usr/bin/env python3
"""
Amalgamator: produce the two-file distribution of AffineUI.

Output:
    <out>/affineui.h    — declarations, types, inline templates
    <out>/affineui.cpp  — all implementation, including vendored deps

End-users copy these two files into their project. No CMake required
for AffineUI itself — they still link lexbor and Yoga separately and
add `external/` to the include path so `sokol_app.h`, `nanovg.h`, and
the stb headers resolve when `affineui.cpp` is compiled.

Strategy (sqlite-amalgamation style):
  1. Concatenate the public headers in topological order into
     ``affineui.h``. Strip ``#pragma once`` and internal ``#include``
     directives; dedupe top-level ``#include <stdlib>`` directives to
     a single banner block. Wrap the result in one outer include
     guard.
  2. Concatenate the internal headers, then our C++ implementation
     translation units, then the vendored-C wrappers, into
     ``affineui.cpp``. Strip internal includes (their content is
     already inlined); leave vendored ``#include "name.h"`` and any
     indented ``#include <…>`` (conditional on a platform ``#if``)
     where they sit. Wrap the C wrappers in a single ``extern "C"``
     block so their symbols keep C linkage when compiled as C++.

The output is deterministic: the same input tree always produces
byte-identical files. A CI smoke test compiles the amalgamation to
catch drift between the modular and amalgamated builds.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

# ── Source manifest ──────────────────────────────────────────────────────────
# Ordering matters: each list is consumed top-to-bottom and the result is
# concatenated into a single TU. Within each list, files must appear in
# dependency order — a header that references a type from another header
# must come *after* that header. Implementation files don't need to follow
# dependency order (all declarations are visible via the inlined headers),
# but engine/dom files are kept ahead of c_api so the C ABI sees the
# definitions it forwards to.

PUBLIC_HEADERS = [
    "include/affineui/version.h",
    "include/affineui/types.h",
    "include/affineui/themes.h",
    "include/affineui/geom.h",
    "include/affineui/style.h",
    "include/affineui/painter.h",
    "include/affineui/computed_style.h",
    "include/affineui/display_list.h",
    "include/affineui/document.h",
    "include/affineui/app.h",
    "include/affineui/imm.h",
    "include/affineui/affineui.h",   # umbrella last
]

# Implementation-side headers. They never appear in ``affineui.h``; they
# inline into the top of ``affineui.cpp`` so the implementation TUs that
# follow can see them.
INTERNAL_HEADERS = [
    "src/internal/element_id.h",
    "src/internal/animated_style.h",
    "src/internal/computed_style.h",
    "src/internal/layer.h",
    "src/internal/compositor.h",
    "src/internal/paint_internal.h",
    "src/internal/display_list.h",
    "src/internal/display_list_painter.h",
    "src/internal/style_resolver.h",
    "src/internal/style_store.h",
    "src/engine/box.h",
    "src/engine/layer.h",
    "src/engine/animation.h",
    "src/engine/compositor.h",
    "src/engine/restyle_queue.h",
    "src/engine/style_engine.h",
    "src/engine/layout_engine.h",
    "src/engine/paint_engine.h",
    "src/engine/rasterizer.h",
    "src/engine/engine.h",
    "src/layout/yoga_adapter.h",
]

# Our C++ TUs. Order here is not load-bearing (declarations come from the
# inlined headers above), but we keep a stable order so diffs of the
# generated file are readable.
ENGINE_SOURCES = [
    "src/themes.cpp",
    "src/affineui.cpp",
    "src/app/app.cpp",
    "src/app/event.cpp",
    "src/dom/document.cpp",
    "src/dom/dom_view.cpp",
    "src/dom/lexbor_bridge.cpp",
    "src/style/cascade.cpp",
    "src/style/computed.cpp",
    "src/style/selector_bridge.cpp",
    "src/style/style_store.cpp",
    "src/style/units.cpp",
    "src/layout/box.cpp",
    "src/layout/block.cpp",
    "src/layout/inline.cpp",
    "src/layout/flex.cpp",
    "src/layout/yoga_adapter.cpp",
    "src/text/shaper.cpp",
    "src/text/line_breaker.cpp",
    "src/text/font_manager.cpp",
    "src/paint/paint_driver.cpp",
    "src/paint/image_loader.cpp",
    "src/paint/layer.cpp",
    "src/paint/composite.cpp",
    "src/paint/nanovg_painter.cpp",
    "src/engine/box.cpp",
    "src/engine/layer.cpp",
    "src/engine/display_list.cpp",
    "src/engine/restyle_queue.cpp",
    "src/engine/animation.cpp",
    "src/engine/style_engine.cpp",
    "src/engine/layout_engine.cpp",
    "src/engine/paint_engine.cpp",
    "src/engine/rasterizer.cpp",
    "src/engine/compositor.cpp",
    "src/engine/engine.cpp",
    "src/imm/imm.cpp",
    "src/imm/reconciler.cpp",
    "src/imm/state_store.cpp",
    "src/c_api.cpp",
]

# Vendored-C wrapper TUs (one file each, ~50 LOC, mostly preprocessor
# stitching to bring in a single-header dep). They go inside one
# `extern "C" { ... }` block at the tail of ``affineui.cpp`` so their
# symbols keep C linkage when the file is compiled as C++. Order
# matters: stb_truetype must be in scope before fontstash uses it, and
# nanovg core must be in scope before the GL backend.
VENDORED_C_TUS = [
    "src/text/stb_impl.c",
    "src/text/fontstash_impl.c",
    "src/paint/nanovg_impl.c",
    "src/paint/nanovg_sokol.c",
    "src/paint/sokol_impl.c",
]

# Public headers always live under ``include/affineui/``; internal/engine
# headers under ``src/internal/``, ``src/engine/``, and a single
# ``src/layout/yoga_adapter.h``. An ``#include "<prefix>…"`` matching one
# of these prefixes refers to a file that has already been inlined and
# must be stripped from its referring TU.
INTERNAL_PREFIXES = ("affineui/", "internal/", "engine/", "layout/")

# ── Regexes ──────────────────────────────────────────────────────────────────
# We match preprocessor directives only when ``#`` is directly followed by
# ``include`` / ``pragma`` — i.e. no whitespace between them. An indented
# ``#    include`` (the pattern used inside `#if defined(__APPLE__)` guards
# in the vendored-C wrappers) intentionally falls through so it stays where
# it is — moving a conditional include to the file's top banner would
# unconditionally pull in a platform header.

PRAGMA_ONCE_RE      = re.compile(r'^#pragma\s+once\s*(?://.*|/\*.*)?$')

# System includes are deduped to the top banner only when they sit at
# column zero — an indented `#    include <foo>` inside an `#if` guard
# must stay where it is.
SYSTEM_INCLUDE_RE   = re.compile(r'^#include\s*<([^>]+)>\s*(?://.*|/\*.*)?$')

# Quoted-include match is whitespace-tolerant: a conditional like
#   `#    include "affineui/imm.h"` inside an `#ifndef AFFINEUI_NO_IMM`
# guard still refers to a file that's been inlined above and must be
# dropped. We also allow a trailing line comment so
#   `#include "engine/box.h"  // LayerId`
# still parses as an include. We decide whether to strip by the path
# prefix, not by column.
QUOTED_INCLUDE_RE   = re.compile(r'^\s*#\s*include\s*"([^"]+)"\s*(?://.*|/\*.*)?$')


def is_internal_include(name: str) -> bool:
    return any(name.startswith(p) for p in INTERNAL_PREFIXES)


# ── Processing ───────────────────────────────────────────────────────────────

@dataclass
class FileBlock:
    rel_path: str
    body: str                              # transformed file content
    system_includes: list[str] = field(default_factory=list)


def process_file(root: Path, rel_path: str) -> FileBlock:
    """
    Read a file, strip ``#pragma once`` and inlined-internal ``#include``s,
    capture top-level ``#include <…>`` directives, and return the rest.

    Indented system includes (inside ``#if`` conditionals) are left intact:
    relocating them to the top would change their conditionality.
    """
    text = (root / rel_path).read_text(encoding="utf-8")
    out_lines: list[str] = []
    syshdr_order: list[str] = []
    seen_sys: set[str] = set()

    for line in text.splitlines():
        if PRAGMA_ONCE_RE.match(line):
            continue
        m = SYSTEM_INCLUDE_RE.match(line)
        if m:
            name = m.group(1)
            if name not in seen_sys:
                seen_sys.add(name)
                syshdr_order.append(name)
            continue
        m = QUOTED_INCLUDE_RE.match(line)
        if m and is_internal_include(m.group(1)):
            # Already inlined upstream in the amalgamation.
            continue
        out_lines.append(line)

    # Trim trailing blank lines so concatenated blocks don't accumulate gaps.
    while out_lines and not out_lines[-1].strip():
        out_lines.pop()

    return FileBlock(
        rel_path=rel_path,
        body="\n".join(out_lines) + "\n",
        system_includes=syshdr_order,
    )


def banner(version: str, kind: str) -> str:
    return (
        "// SPDX-License-Identifier: MIT\n"
        "//\n"
        f"// AffineUI {version} — two-file amalgamated distribution ({kind})\n"
        "//\n"
        "// This file is GENERATED by tools/amalgamate.py. Edit the modular\n"
        "// sources under include/ and src/ instead; re-running the\n"
        "// amalgamator regenerates this artifact.\n"
        "//\n"
        "// Bundled third-party components retain their licenses:\n"
        "//   - sokol        zlib       https://github.com/floooh/sokol\n"
        "//   - nanovg       zlib       https://github.com/memononen/nanovg\n"
        "//   - lexbor       Apache-2.0 https://github.com/lexbor/lexbor\n"
        "//   - yoga         MIT        https://github.com/facebook/yoga\n"
        "//   - stb_*        MIT / PD   https://github.com/nothings/stb\n"
        "//   - fontstash    zlib       https://github.com/memononen/fontstash\n"
        "//\n"
        "// lexbor and Yoga are too large to inline as a single TU and must\n"
        "// be linked as separate libraries. Single-header deps (sokol,\n"
        "// nanovg, stb_*, fontstash) are pulled in by the vendored-C\n"
        "// wrappers at the bottom of affineui.cpp — keep ``external/`` on\n"
        "// the include path when building this amalgamation.\n"
    )


def read_version(root: Path) -> str:
    cml = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    for line in cml.splitlines():
        line = line.strip()
        if line.startswith("VERSION ") and "project(" not in line:
            return line.split()[1]
    return "0.0.0"


def render_block(block: FileBlock) -> str:
    rule = "─" * 72
    header = (
        f"// {rule}\n"
        f"// {block.rel_path}\n"
        f"// {rule}\n"
    )
    return header + block.body + "\n"


def emit_header(root: Path, out_path: Path, version: str) -> int:
    blocks = [process_file(root, p) for p in PUBLIC_HEADERS]

    sys_seen: set[str] = set()
    sys_order: list[str] = []
    for b in blocks:
        for s in b.system_includes:
            if s not in sys_seen:
                sys_seen.add(s)
                sys_order.append(s)

    parts: list[str] = [
        banner(version, "public header"),
        "\n",
        "#ifndef AFFINEUI_AMALGAMATED_H\n",
        "#define AFFINEUI_AMALGAMATED_H\n",
        "\n",
    ]
    for s in sys_order:
        parts.append(f"#include <{s}>\n")
    parts.append("\n")
    for b in blocks:
        parts.append(render_block(b))
    parts.append("#endif  // AFFINEUI_AMALGAMATED_H\n")

    text = "".join(parts)
    out_path.write_text(text, encoding="utf-8")
    return len(text)


def emit_impl(root: Path, out_path: Path, version: str) -> int:
    header_blocks = [process_file(root, p) for p in INTERNAL_HEADERS]
    source_blocks = [process_file(root, p) for p in ENGINE_SOURCES]
    cwrap_blocks  = [process_file(root, p) for p in VENDORED_C_TUS]

    sys_seen: set[str] = set()
    sys_order: list[str] = []
    for b in (*header_blocks, *source_blocks, *cwrap_blocks):
        for s in b.system_includes:
            if s not in sys_seen:
                sys_seen.add(s)
                sys_order.append(s)

    parts: list[str] = [
        banner(version, "implementation"),
        "\n",
        '#include "affineui.h"\n',
        "\n",
    ]
    for s in sys_order:
        parts.append(f"#include <{s}>\n")
    parts.append("\n")

    # ── Vendored-symbol shielding ─────────────────────────────────────────
    # The whole amalgamation is one TU, so every vendored single-header dep
    # we bundle — sokol, stb_image, stb_truetype, fontstash — can be
    # compiled with internal linkage using each library's upstream
    # static-prefix macro. The matching `AFFINEUI_HOST_PROVIDES_*` flag
    # disables the override so a host that brings its own dep keeps the
    # symbols externally visible and ours stays out of the way.
    #
    # NanoVG has no equivalent macro upstream; its `nvg*` symbols stay
    # externally linked. Set AFFINEUI_HOST_PROVIDES_NANOVG to disable our
    # copy if the host also links NanoVG.
    parts.append(
        "// ─── vendored-symbol shielding (TU-local linkage) ───────────────────────\n"
        "// Each define is honored by the matching upstream header; the\n"
        "// AFFINEUI_HOST_PROVIDES_* guard lets a host swap in its own copy.\n"
        "#ifndef AFFINEUI_HOST_PROVIDES_SOKOL\n"
        "#  define SOKOL_API_DECL static\n"
        "#  define SOKOL_API_IMPL static\n"
        "#endif\n"
        "#ifndef AFFINEUI_HOST_PROVIDES_STB_IMAGE\n"
        "#  define STB_IMAGE_STATIC\n"
        "#endif\n"
        "#ifndef AFFINEUI_HOST_PROVIDES_STB_TRUETYPE\n"
        "#  define STB_TRUETYPE_STATIC\n"
        "#endif\n"
        "#ifndef AFFINEUI_HOST_PROVIDES_FONTSTASH\n"
        "#  define FONS_STATIC\n"
        "#endif\n"
        "\n"
    )

    parts.append("// ─── internal headers ───────────────────────────────────────────────────\n\n")
    for b in header_blocks:
        parts.append(render_block(b))

    parts.append("// ─── C++ implementation ─────────────────────────────────────────────────\n\n")
    for b in source_blocks:
        parts.append(render_block(b))

    parts.append("// ─── vendored-C wrappers (C linkage) ────────────────────────────────────\n\n")
    parts.append('extern "C" {\n\n')
    for b in cwrap_blocks:
        parts.append(render_block(b))
    parts.append('}  // extern "C"\n')

    text = "".join(parts)
    out_path.write_text(text, encoding="utf-8")
    return len(text)


def emit_manifest(root: Path) -> None:
    def report(label: str, files: list[str]) -> None:
        print(f"  {label}:")
        for p in files:
            present = (root / p).exists()
            print(f"    [{'x' if present else ' '}] {p}")
        print()

    print("== AffineUI amalgamator manifest ==\n")
    report("Public headers (affineui.h)",          PUBLIC_HEADERS)
    report("Internal headers (affineui.cpp head)", INTERNAL_HEADERS)
    report("Engine sources (affineui.cpp body)",   ENGINE_SOURCES)
    report("Vendored-C wrappers (affineui.cpp tail)", VENDORED_C_TUS)


def verify_inputs(root: Path) -> list[str]:
    missing: list[str] = []
    for group in (PUBLIC_HEADERS, INTERNAL_HEADERS, ENGINE_SOURCES, VENDORED_C_TUS):
        for p in group:
            if not (root / p).exists():
                missing.append(p)
    return missing


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    ap.add_argument("--root", required=True, help="repo root")
    ap.add_argument("--out",  required=True, help="output directory")
    ap.add_argument("--manifest-only", action="store_true",
                    help="print the input manifest and exit (no files written)")
    args = ap.parse_args()

    root = Path(args.root).resolve()
    out  = Path(args.out).resolve()

    if args.manifest_only:
        emit_manifest(root)
        return 0

    missing = verify_inputs(root)
    if missing:
        print("amalgamate: missing input files:", file=sys.stderr)
        for p in missing:
            print(f"  - {p}", file=sys.stderr)
        return 1

    out.mkdir(parents=True, exist_ok=True)
    version = read_version(root)

    h_path   = out / "affineui.h"
    cpp_path = out / "affineui.cpp"
    h_size   = emit_header(root, h_path,   version)
    cpp_size = emit_impl  (root, cpp_path, version)

    print(f"wrote {h_path}   ({h_size:>9,} bytes)")
    print(f"wrote {cpp_path} ({cpp_size:>9,} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
