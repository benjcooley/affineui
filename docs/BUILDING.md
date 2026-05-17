# Building

## Prerequisites

| | macOS | Linux | Windows |
|---|---|---|---|
| Compiler | Apple Clang 14+ | GCC 11+ / Clang 13+ | MSVC 2022+ / Clang 13+ |
| CMake | 3.20+ | 3.20+ | 3.20+ |
| Build tool | Ninja (recommended) | Ninja / Make | Ninja / VS / MSBuild |
| GPU API | Metal (built in) | OpenGL 3.3+ | D3D11 (built in) |

Linux additionally needs: `libx11-dev`, `libxi-dev`, `libxcursor-dev`,
`libgl1-mesa-dev`, `libasound2-dev` for sokol_app's X11 backend.

```bash
# Debian / Ubuntu
sudo apt install build-essential cmake ninja-build \
    libx11-dev libxi-dev libxcursor-dev libgl1-mesa-dev libasound2-dev

# Fedora
sudo dnf install gcc-c++ cmake ninja-build \
    libX11-devel libXi-devel libXcursor-devel mesa-libGL-devel alsa-lib-devel
```

## Vendored deps

The curated dependency sources are checked in under `external/`. A
normal clone can configure, build, and generate the two-file
distribution without fetching upstream projects.

## Configure & build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Useful options (pass as `-DAFFINEUI_<NAME>=ON`):

| Option | Default | Effect |
|---|---|---|
| `AFFINEUI_BUILD_EXAMPLES` | ON if top-level | Build everything under `examples/` |
| `AFFINEUI_BUILD_TESTS` | ON if top-level | Build the doctest test binary |
| `AFFINEUI_BUILD_TOOLS` | ON if top-level | Build `ui-preview` and `benchmark` |
| `AFFINEUI_BUILD_SHARED` | OFF | Build as shared library |
| `AFFINEUI_ENABLE_IMM` | ON | Compile the immediate-mode layer |
| `AFFINEUI_ENABLE_C_API` | ON | Compile the thin `extern "C"` surface |
| `AFFINEUI_WARNINGS_AS_ERRORS` | OFF | `-Werror` / `/WX` |
| `AFFINEUI_BACKEND` | auto | `metal`, `d3d11`, `gl`, `wgpu` |

Detected automatically per platform when `auto`: Metal on macOS, D3D11
on Windows, GL on Linux. WebGPU is opt-in.

## Running examples

```bash
./build/examples/00_hello/hello
./build/examples/01_bootstrap/bootstrap_demo
./build/examples/04_imm_counter/imm_counter
```

## Running tests

```bash
ctest --test-dir build --output-on-failure
```

Or run the binary directly for fast filtered iteration:

```bash
./build/tests/affineui_tests --test-case="CSS parser*"
```

## Using AffineUI from another CMake project

Easiest: `add_subdirectory`.

```cmake
add_subdirectory(third_party/affineui)
target_link_libraries(my_app PRIVATE affineui::affineui)
```

Or, after `cmake --install build --prefix path/to/install`:

```cmake
find_package(affineui CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE affineui::affineui)
```

## Building a minimal embed (no examples / no tests)

```bash
cmake -S . -B build -G Ninja \
    -DAFFINEUI_BUILD_EXAMPLES=OFF \
    -DAFFINEUI_BUILD_TESTS=OFF \
    -DAFFINEUI_BUILD_TOOLS=OFF \
    -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build build
```

## Troubleshooting

**`fatal error: 'sokol_app.h' file not found`** — the checked-in
`external/` source inventory is missing or your include paths do not
point at it.

**Linux: undefined references to `XOpenDisplay`/`glXCreateContext`** —
install the X11 + OpenGL dev packages listed above.

**macOS: `clang: error: linker command failed`** — verify Xcode CLT is
installed (`xcode-select --install`). AffineUI links Metal, Cocoa,
QuartzCore, AudioToolbox automatically.

**Windows MSVC: `C2059: syntax error: 'constant'`** — usually means
`min`/`max` macros from `<windows.h>`. AffineUI defines `NOMINMAX`
before including Windows headers; if you include Windows.h yourself
first, define `NOMINMAX` upstream.
