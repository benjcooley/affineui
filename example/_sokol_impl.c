/* sokol implementation TU for the NanoVG sokol_gfx demo runner.
 * The active backend (SOKOL_D3D11 / SOKOL_GLCORE / ...) is set by the
 * compiler (-D). We provide our own main() (SOKOL_NO_ENTRY). */
#define SOKOL_IMPL
#define SOKOL_NO_ENTRY
#include "sokol_log.h"
#include "sokol_gfx.h"
#include "sokol_app.h"
#include "sokol_glue.h"
#include "sokol_time.h"
