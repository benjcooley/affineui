/*
 * Placeholder for a sokol_gfx-backed NanoVG painter. Phase 1 uses
 * NanoVG's native GL3 backend directly (see nanovg_impl.c); this
 * shim only becomes relevant when we want to drive NanoVG through
 * sokol_gfx so Metal and D3D11 backends are reachable.
 *
 * Until then this TU is intentionally empty — kept in the build so
 * the file path stays stable across phases.
 */
