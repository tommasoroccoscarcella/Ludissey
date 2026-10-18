#import <AppKit/AppKit.h>

#include "Golden.hpp"
#include "Interface/ClientMedia.hpp"
#include "Interface/ClientView.hpp"
#include "Platform/Mac/Platform.hpp"
#include "Sample.hpp"
#include "ServerHarness.hpp"

#include "Pixels.hpp"

#include <Catch2/catch_amalgamated.hpp>

using namespace pixels;

TEST_CASE("Golden: schermate del client", "[platform][parity][screens]") {
    @autoreleasepool {
        [NSApplication sharedApplication];
        platform::installAdapters();
        golden::registry();
        harness::Fixture fixture;
        auto backend = fixture.world->registerPlayer("Pmartina", sample::player);
        auto store = std::make_shared<ld::Store>(std::make_shared<ld::PlayFab>(std::make_shared<ld::MemoryStore>()), backend, nullptr, false);
        sample::populate(*store);
        store->friends = sample::friends();
        store->requests = {{"pending", "Giulia", ""}};
        store->syncNotifications();
        ui::ClientView screen(store);
        ui::ClientMedia media(store);
        screen.thumbnail = [&](const auto& id) { return media.thumbnail(id); };
        screen.itemImage = [&](const auto& item) { return media.itemImage(item); };
        platform::Window window({"Verifica parità schermate", 1400, 760, 860, 680}, [&] { return screen.build(); });
        screen.invalidated = [&] { window.invalidate(); };
        store->changed = [&] { window.invalidate(); };
        window.views = [&](const auto& slots) { return media.views(slots); };
        window.tick = [&](double, double delta) { media.tick(delta); };
        const char* shots = std::getenv("LUDISSEY_SHOTS");
        auto capture = [&](const std::string& name, int width) {
            NSWindow* target = nil;
            for (NSWindow* candidate in NSApp.windows)
                if ([candidate.title isEqualToString:@"Verifica parità schermate"]) target = candidate;
            REQUIRE(target != nil);
            [target setContentSize:NSMakeSize(width, 760)];
            window.invalidate();
            window.show();
            [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.25]];
            window.show();
            CGImageRef image = CGWindowListCreateImage(CGRectNull, kCGWindowListOptionIncludingWindow, static_cast<CGWindowID>(target.windowNumber), kCGWindowImageBoundsIgnoreFraming | kCGWindowImageBestResolution);
            REQUIRE(image != nullptr);
            double scale = target.backingScaleFactor;
            int w = static_cast<int>(width * scale), h = static_cast<int>(760 * scale);
            int titlebar = static_cast<int>(CGImageGetHeight(image)) - h;
            Pixels actual = draw(image, w, h, titlebar);
            CGImageRelease(image);
            auto reference = golden::root() / "Tests/Golden/Screens" / (name + ".png");
            const char* mode = std::getenv("LUDISSEY_GOLDEN");
            if (mode && std::string(mode) == "write") save(actual, reference);
            Pixels expected = load(reference);
            Pixels heat;
            Difference d = compare(actual, expected, &heat);
            if (shots) {
                std::filesystem::create_directories(shots);
                save(actual, std::filesystem::path(shots) / ("cpp " + name + ".png"));
                save(heat, std::filesystem::path(shots) / ("diff " + name + ".png"));
            }
            UNSCOPED_INFO(name << ": differenza media " << d.mean << ", pixel diversi " << d.changed * 100 << "%");
            return d;
        };
        std::vector<std::tuple<std::string, ld::Route, int>> screens{{"home 1400", ld::Route::home, 1400}, {"home 1180", ld::Route::home, 1180}, {"home 920", ld::Route::home, 920}, {"home 860", ld::Route::home, 860},
                                                                      {"experiences 1400", ld::Route::experiences, 1400}, {"friends 1400", ld::Route::friends, 1400}, {"settings 1400", ld::Route::settings, 1400}};
        for (const auto& [name, route, width] : screens) {
            store->navigate(route);
            if (route == ld::Route::experiences) store->experienceScope = ld::ExperienceScope::community;
            auto d = capture(name, width);
            CHECK(d.mean < 0.25);
            CHECK(d.changed < 0.002);
        }
        store->profile.reset();
        auto login = capture("Accesso", 1180);
        CHECK(login.mean < 0.25);
        CHECK(login.changed < 0.002);
        store->changed = nullptr;
    }
}
