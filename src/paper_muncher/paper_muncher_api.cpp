#include "paper_muncher_api.h"
#include "engine.hpp"
#include <cstdlib>
#include <cstring>
#include <string>
#include <exception>

static thread_local std::string last_error;
static void set_error(const std::string& err) { last_error = err; }

static paper_muncher::Options parse_options_json(const char* json) {
    paper_muncher::Options opts;
    if (!json) return opts;

    // Minimal JSON parsing — extract known fields
    // For a robust implementation, use a JSON library
    std::string s(json);

    // Helper to extract a string value for a key
    auto extract = [&](const std::string& key) -> std::string {
        auto pos = s.find("\"" + key + "\"");
        if (pos == std::string::npos) return "";
        pos = s.find(':', pos);
        if (pos == std::string::npos) return "";
        auto start = s.find('"', pos + 1);
        if (start == std::string::npos) return "";
        auto end = s.find('"', start + 1);
        if (end == std::string::npos) return "";
        return s.substr(start + 1, end - start - 1);
    };

    auto extractNum = [&](const std::string& key, double def) -> double {
        auto pos = s.find("\"" + key + "\"");
        if (pos == std::string::npos) return def;
        pos = s.find(':', pos);
        if (pos == std::string::npos) return def;
        return std::stod(s.substr(pos + 1));
    };

    auto v = extract("paper");        if (!v.empty()) opts.paper = v;
    v = extract("orientation");        if (!v.empty()) opts.orientation = v;
    v = extract("output_format");      if (!v.empty()) opts.output_format = v;
    v = extract("background");         if (!v.empty()) opts.background = v;
    opts.width  = extractNum("width", 0);
    opts.height = extractNum("height", 0);
    opts.scale  = extractNum("scale", 1.0);
    opts.density = extractNum("density", 1.0);

    return opts;
}

extern "C" {

int pm_init(const char* bundle_dir) {
    try {
        if (bundle_dir) {
            setenv("CK_BUILDDIR", bundle_dir, 1);
        }
        paper_muncher::init();
        return 0;
    } catch (const std::exception& e) {
        set_error(e.what());
        return -1;
    }
}

void pm_shutdown(void) {
    try { paper_muncher::shutdown(); } catch (...) {}
}

int pm_html_to_pdf_buffer(
    const char* html,
    const char* options_json,
    unsigned char** out_pdf,
    unsigned long* out_size
) {
    if (!html || !out_pdf || !out_size) {
        set_error("invalid arguments");
        return -1;
    }

    try {
        auto opts = parse_options_json(options_json);
        auto pdf = paper_muncher::html_to_pdf(std::string(html), opts);

        auto* buf = static_cast<unsigned char*>(std::malloc(pdf.size()));
        if (!buf) { set_error("out of memory"); return -1; }

        std::memcpy(buf, pdf.data(), pdf.size());
        *out_pdf = buf;
        *out_size = static_cast<unsigned long>(pdf.size());

        return 0;
    } catch (const std::exception& e) {
        set_error(e.what());
        return -1;
    }
}

void pm_free(void* ptr) { std::free(ptr); }

const char* pm_last_error(void) {
    return last_error.empty() ? nullptr : last_error.c_str();
}

} // extern "C"