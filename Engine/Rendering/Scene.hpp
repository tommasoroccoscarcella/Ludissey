#pragma once

#include "Engine/Core/Math.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ld::render {

struct Texture {
    std::vector<std::uint8_t> rgba;
    int width = 0;
    int height = 0;
    std::uint64_t id = 0;
    static std::shared_ptr<Texture> make(std::vector<std::uint8_t> rgba, int width, int height);
};

enum class Lighting { blinn, constant, terrain, water };

struct Material {
    Lighting lighting = Lighting::blinn;
    Color diffuse{1, 1, 1, 1};
    Color multiply{1, 1, 1, 1};
    Color emission{0, 0, 0, 0};
    std::shared_ptr<Texture> texture;
    double opacity = 1;
    bool doubleSided = false;
    bool wireframe = false;
    bool writesDepth = true;
    bool readsDepth = true;
    bool colorWrite = true;
    int terrainKind = 0;
    int surface = 0;
    double transparency = 0;
    double reflectance = 0;
    double waveSize = 0;
    double waveSpeed = 0;
    bool decal = false;
    bool deep = false;
    bool operator==(const Material& o) const {
        return lighting == o.lighting && diffuse == o.diffuse && multiply == o.multiply && emission == o.emission && texture == o.texture && opacity == o.opacity && doubleSided == o.doubleSided && wireframe == o.wireframe &&
               writesDepth == o.writesDepth && readsDepth == o.readsDepth && colorWrite == o.colorWrite && terrainKind == o.terrainKind && surface == o.surface && transparency == o.transparency &&
               reflectance == o.reflectance && waveSize == o.waveSize && waveSpeed == o.waveSpeed && decal == o.decal && deep == o.deep;
    }
};

struct Element {
    std::uint32_t start = 0;
    std::uint32_t count = 0;
    int material = 0;
};

class Geometry {
public:
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> colors;
    std::vector<float> uvs;
    std::vector<std::uint32_t> indices;
    std::vector<Element> elements;
    std::vector<Material> materials{Material{}};
    std::uint64_t id;
    std::uint64_t version = 1;

    Geometry();
    std::size_t vertexCount() const { return positions.size() / 3; }
    Bounds bounds() const;
    void touch();
    void finish();
    Material& material() { return materials.front(); }
    std::shared_ptr<Geometry> copy() const;

private:
    mutable std::optional<Bounds> cachedBounds;
};

using GeometryPtr = std::shared_ptr<Geometry>;

namespace shapes {
GeometryPtr box(double width, double height, double length, double chamfer);
GeometryPtr edges(double width, double height, double length);
GeometryPtr sphere(double radius, int segments = 24);
GeometryPtr cylinder(double radius, double height, int segments = 48);
GeometryPtr cone(double top, double bottom, double height, int segments = 48);
GeometryPtr torus(double ring, double pipe, int ringSegments = 48, int pipeSegments = 24);
GeometryPtr plane(double width, double height, double cornerRadius = 0);
GeometryPtr wedge();
GeometryPtr triangles(const std::vector<float>& positions, const std::vector<float>& normals);
GeometryPtr mesh(const std::vector<Vec3>& positions, const std::vector<Vec3>& normals, const std::vector<std::uint32_t>& indices);
}

enum class LightType { ambient, directional };

struct Light {
    LightType type = LightType::ambient;
    Color color{1, 1, 1, 1};
    double intensity = 1000;
    bool castsShadow = false;
    Color shadowColor{0, 0, 0, 1};
    bool deferredShadows = false;
    int shadowMapSize = 0;
    double shadowRadius = 3;
};

struct Camera {
    double fieldOfView = 60;
    double zNear = 0.1;
    double zFar = 400;
};

class Node;
using NodePtr = std::shared_ptr<Node>;

class Node : public std::enable_shared_from_this<Node> {
public:
    std::string name;
    Vec3 position;
    Quaternion orientation;
    Vec3 scale{1, 1, 1};
    Mat4 pivot;
    bool hidden = false;
    int renderingOrder = 0;
    unsigned categoryMask = 1;
    bool castsShadow = true;
    bool billboard = false;
    GeometryPtr geometry;
    std::optional<Material> material;
    std::optional<Light> light;
    std::optional<Camera> camera;

    static NodePtr make(std::string name = "");
    static NodePtr make(GeometryPtr geometry, std::string name = "");

    void setEuler(Vec3 radians);
    void setEulerDegrees(double x, double y, double z);
    Vec3 euler() const;
    void setYaw(double radians);
    void look(Vec3 target, Vec3 up = {0, 1, 0});
    Mat4 local() const;
    Mat4 world() const;
    Vec3 worldPosition() const;
    Node* parent() const { return owner; }
    const std::vector<NodePtr>& children() const { return nodes; }
    void add(NodePtr child);
    void remove();
    void replace(const NodePtr& old, NodePtr replacement);
    void clear();
    NodePtr child(const std::string& name, bool recursive = false) const;
    bool visible() const;
    void each(const std::function<void(Node&)>& visit);

private:
    Node* owner = nullptr;
    std::vector<NodePtr> nodes;
};

enum class BackgroundKind { color, gradient };

struct Background {
    BackgroundKind kind = BackgroundKind::color;
    Color color{1, 1, 1, 1};
    Color top{1, 1, 1, 1};
    Color bottom{1, 1, 1, 1};
};

struct Fog {
    double start = 0;
    double end = 0;
    Color color{1, 1, 1, 1};
    double exponent = 1;
};

struct Underwater {
    Color color{1, 1, 1, 1};
    double transparency = 0;
};

class Scene {
public:
    NodePtr root = Node::make("root");
    Background background;
    Fog fog;
    double time = 0;
    std::function<std::optional<Underwater>(Vec3)> underwater;
};

struct Viewport {
    double width = 1;
    double height = 1;
};

Mat4 projection(const Camera& camera, double aspect);
Mat4 viewMatrix(const Node& camera);
Vec3 project(const Node& camera, const Viewport& viewport, Vec3 world);
Vec3 unproject(const Node& camera, const Viewport& viewport, Vec3 screen);
Ray ray(const Node& camera, const Viewport& viewport, double x, double y);

struct Hit {
    Node* node = nullptr;
    Vec3 world;
    Vec3 local;
    Vec3 normal;
    double distance = 0;
};

struct HitOptions {
    unsigned mask = ~0u;
    bool all = false;
    bool ignoreHidden = true;
};

std::vector<Hit> hitTest(const Scene& scene, const Node& camera, const Viewport& viewport, double x, double y, HitOptions options = {});
std::vector<Hit> rayTest(const Scene& scene, const Ray& ray, HitOptions options = {});

}
