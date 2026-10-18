#pragma once

#include "Interface/Geometry.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ui {

enum class Truncation { tail, middle, head, none };
enum class TextAlign { leading, center, trailing };

struct TextStyle {
    Font font;
    Color color;
    TextAlign alignment = TextAlign::leading;
    int lineLimit = 0;
    Truncation truncation = Truncation::tail;
    bool underline = false;
    double minimumScale = 1;
};

struct TextRun {
    std::string text;
    Font font;
    Color color;
};

Size measureText(const std::string& text, const TextStyle& style, std::optional<double> width);
Size measureRuns(const std::vector<TextRun>& runs, const TextStyle& style, std::optional<double> width);
double lineHeight(const Font& font);
std::string postScriptName(const Font& font);
void* fontHandle(const Font& font);

struct Image {
    std::shared_ptr<void> handle;
    int pixelsWide = 0;
    int pixelsHigh = 0;
    bool valid() const { return handle != nullptr; }
};

Image loadImage(const std::filesystem::path& path);
Image readImage(const std::filesystem::path& path);
Image imageFromRgba(const std::vector<std::uint8_t>& rgba, int width, int height, bool opaque = false);
Image resampled(const Image& image, int width, int height);

struct GradientStop {
    Color color;
    double position = 0;
};

struct Gradient {
    Gradient() = default;
    Gradient(Color from, Color to, bool vertical = true, bool diagonal = false) : stops{{from, 0}, {to, 1}}, vertical(vertical), diagonal(diagonal) {}
    Gradient(std::vector<GradientStop> stops, bool vertical = true) : stops(std::move(stops)), vertical(vertical) {}
    std::vector<GradientStop> stops;
    bool vertical = true;
    bool diagonal = false;
};

class Canvas {
public:
    Canvas(int width, int height, double scale);
    Canvas(void* context, int width, int height, double scale);
    ~Canvas();
    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;

    int pixelWidth() const { return pixelsWide; }
    int pixelHeight() const { return pixelsHigh; }
    double scale() const { return factor; }
    void* context() const { return cg; }
    const std::uint8_t* pixels() const;
    std::shared_ptr<void> snapshot() const;
    bool writePng(const std::filesystem::path& path) const;
    Color pixel(double x, double y) const;

    void clear(Color color);
    void save();
    void restore();
    void clip(const Frame& rect, double radius = 0);
    void translate(double dx, double dy);
    void setAlpha(double alpha);
    void fill(const Frame& rect, Color color, double radius = 0);
    void fillEllipse(const Frame& rect, Color color);
    void fillGradient(const Frame& rect, const Gradient& gradient, double radius = 0);
    void stroke(const Frame& rect, Color color, double width, double radius = 0);
    void strokeEllipse(const Frame& rect, Color color, double width);
    void line(Point a, Point b, Color color, double width);
    void polygons(const std::vector<std::vector<Point>>& shapes, Color color, bool evenOdd);
    void path(const PathStep* steps, int count, const Frame& rect, double unit, Color color);
    void text(const std::string& text, const Frame& rect, const TextStyle& style);
    void runs(const std::vector<TextRun>& runs, const Frame& rect, const TextStyle& style);
    void image(const Image& image, const Frame& rect, std::optional<Color> tint = std::nullopt, double opacity = 1);
    void pixelImage(const Image& image, const Frame& rect);
    void imageMultiplied(const Image& image, const Frame& rect, Color color, double opacity);
    void shadow(double dx, double dy, double blur, Color color);
    void boxShadow(const Frame& rect, double radius, double dx, double dy, double blur, Color color);
    void innerShadow(const Frame& rect, double radius, double dy, double blur, Color color);
    void stripes(const Frame& rect, Color base, Color stripe, double width, double period);
    void reflection(const Image& image, const Frame& source, double height, double opacity);
    void gloss(const Frame& rect, double radius, double top, double bottom);
    void shine(const Frame& rect, double radius, const Frame& ellipse, double top, double bottom);
    void arrowShape(const Frame& rect, double arrow, double radius, const Gradient& gradient, Color border, Color light);
    void erase(const Frame& rect);
    void beginLayer(const Frame& bounds);
    bool intersectsClip(const Frame& bounds) const;
    void endLayer();

private:
    int pixelsWide;
    int pixelsHigh;
    double factor;
    void* cg = nullptr;
    std::vector<std::uint8_t> buffer;
    bool borrowed = false;
};

}
