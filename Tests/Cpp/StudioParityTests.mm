#import <AppKit/AppKit.h>

#include "Golden.hpp"
#include "Pixels.hpp"
#include "Platform/Mac/Platform.hpp"
#include "Sample.hpp"
#include "ServerHarness.hpp"
#include "Studio/StudioView.hpp"

#include <Catch2/catch_amalgamated.hpp>

using namespace pixels;

namespace {

void mask(Pixels& image, const ui::Frame& frame, double scale) {
    int x0 = std::max(0, static_cast<int>(std::floor(frame.x * scale))), y0 = std::max(0, static_cast<int>(std::floor(frame.y * scale)));
    int x1 = std::min(image.width, static_cast<int>(std::ceil(frame.maxX() * scale))), y1 = std::min(image.height, static_cast<int>(std::ceil(frame.maxY() * scale)));
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            auto* p = &image.rgba[static_cast<std::size_t>((y * image.width + x) * 4)];
            p[0] = p[1] = p[2] = 255;
            p[3] = 255;
        }
}

}

TEST_CASE("Golden: finestra di Studio", "[platform][parity][screens][studio]") {
    @autoreleasepool {
        [NSApplication sharedApplication];
        platform::installAdapters();
        golden::registry();
        harness::Fixture fixture;
        auto store = std::make_shared<ld::Store>(std::make_shared<ld::PlayFab>(std::make_shared<ld::MemoryStore>()), fixture.world->registerPlayer("Pmartina", sample::player), nullptr, false);
        store->profile = ld::PlayerProfile{sample::player, "female", "2026-09-20T10:00:00Z"};
        studio::StudioShell studio(store);
        ld::Experience project;
        project.id = ld::makeUuid();
        project.title = sample::games().front().title;
        project.genre = sample::games().front().genre;
        REQUIRE(studio.document.open(project));
        auto& document = studio.document;
        for (const char* kind : {"cube", "sphere", "cylinder"}) document.addPart(kind);
        ld::WorldBlock mesh;
        mesh.id = ld::makeUuid();
        mesh.kind = "mesh";
        mesh.name = "Mesh";
        mesh.meshAsset = std::string(64, 'a') + ".obj";
        mesh.locked = true;
        mesh.x = 3;
        mesh.y = 0.5;
        document.world.blocks.push_back(mesh);
        std::string cube = document.world.blocks[0].id, sphere = document.world.blocks[1].id;
        document.update(cube, [](ld::WorldBlock& b) { b.x = 0; b.y = 0.5; b.z = 0; });
        REQUIRE(document.insert("Script", cube));
        auto broken = document.insert("Script", sphere);
        REQUIRE(broken);
        document.rename(*broken, "Rotto");
        document.expandedIds.clear();
        document.openScripts.clear();
        document.activeScript.reset();
        document.utilityTab = "output";
        document.utilityCollapsed = false;
        document.tool = ld::StudioTool::move;
        document.select({cube});
        document.setLocked(true);
        document.setLocked(false);
        document.select({cube});
        document.copySelection();
        document.output.clear();
        for (const char* line : {"Play · 3 oggetti · controlli personaggio", "posizione 4", "secondo passo", "Prova finita: le modifiche fatte in gioco sono state annullate."}) {
            ld::OutputLine message;
            message.text = line;
            message.time = 1790000000;
            document.output.push_back(message);
        }
        document.markSaved();
        platform::Window window({"Verifica parità Studio", 1180, 820, 860, 680}, [&] { return studio.build(); });
        studio.invalidated = [&] { window.invalidate(); };
        window.views = [&](const auto& slots) { return studio.views(slots); };
        window.rendered = [&](const auto& surface) { studio.rendered(surface); };
        const char* shots = std::getenv("LUDISSEY_SHOTS");
        for (int width : {1400, 1180, 920}) {
            NSWindow* target = nil;
            for (NSWindow* candidate in NSApp.windows)
                if ([candidate.title isEqualToString:@"Verifica parità Studio"]) target = candidate;
            REQUIRE(target != nil);
            [target setContentSize:NSMakeSize(width, 820)];
            window.invalidate();
            window.show();
            [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.3]];
            window.show();
            CGImageRef image = CGWindowListCreateImage(CGRectNull, kCGWindowListOptionIncludingWindow, static_cast<CGWindowID>(target.windowNumber), kCGWindowImageBoundsIgnoreFraming | kCGWindowImageBestResolution);
            REQUIRE(image != nullptr);
            double scale = target.backingScaleFactor;
            int w = static_cast<int>(width * scale), h = static_cast<int>(820 * scale);
            int titlebar = static_cast<int>(CGImageGetHeight(image)) - h;
            Pixels actual = draw(image, w, h, titlebar);
            CGImageRelease(image);
            std::string name = "studio " + std::to_string(width);
            auto reference = golden::root() / "Tests/Golden/Screens" / (name + ".png");
            Pixels expected = load(reference);
            REQUIRE(expected.width == actual.width);
            for (const auto& slot : window.surface().viewports()) {
                mask(actual, slot.frame, scale);
                mask(expected, slot.frame, scale);
            }
            if (const char* mode = std::getenv("LUDISSEY_GOLDEN"); mode && std::string(mode) == "write") {
                save(actual, reference);
                expected = actual;
            }
            auto clear = window.surface().matching("output.clear");
            REQUIRE(clear.size() == 1);
            for (const auto& field : window.surface().fields())
                if (field.id == "studio.commandBar") {
                    ui::Frame log{0, clear[0].frame.maxY() + 4, static_cast<double>(width), field.frame.y - clear[0].frame.maxY() - 6};
                    mask(actual, log, scale);
                    mask(expected, log, scale);
                }
            Pixels heat;
            Difference d = compare(actual, expected, &heat);
            if (shots) {
                std::filesystem::create_directories(shots);
                save(actual, std::filesystem::path(shots) / ("cpp " + name + ".png"));
                save(heat, std::filesystem::path(shots) / ("diff " + name + ".png"));
            }
            WARN(name << ": differenza media " << d.mean << ", pixel diversi " << d.changed * 100 << "%");
            CHECK(d.mean < 0.25);
            CHECK(d.changed < 0.002);
        }
        studio.invalidated = nullptr;
    }
}
