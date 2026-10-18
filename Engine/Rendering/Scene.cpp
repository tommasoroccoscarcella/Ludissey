#include "Scene.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ld::render {

namespace {

std::atomic<std::uint64_t> counter{1};

struct Builder {
    GeometryPtr g = std::make_shared<Geometry>();
    std::uint32_t vertex(Vec3 p, Vec3 n, double u = 0, double v = 0) {
        g->positions.insert(g->positions.end(), {static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)});
        g->normals.insert(g->normals.end(), {static_cast<float>(n.x), static_cast<float>(n.y), static_cast<float>(n.z)});
        g->uvs.insert(g->uvs.end(), {static_cast<float>(u), static_cast<float>(v)});
        return static_cast<std::uint32_t>(g->positions.size() / 3 - 1);
    }
    void triangle(std::uint32_t a, std::uint32_t b, std::uint32_t c) { g->indices.insert(g->indices.end(), {a, b, c}); }
    void quad(std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d) {
        triangle(a, b, c);
        triangle(a, c, d);
    }
    GeometryPtr done() {
        g->finish();
        return g;
    }
};

std::vector<double> chamferSteps(double half, double radius, int segments) {
    std::vector<double> steps;
    if (radius <= 0) return {-half, half};
    for (int i = 0; i <= segments; ++i) steps.push_back(-half + radius * (1 - std::cos(pi / 2 * i / segments)));
    for (int i = segments; i >= 0; --i) steps.push_back(half - radius * (1 - std::cos(pi / 2 * i / segments)));
    steps.erase(std::unique(steps.begin(), steps.end(), [](double a, double b) { return std::abs(a - b) < 1e-9; }), steps.end());
    return steps;
}

Quaternion quaternion(const Mat3& m) {
    double trace = m[0][0] + m[1][1] + m[2][2];
    if (trace > 0) {
        double s = std::sqrt(trace + 1) * 2;
        return {(m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s, s / 4};
    }
    if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
        double s = std::sqrt(1 + m[0][0] - m[1][1] - m[2][2]) * 2;
        return {s / 4, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s, (m[2][1] - m[1][2]) / s};
    }
    if (m[1][1] > m[2][2]) {
        double s = std::sqrt(1 + m[1][1] - m[0][0] - m[2][2]) * 2;
        return {(m[0][1] + m[1][0]) / s, s / 4, (m[1][2] + m[2][1]) / s, (m[0][2] - m[2][0]) / s};
    }
    double s = std::sqrt(1 + m[2][2] - m[0][0] - m[1][1]) * 2;
    return {(m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, s / 4, (m[1][0] - m[0][1]) / s};
}

bool rayTriangle(const Ray& r, Vec3 a, Vec3 b, Vec3 c, bool twoSided, double& t, Vec3& normal) {
    Vec3 e1 = b - a, e2 = c - a;
    Vec3 p = cross(r.direction, e2);
    double det = dot(e1, p);
    if (std::abs(det) < 1e-12) return false;
    if (!twoSided && det < 0) return false;
    double inv = 1 / det;
    Vec3 s = r.origin - a;
    double u = dot(s, p) * inv;
    if (u < 0 || u > 1) return false;
    Vec3 q = cross(s, e1);
    double v = dot(r.direction, q) * inv;
    if (v < 0 || u + v > 1) return false;
    t = dot(e2, q) * inv;
    normal = cross(e1, e2).normalized();
    return t > 1e-9;
}

bool rayBox(const Ray& r, const Bounds& b, double& enter) {
    double lo = -std::numeric_limits<double>::infinity(), hi = std::numeric_limits<double>::infinity();
    double o[3] = {r.origin.x, r.origin.y, r.origin.z}, d[3] = {r.direction.x, r.direction.y, r.direction.z};
    double l[3] = {b.low.x, b.low.y, b.low.z}, h[3] = {b.high.x, b.high.y, b.high.z};
    for (int i = 0; i < 3; ++i) {
        if (std::abs(d[i]) < 1e-15) {
            if (o[i] < l[i] - 1e-9 || o[i] > h[i] + 1e-9) return false;
            continue;
        }
        double t1 = (l[i] - o[i]) / d[i], t2 = (h[i] - o[i]) / d[i];
        if (t1 > t2) std::swap(t1, t2);
        lo = std::max(lo, t1);
        hi = std::min(hi, t2);
        if (lo > hi) return false;
    }
    enter = lo;
    return hi >= 0;
}

}

std::shared_ptr<Texture> Texture::make(std::vector<std::uint8_t> rgba, int width, int height) {
    auto t = std::make_shared<Texture>();
    t->rgba = std::move(rgba);
    t->width = width;
    t->height = height;
    t->id = counter++;
    return t;
}

Geometry::Geometry() : id(counter++) {}

void Geometry::touch() {
    ++version;
    cachedBounds.reset();
}

void Geometry::finish() {
    if (elements.empty()) elements.push_back({0, static_cast<std::uint32_t>(indices.empty() ? vertexCount() : indices.size()), 0});
    if (uvs.size() / 2 != vertexCount()) uvs.assign(vertexCount() * 2, 0);
    cachedBounds.reset();
}

Bounds Geometry::bounds() const {
    if (cachedBounds) return *cachedBounds;
    Bounds b{{0, 0, 0}, {0, 0, 0}};
    if (!positions.empty()) {
        b.low = b.high = {positions[0], positions[1], positions[2]};
        for (std::size_t i = 0; i + 2 < positions.size(); i += 3) b = b.merged({{positions[i], positions[i + 1], positions[i + 2]}, {positions[i], positions[i + 1], positions[i + 2]}});
    }
    cachedBounds = b;
    return b;
}

std::shared_ptr<Geometry> Geometry::copy() const {
    auto g = std::make_shared<Geometry>(*this);
    g->id = counter++;
    g->version = 1;
    return g;
}

namespace shapes {

GeometryPtr box(double width, double height, double length, double chamfer) {
    Builder b;
    Vec3 half{width / 2, height / 2, length / 2};
    double r = std::clamp(chamfer, 0.0, std::min({half.x, half.y, half.z}));
    int segments = r > 0 ? 3 : 0;
    struct Face {
        Vec3 normal, u, v;
    };
    const Face faces[6] = {{{0, 0, 1}, {1, 0, 0}, {0, 1, 0}}, {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}}, {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
                           {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}}, {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}}, {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}}};
    auto component = [](Vec3 v, Vec3 axis) { return std::abs(axis.x) > 0 ? v.x : std::abs(axis.y) > 0 ? v.y : v.z; };
    for (const auto& f : faces) {
        double hu = std::abs(component(half, f.u)), hv = std::abs(component(half, f.v)), hn = std::abs(component(half, f.normal));
        auto us = chamferSteps(hu, r, segments), vs = chamferSteps(hv, r, segments);
        std::uint32_t base = static_cast<std::uint32_t>(b.g->vertexCount());
        for (std::size_t j = 0; j < vs.size(); ++j)
            for (std::size_t i = 0; i < us.size(); ++i) {
                Vec3 p = f.u * us[i] + f.v * vs[j] + f.normal * hn;
                Vec3 inner{std::clamp(p.x, -half.x + r, half.x - r), std::clamp(p.y, -half.y + r, half.y - r), std::clamp(p.z, -half.z + r, half.z - r)};
                Vec3 n = f.normal;
                if (r > 0) {
                    Vec3 d = p - inner;
                    if (d.length() > 1e-12) n = d.normalized();
                    p = inner + n * r;
                }
                b.vertex(p, n, (us[i] + hu) / (2 * hu), 1 - (vs[j] + hv) / (2 * hv));
            }
        std::uint32_t row = static_cast<std::uint32_t>(us.size());
        for (std::uint32_t j = 0; j + 1 < vs.size(); ++j)
            for (std::uint32_t i = 0; i + 1 < row; ++i) {
                std::uint32_t a = base + j * row + i;
                b.quad(a, a + 1, a + row + 1, a + row);
            }
    }
    return b.done();
}

GeometryPtr edges(double width, double height, double length) {
    Builder b;
    Vec3 h{width / 2, height / 2, length / 2};
    Vec3 corners[8];
    for (int i = 0; i < 8; ++i) corners[i] = {(i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z};
    for (int i = 0; i < 8; ++i)
        for (int bit : {1, 2, 4}) {
            if (i & bit) continue;
            Vec3 a = corners[i], c = corners[i | bit];
            Vec3 n = (a + c).normalized();
            std::uint32_t first = b.vertex(a, n), second = b.vertex(c, n);
            b.triangle(first, second, second);
        }
    return b.done();
}

GeometryPtr sphere(double radius, int segments) {
    Builder b;
    int rings = segments, around = segments;
    for (int j = 0; j <= rings; ++j) {
        double phi = pi * j / rings;
        for (int i = 0; i <= around; ++i) {
            double theta = 2 * pi * i / around;
            Vec3 n{std::sin(phi) * std::sin(theta), std::cos(phi), std::sin(phi) * std::cos(theta)};
            b.vertex(n * radius, n, static_cast<double>(i) / around, static_cast<double>(j) / rings);
        }
    }
    std::uint32_t row = static_cast<std::uint32_t>(around + 1);
    for (int j = 0; j < rings; ++j)
        for (int i = 0; i < around; ++i) {
            std::uint32_t a = static_cast<std::uint32_t>(j) * row + static_cast<std::uint32_t>(i);
            if (j != 0) b.triangle(a, a + row, a + 1);
            if (j != rings - 1) b.triangle(a + 1, a + row, a + row + 1);
        }
    return b.done();
}

namespace {

void cap(Builder& b, double radius, double y, int segments, bool top) {
    Vec3 n{0, top ? 1.0 : -1.0, 0};
    std::uint32_t centre = b.vertex({0, y, 0}, n, 0.5, 0.5);
    std::uint32_t first = static_cast<std::uint32_t>(b.g->vertexCount());
    for (int i = 0; i <= segments; ++i) {
        double theta = 2 * pi * i / segments;
        b.vertex({radius * std::sin(theta), y, radius * std::cos(theta)}, n, 0.5 + 0.5 * std::sin(theta), 0.5 + 0.5 * std::cos(theta));
    }
    for (int i = 0; i < segments; ++i) {
        std::uint32_t a = first + static_cast<std::uint32_t>(i);
        if (top) b.triangle(centre, a, a + 1);
        else b.triangle(centre, a + 1, a);
    }
}

}

GeometryPtr cylinder(double radius, double height, int segments) {
    Builder b;
    double h = height / 2;
    for (int i = 0; i <= segments; ++i) {
        double theta = 2 * pi * i / segments;
        Vec3 n{std::sin(theta), 0, std::cos(theta)};
        b.vertex({n.x * radius, h, n.z * radius}, n, static_cast<double>(i) / segments, 0);
        b.vertex({n.x * radius, -h, n.z * radius}, n, static_cast<double>(i) / segments, 1);
    }
    for (int i = 0; i < segments; ++i) {
        std::uint32_t a = static_cast<std::uint32_t>(i * 2);
        b.quad(a, a + 1, a + 3, a + 2);
    }
    cap(b, radius, h, segments, true);
    cap(b, radius, -h, segments, false);
    return b.done();
}

GeometryPtr cone(double top, double bottom, double height, int segments) {
    Builder b;
    double h = height / 2;
    double slope = (bottom - top) / height;
    for (int i = 0; i <= segments; ++i) {
        double theta = 2 * pi * i / segments;
        Vec3 n = Vec3{std::sin(theta), slope, std::cos(theta)}.normalized();
        b.vertex({std::sin(theta) * top, h, std::cos(theta) * top}, n, static_cast<double>(i) / segments, 0);
        b.vertex({std::sin(theta) * bottom, -h, std::cos(theta) * bottom}, n, static_cast<double>(i) / segments, 1);
    }
    for (int i = 0; i < segments; ++i) {
        std::uint32_t a = static_cast<std::uint32_t>(i * 2);
        if (top > 0) b.quad(a, a + 1, a + 3, a + 2);
        else b.triangle(a, a + 1, a + 3);
    }
    if (top > 0) cap(b, top, h, segments, true);
    cap(b, bottom, -h, segments, false);
    return b.done();
}

GeometryPtr torus(double ring, double pipe, int ringSegments, int pipeSegments) {
    Builder b;
    for (int j = 0; j <= ringSegments; ++j) {
        double theta = 2 * pi * j / ringSegments;
        Vec3 centre{ring * std::sin(theta), 0, ring * std::cos(theta)};
        Vec3 out{std::sin(theta), 0, std::cos(theta)};
        for (int i = 0; i <= pipeSegments; ++i) {
            double phi = 2 * pi * i / pipeSegments;
            Vec3 n = out * std::cos(phi) + Vec3{0, 1, 0} * std::sin(phi);
            b.vertex(centre + n * pipe, n, static_cast<double>(j) / ringSegments, static_cast<double>(i) / pipeSegments);
        }
    }
    std::uint32_t row = static_cast<std::uint32_t>(pipeSegments + 1);
    for (int j = 0; j < ringSegments; ++j)
        for (int i = 0; i < pipeSegments; ++i) {
            std::uint32_t a = static_cast<std::uint32_t>(j) * row + static_cast<std::uint32_t>(i);
            b.quad(a, a + row, a + row + 1, a + 1);
        }
    return b.done();
}

GeometryPtr plane(double width, double height, double cornerRadius) {
    Builder b;
    double hw = width / 2, hh = height / 2;
    double r = std::clamp(cornerRadius, 0.0, std::min(hw, hh));
    Vec3 n{0, 0, 1};
    if (r <= 0) {
        std::uint32_t a = b.vertex({-hw, -hh, 0}, n, 0, 1), c = b.vertex({hw, -hh, 0}, n, 1, 1), d = b.vertex({hw, hh, 0}, n, 1, 0), e = b.vertex({-hw, hh, 0}, n, 0, 0);
        b.quad(a, c, d, e);
        return b.done();
    }
    std::uint32_t centre = b.vertex({0, 0, 0}, n, 0.5, 0.5);
    std::vector<Vec3> outline;
    const int steps = 8;
    const Vec3 corners[4] = {{hw - r, hh - r, 0}, {-hw + r, hh - r, 0}, {-hw + r, -hh + r, 0}, {hw - r, -hh + r, 0}};
    for (int c = 0; c < 4; ++c)
        for (int i = 0; i <= steps; ++i) {
            double a = pi / 2 * c + pi / 2 * i / steps;
            outline.push_back(corners[c] + Vec3{std::cos(a) * r, std::sin(a) * r, 0});
        }
    std::uint32_t first = static_cast<std::uint32_t>(b.g->vertexCount());
    for (auto p : outline) b.vertex(p, n, (p.x + hw) / width, 1 - (p.y + hh) / height);
    for (std::uint32_t i = 0; i < outline.size(); ++i) b.triangle(centre, first + i, first + (i + 1) % static_cast<std::uint32_t>(outline.size()));
    return b.done();
}

GeometryPtr wedge() {
    Builder b;
    const Vec3 corners[6] = {{-0.5, -0.5, -0.5}, {0.5, -0.5, -0.5}, {0.5, -0.5, 0.5}, {-0.5, -0.5, 0.5}, {-0.5, 0.5, 0.5}, {0.5, 0.5, 0.5}};
    const int faces[8][3] = {{0, 2, 1}, {0, 3, 2}, {3, 4, 5}, {3, 5, 2}, {0, 1, 5}, {0, 5, 4}, {0, 4, 3}, {1, 2, 5}};
    for (const auto& f : faces) {
        Vec3 a = corners[f[0]], c = corners[f[1]], d = corners[f[2]];
        Vec3 n = cross(c - a, d - a).normalized();
        std::uint32_t i = b.vertex(a, n), j = b.vertex(c, n), k = b.vertex(d, n);
        b.triangle(i, j, k);
    }
    return b.done();
}

GeometryPtr triangles(const std::vector<float>& positions, const std::vector<float>& normals) {
    auto g = std::make_shared<Geometry>();
    g->positions = positions;
    g->normals = normals;
    g->finish();
    return g;
}

GeometryPtr mesh(const std::vector<Vec3>& positions, const std::vector<Vec3>& normals, const std::vector<std::uint32_t>& indices) {
    Builder b;
    for (std::size_t i = 0; i < positions.size(); ++i) b.vertex(positions[i], i < normals.size() ? normals[i] : Vec3{0, 1, 0});
    b.g->indices = indices;
    return b.done();
}

}

NodePtr Node::make(std::string name) {
    auto n = std::make_shared<Node>();
    n->name = std::move(name);
    return n;
}

NodePtr Node::make(GeometryPtr geometry, std::string name) {
    auto n = make(std::move(name));
    n->geometry = std::move(geometry);
    return n;
}

void Node::setEuler(Vec3 r) {
    Quaternion qx = Quaternion::axisAngle({1, 0, 0}, r.x), qy = Quaternion::axisAngle({0, 1, 0}, r.y), qz = Quaternion::axisAngle({0, 0, 1}, r.z);
    orientation = qz * qy * qx;
}

void Node::setEulerDegrees(double x, double y, double z) { setEuler({x * pi / 180, y * pi / 180, z * pi / 180}); }

Vec3 Node::euler() const {
    Mat3 m = orientation.matrix();
    double y = std::asin(std::clamp(-m[2][0], -1.0, 1.0));
    if (std::abs(m[2][0]) < 0.999999) return {std::atan2(m[2][1], m[2][2]), y, std::atan2(m[1][0], m[0][0])};
    return {std::atan2(-m[1][2], m[1][1]), y, 0};
}

void Node::setYaw(double radians) {
    orientation = Quaternion::axisAngle({0, 1, 0}, radians);
}

void Node::look(Vec3 target, Vec3 up) {
    Vec3 eye = worldPosition();
    Vec3 back = (eye - target).normalized();
    if (back.length() < 1e-12) return;
    Vec3 right = cross(up, back);
    if (right.length() < 1e-9) right = cross(Vec3{0, 0, 1}, back);
    right = right.normalized();
    Vec3 upward = cross(back, right);
    Mat3 m;
    m.m = {{{right.x, upward.x, back.x}, {right.y, upward.y, back.y}, {right.z, upward.z, back.z}}};
    if (owner) {
        Mat4 pw = owner->world();
        Vec3 c0 = Vec3{pw.at(0, 0), pw.at(1, 0), pw.at(2, 0)}.normalized(), c1 = Vec3{pw.at(0, 1), pw.at(1, 1), pw.at(2, 1)}.normalized(), c2 = Vec3{pw.at(0, 2), pw.at(1, 2), pw.at(2, 2)}.normalized();
        Mat3 inverse;
        inverse.m = {{{c0.x, c0.y, c0.z}, {c1.x, c1.y, c1.z}, {c2.x, c2.y, c2.z}}};
        m = inverse * m;
    }
    orientation = quaternion(m);
}

Mat4 Node::local() const { return Mat4::translation(position) * Mat4::rotation(orientation.matrix()) * Mat4::scale(scale) * pivot.inverted(); }

Mat4 Node::world() const { return owner ? owner->world() * local() : local(); }

Vec3 Node::worldPosition() const { return world().transformPoint({0, 0, 0}); }

void Node::add(NodePtr child) {
    if (!child || child.get() == this) return;
    child->remove();
    child->owner = this;
    nodes.push_back(std::move(child));
}

void Node::remove() {
    if (!owner) return;
    Node* p = owner;
    owner = nullptr;
    auto& list = p->nodes;
    list.erase(std::remove_if(list.begin(), list.end(), [this](const NodePtr& n) { return n.get() == this; }), list.end());
}

void Node::replace(const NodePtr& old, NodePtr replacement) {
    for (auto& n : nodes)
        if (n == old) {
            old->owner = nullptr;
            replacement->remove();
            replacement->owner = this;
            n = std::move(replacement);
            return;
        }
}

void Node::clear() {
    for (auto& n : nodes) n->owner = nullptr;
    nodes.clear();
}

NodePtr Node::child(const std::string& value, bool recursive) const {
    for (const auto& n : nodes)
        if (n->name == value) return n;
    if (recursive)
        for (const auto& n : nodes)
            if (auto found = n->child(value, true)) return found;
    return nullptr;
}

bool Node::visible() const {
    for (const Node* n = this; n; n = n->owner)
        if (n->hidden) return false;
    return true;
}

void Node::each(const std::function<void(Node&)>& visit) {
    visit(*this);
    auto copy = nodes;
    for (auto& n : copy) n->each(visit);
}

Mat4 projection(const Camera& camera, double aspect) { return Mat4::perspective(camera.fieldOfView * pi / 180, aspect, camera.zNear, camera.zFar); }

Mat4 viewMatrix(const Node& camera) {
    Mat4 w = camera.world();
    Vec3 x = Vec3{w.at(0, 0), w.at(1, 0), w.at(2, 0)}.normalized(), y = Vec3{w.at(0, 1), w.at(1, 1), w.at(2, 1)}.normalized(), z = Vec3{w.at(0, 2), w.at(1, 2), w.at(2, 2)}.normalized();
    Vec3 p{w.at(0, 3), w.at(1, 3), w.at(2, 3)};
    Mat4 r;
    r.at(0, 0) = x.x; r.at(0, 1) = x.y; r.at(0, 2) = x.z; r.at(0, 3) = -dot(x, p);
    r.at(1, 0) = y.x; r.at(1, 1) = y.y; r.at(1, 2) = y.z; r.at(1, 3) = -dot(y, p);
    r.at(2, 0) = z.x; r.at(2, 1) = z.y; r.at(2, 2) = z.z; r.at(2, 3) = -dot(z, p);
    return r;
}

Vec3 project(const Node& camera, const Viewport& viewport, Vec3 world) {
    Camera settings = camera.camera.value_or(Camera{});
    Vec4 clip = projection(settings, viewport.width / std::max(1.0, viewport.height)) * (viewMatrix(camera) * Vec4{world.x, world.y, world.z, 1});
    if (std::abs(clip.w) < 1e-12) return {0, 0, 1};
    double nx = clip.x / clip.w, ny = clip.y / clip.w, nz = clip.z / clip.w;
    return {(nx + 1) / 2 * viewport.width, (1 - ny) / 2 * viewport.height, nz};
}

Vec3 unproject(const Node& camera, const Viewport& viewport, Vec3 screen) {
    Camera settings = camera.camera.value_or(Camera{});
    Mat4 inverse = (projection(settings, viewport.width / std::max(1.0, viewport.height)) * viewMatrix(camera)).inverted();
    double nx = screen.x / viewport.width * 2 - 1, ny = 1 - screen.y / viewport.height * 2;
    return inverse.transformPoint({nx, ny, screen.z});
}

Ray ray(const Node& camera, const Viewport& viewport, double x, double y) {
    Vec3 a = unproject(camera, viewport, {x, y, 0}), b = unproject(camera, viewport, {x, y, 1});
    return {a, (b - a).normalized()};
}

std::vector<Hit> rayTest(const Scene& scene, const Ray& r, HitOptions options) {
    std::vector<Hit> hits;
    std::function<void(Node&, const Mat4&)> visit = [&](Node& node, const Mat4& parent) {
        if (options.ignoreHidden && node.hidden) return;
        Mat4 world = parent * node.local();
        if (node.geometry && (node.categoryMask & options.mask) && node.geometry->vertexCount() > 0) {
            Mat4 inverse = world.inverted();
            Vec3 o = inverse.transformPoint(r.origin);
            Vec3 e = inverse.transformPoint(r.origin + r.direction);
            Ray local{o, (e - o)};
            double scale = local.direction.length();
            if (scale > 1e-15) {
                local.direction = local.direction * (1 / scale);
                double enter = 0;
                const Geometry& g = *node.geometry;
                Bounds b = g.bounds();
                Bounds padded{b.low - Vec3{1e-6, 1e-6, 1e-6}, b.high + Vec3{1e-6, 1e-6, 1e-6}};
                if (rayBox(local, padded, enter)) {
                    double best = std::numeric_limits<double>::infinity();
                    Vec3 bestNormal;
                    auto at = [&](std::uint32_t i) { return Vec3{g.positions[i * 3], g.positions[i * 3 + 1], g.positions[i * 3 + 2]}; };
                    bool twoSided = true;
                    std::size_t count = g.indices.empty() ? g.vertexCount() : g.indices.size();
                    for (std::size_t i = 0; i + 2 < count; i += 3) {
                        std::uint32_t ia = g.indices.empty() ? static_cast<std::uint32_t>(i) : g.indices[i];
                        std::uint32_t ib = g.indices.empty() ? static_cast<std::uint32_t>(i + 1) : g.indices[i + 1];
                        std::uint32_t ic = g.indices.empty() ? static_cast<std::uint32_t>(i + 2) : g.indices[i + 2];
                        double t;
                        Vec3 n;
                        if (rayTriangle(local, at(ia), at(ib), at(ic), twoSided, t, n) && t < best) {
                            best = t;
                            bestNormal = n;
                        }
                    }
                    if (std::isfinite(best)) {
                        Vec3 p = local.at(best);
                        Vec3 w = world.transformPoint(p);
                        Vec3 wn = (world.transformPoint(p + bestNormal) - w).normalized();
                        hits.push_back({&node, w, p, wn, (w - r.origin).length()});
                    }
                }
            }
        }
        for (const auto& c : node.children()) visit(*c, world);
    };
    visit(*scene.root, Mat4{});
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.distance < b.distance; });
    if (!options.all && hits.size() > 1) hits.resize(1);
    return hits;
}

std::vector<Hit> hitTest(const Scene& scene, const Node& camera, const Viewport& viewport, double x, double y, HitOptions options) { return rayTest(scene, ray(camera, viewport, x, y), options); }

}
