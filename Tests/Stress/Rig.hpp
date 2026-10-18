#pragma once

#include "Engine/Backend/LocalServer.hpp"
#include "Engine/Client/Store.hpp"
#include "Engine/Core/Async.hpp"
#include "Engine/Core/Resources.hpp"
#include "Engine/DataModel/ClassRegistry.hpp"
#include "Engine/Studio/StudioDocument.hpp"
#include "Stress.hpp"

#include <Catch2/catch_amalgamated.hpp>

namespace stress {

inline void prepare() {
    ld::setResourceFolder(root() / "Resources");
    if (!ld::ClassRegistry::hasShared()) {
        auto loaded = ld::ClassRegistry::load(root() / "HistoricalAPI/2017.json", root() / "Resources/class-rules.json");
        REQUIRE(loaded.ok());
        ld::ClassRegistry::setShared(*loaded);
    }
}

inline ld::Experience project(const std::string& title = "Progetto di stress") {
    ld::Experience e = ld::Experience::blank();
    e.title = title;
    return e;
}

inline std::shared_ptr<ld::Store> account(const std::filesystem::path& folder) {
    auto server = std::make_shared<ld::LocalServer>(folder);
    ld::PlayerProfile profile{"Stress", "male", "2026-10-02T08:00:00Z"};
    server->enrollNow(ld::PlayerId("Pstress"), profile);
    auto store = std::make_shared<ld::Store>(std::make_shared<ld::PlayFab>(std::make_shared<ld::MemoryStore>()), std::make_shared<ld::LocalBackend>(server, ld::PlayerId("Pstress")), nullptr, false);
    store->profile = profile;
    return store;
}

}
