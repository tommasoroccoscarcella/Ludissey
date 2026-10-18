#include "WorldSession.hpp"

#include "Engine/Backend/Assets.hpp"
#include "Engine/Character/Character.hpp"
#include "Engine/Core/Rules.hpp"
#include "Engine/Physics/Physics.hpp"
#include "Localization/Localization.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ld {

const std::map<std::uint16_t, std::string>& WorldSession::keyCodes() {
    static const std::map<std::uint16_t, std::string> codes = {
        {0, "A"}, {11, "B"}, {8, "C"}, {2, "D"}, {14, "E"}, {3, "F"}, {5, "G"}, {4, "H"}, {34, "I"}, {38, "J"}, {40, "K"}, {37, "L"}, {46, "M"}, {45, "N"}, {31, "O"}, {35, "P"}, {12, "Q"}, {15, "R"},
        {1, "S"}, {17, "T"}, {32, "U"}, {9, "V"}, {13, "W"}, {7, "X"}, {16, "Y"}, {6, "Z"}, {29, "Zero"}, {18, "One"}, {19, "Two"}, {20, "Three"}, {21, "Four"}, {23, "Five"}, {22, "Six"},
        {26, "Seven"}, {28, "Eight"}, {25, "Nine"}, {36, "Return"}, {48, "Tab"}, {49, "Space"}, {53, "Escape"}, {51, "Backspace"}, {123, "Left"}, {124, "Right"}, {125, "Down"}, {126, "Up"},
        {56, "LeftShift"}, {60, "RightShift"}, {59, "LeftControl"}, {58, "LeftAlt"}};
    return codes;
}

const std::vector<Limb>& WorldSession::limbs() {
    static const std::vector<Limb> table = {{"HumanoidRootPart", {0.8, 0.8, 0.4}, {0, 0.8, 0}}, {"Torso", {0.8, 0.8, 0.4}, {0, 0.8, 0}}, {"Head", {0.5, 0.4, 0.4}, {0, 1.4, 0}},
                                            {"Left Arm", {0.4, 0.8, 0.4}, {-0.6, 0.8, 0}}, {"Right Arm", {0.4, 0.8, 0.4}, {0.6, 0.8, 0}}, {"Left Leg", {0.4, 0.8, 0.4}, {-0.2, 0.2, 0}},
                                            {"Right Leg", {0.4, 0.8, 0.4}, {0.2, 0.2, 0}}};
    return table;
}

WorldSession::WorldSession(const Experience& experience, Mode sessionMode, std::string name, double userId, bool studio, std::function<void(const OutputLine&)> output)
    : model(std::make_unique<DataModel>(experience)), terrain(experience.terrain), mode(sessionMode), playerName(std::move(name)) {
    engine = std::make_unique<ScriptEngine>(*model, std::move(output));
    engine->studio = studio;
    engine->terrain = &terrain;
    auto added = model->added;
    auto changed = model->changed;
    model->added = [this, added](const InstancePtr& node) {
        if (added) added(node);
        discover(node);
    };
    model->changed = [this, changed](const InstancePtr& node, const std::string& property) {
        if (changed) changed(node, property);
        if (property == "Disabled" && !disabled(node)) {
            launched.erase(node->id);
            discover(node);
        }
        if (node->className == "Sound" && sounds.count(node->id) && (property == "Volume" || property == "PlaybackSpeed" || property == "Looped" || property == "SoundId")) sound(node, true);
        if (node->className == "Humanoid" && property == "Health") {
            const PropertyValue* v = node->property("Health");
            if (v && v->kind == PropertyValue::Kind::number) engine->fire(node, "HealthChanged", {Variant::number(v->numbers[0])});
        }
    };
    engine->soundPlayer = [this](const InstancePtr& node, bool playing) { sound(node, playing); };
    if (mode == Mode::play) {
        auto p = model->make("Player", playerName);
        p->properties["UserId"] = PropertyValue::number(userId);
        for (const char* className : {"Backpack", "PlayerGui", "PlayerScripts"}) {
            auto node = model->make(className);
            node->parentLink = p;
            p->children.push_back(node);
        }
        playerNode = p;
        model->service("Players")->properties["LocalPlayer"] = PropertyValue::reference(p->id);
        engine->localPlayer = p;
        assignTeam();
    }
}

void WorldSession::assignTeam() {
    auto teams = model->service("Teams");
    playerNode->properties["Neutral"] = PropertyValue::boolean(true);
    if (!teams) return;
    std::vector<InstancePtr> open;
    for (const auto& team : teams->children) {
        if (team->className != "Team" || team->destroyed) continue;
        const PropertyValue* automatic = team->property("AutoAssignable");
        if (!automatic || automatic->kind != PropertyValue::Kind::boolean || automatic->flag) open.push_back(team);
    }
    if (open.empty()) return;
    std::uint32_t hash = 2166136261u;
    for (unsigned char c : playerNode->name) hash = (hash ^ c) * 16777619u;
    const InstancePtr& team = open[hash % open.size()];
    playerNode->properties["Team"] = PropertyValue::reference(team->id);
    playerNode->properties["Neutral"] = PropertyValue::boolean(false);
    if (const PropertyValue* color = team->property("TeamColor")) playerNode->properties["TeamColor"] = *color;
}

std::optional<std::string> WorldSession::teamColor() const {
    if (!playerNode) return std::nullopt;
    const PropertyValue* neutral = playerNode->property("Neutral");
    const PropertyValue* color = playerNode->property("TeamColor");
    if (!color || (neutral && neutral->kind == PropertyValue::Kind::boolean && neutral->flag)) return std::nullopt;
    return color->text;
}

WorldSession::~WorldSession() { stop(); }

std::vector<WorldBlock> WorldSession::parts() const {
    auto all = model->parts();
    if (!characterNode) return all;
    std::vector<WorldBlock> out;
    for (const auto& part : all) {
        auto node = model->find(part.id);
        if (!node || !node->isDescendantOf(*characterNode)) out.push_back(part);
    }
    return out;
}

double WorldSession::health() const {
    if (!humanoidNode) return 0;
    const PropertyValue* v = humanoidNode->property("Health");
    return v && v->kind == PropertyValue::Kind::number ? v->numbers[0] : 0;
}

double WorldSession::maximumHealth() const {
    if (!humanoidNode) return 100;
    const PropertyValue* v = humanoidNode->property("MaxHealth");
    return v && v->kind == PropertyValue::Kind::number && v->numbers[0] > 0 ? v->numbers[0] : 100;
}

double WorldSession::humanoidNumber(const std::string& name, double standard) const {
    if (!humanoidNode) return standard;
    const PropertyValue* v = humanoidNode->property(name);
    if (!v || v->kind != PropertyValue::Kind::number || !std::isfinite(v->numbers[0])) return standard;
    return std::max(0.0, v->numbers[0]);
}

bool WorldSession::disabled(const InstancePtr& node) {
    const PropertyValue* v = node->property("Disabled");
    return v && v->kind == PropertyValue::Kind::boolean && v->flag;
}

void WorldSession::start(Vec3 point) {
    discover(model->game);
    flush();
    for (const auto& node : model->game->descendants()) {
        const PropertyValue* playing = node->className == "Sound" ? node->property("Playing") : nullptr;
        if (playing && playing->kind == PropertyValue::Kind::boolean && playing->flag) sound(node, true);
    }
    if (!playerNode) return;
    auto players = model->service("Players");
    model->adopt(playerNode, players);
    engine->fire(players, "PlayerAdded", {Variant::object(playerNode)});
    auto scripts = model->find("StarterPlayer.StarterPlayerScripts");
    auto target = container("PlayerScripts");
    if (scripts && target) {
        auto children = scripts->children;
        for (const auto& c : children)
            if (auto copy = model->clone(c)) model->adopt(copy, target);
    }
    spawn(point);
}

InstancePtr WorldSession::container(const std::string& className) const {
    if (!playerNode) return nullptr;
    for (const auto& c : playerNode->children)
        if (c->className == className) return c;
    return nullptr;
}

void WorldSession::spawn(Vec3 point) {
    if (!playerNode) return;
    if (characterNode) {
        engine->fire(playerNode, "CharacterRemoving", {Variant::object(characterNode)});
        model->destroy(characterNode);
    }
    auto resets = [](const InstancePtr& node) {
        const PropertyValue* v = node->property("ResetOnSpawn");
        return !(v && v->kind == PropertyValue::Kind::boolean && !v->flag);
    };
    if (auto gui = container("PlayerGui")) {
        auto children = gui->children;
        for (const auto& c : children)
            if (resets(c)) model->destroy(c);
        std::set<std::string> kept;
        for (const auto& c : gui->children) kept.insert(c->name);
        if (auto starter = model->service("StarterGui")) {
            auto list = starter->children;
            for (const auto& c : list) {
                if (!resets(c) && kept.count(c->name)) continue;
                if (auto copy = model->clone(c)) model->adopt(copy, gui);
            }
        }
    }
    if (auto backpack = container("Backpack")) {
        auto children = backpack->children;
        for (const auto& c : children) model->destroy(c);
        if (auto pack = model->service("StarterPack")) {
            auto list = pack->children;
            for (const auto& c : list)
                if (auto copy = model->clone(c)) model->adopt(copy, backpack);
        }
    }
    auto body = *model->create("Model");
    body->name = playerName;
    for (const auto& limb : limbs()) {
        auto part = *model->create("Part");
        part->name = limb.name;
        part->block->sizeX = limb.size.x;
        part->block->sizeY = limb.size.y;
        part->block->sizeZ = limb.size.z;
        part->block->canCollide = false;
        if (limb.name == "HumanoidRootPart") part->block->transparency = 1;
        part->parentLink = body;
        body->children.push_back(part);
    }
    auto h = model->make("Humanoid");
    h->parentLink = body;
    body->children.push_back(h);
    auto root = body->child("HumanoidRootPart");
    body->properties["PrimaryPart"] = PropertyValue::reference(root ? std::optional<std::string>(root->id) : std::nullopt);
    stand();
    humanoidNode = h;
    characterNode = body;
    died = false;
    contacts.clear();
    place(point.x, point.y, point.z, 0);
    model->adopt(body, model->workspace());
    playerNode->properties["Character"] = PropertyValue::reference(body->id);
    if (auto scripts = model->find("StarterPlayer.StarterCharacterScripts")) {
        auto children = scripts->children;
        for (const auto& c : children)
            if (auto copy = model->clone(c)) model->adopt(copy, body);
    }
    engine->fire(playerNode, "CharacterAdded", {Variant::object(body)});
    flush();
}

void WorldSession::place(double x, double y, double z, double yaw) {
    if (!characterNode) return;
    double c = std::cos(yaw), s = std::sin(yaw);
    for (const auto& node : characterNode->children) {
        if (!node->block) continue;
        auto limb = std::find_if(limbs().begin(), limbs().end(), [&](const Limb& l) { return l.name == node->name; });
        if (limb == limbs().end()) continue;
        node->block->x = x + limb->offset.x * c + limb->offset.z * s;
        node->block->y = y + limb->offset.y;
        node->block->z = z - limb->offset.x * s + limb->offset.z * c;
        node->block->rotationY = yaw * 180 / pi;
    }
    placed = Vec3{x, y, z};
}

std::optional<Vec3> WorldSession::teleport() {
    if (!characterNode || !placed) return std::nullopt;
    auto root = characterNode->child("HumanoidRootPart");
    if (!root || !root->block) return std::nullopt;
    const Vec3& offset = limbs()[0].offset;
    double x = root->block->x - offset.x, y = root->block->y - offset.y, z = root->block->z - offset.z;
    if (std::fabs(x - placed->x) + std::fabs(y - placed->y) + std::fabs(z - placed->z) <= 0.001) return std::nullopt;
    placed = Vec3{x, y, z};
    return placed;
}

void WorldSession::kill() {
    if (!humanoidNode || died) return;
    model->set(humanoidNode, "Health", Variant::number(0));
    observeDeath();
}

void WorldSession::observeDeath() {
    if (!humanoidNode || died || health() > 0) return;
    died = true;
    engine->fire(humanoidNode, "Died", {});
}

void WorldSession::key(std::uint16_t code, bool down) {
    auto found = keyCodes().find(code);
    if (found == keyCodes().end()) return;
    const std::string& name = found->second;
    if (down) {
        if (engine->keys.count(name)) return;
        engine->keys.insert(name);
    } else engine->keys.erase(name);
    auto service = model->service("UserInputService");
    if (!service) return;
    auto input = model->make("InputObject");
    input->properties = {{"KeyCode", PropertyValue::enumItem(name)}, {"UserInputType", PropertyValue::enumItem("Keyboard")}, {"UserInputState", PropertyValue::enumItem(down ? "Begin" : "End")},
                         {"Position", PropertyValue::vector3(0, 0, 0)}, {"Delta", PropertyValue::vector3(0, 0, 0)}};
    engine->fire(service, down ? "InputBegan" : "InputEnded", {Variant::object(input), Variant::boolean(engine->focusedTextBox != nullptr)}, ScriptContext::client);
    model->destroy(input);
}

std::vector<GuiNode> WorldSession::gui() const { return gui_snapshot::capture(container("PlayerGui"), model->registry); }

std::vector<ToolSlot> WorldSession::tools() const {
    std::vector<ToolSlot> out;
    if (characterNode)
        for (const auto& c : characterNode->children)
            if (c->className == "Tool") out.push_back({c->id, c->name, true});
    if (auto backpack = container("Backpack"))
        for (const auto& c : backpack->children)
            if (c->className == "Tool") out.push_back({c->id, c->name, false});
    std::stable_sort(out.begin(), out.end(), [](const ToolSlot& a, const ToolSlot& b) { return a.id < b.id; });
    return out;
}

void WorldSession::activate(const std::string& id) {
    auto node = model->find(id);
    if (!node || node->destroyed) return;
    for (const char* event : {"MouseButton1Down", "MouseButton1Up", "MouseButton1Click", "Activated"}) engine->fire(node, event, {}, ScriptContext::client);
}

void WorldSession::commit(const std::string& id, const std::string& text) {
    auto node = model->find(id);
    if (!node || node->className != "TextBox") return;
    model->set(node, "Text", Variant::string(text));
    engine->fire(node, "FocusLost", {Variant::boolean(true)});
}

void WorldSession::scroll(const std::string& id, double amount) {
    auto node = model->find(id);
    if (!node || node->className != "ScrollingFrame") return;
    const PropertyValue* enabled = node->property("ScrollingEnabled");
    if (enabled && enabled->kind == PropertyValue::Kind::boolean && !enabled->flag) return;
    const PropertyValue* current = node->property("CanvasPosition");
    double x = 0, y = 0;
    if (current && current->kind == PropertyValue::Kind::vector2) {
        x = current->numbers[0];
        y = current->numbers[1];
    }
    model->set(node, "CanvasPosition", Variant::vector2(x, std::max(0.0, y + amount)));
}

void WorldSession::equip(int slot) {
    auto backpack = container("Backpack");
    if (!characterNode || !backpack || !humanoidNode || died) return;
    auto ordered = tools();
    if (slot < 0 || slot >= static_cast<int>(ordered.size())) return;
    auto tool = model->find(ordered[static_cast<std::size_t>(slot)].id);
    if (!tool) return;
    auto held = characterNode->children;
    for (const auto& h : held) {
        if (h->className != "Tool") continue;
        model->setParent(h, backpack);
        engine->fire(h, "Unequipped", {});
    }
    if (ordered[static_cast<std::size_t>(slot)].equipped) return;
    model->setParent(tool, characterNode);
    engine->fire(tool, "Equipped", {Variant::object(humanoidNode)});
}

void WorldSession::activateTool() {
    if (!characterNode) return;
    for (const auto& c : characterNode->children) {
        if (c->className != "Tool") continue;
        const PropertyValue* enabled = c->property("Enabled");
        if (enabled && enabled->kind == PropertyValue::Kind::boolean && !enabled->flag) return;
        engine->fire(c, "Activated", {});
        return;
    }
}

void WorldSession::sound(const InstancePtr& node, bool playing) {
    auto output = audio::output();
    if (!playing) {
        if (sounds.erase(node->id) && output) output->stop(node->id);
        return;
    }
    auto number = [&](const char* name, double standard) {
        const PropertyValue* v = node->property(name);
        return v && v->kind == PropertyValue::Kind::number && std::isfinite(v->numbers[0]) ? v->numbers[0] : standard;
    };
    const PropertyValue* source = node->property("SoundId");
    auto asset = source ? asset_cache::content(*source) : std::nullopt;
    auto file = asset ? asset_cache::verified(*asset) : std::nullopt;
    if (!file) {
        if (sounds.erase(node->id) && output) output->stop(node->id);
        node->properties["Playing"] = PropertyValue::boolean(false);
        OutputLine line;
        line.text = tr("Sound «{0}»: risorsa audio mancante, importala dalla proprietà SoundId", node->name);
        line.kind = OutputLine::Kind::warning;
        engine->emit(line);
        return;
    }
    const PropertyValue* looped = node->property("Looped");
    audio::Playback playback{*file, std::clamp(number("Volume", 0.5), 0.0, 10.0), std::clamp(number("PlaybackSpeed", 1), 0.05, 20.0), looped && looped->kind == PropertyValue::Kind::boolean && looped->flag};
    auto known = sounds.find(node->id);
    if (known != sounds.end() && known->second == playback) return;
    bool restart = known == sounds.end() || known->second.file != playback.file;
    sounds[node->id] = playback;
    if (!output) return;
    if (restart) output->play(node->id, playback);
    else output->update(node->id, playback);
}

void WorldSession::silence() {
    auto output = audio::output();
    for (const auto& [id, playback] : sounds)
        if (output) output->stop(id);
    sounds.clear();
}

void WorldSession::sit(const InstancePtr& seat) {
    seated = seat;
    seat->properties["Occupant"] = PropertyValue::reference(humanoidNode->id);
    model->touch();
    engine->fire(seat, "Changed", {Variant::string("Occupant")});
    auto root = characterNode->child("HumanoidRootPart");
    auto kept = placed;
    double yaw = root && root->block ? root->block->rotationY * pi / 180 : 0;
    place(seat->block->x, seat->block->y + seat->block->halfExtents().y, seat->block->z, yaw);
    placed = kept;
}

void WorldSession::stand() {
    if (!seated) return;
    if (!seated->destroyed) {
        seated->properties["Occupant"] = PropertyValue::reference(std::nullopt);
        model->touch();
        engine->fire(seated, "Changed", {Variant::string("Occupant")});
    }
    seated = nullptr;
    seatReady = clock + rules::locomotion::seatCooldownSeconds;
}

void WorldSession::advance(double delta) {
    clock += delta;
    if (auto output = audio::output()) {
        for (auto it = sounds.begin(); it != sounds.end();) {
            auto node = model->find(it->first);
            bool gone = !node || node->destroyed;
            if (gone) output->stop(it->first);
            if (gone || !output->playing(it->first)) {
                if (node && !node->destroyed) node->properties["Playing"] = PropertyValue::boolean(false);
                it = sounds.erase(it);
            } else ++it;
        }
    }
    flush();
    engine->step(delta);
    if (physics) simulate(delta);
    touch();
    observeDeath();
    flush();
}

void WorldSession::stop() {
    if (engine && playerNode && !departed) {
        departed = true;
        if (auto players = model->service("Players")) engine->fire(players, "PlayerRemoving", {Variant::object(playerNode)});
    }
    silence();
    if (engine) engine->stop();
    pending.clear();
}

void WorldSession::discover(const InstancePtr& node) {
    std::vector<InstancePtr> candidates{node};
    auto inner = node->descendants();
    candidates.insert(candidates.end(), inner.begin(), inner.end());
    for (const auto& candidate : candidates)
        if (!launched.count(candidate->id) && context(candidate)) pending.push_back(candidate);
}

void WorldSession::flush() {
    while (!pending.empty()) {
        auto batch = std::move(pending);
        pending.clear();
        for (const auto& script : batch) {
            if (launched.count(script->id) || script->destroyed) continue;
            auto c = context(script);
            if (!c) continue;
            launched.insert(script->id);
            engine->launch(script, *c);
        }
    }
}

std::optional<ScriptContext> WorldSession::context(const InstancePtr& node) const {
    if (node->destroyed || disabled(node)) return std::nullopt;
    auto inside = [&](const InstancePtr& ancestor) { return ancestor && node->isDescendantOf(*ancestor); };
    bool personal = playerNode && node->isDescendantOf(*playerNode);
    if (node->className == "Script") {
        if (inside(model->workspace()) || inside(model->service("ServerScriptService")) || personal) return ScriptContext::server;
        return std::nullopt;
    }
    if (node->className == "LocalScript") {
        if (mode != Mode::play) return std::nullopt;
        if (personal || inside(characterNode) || inside(model->service("ReplicatedFirst"))) return ScriptContext::client;
    }
    return std::nullopt;
}

bool WorldSession::isCharacter(const InstancePtr& node) const { return characterNode && node->isDescendantOf(*characterNode); }

void WorldSession::simulate(double delta) {
    std::vector<InstancePtr> fallen;
    std::vector<InstancePtr> nodes;
    for (const auto& node : model->workspace()->descendants())
        if (node->block && node->className != "Model" && !isCharacter(node)) nodes.push_back(node);
    std::vector<const WorldBlock*> solids;
    for (const auto& node : nodes) solids.push_back(&*node->block);
    for (const auto& node : nodes) {
        if (node->block->anchored) continue;
        physics::Body body = physics::integrate({*node->block, node->velocity}, solids, terrain, delta);
        node->velocity = body.velocity;
        if (!(body.block == *node->block)) {
            *node->block = body.block;
            model->touch();
        }
        if (physics::fallen(body.block)) fallen.push_back(node);
    }
    for (const auto& node : fallen) {
        OutputLine line;
        line.text = tr("«{0}» è caduto nel vuoto ed è stato rimosso", node->name);
        engine->emit(line);
        model->destroy(node);
    }
}

void WorldSession::touch() {
    if (!characterNode || !placed || died) {
        contacts.clear();
        stand();
        return;
    }
    std::set<std::string> current;
    physics::Capsule body{{placed->x, placed->y, placed->z}, motion::radius, motion::height};
    auto descendants = model->workspace()->descendants();
    for (const auto& node : descendants) {
        if (!node->block || node->className == "Model" || node->isDescendantOf(*characterNode)) continue;
        auto contact = physics::touching(*node->block, body);
        if (!contact) continue;
        current.insert(node->id);
        if (contacts.count(node->id)) continue;
        auto limb = characterNode->child(contact->feet ? "Left Leg" : "Torso");
        if (!limb) limb = characterNode;
        engine->fire(node, "Touched", {Variant::object(limb)});
        engine->fire(limb, "Touched", {Variant::object(node)});
        if (node->block->kind == "seat" && contact->feet && !seated && humanoidNode && clock >= seatReady && !disabled(node) && !node->destroyed) sit(node);
    }
    if (seated && (seated->destroyed || !current.count(seated->id) || disabled(seated))) stand();
    for (const auto& id : contacts) {
        if (current.count(id)) continue;
        auto node = model->find(id);
        if (!node) continue;
        auto torso = characterNode->child("Torso");
        engine->fire(node, "TouchEnded", {Variant::object(torso ? torso : characterNode)});
    }
    contacts = current;
}

}
