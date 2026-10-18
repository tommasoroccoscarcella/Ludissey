#include "PlayerScene.hpp"

namespace ld::render {

PlayerScene::PlayerScene(const Experience& experience, const AvatarConfiguration& look, std::shared_ptr<PlayerGameplay> state, decltype(ScriptEngine::storage) storage)
    : gameplay(std::move(state)), world(worldScene(experience)), blocks(experience.workspaceParts()) {
    gameplay->loaded = false;
    gameplay->gui.clear();
    gameplay->tools.clear();
    for (const auto& block : blocks) structures[block.id] = scene_factory::structure(block);
    eye = Node::make("player.camera");
    eye->camera = Camera{};
    eye->camera->zFar = 400;
    eye->camera->zNear = 0.05;
    world->scene.root->add(eye);
    body = scene_factory::avatar(look);
    if (body) {
        body->node->add(name_tag::make(look.displayName));
        world->scene.root->add(body->node);
    }
    std::weak_ptr<PlayerGameplay> weakGameplay = gameplay;
    session = std::make_shared<WorldSession>(experience, WorldSession::Mode::play, look.displayName, 0, false, [weakGameplay](const OutputLine& line) { if (auto g = weakGameplay.lock()) g->report(line); });
    session->engine->storage = std::move(storage);
    driver.reset(motion::arrival(blocks, session->terrain, session->teamColor()));
    std::weak_ptr<WorldSession> weakSession = session;
    gameplay->activate = [weakSession](const std::string& id) { if (auto s = weakSession.lock()) s->activate(id); };
    gameplay->commit = [weakSession](const std::string& id, const std::string& value) { if (auto s = weakSession.lock()) s->commit(id, value); };
    gameplay->scroll = [weakSession](const std::string& id, double amount) { if (auto s = weakSession.lock()) s->scroll(id, amount); };
    gameplay->equip = [weakSession](int slot) { if (auto s = weakSession.lock()) s->equip(slot); };
    gameplay->dead = false;
    gameplay->health = PlayerGameplay::maximumHealth;
    session->start(driver.state().position());
    synchronize();
    if (body) body->node->position = driver.state().position();
    frame(0);
    gameplay->loaded = true;
}

PlayerScene::~PlayerScene() { stop(); }

void PlayerScene::stop() {
    if (stopped) return;
    stopped = true;
    release();
    session->stop();
    gameplay->activate = nullptr;
    gameplay->commit = nullptr;
    gameplay->scroll = nullptr;
    gameplay->equip = nullptr;
    gameplay->loaded = false;
    gameplay->setLocal(std::nullopt);
}

void PlayerScene::release() {
    for (auto code : held) session->key(code, false);
    held.clear();
    jumpRequested = false;
}

void PlayerScene::key(std::uint16_t code, bool down, bool repeat) {
    if (stopped) return;
    if (!down) { held.erase(code); session->key(code, false); return; }
    bool menu = gameplay->menuOpen, chat = gameplay->chatFocused, open = gameplay->chatOpen, leaving = gameplay->leaveRequested, resetting = gameplay->resetRequested;
    if (code == 53) { gameplay->menuOpen = !gameplay->menuOpen; release(); }
    else if (gameplay->menuOpen) {
        if (code == 15) gameplay->reset();
        if (code == 37) gameplay->leaveRequested = true;
    } else if (code == 44) {
        gameplay->chatOpen = true;
        gameplay->chatFocused = true;
        release();
    }
    else if (!gameplay->suspended()) {
        held.insert(code);
        if (!repeat) {
            session->key(code, true);
            static constexpr std::array<std::uint16_t, 9> slots{18, 19, 20, 21, 23, 22, 26, 28, 25};
            auto found = std::find(slots.begin(), slots.end(), code);
            if (found != slots.end()) session->equip(static_cast<int>(found - slots.begin()));
        }
        if (code == 49) jumpRequested = true;
    }
    if (changed && (menu != gameplay->menuOpen || chat != gameplay->chatFocused || open != gameplay->chatOpen || leaving != gameplay->leaveRequested || resetting != gameplay->resetRequested)) changed();
}

void PlayerScene::activateTool() { if (!stopped && !gameplay->suspended()) session->activateTool(); }

void PlayerScene::look(double dx, double dy) {
    if (stopped || gameplay->suspended()) return;
    auto angles = camera_input::look(yaw, pitch, dx, dy, gameplay->sensitivity, camera_input::characterPitchLow, camera_input::characterPitchHigh);
    yaw = angles.x;
    pitch = angles.y;
}

void PlayerScene::zoom(double amount, bool precise) {
    if (!stopped && !gameplay->suspended()) distance = camera_rig::zoom(distance, amount * (precise ? .01 : .08));
}

void PlayerScene::die(double now) {
    if (gameplay->dead) return;
    gameplay->health = 0;
    gameplay->dead = true;
    ++gameplay->deaths;
    if (body) body->node->hidden = true;
    release();
    session->kill();
    respawnAt = now + rules::respawnSeconds;
    if (changed) changed();
}

void PlayerScene::respawn() {
    driver.reset(motion::arrival(blocks, session->terrain, session->teamColor()));
    session->spawn(driver.state().position());
    gameplay->health = PlayerGameplay::maximumHealth;
    gameplay->dead = false;
    if (body) { body->node->hidden = false; body->node->position = driver.state().position(); }
    if (changed) changed();
}

void PlayerScene::synchronize() {
    if (session->revision() == shown) return;
    shown = session->revision();
    blocks = session->parts();
    std::set<std::string> present;
    for (const auto& block : blocks) {
        present.insert(block.id);
        auto existing = world->parts.find(block.id);
        std::string structure = scene_factory::structure(block);
        if (existing == world->parts.end()) {
            auto node = scene_factory::block(block);
            world->scene.root->add(node);
            world->parts[block.id] = node;
        } else if (structures[block.id] != structure) {
            auto rebuilt = scene_factory::block(block);
            world->scene.root->replace(existing->second, rebuilt);
            existing->second = rebuilt;
        } else scene_factory::apply(block, *existing->second);
        structures[block.id] = std::move(structure);
    }
    for (auto it = world->parts.begin(); it != world->parts.end();) {
        if (!present.count(it->first)) { it->second->remove(); structures.erase(it->first); it = world->parts.erase(it); }
        else ++it;
    }
}

void PlayerScene::remotes(double wallTime, double delta) {
    auto states = gameplay->remotes();
    for (auto it = others.begin(); it != others.end();) {
        if (!states.count(it->first)) { it->second->node->remove(); it = others.erase(it); }
        else ++it;
    }
    for (const auto& [id, remote] : states) {
        auto look = remote.avatar.value_or(AvatarConfiguration::standard(remote.name, std::string(rules::avatar::standard::bodyPreset)));
        auto& character = others[id];
        if (!character || !character->avatar || character->avatar->bodyPreset != look.bodyPreset) {
            if (character) character->node->remove();
            character = scene_factory::avatar(look);
            if (!character) { others.erase(id); continue; }
            character->node->name = "remote." + id.text();
            character->node->hidden = true;
            character->node->add(name_tag::make(remote.name));
            world->scene.root->add(character->node);
        } else if (character->avatar != look) character->dress(look);
        if (auto position = remote.sample(wallTime)) {
            character->node->hidden = false;
            character->node->position = {position->x, position->y, position->z};
            character->node->setYaw(position->yaw);
            character->animate(position->moving ? "Walk" : "Idle", delta);
        }
    }
}

void PlayerScene::tick(double now, double delta, double wallTime) {
    if (stopped) return;
    delta = std::clamp(delta, 0., CharacterDriver::maximumFrame);
    const auto before = driver.state();
    session->place(before.x, before.y, before.z, before.yaw);
    session->advance(delta);
    if (auto point = session->teleport()) {
        auto state = before;
        state.x = point->x; state.y = point->y; state.z = point->z; state.vertical = 0;
        driver.reset(state);
    }
    if (session->revision() != interface) {
        interface = session->revision();
        gameplay->gui = session->gui();
        gameplay->tools.clear();
        for (const auto& tool : session->tools()) gameplay->tools.push_back({tool.id, tool.name, tool.equipped});
        if (changed) changed();
    }
    if (!gameplay->dead) {
        double maximum = session->maximumHealth();
        double health = maximum > 0 ? session->health() / maximum * PlayerGameplay::maximumHealth : 0;
        if (gameplay->health != health) { gameplay->health = health; if (changed) changed(); }
        if (session->dead()) die(now);
    }
    synchronize();
    world->terrain.sync(session->terrain);
    world->scene.time = now;
    remotes(wallTime, delta);
    if (gameplay->resetRequested) { gameplay->resetRequested = false; die(now); }
    if (gameplay->dead && now >= respawnAt) respawn();
    if (!gameplay->dead) {
        CharacterInput input;
        if (!gameplay->suspended()) {
            if (held.count(0) || held.count(123)) input.strafe -= 1;
            if (held.count(2) || held.count(124)) input.strafe += 1;
            if (held.count(13) || held.count(126)) input.forward += 1;
            if (held.count(1) || held.count(125)) input.forward -= 1;
            input.ascend = held.count(49);
        } else release();
        input.pace = session->speedFactor();
        input.leap = session->jumpFactor();
        bool fell = false;
        driver.advance(delta, [&](const CharacterState& state, double step) -> std::optional<CharacterState> {
            auto slice = input;
            slice.jump = jumpRequested && !gameplay->suspended();
            jumpRequested = false;
            auto next = motion::step(state, slice, yaw, blocks, session->terrain, step);
            if (motion::fallen(next)) { fell = true; return std::nullopt; }
            return next;
        });
        if (fell) die(now);
        else if (body) {
            auto pose = driver.pose(delta);
            body->node->position = {pose.x, pose.y, pose.z};
            body->node->setYaw(pose.yaw);
            const auto& motion = driver.state();
            if (!motion.grounded && !motion.swimming) {
                double phase = motion.vertical >= 0 ? 1 - motion.vertical / ld::motion::jumpSpeed : 1 + motion.vertical / ld::motion::jumpSpeed;
                body->animate("Jump", delta, std::clamp(phase, 0., 1.));
            } else body->animate(motion.moving ? "Walk" : "Idle", delta);
        }
    }
    const auto& state = driver.state();
    gameplay->setLocal(AvatarSnapshot{state.x, state.y, state.z, state.yaw, state.moving, wallTime});
    frame(delta);
}

void PlayerScene::frame(double delta) {
    Vec3 base = body ? body->node->position : driver.state().position();
    Vec3 head = driver.follow({base.x, std::max(base.y, motion::voidHeight) + .9, base.z}, delta);
    auto placed = camera_rig::place(head, yaw, pitch, distance, blocks, session->terrain);
    if (body && !gameplay->dead) body->node->hidden = placed.firstPerson;
    eye->position = placed.position;
    auto aim = placed.firstPerson ? world_space::forward(yaw, pitch) : head - placed.position;
    eye->look(placed.position + aim);
    std::vector<NodePtr> characters;
    if (body) characters.push_back(body->node);
    for (const auto& [id, remote] : others) characters.push_back(remote->node);
    name_tag::fit(characters, placed.position);
}

}
