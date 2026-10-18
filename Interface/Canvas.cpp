#include "Canvas.hpp"

#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>
#include <ImageIO/ImageIO.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ui {

namespace {

template <typename T>
struct Owned {
    T value = nullptr;
    Owned() = default;
    explicit Owned(T v) : value(v) {}
    ~Owned() {
        if (value) CFRelease(value);
    }
    Owned(const Owned&) = delete;
    Owned& operator=(const Owned&) = delete;
    operator T() const { return value; }
};

CGColorSpaceRef rgb() {
    static CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    return space;
}

CGColorRef makeColor(Color c) {
    CGFloat components[4] = {c.r, c.g, c.b, c.a};
    return CGColorCreate(rgb(), components);
}

CTFontRef font(const Font& f) {
    static std::mutex lock;
    static std::map<std::string, CTFontRef> cache;
    std::string key = f.family + "|" + std::to_string(f.size) + "|" + std::to_string(static_cast<int>(f.weight));
    std::lock_guard<std::mutex> guard(lock);
    if (auto it = cache.find(key); it != cache.end()) return it->second;
    CTFontRef result = nullptr;
    if (f.family == "Helvetica Neue") {
        const char* face = f.weight == Weight::light ? "HelveticaNeue-Light" : f.weight == Weight::regular ? "HelveticaNeue" : "HelveticaNeue-Bold";
        Owned<CFStringRef> name(CFStringCreateWithCString(nullptr, face, kCFStringEncodingUTF8));
        result = CTFontCreateWithName(name, f.size, nullptr);
    } else if (!f.family.empty()) {
        double weight = f.weight == Weight::light ? -0.4 : f.weight == Weight::bold ? 0.4 : f.weight == Weight::semibold ? 0.3 : f.weight == Weight::medium ? 0.23 : 0;
        Owned<CFStringRef> family(CFStringCreateWithCString(nullptr, f.family.c_str(), kCFStringEncodingUTF8));
        Owned<CFNumberRef> number(CFNumberCreate(nullptr, kCFNumberDoubleType, &weight));
        const void* traitKeys[] = {kCTFontWeightTrait};
        const void* traitValues[] = {number.value};
        Owned<CFDictionaryRef> traits(CFDictionaryCreate(nullptr, traitKeys, traitValues, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
        const void* keys[] = {kCTFontFamilyNameAttribute, kCTFontTraitsAttribute};
        const void* values[] = {family.value, traits.value};
        Owned<CFDictionaryRef> attributes(CFDictionaryCreate(nullptr, keys, values, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
        Owned<CTFontDescriptorRef> descriptor(CTFontDescriptorCreateWithAttributes(attributes));
        result = CTFontCreateWithFontDescriptor(descriptor, f.size, nullptr);
    } else {
        CTFontRef base = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, f.size, nullptr);
        if (f.weight == Weight::regular) result = base;
        else {
            double weight = f.weight == Weight::bold ? 0.4 : f.weight == Weight::semibold ? 0.3 : 0.23;
            Owned<CFNumberRef> number(CFNumberCreate(nullptr, kCFNumberDoubleType, &weight));
            const void* traitKeys[] = {kCTFontWeightTrait};
            const void* traitValues[] = {number.value};
            Owned<CFDictionaryRef> traits(CFDictionaryCreate(nullptr, traitKeys, traitValues, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
            Owned<CFStringRef> family(CTFontCopyFamilyName(base));
            const void* keys[] = {kCTFontFamilyNameAttribute, kCTFontTraitsAttribute};
            const void* values[] = {family.value, traits.value};
            Owned<CFDictionaryRef> attributes(CFDictionaryCreate(nullptr, keys, values, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
            Owned<CTFontDescriptorRef> descriptor(CTFontDescriptorCreateWithAttributes(attributes));
            result = CTFontCreateWithFontDescriptor(descriptor, f.size, nullptr);
            CFRelease(base);
        }
    }
    cache[key] = result;
    return result;
}

}

void* fontHandle(const Font& f) { return const_cast<void*>(static_cast<const void*>(font(f))); }

std::string postScriptName(const Font& f) {
    CTFontRef made = font(f);
    if (!made) return "";
    Owned<CFStringRef> name(CTFontCopyPostScriptName(made));
    char buffer[256] = {0};
    if (!name.value || !CFStringGetCString(name, buffer, sizeof buffer, kCFStringEncodingUTF8)) return "";
    return buffer;
}

namespace {

CFAttributedStringRef attributed(const std::vector<TextRun>& runs, bool underline) {
    CFMutableAttributedStringRef out = CFAttributedStringCreateMutable(nullptr, 0);
    CFAttributedStringBeginEditing(out);
    for (const auto& run : runs) {
        Owned<CFStringRef> text(CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8*>(run.text.data()), static_cast<CFIndex>(run.text.size()), kCFStringEncodingUTF8, false));
        if (!text.value) continue;
        CFIndex start = CFAttributedStringGetLength(out);
        CFAttributedStringReplaceString(out, CFRangeMake(start, 0), text);
        CFRange range = CFRangeMake(start, CFStringGetLength(text));
        CFAttributedStringSetAttribute(out, range, kCTFontAttributeName, font(run.font));
        Owned<CGColorRef> color(makeColor(run.color));
        CFAttributedStringSetAttribute(out, range, kCTForegroundColorAttributeName, color);
        if (underline) {
            int style = kCTUnderlineStyleSingle;
            Owned<CFNumberRef> number(CFNumberCreate(nullptr, kCFNumberIntType, &style));
            CFAttributedStringSetAttribute(out, range, kCTUnderlineStyleAttributeName, number);
        }
    }
    CFAttributedStringEndEditing(out);
    return out;
}

struct Line {
    CTLineRef line = nullptr;
    double width = 0;
    double ascent = 0, descent = 0, leading = 0;
};

struct Layout {
    std::vector<Line> lines;
    double width = 0;
    double height = 0;
    ~Layout() {
        for (auto& l : lines)
            if (l.line) CFRelease(l.line);
    }
};

double exact(double value) { return std::round(value * 1000) / 1000; }

void measure(Line& l) {
    CGFloat a = 0, d = 0, g = 0;
    double w = CTLineGetTypographicBounds(l.line, &a, &d, &g);
    l.width = w - CTLineGetTrailingWhitespaceWidth(l.line);
    l.ascent = a;
    l.descent = d;
    l.leading = g;
}

CTLineRef truncated(CFAttributedStringRef string, CFRange range, double width, Truncation mode, const Font& reference, Color color) {
    Owned<CFAttributedStringRef> part(CFAttributedStringCreateWithSubstring(nullptr, string, range));
    CTLineRef line = CTLineCreateWithAttributedString(part);
    if (mode == Truncation::none || CTLineGetTypographicBounds(line, nullptr, nullptr, nullptr) - CTLineGetTrailingWhitespaceWidth(line) <= width + 0.01) return line;
    Owned<CFAttributedStringRef> ellipsis(attributed({TextRun{"…", reference, color}}, false));
    Owned<CTLineRef> token(CTLineCreateWithAttributedString(ellipsis));
    CTLineTruncationType type = mode == Truncation::head ? kCTLineTruncationStart : mode == Truncation::middle ? kCTLineTruncationMiddle : kCTLineTruncationEnd;
    CTLineRef result = CTLineCreateTruncatedLine(line, width, type, token);
    if (result) {
        CFRelease(line);
        return result;
    }
    return line;
}

void layout(Layout& out, CFAttributedStringRef string, std::optional<double> width, int lineLimit, Truncation truncation, const Font& reference, Color color) {
    CFIndex length = CFAttributedStringGetLength(string);
    CFStringRef plain = CFAttributedStringGetString(string);
    Owned<CTTypesetterRef> typesetter(CTTypesetterCreateWithAttributedString(string));
    CFIndex start = 0;
    if (length == 0) {
        Line l;
        Owned<CFAttributedStringRef> probe(attributed({TextRun{" ", reference, color}}, false));
        l.line = CTLineCreateWithAttributedString(probe);
        measure(l);
        l.width = 0;
        out.lines.push_back(l);
    }
    while (start < length) {
        CFIndex count;
        if (width) count = CTTypesetterSuggestLineBreak(typesetter, start, std::max(1.0, *width));
        else {
            CFRange found;
            if (CFStringFindWithOptions(plain, CFSTR("\n"), CFRangeMake(start, length - start), 0, &found)) count = found.location - start + 1;
            else count = length - start;
        }
        if (count <= 0) count = 1;
        bool last = lineLimit > 0 && static_cast<int>(out.lines.size()) == lineLimit - 1 && start + count < length;
        Line l;
        if (last && width) l.line = truncated(string, CFRangeMake(start, length - start), *width, truncation, reference, color);
        else if (width && lineLimit == 1) l.line = truncated(string, CFRangeMake(start, count), *width, truncation, reference, color);
        else l.line = CTTypesetterCreateLine(typesetter, CFRangeMake(start, count));
        measure(l);
        out.lines.push_back(l);
        start += count;
        if (last) break;
        if (lineLimit > 0 && static_cast<int>(out.lines.size()) >= lineLimit) break;
    }
    if (length > 0) {
        UniChar lastCharacter = CFStringGetCharacterAtIndex(plain, length - 1);
        if (lastCharacter == '\n' && (lineLimit == 0 || static_cast<int>(out.lines.size()) < lineLimit)) {
            Line l;
            Owned<CFAttributedStringRef> probe(attributed({TextRun{" ", reference, color}}, false));
            l.line = CTLineCreateWithAttributedString(probe);
            measure(l);
            l.width = 0;
            out.lines.push_back(l);
        }
    }
    for (const auto& l : out.lines) {
        out.width = std::max(out.width, l.width);
        out.height += exact(l.ascent + l.descent + l.leading);
    }
}

std::vector<TextRun> single(const std::string& text, const TextStyle& style) { return {TextRun{text, style.font, style.color}}; }

double fittingScale(const std::vector<TextRun>& runs, const TextStyle& style, double width) {
    if (style.minimumScale >= 1) return 1;
    for (double scale = 1; scale >= style.minimumScale; scale -= 0.05) {
        std::vector<TextRun> scaled = runs;
        for (auto& r : scaled) r.font.size = r.font.size * scale;
        Owned<CFAttributedStringRef> string(attributed(scaled, false));
        Layout l;
        layout(l, string, std::nullopt, 0, Truncation::none, scaled.front().font, style.color);
        if (l.width <= width + 0.01) return scale;
    }
    return style.minimumScale;
}

}

double lineHeight(const Font& f) {
    CTFontRef ct = font(f);
    return exact(CTFontGetAscent(ct) + CTFontGetDescent(ct) + CTFontGetLeading(ct));
}

namespace {

Size typeset(const std::vector<TextRun>& runs, const TextStyle& style, std::optional<double> width);

}

Size measureRuns(const std::vector<TextRun>& runs, const TextStyle& style, std::optional<double> width) {
    if (runs.empty()) return {0, lineHeight(style.font)};
    std::string key;
    std::size_t length = 0;
    for (const auto& r : runs) length += r.text.size();
    if (length > 512) return typeset(runs, style, width);
    auto number = [&](double value) { key += std::to_string(std::lround(value * 64)) + ','; };
    for (const auto& r : runs) {
        key += r.text;
        key += '\x1f';
        key += r.font.family;
        key += '\x1f';
        number(r.font.size);
        number(static_cast<int>(r.font.weight));
        number(r.font.monospaced);
    }
    number(style.lineLimit);
    number(static_cast<int>(style.truncation));
    number(style.underline);
    number(style.minimumScale);
    number(width ? *width : -1);
    static std::mutex lock;
    static std::unordered_map<std::string, Size> cache;
    {
        std::lock_guard<std::mutex> guard(lock);
        auto found = cache.find(key);
        if (found != cache.end()) return found->second;
    }
    Size measured = typeset(runs, style, width);
    std::lock_guard<std::mutex> guard(lock);
    if (cache.size() > 16384) cache.clear();
    cache.emplace(std::move(key), measured);
    return measured;
}

namespace {

Size typeset(const std::vector<TextRun>& runs, const TextStyle& style, std::optional<double> width) {
    std::vector<TextRun> used = runs;
    if (width && style.minimumScale < 1) {
        double s = fittingScale(runs, style, *width);
        for (auto& r : used) r.font.size *= s;
    }
    Owned<CFAttributedStringRef> string(attributed(used, style.underline));
    Layout l;
    layout(l, string, width, style.lineLimit, style.truncation, used.front().font, style.color);
    double w = std::ceil(l.width * 2) / 2;
    if (width) w = std::min(w, *width);
    return {w, l.height};
}

}

Size measureText(const std::string& text, const TextStyle& style, std::optional<double> width) { return measureRuns(single(text, style), style, width); }

Image readImage(const std::filesystem::path& path) {
    Image out;
    auto key = path.string();
    Owned<CFURLRef> url(CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(key.c_str()), static_cast<CFIndex>(key.size()), false));
    Owned<CGImageSourceRef> source(url.value ? CGImageSourceCreateWithURL(url, nullptr) : nullptr);
    if (!source.value) return out;
    CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
    if (!image) return out;
    out.handle = std::shared_ptr<void>(const_cast<CGImage*>(image), [](void* p) { CGImageRelease(static_cast<CGImageRef>(p)); });
    out.pixelsWide = static_cast<int>(CGImageGetWidth(image));
    out.pixelsHigh = static_cast<int>(CGImageGetHeight(image));
    return out;
}

Image loadImage(const std::filesystem::path& path) {
    static std::mutex lock;
    static std::map<std::string, Image> cache;
    std::lock_guard<std::mutex> guard(lock);
    auto key = path.string();
    if (auto it = cache.find(key); it != cache.end()) return it->second;
    Image out = readImage(path);
    cache[key] = out;
    return out;
}

Image imageFromRgba(const std::vector<std::uint8_t>& rgba, int width, int height, bool opaque) {
    Image out;
    if (width <= 0 || height <= 0 || rgba.size() < static_cast<std::size_t>(width * height * 4)) return out;
    Owned<CFDataRef> data(CFDataCreate(nullptr, rgba.data(), static_cast<CFIndex>(width * height * 4)));
    Owned<CGDataProviderRef> provider(CGDataProviderCreateWithCFData(data));
    CGImageRef image = CGImageCreate(static_cast<size_t>(width), static_cast<size_t>(height), 8, 32, static_cast<size_t>(width * 4), rgb(), static_cast<CGBitmapInfo>(opaque ? kCGImageAlphaNoneSkipLast : kCGImageAlphaPremultipliedLast), provider, nullptr, false, kCGRenderingIntentDefault);
    if (!image) return out;
    out.handle = std::shared_ptr<void>(const_cast<CGImage*>(image), [](void* p) { CGImageRelease(static_cast<CGImageRef>(p)); });
    out.pixelsWide = width;
    out.pixelsHigh = height;
    return out;
}

Image resampled(const Image& picture, int width, int height) {
    Image out;
    if (!picture.valid() || width <= 0 || height <= 0) return out;
    Owned<CGContextRef> context(CGBitmapContextCreate(nullptr, static_cast<size_t>(width), static_cast<size_t>(height), 8, 0, rgb(), static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast)));
    if (!context.value) return out;
    CGContextSetInterpolationQuality(context, kCGInterpolationHigh);
    CGContextDrawImage(context, CGRectMake(0, 0, width, height), static_cast<CGImageRef>(picture.handle.get()));
    CGImageRef made = CGBitmapContextCreateImage(context);
    if (!made) return out;
    out.handle = std::shared_ptr<void>(const_cast<CGImage*>(made), [](void* p) { CGImageRelease(static_cast<CGImageRef>(p)); });
    out.pixelsWide = width;
    out.pixelsHigh = height;
    return out;
}

Canvas::Canvas(int width, int height, double scale) : pixelsWide(std::max(1, static_cast<int>(std::lround(width * scale)))), pixelsHigh(std::max(1, static_cast<int>(std::lround(height * scale)))), factor(scale) {
    buffer.assign(static_cast<std::size_t>(pixelsWide) * static_cast<std::size_t>(pixelsHigh) * 4, 0);
    CGContextRef c = CGBitmapContextCreate(buffer.data(), static_cast<size_t>(pixelsWide), static_cast<size_t>(pixelsHigh), 8, static_cast<size_t>(pixelsWide * 4), rgb(), static_cast<uint32_t>(kCGImageAlphaPremultipliedLast) | static_cast<uint32_t>(kCGBitmapByteOrder32Big));
    CGContextTranslateCTM(c, 0, pixelsHigh);
    CGContextScaleCTM(c, scale, -scale);
    CGContextSetShouldAntialias(c, true);
    CGContextSetShouldSmoothFonts(c, false);
    cg = c;
}

Canvas::Canvas(void* context, int width, int height, double scale)
    : pixelsWide(std::max(1, static_cast<int>(std::lround(width * scale)))), pixelsHigh(std::max(1, static_cast<int>(std::lround(height * scale)))), factor(scale), cg(context), borrowed(true) {
    CGContextRef c = static_cast<CGContextRef>(context);
    CGContextSetShouldAntialias(c, true);
    CGContextSetShouldSmoothFonts(c, false);
}

Canvas::~Canvas() {
    if (cg && !borrowed) CGContextRelease(static_cast<CGContextRef>(cg));
}

const std::uint8_t* Canvas::pixels() const { return borrowed ? nullptr : buffer.data(); }

std::shared_ptr<void> Canvas::snapshot() const {
    if (borrowed) return nullptr;
    CGImageRef image = CGBitmapContextCreateImage(static_cast<CGContextRef>(cg));
    return std::shared_ptr<void>(const_cast<CGImage*>(image), [](void* p) {
        if (p) CGImageRelease(static_cast<CGImageRef>(p));
    });
}

bool Canvas::writePng(const std::filesystem::path& path) const {
    auto image = snapshot();
    if (!image) return false;
    std::string p = path.string();
    Owned<CFURLRef> url(CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(p.c_str()), static_cast<CFIndex>(p.size()), false));
    Owned<CGImageDestinationRef> destination(CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, nullptr));
    if (!destination.value) return false;
    CGImageDestinationAddImage(destination, static_cast<CGImageRef>(image.get()), nullptr);
    return CGImageDestinationFinalize(destination);
}

Color Canvas::pixel(double x, double y) const {
    if (borrowed) return Color::clear();
    int px = std::clamp(static_cast<int>(x * factor), 0, pixelsWide - 1), py = std::clamp(static_cast<int>(y * factor), 0, pixelsHigh - 1);
    const std::uint8_t* p = buffer.data() + (static_cast<std::size_t>(py) * static_cast<std::size_t>(pixelsWide) + static_cast<std::size_t>(px)) * 4;
    double a = p[3] / 255.0;
    if (a <= 0) return Color::clear();
    return {p[0] / 255.0 / a, p[1] / 255.0 / a, p[2] / 255.0 / a, a};
}

namespace {

CGContextRef C(void* c) { return static_cast<CGContextRef>(c); }

CGPathRef rounded(const Frame& r, double radius) {
    CGRect rect = CGRectMake(r.x, r.y, r.width, r.height);
    double limit = std::min(r.width, r.height) / 2;
    radius = std::max(0.0, std::min(radius, limit));
    if (radius <= 0) return CGPathCreateWithRect(rect, nullptr);
    return CGPathCreateWithRoundedRect(rect, radius, radius, nullptr);
}

}

void Canvas::clear(Color color) {
    if (color.a <= 0) {
        if (!borrowed) std::fill(buffer.begin(), buffer.end(), std::uint8_t(0));
        return;
    }
    CGContextSaveGState(C(cg));
    CGContextSetBlendMode(C(cg), kCGBlendModeCopy);
    Owned<CGColorRef> c(makeColor(color));
    CGContextSetFillColorWithColor(C(cg), c);
    CGContextFillRect(C(cg), CGRectMake(0, 0, pixelsWide / factor, pixelsHigh / factor));
    CGContextRestoreGState(C(cg));
}

void Canvas::save() { CGContextSaveGState(C(cg)); }
void Canvas::restore() { CGContextRestoreGState(C(cg)); }

void Canvas::clip(const Frame& rect, double radius) {
    Owned<CGPathRef> path(rounded(rect, radius));
    CGContextAddPath(C(cg), path);
    CGContextClip(C(cg));
}

void Canvas::translate(double dx, double dy) { CGContextTranslateCTM(C(cg), dx, dy); }
void Canvas::setAlpha(double alpha) { CGContextSetAlpha(C(cg), alpha); }

namespace {

Frame reach(const Frame& rect) { return {rect.x - 32, rect.y - 32, rect.width + 64, rect.height + 64}; }

}

void Canvas::fill(const Frame& rect, Color color, double radius) {
    if (!color.visible() || rect.empty() || !intersectsClip(reach(rect))) return;
    Owned<CGColorRef> c(makeColor(color));
    CGContextSetFillColorWithColor(C(cg), c);
    Owned<CGPathRef> path(rounded(rect, radius));
    CGContextAddPath(C(cg), path);
    CGContextFillPath(C(cg));
}

void Canvas::fillEllipse(const Frame& rect, Color color) {
    if (!color.visible()) return;
    Owned<CGColorRef> c(makeColor(color));
    CGContextSetFillColorWithColor(C(cg), c);
    CGContextFillEllipseInRect(C(cg), CGRectMake(rect.x, rect.y, rect.width, rect.height));
}

namespace {

std::shared_ptr<CGGradient> gradientOf(const Gradient& gradient) {
    struct Entry {
        std::vector<CGFloat> key;
        std::shared_ptr<CGGradient> value;
    };
    static std::mutex lock;
    static std::vector<Entry> cache;
    std::vector<CGFloat> components, locations;
    for (const auto& stop : gradient.stops) {
        components.insert(components.end(), {stop.color.r, stop.color.g, stop.color.b, stop.color.a});
        locations.push_back(stop.position);
    }
    std::vector<CGFloat> key = components;
    key.insert(key.end(), locations.begin(), locations.end());
    std::lock_guard<std::mutex> guard(lock);
    for (auto it = cache.begin(); it != cache.end(); ++it)
        if (it->key == key) {
            std::rotate(cache.begin(), it, it + 1);
            return cache.front().value;
        }
    CGGradientRef made = CGGradientCreateWithColorComponents(rgb(), components.data(), locations.data(), locations.size());
    if (!made) return nullptr;
    std::shared_ptr<CGGradient> owned(made, [](CGGradient* g) { CGGradientRelease(g); });
    cache.insert(cache.begin(), Entry{std::move(key), owned});
    if (cache.size() > 128) cache.pop_back();
    return owned;
}

}

void Canvas::fillGradient(const Frame& rect, const Gradient& gradient, double radius) {
    if (rect.empty() || gradient.stops.empty() || !intersectsClip(rect)) return;
    auto g = gradientOf(gradient);
    if (!g) return;
    CGContextSaveGState(C(cg));
    clip(rect, radius);
    CGPoint a = CGPointMake(rect.x, rect.y), b = gradient.diagonal ? CGPointMake(rect.maxX(), rect.maxY()) : gradient.vertical ? CGPointMake(rect.x, rect.maxY()) : CGPointMake(rect.maxX(), rect.y);
    CGContextDrawLinearGradient(C(cg), g.get(), a, b, 0);
    CGContextRestoreGState(C(cg));
}

void Canvas::stroke(const Frame& rect, Color color, double width, double radius) {
    if (!color.visible() || width <= 0 || !intersectsClip(reach(rect))) return;
    Owned<CGColorRef> c(makeColor(color));
    CGContextSetStrokeColorWithColor(C(cg), c);
    CGContextSetLineWidth(C(cg), width);
    Owned<CGPathRef> path(rounded(rect.inset(width / 2, width / 2), std::max(0.0, radius - width / 2)));
    CGContextAddPath(C(cg), path);
    CGContextStrokePath(C(cg));
}

void Canvas::strokeEllipse(const Frame& rect, Color color, double width) {
    Owned<CGColorRef> c(makeColor(color));
    CGContextSetStrokeColorWithColor(C(cg), c);
    CGContextSetLineWidth(C(cg), width);
    Frame r = rect.inset(width / 2, width / 2);
    CGContextStrokeEllipseInRect(C(cg), CGRectMake(r.x, r.y, r.width, r.height));
}

void Canvas::line(Point a, Point b, Color color, double width) {
    Owned<CGColorRef> c(makeColor(color));
    CGContextSetStrokeColorWithColor(C(cg), c);
    CGContextSetLineWidth(C(cg), width);
    CGContextMoveToPoint(C(cg), a.x, a.y);
    CGContextAddLineToPoint(C(cg), b.x, b.y);
    CGContextStrokePath(C(cg));
}

void Canvas::polygons(const std::vector<std::vector<Point>>& shapes, Color color, bool evenOdd) {
    Owned<CGColorRef> c(makeColor(color));
    CGContextSetFillColorWithColor(C(cg), c);
    for (const auto& points : shapes) {
        if (points.empty()) continue;
        CGContextMoveToPoint(C(cg), points[0].x, points[0].y);
        for (std::size_t i = 1; i < points.size(); ++i) CGContextAddLineToPoint(C(cg), points[i].x, points[i].y);
        CGContextClosePath(C(cg));
    }
    if (evenOdd) CGContextEOFillPath(C(cg));
    else CGContextFillPath(C(cg));
}

void Canvas::path(const PathStep* steps, int count, const Frame& rect, double unit, Color color) {
    if (!color.visible() || rect.empty() || unit <= 0) return;
    double sx = rect.width / unit, sy = rect.height / unit;
    Owned<CGColorRef> c(makeColor(color));
    CGContextSetFillColorWithColor(C(cg), c);
    for (int i = 0; i < count; ++i) {
        const PathStep& s = steps[i];
        double x = rect.x + s.x * sx, y = rect.y + s.y * sy;
        if (s.kind == 'M') CGContextMoveToPoint(C(cg), x, y);
        else if (s.kind == 'L') CGContextAddLineToPoint(C(cg), x, y);
        else if (s.kind == 'C') CGContextAddCurveToPoint(C(cg), rect.x + s.x1 * sx, rect.y + s.y1 * sy, rect.x + s.x2 * sx, rect.y + s.y2 * sy, x, y);
        else if (s.kind == 'Q') CGContextAddQuadCurveToPoint(C(cg), rect.x + s.x1 * sx, rect.y + s.y1 * sy, x, y);
        else CGContextClosePath(C(cg));
    }
    CGContextFillPath(C(cg));
}

void Canvas::text(const std::string& value, const Frame& rect, const TextStyle& style) { runs(single(value, style), rect, style); }

namespace {

std::shared_ptr<const Layout> arrange(const std::vector<TextRun>& used, const TextStyle& style, double width) {
    auto build = [&] {
        auto made = std::make_shared<Layout>();
        Owned<CFAttributedStringRef> string(attributed(used, style.underline));
        layout(*made, string, width, style.lineLimit, style.truncation, used.front().font, style.color);
        return made;
    };
    std::size_t length = 0;
    for (const auto& r : used) length += r.text.size();
    if (length > 512) return build();
    std::string key;
    auto number = [&](double value) { key += std::to_string(std::lround(value * 255)) + ','; };
    for (const auto& r : used) {
        key += r.text;
        key += '\x1f';
        key += r.font.family;
        key += '\x1f';
        number(r.font.size);
        number(static_cast<int>(r.font.weight));
        number(r.font.monospaced);
        number(r.color.r);
        number(r.color.g);
        number(r.color.b);
        number(r.color.a);
    }
    number(style.lineLimit);
    number(static_cast<int>(style.truncation));
    number(style.underline);
    number(style.color.r);
    number(style.color.g);
    number(style.color.b);
    number(style.color.a);
    number(width);
    static std::mutex lock;
    static std::unordered_map<std::string, std::shared_ptr<const Layout>> cache;
    {
        std::lock_guard<std::mutex> guard(lock);
        auto found = cache.find(key);
        if (found != cache.end()) return found->second;
    }
    std::shared_ptr<const Layout> made = build();
    std::lock_guard<std::mutex> guard(lock);
    if (cache.size() > 8192) cache.clear();
    cache.emplace(std::move(key), made);
    return made;
}

}

void Canvas::runs(const std::vector<TextRun>& input, const Frame& rect, const TextStyle& style) {
    if (input.empty() || !intersectsClip(rect)) return;
    std::vector<TextRun> used = input;
    if (style.minimumScale < 1) {
        double s = fittingScale(input, style, rect.width);
        for (auto& r : used) r.font.size *= s;
    }
    std::shared_ptr<const Layout> arranged = arrange(used, style, rect.width);
    const Layout& l = *arranged;
    CGContextRef c = C(cg);
    CGContextSaveGState(c);
    CGContextSetTextMatrix(c, CGAffineTransformMakeScale(1, -1));
    double y = rect.y;
    for (const auto& line : l.lines) {
        double x = rect.x;
        if (style.alignment == TextAlign::center) x += (rect.width - line.width) / 2;
        else if (style.alignment == TextAlign::trailing) x += rect.width - line.width;
        double baseline = std::round((y + line.ascent) * factor) / factor;
        CGContextSetTextPosition(c, x, baseline);
        CTLineDraw(line.line, c);
        y += exact(line.ascent + line.descent + line.leading);
    }
    CGContextRestoreGState(c);
}

namespace {

struct Reduced {
    std::weak_ptr<void> source;
    const void* key = nullptr;
    CGColorSpaceRef space = nullptr;
    CGBitmapInfo format = 0;
    std::size_t width = 0, height = 0;
    std::shared_ptr<CGImage> image;
};

CGColorSpaceRef spaceOf(CGContextRef context) {
    CGColorSpaceRef space = CGBitmapContextGetColorSpace(context);
    return space && CGColorSpaceGetModel(space) == kCGColorSpaceModelRGB ? space : rgb();
}

CGBitmapInfo formatOf(CGContextRef context) {
    CGBitmapInfo info = CGBitmapContextGetBitmapInfo(context);
    CGImageAlphaInfo alpha = static_cast<CGImageAlphaInfo>(info & kCGBitmapAlphaInfoMask);
    bool usable = CGBitmapContextGetBitsPerComponent(context) == 8 && CGBitmapContextGetBitsPerPixel(context) == 32 && (alpha == kCGImageAlphaPremultipliedFirst || alpha == kCGImageAlphaPremultipliedLast) && !(info & kCGBitmapFloatComponents);
    return usable ? info : static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast);
}

std::shared_ptr<CGImage> reduced(const Image& picture, std::size_t width, std::size_t height, CGColorSpaceRef space, CGBitmapInfo format) {
    static std::mutex lock;
    static std::vector<Reduced> cache;
    std::lock_guard<std::mutex> guard(lock);
    for (auto it = cache.begin(); it != cache.end(); ++it)
        if (it->key == picture.handle.get() && !it->source.expired() && it->width == width && it->height == height && CFEqual(it->space, space) && it->format == format) {
            std::rotate(cache.begin(), it, it + 1);
            return cache.front().image;
        }
    Owned<CGContextRef> context(CGBitmapContextCreate(nullptr, width, height, 8, 0, space, format));
    if (!context.value) return nullptr;
    CGContextSetInterpolationQuality(context, kCGInterpolationHigh);
    CGContextDrawImage(context, CGRectMake(0, 0, static_cast<CGFloat>(width), static_cast<CGFloat>(height)), static_cast<CGImageRef>(picture.handle.get()));
    CGImageRef made = CGBitmapContextCreateImage(context);
    if (!made) return nullptr;
    std::shared_ptr<CGImage> owned(const_cast<CGImage*>(made), [](CGImage* p) { CGImageRelease(p); });
    CGColorSpaceRetain(space);
    cache.insert(cache.begin(), Reduced{picture.handle, picture.handle.get(), space, format, width, height, owned});
    if (cache.size() > 2) {
        CGColorSpaceRelease(cache.back().space);
        cache.pop_back();
    }
    return owned;
}

}

void Canvas::image(const Image& picture, const Frame& rect, std::optional<Color> tint, double opacity) {
    if (!picture.valid() || rect.empty() || !intersectsClip(reach(rect))) return;
    CGContextRef c = C(cg);
    CGImageRef image = static_cast<CGImageRef>(picture.handle.get());
    CGContextSaveGState(c);
    CGContextSetAlpha(c, opacity);
    CGContextSetInterpolationQuality(c, kCGInterpolationHigh);
    CGContextTranslateCTM(c, rect.x, rect.maxY());
    CGContextScaleCTM(c, 1, -1);
    CGRect local = CGRectMake(0, 0, rect.width, rect.height);
    if (tint) {
        CGContextClipToMask(c, local, image);
        Owned<CGColorRef> color(makeColor(*tint));
        CGContextSetFillColorWithColor(c, color);
        CGContextFillRect(c, local);
    } else {
        CGRect device = CGContextConvertRectToDeviceSpace(c, local);
        double width = std::round(std::abs(device.size.width)), height = std::round(std::abs(device.size.height));
        std::shared_ptr<CGImage> smaller;
        if (width >= 1 && height >= 1 && picture.pixelsWide > width && picture.pixelsHigh > height && static_cast<double>(picture.pixelsWide) * picture.pixelsHigh >= 1048576)
            smaller = reduced(picture, static_cast<std::size_t>(width), static_cast<std::size_t>(height), spaceOf(c), formatOf(c));
        if (smaller) CGContextSetInterpolationQuality(c, kCGInterpolationNone);
        CGContextDrawImage(c, local, smaller ? smaller.get() : image);
    }
    CGContextRestoreGState(c);
}

void Canvas::pixelImage(const Image& picture, const Frame& rect) {
    if (!picture.valid() || rect.empty() || !intersectsClip(reach(rect))) return;
    CGContextRef c = C(cg);
    CGPoint corner = CGContextConvertPointToDeviceSpace(c, CGPointMake(rect.x, rect.y));
    CGPoint snapped = CGContextConvertPointToUserSpace(c, CGPointMake(std::round(corner.x), std::round(corner.y)));
    CGSize size = CGContextConvertSizeToUserSpace(c, CGSizeMake(picture.pixelsWide, picture.pixelsHigh));
    double width = std::abs(size.width), height = std::abs(size.height);
    CGContextSaveGState(c);
    CGContextSetInterpolationQuality(c, kCGInterpolationNone);
    CGContextTranslateCTM(c, snapped.x, snapped.y + height);
    CGContextScaleCTM(c, 1, -1);
    CGContextDrawImage(c, CGRectMake(0, 0, width, height), static_cast<CGImageRef>(picture.handle.get()));
    CGContextRestoreGState(c);
}

void Canvas::imageMultiplied(const Image& picture, const Frame& rect, Color tint, double opacity) {
    if (!picture.valid() || rect.empty() || !intersectsClip(reach(rect))) return;
    CGContextRef c = C(cg);
    CGImageRef image = static_cast<CGImageRef>(picture.handle.get());
    CGContextSaveGState(c);
    CGContextSetAlpha(c, opacity);
    CGContextBeginTransparencyLayerWithRect(c, CGRectMake(rect.x, rect.y, rect.width, rect.height), nullptr);
    CGContextTranslateCTM(c, rect.x, rect.maxY());
    CGContextScaleCTM(c, 1, -1);
    CGRect local = CGRectMake(0, 0, rect.width, rect.height);
    CGContextSetInterpolationQuality(c, kCGInterpolationHigh);
    CGContextDrawImage(c, local, image);
    CGContextClipToMask(c, local, image);
    CGContextSetBlendMode(c, kCGBlendModeMultiply);
    Owned<CGColorRef> color(makeColor(tint));
    CGContextSetFillColorWithColor(c, color);
    CGContextFillRect(c, local);
    CGContextEndTransparencyLayer(c);
    CGContextRestoreGState(c);
}

void Canvas::shadow(double dx, double dy, double blur, Color color) {
    Owned<CGColorRef> c(makeColor(color));
    CGContextSetShadowWithColor(C(cg), CGSizeMake(dx * factor, -dy * factor), blur * factor, c);
}

namespace {

struct BoxShadow {
    CGColorSpaceRef space = nullptr;
    CGBitmapInfo format = 0;
    long width = 0, height = 0, radius = 0, blur = 0, dx = 0, dy = 0;
    Color color;
    std::shared_ptr<CGImage> image;
    long pad = 0;
};

}

void Canvas::innerShadow(const Frame& rect, double radius, double dy, double blur, Color color) {
    if (rect.empty() || !color.visible() || !intersectsClip(rect)) return;
    long width = std::lround(rect.width * factor), height = std::lround(rect.height * factor);
    long r = std::lround(radius * factor), b = std::lround(blur * factor), oy = std::lround(dy * factor);
    if (width <= 0 || height <= 0) return;
    static std::mutex lock;
    static std::vector<BoxShadow> cache;
    std::shared_ptr<CGImage> picture;
    CGColorSpaceRef space = spaceOf(C(cg));
    CGBitmapInfo format = formatOf(C(cg));
    {
        std::lock_guard<std::mutex> guard(lock);
        for (auto it = cache.begin(); it != cache.end(); ++it)
            if (CFEqual(it->space, space) && it->format == format && it->width == width && it->height == height && it->radius == r && it->blur == b && it->dy == oy && it->color.r == color.r && it->color.g == color.g && it->color.b == color.b && it->color.a == color.a) {
                std::rotate(cache.begin(), it, it + 1);
                picture = cache.front().image;
                break;
            }
        if (!picture) {
            Owned<CGContextRef> context(CGBitmapContextCreate(nullptr, static_cast<std::size_t>(width), static_cast<std::size_t>(height), 8, 0, space, format));
            if (!context.value) return;
            CGRect box = CGRectMake(0, 0, static_cast<double>(width), static_cast<double>(height));
            Owned<CGPathRef> shape(CGPathCreateWithRoundedRect(box, std::min<double>(r, width / 2.0), std::min<double>(r, height / 2.0), nullptr));
            CGContextAddPath(context, shape);
            CGContextClip(context);
            double reach = static_cast<double>(b) * 2 + std::abs(static_cast<double>(oy)) + 4;
            CGMutablePathRef ring = CGPathCreateMutable();
            CGPathAddRect(ring, nullptr, CGRectInset(box, -reach, -reach));
            CGPathAddPath(ring, nullptr, shape);
            Owned<CGColorRef> tint(makeColor(color));
            CGContextSetShadowWithColor(context, CGSizeMake(0, -static_cast<double>(oy)), static_cast<double>(b), tint);
            Owned<CGColorRef> solid(makeColor({color.r, color.g, color.b, 1}));
            CGContextSetFillColorWithColor(context, solid);
            CGContextAddPath(context, ring);
            CGContextEOFillPath(context);
            CGPathRelease(ring);
            CGImageRef made = CGBitmapContextCreateImage(context);
            if (!made) return;
            picture = std::shared_ptr<CGImage>(const_cast<CGImage*>(made), [](CGImage* p) { CGImageRelease(p); });
            CGColorSpaceRetain(space);
            cache.insert(cache.begin(), BoxShadow{space, format, width, height, r, b, 0, oy, color, picture, 0});
            if (cache.size() > 96) {
                CGColorSpaceRelease(cache.back().space);
                cache.pop_back();
            }
        }
    }
    CGContextRef c = C(cg);
    CGContextSaveGState(c);
    CGContextTranslateCTM(c, rect.x, rect.y + rect.height);
    CGContextScaleCTM(c, 1, -1);
    CGContextSetInterpolationQuality(c, kCGInterpolationNone);
    CGContextDrawImage(c, CGRectMake(0, 0, rect.width, rect.height), picture.get());
    CGContextRestoreGState(c);
}

void Canvas::stripes(const Frame& rect, Color base, Color stripe, double width, double period) {
    if (rect.empty() || !intersectsClip(rect)) return;
    fill(rect, base);
    CGContextRef c = C(cg);
    Owned<CGColorRef> ink(makeColor(stripe));
    CGContextSetFillColorWithColor(c, ink);
    double start = std::floor(rect.x / period) * period;
    for (double x = start; x < rect.maxX(); x += period) {
        double left = std::max(x, rect.x), right = std::min(x + width, rect.maxX());
        if (right > left) CGContextFillRect(c, CGRectMake(left, rect.y, right - left, rect.height));
    }
}

void Canvas::reflection(const Image& picture, const Frame& source, double height, double opacity) {
    if (!picture.valid() || source.empty() || height <= 0) return;
    Frame area{source.x, source.maxY(), source.width, height};
    if (!intersectsClip(area)) return;
    CGContextRef c = C(cg);
    CGContextSaveGState(c);
    beginLayer(area);
    CGContextSaveGState(c);
    CGContextClipToRect(c, CGRectMake(area.x, area.y, area.width, area.height));
    CGContextTranslateCTM(c, source.x, source.maxY());
    CGContextDrawImage(c, CGRectMake(0, 0, source.width, source.height), static_cast<CGImageRef>(picture.handle.get()));
    CGContextRestoreGState(c);
    CGContextSetBlendMode(c, kCGBlendModeDestinationIn);
    CGFloat components[8] = {1, 1, 1, static_cast<CGFloat>(opacity), 1, 1, 1, 0};
    CGFloat locations[2] = {0, 1};
    Owned<CGGradientRef> fade(CGGradientCreateWithColorComponents(rgb(), components, locations, 2));
    CGContextDrawLinearGradient(c, fade, CGPointMake(area.x, area.y), CGPointMake(area.x, area.maxY()), 0);
    endLayer();
    CGContextRestoreGState(c);
}

void Canvas::arrowShape(const Frame& rect, double arrow, double radius, const Gradient& gradient, Color border, Color light) {
    if (rect.empty() || !intersectsClip(rect)) return;
    CGContextRef c = C(cg);
    auto outline = [&](const Frame& r) {
        CGMutablePathRef path = CGPathCreateMutable();
        CGPathMoveToPoint(path, nullptr, r.x, r.midY());
        CGPathAddLineToPoint(path, nullptr, r.x + arrow, r.y);
        CGPathAddArcToPoint(path, nullptr, r.maxX(), r.y, r.maxX(), r.maxY(), radius);
        CGPathAddArcToPoint(path, nullptr, r.maxX(), r.maxY(), r.x, r.maxY(), radius);
        CGPathAddLineToPoint(path, nullptr, r.x + arrow, r.maxY());
        CGPathCloseSubpath(path);
        return path;
    };
    Frame inner = rect.inset(0.5, 0.5);
    CGPathRef shape = outline(inner);
    CGContextSaveGState(c);
    CGContextAddPath(c, shape);
    CGContextClip(c);
    std::vector<CGFloat> components, locations;
    for (const auto& stop : gradient.stops) {
        components.insert(components.end(), {stop.color.r, stop.color.g, stop.color.b, stop.color.a});
        locations.push_back(stop.position);
    }
    Owned<CGGradientRef> g(CGGradientCreateWithColorComponents(rgb(), components.data(), locations.data(), locations.size()));
    CGContextDrawLinearGradient(c, g, CGPointMake(rect.x, rect.y), CGPointMake(rect.x, rect.maxY()), 0);
    Owned<CGColorRef> shine(makeColor(light));
    CGContextSetStrokeColorWithColor(c, shine);
    CGContextSetLineWidth(c, 1);
    CGContextMoveToPoint(c, inner.x + arrow, inner.y + 1);
    CGContextAddLineToPoint(c, inner.maxX() - radius, inner.y + 1);
    CGContextStrokePath(c);
    CGContextRestoreGState(c);
    Owned<CGColorRef> edge(makeColor(border));
    CGContextSetStrokeColorWithColor(c, edge);
    CGContextSetLineWidth(c, 1);
    CGContextAddPath(c, shape);
    CGContextStrokePath(c);
    CGPathRelease(shape);
}

void Canvas::gloss(const Frame& rect, double radius, double top, double bottom) { shine(rect, radius, {rect.x - rect.width * 0.3, rect.y - rect.height * 0.58, rect.width * 1.6, rect.height * 1.04}, top, bottom); }

void Canvas::shine(const Frame& rect, double radius, const Frame& ellipse, double top, double bottom) {
    if (rect.empty() || !intersectsClip(rect)) return;
    CGContextRef c = C(cg);
    CGContextSaveGState(c);
    Owned<CGPathRef> shape(rounded(rect, radius));
    CGContextAddPath(c, shape);
    CGContextClip(c);
    CGContextAddEllipseInRect(c, CGRectMake(ellipse.x, ellipse.y, ellipse.width, ellipse.height));
    CGContextClip(c);
    CGFloat components[8] = {1, 1, 1, static_cast<CGFloat>(top), 1, 1, 1, static_cast<CGFloat>(bottom)};
    CGFloat locations[2] = {0, 1};
    Owned<CGGradientRef> light(CGGradientCreateWithColorComponents(rgb(), components, locations, 2));
    CGContextDrawLinearGradient(c, light, CGPointMake(rect.x, rect.y), CGPointMake(rect.x, std::max(rect.y + 1, ellipse.maxY())), 0);
    CGContextRestoreGState(c);
}

void Canvas::boxShadow(const Frame& rect, double radius, double dx, double dy, double blur, Color color) {
    if (rect.empty()) return;
    long pad = static_cast<long>(std::ceil((blur * 2 + std::abs(dx) + std::abs(dy)) * factor)) + 2;
    long width = std::lround(rect.width * factor), height = std::lround(rect.height * factor);
    long r = std::lround(radius * factor), b = std::lround(blur * factor), ox = std::lround(dx * factor), oy = std::lround(dy * factor);
    Frame reachable{rect.x - pad / factor, rect.y - pad / factor, rect.width + pad * 2 / factor, rect.height + pad * 2 / factor};
    if (width <= 0 || height <= 0 || !intersectsClip(reachable)) return;
    static std::mutex lock;
    static std::vector<BoxShadow> cache;
    std::shared_ptr<CGImage> picture;
    CGColorSpaceRef space = spaceOf(C(cg));
    CGBitmapInfo format = formatOf(C(cg));
    {
        std::lock_guard<std::mutex> guard(lock);
        for (auto it = cache.begin(); it != cache.end(); ++it)
            if (CFEqual(it->space, space) && it->format == format && it->width == width && it->height == height && it->radius == r && it->blur == b && it->dx == ox && it->dy == oy && it->color.r == color.r && it->color.g == color.g && it->color.b == color.b && it->color.a == color.a) {
                std::rotate(cache.begin(), it, it + 1);
                picture = cache.front().image;
                break;
            }
        if (!picture) {
            std::size_t w = static_cast<std::size_t>(width + pad * 2), h = static_cast<std::size_t>(height + pad * 2);
            Owned<CGContextRef> context(CGBitmapContextCreate(nullptr, w, h, 8, 0, space, format));
            if (!context.value) return;
            double away = static_cast<double>(w) + static_cast<double>(pad) * 4;
            Owned<CGColorRef> shade(makeColor(color));
            CGContextSetShadowWithColor(context, CGSizeMake(away + static_cast<double>(ox), -static_cast<double>(oy)), static_cast<double>(b), shade);
            Owned<CGPathRef> path(CGPathCreateWithRoundedRect(CGRectMake(static_cast<double>(pad) - away, static_cast<double>(pad), static_cast<double>(width), static_cast<double>(height)), std::min<double>(r, width / 2.0), std::min<double>(r, height / 2.0), nullptr));
            CGContextAddPath(context, path);
            CGContextSetRGBFillColor(context, 0, 0, 0, 1);
            CGContextFillPath(context);
            CGImageRef made = CGBitmapContextCreateImage(context);
            if (!made) return;
            picture = std::shared_ptr<CGImage>(const_cast<CGImage*>(made), [](CGImage* p) { CGImageRelease(p); });
            CGColorSpaceRetain(space);
            cache.insert(cache.begin(), BoxShadow{space, format, width, height, r, b, ox, oy, color, picture, pad});
            if (cache.size() > 96) {
                CGColorSpaceRelease(cache.back().space);
                cache.pop_back();
            }
        }
    }
    CGContextRef c = C(cg);
    CGContextSaveGState(c);
    CGRect target = CGRectMake(rect.x - pad / factor, rect.y - pad / factor, (width + pad * 2) / factor, (height + pad * 2) / factor);
    CGContextTranslateCTM(c, target.origin.x, target.origin.y + target.size.height);
    CGContextScaleCTM(c, 1, -1);
    CGContextSetInterpolationQuality(c, kCGInterpolationNone);
    long w = width + pad * 2, h = height + pad * 2;
    long edge = pad + r + std::max(std::labs(ox), std::labs(oy)) + 2;
    if (w - edge * 2 < 64 || h - edge * 2 < 64) CGContextDrawImage(c, CGRectMake(0, 0, target.size.width, target.size.height), picture.get());
    else {
        auto part = [&](long x, long y, long pw, long ph) {
            CGImageRef piece = CGImageCreateWithImageInRect(picture.get(), CGRectMake(static_cast<double>(x), static_cast<double>(y), static_cast<double>(pw), static_cast<double>(ph)));
            if (!piece) return;
            CGContextDrawImage(c, CGRectMake(x / factor, (h - y - ph) / factor, pw / factor, ph / factor), piece);
            CGImageRelease(piece);
        };
        part(0, 0, w, edge);
        part(0, h - edge, w, edge);
        part(0, edge, edge, h - edge * 2);
        part(w - edge, edge, edge, h - edge * 2);
    }
    CGContextRestoreGState(c);
}

void Canvas::erase(const Frame& rect) { CGContextClearRect(C(cg), CGRectMake(rect.x, rect.y, rect.width, rect.height)); }

void Canvas::beginLayer(const Frame& bounds) { CGContextBeginTransparencyLayerWithRect(C(cg), CGRectMake(bounds.x, bounds.y, bounds.width, bounds.height), nullptr); }

bool Canvas::intersectsClip(const Frame& bounds) const {
    CGRect clip = CGContextGetClipBoundingBox(C(cg));
    return CGRectIntersectsRect(clip, CGRectMake(bounds.x, bounds.y, bounds.width, bounds.height));
}

void Canvas::endLayer() { CGContextEndTransparencyLayer(C(cg)); }

}
