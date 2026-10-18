#import <AppKit/AppKit.h>
#import <CoreVideo/CoreVideo.h>
#import <Metal/Metal.h>
#import <objc/runtime.h>
#import <QuartzCore/QuartzCore.h>

#include "Engine/Character/Character.hpp"
#include "Interface/Components.hpp"
#include "Interface/Theme.hpp"
#include "Localization/Localization.hpp"
#include "Platform/Mac/Platform.hpp"

#include <atomic>
#include <cctype>
#include <map>
#include <string>
#include <utility>
#include <vector>

@class LDRootView;

namespace {

NSString* text(const std::string& value) { return [[NSString alloc] initWithBytes:value.data() length:value.size() encoding:NSUTF8StringEncoding] ?: @""; }

std::string utf8(NSString* value) { return value ? std::string(value.UTF8String) : std::string(); }

NSColor* color(ui::Color c) { return [NSColor colorWithSRGBRed:c.r green:c.g blue:c.b alpha:c.a]; }

NSFont* font(const ui::Font& f) {
    if (f.monospaced) return [NSFont fontWithName:@"Menlo" size:f.size] ?: [NSFont monospacedSystemFontOfSize:f.size weight:NSFontWeightRegular];
    bool bold = f.weight == ui::Weight::bold || f.weight == ui::Weight::semibold;
    if (f.family.empty()) return [NSFont systemFontOfSize:f.size weight:bold ? NSFontWeightSemibold : NSFontWeightRegular];
    NSFont* made = (__bridge NSFont*)ui::fontHandle(f);
    return made ?: [NSFont systemFontOfSize:f.size];
}

NSCursor* cursor(ui::Cursor c) {
    switch (c) {
    case ui::Cursor::pointer: return NSCursor.pointingHandCursor;
    case ui::Cursor::text: return NSCursor.IBeamCursor;
    case ui::Cursor::resizeHorizontal: return NSCursor.resizeLeftRightCursor;
    case ui::Cursor::resizeVertical: return NSCursor.resizeUpDownCursor;
    case ui::Cursor::crosshair: return NSCursor.crosshairCursor;
    case ui::Cursor::move: return NSCursor.openHandCursor;
    default: return NSCursor.arrowCursor;
    }
}

NSRange utf16Range(NSString* string, const std::string& source, std::size_t start, std::size_t length) {
    std::size_t end = std::min(source.size(), start + length);
    start = std::min(start, end);
    NSUInteger from = [[NSString alloc] initWithBytes:source.data() length:start encoding:NSUTF8StringEncoding].length;
    NSUInteger to = [[NSString alloc] initWithBytes:source.data() length:end encoding:NSUTF8StringEncoding].length;
    if (to > string.length) to = string.length;
    if (from > to) from = to;
    return NSMakeRange(from, to - from);
}

enum class FieldKind { plain, secure, code, popup };

ui::Modifiers modifiersOf(NSEvent* event) {
    NSEventModifierFlags flags = event.modifierFlags;
    return {static_cast<bool>(flags & NSEventModifierFlagCommand), static_cast<bool>(flags & NSEventModifierFlagShift), static_cast<bool>(flags & NSEventModifierFlagOption), static_cast<bool>(flags & NSEventModifierFlagControl)};
}

double inkTop(NSFont* typeface) {
    static NSMutableDictionary<NSString*, NSNumber*>* cache = [NSMutableDictionary dictionary];
    NSString* key = [NSString stringWithFormat:@"%@-%f", typeface.fontName, typeface.pointSize];
    if (NSNumber* known = cache[key]) return known.doubleValue;
    NSTextField* probe = [[NSTextField alloc] initWithFrame:NSZeroRect];
    probe.bezeled = NO;
    probe.bordered = NO;
    probe.drawsBackground = NO;
    probe.usesSingleLineMode = YES;
    probe.font = typeface;
    probe.textColor = NSColor.blackColor;
    probe.stringValue = @"H";
    NSSize size = NSMakeSize(40, probe.intrinsicContentSize.height);
    probe.frame = NSMakeRect(0, 0, size.width, size.height);
    int scale = 4, width = static_cast<int>(size.width) * scale, height = static_cast<int>(std::ceil(size.height)) * scale;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width * height * 4), 0);
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef context = CGBitmapContextCreate(pixels.data(), static_cast<size_t>(width), static_cast<size_t>(height), 8, static_cast<size_t>(width * 4), space, static_cast<uint32_t>(kCGImageAlphaPremultipliedLast));
    CGContextTranslateCTM(context, 0, height);
    CGContextScaleCTM(context, scale, -scale);
    [NSGraphicsContext saveGraphicsState];
    NSGraphicsContext.currentContext = [NSGraphicsContext graphicsContextWithCGContext:context flipped:YES];
    [probe.cell drawWithFrame:probe.bounds inView:probe];
    [NSGraphicsContext restoreGraphicsState];
    CGContextRelease(context);
    CGColorSpaceRelease(space);
    double top = 0;
    bool found = false;
    for (int y = 0; y < height && !found; ++y)
        for (int x = 0; x < width; ++x)
            if (pixels[static_cast<std::size_t>((y * width + x) * 4 + 3)] > 127) {
                top = y / static_cast<double>(scale);
                found = true;
                break;
            }
    if (!found) top = typeface.ascender - typeface.capHeight;
    cache[key] = @(top);
    return top;
}

}

@interface LDCodeTextView : NSTextView {
@public
    ui::NativeField codeSpec;
}
- (void)runEditorCommand:(NSMenuItem*)sender;
- (void)undo:(id)sender;
- (void)redo:(id)sender;
- (void)find:(NSTextFinderAction)action;
@end

@implementation LDCodeTextView
- (void)drawViewBackgroundInRect:(NSRect)rect {
    [super drawViewBackgroundInRect:rect];
    if (!self.editable || self.selectedRange.length > 0) return;
    NSRange line = [self.string lineRangeForRange:NSMakeRange(std::min(self.selectedRange.location, self.string.length), 0)];
    NSRange glyphs = [self.layoutManager glyphRangeForCharacterRange:line actualCharacterRange:nil];
    NSRect band = glyphs.length > 0 ? [self.layoutManager lineFragmentRectForGlyphAtIndex:glyphs.location effectiveRange:nil] : self.layoutManager.extraLineFragmentRect;
    band.origin.x = 0;
    band.size.width = NSWidth(self.bounds);
    band.origin.y += self.textContainerOrigin.y;
    [color(ui::theme::code::currentLine) setFill];
    NSRectFill(NSIntersectionRect(band, rect));
}
- (void)setSelectedRanges:(NSArray<NSValue*>*)ranges affinity:(NSSelectionAffinity)affinity stillSelecting:(BOOL)selecting {
    [super setSelectedRanges:ranges affinity:affinity stillSelecting:selecting];
    [self setNeedsDisplay:YES];
    [self.enclosingScrollView.verticalRulerView setNeedsDisplay:YES];
}
- (NSMenu*)menuForEvent:(NSEvent*)event {
    NSMenu* menu = [[super menuForEvent:event] copy] ?: [[NSMenu alloc] initWithTitle:@""];
    if (!codeSpec.commands.empty()) [menu addItem:NSMenuItem.separatorItem];
    for (std::size_t i = 0; i < codeSpec.commands.size(); ++i) {
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:text(codeSpec.commands[i].title) action:@selector(runEditorCommand:) keyEquivalent:@""];
        item.target = self;
        item.tag = static_cast<NSInteger>(i);
        item.enabled = self.editable || !codeSpec.commands[i].changesText;
        [menu addItem:item];
    }
    return menu;
}
- (BOOL)validateMenuItem:(NSMenuItem*)item {
    if (item.action == @selector(undo:) || item.action == @selector(redo:)) {
        bool redo = item.action == @selector(redo:);
        if (!self.editable) return NO;
        if (codeSpec.historyAction) return codeSpec.historyEnabled && codeSpec.historyEnabled(redo);
        return redo ? self.undoManager.canRedo : self.undoManager.canUndo;
    }
    if (item.action == @selector(runEditorCommand:)) {
        return item.tag >= 0 && static_cast<std::size_t>(item.tag) < codeSpec.commands.size() &&
            (self.editable || !codeSpec.commands[static_cast<std::size_t>(item.tag)].changesText);
    }
    return [super validateMenuItem:item];
}
- (void)undo:(id)sender {
    if (!self.editable) return;
    auto action = codeSpec.historyAction;
    if (action) action(false);
    else if (self.undoManager.canUndo) [self.undoManager undo];
}
- (void)redo:(id)sender {
    if (!self.editable) return;
    auto action = codeSpec.historyAction;
    if (action) action(true);
    else if (self.undoManager.canRedo) [self.undoManager redo];
}
- (void)runEditorCommand:(NSMenuItem*)sender {
    if (![self validateMenuItem:sender]) return;
    auto command = codeSpec.commands[static_cast<std::size_t>(sender.tag)];
    if (!command.run) return;
    NSString* original = [self.string copy];
    NSRange selection = self.selectedRange;
    std::size_t offset = utf8([original substringToIndex:std::min(selection.location, original.length)]).size();
    std::string argument;
    if (!command.prompt.empty()) {
        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = text(command.title);
        alert.informativeText = text(command.prompt);
        [alert addButtonWithTitle:@"Applica"];
        [alert addButtonWithTitle:@"Annulla"];
        NSTextField* input = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 280, 24)];
        if (NSMaxRange(selection) <= original.length) input.stringValue = [original substringWithRange:selection];
        alert.accessoryView = input;
        alert.window.initialFirstResponder = input;
        if ([alert runModal] != NSAlertFirstButtonReturn) return;
        argument = utf8(input.stringValue);
    }
    if (![self.string isEqualToString:original]) return;
    auto reply = command.run(utf8(original), offset, argument);
    if (reply.text && self.editable) {
        NSString* replacement = text(*reply.text);
        if (codeSpec.limit > 0 && replacement.length > codeSpec.limit) reply.message = ld::tr("Lo script supera la lunghezza massima.");
        else {
            if (codeSpec.editBoundary) codeSpec.editBoundary();
            [self insertText:replacement replacementRange:NSMakeRange(0, original.length)];
            if (codeSpec.editBoundary) codeSpec.editBoundary();
            if (!codeSpec.historyAction) [self.undoManager setActionName:text(command.title)];
        }
    }
    if (reply.selection) {
        NSRange range = utf16Range(self.string, utf8(self.string), reply.selection->first, reply.selection->second);
        [self setSelectedRange:range];
        [self scrollRangeToVisible:range];
    }
    if (!reply.message.empty()) {
        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = text(command.title);
        alert.informativeText = text(reply.message);
        [alert addButtonWithTitle:@"OK"];
        [alert beginSheetModalForWindow:self.window completionHandler:nil];
    }
}
- (NSArray<NSString*>*)completionsForPartialWordRange:(NSRange)range indexOfSelectedItem:(NSInteger*)index {
    if (!codeSpec.complete) return [super completionsForPartialWordRange:range indexOfSelectedItem:index];
    NSUInteger position = std::min(NSMaxRange(range), self.string.length);
    auto items = codeSpec.complete(utf8(self.string), utf8([self.string substringToIndex:position]).size());
    NSMutableArray<NSString*>* result = [NSMutableArray array];
    for (const auto& item : items) [result addObject:text(item)];
    if (index) *index = result.count ? 0 : -1;
    return result;
}
- (BOOL)performKeyEquivalent:(NSEvent*)event {
    if (self.window.firstResponder != self || !(event.modifierFlags & NSEventModifierFlagCommand)) return [super performKeyEquivalent:event];
    NSString* key = event.charactersIgnoringModifiers.lowercaseString;
    if ([key isEqualToString:@"z"]) {
        if (event.modifierFlags & NSEventModifierFlagShift) [self redo:nil];
        else [self undo:nil];
        return YES;
    }
    if ([key isEqualToString:@"f"]) {
        [self find:(event.modifierFlags & NSEventModifierFlagOption) ? NSTextFinderActionShowReplaceInterface : NSTextFinderActionShowFindInterface];
        return YES;
    }
    if ([key isEqualToString:@"g"]) {
        [self find:(event.modifierFlags & NSEventModifierFlagShift) ? NSTextFinderActionPreviousMatch : NSTextFinderActionNextMatch];
        return YES;
    }
    return [super performKeyEquivalent:event];
}
- (void)find:(NSTextFinderAction)action {
    NSMenuItem* item = [[NSMenuItem alloc] init];
    item.tag = action;
    [self performTextFinderAction:item];
}
@end

@interface LDMenuAction : NSObject
@property(nonatomic) std::function<void()> action;
- (void)invoke:(id)sender;
@end

@implementation LDMenuAction
- (void)invoke:(id)sender {
    if (self.action) self.action();
}
@end

namespace {

NSMenu* buildMenu(const std::vector<ui::MenuItem>& items) {
    NSMenu* menu = [[NSMenu alloc] init];
    menu.autoenablesItems = NO;
    for (const auto& item : items) {
        if (item.separator) {
            [menu addItem:[NSMenuItem separatorItem]];
            continue;
        }
        NSMenuItem* entry = [[NSMenuItem alloc] initWithTitle:text(item.title) action:nil keyEquivalent:@""];
        if (!item.shortcut.empty()) {
            NSEventModifierFlags flags = 0;
            std::string rest = item.shortcut;
            for (auto [glyph, flag] : std::vector<std::pair<std::string, NSEventModifierFlags>>{{"⌘", NSEventModifierFlagCommand}, {"⇧", NSEventModifierFlagShift}, {"⌥", NSEventModifierFlagOption}, {"⌃", NSEventModifierFlagControl}})
                if (auto at = rest.find(glyph); at != std::string::npos) {
                    flags |= flag;
                    rest.erase(at, glyph.size());
                }
            unichar key = 0;
            if (rest == "⌫") key = NSBackspaceCharacter;
            else if (rest.size() >= 2 && rest[0] == 'F') key = static_cast<unichar>(NSF1FunctionKey + std::stoi(rest.substr(1)) - 1);
            else if (rest.size() == 1) key = static_cast<unichar>(std::tolower(static_cast<unsigned char>(rest[0])));
            if (key) {
                entry.keyEquivalent = [NSString stringWithCharacters:&key length:1];
                entry.keyEquivalentModifierMask = flags;
            }
        }
        if (!item.submenu.empty()) entry.submenu = buildMenu(item.submenu);
        else if (item.action) {
            LDMenuAction* handler = [[LDMenuAction alloc] init];
            handler.action = item.action;
            entry.target = handler;
            entry.action = @selector(invoke:);
            entry.representedObject = handler;
        }
        entry.enabled = item.enabled;
        if (item.checked) entry.state = NSControlStateValueOn;
        NSImage* picture = nil;
        if (!item.icon.empty() && ui::icons::packed(item.icon)) picture = [[NSImage alloc] initWithContentsOfFile:text((ui::icons::folder() / ui::icons::file(item.icon)).string())];
        if (picture) {
            picture.size = NSMakeSize(16, 16);
            entry.image = picture;
        }
        [menu addItem:entry];
    }
    return menu;
}

}

@interface LDLineRuler : NSRulerView
@property(nonatomic) NSInteger errorLine;
@end

@implementation LDLineRuler
- (void)drawHashMarksAndLabelsInRect:(NSRect)rect {
    NSTextView* textView = (NSTextView*)self.clientView;
    NSLayoutManager* layout = textView.layoutManager;
    NSTextContainer* container = textView.textContainer;
    if (!layout || !container) return;
    [color(ui::theme::code::gutter) setFill];
    NSRectFill(self.bounds);
    [color(ui::theme::colors::borderSubtle) setFill];
    NSRectFill(NSMakeRect(NSMaxX(self.bounds) - 1, NSMinY(self.bounds), 1, self.bounds.size.height));
    NSString* content = textView.string;
    NSRect visible = self.scrollView.contentView.bounds;
    NSRange glyphs = [layout glyphRangeForBoundingRect:visible inTextContainer:container];
    NSRange characters = [layout characterRangeForGlyphRange:glyphs actualGlyphRange:nil];
    __block NSInteger line = 1;
    if (characters.location > 0)
        [content enumerateSubstringsInRange:NSMakeRange(0, characters.location) options:NSStringEnumerationByLines | NSStringEnumerationSubstringNotRequired usingBlock:^(NSString*, NSRange, NSRange, BOOL*) {
            line += 1;
        }];
    NSFont* code = [NSFont fontWithName:@"Menlo" size:12] ?: [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular];
    NSDictionary* plain = @{NSFontAttributeName : code, NSForegroundColorAttributeName : color(ui::theme::colors::textTertiary)};
    NSDictionary* failing = @{NSFontAttributeName : code, NSForegroundColorAttributeName : color(ui::theme::colors::textOnAccent)};
    void (^label)(NSInteger, CGFloat, CGFloat) = ^(NSInteger number, CGFloat height, CGFloat lineHeight) {
        NSString* value = [NSString stringWithFormat:@"%ld", (long)number];
        NSPoint origin = [self convertPoint:NSMakePoint(0, height) fromView:textView];
        BOOL error = number == self.errorLine;
        if ([textView isKindOfClass:[LDCodeTextView class]] && ((LDCodeTextView*)textView)->codeSpec.breakpoints.contains(static_cast<std::uint32_t>(number))) {
            [color(ui::theme::colors::danger) setFill];
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(3, origin.y + textView.textContainerInset.height + 3, 7, 7)] fill];
        }
        if (error) {
            [color(ui::theme::colors::danger) setFill];
            NSRectFill(NSMakeRect(0, origin.y + textView.textContainerInset.height, self.bounds.size.width - 1, lineHeight));
        }
        NSDictionary* attributes = error ? failing : plain;
        NSSize size = [value sizeWithAttributes:attributes];
        [value drawAtPoint:NSMakePoint(NSMaxX(self.bounds) - size.width - 8, origin.y + textView.textContainerInset.height) withAttributes:attributes];
    };
    NSUInteger index = characters.location;
    NSUInteger end = NSMaxRange(characters);
    while (index < content.length && index <= end) {
        NSRange lineRange = [content lineRangeForRange:NSMakeRange(index, 0)];
        NSRange glyphRange = [layout glyphRangeForCharacterRange:lineRange actualCharacterRange:nil];
        NSRect box = [layout boundingRectForGlyphRange:glyphRange inTextContainer:container];
        label(line, NSMinY(box), box.size.height);
        line += 1;
        NSUInteger next = NSMaxRange(lineRange);
        if (next <= index) break;
        index = next;
    }
    if (layout.extraLineFragmentTextContainer && index >= content.length) label(line, NSMinY(layout.extraLineFragmentRect), layout.extraLineFragmentRect.size.height);
}
@end

struct FieldEntry {
    FieldKind kind = FieldKind::plain;
    NSView* view = nil;
    NSView* clipper = nil;
    ui::NativeField spec;
    bool revealApplied = false;
    bool findApplied = false;
    id boundsObserver = nil;
};

struct DisplayDispatch {
    std::atomic<bool> pending{false};
    platform::Window::Native* owner = nullptr;
};

struct platform::Window::Native {
    platform::Window* owner = nullptr;
    NSWindow* window = nil;
    LDRootView* root = nil;
    NSView* metalView = nil;
    NSView* overlayView = nil;
    CAMetalLayer* metal = nil;
    std::unique_ptr<ui::Surface> surface;
    std::map<std::string, FieldEntry> fields;
    CVDisplayLinkRef link = nullptr;
    std::shared_ptr<DisplayDispatch> dispatch = std::make_shared<DisplayDispatch>();
    double last = 0;
    ld::FramePacer pacer;
    bool viewsDirty = true;
    bool framing = false;
    bool hadViews = false;
    ui::Size drawn;
    double drawnScale = 0;
    std::string captured;
    std::string hovering;
    bool rightCaptured = false;
    ui::Point lastPoint;
    void frame(double now);
    void renderInterface();
    void paintInterface(void* context);
    void syncFields();
    void removeField(const std::string& id);
    std::optional<ui::ViewportSlot> slotAt(ui::Point p) const;
    platform::PointerEvent pointerEvent(NSEvent* event, platform::PointerPhase phase, ui::Point location) const;
    void applyHighlight(FieldEntry& entry, NSTextView* view);
    FieldEntry* entryFor(NSView* view);
};

@interface LDRootView : NSView <NSTextFieldDelegate, NSTextViewDelegate>
@property(nonatomic, assign) platform::Window::Native* native;
@end

@interface LDPassiveView : NSView
@end

@interface LDInterfaceView : LDPassiveView
@property(nonatomic) platform::Window::Native* native;
@end

@implementation LDPassiveView
- (NSView*)hitTest:(NSPoint)point {
    return nil;
}
- (BOOL)isFlipped {
    return YES;
}
@end

@interface LDClipView : NSView
@end

@implementation LDClipView
- (BOOL)isFlipped {
    return YES;
}
@end

@implementation LDInterfaceView
- (BOOL)isOpaque {
    return NO;
}
- (void)drawRect:(NSRect)dirty {
    CGContextRef context = NSGraphicsContext.currentContext.CGContext;
    if (self.native && context) self.native->paintInterface(context);
}
@end

namespace {

CVReturn displayCallback(CVDisplayLinkRef, const CVTimeStamp*, const CVTimeStamp*, CVOptionFlags, CVOptionFlags*, void* context) {
    auto* native = static_cast<platform::Window::Native*>(context);
    auto state = native->dispatch;
    bool expected = false;
    if (!state->pending.compare_exchange_strong(expected, true)) return kCVReturnSuccess;
    dispatch_async(dispatch_get_main_queue(), ^{
        state->pending = false;
        if (state->owner) state->owner->frame(CACurrentMediaTime());
    });
    return kCVReturnSuccess;
}

ui::Point location(NSView* view, NSEvent* event) {
    NSPoint p = [view convertPoint:event.locationInWindow fromView:nil];
    return {p.x, p.y};
}

}

@implementation LDRootView {
    NSTrackingArea* tracking;
}
- (BOOL)isFlipped {
    return YES;
}
- (BOOL)acceptsFirstResponder {
    return YES;
}
- (BOOL)acceptsFirstMouse:(NSEvent*)event {
    return YES;
}
- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if (tracking) [self removeTrackingArea:tracking];
    tracking = [[NSTrackingArea alloc] initWithRect:NSZeroRect options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect owner:self userInfo:nil];
    [self addTrackingArea:tracking];
}
- (void)setFrameSize:(NSSize)size {
    [super setFrameSize:size];
    if (self.native) {
        self.native->surface->invalidate();
        self.native->viewsDirty = true;
    }
}
- (void)viewDidChangeBackingProperties {
    [super viewDidChangeBackingProperties];
    if (self.native) {
        self.native->surface->invalidate();
        self.native->viewsDirty = true;
    }
}
- (BOOL)showMenu:(NSEvent*)event at:(ui::Point)p {
    auto items = self.native->surface->menuAt(p);
    if (!items) return NO;
    [NSMenu popUpContextMenu:buildMenu(*items) withEvent:event forView:self];
    self.native->surface->invalidate();
    return YES;
}
- (void)mouseDown:(NSEvent*)event {
    auto* n = self.native;
    ui::Point p = location(self, event);
    n->lastPoint = p;
    n->surface->setModifiers(modifiersOf(event));
    if ((event.modifierFlags & NSEventModifierFlagControl) && [self showMenu:event at:p]) return;
    if (auto pull = n->surface->pullDownAt(p)) {
        [buildMenu(pull->first) popUpMenuPositioningItem:nil atLocation:NSMakePoint(pull->second.x, pull->second.y + pull->second.height + 2) inView:self];
        n->surface->invalidate();
        return;
    }
    auto slot = n->slotAt(p);
    if (slot && !n->surface->covers(p)) {
        n->captured = slot->id;
        [self.window makeFirstResponder:self];
        if (n->owner->pointer) n->owner->pointer(slot->id, n->pointerEvent(event, platform::PointerPhase::down, {p.x - slot->frame.x, p.y - slot->frame.y}));
        n->viewsDirty = true;
        return;
    }
    n->surface->mouseDown(p, static_cast<int>(event.clickCount));
}
- (void)mouseDragged:(NSEvent*)event {
    auto* n = self.native;
    ui::Point p = location(self, event);
    n->surface->setModifiers(modifiersOf(event));
    if (!n->captured.empty()) {
        for (const auto& s : n->surface->viewports())
            if (s.id == n->captured && n->owner->pointer) n->owner->pointer(s.id, n->pointerEvent(event, platform::PointerPhase::dragged, {p.x - s.frame.x, p.y - s.frame.y}));
        n->lastPoint = p;
        n->viewsDirty = true;
        return;
    }
    n->surface->mouseDragged(p);
}
- (void)mouseUp:(NSEvent*)event {
    auto* n = self.native;
    ui::Point p = location(self, event);
    n->surface->setModifiers(modifiersOf(event));
    if (!n->captured.empty()) {
        for (const auto& s : n->surface->viewports())
            if (s.id == n->captured && n->owner->pointer) n->owner->pointer(s.id, n->pointerEvent(event, platform::PointerPhase::up, {p.x - s.frame.x, p.y - s.frame.y}));
        n->captured.clear();
        n->viewsDirty = true;
        return;
    }
    n->surface->mouseUp(p);
}
- (void)rightMouseDown:(NSEvent*)event {
    auto* n = self.native;
    ui::Point p = location(self, event);
    n->lastPoint = p;
    if ([self showMenu:event at:p]) return;
    auto slot = n->slotAt(p);
    if (slot && !n->surface->covers(p)) {
        n->captured = slot->id;
        n->rightCaptured = true;
        [self.window makeFirstResponder:self];
        if (n->owner->pointer) n->owner->pointer(slot->id, n->pointerEvent(event, platform::PointerPhase::rightDown, {p.x - slot->frame.x, p.y - slot->frame.y}));
        return;
    }
    n->surface->rightMouseDown(p);
}
- (void)rightMouseDragged:(NSEvent*)event {
    auto* n = self.native;
    ui::Point p = location(self, event);
    if (n->captured.empty()) return;
    for (const auto& s : n->surface->viewports())
        if (s.id == n->captured && n->owner->pointer) n->owner->pointer(s.id, n->pointerEvent(event, platform::PointerPhase::rightDragged, {p.x - s.frame.x, p.y - s.frame.y}));
    n->lastPoint = p;
    n->viewsDirty = true;
}
- (void)rightMouseUp:(NSEvent*)event {
    auto* n = self.native;
    ui::Point p = location(self, event);
    if (n->captured.empty()) return;
    for (const auto& s : n->surface->viewports())
        if (s.id == n->captured && n->owner->pointer) n->owner->pointer(s.id, n->pointerEvent(event, platform::PointerPhase::rightUp, {p.x - s.frame.x, p.y - s.frame.y}));
    n->captured.clear();
    n->rightCaptured = false;
}
- (void)otherMouseDown:(NSEvent*)event {
    auto* n = self.native;
    ui::Point p = location(self, event);
    n->lastPoint = p;
    auto slot = n->slotAt(p);
    if (!slot || n->surface->covers(p)) return;
    n->captured = slot->id;
    [self.window makeFirstResponder:self];
    if (n->owner->pointer) n->owner->pointer(slot->id, n->pointerEvent(event, platform::PointerPhase::otherDown, {p.x - slot->frame.x, p.y - slot->frame.y}));
}
- (void)otherMouseDragged:(NSEvent*)event {
    auto* n = self.native;
    ui::Point p = location(self, event);
    if (n->captured.empty()) return;
    for (const auto& s : n->surface->viewports())
        if (s.id == n->captured && n->owner->pointer) n->owner->pointer(s.id, n->pointerEvent(event, platform::PointerPhase::otherDragged, {p.x - s.frame.x, p.y - s.frame.y}));
    n->lastPoint = p;
    n->viewsDirty = true;
}
- (void)otherMouseUp:(NSEvent*)event {
    auto* n = self.native;
    ui::Point p = location(self, event);
    if (n->captured.empty()) return;
    for (const auto& s : n->surface->viewports())
        if (s.id == n->captured && n->owner->pointer) n->owner->pointer(s.id, n->pointerEvent(event, platform::PointerPhase::otherUp, {p.x - s.frame.x, p.y - s.frame.y}));
    n->captured.clear();
}
- (void)mouseMoved:(NSEvent*)event {
    auto* n = self.native;
    ui::Point p = location(self, event);
    n->surface->mouseMoved(p);
    auto slot = n->slotAt(p);
    std::string inside = slot && !n->surface->covers(p) ? slot->id : std::string();
    if (!n->hovering.empty() && n->hovering != inside && n->owner->pointer && n->owner->pointer(n->hovering, n->pointerEvent(event, platform::PointerPhase::exited, {-1, -1}))) n->viewsDirty = true;
    n->hovering = inside;
    if (!inside.empty() && n->owner->pointer && n->owner->pointer(inside, n->pointerEvent(event, platform::PointerPhase::moved, {p.x - slot->frame.x, p.y - slot->frame.y}))) n->viewsDirty = true;
    n->lastPoint = p;
    [cursor(n->surface->cursor()) set];
    NSString* tip = text(n->surface->tooltip());
    if (![self.toolTip ?: @"" isEqualToString:tip]) self.toolTip = tip.length ? tip : nil;
}
- (void)mouseExited:(NSEvent*)event {
    auto* n = self.native;
    n->surface->mouseExited();
    n->hovering.clear();
    for (const auto& s : n->surface->viewports())
        if (n->owner->pointer && n->owner->pointer(s.id, n->pointerEvent(event, platform::PointerPhase::exited, {-1, -1}))) n->viewsDirty = true;
    [NSCursor.arrowCursor set];
}
- (void)scrollWheel:(NSEvent*)event {
    auto* n = self.native;
    ui::Point p = location(self, event);
    double factor = event.hasPreciseScrollingDeltas ? 1 : 10;
    auto slot = n->slotAt(p);
    if (slot && !n->surface->covers(p)) {
        if (n->owner->pointer) n->owner->pointer(slot->id, n->pointerEvent(event, platform::PointerPhase::scroll, {p.x - slot->frame.x, p.y - slot->frame.y}));
        n->viewsDirty = true;
        return;
    }
    n->surface->scrollWheel(p, event.scrollingDeltaX * factor, event.scrollingDeltaY * factor);
}
- (platform::KeyInput)keyInput:(NSEvent*)event down:(bool)down {
    platform::KeyInput k;
    k.keyCode = event.keyCode;
    k.characters = down || event.type == NSEventTypeKeyUp ? utf8(event.charactersIgnoringModifiers) : "";
    k.down = down;
    k.repeat = event.type == NSEventTypeKeyDown && event.isARepeat;
    k.command = event.modifierFlags & NSEventModifierFlagCommand;
    k.shift = event.modifierFlags & NSEventModifierFlagShift;
    k.option = event.modifierFlags & NSEventModifierFlagOption;
    k.control = event.modifierFlags & NSEventModifierFlagControl;
    return k;
}
- (void)keyDown:(NSEvent*)event {
    auto* n = self.native;
    platform::KeyInput k = [self keyInput:event down:true];
    if (n->owner->keys && n->owner->keys(k)) {
        n->viewsDirty = true;
        return;
    }
    ui::KeyEvent e{k.characters, k.keyCode, k.command, k.shift, k.option, k.control, k.repeat};
    if (!n->surface->key(e)) [super keyDown:event];
}
- (void)keyUp:(NSEvent*)event {
    auto* n = self.native;
    if (n->owner->keys) n->owner->keys([self keyInput:event down:false]);
}
- (BOOL)performKeyEquivalent:(NSEvent*)event {
    auto* n = self.native;
    if (event.type != NSEventTypeKeyDown || !(event.modifierFlags & NSEventModifierFlagCommand)) return [super performKeyEquivalent:event];
    NSResponder* responder = self.window.firstResponder;
    if ([responder isKindOfClass:[NSTextView class]] && responder != self) {
        NSString* key = event.charactersIgnoringModifiers.lowercaseString;
        if ([@[ @"z", @"x", @"c", @"v", @"a", @"f", @"g" ] containsObject:key]) return [super performKeyEquivalent:event];
    }
    platform::KeyInput k = [self keyInput:event down:true];
    if (n->owner->keys && n->owner->keys(k)) return YES;
    ui::KeyEvent e{k.characters, k.keyCode, k.command, k.shift, k.option, k.control, k.repeat};
    if (n->surface->key(e)) return YES;
    return [super performKeyEquivalent:event];
}
- (BOOL)resignFirstResponder {
    auto* n = self.native;
    if (n && n->owner->resignedViewport) n->owner->resignedViewport();
    return [super resignFirstResponder];
}
- (void)popupChanged:(NSPopUpButton*)sender {
    FieldEntry* entry = self.native->entryFor(sender);
    if (!entry || !entry->spec.choose) return;
    entry->spec.choose(static_cast<int>(sender.indexOfSelectedItem));
    self.native->surface->invalidate();
}
- (void)controlTextDidBeginEditing:(NSNotification*)notification {
    FieldEntry* entry = self.native->entryFor(notification.object);
    if (entry && entry->spec.focus) entry->spec.focus(true);
    self.native->surface->invalidate();
}
- (void)controlTextDidChange:(NSNotification*)notification {
    NSTextField* field = notification.object;
    FieldEntry* entry = self.native->entryFor(field);
    if (!entry) return;
    std::string value = utf8(field.stringValue);
    if (entry->spec.limit > 0 && field.stringValue.length > entry->spec.limit) {
        field.stringValue = [field.stringValue substringToIndex:entry->spec.limit];
        value = utf8(field.stringValue);
    }
    entry->spec.text = value;
    if (entry->spec.changed) entry->spec.changed(value);
    self.native->surface->invalidate();
}
- (void)controlTextDidEndEditing:(NSNotification*)notification {
    NSTextField* field = notification.object;
    FieldEntry* entry = self.native->entryFor(field);
    if (!entry) return;
    NSInteger movement = [notification.userInfo[@"NSTextMovement"] integerValue];
    ui::NativeField spec = entry->spec;
    if (spec.focus) spec.focus(false);
    if (movement == NSReturnTextMovement && spec.commit) spec.commit(utf8(field.stringValue));
    self.native->surface->invalidate();
}
- (BOOL)control:(NSControl*)control textView:(NSTextView*)textView doCommandBySelector:(SEL)selector {
    FieldEntry* entry = self.native->entryFor(control);
    if (entry && selector == @selector(cancelOperation:) && entry->spec.cancel) {
        entry->spec.cancel();
        self.native->surface->invalidate();
        return YES;
    }
    return NO;
}
- (void)textDidChange:(NSNotification*)notification {
    NSTextView* view = notification.object;
    FieldEntry* entry = self.native->entryFor(view.enclosingScrollView);
    if (!entry) return;
    if (entry->spec.limit > 0 && view.string.length > entry->spec.limit) view.string = [view.string substringToIndex:entry->spec.limit];
    entry->spec.text = utf8(view.string);
    self.native->applyHighlight(*entry, view);
    if (entry->spec.changed) entry->spec.changed(entry->spec.text);
    [view.enclosingScrollView.verticalRulerView setNeedsDisplay:YES];
    self.native->surface->invalidate();
}
- (void)textViewDidChangeSelection:(NSNotification*)notification {
    NSTextView* view = notification.object;
    FieldEntry* entry = self.native->entryFor(view.enclosingScrollView);
    if (!entry || !entry->spec.inspect) return;
    NSUInteger position = std::min(view.selectedRange.location, view.string.length);
    auto hint = entry->spec.inspect(utf8(view.string), utf8([view.string substringToIndex:position]).size());
    view.toolTip = hint.empty() ? nil : text(hint);
}
- (void)textDidBeginEditing:(NSNotification*)notification {
    NSTextView* view = notification.object;
    FieldEntry* entry = self.native->entryFor(view.enclosingScrollView);
    if (entry && entry->spec.focus) entry->spec.focus(true);
}
- (void)textDidEndEditing:(NSNotification*)notification {
    NSTextView* view = notification.object;
    FieldEntry* entry = self.native->entryFor(view.enclosingScrollView);
    if (entry && entry->spec.focus) entry->spec.focus(false);
}
@end

@interface LDWindowDelegate : NSObject <NSWindowDelegate>
@property(nonatomic, assign) platform::Window::Native* native;
@end

@implementation LDWindowDelegate
- (void)windowDidResize:(NSNotification*)notification {
    self.native->surface->invalidate();
    self.native->viewsDirty = true;
    self.native->frame(CACurrentMediaTime());
}
- (BOOL)windowShouldClose:(NSWindow*)sender {
    if (self.native->owner && self.native->owner->closing) return self.native->owner->closing() ? YES : NO;
    return YES;
}
- (void)windowDidChangeScreen:(NSNotification*)notification {
    if (self.native->link) CVDisplayLinkSetCurrentCGDisplay(self.native->link, [self.native->window.screen.deviceDescription[@"NSScreenNumber"] unsignedIntValue]);
}
@end

namespace platform {

FieldEntry* Window::Native::entryFor(NSView* view) {
    for (auto& [id, entry] : fields)
        if (entry.view == view) return &entry;
    return nullptr;
}

std::optional<ui::ViewportSlot> Window::Native::slotAt(ui::Point p) const {
    const auto& slots = surface->viewports();
    for (auto it = slots.rbegin(); it != slots.rend(); ++it)
        if (it->frame.contains(p)) return *it;
    return std::nullopt;
}

PointerEvent Window::Native::pointerEvent(NSEvent* event, PointerPhase phase, ui::Point local) const {
    PointerEvent e;
    e.phase = phase;
    e.location = local;
    if (phase == PointerPhase::scroll) {
        e.scrollX = event.scrollingDeltaX;
        e.scrollY = event.scrollingDeltaY;
        e.precise = event.hasPreciseScrollingDeltas;
    } else if (event.type != NSEventTypeMouseExited) {
        e.delta = {event.deltaX, event.deltaY};
        if (event.type == NSEventTypeLeftMouseDown || event.type == NSEventTypeRightMouseDown || event.type == NSEventTypeLeftMouseUp) e.clicks = static_cast<int>(event.clickCount);
    }
    NSEventModifierFlags flags = event.modifierFlags;
    e.command = flags & NSEventModifierFlagCommand;
    e.shift = flags & NSEventModifierFlagShift;
    e.option = flags & NSEventModifierFlagOption;
    e.control = flags & NSEventModifierFlagControl;
    return e;
}

void Window::Native::removeField(const std::string& id) {
    auto it = fields.find(id);
    if (it == fields.end()) return;
    if (it->second.boundsObserver) [[NSNotificationCenter defaultCenter] removeObserver:it->second.boundsObserver];
    NSResponder* responder = window.firstResponder;
    if ([responder isKindOfClass:[NSView class]] && [(NSView*)responder isDescendantOf:it->second.view]) [window makeFirstResponder:root];
    [it->second.view removeFromSuperview];
    [it->second.clipper removeFromSuperview];
    fields.erase(it);
}

void Window::Native::applyHighlight(FieldEntry& entry, NSTextView* view) {
    NSTextStorage* storage = view.textStorage;
    NSFont* code = [NSFont fontWithName:@"Menlo" size:12] ?: [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular];
    NSFont* bold = [[NSFontManager sharedFontManager] convertFont:code toHaveTrait:NSBoldFontMask];
    NSRange whole = NSMakeRange(0, storage.length);
    [storage beginEditing];
    [storage setAttributes:@{NSFontAttributeName : code, NSForegroundColorAttributeName : color(ui::theme::code::identifier)} range:whole];
    if (entry.spec.highlight) {
        std::string source = utf8(view.string);
        for (const auto& span : entry.spec.highlight(source)) {
            NSRange r = utf16Range(view.string, source, span.start, span.length);
            if (r.length == 0) continue;
            [storage addAttribute:NSForegroundColorAttributeName value:color(span.color) range:r];
            [storage addAttribute:NSFontAttributeName value:span.bold ? bold : code range:r];
        }
    }
    [storage endEditing];
}

void Window::Native::syncFields() {
    std::map<std::string, bool> present;
    for (const auto& spec : surface->fields()) {
        present[spec.id] = true;
        FieldKind kind = !spec.options.empty() ? FieldKind::popup : spec.code ? FieldKind::code : spec.secure ? FieldKind::secure : FieldKind::plain;
        auto it = fields.find(spec.id);
        if (it != fields.end() && it->second.kind != kind) {
            removeField(spec.id);
            it = fields.end();
        }
        if (it == fields.end()) {
            FieldEntry entry;
            entry.kind = kind;
            if (kind == FieldKind::code) {
                NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
                LDCodeTextView* view = [[LDCodeTextView alloc] initWithFrame:NSZeroRect];
                view.richText = NO;
                view.allowsUndo = YES;
                view.font = [NSFont fontWithName:@"Menlo" size:12];
                view.textColor = color(ui::theme::code::identifier);
                view.insertionPointColor = color(ui::theme::colors::accent);
                view.selectedTextAttributes = @{NSBackgroundColorAttributeName : color(ui::theme::colors::selection)};
                view.automaticQuoteSubstitutionEnabled = NO;
                view.automaticDashSubstitutionEnabled = NO;
                view.automaticTextReplacementEnabled = NO;
                view.automaticSpellingCorrectionEnabled = NO;
                view.continuousSpellCheckingEnabled = NO;
                view.usesFindBar = YES;
                view.incrementalSearchingEnabled = YES;
                view.textContainerInset = NSMakeSize(6, 8);
                view.verticallyResizable = YES;
                view.horizontallyResizable = NO;
                view.autoresizingMask = NSViewWidthSizable;
                view.minSize = NSMakeSize(0, 0);
                view.maxSize = NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX);
                view.textContainer.widthTracksTextView = YES;
                view.delegate = root;
                view.string = text(spec.text);
                entry.spec = spec;
                applyHighlight(entry, view);
                scroll.documentView = view;
                scroll.hasVerticalScroller = YES;
                scroll.drawsBackground = YES;
                scroll.borderType = NSNoBorder;
                LDLineRuler* ruler = [[LDLineRuler alloc] initWithScrollView:scroll orientation:NSVerticalRuler];
                ruler.clientView = view;
                ruler.ruleThickness = 40;
                ruler.errorLine = 0;
                scroll.verticalRulerView = ruler;
                scroll.hasVerticalRuler = YES;
                scroll.rulersVisible = YES;
                scroll.contentView.postsBoundsChangedNotifications = YES;
                entry.boundsObserver = [[NSNotificationCenter defaultCenter] addObserverForName:NSViewBoundsDidChangeNotification object:scroll.contentView queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification*) {
                    [ruler setNeedsDisplay:YES];
                }];
                entry.view = scroll;
            } else if (kind == FieldKind::popup) {
                NSPopUpButton* button = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
                button.transparent = YES;
                button.target = root;
                button.action = @selector(popupChanged:);
                entry.view = button;
            } else {
                NSTextField* field = kind == FieldKind::secure ? [[NSSecureTextField alloc] initWithFrame:NSZeroRect] : [[NSTextField alloc] initWithFrame:NSZeroRect];
                field.delegate = root;
                field.bezeled = NO;
                field.bordered = NO;
                field.drawsBackground = NO;
                field.focusRingType = NSFocusRingTypeNone;
                field.usesSingleLineMode = YES;
                field.lineBreakMode = NSLineBreakByTruncatingTail;
                field.cell.scrollable = YES;
                field.stringValue = text(spec.text);
                entry.view = field;
            }
            entry.clipper = [[LDClipView alloc] initWithFrame:NSZeroRect];
            entry.clipper.wantsLayer = YES;
            entry.clipper.layer.masksToBounds = YES;
            [root addSubview:entry.clipper];
            [entry.clipper addSubview:entry.view];
            it = fields.emplace(spec.id, std::move(entry)).first;
        }
        FieldEntry& entry = it->second;
        entry.spec = spec;
        const ui::Frame& placed = spec.full.empty() ? spec.frame : spec.full;
        NSRect clipRect = spec.clip.empty() ? root.bounds : NSMakeRect(spec.clip.x, spec.clip.y, spec.clip.width, spec.clip.height);
        if (!NSEqualRects(entry.clipper.frame, clipRect)) entry.clipper.frame = clipRect;
        NSRect frame = NSMakeRect(placed.x, placed.y, placed.width, placed.height);
        if ((kind == FieldKind::plain || kind == FieldKind::secure) && !spec.bordered) {
            NSTextField* field = (NSTextField*)entry.view;
            NSFont* typeface = font(spec.font);
            if (![field.font isEqual:typeface]) field.font = typeface;
            double height = std::max(placed.height, field.intrinsicContentSize.height);
            if (!NSEqualSizes(field.frame.size, NSMakeSize(placed.width + 4, height))) [field setFrameSize:NSMakeSize(placed.width + 4, height)];
            frame = NSMakeRect(placed.x - 2, placed.y + typeface.ascender - typeface.capHeight - inkTop(typeface), placed.width + 4, height);
        }
        frame = NSOffsetRect(frame, -clipRect.origin.x, -clipRect.origin.y);
        if (!NSEqualRects(entry.view.frame, frame)) entry.view.frame = frame;
        if (kind == FieldKind::popup) {
            NSPopUpButton* button = (NSPopUpButton*)entry.view;
            NSMutableArray* titles = [NSMutableArray array];
            for (const auto& option : spec.options) [titles addObject:text(option)];
            if (![button.itemTitles isEqualToArray:titles]) {
                [button removeAllItems];
                for (NSString* title in titles) {
                    [button addItemWithTitle:@""];
                    button.lastItem.title = title;
                }
            }
            if (spec.selectedOption >= 0 && spec.selectedOption < button.numberOfItems && button.indexOfSelectedItem != spec.selectedOption) [button selectItemAtIndex:spec.selectedOption];
            button.enabled = spec.editable;
            continue;
        }
        if (kind == FieldKind::code) {
            NSScrollView* scroll = (NSScrollView*)entry.view;
            LDCodeTextView* view = (LDCodeTextView*)scroll.documentView;
            bool pointsChanged = view->codeSpec.breakpoints != spec.breakpoints;
            view->codeSpec = spec;
            view.allowsUndo = !spec.historyAction;
            if (pointsChanged) [scroll.verticalRulerView setNeedsDisplay:YES];
            NSString* desired = text(spec.text);
            if (![view.string isEqualToString:desired]) {
                view.string = desired;
                applyHighlight(entry, view);
            } else if (view.textStorage.length > 0 && ![view.textStorage attribute:NSFontAttributeName atIndex:0 effectiveRange:nil]) applyHighlight(entry, view);
            view.editable = spec.editable;
            view.backgroundColor = color(spec.editable ? ui::theme::code::background : ui::theme::colors::surfaceAlt);
            scroll.backgroundColor = view.backgroundColor;
            LDLineRuler* ruler = (LDLineRuler*)scroll.verticalRulerView;
            NSInteger error = spec.errorLine.value_or(0);
            if (ruler.errorLine != error) {
                ruler.errorLine = error;
                [ruler setNeedsDisplay:YES];
            }
            if (spec.revealLine && !entry.revealApplied) {
                entry.revealApplied = true;
                NSString* content = view.string;
                NSUInteger location = 0;
                for (int i = 1; i < std::max(1, *spec.revealLine); ++i) {
                    NSUInteger next = NSMaxRange([content lineRangeForRange:NSMakeRange(location, 0)]);
                    if (next <= location || next > content.length) break;
                    location = next;
                }
                NSRange range = [content lineRangeForRange:NSMakeRange(std::min<NSUInteger>(location, content.length), 0)];
                [window makeFirstResponder:view];
                [view setSelectedRange:NSMakeRange(range.location, range.length > 0 ? range.length - 1 : 0)];
                [view scrollRangeToVisible:range];
            } else if (!spec.revealLine) entry.revealApplied = false;
            if (spec.find != ui::FindAction::none && !entry.findApplied) {
                entry.findApplied = true;
                [window makeFirstResponder:view];
                NSTextFinderAction action = spec.find == ui::FindAction::find ? NSTextFinderActionShowFindInterface : spec.find == ui::FindAction::replace ? NSTextFinderActionShowReplaceInterface : spec.find == ui::FindAction::next ? NSTextFinderActionNextMatch : NSTextFinderActionPreviousMatch;
                [view find:action];
            } else if (spec.find == ui::FindAction::none) entry.findApplied = false;
        } else {
            NSTextField* field = (NSTextField*)entry.view;
            field.font = font(spec.font);
            field.textColor = color(spec.color);
            field.alignment = spec.alignment == ui::TextAlign::center ? NSTextAlignmentCenter : spec.alignment == ui::TextAlign::trailing ? NSTextAlignmentRight : NSTextAlignmentLeft;
            NSAttributedString* prompt = [[NSAttributedString alloc] initWithString:text(spec.prompt) attributes:@{NSFontAttributeName : font(spec.font), NSForegroundColorAttributeName : color(ui::theme::colors::textTertiary)}];
            if (![[field valueForKey:@"placeholderAttributedString"] isEqual:prompt]) [field setValue:prompt forKey:@"placeholderAttributedString"];
            field.editable = spec.editable;
            field.selectable = spec.editable;
            field.enabled = spec.editable;
            if (spec.bordered != field.bezeled) {
                field.bezeled = spec.bordered;
                field.bezelStyle = NSTextFieldRoundedBezel;
                field.drawsBackground = spec.bordered;
            }
            NSText* editor = field.currentEditor;
            NSString* desired = text(spec.text);
            if (!editor && ![field.stringValue isEqualToString:desired]) field.stringValue = desired;
            if (editor && ![editor.string isEqualToString:desired] && spec.text.empty()) editor.string = desired;
        }
    }
    std::vector<std::string> stale;
    for (const auto& [id, entry] : fields)
        if (!present.count(id)) stale.push_back(id);
    for (const auto& id : stale) removeField(id);
    std::string focused;
    NSResponder* responder = window.firstResponder;
    if ([responder isKindOfClass:[NSView class]])
        for (const auto& [key, entry] : fields)
            if (entry.kind != FieldKind::popup && [(NSView*)responder isDescendantOf:entry.view]) focused = key;
    if ([responder isKindOfClass:[NSTextView class]] && [(NSTextView*)responder isFieldEditor]) {
        NSObject* owner = (NSObject*)((NSTextView*)responder).delegate;
        for (const auto& [key, entry] : fields)
            if (owner == entry.view) focused = key;
    }
    surface->setFocusedField(focused);
}

void Window::Native::paintInterface(void* context) {
    NSSize size = root.bounds.size;
    double scale = window.backingScaleFactor > 0 ? window.backingScaleFactor : 2;
    surface->resize({size.width, size.height}, scale);
    ui::Canvas canvas(context, std::max(1, static_cast<int>(size.width)), std::max(1, static_cast<int>(size.height)), scale);
    surface->render(canvas);
}

void Window::Native::renderInterface() {
    NSSize size = root.bounds.size;
    double scale = window.backingScaleFactor > 0 ? window.backingScaleFactor : 2;
    surface->resize({size.width, size.height}, scale);
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    [overlayView setNeedsDisplay:YES];
    [overlayView displayIfNeeded];
    [CATransaction commit];
    drawn = {size.width, size.height};
    drawnScale = scale;
    syncFields();
    if (owner->rendered) owner->rendered(*surface);
    NSPoint mouse = [root convertPoint:[window mouseLocationOutsideOfEventStream] fromView:nil];
    if (NSPointInRect(mouse, root.bounds) && window.isKeyWindow) [cursor(surface->cursor()) set];
}

void Window::Native::frame(double now) {
    if (framing) return;
    framing = true;
    struct Release {
        bool& flag;
        ~Release() { flag = false; }
    } release{framing};
    double refresh = 1.0 / std::max<NSInteger>(30, window.screen ? window.screen.maximumFramesPerSecond : 60);
    FrameSample sample;
    sample.interval = last > 0 ? now - last : refresh;
    double started = CACurrentMediaTime();
    double delta = pacer.next(last > 0 ? now - last : refresh, refresh);
    last = now;
    if (owner->tick) owner->tick(now, delta);
    sample.tick = CACurrentMediaTime() - started;
    NSSize size = root.bounds.size;
    double scale = window.backingScaleFactor > 0 ? window.backingScaleFactor : 2;
    bool resized = drawn.width != size.width || drawn.height != size.height || drawnScale != scale;
    bool interfaceDirty = surface->needsDisplay() || surface->wantsFrame() || resized;
    double interfaceStarted = CACurrentMediaTime();
    if (interfaceDirty) renderInterface();
    sample.interfaceRendered = interfaceDirty;
    sample.interface = CACurrentMediaTime() - interfaceStarted;
    double presentStarted = CACurrentMediaTime();
    const auto& slots = surface->viewports();
    if (!slots.empty() && owner->views) {
        bool continuous = owner->continuous ? owner->continuous() : true;
        if (continuous || viewsDirty || interfaceDirty) {
            CGSize drawable = CGSizeMake(size.width * scale, size.height * scale);
            if (!CGSizeEqualToSize(metal.drawableSize, drawable)) metal.drawableSize = drawable;
            metal.contentsScale = scale;
            ld::render::Renderer::shared().present((__bridge void*)metal, owner->views(slots), scale);
            viewsDirty = false;
            hadViews = true;
        }
    } else if (hadViews) {
        ld::render::Renderer::shared().present((__bridge void*)metal, {}, scale);
        hadViews = false;
    }
    if (owner->sampled) {
        double finished = CACurrentMediaTime();
        sample.present = finished - presentStarted;
        sample.total = finished - started;
        owner->sampled(sample);
    }
}

Window::Window(WindowSpec spec, std::function<ui::NodePtr()> build) : native(std::make_unique<Native>()) {
    auto* n = native.get();
    n->owner = this;
    n->dispatch->owner = n;
    n->surface = std::make_unique<ui::Surface>(std::move(build));
    NSRect rect = NSMakeRect(0, 0, spec.width, spec.height);
    NSWindowStyleMask style = spec.sheet ? NSWindowStyleMaskTitled : NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
    n->window = [[NSWindow alloc] initWithContentRect:rect styleMask:style backing:NSBackingStoreBuffered defer:NO];
    n->window.title = text(spec.title);
    n->window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
    n->window.contentMinSize = NSMakeSize(spec.minimumWidth, spec.minimumHeight);
    n->window.tabbingMode = NSWindowTabbingModeDisallowed;
    n->window.releasedWhenClosed = NO;
    n->window.acceptsMouseMovedEvents = YES;
    n->window.backgroundColor = color(ui::theme::colors::window);
    LDRootView* root = [[LDRootView alloc] initWithFrame:rect];
    root.native = n;
    root.wantsLayer = YES;
    root.layer.backgroundColor = color(ui::theme::colors::window).CGColor;
    n->root = root;
    n->metalView = [[LDPassiveView alloc] initWithFrame:rect];
    n->metalView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    n->metal = [CAMetalLayer layer];
    n->metal.device = (__bridge id<MTLDevice>)ld::render::Renderer::shared().device();
    n->metal.pixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
    n->metal.framebufferOnly = YES;
    n->metal.opaque = NO;
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    n->metal.colorspace = space;
    CGColorSpaceRelease(space);
    n->metalView.layer = n->metal;
    n->metalView.wantsLayer = YES;
    LDInterfaceView* overlay = [[LDInterfaceView alloc] initWithFrame:rect];
    overlay.native = n;
    n->overlayView = overlay;
    n->overlayView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    n->overlayView.wantsLayer = YES;
    n->overlayView.layerContentsRedrawPolicy = NSViewLayerContentsRedrawOnSetNeedsDisplay;
    n->overlayView.layer.opaque = NO;
    [root addSubview:n->metalView];
    [root addSubview:n->overlayView];
    n->window.contentView = root;
    LDWindowDelegate* delegate = [[LDWindowDelegate alloc] init];
    delegate.native = n;
    n->window.delegate = delegate;
    objc_setAssociatedObject(n->window, "LDWindowDelegate", delegate, OBJC_ASSOCIATION_RETAIN);
    [n->window center];
    if (CVDisplayLinkCreateWithActiveCGDisplays(&n->link) == kCVReturnSuccess) {
        CVDisplayLinkSetOutputCallback(n->link, displayCallback, n);
        CVDisplayLinkStart(n->link);
    }
}

Window::~Window() {
    if (native->link) {
        CVDisplayLinkStop(native->link);
        CVDisplayLinkRelease(native->link);
    }
    ld::render::Renderer::shared().release((__bridge void*)native->metal);
    native->dispatch->owner = nullptr;
    native->root.native = nullptr;
    static_cast<LDInterfaceView*>(native->overlayView).native = nullptr;
    for (auto& [id, entry] : native->fields) {
        if (entry.boundsObserver) [[NSNotificationCenter defaultCenter] removeObserver:entry.boundsObserver];
        if (entry.kind == FieldKind::code) ((NSTextView*)((NSScrollView*)entry.view).documentView).delegate = nil;
        else if (entry.kind != FieldKind::popup) ((NSTextField*)entry.view).delegate = nil;
    }
    native->window.delegate = nil;
    [native->window close];
}

ui::Surface& Window::surface() { return *native->surface; }

void Window::setTitle(const std::string& title) {
    NSString* value = text(title);
    if (![native->window.title isEqualToString:value]) native->window.title = value;
}

void Window::setEdited(bool edited) {
    if (native->window.documentEdited != edited) native->window.documentEdited = edited;
}

void Window::invalidate() {
    native->surface->invalidate();
    native->viewsDirty = true;
}

void Window::show() {
    [native->window makeKeyAndOrderFront:nil];
    native->frame(CACurrentMediaTime());
}

void Window::focusViewport() { [native->window makeFirstResponder:native->root]; }

void Window::beginSheet(Window& sheet) {
    if (native->window.attachedSheet == sheet.native->window) return;
    sheet.native->frame(CACurrentMediaTime());
    [native->window beginSheet:sheet.native->window completionHandler:nil];
}

void Window::endSheet(Window& sheet) {
    if (native->window.attachedSheet != sheet.native->window) return;
    [native->window endSheet:sheet.native->window];
}

void Window::focusField(const std::string& id) {
    auto it = native->fields.find(id);
    if (it == native->fields.end()) return;
    NSView* view = it->second.view;
    if ([view isKindOfClass:[NSScrollView class]]) view = ((NSScrollView*)view).documentView;
    [native->window makeFirstResponder:view];
}

bool Window::editingText() const {
    NSResponder* responder = native->window.firstResponder;
    return [responder isKindOfClass:[NSTextView class]];
}

double Window::scale() const { return native->window.backingScaleFactor; }

}
