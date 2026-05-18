// media — image, background-image, and border-image coverage.

#include <affineui/affineui.h>

#include <sokol_log.h>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view kCheckerPng =
    "iVBORw0KGgoAAAANSUhEUgAAAEAAAAAwCAYAAAChS3wfAAAAaklEQVR42u3YsREAIAgDQDs3cVIXczLcAGvhC8oUfJXLWDsiuzgzvd/zAwAAAAAAAGgMUP3BVx4AAAAAAADoDKAJAgAAAAAAAPYATRAAAAAAAACwB2iCAAAAAAAAgD1AEwQAAAAAAADKA1wsa5XhjvOmdQAAAABJRU5ErkJggg==";
constexpr std::string_view kPanelPng =
    "iVBORw0KGgoAAAANSUhEUgAAADAAAAAwCAYAAABXAvmHAAAAa0lEQVR42u3YMQ2AQBBE0TOAB0wgATvYQdN5QAiQENAwXLZ7xa93Xztt2p43ae57aceyRjUAAAAAAAAAgAFAeuC87tLSfwAAAAAAAAAAAAAAAAAAAAAA/gHSIal62EqHNgAAAAAAAACAAcAH2kRw+D+W3MUAAAAASUVORK5CYII=";
constexpr std::string_view kPhotoJpg =
    "/9j/4AAQSkZJRgABAQAASABIAAD/4QBMRXhpZgAATU0AKgAAAAgAAYdpAAQAAAABAAAAGgAAAAAAA6ABAAMAAAABAAEAAKACAAQAAAABAAAAYKADAAQAAAABAAAAQAAAAAD/7QA4UGhvdG9zaG9wIDMuMAA4QklNBAQAAAAAAAA4QklNBCUAAAAAABDUHYzZjwCyBOmACZjs+EJ+/8AAEQgAQABgAwEiAAIRAQMRAf/EAB8AAAEFAQEBAQEBAAAAAAAAAAABAgMEBQYHCAkKC//EALUQAAIBAwMCBAMFBQQEAAABfQECAwAEEQUSITFBBhNRYQcicRQygZGhCCNCscEVUtHwJDNicoIJChYXGBkaJSYnKCkqNDU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGVmZ2hpanN0dXZ3eHl6g4SFhoeIiYqSk5SVlpeYmZqio6Slpqeoqaqys7S1tre4ubrCw8TFxsfIycrS09TV1tfY2drh4uPk5ebn6Onq8fLz9PX29/j5+v/EAB8BAAMBAQEBAQEBAQEAAAAAAAABAgMEBQYHCAkKC//EALURAAIBAgQEAwQHBQQEAAECdwABAgMRBAUhMQYSQVEHYXETIjKBCBRCkaGxwQkjM1LwFWJy0QoWJDThJfEXGBkaJicoKSo1Njc4OTpDREVGR0hJSlNUVVZXWFlaY2RlZmdoaWpzdHV2d3h5eoKDhIWGh4iJipKTlJWWl5iZmqKjpKWmp6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uLj5OXm5+jp6vLz9PX29/j5+v/bAEMAAgICAgICAwICAwUDAwMFBgUFBQUGCAYGBgYGCAoICAgICAgKCgoKCgoKCgwMDAwMDA4ODg4ODw8PDw8PDw8PD//bAEMBAgICBAQEBwQEBxALCQsQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEP/dAAQABv/aAAwDAQACEQMRAD8A+XI7er0dvV+O3q9Hb1/f+Nrn8rZbjNihHb1fjt6vR29Xo7evksbXPvstxhRjt+lXo7er8dvV6O3r5LG1z77LsZsUY7fpV6O3q/Hb1ejt6+Sxtc+9y7GbFCO39qvR2/Sr8dvV6O3r5LG1z77LcYUY7er0dvV+O3q9Hb18lja599luM2P/0PFY7er8dvV6O3q/Hb+1f3Nja5/FuW4zYoR29Xo7er8dvV6O3r5HG1z77LsYUY7er0dvV+O3q9Hb18lja599luM2KEdvV+O39qvR29Xo7evksbXPvcuxmxRjt6vR29X47er0dvXyWNrn32W4wox29Xo7er0dvV+O3r5LG1z77LcZsf/R4+O3q/Hb1ejt6vx29f2Zja5/AmW4zYoR29Xo7er8dvV6O3r5LG1z73LcYUY7er0dvV+O3q9Hb18lja599l2M2KEdvV6O3q/Hb1ejt6+Sxtc++y3GbFGO39qvR29X47er0dv0r5LG1z77LsYUI7er8dvV6O3q/Hb18lja599luM2P/9K1Hb+1X47er0dvV+O3r+ssbXP82stxmxQjt6vR2/tV+O39qvR29fJ42uffZbjChHb1fjt6vR29Xo7evksbXPvstxmxRjt6vR29X47er0dvXyONrn32XYzYox29Xo7er8dvV6O3r5PG1z77LcYUI7f2q/Hb1ejt6vR29fI42ufe5bjNj//Z";

int decode_char(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

std::vector<unsigned char> decode_base64(std::string_view text) {
    std::vector<unsigned char> out;
    int value = 0;
    int bits = -8;
    for (char c : text) {
        if (c == '=') break;
        if (std::isspace(static_cast<unsigned char>(c))) continue;
        const int d = decode_char(c);
        if (d < 0) throw std::runtime_error("invalid base64 asset");
        value = (value << 6) | d;
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<unsigned char>((value >> bits) & 0xff));
            bits -= 8;
        }
    }
    return out;
}

std::filesystem::path write_asset(const std::filesystem::path& dir,
                                  const char* name,
                                  std::string_view base64) {
    std::filesystem::create_directories(dir);
    const auto path = dir / name;
    const auto bytes = decode_base64(base64);
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return path;
}

std::string url_path(const std::filesystem::path& path) {
    return path.lexically_normal().generic_string();
}

}  // namespace

int main() {
    const auto asset_dir =
        std::filesystem::temp_directory_path() / "affineui-media-example";
    const auto checker = write_asset(asset_dir, "checker.png", kCheckerPng);
    const auto panel   = write_asset(asset_dir, "panel.png", kPanelPng);
    const auto photo   = write_asset(asset_dir, "photo.jpg", kPhotoJpg);

    affineui::Ui ui;

    std::string html = R"HTML(
        <style>
        body { margin: 0; background: #f8f9fa; color: #212529;
               font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; }
        main { padding: 24px; }
        h1 { margin: 0 0 20px 0; font-size: 32px; }
        .grid { display: flex; gap: 20px; align-items: stretch; flex-wrap: wrap; }
        .card { width: 260px; padding: 16px; border: 1px solid #dee2e6;
                border-radius: 6px; background: #ffffff; }
        .card h2 { margin: 0 0 12px 0; font-size: 20px; }
        .sample-img { width: 160px; height: 120px; border: 1px solid #adb5bd; }
        .background-sample {
            height: 120px;
            border-radius: 6px;
            background-color: #cfe2ff;
            background-image: url("__PHOTO__");
            background-size: cover;
            background-position: center;
        }
        .nine-slice {
            min-height: 88px;
            background: #f8f9fa;
            border: 24px solid transparent;
            border-image: url("__PANEL__") 12 fill / 24px stretch;
        }
        p { margin: 12px 0 0; line-height: 1.45; }
        </style>
        <main>
            <h1>Media Compatibility</h1>
            <div class="grid">
                <section class="card">
                    <h2>PNG image</h2>
                    <img class="sample-img" src="__CHECKER__">
                    <p>Exercises replaced-element sizing and image upload.</p>
                </section>
                <section class="card">
                    <h2>JPG background</h2>
                    <div class="background-sample"></div>
                    <p>Exercises background-image, size, and position.</p>
                </section>
                <section class="card">
                    <h2>Nine slice</h2>
                    <div class="nine-slice">Border-image panel</div>
                    <p>Exercises CSS border-image style panels.</p>
                </section>
            </div>
        </main>
    )HTML";
    const auto replace_all = [](std::string& s,
                                std::string_view from,
                                std::string_view to) {
        std::size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
    };
    replace_all(html, "__CHECKER__", url_path(checker));
    replace_all(html, "__PHOTO__", url_path(photo));
    replace_all(html, "__PANEL__", url_path(panel));
    ui.html(html);

    sapp_desc desc{};
    desc.width         = 1100;
    desc.height        = 700;
    desc.window_title  = "AffineUI — media";
    desc.high_dpi      = true;
    desc.swap_interval = 1;
    desc.sample_count  = 1;
    desc.logger.func   = slog_func;
    affineui::sokol::wire(desc, ui);
    sapp_run(&desc);
    return 0;
}
