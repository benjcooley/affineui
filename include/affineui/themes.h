#pragma once

#include <string_view>

namespace affineui::theme {

/// Bundled, tested stylesheets. Pass into `App::Config::stylesheet`
/// or `App::set_stylesheet()` to get a polished look with zero setup.

std::string_view ua_default();      ///< User-agent default. Always applied.
std::string_view material_dark();
std::string_view material_light();
std::string_view bootstrap_dark();
std::string_view bootstrap_light();

}  // namespace affineui::theme
