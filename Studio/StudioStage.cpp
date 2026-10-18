#include "StudioStage.hpp"

#include "Engine/Core/Text.hpp"
#include "Engine/Rendering/Renderer.hpp"
#include "Interface/Theme.hpp"

#include <algorithm>
#include <cmath>
#include <functional>

namespace studio {

using namespace ld;
using namespace ld::render;

namespace {

constexpr unsigned decoration = 1 | 16;
const ui::Color axisColors[3] = {ui::theme::stage::axisX, ui::theme::stage::axisY, ui::theme::stage::axisZ};

Color engine(ui::Color c) { return {c.r, c.g, c.b, c.a}; }

Material constant(Color color) {
    Material m;
    m.lighting = Lighting::constant;
    m.diffuse = color;
    return m;
}

Material overlay(Color color) {
    Material m = constant(color);
    m.readsDepth = false;
    m.writesDepth = false;
    return m;
}

constexpr int gizmoOrder = 40;

double& component(Vec3& v, int axis) { return axis == 0 ? v.x : axis == 1 ? v.y : v.z; }
Vec3 unit(int axis) { return axis == 0 ? Vec3{1, 0, 0} : axis == 1 ? Vec3{0, 1, 0} : Vec3{0, 0, 1}; }

std::optional<StudioStage::RegionHandle> regionOf(const std::string& name) {
    if (name.rfind("region:", 0) != 0) return std::nullopt;
    auto parts = split(name.substr(7), ':');
    if (parts.size() != 3) return std::nullopt;
    return StudioStage::RegionHandle{std::stoi(parts[0]), std::stoi(parts[1]), std::stoi(parts[2])};
}

std::optional<std::pair<int, int>> handleOf(const std::string& name) {
    if (name.rfind("gizmo:", 0) != 0) return std::nullopt;
    std::string rest = name.substr(6);
    auto colon = rest.find(':');
    if (colon == std::string::npos) return std::nullopt;
    try {
        return std::make_pair(std::stoi(rest.substr(0, colon)), std::stoi(rest.substr(colon + 1)));
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

}

StudioStage::StudioStage(StudioDocument& owner) : document(owner) {
    scene_factory::illuminate(stage, document.world.lighting);
    stage.root->add(grid);
    grid->castsShadow = false;
    selectionTemplate = Node::make(shapes::edges(1, 1, 1));
    Material selected = constant(engine(ui::theme::stage::selection));
    selected.wireframe = true;
    selected.doubleSided = true;
    selectionTemplate->material = selected;
    stage.root->add(highlights);
    stage.root->add(terrain.root);
    stage.underwater = [this](Vec3 p) { return terrain.underwater(p); };
    Material glow = constant(engine(ui::theme::colors::accent));
    glow.diffuse.a = 0.1;
    glow.writesDepth = false;
    Material wire = constant(engine(ui::theme::colors::accent));
    wire.wireframe = true;
    wire.doubleSided = true;
    wire.writesDepth = false;
    for (auto [holder, geometry, outline] : {std::tuple{ball, shapes::sphere(1, 16), shapes::sphere(1, 16)}, std::tuple{disc, shapes::cylinder(1, 1, 48), shapes::torus(1, 0.02, 64, 4)}}) {
        auto fill = Node::make(geometry, "fill");
        fill->material = glow;
        fill->castsShadow = false;
        auto edges = Node::make(outline, "wire");
        edges->material = holder == disc ? constant(engine(ui::theme::colors::accent)) : wire;
        edges->castsShadow = false;
        holder->add(fill);
        holder->add(edges);
        holder->castsShadow = false;
        brush->add(holder);
    }
    brush->hidden = true;
    brush->renderingOrder = 20;
    brush->castsShadow = false;
    stage.root->add(brush);
    region->hidden = true;
    region->castsShadow = false;
    stage.root->add(region);
    buildRegion();
    gizmo->hidden = true;
    stage.root->add(gizmo);
    eye->camera = Camera{};
    eye->camera->zFar = 400;
    eye->camera->zNear = 0.05;
    stage.root->add(eye);
    document.viewport = this;
    applyCamera();
    refresh();
}

StudioStage::~StudioStage() {
    if (document.viewport == this) document.viewport = nullptr;
}

void StudioStage::stop() {
    endPlay();
    if (document.viewport == this) document.viewport = nullptr;
}

void StudioStage::notify() {
    if (invalidated) invalidated();
}

Quality StudioStage::quality() const {
    Quality q;
    q.samples = 4;
    q.shadows = true;
    q.deferredShadows = true;
    q.scale = 2;
    return q;
}

bool StudioStage::continuous() const { return playActive || terrain.water || !keys.empty(); }

void StudioStage::refresh() {
    auto blocks = document.blocks();
    collisionBlocks = blocks;
    collisionTerrain = document.activeTerrain();
    terrain.sync(collisionTerrain);
    scene_factory::illuminate(stage, document.world.lighting);
    std::set<std::string> present;
    for (const auto& block : blocks) {
        present.insert(block.id);
        std::string signature = scene_factory::structure(block);
        if (signatures[block.id] != signature) {
            if (auto existing = nodes.find(block.id); existing != nodes.end()) existing->second->remove();
            auto node = scene_factory::block(block);
            stage.root->add(node);
            nodes[block.id] = node;
            signatures[block.id] = signature;
        }
        scene_factory::apply(block, *nodes[block.id]);
    }
    for (auto it = nodes.begin(); it != nodes.end();) {
        if (present.count(it->first)) {
            ++it;
            continue;
        }
        it->second->remove();
        signatures.erase(it->first);
        it = nodes.erase(it);
    }
    syncGrid(blocks);
    syncGizmo();
    syncRegion();
    std::vector<WorldBlock> targets;
    if (!document.playing)
        for (const auto& id : document.expanded(document.selectedIds()))
            for (const auto& block : blocks)
                if (block.id == id) targets.push_back(block);
    while (highlights->children().size() < targets.size()) {
        auto holder = Node::make("studio.outline");
        auto node = Node::make(selectionTemplate->geometry);
        node->material = selectionTemplate->material;
        node->castsShadow = false;
        holder->add(node);
        highlights->add(holder);
    }
    for (std::size_t i = 0; i < highlights->children().size(); ++i) {
        auto& holder = highlights->children()[i];
        if (i >= targets.size()) {
            holder->hidden = true;
            continue;
        }
        const auto& block = targets[i];
        auto found = nodes.find(block.id);
        Bounds bounds = found != nodes.end() ? scene_factory::bounds(block, *found->second) : scene_factory::bounds(block, Node());
        holder->hidden = false;
        holder->setEulerDegrees(block.rotationX, block.rotationY, block.rotationZ);
        holder->position = {block.x, block.y, block.z};
        auto& line = holder->children().front();
        line->position = (bounds.low + bounds.high) * 0.5;
        line->scale = bounds.high - bounds.low;
    }
}

const WorldBlock* StudioStage::baseplate(const std::vector<WorldBlock>& blocks) const {
    std::string canonical = Experience::baseplate(document.world.id).id;
    for (const auto& block : blocks)
        if (block.id == canonical) return &block;
    for (const auto& block : blocks)
        if (block.name == rules::baseplate::name && block.kind == "cube") return &block;
    return nullptr;
}

void StudioStage::syncGrid(const std::vector<WorldBlock>& blocks) {
    const WorldBlock* plate = baseplate(blocks);
    grid->hidden = !plate || !document.showGrid || document.playing;
    if (!plate) return;
    grid->position = {plate->x, plate->y, plate->z};
    grid->setEulerDegrees(plate->rotationX, plate->rotationY, plate->rotationZ);
    std::string signature = std::to_string(plate->sizeX) + "x" + std::to_string(plate->sizeY) + "x" + std::to_string(plate->sizeZ);
    if (signature == gridSignature) return;
    gridSignature = signature;
    grid->clear();
    auto thin = shapes::box(1, 1, 1, 0);
    double surface = plate->sizeY / 2 + 0.004;
    for (bool horizontal : {false, true}) {
        double across = horizontal ? plate->sizeZ : plate->sizeX, along = horizontal ? plate->sizeX : plate->sizeZ;
        int count = static_cast<int>(std::floor(across / 2 / gridSpacing + 1e-9));
        for (int step = -count; step <= count; ++step) {
            bool axis = step == 0;
            bool major = !axis && step % gridMajor == 0;
            double width = axis ? 0.06 : major ? 0.035 : 0.02;
            auto line = Node::make(thin);
            line->scale = {horizontal ? along - 0.0001 : width, 0.006, horizontal ? width : along - 0.0001};
            line->material = constant(engine(axis ? (horizontal ? ui::theme::stage::axisX : ui::theme::stage::axisZ) : major ? ui::theme::stage::gridMajor : ui::theme::stage::gridMinor));
            double offset = std::clamp(step * gridSpacing, -across / 2 + width / 2, across / 2 - width / 2);
            line->position = {horizontal ? 0.0 : offset, surface, horizontal ? offset : 0.0};
            line->castsShadow = false;
            grid->add(line);
        }
    }
}

void StudioStage::buildGizmo(StudioTool tool) {
    gizmo->clear();
    gizmoKind = tool;
    if (tool == StudioTool::select) return;
    for (int axis = 0; axis < 3; ++axis) {
        Color color = engine(axisColors[axis]);
        if (tool == StudioTool::rotate) {
            auto holder = Node::make("gizmo:" + std::to_string(axis) + ":1");
            auto ring = Node::make(shapes::torus(1, 0.035));
            ring->material = overlay(color);
            ring->castsShadow = false;
            ring->categoryMask = decoration;
            if (axis == 0) ring->setEuler({0, 0, pi / 2});
            if (axis == 2) ring->setEuler({pi / 2, 0, 0});
            holder->add(ring);
            holder->renderingOrder = gizmoOrder;
            gizmo->add(holder);
            continue;
        }
        for (int sign : {1, -1}) {
            auto holder = Node::make("gizmo:" + std::to_string(axis) + ":" + std::to_string(sign));
            auto shaft = Node::make(shapes::cylinder(0.03, 1));
            shaft->material = overlay(color);
            shaft->castsShadow = false;
            shaft->position = {0, 0.5, 0};
            shaft->categoryMask = decoration;
            auto tip = Node::make(tool == StudioTool::scale ? shapes::sphere(0.1) : shapes::cone(0, 0.09, 0.26));
            tip->material = overlay(color);
            tip->castsShadow = false;
            tip->position = {0, 1.05, 0};
            tip->categoryMask = decoration;
            holder->add(shaft);
            holder->add(tip);
            holder->renderingOrder = gizmoOrder;
            gizmo->add(holder);
        }
    }
    shade();
}

Color StudioStage::handleColor(std::pair<int, int> handle) const {
    if (gizmoDisabled) return engine(ui::theme::colors::textDisabled);
    Color base = engine(axisColors[handle.first]);
    double lift = activeHandle == handle ? 0.35 : hoveredHandle == handle && !activeHandle ? 0.25 : 0;
    return {base.r + (1 - base.r) * lift, base.g + (1 - base.g) * lift, base.b + (1 - base.b) * lift, base.a};
}

void StudioStage::shade() {
    for (const auto& holder : gizmo->children()) {
        auto handle = handleOf(holder->name);
        if (!handle) continue;
        Color color = handleColor(*handle);
        bool lit = highlightedHandle() == handle;
        double thickness = lit && document.tool != StudioTool::rotate ? 1.3 : 1;
        for (const auto& child : holder->children()) {
            if (child->material) child->material->diffuse = color;
            if (document.tool != StudioTool::rotate) child->scale = {thickness, 1, thickness};
        }
    }
}

StudioStage::GizmoState StudioStage::gizmoState() const {
    if (gizmoDisabled && !gizmo->hidden) return GizmoState::disabled;
    if (activeHandle) return dragging ? GizmoState::dragging : GizmoState::pressed;
    return hoveredHandle ? GizmoState::hover : GizmoState::idle;
}

bool StudioStage::interacting() const { return activeHandle.has_value() || dragStart.has_value() || (document.terrainTool.has_value() && lastBrush.has_value()); }

bool StudioStage::painting() const { return document.terrainTool.has_value() && lastBrush.has_value(); }

std::optional<std::pair<int, int>> StudioStage::highlightedHandle() const { return activeHandle ? activeHandle : hoveredHandle; }

double StudioStage::pixels(Vec3 world, double radius) const {
    Mat4 camera = eye->world();
    Vec3 right = (camera.transformPoint({1, 0, 0}) - camera.transformPoint({0, 0, 0})).normalized();
    Vec3 a = project(*eye, viewport(), world), b = project(*eye, viewport(), world + right * radius);
    return std::hypot(b.x - a.x, b.y - a.y);
}

std::optional<std::pair<int, int>> StudioStage::handleAt(ui::Point point) const {
    if (gizmo->hidden || gizmoDisabled) return std::nullopt;
    struct Candidate {
        std::pair<int, int> handle;
        double distance;
        double depth;
    };
    std::optional<Candidate> best;
    auto consider = [&](std::pair<int, int> handle, Vec3 a, Vec3 b, double radius) {
        Vec3 pa = project(*eye, viewport(), a), pb = project(*eye, viewport(), b);
        if (pa.z <= -1 || pa.z >= 1 || pb.z <= -1 || pb.z >= 1) return;
        double dx = pb.x - pa.x, dy = pb.y - pa.y, length = dx * dx + dy * dy;
        double t = length > 1e-9 ? std::clamp(((point.x - pa.x) * dx + (point.y - pa.y) * dy) / length, 0.0, 1.0) : 0;
        double distance = std::hypot(point.x - (pa.x + dx * t), point.y - (pa.y + dy * t));
        Vec3 world = a + (b - a) * t;
        double reach = std::max(pixels(world, radius), 1.0) + handleTolerance;
        if (distance > reach) return;
        double depth = (world - eye->worldPosition()).length();
        if (!best || distance < best->distance - 0.5 || (std::abs(distance - best->distance) <= 0.5 && depth < best->depth)) best = Candidate{handle, distance, depth};
    };
    for (const auto& holder : gizmo->children()) {
        auto handle = handleOf(holder->name);
        if (!handle) continue;
        Mat4 frame = holder->world();
        for (const auto& child : holder->children()) {
            if (!child->geometry) continue;
            Mat4 local = frame * child->local();
            Bounds bounds = child->geometry->bounds();
            if (document.tool == StudioTool::rotate) {
                double pipe = bounds.high.y;
                double ring = bounds.high.x - pipe;
                int segments = 96;
                for (int i = 0; i < segments; ++i) {
                    double t0 = 2 * pi * i / segments, t1 = 2 * pi * (i + 1) / segments;
                    Vec3 a = local.transformPoint({ring * std::sin(t0), 0, ring * std::cos(t0)}), b = local.transformPoint({ring * std::sin(t1), 0, ring * std::cos(t1)});
                    double radius = (local.transformPoint({0, pipe, 0}) - local.transformPoint({0, 0, 0})).length();
                    consider(*handle, a, b, radius);
                }
                continue;
            }
            Vec3 a = local.transformPoint({0, bounds.low.y, 0}), b = local.transformPoint({0, bounds.high.y, 0});
            double radius = (local.transformPoint({std::max(std::abs(bounds.low.x), std::abs(bounds.high.x)), 0, 0}) - local.transformPoint({0, 0, 0})).length();
            consider(*handle, a, b, radius);
        }
    }
    if (!best) return std::nullopt;
    return best->handle;
}

std::optional<TransformFrame> StudioStage::gizmoFrame() const {
    std::vector<WorldBlock> blocks;
    auto targets = document.transformTargets();
    if (targets.empty())
        for (const auto& id : document.expanded(document.selectedIds()))
            if (const WorldBlock* b = document.world.block(id); b && b->kind != "model") targets.push_back(id);
    for (const auto& id : targets)
        for (const auto& block : document.world.blocks)
            if (block.id == id) blocks.push_back(block);
    TransformSpace space = document.tool == StudioTool::scale ? TransformSpace::local : document.tool == StudioTool::move ? document.moveSpace : TransformSpace::world;
    return transform_math::frame(blocks, space, blocks.size() == 1);
}

void StudioStage::syncGizmo() {
    auto frame = gizmoFrame();
    if (document.playing || document.terrainTool || document.regionVisible() || document.tool == StudioTool::select || !frame) {
        gizmo->hidden = true;
        hoveredHandle.reset();
        return;
    }
    bool locked = document.transformTargets().empty();
    if (gizmoKind != document.tool) {
        hoveredHandle.reset();
        gizmoDisabled = locked;
        buildGizmo(document.tool);
    } else if (locked != gizmoDisabled) {
        gizmoDisabled = locked;
        hoveredHandle.reset();
        shade();
    }
    double span = std::max(0.6, distance) * 0.13;
    gizmo->position = frame->center;
    if (document.tool == StudioTool::rotate) gizmo->scale = {span, span, span};
    else {
        gizmo->scale = {1, 1, 1};
        for (const auto& holder : gizmo->children()) {
            auto handle = handleOf(holder->name);
            if (!handle) continue;
            Vec3 direction = frame->direction(handle->first, handle->second);
            double reach = frame->half[static_cast<std::size_t>(handle->first)] + span * 0.15;
            holder->position = direction * reach;
            holder->orientation = Quaternion::between({0, 1, 0}, direction.normalized());
            holder->scale = {span, span, span};
        }
    }
    gizmo->hidden = false;
}

std::optional<double> StudioStage::handleParameter(ui::Point point) {
    if (!axisFrame || !activeAxis) return std::nullopt;
    Ray r = ray(*eye, viewport(), point.x, point.y);
    return transform_math::closestParameter(r.origin, r.direction, axisFrame->face(*activeAxis, activeSign), axisFrame->direction(*activeAxis, activeSign));
}

std::optional<double> StudioStage::axisAngle(ui::Point point, int axis, Vec3 origin) {
    Ray r = ray(*eye, viewport(), point.x, point.y);
    Vec3 normal = axis == 0 ? Vec3{1, 0, 0} : axis == 1 ? Vec3{0, 1, 0} : Vec3{0, 0, 1};
    double denominator = dot(normal, r.direction);
    if (std::abs(denominator) <= 0.000001) return std::nullopt;
    double travel = dot(normal, origin - r.origin) / denominator;
    if (travel <= 0) return std::nullopt;
    Vec3 local = r.at(travel) - origin;
    if (axis == 0) return std::atan2(local.z, local.y) * 180 / pi;
    if (axis == 1) return std::atan2(local.x, local.z) * 180 / pi;
    return std::atan2(local.y, local.x) * 180 / pi;
}

void StudioStage::dragAxis(ui::Point point) {
    if (!activeAxis) return;
    int axis = *activeAxis;
    if (document.tool == StudioTool::rotate) {
        auto angle = axisAngle(point, axis, axisAnchor);
        if (!angle) return;
        double delta = *angle - axisStart;
        if (delta > 180) delta -= 360;
        if (delta < -180) delta += 360;
        bool direct = axisOrigins.size() == 1 && (axis == 2 || (axisOrigin.rotationZ == 0 && (axis == 1 || axisOrigin.rotationY == 0)));
        if (!direct) {
            document.perform(StudioEdit::rotateGroup(axis, delta, std::string("gizmo")));
            refresh();
            return;
        }
        auto origin = axisOrigin;
        double snap = document.angleSnap;
        document.update(origin.id, [origin, axis, delta, snap](WorldBlock& block) {
            double base = axis == 0 ? origin.rotationX : axis == 1 ? origin.rotationY : origin.rotationZ;
            double value = base + StudioDocument::snapped(delta, snap);
            if (axis == 0) block.rotationX = value;
            else if (axis == 1) block.rotationY = value;
            else block.rotationZ = value;
        }, std::string("Ruota"), std::string("gizmo"));
    } else if (document.tool == StudioTool::scale) {
        auto parameter = handleParameter(point);
        if (!parameter || !axisFrame) return;
        double delta = *parameter - axisStart;
        if (axisOrigins.size() == 1) document.resize(axisOrigin, axis, activeSign, delta, "gizmo");
        else {
            double extent = std::max(studio_limits::minimumSize, axisFrame->half[static_cast<std::size_t>(axis)] * 2);
            document.perform(StudioEdit::scaleGroup(axis, std::max(studio_limits::minimumSize, extent + delta) / extent, ScaleAnchor::face(activeSign), std::string("gizmo")));
        }
    } else {
        auto parameter = handleParameter(point);
        if (!parameter || !axisFrame) return;
        Vec3 unit = axis == 0 ? Vec3{1, 0, 0} : axis == 1 ? Vec3{0, 1, 0} : Vec3{0, 0, 1};
        bool aligned = axisFrame->axes[static_cast<std::size_t>(axis)] == unit;
        document.translate(axisOrigins, axisFrame->direction(axis, activeSign), *parameter - axisStart, aligned, "gizmo");
    }
    refresh();
}

std::optional<std::string> StudioStage::partAt(ui::Point point) {
    HitOptions options;
    options.all = true;
    for (const auto& result : hitTest(stage, *eye, viewport(), point.x, point.y, options))
        for (Node* node = result.node; node; node = node->parent())
            if (nodes.count(node->name) && !document.isLocked(node->name)) return node->name;
    return std::nullopt;
}

std::optional<Vec3> StudioStage::hit(ui::Point point, double height) {
    Ray r = ray(*eye, viewport(), point.x, point.y);
    if (std::abs(r.direction.y) <= 0.000001) return std::nullopt;
    double travel = (height - r.origin.y) / r.direction.y;
    if (travel <= 0) return std::nullopt;
    return Vec3{r.origin.x + r.direction.x * travel, height, r.origin.z + r.direction.z * travel};
}

std::optional<Vec3> StudioStage::brushPoint(ui::Point point) {
    Ray r = ray(*eye, viewport(), point.x, point.y);
    if (auto travel = collisionTerrain.raycast(r.origin, r.direction, 400)) return r.at(*travel);
    for (const auto& result : hitTest(stage, *eye, viewport(), point.x, point.y)) {
        if (result.node == brush.get()) continue;
        for (Node* node = result.node; node; node = node->parent())
            if (nodes.count(node->name)) return result.world;
    }
    return hit(point, 0);
}

void StudioStage::paint(ui::Point point, TerrainTool tool) {
    auto point3 = brushPoint(point);
    if (!point3) return;
    double radius = document.terrainBrush.radius;
    showBrush(*point3, tool);
    if (lastBrush && (*lastBrush - *point3).length() < radius * 0.3) return;
    lastBrush = point3;
    document.perform(StudioEdit::paint(tool, *point3, stroke));
}

bool StudioStage::pointer(const ui::PointerEvent& event, ui::Size) {
    ui::Point point = event.location;
    shift = event.shift;
    switch (event.phase) {
    case ui::PointerPhase::down: {
        lastPoint = point;
        dragOrigins.clear();
        dragStart.reset();
        marqueeStart.reset();
        marquee.reset();
        activeAxis.reset();
        if (!document.playing && document.regionVisible())
            if (auto handle = regionHandleAt(point)) {
                regionActive = handle;
                regionOrigin = document.terrainRegion;
                auto [move, axis, sign] = *handle;
                Vec3 anchor = (regionOrigin.minimum + regionOrigin.maximum) * 0.5;
                if (!move) component(anchor, axis) = sign > 0 ? component(regionOrigin.maximum, axis) : component(regionOrigin.minimum, axis);
                regionLine = anchor;
                regionStart = regionParameter(point).value_or(0);
                syncRegion();
                notify();
                return true;
            }
        if (document.terrainTool && !document.playing) {
            stroke = document.beginStroke();
            lastBrush.reset();
            paint(point, *document.terrainTool);
            return true;
        }
        if (!document.playing && !gizmo->hidden) {
            auto handle = handleAt(point);
            auto anchor = document.pivot();
            if (handle && anchor) {
                activeHandle = handle;
                hoveredHandle = handle;
                dragging = false;
                shade();
                activeAxis = handle->first;
                activeSign = handle->second;
                axisAnchor = *anchor;
                axisOrigins.clear();
                for (const auto& id : document.transformTargets())
                    for (const auto& block : document.world.blocks)
                        if (block.id == id) axisOrigins[id] = block;
                axisOrigin = axisOrigins.size() == 1 ? axisOrigins.begin()->second : WorldBlock{};
                axisFrame = gizmoFrame();
                axisStart = (document.tool == StudioTool::rotate ? axisAngle(point, handle->first, axisAnchor) : handleParameter(point)).value_or(0);
                return true;
            }
        }
        if (document.playing) return true;
        bool extend = event.shift || event.command;
        auto id = partAt(point);
        if (!id) {
            if (!extend) document.setSelection(std::nullopt);
            marqueeStart = point;
            refresh();
            return true;
        }
        if (extend) document.extendSelection(*id);
        else {
            auto ids = document.selectedIds();
            if (std::find(ids.begin(), ids.end(), *id) == ids.end()) document.setSelection(StudioSelection::instance(*id));
        }
        document.activeScript.reset();
        if (document.tool == StudioTool::select || document.tool == StudioTool::move)
            for (const auto& block : document.world.blocks)
                if (block.id == *id)
                    if (auto contact = hit(point, block.y)) {
                        for (const auto& target : document.transformTargets())
                            for (const auto& candidate : document.world.blocks)
                                if (candidate.id == target) dragOrigins[target] = candidate;
                        dragStart = contact;
                    }
        refresh();
        return true;
    }
    case ui::PointerPhase::dragged: {
        double dx = point.x - lastPoint.x, dy = -(point.y - lastPoint.y);
        lastPoint = point;
        if (document.playing) {
            look(dx, dy);
            return true;
        }
        if (regionActive) {
            if (auto parameter = regionParameter(point)) {
                auto [move, axis, sign] = *regionActive;
                double delta = *parameter - regionStart;
                TerrainRegion next = regionOrigin;
                if (move) {
                    component(next.minimum, axis) += delta;
                    component(next.maximum, axis) += delta;
                } else if (sign > 0) component(next.maximum, axis) += delta;
                else component(next.minimum, axis) += delta;
                document.terrainRegion = TerrainRegion::snapped(next.minimum, next.maximum);
                document.regionCustom = true;
                syncRegion();
                notify();
            }
            return true;
        }
        if (document.terrainTool) {
            paint(point, *document.terrainTool);
            return true;
        }
        if (activeAxis) {
            dragging = true;
            dragAxis(point);
            return true;
        }
        if (marqueeStart) {
            marquee = ui::Frame{std::min(marqueeStart->x, point.x), std::min(marqueeStart->y, point.y), std::abs(point.x - marqueeStart->x), std::abs(point.y - marqueeStart->y)};
            notify();
            return true;
        }
        if (!dragStart || dragOrigins.empty()) return true;
        auto contact = hit(point, dragStart->y);
        if (!contact) return true;
        const auto& anchor = dragOrigins.begin()->second;
        double x = StudioDocument::snapped(anchor.x + (contact->x - dragStart->x), document.moveSnap) - anchor.x;
        double z = StudioDocument::snapped(anchor.z + (contact->z - dragStart->z), document.moveSnap) - anchor.z;
        document.translate(dragOrigins, Vec3{x, 0, z}, "drag");
        refresh();
        return true;
    }
    case ui::PointerPhase::up: {
        if (marqueeStart) {
            ui::Frame area{std::min(marqueeStart->x, point.x), std::min(marqueeStart->y, point.y), std::abs(point.x - marqueeStart->x), std::abs(point.y - marqueeStart->y)};
            if (area.width > 3 || area.height > 3) {
                std::vector<std::string> inside;
                for (const auto& block : document.blocks()) {
                    if (block.kind == "model" || block.name == rules::baseplate::name || document.isLocked(block.id)) continue;
                    Vec3 projected = project(*eye, viewport(), block.position());
                    if (projected.z > 0 && projected.z < 1 && projected.x >= area.x && projected.x <= area.maxX() && projected.y >= area.y && projected.y <= area.maxY()) inside.push_back(block.id);
                }
                bool extend = event.shift || event.command;
                if (extend) {
                    auto selected = document.selectedIds();
                    for (const auto& id : inside)
                        if (std::find(selected.begin(), selected.end(), id) == selected.end()) selected.push_back(id);
                    document.select(selected);
                } else document.select(inside);
            }
            refresh();
        }
        marqueeStart.reset();
        marquee.reset();
        dragOrigins.clear();
        dragStart.reset();
        activeAxis.reset();
        activeHandle.reset();
        dragging = false;
        hoveredHandle = document.playing ? std::nullopt : handleAt(point);
        shade();
        lastBrush.reset();
        brush->hidden = true;
        regionActive.reset();
        syncRegion();
        document.endStroke();
        notify();
        return true;
    }
    case ui::PointerPhase::moved: {
        if (activeHandle) return false;
        if (document.terrainTool && !document.playing) {
            if (auto spot = brushPoint(point)) showBrush(*spot, *document.terrainTool);
            else brush->hidden = true;
            notify();
            return true;
        }
        if (document.regionVisible()) {
            auto over = regionHandleAt(point);
            if (over != regionHover) {
                regionHover = over;
                syncRegion();
                notify();
                return true;
            }
        }
        auto handle = document.playing ? std::nullopt : handleAt(point);
        if (handle == hoveredHandle) return false;
        hoveredHandle = handle;
        shade();
        return true;
    }
    case ui::PointerPhase::exited: {
        if (!brush->hidden && !lastBrush) {
            brush->hidden = true;
            notify();
        }
        if (activeHandle || !hoveredHandle) return false;
        hoveredHandle.reset();
        shade();
        return true;
    }
    case ui::PointerPhase::rightDown: lastPoint = point; return true;
    case ui::PointerPhase::rightDragged: {
        double dx = point.x - lastPoint.x, dy = -(point.y - lastPoint.y);
        lastPoint = point;
        look(dx, dy);
        return true;
    }
    case ui::PointerPhase::otherDown: lastPoint = point; return true;
    case ui::PointerPhase::otherDragged: {
        double dx = point.x - lastPoint.x, dy = -(point.y - lastPoint.y);
        lastPoint = point;
        if (document.playing) return true;
        target = camera_input::pan(target, yaw, pitch, dx, dy, distance * camera_input::panSpeed);
        applyCamera();
        return true;
    }
    case ui::PointerPhase::scroll: {
        double amount = event.precise ? event.scrollY * 0.01 : event.scrollY * 0.08;
        if (playActive) playDistance = camera_rig::zoom(playDistance, amount);
        else {
            distance = std::clamp(distance * std::exp(-amount), 2.0, 160.0);
            applyCamera();
        }
        return true;
    }
    default: return false;
    }
}

void StudioStage::look(double dx, double dy) {
    bool playing = document.playing;
    Vec3 before = eye->position;
    auto turned = camera_input::look(yaw, pitch, dx, dy, 1, playing ? camera_input::characterPitchLow : -camera_input::pitchLimit, playing ? camera_input::characterPitchHigh : camera_input::pitchLimit);
    yaw = turned.x;
    pitch = turned.y;
    if (playing) return;
    target = camera_input::target(before, yaw, pitch, distance);
    applyCamera();
}

void StudioStage::applyCamera() {
    if (playActive) return;
    eye->position = camera_input::eye(target, yaw, pitch, distance);
    eye->look(target);
    syncGizmo();
    syncRegion();
}

void StudioStage::buildRegion() {
    region->clear();
    Material fill = constant({1, 1, 1, 0.15});
    fill.writesDepth = false;
    fill.doubleSided = true;
    auto box = Node::make(shapes::box(1, 1, 1, 0), "region.fill");
    box->material = fill;
    box->castsShadow = false;
    box->renderingOrder = 25;
    region->add(box);
    for (int edge = 0; edge < 12; ++edge) {
        auto beam = Node::make(shapes::box(1, 1, 1, 0), "region.edge." + std::to_string(edge));
        beam->material = overlay(engine(ui::theme::colors::accent));
        beam->castsShadow = false;
        beam->renderingOrder = gizmoOrder;
        region->add(beam);
    }
    for (int axis = 0; axis < 3; ++axis)
        for (int sign : {1, -1}) {
            auto knob = Node::make(shapes::sphere(1, 16), "region:0:" + std::to_string(axis) + ":" + std::to_string(sign));
            knob->material = overlay(engine(axisColors[axis]));
            knob->castsShadow = false;
            knob->renderingOrder = gizmoOrder;
            region->add(knob);
            auto arrow = Node::make("region:1:" + std::to_string(axis) + ":" + std::to_string(sign));
            auto shaft = Node::make(shapes::cylinder(0.03, 1));
            shaft->material = overlay(engine(axisColors[axis]));
            shaft->position = {0, 0.5, 0};
            shaft->castsShadow = false;
            auto tip = Node::make(shapes::cone(0, 0.09, 0.26));
            tip->material = overlay(engine(axisColors[axis]));
            tip->position = {0, 1.05, 0};
            tip->castsShadow = false;
            arrow->add(shaft);
            arrow->add(tip);
            arrow->renderingOrder = gizmoOrder;
            region->add(arrow);
        }
}

void StudioStage::syncRegion() {
    bool visible = document.regionVisible();
    region->hidden = !visible;
    if (!visible) {
        regionHover.reset();
        return;
    }
    const auto& r = document.terrainRegion;
    Vec3 centre = (r.minimum + r.maximum) * 0.5, size = r.maximum - r.minimum;
    double perStud = std::max(1e-3, pixels(centre, 1));
    double line = 2 / perStud, knob = 7 / perStud, span = std::max(0.6, distance) * 0.13;
    int edge = 0;
    for (const auto& child : region->children()) {
        if (child->name == "region.fill") {
            child->position = centre;
            child->scale = size;
            continue;
        }
        if (child->name.rfind("region.edge.", 0) == 0) {
            int axis = edge / 4, corner = edge % 4;
            int a = (axis + 1) % 3, b = (axis + 2) % 3;
            Vec3 position = centre, scale{line, line, line};
            component(position, a) += (corner & 1 ? 0.5 : -0.5) * component(size, a);
            component(position, b) += (corner & 2 ? 0.5 : -0.5) * component(size, b);
            component(scale, axis) = component(size, axis) + line;
            child->position = position;
            child->scale = scale;
            ++edge;
            continue;
        }
        auto handle = regionOf(child->name);
        if (!handle) continue;
        auto [move, axis, sign] = *handle;
        bool lit = regionActive == handle || (!regionActive && regionHover == handle);
        double grow = lit ? 1.35 : 1;
        if (move) {
            child->position = centre;
            child->orientation = Quaternion::between({0, 1, 0}, unit(axis) * sign);
            child->scale = {span * grow, span, span * grow};
        } else {
            Vec3 position = centre;
            component(position, axis) += sign * component(size, axis) * 0.5;
            child->position = position;
            child->scale = {knob * grow, knob * grow, knob * grow};
        }
    }
}

std::optional<StudioStage::RegionHandle> StudioStage::regionHandleAt(ui::Point point) const {
    if (region->hidden) return std::nullopt;
    std::optional<RegionHandle> best;
    double closest = 1e9;
    for (const auto& child : region->children()) {
        auto handle = regionOf(child->name);
        if (!handle) continue;
        auto [move, axis, sign] = *handle;
        Vec3 a = child->position, b = child->position;
        double reach = 0;
        if (move) {
            b = a + unit(axis) * (sign * child->scale.y * 1.3);
            reach = handleTolerance + 4;
        } else reach = std::max(4.0, pixels(a, child->scale.x)) + handleTolerance;
        Vec3 pa = project(*eye, viewport(), a), pb = project(*eye, viewport(), b);
        if (pa.z <= -1 || pa.z >= 1) continue;
        double dx = pb.x - pa.x, dy = pb.y - pa.y, length = dx * dx + dy * dy;
        double t = length > 1e-9 ? std::clamp(((point.x - pa.x) * dx + (point.y - pa.y) * dy) / length, 0.0, 1.0) : 0;
        if (move && t < 0.35) continue;
        double gap = std::hypot(point.x - (pa.x + dx * t), point.y - (pa.y + dy * t));
        if (gap <= reach && gap < closest) {
            closest = gap;
            best = handle;
        }
    }
    return best;
}

std::optional<double> StudioStage::regionParameter(ui::Point point) const {
    if (!regionActive) return std::nullopt;
    Ray r = ray(*eye, viewport(), point.x, point.y);
    return transform_math::closestParameter(r.origin, r.direction, regionLine, unit(std::get<1>(*regionActive)));
}

void StudioStage::showBrush(Vec3 point, TerrainTool tool) {
    syncRegion();
    syncGizmo();
    double radius = document.terrainBrush.radius;
    bool surface = document.terrainBrush.placement == TerrainPlacement::surface && (tool == TerrainTool::raise || tool == TerrainTool::add || tool == TerrainTool::subtract) && document.terrainBrush.material != TerrainMaterial::water;
    brush->hidden = false;
    ball->hidden = surface;
    disc->hidden = !surface;
    brush->position = surface ? Terrain::snapped(point) : point;
    brush->scale = surface ? Vec3{radius, Terrain::cellSize / 8, radius} : Vec3{radius, radius, radius};
}

void StudioStage::orient(double y, double p, double d, Vec3 t) {
    yaw = y;
    pitch = p;
    distance = d;
    target = t;
    applyCamera();
}

void StudioStage::frameSelection() {
    auto anchor = document.anchor();
    if (!anchor) return;
    double reach = 1;
    bool any = false;
    for (const auto& id : document.transformTargets())
        for (const auto& block : document.world.blocks)
            if (block.id == id) {
                double value = std::max(std::abs(block.x - anchor->x), std::abs(block.z - anchor->z)) + block.size();
                reach = any ? std::max(reach, value) : value;
                any = true;
            }
    target = *anchor;
    distance = std::max(4.0, reach * 4);
    applyCamera();
}

bool StudioStage::key(const ui::KeyInput& key) {
    if (!key.down) {
        keys.erase(key.keyCode);
        playKeys.erase(key.keyCode);
        if (playActive && !key.command) document.key(static_cast<std::uint16_t>(key.keyCode), false);
        return playActive;
    }
    if (playActive && !key.command) {
        if (!key.repeat) document.key(static_cast<std::uint16_t>(key.keyCode), true);
        static const std::set<int> handled{0, 1, 2, 13, 49, 123, 124, 125, 126};
        if (handled.count(key.keyCode)) {
            if (!dead) playKeys.insert(key.keyCode);
            if (key.keyCode == 49 && !dead) jumpRequested = true;
            return true;
        }
    }
    if (document.playing && document.playMode == StudioPlayMode::character) return true;
    static const std::set<int> flyKeys{0, 1, 2, 13, 12, 14};
    if (!document.playing && flyKeys.count(key.keyCode) && !key.command && !key.control) {
        keys.insert(key.keyCode);
        shift = key.shift;
        return true;
    }
    if ((key.keyCode == 51 || key.keyCode == 117) && !document.playing && !key.command) {
        document.deleteSelection();
        refresh();
        return true;
    }
    if (key.keyCode == 3 && !key.command) {
        frameSelection();
        return true;
    }
    return false;
}

void StudioStage::resigned() {
    keys.clear();
    playKeys.clear();
}

void StudioStage::tick(double now, double delta) {
    if (document.debugPaused()) return;
    clock = now;
    stage.time = now;
    if (playActive) stepCharacter(delta);
    if (!keys.empty()) fly(delta);
}

void StudioStage::fly(double elapsed) {
    double delta = std::min(editor_flight::maximumStep, std::max(0.0, elapsed));
    auto axis = [this](int positive, int negative) { return (keys.count(positive) ? 1.0 : 0.0) - (keys.count(negative) ? 1.0 : 0.0); };
    auto direction = camera_input::fly(axis(13, 1), axis(2, 0), axis(14, 12), yaw, pitch);
    if (!direction) return;
    double speed = shift ? editor_flight::preciseSpeed : editor_flight::speed;
    target += *direction * (speed * delta);
    applyCamera();
}

void StudioStage::startPlay() {
    endPlay();
    collisionBlocks = document.blocks();
    if (document.playMode != StudioPlayMode::character) return;
    auto body = scene_factory::avatar(avatar);
    CharacterState spawn = document.playSpawn ? motion::spawn(collisionBlocks, collisionTerrain, document.playSpawn->x, document.playSpawn->y) : motion::arrival(collisionBlocks, collisionTerrain, document.playTeam());
    driver.reset(spawn);
    if (body) {
        body->node->position = spawn.position();
        stage.root->add(body->node);
    }
    character = body;
    jumpRequested = false;
    playKeys.clear();
    playActive = true;
    fell = false;
    dead = false;
    paceFactor = leapFactor = 1;
    notify();
}

std::optional<CharacterState> StudioStage::characterState() { return playActive && character ? std::optional<CharacterState>(driver.state()) : std::nullopt; }

bool StudioStage::takeFall() {
    bool value = fell;
    fell = false;
    return value;
}

void StudioStage::teleport(Vec3 point) {
    auto state = driver.state();
    state.x = point.x;
    state.y = point.y;
    state.z = point.z;
    state.vertical = 0;
    driver.reset(state);
}

void StudioStage::pace(double p, double l) {
    paceFactor = p;
    leapFactor = l;
}

void StudioStage::hideCharacter() {
    dead = true;
    playKeys.clear();
    if (character) character->node->hidden = true;
}

std::optional<CharacterState> StudioStage::respawn() {
    if (!playActive || !character) return std::nullopt;
    CharacterState spawn = document.playSpawn ? motion::spawn(collisionBlocks, collisionTerrain, document.playSpawn->x, document.playSpawn->y) : motion::arrival(collisionBlocks, collisionTerrain, document.playTeam());
    driver.reset(spawn);
    dead = false;
    fell = false;
    character->node->hidden = false;
    return spawn;
}

void StudioStage::endPlay() {
    playActive = false;
    if (character) character->node->remove();
    character.reset();
    playKeys.clear();
    applyCamera();
}

void StudioStage::stepCharacter(double delta) {
    if (!playActive || !character) return;
    CharacterInput input;
    if (playKeys.count(0) || playKeys.count(123)) input.strafe -= 1;
    if (playKeys.count(2) || playKeys.count(124)) input.strafe += 1;
    if (playKeys.count(13) || playKeys.count(126)) input.forward += 1;
    if (playKeys.count(1) || playKeys.count(125)) input.forward -= 1;
    input.ascend = playKeys.count(49) > 0;
    input.pace = paceFactor;
    input.leap = leapFactor;
    bool dropped = false;
    if (!dead)
        driver.advance(delta, [&](const CharacterState& state, double step) -> std::optional<CharacterState> {
            auto slice = input;
            slice.jump = jumpRequested;
            jumpRequested = false;
            auto next = motion::step(state, slice, yaw, collisionBlocks, collisionTerrain, step);
            if (motion::fallen(next)) {
                dropped = true;
                return std::nullopt;
            }
            return next;
        });
    if (dropped) {
        fell = true;
        dead = true;
    }
    const auto& state = driver.state();
    auto pose = driver.pose(delta);
    character->node->position = {pose.x, pose.y, pose.z};
    character->node->setYaw(pose.yaw);
    if (!state.grounded && !state.swimming) {
        double phase = state.vertical >= 0 ? 1 - state.vertical / motion::jumpSpeed : 1 + state.vertical / motion::jumpSpeed;
        character->animate("Jump", delta, std::clamp(phase, 0.0, 1.0));
    } else character->animate(state.moving ? "Walk" : "Idle", delta);
    Vec3 head = driver.follow({pose.x, pose.y + 0.9, pose.z}, delta);
    auto placed = camera_rig::place(head, yaw, pitch, playDistance, collisionBlocks, collisionTerrain);
    if (!dead) character->node->hidden = placed.firstPerson;
    eye->position = placed.position;
    Vec3 aim = placed.firstPerson ? world_space::forward(yaw, pitch) : head - placed.position;
    eye->look(placed.position + aim);
}

Image StudioStage::snapshot(int width, int height) { return Renderer::shared().snapshot(stage, *eye, width, height, 4, true); }

}
