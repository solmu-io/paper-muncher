#include <karm/macros>
#include <stdexcept>
#include <string>
#include <vector>
#include "engine.hpp"
#include <cstdlib>

import Vaev.Engine;
import Karm.Sys;
import Karm.Http;
import Karm.Gc;
import Karm.Print;
import Karm.Core;
import Karm.Ref;
import Karm.Scene;
import Karm.Gfx;
import Karm.Image;

using namespace Karm;
using namespace Karm::Literals;
using namespace Karm::Math::Literals;
using namespace Karm::Fmt::Literals;

// This code originates from Paper Muncher:
// https://github.com/odoo/paper-muncher
// GNU Affero General Public License v3.0
namespace SolPDF {

// --- Duplicated from PaperMuncher (src/mod.cpp) to avoid module dependency ---
// The methods we need do not justify depending on the whole module and can be rewritten.

static Rc<Http::Transport> _createHttpTransport(bool sandboxed) {
    if (sandboxed) {
        return Http::multiplexTransport({
            Http::cacheTransport(Http::pipeTransport()),
            Http::localTransport({"bundle"s, "fd"s, "data"s}),
        });
    }

    return Http::multiplexTransport({
        Http::cacheTransport({
            Http::pipeTransport(),
            Http::httpTransport(),
        }),
        Http::localTransport(Http::LocalTransportPolicy::ALLOW_ALL),
    });
}

static Rc<Http::Client> _defaultHttpClient(bool sandboxed) {
    auto transport = _createHttpTransport(sandboxed);
    auto client = makeRc<Http::Client>(transport);
    client->userAgent = "Paper-Muncher-Lib/0.1"s;
    return client;
}

// --- End duplicated section ---

void init() {
}

void shutdown() {
}

static Async::Task<Buf<u8>> _htmlToPdfAsync(
    std::string const& html,
    Options const& opts,
    Async::CancellationToken ct
) {
    // 1. Build print settings via string parsing (same as main.cpp)
    Vaev::Resolution scale = Vaev::Resolution::fromDppx(1);
    Vaev::Resolution density = Vaev::Resolution::fromDppx(1);
    Print::PaperStock paper = Print::A4;
    Print::Orientation orientation = Print::Orientation::PORTRAIT;
    Print::Margins margins = Print::Margins::DEFAULT;
    Opt<Vaev::Length> width = NONE;
    Opt<Vaev::Length> height = NONE;

    if (!opts.paper.empty())
        paper = co_try$(Print::lookupStockByName(Str(opts.paper.c_str())));

    if (!opts.orientation.empty())
        orientation = co_try$(
            Vaev::parseValue<Print::Orientation>(Str(opts.orientation.c_str()))
        );

    if (opts.scale != 1.0) {
        auto s = std::to_string(opts.scale) + "x";
        scale = co_try$(Vaev::parseValue<Vaev::Resolution>(Str(s.c_str())));
    }

    if (opts.density != 1.0) {
        auto d = std::to_string(opts.density) + "x";
        density = co_try$(Vaev::parseValue<Vaev::Resolution>(Str(d.c_str())));
    }

    if (opts.width > 0) {
        auto w = std::to_string(opts.width) + "px";
        width = co_try$(Vaev::parseValue<Vaev::Length>(Str(w.c_str())));
    }

    if (opts.height > 0) {
        auto h = std::to_string(opts.height) + "px";
        height = co_try$(Vaev::parseValue<Vaev::Length>(Str(h.c_str())));
    }

    // 2. Derive print settings (mirrors Option::derivePrintSettings in mod.cpp)
    Vaev::Layout::Resolver resolver;
    resolver.viewport.dpi = density;

    auto stock = paper;
    if (width or height)
        stock = Print::PaperStock::custom(
            width  ? resolver.resolve(*width)  : stock.minorAxis,
            height ? resolver.resolve(*height) : stock.majorAxis
        );

    Print::Settings settings{
        .stock = stock,
        .orientation = orientation,
        .margins = margins,
        .scale = scale.toDppx(),
    };

    // 3. Create sandboxed HTTP client
    auto client = _defaultHttpClient(true);

    // 4. Input: HTML string as a data: URL
    auto htmlStr = String(html.c_str());
    auto dataUrl = Ref::Url::data("text/html"_mime, bytes(htmlStr));

    // 5. Create the PDF printer
    auto printer = co_try$(
        Print::FilePrinter::create(
            Ref::Uti::PUBLIC_PDF,
            {.density = density.toDppx()}
        )
    );

    // 6. Load the document
    auto window = Vaev::Dom::Window::create(client);
    co_trya$(window->loadLocationAsync(dataUrl, Ref::Uti::PUBLIC_OPEN, ct));

    // 7. Paginate and print
    window->print(settings) | ForEach([&](Print::Page& page) {
        page.print(*printer, {.showBackgroundGraphics = true});
    });

    // 8. Capture PDF into buffer — no temp file
    Io::BufferWriter bw;
    co_try$(printer->write(bw));

    co_return Ok(bw.take());
}

std::vector<unsigned char> html_to_pdf(
    std::string const& html,
    Options const& opts
) {
    Sys::Context ctx;
    Async::Cancellation cancellation;

    Buf<u8> captured;

    auto task = [&]() -> Async::Task<> {
        auto result = co_await _htmlToPdfAsync(html, opts, cancellation.token());
        captured = co_try$(std::move(result));
        co_return Ok();
    };

    Res<> code = Sys::run(task());
    cancellation.cancel();

    if (not code) {
        throw std::runtime_error("paper-muncher conversion failed");
    }

    // Convert Karm Buf<u8> to std::vector<unsigned char>
    auto raw = bytes(captured);
    std::vector<unsigned char> out(raw.len());
    for (usize i = 0; i < raw.len(); i++)
        out[i] = raw[i];

    return out;
}

} // namespace SolPDF