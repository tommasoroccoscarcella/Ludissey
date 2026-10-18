#define NS_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

#include "Renderer.hpp"

#include "Engine/Rendering/Shaders.hpp"

#include <simd/simd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <map>
#include <mutex>
#include <tuple>

namespace ld::render {

namespace {

template <typename T>
class Ref {
public:
    Ref() = default;
    explicit Ref(T* value) : pointer(value) {}
    Ref(const Ref& o) : pointer(o.pointer) {
        if (pointer) pointer->retain();
    }
    Ref& operator=(const Ref& o) {
        if (this != &o) {
            if (o.pointer) o.pointer->retain();
            if (pointer) pointer->release();
            pointer = o.pointer;
        }
        return *this;
    }
    ~Ref() {
        if (pointer) pointer->release();
    }
    T* get() const { return pointer; }
    T* operator->() const { return pointer; }
    explicit operator bool() const { return pointer != nullptr; }

private:
    T* pointer = nullptr;
};

struct FrameUniforms {
    simd_float4x4 view;
    simd_float4x4 projection;
    simd_float4x4 viewInverse;
    simd_float4x4 light;
    simd_float4 sunDirection;
    simd_float4 sunColor;
    simd_float4 ambient;
    simd_float4 fogColor;
    simd_float4 shadowColor;
    float fogStart;
    float fogEnd;
    float fogExponent;
    float time;
    int shadowMode;
    float shadowTexel;
    float shadowRadius;
    float padding;
    simd_float4 waterColor;
    simd_float4 skyTop;
    simd_float4 skyHorizon;
    float nearPlane;
    float farPlane;
    float waterFog;
    int underwater;
    int waterDepth;
    float pad2;
    float pad3;
    float pad4;
};

struct ObjectUniforms {
    simd_float4x4 model;
    simd_float4x4 normal;
    simd_float4 diffuse;
    simd_float4 multiply;
    simd_float4 emission;
    int lighting;
    int terrainKind;
    int hasColors;
    int hasTexture;
    int doubleSided;
    int receivesShadow;
    int surface;
    int decal;
    int deep;
    simd_float4 water;
};

struct BackgroundUniforms {
    simd_float4 top;
    simd_float4 bottom;
    int gradient;
    int pad[3];
};

simd_float4x4 convert(const Mat4& m) {
    simd_float4x4 r;
    for (int c = 0; c < 4; ++c) r.columns[c] = simd_make_float4(static_cast<float>(m.at(0, c)), static_cast<float>(m.at(1, c)), static_cast<float>(m.at(2, c)), static_cast<float>(m.at(3, c)));
    return r;
}

double linear(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }

simd_float4 linearColor(Color c, double scale = 1) {
    return simd_make_float4(static_cast<float>(linear(c.r) * scale), static_cast<float>(linear(c.g) * scale), static_cast<float>(linear(c.b) * scale), static_cast<float>(c.a));
}

simd_float4 srgbColor(Color c) { return simd_make_float4(static_cast<float>(c.r), static_cast<float>(c.g), static_cast<float>(c.b), static_cast<float>(c.a)); }

Mat4 orthographic(double left, double right, double bottom, double top, double near, double far) {
    Mat4 r;
    r.at(0, 0) = 2 / (right - left);
    r.at(1, 1) = 2 / (top - bottom);
    r.at(2, 2) = -1 / (far - near);
    r.at(0, 3) = -(right + left) / (right - left);
    r.at(1, 3) = -(top + bottom) / (top - bottom);
    r.at(2, 3) = -near / (far - near);
    return r;
}

struct GpuGeometry {
    std::uint64_t version = 0;
    Ref<MTL::Buffer> positions, normals, colors, uvs, indices;
    bool hasColors = false;
    std::size_t vertexCount = 0;
    std::size_t indexCount = 0;
    std::uint64_t used = 0;
};

struct GpuTexture {
    Ref<MTL::Texture> texture;
    std::uint64_t used = 0;
};

struct Targets {
    int width = 0, height = 0, samples = 0;
    Ref<MTL::Texture> color, depth, resolved, sceneDepth;
    std::uint64_t used = 0;
};

struct Item {
    const Geometry* geometry = nullptr;
    const Element* element = nullptr;
    const Material* material = nullptr;
    Mat4 world;
    int order = 0;
    bool transparent = false;
    bool castsShadow = true;
    bool water = false;
    double depth = 0;
};

struct Lights {
    Color ambient{0, 0, 0, 1};
    bool hasSun = false;
    Vec3 direction{0, -1, 0};
    Color sun{0, 0, 0, 1};
    bool shadows = false;
    bool deferred = false;
    Color shadowColor{0, 0, 0, 1};
    double shadowRadius = 3;
    int shadowMapSize = 0;
    bool anyLight = false;
};

}

struct Renderer::State {
    std::mutex lock;
    std::map<void*, int> saturated;
    Ref<MTL::Device> device;
    Ref<MTL::CommandQueue> queue;
    Ref<MTL::Library> library;
    std::map<std::tuple<int, int, int, int>, Ref<MTL::RenderPipelineState>> pipelines;
    Ref<MTL::RenderPipelineState> shadowPipeline;
    Ref<MTL::RenderPipelineState> compositePipeline;
    std::map<std::pair<bool, bool>, Ref<MTL::DepthStencilState>> depthStates;
    Ref<MTL::DepthStencilState> shadowDepth;
    Ref<MTL::Texture> noise;
    Ref<MTL::Texture> white;
    Ref<MTL::Texture> blankDepth;
    bool depthResolve = false;
    std::map<int, Ref<MTL::Texture>> shadowMaps;
    std::map<std::uint64_t, GpuGeometry> geometries;
    std::map<std::uint64_t, GpuTexture> textures;
    std::map<std::string, Targets> targets;
    std::string failure;
    std::uint64_t frame = 0;
    FrameStatistics stats;
    std::uint64_t skipped = 0;
    bool lowPower = false;

    bool ready() const { return device && queue && library && failure.empty(); }

    void build() {
        device = Ref<MTL::Device>(MTL::CreateSystemDefaultDevice());
        if (!device) {
            failure = "Nessun dispositivo Metal disponibile";
            return;
        }
        lowPower = device->lowPower();
        depthResolve = device->supportsFamily(MTL::GPUFamilyMac2);
        queue = Ref<MTL::CommandQueue>(device->newCommandQueue());
        NS::Error* error = nullptr;
        library = Ref<MTL::Library>(device->newLibrary(NS::String::string(shaderSource(), NS::UTF8StringEncoding), nullptr, &error));
        if (!library) {
            failure = error ? error->localizedDescription()->utf8String() : "Compilazione shader non riuscita";
            return;
        }
        makeNoise();
        std::vector<std::uint8_t> one = {255, 255, 255, 255};
        white = texture(one, 1, 1, false);
        auto* blank = MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatDepth32Float, 1, 1, false);
        blank->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
        blank->setStorageMode(MTL::StorageModePrivate);
        blankDepth = Ref<MTL::Texture>(device->newTexture(blank));
        auto* shadow = MTL::DepthStencilDescriptor::alloc()->init();
        shadow->setDepthCompareFunction(MTL::CompareFunctionLessEqual);
        shadow->setDepthWriteEnabled(true);
        shadowDepth = Ref<MTL::DepthStencilState>(device->newDepthStencilState(shadow));
        shadow->release();
    }

    Ref<MTL::Texture> texture(const std::vector<std::uint8_t>& rgba, int width, int height, bool mipmaps) {
        auto* d = MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm_sRGB, static_cast<NS::UInteger>(width), static_cast<NS::UInteger>(height), mipmaps);
        d->setUsage(MTL::TextureUsageShaderRead);
        Ref<MTL::Texture> t(device->newTexture(d));
        t->replaceRegion(MTL::Region(0, 0, static_cast<NS::UInteger>(width), static_cast<NS::UInteger>(height)), 0, rgba.data(), static_cast<NS::UInteger>(width * 4));
        if (mipmaps && t->mipmapLevelCount() > 1) {
            auto* buffer = queue->commandBuffer();
            auto* blit = buffer->blitCommandEncoder();
            blit->generateMipmaps(t.get());
            blit->endEncoding();
            buffer->commit();
        }
        return t;
    }

    void makeNoise() {
        const int cells = 32, texels = 8, side = cells * texels;
        std::uint64_t state = 0x9E3779B97F4A7C15ull;
        std::vector<double> lattice(cells * cells);
        for (auto& v : lattice) {
            state = state * 6364136223846793005ull + 1442695040888963407ull;
            v = static_cast<double>(state >> 11) / static_cast<double>(1ull << 53);
        }
        auto at = [&](int x, int y) { return lattice[static_cast<std::size_t>((y % cells) * cells + x % cells)]; };
        auto ease = [](double t) { return t * t * (3 - 2 * t); };
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(side * side));
        for (int y = 0; y < side; ++y)
            for (int x = 0; x < side; ++x) {
                int cx = x / texels, cy = y / texels;
                double fx = ease(static_cast<double>(x % texels) / texels), fy = ease(static_cast<double>(y % texels) / texels);
                double low = at(cx, cy) + (at(cx + 1, cy) - at(cx, cy)) * fx;
                double high = at(cx, cy + 1) + (at(cx + 1, cy + 1) - at(cx, cy + 1)) * fx;
                bytes[static_cast<std::size_t>(y * side + x)] = static_cast<std::uint8_t>(std::clamp(std::round((low + (high - low) * fy) * 255), 0.0, 255.0));
            }
        auto* d = MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatR8Unorm, side, side, true);
        d->setUsage(MTL::TextureUsageShaderRead);
        noise = Ref<MTL::Texture>(device->newTexture(d));
        noise->replaceRegion(MTL::Region(0, 0, side, side), 0, bytes.data(), side);
        auto* buffer = queue->commandBuffer();
        auto* blit = buffer->blitCommandEncoder();
        blit->generateMipmaps(noise.get());
        blit->endEncoding();
        buffer->commit();
        buffer->waitUntilCompleted();
    }

    Ref<MTL::RenderPipelineState> pipeline(bool blend, bool colorWrite, int samples, int background) {
        auto key = std::make_tuple(blend ? 1 : 0, colorWrite ? 1 : 0, samples, background);
        if (auto it = pipelines.find(key); it != pipelines.end()) return it->second;
        auto* d = MTL::RenderPipelineDescriptor::alloc()->init();
        auto* vf = library->newFunction(NS::String::string(background ? "backgroundVertex" : "sceneVertex", NS::UTF8StringEncoding));
        auto* ff = library->newFunction(NS::String::string(background ? "backgroundFragment" : "sceneFragment", NS::UTF8StringEncoding));
        d->setVertexFunction(vf);
        d->setFragmentFunction(ff);
        d->setSampleCount(static_cast<NS::UInteger>(samples));
        auto* c = d->colorAttachments()->object(0);
        c->setPixelFormat(MTL::PixelFormatBGRA8Unorm_sRGB);
        c->setWriteMask(colorWrite ? MTL::ColorWriteMaskAll : MTL::ColorWriteMaskNone);
        if (blend) {
            c->setBlendingEnabled(true);
            c->setRgbBlendOperation(MTL::BlendOperationAdd);
            c->setAlphaBlendOperation(MTL::BlendOperationAdd);
            c->setSourceRGBBlendFactor(MTL::BlendFactorSourceAlpha);
            c->setDestinationRGBBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
            c->setSourceAlphaBlendFactor(MTL::BlendFactorOne);
            c->setDestinationAlphaBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
        }
        d->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);
        NS::Error* error = nullptr;
        Ref<MTL::RenderPipelineState> state(device->newRenderPipelineState(d, &error));
        if (!state && error) failure = error->localizedDescription()->utf8String();
        vf->release();
        ff->release();
        d->release();
        pipelines[key] = state;
        return state;
    }

    Ref<MTL::RenderPipelineState> shadowState() {
        if (shadowPipeline) return shadowPipeline;
        auto* d = MTL::RenderPipelineDescriptor::alloc()->init();
        auto* vf = library->newFunction(NS::String::string("shadowVertex", NS::UTF8StringEncoding));
        d->setVertexFunction(vf);
        d->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);
        NS::Error* error = nullptr;
        shadowPipeline = Ref<MTL::RenderPipelineState>(device->newRenderPipelineState(d, &error));
        vf->release();
        d->release();
        return shadowPipeline;
    }

    Ref<MTL::RenderPipelineState> composite() {
        if (compositePipeline) return compositePipeline;
        static const char* source = R"METAL(
#include <metal_stdlib>
using namespace metal;
struct Out { float4 position [[position]]; float2 uv; };
vertex Out compositeVertex(uint vid [[vertex_id]]) {
    float2 corners[4] = {float2(-1.0, -1.0), float2(1.0, -1.0), float2(-1.0, 1.0), float2(1.0, 1.0)};
    Out o;
    o.position = float4(corners[vid], 0.0, 1.0);
    o.uv = float2(corners[vid].x * 0.5 + 0.5, 0.5 - corners[vid].y * 0.5);
    return o;
}
fragment float4 compositeFragment(Out in [[stage_in]], texture2d<float> image [[texture(0)]]) {
    constexpr sampler smooth(filter::linear, address::clamp_to_edge);
    return float4(image.sample(smooth, in.uv).rgb, 1.0);
}
)METAL";
        NS::Error* error = nullptr;
        auto* lib = device->newLibrary(NS::String::string(source, NS::UTF8StringEncoding), nullptr, &error);
        if (!lib) return compositePipeline;
        auto* d = MTL::RenderPipelineDescriptor::alloc()->init();
        auto* vf = lib->newFunction(NS::String::string("compositeVertex", NS::UTF8StringEncoding));
        auto* ff = lib->newFunction(NS::String::string("compositeFragment", NS::UTF8StringEncoding));
        d->setVertexFunction(vf);
        d->setFragmentFunction(ff);
        d->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatBGRA8Unorm_sRGB);
        compositePipeline = Ref<MTL::RenderPipelineState>(device->newRenderPipelineState(d, &error));
        vf->release();
        ff->release();
        d->release();
        lib->release();
        return compositePipeline;
    }

    Ref<MTL::DepthStencilState> depth(bool reads, bool writes) {
        auto key = std::make_pair(reads, writes);
        if (auto it = depthStates.find(key); it != depthStates.end()) return it->second;
        auto* d = MTL::DepthStencilDescriptor::alloc()->init();
        d->setDepthCompareFunction(reads ? MTL::CompareFunctionLessEqual : MTL::CompareFunctionAlways);
        d->setDepthWriteEnabled(writes);
        Ref<MTL::DepthStencilState> state(device->newDepthStencilState(d));
        d->release();
        depthStates[key] = state;
        return state;
    }

    Ref<MTL::Texture> shadowMap(int size) {
        if (auto it = shadowMaps.find(size); it != shadowMaps.end()) return it->second;
        auto* d = MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatDepth32Float, static_cast<NS::UInteger>(size), static_cast<NS::UInteger>(size), false);
        d->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
        d->setStorageMode(MTL::StorageModePrivate);
        Ref<MTL::Texture> t(device->newTexture(d));
        shadowMaps[size] = t;
        return t;
    }

    Targets& target(const std::string& key, int width, int height, int samples) {
        auto& t = targets[key];
        t.used = frame;
        if (t.width == width && t.height == height && t.samples == samples && t.color) return t;
        t.width = width;
        t.height = height;
        t.samples = samples;
        auto make = [&](MTL::PixelFormat format, int count, MTL::TextureUsage usage) {
            auto* d = MTL::TextureDescriptor::texture2DDescriptor(format, static_cast<NS::UInteger>(width), static_cast<NS::UInteger>(height), false);
            if (count > 1) {
                d->setTextureType(MTL::TextureType2DMultisample);
                d->setSampleCount(static_cast<NS::UInteger>(count));
            }
            d->setUsage(usage);
            d->setStorageMode(MTL::StorageModePrivate);
            return Ref<MTL::Texture>(device->newTexture(d));
        };
        t.color = make(MTL::PixelFormatBGRA8Unorm_sRGB, samples, samples > 1 ? MTL::TextureUsageRenderTarget : MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
        t.depth = make(MTL::PixelFormatDepth32Float, samples, MTL::TextureUsageRenderTarget);
        t.resolved = samples > 1 ? make(MTL::PixelFormatBGRA8Unorm_sRGB, 1, MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead) : Ref<MTL::Texture>();
        t.sceneDepth = make(MTL::PixelFormatDepth32Float, 1, MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
        return t;
    }

    MTL::Texture* output(const Targets& t) const { return t.samples > 1 ? t.resolved.get() : t.color.get(); }

    GpuGeometry* upload(const Geometry& g) {
        auto& gpu = geometries[g.id];
        gpu.used = frame;
        if (gpu.version == g.version && gpu.positions) return &gpu;
        if (g.positions.empty()) return nullptr;
        auto buffer = [&](const void* data, std::size_t bytes) { return Ref<MTL::Buffer>(device->newBuffer(data, static_cast<NS::UInteger>(std::max<std::size_t>(bytes, 16)), MTL::ResourceStorageModeShared)); };
        std::size_t n = g.vertexCount();
        gpu.positions = buffer(g.positions.data(), g.positions.size() * sizeof(float));
        if (g.normals.size() == g.positions.size()) gpu.normals = buffer(g.normals.data(), g.normals.size() * sizeof(float));
        else {
            std::vector<float> up(n * 3, 0);
            for (std::size_t i = 0; i < n; ++i) up[i * 3 + 1] = 1;
            gpu.normals = buffer(up.data(), up.size() * sizeof(float));
        }
        gpu.hasColors = g.colors.size() == g.positions.size();
        gpu.colors = gpu.hasColors ? buffer(g.colors.data(), g.colors.size() * sizeof(float)) : gpu.positions;
        if (g.uvs.size() == n * 2) gpu.uvs = buffer(g.uvs.data(), g.uvs.size() * sizeof(float));
        else {
            std::vector<float> zero(n * 2, 0);
            gpu.uvs = buffer(zero.data(), zero.size() * sizeof(float));
        }
        if (g.indices.empty()) {
            std::vector<std::uint32_t> sequence(n);
            for (std::size_t i = 0; i < n; ++i) sequence[i] = static_cast<std::uint32_t>(i);
            gpu.indices = buffer(sequence.data(), sequence.size() * sizeof(std::uint32_t));
            gpu.indexCount = n;
        } else {
            gpu.indices = buffer(g.indices.data(), g.indices.size() * sizeof(std::uint32_t));
            gpu.indexCount = g.indices.size();
        }
        gpu.vertexCount = n;
        gpu.version = g.version;
        ++stats.uploads;
        return &gpu;
    }

    MTL::Texture* image(const Texture& t) {
        auto& gpu = textures[t.id];
        gpu.used = frame;
        if (!gpu.texture) gpu.texture = texture(t.rgba, t.width, t.height, true);
        return gpu.texture.get();
    }

    void evict() {
        for (auto it = geometries.begin(); it != geometries.end();) it = frame - it->second.used > 240 ? geometries.erase(it) : std::next(it);
        for (auto it = textures.begin(); it != textures.end();) it = frame - it->second.used > 240 ? textures.erase(it) : std::next(it);
        for (auto it = targets.begin(); it != targets.end();) it = frame - it->second.used > 240 ? targets.erase(it) : std::next(it);
    }

    static Lights lights(const Scene& scene) {
        Lights l;
        std::function<void(const Node&, const Mat4&)> visit = [&](const Node& n, const Mat4& parent) {
            if (n.hidden) return;
            Mat4 w = parent * n.local();
            if (n.light) {
                const Light& light = *n.light;
                double k = light.intensity / 1000;
                if (light.type == LightType::ambient) {
                    l.ambient.r += linear(light.color.r) * k;
                    l.ambient.g += linear(light.color.g) * k;
                    l.ambient.b += linear(light.color.b) * k;
                } else if (!l.hasSun) {
                    l.hasSun = true;
                    l.anyLight = true;
                    l.direction = Vec3{-w.at(0, 2), -w.at(1, 2), -w.at(2, 2)}.normalized();
                    l.sun = {linear(light.color.r) * k, linear(light.color.g) * k, linear(light.color.b) * k, 1};
                    l.shadows = light.castsShadow;
                    l.deferred = light.deferredShadows;
                    l.shadowColor = light.shadowColor;
                    l.shadowRadius = light.shadowRadius;
                    l.shadowMapSize = light.shadowMapSize;
                }
            }
            for (const auto& c : n.children()) visit(*c, w);
        };
        visit(*scene.root, Mat4{});
        if (!l.anyLight) {
            l.hasSun = true;
            l.direction = {0, 0, -1};
            l.sun = {1, 1, 1, 1};
        }
        return l;
    }

    static std::vector<Item> items(const Scene& scene, const Node& camera, const Mat4& view) {
        std::vector<Item> out;
        Mat4 cameraWorld = camera.world();
        Mat3 cameraRotation;
        for (int i = 0; i < 3; ++i) {
            Vec3 axis = Vec3{cameraWorld.at(0, i), cameraWorld.at(1, i), cameraWorld.at(2, i)}.normalized();
            cameraRotation.m[0][i] = axis.x;
            cameraRotation.m[1][i] = axis.y;
            cameraRotation.m[2][i] = axis.z;
        }
        std::function<void(const Node&, const Mat4&, int, bool)> visit = [&](const Node& n, const Mat4& parent, int inherited, bool casts) {
            if (n.hidden) return;
            Mat4 w = parent * n.local();
            if (n.billboard) {
                Vec3 p{w.at(0, 3), w.at(1, 3), w.at(2, 3)};
                double sx = Vec3{w.at(0, 0), w.at(1, 0), w.at(2, 0)}.length(), sy = Vec3{w.at(0, 1), w.at(1, 1), w.at(2, 1)}.length(), sz = Vec3{w.at(0, 2), w.at(1, 2), w.at(2, 2)}.length();
                w = Mat4::translation(p) * Mat4::rotation(cameraRotation) * Mat4::scale({sx, sy, sz});
            }
            int order = n.renderingOrder != 0 ? n.renderingOrder : inherited;
            bool shadow = casts && n.castsShadow;
            if (n.geometry && n.geometry->vertexCount() > 0) {
                const Geometry& g = *n.geometry;
                Vec3 centre = w.transformPoint(g.bounds().center());
                double depth = -(view.transformPoint(centre)).z;
                for (const auto& e : g.elements) {
                    const Material& m = n.material ? *n.material : g.materials[static_cast<std::size_t>(e.material) % g.materials.size()];
                    bool water = m.lighting == Lighting::water;
                    bool transparent = water || m.opacity < 1 || m.diffuse.a < 1 || !m.writesDepth || (m.texture != nullptr && !m.decal);
                    out.push_back({&g, &e, &m, w, order, transparent, shadow && m.colorWrite && !water, water, depth});
                }
            }
            for (const auto& c : n.children()) visit(*c, w, n.renderingOrder != 0 ? n.renderingOrder : inherited, shadow);
        };
        visit(*scene.root, Mat4{}, 0, true);
        std::stable_sort(out.begin(), out.end(), [](const Item& a, const Item& b) {
            if (a.transparent != b.transparent) return !a.transparent;
            if (a.water != b.water) return a.water;
            if (a.order != b.order) return a.order < b.order;
            if (a.transparent) return a.depth > b.depth;
            return false;
        });
        return out;
    }

    Mat4 lightMatrix(const Lights& l, const Node& camera, double aspect, Bounds sceneBounds, int size) {
        Camera settings = camera.camera.value_or(Camera{});
        double reach = std::min(settings.zFar, 64.0);
        Mat4 limited = projection({settings.fieldOfView, settings.zNear, std::max(settings.zNear + 0.01, reach)}, aspect);
        Mat4 inverse = (limited * viewMatrix(camera)).inverted();
        std::vector<Vec3> frustum;
        for (double z : {0.0, 1.0})
            for (double y : {-1.0, 1.0})
                for (double x : {-1.0, 1.0}) frustum.push_back(inverse.transformPoint({x, y, z}));
        std::vector<Vec3> box;
        for (double x : {sceneBounds.low.x, sceneBounds.high.x})
            for (double y : {sceneBounds.low.y, sceneBounds.high.y})
                for (double z : {sceneBounds.low.z, sceneBounds.high.z}) box.push_back({x, y, z});
        Vec3 up = std::abs(l.direction.y) > 0.99 ? Vec3{0, 0, 1} : Vec3{0, 1, 0};
        Vec3 centre = sceneBounds.center();
        double span = sceneBounds.extent().length() + 1;
        Mat4 view = Mat4::lookAt(centre - l.direction * span, centre, up);
        auto range = [&](const std::vector<Vec3>& points) {
            Bounds r{view.transformPoint(points[0]), view.transformPoint(points[0])};
            for (auto p : points) {
                Vec3 q = view.transformPoint(p);
                r = r.merged({q, q});
            }
            return r;
        };
        Bounds b = range(box);
        Vec3 middle{0, 0, 0};
        for (auto p : frustum) middle = middle + p;
        middle = middle * (1.0 / static_cast<double>(frustum.size()));
        double radius = 0;
        for (auto p : frustum) radius = std::max(radius, (p - middle).length());
        radius = std::ceil(radius * 4) / 4;
        double sceneHalf = std::max(b.high.x - b.low.x, b.high.y - b.low.y) / 2;
        Vec3 focus = view.transformPoint(middle);
        double half = radius;
        if (sceneHalf <= radius) {
            half = sceneHalf;
            focus = {(b.low.x + b.high.x) / 2, (b.low.y + b.high.y) / 2, 0};
        }
        half = half * 1.02 + 0.05;
        double texel = half * 2 / size;
        double cx = std::round(focus.x / texel) * texel, cy = std::round(focus.y / texel) * texel;
        double left = cx - half, right = cx + half, bottom = cy - half, top = cy + half;
        double near = std::max(0.01, -b.high.z - 1), far = -b.low.z + 1;
        stats.shadowTexel = texel;
        return orthographic(left, right, bottom, top, near, far) * view;
    }

    static Bounds bounds(const Scene& scene) {
        Bounds total{{0, 0, 0}, {0, 0, 0}};
        bool first = true;
        std::function<void(const Node&, const Mat4&)> visit = [&](const Node& n, const Mat4& parent) {
            if (n.hidden) return;
            Mat4 w = parent * n.local();
            if (n.geometry && n.geometry->vertexCount() > 0) {
                Bounds b = n.geometry->bounds();
                for (double x : {b.low.x, b.high.x})
                    for (double y : {b.low.y, b.high.y})
                        for (double z : {b.low.z, b.high.z}) {
                            Vec3 p = w.transformPoint({x, y, z});
                            total = first ? Bounds{p, p} : total.merged({p, p});
                            first = false;
                        }
            }
            for (const auto& c : n.children()) visit(*c, w);
        };
        visit(*scene.root, Mat4{});
        return total;
    }

    void encode(MTL::CommandBuffer* buffer, const Scene& scene, const Node& camera, Targets& t, const Quality& quality, double aspect) {
        Camera settings = camera.camera.value_or(Camera{});
        Mat4 view = viewMatrix(camera);
        Mat4 proj = projection(settings, aspect);
        Lights l = lights(scene);
        auto list = items(scene, camera, view);
        FrameUniforms f{};
        f.view = convert(view);
        f.projection = convert(proj);
        f.viewInverse = convert(view.inverted());
        f.sunDirection = simd_make_float4(static_cast<float>(l.direction.x), static_cast<float>(l.direction.y), static_cast<float>(l.direction.z), 0);
        f.sunColor = simd_make_float4(static_cast<float>(l.sun.r), static_cast<float>(l.sun.g), static_cast<float>(l.sun.b), 1);
        f.ambient = simd_make_float4(static_cast<float>(l.ambient.r), static_cast<float>(l.ambient.g), static_cast<float>(l.ambient.b), 1);
        f.fogColor = linearColor(scene.fog.color);
        f.fogStart = static_cast<float>(scene.fog.start);
        f.fogEnd = static_cast<float>(scene.fog.end);
        f.fogExponent = static_cast<float>(scene.fog.exponent);
        f.time = static_cast<float>(std::fmod(scene.time, 3600.0));
        f.shadowColor = linearColor(l.shadowColor);
        bool shadows = quality.shadows && l.shadows && l.hasSun;
        f.shadowMode = shadows ? (l.deferred && quality.deferredShadows ? 2 : 1) : 0;
        int size = l.shadowMapSize > 0 ? l.shadowMapSize : (f.shadowMode == 2 ? 2048 : 1024);
        f.shadowRadius = static_cast<float>(std::max(0.5, l.shadowRadius * 0.5));
        f.shadowTexel = 1.0f / static_cast<float>(size);
        Ref<MTL::Texture> map = shadowMap(size);
        if (shadows) {
            Mat4 light = lightMatrix(l, camera, aspect, bounds(scene), size);
            f.light = convert(light);
            auto* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
            pass->depthAttachment()->setTexture(map.get());
            pass->depthAttachment()->setLoadAction(MTL::LoadActionClear);
            pass->depthAttachment()->setClearDepth(1.0);
            pass->depthAttachment()->setStoreAction(MTL::StoreActionStore);
            auto* encoder = buffer->renderCommandEncoder(pass);
            encoder->setRenderPipelineState(shadowState().get());
            encoder->setDepthStencilState(shadowDepth.get());
            encoder->setCullMode(MTL::CullModeNone);
            encoder->setDepthBias(0.0005f, 1.5f, 0.01f);
            encoder->setVertexBytes(&f, sizeof(f), 4);
            for (const auto& item : list) {
                if (!item.castsShadow || item.transparent || item.material->lighting == Lighting::water) continue;
                GpuGeometry* gpu = upload(*item.geometry);
                if (!gpu) continue;
                ObjectUniforms o{};
                o.model = convert(item.world);
                encoder->setVertexBuffer(gpu->positions.get(), 0, 0);
                encoder->setVertexBytes(&o, sizeof(o), 5);
                std::uint32_t count = std::min<std::uint32_t>(item.element->count, static_cast<std::uint32_t>(gpu->indexCount - std::min<std::size_t>(gpu->indexCount, item.element->start)));
                if (count == 0) continue;
                encoder->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle, count, MTL::IndexTypeUInt32, gpu->indices.get(), item.element->start * sizeof(std::uint32_t));
            }
            encoder->endEncoding();
        }
        Vec3 eyePosition = camera.worldPosition();
        std::optional<Underwater> submerged = scene.underwater ? scene.underwater(eyePosition) : std::nullopt;
        Color skyTop = scene.background.kind == BackgroundKind::gradient ? scene.background.top : scene.background.color;
        Color skyHorizon = scene.background.kind == BackgroundKind::gradient ? scene.background.bottom : scene.background.color;
        f.skyTop = linearColor(skyTop);
        f.skyHorizon = linearColor(skyHorizon);
        f.nearPlane = static_cast<float>(settings.zNear);
        f.farPlane = static_cast<float>(settings.zFar);
        bool readable = quality.waterDepth && (t.samples == 1 || depthResolve);
        f.waterDepth = readable ? 1 : 0;
        Color deep{1, 1, 1, 1};
        if (submerged) {
            deep = {submerged->color.r * 0.5, submerged->color.g * 0.5, submerged->color.b * 0.5, 1};
            f.underwater = 1;
            f.waterColor = linearColor(deep);
            f.waterFog = static_cast<float>((1.5 + (40.0 - 1.5) * std::clamp(submerged->transparency, 0.0, 1.0)) * 0.6);
        }
        auto first = MTL::RenderPassDescriptor::renderPassDescriptor();
        auto* color = first->colorAttachments()->object(0);
        color->setTexture(t.color.get());
        color->setLoadAction(MTL::LoadActionClear);
        color->setClearColor(MTL::ClearColor(0, 0, 0, 1));
        color->setStoreAction(MTL::StoreActionStore);
        first->depthAttachment()->setTexture(t.depth.get());
        first->depthAttachment()->setLoadAction(MTL::LoadActionClear);
        first->depthAttachment()->setClearDepth(1.0);
        if (t.samples > 1 && readable) {
            first->depthAttachment()->setResolveTexture(t.sceneDepth.get());
            first->depthAttachment()->setDepthResolveFilter(MTL::MultisampleDepthResolveFilterMin);
            first->depthAttachment()->setStoreAction(MTL::StoreActionStoreAndMultisampleResolve);
        } else first->depthAttachment()->setStoreAction(MTL::StoreActionStore);
        auto* encoder = buffer->renderCommandEncoder(first);
        BackgroundUniforms bg{};
        bg.gradient = scene.background.kind == BackgroundKind::gradient && !submerged ? 1 : 0;
        bg.top = srgbColor(submerged ? deep : bg.gradient ? scene.background.top : scene.background.color);
        bg.bottom = srgbColor(submerged ? deep : scene.background.bottom);
        encoder->setRenderPipelineState(pipeline(false, true, t.samples, 1).get());
        encoder->setDepthStencilState(depth(false, false).get());
        encoder->setFragmentBytes(&bg, sizeof(bg), 0);
        encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
        auto prepare = [&](MTL::RenderCommandEncoder* e, MTL::Texture* sceneDepth) {
            e->setFrontFacingWinding(MTL::WindingCounterClockwise);
            e->setVertexBytes(&f, sizeof(f), 4);
            e->setFragmentBytes(&f, sizeof(f), 4);
            e->setFragmentTexture(noise.get(), 0);
            e->setFragmentTexture(map.get(), 1);
            e->setFragmentTexture(sceneDepth, 3);
        };
        auto draw = [&](MTL::RenderCommandEncoder* e, const Item& item) {
            GpuGeometry* gpu = upload(*item.geometry);
            if (!gpu) return;
            const Material& m = *item.material;
            auto state = pipeline(item.transparent, m.colorWrite, t.samples, 0);
            if (!state) return;
            e->setRenderPipelineState(state.get());
            e->setDepthStencilState(depth(m.readsDepth, m.writesDepth).get());
            if (item.water) e->setCullMode(submerged ? MTL::CullModeFront : MTL::CullModeBack);
            else e->setCullMode(m.doubleSided || m.wireframe ? MTL::CullModeNone : MTL::CullModeBack);
            e->setTriangleFillMode(m.wireframe ? MTL::TriangleFillModeLines : MTL::TriangleFillModeFill);
            ObjectUniforms o{};
            o.model = convert(item.world);
            Mat4 normal = item.world.inverted();
            Mat4 transposed;
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 4; ++c) transposed.at(r, c) = normal.at(c, r);
            o.normal = convert(transposed);
            o.diffuse = linearColor(m.diffuse);
            o.diffuse.w = static_cast<float>(m.diffuse.a * m.opacity);
            o.multiply = linearColor(m.multiply);
            o.emission = linearColor(m.emission, m.emission.a);
            o.lighting = static_cast<int>(m.lighting);
            o.terrainKind = m.terrainKind;
            o.hasColors = gpu->hasColors ? 1 : 0;
            o.doubleSided = m.doubleSided ? 1 : 0;
            o.receivesShadow = m.lighting == Lighting::constant && f.shadowMode != 2 ? 0 : 1;
            o.surface = m.surface;
            o.decal = m.decal ? 1 : 0;
            o.deep = m.deep ? 1 : 0;
            o.water = simd_make_float4(static_cast<float>(m.transparency), static_cast<float>(m.reflectance), static_cast<float>(m.waveSize), static_cast<float>(m.waveSpeed));
            MTL::Texture* picture = white.get();
            if (m.texture && !m.texture->rgba.empty()) {
                picture = image(*m.texture);
                o.hasTexture = 1;
            }
            e->setFragmentTexture(picture, 2);
            e->setVertexBuffer(gpu->positions.get(), 0, 0);
            e->setVertexBuffer(gpu->normals.get(), 0, 1);
            e->setVertexBuffer(gpu->colors.get(), 0, 2);
            e->setVertexBuffer(gpu->uvs.get(), 0, 3);
            e->setVertexBytes(&o, sizeof(o), 5);
            e->setFragmentBytes(&o, sizeof(o), 5);
            std::uint32_t count = std::min<std::uint32_t>(item.element->count, static_cast<std::uint32_t>(gpu->indexCount - std::min<std::size_t>(gpu->indexCount, item.element->start)));
            if (count == 0) return;
            e->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle, count, MTL::IndexTypeUInt32, gpu->indices.get(), item.element->start * sizeof(std::uint32_t));
            ++stats.draws;
            stats.triangles += static_cast<int>(count / 3);
        };
        prepare(encoder, blankDepth.get());
        auto split = std::find_if(list.begin(), list.end(), [](const Item& item) { return item.transparent; });
        for (auto it = list.begin(); it != split; ++it) draw(encoder, *it);
        encoder->endEncoding();
        if (t.samples == 1 && readable) {
            auto* blit = buffer->blitCommandEncoder();
            blit->copyFromTexture(t.depth.get(), 0, 0, MTL::Origin(0, 0, 0), MTL::Size(static_cast<NS::UInteger>(t.width), static_cast<NS::UInteger>(t.height), 1), t.sceneDepth.get(), 0, 0, MTL::Origin(0, 0, 0));
            blit->endEncoding();
        }
        auto second = MTL::RenderPassDescriptor::renderPassDescriptor();
        auto* overlay = second->colorAttachments()->object(0);
        overlay->setTexture(t.color.get());
        overlay->setLoadAction(MTL::LoadActionLoad);
        if (t.samples > 1) {
            overlay->setResolveTexture(t.resolved.get());
            overlay->setStoreAction(MTL::StoreActionMultisampleResolve);
        } else overlay->setStoreAction(MTL::StoreActionStore);
        second->depthAttachment()->setTexture(t.depth.get());
        second->depthAttachment()->setLoadAction(MTL::LoadActionLoad);
        second->depthAttachment()->setStoreAction(MTL::StoreActionDontCare);
        encoder = buffer->renderCommandEncoder(second);
        prepare(encoder, readable ? t.sceneDepth.get() : blankDepth.get());
        for (auto it = split; it != list.end(); ++it) draw(encoder, *it);
        encoder->endEncoding();
    }
};

Renderer& Renderer::shared() {
    static Renderer* instance = new Renderer();
    return *instance;
}

Renderer::Renderer() : state(std::make_unique<State>()) {
    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
    state->build();
    pool->release();
}

Renderer::~Renderer() = default;

bool Renderer::available() const { return state->ready(); }

bool Renderer::lowPower() const { return state->lowPower; }

std::string Renderer::error() const { return state->failure; }

void* Renderer::device() const { return state->device.get(); }

FrameStatistics Renderer::statistics() const { return state->stats; }

std::uint64_t Renderer::skipped() const {
    std::lock_guard<std::mutex> guard(state->lock);
    return state->skipped;
}

void Renderer::release(void* layer) {
    std::lock_guard<std::mutex> guard(state->lock);
    state->saturated.erase(layer);
}

void Renderer::forget(const std::string& key) {
    std::lock_guard<std::mutex> guard(state->lock);
    state->targets.erase(key);
}

void Renderer::present(void* layer, const std::vector<View>& views, double scale) {
    std::lock_guard<std::mutex> guard(state->lock);
    if (!state->ready() || !layer) return;
    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
    auto* metal = static_cast<CA::MetalLayer*>(layer);
    int& saturated = state->saturated[layer];
    if (saturated >= 2) {
        saturated = 0;
        ++state->skipped;
        pool->release();
        return;
    }
    ++state->frame;
    state->stats = {};
    auto before = std::chrono::steady_clock::now();
    CA::MetalDrawable* drawable = metal->nextDrawable();
    double waited = std::chrono::duration<double>(std::chrono::steady_clock::now() - before).count();
    saturated = waited > 0.004 ? saturated + 1 : 0;
    if (!drawable) {
        pool->release();
        return;
    }
    auto* buffer = state->queue->commandBuffer();
    std::vector<std::pair<const View*, Targets*>> rendered;
    for (const auto& v : views) {
        if (!v.scene || !v.camera || v.width < 1 || v.height < 1) continue;
        double s = std::min(scale, v.quality.scale);
        int w = std::max(1, static_cast<int>(std::lround(v.width * s))), h = std::max(1, static_cast<int>(std::lround(v.height * s)));
        Targets& t = state->target(v.key, w, h, std::max(1, v.quality.samples));
        state->encode(buffer, *v.scene, *v.camera, t, v.quality, v.width / v.height);
        rendered.push_back({&v, &t});
    }
    auto* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
    auto* color = pass->colorAttachments()->object(0);
    color->setTexture(drawable->texture());
    color->setLoadAction(MTL::LoadActionClear);
    color->setClearColor(MTL::ClearColor(0, 0, 0, 0));
    color->setStoreAction(MTL::StoreActionStore);
    auto* encoder = buffer->renderCommandEncoder(pass);
    if (auto composite = state->composite()) {
        encoder->setRenderPipelineState(composite.get());
        double drawableHeight = static_cast<double>(drawable->texture()->height());
        double drawableWidth = static_cast<double>(drawable->texture()->width());
        for (auto [view, targets] : rendered) {
            double x = std::clamp(view->x * scale, 0.0, drawableWidth), y = std::clamp(view->y * scale, 0.0, drawableHeight);
            double w = std::min(view->width * scale, drawableWidth - x), h = std::min(view->height * scale, drawableHeight - y);
            if (w <= 0 || h <= 0) continue;
            encoder->setViewport(MTL::Viewport{view->x * scale, view->y * scale, view->width * scale, view->height * scale, 0, 1});
            encoder->setScissorRect(MTL::ScissorRect{static_cast<NS::UInteger>(x), static_cast<NS::UInteger>(y), static_cast<NS::UInteger>(w), static_cast<NS::UInteger>(h)});
            encoder->setFragmentTexture(state->output(*targets), 0);
            encoder->drawPrimitives(MTL::PrimitiveTypeTriangleStrip, NS::UInteger(0), NS::UInteger(4));
        }
    }
    encoder->endEncoding();
    buffer->presentDrawable(drawable);
    buffer->commit();
    state->evict();
    pool->release();
}

Image Renderer::snapshot(const Scene& scene, const Node& camera, int width, int height, int samples, bool shadows) {
    std::lock_guard<std::mutex> guard(state->lock);
    Image out;
    if (!state->ready() || width < 1 || height < 1) return out;
    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
    ++state->frame;
    Targets& t = state->target("snapshot." + std::to_string(width) + "x" + std::to_string(height) + "." + std::to_string(samples), width, height, std::max(1, samples));
    auto* buffer = state->queue->commandBuffer();
    Quality quality;
    quality.samples = samples;
    quality.shadows = shadows;
    state->encode(buffer, scene, camera, t, quality, static_cast<double>(width) / height);
    std::size_t row = static_cast<std::size_t>(width) * 4;
    Ref<MTL::Buffer> readback(state->device->newBuffer(static_cast<NS::UInteger>(row * static_cast<std::size_t>(height)), MTL::ResourceStorageModeShared));
    auto* blit = buffer->blitCommandEncoder();
    blit->copyFromTexture(state->output(t), 0, 0, MTL::Origin(0, 0, 0), MTL::Size(static_cast<NS::UInteger>(width), static_cast<NS::UInteger>(height), 1), readback.get(), 0, static_cast<NS::UInteger>(row), static_cast<NS::UInteger>(row * static_cast<std::size_t>(height)));
    blit->endEncoding();
    buffer->commit();
    buffer->waitUntilCompleted();
    const auto* bytes = static_cast<const std::uint8_t*>(readback->contents());
    out.width = width;
    out.height = height;
    out.rgba.resize(row * static_cast<std::size_t>(height));
    for (std::size_t i = 0; i < out.rgba.size(); i += 4) {
        out.rgba[i] = bytes[i + 2];
        out.rgba[i + 1] = bytes[i + 1];
        out.rgba[i + 2] = bytes[i];
        out.rgba[i + 3] = 255;
    }
    state->evict();
    pool->release();
    return out;
}

}
