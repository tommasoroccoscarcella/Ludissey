#include "StudioDocument.hpp"

#include "Engine/Assets/Mesh.hpp"
#include "Engine/Backend/Assets.hpp"
#include "Engine/Client/Models.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Core/Rules.hpp"
#include "Engine/Core/Text.hpp"
#include "Engine/DataModel/DataModel.hpp"
#include "Engine/Scripting/EngineTypes.hpp"
#include "Engine/Scripting/ScriptEngine.hpp"
#include "Lim/Compiler/Compiler.hpp"
#include "Localization/Localization.hpp"

#include <ImageIO/ImageIO.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <regex>

namespace ld {

namespace studio_catalog {

const std::vector<std::string>& primitives() {
    static const std::vector<std::string> list = {"cube", "sphere", "cylinder"};
    return list;
}

std::vector<std::string> contentExtensions(std::string_view property) {
    std::vector<std::string> out;
    if (property == "SoundId") for (auto extension : rules::assets::audioExtensions) out.emplace_back(extension);
    else for (auto extension : rules::assets::imageExtensions) out.emplace_back(extension);
    return out;
}

const std::vector<std::string>& meshExtensions() {
    static const std::vector<std::string> list = {"obj", "usdz", "usd", "usda", "usdc", "ply", "stl", "abc"};
    return list;
}

std::string materialTitle(std::string_view material) {
    if (material == "wood") return tr("Legno");
    if (material == "metal") return tr("Metallo");
    if (material == "neon") return tr("Neon");
    if (material == "glass") return tr("Vetro");
    return tr("Plastica");
}

std::string title(std::string_view kind) {
    if (kind == "model") return tr("Modello");
    if (kind == "sphere") return tr("Sfera");
    if (kind == "cylinder") return tr("Cilindro");
    if (kind == "mesh") return tr("Mesh");
    if (kind == "wedge") return tr("Cuneo");
    if (kind == "spawn") return tr("Punto di partenza");
    if (kind == "seat") return tr("Sedile");
    return tr("Cubo");
}

std::string symbol(std::string_view kind) {
    if (kind == "sphere") return "ld-icon-object-sphere";
    if (kind == "cylinder") return "ld-icon-object-cylinder";
    if (kind == "wedge") return "ld-icon-object-wedge";
    if (kind == "mesh") return "ld-icon-object-mesh";
    if (kind == "model") return "ld-icon-object-model";
    return "ld-icon-object-part";
}

}

namespace studio_assets {

std::filesystem::path previews() { return supportFolder() / "Anteprime"; }

std::filesystem::path preview(const std::string& project) { return previews() / (project + ".png"); }

std::filesystem::path legacyFolder() { return supportFolder() / "Modelli"; }

namespace {

std::filesystem::path indexFile() { return asset_cache::folder() / "indice.json"; }

}

std::map<std::string, std::string> names() {
    std::map<std::string, std::string> out;
    try {
        auto parsed = Json::parse(asset_cache::readFile(indexFile()));
        if (parsed && parsed->isObject())
            for (const auto& [key, value] : parsed->members())
                if (value.isString()) out[key] = value.string();
    } catch (const std::exception&) {
    }
    return out;
}

StudioAsset install(const std::filesystem::path& source) {
    std::string asset = asset_cache::adopt(source);
    auto known = names();
    std::string name = source.stem().string();
    if (!known.count(asset)) {
        known[asset] = name;
        Json index = Json::object();
        for (const auto& [key, value] : known) index.set(key, value);
        asset_cache::writeFile(indexFile(), index.dump());
    }
    std::string extension = source.extension().string();
    if (!extension.empty()) extension = lowercase(extension.substr(1));
    return {asset, known[asset], extension};
}

std::optional<std::string> adoptLegacy(const std::string& reference) {
    if (asset_cache::valid(reference)) return reference;
    auto legacy = legacyFolder() / reference;
    if (reference.find('/') != std::string::npos || !std::filesystem::exists(legacy)) return std::nullopt;
    try {
        return install(legacy).asset;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::vector<StudioAsset> catalog() {
    std::vector<StudioAsset> out;
    if (!std::filesystem::exists(asset_cache::folder())) return out;
    auto known = names();
    for (const auto& entry : std::filesystem::directory_iterator(asset_cache::folder())) {
        std::string file = entry.path().filename().string();
        std::string extension = entry.path().extension().string();
        if (!extension.empty()) extension = extension.substr(1);
        const auto& allowed = studio_catalog::meshExtensions();
        if (!asset_cache::valid(file) || std::find(allowed.begin(), allowed.end(), extension) == allowed.end()) continue;
        out.push_back({file, known.count(file) ? known[file] : file.substr(0, 12), extension});
    }
    std::sort(out.begin(), out.end(), [](const StudioAsset& a, const StudioAsset& b) { return localizedCompare(a.name, b.name) < 0; });
    return out;
}

}

std::string commandTitle(StudioCommand command) {
    switch (command) {
    case StudioCommand::undo: return tr("Annulla@@modifica");
    case StudioCommand::redo: return tr("Ripeti");
    case StudioCommand::copy: return tr("Copia");
    case StudioCommand::cut: return tr("Taglia");
    case StudioCommand::paste: return tr("Incolla");
    case StudioCommand::duplicate: return tr("Duplica");
    case StudioCommand::remove: return tr("Elimina");
    case StudioCommand::group: return tr("Raggruppa");
    case StudioCommand::ungroup: return tr("Separa");
    case StudioCommand::selectAll: return tr("Seleziona tutto");
    case StudioCommand::lock: return tr("Blocca");
    case StudioCommand::unlock: return tr("Sblocca");
    case StudioCommand::play: return tr("Gioca");
    case StudioCommand::stop: return tr("Ferma");
    case StudioCommand::playHere: return tr("Gioca da qui");
    case StudioCommand::run: return tr("Esegui");
    case StudioCommand::rotate: return tr("Ruota 90°");
    case StudioCommand::tilt: return tr("Inclina 90°");
    }
    return "";
}

std::string toolTitle(StudioTool tool) {
    switch (tool) {
    case StudioTool::select: return tr("Seleziona");
    case StudioTool::move: return tr("Sposta");
    case StudioTool::scale: return tr("Scala");
    case StudioTool::rotate: return tr("Ruota");
    }
    return "";
}

std::string_view toolKey(StudioTool tool) {
    switch (tool) {
    case StudioTool::select: return "select";
    case StudioTool::move: return "move";
    case StudioTool::scale: return "scale";
    case StudioTool::rotate: return "rotate";
    }
    return "";
}

std::string_view toolSymbol(StudioTool tool) {
    switch (tool) {
    case StudioTool::select: return "ld-icon-studio-select";
    case StudioTool::move: return "ld-icon-studio-move";
    case StudioTool::scale: return "ld-icon-studio-scale";
    case StudioTool::rotate: return "ld-icon-studio-rotate";
    }
    return "";
}

StudioClipboard& StudioDocument::clipboard() {
    static StudioClipboard shared;
    return shared;
}

StudioDocument::StudioDocument(const ClassRegistry& classes) : registry(classes) {
    meshAvailable = [](const std::string& asset) {
        auto file = asset_cache::verified(asset);
        return file && mesh_import::cached(*file) != nullptr;
    };
}

StudioDocument::~StudioDocument() {
    if (live) live->stop();
}

void StudioDocument::notify() {
    if (changed) changed();
}

void StudioDocument::setSelection(std::optional<StudioSelection> value) {
    current = std::move(value);
    if (!extending) {
        parts.clear();
        if (current && !current->service) parts.push_back(current->id);
    }
}

std::vector<WorldBlock> StudioDocument::blocks() const { return playing && live ? live->parts() : world.workspaceParts(); }

std::optional<WorldBlock> StudioDocument::selectedPart() const {
    if (!current || current->service) return std::nullopt;
    if (const WorldBlock* b = world.block(current->id)) return *b;
    return std::nullopt;
}

std::optional<InstanceRecord> StudioDocument::selectedRecord() const {
    if (!current || current->service) return std::nullopt;
    if (const InstanceRecord* r = world.record(current->id)) return *r;
    return std::nullopt;
}

std::optional<std::size_t> StudioDocument::index(std::string_view part) const {
    for (std::size_t i = 0; i < world.blocks.size(); ++i)
        if (world.blocks[i].id == part) return i;
    return std::nullopt;
}

std::optional<std::size_t> StudioDocument::recordIndex(std::string_view id) const {
    for (std::size_t i = 0; i < world.instances.size(); ++i)
        if (world.instances[i].id == id) return i;
    return std::nullopt;
}

std::vector<std::tuple<std::string, std::string, std::string>> StudioDocument::virtualChildren() const {
    std::vector<std::tuple<std::string, std::string, std::string>> out;
    for (const auto& [className, parent] : registry.defaultChildren()) out.emplace_back(parent + "." + className, className, parent);
    return out;
}

std::optional<std::string> StudioDocument::className(std::string_view id) const {
    if (const WorldBlock* b = world.block(id)) return b->className();
    if (const InstanceRecord* r = world.record(id)) return r->className;
    if (registry.isService(id)) return std::string(id);
    for (const auto& [vid, className, parent] : virtualChildren())
        if (vid == id) return className;
    return std::nullopt;
}

std::string StudioDocument::displayName(std::string_view id) const {
    if (const WorldBlock* b = world.block(id)) return b->displayName();
    if (const InstanceRecord* r = world.record(id)) return r->name;
    for (const auto& [vid, className, parent] : virtualChildren())
        if (vid == id) return className;
    return std::string(id);
}

std::string StudioDocument::path(std::string_view id) const {
    std::vector<std::string> names{displayName(id)};
    auto node = world.parentOf(id);
    std::size_t steps = 0;
    while (node && steps <= world.blocks.size() + world.instances.size()) {
        names.insert(names.begin(), displayName(*node));
        node = world.parentOf(*node);
        ++steps;
    }
    for (const auto& [vid, className, parent] : virtualChildren())
        if (vid == names.front()) names.front() = parent + "." + className;
    return join(names, ".");
}

bool StudioDocument::reveal(const OutputLine& line) {
    static const std::regex pattern("^([^:]+):(\\d+):");
    std::smatch match;
    if (!std::regex_search(line.text, match, pattern)) return false;
    std::string wanted = match[1];
    for (const auto& record : world.instances) {
        if (!isScript(record.id) || path(record.id) != wanted) continue;
        openScript(record.id);
        setSelection(StudioSelection::instance(record.id));
        focusLine = std::stoi(match[2]);
        notify();
        return true;
    }
    return false;
}

bool StudioDocument::isScript(std::string_view id) const {
    auto c = className(id);
    return c && registry.isA(*c, "LuaSourceContainer");
}

std::vector<std::string> StudioDocument::children(std::string_view parent) const {
    std::vector<std::string> out;
    for (const auto& [vid, className, owner] : virtualChildren())
        if (owner == parent) out.push_back(vid);
    auto real = world.childIds(parent);
    out.insert(out.end(), real.begin(), real.end());
    return out;
}

std::vector<std::string> StudioDocument::visibleScripts() const {
    std::vector<std::string> out;
    for (const auto& id : openScripts)
        if (recordIndex(id) && isScript(id)) out.push_back(id);
    return out;
}

Result<void> StudioDocument::open(const Experience& project) {
    Log::info("Studio", "apertura progetto, " + std::to_string(project.blocks.size()) + " parti, " + std::to_string(project.instances.size()) + " istanze");
    if (auto problem = project.compatibility()) return Error::make(ErrorCode::conflict, *problem);
    stop();
    playData.reset();
    lastPerformance = ScriptPerformance{};
    breakpoints.clear();
    world = project;
    for (auto& block : world.blocks)
        if (block.kind == "mesh" && block.meshAsset)
            if (auto adopted = studio_assets::adoptLegacy(*block.meshAsset)) block.meshAsset = adopted;
    savedWorld = world;
    auto legacy = world.legacyScripts();
    world.schemaVersion = rules::schemaVersion;
    opened = true;
    openScripts.clear();
    activeScript.reset();
    output.clear();
    expandedIds.clear();
    problems.clear();
    setSelection(world.blocks.empty() ? std::nullopt : std::optional<StudioSelection>(StudioSelection::instance(world.blocks.front().id)));
    resetHistory();
    log(tr("Progetto aperto: {0}", project.title), false);
    for (const auto& id : legacy)
        if (const auto* record = world.record(id)) log(tr("«{0}» è scritto per una versione vecchia ed è stato disattivato. Riscrivilo in Lim per riattivarlo.", displayName(record->id)), true);
    for (const auto& record : world.instances)
        if (isScript(record.id)) checkSyntax(record.id);
    return {};
}

Result<void> StudioDocument::recover(const Experience& recovered) {
    if (!opened || recovered.id != world.id) return Error::make(ErrorCode::conflict, tr("Questa copia di recupero è di un altro progetto."));
    if (playing) return Error::make(ErrorCode::conflict, tr("Ferma la prova prima di recuperare"));
    if (auto problem = recovered.compatibility()) return Error::make(ErrorCode::conflict, *problem);
    world = recovered;
    world.schemaVersion = rules::schemaVersion;
    openScripts.clear();
    activeScript.reset();
    setSelection(std::nullopt);
    resetHistory();
    for (const auto& record : world.instances)
        if (isScript(record.id)) checkSyntax(record.id);
    log(tr("Modifiche recuperate. Salva per tenerle."), false);
    notify();
    return {};
}

void StudioDocument::close() {
    stop();
    settingsOpen = false;
    breakpoints.clear();
    opened = false;
    resetHistory();
    world = Experience();
    savedWorld = world;
    setSelection(std::nullopt);
    openScripts.clear();
    activeScript.reset();
    output.clear();
    notify();
}

void StudioDocument::mark(const std::string& label, std::optional<std::string> coalesce) {
    if (!opened || playing) return;
    if (coalesce && coalesce == coalescing) return;
    coalescing = coalesce;
    undoStack.push_back({label, world, current, parts});
    if (undoStack.size() > static_cast<std::size_t>(history)) undoStack.erase(undoStack.begin(), undoStack.begin() + static_cast<std::ptrdiff_t>(undoStack.size() - history));
    redoStack.clear();
    refreshHistoryLabels();
}

void StudioDocument::endGesture() {
    coalescing.reset();
    gestureToken.reset();
}

bool StudioDocument::isLocked(std::string_view id) const {
    std::optional<std::string> node = std::string(id);
    std::size_t steps = 0;
    while (node && steps <= world.blocks.size() + world.instances.size()) {
        if (const WorldBlock* b = world.block(*node); b && b->locked) return true;
        node = world.parentOf(*node);
        ++steps;
    }
    return false;
}

bool StudioDocument::canEdit(const std::optional<std::string>& part, StudioAction action) const {
    if (playing || !part || !exists(*part)) return false;
    if (action == StudioAction::unlock) return true;
    return !isLocked(*part);
}

std::vector<WorldBlock> StudioDocument::partsIn(std::string_view service) const {
    std::vector<WorldBlock> out;
    for (const auto& b : world.blocks)
        if (b.parent == service) out.push_back(b);
    return out;
}

std::vector<std::string> StudioDocument::selectedIds() const {
    std::vector<std::string> out;
    for (const auto& id : parts)
        if (exists(id)) out.push_back(id);
    return out;
}

void StudioDocument::extendSelection(const std::string& id) {
    if (!exists(id)) return;
    extending = true;
    if (auto position = std::find(parts.begin(), parts.end(), id); position != parts.end()) {
        parts.erase(position);
        setSelection(parts.empty() ? std::nullopt : std::optional<StudioSelection>(StudioSelection::instance(parts.back())));
    } else {
        parts.push_back(id);
        setSelection(StudioSelection::instance(id));
    }
    extending = false;
    activeScript.reset();
    notify();
}

void StudioDocument::select(const std::vector<std::string>& ids) {
    std::vector<std::string> valid;
    for (const auto& id : ids)
        if (exists(id)) valid.push_back(id);
    extending = true;
    parts = valid;
    setSelection(valid.empty() ? std::nullopt : std::optional<StudioSelection>(StudioSelection::instance(valid.back())));
    extending = false;
    notify();
}

void StudioDocument::selectChildren() {
    if (!current) return;
    std::string id = current->id;
    auto found = world.childIds(id);
    if (found.empty()) return;
    select(found);
    if (registry.isService(id)) expandedServices.insert(id);
    else expandedIds.insert(id);
}

std::vector<std::string> StudioDocument::roots(const std::vector<std::string>& ids) const {
    std::set<std::string> chosen(ids.begin(), ids.end());
    std::vector<std::string> out;
    for (const auto& id : ids) {
        auto parent = world.parentOf(id);
        std::size_t steps = 0;
        bool nested = false;
        while (parent && steps <= world.blocks.size() + world.instances.size()) {
            if (chosen.count(*parent)) {
                nested = true;
                break;
            }
            parent = world.parentOf(*parent);
            ++steps;
        }
        if (!nested) out.push_back(id);
    }
    return out;
}

std::vector<std::string> StudioDocument::expanded(const std::vector<std::string>& ids) const {
    std::set<std::string> result;
    for (const auto& id : roots(ids)) {
        result.insert(id);
        for (const auto& d : world.descendants(id)) result.insert(d);
    }
    std::vector<std::string> out;
    for (const auto& b : world.blocks)
        if (result.count(b.id)) out.push_back(b.id);
    for (const auto& r : world.instances)
        if (result.count(r.id)) out.push_back(r.id);
    return out;
}

std::vector<std::string> StudioDocument::transformTargets() const {
    std::vector<std::string> out;
    for (const auto& id : expanded(selectedIds())) {
        const WorldBlock* b = world.block(id);
        if (b && b->kind != "model" && !isLocked(id)) out.push_back(id);
    }
    return out;
}

std::optional<Bounds> StudioDocument::bounds(const std::vector<WorldBlock>& list) {
    if (list.empty()) return std::nullopt;
    double big = std::numeric_limits<double>::max();
    Bounds box{{big, big, big}, {-big, -big, -big}};
    for (const auto& b : list) {
        Vec3 half = b.halfExtents();
        box.low = {std::min(box.low.x, b.x - half.x), std::min(box.low.y, b.y - half.y), std::min(box.low.z, b.z - half.z)};
        box.high = {std::max(box.high.x, b.x + half.x), std::max(box.high.y, b.y + half.y), std::max(box.high.z, b.z + half.z)};
    }
    return box;
}

std::optional<Vec3> StudioDocument::pivot() const {
    std::vector<WorldBlock> list;
    for (const auto& id : transformTargets())
        if (const WorldBlock* b = world.block(id)) list.push_back(*b);
    auto box = bounds(list);
    if (!box) return std::nullopt;
    return Vec3{(box->low.x + box->high.x) / 2, (box->low.y + box->high.y) / 2, (box->low.z + box->high.z) / 2};
}

std::optional<Vec3> StudioDocument::anchor() const {
    std::vector<const WorldBlock*> list;
    for (const auto& id : transformTargets())
        if (const WorldBlock* b = world.block(id)) list.push_back(b);
    if (list.empty()) return std::nullopt;
    double count = static_cast<double>(list.size()), x = 0, y = 0, z = 0;
    for (const auto* b : list) x += b->x;
    for (const auto* b : list) y += b->y;
    for (const auto* b : list) z += b->z;
    return Vec3{x / count, y / count, z / count};
}

int StudioDocument::transformGroup(const std::string& label, const std::optional<std::string>& gesture, const std::function<BlockSet(const BlockSet&, Vec3)>& compute) {
    auto targets = transformTargets();
    if (targets.empty()) return 0;
    if (!gesture || gesture != gestureToken) {
        gestureOrigins.clear();
        for (const auto& id : targets)
            if (const WorldBlock* b = world.block(id)) gestureOrigins[id] = *b;
        gesturePivot = pivot().value_or(Vec3{});
        gestureToken = gesture;
    }
    mark(label, gesture);
    BlockSet results = compute(gestureOrigins, gesturePivot);
    for (const auto& [id, block] : results) {
        WorldBlock target = block;
        update(id, [&](WorldBlock& part) {
            part.x = target.x;
            part.y = target.y;
            part.z = target.z;
            part.rotationX = target.rotationX;
            part.rotationY = target.rotationY;
            part.rotationZ = target.rotationZ;
            part.sizeX = target.sizeX;
            part.sizeY = target.sizeY;
            part.sizeZ = target.sizeZ;
        });
    }
    if (viewport) viewport->refresh();
    return static_cast<int>(results.size());
}

void StudioDocument::resize(const WorldBlock& origin, int axis, int sign, double delta, const std::string& gesture) {
    if (!canEdit(origin.id)) return;
    WorldBlock target = transform_math::resized(origin, axis, sign, delta, moveSnap);
    update(origin.id, [&](WorldBlock& block) {
        block.x = target.x;
        block.y = target.y;
        block.z = target.z;
        block.sizeX = target.sizeX;
        block.sizeY = target.sizeY;
        block.sizeZ = target.sizeZ;
    }, tr("Scala"), gesture);
}

void StudioDocument::translate(const BlockSet& origins, Vec3 direction, double delta, bool worldAligned, const std::string& coalesce) {
    BlockSet editable;
    for (const auto& [id, b] : origins)
        if (canEdit(id)) editable[id] = b;
    if (editable.empty()) return;
    mark(editable.size() > 1 ? tr("Sposta selezione") : tr("Sposta"), coalesce);
    for (const auto& [id, block] : transform_math::moved(editable, direction, delta, moveSnap, worldAligned)) {
        WorldBlock target = block;
        update(id, [&](WorldBlock& part) {
            part.x = target.x;
            part.y = target.y;
            part.z = target.z;
        });
    }
}

void StudioDocument::translate(const BlockSet& origins, Vec3 offset, const std::string& coalesce) {
    BlockSet editable;
    for (const auto& [id, b] : origins)
        if (canEdit(id)) editable[id] = b;
    if (editable.empty()) return;
    mark(editable.size() > 1 ? tr("Sposta selezione") : tr("Sposta"), coalesce);
    for (const auto& [id, origin] : editable) {
        WorldBlock start = origin;
        update(id, [&](WorldBlock& block) {
            block.x = start.x + offset.x;
            block.y = start.y + offset.y;
            block.z = start.z + offset.z;
        });
    }
}

bool StudioDocument::accepts(std::string_view target, std::string_view childClass) const {
    auto parentClass = className(target);
    return parentClass && registry.canParent(childClass, *parentClass);
}

bool StudioDocument::writable(std::string_view target) const {
    if (registry.isService(target)) return true;
    for (const auto& [vid, className, parent] : virtualChildren())
        if (vid == target) return true;
    return canEdit(std::string(target), StudioAction::structure);
}

bool StudioDocument::canDrop(const std::vector<std::string>& ids, std::string_view target) const {
    std::vector<std::string> present;
    for (const auto& id : ids)
        if (exists(id)) present.push_back(id);
    auto movers = roots(present);
    if (playing || movers.empty() || !writable(target)) return false;
    for (const auto& id : movers)
        if (!canEdit(id, StudioAction::structure)) return false;
    std::set<std::string> forbidden(movers.begin(), movers.end());
    for (const auto& id : movers)
        for (const auto& d : world.descendants(id)) forbidden.insert(d);
    if (forbidden.count(std::string(target))) return false;
    for (const auto& id : movers) {
        auto c = className(id);
        if (!c || !accepts(target, *c)) return false;
    }
    return true;
}

void StudioDocument::setParent(const std::string& id, const std::string& parent) {
    if (auto i = index(id)) world.blocks[*i].parent = parent;
    if (auto i = recordIndex(id)) world.instances[*i].parent = parent;
}

void StudioDocument::revealTarget(const std::string& target) {
    if (registry.isService(target)) expandedServices.insert(target);
    else expandedIds.insert(target);
}

bool StudioDocument::reparent(const std::vector<std::string>& ids, const std::string& target) {
    std::vector<std::string> present;
    for (const auto& id : ids)
        if (exists(id)) present.push_back(id);
    auto movers = roots(present);
    if (!canDrop(ids, target)) return false;
    bool moves = std::any_of(movers.begin(), movers.end(), [&](const std::string& id) { return world.parentOf(id) != std::optional<std::string>(target); });
    if (!moves) return false;
    mark(tr("Sposta in {0}", displayName(target)));
    for (const auto& id : movers) setParent(id, target);
    revealTarget(target);
    notify();
    return true;
}

std::optional<std::string> StudioDocument::dragTarget(Vec2 location) const {
    std::optional<std::string> best;
    double height = std::numeric_limits<double>::max();
    for (const auto& [id, frame] : explorerFrames) {
        if (location.x < frame.minX() || location.x > frame.maxX() || location.y < frame.minY() || location.y > frame.maxY()) continue;
        if (frame.height() < height) {
            height = frame.height();
            best = id;
        }
    }
    return best;
}

void StudioDocument::dragExplorer(const std::vector<std::string>& ids, Vec2 location) {
    auto target = dragTarget(location);
    explorerDrag = StudioExplorerDrag{ids, location, target, target ? canDrop(ids, *target) : false};
    notify();
}

bool StudioDocument::dropExplorer() {
    auto drag = explorerDrag;
    explorerDrag.reset();
    if (!drag || !drag->target || !drag->valid) {
        if (drag && drag->target) {
            std::vector<std::string> names;
            for (const auto& id : roots(drag->ids))
                if (auto c = className(id)) names.push_back(*c);
            log(tr("{0} non può essere inserito in {1}", names.empty() ? tr("Oggetto") : names.front(), className(*drag->target).value_or(*drag->target)), true);
        }
        notify();
        return false;
    }
    return reparent(drag->ids, *drag->target);
}

std::vector<const ClassDescriptor*> StudioDocument::insertable(std::string_view target) const {
    auto parentClass = className(target);
    if (!parentClass) return {};
    std::string query = lowercase(trim(insertQuery));
    std::vector<const ClassDescriptor*> out;
    for (const auto* d : registry.insertable(*parentClass))
        if (query.empty() || lowercase(d->className).find(query) != std::string::npos) out.push_back(d);
    return out;
}

std::string StudioDocument::insertionTarget() const {
    if (current && className(current->id)) return current->id;
    return "Workspace";
}

void StudioDocument::placeBlock(WorldBlock& block) const {
    int count = static_cast<int>(world.blocks.size());
    block.x = clampHorizontal(static_cast<double>((count % 7) - 3) * 2);
    block.z = clampHorizontal(static_cast<double>(((count / 7) % 7) - 3) * 2);
    block.y = block.sizeY / 2;
}

std::optional<std::string> StudioDocument::insert(const std::string& type, std::optional<std::string> target) {
    std::string parent = target.value_or(insertionTarget());
    const ClassDescriptor* descriptor = registry.find(type);
    if (!opened || playing || !descriptor || !descriptor->canCreate()) return std::nullopt;
    if (!accepts(parent, type) || !writable(parent)) {
        log(tr("{0} non può essere inserito in {1}", type, className(parent).value_or(parent)), true);
        return std::nullopt;
    }
    if (descriptor->kind) {
        if (static_cast<int>(world.blocks.size()) >= rules::world::parts) {
            log(tr("Hai raggiunto il limite di {0} parti.", rules::world::parts), true);
            return std::nullopt;
        }
    } else if (static_cast<int>(world.instances.size()) >= rules::instances::maximum) {
        log(tr("Hai raggiunto il numero massimo di istanze del progetto."), true);
        return std::nullopt;
    }
    mark(tr("Inserisci {0}", type));
    std::string id;
    if (descriptor->kind) {
        WorldBlock block;
        block.kind = *descriptor->kind;
        block.name = studio_catalog::title(block.kind);
        if (block.kind == "cube" || block.kind == "wedge" || block.kind == "spawn" || block.kind == "seat") {
            block.sizeX = 4;
            block.sizeY = 1.2;
            block.sizeZ = 2;
        }
        placeBlock(block);
        block.parent = parent;
        world.blocks.push_back(block);
        id = block.id;
    } else {
        InstanceRecord record{makeUuid(), type, type, parent, registry.defaults(type)};
        world.instances.push_back(record);
        id = record.id;
    }
    revealTarget(parent);
    setSelection(StudioSelection::instance(id));
    activeScript.reset();
    if (registry.isA(type, "LuaSourceContainer")) openScript(id);
    if (viewport) viewport->refresh();
    notify();
    return id;
}

void StudioDocument::copySelection() {
    auto chosen = expanded(selectedIds());
    std::set<std::string> ids(chosen.begin(), chosen.end());
    if (ids.empty()) return;
    StudioClipboard board;
    for (const auto& b : world.blocks)
        if (ids.count(b.id)) board.blocks.push_back(b);
    for (const auto& r : world.instances)
        if (ids.count(r.id)) board.instances.push_back(r);
    clipboard() = board;
    log(tr("Copiati {0} oggetti", static_cast<long long>(roots(selectedIds()).size())), false);
}

void StudioDocument::cutSelection() {
    auto chosen = roots(selectedIds());
    bool allowed = !selectedIds().empty() && std::all_of(chosen.begin(), chosen.end(), [&](const std::string& id) { return canEdit(id, StudioAction::structure); });
    if (!allowed) {
        log(tr("Oggetto bloccato: sbloccalo per tagliarlo."), true);
        return;
    }
    copySelection();
    deleteSelection();
}

std::vector<std::string> StudioDocument::clone(const StudioClipboard& source, const std::string& label, std::optional<std::string> destination, double offset) {
    if (playing || source.empty()) return {};
    if (static_cast<int>(world.blocks.size() + source.blocks.size()) > rules::world::parts || static_cast<int>(world.instances.size() + source.instances.size()) > rules::instances::maximum) {
        log(tr("Hai raggiunto il limite di {0} oggetti.", rules::instances::maximum), true);
        return {};
    }
    std::map<std::string, std::string> mapping;
    for (const auto& b : source.blocks) mapping[b.id] = makeUuid();
    for (const auto& r : source.instances) mapping[r.id] = makeUuid();
    auto target = [&](const std::string& parent, const std::string& childClass) -> std::optional<std::string> {
        if (auto mapped = mapping.find(parent); mapped != mapping.end()) return mapped->second;
        std::string wanted = destination.value_or(className(parent) ? parent : std::string("Workspace"));
        if (accepts(wanted, childClass) && writable(wanted)) return wanted;
        if (accepts("Workspace", childClass)) return std::string("Workspace");
        return std::nullopt;
    };
    std::vector<WorldBlock> blocksOut;
    std::vector<InstanceRecord> recordsOut;
    std::vector<std::string> created;
    for (const auto& block : source.blocks) {
        auto parent = target(block.parent, block.className());
        if (!parent) continue;
        WorldBlock copy = block;
        copy.id = mapping[block.id];
        copy.parent = *parent;
        bool top = !mapping.count(block.parent);
        if (top && copy.kind != "model") copy.x = clampHorizontal(copy.x + offset * copy.size());
        blocksOut.push_back(copy);
        if (top) created.push_back(copy.id);
    }
    for (const auto& record : source.instances) {
        auto parent = target(record.parent, record.className);
        if (!parent) continue;
        InstanceRecord copy = record;
        copy.id = mapping[record.id];
        copy.parent = *parent;
        recordsOut.push_back(copy);
        if (!mapping.count(record.parent)) created.push_back(copy.id);
    }
    if (created.empty()) {
        log(tr("Nessun oggetto può essere incollato in {0}", displayName(destination.value_or("Workspace"))), true);
        return {};
    }
    mark(label);
    world.blocks.insert(world.blocks.end(), blocksOut.begin(), blocksOut.end());
    world.instances.insert(world.instances.end(), recordsOut.begin(), recordsOut.end());
    if (destination) revealTarget(*destination);
    select(created);
    activeScript.reset();
    return created;
}

std::vector<std::string> StudioDocument::paste() { return clone(clipboard(), tr("Incolla"), std::nullopt, 0); }

std::vector<std::string> StudioDocument::pasteInto() {
    if (!current) return paste();
    return clone(clipboard(), tr("Incolla in {0}", displayName(current->id)), current->id, 0);
}

void StudioDocument::group() {
    auto chosen = roots(selectedIds());
    if (playing || chosen.empty() || static_cast<int>(world.blocks.size()) >= rules::world::parts) return;
    for (const auto& id : chosen)
        if (!canEdit(id, StudioAction::structure)) return;
    std::set<std::string> parents;
    for (const auto& id : chosen)
        if (auto p = world.parentOf(id)) parents.insert(*p);
    std::string parent = parents.size() == 1 && accepts(*parents.begin(), "Model") ? *parents.begin() : "Workspace";
    for (const auto& id : chosen) {
        auto c = className(id);
        if (!c || !registry.canParent(*c, "Model")) {
            log(tr("In un modello puoi raggruppare solo parti, modelli, script e valori."), true);
            return;
        }
    }
    mark(tr("Raggruppa"));
    WorldBlock model;
    model.kind = "model";
    model.name = studio_catalog::title("model");
    model.parent = parent;
    world.blocks.push_back(model);
    for (const auto& id : chosen) setParent(id, model.id);
    expandedIds.insert(model.id);
    setSelection(StudioSelection::instance(model.id));
    notify();
}

void StudioDocument::ungroup() {
    if (playing || !current || current->service) return;
    std::string id = current->id;
    const WorldBlock* model = world.block(id);
    if (!model || model->kind != "model" || !canEdit(id, StudioAction::structure)) return;
    std::string parent = model->parent;
    mark(tr("Separa"));
    auto kids = world.childIds(id);
    for (const auto& child : kids) setParent(child, parent);
    world.blocks.erase(std::remove_if(world.blocks.begin(), world.blocks.end(), [&](const WorldBlock& b) { return b.id == id; }), world.blocks.end());
    select(kids);
}

void StudioDocument::rename(const std::string& id, const std::string& value) {
    std::string name = utf8Prefix(value, rules::instances::nameCharacters);
    if (index(id)) {
        std::string short_ = utf8Prefix(name, rules::world::nameCharacters);
        if (!short_.empty()) short_ = freeName(id, short_);
        update(id, [&](WorldBlock& b) { b.name = short_; }, tr("Rinomina"), std::nullopt, StudioAction::rename);
        notify();
        return;
    }
    auto position = recordIndex(id);
    if (!canEdit(id, StudioAction::rename) || !position || name.empty() || world.instances[*position].name == name) return;
    name = freeName(id, name);
    if (world.instances[*position].name == name) return;
    mark(tr("Rinomina"));
    world.instances[*position].name = name;
    notify();
}

void StudioDocument::setProperty(const std::string& id, const std::string& property, const PropertyValue& value) {
    auto position = recordIndex(id);
    if (!canEdit(id, StudioAction::behavior) || !position) return;
    auto existing = world.instances[*position].properties.find(property);
    if (existing != world.instances[*position].properties.end() && existing->second == value) return;
    mark(property, "property." + id + "." + property);
    world.instances[*position].properties[property] = value;
    notify();
}

void StudioDocument::setSource(const std::string& id, const std::string& source) {
    auto position = recordIndex(id);
    if (playing || !position || world.instances[*position].source() == source) return;
    mark(tr("Modifica script"), "source." + id);
    world.instances[*position].setSource(utf8Prefix(source, rules::instances::sourceCharacters));
    checkSyntax(id);
    notify();
}

void StudioDocument::checkSyntax(const std::string& id) {
    const InstanceRecord* record = world.record(id);
    if (!record) {
        problems.erase(id);
        return;
    }
    lim::AnalyzeOptions options;
    options.context = record->className == "LocalScript" ? lim::ExecutionContext::client : record->className == "ModuleScript" ? lim::ExecutionContext::shared : lim::ExecutionContext::server;
    options.file = record->name;
    options.extraGlobals.push_back({"script", lim::Type::host(record->className)});
    options.resolveModule = [this](const std::string& name) -> std::optional<lim::ModuleInterface> {
        for (const auto& r : world.instances)
            if (r.className == "ModuleScript" && r.name == name) return lim::ModuleInterface{name, {}, {}, {}};
        return std::nullopt;
    };
    auto result = lim::compileSource(record->source(), record->name, EngineTypes::shared().registry(), options);
    for (const auto& d : result.diagnostics)
        if (d.severity == lim::Severity::error) {
            problems[id] = ScriptProblem{static_cast<int>(d.range.start.line), d.message};
            return;
        }
    problems.erase(id);
}

void StudioDocument::setAnchored(bool value) {
    if (!current || current->service) return;
    update(current->id, [&](WorldBlock& b) { b.anchored = value; }, value ? tr("Ancorato") : tr("Non ancorato"));
    notify();
}

void StudioDocument::setLocked(bool value) {
    std::vector<std::string> ids;
    for (const auto& id : selectedIds())
        if (index(id)) ids.push_back(id);
    if (playing || ids.empty()) return;
    mark(value ? tr("Blocca") : tr("Sblocca"));
    for (const auto& id : ids)
        if (auto i = index(id)) world.blocks[*i].locked = value;
    notify();
}

void StudioDocument::dropToFloor() {
    auto block = selectedPart();
    if (!block || !canEdit(block->id)) return;
    Vec3 half = block->halfExtents();
    double floor = -std::numeric_limits<double>::max();
    for (const auto& other : world.blocks) {
        if (other.id == block->id || !other.canCollide) continue;
        Vec3 reach = other.halfExtents();
        if (!(std::fabs(block->x - other.x) < half.x + reach.x && std::fabs(block->z - other.z) < half.z + reach.z)) continue;
        double top = other.y + reach.y;
        if (top <= block->y - half.y + 0.001) floor = std::max(floor, top);
    }
    if (floor <= -std::numeric_limits<double>::max()) {
        log(tr("Nessuna superficie sotto «{0}»", block->displayName()), true);
        return;
    }
    update(block->id, [&](WorldBlock& b) { b.y = floor + half.y; }, tr("Appoggia al suolo"));
    notify();
}

void StudioDocument::configure(std::optional<std::string> title, std::optional<std::string> genre, std::optional<int> serverSize) {
    if (!opened || playing) return;
    Experience next = world;
    if (title) next.title = utf8Prefix(*title, rules::world::titleCharacters);
    if (genre && std::find(rules::genres.begin(), rules::genres.end(), *genre) != rules::genres.end()) next.genre = *genre;
    if (serverSize) next.serverSize = std::min(rules::serverSize::maximum, std::max(rules::serverSize::minimum, *serverSize));
    std::string field = next.title != world.title ? "title" : next.genre != world.genre ? "genre" : next.serverSize != world.serverSize ? "serverSize" : "";
    if (field.empty()) return;
    mark(tr("Impostazioni esperienza"), "settings." + field);
    world.title = next.title;
    world.genre = next.genre;
    world.serverSize = next.serverSize;
    notify();
}

void StudioDocument::setLighting(const std::string& field, const std::function<void(WorldLighting&)>& change) {
    if (!opened || playing) return;
    WorldLighting next = world.lighting;
    change(next);
    next.clockTime = std::min<double>(rules::lighting::clockTime::maximum, std::max<double>(rules::lighting::clockTime::minimum, next.clockTime));
    next.brightness = std::min<double>(rules::lighting::brightness::maximum, std::max<double>(rules::lighting::brightness::minimum, next.brightness));
    next.fogEnd = std::min<double>(rules::lighting::fogEnd::maximum, std::max<double>(rules::lighting::fogEnd::minimum, next.fogEnd));
    if (next == world.lighting) return;
    mark(tr("Illuminazione · {0}", field), "lighting." + field);
    world.lighting = next;
    if (viewport) viewport->refresh();
    notify();
}

bool StudioDocument::can(StudioCommand command) const {
    if (!opened) return false;
    auto ids = selectedIds();
    bool editable = !playing && !ids.empty();
    auto structural = [&] {
        auto r = roots(ids);
        return std::all_of(r.begin(), r.end(), [&](const std::string& id) { return canEdit(id, StudioAction::structure); });
    };
    switch (command) {
    case StudioCommand::undo: return !playing && undoTitle.has_value();
    case StudioCommand::redo: return !playing && redoTitle.has_value();
    case StudioCommand::copy: return editable;
    case StudioCommand::cut:
    case StudioCommand::duplicate:
    case StudioCommand::remove:
    case StudioCommand::group: return editable && structural();
    case StudioCommand::paste: return !playing && !clipboard().empty();
    case StudioCommand::ungroup: {
        auto part = selectedPart();
        return editable && part && part->kind == "model";
    }
    case StudioCommand::selectAll: return !playing;
    case StudioCommand::lock:
    case StudioCommand::unlock: {
        bool wanted = command == StudioCommand::unlock;
        return editable && std::any_of(ids.begin(), ids.end(), [&](const std::string& id) {
                   const WorldBlock* b = world.block(id);
                   return b && b->locked == wanted;
               });
    }
    case StudioCommand::play:
    case StudioCommand::playHere:
    case StudioCommand::run: return !playing;
    case StudioCommand::stop: return playing;
    case StudioCommand::rotate:
    case StudioCommand::tilt: return !playing && !transformTargets().empty();
    }
    return false;
}

void StudioDocument::run(StudioCommand command) {
    if (!can(command)) return;
    switch (command) {
    case StudioCommand::undo: undo(); break;
    case StudioCommand::redo: redo(); break;
    case StudioCommand::copy: copySelection(); break;
    case StudioCommand::cut: cutSelection(); break;
    case StudioCommand::paste: paste(); break;
    case StudioCommand::duplicate: duplicateSelection(); break;
    case StudioCommand::remove: deleteSelection(); break;
    case StudioCommand::group: group(); break;
    case StudioCommand::ungroup: ungroup(); break;
    case StudioCommand::selectAll: selectAll(); break;
    case StudioCommand::lock: setLocked(true); break;
    case StudioCommand::unlock: setLocked(false); break;
    case StudioCommand::play:
        playSpawn.reset();
        playMode = StudioPlayMode::character;
        play();
        break;
    case StudioCommand::playHere:
        playSpawn = viewport ? viewport->focus() : std::nullopt;
        playMode = StudioPlayMode::character;
        play();
        break;
    case StudioCommand::run:
        playSpawn.reset();
        playMode = StudioPlayMode::camera;
        play();
        break;
    case StudioCommand::stop: stop(); break;
    case StudioCommand::rotate: perform(StudioEdit::rotateGroup(1, 90, std::nullopt)); break;
    case StudioCommand::tilt: perform(StudioEdit::rotateGroup(0, 90, std::nullopt)); break;
    }
    if (viewport) viewport->refresh();
    notify();
}

void StudioDocument::adopt(const Experience& stored, const Experience& snapshot) {
    auto stamp = [&](Experience& target) {
        target.owner = stored.owner;
        target.author = stored.author;
        target.createdAt = stored.createdAt;
        target.updatedAt = stored.updatedAt;
        target.revision = stored.revision;
    };
    Experience saved = snapshot;
    stamp(saved);
    stamp(world);
    savedWorld = saved;
    notify();
}

int StudioDocument::perform(const StudioEdit& edit) {
    if (!opened || playing) return 0;
    switch (edit.kind) {
    case StudioEdit::Kind::generateTerrain:
        mark(tr("Genera terreno"));
        world.terrain.generate(edit.settings);
        terrainSettings = edit.settings;
        if (viewport) viewport->refresh();
        notify();
        return static_cast<int>(world.terrain.keys().size());
    case StudioEdit::Kind::clearTerrain:
        if (world.terrain.empty()) return 0;
        mark(tr("Cancella terreno"));
        world.terrain = Terrain();
        if (viewport) viewport->refresh();
        notify();
        return 1;
    case StudioEdit::Kind::region: {
        Terrain probe = world.terrain;
        int changedCells = 0;
        std::string label;
        switch (edit.action) {
        case TerrainRegionAction::fillWater: changedCells = probe.fillWater(edit.point, edit.extent); label = tr("Crea acqua"); break;
        case TerrainRegionAction::clearWater: changedCells = probe.clearWater(edit.point, edit.extent); label = tr("Svuota acqua"); break;
        case TerrainRegionAction::fill: changedCells = probe.fillRegion(edit.point, edit.extent, edit.material); label = tr("Riempi regione"); break;
        case TerrainRegionAction::clear: changedCells = probe.clearRegion(edit.point, edit.extent); label = tr("Cancella regione"); break;
        }
        if (changedCells <= 0) return 0;
        mark(label);
        world.terrain = std::move(probe);
        if (viewport) viewport->refresh();
        notify();
        return changedCells;
    }
    case StudioEdit::Kind::water: {
        TerrainWater next = TerrainWater::fromJson(edit.water.json());
        if (next == world.terrain.waterProperties) return 0;
        mark(tr("Proprietà dell’acqua"), "terrain.water");
        world.terrain.waterProperties = next;
        if (viewport) viewport->refresh();
        notify();
        return 1;
    }
    case StudioEdit::Kind::terrain: {
        std::string token = "terrain.stroke." + std::to_string(edit.stroke);
        Terrain probe = world.terrain;
        int changedCells = probe.apply(edit.tool, edit.point, terrainBrush);
        if (changedCells <= 0) return 0;
        mark(tr("Terreno · {0}", toolTitle(edit.tool)), token);
        world.terrain = probe;
        if (viewport) viewport->refresh();
        notify();
        return changedCells;
    }
    case StudioEdit::Kind::rotateGroup: {
        double step = snapped(edit.amount, angleSnap);
        int axis = edit.axis;
        int count = transformGroup(tr("Ruota selezione"), edit.gesture, [&](const BlockSet& origins, Vec3 pivotPoint) { return transform_math::rotated(origins, pivotPoint, axis, step); });
        notify();
        return count;
    }
    case StudioEdit::Kind::scaleGroup: {
        int axis = edit.axis;
        double factor = edit.amount;
        ScaleAnchor anchorPoint = edit.anchor;
        double snap = moveSnap;
        int count = transformGroup(tr("Scala selezione"), edit.gesture, [&](const BlockSet& origins, Vec3) -> BlockSet {
            std::vector<WorldBlock> list;
            for (const auto& [id, b] : origins) list.push_back(b);
            auto box = bounds(list);
            if (!box) return {};
            double extent = std::max(rules::world::minimumSize, axis == 0 ? box->high.x - box->low.x : axis == 1 ? box->high.y - box->low.y : box->high.z - box->low.z);
            double target = std::max(rules::world::minimumSize, snapped(extent * factor, snap));
            return transform_math::scaled(origins, axis, anchorPoint, target / extent);
        });
        notify();
        return count;
    }
    }
    return 0;
}

void StudioDocument::restore(const StudioRevision& revision) {
    if (revision.parts.size() > 1) {
        extending = true;
        parts.clear();
        for (const auto& id : revision.parts)
            if (exists(id)) parts.push_back(id);
        setSelection(revision.selection);
        extending = false;
    } else {
        setSelection(revision.selection);
    }
}

void StudioDocument::undo() {
    if (playing || undoStack.empty()) return;
    StudioRevision revision = undoStack.back();
    undoStack.pop_back();
    redoStack.push_back({revision.label, world, current, parts});
    world = revision.world;
    restore(revision);
    coalescing.reset();
    pruneTabs();
    refreshHistoryLabels();
    if (viewport) viewport->refresh();
    notify();
}

void StudioDocument::redo() {
    if (playing || redoStack.empty()) return;
    StudioRevision revision = redoStack.back();
    redoStack.pop_back();
    undoStack.push_back({revision.label, world, current, parts});
    world = revision.world;
    restore(revision);
    coalescing.reset();
    pruneTabs();
    refreshHistoryLabels();
    if (viewport) viewport->refresh();
    notify();
}

void StudioDocument::pruneTabs() {
    openScripts = visibleScripts();
    if (activeScript && std::find(openScripts.begin(), openScripts.end(), *activeScript) == openScripts.end())
        activeScript = openScripts.empty() ? std::nullopt : std::optional<std::string>(openScripts.back());
    if (current && !current->service && !exists(current->id))
        setSelection(world.blocks.empty() ? std::nullopt : std::optional<StudioSelection>(StudioSelection::instance(world.blocks.front().id)));
    for (auto it = problems.begin(); it != problems.end();) {
        if (!recordIndex(it->first)) it = problems.erase(it);
        else ++it;
    }
}

void StudioDocument::refreshHistoryLabels() {
    undoTitle = undoStack.empty() ? std::nullopt : std::optional<std::string>(undoStack.back().label);
    redoTitle = redoStack.empty() ? std::nullopt : std::optional<std::string>(redoStack.back().label);
}

void StudioDocument::resetHistory() {
    undoStack.clear();
    redoStack.clear();
    coalescing.reset();
    refreshHistoryLabels();
}

std::string StudioDocument::freeName(const std::string& id, const std::string& wanted) const {
    auto parent = world.parentOf(id);
    std::set<std::string> taken;
    for (const auto& b : world.blocks)
        if (b.id != id && parent && b.parent == *parent) taken.insert(b.displayName());
    for (const auto& r : world.instances)
        if (r.id != id && parent && r.parent == *parent) taken.insert(r.name);
    if (!taken.count(wanted)) return wanted;
    int i = 2;
    while (taken.count(wanted + " " + std::to_string(i))) ++i;
    return wanted + " " + std::to_string(i);
}

bool StudioDocument::addPart(const std::string& kind) {
    if (playing || static_cast<int>(world.blocks.size()) >= rules::world::parts) return false;
    mark(tr("Inserisci {0}", studio_catalog::title(kind)));
    WorldBlock block;
    block.kind = kind;
    block.name = studio_catalog::title(kind);
    placeBlock(block);
    world.blocks.push_back(block);
    setSelection(StudioSelection::instance(block.id));
    activeScript.reset();
    notify();
    return true;
}

Result<void> StudioDocument::insertMesh(const StudioAsset& asset) {
    if (playing) return Error::make(ErrorCode::conflict, tr("Ferma la prova prima di inserire un modello."));
    if (static_cast<int>(world.blocks.size()) >= rules::world::parts) return Error::make(ErrorCode::limit, tr("Hai raggiunto il limite di {0} parti.", rules::world::parts));
    if (!meshAvailable || !meshAvailable(asset.asset)) return Error::make(ErrorCode::notFound, tr("Risorsa non leggibile: {0}.", asset.name));
    mark(tr("Inserisci {0}", asset.name));
    WorldBlock block;
    block.kind = "mesh";
    block.meshAsset = asset.asset;
    block.name = std::filesystem::path(asset.name).stem().string();
    if (block.name.empty()) block.name = studio_catalog::title("mesh");
    placeBlock(block);
    world.blocks.push_back(block);
    setSelection(StudioSelection::instance(block.id));
    activeScript.reset();
    notify();
    return {};
}

Result<void> StudioDocument::importMesh(const std::filesystem::path& file) {
    if (playing) return Error::make(ErrorCode::conflict, tr("Ferma la prova prima di importare un modello."));
    std::string extension = file.extension().string();
    if (!extension.empty()) extension = lowercase(extension.substr(1));
    const auto& allowed = studio_catalog::meshExtensions();
    if (std::find(allowed.begin(), allowed.end(), extension) == allowed.end() || !mesh_import::supported(extension)) {
        std::vector<std::string> listed;
        for (const auto& e : allowed) listed.push_back("." + e);
        return Error::make(ErrorCode::invalidArgument, tr("Ludissey non apre i file .{0}. Usa {1}.", extension, join(listed, " ")));
    }
    auto mesh = mesh_import::load(file);
    if (!mesh || mesh->empty()) return Error::make(ErrorCode::invalidArgument, tr("Nessuna mesh trovata in {0}.", file.filename().string()));
    StudioAsset asset;
    try {
        asset = studio_assets::install(file);
    } catch (const std::exception& e) {
        return Error::make(ErrorCode::invalidArgument, e.what());
    }
    auto inserted = insertMesh(asset);
    if (!inserted) return inserted;
    loadAssets();
    log(tr("Modello importato: {0}, {1}", file.filename().string(), asset.asset.substr(0, 12)), false);
    return {};
}

Result<void> StudioDocument::importContent(const std::string& id, const std::string& property, const std::filesystem::path& file) {
    if (playing) return Error::make(ErrorCode::conflict, tr("Ferma la prova prima di importare una risorsa."));
    std::string extension = file.extension().string();
    if (!extension.empty()) extension = lowercase(extension.substr(1));
    auto allowed = studio_catalog::contentExtensions(property);
    if (std::find(allowed.begin(), allowed.end(), extension) == allowed.end()) {
        std::vector<std::string> listed;
        for (const auto& e : allowed) listed.push_back("." + e);
        return Error::make(ErrorCode::invalidArgument, tr("Ludissey non apre i file .{0} per {1}. Usa {2}.", extension, property, join(listed, " ")));
    }
    std::error_code problem;
    auto size = std::filesystem::file_size(file, problem);
    if (problem) return Error::make(ErrorCode::notFound, tr("File non leggibile: {0}.", file.filename().string()));
    if (size == 0) return Error::make(ErrorCode::invalidArgument, tr("Il file è vuoto: {0}.", file.filename().string()));
    if (size > static_cast<std::uintmax_t>(rules::assets::megabytes) * 1024 * 1024)
        return Error::make(ErrorCode::limit, tr("La risorsa deve pesare al massimo {0} MB.", rules::assets::megabytes));
    if (!recordIndex(id)) return Error::make(ErrorCode::notFound, tr("Oggetto non trovato."));
    StudioAsset asset;
    try {
        asset = studio_assets::install(file);
    } catch (const std::exception& e) {
        return Error::make(ErrorCode::invalidArgument, e.what());
    }
    setProperty(id, property, PropertyValue::content(asset_cache::reference(asset.asset)));
    log(tr("Risorsa importata: {0}, {1}", file.filename().string(), asset.asset.substr(0, 12)), false);
    return {};
}

namespace {

bool finished(const std::filesystem::path& file, const std::string& extension) {
    std::ifstream in(file, std::ios::binary | std::ios::ate);
    if (!in) return false;
    std::streamoff size = in.tellg();
    if (extension == "png") {
        static const char end[] = {'I', 'E', 'N', 'D', static_cast<char>(0xAE), 0x42, 0x60, static_cast<char>(0x82)};
        if (size < 8) return false;
        char tail[8];
        in.seekg(size - 8);
        in.read(tail, 8);
        return in && std::equal(std::begin(tail), std::end(tail), std::begin(end));
    }
    if (size < 2) return false;
    char tail[2];
    in.seekg(size - 2);
    in.read(tail, 2);
    return in && static_cast<unsigned char>(tail[0]) == 0xFF && static_cast<unsigned char>(tail[1]) == 0xD9;
}

}

Result<void> StudioDocument::setExperienceImage(ExperienceImage kind, const std::filesystem::path& file) {
    if (!opened) return Error::make(ErrorCode::conflict, tr("Apri un progetto prima di scegliere un’immagine."));
    if (playing) return Error::make(ErrorCode::conflict, tr("Ferma la prova prima di cambiare le immagini dell’esperienza."));
    std::string extension = file.extension().string();
    if (!extension.empty()) extension = lowercase(extension.substr(1));
    if (std::find(rules::assets::imageExtensions.begin(), rules::assets::imageExtensions.end(), extension) == rules::assets::imageExtensions.end())
        return Error::make(ErrorCode::invalidArgument, tr("Usa un’immagine .png o .jpg."));
    std::error_code problem;
    auto size = std::filesystem::file_size(file, problem);
    if (problem) return Error::make(ErrorCode::notFound, tr("File non leggibile: {0}.", file.filename().string()));
    if (size == 0 || size > static_cast<std::uintmax_t>(rules::assets::megabytes) * 1024 * 1024)
        return Error::make(ErrorCode::limit, tr("L’immagine deve pesare tra 1 byte e {0} MB.", rules::assets::megabytes));
    CFURLRef location = CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(file.c_str()), static_cast<CFIndex>(file.native().size()), false);
    CGImageSourceRef source = location ? CGImageSourceCreateWithURL(location, nullptr) : nullptr;
    bool complete = source && CGImageSourceGetCount(source) > 0 && finished(file, extension);
    CGImageRef decoded = complete ? CGImageSourceCreateImageAtIndex(source, 0, nullptr) : nullptr;
    std::size_t width = decoded ? CGImageGetWidth(decoded) : 0, height = decoded ? CGImageGetHeight(decoded) : 0;
    if (decoded) CGImageRelease(decoded);
    if (source) CFRelease(source);
    if (location) CFRelease(location);
    if (width < 16 || height < 16 || width > 4096 || height > 4096) return Error::make(ErrorCode::invalidArgument, tr("L’immagine deve misurare da 16 a 4096 pixel per lato."));
    std::string asset;
    try {
        asset = asset_cache::adopt(file);
    } catch (const std::exception& e) {
        return Error::make(ErrorCode::invalidArgument, e.what());
    }
    if (!isImageAsset(asset)) return Error::make(ErrorCode::invalidArgument, tr("Immagine non valida."));
    bool icon = kind == ExperienceImage::icon;
    mark(icon ? tr("Icona dell’esperienza") : tr("Miniatura dell’esperienza"), std::nullopt);
    (icon ? world.icon : world.thumbnail) = asset;
    log(icon ? tr("Icona impostata: {0}", file.filename().string()) : tr("Miniatura impostata: {0}", file.filename().string()), false);
    notify();
    return {};
}

void StudioDocument::clearExperienceImage(ExperienceImage kind) {
    if (!opened || playing) return;
    auto& target = kind == ExperienceImage::icon ? world.icon : world.thumbnail;
    if (!target) return;
    mark(kind == ExperienceImage::icon ? tr("Rimuovi icona") : tr("Rimuovi miniatura"), std::nullopt);
    target.reset();
    notify();
}

std::optional<std::string> StudioDocument::experienceImage(ExperienceImage kind) const { return kind == ExperienceImage::icon ? world.icon : world.thumbnail; }

std::optional<std::filesystem::path> StudioDocument::experienceImageFile(ExperienceImage kind) const {
    auto asset = experienceImage(kind);
    if (!asset) return std::nullopt;
    return asset_cache::verified(*asset);
}

bool StudioDocument::insertAsset(const StudioAsset& asset) {
    auto inserted = insertMesh(asset);
    if (!inserted) {
        log(inserted.error().message, true);
        return false;
    }
    return true;
}

void StudioDocument::loadAssets() {
    assetsError.reset();
    try {
        assets = studio_assets::catalog();
    } catch (const std::exception& e) {
        assets.clear();
        assetsError = e.what();
    }
    notify();
}

Result<Experience> StudioDocument::publishable() const {
    Experience copy = world;
    copy.schemaVersion = rules::schemaVersion;
    for (const auto& block : copy.blocks) {
        if (block.kind != "mesh") continue;
        if (!block.meshAsset || !asset_cache::valid(*block.meshAsset))
            return Error::make(ErrorCode::invalidArgument, tr("«{0}» ha perso il suo modello 3D. Importalo di nuovo prima di pubblicare.", block.displayName()));
    }
    for (const auto& record : copy.instances) {
        const ClassDescriptor* d = registry.find(record.className);
        if (!d || !(d->canCreate() || d->runtime == std::optional<std::string>("container")))
            return Error::make(ErrorCode::invalidArgument, tr("Tipo di oggetto non supportato: {0}.", record.className));
        auto parentClass = className(record.parent);
        if (!parentClass || !registry.canParent(record.className, *parentClass))
            return Error::make(ErrorCode::invalidArgument, tr("«{0}» ({1}) non può stare in {2}.", record.name, record.className, displayName(record.parent)));
        if (static_cast<int>(utf8Length(record.source())) > rules::instances::sourceCharacters)
            return Error::make(ErrorCode::limit, tr("Lo script «{0}» supera {1} caratteri.", record.name, rules::instances::sourceCharacters));
    }
    Experience body = copy;
    body.terrain = Terrain();
    body.instances.clear();
    std::size_t payload = body.json().dump().size();
    std::size_t ground = copy.terrain.json().dump().size();
    Json objects = Json::array();
    for (const auto& r : copy.instances) objects.push(r.json());
    std::size_t objectSize = objects.dump().size();
    if (ground > static_cast<std::size_t>(rules::terrain::keys * rules::terrain::keyCharacters))
        return Error::make(ErrorCode::limit, tr("Il terreno è troppo grande per essere salvato. Prova a semplificarlo.", static_cast<long long>(ground)));
    if (objectSize > static_cast<std::size_t>(rules::instances::keys * rules::instances::keyCharacters))
        return Error::make(ErrorCode::limit, tr("Gli script sono troppo lunghi per essere salvati. Accorciali un po’.", static_cast<long long>(objectSize)));
    if (payload > static_cast<std::size_t>(rules::world::payloadCharacters))
        return Error::make(ErrorCode::limit, tr("Il progetto è troppo grande per essere salvato ({0}% dello spazio). Togli qualche oggetto.", static_cast<long long>(std::llround(100.0 * static_cast<double>(payload) / rules::world::payloadCharacters))));
    return copy;
}

void StudioDocument::duplicateSelection() {
    auto all = expanded(selectedIds());
    std::set<std::string> ids(all.begin(), all.end());
    if (playing || ids.empty()) return;
    auto chosen = roots(selectedIds());
    if (!std::all_of(chosen.begin(), chosen.end(), [&](const std::string& id) { return canEdit(id, StudioAction::structure); })) {
        log(tr("Oggetto bloccato: sbloccalo per duplicarlo."), true);
        return;
    }
    std::string label = chosen.size() == 1 ? tr("Duplica {0}", displayName(chosen[0])) : tr("Duplica {0} oggetti", static_cast<long long>(chosen.size()));
    StudioClipboard source;
    for (const auto& b : world.blocks)
        if (ids.count(b.id)) source.blocks.push_back(b);
    for (const auto& r : world.instances)
        if (ids.count(r.id)) source.instances.push_back(r);
    clone(source, label, std::nullopt, 1);
}

void StudioDocument::deleteSelection() {
    if (playing) return;
    auto chosen = roots(selectedIds());
    if (chosen.empty()) return;
    if (!std::all_of(chosen.begin(), chosen.end(), [&](const std::string& id) { return canEdit(id, StudioAction::structure); })) {
        log(tr("Oggetto bloccato: sbloccalo per eliminarlo."), true);
        return;
    }
    auto removedList = expanded(chosen);
    std::set<std::string> removed(removedList.begin(), removedList.end());
    auto parent = world.parentOf(chosen[0]);
    mark(tr("Elimina"));
    openScripts.erase(std::remove_if(openScripts.begin(), openScripts.end(), [&](const std::string& id) { return removed.count(id) > 0; }), openScripts.end());
    if (activeScript && std::find(openScripts.begin(), openScripts.end(), *activeScript) == openScripts.end()) activeScript.reset();
    world.blocks.erase(std::remove_if(world.blocks.begin(), world.blocks.end(), [&](const WorldBlock& b) { return removed.count(b.id) > 0; }), world.blocks.end());
    world.instances.erase(std::remove_if(world.instances.begin(), world.instances.end(), [&](const InstanceRecord& r) { return removed.count(r.id) > 0; }), world.instances.end());
    for (const auto& id : removed) {
        problems.erase(id);
        expandedIds.erase(id);
    }
    if (parent && exists(*parent)) setSelection(StudioSelection::instance(*parent));
    else setSelection(parent ? std::optional<StudioSelection>(StudioSelection::ofService(*parent)) : std::nullopt);
    notify();
}

void StudioDocument::selectAll() {
    if (playing) return;
    std::vector<std::string> ids;
    for (const auto& b : world.blocks)
        if (b.parent == "Workspace" && !b.locked) ids.push_back(b.id);
    select(ids);
}

void StudioDocument::openScript(const std::string& id) {
    Log::info("Studio", "apertura script");
    if (!isScript(id)) return;
    if (std::find(openScripts.begin(), openScripts.end(), id) == openScripts.end()) openScripts.push_back(id);
    activeScript = id;
    setSelection(StudioSelection::instance(id));
    checkSyntax(id);
    notify();
}

void StudioDocument::closeScript(const std::string& id) {
    openScripts.erase(std::remove(openScripts.begin(), openScripts.end(), id), openScripts.end());
    if (activeScript == id) {
        activeScript = openScripts.empty() ? std::nullopt : std::optional<std::string>(openScripts.back());
        if (activeScript) setSelection(StudioSelection::instance(*activeScript));
    }
    notify();
}

void StudioDocument::update(const std::string& part, const std::function<void(WorldBlock&)>& change, std::optional<std::string> label, std::optional<std::string> coalesce, StudioAction action) {
    auto i = index(part);
    if (!canEdit(part, action) || !i) return;
    WorldBlock b = world.blocks[*i];
    change(b);
    b.x = clampHorizontal(b.x);
    b.z = clampHorizontal(b.z);
    b.sizeX = std::min<double>(rules::world::maximumSize, std::max(rules::world::minimumSize, b.sizeX));
    b.sizeY = std::min<double>(rules::world::maximumSize, std::max(rules::world::minimumSize, b.sizeY));
    b.sizeZ = std::min<double>(rules::world::maximumSize, std::max(rules::world::minimumSize, b.sizeZ));
    b.rotationX = wrapDegrees(b.rotationX);
    b.rotationY = wrapDegrees(b.rotationY);
    b.rotationZ = wrapDegrees(b.rotationZ);
    b.transparency = std::min(1.0, std::max(0.0, b.transparency));
    b.reflectance = std::min(1.0, std::max(0.0, b.reflectance));
    b.y = std::min<double>(rules::world::vertical, std::max<double>(-rules::world::vertical, b.y));
    if (b == world.blocks[*i]) return;
    if (label) mark(*label, coalesce);
    world.blocks[*i] = std::move(b);
}

double StudioDocument::wrapDegrees(double value) {
    double angle = std::fmod(value, 360);
    if (angle > 180) angle -= 360;
    if (angle < -180) angle += 360;
    return angle;
}

double StudioDocument::clampHorizontal(double value) { return std::min<double>(rules::world::horizontal, std::max<double>(-rules::world::horizontal, value)); }

double StudioDocument::snapped(double value, double step) {
    if (step <= 0) return value;
    return std::round(value / step) * step;
}

void StudioDocument::toggleBreakpoint(const std::string& script, std::uint32_t line) {
    if (playing || !isScript(script) || line == 0) return;
    auto& lines = breakpoints[script];
    if (!lines.erase(line)) lines.insert(line);
    if (lines.empty()) breakpoints.erase(script);
    notify();
}

void StudioDocument::resumeDebug(lim::StepMode mode) {
    if (!live || debugStops.empty()) return;
    auto context = debugStops.begin()->first;
    debugStops.erase(context);
    lastTick.reset();
    live->engine->vm(context)->resume(mode);
    if (!debugStops.empty() && !debugStops.begin()->second.frames.empty()) {
        const auto& frame = debugStops.begin()->second.frames.front();
        for (const auto& r : world.instances) if (auto instance = live->model->find(r.id); instance && instance->path() == frame.file) {
            openScript(r.id);
            focusLine = static_cast<int>(frame.line);
            break;
        }
    }
    notify();
}

void StudioDocument::play() {
    Log::info("Studio", playMode == StudioPlayMode::character ? "Play personaggio" : "Play camera");
    if (!opened || playing) return;
    output.clear();
    std::string name = viewport ? viewport->avatarName() : "Player";
    live = std::make_unique<WorldSession>(world, playMode == StudioPlayMode::character ? WorldSession::Mode::play : WorldSession::Mode::run, name, 0, true,
                                          [this](const OutputLine& line) { emit(line); });
    live->engine->storage = [this](const std::string& action, const Json& value, ScriptEngine::StorageReply reply) {
        if (action == "save") playData = value;
        reply(action == "save" ? std::nullopt : playData, "");
    };
    playing = true;
    respawnAt.reset();
    log(tr("Prova avviata: {0} oggetti, {1}", static_cast<long long>(live->parts().size()), playMode == StudioPlayMode::character ? tr("controlli personaggio") : tr("solo server, camera libera")), false);
    if (viewport) viewport->startPlay();
    std::optional<CharacterState> state = viewport ? viewport->characterState() : std::nullopt;
    CharacterState spawn = state ? *state : motion::arrival(live->parts(), world.terrain, live->teamColor());
    debugStops.clear();
    if (!breakpoints.empty()) for (auto context : {ScriptContext::server, ScriptContext::client}) {
        lim::DebugSettings settings;
        for (const auto& [id, lines] : breakpoints) if (auto instance = live->model->find(id)) settings.breakpoints[instance->path()] = lines;
        settings.stopped = [this, context](const lim::DebugStop& stopped) {
            debugStops[context] = stopped;
            if (!stopped.frames.empty()) {
                const auto& frame = stopped.frames.front();
                for (const auto& r : world.instances) if (auto instance = live->model->find(r.id); instance && instance->path() == frame.file) {
                    openScript(r.id);
                    focusLine = static_cast<int>(frame.line);
                    break;
                }
            }
            notify();
        };
        live->engine->vm(context)->setDebugger(std::move(settings));
    }
    live->start({spawn.x, spawn.y, spawn.z});
    lastTick.reset();
    shownRevision = -1;
    if (viewport) viewport->refresh();
    notify();
}

void StudioDocument::stop() {
    if (playing) Log::info("Studio", "Stop");
    debugStops.clear();
    if (playing) log(tr("Prova finita: le modifiche fatte in gioco sono state annullate."), false);
    if (live) lastPerformance = live->engine->performance();
    if (live) live->stop();
    live.reset();
    gui.clear();
    bool was = playing;
    playing = false;
    if (viewport) {
        viewport->endPlay();
        viewport->refresh();
    }
    if (was) notify();
}

void StudioDocument::tick(double time) {
    if (!playing || !live || debugPaused()) return;
    double delta = std::min(CharacterDriver::maximumFrame, lastTick ? time - *lastTick : CharacterDriver::step);
    lastTick = time;
    if (viewport)
        if (auto state = viewport->characterState()) {
            if (viewport->takeFall()) live->kill();
            live->place(state->x, state->y, state->z, state->yaw);
        }
    live->advance(delta);
    if (auto point = live->teleport(); point && viewport) viewport->teleport(*point);
    if (viewport) viewport->pace(live->speedFactor(), live->jumpFactor());
    if (live->dead() && playMode == StudioPlayMode::character) {
        if (respawnAt) {
            if (time >= *respawnAt) {
                respawnAt.reset();
                if (viewport)
                    if (auto spawn = viewport->respawn()) live->spawn({spawn->x, spawn->y, spawn->z});
            }
        } else {
            respawnAt = time + rules::respawnSeconds;
            if (viewport) viewport->hideCharacter();
        }
    }
    if (live->revision() != shownRevision) {
        shownRevision = live->revision();
        auto screensNow = live->gui();
        bool screensChanged = !(gui == screensNow);
        if (screensChanged) gui = std::move(screensNow);
        if (viewport) viewport->refresh();
        if (screensChanged) notify();
    }
}

void StudioDocument::key(std::uint16_t code, bool down) {
    if (live) live->key(code, down);
}

std::vector<GuiNode> StudioDocument::screens() const { return playing ? gui : gui_snapshot::capture(world, "StarterGui", registry); }

int StudioDocument::runCommand(const std::string& source) {
    std::string code = trim(source);
    if (!opened || playing || code.empty()) return 0;
    log("> " + code, false);
    DataModel model(world, registry);
    ScriptEngine engine(model, [this](const OutputLine& line) { emit(line); });
    engine.studio = true;
    Terrain ground = world.terrain;
    engine.terrain = &ground;
    for (const auto& id : selectedIds())
        if (auto node = model.find(id)) engine.selection.push_back(node);
    if (!engine.execute(code, ScriptContext::edit, tr("Barra dei comandi"))) return 0;
    for (int i = 0; i < 600 && engine.running() > 0; ++i) engine.step(1.0 / 60);
    engine.stop();
    std::vector<std::string> chosen;
    for (const auto& n : engine.selection)
        if (!n->destroyed) chosen.push_back(n->id);
    Experience result = model.exportInto(world);
    result.terrain = std::move(ground);
    std::map<std::string, WorldBlock> before;
    std::map<std::string, InstanceRecord> beforeRecords;
    for (const auto& b : world.blocks) before[b.id] = b;
    for (const auto& r : world.instances) beforeRecords[r.id] = r;
    auto findBlock = [](const Experience& e, const std::string& id) -> std::optional<WorldBlock> {
        for (const auto& b : e.blocks)
            if (b.id == id) return b;
        return std::nullopt;
    };
    auto findRecord = [](const Experience& e, const std::string& id) -> std::optional<InstanceRecord> {
        for (const auto& r : e.instances)
            if (r.id == id) return r;
        return std::nullopt;
    };
    for (const auto& entry : before) {
        const std::string& id = entry.first;
        const WorldBlock& original = entry.second;
        if (!isLocked(id) || findBlock(result, id) == std::optional<WorldBlock>(original)) continue;
        log(tr("«{0}» è bloccato.", displayName(id)), true);
        auto it = std::find_if(result.blocks.begin(), result.blocks.end(), [&](const WorldBlock& b) { return b.id == id; });
        if (it != result.blocks.end()) *it = original;
        else result.blocks.push_back(original);
    }
    for (const auto& entry : beforeRecords) {
        const std::string& id = entry.first;
        const InstanceRecord& original = entry.second;
        if (!isLocked(id) || findRecord(result, id) == std::optional<InstanceRecord>(original)) continue;
        log(tr("«{0}» è bloccato.", displayName(id)), true);
        auto it = std::find_if(result.instances.begin(), result.instances.end(), [&](const InstanceRecord& r) { return r.id == id; });
        if (it != result.instances.end()) *it = original;
        else result.instances.push_back(original);
    }
    std::set<std::string> blockIds, recordIds;
    for (const auto& [id, b] : before) blockIds.insert(id);
    for (const auto& b : result.blocks) blockIds.insert(b.id);
    for (const auto& [id, r] : beforeRecords) recordIds.insert(id);
    for (const auto& r : result.instances) recordIds.insert(r.id);
    int changedCount = 0;
    for (const auto& id : blockIds) {
        auto was = before.count(id) ? std::optional<WorldBlock>(before[id]) : std::nullopt;
        if (was != findBlock(result, id)) ++changedCount;
    }
    for (const auto& id : recordIds) {
        auto was = beforeRecords.count(id) ? std::optional<InstanceRecord>(beforeRecords[id]) : std::nullopt;
        if (was != findRecord(result, id)) ++changedCount;
    }
    if (!(result.terrain == world.terrain)) ++changedCount;
    if (changedCount == 0 || result == world) {
        if (chosen != selectedIds()) select(chosen);
        return 0;
    }
    mark(tr("Barra dei comandi"));
    world = result;
    select(chosen);
    pruneTabs();
    if (viewport) viewport->refresh();
    notify();
    return changedCount;
}

void StudioDocument::emit(const OutputLine& line) {
    output.push_back(line);
    if (output.size() > static_cast<std::size_t>(outputLines)) output.erase(output.begin(), output.begin() + static_cast<std::ptrdiff_t>(output.size() - outputLines));
    notify();
}

void StudioDocument::log(const std::string& text, bool error) {
    OutputLine line;
    line.text = text;
    line.kind = error ? OutputLine::Kind::error : OutputLine::Kind::info;
    line.time = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    emit(line);
}

}
