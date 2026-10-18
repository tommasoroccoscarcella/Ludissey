#import <AppKit/AppKit.h>

#include "City.hpp"
#include "Engine/Core/Resources.hpp"
#include "Engine/Rendering/Renderer.hpp"
#include "Golden.hpp"
#include "Interface/ClientView.hpp"
#include "Pixels.hpp"
#include "Platform/Mac/Platform.hpp"
#include "ServerHarness.hpp"

#include <Catch2/catch_amalgamated.hpp>

#include <cmath>
#include <cstdlib>

using namespace pixels;

namespace {

Pixels shrink(const Pixels& source, int width, int height) {
    Pixels out{width, height, std::vector<std::uint8_t>(static_cast<std::size_t>(width * height * 4), 255)};
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            int x0 = x * source.width / width, x1 = (x + 1) * source.width / width, y0 = y * source.height / height, y1 = (y + 1) * source.height / height;
            double sum[3] = {};
            int count = 0;
            for (int sy = y0; sy < y1; ++sy)
                for (int sx = x0; sx < x1; ++sx) {
                    std::size_t i = static_cast<std::size_t>((sy * source.width + sx) * 4);
                    for (int k = 0; k < 3; ++k) sum[k] += source.rgba[i + static_cast<std::size_t>(k)];
                    ++count;
                }
            std::size_t o = static_cast<std::size_t>((y * width + x) * 4);
            for (int k = 0; k < 3; ++k) out.rgba[o + static_cast<std::size_t>(k)] = static_cast<std::uint8_t>(sum[k] / std::max(1, count));
        }
    return out;
}

double difference(const Pixels& a, const Pixels& b) {
    REQUIRE(a.width == b.width);
    REQUIRE(a.height == b.height);
    double total = 0;
    for (std::size_t i = 0; i < a.rgba.size(); i += 4)
        for (int k = 0; k < 3; ++k) total += std::abs(a.rgba[i + static_cast<std::size_t>(k)] - b.rgba[i + static_cast<std::size_t>(k)]);
    return total / (a.rgba.size() / 4 * 3);
}

double detail(const Pixels& p, double left, double right, double top, double bottom) {
    double total = 0;
    int count = 0;
    for (int y = static_cast<int>(top * p.height); y < static_cast<int>(bottom * p.height) - 1; ++y)
        for (int x = static_cast<int>(left * p.width); x < static_cast<int>(right * p.width) - 1; ++x) {
            std::size_t i = static_cast<std::size_t>((y * p.width + x) * 4), r = i + 4, d = i + static_cast<std::size_t>(p.width * 4);
            for (int k = 0; k < 3; ++k) total += std::abs(p.rgba[i + static_cast<std::size_t>(k)] - p.rgba[r + static_cast<std::size_t>(k)]) + std::abs(p.rgba[i + static_cast<std::size_t>(k)] - p.rgba[d + static_cast<std::size_t>(k)]);
            ++count;
        }
    return total / std::max(1, count);
}

void keep(const Pixels& image, const std::string& name) {
    if (const char* folder = std::getenv("LUDISSEY_SHOTS")) {
        std::filesystem::create_directories(folder);
        save(image, std::filesystem::path(folder) / ("login " + name + ".png"));
    }
}

}

TEST_CASE("Accesso: sfondo della città", "[login][backdrop][rendering]") {
    ld::setResourceFolder(golden::root() / "Resources");
    auto scene = city::build();
    CHECK(scene->parts > 1000);
    CHECK(scene->people.size() > 60);
    auto image = ld::render::Renderer::shared().snapshot(scene->world->scene, *scene->camera, 640, 360, 4, true);
    REQUIRE(image.width == 640);
    Pixels small{image.width, image.height, image.rgba};
    keep(small, "citta 640");
    auto reference = golden::root() / "Tests/Golden/Login/Citta 640.png";
    const char* mode = std::getenv("LUDISSEY_GOLDEN");
    if (mode && std::string(mode) == "write") {
        std::filesystem::create_directories(reference.parent_path());
        save(small, reference);
    }
    REQUIRE(std::filesystem::exists(reference));
    CHECK(difference(small, load(reference)) < 3);
    auto masterPath = golden::root() / "Resources/Backgrounds/Login.jpg";
    REQUIRE(std::filesystem::exists(masterPath));
    CHECK(std::filesystem::file_size(masterPath) < 4 * 1024 * 1024);
    Pixels master = load(masterPath);
    CHECK(master.width == 3840);
    CHECK(master.height == 2160);
    Pixels reduced = shrink(master, 640, 360);
    INFO("differenza master e scena attuale " << difference(reduced, small));
    CHECK(difference(reduced, small) < 8);
    double calm = detail(reduced, 0.38, 0.62, 0.22, 0.78), sides = (detail(reduced, 0.0, 0.3, 0.22, 0.78) + detail(reduced, 0.7, 1.0, 0.22, 0.78)) / 2;
    INFO("dettaglio al centro " << calm << ", ai lati " << sides);
    CHECK(calm < sides * 0.75);
}

TEST_CASE("Accesso: sfondo nei formati della finestra", "[login][backdrop][platform][screens]") {
    @autoreleasepool {
        [NSApplication sharedApplication];
        golden::registry();
        ld::setResourceFolder(golden::root() / "Resources");
        harness::Fixture fixture;
        auto store = std::make_shared<ld::Store>(std::make_shared<ld::PlayFab>(std::make_shared<ld::MemoryStore>()), fixture.world->registerPlayer("Plogin", "Login"), nullptr, false);
        store->profile.reset();
        ui::ClientView screen(store);
        platform::Window window({"Verifica Login", 1600, 900, 640, 480}, [&] { return screen.build(); });
        screen.invalidated = [&] { window.invalidate(); };
        NSWindow* target = nil;
        for (NSWindow* candidate in NSApp.windows)
            if ([candidate.title isEqualToString:@"Verifica Login"]) target = candidate;
        REQUIRE(target != nil);
        for (auto [name, width, height] : std::vector<std::tuple<std::string, int, int>>{{"16x9", 1600, 900}, {"16x10", 1440, 900}, {"4x3", 1024, 768}, {"ridotta", 760, 560}}) {
            INFO(name);
            [target setContentSize:NSMakeSize(width, height)];
            window.invalidate();
            window.show();
            [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.35]];
            auto cards = window.surface().matching("login.card");
            REQUIRE(cards.size() == 1);
            const auto& card = cards.front().frame;
            CHECK(card.x >= 0);
            CHECK(card.y >= 0);
            CHECK(card.maxX() <= width);
            CHECK(card.maxY() <= height);
            CGImageRef shot = CGWindowListCreateImage(CGRectNull, kCGWindowListOptionIncludingWindow, static_cast<CGWindowID>(target.windowNumber), kCGWindowImageBoundsIgnoreFraming | kCGWindowImageBestResolution);
            REQUIRE(shot != nullptr);
            double scale = target.backingScaleFactor;
            int w = static_cast<int>(width * scale), h = static_cast<int>(height * scale);
            Pixels actual = draw(shot, w, h, static_cast<int>(CGImageGetHeight(shot)) - h);
            CGImageRelease(shot);
            keep(actual, name);
            double plain = detail(actual, 0.02, 0.18, 0.05, 0.95);
            CHECK(plain > 2);
        }
        window.views = nullptr;
    }
}
