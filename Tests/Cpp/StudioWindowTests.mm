#import <AppKit/AppKit.h>

#include "Golden.hpp"
#include "Platform/Mac/Platform.hpp"
#include "ServerHarness.hpp"
#include "Studio/StudioStage.hpp"
#include "Studio/StudioView.hpp"

#include <Catch2/catch_amalgamated.hpp>

#include <cmath>

namespace {

NSView* rootView(NSView* view) {
    if ([NSStringFromClass([view class]) isEqualToString:@"LDRootView"]) return view;
    for (NSView* child in view.subviews)
        if (NSView* found = rootView(child)) return found;
    return nil;
}

NSEvent* mouse(NSEventType type, NSView* view, double x, double y) {
    NSPoint inWindow = [view convertPoint:NSMakePoint(x, y) toView:nil];
    return [NSEvent mouseEventWithType:type location:inWindow modifierFlags:0 timestamp:NSProcessInfo.processInfo.systemUptime windowNumber:view.window.windowNumber context:nil eventNumber:0 clickCount:1 pressure:type == NSEventTypeMouseMoved ? 0 : 1];
}

void pump(double seconds) { [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:seconds]]; }

}

TEST_CASE("Gizmo: finestra reale ed eventi AppKit", "[platform][studio][gizmo][window]") {
    @autoreleasepool {
        [NSApplication sharedApplication];
        platform::installAdapters();
        golden::registry();
        harness::Fixture fixture;
        auto store = std::make_shared<ld::Store>(std::make_shared<ld::PlayFab>(std::make_shared<ld::MemoryStore>()), fixture.world->registerPlayer("Pfinestra", "Finestra"), nullptr, false);
        store->profile = ld::PlayerProfile{"Finestra", "male", "2026-10-03T10:00:00Z"};
        studio::StudioShell studio(store);
        REQUIRE(studio.document.open(ld::Experience::blank()));
        auto& document = studio.document;
        REQUIRE(document.addPart("cube"));
        std::string part = document.selectedIds().front();
        document.update(part, [](ld::WorldBlock& b) {
            b.x = 0;
            b.z = 0;
            b.y = 1;
            b.sizeX = 4;
            b.sizeY = 2;
            b.sizeZ = 3;
        });
        document.select({part});
        document.tool = ld::StudioTool::move;
        document.moveSnap = 0;
        platform::Window window({"Verifica gizmo Studio", 1180, 820, 860, 680}, [&] { return studio.build(); });
        studio.invalidated = [&] { window.invalidate(); };
        window.views = [&](const auto& slots) { return studio.views(slots); };
        window.rendered = [&](const auto& surface) { studio.rendered(surface); };
        window.pointer = [&](const std::string& slot, const ui::PointerEvent& event) { return studio.pointer(slot, event); };
        window.show();
        pump(0.4);
        NSWindow* target = nil;
        for (NSWindow* candidate in NSApp.windows)
            if ([candidate.title isEqualToString:@"Verifica gizmo Studio"]) target = candidate;
        REQUIRE(target != nil);
        NSView* root = rootView(target.contentView);
        REQUIRE(root != nil);
        std::optional<ui::Frame> viewport;
        for (const auto& slot : window.surface().viewports())
            if (slot.id == "studio.viewport") viewport = slot.frame;
        REQUIRE(viewport);
        auto stage = studio.stage;
        REQUIRE(stage);
        stage->orient(0.6, 0.4, 18, {0, 1, 0});
        stage->refresh();
        pump(0.1);
        auto gizmo = stage->scene()->root->child("studio.gizmo");
        REQUIRE(gizmo);
        REQUIRE_FALSE(gizmo->hidden);
        ld::render::NodePtr xPlus;
        for (const auto& holder : gizmo->children())
            if (holder->name == "gizmo:0:1") xPlus = holder;
        REQUIRE(xPlus);
        auto toWindow = [&](ld::Vec3 world) {
            ld::Vec3 p = ld::render::project(*stage->camera(), {stage->size.width, stage->size.height}, world);
            return NSMakePoint(viewport->x + p.x, viewport->y + p.y);
        };
        const auto& shaft = xPlus->children()[0];
        ld::Bounds bounds = shaft->geometry->bounds();
        ld::Mat4 frame = xPlus->world() * shaft->local();
        NSPoint middle = toWindow(frame.transformPoint({0, (bounds.low.y + bounds.high.y) / 2, 0}));
        NSPoint head = toWindow(xPlus->children()[1]->worldPosition());
        NSPoint outside = NSMakePoint(viewport->x + 6, viewport->y + 6);
        [root mouseMoved:mouse(NSEventTypeMouseMoved, root, outside.x, outside.y)];
        CHECK(stage->gizmoState() == studio::StudioStage::GizmoState::idle);
        [root mouseMoved:mouse(NSEventTypeMouseMoved, root, head.x, head.y)];
        CHECK(stage->gizmoState() == studio::StudioStage::GizmoState::hover);
        CHECK(stage->highlightedHandle() == std::make_pair(0, 1));
        [root mouseMoved:mouse(NSEventTypeMouseMoved, root, viewport->x - 40, viewport->y + 200)];
        CHECK(stage->gizmoState() == studio::StudioStage::GizmoState::idle);
        [root mouseMoved:mouse(NSEventTypeMouseMoved, root, middle.x, middle.y)];
        CHECK(stage->highlightedHandle() == std::make_pair(0, 1));
        double before = document.world.blocks.back().x;
        [root mouseDown:mouse(NSEventTypeLeftMouseDown, root, middle.x, middle.y)];
        CHECK(stage->gizmoState() == studio::StudioStage::GizmoState::pressed);
        double dx = head.x - middle.x, dy = head.y - middle.y, length = std::hypot(dx, dy);
        REQUIRE(length > 4);
        dx /= length;
        dy /= length;
        [root mouseDragged:mouse(NSEventTypeLeftMouseDragged, root, middle.x - dy * 120, middle.y + dx * 120)];
        CHECK(stage->gizmoState() == studio::StudioStage::GizmoState::dragging);
        [root mouseDragged:mouse(NSEventTypeLeftMouseDragged, root, viewport->x - 30, viewport->y + 100)];
        CHECK(stage->gizmoState() == studio::StudioStage::GizmoState::dragging);
        CHECK(stage->highlightedHandle() == std::make_pair(0, 1));
        NSPoint end = NSMakePoint(middle.x + dx * 80, middle.y + dy * 80);
        [root mouseDragged:mouse(NSEventTypeLeftMouseDragged, root, end.x, end.y)];
        [root mouseUp:mouse(NSEventTypeLeftMouseUp, root, end.x, end.y)];
        pump(0.1);
        CHECK(stage->gizmoState() != studio::StudioStage::GizmoState::dragging);
        CHECK(stage->gizmoState() != studio::StudioStage::GizmoState::pressed);
        const auto& moved = document.world.blocks.back();
        CHECK(moved.x - before > 0.3);
        CHECK(std::abs(moved.y - 1) < 1e-9);
        CHECK(std::abs(moved.z) < 1e-9);
        CHECK(document.undoLabel());
        document.setLocked(true);
        stage->refresh();
        pump(0.1);
        REQUIRE_FALSE(gizmo->hidden);
        CHECK(stage->gizmoState() == studio::StudioStage::GizmoState::disabled);
        NSPoint lockedHead = toWindow(xPlus->children()[1]->worldPosition());
        [root mouseMoved:mouse(NSEventTypeMouseMoved, root, lockedHead.x, lockedHead.y)];
        CHECK(stage->gizmoState() == studio::StudioStage::GizmoState::disabled);
        CHECK_FALSE(stage->highlightedHandle());
        ld::Color grey = stage->handleColor({0, 1});
        CHECK(std::max({grey.r, grey.g, grey.b}) - std::min({grey.r, grey.g, grey.b}) < 0.08);
        double lockedX = document.world.blocks.back().x;
        [root mouseDown:mouse(NSEventTypeLeftMouseDown, root, lockedHead.x, lockedHead.y)];
        [root mouseDragged:mouse(NSEventTypeLeftMouseDragged, root, lockedHead.x + 80, lockedHead.y)];
        [root mouseUp:mouse(NSEventTypeLeftMouseUp, root, lockedHead.x + 80, lockedHead.y)];
        pump(0.1);
        CHECK(document.world.blocks.back().x == lockedX);
        document.setLocked(false);
        stage->refresh();
        pump(0.1);
        CHECK(stage->gizmoState() != studio::StudioStage::GizmoState::disabled);
        window.views = nullptr;
        window.pointer = nullptr;
    }
}

TEST_CASE("Studio: campi nativi delle Proprietà", "[platform][studio][window][panels]") {
    @autoreleasepool {
        [NSApplication sharedApplication];
        platform::installAdapters();
        golden::registry();
        harness::Fixture fixture;
        auto store = std::make_shared<ld::Store>(std::make_shared<ld::PlayFab>(std::make_shared<ld::MemoryStore>()), fixture.world->registerPlayer("Ppannelli", "Pannelli"), nullptr, false);
        store->profile = ld::PlayerProfile{"Pannelli", "male", "2026-10-04T10:00:00Z"};
        studio::StudioShell studio(store);
        REQUIRE(studio.document.open(ld::Experience::blank()));
        REQUIRE(studio.document.addPart("cube"));
        studio.document.select({studio.document.selectedIds().front()});
        platform::Window window({"Verifica pannelli Studio", 1180, 760, 860, 680}, [&] { return studio.build(); });
        studio.invalidated = [&] { window.invalidate(); };
        window.rendered = [&](const auto& surface) { studio.rendered(surface); };
        window.show();
        pump(0.4);
        NSWindow* target = nil;
        for (NSWindow* candidate in NSApp.windows)
            if ([candidate.title isEqualToString:@"Verifica pannelli Studio"]) target = candidate;
        REQUIRE(target != nil);
        NSView* root = rootView(target.contentView);
        REQUIRE(root != nil);
        auto form = window.surface().find("properties.scroll");
        REQUIRE(form);
        ui::Frame bounds = form->frame;
        int checked = 0, escaped = 0;
        std::string example;
        for (double offset = 0; offset <= 420; offset += 3) {
            window.surface().scrollTo("properties.form", {0, offset});
            window.invalidate();
            pump(0.03);
            for (NSView* clip in root.subviews) {
                if (![NSStringFromClass([clip class]) isEqualToString:@"LDClipView"]) continue;
                CHECK(clip.layer.masksToBounds);
                for (NSView* child in clip.subviews) {
                    if (child.hidden || !([child isKindOfClass:[NSTextField class]] || [child isKindOfClass:[NSPopUpButton class]])) continue;
                    NSRect full = NSOffsetRect(child.frame, NSMinX(clip.frame), NSMinY(clip.frame));
                    NSRect shown = NSIntersectionRect(full, clip.frame);
                    NSRect area = NSMakeRect(bounds.x, bounds.y, bounds.width, bounds.height);
                    if (NSIsEmptyRect(shown) || !NSIntersectsRect(full, area) || NSMidX(full) < bounds.x || NSMidX(full) > bounds.maxX()) continue;
                    ++checked;
                    if (NSMinY(shown) < bounds.y - 1 || NSMaxY(shown) > bounds.maxY() + 1) {
                        if (escaped == 0) example = NSStringFromRect(shown).UTF8String + std::string(" contenitore ") + NSStringFromRect(clip.frame).UTF8String + " campo " + NSStringFromRect(child.frame).UTF8String;
                        ++escaped;
                    }
                }
            }
        }
        INFO("campi controllati " << checked << ", sconfinati " << escaped << "; area " << bounds.y << "–" << bounds.maxY() << "; esempio " << example);
        CHECK(checked > 50);
        CHECK(escaped == 0);
        studio.invalidated = nullptr;
    }
}
