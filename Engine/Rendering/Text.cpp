#include "Engine/Rendering/Renderer.hpp"

#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>

#include <cmath>

namespace ld::render {

std::shared_ptr<Texture> textTexture(const std::string& text, const std::string& fontName, double pixelHeight, Color color) {
    CFStringRef name = CFStringCreateWithCString(nullptr, fontName.c_str(), kCFStringEncodingUTF8);
    CTFontRef font = CTFontCreateWithName(name, pixelHeight, nullptr);
    CFRelease(name);
    CFStringRef string = CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8*>(text.data()), static_cast<CFIndex>(text.size()), kCFStringEncodingUTF8, false);
    if (!string) string = CFStringCreateWithCString(nullptr, "", kCFStringEncodingUTF8);
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGFloat components[4] = {color.r, color.g, color.b, color.a};
    CGColorRef fill = CGColorCreate(space, components);
    const void* keys[] = {kCTFontAttributeName, kCTForegroundColorAttributeName};
    const void* values[] = {font, fill};
    CFDictionaryRef attributes = CFDictionaryCreate(nullptr, keys, values, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFAttributedStringRef attributed = CFAttributedStringCreate(nullptr, string, attributes);
    CTLineRef line = CTLineCreateWithAttributedString(attributed);
    CGFloat ascent = 0, descent = 0, leading = 0;
    double width = CTLineGetTypographicBounds(line, &ascent, &descent, &leading);
    int w = std::max(1, static_cast<int>(std::ceil(width)) + 2), h = std::max(1, static_cast<int>(std::ceil(ascent + descent)) + 2);
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w * h * 4), 0);
    CGContextRef context = CGBitmapContextCreate(pixels.data(), static_cast<size_t>(w), static_cast<size_t>(h), 8, static_cast<size_t>(w * 4), space, static_cast<uint32_t>(kCGImageAlphaPremultipliedLast) | static_cast<uint32_t>(kCGBitmapByteOrder32Big));
    CGContextSetShouldAntialias(context, true);
    CGContextSetTextPosition(context, 1, 1 + descent);
    CTLineDraw(line, context);
    CGContextRelease(context);
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        double a = pixels[i + 3] / 255.0;
        if (a <= 0) continue;
        for (int c = 0; c < 3; ++c) pixels[i + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(std::min(255.0, std::round(pixels[i + static_cast<std::size_t>(c)] / a)));
    }
    CFRelease(line);
    CFRelease(attributed);
    CFRelease(attributes);
    CGColorRelease(fill);
    CGColorSpaceRelease(space);
    CFRelease(string);
    CFRelease(font);
    return Texture::make(std::move(pixels), w, h);
}

}
