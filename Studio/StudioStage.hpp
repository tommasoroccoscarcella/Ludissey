#pragma once

#include "Engine/Character/Character.hpp"
#include "Engine/Rendering/Quality.hpp"
#include "Engine/Rendering/World.hpp"
#include "Engine/Studio/StudioDocument.hpp"
#include "Interface/Stage.hpp"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>

namespace studio {

class StudioStage final : public ui::Stage, public ld::StudioViewportHooks {
public:
    enum class GizmoState { idle, hover, pressed, dragging, disabled };
    static constexpr double handleTolerance = 4;

    explicit StudioStage(ld::StudioDocument& document);
    ~StudioStage() override;

    ld::AvatarConfiguration avatar = ld::AvatarConfiguration::standard("", std::string(ld::rules::avatar::standard::bodyPreset));
    std::function<void()> invalidated;
    std::optional<ui::Frame> marquee;

    const ld::render::Scene* scene() const override { return &stage; }
    const ld::render::Node* camera() const override { return eye.get(); }
    ld::render::Quality quality() const override;
    bool continuous() const override;
    void tick(double now, double delta) override;
    bool pointer(const ui::PointerEvent& event, ui::Size viewport) override;
    bool key(const ui::KeyInput& key) override;
    void resigned() override;
    void stop() override;

    void refresh() override;
    std::optional<ld::Vec2> focus() override { return ld::Vec2{target.x, target.z}; }
    std::optional<ld::CharacterState> characterState() override;
    void startPlay() override;
    void endPlay() override;
    bool takeFall() override;
    void teleport(ld::Vec3 point) override;
    void pace(double pace, double leap) override;
    std::optional<ld::CharacterState> respawn() override;
    void hideCharacter() override;
    std::string avatarName() override { return avatar.displayName.empty() ? "Player" : avatar.displayName; }

    GizmoState gizmoState() const;
    bool interacting() const;
    bool painting() const;
    std::optional<std::pair<int, int>> highlightedHandle() const;
    std::optional<std::pair<int, int>> handleAt(ui::Point point) const;
    ld::Color handleColor(std::pair<int, int> handle) const;
    void frameSelection();
    void orient(double yaw, double pitch, double distance, ld::Vec3 target);
    std::optional<ld::Vec3> brushPoint(ui::Point point);
    using RegionHandle = std::tuple<int, int, int>;
    std::optional<RegionHandle> regionHandleAt(ui::Point point) const;
    bool regionShown() const { return !region->hidden; }
    bool brushShown() const { return !brush->hidden; }
    bool brushDisc() const { return !disc->hidden; }
    ld::render::Image snapshot(int width, int height);

private:
    ld::StudioDocument& document;
    ld::render::Scene stage;
    ld::render::NodePtr eye = ld::render::Node::make("studio.camera");
    ld::render::NodePtr selectionTemplate;
    ld::render::NodePtr highlights = ld::render::Node::make("studio.highlights");
    ld::render::NodePtr grid = ld::render::Node::make("studio.grid");
    std::string gridSignature;
    static constexpr double gridSpacing = 2;
    static constexpr int gridMajor = 5;
    ld::render::NodePtr gizmo = ld::render::Node::make("studio.gizmo");
    ld::render::NodePtr brush = ld::render::Node::make("studio.brush");
    ld::render::NodePtr ball = ld::render::Node::make("studio.brush.sphere");
    ld::render::NodePtr disc = ld::render::Node::make("studio.brush.disc");
    ld::render::NodePtr region = ld::render::Node::make("studio.region");
    std::optional<RegionHandle> regionActive;
    std::optional<RegionHandle> regionHover;
    ld::TerrainRegion regionOrigin;
    double regionStart = 0;
    ld::Vec3 regionLine;
    void buildRegion();
    void syncRegion();
    std::optional<double> regionParameter(ui::Point point) const;
    void showBrush(ld::Vec3 point, ld::TerrainTool tool);
    ld::render::TerrainView terrain;
    std::map<std::string, ld::render::NodePtr> nodes;
    std::map<std::string, std::string> signatures;
    std::optional<ld::StudioTool> gizmoKind;
    std::optional<int> activeAxis;
    std::optional<std::pair<int, int>> activeHandle;
    std::optional<std::pair<int, int>> hoveredHandle;
    bool dragging = false;
    bool gizmoDisabled = false;
    int activeSign = 1;
    double axisStart = 0;
    ld::WorldBlock axisOrigin;
    ld::BlockSet axisOrigins;
    ld::Vec3 axisAnchor;
    std::optional<ld::TransformFrame> axisFrame;
    int stroke = 0;
    std::optional<ld::Vec3> lastBrush;
    ld::Vec3 target{0, 1, 0};
    double yaw = ld::pi / 4;
    double pitch = 0.6;
    double distance = 24;
    double playDistance = 7;
    ui::Point lastPoint;
    std::set<int> keys;
    ld::BlockSet dragOrigins;
    std::optional<ld::Vec3> dragStart;
    std::optional<ui::Point> marqueeStart;
    ld::CharacterDriver driver;
    ld::render::CharacterPtr character;
    bool jumpRequested = false;
    std::set<int> playKeys;
    bool playActive = false;
    bool fell = false;
    bool dead = false;
    double paceFactor = 1;
    double leapFactor = 1;
    std::vector<ld::WorldBlock> collisionBlocks;
    ld::Terrain collisionTerrain;
    ld::render::GraphicsQuality graphics;
    double clock = 0;
    bool shift = false;

    ld::render::Viewport viewport() const { return {std::max(1.0, size.width), std::max(1.0, size.height)}; }
    const ld::WorldBlock* baseplate(const std::vector<ld::WorldBlock>& blocks) const;
    void syncGrid(const std::vector<ld::WorldBlock>& blocks);
    void buildGizmo(ld::StudioTool tool);
    std::optional<ld::TransformFrame> gizmoFrame() const;
    void syncGizmo();
    void shade();
    double pixels(ld::Vec3 world, double radius) const;
    std::optional<double> handleParameter(ui::Point point);
    std::optional<double> axisAngle(ui::Point point, int axis, ld::Vec3 origin);
    void dragAxis(ui::Point point);
    std::optional<std::string> partAt(ui::Point point);
    std::optional<ld::Vec3> hit(ui::Point point, double height);
    void paint(ui::Point point, ld::TerrainTool tool);
    void look(double dx, double dy);
    void applyCamera();
    void stepCharacter(double delta);
    void fly(double delta);
    void notify();
};

}
