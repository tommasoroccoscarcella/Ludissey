#include "City.hpp"
#include "Harbor.hpp"

#include "Engine/Core/Resources.hpp"
#include "Engine/Rendering/Renderer.hpp"

#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

bool save(const ld::render::Image& image, const std::filesystem::path& path, double quality) {
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CFDataRef data = CFDataCreate(nullptr, image.rgba.data(), static_cast<CFIndex>(image.rgba.size()));
    CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
    CGImageRef picture = CGImageCreate(static_cast<size_t>(image.width), static_cast<size_t>(image.height), 8, 32, static_cast<size_t>(image.width * 4), space, static_cast<CGBitmapInfo>(kCGImageAlphaNoneSkipLast), provider, nullptr, false, kCGRenderingIntentDefault);
    CFStringRef location = CFStringCreateWithCString(nullptr, path.c_str(), kCFStringEncodingUTF8);
    CFURLRef url = CFURLCreateWithFileSystemPath(nullptr, location, kCFURLPOSIXPathStyle, false);
    bool jpeg = path.extension() == ".jpg";
    CGImageDestinationRef destination = CGImageDestinationCreateWithURL(url, jpeg ? CFSTR("public.jpeg") : CFSTR("public.png"), 1, nullptr);
    bool written = false;
    if (destination && picture) {
        CFNumberRef level = CFNumberCreate(nullptr, kCFNumberDoubleType, &quality);
        const void* keys[] = {kCGImageDestinationLossyCompressionQuality};
        const void* values[] = {level};
        CFDictionaryRef options = CFDictionaryCreate(nullptr, keys, values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CGImageDestinationAddImage(destination, picture, jpeg ? options : nullptr);
        written = CGImageDestinationFinalize(destination);
        CFRelease(options);
        CFRelease(level);
    }
    if (destination) CFRelease(destination);
    CFRelease(url);
    CFRelease(location);
    if (picture) CGImageRelease(picture);
    CGDataProviderRelease(provider);
    CFRelease(data);
    CGColorSpaceRelease(space);
    return written;
}

bool strip(const std::filesystem::path& path) {
    if (path.extension() != ".jpg") return true;
    std::ifstream input(path, std::ios::binary);
    std::string data{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (data.size() < 4 || static_cast<unsigned char>(data[0]) != 0xFF || static_cast<unsigned char>(data[1]) != 0xD8) return false;
    std::string output = data.substr(0, 2);
    std::size_t at = 2;
    while (at + 4 <= data.size() && static_cast<unsigned char>(data[at]) == 0xFF) {
        unsigned char marker = static_cast<unsigned char>(data[at + 1]);
        if (marker == 0xDA) break;
        std::size_t length = (static_cast<unsigned char>(data[at + 2]) << 8) | static_cast<unsigned char>(data[at + 3]);
        if (marker < 0xE1 || marker > 0xEF) output += data.substr(at, 2 + length);
        at += 2 + length;
    }
    output += data.substr(at);
    std::ofstream(path, std::ios::binary | std::ios::trunc) << output;
    return true;
}

}

int main(int argc, char** argv) {
    std::string which = argc > 2 ? argv[2] : "";
    if (argc < 4 || (which != "citta" && which != "porto")) {
        std::cerr << "Uso: LudisseyBackdrop <radice del repository> <citta|porto> <immagine .jpg|.png> [larghezza altezza]\n";
        return 2;
    }
    std::filesystem::path root = argv[1];
    ld::setResourceFolder(root / "Resources");
    int width = argc > 5 ? std::stoi(argv[4]) : 3840, height = argc > 5 ? std::stoi(argv[5]) : 2160;
    auto started = std::chrono::steady_clock::now();
    auto scene = which == "porto" ? harbor::build() : city::build();
    auto built = std::chrono::steady_clock::now();
    auto image = ld::render::Renderer::shared().snapshot(scene->world->scene, *scene->camera, width, height, 4, true);
    auto rendered = std::chrono::steady_clock::now();
    if (image.width != width || !save(image, argv[3], 0.9) || !strip(argv[3])) {
        std::cerr << "Render non riuscito\n";
        return 1;
    }
    std::cout << (which == "porto" ? "Porto: " : "Città: ") << scene->parts << " parti, " << scene->people.size() << " avatar; costruzione " << std::chrono::duration<double>(built - started).count() << " s, render "
              << std::chrono::duration<double>(rendered - built).count() << " s\n";
    return 0;
}
