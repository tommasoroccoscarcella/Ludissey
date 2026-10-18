#pragma once

#include "Engine/Core/Png.hpp"

#import <AppKit/AppKit.h>

#include <Catch2/catch_amalgamated.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace pixels {

struct Pixels {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

inline Pixels draw(CGImageRef image, int width, int height, int top) {
    Pixels out{width, height, std::vector<std::uint8_t>(static_cast<std::size_t>(width * height * 4), 0)};
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef context = CGBitmapContextCreate(out.rgba.data(), static_cast<size_t>(width), static_cast<size_t>(height), 8, static_cast<size_t>(width * 4), space, static_cast<uint32_t>(kCGImageAlphaPremultipliedLast));
    CGContextDrawImage(context, CGRectMake(0, height - static_cast<CGFloat>(CGImageGetHeight(image)) + top, CGImageGetWidth(image), CGImageGetHeight(image)), image);
    CGContextRelease(context);
    CGColorSpaceRelease(space);
    return out;
}

inline Pixels load(const std::filesystem::path& path) {
    NSImage* image = [[NSImage alloc] initWithContentsOfFile:[NSString stringWithUTF8String:path.c_str()]];
    REQUIRE(image != nil);
    CGImageRef cg = [image CGImageForProposedRect:nil context:nil hints:nil];
    return draw(cg, static_cast<int>(CGImageGetWidth(cg)), static_cast<int>(CGImageGetHeight(cg)), 0);
}

inline void save(const Pixels& p, const std::filesystem::path& path) {
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef context = CGBitmapContextCreate(const_cast<std::uint8_t*>(p.rgba.data()), static_cast<size_t>(p.width), static_cast<size_t>(p.height), 8, static_cast<size_t>(p.width * 4), space, static_cast<uint32_t>(kCGImageAlphaPremultipliedLast));
    CGImageRef image = CGBitmapContextCreateImage(context);
    NSBitmapImageRep* bitmap = [[NSBitmapImageRep alloc] initWithCGImage:image];
    NSData* encoded = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    std::string bytes(static_cast<const char*>(encoded.bytes), encoded.length);
    std::ofstream(path, std::ios::binary | std::ios::trunc) << ld::stripPng(bytes);
    CGImageRelease(image);
    CGContextRelease(context);
    CGColorSpaceRelease(space);
}

struct Difference {
    double mean = 0;
    double changed = 0;
};

inline Difference compare(const Pixels& a, const Pixels& b, Pixels* heat) {
    Difference d;
    if (a.width != b.width || a.height != b.height) return {255, 1};
    std::size_t over = 0;
    double total = 0;
    if (heat) *heat = {a.width, a.height, std::vector<std::uint8_t>(a.rgba.size(), 255)};
    for (std::size_t i = 0; i < a.rgba.size(); i += 4) {
        int delta = (std::abs(a.rgba[i] - b.rgba[i]) + std::abs(a.rgba[i + 1] - b.rgba[i + 1]) + std::abs(a.rgba[i + 2] - b.rgba[i + 2])) / 3;
        total += delta;
        if (delta > 16) ++over;
        if (heat) {
            heat->rgba[i] = static_cast<std::uint8_t>(std::min(255, delta * 4));
            heat->rgba[i + 1] = 0;
            heat->rgba[i + 2] = static_cast<std::uint8_t>(255 - std::min(255, delta * 4));
        }
    }
    double count = static_cast<double>(a.rgba.size() / 4);
    d.mean = total / count;
    d.changed = static_cast<double>(over) / count;
    return d;
}

}
