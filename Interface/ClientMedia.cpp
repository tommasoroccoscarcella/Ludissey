#include "ClientMedia.hpp"

#include "Engine/Backend/Assets.hpp"
#include "Engine/Core/Png.hpp"
#include "Engine/Core/Text.hpp"
#include "Engine/Studio/StudioDocument.hpp"
#include "Interface/Tokens.hpp"

namespace ui {

namespace {

ld::Async<void> fetch(std::shared_ptr<ld::Store> store, ld::Experience stub) {
    try {
        co_await ld::AssetService(store->backend).resolve(std::move(stub));
    } catch (const std::exception&) {
    }
    if (store->changed) store->changed();
}

void stage(ld::render::Scene& scene) {
    ld::render::scene_factory::base(scene);
    scene.background = ld::render::scene_factory::sky(ld::WorldLighting{});
    scene.root->add(ld::render::scene_factory::block(ld::Experience::baseplate("avatar")));
}

ld::render::Image pose(const ld::AvatarConfiguration& look, ld::Vec3 from, ld::Vec3 target, int side) {
    ld::render::Scene scene;
    ld::render::scene_factory::base(scene);
    auto character = ld::render::scene_factory::avatar(look);
    if (!character) return {};
    scene.root->add(character->node);
    auto camera = ld::render::Node::make("portrait.camera");
    camera->camera = ld::render::Camera{};
    camera->position = from;
    camera->look(target);
    scene.root->add(camera);
    return ld::render::Renderer::shared().snapshot(scene, *camera, side, side, 4, false);
}

const ld::ExperienceEntry* entryFor(const ld::Store& store, const std::string& id) {
    const auto& catalog = store.experienceCatalog();
    auto found = std::find_if(catalog.begin(), catalog.end(), [&](const ld::ExperienceEntry& e) { return e.id == id; });
    return found == catalog.end() ? nullptr : &*found;
}

}

Image ClientMedia::experienceImage(const std::string& experience, const std::string& asset) {
    if (!ld::isUuid(experience) || !ld::isImageAsset(asset)) return {};
    if (auto known = uploaded.find(asset); known != uploaded.end()) return known->second;
    if (auto file = ld::asset_cache::verified(asset)) {
        Image picture = loadImage(*file);
        if (picture.valid()) uploaded[asset] = picture;
        return picture;
    }
    if (store->backend && fetching->insert(asset).second) {
        ld::Experience stub;
        stub.id = experience;
        stub.thumbnail = asset;
        fetch(store, std::move(stub)).start();
    }
    return {};
}

Image ClientMedia::icon(const std::string& id) {
    const ld::ExperienceEntry* entry = entryFor(*store, id);
    return entry && entry->icon ? experienceImage(id, *entry->icon) : Image{};
}

void ClientMedia::AvatarPreview::positionCamera() {
    camera->position = {distance * std::cos(pitch) * std::sin(yaw), .8 + distance * std::sin(pitch), distance * std::cos(pitch) * std::cos(yaw)};
    camera->look({0, .8, 0});
}

Image ClientMedia::thumbnail(const std::string& id) {
    if (!ld::isUuid(id)) return {};
    if (const ld::ExperienceEntry* entry = entryFor(*store, id); entry && entry->thumbnail)
        if (Image custom = experienceImage(id, *entry->thumbnail); custom.valid()) return custom;
    const ld::Experience* world = nullptr;
    for (const auto& candidate : store->worlds()) if (candidate.id == id) { world = &candidate; break; }
    std::error_code error;
    auto file = ld::studio_assets::preview(id);
    auto modified = std::filesystem::last_write_time(file, error);
    std::string source = error ? "" : std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(modified.time_since_epoch()).count());
    if (world) source += ":" + ld::sha256Hex(world->json().dump());
    auto found = thumbnails.find(id);
    if (found != thumbnails.end() && found->second.source == source) return found->second.image;
    Image picture = error ? Image{} : loadImage(file);
    if (!picture.valid() && world && (!world->workspaceParts().empty() || !world->terrain.empty())) {
        auto scene = ld::render::worldScene(*world);
        auto camera = ld::render::Node::make("thumbnail.camera");
        camera->camera = ld::render::Camera{};
        double extent = 12;
        for (const auto& block : world->workspaceParts()) extent = std::max(extent, std::max(std::abs(block.x) + block.sizeX, std::abs(block.z) + block.sizeZ));
        camera->position = {extent * .8, extent * .65, extent * .8};
        camera->look({0, 0, 0});
        scene->scene.root->add(camera);
        auto rendered = ld::render::Renderer::shared().snapshot(scene->scene, *camera, 480, 270, 4, true);
        if (!rendered.rgba.empty()) picture = imageFromRgba(rendered.rgba, rendered.width, rendered.height);
    }
    if (picture.valid()) thumbnails[id] = {source, picture};
    return picture;
}

Image ClientMedia::itemImage(const ld::AvatarItem& item) {
    if (!item.asset) return {};
    auto cached = itemImages.find(*item.asset);
    if (cached != itemImages.end()) return cached->second;
    auto file = ld::asset_cache::verified(*item.asset);
    if (!file) return {};
    auto picture = loadImage(*file);
    if (picture.valid()) itemImages[*item.asset] = picture;
    return picture;
}

Image ClientMedia::portrait(const std::string& username) {
    if (!store->profile || store->profile->username != username) {
        auto known = store->portraits.find(username);
        if (known == store->portraits.end()) return {};
        if (auto cached = uploaded.find(known->second); cached != uploaded.end()) return cached->second;
        auto file = ld::asset_cache::verified(known->second);
        Image picture = file ? loadImage(*file) : Image{};
        if (picture.valid()) uploaded[known->second] = picture;
        return picture;
    }
    auto look = store->avatar ? store->avatar : store->currentAvatar();
    if (!look) return {};
    std::string source = look->json().dump();
    if (source == portraitSource) return portraitImage;
    auto rendered = pose(*look, {0.35, 1.5, 1.35}, {0, 1.32, 0}, static_cast<int>(tokens::size::tileCompact) * 2);
    portraitSource = source;
    portraitImage = rendered.rgba.empty() ? Image{} : imageFromRgba(rendered.rgba, rendered.width, rendered.height);
    return portraitImage;
}

std::optional<ld::PortraitImages> ClientMedia::portraitImages(const ld::AvatarConfiguration& look) {
    auto bust = pose(look, {0.35, 1.5, 1.35}, {0, 1.32, 0}, ld::rules::account::portraitBust);
    auto figure = pose(look, {1.5, 1.25, 3.6}, {0, 0.9, 0}, ld::rules::account::portraitFigure);
    auto first = ld::encodePng(bust.rgba, bust.width, bust.height), second = ld::encodePng(figure.rgba, figure.width, figure.height);
    if (!first || !second) return std::nullopt;
    return ld::PortraitImages{*first, *second};
}

std::vector<ld::render::View> ClientMedia::views(const std::vector<ViewportSlot>& slots) {
    using namespace ld::render;
    std::set<std::string> active;
    std::vector<View> result;
    for (const auto& slot : slots) {
        if (slot.id != "avatar.preview") continue;
        active.insert(slot.id);
        auto look = store->avatarDraft;
        if (!look) look = store->currentAvatar();
        if (!look) look = ld::AvatarConfiguration::standard("", std::string(ld::rules::avatar::standard::bodyPreset));
        auto& preview = previews[slot.id];
        if (!preview.character || !preview.character->avatar || preview.character->avatar->bodyPreset != look->bodyPreset) {
            preview = AvatarPreview{};
            stage(preview.scene);
            preview.character = scene_factory::avatar(*look);
            if (preview.character) preview.scene.root->add(preview.character->node);
            preview.camera = ld::render::Node::make("avatar.camera");
            preview.camera->camera = Camera{};
            preview.positionCamera();
            preview.scene.root->add(preview.camera);
        } else if (preview.character->avatar != look) preview.character->dress(*look);
        result.push_back({slot.id, &preview.scene, preview.camera.get(), slot.frame.x, slot.frame.y, slot.frame.width, slot.frame.height, {4, true, true, 2}});
    }
    for (auto it = previews.begin(); it != previews.end();) {
        if (!active.count(it->first)) {
            Renderer::shared().forget(it->first);
            it = previews.erase(it);
        } else ++it;
    }
    return result;
}

void ClientMedia::tick(double delta) {
    for (auto& [id, preview] : previews) if (preview.character) preview.character->animate("Idle", std::clamp(delta, 0., .05));
}

void ClientMedia::orbit(const std::string& slot, double dx, double dy) {
    auto found = previews.find(slot);
    if (found == previews.end()) return;
    auto& preview = found->second;
    preview.yaw -= dx * .01;
    preview.pitch = std::clamp(preview.pitch + dy * .01, ld::rules::avatar::view::pitchMinimum, ld::rules::avatar::view::pitchMaximum);
    preview.positionCamera();
}

void ClientMedia::zoom(const std::string& slot, double delta) {
    auto found = previews.find(slot);
    if (found == previews.end()) return;
    auto& preview = found->second;
    preview.distance = std::clamp(preview.distance * std::exp(delta * .03), ld::rules::avatar::view::distanceMinimum, static_cast<double>(ld::rules::avatar::view::distanceMaximum));
    preview.positionCamera();
}

Image ClientMedia::figure(const std::string& id) {
    std::optional<ld::AvatarConfiguration> look;
    auto page = store->profiles.find(id);
    if (page != store->profiles.end() && page->second.self) look = store->avatar ? store->avatar : store->currentAvatar();
    else if (auto known = store->profileLooks.find(id); known != store->profileLooks.end()) look = known->second;
    else if (page != store->profiles.end()) look = ld::AvatarConfiguration::standard(page->second.username, page->second.gender);
    if (!look) return {};
    std::string source = look->json().dump();
    if (auto cached = figures.find(id); cached != figures.end() && cached->second.source == source) return cached->second.image;
    ld::render::Scene scene;
    stage(scene);
    auto character = ld::render::scene_factory::avatar(*look);
    if (!character) return {};
    scene.root->add(character->node);
    auto camera = ld::render::Node::make("figure.camera");
    camera->camera = ld::render::Camera{};
    camera->position = {1.05, 1.2, 2.55};
    camera->look({0, 0.86, 0});
    scene.root->add(camera);
    int scale = 2;
    auto rendered = ld::render::Renderer::shared().snapshot(scene, *camera, static_cast<int>(tokens::size::profileFigureWidth) * scale, static_cast<int>(tokens::size::profileFigureHeight) * scale, 4, true);
    Image picture = rendered.rgba.empty() ? Image{} : imageFromRgba(rendered.rgba, rendered.width, rendered.height);
    figures[id] = Thumbnail{source, picture};
    return picture;
}

void ClientMedia::clear() {
    for (const auto& [id, preview] : previews) ld::render::Renderer::shared().forget(id);
    previews.clear(); thumbnails.clear(); itemImages.clear(); figures.clear();
    portraitSource.clear();
    portraitImage = {};
}

}
