#pragma once

#include "Engine/Client/Store.hpp"
#include "Golden.hpp"

#include <map>
#include <string>
#include <vector>

namespace sample {

inline const ld::Json& fixture() {
    static const ld::Json data = ld::Json::parse(golden::readText(golden::root() / "Tests/Emulator/Sample.json")).valueOr(ld::Json::object());
    return data;
}

inline const std::string player = fixture()["player"]["name"].string();

struct Game {
    std::string id;
    std::string title;
    std::string genre;
    std::string author;
    std::string description;
};

inline const std::vector<Game>& games() {
    static const std::vector<Game> list = [] {
        std::vector<Game> out;
        for (const auto& g : fixture()["games"].items()) out.push_back({g["id"].string(), g["title"].string(), g["genre"].string(), g["author"].string(), g["description"].string()});
        return out;
    }();
    return list;
}

inline std::vector<ld::Friend> friends() {
    std::vector<ld::Friend> out;
    for (const auto& f : fixture()["friends"].items()) out.push_back({f["id"].string(), f["name"].string(), f["online"].boolean(), std::nullopt, f["gender"].string()});
    return out;
}

inline ld::Experience own() {
    const Game& game = games().front();
    ld::Experience world;
    world.id = game.id;
    world.title = game.title;
    world.author = game.author;
    world.genre = game.genre;
    return world;
}

inline void populate(ld::Store& store) {
    const ld::Json& me = fixture()["player"];
    store.profile = ld::PlayerProfile{player, me["gender"].string(), me["createdAt"].string()};
    store.avatar = ld::AvatarConfiguration::standard(player, me["gender"].string());
    for (const auto& [part, color] : me["bodyColors"].members()) store.avatar->bodyColors[part] = color.string();
    store.avatarDraft = store.avatar;
    store.setWorlds({own()});
    std::vector<ld::WorldSummary> discover;
    std::map<std::string, ld::ExperienceMetadata> metadata;
    for (const Game& game : games()) {
        discover.push_back(ld::WorldSummary{game.id, game.title, game.author == player ? "test" : game.author, game.author, game.genre, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt});
        metadata[game.id] = ld::ExperienceMetadata{game.description, game.author == player, static_cast<int>(fixture()["weeklyPlays"].number())};
    }
    store.setDiscover(discover);
    store.setExperienceMetadata(metadata);
}

}
