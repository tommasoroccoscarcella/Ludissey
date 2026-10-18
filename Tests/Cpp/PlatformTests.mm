#import <AppKit/AppKit.h>

#include "Engine/Audio/Audio.hpp"
#include "Interface/ClientMedia.hpp"
#include "Interface/ClientView.hpp"
#include "Platform/Mac/Platform.hpp"
#include "ServerHarness.hpp"
#include "Studio/StudioView.hpp"

#include <Catch2/catch_amalgamated.hpp>
#include <chrono>
#include <cmath>
#include <fstream>
#include <thread>

TEST_CASE("Piattaforma: chiusura della finestra", "[platform]") {
    @autoreleasepool {
        [NSApplication sharedApplication];
        int frames = 0;
        for (int iteration = 0; iteration < 6; ++iteration) {
            auto window = std::make_unique<platform::Window>(platform::WindowSpec{"Verifica ciclo finestra", 320, 240, 320, 240}, [] { return ui::text("Ludissey")->fill(); });
            window->tick = [&](double, double) { ++frames; };
            window->show();
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            window.reset();
            int atClose = frames;
            [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.02]];
            CHECK(frames == atClose);
        }
    }
}

TEST_CASE("Piattaforma: popup e viewport di Studio", "[platform][studio]") {
    @autoreleasepool {
        [NSApplication sharedApplication];
        platform::installAdapters();
        golden::registry();
        harness::Fixture fixture;
        auto store = std::make_shared<ld::Store>(std::make_shared<ld::PlayFab>(std::make_shared<ld::MemoryStore>()), fixture.world->registerPlayer("PstudioWindow", "Creatore"), nullptr, false);
        studio::StudioShell studio(store);
        studio.document.open(ld::Experience::blank());
        studio.document.select({studio.document.world.blocks.front().id});
        platform::Window window({"Verifica Studio C++", 1180, 760, 860, 680}, [&] { return studio.build(); });
        studio.invalidated = [&] { window.invalidate(); };
        window.views = [&](const auto& slots) { return studio.views(slots); };
        window.rendered = [&](const auto& surface) { studio.rendered(surface); };
        window.show();
        CHECK_FALSE(studio.document.explorerFrames.empty());
        CHECK(studio.views(window.surface().viewports()).size() == 1);
        if (const char* folder = std::getenv("LUDISSEY_SHOTS")) {
            [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.1]];
            NSWindow* target = nil;
            for (NSWindow* candidate in NSApp.windows) if ([candidate.title isEqualToString:@"Verifica Studio C++"]) target = candidate;
            REQUIRE(target != nil);
            CGImageRef image = CGWindowListCreateImage(CGRectNull, kCGWindowListOptionIncludingWindow, static_cast<CGWindowID>(target.windowNumber), kCGWindowImageBoundsIgnoreFraming);
            REQUIRE(image != nullptr);
            NSBitmapImageRep* bitmap = [[NSBitmapImageRep alloc] initWithCGImage:image];
            CGImageRelease(image);
            auto path = std::filesystem::path(folder) / "native-studio-editor.png";
            REQUIRE([[bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:[NSString stringWithUTF8String:path.c_str()] atomically:YES]);
        }
        studio.document.settingsOpen = true;
        window.invalidate();
        window.show();
        bool hasPopup = false;
        for (const auto& field : window.surface().fields()) hasPopup |= !field.options.empty();
        CHECK(hasPopup);
        {
            studio.document.insertTarget = "Workspace";
            platform::Window sheet({"Inserisci oggetto", 360, 390, 360, 390, true}, [&] { return studio.insertSheet(); });
            window.beginSheet(sheet);
            sheet.focusField("studio.insertObject.search");
            CHECK(sheet.editingText());
            window.endSheet(sheet);
        }
        studio.invalidated = nullptr;
    }
}

TEST_CASE("Piattaforma: campi di codice ricreati", "[platform]") {
    @autoreleasepool {
        [NSApplication sharedApplication];
        bool editing = true;
        platform::Window window({"Verifica editor", 420, 300, 320, 240}, [&] {
            if (!editing) return ui::text("Editor chiuso")->fill();
            ui::NativeField code;
            code.id = "script";
            code.code = true;
            code.text = "let titolo = \"Ludissey\"\nprint(titolo)";
            return ui::textField(code)->fill();
        });
        window.show();
        REQUIRE(window.surface().fields().size() == 1);
        for (int i = 0; i < 5; ++i) {
            window.focusField("script");
            CHECK(window.editingText());
            editing = false;
            window.invalidate();
            window.show();
            CHECK(window.surface().fields().empty());
            editing = true;
            window.invalidate();
            window.show();
            CHECK(window.surface().fields().size() == 1);
        }
    }
}

TEST_CASE("Piattaforma: input nativo e anteprime avatar", "[platform][client][rendering]") {
    @autoreleasepool {
        [NSApplication sharedApplication];
        golden::registry();
        harness::Fixture fixture;
        auto backend = fixture.world->registerPlayer("Ppreview", "Giocatrice");
        auto store = std::make_shared<ld::Store>(std::make_shared<ld::PlayFab>(std::make_shared<ld::MemoryStore>()), backend, nullptr, false);
        ui::ClientView screen(store);
        ui::ClientMedia media(store);
        screen.thumbnail = [&](const auto& id) { return media.thumbnail(id); };
        screen.itemImage = [&](const auto& item) { return media.itemImage(item); };
        platform::Window window({"Verifica client C++", 1180, 760, 860, 680}, [&] { return screen.build(); });
        screen.invalidated = [&] { window.invalidate(); };
        store->changed = [&] { window.invalidate(); };
        window.views = [&](const auto& slots) { return media.views(slots); };
        window.tick = [&](double, double delta) { media.tick(delta); };
        auto capture = [&](const std::string& filename) {
            window.show();
            [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.08]];
            if (const char* folder = std::getenv("LUDISSEY_SHOTS")) {
                NSWindow* target = nil;
                for (NSWindow* candidate in NSApp.windows) if ([candidate.title isEqualToString:@"Verifica client C++"]) { target = candidate; break; }
                REQUIRE(target != nil);
                CGImageRef image = CGWindowListCreateImage(CGRectNull, kCGWindowListOptionIncludingWindow, static_cast<CGWindowID>(target.windowNumber), kCGWindowImageBoundsIgnoreFraming);
                REQUIRE(image != nullptr);
                NSBitmapImageRep* bitmap = [[NSBitmapImageRep alloc] initWithCGImage:image];
                CGImageRelease(image);
                NSData* data = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
                std::filesystem::create_directories(folder);
                auto path = std::filesystem::path(folder) / filename;
                REQUIRE([data writeToFile:[NSString stringWithUTF8String:path.c_str()] atomically:YES]);
            }
        };
        capture("native-client-login.png");
        window.focusField("login.username");
        CHECK(window.editingText());
        store->profile = ld::PlayerProfile{"Giocatrice", "female", "2026-09-20T10:00:00Z"};
        store->avatar = ld::AvatarConfiguration::standard("Giocatrice", "female");
        store->avatarDraft = store->avatar;
        store->navigate(ld::Route::avatar);
        capture("native-client-avatar.png");
        auto views = media.views(window.surface().viewports());
        REQUIRE(views.size() == 1);
        CHECK(views[0].scene != nullptr);
        CHECK(views[0].camera != nullptr);
        auto before = views[0].camera->position;
        media.orbit("avatar.preview", 25, 10);
        views = media.views(window.surface().viewports());
        CHECK(views[0].camera->position != before);
        auto world = ld::Experience::blank();
        store->setWorlds({world});
        auto preview = media.thumbnail(world.id);
        CHECK(preview.valid());
        CHECK(preview.pixelsWide == 480);
        CHECK_FALSE(media.thumbnail("../invalid").valid());
        store->changed = nullptr;
    }
}

TEST_CASE("Piattaforma: completamento nell'editor nativo", "[platform][tooling]") {
    @autoreleasepool {
        @try {
        [NSApplication sharedApplication];
        platform::installAdapters();
        golden::registry();
        harness::Fixture fixture;
        auto store = std::make_shared<ld::Store>(std::make_shared<ld::PlayFab>(std::make_shared<ld::MemoryStore>()), fixture.world->registerPlayer("Peditor", "Creatore"), nullptr, false);
        studio::StudioShell studio(store);
        REQUIRE(studio.document.open(ld::Experience::blank()));
        auto id = studio.document.insert("Script", "ServerScriptService");
        REQUIRE(id);
        studio.document.activeScript = *id;
        std::string source = "let valore=1\nprint(valore)\n";
        studio.document.setSource(*id, source);
        platform::Window window({"Verifica editor Lim", 1180, 760, 860, 680}, [&] { return studio.build(); });
        studio.invalidated = [&] { window.invalidate(); };
        window.show();
        NSWindow* target = nil;
        for (NSWindow* candidate in NSApp.windows) if ([candidate.title isEqualToString:@"Verifica editor Lim"]) target = candidate;
        REQUIRE(target != nil);
        std::function<NSTextView*(NSView*)> findEditor = [&](NSView* parent) -> NSTextView* {
            if ([parent isKindOfClass:[NSTextView class]]) return (NSTextView*)parent;
            for (NSView* child in parent.subviews) if (auto editor = findEditor(child)) return editor;
            return nil;
        };
        NSTextView* editor = findEditor(target.contentView);
        REQUIRE(editor != nil);
        [target makeFirstResponder:editor];
        NSInteger selected = 0;
        auto offset = source.rfind("valore");
        NSArray<NSString*>* candidates = [editor completionsForPartialWordRange:NSMakeRange(offset, 3) indexOfSelectedItem:&selected];
        CHECK([candidates containsObject:@"valore"]);
        NSMenuItem* command = [[NSMenuItem alloc] initWithTitle:@"Formatta script" action:NSSelectorFromString(@"runEditorCommand:") keyEquivalent:@""];
        command.tag = 0;
        REQUIRE([editor respondsToSelector:command.action]);
        [NSApp sendAction:command.action to:editor from:command];
        CHECK(studio.document.world.record(*id)->source().find("valore = 1") != std::string::npos);
        auto formatted = studio.document.world.record(*id)->source();
        REQUIRE([NSApp sendAction:NSSelectorFromString(@"undo:") to:editor from:nil]);
        CHECK(studio.document.world.record(*id)->source() == source);
        window.show();
        CHECK(std::string(editor.string.UTF8String) == source);
        REQUIRE([NSApp sendAction:NSSelectorFromString(@"redo:") to:editor from:nil]);
        CHECK(studio.document.world.record(*id)->source() == formatted);
        window.show();
        CHECK(std::string(editor.string.UTF8String) == formatted);
        [editor insertText:@"print(2)\n" replacementRange:NSMakeRange(editor.string.length, 0)];
        CHECK(studio.document.world.record(*id)->source() == formatted + "print(2)\n");
        studio.document.undo();
        CHECK(studio.document.world.record(*id)->source() == formatted);
        window.show();
        REQUIRE([NSApp sendAction:NSSelectorFromString(@"undo:") to:editor from:nil]);
        CHECK(studio.document.world.record(*id)->source() == source);
        studio.document.redo();
        window.show();
        CHECK(std::string(editor.string.UTF8String) == formatted);
        studio.invalidated = nullptr;
        } @catch (NSException* exception) { FAIL(std::string(exception.reason.UTF8String)); }
    }
}

TEST_CASE("Piattaforma: audio WAV", "[platform][audio]") {
    @autoreleasepool {
        platform::installAudio();
        auto output = ld::audio::output();
        REQUIRE(output);
        auto file = std::filesystem::temp_directory_path() / ("ludissey-" + ld::makeUuid() + ".wav");
        const int rate = 8000, samples = rate * 2;
        std::string wave = "RIFF";
        auto word = [&](std::uint32_t value, int bytes) {
            for (int i = 0; i < bytes; ++i) wave.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
        };
        word(36 + samples * 2, 4);
        wave += "WAVEfmt ";
        word(16, 4);
        word(1, 2);
        word(1, 2);
        word(rate, 4);
        word(rate * 2, 4);
        word(2, 2);
        word(16, 2);
        wave += "data";
        word(samples * 2, 4);
        for (int i = 0; i < samples; ++i) word(static_cast<std::uint16_t>(static_cast<std::int16_t>(std::sin(i * 2 * 3.14159265358979 * 440 / rate) * 600)), 2);
        std::ofstream(file, std::ios::binary) << wave;
        CHECK_FALSE(output->playing("suono"));
        REQUIRE(output->play("suono", ld::audio::Playback{file, 0.05, 1, true}));
        CHECK(output->playing("suono"));
        output->update("suono", ld::audio::Playback{file, 0.02, 1.5, true});
        CHECK(output->playing("suono"));
        output->stop("suono");
        CHECK_FALSE(output->playing("suono"));
        CHECK_FALSE(output->play("rotto", ld::audio::Playback{std::filesystem::temp_directory_path() / "ludissey-assente.wav", 1, 1, false}));
        std::filesystem::remove(file);
        ld::audio::setOutput(nullptr);
    }
}
