#pragma once

#include "Engine/Character/Character.hpp"
#include "Engine/DataModel/ClassRegistry.hpp"
#include "Engine/Experience/Experience.hpp"
#include "Engine/Gui/GuiLayout.hpp"
#include "Engine/Scripting/WorldSession.hpp"
#include "Engine/Studio/TransformMath.hpp"
#include "Engine/Terrain/Terrain.hpp"

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ld {

struct StudioAsset {
    std::string asset;
    std::string name;
    std::string kind;
    bool operator==(const StudioAsset&) const = default;
};

namespace studio_catalog {
const std::vector<std::string>& primitives();
const std::vector<std::string>& meshExtensions();
std::vector<std::string> contentExtensions(std::string_view property);
std::string materialTitle(std::string_view material);
std::string title(std::string_view kind);
std::string symbol(std::string_view kind);
}

namespace studio_assets {
std::filesystem::path previews();
std::filesystem::path preview(const std::string& project);
std::filesystem::path legacyFolder();
std::map<std::string, std::string> names();
StudioAsset install(const std::filesystem::path& source);
std::optional<std::string> adoptLegacy(const std::string& reference);
std::vector<StudioAsset> catalog();
}

enum class StudioCommand { undo, redo, copy, cut, paste, duplicate, remove, group, ungroup, selectAll, lock, unlock, play, playHere, run, stop, rotate, tilt };
std::string commandTitle(StudioCommand command);

enum class StudioAction { transform, rename, appearance, behavior, unlock, structure };
enum class StudioPlayMode { character, camera };
enum class ExperienceImage { icon, thumbnail };
enum class StudioTool { select, move, scale, rotate };
std::string toolTitle(StudioTool tool);
std::string_view toolKey(StudioTool tool);
std::string_view toolSymbol(StudioTool tool);

struct StudioSelection {
    bool service = false;
    std::string id;
    static StudioSelection instance(std::string id) { return {false, std::move(id)}; }
    static StudioSelection ofService(std::string name) { return {true, std::move(name)}; }
    std::optional<std::string> instanceId() const { return service ? std::nullopt : std::optional<std::string>(id); }
    bool operator==(const StudioSelection&) const = default;
};

struct StudioClipboard {
    std::vector<WorldBlock> blocks;
    std::vector<InstanceRecord> instances;
    bool empty() const { return blocks.empty() && instances.empty(); }
};

struct StudioExplorerDrag {
    std::vector<std::string> ids;
    Vec2 location;
    std::optional<std::string> target;
    bool valid = false;
};

enum class TerrainRegionAction { fillWater, clearWater, fill, clear };

struct StudioEdit {
    enum class Kind { generateTerrain, clearTerrain, terrain, region, water, rotateGroup, scaleGroup };
    Kind kind = Kind::clearTerrain;
    TerrainSettings settings;
    TerrainTool tool = TerrainTool::add;
    Vec3 point;
    Vec3 extent;
    TerrainRegionAction action = TerrainRegionAction::fillWater;
    TerrainMaterial material = TerrainMaterial::grass;
    TerrainWater water;
    int stroke = 0;
    int axis = 0;
    double amount = 0;
    ScaleAnchor anchor;
    std::optional<std::string> gesture;
    static StudioEdit generate(TerrainSettings s) { StudioEdit e; e.kind = Kind::generateTerrain; e.settings = s; return e; }
    static StudioEdit clear() { return StudioEdit{}; }
    static StudioEdit region(TerrainRegionAction action, const TerrainRegion& r, TerrainMaterial m = TerrainMaterial::grass) { StudioEdit e; e.kind = Kind::region; e.action = action; e.point = r.minimum; e.extent = r.maximum; e.material = m; return e; }
    static StudioEdit sea(const TerrainRegion& r, bool remove = false) { return region(remove ? TerrainRegionAction::clearWater : TerrainRegionAction::fillWater, r); }
    static StudioEdit waterProperties(const TerrainWater& w) { StudioEdit e; e.kind = Kind::water; e.water = w; return e; }
    static StudioEdit paint(TerrainTool t, Vec3 p, int stroke) { StudioEdit e; e.kind = Kind::terrain; e.tool = t; e.point = p; e.stroke = stroke; return e; }
    static StudioEdit rotateGroup(int axis, double degrees, std::optional<std::string> gesture) { StudioEdit e; e.kind = Kind::rotateGroup; e.axis = axis; e.amount = degrees; e.gesture = std::move(gesture); return e; }
    static StudioEdit scaleGroup(int axis, double factor, ScaleAnchor anchor, std::optional<std::string> gesture) { StudioEdit e; e.kind = Kind::scaleGroup; e.axis = axis; e.amount = factor; e.anchor = anchor; e.gesture = std::move(gesture); return e; }
};

struct StudioRevision {
    std::string label;
    Experience world;
    std::optional<StudioSelection> selection;
    std::vector<std::string> parts;
};

struct ScriptProblem {
    int line = 0;
    std::string message;
    bool operator==(const ScriptProblem&) const = default;
};

class StudioViewportHooks {
public:
    virtual ~StudioViewportHooks() = default;
    virtual void refresh() {}
    virtual std::optional<Vec2> focus() { return std::nullopt; }
    virtual std::optional<CharacterState> characterState() { return std::nullopt; }
    virtual void startPlay() {}
    virtual void endPlay() {}
    virtual bool takeFall() { return false; }
    virtual void teleport(Vec3) {}
    virtual void pace(double, double) {}
    virtual std::optional<CharacterState> respawn() { return std::nullopt; }
    virtual void hideCharacter() {}
    virtual std::string avatarName() { return "Player"; }
};

class StudioDocument {
public:
    static constexpr int outputLines = 400;
    static constexpr int history = 120;
    static StudioClipboard& clipboard();

    explicit StudioDocument(const ClassRegistry& registry = ClassRegistry::shared());
    ~StudioDocument();

    const ClassRegistry& registry;
    StudioViewportHooks* viewport = nullptr;
    std::function<bool(const std::string& asset)> meshAvailable;
    std::function<void()> changed;

    bool opened = false;
    Experience world;
    Experience savedWorld;
    std::vector<std::string> openScripts;
    std::optional<std::string> activeScript;
    bool playing = false;
    StudioPlayMode playMode = StudioPlayMode::character;
    std::optional<Vec2> playSpawn;
    std::vector<OutputLine> output;
    StudioTool tool = StudioTool::move;
    double moveSnap = studio_limits::snap;
    TransformSpace moveSpace = TransformSpace::world;
    double angleSnap = 15;
    std::set<std::string> expandedIds;
    std::set<std::string> toolboxOpen{"Parti", "Script"};
    bool workspaceOpen = true;
    std::set<std::string> panels{"explorer", "properties", "toolbox", "output"};
    bool showGrid = true;
    bool showGui = true;
    bool settingsOpen = false;
    std::optional<TerrainTool> terrainTool;
    std::map<std::string, Rect> explorerFrames;
    std::set<std::string> openVectors;
    std::optional<StudioExplorerDrag> explorerDrag;
    TerrainBrush terrainBrush;
    TerrainSettings terrainSettings;
    TerrainRegion terrainRegion = TerrainRegion::whole(rules::terrain::generation::waterLevel);
    bool regionCustom = false;
    std::string terrainTab = "create";
    std::optional<std::string> terrainMode;
    bool regionEditing = false;
    TerrainMaterial regionMaterial = TerrainMaterial::water;
    bool regionVisible() const { return terrainEditing && !playing && (terrainMode == std::optional<std::string>("sea") || (terrainTab == "region" && regionEditing)); }
    void closeTerrainTool() {
        terrainTool.reset();
        terrainMode.reset();
        regionEditing = false;
    }
    std::optional<std::string> insertTarget;
    std::string insertQuery;
    std::string toolboxCategory = "parts";
    std::string toolboxQuery;
    std::string apiQuery;
    std::vector<StudioAsset> assets;
    std::optional<std::string> assetsError;
    std::optional<std::string> renaming;
    std::set<std::string> expandedServices{"Workspace"};
    double dockWidth = 250;
    double propertiesWidth = 300;
    double utilityHeight = 200;
    std::string utilityTab = "output";
    std::string dockTab = "toolbox";
    double explorerShare = 0.45;
    bool utilityCollapsed = true;
    bool terrainEditing = false;
    std::optional<int> focusLine;
    std::map<std::string, std::set<std::uint32_t>> breakpoints;
    std::map<ScriptContext, lim::DebugStop> debugStops;
    bool debugPaused() const { return !debugStops.empty(); }
    void toggleBreakpoint(const std::string& script, std::uint32_t line);
    void resumeDebug(lim::StepMode mode);

    const std::optional<StudioSelection>& selection() const { return current; }
    void setSelection(std::optional<StudioSelection> value);
    const std::vector<std::string>& selectedParts() const { return parts; }
    const std::optional<std::string>& undoLabel() const { return undoTitle; }
    const std::optional<std::string>& redoLabel() const { return redoTitle; }
    const std::map<std::string, ScriptProblem>& scriptErrors() const { return problems; }
    const std::vector<GuiNode>& playingGui() const { return gui; }
    WorldSession* session() const { return live.get(); }
    const Terrain& activeTerrain() const { return playing && live ? live->terrain : world.terrain; }
    ScriptPerformance scriptPerformance() const { return playing && live ? live->engine->performance() : lastPerformance; }

    bool dirty() const { return !(world == savedWorld); }
    std::vector<WorldBlock> blocks() const;
    std::optional<WorldBlock> selectedPart() const;
    std::optional<InstanceRecord> selectedRecord() const;
    std::optional<std::size_t> index(std::string_view part) const;
    std::optional<std::size_t> recordIndex(std::string_view id) const;
    bool exists(std::string_view id) const { return world.contains(id); }
    std::vector<std::tuple<std::string, std::string, std::string>> virtualChildren() const;
    std::optional<std::string> className(std::string_view id) const;
    std::string displayName(std::string_view id) const;
    std::string path(std::string_view id) const;
    bool reveal(const OutputLine& line);
    bool isScript(std::string_view id) const;
    std::vector<std::string> children(std::string_view parent) const;
    std::vector<std::string> visibleScripts() const;

    Result<void> open(const Experience& project);
    Result<void> recover(const Experience& recovered);
    void close();
    void markSaved() { savedWorld = world; }
    void mark(const std::string& label, std::optional<std::string> coalesce = std::nullopt);
    void endGesture();
    bool isLocked(std::string_view id) const;
    bool canEdit(const std::optional<std::string>& part, StudioAction action = StudioAction::transform) const;
    std::vector<WorldBlock> partsIn(std::string_view service) const;
    std::vector<std::string> selectedIds() const;
    void extendSelection(const std::string& id);
    void select(const std::vector<std::string>& ids);
    void selectChildren();
    std::vector<std::string> roots(const std::vector<std::string>& ids) const;
    std::vector<std::string> expanded(const std::vector<std::string>& ids) const;
    std::vector<std::string> transformTargets() const;
    static std::optional<Bounds> bounds(const std::vector<WorldBlock>& blocks);
    std::optional<Vec3> pivot() const;
    std::optional<Vec3> anchor() const;
    void resize(const WorldBlock& origin, int axis, int sign, double delta, const std::string& gesture);
    void translate(const BlockSet& origins, Vec3 direction, double delta, bool worldAligned, const std::string& coalesce);
    void translate(const BlockSet& origins, Vec3 offset, const std::string& coalesce);
    bool accepts(std::string_view target, std::string_view childClass) const;
    bool canDrop(const std::vector<std::string>& ids, std::string_view target) const;
    bool reparent(const std::vector<std::string>& ids, const std::string& target);
    std::optional<std::string> dragTarget(Vec2 location) const;
    void dragExplorer(const std::vector<std::string>& ids, Vec2 location);
    bool dropExplorer();
    std::vector<const ClassDescriptor*> insertable(std::string_view target) const;
    std::string insertionTarget() const;
    std::optional<std::string> insert(const std::string& className, std::optional<std::string> target = std::nullopt);
    void copySelection();
    void cutSelection();
    std::vector<std::string> paste();
    std::vector<std::string> pasteInto();
    void group();
    void ungroup();
    void rename(const std::string& id, const std::string& value);
    void setProperty(const std::string& id, const std::string& property, const PropertyValue& value);
    void setSource(const std::string& id, const std::string& source);
    void checkSyntax(const std::string& id);
    void setAnchored(bool value);
    void setLocked(bool value);
    void dropToFloor();
    void configure(std::optional<std::string> title, std::optional<std::string> genre, std::optional<int> serverSize);
    void setLighting(const std::string& field, const std::function<void(WorldLighting&)>& change);
    bool can(StudioCommand command) const;
    void run(StudioCommand command);
    void adopt(const Experience& stored, const Experience& snapshot);
    int perform(const StudioEdit& edit);
    int beginStroke() { return ++strokes; }
    void endStroke() { endGesture(); }
    void undo();
    void redo();
    std::string freeName(const std::string& id, const std::string& wanted) const;
    bool addPart(const std::string& kind);
    Result<void> insertMesh(const StudioAsset& asset);
    Result<void> importMesh(const std::filesystem::path& file);
    Result<void> importContent(const std::string& id, const std::string& property, const std::filesystem::path& file);
    Result<void> setExperienceImage(ExperienceImage kind, const std::filesystem::path& file);
    void clearExperienceImage(ExperienceImage kind);
    std::optional<std::string> experienceImage(ExperienceImage kind) const;
    std::optional<std::filesystem::path> experienceImageFile(ExperienceImage kind) const;
    bool insertAsset(const StudioAsset& asset);
    void loadAssets();
    Result<Experience> publishable() const;
    void duplicateSelection();
    void deleteSelection();
    void selectAll();
    bool addScript(const std::string& parent) { return insert("Script", parent).has_value(); }
    void openScript(const std::string& id);
    void closeScript(const std::string& id);
    void update(const std::string& part, const std::function<void(WorldBlock&)>& change, std::optional<std::string> label = std::nullopt, std::optional<std::string> coalesce = std::nullopt,
                StudioAction action = StudioAction::transform);
    static double wrapDegrees(double value);
    static double clampHorizontal(double value);
    static double snapped(double value, double step = studio_limits::snap);
    void play();
    void stop();
    void tick(double time);
    void key(std::uint16_t code, bool down);
    std::vector<GuiNode> screens() const;
    int runCommand(const std::string& source);
    void log(const std::string& text, bool error);
    void clearOutput() { output.clear(); }

private:
    std::optional<StudioSelection> current;
    std::vector<std::string> parts;
    bool extending = false;
    int strokes = 0;
    std::optional<std::string> gestureToken;
    BlockSet gestureOrigins;
    Vec3 gesturePivot;
    std::vector<StudioRevision> undoStack;
    std::vector<StudioRevision> redoStack;
    std::optional<std::string> coalescing;
    std::optional<std::string> undoTitle;
    std::optional<std::string> redoTitle;
    std::map<std::string, ScriptProblem> problems;
    std::vector<GuiNode> gui;
    std::unique_ptr<WorldSession> live;

public:
    std::optional<std::string> playTeam() const { return live ? live->teamColor() : std::nullopt; }

private:
    std::optional<Json> playData;
    ScriptPerformance lastPerformance;
    std::optional<double> lastTick;
    int shownRevision = -1;
    std::optional<double> respawnAt;

    void notify();
    bool writable(std::string_view target) const;
    void setParent(const std::string& id, const std::string& parent);
    void revealTarget(const std::string& target);
    std::vector<std::string> clone(const StudioClipboard& source, const std::string& label, std::optional<std::string> destination, double offset);
    int transformGroup(const std::string& label, const std::optional<std::string>& gesture, const std::function<BlockSet(const BlockSet&, Vec3)>& compute);
    void placeBlock(WorldBlock& block) const;
    void restore(const StudioRevision& revision);
    void pruneTabs();
    void refreshHistoryLabels();
    void resetHistory();
    void emit(const OutputLine& line);
};

}
