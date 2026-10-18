#pragma once

#include "Engine/Rendering/Scene.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ld::render {

struct Quality {
    int samples = 4;
    bool shadows = true;
    bool deferredShadows = true;
    double scale = 2;
    bool waterDepth = true;
};

struct View {
    std::string key;
    const Scene* scene = nullptr;
    const Node* camera = nullptr;
    double x = 0;
    double y = 0;
    double width = 0;
    double height = 0;
    Quality quality;
};

struct Image {
    std::vector<std::uint8_t> rgba;
    int width = 0;
    int height = 0;
};

struct FrameStatistics {
    int draws = 0;
    int triangles = 0;
    int uploads = 0;
    double shadowTexel = 0;
};

class Renderer {
public:
    static Renderer& shared();
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool available() const;
    bool lowPower() const;
    std::string error() const;
    void* device() const;
    void present(void* layer, const std::vector<View>& views, double scale);
    void release(void* layer);
    std::uint64_t skipped() const;
    Image snapshot(const Scene& scene, const Node& camera, int width, int height, int samples = 4, bool shadows = true);
    FrameStatistics statistics() const;
    void forget(const std::string& key);

private:
    Renderer();
    struct State;
    std::unique_ptr<State> state;
};

std::shared_ptr<Texture> textTexture(const std::string& text, const std::string& fontName, double pixelHeight, Color color);

}
