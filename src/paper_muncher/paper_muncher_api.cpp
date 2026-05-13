#include "paper_muncher_api.h"
#include "engine.hpp"
#include <cstdlib>
#include <cstring>
#include <string>
#include <exception>
#include <vector>

#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <karm/macros>

import Karm.Core;
import Karm.Ref;
import Karm.Sys;
import Karm.Sys.Posix;
import Karm.Logger;

extern "C" char** environ;

using namespace Karm::Ref::Literals;

#ifndef BUILD_DIGEST
#define BUILD_DIGEST "unknown"
#endif

#define PM_API __attribute__((visibility("default")))

static thread_local std::string last_error;
static void set_error(const std::string& err) { last_error = err; }

// (parse_options_json unchanged — keep your existing version)

namespace {

struct EmbeddedEnv : Karm::Sys::Env {
    Karm::Sys::Argv _argv;
    Karm::Sys::Envp _envp;
    Karm::Ref::Url  _cwd;

    EmbeddedEnv(int argc, char const** argv, char** envp, Karm::Ref::Url cwd)
        : _argv(argc, argv), _envp(envp), _cwd(cwd) {}

    Karm::Sys::Args const& args() const override { return _argv; }
    Karm::Ref::Url         cwd()  const override { return _cwd;  }
    Karm::Sys::Vars const& vars() const override { return _envp; }
};

void embeddedPanicHandler(Karm::PanicKind kind, char const* msg, Karm::usize len) {
    fprintf(stderr, "%s: %.*s\n",
            kind == Karm::PanicKind::PANIC ? "panic" : "debug",
            (int)len, msg);
    if (kind == Karm::PanicKind::PANIC) {
        std::abort();
        __builtin_unreachable();
    }
}

Karm::Res<Karm::Ref::Url> embeddedPwd() {
    std::vector<char> buf(256);
    while (true) {
        if (::getcwd(buf.data(), buf.size()) != nullptr)
            break;
        if (errno != ERANGE)
            return Karm::Posix::fromLastErrno();
        buf.resize(buf.size() * 2);
    }
    return Karm::Ok(Karm::Ref::parseUrlOrPath(
        Karm::Str::fromNullterminated(buf.data()), "file:"_url));
}

char const* g_fake_argv[] = { "solpdf", nullptr };

EmbeddedEnv& embeddedEnv() {
    static EmbeddedEnv env{
        /* argc */ 1,
        /* argv */ g_fake_argv,
        /* envp */ environ,
        /* cwd  */ embeddedPwd().unwrap(),
    };
    return env;
}

} // anonymous namespace

extern "C" {

PM_API int pm_init(const char* bundle_dir) {
    try {
        if (bundle_dir) {
            setenv("CK_BUILDDIR", bundle_dir, 1);
        }

        // Panic handler installed once across the process.
        static bool panicHandlerInstalled = [] {
            Karm::registerPanicHandler(embeddedPanicHandler);
            return true;
        }();
        (void)panicHandlerInstalled;

        // Construct the global Env (Magic Static = once, threadsafe).
        // The base Env::Env() ctor installs `this` into _globalEnv.
        (void)embeddedEnv();

        SolPDF::init();
        return 0;
    } catch (const std::exception& e) {
        set_error(e.what());
        return -1;
    }
}

PM_API void pm_set_log_level(int level) {
    Karm::setLogLevel({level, "", {}});
}

PM_API void pm_shutdown(void) {
    try { SolPDF::shutdown(); } catch (...) {}
}

PM_API int pm_html_to_pdf_buffer(
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
        auto pdf = SolPDF::html_to_pdf(std::string(html), opts);

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

PM_API const char* pm_build_digest(void) {
    return BUILD_DIGEST;
}

PM_API void pm_free(void* ptr) { std::free(ptr); }

PM_API const char* pm_last_error(void) {
    return last_error.empty() ? nullptr : last_error.c_str();
}

} // extern "C"