#pragma once

// Tiny internal log sink. Embedders install a LogFn via InitDesc; until then
// messages go to stderr in debug builds and are dropped otherwise. Used in
// place of bare fprintf(stderr, ...) so a host can capture our diagnostics.

#include "affineui/embed.h"

namespace affineui::detail {

void set_log_sink(LogFn fn, void* user) noexcept;
void log_msg(LogLevel level, const char* msg) noexcept;

}  // namespace affineui::detail
