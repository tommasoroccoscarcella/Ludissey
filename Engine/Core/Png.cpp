#include "Png.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include <set>

namespace ld {

std::string stripPng(const std::string& png) {
    if (png.size() < 8 || png.compare(0, 8, "\x89PNG\r\n\x1a\n") != 0) return png;
    auto big = [&](std::size_t at) {
        return (static_cast<std::uint32_t>(static_cast<unsigned char>(png[at])) << 24) | (static_cast<std::uint32_t>(static_cast<unsigned char>(png[at + 1])) << 16) |
               (static_cast<std::uint32_t>(static_cast<unsigned char>(png[at + 2])) << 8) | static_cast<std::uint32_t>(static_cast<unsigned char>(png[at + 3]));
    };
    static const std::set<std::string> kept{"IHDR", "PLTE", "tRNS", "IDAT", "IEND", "sRGB", "gAMA", "cHRM", "iCCP", "pHYs"};
    std::string output = png.substr(0, 8);
    for (std::size_t at = 8; at + 12 <= png.size();) {
        std::uint32_t length = big(at);
        if (at + 12 + length > png.size()) return png;
        std::string kind = png.substr(at + 4, 4);
        if (kept.count(kind)) output += png.substr(at, 12 + length);
        at += 12 + length;
        if (kind == "IEND") break;
    }
    return output;
}

std::optional<std::string> encodePng(const std::vector<std::uint8_t>& rgba, int width, int height) {
    if (width <= 0 || height <= 0 || static_cast<std::size_t>(width) > rgba.size() / 4 / static_cast<std::size_t>(height)) return std::nullopt;
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CFDataRef data = CFDataCreate(nullptr, rgba.data(), static_cast<CFIndex>(rgba.size()));
    CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
    CGImageRef image = CGImageCreate(static_cast<size_t>(width), static_cast<size_t>(height), 8, 32, static_cast<size_t>(width * 4), space, static_cast<CGBitmapInfo>(kCGImageAlphaLast), provider, nullptr, false, kCGRenderingIntentDefault);
    CFMutableDataRef output = CFDataCreateMutable(nullptr, 0);
    CGImageDestinationRef destination = CGImageDestinationCreateWithData(output, CFSTR("public.png"), 1, nullptr);
    std::optional<std::string> result;
    if (destination && image) {
        CGImageDestinationAddImage(destination, image, nullptr);
        if (CGImageDestinationFinalize(destination)) result = stripPng(std::string(reinterpret_cast<const char*>(CFDataGetBytePtr(output)), static_cast<std::size_t>(CFDataGetLength(output))));
    }
    if (destination) CFRelease(destination);
    CFRelease(output);
    if (image) CGImageRelease(image);
    CGDataProviderRelease(provider);
    CFRelease(data);
    CGColorSpaceRelease(space);
    return result;
}

}
